#include "CkCompassGym_SourceMembership.h"
#include "CkCompassGym_SourceMembership_Private.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkGym_AdmissionRuntime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkPoi/CkPoi_Fragment.h"
#include "CkCompass/CkCompass_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"

CK_REGISTER_PROCESSOR(ck::FProcessor_CompassGym_SourceMembership);

namespace ck_compass_membership
{
    constexpr double PendingTimeoutSeconds = 10.0;

    bool Live(const FCk_Handle& H)
    {
        return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) &&
            ck::IsValid(H) &&
            !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    bool Preflight(UWorld* World, AActor* ExactGameState,
        const FCk_Handle& SourceRoot)
    {
        if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
            World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            World->bIsTearingDown || World->IsBeingCleanedUp() ||
            World->IsNetMode(NM_Client) || !IsValid(ExactGameState) ||
            ExactGameState->IsActorBeingDestroyed() ||
            World->GetGameState() != ExactGameState ||
            ExactGameState->GetWorld() != World)
        { return false; }
        const auto WorldRoot =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!Live(WorldRoot) || !Live(SourceRoot) ||
            SourceRoot.Get_RegistryView().Get_RegistryHandle() !=
                WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            !SourceRoot.Has<ck::FFragment_LifetimeOwner>())
        { return false; }
        return SourceRoot.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == WorldRoot;
    }

    bool Open(UWorld* World)
    {
        if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
            World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            World->bIsTearingDown || World->IsBeingCleanedUp())
        { return false; }
        const auto WorldRoot =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!Live(WorldRoot) || !WorldRoot.Has<TWeakObjectPtr<UWorld>>() ||
            WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get() != World)
        { return false; }
        bool AdmissionClosed = true;
        if (!UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(
                WorldRoot, AdmissionClosed))
        { return false; }
        return !AdmissionClosed &&
            !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(WorldRoot);
    }

    void Reject(ck::FFragment_CompassGym_SourceMembership& State,
        const FString& Why)
    {
        State.Result = ECkCompassGym_MembershipResult::Rejected;
        State.ClosedUnproven = true;
        State.Failure = Why.IsEmpty() ? TEXT("Source membership unproven") : Why;
        State.CommittedRows.Reset();
        State.CurrentRows.Reset();
        State.CurrentRowsDirty = false;
        if (auto* World = State.World.Get(); IsValid(World))
        {
            const auto WorldRoot =
                UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            const bool ProjectionClosed =
                UCk_Utils_Compass_UE::TryCloseWorldProjection(WorldRoot);
            const bool AdmissionClosed =
                UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(WorldRoot);
            if (!ProjectionClosed || !AdmissionClosed)
            { State.Failure += TEXT("; world closure unproven"); }
        }
        else { State.Failure += TEXT("; source world unavailable for closure"); }
    }

    bool SafeOwned(const FCk_Handle& Host, const FCk_Handle& SourceRoot)
    {
        return Live(Host) && Live(SourceRoot) &&
            Host.Get_RegistryView().Get_RegistryHandle() ==
                SourceRoot.Get_RegistryView().Get_RegistryHandle() &&
            Host.Has<ck::FFragment_LifetimeOwner>() &&
            Host.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == SourceRoot;
    }

    // This runs before Compass_Update. A failed repair closes admission;
    // never touch a host after its exact ownership can no longer be proved.
    bool RollbackPoi(const TArray<FCk_Handle>& Changed,
        const FCk_Handle& SourceRoot, bool RestorePresent)
    {
        bool Restored = true;
        for (const auto& Host : Changed)
        {
            if (!SafeOwned(Host, SourceRoot))
            { Restored = false; continue; }
            auto MutableHost = Host;
            if (RestorePresent)
            {
                if (!MutableHost.Has<ck::FTag_Poi>()) { MutableHost.Add<ck::FTag_Poi>(); }
            }
            else if (MutableHost.Has<ck::FTag_Poi>())
            { MutableHost.Try_Remove<ck::FTag_Poi>(); }
            if (!SafeOwned(Host, SourceRoot) ||
                MutableHost.Has<ck::FTag_Poi>() != RestorePresent)
            { Restored = false; }
        }
        return Restored;
    }

    bool AllPoi(const TArray<FCkCompassGym_SourceReceipt>& Stress,
        const FCk_Handle& SourceRoot, bool ExpectedPresent)
    {
        for (const auto& R : Stress)
        {
            if (!SafeOwned(R.Host, SourceRoot) ||
                R.Host.Has<ck::FTag_Poi>() != ExpectedPresent)
            { return false; }
        }
        return true;
    }

    bool SameReceipts(const TArray<FCkCompassGym_SourceReceipt>& A,
        const TArray<FCkCompassGym_SourceReceipt>& B)
    {
        if (A.Num() != B.Num()) { return false; }
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            const auto& L = A[Index];
            const auto& R = B[Index];
            if (L.CatalogId != R.CatalogId || L.Kind != R.Kind ||
                L.Host != R.Host || L.Marker != R.Marker || L.Timer != R.Timer ||
                L.ExpectedCategory != R.ExpectedCategory)
            { return false; }
        }
        return true;
    }

    TArray<FCkCompassGym_SourceReceipt> WithActivePings(
        const ck::FFragment_CompassGym_SourceMembership& State)
    {
        TArray<FCkCompassGym_SourceReceipt> Whole = State.Active;
        Whole.Append(State.ActivePings);
        return Whole;
    }

    ECkCompassGym_CatalogOutcome Observe(
        UWorld* World, AActor* GameState, const FCk_Handle& SourceRoot,
        const ck::FFragment_CompassGym_SourceMembership& State,
        TArray<FCkCompassGym_SourceRow>& OutActive,
        TArray<FCkCompassGym_SourceRow>& OutStagedStress,
        TArray<FCkCompassGym_SourceRow>& OutStagedPing,
        FString& OutFailure)
    {
        const auto Whole = WithActivePings(State);
        return UCkCompassGym_SourceCatalog::
            TryGet_SourceCatalogTransactionWithPingPhases(
                World, GameState, SourceRoot, true, Whole, State.Staged,
                State.FrozenRetiring, State.StagedPings, State.FrozenPings,
                OutActive, OutStagedStress, OutStagedPing, OutFailure);
    }

    bool BoundBase(const ck::FFragment_CompassGym_SourceMembership& State,
        UWorld* World, AActor* GameState)
    {
        if (!State.BaseInitialized || State.Base.Num() != 9 ||
            State.World.Get() != World || State.GameState.Get() != GameState ||
            (State.Active.Num() != 9 && State.Active.Num() != 509))
        { return false; }
        for (int32 Index = 0; Index < 9; ++Index)
        {
            const auto& Base = State.Base[Index];
            const auto& Current = State.Active[Index];
            if (Base.CatalogId != Current.CatalogId || Base.Kind != Current.Kind ||
                Base.Host != Current.Host || Base.Marker != Current.Marker ||
                Base.Timer != Current.Timer ||
                Base.ExpectedCategory != Current.ExpectedCategory)
            { return false; }
        }
        return true;
    }

    bool FrozenPingGone(const FCkCompassGym_SourceReceipt& R)
    {
        return !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Host) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Marker) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Timer);
    }

    TArray<FCkCompassGym_SourceRow> WithoutPings(
        const TArray<FCkCompassGym_SourceRow>& Whole)
    {
        TArray<FCkCompassGym_SourceRow> Result;
        for (const auto& Row : Whole)
        {
            if (Row.Kind != ECkCompassGym_SourceKind::Ping)
            { Result.Add(Row); }
        }
        return Result;
    }

    bool SameRows(const TArray<FCkCompassGym_SourceRow>& A,
        const TArray<FCkCompassGym_SourceRow>& B)
    {
        if (A.Num() != B.Num()) { return false; }
        const auto* Type = FCkCompassGym_SourceRow::StaticStruct();
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            if (!Type->CompareScriptStruct(&A[Index], &B[Index], PPF_None))
            { return false; }
        }
        return true;
    }

    int64 HighestId(const TArray<FCkCompassGym_SourceReceipt>& Receipts)
    {
        int64 Highest = 0;
        for (const auto& R : Receipts)
        { Highest = FMath::Max(Highest, R.CatalogId); }
        return Highest;
    }

    void ProcessAdmit(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        ck::FFragment_CompassGym_SourceMembership& State)
    {
        TArray<FCkCompassGym_SourceRow> ActiveRows, StagedRows, StagedPingRows;
        FString Why;
        const auto Before = Observe(World, GameState, SourceRoot, State,
            ActiveRows, StagedRows, StagedPingRows, Why);
        if (Before == ECkCompassGym_CatalogOutcome::Pending) { return; }
        if (Before != ECkCompassGym_CatalogOutcome::Complete ||
            ActiveRows.Num() != 9 + State.ActivePings.Num() ||
            StagedRows.Num() != 500 ||
            StagedPingRows.Num() != State.StagedPings.Num() ||
            !AllPoi(State.Staged, SourceRoot, false))
        { Reject(State, Why.IsEmpty() ? TEXT("Staged cohort proof failed") : Why); return; }

        TArray<FCk_Handle> Changed;
        Changed.Reserve(State.Staged.Num());
        for (const auto& R : State.Staged)
        {
            if (!SafeOwned(R.Host, SourceRoot) || R.Host.Has<ck::FTag_Poi>())
            { const bool Repaired = RollbackPoi(Changed, SourceRoot, false);
              const bool Parity = AllPoi(State.Staged, SourceRoot, false);
              Reject(State, Repaired && Parity ? TEXT("Staged host changed during admission") :
                  TEXT("Staged host changed; rollback unproven")); return; }
            auto MutableHost = R.Host;
            MutableHost.Add<ck::FTag_Poi>();
            Changed.Add(R.Host);
            if (!SafeOwned(R.Host, SourceRoot) ||
                !R.Host.Has<ck::FTag_Poi>())
            {
                const bool Repaired = RollbackPoi(Changed, SourceRoot, false);
                const bool Parity = AllPoi(State.Staged, SourceRoot, false);
                Reject(State, Repaired && Parity
                    ? TEXT("Staged tag add rejected during admission")
                    : TEXT("Staged tag add failed; rollback unproven"));
                return;
            }
        }
        TArray<FCkCompassGym_SourceReceipt> Combined = State.Active;
        Combined.Append(State.Staged);
        for (auto& R : Combined) { R.Admitted = true; }
        TArray<FCkCompassGym_SourceReceipt> Whole = Combined;
        Whole.Append(State.ActivePings);
        TArray<FCkCompassGym_SourceRow> Proven509, UnusedStress, ProvenStagedPings;
        const TArray<FCkCompassGym_SourceReceipt> Empty;
        const auto After = AllPoi(State.Staged, SourceRoot, true)
            ? UCkCompassGym_SourceCatalog::TryGet_SourceCatalogTransactionWithPingPhases(
                World, GameState, SourceRoot, true, Whole, Empty, Empty,
                State.StagedPings, State.FrozenPings,
                Proven509, UnusedStress, ProvenStagedPings, Why)
            : ECkCompassGym_CatalogOutcome::Invalid;
        if (After != ECkCompassGym_CatalogOutcome::Complete ||
            Proven509.Num() != 509 + State.ActivePings.Num() ||
            !UnusedStress.IsEmpty() ||
            ProvenStagedPings.Num() != State.StagedPings.Num())
        {
            const bool Repaired = RollbackPoi(Changed, SourceRoot, false);
            const bool Parity = AllPoi(State.Staged, SourceRoot, false);
            Reject(State, Repaired && Parity
                ? (Why.IsEmpty() ? TEXT("Post-admission cohort proof failed") : Why)
                : TEXT("Post-admission proof failed; rollback unproven"));
            return;
        }
        State.Active = MoveTemp(Combined);
        for (auto& R : State.Base) { R.Admitted = true; }
        State.Staged.Reset();
        State.CommittedRows = WithoutPings(Proven509);
        State.Result = ECkCompassGym_MembershipResult::Committed;
        State.CurrentRowsDirty = true;
    }

    void ProcessWithdraw(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        ck::FFragment_CompassGym_SourceMembership& State)
    {
        TArray<FCkCompassGym_SourceRow> Proven509, UnusedStress, StagedPingRows;
        FString Why;
        const auto Before = Observe(World, GameState, SourceRoot, State,
            Proven509, UnusedStress, StagedPingRows, Why);
        if (Before == ECkCompassGym_CatalogOutcome::Pending) { return; }
        if (Before != ECkCompassGym_CatalogOutcome::Complete ||
            Proven509.Num() != 509 + State.ActivePings.Num() ||
            !UnusedStress.IsEmpty() ||
            StagedPingRows.Num() != State.StagedPings.Num())
        { Reject(State, Why.IsEmpty() ? TEXT("Pre-withdrawal cohort proof failed") : Why); return; }
        TArray<FCkCompassGym_SourceReceipt> Base, Stress;
        for (const auto& R : State.Active)
        {
            if (R.Kind == ECkCompassGym_SourceKind::Stress) { Stress.Add(R); }
            else { Base.Add(R); }
        }
        if (Base.Num() != 9 || Stress.Num() != 500 ||
            !AllPoi(Stress, SourceRoot, true))
        { Reject(State, TEXT("Withdrawal receipt partition invalid")); return; }

        TArray<FCk_Handle> Changed;
        Changed.Reserve(Stress.Num());
        for (const auto& R : Stress)
        {
            if (!SafeOwned(R.Host, SourceRoot) || !R.Host.Has<ck::FTag_Poi>())
            { const bool Repaired = RollbackPoi(Changed, SourceRoot, true);
              const bool Parity = AllPoi(Stress, SourceRoot, true);
              Reject(State, Repaired && Parity ? TEXT("Stress host changed during withdrawal") :
                  TEXT("Stress host changed; rollback unproven")); return; }
            auto MutableHost = R.Host;
            const bool Removed = MutableHost.Try_Remove<ck::FTag_Poi>();
            if (Removed) { Changed.Add(R.Host); }
            const bool StillOwned = SafeOwned(R.Host, SourceRoot);
            if (!Removed || !StillOwned || R.Host.Has<ck::FTag_Poi>())
            {
                const bool Repaired = RollbackPoi(Changed, SourceRoot, true);
                const bool Parity = AllPoi(Stress, SourceRoot, true);
                Reject(State, Repaired && Parity
                    ? TEXT("Stress tag removal rejected during withdrawal")
                    : TEXT("Stress tag removal failed; rollback unproven"));
                return;
            }
        }
        TArray<FCkCompassGym_SourceReceipt> WholeBase = Base;
        WholeBase.Append(State.ActivePings);
        TArray<FCkCompassGym_SourceRow> Proven9, UnusedStaged, ProvenStagedPings;
        const TArray<FCkCompassGym_SourceReceipt> Empty;
        const auto After = AllPoi(Stress, SourceRoot, false)
            ? UCkCompassGym_SourceCatalog::TryGet_SourceCatalogTransactionWithPingPhases(
                World, GameState, SourceRoot, true, WholeBase, Empty, Stress,
                State.StagedPings, State.FrozenPings,
                Proven9, UnusedStaged, ProvenStagedPings, Why)
            : ECkCompassGym_CatalogOutcome::Invalid;
        if (After != ECkCompassGym_CatalogOutcome::Complete ||
            Proven9.Num() != 9 + State.ActivePings.Num() ||
            !UnusedStaged.IsEmpty() ||
            ProvenStagedPings.Num() != State.StagedPings.Num())
        {
            const bool Repaired = RollbackPoi(Changed, SourceRoot, true);
            const bool Parity = AllPoi(Stress, SourceRoot, true);
            Reject(State, Repaired && Parity
                ? (Why.IsEmpty() ? TEXT("Post-withdrawal cohort proof failed") : Why)
                : TEXT("Post-withdrawal proof failed; rollback unproven"));
            return;
        }
        State.Active = MoveTemp(Base);
        State.FrozenRetiring = MoveTemp(Stress);
        if (State.JournalEnabled)
        {
            State.FrozenStressCursor = 0;
            State.FrozenStressDeadlineSeconds = FPlatformTime::Seconds() + 30.0;
            State.FrozenStressRetirementComplete = false;
        }
        State.CommittedRows = WithoutPings(Proven9);
        State.Result = ECkCompassGym_MembershipResult::Committed;
        State.CurrentRowsDirty = true;
    }

    bool DonePingTimer(const FCkCompassGym_SourceReceipt& R,
        const FCk_Handle& SourceRoot)
    {
        if (!SafeOwned(R.Host, SourceRoot) ||
            !SafeOwned(R.Timer, R.Host) ||
            !R.Timer.Has<ck::FFragment_Timer_Params>() ||
            !R.Timer.Has<ck::FFragment_Timer>() ||
            R.Timer.Has<ck::FTag_Timer_Countdown>())
        { return false; }
        const auto& Params = R.Timer.Get<ck::FFragment_Timer_Params>();
        const auto& Chrono = R.Timer.Get<ck::FFragment_Timer>().Get_Chrono();
        const double Goal = Chrono.Get_GoalValue().Get_Seconds();
        const double Elapsed = Chrono.Get_TimeElapsed().Get_Seconds();
        return Params.Get_Behavior() == ECk_Timer_Behavior::StopOnDone &&
            FMath::IsFinite(Goal) && Goal == 5.0 &&
            FMath::IsFinite(Elapsed) && Elapsed >= 5.0 &&
            Chrono.Get_IsDone();
    }

    void ProcessStagedPings(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        ck::FFragment_CompassGym_SourceMembership& State)
    {
        if (State.StagedPings.IsEmpty()) { return; }
        if (!FMath::IsFinite(State.PingStageDeadlineSeconds) ||
            State.PingStageDeadlineSeconds <= 0.0 ||
            FPlatformTime::Seconds() > State.PingStageDeadlineSeconds)
        { Reject(State, TEXT("Staged Ping materialization timed out")); return; }
        for (const auto& R : State.StagedPings)
        {
            if (DonePingTimer(R, SourceRoot))
            { Reject(State, TEXT("Staged Ping reached Done before admission")); return; }
        }
        TArray<FCkCompassGym_SourceRow> Active, Stress, Pings;
        FString Why;
        const auto Before = Observe(World, GameState, SourceRoot, State,
            Active, Stress, Pings, Why);
        if (Before == ECkCompassGym_CatalogOutcome::Pending) { return; }
        if (Before != ECkCompassGym_CatalogOutcome::Complete ||
            Pings.Num() != State.StagedPings.Num())
        { Reject(State, Why.IsEmpty() ? TEXT("Staged Ping proof failed") : Why); return; }
        TArray<FCk_Handle> Changed;
        Changed.Reserve(State.StagedPings.Num());
        for (const auto& R : State.StagedPings)
        {
            if (!SafeOwned(R.Host, SourceRoot) || R.Host.Has<ck::FTag_Poi>())
            {
                const bool Repaired = RollbackPoi(Changed, SourceRoot, false);
                Reject(State, Repaired ? TEXT("Staged Ping changed during admission") :
                    TEXT("Staged Ping admission rollback unproven"));
                return;
            }
            auto MutableHost = R.Host;
            MutableHost.Add<ck::FTag_Poi>();
            Changed.Add(R.Host);
            if (!SafeOwned(R.Host, SourceRoot) ||
                !R.Host.Has<ck::FTag_Poi>())
            {
                const bool Repaired = RollbackPoi(Changed, SourceRoot, false);
                Reject(State, Repaired ? TEXT("Staged Ping tag add failed") :
                    TEXT("Staged Ping tag add rollback unproven"));
                return;
            }
        }
        TArray<FCkCompassGym_SourceReceipt> Whole = WithActivePings(State);
        for (auto R : State.StagedPings)
        { R.Admitted = true; Whole.Add(R); }
        const TArray<FCkCompassGym_SourceReceipt> Empty;
        TArray<FCkCompassGym_SourceRow> Proven, ProvenStress, UnusedPing;
        const auto After = UCkCompassGym_SourceCatalog::
            TryGet_SourceCatalogTransactionWithPingPhases(
                World, GameState, SourceRoot, true, Whole, State.Staged,
                State.FrozenRetiring, Empty, State.FrozenPings,
                Proven, ProvenStress, UnusedPing, Why);
        if (After != ECkCompassGym_CatalogOutcome::Complete ||
            Proven.Num() != State.Active.Num() + State.ActivePings.Num() +
                State.StagedPings.Num() ||
            ProvenStress.Num() != State.Staged.Num() || !UnusedPing.IsEmpty())
        {
            const bool Repaired = RollbackPoi(Changed, SourceRoot, false);
            Reject(State, Repaired ? (Why.IsEmpty() ?
                TEXT("Post-admission Ping union unproven") : Why) :
                TEXT("Post-admission Ping rollback unproven"));
            return;
        }
        for (auto R : State.StagedPings)
        { R.Admitted = true; State.ActivePings.Add(R); }
        State.StagedPings.Reset();
        State.PingStageDeadlineSeconds = 0.0;
        State.CurrentRowsDirty = true;
    }

    bool ProveFrozenUnion(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        const ck::FFragment_CompassGym_SourceMembership& State,
        FString& OutFailure)
    {
        OutFailure.Reset();
        if (State.ClosedUnproven ||
            !Preflight(World, GameState, SourceRoot) || !Open(World) ||
            !BoundBase(State, World, GameState))
        {
            OutFailure = TEXT("Frozen Ping authority or world closure changed");
            return false;
        }
        TArray<FCkCompassGym_SourceRow> Active, Stress, Pings;
        const auto Outcome = Observe(World, GameState, SourceRoot, State,
            Active, Stress, Pings, OutFailure);
        return Outcome == ECkCompassGym_CatalogOutcome::Complete ||
            (Outcome == ECkCompassGym_CatalogOutcome::Pending &&
             (!State.Staged.IsEmpty() || !State.StagedPings.IsEmpty()));
    }

    bool RetiringOrGone(const FCk_Handle& Handle)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle))
        { return true; }
        return Handle.Has_Any<ck::FTag_DestroyEntity_Initiate,
            ck::FTag_DestroyEntity_EndPlay,
            ck::FTag_DestroyEntity_Teardown,
            ck::FTag_DestroyEntity_Await,
            ck::FTag_DestroyEntity_Finalize>();
    }

    void ProcessFrozenStress(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        ck::FFragment_CompassGym_SourceMembership& State)
    {
        constexpr int32 MaxStressLeafMutationsPerBarrier = 8;
        if (State.FrozenRetiring.IsEmpty()) { return; }
        if (State.FrozenRetiring.Num() != 500 ||
            State.FrozenStressCursor < 0 || State.FrozenStressCursor > 500 ||
            !FMath::IsFinite(State.FrozenStressDeadlineSeconds) ||
            State.FrozenStressDeadlineSeconds <= 0.0)
        { Reject(State, TEXT("Frozen Stress retirement state invalid")); return; }
        if (State.FrozenStressRetirementComplete) { return; }
        if (FPlatformTime::Seconds() > State.FrozenStressDeadlineSeconds)
        { Reject(State, TEXT("Frozen Stress retirement timed out")); return; }
        int32 Mutations = 0;
        while (State.FrozenStressCursor < 500 &&
            Mutations < MaxStressLeafMutationsPerBarrier)
        {
            const auto R = State.FrozenRetiring[State.FrozenStressCursor];
            FString Why;
            if (!ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
            { Reject(State, Why.IsEmpty() ? TEXT("Frozen Stress union unproven") : Why); return; }
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Host))
            { ++State.FrozenStressCursor; continue; }
            const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World);
            TArray<FCk_Handle> Children;
            if (R.Kind != ECkCompassGym_SourceKind::Stress ||
                R.Marker != FCk_Handle{} || R.Timer != FCk_Handle{} ||
                !SafeOwned(R.Host, SourceRoot) || R.Host.Has<ck::FTag_Poi>() ||
                !UCk_EntityScriptGym_Spawn_Runtime_UE::
                    TryGet_AllocatedDirectLifetimeDependents(
                        R.Host, WorldRoot, Children) || !Children.IsEmpty())
            { Reject(State, TEXT("Frozen Stress host is not an exact unexposed leaf")); return; }
            auto MutableHost = R.Host;
            State.RetirementMutationInProgress = true;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableHost);
            State.RetirementMutationInProgress = false;
            if (!RetiringOrGone(R.Host) ||
                !ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
            { Reject(State, Why.IsEmpty() ? TEXT("Frozen Stress leaf action unproven") : Why); return; }
            ++State.FrozenStressCursor;
            ++Mutations;
        }
        if (State.FrozenStressCursor != 500) { return; }
        for (const auto& R : State.FrozenRetiring)
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Host))
            { return; }
        }
        FString Why;
        if (!ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
        { Reject(State, Why.IsEmpty() ? TEXT("Frozen Stress final union unproven") : Why); return; }
        State.FrozenStressRetirementComplete = true;
    }

    void ProcessFrozenPings(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        ck::FFragment_CompassGym_SourceMembership& State)
    {
        if (State.FrozenPings.Num() != State.FrozenPingPhases.Num() ||
            State.FrozenPings.Num() != State.FrozenPingDeadlines.Num())
        { Reject(State, TEXT("Frozen Ping retirement ledger mismatch")); return; }
        for (int32 Index = 0; Index < State.FrozenPings.Num(); ++Index)
        {
            if (!FMath::IsFinite(State.FrozenPingDeadlines[Index]) ||
                State.FrozenPingDeadlines[Index] <= 0.0 ||
                FPlatformTime::Seconds() > State.FrozenPingDeadlines[Index])
            { Reject(State, TEXT("Frozen Ping leaf retirement timed out")); return; }
            FString Why;
            if (!ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
            { Reject(State, Why.IsEmpty() ? TEXT("Frozen Ping union unproven") : Why); return; }
            const auto R = State.FrozenPings[Index];
            auto& Phase = State.FrozenPingPhases[Index];
            const auto Fail = [&State](const TCHAR* Reason)
            { Reject(State, Reason); };
            if (Phase == ck::ECompassGym_PingRetirement::Marker)
            {
                if (!SafeOwned(R.Host, SourceRoot) ||
                    !SafeOwned(R.Marker, R.Host))
                { Fail(TEXT("Retiring Ping Marker owner changed")); return; }
                auto MutableMarker = R.Marker;
                State.RetirementMutationInProgress = true;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableMarker);
                State.RetirementMutationInProgress = false;
                if (!RetiringOrGone(R.Marker) ||
                    !ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
                { Reject(State, Why.IsEmpty() ? TEXT("Retiring Marker action unproven") : Why); return; }
                Phase = ck::ECompassGym_PingRetirement::WaitMarkerGone;
            }
            else if (Phase == ck::ECompassGym_PingRetirement::WaitMarkerGone)
            {
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Marker))
                { Phase = ck::ECompassGym_PingRetirement::Timer; }
            }
            else if (Phase == ck::ECompassGym_PingRetirement::Timer)
            {
                if (!SafeOwned(R.Host, SourceRoot) ||
                    !SafeOwned(R.Timer, R.Host))
                { Fail(TEXT("Retiring Ping Timer owner changed")); return; }
                auto MutableTimer = R.Timer;
                State.RetirementMutationInProgress = true;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableTimer);
                State.RetirementMutationInProgress = false;
                if (!RetiringOrGone(R.Timer) ||
                    !ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
                { Reject(State, Why.IsEmpty() ? TEXT("Retiring Timer action unproven") : Why); return; }
                Phase = ck::ECompassGym_PingRetirement::WaitTimerGone;
            }
            else if (Phase == ck::ECompassGym_PingRetirement::WaitTimerGone)
            {
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Timer))
                { Phase = ck::ECompassGym_PingRetirement::Host; }
            }
            else if (Phase == ck::ECompassGym_PingRetirement::Host)
            {
                TArray<FCk_Handle> Children;
                const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*World);
                if (!SafeOwned(R.Host, SourceRoot) ||
                    UCk_EntityScriptGym_Spawn_Runtime_UE::
                        Get_IsEntityAllocated(R.Marker) ||
                    UCk_EntityScriptGym_Spawn_Runtime_UE::
                        Get_IsEntityAllocated(R.Timer) ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::
                        TryGet_AllocatedDirectLifetimeDependents(
                            R.Host, WorldRoot, Children) ||
                    !Children.IsEmpty())
                { Fail(TEXT("Retiring Ping Host still has dependent or foreign owner")); return; }
                auto MutableHost = R.Host;
                State.RetirementMutationInProgress = true;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableHost);
                State.RetirementMutationInProgress = false;
                if (!RetiringOrGone(R.Host) ||
                    !ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
                { Reject(State, Why.IsEmpty() ? TEXT("Retiring Host action unproven") : Why); return; }
                Phase = ck::ECompassGym_PingRetirement::WaitHostGone;
            }
            else if (Phase == ck::ECompassGym_PingRetirement::WaitHostGone)
            {
                if (!FrozenPingGone(R)) { continue; }
                State.FrozenPings.RemoveAt(Index);
                State.FrozenPingPhases.RemoveAt(Index);
                State.FrozenPingDeadlines.RemoveAt(Index);
                if (!ProveFrozenUnion(World, GameState, SourceRoot, State, Why))
                { Reject(State, Why.IsEmpty() ? TEXT("Post-retirement union unproven") : Why); }
                return;
            }
            else
            { Fail(TEXT("Unknown frozen Ping retirement phase")); return; }
        }
    }
}

bool UCkCompassGym_SourceMembership::TryInitialize_BaseCohort(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    const TArray<FCkCompassGym_SourceReceipt>& BaseReceipts9,
    FString& OutFailureReason)
{
    using namespace ck_compass_membership;
    OutFailureReason.Reset();
    if (!Preflight(World, ExactGameState, SourceRoot) || !Open(World) ||
        BaseReceipts9.Num() != 9)
    { OutFailureReason = TEXT("Base source preflight failed"); return false; }
    if (SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    {
        const auto& Existing = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (Existing.ClosedUnproven ||
            !BoundBase(Existing, World, ExactGameState) ||
            !SameReceipts(Existing.Base, BaseReceipts9))
        {
            OutFailureReason = TEXT("Base source already bound to another cohort or closed");
            return false;
        }
        if (Existing.JournalEnabled) { return true; }
        if (Existing.Result == ECkCompassGym_MembershipResult::Pending ||
            Existing.RetirementMutationInProgress)
        { OutFailureReason = TEXT("Legacy membership is busy"); return false; }
        for (const auto& R : Existing.FrozenRetiring)
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Host))
            {
                OutFailureReason = TEXT("Legacy frozen Stress must retire before journal opt-in");
                return false;
            }
        }
        TArray<FCkCompassGym_SourceRow> Active, Stress, Pings;
        const auto Proof = Observe(World, ExactGameState, SourceRoot, Existing,
            Active, Stress, Pings, OutFailureReason);
        if (Proof != ECkCompassGym_CatalogOutcome::Complete ||
            !Stress.IsEmpty() || !Pings.IsEmpty() ||
            Active.Num() != Existing.Active.Num())
        {
            if (OutFailureReason.IsEmpty())
            { OutFailureReason = TEXT("Legacy retained union not complete for journal opt-in"); }
            return false;
        }
        auto MutableRoot = SourceRoot;
        auto& State = MutableRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        for (auto& R : State.Base) { R.Admitted = true; }
        for (auto& R : State.Active) { R.Admitted = true; }
        if (!State.FrozenRetiring.IsEmpty())
        {
            State.FrozenStressCursor = State.FrozenRetiring.Num();
            State.FrozenStressDeadlineSeconds = FPlatformTime::Seconds() + 30.0;
            State.FrozenStressRetirementComplete = true;
        }
        State.JournalEnabled = true;
        State.CurrentRowsDirty = true;
        OutFailureReason.Reset();
        return true;
    }
    TArray<FCkCompassGym_SourceRow> Rows;
    const auto Proof = UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
        World, ExactGameState, SourceRoot, true, BaseReceipts9,
        Rows, OutFailureReason);
    if (Proof != ECkCompassGym_CatalogOutcome::Complete || Rows.Num() != 9)
    {
        if (OutFailureReason.IsEmpty())
        { OutFailureReason = TEXT("Base nine physical proof is not complete"); }
        return false;
    }
    auto MutableRoot = SourceRoot;
    auto& State = MutableRoot.AddOrGet<ck::FFragment_CompassGym_SourceMembership>();
    State.World = World;
    State.GameState = ExactGameState;
    State.Base = BaseReceipts9;
    State.Active = BaseReceipts9;
    for (auto& R : State.Base) { R.Admitted = true; }
    for (auto& R : State.Active) { R.Admitted = true; }
    State.BaseInitialized = true;
    State.JournalEnabled = true;
    State.HighestAcceptedCatalogId = HighestId(BaseReceipts9);
    State.CurrentRowsDirty = true;
    OutFailureReason.Reset();
    return true;
}

bool UCkCompassGym_SourceMembership::TryStage_Ping(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    const FCkCompassGym_SourceReceipt& StagedPingReceipt,
    FString& OutFailureReason)
{
    using namespace ck_compass_membership;
    OutFailureReason.Reset();
    if (!Preflight(World, ExactGameState, SourceRoot) || !Open(World) ||
        !SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    { OutFailureReason = TEXT("Ping staging source unavailable"); return false; }
    const auto& State = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
    const auto PingTag = FGameplayTag::RequestGameplayTag(
        FName(TEXT("Poi.Category.Ping")), false);
    if (State.ClosedUnproven || !State.JournalEnabled ||
        State.RetirementMutationInProgress ||
        !BoundBase(State, World, ExactGameState) ||
        !PingTag.IsValid() ||
        StagedPingReceipt.CatalogId <= State.HighestAcceptedCatalogId ||
        StagedPingReceipt.Kind != ECkCompassGym_SourceKind::Ping ||
        StagedPingReceipt.Admitted ||
        StagedPingReceipt.ExpectedCategory != PingTag ||
        StagedPingReceipt.Host == FCk_Handle{} ||
        StagedPingReceipt.Marker == FCk_Handle{} ||
        StagedPingReceipt.Timer == FCk_Handle{} ||
        StagedPingReceipt.Host == StagedPingReceipt.Marker ||
        StagedPingReceipt.Host == StagedPingReceipt.Timer ||
        StagedPingReceipt.Marker == StagedPingReceipt.Timer)
    { OutFailureReason = TEXT("Ping staging receipt or bound base invalid"); return false; }
    auto CandidateStaged = State.StagedPings;
    CandidateStaged.Add(StagedPingReceipt);
    auto Whole = WithActivePings(State);
    TArray<FCkCompassGym_SourceRow> ActiveRows, StressRows, PingRows;
    const auto Proof = UCkCompassGym_SourceCatalog::
        TryGet_SourceCatalogTransactionWithPingPhases(
            World, ExactGameState, SourceRoot, true, Whole, State.Staged,
            State.FrozenRetiring, CandidateStaged, State.FrozenPings,
            ActiveRows, StressRows, PingRows, OutFailureReason);
    if (Proof == ECkCompassGym_CatalogOutcome::Invalid)
    { return false; }
    auto MutableRoot = SourceRoot;
    auto& MutableState = MutableRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
    MutableState.StagedPings = MoveTemp(CandidateStaged);
    MutableState.HighestAcceptedCatalogId = StagedPingReceipt.CatalogId;
    if (MutableState.PingStageDeadlineSeconds == 0.0)
    { MutableState.PingStageDeadlineSeconds = FPlatformTime::Seconds() + PendingTimeoutSeconds; }
    OutFailureReason.Reset();
    return true;
}

bool UCkCompassGym_SourceMembership::TryRetire_Ping(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    const FCk_Handle& ExactDoneTimer, FString& OutFailureReason)
{
    using namespace ck_compass_membership;
    OutFailureReason.Reset();
    if (!Preflight(World, ExactGameState, SourceRoot) || !Open(World) ||
        !SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    { OutFailureReason = TEXT("Ping retirement source unavailable"); return false; }
    auto MutableRoot = SourceRoot;
    auto& State = MutableRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
    if (State.ClosedUnproven || !State.JournalEnabled ||
        State.RetirementMutationInProgress ||
        !BoundBase(State, World, ExactGameState))
    { OutFailureReason = TEXT("Ping retirement source binding invalid"); return false; }
    for (const auto& R : State.StagedPings)
    {
        if (R.Timer == ExactDoneTimer && DonePingTimer(R, SourceRoot))
        {
            Reject(State, TEXT("Staged Ping reached Done before ordered admission"));
            OutFailureReason = State.Failure;
            return false;
        }
    }
    int32 Index = INDEX_NONE;
    for (int32 Candidate = 0; Candidate < State.ActivePings.Num(); ++Candidate)
    {
        if (State.ActivePings[Candidate].Timer == ExactDoneTimer)
        { Index = Candidate; break; }
    }
    if (Index == INDEX_NONE || State.CurrentRevision <= 0)
    { OutFailureReason = TEXT("Exact admitted Ping timer not retained"); return false; }
    const auto R = State.ActivePings[Index];
    if (!SafeOwned(R.Host, SourceRoot) ||
        !SafeOwned(R.Marker, R.Host) ||
        !SafeOwned(R.Timer, R.Host) ||
        !R.Timer.Has<ck::FFragment_Timer_Params>() ||
        !R.Timer.Has<ck::FFragment_Timer>() ||
        R.Timer.Has<ck::FTag_Timer_Countdown>() ||
        !R.Host.Has<ck::FTag_Poi>())
    {
        Reject(State, TEXT("Retained Ping Timer or lineage became unproven"));
        OutFailureReason = State.Failure;
        return false;
    }
    const auto& Params = R.Timer.Get<ck::FFragment_Timer_Params>();
    const auto& Chrono = R.Timer.Get<ck::FFragment_Timer>().Get_Chrono();
    const double Goal = Chrono.Get_GoalValue().Get_Seconds();
    const double Elapsed = Chrono.Get_TimeElapsed().Get_Seconds();
    if (Params.Get_Behavior() != ECk_Timer_Behavior::StopOnDone ||
        !FMath::IsFinite(Goal) || Goal != 5.0 ||
        !FMath::IsFinite(Elapsed))
    {
        Reject(State, TEXT("Retained Ping Timer values became unproven"));
        OutFailureReason = State.Failure;
        return false;
    }
    if (!Chrono.Get_IsDone() || Elapsed < 5.0)
    { OutFailureReason = TEXT("Exact retained Ping has not reached Done"); return false; }
    TArray<FCkCompassGym_SourceRow> ActiveRows, StressRows, PingRows;
    FString Why;
    const auto Before = Observe(World, ExactGameState, SourceRoot, State,
        ActiveRows, StressRows, PingRows, Why);
    if (Before == ECkCompassGym_CatalogOutcome::Invalid ||
        (Before == ECkCompassGym_CatalogOutcome::Pending &&
         State.Staged.IsEmpty() && State.StagedPings.IsEmpty()))
    {
        Reject(State, Why.IsEmpty() ? TEXT("Pre-retirement union unproven") : Why);
        OutFailureReason = State.Failure;
        return false;
    }
    auto MutableHost = R.Host;
    const bool Removed = MutableHost.Try_Remove<ck::FTag_Poi>();
    if (!Removed || !SafeOwned(R.Host, SourceRoot) ||
        R.Host.Has<ck::FTag_Poi>())
    {
        Reject(State, TEXT("Ping POI withdrawal failed after accepted retirement"));
        OutFailureReason = State.Failure;
        return false;
    }
    State.ActivePings.RemoveAt(Index);
    State.FrozenPings.Add(R);
    State.FrozenPingPhases.Add(ck::ECompassGym_PingRetirement::Marker);
    State.FrozenPingDeadlines.Add(FPlatformTime::Seconds() + PendingTimeoutSeconds);
    const auto Proof = Observe(World, ExactGameState, SourceRoot, State,
        ActiveRows, StressRows, PingRows, Why);
    if (Proof == ECkCompassGym_CatalogOutcome::Invalid ||
        (Proof == ECkCompassGym_CatalogOutcome::Pending &&
         State.Staged.IsEmpty() && State.StagedPings.IsEmpty()))
    {
        Reject(State, Why.IsEmpty() ? TEXT("Ping retirement union unproven") : Why);
        OutFailureReason = State.Failure;
        return false;
    }
    State.CurrentRowsDirty = true;
    OutFailureReason.Reset();
    return true;
}

bool UCkCompassGym_SourceMembership::TryGet_CurrentRows(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    int64& OutRevision, TArray<FCkCompassGym_SourceRow>& OutActiveRows,
    FString& OutFailureReason)
{
    OutRevision = 0;
    OutActiveRows.Reset();
    OutFailureReason.Reset();
    if (!ck_compass_membership::Preflight(World, ExactGameState, SourceRoot) ||
        !ck_compass_membership::Open(World) ||
        !SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    { OutFailureReason = TEXT("Current source snapshot unavailable"); return false; }
    const auto& State = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
    if (State.ClosedUnproven || !State.JournalEnabled ||
        !ck_compass_membership::BoundBase(State, World, ExactGameState) ||
        State.CurrentRevision <= 0 || State.CurrentRowsDirty)
    { OutFailureReason = TEXT("Current source snapshot not published"); return false; }
    OutRevision = State.CurrentRevision;
    OutActiveRows = State.CurrentRows;
    return true;
}

bool UCkCompassGym_SourceMembership::TryRequest_StressAdmission(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    const TArray<FCkCompassGym_SourceReceipt>& ActiveReceipts,
    const TArray<FCkCompassGym_SourceReceipt>& StagedStressReceipts,
    int64 RequestSerial, FString& OutFailureReason)
{
    using namespace ck_compass_membership;
    OutFailureReason.Reset();
    if (RequestSerial <= 0 || !Preflight(World, ExactGameState, SourceRoot) ||
        !Open(World) ||
        StagedStressReceipts.Num() != 500 || ActiveReceipts.Num() != 9)
    { OutFailureReason = TEXT("Admission request preflight failed"); return false; }
    if (SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    {
        const auto& Existing = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (Existing.ClosedUnproven || Existing.RetirementMutationInProgress ||
            Existing.Result == ECkCompassGym_MembershipResult::Pending ||
            RequestSerial <= Existing.Serial)
        { OutFailureReason = TEXT("Membership busy, closed, or serial replayed"); return false; }
        if (!BoundBase(Existing, World, ExactGameState) ||
            !SameReceipts(Existing.Base, ActiveReceipts))
        { OutFailureReason = TEXT("Prior membership authority changed"); return false; }
        const bool InitialIdle =
            Existing.Request == ck::ECompassGym_SourceRequest::None &&
            Existing.Result == ECkCompassGym_MembershipResult::Unavailable;
        const bool Withdrawn =
            Existing.Result == ECkCompassGym_MembershipResult::Committed &&
            Existing.Request == ck::ECompassGym_SourceRequest::Withdraw &&
            SameReceipts(Existing.Active, ActiveReceipts);
        if (!InitialIdle && !Withdrawn)
        { OutFailureReason = TEXT("Previous stress membership is not withdrawn"); return false; }
        for (const auto& R : Existing.FrozenRetiring)
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Host))
            { OutFailureReason = TEXT("Frozen stress retirement is still allocated"); return false; }
        }
    }
    TArray<FCkCompassGym_SourceReceipt> Whole = ActiveReceipts;
    TArray<FCkCompassGym_SourceReceipt> StagedPings, FrozenPings;
    if (SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    {
        const auto& Existing = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (Existing.JournalEnabled) { Whole = Existing.Base; }
        Whole.Append(Existing.ActivePings);
        StagedPings = Existing.StagedPings;
        FrozenPings = Existing.FrozenPings;
    }
    TArray<FCkCompassGym_SourceRow> ActiveRows, StagedRows, StagedPingRows;
    const TArray<FCkCompassGym_SourceReceipt> Empty;
    const auto Proof = UCkCompassGym_SourceCatalog::
        TryGet_SourceCatalogTransactionWithPingPhases(
            World, ExactGameState, SourceRoot, true, Whole,
            StagedStressReceipts, Empty, StagedPings, FrozenPings,
            ActiveRows, StagedRows, StagedPingRows, OutFailureReason);
    if (Proof == ECkCompassGym_CatalogOutcome::Invalid)
    { return false; }
    auto MutableRoot = SourceRoot;
    auto& State = MutableRoot.AddOrGet<ck::FFragment_CompassGym_SourceMembership>();
    State.World = World;
    State.GameState = ExactGameState;
    if (!State.BaseInitialized)
    {
        State.Base = ActiveReceipts;
        State.BaseInitialized = true;
        State.HighestAcceptedCatalogId = HighestId(ActiveReceipts);
    }
    State.Serial = RequestSerial;
    State.DeadlineSeconds = FPlatformTime::Seconds() + PendingTimeoutSeconds;
    State.Request = ck::ECompassGym_SourceRequest::Admit;
    State.Result = ECkCompassGym_MembershipResult::Pending;
    State.Failure.Reset();
    State.Active = State.JournalEnabled ? State.Base : ActiveReceipts;
    State.Staged = StagedStressReceipts;
    State.HighestAcceptedCatalogId = FMath::Max(
        State.HighestAcceptedCatalogId, HighestId(StagedStressReceipts));
    State.CommittedRows.Reset();
    State.FrozenRetiring.Reset();
    State.FrozenStressCursor = 0;
    State.FrozenStressDeadlineSeconds = 0.0;
    State.FrozenStressRetirementComplete = false;
    OutFailureReason.Reset();
    return true;
}

bool UCkCompassGym_SourceMembership::TryRequest_StressWithdrawal(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    const TArray<FCkCompassGym_SourceReceipt>& ActiveReceipts509,
    int64 RequestSerial, FString& OutFailureReason)
{
    using namespace ck_compass_membership;
    OutFailureReason.Reset();
    if (RequestSerial <= 0 || !Preflight(World, ExactGameState, SourceRoot) ||
        !Open(World) ||
        ActiveReceipts509.Num() != 509)
    { OutFailureReason = TEXT("Withdrawal request preflight failed"); return false; }
    if (SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    {
        const auto& Existing = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (Existing.ClosedUnproven || Existing.RetirementMutationInProgress ||
            Existing.Result == ECkCompassGym_MembershipResult::Pending ||
            RequestSerial <= Existing.Serial)
        { OutFailureReason = TEXT("Membership busy, closed, or serial replayed"); return false; }
        if (!BoundBase(Existing, World, ExactGameState))
        { OutFailureReason = TEXT("Prior membership authority changed"); return false; }
        if (Existing.Result != ECkCompassGym_MembershipResult::Committed ||
            Existing.Request != ck::ECompassGym_SourceRequest::Admit ||
            !SameReceipts(Existing.Active, ActiveReceipts509))
        { OutFailureReason = TEXT("Withdrawal differs from committed stress cohort"); return false; }
    }
    else
    { OutFailureReason = TEXT("No committed stress admission to withdraw"); return false; }
    const auto& Existing = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
    TArray<FCkCompassGym_SourceReceipt> Whole =
        Existing.JournalEnabled ? Existing.Active : ActiveReceipts509;
    Whole.Append(Existing.ActivePings);
    TArray<FCkCompassGym_SourceRow> Rows, StagedStressRows, StagedPingRows;
    const TArray<FCkCompassGym_SourceReceipt> Empty;
    const auto Proof = UCkCompassGym_SourceCatalog::
        TryGet_SourceCatalogTransactionWithPingPhases(
            World, ExactGameState, SourceRoot, true, Whole, Empty, Empty,
            Existing.StagedPings, Existing.FrozenPings,
            Rows, StagedStressRows, StagedPingRows, OutFailureReason);
    if (Proof == ECkCompassGym_CatalogOutcome::Invalid)
    { return false; }
    auto MutableRoot = SourceRoot;
    auto& State = MutableRoot.AddOrGet<ck::FFragment_CompassGym_SourceMembership>();
    State.World = World;
    State.GameState = ExactGameState;
    State.Serial = RequestSerial;
    State.DeadlineSeconds = FPlatformTime::Seconds() + PendingTimeoutSeconds;
    State.Request = ck::ECompassGym_SourceRequest::Withdraw;
    State.Result = ECkCompassGym_MembershipResult::Pending;
    State.Failure.Reset();
    if (!State.JournalEnabled) { State.Active = ActiveReceipts509; }
    State.Staged.Reset();
    State.CommittedRows.Reset();
    State.FrozenRetiring.Reset();
    OutFailureReason.Reset();
    return true;
}

ECkCompassGym_MembershipResult UCkCompassGym_SourceMembership::Get_Result(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    int64 RequestSerial, TArray<FCkCompassGym_SourceRow>& OutActiveRows,
    TArray<FCkCompassGym_SourceReceipt>& OutFrozenRetiringStressReceipts,
    FString& OutFailureReason)
{
    OutActiveRows.Reset();
    OutFrozenRetiringStressReceipts.Reset();
    OutFailureReason.Reset();
    if (RequestSerial <= 0 || !ck_compass_membership::Preflight(
            World, ExactGameState, SourceRoot) ||
        !SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
    { return ECkCompassGym_MembershipResult::Unavailable; }
    const auto& State = SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
    if (State.World.Get() != World || State.GameState.Get() != ExactGameState ||
        State.Serial != RequestSerial)
    { return ECkCompassGym_MembershipResult::Unavailable; }
    if (State.Result == ECkCompassGym_MembershipResult::Rejected)
    { OutFailureReason = State.Failure; return State.Result; }
    if (!ck_compass_membership::Open(World))
    {
        OutFailureReason = TEXT("Source membership world admission or projection closed");
        return ECkCompassGym_MembershipResult::Unavailable;
    }
    if (State.Result == ECkCompassGym_MembershipResult::Committed)
    {
        OutActiveRows = State.CommittedRows;
        OutFrozenRetiringStressReceipts = State.FrozenRetiring;
    }
    return State.Result;
}

void ck::FProcessor_CompassGym_SourceMembership::ForEachEntity(
    TimeType, HandleType SourceRoot,
    FFragment_CompassGym_SourceMembership& State)
{
    if (State.ClosedUnproven || !State.BaseInitialized) { return; }
    if (!State.JournalEnabled &&
        State.Result != ECkCompassGym_MembershipResult::Pending)
    { return; }
    auto* World = State.World.Get();
    auto* GameState = State.GameState.Get();
    if (!ck_compass_membership::Preflight(World, GameState, SourceRoot) ||
        !ck_compass_membership::Open(World) ||
        !ck_compass_membership::BoundBase(State, World, GameState))
    { ck_compass_membership::Reject(State, TEXT("Accepted source authority or base changed")); return; }
    if (State.Result == ECkCompassGym_MembershipResult::Pending)
    {
        if (State.Serial <= 0 ||
            FPlatformTime::Seconds() > State.DeadlineSeconds)
        { ck_compass_membership::Reject(State, TEXT("Membership proof timed out")); return; }
        if (State.Request == ECompassGym_SourceRequest::Admit)
        { ck_compass_membership::ProcessAdmit(World, GameState, SourceRoot, State); }
        else if (State.Request == ECompassGym_SourceRequest::Withdraw)
        { ck_compass_membership::ProcessWithdraw(World, GameState, SourceRoot, State); }
        else
        { ck_compass_membership::Reject(State, TEXT("Unknown membership request")); }
        if (State.ClosedUnproven) { return; }
    }
    if (!State.JournalEnabled) { return; }
    ck_compass_membership::ProcessFrozenStress(World, GameState, SourceRoot, State);
    if (State.ClosedUnproven) { return; }
    ck_compass_membership::ProcessFrozenPings(World, GameState, SourceRoot, State);
    if (State.ClosedUnproven) { return; }
    ck_compass_membership::ProcessStagedPings(World, GameState, SourceRoot, State);
    if (State.ClosedUnproven) { return; }
    TArray<FCkCompassGym_SourceRow> Active, Stress, Pings;
    FString Why;
    const auto Outcome = ck_compass_membership::Observe(World, GameState,
        SourceRoot, State, Active, Stress, Pings, Why);
    if (Outcome == ECkCompassGym_CatalogOutcome::Invalid)
    { ck_compass_membership::Reject(State, Why.IsEmpty() ?
        TEXT("Accepted source union unproven") : Why); return; }
    if (Outcome == ECkCompassGym_CatalogOutcome::Complete &&
        (State.CurrentRowsDirty || State.CurrentRevision == 0 ||
         !ck_compass_membership::SameRows(State.CurrentRows, Active)))
    {
        if (State.CurrentRevision == MAX_int64)
        { ck_compass_membership::Reject(State, TEXT("Current source revision exhausted")); return; }
        State.CurrentRows = MoveTemp(Active);
        ++State.CurrentRevision;
        State.CurrentRowsDirty = false;
    }
}
