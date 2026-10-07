#include "CkSceneNode_MotionTestAccess.h"

#include "CkSceneNode_ActorLedgerProbe.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_EcsRoots.h"
#include "CkSceneNodeGym_NodeGraph.h"
#include "CkSceneNodeGym_QueueRuntime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Components/StaticMeshComponent.h"
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

    struct FState
    {
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
        uint64 FirstMotionFrame = 0;
        TArray<int32> FirstIssued;
        uint64 StableFrame = 0;
        TArray<int32> IssuedAtClose;
    };

    void Fail(FAutomationTestBase* Test, const TCHAR* Reason)
    {
        Test->AddError(Reason);
        ck::auto_test::net::FailSession(Reason);
    }

    bool SetupActorsAndRoots(FState& S, UWorld* World)
    {
        if (!IsValid(World) || !IsValid(World->GetGameState()) ||
            !IsValid(ck::auto_test::net::Get_ClientWorld(0)))
        { return false; }
        S.World = World;
        S.Source = World->GetGameState();
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
        auto* Ledger = UCk_SceneNodeGym_ActorLedgerRuntime_UE::
            TryBeginActorGeneration(World->GetGameState(), 1, Classes);
        if (!IsValid(Ledger)) { return false; }
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
            Generation != 1 || Slots.Num() != 6)
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

#endif
