#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include <limits>
#include <cfloat>

namespace ck_scene_anchor_seam_test
{
    struct FState
    {
        FCk_Handle WorldRoot;
        FCk_Handle Parent;
        FCk_Handle ComponentRaw;
        FCk_Handle ActorRoot;
        FCk_Handle_SceneNode LegacyComponentNode;
        TWeakObjectPtr<AActor> HostActor;
        TWeakObjectPtr<USceneComponent> Component;
        FTransform Offset = FTransform(FVector(25.0, 0.0, 0.0));
        bool bInitialPassed = false;
        FCk_Handle MissingRecordParent;
        FCk_Handle MissingRecordChild;
        FCk_Handle IntactRecordParent;
        FCk_Handle IntactRecordChild;
        bool bRecordCleanupSetup = false;
    };
    static bool PutRotation(FTransform& Transform, const FQuat& Rotation)
    {
        const auto* Property = FindFProperty<FStructProperty>(
            TBaseStructure<FTransform>::Get(), TEXT("Rotation"));
        if (Property == nullptr || Property->Struct != TBaseStructure<FQuat>::Get())
        { return false; }
        *Property->ContainerPtrToValuePtr<FQuat>(&Transform) = Rotation;
        return true;
    }
    static bool PutTranslation(FTransform& Transform, const FVector& Translation)
    {
        const auto* Property = FindFProperty<FStructProperty>(
            TBaseStructure<FTransform>::Get(), TEXT("Translation"));
        if (Property == nullptr || Property->Struct != TBaseStructure<FVector>::Get())
        { return false; }
        *Property->ContainerPtrToValuePtr<FVector>(&Transform) = Translation;
        return true;
    }
    static TArray<FCk_Handle_SceneNode> DirectEntries(const FCk_Handle& Parent)
    {
        if (!ck::IsValid(Parent) || !Parent.Has<ck::FFragment_RecordOfSceneNodes>())
        { return {}; }
        if (Parent.Has<ck::FFragment_RecordOfEntityExtensions>()) { return {}; }
        return ck::FUtils_RecordOfSceneNodes::Get_Entries(Parent);
    }
    static int32 ActualSingleLayer(const FCk_Handle& H)
    {
        if (!ck::IsValid(H)) { return -1; }
        int32 Count = 0;
        int32 Index = -1;
        if (H.Has<ck::FTag_SceneNode_Layer0>()) { ++Count; Index = 0; }
        if (H.Has<ck::FTag_SceneNode_Layer1>()) { ++Count; Index = 1; }
        if (H.Has<ck::FTag_SceneNode_Layer2>()) { ++Count; Index = 2; }
        if (H.Has<ck::FTag_SceneNode_Layer3>()) { ++Count; Index = 3; }
        if (H.Has<ck::FTag_SceneNode_Layer4>()) { ++Count; Index = 4; }
        if (H.Has<ck::FTag_SceneNode_Layer5>()) { ++Count; Index = 5; }
        if (H.Has<ck::FTag_SceneNode_Layer6>()) { ++Count; Index = 6; }
        if (H.Has<ck::FTag_SceneNode_Layer7>()) { ++Count; Index = 7; }
        if (H.Has<ck::FTag_SceneNode_Layer8>()) { ++Count; Index = 8; }
        if (H.Has<ck::FTag_SceneNode_Layer9>()) { ++Count; Index = 9; }
        return Count == 1 ? Index : -1;
    }
    static bool NoSceneStage(const FCk_Handle& H)
    {
        return ck::IsValid(H) &&
            !H.Has_Any<ck::FFragment_Transform, ck::FFragment_Transform_Previous,
                ck::FFragment_SceneNode, ck::SceneNodeParent,
                ck::FFragment_SceneNode_UnrealAnchor, ck::FFragment_RecordEntry,
                ck::FTag_SceneNode_Layer0, ck::FTag_SceneNode_Layer1,
                ck::FTag_SceneNode_Layer2, ck::FTag_SceneNode_Layer3,
                ck::FTag_SceneNode_Layer4, ck::FTag_SceneNode_Layer5,
                ck::FTag_SceneNode_Layer6, ck::FTag_SceneNode_Layer7,
                ck::FTag_SceneNode_Layer8, ck::FTag_SceneNode_Layer9,
                ck::FTag_SceneNode_RelativeTransformUpdated,
                ck::FTag_SceneNode_PropagationQueued>();
    }
    static bool Live(const FCk_Handle& H)
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodePreallocatedAnchor,
    "Ck.SceneNode.Network.PreallocatedAnchor.FreshLeaf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodePreallocatedAnchor::RunTest(const FString& Parameters)
{
    using namespace ck_scene_anchor_seam_test;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
    {
        if (!TestTrue(TEXT("actual initialized authority PIE world"),
            IsValid(World) && World->GetAuthGameMode() != nullptr)) { return; }
        State->WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!TestTrue(TEXT("real ECS world transient"), Live(State->WorldRoot))) { return; }
        State->Parent = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->WorldRoot);
        if (!TestTrue(TEXT("real lifetime parent"), Live(State->Parent))) { return; }
        auto ParentTransform = UCk_Utils_Transform_UE::Add(State->Parent,
            FTransform(FVector(100.0, 0.0, 0.0)), ECk_Replication::DoesNotReplicate);
        if (!TestTrue(TEXT("real parent Transform"), ck::IsValid(ParentTransform))) { return; }

        auto PlainRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        State->ComponentRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        auto* Actor = World->SpawnActor<AActor>();
        auto* Component = IsValid(Actor) ? NewObject<USceneComponent>(Actor) : nullptr;
        if (!TestTrue(TEXT("real component host"), IsValid(Actor) && IsValid(Component))) { return; }
        Actor->SetRootComponent(Component);
        Component->RegisterComponentWithWorld(World);
        Component->SetWorldLocation(FVector(0.0, 200.0, 0.0));
        State->Component = Component;
        State->HostActor = Actor;
        if (!TestTrue(TEXT("real registered component"), Component->IsRegistered())) { return; }

        int32 NewAdded = 0;
        FCk_Handle ReentrantRaw;
        bool bReentrantObserverComplete = false;
        bool bReentrantClosureProven = false;
        bool bPlainObserverComplete = false;
        bool bComponentObserverComplete = false;
        const auto Observer = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
            [&](const FCk_Handle_Transform& Added)
            {
                ++NewAdded;
                if (Added == PlainRaw)
                {
                    bPlainObserverComplete = Added.Has_All<ck::FFragment_SceneNode,
                        ck::SceneNodeParent, ck::FFragment_SceneNode_PropagationState>() &&
                        ck::FUtils_RecordOfSceneNodes::Get_ContainsEntry(State->Parent,
                            UCk_Utils_SceneNode_UE::Cast(PlainRaw));
                }
                if (Added == ReentrantRaw && ck::IsValid(ReentrantRaw))
                {
                    bReentrantObserverComplete = Added.Has_All<ck::FFragment_SceneNode,
                        ck::SceneNodeParent, ck::FFragment_SceneNode_PropagationState>() &&
                        DirectEntries(State->Parent).Contains(
                            UCk_Utils_SceneNode_UE::Cast(ReentrantRaw));
                    bReentrantClosureProven =
                        UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                            {ReentrantRaw}, {ReentrantRaw}, State->WorldRoot);
                    if (bReentrantObserverComplete && bReentrantClosureProven)
                    { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(ReentrantRaw); }
                }
                if (Added == State->ComponentRaw)
                {
                    bComponentObserverComplete = Added.Has_All<ck::FFragment_SceneNode,
                        ck::SceneNodeParent, ck::FFragment_SceneNode_UnrealAnchor>() &&
                        ck::FUtils_RecordOfSceneNodes::Get_ContainsEntry(State->Parent,
                            UCk_Utils_SceneNode_UE::Cast(State->ComponentRaw));
                }
            });
        ON_SCOPE_EXIT { UCk_Utils_Transform_UE::Get_OnAdded().Remove(Observer); };
        FCk_Handle_SceneNode Plain, ComponentNode;
        const bool PlainOK = UCk_Utils_SceneNode_UE::TryAdd_Anchor(
            PlainRaw, ParentTransform, State->Offset, Plain);
        const bool ComponentOK = UCk_Utils_SceneNode_UE::TryAdd_AnchorToUnrealComponent(
            State->ComponentRaw, ParentTransform, Component, State->Offset, ComponentNode);
        if (!TestTrue(TEXT("two genuine Anchor constructions retain raw identities"),
            PlainOK && ComponentOK && Plain == PlainRaw && ComponentNode == State->ComponentRaw))
        { return; }
        TestTrue(TEXT("postcommit editor callbacks see complete record and anchor"),
            NewAdded == 2 && bPlainObserverComplete && bComponentObserverComplete);
        TestTrue(TEXT("no Parent-driven Transform tag"),
            !PlainRaw.Has<ck::FTag_Transform_ExternallyDriven>() &&
            !State->ComponentRaw.Has<ck::FTag_Transform_ExternallyDriven>());
        TestTrue(TEXT("plain authored world seed"),
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::Cast(Plain)).Equals(
                State->Offset * UCk_Utils_Transform_UE::Get_EntityCurrentTransform(ParentTransform), 0.0));
        TestTrue(TEXT("component authored world seed"),
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::Cast(ComponentNode)).Equals(
                State->Offset * Component->GetComponentTransform(), 0.0));

        auto NegativeRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        auto ZeroRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        FCk_Handle_SceneNode NegativeNode, ZeroNode;
        TestTrue(TEXT("ordinary negative scale remains supported"),
            UCk_Utils_SceneNode_UE::TryAdd_Anchor(NegativeRaw, ParentTransform,
                FTransform(FQuat::Identity, FVector(3.0, 4.0, 5.0),
                    FVector(-1.0, 1.0, 1.0)), NegativeNode) && NegativeNode == NegativeRaw);
        TestTrue(TEXT("ordinary zero scale remains supported"),
            UCk_Utils_SceneNode_UE::TryAdd_Anchor(ZeroRaw, ParentTransform,
                FTransform(FQuat::Identity, FVector(6.0, 7.0, 8.0),
                    FVector(0.0, 1.0, 1.0)), ZeroNode) && ZeroNode == ZeroRaw);

        const auto LegacyPlain = UCk_Utils_SceneNode_UE::Create(
            ParentTransform, State->Offset);
        State->LegacyComponentNode = UCk_Utils_SceneNode_UE::CreateAndAttachToUnrealComponent(
            ParentTransform, Component, State->Offset);
        if (!TestTrue(TEXT("existing Create overloads remain usable beside restricted seam"),
            ck::IsValid(LegacyPlain) && ck::IsValid(State->LegacyComponentNode) &&
            LegacyPlain.Has_All<ck::SceneNodeParent, ck::FFragment_SceneNode>() &&
            State->LegacyComponentNode.Has_All<ck::SceneNodeParent,
                ck::FFragment_SceneNode>() &&
            UCk_Utils_SceneNode_UE::Get_Parent(LegacyPlain) == State->Parent &&
            UCk_Utils_SceneNode_UE::Get_Parent(State->LegacyComponentNode) == State->Parent))
        { return; }

        auto Reject = [&](FCk_Handle& Raw, FCk_Handle_Transform& Parent,
                          const FTransform& Local, USceneComponent* Anchor = nullptr)
        {
            const auto BeforeHadRecord = Parent.Has<ck::FFragment_RecordOfSceneNodes>();
            const auto BeforeEntries = DirectEntries(Parent);
            const auto BeforeAdded = NewAdded;
            const auto ParentHadQueue = Parent.Has<ck::FTag_SceneNode_PropagationQueued>();
            const auto ParentHadState = Parent.Has<ck::FFragment_SceneNode_PropagationState>();
            const auto ParentGeneration = ParentHadState
                ? Parent.Get<ck::FFragment_SceneNode_PropagationState>().Get_QueueGeneration() : 0;
            FCk_Handle_SceneNode Out = Plain;
            const bool Accepted = Anchor == nullptr
                ? UCk_Utils_SceneNode_UE::TryAdd_Anchor(Raw, Parent, Local, Out)
                : UCk_Utils_SceneNode_UE::TryAdd_AnchorToUnrealComponent(Raw, Parent, Anchor, Local, Out);
            return !Accepted && Out == FCk_Handle_SceneNode{} && NewAdded == BeforeAdded &&
                Parent.Has<ck::FFragment_RecordOfSceneNodes>() == BeforeHadRecord &&
                DirectEntries(Parent) == BeforeEntries &&
                Parent.Has<ck::FTag_SceneNode_PropagationQueued>() == ParentHadQueue &&
                Parent.Has<ck::FFragment_SceneNode_PropagationState>() == ParentHadState &&
                (!ParentHadState ||
                    Parent.Get<ck::FFragment_SceneNode_PropagationState>()
                        .Get_QueueGeneration() == ParentGeneration);
        };
        auto TearingDownPlain = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        auto TearingDownComponent = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        if (!TestTrue(TEXT("live raw leaves before world teardown rejection"),
            Live(TearingDownPlain) && Live(TearingDownComponent) && !World->bIsTearingDown))
        { return; }
        bool PlainRejectedDuringTeardown = false;
        bool ComponentRejectedDuringTeardown = false;
        {
            const bool OriginalTeardown = World->bIsTearingDown;
            const auto RestoreWorldTeardown = ::ScopeExitSupport::FScopeGuardSyntaxSupport() +
                [World, OriginalTeardown]() { World->bIsTearingDown = OriginalTeardown; };
            World->bIsTearingDown = true;
            PlainRejectedDuringTeardown = Reject(TearingDownPlain, ParentTransform, State->Offset);
            ComponentRejectedDuringTeardown =
                Reject(TearingDownComponent, ParentTransform, State->Offset, Component);
        }
        TestTrue(TEXT("both Anchor overloads reject world teardown without partial state or observers"),
            PlainRejectedDuringTeardown && ComponentRejectedDuringTeardown &&
            NoSceneStage(TearingDownPlain) && NoSceneStage(TearingDownComponent) &&
            !World->bIsTearingDown);
        FCk_Handle Default;
        TestTrue(TEXT("default raw rejects without output or callback"),
            Reject(Default, ParentTransform, State->Offset));
        auto WrongOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->WorldRoot);
        TestTrue(TEXT("wrong direct owner rejects unchanged"),
            Reject(WrongOwner, ParentTransform, State->Offset) && NoSceneStage(WrongOwner));
        auto Dirty = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        UCk_Utils_Transform_UE::Add(Dirty, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        const auto AddedAfterDirtySetup = NewAdded;
        TestTrue(TEXT("dirty child rejects without new SceneNode or observer"),
            !UCk_Utils_SceneNode_UE::TryAdd_Anchor(Dirty, ParentTransform,
                State->Offset, ComponentNode) && !Dirty.Has<ck::FFragment_SceneNode>() &&
                NewAdded == AddedAfterDirtySetup);
        auto MalformedLocalRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        auto BadLocal = FTransform::Identity;
        if (!TestTrue(TEXT("reflected raw malformed local transform"),
            PutTranslation(BadLocal, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0))))
        { return; }
        TestTrue(TEXT("nonfinite local rejects before getter/composition"),
            Reject(MalformedLocalRaw, ParentTransform, BadLocal) && NoSceneStage(MalformedLocalRaw));
        auto NonunitRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        auto NonunitLocal = FTransform::Identity;
        if (!TestTrue(TEXT("reflected raw nonunit quaternion"),
            PutRotation(NonunitLocal, FQuat(0.0, 0.0, 0.0, 2.0))))
        { return; }
        TestTrue(TEXT("finite nonunit quaternion rejects without composition"),
            Reject(NonunitRaw, ParentTransform, NonunitLocal) && NoSceneStage(NonunitRaw));
        auto ExtremeRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        auto ExtremeLocal = FTransform::Identity;
        if (!TestTrue(TEXT("reflected raw extreme finite translation"),
            PutTranslation(ExtremeLocal, FVector(DBL_MAX / 2.0, 0.0, 0.0))))
        { return; }
        TestTrue(TEXT("extreme finite composition rejects before multiply"),
            Reject(ExtremeRaw, ParentTransform, ExtremeLocal) && NoSceneStage(ExtremeRaw));
        auto BadParent = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->WorldRoot);
        auto BadParentTransform = UCk_Utils_Transform_UE::Add(BadParent,
            FTransform::Identity, ECk_Replication::DoesNotReplicate);
        auto BadParentRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(BadParent);
        auto& BadParentPose = const_cast<FTransform&>(
            BadParent.Get<ck::FFragment_Transform>().Get_Transform());
        if (!TestTrue(TEXT("reflected raw malformed parent fragment"),
            PutTranslation(BadParentPose, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0))))
        { return; }
        TestTrue(TEXT("nonfinite parent rejects before current-transform read"),
            Reject(BadParentRaw, BadParentTransform, State->Offset) && NoSceneStage(BadParentRaw));
        // Restore the intentionally malformed fixture before any ECS frame runs.
        TestTrue(TEXT("restore parent fragment after rejection"),
            PutTranslation(BadParentPose, FVector::ZeroVector));

        // Build all ten real SceneNode layers, then test an actual depth-10 overflow.
        FCk_Handle_Transform ChainParent = ParentTransform;
        FCk_Handle_SceneNode Layer0Node;
        FCk_Handle_SceneNode Layer8Node;
        bool bChainGood = true;
        for (int32 Layer = 0; Layer < 10; ++Layer)
        {
            auto Raw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(ChainParent);
            FCk_Handle_SceneNode Node;
            if (!TestTrue(TEXT("real layer 0..9 composes"),
                UCk_Utils_SceneNode_UE::TryAdd_Anchor(Raw, ChainParent,
                    FTransform::Identity, Node) && Node == Raw))
            { bChainGood = false; break; }
            if (Layer == 0) { Layer0Node = Node; }
            if (Layer == 8) { Layer8Node = Node; }
            if (!TestTrue(TEXT("real chain layer and parent edge are exact"),
                ActualSingleLayer(Node) == Layer &&
                Node.Has_All<ck::SceneNodeParent, ck::FFragment_SceneNode>() &&
                UCk_Utils_SceneNode_UE::Get_Parent(Node) == ChainParent))
            { bChainGood = false; break; }
            ChainParent = UCk_Utils_Transform_UE::Cast(Node);
        }
        if (!bChainGood)
        { return; }
        const auto Layer9 = ChainParent;
        // Public record disconnect temporarily removes an ancestor's forward link while
        // leaving the child's scene parent and lifetime parent untouched. Restore before
        // any ECS frame, and reject a new child without modifying its retained raw handle.
        const auto DirectBeforeCorruption = DirectEntries(State->Parent);
        if (!TestTrue(TEXT("disconnect genuine ancestor forward record link"),
            ck::FUtils_RecordOfSceneNodes::Request_Disconnect(
                State->Parent, Layer0Node, {})))
        { return; }
        auto LegalChainParent = UCk_Utils_Transform_UE::Cast(Layer8Node);
        auto BrokenChainRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
            LegalChainParent);
        TestTrue(TEXT("missing ancestor forward link rejects legal layer-9 add"),
            Reject(BrokenChainRaw, LegalChainParent, FTransform::Identity) &&
            NoSceneStage(BrokenChainRaw));
        ck::FUtils_RecordOfSceneNodes::Request_Connect(
            State->Parent, Layer0Node, ECk_Record_LabelRequirementPolicy::Optional, {});
        const auto DirectAfterRestore = DirectEntries(State->Parent);
        bool bExactRestoration = DirectAfterRestore.Num() == DirectBeforeCorruption.Num();
        for (const auto& Earlier : DirectBeforeCorruption)
        {
            int32 Matches = 0;
            for (const auto& Later : DirectAfterRestore)
            { if (Earlier == Later) { ++Matches; } }
            bExactRestoration &= Matches == 1;
        }
        if (!TestTrue(TEXT("restore exact ancestor membership before world tick"),
            bExactRestoration))
        { return; }
        auto OverflowRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Layer9);
        const auto BeforeOverflowEntries = ck::FUtils_RecordOfSceneNodes::Get_Entries(Layer9);
        const auto BeforeOverflowCallbacks = NewAdded;
        FCk_Handle_SceneNode OverflowOut = Plain;
        TestTrue(TEXT("real layer-10 rejection has no stage, record, or observer"),
            !UCk_Utils_SceneNode_UE::TryAdd_Anchor(OverflowRaw, ChainParent,
                FTransform::Identity, OverflowOut) && OverflowOut == FCk_Handle_SceneNode{} &&
            NoSceneStage(OverflowRaw) &&
            ck::FUtils_RecordOfSceneNodes::Get_Entries(Layer9) == BeforeOverflowEntries &&
            NewAdded == BeforeOverflowCallbacks);

        auto PendingRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PendingRaw);
        TestTrue(TEXT("real BeginDestroy child rejects unchanged"),
            Reject(PendingRaw, ParentTransform, State->Offset));
        auto NeverRegistered = NewObject<USceneComponent>(Actor);
        auto UnregisteredRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        TestTrue(TEXT("unregistered component rejects before stage"),
            Reject(UnregisteredRaw, ParentTransform, State->Offset, NeverRegistered) &&
            NoSceneStage(UnregisteredRaw));
        UWorld* ForeignPIEWorld = nullptr;
        for (auto* Candidate : ck::auto_test::net::Get_AllPIEWorlds())
        { if (IsValid(Candidate) && Candidate != World) { ForeignPIEWorld = Candidate; break; } }
        if (TestNotNull(TEXT("actual independent PIE world"), ForeignPIEWorld))
        {
            auto* ForeignActor = ForeignPIEWorld->SpawnActor<AActor>();
            auto* ForeignComponent = IsValid(ForeignActor)
                ? NewObject<USceneComponent>(ForeignActor) : nullptr;
            if (TestTrue(TEXT("actual foreign registered component"),
                IsValid(ForeignComponent)))
            {
                ForeignActor->SetRootComponent(ForeignComponent);
                ForeignComponent->RegisterComponentWithWorld(ForeignPIEWorld);
                auto ForeignComponentRaw =
                    UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
                TestTrue(TEXT("foreign component world rejects before mutation"),
                    ForeignComponent->IsRegistered() &&
                    Reject(ForeignComponentRaw, ParentTransform, State->Offset,
                        ForeignComponent) && NoSceneStage(ForeignComponentRaw));
            }
        }
        auto NullRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        FCk_Handle_SceneNode NullOut = Plain;
        TestTrue(TEXT("null component rejects and clears output"),
            !UCk_Utils_SceneNode_UE::TryAdd_AnchorToUnrealComponent(
                NullRaw, ParentTransform, nullptr, State->Offset, NullOut) &&
            NullOut == FCk_Handle_SceneNode{} && NoSceneStage(NullRaw));
        ck::FEcsWorld ForeignWorld;
        auto ForeignRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
            UCk_Utils_EntityLifetime_UE::Get_TransientEntity(ForeignWorld.Get_Registry()));
        TestTrue(TEXT("foreign registry rejects unchanged"),
            Reject(ForeignRaw, ParentTransform, State->Offset) && NoSceneStage(ForeignRaw));
        FCk_Handle Expired;
        {
            ck::FEcsWorld Temporary;
            Expired = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                UCk_Utils_EntityLifetime_UE::Get_TransientEntity(Temporary.Get_Registry()));
        }
        TestTrue(TEXT("stale registry rejects without callback"),
            Reject(Expired, ParentTransform, State->Offset));
        // Corrupt a genuine direct child before another add; reject and restore in this stack.
        const auto OriginalRelative = State->ComponentRaw.Get<ck::FFragment_SceneNode>()
            .Get_RelativeTransform();
        State->ComponentRaw.Remove<ck::FFragment_SceneNode>();
        auto BlockedByMalformedExisting =
            UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        TestTrue(TEXT("malformed existing direct child blocks composition"),
            Reject(BlockedByMalformedExisting, ParentTransform, State->Offset) &&
            NoSceneStage(BlockedByMalformedExisting));
        State->ComponentRaw.Add<ck::FFragment_SceneNode>(OriginalRelative);

        // Observer sees the complete committed leaf and can retire it immediately.
        // True/out still denote the committed callback boundary; a caller must revalidate.
        ReentrantRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Parent);
        FCk_Handle_SceneNode ReentrantOut;
        const auto ReentrantAccepted = UCk_Utils_SceneNode_UE::TryAdd_Anchor(
            ReentrantRaw, ParentTransform, State->Offset, ReentrantOut);
        TestTrue(TEXT("observer retirement after full composition demands revalidation"),
            ReentrantAccepted && ReentrantOut == ReentrantRaw &&
            bReentrantObserverComplete && bReentrantClosureProven &&
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(ReentrantRaw,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy));
        Component->SetWorldLocation(FVector(0.0, 350.0, 0.0));
        const auto ActorPending =
            UCk_Utils_EntityScript_WithActor_UE::Request_SpawnEntityScript_OnActor(
                Actor, UCk_EntityScript_WithActor_UE::StaticClass());
        if (!TestTrue(TEXT("real WithActor pending spawn admitted"),
            ck::IsValid(ActorPending))) { return; }
        State->ActorRoot = ActorPending.Get_EntityUnderConstruction();
        TestTrue(TEXT("real WithActor parent target retained"), Live(State->ActorRoot));
        State->bInitialPassed = true;
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(4));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([State]()
    {
        if (!State->bInitialPassed || !Live(State->ComponentRaw) ||
            !State->ComponentRaw.Has<ck::FFragment_Transform>() ||
            !ck::IsValid(State->LegacyComponentNode) ||
            !State->LegacyComponentNode.Has<ck::FFragment_Transform>() ||
            !State->Component.IsValid()) { return false; }
        const auto Node = UCk_Utils_Transform_UE::Cast(State->ComponentRaw);
        return ck::IsValid(Node) &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Node).Equals(
                State->Offset * State->Component->GetComponentTransform(), 0.0) &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::Cast(State->LegacyComponentNode)).Equals(
                    State->Offset * State->Component->GetComponentTransform(), 0.0);
    }), 15.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([State]()
    {
        return Live(State->ActorRoot) && State->HostActor.IsValid() &&
            State->ActorRoot.Has_All<ck::FFragment_Transform,
                ck::FTag_EntityScript_HasBegunPlay>() &&
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(State->ActorRoot)
                == State->HostActor.Get();
    }), 15.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
    {
        if (!TestTrue(TEXT("actual WithActor parent constructed in authority world"),
            IsValid(World) && Live(State->ActorRoot) &&
            State->ActorRoot.Has_All<ck::FFragment_Transform,
                ck::FTag_EntityScript_HasBegunPlay>() &&
            State->HostActor.IsValid() && State->HostActor->GetWorld() == World))
        { return; }
        auto ChildRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->ActorRoot);
        FCk_Handle_SceneNode ChildNode;
        auto ActorTransform = UCk_Utils_Transform_UE::Cast(State->ActorRoot);
        if (!TestTrue(TEXT("real WithActor parent composes Anchor leaf"),
            Live(ChildRaw) && UCk_Utils_SceneNode_UE::TryAdd_Anchor(
                ChildRaw, ActorTransform, FTransform(FVector(0.0, 0.0, 20.0)), ChildNode) &&
            ChildNode == ChildRaw &&
            ChildRaw.Has_All<ck::SceneNodeParent, ck::FFragment_SceneNode>() &&
            UCk_Utils_SceneNode_UE::Get_Parent(
                UCk_Utils_SceneNode_UE::Cast(ChildRaw)) == State->ActorRoot))
        { return; }
        auto* const RootComponent = State->HostActor->GetRootComponent();
        if (!TestTrue(TEXT("actual WithActor root bridge is registered"),
            State->ActorRoot.Has<ck::FFragment_Transform_RootComponent>() &&
            IsValid(RootComponent) && RootComponent->IsRegistered() &&
            State->ActorRoot.Get<ck::FFragment_Transform_RootComponent>()
                .Get_RootComponent().Get() == RootComponent))
        { return; }
        auto* const SeparateActor = World->SpawnActor<AActor>();
        auto* const SeparateAnchor = IsValid(SeparateActor)
            ? NewObject<USceneComponent>(SeparateActor) : nullptr;
        if (!TestTrue(TEXT("separate same-world Anchor component exists"),
            IsValid(SeparateActor) && IsValid(SeparateAnchor)))
        { return; }
        SeparateActor->SetRootComponent(SeparateAnchor);
        SeparateAnchor->RegisterComponentWithWorld(World);
        if (!TestTrue(TEXT("separate Anchor is registered"),
            SeparateAnchor->IsRegistered()))
        { return; }
        // GetComponentTransform is a const reference to the real ComponentToWorld
        // (configured engine SceneComponent.h:1097-1099). Mutate reflected raw bits,
        // test before any scheduler frame, then restore the entire original pose.
        auto MalformedDriverRaw =
            UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->ActorRoot);
        if (!TestTrue(TEXT("fresh child for malformed actual driver pose"),
            Live(MalformedDriverRaw)))
        { return; }
        const auto DriverEntriesBefore = DirectEntries(State->ActorRoot);
        int32 DriverAdded = 0;
        const auto DriverObserver = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
            [&](const FCk_Handle_Transform&) { ++DriverAdded; });
        const FTransform OriginalDriverPose = RootComponent->GetComponentTransform();
        auto& MutableDriverPose = const_cast<FTransform&>(RootComponent->GetComponentTransform());
        if (!TestTrue(TEXT("reflectively inject malformed actual driver pose"),
            PutTranslation(MutableDriverPose,
                FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0))))
        {
            UCk_Utils_Transform_UE::Get_OnAdded().Remove(DriverObserver);
            return;
        }
        FCk_Handle_SceneNode MalformedOut = ChildNode;
        const bool MalformedAccepted =
            UCk_Utils_SceneNode_UE::TryAdd_AnchorToUnrealComponent(
                MalformedDriverRaw, ActorTransform, SeparateAnchor,
                State->Offset, MalformedOut);
        MutableDriverPose = OriginalDriverPose;
        UCk_Utils_Transform_UE::Get_OnAdded().Remove(DriverObserver);
        TestTrue(TEXT("component overload rejects malformed actual parent driver pose"),
            !MalformedAccepted && MalformedOut == FCk_Handle_SceneNode{} &&
            NoSceneStage(MalformedDriverRaw) && DriverAdded == 0 &&
            DirectEntries(State->ActorRoot) == DriverEntriesBefore &&
            RootComponent->GetComponentTransform().Equals(OriginalDriverPose, 0.0));
        auto RejectRaw = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->ActorRoot);
        if (!TestTrue(TEXT("fresh child under real WithActor root retained"), Live(RejectRaw)))
        { return; }
        const auto EntriesBefore = DirectEntries(State->ActorRoot);
        int32 NewAdded = 0;
        const auto Observer = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
            [&](const FCk_Handle_Transform&) { ++NewAdded; });
        ON_SCOPE_EXIT { UCk_Utils_Transform_UE::Get_OnAdded().Remove(Observer); };
        RootComponent->UnregisterComponent();
        FCk_Handle_SceneNode Out = ChildNode;
        const bool Accepted = UCk_Utils_SceneNode_UE::TryAdd_AnchorToUnrealComponent(
            RejectRaw, ActorTransform, SeparateAnchor, State->Offset, Out);
        RootComponent->RegisterComponentWithWorld(World);
        TestTrue(TEXT("component overload rejects unregistered parent root bridge atomically"),
            !Accepted && Out == FCk_Handle_SceneNode{} &&
            NoSceneStage(RejectRaw) && NewAdded == 0 &&
            DirectEntries(State->ActorRoot) == EntriesBefore &&
            RootComponent->IsRegistered());
    })));
    // Both specimens are composed through the public SceneNode path. Only one
    // parent record disappears before its genuine child retires.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
    {
        if (!TestTrue(TEXT("record cleanup specimens have the original authority world"),
            IsValid(World) && Live(State->WorldRoot) &&
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World)
                == State->WorldRoot))
        { return; }
        const auto Compose = [this, State](FCk_Handle& Parent, FCk_Handle& Child)
        {
            Parent = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->WorldRoot);
            if (!TestTrue(TEXT("isolated live record parent"), Live(Parent))) { return false; }
            auto ParentTransform = UCk_Utils_Transform_UE::Add(Parent,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            if (!TestTrue(TEXT("isolated parent real Transform"),
                ck::IsValid(ParentTransform))) { return false; }
            Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Parent);
            if (!TestTrue(TEXT("isolated live direct lifetime child"),
                Live(Child) && UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Parent))
            { return false; }
            FCk_Handle_SceneNode Node;
            if (!TestTrue(TEXT("real TryAdd_Anchor installs record cleanup"),
                UCk_Utils_SceneNode_UE::TryAdd_Anchor(
                    Child, ParentTransform, FTransform::Identity, Node) &&
                Node == Child && Live(Child) &&
                Child.Has_All<ck::FFragment_RecordEntry, ck::SceneNodeParent,
                    ck::FFragment_SceneNode>() &&
                Parent.Has<ck::FFragment_RecordOfSceneNodes>() &&
                ck::FUtils_RecordOfSceneNodes::Get_ContainsEntry(Parent, Node)))
            { return false; }
            TArray<FCk_Handle_SceneNode> Direct;
            if (!TestTrue(TEXT("direct-entry API returns exactly the real SceneNode child"),
                ck::FUtils_RecordOfSceneNodes::TryGet_DirectEntries(Parent, Direct) &&
                Direct.Num() == 1 && Direct[0] == Node))
            { return false; }
            return true;
        };
        if (!Compose(State->MissingRecordParent, State->MissingRecordChild) ||
            !Compose(State->IntactRecordParent, State->IntactRecordChild))
        { return; }
        const auto IntactEntriesBefore = DirectEntries(State->IntactRecordParent);
        int32 UnexpectedTransformCallbacks = 0;
        const auto Observer = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
            [&](const FCk_Handle_Transform&) { ++UnexpectedTransformCallbacks; });
        ON_SCOPE_EXIT { UCk_Utils_Transform_UE::Get_OnAdded().Remove(Observer); };
        const auto RejectedRead = [State, &IntactEntriesBefore](
            const FCk_Handle& Invalid)
        {
            TArray<FCk_Handle_SceneNode> Out{
                UCk_Utils_SceneNode_UE::Cast(State->IntactRecordChild)};
            return !ck::FUtils_RecordOfSceneNodes::TryGet_DirectEntries(Invalid, Out) &&
                Out.IsEmpty() && Live(State->IntactRecordParent) &&
                DirectEntries(State->IntactRecordParent) == IntactEntriesBefore &&
                Live(State->IntactRecordChild);
        };
        FCk_Handle Default;
        if (!TestTrue(TEXT("default record input fails and resets output unchanged"),
            RejectedRead(Default))) { return; }
        FCk_Handle Stale;
        {
            ck::FEcsWorld Temporary;
            Stale = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                UCk_Utils_EntityLifetime_UE::Get_TransientEntity(
                    Temporary.Get_Registry()));
        }
        if (!TestTrue(TEXT("stale record input fails and resets output unchanged"),
            RejectedRead(Stale))) { return; }
        if (!TestTrue(TEXT("remove only genuine parent SceneNode record"),
            State->MissingRecordParent.Try_Remove<ck::FFragment_RecordOfSceneNodes>() &&
            Live(State->MissingRecordParent) &&
            !State->MissingRecordParent.Has<ck::FFragment_RecordOfSceneNodes>() &&
            Live(State->MissingRecordChild) &&
            State->MissingRecordChild.Has<ck::FFragment_RecordEntry>()))
        { return; }
        if (!TestTrue(TEXT("live no-record input fails and resets output unchanged"),
            RejectedRead(State->MissingRecordParent) &&
            UnexpectedTransformCallbacks == 0)) { return; }
        State->bRecordCleanupSetup = true;
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->MissingRecordChild);
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->IntactRecordChild);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([State]()
    {
        return !State->bRecordCleanupSetup ||
            (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->MissingRecordChild) &&
             !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->IntactRecordChild));
    }), 15.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
    {
        TestTrue(TEXT("both real record-entry children finish retirement"),
            State->bRecordCleanupSetup &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->MissingRecordChild) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->IntactRecordChild) &&
            Live(State->MissingRecordParent) &&
            !State->MissingRecordParent.Has<ck::FFragment_RecordOfSceneNodes>() &&
            Live(State->IntactRecordParent) &&
            State->IntactRecordParent.Has<ck::FFragment_RecordOfSceneNodes>() &&
            DirectEntries(State->IntactRecordParent).IsEmpty());
        TArray<FCk_Handle_SceneNode> EmptyDirect{FCk_Handle_SceneNode{}};
        const bool IntactRecordStillLive = Live(State->IntactRecordParent) &&
            State->IntactRecordParent.Has<ck::FFragment_RecordOfSceneNodes>();
        TestTrue(TEXT("valid empty record succeeds and clears output"),
            IntactRecordStillLive &&
            ck::FUtils_RecordOfSceneNodes::TryGet_DirectEntries(
                State->IntactRecordParent, EmptyDirect) && EmptyDirect.IsEmpty());
        // Also retire partially composed specimens after any earlier visible failure.
        if (Live(State->MissingRecordParent))
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->MissingRecordParent); }
        if (Live(State->IntactRecordParent))
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->IntactRecordParent); }
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([State]()
    {
        return !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->MissingRecordParent) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->MissingRecordChild) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->IntactRecordParent) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                State->IntactRecordChild);
    }), 15.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
