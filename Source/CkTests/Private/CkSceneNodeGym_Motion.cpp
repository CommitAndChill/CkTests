#include "CkSceneNodeGym_Motion.h"

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_NodeGraph.h"
#include "CkSceneNodeGym_StopReceipt.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkRecord/Record/CkRecord_Utils.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"

namespace CkSceneNodeMotionPrivate
{
    constexpr int32 MotionCount = 6;
    constexpr int32 NodeCount = 11;

    bool Live(const UObject* Value)
    {
        return ck::IsValid(Value) &&
            !Value->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    FCk_Timer_Spec TickSpec()
    {
        auto Spec = FCk_Timer_Spec{FCk_Time{0.0}};
        Spec.Set_StartingState(ECk_Timer_State::Paused);
        Spec.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        Spec.Set_CountDirection(ECk_Timer_CountDirection::CountUp);
        return Spec;
    }

    bool IsFiniteOffset(const FTransform& Value)
    { return Value.IsValid(); }
}

void UCk_SceneNodeGym_MotionReceiver_UE::OnResume(
    FCk_Handle Owner, ECk_Request_OperationResult Result)
{
    if (!IsInGameThread() || !CkSceneNodeMotionPrivate::Live(this) || !CkSceneNodeMotionPrivate::Live(Receipt.Get())) { return; }
    auto* const M = Receipt.Get();
    if (M->bClosed || M->bFailed) { return; }
    if (Slot < 0 || Slot >= M->Slots.Num() ||
        M->Receivers.Num() != CkSceneNodeMotionPrivate::MotionCount || M->Receivers[Slot].Get() != this ||
        !UCk_SceneNodeGym_MotionRuntime_UE::HasContext(M, false))
    { UCk_SceneNodeGym_MotionRuntime_UE::Fail(M, false); return; }
    auto& Entry = M->Slots[Slot];
    if (!M->bActivationAttempted || !Entry.bResumeIssued ||
        Entry.bResumeCallback || Owner != Entry.Timer ||
        Result != ECk_Request_OperationResult::Succeeded)
    { UCk_SceneNodeGym_MotionRuntime_UE::Fail(M, false); return; }
    Entry.bResumeCallback = true;
    Entry.bResumeSucceeded = true;
}

void UCk_SceneNodeGym_MotionReceiver_UE::OnUpdate(
    FCk_Handle_Timer Timer, FCk_Chrono, FCk_Time Delta)
{
    if (!IsInGameThread() || !CkSceneNodeMotionPrivate::Live(this) || !CkSceneNodeMotionPrivate::Live(Receipt.Get())) { return; }
    auto* const M = Receipt.Get();
    if (M->bClosed || M->bFailed || !M->bActivationAttempted) { return; }
    // A producer may fire while some Resume receipts remain outstanding.
    // Such callbacks are deliberately inert until the whole batch is accepted.
    if (!UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(M))
    {
        // The first update can arrive after all six real Resume completions
        // but before any update has observed Active. In that window, a lost
        // source/topology/timer proof is terminal, not another pending receipt.
        bool bAllAccepted = M->Slots.Num() == CkSceneNodeMotionPrivate::MotionCount;
        for (const auto& Entry : M->Slots)
        {
            bAllAccepted &= Entry.bResumeIssued && Entry.bResumeReturned &&
                Entry.bResumeCallback && Entry.bResumeSucceeded &&
                Entry.Timer != FCk_Handle_Timer{} &&
                Entry.ResumeReturn == Entry.Timer;
        }
        if (M->bEverActive || bAllAccepted)
        { UCk_SceneNodeGym_MotionRuntime_UE::Fail(M, false); }
        return;
    }
    M->bEverActive = true;
    UCk_SceneNodeGym_MotionRuntime_UE::HandleUpdate(M, Slot, Timer, Delta);
}

bool UCk_SceneNodeGym_MotionRuntime_UE::HasContext(
    const UCk_SceneNodeGym_Motion_UE* M, bool bRequireDrained)
{
    if (!IsInGameThread() || !CkSceneNodeMotionPrivate::Live(M) || M->bClosed || M->bFailed ||
        M->Graph.IsStale() || M->Ledger.IsStale() ||
        M->Slots.Num() != CkSceneNodeMotionPrivate::MotionCount || M->Nodes.Num() != CkSceneNodeMotionPrivate::NodeCount ||
        M->Receivers.Num() != CkSceneNodeMotionPrivate::MotionCount ||
        M->UpdateDelegates.Num() != CkSceneNodeMotionPrivate::MotionCount ||
        M->ResumeDelegates.Num() != CkSceneNodeMotionPrivate::MotionCount)
    { return false; }
    auto* const Graph = M->Graph.Get();
    UCk_SceneNodeGym_ActorLedger_UE* CurrentLedger = nullptr;
    FCk_Handle CurrentWorldRoot;
    FCk_Handle CurrentRoots[CkSceneNodeMotionPrivate::MotionCount];
    FCk_Handle CurrentNodes[CkSceneNodeMotionPrivate::NodeCount];
    if (!CkSceneNodeMotionPrivate::Live(Graph) || Graph->Motion != M || !Graph->bMotionAttempted ||
        !UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGetMotionContext(
            Graph, bRequireDrained, CurrentLedger, CurrentWorldRoot,
            CurrentRoots, CurrentNodes) ||
        CurrentLedger != M->Ledger.Get() || CurrentWorldRoot != M->WorldRoot ||
        Graph->Generation != M->Generation)
    { return false; }
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    { if (CurrentRoots[I] != M->Slots[I].Root) { return false; } }
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::NodeCount; ++I)
    { if (M->Nodes[I] != CurrentNodes[I]) { return false; } }
    return true;
}

bool UCk_SceneNodeGym_MotionRuntime_UE::HasExactTimer(
    const UCk_SceneNodeGym_Motion_UE* M, int32 Index, bool bRequirePaused)
{
    if (Index < 0 || Index >= M->Slots.Num()) { return false; }
    const auto& Slot = M->Slots[Index];
    const auto& Root = Slot.Root;
    const auto& Timer = Slot.Timer;
    if (!Slot.bAllocationAttempted || !Slot.bBound ||
        Root == FCk_Handle{} || Slot.Raw == FCk_Handle{} ||
        Timer == FCk_Handle_Timer{} ||
        Root.Get_RegistryView().Get_RegistryHandle() !=
            M->WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        !ck::IsValid(Root) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Root,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        FCk_Handle(Timer) != Slot.Raw ||
        Slot.Raw.Get_RegistryView().Get_RegistryHandle() !=
            M->WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        !ck::IsValid(Timer) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Timer,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !Timer.Has_All<ck::FFragment_LifetimeOwner,
            ck::FFragment_Timer, ck::FFragment_Timer_Params>() ||
        Timer.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Root ||
        !Root.Has<ck::FFragment_RecordOfTimers>() ||
        Root.Has<ck::FFragment_RecordOfEntityExtensions>() ||
        !Timer.Has<ck::FFragment_RecordEntry>() ||
        !ck::TUtils_RecordOfEntities<ck::FFragment_RecordOfTimers>::
            Get_ContainsEntry(Root, Timer) ||
        ck::TUtils_RecordOfEntities<ck::FFragment_RecordOfTimers>::
            Get_Entries(Root).Num() != 1 ||
        Timer.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>() ||
        Timer.Has<ck::FTag_Timer_NeedsSetup>() ||
        Timer.Has<ck::FTag_Timer_Countdown>() ||
        Timer.Get<ck::FFragment_Timer>().Get_Chrono().Get_GoalValue() !=
            FCk_Time{0.0} ||
        Timer.Get<ck::FFragment_Timer_Params>().Get_Behavior() !=
            ECk_Timer_Behavior::ResetOnDone)
    { return false; }
#if !CK_DISABLE_NET_PARAM_COPY_PER_ENTITY
    // Lifetime creation copies the source-only root's exact net policy when
    // this build configuration enables per-entity metadata propagation.
    if (!Root.Has<ck::FFragment_Net_Params>() ||
        !Timer.Has<ck::FFragment_Net_Params>() ||
        Timer.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings().Get_Replication() !=
            ECk_Replication::DoesNotReplicate ||
        Timer.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings().Get_NetRole() !=
            Root.Get<ck::FFragment_Net_Params>()
                .Get_ConnectionSettings().Get_NetRole() ||
        Timer.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings().Get_NetMode() !=
            Root.Get<ck::FFragment_Net_Params>()
                .Get_ConnectionSettings().Get_NetMode() ||
        Timer.Has<ck::FTag_HasAuthority>() !=
            Root.Has<ck::FTag_HasAuthority>() ||
        Timer.Has<ck::FTag_NetMode_IsHost>() !=
            Root.Has<ck::FTag_NetMode_IsHost>() ||
        Timer.Has<ck::FTag_NetMode_IsClient>() !=
            Root.Has<ck::FTag_NetMode_IsClient>())
    { return false; }
#else
    if (Timer.Has_Any<ck::FFragment_Net_Params, ck::FTag_HasAuthority,
        ck::FTag_NetMode_IsHost, ck::FTag_NetMode_IsClient>())
    { return false; }
#endif
    if (bRequirePaused &&
        (Timer.Has_Any<ck::FTag_Timer_NeedsUpdate,
            ck::FFragment_Timer_Requests>() ||
         UCk_Utils_Timer_UE::Get_CurrentState(Timer) != ECk_Timer_State::Paused))
    { return false; }
    return true;
}

void UCk_SceneNodeGym_MotionRuntime_UE::Fail(
    UCk_SceneNodeGym_Motion_UE* M, bool bOpaque)
{
    if (!IsInGameThread() || !CkSceneNodeMotionPrivate::Live(M)) { return; }
    M->bClosed = true; // callback gate closes before the ledger operation
    M->bFailed = true;
    M->bUnproven |= bOpaque;
    auto* const Ledger = M->Ledger.Get();
    if (CkSceneNodeMotionPrivate::Live(Ledger))
    { UCk_SceneNodeGym_ActorLedgerRuntime_UE::Request_Close(Ledger); }
}

UCk_SceneNodeGym_Motion_UE* UCk_SceneNodeGym_MotionRuntime_UE::TryBeginDormant(
    UCk_SceneNodeGym_NodeGraph_UE* Graph)
{
    if (!IsInGameThread() || !CkSceneNodeMotionPrivate::Live(Graph) || Graph->bMotionAttempted ||
        Graph->Motion != nullptr)
    { return nullptr; }
    UCk_SceneNodeGym_ActorLedger_UE* Ledger = nullptr;
    FCk_Handle WorldRoot;
    FCk_Handle Roots[CkSceneNodeMotionPrivate::MotionCount];
    FCk_Handle Nodes[CkSceneNodeMotionPrivate::NodeCount];
    if (!UCk_SceneNodeGym_NodeGraphRuntime_UE::TryGetMotionContext(
            Graph, true, Ledger, WorldRoot, Roots, Nodes))
    { return nullptr; }
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    {
        if (!UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Roots[I]))
        { return nullptr; }
    }
    // The one attempt is reserved before NewObject and before any allocation.
    Graph->bMotionAttempted = true;
    auto* const M = NewObject<UCk_SceneNodeGym_Motion_UE>(Graph);
    if (!CkSceneNodeMotionPrivate::Live(M))
    {
        UCk_SceneNodeGym_NodeGraphRuntime_UE::Fail(Graph, true);
        return nullptr;
    }
    Graph->Motion = M;
    M->Graph = Graph;
    M->Ledger = Ledger;
    M->WorldRoot = WorldRoot;
    M->Generation = Graph->Generation;
    M->Slots.SetNum(CkSceneNodeMotionPrivate::MotionCount);
    M->Nodes.SetNum(CkSceneNodeMotionPrivate::NodeCount);
    M->Receivers.SetNum(CkSceneNodeMotionPrivate::MotionCount);
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    { M->Slots[I].Root = Roots[I]; }
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::NodeCount; ++I)
    { M->Nodes[I] = Nodes[I]; }

    M->UpdateDelegates.SetNum(CkSceneNodeMotionPrivate::MotionCount);
    M->ResumeDelegates.SetNum(CkSceneNodeMotionPrivate::MotionCount);
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    {
        auto* const Receiver = NewObject<UCk_SceneNodeGym_MotionReceiver_UE>(M);
        if (!CkSceneNodeMotionPrivate::Live(Receiver)) { Fail(M, true); return M; }
        Receiver->Receipt = M;
        Receiver->Slot = I;
        M->Receivers[I] = Receiver;
        M->UpdateDelegates[I].BindDynamic(Receiver,
            &UCk_SceneNodeGym_MotionReceiver_UE::OnUpdate);
        M->ResumeDelegates[I].BindDynamic(Receiver,
            &UCk_SceneNodeGym_MotionReceiver_UE::OnResume);
        if (!M->UpdateDelegates[I].IsBound() ||
            !M->ResumeDelegates[I].IsBound())
        { Fail(M, false); return M; }
    }

    const auto Spec = CkSceneNodeMotionPrivate::TickSpec();
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    {
        if (!HasContext(M, false) ||
            !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Roots[I]))
        { Fail(M, false); return M; }
        auto& Slot = M->Slots[I];
        // The capture result is copied immediately, before any binding or
        // additional inspection. A default raw return after an attempt is opaque.
        const auto Capture = UCk_Utils_Timer_UE::TryAddCapturedPaused(Roots[I], Spec);
        Slot.bAllocationAttempted = Capture.bAllocationAttempted;
        Slot.Raw = Capture.RawEntity;
        Slot.Timer = Capture.Timer;
        if (!HasContext(M, false) || !Capture.bAllocationAttempted ||
            Capture.RawEntity == FCk_Handle{} ||
            Capture.Timer == FCk_Handle_Timer{} ||
            FCk_Handle(Capture.Timer) != Capture.RawEntity)
        { Fail(M, Capture.bAllocationAttempted); return M; }
        auto MutableTimer = Capture.Timer;
        const auto Bound = UCk_Utils_Timer_UE::BindTo_OnUpdate(
            MutableTimer, M->UpdateDelegates[I],
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
        if (!HasContext(M, false) || Bound != Capture.Timer ||
            MutableTimer != Capture.Timer)
        { Fail(M, false); return M; }
        Slot.bBound = true;
    }
    return M;
}

bool UCk_SceneNodeGym_MotionRuntime_UE::Get_IsDormantReady(
    const UCk_SceneNodeGym_Motion_UE* M)
{
    if (!HasContext(M, true) || M->bActivationAttempted) { return false; }
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    { if (!HasExactTimer(M, I, true)) { return false; } }
    return true;
}

bool UCk_SceneNodeGym_MotionRuntime_UE::TryActivateOnce(
    UCk_SceneNodeGym_Motion_UE* M)
{
    if (!Get_IsDormantReady(M)) { return false; }
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    {
        auto* const Receiver = M->Receivers[I].Get();
        if (!CkSceneNodeMotionPrivate::Live(Receiver) || Receiver->Receipt.Get() != M || Receiver->Slot != I)
        { Fail(M, false); return false; }
        if (!M->ResumeDelegates[I].IsBound())
        { Fail(M, false); return false; }
    }
    M->bActivationAttempted = true;
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    {
        if (!HasContext(M, false) || !HasExactTimer(M, I, true))
        { Fail(M, false); return false; }
        auto& Slot = M->Slots[I];
        Slot.bResumeIssued = true; // real completion may be synchronous
        auto MutableTimer = Slot.Timer;
        Slot.ResumeReturn = UCk_Utils_Timer_UE::Request_Resume(
            MutableTimer, M->ResumeDelegates[I]);
        Slot.bResumeReturned = Slot.ResumeReturn == Slot.Timer &&
            MutableTimer == Slot.Timer;
        if (!Slot.bResumeReturned || M->bClosed || M->bFailed)
        { Fail(M, false); return false; }
    }
    return true;
}

bool UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(
    const UCk_SceneNodeGym_Motion_UE* M)
{
    if (!HasContext(M, false) || !M->bActivationAttempted) { return false; }
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    {
        const auto& Slot = M->Slots[I];
        auto* const Receiver = M->Receivers[I].Get();
        if (!CkSceneNodeMotionPrivate::Live(Receiver) || Receiver->Receipt.Get() != M || Receiver->Slot != I ||
            !Slot.bResumeIssued || !Slot.bResumeReturned ||
            !Slot.bResumeCallback || !Slot.bResumeSucceeded ||
            Slot.ResumeReturn != Slot.Timer || !HasExactTimer(M, I, false) ||
            UCk_Utils_Timer_UE::Get_CurrentState(Slot.Timer) !=
                ECk_Timer_State::Running)
        { return false; }
    }
    return true;
}

bool UCk_SceneNodeGym_MotionRuntime_UE::Get_IsFailed(
    const UCk_SceneNodeGym_Motion_UE* M)
{ return IsInGameThread() && CkSceneNodeMotionPrivate::Live(M) && M->bFailed; }

bool UCk_SceneNodeGym_MotionRuntime_UE::Get_IsUnproven(
    const UCk_SceneNodeGym_Motion_UE* M)
{ return IsInGameThread() && CkSceneNodeMotionPrivate::Live(M) &&
    (M->bUnproven || M->bStopUnproven); }

bool UCk_SceneNodeGym_MotionRuntime_UE::TryClose(UCk_SceneNodeGym_Motion_UE* M)
{
    if (!IsInGameThread() || !CkSceneNodeMotionPrivate::Live(M)) { return false; }
    M->bClosed = true; // synchronous callback suppression precedes Stop
    auto* const Ledger = M->Ledger.Get();
    if (!CkSceneNodeMotionPrivate::Live(Ledger) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Request_Close(Ledger))
    { M->bStopUnproven = true; return true; }
    if (CkSceneNodeMotionPrivate::Live(M->StopReceipt.Get()))
    { M->bStopUnproven = false; return true; }
    if (M->WorldRoot == FCk_Handle{} || M->Slots.Num() != CkSceneNodeMotionPrivate::MotionCount)
    { M->bStopUnproven = true; return true; }
    const auto RegistryHandle = M->WorldRoot.Get_RegistryView().Get_RegistryHandle();
    if (ck::registry_table::Get_IsInParallelRegion(RegistryHandle) ||
        !ck::IsValid(M->WorldRoot))
    { M->bStopUnproven = true; return true; }
    TArray<FCk_Handle_Timer> Timers;
    Timers.Reserve(CkSceneNodeMotionPrivate::MotionCount);
    for (int32 I = 0; I < CkSceneNodeMotionPrivate::MotionCount; ++I)
    {
        if (!HasExactTimer(M, I, false))
        { M->bStopUnproven = true; return true; }
        Timers.Add(M->Slots[I].Timer);
    }
    M->StopReceipt = UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
        Ledger, Timers, M->WorldRoot);
    M->bStopUnproven = !CkSceneNodeMotionPrivate::Live(M->StopReceipt.Get());
    return true;
}

bool UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(
    const UCk_SceneNodeGym_Motion_UE* M)
{
    return IsInGameThread() && CkSceneNodeMotionPrivate::Live(M) && M->bClosed && !M->bUnproven &&
        !M->bStopUnproven &&
        CkSceneNodeMotionPrivate::Live(M->StopReceipt.Get()) &&
        UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(M->StopReceipt.Get());
}

void UCk_SceneNodeGym_MotionRuntime_UE::HandleUpdate(
    UCk_SceneNodeGym_Motion_UE* M, int32 Index,
    FCk_Handle_Timer Timer, FCk_Time Delta)
{
    if (!M->bEverActive || Index < 0 || Index >= CkSceneNodeMotionPrivate::MotionCount ||
        Timer != M->Slots[Index].Timer)
    { Fail(M, false); return; }
    const double Seconds = Delta.Get_Seconds();
    if (!FMath::IsFinite(Seconds) || Seconds < 0.0 ||
        Seconds > static_cast<double>(TNumericLimits<float>::Max()))
    { Fail(M, false); return; }
    const float Step = static_cast<float>(Seconds);
    const float Elapsed = M->Slots[Index].Elapsed + Step;
    // Bound only arithmetic that the authored formulas perform before making
    // a quaternion or trigonometric request. Ordinary frame deltas are inside.
    if (!FMath::IsFinite(Elapsed) ||
        Step > TNumericLimits<float>::Max() / 45.0f ||
        Elapsed > TNumericLimits<float>::Max() / 1.5f)
    { Fail(M, false); return; }

    // All target identities and current offsets are checked before the first
    // request in a multi-operation authored callback. Requests are queued;
    // they are not a rollback-capable transaction.
    auto ValidNode = [&](int32 N)
    {
        return N >= 0 && N < M->Nodes.Num() && ck::IsValid(M->Nodes[N]) &&
            M->Nodes[N].Has_All<ck::FFragment_SceneNode,
                ck::FFragment_Transform>() &&
            M->Nodes[N].Get<ck::FFragment_SceneNode>()
                .Get_RelativeTransform().IsValid();
    };
    auto ValidRoot = [&](int32 R)
    {
        return R >= 0 && R < M->Slots.Num() &&
            ck::IsValid(M->Slots[R].Root) &&
            M->Slots[R].Root.Has<ck::FFragment_Transform>();
    };
    if ((Index == 0 && !ValidNode(0)) ||
        (Index == 1 && !ValidNode(1)) ||
        (Index == 2 && !ValidRoot(2)) ||
        (Index == 3 && (!ValidRoot(3) || !ValidNode(6) || !ValidNode(7))) ||
        (Index == 4 && !ValidRoot(4)) ||
        (Index == 5 && (!ValidRoot(5) || !ValidNode(10))))
    { Fail(M, false); return; }
    if (Step == 0.0f) { return; }

    const float Sine = FMath::Sin(Elapsed);
    const float Cosine = FMath::Cos(Elapsed);
    if (Index == 0)
    {
        auto Node = UCk_Utils_SceneNode_UE::Cast(M->Nodes[0]);
        auto Offset = UCk_Utils_SceneNode_UE::Get_Offset(Node);
        Offset.SetLocation(FVector(150.0f * FMath::Cos(Elapsed * 1.5f),
            150.0f * FMath::Sin(Elapsed * 1.5f), 0.0f));
        if (!CkSceneNodeMotionPrivate::IsFiniteOffset(Offset) ||
            UCk_Utils_SceneNode_UE::Request_UpdateOffset(Node,
                FCk_Request_SceneNode_UpdateRelativeTransform{Offset}, {}) != Node)
        { Fail(M, false); return; }
    }
    else if (Index == 1)
    {
        auto Node = UCk_Utils_SceneNode_UE::Cast(M->Nodes[1]);
        auto Offset = UCk_Utils_SceneNode_UE::Get_Offset(Node);
        Offset.SetLocation(FVector(100.0f + Sine * 80.0f,
            Cosine * 50.0f, 50.0f + Sine * 30.0f));
        Offset.SetRotation(FRotator(0.0f, Sine * 45.0f, 0.0f).Quaternion());
        const float Scale = .5f + Sine * .2f;
        Offset.SetScale3D(FVector(Scale, Scale, Scale));
        if (!CkSceneNodeMotionPrivate::IsFiniteOffset(Offset) ||
            UCk_Utils_SceneNode_UE::Request_UpdateOffset(Node,
                FCk_Request_SceneNode_UpdateRelativeTransform{Offset}, {}) != Node)
        { Fail(M, false); return; }
    }
    else
    {
        const float RootSpeed[CkSceneNodeMotionPrivate::MotionCount] = {0, 0, 30.0f, 25.0f, 40.0f, 35.0f};
        auto Root = UCk_Utils_Transform_UE::Cast(M->Slots[Index].Root);
        const float RootYaw = RootSpeed[Index] * Step;
        if (!FMath::IsFinite(RootYaw)) { Fail(M, false); return; }
        auto Rotation = FCk_Request_Transform_AddRotationOffset{
            FRotator(0.0f, RootYaw, 0.0f)};
        Rotation.Set_LocalWorld(ECk_LocalWorld::World);
        FCk_Handle_SceneNode Child;
        FCk_Handle_SceneNode Grandchild;
        FCk_Handle_SceneNode AnchorNode;
        FTransform ChildOffset = FTransform::Identity;
        FTransform GrandchildOffset = FTransform::Identity;
        FTransform AnchorOffset = FTransform::Identity;
        if (Index == 3)
        {
            Child = UCk_Utils_SceneNode_UE::Cast(M->Nodes[6]);
            Grandchild = UCk_Utils_SceneNode_UE::Cast(M->Nodes[7]);
            ChildOffset = UCk_Utils_SceneNode_UE::Get_Offset(Child);
            GrandchildOffset = UCk_Utils_SceneNode_UE::Get_Offset(Grandchild);
            ChildOffset.SetRotation((ChildOffset.Rotator() +
                FRotator(0.0f, 45.0f * Step, 0.0f)).Quaternion());
            GrandchildOffset.SetRotation((GrandchildOffset.Rotator() +
                FRotator(0.0f, -22.5f * Step, 0.0f)).Quaternion());
            if (!CkSceneNodeMotionPrivate::IsFiniteOffset(ChildOffset) ||
                !CkSceneNodeMotionPrivate::IsFiniteOffset(GrandchildOffset))
            { Fail(M, false); return; }
        }
        if (Index == 5)
        {
            AnchorNode = UCk_Utils_SceneNode_UE::Cast(M->Nodes[10]);
            AnchorOffset = UCk_Utils_SceneNode_UE::Get_Offset(AnchorNode);
            AnchorOffset.SetLocation(FVector(180.0f, 0.0f,
                40.0f + FMath::Sin(Elapsed * 1.5f) * 60.0f));
            if (!CkSceneNodeMotionPrivate::IsFiniteOffset(AnchorOffset))
            { Fail(M, false); return; }
        }
        UCk_Utils_Transform_UE::Request_AddRotationOffset(Root, Rotation, {});
        if (FCk_Handle(Root) != M->Slots[Index].Root || !HasContext(M, false))
        { Fail(M, false); return; }
        if (Index == 3)
        {
            if (UCk_Utils_SceneNode_UE::Request_UpdateOffset(Child,
                    FCk_Request_SceneNode_UpdateRelativeTransform{ChildOffset}, {}) != Child ||
                FCk_Handle(Child) != M->Nodes[6] || !HasContext(M, false) ||
                UCk_Utils_SceneNode_UE::Request_UpdateOffset(Grandchild,
                    FCk_Request_SceneNode_UpdateRelativeTransform{GrandchildOffset}, {}) != Grandchild ||
                FCk_Handle(Grandchild) != M->Nodes[7])
            { Fail(M, false); return; }
        }
        if (Index == 5 &&
            (UCk_Utils_SceneNode_UE::Request_UpdateOffset(AnchorNode,
                FCk_Request_SceneNode_UpdateRelativeTransform{AnchorOffset}, {}) != AnchorNode ||
             FCk_Handle(AnchorNode) != M->Nodes[10]))
        { Fail(M, false); return; }
    }
    M->Slots[Index].Elapsed = Elapsed;
    ++M->Slots[Index].IssuedMotionUpdateCount;
}
