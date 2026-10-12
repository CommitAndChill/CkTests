#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkSceneNodeGym_EcsRoots.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"
#include <cfloat>

namespace ck_existing_anchor_test
{
    struct FState
    {
        FCk_Handle Parent;
        FCk_Handle Child;
        FCk_Handle Carrier;
        TWeakObjectPtr<AActor> Actor;
        TWeakObjectPtr<AActor> AnchorActor;
        TWeakObjectPtr<USceneComponent> Anchor;
        FTransform Offset = FTransform(FVector(25.0, 0.0, 0.0));
        FDelegateHandle LateTick;
        bool Composed = false;
        bool Moved = false;
        bool Rejected = false;
        ~FState() { if (LateTick.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(LateTick); } }
    };
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
        return Current.Equals(Expected) && S->Actor->GetRootComponent()->GetComponentTransform().Equals(Current);
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
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([S]() { return Constructed(S); }), 20.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* W)
    {
        if (!TestTrue(TEXT("real actor bridge and deallocated construction carrier"), Constructed(S))) { return; }
        auto Child = UCk_Utils_Transform_UE::Cast(S->Child);
        auto Parent = UCk_Utils_Transform_UE::Cast(S->Parent);
        const auto Before = S->Child.Get<ck::FFragment_Transform>().Get_Transform();
        const auto Previous = S->Child.Get<ck::FFragment_Transform_Previous>().Get_Transform();
        auto* Root = S->Actor->GetRootComponent();
        const auto VisibleBefore = Root->GetComponentTransform();
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
                Root->GetComponentTransform().Equals(VisibleBefore, 0.0);
        };
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
            TestTrue(TEXT("late-frame composition preserves own output bridge"),
                Pinned->Composed && Node == Pinned->Child &&
                Pinned->Child.Has<ck::FTag_Transform_ExternallyDriven>() &&
                Pinned->Child.Get<ck::FFragment_Transform_RootComponent>().Get_RootComponent().Get() ==
                    Pinned->Actor->GetRootComponent());
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
        auto Child = UCk_Utils_Transform_UE::Cast(S->Child);
        auto Parent = UCk_Utils_Transform_UE::Cast(S->Parent);
        FCk_Handle_SceneNode Duplicate;
        TestTrue(TEXT("duplicate composition rejected without rewriting Transform"),
            !UCk_Utils_SceneNode_UE::TryAdd_AnchorToExistingTransform(Child, Parent,
                S->Anchor.Get(), FTransform::Identity, Duplicate) && Duplicate == FCk_Handle_SceneNode{} &&
            S->Child.Get<ck::FFragment_Transform>().Get_Transform().Equals(Before, 0.0));
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
