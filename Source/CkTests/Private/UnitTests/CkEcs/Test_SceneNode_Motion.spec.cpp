#include "CkSceneNode_MotionTestAccess.h"
#include "CkSceneNode_FrameSamplerTestAccess.h"
#include "CkSceneNode_EcsRootsTestAccess.h"
#include "CkSceneNode_NodeGraphTestAccess.h"
#include "CkSceneNode_RetirementTestAccess.h"

#include "CkSceneNode_ActorLedgerProbe.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_EcsRoots.h"
#include "CkSceneNodeGym_NodeGraph.h"
#include "CkSceneNodeGym_FrameSampler.h"
#include "CkSceneNodeGym_QueueRuntime.h"
#include "CkSceneNodeGym_Retirement.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "CoreGlobals.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

namespace ck_scene_node_motion_test
{
    using FSlot = FCk_SceneNodeMotion_TestAccess::FSlot;
    constexpr int32 ActorRole[6] = {1, 3, 5, 10, 13, 16};

    struct FPose
    {
        FTransform AnimatedOffset;
        FTransform AnimatedWorld;
        FTransform RotatingRoot;
        FTransform VisibleCube;
    };

    struct FState;
    void RemoveSamplerObservers(FState& S);
    void RemoveRetirementActorObserver(FState& S);
    bool RetirementReceiptMatchesFrozen(const FState& S,
        bool bRequireQuiescentDestroy);

    struct FState
    {
        ~FState()
        {
            RemoveSamplerObservers(*this);
            RemoveRetirementActorObserver(*this);
        }
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> Source;
        TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
        TWeakObjectPtr<UCk_SceneNodeGym_EcsRoots_UE> Roots;
        TWeakObjectPtr<UCk_SceneNodeGym_NodeGraph_UE> Graph;
        TStrongObjectPtr<UCk_SceneNodeGym_Motion_UE> Motion;
        TArray<TWeakObjectPtr<ACk_SceneNode_ActorLedgerProbe>> Visuals;
        FCk_Handle WorldRoot;
        FCk_Handle RootHandles[6];
        FCk_Handle NodeHandles[11];
        TArray<FSlot> Frozen;
        FPose DormantPose;
        FPose RunningPose;
        FPose StoppedPose;
        int32 Generation = 0;
        int32 ExpectedGeneration = 1;
        uint64 FirstMotionFrame = 0;
        TArray<int32> FirstIssued;
        uint64 StableFrame = 0;
        TArray<int32> IssuedAtClose;
        TStrongObjectPtr<UCk_SceneNodeGym_FrameSampler_UE> Sampler;
        FDelegateHandle FrameObserver;
        FDelegateHandle ComponentObserver;
        TWeakObjectPtr<USceneComponent> ObservedComponent;
        int64 ObservedFrameSerial = 0;
        uint64 ObservedEngineFrame = 0;
        FCkSceneNodeGym_Frame FirstFrame;
        FCkSceneNodeGym_Frame LastFrame;
        TArray<int32> FirstFrameIssues;
        int32 ObservedFrames = 0;
        int32 PassiveWritesAfterInvalidation = 0;
        bool bSawInSample = false;
        bool bReentrantGetterRejected = false;
        bool bReentrantLossDestroyed = false;
        bool bFrameObservationFailed = false;
        FString FrameFailure;
        FTransform LaterPassivePose;
        bool bLaterPassiveCaptured = false;
        TArray<FTransform> FrozenVisiblePoses;
        TArray<FTransform> FrozenPassivePoses;
        TStrongObjectPtr<UCk_SceneNodeGym_Retirement_UE> Retirement;
        TArray<FCk_Handle> RetirementKnown;
        TArray<FCk_Handle> RetirementParents;
        TArray<TWeakObjectPtr<AActor>> RetirementActors;
        FCk_Handle ForeignChild;
        int64 RetirementLastFrameSerial = 0;
        int32 RetirementWorldTicks = 0;
        int32 RetirementTickAtStop = 0;
        TArray<int32> RetirementIssuedAtClose;
        FPose RetirementStoppedPose;
        TArray<FTransform> RetirementVisiblePoses;
        FDelegateHandle RetirementActorDestroyedObserver;
        TWeakObjectPtr<AActor> ReentrantUnexpectedActor;
        bool bReentrantRoleTwoDestroyed = false;
        bool bReentrantPrerequisitesMet = false;
        bool bReentrantUnknownSpawned = false;
        TArray<FTransform> ReentrantRemainingVisualPoses;
    };

    void Fail(FAutomationTestBase* Test, const TCHAR* Reason)
    {
        Test->AddError(Reason);
        ck::auto_test::net::FailSession(Reason);
    }

    bool SetupActorsAndRoots(FState& S, UWorld* World,
        UCk_SceneNodeGym_ActorLedger_UE* ProvidedLedger = nullptr,
        int32 ExpectedGeneration = 1)
    {
        if (!IsValid(World) || !IsValid(World->GetGameState()) ||
            !IsValid(ck::auto_test::net::Get_ClientWorld(0)))
        { return false; }
        S.World = World;
        S.Source = World->GetGameState();
        S.ExpectedGeneration = ExpectedGeneration;
        S.WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(S.WorldRoot)) { return false; }
        const auto* ProbeCDO = ACk_SceneNode_ActorLedgerProbe::StaticClass()
            ->GetDefaultObject<ACk_SceneNode_ActorLedgerProbe>();
        const auto* ScriptCDO = UCk_SceneNodeGym_WithActorScript_UE::StaticClass()
            ->GetDefaultObject<UCk_SceneNodeGym_WithActorScript_UE>();
        if (!IsValid(ProbeCDO) || !IsValid(ProbeCDO->Cube) ||
            !IsValid(ProbeCDO->Cube->GetStaticMesh()) ||
            !IsValid(ScriptCDO) || ProbeCDO->GetIsReplicated() ||
            ScriptCDO->Get_EffectiveReplication() != ECk_Replication::DoesNotReplicate)
        { return false; }
        TArray<TSubclassOf<AActor>> Classes;
        Classes.Init(ACk_SceneNode_ActorLedgerProbe::StaticClass(), 17);
        auto* Ledger = ProvidedLedger;
        if (Ledger == nullptr && ExpectedGeneration == 1)
        { Ledger = UCk_SceneNodeGym_ActorLedgerRuntime_UE::
            TryBeginActorGeneration(World->GetGameState(), 1, Classes); }
        if (!IsValid(Ledger) ||
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_Generation(Ledger) !=
                ExpectedGeneration ||
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) != 0 ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) ||
            !World->GetGameState()->GetInstanceComponents().Contains(Ledger))
        { return false; }
        S.Ledger = Ledger;
        AActor* Scope = nullptr;
        if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                Ledger, 0, FTransform::Identity, Scope) || !IsValid(Scope))
        { return false; }
        for (int32 Role = 1; Role <= 17; ++Role)
        {
            AActor* Actor = nullptr;
            if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TrySpawnConfiguredRole(
                    Ledger, Role, FTransform(FVector(Role * 10.0, 0, 0)), Actor) ||
                !IsValid(Actor) || Actor->GetOwner() != Scope ||
                Actor->GetClass() != ACk_SceneNode_ActorLedgerProbe::StaticClass())
            { return false; }
            auto* Probe = Cast<ACk_SceneNode_ActorLedgerProbe>(Actor);
            if (!IsValid(Probe) || !Probe->bObservedNativeClaimAtBeginPlay ||
                Probe->BeginPlayCount != 1 || !IsValid(Probe->Cube) ||
                !Probe->Cube->IsRegistered())
            { return false; }
            S.Visuals.Add(Probe);
        }
        if (S.Visuals.Num() != 17 ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true))
        { return false; }
        auto* Roots = UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
            Ledger, UCk_SceneNodeGym_WithActorScript_UE::StaticClass());
        S.Roots = Roots;
        return IsValid(Roots) &&
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsFailed(Roots) &&
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsUnproven(Roots);
    }

    bool CaptureRootsAndBeginGraph(FState& S)
    {
        auto* Roots = S.Roots.Get();
        if (!IsValid(Roots) ||
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Roots))
        { return false; }
        for (int32 I = 0; I < 6; ++I)
        {
            if (!UCk_SceneNodeGym_EcsRootsRuntime_UE::TryGet_CompletedRoot(
                    Roots, I, S.RootHandles[I]) ||
                !ck::IsValid(S.RootHandles[I]) ||
                !S.RootHandles[I].Has<ck::FFragment_Transform>())
            { return false; }
        }
        auto* Graph = UCk_SceneNodeGym_NodeGraphRuntime_UE::TryBegin(Roots);
        S.Graph = Graph;
        return IsValid(Graph) &&
            !UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsFailed(Graph) &&
            !UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsUnproven(Graph);
    }

    bool CaptureNodes(FState& S)
    {
        auto* Graph = S.Graph.Get();
        if (!IsValid(Graph) ||
            !UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(Graph))
        { return false; }
        for (int32 I = 0; I < 11; ++I)
        {
            FCk_Handle_SceneNode Node;
            if (!UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGet_CompletedNode(
                    Graph, I, Node) || !ck::IsValid(Node) ||
                !Node.Has_All<ck::FFragment_SceneNode, ck::FFragment_Transform>())
            { return false; }
            S.NodeHandles[I] = FCk_Handle(Node);
        }
        return true;
    }

    bool ReadPose(const FState& S, FPose& Out)
    {
        const auto Node = S.NodeHandles[1];
        const auto Root = S.RootHandles[2];
        auto* Actor = S.Visuals.IsValidIndex(ActorRole[2] - 1)
            ? S.Visuals[ActorRole[2] - 1].Get() : nullptr;
        if (!ck::IsValid(Node) ||
            !Node.Has_All<ck::FFragment_SceneNode, ck::FFragment_Transform>() ||
            !ck::IsValid(Root) || !Root.Has<ck::FFragment_Transform>() ||
            !IsValid(Actor) || !IsValid(Actor->Cube) ||
            !Actor->Cube->IsRegistered())
        { return false; }
        const auto TypedNode = UCk_Utils_SceneNode_UE::Cast(Node);
        Out.AnimatedOffset = UCk_Utils_SceneNode_UE::Get_Offset(TypedNode);
        Out.AnimatedWorld = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(Node));
        Out.RotatingRoot = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(Root));
        Out.VisibleCube = Actor->Cube->GetComponentTransform();
        return Out.AnimatedOffset.IsValid() && Out.AnimatedWorld.IsValid() &&
            Out.RotatingRoot.IsValid() &&
            Out.VisibleCube.IsValid();
    }

    bool SamePose(const FPose& A, const FPose& B)
    {
        return A.AnimatedOffset.Equals(B.AnimatedOffset, 0.0) &&
            A.AnimatedWorld.Equals(B.AnimatedWorld, 0.0) &&
            A.RotatingRoot.Equals(B.RotatingRoot, 0.0) &&
            A.VisibleCube.Equals(B.VisibleCube, 0.0);
    }

    bool MotionChanged(const FPose& Initial, const FPose& Actual)
    {
        const auto& A = Initial.AnimatedOffset;
        const auto& B = Actual.AnimatedOffset;
        return !A.GetLocation().Equals(B.GetLocation(), 0.0) &&
            !A.GetRotation().Equals(B.GetRotation(), 0.0) &&
            !A.GetScale3D().Equals(B.GetScale3D(), 0.0) &&
            !Initial.AnimatedWorld.Equals(Actual.AnimatedWorld, 0.0) &&
            !Initial.RotatingRoot.Equals(Actual.RotatingRoot, 0.0) &&
            !Initial.VisibleCube.Equals(Actual.VisibleCube, 0.0);
    }

    bool CaptureMotion(FState& S)
    {
        bool bClosed = false;
        bool bEverActive = false;
        int32 Generation = 0;
        TArray<FSlot> Slots;
        if (!FCk_SceneNodeMotion_TestAccess::TryCapture(
                S.Motion.Get(), Generation, bClosed, bEverActive, Slots) ||
            Generation != S.ExpectedGeneration || Slots.Num() != 6)
        { return false; }
        for (int32 I = 0; I < 6; ++I)
        {
            const auto& Slot = Slots[I];
            if (!Slot.bAllocationAttempted || !Slot.bBound ||
                Slot.Root != S.RootHandles[I] ||
                Slot.Raw == FCk_Handle{} || Slot.Timer == FCk_Handle_Timer{} ||
                FCk_Handle(Slot.Timer) != Slot.Raw || !ck::IsValid(Slot.Timer) ||
                !Slot.Timer.Has_All<ck::FFragment_LifetimeOwner,
                    ck::FFragment_Timer>() ||
                Slot.Timer.Get<ck::FFragment_LifetimeOwner>().Get_Entity() !=
                    S.RootHandles[I] ||
                UCk_Utils_Timer_UE::Get_CurrentState(Slot.Timer) !=
                    ECk_Timer_State::Paused ||
                Slot.IssuedMotionUpdateCount != 0 || Slot.bResumeIssued ||
                Slot.bResumeCallback)
            { return false; }
            for (int32 J = 0; J < I; ++J)
            { if (Slot.Raw == Slots[J].Raw) { return false; } }
        }
        S.Generation = Generation;
        S.Frozen = MoveTemp(Slots);
        return true;
    }

    bool RawIdentityRetained(const FState& S, TArray<FSlot>* Out = nullptr)
    {
        bool bClosed = false;
        bool bEverActive = false;
        int32 Generation = 0;
        TArray<FSlot> Slots;
        if (!FCk_SceneNodeMotion_TestAccess::TryCapture(
                S.Motion.Get(), Generation, bClosed, bEverActive, Slots) ||
            Generation != S.Generation || Slots.Num() != S.Frozen.Num())
        { return false; }
        for (int32 I = 0; I < Slots.Num(); ++I)
        {
            if (Slots[I].Root != S.Frozen[I].Root ||
                Slots[I].Raw != S.Frozen[I].Raw ||
                Slots[I].Timer != S.Frozen[I].Timer ||
                Slots[I].bAllocationAttempted != S.Frozen[I].bAllocationAttempted ||
                Slots[I].bBound != S.Frozen[I].bBound)
            { return false; }
        }
        if (Out != nullptr) { *Out = MoveTemp(Slots); }
        return true;
    }

    bool KnownQueuesDrained(const FState& S)
    {
        TArray<FCk_Handle> Known;
        Known.Reserve(23);
        for (const auto& Root : S.RootHandles) { Known.Add(Root); }
        for (const auto& Node : S.NodeHandles) { Known.Add(Node); }
        for (const auto& Slot : S.Frozen) { Known.Add(Slot.Raw); }
        return Known.Num() == 23 &&
            UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Known, S.WorldRoot);
    }

    bool AllSixQueuedResume(const FState& S)
    {
        TArray<FSlot> Slots;
        if (!RawIdentityRetained(S, &Slots) || Slots.Num() != 6)
        { return false; }
        for (const auto& Slot : Slots)
        {
            if (!Slot.bResumeIssued || !Slot.bResumeReturned ||
                Slot.ResumeReturn != Slot.Timer || !ck::IsValid(Slot.Timer) ||
                !UCk_Utils_Timer_UE::Has(Slot.Timer) ||
                !Slot.Timer.Has<ck::FFragment_Timer_Requests>() ||
                Slot.Timer.Get<ck::FFragment_Timer_Requests>()
                    .Get_Requests().Num() != 1)
            { return false; }
            const auto& Request = Slot.Timer.Get<ck::FFragment_Timer_Requests>()
                .Get_Requests()[0];
            const auto* Manipulate = std::get_if<FCk_Request_Timer_Manipulate>(&Request);
            if (Manipulate == nullptr || Manipulate->Get_Manipulate() !=
                ECk_Timer_Manipulate::Resume)
            { return false; }
        }
        return true;
    }

    bool AllSixResumeDrained(const FState& S)
    {
        TArray<FSlot> Slots;
        if (!RawIdentityRetained(S, &Slots) || Slots.Num() != 6)
        { return false; }
        for (const auto& Slot : Slots)
        {
            if (!ck::IsValid(Slot.Timer) ||
                !UCk_Utils_Timer_UE::Has(Slot.Timer) ||
                Slot.Timer.Has_Any<ck::FTag_Timer_NeedsSetup,
                    ck::FFragment_Timer_Requests>() ||
                UCk_Utils_Timer_UE::Get_CurrentState(Slot.Timer) !=
                    ECk_Timer_State::Running ||
                Slot.IssuedMotionUpdateCount != 0 || Slot.bResumeCallback)
            { return false; }
        }
        return true;
    }

    constexpr int32 SamplerNodeRole[11] =
        {2, 4, 6, 7, 8, 9, 11, 12, 14, 15, 17};

    bool IsClearedFrame(const FCkSceneNodeGym_Frame& Frame)
    {
        return Frame.Generation == 0 && Frame.FrameSerial == 0 &&
            Frame.WorldPoses.IsEmpty();
    }

    FCkSceneNodeGym_Frame PoisonedFrame()
    {
        FCkSceneNodeGym_Frame Frame;
        Frame.Generation = 71;
        Frame.FrameSerial = 89;
        Frame.WorldPoses.Add(FTransform::Identity);
        return Frame;
    }

    bool CaptureAllVisiblePoses(const FState& S, TArray<FTransform>& Out)
    {
        Out.Reset();
        if (S.Visuals.Num() != 17) { return false; }
        TArray<FTransform> Candidate;
        Candidate.Reserve(17);
        for (const auto& Weak : S.Visuals)
        {
            const auto* Actor = Weak.Get();
            if (!IsValid(Actor) || !IsValid(Actor->Cube) ||
                !Actor->Cube->IsRegistered())
            { return false; }
            const FTransform Pose = Actor->Cube->GetComponentTransform();
            if (!Pose.IsValid()) { return false; }
            Candidate.Add(Pose);
        }
        Out = MoveTemp(Candidate);
        return true;
    }

    bool SameAllVisiblePoses(const FState& S, const TArray<FTransform>& Frozen)
    {
        TArray<FTransform> Actual;
        if (!CaptureAllVisiblePoses(S, Actual) || Frozen.Num() != 17)
        { return false; }
        for (int32 I = 0; I < 17; ++I)
        { if (!Actual[I].Equals(Frozen[I], 0.0)) { return false; } }
        return true;
    }

    bool CaptureRemainingPassivePoses(const FState& S, int32 ExcludedA,
        int32 ExcludedB, TArray<FTransform>& Out)
    {
        Out.Reset();
        TArray<FTransform> Candidate;
        Candidate.Reserve(11);
        for (const int32 Role : SamplerNodeRole)
        {
            if (Role == ExcludedA || Role == ExcludedB)
            { Candidate.Add(FTransform::Identity); continue; }
            auto* Actor = S.Visuals.IsValidIndex(Role - 1)
                ? S.Visuals[Role - 1].Get() : nullptr;
            if (!IsValid(Actor) || !IsValid(Actor->Cube) ||
                !Actor->Cube->IsRegistered())
            { return false; }
            const FTransform Pose = Actor->Cube->GetComponentTransform();
            if (!Pose.IsValid()) { return false; }
            Candidate.Add(Pose);
        }
        Out = MoveTemp(Candidate);
        return Out.Num() == 11;
    }

    bool SameRemainingPassivePoses(const FState& S, int32 ExcludedA,
        int32 ExcludedB, const TArray<FTransform>& Frozen)
    {
        TArray<FTransform> Actual;
        if (Frozen.Num() != 11 ||
            !CaptureRemainingPassivePoses(S, ExcludedA, ExcludedB, Actual))
        { return false; }
        for (int32 I = 0; I < 11; ++I)
        { if (!Actual[I].Equals(Frozen[I], 0.0)) { return false; } }
        return true;
    }

    void RemoveSamplerObservers(FState& S)
    {
        if (S.FrameObserver.IsValid())
        {
            FWorldDelegates::OnWorldTickEnd.Remove(S.FrameObserver);
            S.FrameObserver.Reset();
        }
        if (S.ComponentObserver.IsValid())
        {
            auto* Component = S.ObservedComponent.Get();
            if (IsValid(Component))
            { Component->TransformUpdated.Remove(S.ComponentObserver); }
            S.ComponentObserver.Reset();
            S.ObservedComponent.Reset();
        }
    }

    void RemoveRetirementActorObserver(FState& S)
    {
        if (!S.RetirementActorDestroyedObserver.IsValid()) { return; }
        if (auto* World = S.World.Get())
        { World->RemoveOnActorDestroyedHandler(S.RetirementActorDestroyedObserver); }
        S.RetirementActorDestroyedObserver.Reset();
    }

    void ObserveCurrentSamplerFrame(FState& S, UWorld* World)
    {
        if (S.bFrameObservationFailed || World != S.World.Get() ||
            !S.Sampler.IsValid())
        { return; }
        uint64 Published = 0;
        int64 Serial = 0;
        bool bSampling = false;
        bool bClosed = true;
        if (!FCk_SceneNodeFrameSampler_TestAccess::TryCapture(
                S.Sampler.Get(), Published, Serial, bSampling, bClosed) ||
            bSampling || bClosed || Published != GFrameCounter ||
            Serial <= S.ObservedFrameSerial)
        { return; } // multicast order is not assumed
        auto Reject = [&S](const TCHAR* Reason)
        {
            S.bFrameObservationFailed = true;
            S.FrameFailure = Reason;
        };
        FCkSceneNodeGym_Frame Frame;
        if (!UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                S.Sampler.Get(), Frame) ||
            Frame.Generation != S.ExpectedGeneration ||
            Frame.FrameSerial != Serial || Frame.WorldPoses.Num() != 17)
        { Reject(TEXT("current-frame value receipt was incomplete")); return; }
        for (int32 Role = 1; Role <= 17; ++Role)
        {
            auto* Actor = S.Visuals.IsValidIndex(Role - 1)
                ? S.Visuals[Role - 1].Get() : nullptr;
            if (!IsValid(Actor) || !IsValid(Actor->Cube) ||
                !Actor->Cube->IsRegistered() ||
                !Frame.WorldPoses[Role - 1].IsValid() ||
                !Frame.WorldPoses[Role - 1].Equals(
                    Actor->Cube->GetComponentTransform(), 0.0))
            { Reject(TEXT("receipt pose differed from an actual registered visual")); return; }
        }
        for (int32 I = 0; I < 11; ++I)
        {
            const auto Node = S.NodeHandles[I];
            if (!ck::IsValid(Node) || !Node.Has<ck::FFragment_Transform>())
            { Reject(TEXT("source node disappeared during current-frame observation")); return; }
            const FTransform Solved = UCk_Utils_Transform_UE::
                Get_EntityCurrentTransform(UCk_Utils_Transform_UE::Cast(Node));
            if (!Solved.IsValid() || !Frame.WorldPoses[SamplerNodeRole[I] - 1]
                .Equals(Solved, 0.01))
            { Reject(TEXT("receipt passive pose differed from current solved node")); return; }
        }
        TArray<FSlot> Slots;
        if (!RawIdentityRetained(S, &Slots) || Slots.Num() != 6)
        { Reject(TEXT("source timer identities changed during sample")); return; }
        for (const auto& Slot : Slots)
        {
            if (Slot.IssuedMotionUpdateCount < 1)
            { Reject(TEXT("frame published without actual timer-issued motion")); return; }
        }
        if (S.ObservedFrames == 0)
        {
            S.FirstFrame = Frame;
            S.FirstFrameIssues.Reset(6);
            for (const auto& Slot : Slots)
            { S.FirstFrameIssues.Add(Slot.IssuedMotionUpdateCount); }
        }
        else if (S.ObservedFrames == 1)
        {
            if (GFrameCounter <= S.ObservedEngineFrame ||
                S.FirstFrameIssues.Num() != 6)
            { Reject(TEXT("second receipt was not from a later engine frame")); return; }
            for (int32 I = 0; I < 6; ++I)
            {
                if (Slots[I].IssuedMotionUpdateCount <= S.FirstFrameIssues[I])
                { Reject(TEXT("a producer did not issue motion across receipt frames")); return; }
            }
        }
        S.LastFrame = MoveTemp(Frame);
        S.ObservedFrameSerial = Serial;
        S.ObservedEngineFrame = GFrameCounter;
        ++S.ObservedFrames;
    }

    bool CollectRetirementCreators(const FState& S,
        TArray<FCk_Handle>& OutKnown, TArray<FCk_Handle>& OutParents,
        bool bRequireCurrentAllocation = true)
    {
        OutKnown.Reset();
        OutParents.Reset();
        FCk_Handle RecordedWorld;
        FCk_Handle Owner;
        TArray<FCk_SceneNodeEcsRoots_TestAccess::FSlot> RootSlots;
        TArray<FCk_SceneNodeNodeGraph_TestAccess::FSlot> NodeSlots;
        int32 GraphGeneration = 0;
        if (!FCk_SceneNodeEcsRoots_TestAccess::TryCapture(S.Roots.Get(),
                RecordedWorld, Owner, RootSlots) || RecordedWorld != S.WorldRoot ||
            !FCk_SceneNodeNodeGraph_TestAccess::TryCapture(S.Graph.Get(),
                GraphGeneration, NodeSlots) || GraphGeneration != S.Generation ||
            RootSlots.Num() != 6 || NodeSlots.Num() != 11 ||
            S.Frozen.Num() != 6 || Owner == FCk_Handle{})
        { return false; }
        auto Add = [&OutKnown, &OutParents](const FCk_Handle& H,
            const FCk_Handle& Parent)
        { OutKnown.Add(H); OutParents.Add(Parent); };
        Add(Owner, S.WorldRoot);
        for (int32 I = 0; I < 6; ++I)
        {
            if (!RootSlots[I].bAllocationAttempted || !RootSlots[I].bIssued ||
                !RootSlots[I].bReturned || !RootSlots[I].bCallback ||
                !RootSlots[I].bSucceeded || RootSlots[I].Target != S.RootHandles[I] ||
                RootSlots[I].Carrier == FCk_Handle{})
            { return false; }
            Add(RootSlots[I].Target, Owner);
        }
        for (int32 I = 0; I < 6; ++I)
        { Add(RootSlots[I].Carrier, RootSlots[I].Target); }
        for (int32 I = 0; I < 11; ++I)
        {
            if (!NodeSlots[I].bAllocationAttempted || !NodeSlots[I].bComposed ||
                NodeSlots[I].Raw != S.NodeHandles[I])
            { return false; }
            Add(NodeSlots[I].Raw, NodeSlots[I].Parent);
        }
        for (int32 I = 0; I < 6; ++I)
        { Add(S.Frozen[I].Raw, S.RootHandles[I]); }
        if (OutKnown.Num() != 30 || OutParents.Num() != 30)
        { return false; }
        for (int32 I = 0; I < 30; ++I)
        {
            if (OutKnown[I] == FCk_Handle{} ||
                OutParents[I] == FCk_Handle{} ||
                OutKnown[I] == OutParents[I])
            { return false; }
            for (int32 J = 0; J < I; ++J)
            { if (OutKnown[I] == OutKnown[J]) { return false; } }
            if (!bRequireCurrentAllocation) { continue; }
            const bool Allocated = UCk_EntityScriptGym_Spawn_Runtime_UE::
                Get_IsEntityAllocated(OutKnown[I]);
            if ((I >= 7 && I < 13) ? Allocated : !Allocated)
            { return false; }
            if (Allocated && (!OutKnown[I].Has<ck::FFragment_LifetimeOwner>() ||
                OutKnown[I].Get<ck::FFragment_LifetimeOwner>().Get_Entity() !=
                    OutParents[I]))
            { return false; }
        }
        return true;
    }

    bool FreezeRetirementInputs(FState& S)
    {
        if (!CollectRetirementCreators(S, S.RetirementKnown,
                S.RetirementParents) || S.Visuals.Num() != 17 ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                S.Ledger.Get(), true))
        { return false; }
        S.RetirementActors.Reset(18);
        for (int32 Role = 0; Role < 18; ++Role)
        {
            AActor* Actor = nullptr;
            if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                    S.Ledger.Get(), Role, Actor) || !IsValid(Actor) ||
                Actor->GetWorld() != S.World.Get() ||
                (Role > 0 && Actor != S.Visuals[Role - 1].Get()))
            { return false; }
            S.RetirementActors.Add(Actor);
        }
        S.RetirementLastFrameSerial = S.LastFrame.FrameSerial;
        return S.RetirementActors.Num() == 18 &&
            S.RetirementLastFrameSerial > S.FirstFrame.FrameSerial;
    }

    bool SameRetirementCreators(const FState& S)
    {
        TArray<FCk_Handle> Known;
        TArray<FCk_Handle> Parents;
        if (!CollectRetirementCreators(S, Known, Parents, false) ||
            Known != S.RetirementKnown || Parents != S.RetirementParents)
        { return false; }
        return true;
    }

    bool AllRetirementEntitiesGone(const FState& S)
    {
        if (S.RetirementKnown.Num() != 30) { return false; }
        for (const auto& H : S.RetirementKnown)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H))
          { return false; } }
        return true;
    }

    bool HasReadableRetirementWorldLevels(const UWorld* World)
    {
        if (!IsInGameThread() || !IsValid(World) ||
            World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            World->bIsTearingDown || !World->IsGameWorld() ||
            !IsValid(World->PersistentLevel) ||
            World->PersistentLevel->HasAnyFlags(
                RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        for (const ULevel* Level : World->GetLevels())
        { if (!IsValid(Level) || Level->HasAnyFlags(
                RF_BeginDestroyed | RF_FinishDestroyed))
          { return false; } }
        return true;
    }

    TArray<TSubclassOf<AActor>> MakeSuccessorVisualClasses()
    {
        TArray<TSubclassOf<AActor>> Classes;
        Classes.Init(ACk_SceneNode_ActorLedgerProbe::StaticClass(), 17);
        return Classes;
    }

    bool CaptureSuccessorAdmissionCensus(const FState& S,
        TArray<FCk_Handle>& OutWorldChildren, int32& OutActorSlots,
        int32& OutInstanceComponents)
    {
        OutWorldChildren.Reset();
        OutActorSlots = 0;
        OutInstanceComponents = 0;
        auto* const World = S.World.Get();
        auto* const Source = S.Source.Get();
        if (!HasReadableRetirementWorldLevels(World) || !IsValid(Source) ||
            Source->GetWorld() != World || !ck::IsValid(S.WorldRoot) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryGet_AllocatedDirectLifetimeDependents(S.WorldRoot,
                    S.WorldRoot, OutWorldChildren))
        { return false; }
        auto CountLevel = [&OutActorSlots](const ULevel* Level)
        { for (const TObjectPtr<AActor>& Entry : Level->Actors)
          { OutActorSlots += Entry.Get() != nullptr ? 1 : 0; } };
        CountLevel(World->PersistentLevel);
        for (const ULevel* Level : World->GetLevels())
        { if (Level != World->PersistentLevel) { CountLevel(Level); } }
        OutInstanceComponents = Source->GetInstanceComponents().Num();
        return true;
    }

    bool ActorSerialInWorld(const UWorld* World,
        const TWeakObjectPtr<AActor>& Frozen)
    {
        if (!HasReadableRetirementWorldLevels(World))
        { return false; }
        auto ContainsFrozen = [&Frozen](const ULevel* Level)
        {
            for (const TObjectPtr<AActor>& Entry : Level->Actors)
            {
                AActor* const Raw = Entry.Get();
                if (Raw == nullptr) { continue; }
                const TWeakObjectPtr<AActor> Current(Raw);
                if (Frozen.HasSameIndexAndSerialNumber(Current))
                { return true; }
            }
            return false;
        };
        if (ContainsFrozen(World->PersistentLevel)) { return true; }
        for (const ULevel* Level : World->GetLevels())
        { if (Level != World->PersistentLevel && ContainsFrozen(Level))
          { return true; } }
        return false;
    }

    bool AllRetirementActorsWorldAbsent(const FState& S)
    {
        if (!HasReadableRetirementWorldLevels(S.World.Get()) ||
            S.RetirementActors.Num() != 18)
        { return false; }
        for (const auto& Frozen : S.RetirementActors)
        { if (ActorSerialInWorld(S.World.Get(), Frozen)) { return false; } }
        return true;
    }

    bool AllRetirementRootActorsGone(const FState& S)
    {
        if (!HasReadableRetirementWorldLevels(S.World.Get()) ||
            S.RetirementActors.Num() != 18)
        { return false; }
        constexpr int32 RootRoles[6] = {1, 3, 5, 10, 13, 16};
        for (const int32 Role : RootRoles)
        { if (ActorSerialInWorld(S.World.Get(), S.RetirementActors[Role]))
          { return false; } }
        return true;
    }

    bool ReentrantResidualActorsSurvive(const FState& S)
    {
        if (!IsValid(S.World.Get()) || S.RetirementActors.Num() != 18 ||
            !S.bReentrantRoleTwoDestroyed ||
            ActorSerialInWorld(S.World.Get(), S.RetirementActors[2]))
        { return false; }
        constexpr int32 RootRoles[6] = {1, 3, 5, 10, 13, 16};
        for (const int32 Role : RootRoles)
        { if (ActorSerialInWorld(S.World.Get(), S.RetirementActors[Role]))
          { return false; } }
        for (int32 Role = 0; Role < 18; ++Role)
        {
            if (Role == 2) { continue; }
            bool bRoot = false;
            for (const int32 RootRole : RootRoles)
            { bRoot |= Role == RootRole; }
            if (bRoot) { continue; }
            auto* const Actor = S.RetirementActors[Role].Get();
            if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
                !ActorSerialInWorld(S.World.Get(), S.RetirementActors[Role]))
            { return false; }
        }
        auto* const Scope = S.RetirementActors[0].Get();
        auto* const Unknown = S.ReentrantUnexpectedActor.Get();
        return IsValid(Scope) && IsValid(Unknown) &&
            !Unknown->IsActorBeingDestroyed() &&
            Unknown->GetWorld() == S.World.Get() &&
            Unknown->GetOwner() == Scope &&
            ActorSerialInWorld(S.World.Get(), S.ReentrantUnexpectedActor) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(
                S.Ledger.Get()) == 18;
    }

    bool OnlyRoleTwoDestroyRequested(const FState& S)
    {
        TArray<bool> Requested;
        if (!FCk_SceneNodeRetirement_TestAccess::TryCaptureDestroyRequested(
                S.Retirement.Get(), Requested) || Requested.Num() != 18)
        { return false; }
        for (int32 Role = 0; Role < 18; ++Role)
        { if (Requested[Role] != (Role == 2)) { return false; } }
        return true;
    }

    bool CaptureReentrantRemainingVisualPoses(const FState& S,
        TArray<FTransform>& Out)
    {
        Out.Reset();
        if (S.Visuals.Num() != 17) { return false; }
        constexpr int32 RootRoles[6] = {1, 3, 5, 10, 13, 16};
        TArray<FTransform> Candidate;
        Candidate.Reserve(10);
        for (int32 Role = 1; Role <= 17; ++Role)
        {
            if (Role == 2) { continue; }
            bool bRoot = false;
            for (const int32 RootRole : RootRoles)
            { bRoot |= Role == RootRole; }
            if (bRoot) { continue; }
            auto* Actor = S.Visuals[Role - 1].Get();
            if (!IsValid(Actor) || !IsValid(Actor->Cube) ||
                !Actor->Cube->IsRegistered())
            { return false; }
            const FTransform Pose = Actor->Cube->GetComponentTransform();
            if (!Pose.IsValid()) { return false; }
            Candidate.Add(Pose);
        }
        if (Candidate.Num() != 10) { return false; }
        Out = MoveTemp(Candidate);
        return true;
    }

    bool SameReentrantRemainingVisualPoses(const FState& S,
        const TArray<FTransform>& Frozen)
    {
        TArray<FTransform> Actual;
        if (Frozen.Num() != 10 ||
            !CaptureReentrantRemainingVisualPoses(S, Actual))
        { return false; }
        for (int32 I = 0; I < 10; ++I)
        { if (!Actual[I].Equals(Frozen[I], 0.0)) { return false; } }
        return true;
    }

    bool AllOriginalRetirementMembersSurvive(const FState& S)
    {
        if (S.RetirementKnown.Num() != 30 ||
            S.RetirementActors.Num() != 18)
        { return false; }
        for (int32 I = 0; I < 30; ++I)
        {
            const bool Allocated = UCk_EntityScriptGym_Spawn_Runtime_UE::
                Get_IsEntityAllocated(S.RetirementKnown[I]);
            if ((I >= 7 && I < 13) ? Allocated : !Allocated)
            { return false; }
        }
        for (int32 Role = 0; Role < 18; ++Role)
        {
            AActor* Actor = nullptr;
            if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                    S.Ledger.Get(), Role, Actor) || !IsValid(Actor) ||
                !S.RetirementActors[Role].HasSameIndexAndSerialNumber(
                    TWeakObjectPtr<AActor>(Actor)) ||
                Actor->GetWorld() != S.World.Get() ||
                Actor->IsActorBeingDestroyed())
            { return false; }
        }
        return true;
    }

    bool ForeignChildSurvives(const FState& S)
    {
        return S.ForeignChild != FCk_Handle{} &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                S.ForeignChild) &&
            S.ForeignChild.Has<ck::FFragment_LifetimeOwner>() &&
            S.ForeignChild.Get<ck::FFragment_LifetimeOwner>()
                .Get_Entity() == S.NodeHandles[0];
    }

    bool StartRetirementTickCounter(const TSharedPtr<FState>& S)
    {
        if (!S.IsValid() || S->FrameObserver.IsValid()) { return false; }
        const TWeakPtr<FState> WeakS = S;
        S->FrameObserver = FWorldDelegates::OnWorldTickEnd.AddLambda(
            [WeakS](UWorld* World, ELevelTick Tick, float)
        {
            const auto State = WeakS.Pin();
            if (State.IsValid() && Tick == LEVELTICK_All &&
                World == State->World.Get())
            { ++State->RetirementWorldTicks; }
        });
        return S->FrameObserver.IsValid();
    }

    bool BindReentrantUnknownActor(const TSharedPtr<FState>& S)
    {
        auto* World = S.IsValid() ? S->World.Get() : nullptr;
        if (!IsValid(World) || S->RetirementActors.Num() != 18 ||
            S->RetirementActorDestroyedObserver.IsValid())
        { return false; }
        const TWeakPtr<FState> WeakS = S;
        S->RetirementActorDestroyedObserver = World->AddOnActorDestroyedHandler(
            FOnActorDestroyed::FDelegate::CreateLambda([WeakS](AActor* Destroyed)
        {
            const auto State = WeakS.Pin();
            if (!State.IsValid() || State->bReentrantRoleTwoDestroyed ||
                Destroyed == nullptr || State->RetirementActors.Num() != 18 ||
                !State->RetirementActors[2].HasSameIndexAndSerialNumber(
                    TWeakObjectPtr<AActor>(Destroyed)))
            { return; }
            State->bReentrantRoleTwoDestroyed = true;
            State->bReentrantPrerequisitesMet =
                State->Retirement.IsValid() &&
                UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                    State->Retirement.Get()) ==
                    ECk_SceneNodeGym_RetirementState::Pending &&
                RetirementReceiptMatchesFrozen(*State, true) &&
                AllRetirementEntitiesGone(*State) &&
                AllRetirementRootActorsGone(*State);
            if (!State->bReentrantPrerequisitesMet) { return; }
            auto* Scope = State->RetirementActors[0].Get();
            auto* SourceWorld = State->World.Get();
            if (!IsValid(Scope) || !IsValid(SourceWorld) ||
                Scope->IsActorBeingDestroyed() ||
                !ActorSerialInWorld(SourceWorld, State->RetirementActors[0]))
            { return; }
            FActorSpawnParameters Params;
            Params.Owner = Scope;
            auto* Unexpected = SourceWorld->SpawnActor<AActor>(
                AActor::StaticClass(), FTransform::Identity, Params);
            State->ReentrantUnexpectedActor = Unexpected;
            State->bReentrantUnknownSpawned = IsValid(Unexpected) &&
                Unexpected->GetOwner() == Scope &&
                Unexpected->GetWorld() == SourceWorld;
        }));
        return S->RetirementActorDestroyedObserver.IsValid();
    }

    bool RetirementReceiptMatchesFrozen(const FState& S,
        bool bRequireQuiescentDestroy)
    {
        TArray<FCk_Handle> Known;
        TArray<FCk_Handle> Parents;
        TArray<TWeakObjectPtr<AActor>> Actors;
        bool bQuiescenceObserved = false;
        bool bDestroyIssued = false;
        if (!FCk_SceneNodeRetirement_TestAccess::TryCapture(
                S.Retirement.Get(), Known, Parents, Actors,
                bQuiescenceObserved, bDestroyIssued) ||
            Known != S.RetirementKnown || Parents != S.RetirementParents ||
            Actors.Num() != S.RetirementActors.Num())
        { return false; }
        for (int32 I = 0; I < Actors.Num(); ++I)
        {
            if (!Actors[I].HasSameIndexAndSerialNumber(S.RetirementActors[I]))
            { return false; }
        }
        return bRequireQuiescentDestroy
            ? bQuiescenceObserved && bDestroyIssued
            : !bQuiescenceObserved && !bDestroyIssued;
    }

    bool RejectSuccessorWithoutMutation(const FState& S,
        AGameStateBase* ExpectedSource,
        UCk_SceneNodeGym_Retirement_UE* CandidateReceipt,
        const TArray<TSubclassOf<AActor>>& Classes)
    {
        if (!S.Retirement.IsValid() || !IsValid(S.Ledger.Get()) ||
            !IsValid(S.Source.Get()))
        { return false; }
        TArray<FCk_Handle> BeforeChildren;
        TArray<FCk_Handle> AfterChildren;
        int32 BeforeActors = 0;
        int32 AfterActors = 0;
        int32 BeforeComponents = 0;
        int32 AfterComponents = 0;
        if (!CaptureSuccessorAdmissionCensus(S, BeforeChildren,
                BeforeActors, BeforeComponents))
        { return false; }
        auto* const OldLedger = S.Ledger.Get();
        const auto BeforeState =
            UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                S.Retirement.Get());
        const FName BeforeReason =
            UCk_SceneNodeGym_RetirementRuntime_UE::Get_FailureReason(
                S.Retirement.Get());
        if (UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginSuccessor(
                ExpectedSource, CandidateReceipt, Classes) != nullptr ||
            !CaptureSuccessorAdmissionCensus(S, AfterChildren,
                AfterActors, AfterComponents))
        { return false; }
        return BeforeChildren == AfterChildren &&
            BeforeActors == AfterActors &&
            BeforeComponents == AfterComponents &&
            S.Ledger.Get() == OldLedger &&
            S.Source.Get()->GetInstanceComponents().Contains(OldLedger) &&
            UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                S.Retirement.Get()) == BeforeState &&
            UCk_SceneNodeGym_RetirementRuntime_UE::Get_FailureReason(
                S.Retirement.Get()) == BeforeReason &&
            SameRetirementCreators(S);
    }

    enum class ESamplerCase : uint8
    {
        Production,
        ReentrantActorLoss,
        UnregisteredChild
    };

    void RunSampler(FAutomationTestBase* Test, const FString& Parameters,
        ESamplerCase Case)
    {
        const auto S = MakeShared<FState>();
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
            2, TEXT("/Engine/Maps/Entry"), Parameters));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            const auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            return IsValid(Server) && IsValid(Client) &&
                IsValid(Server->GetGameState()) &&
                Server->GetGameState()->HasActorBegunPlay() &&
                IsValid(Client->GetGameState());
        }), 15.0, TEXT("FrameSampler authority and client worlds become ready")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (!SetupActorsAndRoots(*S, World))
            { Fail(Test, TEXT("FrameSampler could not construct real actors and ECS roots")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (IsValid(S->Roots.Get()) &&
                 UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(
                     S->Roots.Get()));
        }), 20.0, TEXT("FrameSampler six real WithActor roots complete")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureRootsAndBeginGraph(*S))
            { Fail(Test, TEXT("FrameSampler eleven-node graph admission failed")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (IsValid(S->Graph.Get()) &&
                 UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(
                     S->Graph.Get()));
        }), 20.0, TEXT("FrameSampler eleven nodes settle")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureNodes(*S) ||
                !ReadPose(*S, S->DormantPose))
            { Fail(Test, TEXT("FrameSampler could not capture dormant real poses")); return; }
            S->Motion.Reset(UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(
                S->Graph.Get()));
            if (!S->Motion.IsValid() ||
                UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(S->Motion.Get()))
            { Fail(Test, TEXT("FrameSampler six paused producers failed admission")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (S->Motion.IsValid() &&
                 UCk_SceneNodeGym_MotionRuntime_UE::Get_IsDormantReady(
                     S->Motion.Get()));
        }), 20.0, TEXT("FrameSampler six producers become dormant")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureMotion(*S) ||
                !UCk_SceneNodeGym_MotionRuntime_UE::TryActivateOnce(S->Motion.Get()))
            { Fail(Test, TEXT("FrameSampler genuine six-Resume admission failed")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (S->Motion.IsValid() &&
                 UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(S->Motion.Get()));
        }), 20.0, TEXT("FrameSampler genuine six Resume receipts become active")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            TArray<FSlot> Slots;
            if (!RawIdentityRetained(*S, &Slots) || Slots.Num() != 6)
            { return false; }
            for (const auto& Slot : Slots)
            { if (Slot.IssuedMotionUpdateCount < 1) { return false; } }
            return true;
        }), 20.0, TEXT("all six authored motion callbacks issue before sampling")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !RawIdentityRetained(*S))
            { Fail(Test, TEXT("FrameSampler source identities changed before begin")); return; }
            S->Sampler.Reset(UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                S->Motion.Get()));
            if (!S->Sampler.IsValid())
            { Fail(Test, TEXT("FrameSampler actual active source rejected")); return; }
            FCkSceneNodeGym_Frame Empty = PoisonedFrame();
            if (UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                    S->Sampler.Get(), Empty) || !IsClearedFrame(Empty) ||
                UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                    S->Motion.Get()) != nullptr)
            { Fail(Test, TEXT("FrameSampler published before post-tick or admitted retry")); return; }
            const TWeakPtr<FState> WeakS = S;
            S->FrameObserver = FWorldDelegates::OnWorldTickEnd.AddLambda(
                [WeakS](UWorld* Observed, ELevelTick Tick, float)
            {
                const auto State = WeakS.Pin();
                if (State.IsValid() && Tick == LEVELTICK_All)
                { ObserveCurrentSamplerFrame(*State, Observed); }
            });
            if (!S->FrameObserver.IsValid())
            { Fail(Test, TEXT("FrameSampler post-tick observer could not bind")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                S->bFrameObservationFailed || S->ObservedFrames >= 2;
        }), 20.0, TEXT("two actual current-frame complete seventeen-pose receipts")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S, Case](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            FPose Actual;
            if (World != S->World.Get() || S->bFrameObservationFailed ||
                S->ObservedFrames < 2 || S->FirstFrame.FrameSerial <= 0 ||
                S->LastFrame.FrameSerial <= S->FirstFrame.FrameSerial ||
                !ReadPose(*S, Actual) || !MotionChanged(S->DormantPose, Actual) ||
                !RawIdentityRetained(*S))
            {
                Fail(Test, S->bFrameObservationFailed
                    ? *S->FrameFailure : TEXT("FrameSampler lacked two complete moving frames"));
                return;
            }
            RemoveSamplerObservers(*S); // observation phase is complete
            if (Case == ESamplerCase::Production)
            {
                if (!UCk_SceneNodeGym_FrameSamplerRuntime_UE::Request_Close(
                        S->Sampler.Get()))
                { Fail(Test, TEXT("FrameSampler explicit close rejected")); return; }
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty) ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                        S->Motion.Get()) != nullptr)
                { Fail(Test, TEXT("closed FrameSampler exposed a frame or admitted retry")); }
                return;
            }
            auto* Role2 = S->Visuals.IsValidIndex(1) ? S->Visuals[1].Get() : nullptr;
            if (!IsValid(Role2) || !IsValid(Role2->Cube) ||
                !Role2->Cube->IsRegistered())
            { Fail(Test, TEXT("FrameSampler role-two visual was not live")); return; }
            if (Case == ESamplerCase::ReentrantActorLoss)
            {
                S->ObservedComponent = Role2->Cube;
                const TWeakPtr<FState> WeakS = S;
                S->ComponentObserver = Role2->Cube->TransformUpdated.AddLambda(
                    [WeakS](USceneComponent* Changed, EUpdateTransformFlags,
                        ETeleportType)
                {
                    const auto S = WeakS.Pin();
                    if (!S.IsValid()) { return; }
                    if (S->bSawInSample || !S->Sampler.IsValid() ||
                        Changed != S->ObservedComponent.Get())
                    { return; }
                    uint64 Published = 0;
                    int64 Serial = 0;
                    bool bSampling = false;
                    bool bClosed = true;
                    if (!FCk_SceneNodeFrameSampler_TestAccess::TryCapture(
                            S->Sampler.Get(), Published, Serial, bSampling, bClosed) ||
                        !bSampling || bClosed)
                    { return; }
                    S->bSawInSample = true;
                    FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                    S->bReentrantGetterRejected =
                        !UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                            S->Sampler.Get(), Empty) && IsClearedFrame(Empty);
                    auto* LaterPassive = S->Visuals.IsValidIndex(5)
                        ? S->Visuals[5].Get() : nullptr;
                    if (IsValid(LaterPassive) && IsValid(LaterPassive->Cube) &&
                        LaterPassive->Cube->IsRegistered())
                    {
                        S->LaterPassivePose =
                            LaterPassive->Cube->GetComponentTransform();
                        S->bLaterPassiveCaptured =
                            S->LaterPassivePose.IsValid();
                    }
                    S->bLaterPassiveCaptured = S->bLaterPassiveCaptured &&
                        CaptureRemainingPassivePoses(*S, 2, 4,
                            S->FrozenPassivePoses);
                    auto* Later = S->Visuals.IsValidIndex(3)
                        ? S->Visuals[3].Get() : nullptr;
                    S->bReentrantLossDestroyed = IsValid(Later) && Later->Destroy();
                });
            }
            else
            {
                auto* Role4 = S->Visuals.IsValidIndex(3)
                    ? S->Visuals[3].Get() : nullptr;
                if (!IsValid(Role4) || !IsValid(Role4->Cube) ||
                    !Role4->Cube->IsRegistered())
                { Fail(Test, TEXT("later passive visual missing before unregister")); return; }
                S->ObservedComponent = Role4->Cube;
                const TWeakPtr<FState> WeakS = S;
                S->ComponentObserver = Role4->Cube->TransformUpdated.AddLambda(
                    [WeakS](USceneComponent*, EUpdateTransformFlags, ETeleportType)
                    {
                        const auto S = WeakS.Pin();
                        if (S.IsValid()) { ++S->PassiveWritesAfterInvalidation; }
                    });
                S->LaterPassivePose = Role4->Cube->GetComponentTransform();
                S->bLaterPassiveCaptured = S->LaterPassivePose.IsValid() &&
                    CaptureRemainingPassivePoses(*S, 2, 0,
                        S->FrozenPassivePoses);
                Role2->Cube->UnregisterComponent();
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (Role2->Cube->IsRegistered() ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty))
                { Fail(Test, TEXT("unregistered child remained readable before next sample")); }
            }
            if (!S->ComponentObserver.IsValid())
            { Fail(Test, TEXT("FrameSampler component observer could not bind")); }
        })));
        if (Case == ESamplerCase::Production)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
                        S->Motion.Get()) && KnownQueuesDrained(*S));
            }), 20.0, TEXT("closed sampler obtains six real Stop receipts and drains known queues")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                TArray<FSlot> Slots;
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !ReadPose(*S, S->StoppedPose) ||
                    !RawIdentityRetained(*S, &Slots) || !KnownQueuesDrained(*S) ||
                    !CaptureAllVisiblePoses(*S, S->FrozenVisiblePoses))
                { Fail(Test, TEXT("stopped sampler lost current queue or pose evidence")); return; }
                S->IssuedAtClose.Reset(6);
                for (const auto& Slot : Slots)
                { S->IssuedAtClose.Add(Slot.IssuedMotionUpdateCount); }
                S->StableFrame = GFrameCounter;
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            { return ck::auto_test::net::Get_HasSessionFailed() ||
                GFrameCounter >= S->StableFrame + 2; }),
                15.0, TEXT("two later frames after sampler closure")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                FPose Later;
                TArray<FSlot> Slots;
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !ReadPose(*S, Later) ||
                    !SamePose(S->StoppedPose, Later) || !KnownQueuesDrained(*S) ||
                    !SameAllVisiblePoses(*S, S->FrozenVisiblePoses) ||
                    !RawIdentityRetained(*S, &Slots) ||
                    Slots.Num() != 6 || S->IssuedAtClose.Num() != 6 ||
                    !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(S->Motion.Get()) ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty))
                { Fail(Test, TEXT("closed sampler wrote poses or exposed a later frame")); return; }
                for (int32 I = 0; I < 6; ++I)
                { if (Slots[I].IssuedMotionUpdateCount != S->IssuedAtClose[I])
                  { Fail(Test, TEXT("sampler producer issued motion after close")); return; } }
            })));
        }
        else
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S, Case]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (Case == ESamplerCase::ReentrantActorLoss && S->bSawInSample) ||
                    (S->Sampler.IsValid() &&
                     !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(
                         S->Motion.Get()));
            }), 20.0, TEXT("real invalidation reaches sampler production callback")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S, Case](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                uint64 Published = 0;
                int64 Serial = 0;
                bool bSampling = false;
                bool bClosed = false;
                if (World != S->World.Get() || !RawIdentityRetained(*S) ||
                    !FCk_SceneNodeFrameSampler_TestAccess::TryCapture(
                        S->Sampler.Get(), Published, Serial, bSampling, bClosed) ||
                    !bClosed || bSampling ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty) ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                        S->Motion.Get()) != nullptr ||
                    !S->bLaterPassiveCaptured ||
                    (Case == ESamplerCase::ReentrantActorLoss &&
                     (!S->bSawInSample || !S->bReentrantGetterRejected ||
                      !S->bReentrantLossDestroyed)) ||
                    (Case == ESamplerCase::UnregisteredChild &&
                     S->PassiveWritesAfterInvalidation != 0))
                { Fail(Test, TEXT("invalid sampler published, retried or wrote a later passive visual")); }
                RemoveSamplerObservers(*S);
                S->StableFrame = GFrameCounter;
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            { return ck::auto_test::net::Get_HasSessionFailed() ||
                GFrameCounter >= S->StableFrame + 2; }),
                15.0, TEXT("two later frames after invalid sampler closure")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                auto* LaterPassive = S->Visuals.IsValidIndex(
                    S->bReentrantLossDestroyed ? 5 : 3)
                    ? S->Visuals[S->bReentrantLossDestroyed ? 5 : 3].Get()
                    : nullptr;
                if (World != S->World.Get() || !RawIdentityRetained(*S) ||
                    !S->bLaterPassiveCaptured || !IsValid(LaterPassive) ||
                    !IsValid(LaterPassive->Cube) ||
                    !LaterPassive->Cube->IsRegistered() ||
                    !LaterPassive->Cube->GetComponentTransform().Equals(
                        S->LaterPassivePose, 0.0) ||
                    !SameRemainingPassivePoses(*S, 2,
                        S->bReentrantLossDestroyed ? 4 : 0,
                        S->FrozenPassivePoses) ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty) ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                        S->Motion.Get()) != nullptr ||
                    S->PassiveWritesAfterInvalidation != 0)
                { Fail(Test, TEXT("invalid sampler recovered publication after two frames")); }
            })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([S](UWorld*)
        { RemoveSamplerObservers(*S); })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }

    enum class ERetirementCase : uint8
    {
        Production,
        ForeignAllocatedDescendant,
        ReentrantUnexpectedActor,
        SuccessorProduction
    };

    void RunRetirement(FAutomationTestBase* Test, const FString& Parameters,
        ERetirementCase Case)
    {
        const auto S = MakeShared<FState>();
        const bool bForeignAllocatedDescendant =
            Case == ERetirementCase::ForeignAllocatedDescendant;
        const bool bReentrantUnexpectedActor =
            Case == ERetirementCase::ReentrantUnexpectedActor;
        const bool bSuccessorProduction =
            Case == ERetirementCase::SuccessorProduction;
        const auto Next = MakeShared<FState>();
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
            2, TEXT("/Engine/Maps/Entry"), Parameters));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            const auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            return IsValid(Server) && IsValid(Client) &&
                IsValid(Server->GetGameState()) &&
                Server->GetGameState()->HasActorBegunPlay() &&
                IsValid(Client->GetGameState());
        }), 15.0, TEXT("Retirement real authority and client worlds become ready")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (!SetupActorsAndRoots(*S, World))
            { Fail(Test, TEXT("Retirement could not construct actual eighteen actors and six roots")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (IsValid(S->Roots.Get()) &&
                 UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(
                     S->Roots.Get()));
        }), 20.0, TEXT("Retirement six actual WithActor roots complete")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureRootsAndBeginGraph(*S))
            { Fail(Test, TEXT("Retirement eleven-node graph admission failed")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (IsValid(S->Graph.Get()) &&
                 UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(
                     S->Graph.Get()));
        }), 20.0, TEXT("Retirement eleven actual graph nodes settle")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureNodes(*S) ||
                !ReadPose(*S, S->DormantPose))
            { Fail(Test, TEXT("Retirement dormant source graph was incomplete")); return; }
            S->Motion.Reset(UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(
                S->Graph.Get()));
            if (!S->Motion.IsValid() ||
                UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(S->Motion.Get()))
            { Fail(Test, TEXT("Retirement six paused producers failed admission")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (S->Motion.IsValid() &&
                 UCk_SceneNodeGym_MotionRuntime_UE::Get_IsDormantReady(
                     S->Motion.Get()));
        }), 20.0, TEXT("Retirement six captured timers become dormant")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureMotion(*S) ||
                !UCk_SceneNodeGym_MotionRuntime_UE::TryActivateOnce(S->Motion.Get()))
            { Fail(Test, TEXT("Retirement six genuine Resume requests rejected")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (S->Motion.IsValid() &&
                 UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(S->Motion.Get()));
        }), 20.0, TEXT("Retirement six real Resume receipts become active")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !RawIdentityRetained(*S))
            { Fail(Test, TEXT("Retirement source identities changed before sampling")); return; }
            S->Sampler.Reset(UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                S->Motion.Get()));
            if (!S->Sampler.IsValid())
            { Fail(Test, TEXT("Retirement actual active sampler rejected")); return; }
            const TWeakPtr<FState> WeakS = S;
            S->FrameObserver = FWorldDelegates::OnWorldTickEnd.AddLambda(
                [WeakS](UWorld* Observed, ELevelTick Tick, float)
            {
                const auto State = WeakS.Pin();
                if (State.IsValid() && Tick == LEVELTICK_All)
                { ObserveCurrentSamplerFrame(*State, Observed); }
            });
            if (!S->FrameObserver.IsValid())
            { Fail(Test, TEXT("Retirement exact-world sampler observer did not bind")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                S->bFrameObservationFailed || S->ObservedFrames >= 2;
        }), 20.0, TEXT("Retirement two real current-frame seventeen-pose publications")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [Test, S, bForeignAllocatedDescendant,
                    bReentrantUnexpectedActor, bSuccessorProduction](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            FPose Current;
            TArray<FSlot> Slots;
            if (World != S->World.Get() || S->bFrameObservationFailed ||
                S->ObservedFrames < 2 || S->FirstFrame.FrameSerial <= 0 ||
                S->LastFrame.FrameSerial <= S->FirstFrame.FrameSerial)
            { Fail(Test, S->bFrameObservationFailed
                ? *S->FrameFailure
                : TEXT("Retirement current-frame presentation evidence was incomplete"));
              return; }
            if (!ReadPose(*S, Current) ||
                !MotionChanged(S->DormantPose, Current))
            { Fail(Test, TEXT("Retirement real source pose had no verified motion"));
              return; }
            if (!RawIdentityRetained(*S, &Slots) || Slots.Num() != 6)
            { Fail(Test, TEXT("Retirement six actual timer identities changed"));
              return; }
            if (!UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(
                    S->Motion.Get()))
            { Fail(Test, TEXT("Retirement source Motion was not active"));
              return; }
            if (!FreezeRetirementInputs(*S))
            { Fail(Test, TEXT("Retirement thirty creators or eighteen actors failed freeze"));
              return; }
            RemoveSamplerObservers(*S);
            S->RetirementIssuedAtClose.Reset(6);
            for (const auto& Slot : Slots)
            { S->RetirementIssuedAtClose.Add(Slot.IssuedMotionUpdateCount); }
            if (bForeignAllocatedDescendant)
            {
                const auto Parent = S->NodeHandles[0];
                if (!ck::IsValid(Parent) ||
                    !Parent.Has<ck::FFragment_LifetimeOwner>())
                { Fail(Test, TEXT("Retirement foreign-child parent was not a real live node")); return; }
                S->ForeignChild = UCk_Utils_EntityLifetime_UE::
                    Request_CreateEntity(Parent);
                if (!ForeignChildSurvives(*S))
                { Fail(Test, TEXT("Retirement failed to create a real unknown direct descendant")); return; }
            }
            if (bReentrantUnexpectedActor && !BindReentrantUnknownActor(S))
            { Fail(Test, TEXT("Retirement real actor-destroy callback did not bind")); return; }
            S->Retirement.Reset(
                UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginCompleted(
                    S->Motion.Get()));
            if (!S->Retirement.IsValid() ||
                UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                    S->Retirement.Get()) !=
                    (bForeignAllocatedDescendant
                        ? ECk_SceneNodeGym_RetirementState::Unproven
                        : ECk_SceneNodeGym_RetirementState::Pending))
            { Fail(Test, TEXT("Retirement one-shot receipt did not enter expected state")); return; }
            if (bSuccessorProduction &&
                !RejectSuccessorWithoutMutation(*S, S->Source.Get(),
                    S->Retirement.Get(), MakeSuccessorVisualClasses()))
            { Fail(Test, TEXT("pending retirement consumed successor admission or changed the cohort"));
              return; }
            FCkSceneNodeGym_Frame Empty = PoisonedFrame();
            if (UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                    S->Sampler.Get(), Empty) || !IsClearedFrame(Empty) ||
                UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                    S->Motion.Get()) != nullptr ||
                UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginCompleted(
                    S->Motion.Get()) != nullptr ||
                !RetirementReceiptMatchesFrozen(*S, false) ||
                !SameRetirementCreators(*S) ||
                !AllOriginalRetirementMembersSurvive(*S) ||
                !StartRetirementTickCounter(S))
            { Fail(Test, TEXT("Retirement closure left publication open or admitted retry")); return; }
            if (bForeignAllocatedDescendant &&
                (UCk_SceneNodeGym_RetirementRuntime_UE::Get_FailureReason(
                    S->Retirement.Get()) != FName(TEXT("CompletedFreezeRejected")) ||
                 !AllOriginalRetirementMembersSurvive(*S) ||
                 !ForeignChildSurvives(*S)))
            { Fail(Test, TEXT("Retirement adopted or destroyed the foreign child or known graph")); }
        })));
        if (bForeignAllocatedDescendant)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    KnownQueuesDrained(*S);
            }), 20.0, TEXT("rejected retirement drains previously issued known requests")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                TArray<FSlot> Slots;
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() ||
                    !RetirementReceiptMatchesFrozen(*S, false) ||
                    !AllOriginalRetirementMembersSurvive(*S) ||
                    !RawIdentityRetained(*S, &Slots) || Slots.Num() != 6 ||
                    !ForeignChildSurvives(*S) ||
                    !ReadPose(*S, S->RetirementStoppedPose) ||
                    !CaptureAllVisiblePoses(*S, S->RetirementVisiblePoses) ||
                    UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                        S->Retirement.Get()) !=
                        ECk_SceneNodeGym_RetirementState::Unproven ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty))
                { Fail(Test, TEXT("unproven retirement changed known ownership or visual state")); return; }
                S->RetirementTickAtStop = S->RetirementWorldTicks;
            })));
        }
        else if (bReentrantUnexpectedActor)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (S->Retirement.IsValid() &&
                     UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                         S->Retirement.Get()) !=
                         ECk_SceneNodeGym_RetirementState::Pending);
            }), 25.0, TEXT("real Role2 destruction injects unknown scope-owned actor")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                TArray<FSlot> Slots;
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !S->Retirement.IsValid() ||
                    !S->bReentrantRoleTwoDestroyed ||
                    !S->bReentrantPrerequisitesMet ||
                    !S->bReentrantUnknownSpawned ||
                    UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                        S->Retirement.Get()) !=
                        ECk_SceneNodeGym_RetirementState::Unproven ||
                    UCk_SceneNodeGym_RetirementRuntime_UE::Get_FailureReason(
                        S->Retirement.Get()).IsNone() ||
                    !RetirementReceiptMatchesFrozen(*S, true) ||
                    !AllRetirementEntitiesGone(*S) ||
                    !ReentrantResidualActorsSurvive(*S) ||
                    !OnlyRoleTwoDestroyRequested(*S) ||
                    !RawIdentityRetained(*S, &Slots) || Slots.Num() != 6 ||
                    !CaptureReentrantRemainingVisualPoses(*S,
                        S->ReentrantRemainingVisualPoses) ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty))
                { Fail(Test, TEXT("reentrant unknown actor did not stop retirement after Role2")); return; }
                S->RetirementTickAtStop = S->RetirementWorldTicks;
            })));
        }
        else
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (S->Retirement.IsValid() &&
                     UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                         S->Retirement.Get()) !=
                         ECk_SceneNodeGym_RetirementState::Pending);
            }), 25.0, TEXT("normal retirement observes actual stopped queues and completes")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                TArray<FSlot> Slots;
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !S->Retirement.IsValid() ||
                    UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                        S->Retirement.Get()) !=
                        ECk_SceneNodeGym_RetirementState::Complete ||
                    !UCk_SceneNodeGym_RetirementRuntime_UE::Get_FailureReason(
                        S->Retirement.Get()).IsNone() ||
                    !RetirementReceiptMatchesFrozen(*S, true) ||
                    !AllRetirementEntitiesGone(*S) ||
                    !AllRetirementActorsWorldAbsent(*S) ||
                    !RawIdentityRetained(*S, &Slots) || Slots.Num() != 6 ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty))
                { Fail(Test, TEXT("normal retirement lacked complete thirty-plus-eighteen absence")); return; }
                S->RetirementTickAtStop = S->RetirementWorldTicks;
            })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                S->RetirementWorldTicks >= S->RetirementTickAtStop + 2;
        }), 15.0, TEXT("two later ticks of the exact authority world after retirement")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [Test, S, bForeignAllocatedDescendant,
                    bReentrantUnexpectedActor](UWorld* World)
        {
            TArray<FSlot> Slots;
            FCkSceneNodeGym_Frame Empty = PoisonedFrame();
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !RawIdentityRetained(*S, &Slots) ||
                Slots.Num() != 6 || S->RetirementIssuedAtClose.Num() != 6 ||
                UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                    S->Sampler.Get(), Empty) || !IsClearedFrame(Empty))
            { Fail(Test, TEXT("retirement changed a frozen creator or republished")); return; }
            for (int32 I = 0; I < 6; ++I)
            { if (Slots[I].IssuedMotionUpdateCount != S->RetirementIssuedAtClose[I])
              { Fail(Test, TEXT("producer issued motion after retirement closure")); return; } }
            if (bForeignAllocatedDescendant)
            {
                FPose Later;
                if (UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                        S->Retirement.Get()) !=
                        ECk_SceneNodeGym_RetirementState::Unproven ||
                    !AllOriginalRetirementMembersSurvive(*S) ||
                    !ForeignChildSurvives(*S) || !ReadPose(*S, Later) ||
                    !SamePose(S->RetirementStoppedPose, Later) ||
                    !SameAllVisiblePoses(*S, S->RetirementVisiblePoses) ||
                    !RejectSuccessorWithoutMutation(*S, S->Source.Get(),
                        S->Retirement.Get(), MakeSuccessorVisualClasses()))
                { Fail(Test, TEXT("rejected retirement mutated the foreign or known cohort")); }
            }
            else if (bReentrantUnexpectedActor)
            {
                if (UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                        S->Retirement.Get()) !=
                        ECk_SceneNodeGym_RetirementState::Unproven ||
                    !RetirementReceiptMatchesFrozen(*S, true) ||
                    !AllRetirementEntitiesGone(*S) ||
                    !ReentrantResidualActorsSurvive(*S) ||
                    !OnlyRoleTwoDestroyRequested(*S) ||
                    !SameReentrantRemainingVisualPoses(*S,
                        S->ReentrantRemainingVisualPoses) ||
                    !RejectSuccessorWithoutMutation(*S, S->Source.Get(),
                        S->Retirement.Get(), MakeSuccessorVisualClasses()))
                { Fail(Test, TEXT("reentrant retirement destroyed a later actor or resumed")); }
            }
            else if (UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                    S->Retirement.Get()) !=
                    ECk_SceneNodeGym_RetirementState::Complete ||
                !AllRetirementEntitiesGone(*S) ||
                !AllRetirementActorsWorldAbsent(*S))
            { Fail(Test, TEXT("completed retirement reintroduced an entity or actor")); }
            RemoveSamplerObservers(*S);
        })));
        if (bSuccessorProduction)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda(
                    [Test, S, Next](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                auto* const Source = S->Source.Get();
                auto* const OldLedger = S->Ledger.Get();
                auto* const ClientWorld = ck::auto_test::net::Get_ClientWorld(0);
                auto* const WrongSource = IsValid(ClientWorld)
                    ? ClientWorld->GetGameState() : nullptr;
                const auto Classes = MakeSuccessorVisualClasses();
                auto ShortClasses = Classes;
                ShortClasses.Pop();
                if (World != S->World.Get() || !IsValid(Source) ||
                    !IsValid(OldLedger) || !IsValid(WrongSource) ||
                    WrongSource == Source ||
                    UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
                        S->Retirement.Get()) !=
                        ECk_SceneNodeGym_RetirementState::Complete ||
                    !AllRetirementEntitiesGone(*S) ||
                    !AllRetirementActorsWorldAbsent(*S) ||
                    !RejectSuccessorWithoutMutation(*S, nullptr,
                        S->Retirement.Get(), Classes) ||
                    !RejectSuccessorWithoutMutation(*S, Source,
                        nullptr, Classes) ||
                    !RejectSuccessorWithoutMutation(*S, WrongSource,
                        S->Retirement.Get(), Classes) ||
                    !RejectSuccessorWithoutMutation(*S, Source,
                        S->Retirement.Get(), ShortClasses))
                { Fail(Test, TEXT("successor malformed or foreign admission mutated completed generation"));
                  return; }

                // Remove only this test-owned completed ledger from the instance
                // list within the same action; restore before any world tick.
                TStrongObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> PinnedOld(
                    OldLedger);
                TArray<FCk_Handle> BeforeChildren;
                TArray<FCk_Handle> AfterChildren;
                int32 BeforeActors = 0, AfterActors = 0;
                int32 BeforeComponents = 0, AfterComponents = 0;
                if (!CaptureSuccessorAdmissionCensus(*S, BeforeChildren,
                        BeforeActors, BeforeComponents))
                { Fail(Test, TEXT("successor old-component bypass baseline was unreadable"));
                  return; }
                Source->RemoveInstanceComponent(OldLedger);
                const bool bBypassRejected =
                    UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginSuccessor(
                        Source, S->Retirement.Get(), Classes) == nullptr;
                if (IsValid(OldLedger))
                { Source->AddInstanceComponent(OldLedger); }
                if (!bBypassRejected || !IsValid(OldLedger) ||
                    !Source->GetInstanceComponents().Contains(OldLedger) ||
                    !CaptureSuccessorAdmissionCensus(*S, AfterChildren,
                        AfterActors, AfterComponents) ||
                    BeforeChildren != AfterChildren ||
                    BeforeActors != AfterActors ||
                    BeforeComponents != AfterComponents ||
                    !SameRetirementCreators(*S))
                { Fail(Test, TEXT("removed old ledger bypassed successor claim or changed population"));
                  return; }

                auto* const NewLedger =
                    UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginSuccessor(
                        Source, S->Retirement.Get(), Classes);
                if (!IsValid(NewLedger) || NewLedger == OldLedger ||
                    UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_Generation(
                        NewLedger) != 2 ||
                    UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(
                        NewLedger) != 0 ||
                    !Source->GetInstanceComponents().Contains(NewLedger) ||
                    !AllRetirementEntitiesGone(*S) ||
                    !AllRetirementActorsWorldAbsent(*S))
                { Fail(Test, TEXT("complete retirement did not admit one empty generation-two ledger"));
                  return; }
                Next->Ledger = NewLedger;
                Next->World = World;
                Next->Source = Source;
                Next->WorldRoot = S->WorldRoot;
                Next->ExpectedGeneration = 2;
                TArray<FCk_Handle> BeforeRetryChildren;
                TArray<FCk_Handle> AfterRetryChildren;
                int32 BeforeRetryActors = 0, AfterRetryActors = 0;
                int32 BeforeRetryComponents = 0, AfterRetryComponents = 0;
                if (!CaptureSuccessorAdmissionCensus(*Next,
                        BeforeRetryChildren, BeforeRetryActors,
                        BeforeRetryComponents) ||
                    UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginSuccessor(
                        Source, S->Retirement.Get(), Classes) != nullptr ||
                    !CaptureSuccessorAdmissionCensus(*Next,
                        AfterRetryChildren, AfterRetryActors,
                        AfterRetryComponents) ||
                    BeforeRetryChildren != AfterRetryChildren ||
                    BeforeRetryActors != AfterRetryActors ||
                    BeforeRetryComponents != AfterRetryComponents ||
                    !Source->GetInstanceComponents().Contains(NewLedger) ||
                    UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(
                        NewLedger) != 0 ||
                    !SetupActorsAndRoots(*Next, World, NewLedger, 2))
                { Fail(Test, TEXT("successor retry mutated ledger or real generation-two actors failed")); }
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([Next]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (IsValid(Next->Roots.Get()) &&
                     UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(
                         Next->Roots.Get()));
            }), 20.0, TEXT("successor six genuine WithActor roots complete")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda(
                    [Test, S, Next](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != Next->World.Get() ||
                    !CaptureRootsAndBeginGraph(*Next) ||
                    !AllRetirementEntitiesGone(*S) ||
                    !AllRetirementActorsWorldAbsent(*S))
                { Fail(Test, TEXT("successor six roots or old-generation absence failed")); }
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([Next]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (IsValid(Next->Graph.Get()) &&
                     UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(
                         Next->Graph.Get()));
            }), 20.0, TEXT("successor eleven authored nodes compose")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda(
                    [Test, Next](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != Next->World.Get() || !CaptureNodes(*Next) ||
                    !ReadPose(*Next, Next->DormantPose))
                { Fail(Test, TEXT("successor graph lacked eleven real nodes"));
                  return; }
                Next->Motion.Reset(
                    UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(
                        Next->Graph.Get()));
                if (!Next->Motion.IsValid() ||
                    UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(
                        Next->Motion.Get()))
                { Fail(Test, TEXT("successor six paused motion producers rejected")); }
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([Next]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (Next->Motion.IsValid() &&
                     UCk_SceneNodeGym_MotionRuntime_UE::Get_IsDormantReady(
                         Next->Motion.Get()));
            }), 20.0, TEXT("successor six captured timers reach dormant readiness")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda(
                    [Test, Next](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != Next->World.Get() || !CaptureMotion(*Next) ||
                    Next->Generation != 2 ||
                    !UCk_SceneNodeGym_MotionRuntime_UE::TryActivateOnce(
                        Next->Motion.Get()))
                { Fail(Test, TEXT("successor six real Resume requests rejected")); }
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([Next]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (Next->Motion.IsValid() &&
                     UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(
                         Next->Motion.Get()));
            }), 20.0, TEXT("successor six actual Resume receipts become active")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda(
                    [Test, S, Next](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != Next->World.Get() || !RawIdentityRetained(*Next) ||
                    !AllRetirementEntitiesGone(*S) ||
                    !AllRetirementActorsWorldAbsent(*S))
                { Fail(Test, TEXT("successor active motion lost identity or old absence"));
                  return; }
                Next->Sampler.Reset(
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryBegin(
                        Next->Motion.Get()));
                if (!Next->Sampler.IsValid())
                { Fail(Test, TEXT("successor generation-two sampler rejected"));
                  return; }
                const TWeakPtr<FState> WeakNext = Next;
                Next->FrameObserver = FWorldDelegates::OnWorldTickEnd.AddLambda(
                    [WeakNext](UWorld* Observed, ELevelTick Tick, float)
                {
                    const auto State = WeakNext.Pin();
                    if (State.IsValid() && Tick == LEVELTICK_All)
                    { ObserveCurrentSamplerFrame(*State, Observed); }
                });
                if (!Next->FrameObserver.IsValid())
                { Fail(Test, TEXT("successor exact-frame observer did not bind")); }
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([Next]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    Next->bFrameObservationFailed || Next->ObservedFrames >= 2;
            }), 20.0, TEXT("successor two complete seventeen-pose current-frame receipts")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda(
                    [Test, S, Next](UWorld* World)
            {
                FPose Current;
                FCkSceneNodeGym_Frame Empty = PoisonedFrame();
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != Next->World.Get() ||
                    Next->bFrameObservationFailed || Next->ObservedFrames < 2 ||
                    Next->Generation != 2 ||
                    Next->FirstFrame.FrameSerial <= 0 ||
                    Next->LastFrame.FrameSerial <= Next->FirstFrame.FrameSerial ||
                    Next->LastFrame.Generation != 2 ||
                    !ReadPose(*Next, Current) ||
                    !MotionChanged(Next->DormantPose, Current) ||
                    !RawIdentityRetained(*Next) ||
                    UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
                        S->Sampler.Get(), Empty) || !IsClearedFrame(Empty) ||
                    !AllRetirementEntitiesGone(*S) ||
                    !AllRetirementActorsWorldAbsent(*S))
                { Fail(Test, Next->bFrameObservationFailed
                    ? *Next->FrameFailure
                    : TEXT("successor generation-two actors, motion or complete frame failed")); }
                RemoveSamplerObservers(*Next);
            })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }

    void Run(FAutomationTestBase* Test, const FString& Parameters,
        bool bLoseActor, bool bCloseDuringResume)
    {
        const auto S = MakeShared<FState>();
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
            2, TEXT("/Engine/Maps/Entry"), Parameters));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            const auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            return IsValid(Server) && IsValid(Client) &&
                IsValid(Server->GetGameState()) &&
                Server->GetGameState()->HasActorBegunPlay() &&
                IsValid(Client->GetGameState());
        }), 15.0, TEXT("real authority and client worlds become ready")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (!SetupActorsAndRoots(*S, World))
            { Fail(Test, TEXT("could not construct the real actor cohort and six ECS roots")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (IsValid(S->Roots.Get()) &&
                 UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(
                     S->Roots.Get()));
        }), 20.0, TEXT("six real WithActor roots complete")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureRootsAndBeginGraph(*S))
            { Fail(Test, TEXT("source roots did not admit the eleven-node graph")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (IsValid(S->Graph.Get()) &&
                 UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(
                     S->Graph.Get()));
        }), 20.0, TEXT("eleven real nodes and source queues settle")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != S->World.Get() || !CaptureNodes(*S) ||
                !ReadPose(*S, S->DormantPose))
            { Fail(Test, TEXT("dormant graph did not expose real solver and visible poses")); return; }
            auto* Motion = UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(
                S->Graph.Get());
            S->Motion.Reset(Motion);
            if (!IsValid(Motion) ||
                UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(Motion) ||
                UCk_SceneNodeGym_MotionRuntime_UE::Get_IsUnproven(Motion))
            { Fail(Test, TEXT("six paused captured producers lost raw IDs or attempted motion")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([S]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (S->Motion.IsValid() &&
                 UCk_SceneNodeGym_MotionRuntime_UE::Get_IsDormantReady(
                     S->Motion.Get()));
        }), 20.0, TEXT("six real paused timers reach dormant readiness")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, S, bCloseDuringResume](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            FPose Later;
            if (World != S->World.Get() || !CaptureMotion(*S) ||
                !RawIdentityRetained(*S) ||
                !ReadPose(*S, Later) || !SamePose(S->DormantPose, Later) ||
                !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsDormantReady(
                    S->Motion.Get()) ||
                !UCk_SceneNodeGym_MotionRuntime_UE::TryActivateOnce(
                    S->Motion.Get()))
            { Fail(Test, TEXT("dormant poses changed or genuine six-Resume admission failed")); return; }
            bool bClosed = false;
            bool bOpaque = false;
            bool bStopUnproven = false;
            if (bCloseDuringResume &&
                (!AllSixQueuedResume(*S) ||
                 !UCk_SceneNodeGym_MotionRuntime_UE::TryClose(S->Motion.Get()) ||
                 !FCk_SceneNodeMotion_TestAccess::TryGet_StopState(
                     S->Motion.Get(), bClosed, bOpaque, bStopUnproven) ||
                 !bClosed || bOpaque || !bStopUnproven ||
                 !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsUnproven(
                     S->Motion.Get()) ||
                 UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(S->Motion.Get()) ||
                 !AllSixQueuedResume(*S)))
            { Fail(Test, TEXT("same-action source close did not retain six queued Resume attempts as recoverable Stop state")); }
        })));
        if (!bCloseDuringResume)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (S->Motion.IsValid() &&
                     UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(
                         S->Motion.Get()));
            }), 20.0, TEXT("six genuine Resume completion receipts make motion active")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
                TArray<FSlot> Slots;
                if (!RawIdentityRetained(*S, &Slots)) { return false; }
                for (const auto& Slot : Slots)
                { if (!Slot.bResumeIssued || !Slot.bResumeReturned ||
                      !Slot.bResumeCallback || !Slot.bResumeSucceeded ||
                      Slot.ResumeReturn != Slot.Timer ||
                      Slot.IssuedMotionUpdateCount < 1)
                  { return false; } }
                if (S->FirstIssued.Num() != 6)
                {
                    S->FirstMotionFrame = GFrameCounter;
                    S->FirstIssued.Reset(6);
                    for (const auto& Slot : Slots)
                    { S->FirstIssued.Add(Slot.IssuedMotionUpdateCount); }
                    return false;
                }
                if (GFrameCounter <= S->FirstMotionFrame) { return false; }
                for (int32 I = 0; I < 6; ++I)
                { if (Slots[I].IssuedMotionUpdateCount <= S->FirstIssued[I])
                  { return false; } }
                return true;
            }), 20.0, TEXT("six authored motion requests issue on distinct production frames")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
                FPose Actual;
                return ReadPose(*S, Actual) &&
                    MotionChanged(S->DormantPose, Actual);
            }), 20.0, TEXT("real solver and actor component apply authored motion")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S, bLoseActor](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !RawIdentityRetained(*S) ||
                    !ReadPose(*S, S->RunningPose) ||
                    !MotionChanged(S->DormantPose, S->RunningPose))
                { Fail(Test, TEXT("real authored location, rotation, scale, root and visible motion missing")); return; }
                if (bLoseActor)
                {
                    TArray<FSlot> BeforeLoss;
                    if (!RawIdentityRetained(*S, &BeforeLoss) ||
                        BeforeLoss.Num() != 6)
                    { Fail(Test, TEXT("active producer identities were not retained before actor loss")); return; }
                    S->IssuedAtClose.Reset(6);
                    for (const auto& Slot : BeforeLoss)
                    { S->IssuedAtClose.Add(Slot.IssuedMotionUpdateCount); }
                    auto* Lost = S->Visuals[ActorRole[0] - 1].Get();
                    if (!IsValid(Lost) || !Lost->Destroy() ||
                        UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(
                            S->Ledger.Get()) ||
                        UCk_SceneNodeGym_MotionRuntime_UE::TryActivateOnce(
                            S->Motion.Get()) ||
                        UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(
                            S->Graph.Get()) != nullptr)
                    { Fail(Test, TEXT("real known actor loss left a successor admission open")); }
                    return;
                }
                if (!UCk_SceneNodeGym_MotionRuntime_UE::TryClose(S->Motion.Get()))
                { Fail(Test, TEXT("source closure did not synchronously close producer callbacks")); }
            })));
            if (bLoseActor)
            {
                ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                    FCk_NetAutoTest_Condition::CreateLambda([S]()
                {
                    return ck::auto_test::net::Get_HasSessionFailed() ||
                        (S->Motion.IsValid() &&
                         UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(
                             S->Motion.Get()));
                }), 20.0, TEXT("a real surviving timer callback closes motion after actor loss")));
                ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                    FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
                {
                    TArray<FSlot> Slots;
                    bool bClosed = false;
                    bool bEverActive = false;
                    int32 Generation = 0;
                    if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                    if (World != S->World.Get() || !RawIdentityRetained(*S, &Slots) ||
                        !FCk_SceneNodeMotion_TestAccess::TryCapture(S->Motion.Get(),
                            Generation, bClosed, bEverActive, Slots) ||
                        !bClosed || !bEverActive || Generation != 1 ||
                        Slots.Num() != 6 || S->IssuedAtClose.Num() != 6 ||
                        !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(
                            S->Motion.Get()))
                    { Fail(Test, TEXT("known actor loss did not retain failed producer identities")); return; }
                    for (int32 I = 0; I < 6; ++I)
                    { if (Slots[I].IssuedMotionUpdateCount != S->IssuedAtClose[I])
                      { Fail(Test, TEXT("producer wrote motion after actor loss but before failure latch")); return; } }
                    S->StableFrame = GFrameCounter;
                })));
                ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                    FCk_NetAutoTest_Condition::CreateLambda([S]()
                { return ck::auto_test::net::Get_HasSessionFailed() ||
                    GFrameCounter >= S->StableFrame + 2; }),
                    15.0, TEXT("two later production frames after known actor loss")));
                ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                    FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
                {
                    TArray<FSlot> Slots;
                    bool bClosed = false;
                    bool bEverActive = false;
                    int32 Generation = 0;
                    if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                    if (World != S->World.Get() || !RawIdentityRetained(*S, &Slots) ||
                        S->IssuedAtClose.Num() != 6 ||
                        !FCk_SceneNodeMotion_TestAccess::TryCapture(S->Motion.Get(),
                            Generation, bClosed, bEverActive, Slots) ||
                        !bClosed || !bEverActive || Generation != 1 ||
                        !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(
                            S->Motion.Get()) ||
                        UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(
                            S->Graph.Get()) != nullptr)
                    { Fail(Test, TEXT("actor-loss closure was not durable")); return; }
                    for (int32 I = 0; I < 6; ++I)
                    { if (Slots[I].IssuedMotionUpdateCount != S->IssuedAtClose[I])
                      { Fail(Test, TEXT("actor-loss producer issued new motion after closure")); return; } }
                })));
        }
        else
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (S->Motion.IsValid() &&
                     UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
                         S->Motion.Get()) && KnownQueuesDrained(*S));
            }), 20.0, TEXT("six actual Stop receipts and known 23 request queues drain")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                TArray<FSlot> Slots;
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !RawIdentityRetained(*S, &Slots) ||
                    !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
                        S->Motion.Get()) || !KnownQueuesDrained(*S) ||
                    !ReadPose(*S, S->StoppedPose))
                { Fail(Test, TEXT("stopped motion lost receipt, queue or visible pose proof")); return; }
                S->IssuedAtClose.Reset(6);
                for (const auto& Slot : Slots)
                { S->IssuedAtClose.Add(Slot.IssuedMotionUpdateCount); }
                S->StableFrame = GFrameCounter;
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            { return ck::auto_test::net::Get_HasSessionFailed() ||
                GFrameCounter >= S->StableFrame + 2; }),
                15.0, TEXT("two production frames after stopped source queues drained")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                TArray<FSlot> Slots;
                FPose Later;
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !RawIdentityRetained(*S, &Slots) ||
                    !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
                        S->Motion.Get()) || !KnownQueuesDrained(*S) ||
                    !ReadPose(*S, Later) || !SamePose(S->StoppedPose, Later) ||
                    S->IssuedAtClose.Num() != 6)
                { Fail(Test, TEXT("stopped source poses or known queues changed later")); return; }
                for (int32 I = 0; I < 6; ++I)
                { if (Slots[I].IssuedMotionUpdateCount != S->IssuedAtClose[I])
                  { Fail(Test, TEXT("closed producer issued motion after Stop")); return; } }
            })));
        }
        }
        else
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    AllSixResumeDrained(*S);
            }), 20.0, TEXT("six genuine queued Resume requests drain with callbacks closed")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                FPose Later;
                bool bClosed = false;
                bool bOpaque = false;
                bool bStopUnproven = false;
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !AllSixResumeDrained(*S) ||
                    !ReadPose(*S, Later) || !SamePose(S->DormantPose, Later) ||
                    !FCk_SceneNodeMotion_TestAccess::TryGet_StopState(
                        S->Motion.Get(), bClosed, bOpaque, bStopUnproven) ||
                    !bClosed || bOpaque || !bStopUnproven ||
                    !UCk_SceneNodeGym_MotionRuntime_UE::TryClose(S->Motion.Get()))
                { Fail(Test, TEXT("closed Resume batch mutated motion or could not retry real Stop")); }
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (S->Motion.IsValid() &&
                     UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
                         S->Motion.Get()) && KnownQueuesDrained(*S));
            }), 20.0, TEXT("retried real six Stop receipts and known 23 queues drain")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                FPose Later;
                TArray<FSlot> Slots;
                bool bClosed = false;
                bool bOpaque = false;
                bool bStopUnproven = true;
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !RawIdentityRetained(*S, &Slots) ||
                    !FCk_SceneNodeMotion_TestAccess::TryGet_StopState(
                        S->Motion.Get(), bClosed, bOpaque, bStopUnproven) ||
                    !bClosed || bOpaque || bStopUnproven ||
                    !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
                        S->Motion.Get()) || !KnownQueuesDrained(*S) ||
                    !ReadPose(*S, Later) || !SamePose(S->DormantPose, Later))
                { Fail(Test, TEXT("recovered Stop lost exact producer identity, queue or pose state")); return; }
                for (const auto& Slot : Slots)
                { if (Slot.IssuedMotionUpdateCount != 0 || Slot.bResumeCallback)
                  { Fail(Test, TEXT("closed producer issued motion or observed a Resume callback")); return; } }
                S->StoppedPose = Later;
                S->StableFrame = GFrameCounter;
            })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([S]()
            { return ck::auto_test::net::Get_HasSessionFailed() ||
                GFrameCounter >= S->StableFrame + 2; }),
                15.0, TEXT("two later production frames after recoverable Stop")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, S](UWorld* World)
            {
                FPose Later;
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                if (World != S->World.Get() || !ReadPose(*S, Later) ||
                    !SamePose(S->StoppedPose, Later) ||
                    !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
                        S->Motion.Get()) || !KnownQueuesDrained(*S))
                { Fail(Test, TEXT("recovered Stop did not hold the actual visible poses stable")); }
            })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeMotionProduction,
    "Ck.SceneNode.Network.Motion.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeMotionProduction::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::Run(this, Parameters, false, false);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeMotionActorLoss,
    "Ck.SceneNode.Network.Motion.ActiveActorLoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeMotionActorLoss::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::Run(this, Parameters, true, false);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeMotionClosingDuringResume,
    "Ck.SceneNode.Network.Motion.ClosingDuringResume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeMotionClosingDuringResume::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::Run(this, Parameters, false, true);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeFrameSamplerProduction,
    "Ck.SceneNode.Network.Motion.FrameSampler.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeFrameSamplerProduction::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::RunSampler(this, Parameters,
        ck_scene_node_motion_test::ESamplerCase::Production);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeFrameSamplerReentrantActorLoss,
    "Ck.SceneNode.Network.Motion.FrameSampler.ReentrantActorLoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeFrameSamplerReentrantActorLoss::RunTest(
    const FString& Parameters)
{
    ck_scene_node_motion_test::RunSampler(this, Parameters,
        ck_scene_node_motion_test::ESamplerCase::ReentrantActorLoss);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeFrameSamplerUnregisteredChild,
    "Ck.SceneNode.Network.Motion.FrameSampler.UnregisteredChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeFrameSamplerUnregisteredChild::RunTest(
    const FString& Parameters)
{
    ck_scene_node_motion_test::RunSampler(this, Parameters,
        ck_scene_node_motion_test::ESamplerCase::UnregisteredChild);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeRetirementProduction,
    "Ck.SceneNode.Network.Motion.Retirement.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeRetirementProduction::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::RunRetirement(this, Parameters,
        ck_scene_node_motion_test::ERetirementCase::Production);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeRetirementForeignChild,
    "Ck.SceneNode.Network.Motion.Retirement.ForeignAllocatedDescendant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeRetirementForeignChild::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::RunRetirement(this, Parameters,
        ck_scene_node_motion_test::ERetirementCase::ForeignAllocatedDescendant);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeRetirementReentrantActor,
    "Ck.SceneNode.Network.Motion.Retirement.ReentrantUnexpectedActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeRetirementReentrantActor::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::RunRetirement(this, Parameters,
        ck_scene_node_motion_test::ERetirementCase::ReentrantUnexpectedActor);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeRetirementSuccessor,
    "Ck.SceneNode.Network.Motion.Retirement.SuccessorProduction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeRetirementSuccessor::RunTest(const FString& Parameters)
{
    ck_scene_node_motion_test::RunRetirement(this, Parameters,
        ck_scene_node_motion_test::ERetirementCase::SuccessorProduction);
    return true;
}

#endif
