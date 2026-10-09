#include "CkCompassGym_SourceMembership.h"
#include "CkCompassGym_SourceMembership_Private.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkGym_AdmissionRuntime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkPoi/CkPoi_Fragment.h"
#include "CkCompass/CkCompass_Utils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"

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
        const auto WorldRoot =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        return !UCkGym_AdmissionRuntime_UE::Get_IsClosedUnproven(WorldRoot) &&
            !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(WorldRoot);
    }

    void Reject(ck::FFragment_CompassGym_SourceMembership& State,
        const FString& Why)
    {
        State.Result = ECkCompassGym_MembershipResult::Rejected;
        State.ClosedUnproven = true;
        State.Failure = Why.IsEmpty() ? TEXT("Source membership unproven") : Why;
        State.CommittedRows.Reset();
        State.FrozenRetiring.Reset();
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

    void ProcessAdmit(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        ck::FFragment_CompassGym_SourceMembership& State)
    {
        TArray<FCkCompassGym_SourceRow> ActiveRows, StagedRows;
        FString Why;
        const TArray<FCkCompassGym_SourceReceipt> Empty;
        const auto Before = UCkCompassGym_SourceCatalog::TryGet_SourceCatalogTransaction(
            World, GameState, SourceRoot, true, State.Active, State.Staged, Empty,
            ActiveRows, StagedRows, Why);
        if (Before == ECkCompassGym_CatalogOutcome::Pending) { return; }
        if (Before != ECkCompassGym_CatalogOutcome::Complete ||
            ActiveRows.Num() != 9 || StagedRows.Num() != 500 ||
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
        TArray<FCkCompassGym_SourceRow> Proven509;
        const auto After = AllPoi(State.Staged, SourceRoot, true)
            ? UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
                World, GameState, SourceRoot, true, Combined, Proven509, Why)
            : ECkCompassGym_CatalogOutcome::Invalid;
        if (After != ECkCompassGym_CatalogOutcome::Complete ||
            Proven509.Num() != 509)
        {
            const bool Repaired = RollbackPoi(Changed, SourceRoot, false);
            const bool Parity = AllPoi(State.Staged, SourceRoot, false);
            Reject(State, Repaired && Parity
                ? (Why.IsEmpty() ? TEXT("Post-admission cohort proof failed") : Why)
                : TEXT("Post-admission proof failed; rollback unproven"));
            return;
        }
        State.Active = MoveTemp(Combined);
        State.Staged.Reset();
        State.CommittedRows = MoveTemp(Proven509);
        State.Result = ECkCompassGym_MembershipResult::Committed;
    }

    void ProcessWithdraw(UWorld* World, AActor* GameState,
        const FCk_Handle& SourceRoot,
        ck::FFragment_CompassGym_SourceMembership& State)
    {
        TArray<FCkCompassGym_SourceRow> Proven509;
        FString Why;
        const auto Before = UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
            World, GameState, SourceRoot, true, State.Active, Proven509, Why);
        if (Before == ECkCompassGym_CatalogOutcome::Pending) { return; }
        if (Before != ECkCompassGym_CatalogOutcome::Complete ||
            Proven509.Num() != 509)
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
        TArray<FCkCompassGym_SourceRow> Proven9, UnusedStaged;
        const TArray<FCkCompassGym_SourceReceipt> Empty;
        const auto After = AllPoi(Stress, SourceRoot, false)
            ? UCkCompassGym_SourceCatalog::TryGet_SourceCatalogTransaction(
                World, GameState, SourceRoot, true, Base, Empty, Stress,
                Proven9, UnusedStaged, Why)
            : ECkCompassGym_CatalogOutcome::Invalid;
        if (After != ECkCompassGym_CatalogOutcome::Complete ||
            Proven9.Num() != 9 || !UnusedStaged.IsEmpty())
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
        State.CommittedRows = MoveTemp(Proven9);
        State.Result = ECkCompassGym_MembershipResult::Committed;
    }
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
        if (Existing.ClosedUnproven ||
            Existing.Result == ECkCompassGym_MembershipResult::Pending ||
            RequestSerial <= Existing.Serial)
        { OutFailureReason = TEXT("Membership busy, closed, or serial replayed"); return false; }
        if (Existing.World.Get() != World ||
            Existing.GameState.Get() != ExactGameState)
        { OutFailureReason = TEXT("Prior membership authority changed"); return false; }
        if (Existing.Result != ECkCompassGym_MembershipResult::Committed ||
            Existing.Request != ck::ECompassGym_SourceRequest::Withdraw ||
            !SameReceipts(Existing.Active, ActiveReceipts))
        { OutFailureReason = TEXT("Previous stress membership is not withdrawn"); return false; }
        for (const auto& R : Existing.FrozenRetiring)
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(R.Host))
            { OutFailureReason = TEXT("Frozen stress retirement is still allocated"); return false; }
        }
    }
    TArray<FCkCompassGym_SourceRow> ActiveRows, StagedRows;
    const TArray<FCkCompassGym_SourceReceipt> Empty;
    const auto Proof = UCkCompassGym_SourceCatalog::TryGet_SourceCatalogTransaction(
        World, ExactGameState, SourceRoot, true, ActiveReceipts,
        StagedStressReceipts, Empty, ActiveRows, StagedRows, OutFailureReason);
    if (Proof == ECkCompassGym_CatalogOutcome::Invalid)
    { return false; }
    auto MutableRoot = SourceRoot;
    auto& State = MutableRoot.AddOrGet<ck::FFragment_CompassGym_SourceMembership>();
    State.World = World;
    State.GameState = ExactGameState;
    State.Serial = RequestSerial;
    State.DeadlineSeconds = FPlatformTime::Seconds() + PendingTimeoutSeconds;
    State.Request = ck::ECompassGym_SourceRequest::Admit;
    State.Result = ECkCompassGym_MembershipResult::Pending;
    State.Failure.Reset();
    State.Active = ActiveReceipts;
    State.Staged = StagedStressReceipts;
    State.CommittedRows.Reset();
    State.FrozenRetiring.Reset();
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
        if (Existing.ClosedUnproven ||
            Existing.Result == ECkCompassGym_MembershipResult::Pending ||
            RequestSerial <= Existing.Serial)
        { OutFailureReason = TEXT("Membership busy, closed, or serial replayed"); return false; }
        if (Existing.World.Get() != World ||
            Existing.GameState.Get() != ExactGameState)
        { OutFailureReason = TEXT("Prior membership authority changed"); return false; }
        if (Existing.Result != ECkCompassGym_MembershipResult::Committed ||
            Existing.Request != ck::ECompassGym_SourceRequest::Admit ||
            !SameReceipts(Existing.Active, ActiveReceipts509))
        { OutFailureReason = TEXT("Withdrawal differs from committed stress cohort"); return false; }
    }
    else
    { OutFailureReason = TEXT("No committed stress admission to withdraw"); return false; }
    TArray<FCkCompassGym_SourceRow> Rows;
    const auto Proof = UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
        World, ExactGameState, SourceRoot, true, ActiveReceipts509, Rows,
        OutFailureReason);
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
    State.Active = ActiveReceipts509;
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
    if (State.Result != ECkCompassGym_MembershipResult::Pending) { return; }
    auto* World = State.World.Get();
    auto* GameState = State.GameState.Get();
    if (!ck_compass_membership::Preflight(World, GameState, SourceRoot) ||
        !ck_compass_membership::Open(World) ||
        State.Serial <= 0 || State.ClosedUnproven ||
        FPlatformTime::Seconds() > State.DeadlineSeconds)
    { ck_compass_membership::Reject(State, TEXT("Membership proof timed out or authority changed")); return; }
    if (State.Request == ECompassGym_SourceRequest::Admit)
    { ck_compass_membership::ProcessAdmit(World, GameState, SourceRoot, State); }
    else if (State.Request == ECompassGym_SourceRequest::Withdraw)
    { ck_compass_membership::ProcessWithdraw(World, GameState, SourceRoot, State); }
    else
    { ck_compass_membership::Reject(State, TEXT("Unknown membership request")); }
}
