#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkSceneNodeGym_EcsRoots.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor_Data.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkLabel/CkLabel_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"
#include <cfloat>

namespace ck_existing_anchor_test
{
    struct FState
    {
        FCk_Handle Parent;
        FCk_Handle Child;
        FCk_Handle Carrier;
        FCk_Handle NamedChild;
        FCk_Handle NamedCarrier;
        TWeakObjectPtr<AActor> Actor;
        TWeakObjectPtr<AActor> NamedActor;
        TWeakObjectPtr<AActor> AnchorActor;
        TWeakObjectPtr<USceneComponent> Anchor;
        FGameplayTag OriginalLabel;
        FGameplayTag NamedLabel;
        FTransform Offset = FTransform(FQuat::Identity, FVector(25.0, 0.0, 0.0),
            FVector(0.5, 0.5, 0.5));
        FDelegateHandle LateTick;
        bool Composed = false;
        bool Moved = false;
        bool Rejected = false;
        ~FState() { if (LateTick.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(LateTick); } }
    };
    struct FNativeData
    {
        FInstancedStruct Location;
        FInstancedStruct Rotation;
        FInstancedStruct Scale;
    };
    bool CopyNativeData(const FCk_Handle& Entity, FNativeData& Out)
    {
        if (!ck::IsValid(Entity) ||
            !Entity.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
        { return false; }
        const TWeakObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep> Driver =
            Entity.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        if (!Driver.IsValid() || Driver->Get_AssociatedEntity() != Entity)
        { return false; }
        if (!Driver->FindEntry(FCk_RepData_Location::StaticStruct()) ||
            !Driver->FindEntry(FCk_RepData_Rotation::StaticStruct()) ||
            !Driver->FindEntry(FCk_RepData_Scale::StaticStruct()))
        { return false; }
        const FInstancedStruct Location =
            Driver->FindEntry(FCk_RepData_Location::StaticStruct())->Data;
        const FInstancedStruct Rotation =
            Driver->FindEntry(FCk_RepData_Rotation::StaticStruct())->Data;
        const FInstancedStruct Scale =
            Driver->FindEntry(FCk_RepData_Scale::StaticStruct())->Data;
        if (Location.GetScriptStruct() != FCk_RepData_Location::StaticStruct() ||
            Rotation.GetScriptStruct() != FCk_RepData_Rotation::StaticStruct() ||
            Scale.GetScriptStruct() != FCk_RepData_Scale::StaticStruct())
        { return false; }
        Out.Location = Location; Out.Rotation = Rotation; Out.Scale = Scale;
        return true;
    }
    bool NativeDataMatchesPose(const FCk_Handle& Entity, const FTransform& Expected)
    {
        FNativeData Actual;
        return CopyNativeData(Entity, Actual) &&
            Actual.Location.Get<FCk_RepData_Location>().Value.Equals(Expected.GetLocation(), 0.0) &&
            Actual.Rotation.Get<FCk_RepData_Rotation>().Value.Equals(Expected.GetRotation(), 0.0) &&
            Actual.Scale.Get<FCk_RepData_Scale>().Value.Equals(Expected.GetScale3D(), 0.0);
    }
    bool NativeDataUnchanged(const FCk_Handle& Entity, const FNativeData& Before)
    {
        FNativeData After;
        return CopyNativeData(Entity, After) &&
            After.Location.Get<FCk_RepData_Location>().Value.Equals(
                Before.Location.Get<FCk_RepData_Location>().Value, 0.0) &&
            After.Rotation.Get<FCk_RepData_Rotation>().Value.Equals(
                Before.Rotation.Get<FCk_RepData_Rotation>().Value, 0.0) &&
            After.Scale.Get<FCk_RepData_Scale>().Value.Equals(
                Before.Scale.Get<FCk_RepData_Scale>().Value, 0.0);
    }
    bool Constructed(const TSharedPtr<FState>& S)
    {
        return ck::IsValid(S->Child) && !ck::IsValid(S->Carrier, ck::IsValid_Policy_IncludePendingKill{}) &&
            S->Child.Has_All<ck::FFragment_Transform, ck::FFragment_Transform_Previous,
                ck::FFragment_Transform_RootComponent, ck::FTag_Transform_Movable,
                ck::FTag_EntityScript_HasBegunPlay>() && S->Actor.IsValid() &&
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(S->Actor.Get()) == S->Child;
    }
    bool OutputMatches(const TSharedPtr<FState>& S)
    {
        if (!S->Composed || !Constructed(S) || !S->Anchor.IsValid()) { return false; }
        const auto Expected = S->Offset * S->Anchor->GetComponentTransform();
        const auto Current = S->Child.Get<ck::FFragment_Transform>().Get_Transform();
        return Current.Equals(Expected) && S->Actor->GetRootComponent()->GetComponentTransform().Equals(Current) &&
            NativeDataMatchesPose(S->Child, Current);
    }
    bool NamedConstructed(const TSharedPtr<FState>& S)
    {
        return ck::IsValid(S->NamedChild) &&
            !ck::IsValid(S->NamedCarrier, ck::IsValid_Policy_IncludePendingKill{}) &&
            S->NamedChild.Has_All<ck::FFragment_Transform, ck::FFragment_Transform_Previous,
                ck::FFragment_Transform_RootComponent, ck::FTag_Transform_Movable,
                ck::FTag_EntityScript_HasBegunPlay>() && S->NamedActor.IsValid() &&
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(S->NamedActor.Get()) == S->NamedChild &&
            UCk_Utils_GameplayLabel_UE::Has(S->NamedChild) &&
            UCk_Utils_GameplayLabel_UE::Get_Label(S->NamedChild) == S->NamedLabel;
    }
}
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeExistingTransformAnchor,
    "Ck.SceneNode.Network.ExistingTransform.ForeignAnchor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeExistingTransformAnchor::RunTest(const FString& Parameters)
{
    using namespace ck_existing_anchor_test;
    const auto S = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* W)
    {
        const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
        S->Parent = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldRoot);
        UCk_Utils_Transform_UE::Add(S->Parent, FTransform(FVector(500.0, 0.0, 0.0)), ECk_Replication::DoesNotReplicate);
        auto* Class = FSoftClassPath(TEXT("/Script/Angelscript.Ck_SceneNodeGym_SourceChild")).TryLoadClass<AActor>();
        auto* Actor = IsValid(Class) ? W->SpawnActor<AActor>(Class) : nullptr;
        auto* AnchorActor = W->SpawnActor<AActor>();
        auto* Anchor = IsValid(AnchorActor) ? NewObject<USceneComponent>(AnchorActor) : nullptr;
        if (!TestTrue(TEXT("real independent anchor and replicated output actor"),
            IsValid(Actor) && IsValid(AnchorActor) && IsValid(Anchor))) { return; }
        AnchorActor->SetRootComponent(Anchor);
        Anchor->SetMobility(EComponentMobility::Movable);
        Anchor->RegisterComponentWithWorld(W);
        Anchor->SetWorldLocation(FVector(0.0, 200.0, 0.0));
        S->Actor = Actor; S->AnchorActor = AnchorActor; S->Anchor = Anchor;
        S->Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->Parent);
        if (S->Child.Has<ck::FFragment_Net_Params>()) { S->Child.Remove<ck::FFragment_Net_Params>(); }
        if (S->Child.Has<ck::FTag_HasAuthority>()) { S->Child.Remove<ck::FTag_HasAuthority>(); }
        if (S->Child.Has<ck::FTag_NetMode_IsHost>()) { S->Child.Remove<ck::FTag_NetMode_IsHost>(); }
        if (S->Child.Has<ck::FTag_NetMode_IsClient>()) { S->Child.Remove<ck::FTag_NetMode_IsClient>(); }
        UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(S->Child, {});
        UCk_SceneNodeGym_WithActorScript_UE::StaticClass()->GetDefaultObject();
        const auto Capture = UCk_Utils_EntityScript_UE::TryAddCaptured(S->Child,
            UCk_SceneNodeGym_WithActorScript_UE::StaticClass(),
            FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Actor));
        S->Carrier = Capture.RequestCarrier;
        TestTrue(TEXT("captured real production WithActor construction"),
            Capture.bAllocationAttempted && ck::IsValid(Capture.Pending) && S->Carrier != FCk_Handle{});
        S->NamedLabel = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Ck.AttributeModifier.Override")), false);
        if (!TestTrue(TEXT("registered named label available"), S->NamedLabel.IsValid())) { return; }
        TObjectPtr<AActor> NamedActor = W->SpawnActor<AActor>(Class);
        if (!TestTrue(TEXT("separate real named-label output actor"), IsValid(NamedActor.Get()))) { return; }
        S->NamedActor = NamedActor.Get();
        S->NamedChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->Parent);
        if (S->NamedChild.Has<ck::FFragment_Net_Params>()) { S->NamedChild.Remove<ck::FFragment_Net_Params>(); }
        if (S->NamedChild.Has<ck::FTag_HasAuthority>()) { S->NamedChild.Remove<ck::FTag_HasAuthority>(); }
        if (S->NamedChild.Has<ck::FTag_NetMode_IsHost>()) { S->NamedChild.Remove<ck::FTag_NetMode_IsHost>(); }
        if (S->NamedChild.Has<ck::FTag_NetMode_IsClient>()) { S->NamedChild.Remove<ck::FTag_NetMode_IsClient>(); }
        UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(S->NamedChild, {});
        UCk_Utils_GameplayLabel_UE::Add(S->NamedChild, S->NamedLabel);
        const auto NamedCapture = UCk_Utils_EntityScript_UE::TryAddCaptured(S->NamedChild,
            UCk_SceneNodeGym_WithActorScript_UE::StaticClass(),
            FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(NamedActor.Get()));
        S->NamedCarrier = NamedCapture.RequestCarrier;
        TestTrue(TEXT("captured named production WithActor construction"),
            NamedCapture.bAllocationAttempted && ck::IsValid(NamedCapture.Pending) &&
            S->NamedCarrier != FCk_Handle{});
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([S]() { return Constructed(S) && NamedConstructed(S); }), 20.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* W)
    {
        if (!TestTrue(TEXT("real actor bridge and deallocated construction carrier"), Constructed(S)) ||
            !TestTrue(TEXT("named actor bridge and deallocated construction carrier"), NamedConstructed(S)))
        { return; }
        if (!TestTrue(TEXT("production child has its label fragment"),
            UCk_Utils_GameplayLabel_UE::Has(S->Child))) { return; }
        S->OriginalLabel = UCk_Utils_GameplayLabel_UE::Get_Label(S->Child);
        TestTrue(TEXT("production child begins with its own unnamed label"),
            UCk_Utils_GameplayLabel_UE::Has(S->Child) &&
            UCk_Utils_GameplayLabel_UE::Get_IsUnnamedLabel(S->Child) && S->OriginalLabel.IsValid());
        auto Child = UCk_Utils_Transform_UE::Cast(S->Child);
        auto Parent = UCk_Utils_Transform_UE::Cast(S->Parent);
        const auto Before = S->Child.Get<ck::FFragment_Transform>().Get_Transform();
        const auto Previous = S->Child.Get<ck::FFragment_Transform_Previous>().Get_Transform();
        auto* Root = S->Actor->GetRootComponent();
        const auto VisibleBefore = Root->GetComponentTransform();
        FNativeData NativeBefore;
        if (!TestTrue(TEXT("production child has complete native LRS data"),
            CopyNativeData(S->Child, NativeBefore))) { return; }
        auto Reject = [&](USceneComponent* Component, const FTransform& Offset)
        {
            FCk_Handle_SceneNode Out;
            const bool Result = UCk_Utils_SceneNode_UE::TryAdd_AnchorToExistingTransform(
                Child, Parent, Component, Offset, Out);
            return !Result && Out == FCk_Handle_SceneNode{} &&
                !S->Child.Has_Any<ck::FFragment_SceneNode, ck::SceneNodeParent,
                    ck::FFragment_SceneNode_UnrealAnchor, ck::FFragment_RecordEntry,
                    ck::FTag_Transform_ExternallyDriven>() &&
                S->Child.Get<ck::FFragment_Transform>().Get_Transform().Equals(Before, 0.0) &&
                S->Child.Get<ck::FFragment_Transform_Previous>().Get_Transform().Equals(Previous, 0.0) &&
                S->Child.Get<ck::FFragment_Transform_RootComponent>().Get_RootComponent().Get() == Root &&
                Root->GetComponentTransform().Equals(VisibleBefore, 0.0) &&
                !S->Parent.Has<ck::FFragment_RecordOfSceneNodes>() &&
                UCk_Utils_GameplayLabel_UE::Get_Label(S->Child) == S->OriginalLabel &&
                NativeDataUnchanged(S->Child, NativeBefore);
        };
        if (!TestTrue(TEXT("production child has complete native LRS references"),
            S->Child.Has_All<ck::FFragment_ContainerRef_Location,
                ck::FFragment_ContainerRef_Rotation, ck::FFragment_ContainerRef_Scale>()))
        { return; }
        const auto OriginalScaleRef = S->Child.Get<ck::FFragment_ContainerRef_Scale>();
        S->Child.Remove<ck::FFragment_ContainerRef_Scale>();
        TestTrue(TEXT("missing native scale reference rejects without output or native data mutation"),
            Reject(S->Anchor.Get(), S->Offset));
        S->Child.Add<ck::FFragment_ContainerRef_Scale>(OriginalScaleRef);
        TestTrue(TEXT("native scale reference restored exactly after rejection"),
            S->Child.Has<ck::FFragment_ContainerRef_Scale>() &&
            S->Child.Get<ck::FFragment_ContainerRef_Scale>().Get_Driver() ==
                OriginalScaleRef.Get_Driver() &&
            S->Child.Get<ck::FFragment_ContainerRef_Scale>().Get_EntryIndex() ==
                OriginalScaleRef.Get_EntryIndex() && NativeDataUnchanged(S->Child, NativeBefore));
        TestTrue(TEXT("reject null and own root without partial state"),
            Reject(nullptr, S->Offset) && Reject(Root, S->Offset));
        auto* OwnChild = NewObject<USceneComponent>(S->Actor.Get());
        OwnChild->SetupAttachment(Root); OwnChild->RegisterComponentWithWorld(W);
        TestTrue(TEXT("reject own attached component feedback"), Reject(OwnChild, S->Offset));
        auto* ForeignAttached = NewObject<USceneComponent>(S->AnchorActor.Get());
        ForeignAttached->SetupAttachment(Root); ForeignAttached->RegisterComponentWithWorld(W);
        TestTrue(TEXT("reject foreign-owned anchor beneath output root"), Reject(ForeignAttached, S->Offset));
        auto* Unregistered = NewObject<USceneComponent>(S->AnchorActor.Get());
        TestTrue(TEXT("reject unregistered anchor"), Reject(Unregistered, S->Offset));
        TestTrue(TEXT("reject unsafe finite offset without partial state"),
            Reject(S->Anchor.Get(), FTransform(FVector(DBL_MAX / 2.0, 0.0, 0.0))));
        Root->SetMobility(EComponentMobility::Static);
        TestTrue(TEXT("reject static own output root"), Reject(S->Anchor.Get(), S->Offset));
        Root->SetMobility(EComponentMobility::Movable);
        FCk_Handle_Transform DefaultChild;
        FCk_Handle_SceneNode InvalidOut;
        TestTrue(TEXT("default child rejected without calling Transform accessors"),
            !UCk_Utils_SceneNode_UE::TryAdd_AnchorToExistingTransform(DefaultChild, Parent,
                S->Anchor.Get(), S->Offset, InvalidOut) && InvalidOut == FCk_Handle_SceneNode{});
        const auto OtherWorlds = ck::auto_test::net::Get_AllPIEWorlds();
        for (auto* OtherWorld : OtherWorlds)
        {
            if (!IsValid(OtherWorld) || OtherWorld == W) { continue; }
            auto* OtherActor = OtherWorld->SpawnActor<AActor>();
            auto* OtherComponent = IsValid(OtherActor) ? NewObject<USceneComponent>(OtherActor) : nullptr;
            if (!TestTrue(TEXT("actual foreign-world component host"), IsValid(OtherComponent))) { return; }
            OtherActor->SetRootComponent(OtherComponent);
            OtherComponent->RegisterComponentWithWorld(OtherWorld);
            TestTrue(TEXT("reject cross-world anchor without partial state"), Reject(OtherComponent, S->Offset));
            OtherActor->Destroy();
            break;
        }
        const auto Dependent = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->Child);
        TestTrue(TEXT("reject allocated dependent"), Reject(S->Anchor.Get(), S->Offset));
        auto Pending = Dependent;
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Pending);
        TestTrue(TEXT("reject allocated pending-kill dependent"), Reject(S->Anchor.Get(), S->Offset));
        S->Rejected = true;
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(4));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* W)
    {
        if (!TestTrue(TEXT("rejections left production child intact"), S->Rejected && Constructed(S))) { return; }
        if (!TestTrue(TEXT("named production child still constructed"), NamedConstructed(S))) { return; }
        if (!TestTrue(TEXT("named rejection has no competing admission blocker"),
            !UCk_Utils_GameplayLabel_UE::Get_IsUnnamedLabel(S->NamedChild) &&
            !S->NamedChild.Has_Any<ck::FFragment_Transform_Requests,
                ck::FFragment_SceneNode, ck::SceneNodeParent,
                ck::FFragment_SceneNode_UnrealAnchor, ck::FFragment_RecordEntry>() &&
            !S->Parent.Has<ck::FFragment_RecordOfSceneNodes>() &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(S->NamedChild) == S->Parent &&
            S->NamedChild.Get<ck::FFragment_Transform_RootComponent>().Get_RootComponent().Get() ==
                S->NamedActor->GetRootComponent()))
        { return; }
        const auto NamedBefore = S->NamedChild.Get<ck::FFragment_Transform>().Get_Transform();
        const auto NamedPrevious = S->NamedChild.Get<ck::FFragment_Transform_Previous>().Get_Transform();
        const auto NamedRootBefore = S->NamedActor->GetRootComponent()->GetComponentTransform();
        FNativeData NamedNativeBefore;
        if (!TestTrue(TEXT("named child has complete native LRS data"),
            CopyNativeData(S->NamedChild, NamedNativeBefore))) { return; }
        FCk_Handle_SceneNode NamedOut;
        auto NamedChild = UCk_Utils_Transform_UE::Cast(S->NamedChild);
        auto NamedParent = UCk_Utils_Transform_UE::Cast(S->Parent);
        TestTrue(TEXT("named production label rejects existing-transform anchor without mutation"),
            !UCk_Utils_SceneNode_UE::TryAdd_AnchorToExistingTransform(
                NamedChild, NamedParent, S->Anchor.Get(), S->Offset, NamedOut) &&
            NamedOut == FCk_Handle_SceneNode{} &&
            !S->NamedChild.Has_Any<ck::FFragment_SceneNode, ck::SceneNodeParent,
                ck::FFragment_SceneNode_UnrealAnchor, ck::FFragment_RecordEntry,
                ck::FFragment_RecordOfSceneNodes, ck::FTag_Transform_ExternallyDriven>() &&
            !S->Parent.Has<ck::FFragment_RecordOfSceneNodes>() &&
            S->NamedChild.Get<ck::FFragment_Transform>().Get_Transform().Equals(NamedBefore, 0.0) &&
            S->NamedChild.Get<ck::FFragment_Transform_Previous>().Get_Transform().Equals(NamedPrevious, 0.0) &&
            S->NamedChild.Get<ck::FFragment_Transform_RootComponent>().Get_RootComponent().Get() ==
                S->NamedActor->GetRootComponent() &&
            S->NamedActor->GetRootComponent()->GetComponentTransform().Equals(NamedRootBefore, 0.0) &&
            UCk_Utils_GameplayLabel_UE::Get_Label(S->NamedChild) == S->NamedLabel &&
            NativeDataUnchanged(S->NamedChild, NamedNativeBefore));
        TWeakPtr<FState> Weak(S);
        S->LateTick = FWorldDelegates::OnWorldPostActorTick.AddLambda([this, Weak, W](UWorld* TickWorld, ELevelTick, float)
        {
            auto Pinned = Weak.Pin();
            if (!Pinned.IsValid() || TickWorld != W || Pinned->Composed) { return; }
            auto Child = UCk_Utils_Transform_UE::Cast(Pinned->Child);
            auto Parent = UCk_Utils_Transform_UE::Cast(Pinned->Parent);
            FCk_Handle_SceneNode Node;
            Pinned->Composed = UCk_Utils_SceneNode_UE::TryAdd_AnchorToExistingTransform(
                Child, Parent, Pinned->Anchor.Get(), Pinned->Offset, Node);
            TestTrue(TEXT("late-frame existing-transform admission succeeds"), Pinned->Composed);
            TestTrue(TEXT("late-frame composition retains child identity"),
                Pinned->Composed && Node == Pinned->Child);
            TestTrue(TEXT("late-frame composition marks external transform drive"),
                Pinned->Composed && Pinned->Child.Has<ck::FTag_Transform_ExternallyDriven>());
            TestTrue(TEXT("late-frame composition immediately publishes exact native LRS seed"),
                Pinned->Composed && NativeDataMatchesPose(Pinned->Child,
                    Pinned->Offset * Pinned->Anchor->GetComponentTransform()));
            TestTrue(TEXT("late-frame composition preserves own output bridge"),
                Pinned->Composed &&
                Pinned->Child.Get<ck::FFragment_Transform_RootComponent>().Get_RootComponent().Get() ==
                    Pinned->Actor->GetRootComponent());
            TestTrue(TEXT("late-frame composition preserves production unnamed label"),
                Pinned->Composed && UCk_Utils_GameplayLabel_UE::Has(Pinned->Child) &&
                UCk_Utils_GameplayLabel_UE::Get_IsUnnamedLabel(Pinned->Child) &&
                UCk_Utils_GameplayLabel_UE::Get_Label(Pinned->Child) == Pinned->OriginalLabel);
            FWorldDelegates::OnWorldPostActorTick.Remove(Pinned->LateTick); Pinned->LateTick.Reset();
        });
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([S]() { return OutputMatches(S); }), 20.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        TestTrue(TEXT("stationary anchor publishes initial own-root pose after late-frame call"), OutputMatches(S));
        const auto Before = S->Child.Get<ck::FFragment_Transform>().Get_Transform();
        FNativeData NativeBefore;
        if (!TestTrue(TEXT("composed child retains complete native LRS data"),
            CopyNativeData(S->Child, NativeBefore))) { return; }
        auto Child = UCk_Utils_Transform_UE::Cast(S->Child);
        auto Parent = UCk_Utils_Transform_UE::Cast(S->Parent);
        FCk_Handle_SceneNode Duplicate;
        TestTrue(TEXT("duplicate composition rejected without rewriting Transform"),
            !UCk_Utils_SceneNode_UE::TryAdd_AnchorToExistingTransform(Child, Parent,
                S->Anchor.Get(), FTransform::Identity, Duplicate) && Duplicate == FCk_Handle_SceneNode{} &&
            S->Child.Get<ck::FFragment_Transform>().Get_Transform().Equals(Before, 0.0) &&
            NativeDataUnchanged(S->Child, NativeBefore));
        UCk_Utils_Transform_UE::Request_SetLocation(Parent,
            FCk_Request_Transform_SetLocation(FVector(-500.0, 0.0, 0.0)), {});
        S->Anchor->SetWorldLocation(FVector(0.0, 350.0, 0.0));
        S->Moved = true;
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([S]() { return S->Moved && OutputMatches(S); }), 20.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    { TestTrue(TEXT("foreign movement controls output independently from structural parent"), OutputMatches(S)); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
