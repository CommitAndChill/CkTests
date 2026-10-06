#include "CkSceneNode_SourceQueuesProbe.h"

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkSceneNodeGym_QueueRuntime.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

void UCk_SceneNode_SourceQueuesProbe_UE::OnTimerUpdate(
    FCk_Handle_Timer InTimer, FCk_Chrono, FCk_Time)
{
    ++UpdateCalls;
    if (bClosed) { ++ClosedUpdateCalls; return; }
    if (!ck::IsValid(InTimer) || !ck::IsValid(Root) || !ck::IsValid(Node) ||
        !SourceComponent.IsValid()) { return; }
    const double X = 20.0 + static_cast<double>(QueuedUpdates) * 3.0;
    auto MutableNode = Node;
    auto MutableRoot = Root;
    UCk_Utils_SceneNode_UE::Request_UpdateOffset(MutableNode,
        FCk_Request_SceneNode_UpdateRelativeTransform(FTransform(FVector(X, 0.0, 0.0))), {});
    UCk_Utils_Transform_UE::Request_SetTransform(MutableRoot,
        FCk_Request_Transform_SetTransform(FTransform(FVector(0.0, X, 0.0))), {});
    SourceComponent->SetWorldLocation(FVector(0.0, 0.0, X));
    ++QueuedUpdates;
}

void UCk_SceneNode_SourceQueuesProbe_UE::OnTimerStop(
    FCk_Handle_Timer, FCk_Chrono, FCk_Time)
{ ++StopSignalCalls; }

void UCk_SceneNode_SourceQueuesProbe_UE::OnStopCompleted(
    FCk_Handle InOwner, ECk_Request_OperationResult InResult)
{
    ++StopReceiptCalls;
    StopReceiptOwner = InOwner;
    StopReceipt = InResult;
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace ck_scene_source_queues_test
{
    struct FState
    {
        FCk_Handle WorldRoot;
        FCk_Handle Owner;
        FCk_Handle_Transform Root;
        FCk_Handle_SceneNode Node;
        FCk_Handle_Timer Timer;
        FCk_Handle RetiringLeaf;
        TWeakObjectPtr<AActor> Actor;
        TWeakObjectPtr<USceneComponent> Component;
        TStrongObjectPtr<UCk_SceneNode_SourceQueuesProbe_UE> Probe;
        FTransform SettledRoot;
        FTransform SettledNode;
        FTransform SettledComponent;
        int32 QueuedAtClose = 0;
        int32 UpdatesAtClose = 0;
        bool bSetup = false;
        bool bStopped = false;
    };
    static bool Live(const FCk_Handle& H)
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }
    static TArray<FCk_Handle> Known(const FState& S)
    { return {S.Root, S.Node, S.Timer}; }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeSourceQueues,
    "Ck.SceneNode.Network.SourceQueues.StopDrain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeSourceQueues::RunTest(const FString& Parameters)
{
    using namespace ck_scene_source_queues_test;
    const auto S = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* World)
    {
        if (!TestTrue(TEXT("initialized authority PIE world"),
            IsValid(World) && World->GetAuthGameMode() != nullptr)) { return; }
        S->WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!TestTrue(TEXT("real world root"), Live(S->WorldRoot))) { return; }
        S->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->WorldRoot);
        if (!TestTrue(TEXT("real retained owner"), Live(S->Owner))) { return; }
        S->Root = UCk_Utils_Transform_UE::Add(S->Owner,
            FTransform::Identity, ECk_Replication::DoesNotReplicate);
        auto RawNode = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->Owner);
        auto* Actor = World->SpawnActor<AActor>();
        auto* Component = IsValid(Actor) ? NewObject<UStaticMeshComponent>(Actor) : nullptr;
        if (!TestTrue(TEXT("real registered visible source component"),
            Live(S->Root) && Live(RawNode) && IsValid(Actor) && IsValid(Component)))
        { return; }
        auto* const Cube = LoadObject<UStaticMesh>(nullptr,
            TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!TestTrue(TEXT("real visible cube asset"), IsValid(Cube))) { return; }
        Component->SetStaticMesh(Cube);
        Actor->SetRootComponent(Component);
        Component->RegisterComponentWithWorld(World);
        S->Actor = Actor;
        S->Component = Component;
        if (!TestTrue(TEXT("production preallocated anchor creation"),
            Component->IsRegistered() && Component->IsVisible() &&
            !Component->bHiddenInGame && Component->GetStaticMesh() == Cube &&
            UCk_Utils_SceneNode_UE::TryAdd_AnchorToUnrealComponent(
                RawNode, S->Root, Component, FTransform::Identity, S->Node) &&
            S->Node == RawNode)) { return; }
        auto Spec = FCk_Timer_Spec{FCk_Time{1000.0}};
        Spec.Set_StartingState(ECk_Timer_State::Running);
        S->Timer = UCk_Utils_Timer_UE::Add(S->Owner, Spec);
        if (!TestTrue(TEXT("real running retained timer"),
            Live(S->Timer) && S->Timer.Has<ck::FTag_Timer_NeedsUpdate>())) { return; }
        S->Probe.Reset(NewObject<UCk_SceneNode_SourceQueuesProbe_UE>());
        S->Probe->Root = S->Root;
        S->Probe->Node = S->Node;
        S->Probe->SourceComponent = Component;
        FCk_Delegate_Timer Update;
        Update.BindDynamic(S->Probe.Get(), &UCk_SceneNode_SourceQueuesProbe_UE::OnTimerUpdate);
        FCk_Delegate_Timer Stop;
        Stop.BindDynamic(S->Probe.Get(), &UCk_SceneNode_SourceQueuesProbe_UE::OnTimerStop);
        const auto BoundUpdate = UCk_Utils_Timer_UE::BindTo_OnUpdate(S->Timer, Update);
        const auto BoundStop = UCk_Utils_Timer_UE::BindTo_OnStop(S->Timer, Stop);
        S->bSetup = TestTrue(TEXT("actual timer signals bound"),
            BoundUpdate == S->Timer && BoundStop == S->Timer);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([S]()
    { return S->bSetup && S->Probe.IsValid() && S->Probe->QueuedUpdates >= 2; }), 15.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* World)
    {
        if (!TestTrue(TEXT("two real timer updates queued motion"),
            S->bSetup && S->Probe.IsValid() && S->Probe->QueuedUpdates >= 2 &&
            Live(S->Root) && Live(S->Node) && Live(S->Timer) &&
            IsValid(World) && S->Component.IsValid())) { return; }
        S->Probe->bClosed = true;
        S->QueuedAtClose = S->Probe->QueuedUpdates;
        S->UpdatesAtClose = S->Probe->UpdateCalls;
        FCk_Delegate_Request_OnCompleted Receipt;
        Receipt.BindDynamic(S->Probe.Get(), &UCk_SceneNode_SourceQueuesProbe_UE::OnStopCompleted);
        auto MutableTimer = S->Timer;
        const auto Returned = UCk_Utils_Timer_UE::Request_Stop(MutableTimer, Receipt);
        TestTrue(TEXT("Stop accepted against exact timer; queue not yet drained"),
            Returned == S->Timer && S->Probe->StopReceiptCalls == 0 &&
            !UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Known(*S), S->WorldRoot));
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([S]()
    {
        return S->Probe.IsValid() && Live(S->WorldRoot) && Live(S->Root) && Live(S->Node) && Live(S->Timer) &&
            UCk_Utils_Transform_UE::Has(S->Root) &&
            UCk_Utils_Transform_UE::Has(S->Node) &&
            UCk_Utils_SceneNode_UE::Has(S->Node) &&
            UCk_Utils_Timer_UE::Has(S->Timer) &&
            S->Probe->StopReceiptCalls == 1 &&
            S->Probe->StopReceiptOwner == S->Timer &&
            S->Probe->StopReceipt == ECk_Request_OperationResult::Succeeded &&
            UCk_Utils_Timer_UE::Get_CurrentState(S->Timer) == ECk_Timer_State::Paused &&
            UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Known(*S), S->WorldRoot);
    }), 15.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        if (!TestTrue(TEXT("Stop receipt and production queues drained"),
            S->Probe.IsValid() && Live(S->WorldRoot) && Live(S->Root) && Live(S->Node) && Live(S->Timer) &&
            UCk_Utils_Transform_UE::Has(S->Root) &&
            UCk_Utils_Transform_UE::Has(S->Node) &&
            UCk_Utils_SceneNode_UE::Has(S->Node) &&
            UCk_Utils_Timer_UE::Has(S->Timer) &&
            S->Component.IsValid() && S->Probe->StopReceiptCalls == 1 &&
            S->Probe->StopReceiptOwner == S->Timer &&
            S->Probe->StopReceipt == ECk_Request_OperationResult::Succeeded &&
            UCk_Utils_Timer_UE::Get_CurrentState(S->Timer) == ECk_Timer_State::Paused &&
            UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Known(*S), S->WorldRoot))) { return; }
        const auto BeforeUpdates = S->Probe->QueuedUpdates;
        const auto BeforeRoot = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root);
        const auto BeforeNode = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(S->Node));
        const auto BeforeComponent = S->Component->GetComponentTransform();
        auto Bad = [&](const TArray<FCk_Handle>& Set, const FCk_Handle& Root)
        { return !UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(Set, Root); };
        UWorld* const Client = ck::auto_test::net::Get_ClientWorld(0);
        if (!TestTrue(TEXT("real remote client world for foreign-root query"), IsValid(Client)))
        { return; }
        TestTrue(TEXT("drained singleton query is true before malformed set probes"),
            UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                {S->Root}, S->WorldRoot));
        TestTrue(TEXT("default, empty, duplicate, and foreign roots reject without mutation"),
            Bad({}, S->WorldRoot) && Bad({FCk_Handle{}}, S->WorldRoot) &&
            Bad({S->Root, S->Root}, S->WorldRoot) &&
            Bad({S->Root}, FCk_Handle{}) &&
            Bad({S->Root}, UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(
                *Client)) &&
            S->Probe->QueuedUpdates == BeforeUpdates &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root).Equals(BeforeRoot, 0.0) &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::Cast(S->Node)).Equals(BeforeNode, 0.0) &&
            S->Component->GetComponentTransform().Equals(BeforeComponent, 0.0));
        S->bStopped = true;
        TestTrue(TEXT("Stop actually fired final OnUpdate behind closed producer"),
            S->Probe->StopSignalCalls == 1 &&
            S->Probe->ClosedUpdateCalls >= 1 &&
            S->Probe->UpdateCalls > S->UpdatesAtClose &&
            S->Probe->QueuedUpdates == S->QueuedAtClose);
        S->SettledRoot = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root);
        S->SettledNode = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(S->Node));
        S->SettledComponent = S->Component->GetComponentTransform();
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(6));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* World)
    {
        if (!TestTrue(TEXT("stable live test identities"),
            S->bStopped && IsValid(World) && Live(S->Root) && Live(S->Node) &&
            Live(S->Timer) && UCk_Utils_Transform_UE::Has(S->Root) &&
            UCk_Utils_Transform_UE::Has(S->Node) &&
            UCk_Utils_SceneNode_UE::Has(S->Node) &&
            UCk_Utils_Timer_UE::Has(S->Timer) &&
            S->Component.IsValid() && S->Probe.IsValid())) { return; }
        TestTrue(TEXT("closed source stays stopped and all known queues stay drained"),
            S->Probe->QueuedUpdates == S->QueuedAtClose &&
            S->Probe->StopReceiptCalls == 1 &&
            UCk_Utils_Timer_UE::Get_CurrentState(S->Timer) == ECk_Timer_State::Paused &&
            UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Known(*S), S->WorldRoot));
        TestTrue(TEXT("actual root, scene node and visible source component stay fixed"),
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root).Equals(
                S->SettledRoot, 0.0) &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::Cast(S->Node)).Equals(S->SettledNode, 0.0) &&
            S->Component->GetComponentTransform().Equals(S->SettledComponent, 0.0));
        auto Pending = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->Owner);
        S->RetiringLeaf = Pending;
        if (!TestTrue(TEXT("fresh probe leaf allocated"), Live(Pending))) { return; }
        if (!TestTrue(TEXT("fresh leaf has exact complete retirement ledger before destruction"),
            UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                {Pending}, {Pending}, S->WorldRoot))) { return; }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Pending);
        const auto RootBeforePending = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root);
        const auto NodeBeforePending = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(S->Node));
        const auto ComponentBeforePending = S->Component->GetComponentTransform();
        const int32 WritesBeforePending = S->Probe->QueuedUpdates;
        TestTrue(TEXT("pending destruction rejects without touching known queues"),
            !UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                {S->Root, Pending}, S->WorldRoot) &&
            UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Known(*S), S->WorldRoot) &&
            S->Probe->QueuedUpdates == WritesBeforePending &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root).Equals(
                RootBeforePending, 0.0) &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::Cast(S->Node)).Equals(NodeBeforePending, 0.0) &&
            S->Component->GetComponentTransform().Equals(ComponentBeforePending, 0.0));
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForCondition(
        FCk_NetAutoTest_Condition::CreateLambda([S]()
    { return S->RetiringLeaf != FCk_Handle{} && !ck::IsValid(S->RetiringLeaf); }), 15.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        if (!TestTrue(TEXT("retired test-owned leaf really became stale"),
            S->RetiringLeaf != FCk_Handle{} && !ck::IsValid(S->RetiringLeaf) &&
            S->Probe.IsValid() && Live(S->Root) && Live(S->Node) &&
            Live(S->Timer) && UCk_Utils_Transform_UE::Has(S->Root) &&
            UCk_Utils_Transform_UE::Has(S->Node) &&
            UCk_Utils_SceneNode_UE::Has(S->Node) &&
            UCk_Utils_Timer_UE::Has(S->Timer) && S->Component.IsValid())) { return; }
        const int32 BeforeWrites = S->Probe->QueuedUpdates;
        const auto RootBeforeStale = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root);
        const auto NodeBeforeStale = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(S->Node));
        const auto ComponentBeforeStale = S->Component->GetComponentTransform();
        TestTrue(TEXT("stale identity rejects without mutating surviving known queues"),
            !UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                {S->Root, S->RetiringLeaf}, S->WorldRoot) &&
            UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Known(*S), S->WorldRoot) && S->Probe->QueuedUpdates == BeforeWrites &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S->Root).Equals(
                RootBeforeStale, 0.0) &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                UCk_Utils_Transform_UE::Cast(S->Node)).Equals(NodeBeforeStale, 0.0) &&
            S->Component->GetComponentTransform().Equals(ComponentBeforeStale, 0.0));
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
