#include "CkSceneNodeGym_StopReceipt.h"

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

#include <variant>

namespace CkSceneNodeStopReceiptPrivate
{
    bool HasPermittedPendingReset(const FCk_Handle_Timer& Timer)
    {
        if (!Timer.Has<ck::FFragment_Timer_Requests>()) { return true; }
        const auto& Requests = Timer.Get<ck::FFragment_Timer_Requests>().Get_Requests();
        if (Requests.IsEmpty()) { return true; }
        if (Requests.Num() != 1) { return false; }
        const auto* Manipulate = std::get_if<FCk_Request_Timer_Manipulate>(&Requests[0]);
        return Manipulate != nullptr &&
            Manipulate->Get_Manipulate() == ECk_Timer_Manipulate::Reset;
    }
}

void UCk_SceneNodeGym_StopReceiver_UE::OnCompleted(
    FCk_Handle Owner, ECk_Request_OperationResult Result)
{
    if (!IsInGameThread()) { return; }
    if (!ck::IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return; }
    auto* const R = Receipt.Get();
    if (!ck::IsValid(R)) { return; }
    if (R->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return; }
    if (Slot < 0 || Slot >= R->Slots.Num() ||
        R->Receivers.Num() != R->Slots.Num() ||
        R->Receivers[Slot].Get() != this ||
        !UCk_SceneNodeGym_StopRuntime_UE::HasExactReceipt(R))
    { R->bFailed = true; return; }
    auto& Entry = R->Slots[Slot];
    if (!Entry.bIssued || Entry.bCallback || Owner != Entry.Timer ||
        Result != ECk_Request_OperationResult::Succeeded)
    { R->bFailed = true; return; }
    Entry.bCallback = true;
    Entry.bSucceeded = true;
}

bool UCk_SceneNodeGym_StopRuntime_UE::HasClosedContext(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger, const FCk_Handle& WorldRoot)
{
    if (!IsInGameThread() || !ck::IsValid(Ledger)) { return false; }
    if (Ledger->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Ledger->Source.IsStale() || Ledger->SourceWorld.IsStale())
    { return false; }
    auto* const Source = Ledger->Source.Get();
    auto* const SourceWorld = Ledger->SourceWorld.Get();
    if (!ck::IsValid(Source) || !ck::IsValid(SourceWorld) ||
        Source->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        SourceWorld->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasLiveSourceContext(Ledger) ||
        !Ledger->bClosed || Ledger->ActiveRole != -1 ||
        Ledger->Generation <= 0 || WorldRoot == FCk_Handle{})
    { return false; }
    const auto Registry = WorldRoot.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    if (ck::registry_table::Get_IsInParallelRegion(RegistryHandle) ||
        !ck::IsValid(WorldRoot) ||
        WorldRoot != FCk_Handle{Registry.Get_TransientEntity(), RegistryHandle} ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(WorldRoot,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    UWorld* const World = WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get();
    if (!ck::IsValid(World) ||
        World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        World->bIsTearingDown || World != Ledger->SourceWorld.Get() ||
        !World->IsGameWorld() || World->GetNetMode() == NM_Client)
    { return false; }
    auto* const Subsystem = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    return ck::IsValid(Subsystem) &&
        Subsystem->Get_LoadHold() != ECk_EcsWorld_LoadHold::Teardown &&
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) == WorldRoot;
}

bool UCk_SceneNodeGym_StopRuntime_UE::HasExactReceipt(
    const UCk_SceneNodeGym_StopReceipt_UE* Receipt)
{
    if (!IsInGameThread() || !ck::IsValid(Receipt) ||
        Receipt->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Receipt->Ledger.IsStale() ||
        !HasClosedContext(Receipt->Ledger.Get(), Receipt->WorldRoot))
    { return false; }
    const auto* const Ledger = Receipt->Ledger.Get();
    return Ledger->StopReceipt == Receipt &&
        Ledger->Generation == Receipt->Generation;
}

UCk_SceneNodeGym_StopReceipt_UE* UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
    UCk_SceneNodeGym_ActorLedger_UE* Ledger,
    const TArray<FCk_Handle_Timer>& Timers, const FCk_Handle& WorldRoot)
{
    if (!HasClosedContext(Ledger, WorldRoot) || Ledger->StopReceipt != nullptr ||
        Timers.Num() < 1 || Timers.Num() > 6)
    { return nullptr; }
    const auto RegistryHandle = WorldRoot.Get_RegistryView().Get_RegistryHandle();
    for (int32 I = 0; I < Timers.Num(); ++I)
    {
        const auto& Timer = Timers[I];
        if (Timer == FCk_Handle_Timer{} || Timer == WorldRoot ||
            Timer.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
            !ck::IsValid(Timer) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Timer,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !Timer.Has<ck::FFragment_Timer>() ||
            !Timer.Has<ck::FFragment_Timer_Params>() ||
            Timer.Has<ck::FTag_Timer_NeedsSetup>() ||
            !CkSceneNodeStopReceiptPrivate::HasPermittedPendingReset(Timer))
        { return nullptr; }
        for (int32 J = 0; J < I; ++J)
        { if (Timers[J] == Timer) { return nullptr; } }
    }

    auto* const R = NewObject<UCk_SceneNodeGym_StopReceipt_UE>(Ledger);
    if (!ck::IsValid(R)) { return nullptr; }
    R->Ledger = Ledger;
    R->WorldRoot = WorldRoot;
    R->Generation = Ledger->Generation;
    R->Slots.Reserve(Timers.Num());
    for (int32 I = 0; I < Timers.Num(); ++I)
    {
        UCk_SceneNodeGym_StopReceipt_UE::FSlot Slot;
        Slot.Timer = Timers[I];
        R->Slots.Add(Slot);
    }
    // Reserve the one ledger-owned record before any fallible receiver creation.
    Ledger->StopReceipt = R;
    R->Receivers.Reserve(Timers.Num());
    for (int32 I = 0; I < Timers.Num(); ++I)
    {
        auto* const Receiver = NewObject<UCk_SceneNodeGym_StopReceiver_UE>(R);
        if (!ck::IsValid(Receiver)) { R->bFailed = true; return R; }
        Receiver->Receipt = R;
        Receiver->Slot = I;
        R->Receivers.Add(Receiver);
    }
    // Every callback must bind before any Stop can be queued. A bind failure
    // retains the claimed, failed record without issuing a successful subset.
    TArray<FCk_Delegate_Request_OnCompleted> Delegates;
    Delegates.Reserve(Timers.Num());
    for (int32 I = 0; I < Timers.Num(); ++I)
    {
        FCk_Delegate_Request_OnCompleted Delegate;
        Delegate.BindDynamic(R->Receivers[I].Get(),
            &UCk_SceneNodeGym_StopReceiver_UE::OnCompleted);
        if (!Delegate.IsBound()) { R->bFailed = true; return R; }
        Delegates.Add(Delegate);
    }
    // Slots, receivers, and delegates are frozen before the first Stop.
    for (int32 I = 0; I < Timers.Num(); ++I)
    {
        auto& Slot = R->Slots[I];
        Slot.bIssued = true; // completion may be synchronous
        auto MutableTimer = Slot.Timer;
        Slot.Returned = UCk_Utils_Timer_UE::Request_Stop(MutableTimer, Delegates[I]);
        Slot.bReturned = Slot.Returned == Slot.Timer && MutableTimer == Slot.Timer;
        if (!Slot.bReturned || R->bFailed) { R->bFailed = true; return R; }
    }
    return R;
}

bool UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(
    const UCk_SceneNodeGym_StopReceipt_UE* Receipt)
{
    if (!HasExactReceipt(Receipt) || Receipt->bFailed)
    { return false; }
    if (Receipt->Slots.IsEmpty() || Receipt->Slots.Num() > 6 ||
        Receipt->Receivers.Num() != Receipt->Slots.Num())
    { return false; }
    const auto RegistryHandle = Receipt->WorldRoot.Get_RegistryView().Get_RegistryHandle();
    for (int32 I = 0; I < Receipt->Slots.Num(); ++I)
    {
        const auto& Slot = Receipt->Slots[I];
        auto* const Receiver = Receipt->Receivers[I].Get();
        if (!ck::IsValid(Receiver) ||
            Receiver->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Receiver->Receipt.Get() != Receipt || Receiver->Slot != I)
        { return false; }
        if (!Slot.bIssued || !Slot.bReturned || !Slot.bCallback ||
            !Slot.bSucceeded || Slot.Returned != Slot.Timer ||
            Slot.Timer == FCk_Handle_Timer{} ||
            Slot.Timer.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
            !ck::IsValid(Slot.Timer) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Slot.Timer,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !Slot.Timer.Has<ck::FFragment_Timer>() ||
            !Slot.Timer.Has<ck::FFragment_Timer_Params>() ||
            Slot.Timer.Has_Any<ck::FTag_Timer_NeedsSetup,
                ck::FTag_Timer_NeedsUpdate,
                ck::FFragment_Timer_Requests>() ||
            UCk_Utils_Timer_UE::Get_CurrentState(Slot.Timer) != ECk_Timer_State::Paused)
        { return false; }
    }
    return true;
}

bool UCk_SceneNodeGym_StopRuntime_UE::Get_IsFailed(
    const UCk_SceneNodeGym_StopReceipt_UE* Receipt)
{
    return IsInGameThread() && ck::IsValid(Receipt) &&
        !Receipt->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        Receipt->bFailed;
}

int32 UCk_SceneNodeGym_StopRuntime_UE::Get_ExpectedCount(
    const UCk_SceneNodeGym_StopReceipt_UE* Receipt)
{
    return IsInGameThread() && ck::IsValid(Receipt) &&
        !Receipt->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        ? Receipt->Slots.Num() : 0;
}

int32 UCk_SceneNodeGym_StopRuntime_UE::Get_SucceededCount(
    const UCk_SceneNodeGym_StopReceipt_UE* Receipt)
{
    if (!IsInGameThread() || !ck::IsValid(Receipt) ||
        Receipt->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return 0; }
    int32 Count = 0;
    for (const auto& Slot : Receipt->Slots)
    { if (Slot.bIssued && Slot.bReturned && Slot.bCallback && Slot.bSucceeded &&
          Slot.Returned == Slot.Timer) { ++Count; } }
    return Count;
}
