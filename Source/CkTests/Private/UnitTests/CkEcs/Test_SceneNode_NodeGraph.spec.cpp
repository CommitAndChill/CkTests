#include "CkSceneNode_NodeGraphTestAccess.h"

#include "CkSceneNode_ActorLedgerProbe.h"
#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_EcsRoots.h"
#include "CkSceneNodeGym_NodeGraph.h"
#include "CkSceneNodeGym_QueueRuntime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Registry/CkRegistry.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkRecord/Record/CkRecord_Utils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

namespace ck_scene_node_graph_test
{
    using FSlot = FCk_SceneNodeNodeGraph_TestAccess::FSlot;
    constexpr int32 RootSlot[11] = {0, 1, 2, 2, 2, 2, 3, 3, 4, 4, 5};
    constexpr int32 ParentNode[11] = {
        INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE,
        INDEX_NONE, INDEX_NONE, 6, INDEX_NONE, 8, INDEX_NONE};
    constexpr int32 ActorRole[6] = {1, 3, 5, 10, 13, 16};

    FTransform ExpectedOffset(int32 Slot)
    {
        const FVector Positions[11] = {
            {150, 0, 0}, {100, 0, 50}, {120, 0, 0}, {0, 120, 0},
            {-120, 0, 0}, {0, -120, 0}, {120, 0, 0}, {100, 0, 0},
            {160, 0, 0}, {80, 0, 0}, {180, 0, 40}};
        const float Scales[11] = {.5f, .5f, .4f, .4f, .4f, .4f,
            .7f, .6f, .6f, .4f, .5f};
        if (Slot < 0 || Slot >= 11) { return FTransform::Identity; }
        return FTransform(FRotator::ZeroRotator, Positions[Slot],
            FVector(Scales[Slot], Scales[Slot], Scales[Slot]));
    }

    struct FState
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> Source;
        TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
        TWeakObjectPtr<UCk_SceneNodeGym_EcsRoots_UE> Roots;
        TWeakObjectPtr<UCk_SceneNodeGym_NodeGraph_UE> Graph;
        TArray<TWeakObjectPtr<ACk_SceneNode_ActorLedgerProbe>> Visuals;
        FCk_Handle WorldRoot;
        FCk_Handle RootHandles[6];
        TArray<FSlot> Frozen;
        bool bReentryObserved = false;
    };

    void Fail(FAutomationTestBase* Test, const TCHAR* Reason)
    {
        Test->AddError(Reason);
        ck::auto_test::net::FailSession(Reason);
    }

    bool SetupActorsAndRoots(FState& State, UWorld* World)
    {
        if (!IsValid(World) || !IsValid(World->GetGameState()) ||
            !IsValid(ck::auto_test::net::Get_ClientWorld(0)))
        { return false; }
        State.World = World;
        State.Source = World->GetGameState();
        State.WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(State.WorldRoot)) { return false; }
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
        State.Ledger = Ledger;
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
            State.Visuals.Add(Probe);
        }
        if (State.Visuals.Num() != 17 ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
                Ledger, true)) { return false; }
        auto* Roots = UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
            Ledger, UCk_SceneNodeGym_WithActorScript_UE::StaticClass());
        State.Roots = Roots;
        return IsValid(Roots) &&
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsFailed(Roots) &&
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsUnproven(Roots);
    }

    bool CaptureRoots(FState& State)
    {
        auto* Roots = State.Roots.Get();
        if (!IsValid(Roots) ||
            !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Roots))
        { return false; }
        for (int32 I = 0; I < 6; ++I)
        {
            if (!UCk_SceneNodeGym_EcsRootsRuntime_UE::TryGet_CompletedRoot(
                    Roots, I, State.RootHandles[I]) ||
                !ck::IsValid(State.RootHandles[I]) ||
                !State.RootHandles[I].Has<ck::FFragment_Transform>())
            { return false; }
        }
        return true;
    }

    bool FrozenUnchanged(const FState& State)
    {
        int32 Generation = 0;
        TArray<FSlot> Slots;
        if (!FCk_SceneNodeNodeGraph_TestAccess::TryCapture(
                State.Graph.Get(), Generation, Slots) ||
            Generation != 1 || Slots.Num() != State.Frozen.Num())
        { return false; }
        for (int32 I = 0; I < Slots.Num(); ++I)
        {
            if (Slots[I].Raw != State.Frozen[I].Raw ||
                Slots[I].Parent != State.Frozen[I].Parent ||
                !Slots[I].InitialOffset.Equals(State.Frozen[I].InitialOffset, 0.0) ||
                Slots[I].RootSlot != State.Frozen[I].RootSlot ||
                Slots[I].ParentNodeSlot != State.Frozen[I].ParentNodeSlot ||
                Slots[I].bAllocationAttempted != State.Frozen[I].bAllocationAttempted ||
                Slots[I].bComposed != State.Frozen[I].bComposed)
            { return false; }
        }
        return true;
    }

    void Run(FAutomationTestBase* Test, const FString& Parameters,
        bool bDestroyParentDuringFirstCommit)
    {
        const auto State = MakeShared<FState>();
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
            FCk_NetAutoTest_ServerAction::CreateLambda([Test, State](UWorld* World)
        {
            if (!SetupActorsAndRoots(*State, World))
            { Fail(Test, TEXT("could not construct real actor cohort and six queued ECS roots")); }
        })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
            FCk_NetAutoTest_Condition::CreateLambda([State]()
        {
            return ck::auto_test::net::Get_HasSessionFailed() ||
                (IsValid(State->Roots.Get()) &&
                 UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(
                     State->Roots.Get()));
        }), 20.0, TEXT("six real captured WithActor requests finish")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [Test, State, bDestroyParentDuringFirstCommit](UWorld* World)
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
            if (World != State->World.Get() || !CaptureRoots(*State))
            { Fail(Test, TEXT("six real source roots did not complete before graph admission")); return; }
            auto* Roots = State->Roots.Get();
            auto* LostActor = State->Visuals[ActorRole[0] - 1].Get();
            FDelegateHandle Observer;
            if (bDestroyParentDuringFirstCommit)
            {
                if (!IsValid(LostActor))
                { Fail(Test, TEXT("reentry parent actor is not live")); return; }
                const FCk_Handle FirstRoot = State->RootHandles[0];
                Observer = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
                    [State, LostActor, FirstRoot](const FCk_Handle_Transform& Added)
                {
                    if (State->bReentryObserved || !ck::IsValid(Added) ||
                        !Added.Has<ck::FFragment_LifetimeOwner>() ||
                        Added.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != FirstRoot)
                    { return; }
                    State->bReentryObserved = true;
                    if (IsValid(LostActor)) { LostActor->Destroy(); }
                });
            }
            auto* Graph = UCk_SceneNodeGym_NodeGraphRuntime_UE::TryBegin(Roots);
            if (Observer.IsValid())
            { UCk_Utils_Transform_UE::Get_OnAdded().Remove(Observer); }
            State->Graph = Graph;
            int32 Generation = 0;
            TArray<FSlot> Slots;
            if (!IsValid(Graph) ||
                !FCk_SceneNodeNodeGraph_TestAccess::TryCapture(
                    Graph, Generation, Slots) ||
                Generation != 1 || Slots.Num() != 11)
            { Fail(Test, TEXT("graph did not retain fixed eleven-slot creator record")); return; }
            State->Frozen = Slots;
            if (bDestroyParentDuringFirstCommit)
            {
                if (!State->bReentryObserved ||
                    !UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsFailed(Graph) ||
                    !Slots[0].bAllocationAttempted ||
                    !ck::IsValid(Slots[0].Raw,
                        ck::IsValid_Policy_IncludePendingKill{}) ||
                    UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_ComposedCount(Graph) != 0 ||
                    UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(Graph))
                { Fail(Test, TEXT("real reentrant actor loss did not close graph after first raw return")); return; }
                for (int32 I = 1; I < Slots.Num(); ++I)
                {
                    if (Slots[I].bAllocationAttempted || Slots[I].Raw != FCk_Handle{})
                    { Fail(Test, TEXT("reentrant failure allocated a later graph node")); return; }
                }
                FCk_Handle_SceneNode Out;
                if (UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGet_CompletedNode(
                        Graph, 0, Out) || Out != FCk_Handle_SceneNode{} ||
                    UCk_SceneNodeGym_NodeGraphRuntime_UE::TryBegin(Roots) != nullptr ||
                    !FrozenUnchanged(*State))
                { Fail(Test, TEXT("reentrant failed graph exposed a node or allowed retry")); }
                return;
            }
            if (UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsFailed(Graph) ||
                UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsUnproven(Graph) ||
                UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_ComposedCount(Graph) != 11 ||
                UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(Graph))
            { Fail(Test, TEXT("fresh graph did not retain eleven committed pending nodes")); return; }
            TArray<FCk_Handle> Known;
            for (const auto& Root : State->RootHandles) { Known.Add(Root); }
            for (int32 I = 0; I < 11; ++I)
            {
                const auto& Slot = Slots[I];
                const FCk_Handle ExpectedParent = ParentNode[I] == INDEX_NONE
                    ? State->RootHandles[RootSlot[I]] : Slots[ParentNode[I]].Raw;
                if (!Slot.bAllocationAttempted || !Slot.bComposed ||
                    !ck::IsValid(Slot.Raw) || Slot.Parent != ExpectedParent ||
                    Slot.RootSlot != RootSlot[I] ||
                    Slot.ParentNodeSlot != ParentNode[I] ||
                    !Slot.InitialOffset.Equals(ExpectedOffset(I), 0.0) ||
                    !Slot.Raw.Has<ck::FFragment_LifetimeOwner>() ||
                    Slot.Raw.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != ExpectedParent ||
                    !Slot.Raw.Has<ck::FFragment_SceneNode>() ||
                    !Slot.Raw.Get<ck::FFragment_SceneNode>()
                        .Get_RelativeTransform().Equals(ExpectedOffset(I), 0.0))
                { Fail(Test, *FString::Printf(TEXT("fresh node %d lost authored parent or offset"), I)); return; }
                for (int32 J = 0; J < I; ++J)
                { if (Slot.Raw == Slots[J].Raw)
                  { Fail(Test, TEXT("two authored nodes reused one raw identity")); return; } }
                FCk_Handle_SceneNode Out;
                if (UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGet_CompletedNode(
                        Graph, I, Out) || Out != FCk_Handle_SceneNode{})
                { Fail(Test, TEXT("pending graph exposed partial public node")); return; }
                Known.Add(Slot.Raw);
            }
            if (UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                    Known, State->WorldRoot) ||
                UCk_SceneNodeGym_NodeGraphRuntime_UE::TryBegin(Roots) != nullptr ||
                !FrozenUnchanged(*State))
            { Fail(Test, TEXT("real root/node queues or single-attempt record are wrong")); }
        })));
        if (!bDestroyParentDuringFirstCommit)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(Test,
                FCk_NetAutoTest_Condition::CreateLambda([State]()
            {
                return ck::auto_test::net::Get_HasSessionFailed() ||
                    (IsValid(State->Graph.Get()) &&
                     UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(
                         State->Graph.Get()));
            }), 20.0, TEXT("real scene propagation and known queues drain")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([Test, State](UWorld* World)
            {
                if (ck::auto_test::net::Get_HasSessionFailed()) { return; }
                auto* Graph = State->Graph.Get();
                auto* Ledger = State->Ledger.Get();
                if (World != State->World.Get() || !IsValid(Graph) ||
                    !FrozenUnchanged(*State) ||
                    !UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(Graph))
                { Fail(Test, TEXT("eleven-node graph lost exact completed proof")); return; }
                for (int32 I = 0; I < 11; ++I)
                {
                    FCk_Handle_SceneNode Completed;
                    const FCk_Handle Parent = ParentNode[I] == INDEX_NONE
                        ? State->RootHandles[RootSlot[I]]
                        : State->Frozen[ParentNode[I]].Raw;
                    if (!UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGet_CompletedNode(
                            Graph, I, Completed) ||
                        FCk_Handle(Completed) != State->Frozen[I].Raw ||
                        !ck::IsValid(Parent) || !Parent.Has<ck::FFragment_Transform>() ||
                        !ck::IsValid(State->Frozen[I].Raw) ||
                        !State->Frozen[I].Raw.Has<ck::FFragment_Transform>() ||
                        !State->Frozen[I].Raw.Has<ck::SceneNodeParent>() ||
                        FCk_Handle(UCk_Utils_SceneNode_UE::Get_Parent(Completed)) != Parent ||
                        !Parent.Has<ck::FFragment_RecordOfSceneNodes>())
                    { Fail(Test, *FString::Printf(TEXT("completed node %d identity invalid"), I)); return; }
                    const auto Forward = ck::FUtils_RecordOfSceneNodes::Get_Entries(Parent);
                    int32 ExpectedChildren = 0;
                    for (int32 J = 0; J < 11; ++J)
                    { ExpectedChildren += State->Frozen[J].Parent == Parent ? 1 : 0; }
                    if (Forward.Num() != ExpectedChildren ||
                        !Forward.Contains(Completed))
                    { Fail(Test, *FString::Printf(TEXT("completed node %d lacks exact parent forward record"), I)); return; }
                    const FTransform Actual = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                        UCk_Utils_Transform_UE::Cast(State->Frozen[I].Raw));
                    auto* AnchorActor = State->Visuals[ActorRole[5] - 1].Get();
                    if (I == 10 && (!IsValid(AnchorActor) ||
                        !IsValid(AnchorActor->Cube) ||
                        !AnchorActor->Cube->IsRegistered()))
                    { Fail(Test, TEXT("original actor mesh anchor is no longer registered")); return; }
                    const FTransform ExpectedParentWorld = I == 10
                        ? AnchorActor->Cube->GetComponentTransform()
                        : UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                            UCk_Utils_Transform_UE::Cast(Parent));
                    if (!Actual.Equals(ExpectedOffset(I) * ExpectedParentWorld))
                    { Fail(Test, *FString::Printf(TEXT("completed node %d misses real composed world pose"), I)); return; }
                }
                auto* Lost = State->Visuals[ActorRole[0] - 1].Get();
                if (!IsValid(Lost) || !Lost->Destroy() ||
                    !FrozenUnchanged(*State) ||
                    UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) ||
                    UCk_SceneNodeGym_NodeGraphRuntime_UE::Get_IsConstructionComplete(Graph))
                { Fail(Test, TEXT("known actor loss did not invalidate current graph proof")); }
            })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeNodeGraph,
    "Ck.SceneNode.Network.NodeGraph.Construction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeNodeGraph::RunTest(const FString& Parameters)
{
    ck_scene_node_graph_test::Run(this, Parameters, false);
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeNodeGraphReentry,
    "Ck.SceneNode.Network.NodeGraph.ReentrantLoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeNodeGraphReentry::RunTest(const FString& Parameters)
{
    ck_scene_node_graph_test::Run(this, Parameters, true);
    return true;
}

#endif
