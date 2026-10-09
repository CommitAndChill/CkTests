#include "CkCompassGym_PingTtlProbe.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkPoi/CkPoi_Fragment.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace ck_compass_ping_ttl_probe
{
    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    bool Live(const FCk_Handle& H)
    {
        return Allocated(H) && ck::IsValid(H) &&
            !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    bool Direct(const FCk_Handle& Child, const FCk_Handle& Parent)
    {
        return Live(Child) && Live(Parent) &&
            Child.Get_RegistryView().Get_RegistryHandle() ==
                Parent.Get_RegistryView().Get_RegistryHandle() &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Parent;
    }
}

bool UCkCompassGym_PingTtlProbe::Arm(UWorld* World, AActor* GameState,
    const FCk_Handle& InSourceRoot,
    const TArray<FCkCompassGym_SourceReceipt>& ActiveTen)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    using namespace ck_compass_ping_ttl_probe;
    if (bArmed || bFailed || DoneCount != 0 || !IsInGameThread() ||
        !IsValid(World) || !World->IsGameWorld() || World->IsNetMode(NM_Client) ||
        World->bIsTearingDown || World->IsBeingCleanedUp() ||
        !IsValid(GameState) || GameState->IsActorBeingDestroyed() ||
        GameState->GetWorld() != World || World->GetGameState() != GameState ||
        ActiveTen.Num() != 10)
    { return false; }
    const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::
        TryGet_TransientEntityForWorld(*World);
    const auto& Ping = ActiveTen.Last();
    if (!Live(Root) || !Direct(InSourceRoot, Root) ||
        Ping.Kind != ECkCompassGym_SourceKind::Ping || Ping.CatalogId <= 0 ||
        !Direct(Ping.Host, InSourceRoot) ||
        !Direct(Ping.Marker, Ping.Host) ||
        !Direct(Ping.Timer, Ping.Host) ||
        !Ping.Host.Has<ck::FTag_Poi>() ||
        Ping.Marker.Has<ck::FTag_Poi>() || Ping.Timer.Has<ck::FTag_Poi>() ||
        Ping.Timer.Has<ck::FTag_Timer_Countdown>() ||
        !Ping.Timer.Has<ck::FFragment_Timer_Params>() ||
        !Ping.Timer.Has<ck::FFragment_Timer>() ||
        !Ping.Timer.Has<ck::FTag_Timer_NeedsUpdate>())
    { return false; }
    const auto& TimerParams = Ping.Timer.Get<ck::FFragment_Timer_Params>();
    const auto& TimerCurrent = Ping.Timer.Get<ck::FFragment_Timer>();
    if (TimerParams.Get_Behavior() != ECk_Timer_Behavior::StopOnDone ||
        TimerCurrent.Get_Chrono().Get_GoalValue().Get_Seconds() != 5.0 ||
        TimerCurrent.Get_Chrono().Get_IsDone() ||
        !FMath::IsFinite(TimerCurrent.Get_Chrono().Get_TimeElapsed().Get_Seconds()) ||
        TimerCurrent.Get_Chrono().Get_TimeElapsed().Get_Seconds() < 0.0 ||
        TimerCurrent.Get_Chrono().Get_TimeElapsed().Get_Seconds() >= 5.0)
    { return false; }
    TArray<FCkCompassGym_SourceRow> Rows;
    FString Failure;
    const auto Outcome = UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
        World, GameState, InSourceRoot, true, ActiveTen, Rows, Failure);
    if (Outcome != ECkCompassGym_CatalogOutcome::Complete ||
        Rows.Num() != 10 || !Failure.IsEmpty() ||
        !Rows.ContainsByPredicate([&Ping](const FCkCompassGym_SourceRow& Row) {
            return Row.CatalogId == Ping.CatalogId &&
                Row.Kind == ECkCompassGym_SourceKind::Ping && Row.HasMarker;
        }))
    { return false; }
    ExactWorld = World;
    ExactGameState = GameState;
    WorldRoot = Root;
    SourceRoot = InSourceRoot;
    AdmittedReceipt = Ping;
    AdmittedReceipt.Admitted = true;
    bArmed = true;
    return true;
#else
    (void)World;
    (void)GameState;
    (void)InSourceRoot;
    (void)ActiveTen;
    return false;
#endif
}

void UCkCompassGym_PingTtlProbe::OnDone(FCk_Handle_Timer InTimer,
    FCk_Chrono InChrono, FCk_Time InDeltaT)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    using namespace ck_compass_ping_ttl_probe;
    ++DoneCount;
    if (DoneCount != 1 || !bArmed || bFailed || !IsInGameThread())
    { bFailed = true; return; }
    auto* World = ExactWorld.Get();
    auto* GameState = ExactGameState.Get();
    if (!IsValid(World) || !World->IsGameWorld() ||
        World->IsNetMode(NM_Client) || World->bIsTearingDown ||
        World->IsBeingCleanedUp() || !IsValid(GameState) ||
        GameState->IsActorBeingDestroyed() || GameState->GetWorld() != World ||
        World->GetGameState() != GameState ||
        UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World) != WorldRoot ||
        !Direct(SourceRoot, WorldRoot) ||
        FCk_Handle(InTimer) != AdmittedReceipt.Timer ||
        !Direct(AdmittedReceipt.Host, SourceRoot) ||
        !Direct(AdmittedReceipt.Marker, AdmittedReceipt.Host) ||
        !Direct(AdmittedReceipt.Timer, AdmittedReceipt.Host) ||
        !AdmittedReceipt.Host.Has<ck::FTag_Poi>() ||
        AdmittedReceipt.Marker.Has<ck::FTag_Poi>() ||
        AdmittedReceipt.Timer.Has<ck::FTag_Poi>() ||
        AdmittedReceipt.Timer.Has<ck::FTag_Timer_Countdown>() ||
        !AdmittedReceipt.Timer.Has<ck::FFragment_Timer_Params>() ||
        !InChrono.Get_IsDone() ||
        InChrono.Get_GoalValue().Get_Seconds() != 5.0 ||
        !FMath::IsFinite(InChrono.Get_TimeElapsed().Get_Seconds()) ||
        InChrono.Get_TimeElapsed().Get_Seconds() < 5.0 ||
        !FMath::IsFinite(InDeltaT.Get_Seconds()) ||
        InDeltaT.Get_Seconds() < 0.0)
    { bFailed = true; return; }
    if (AdmittedReceipt.Timer.Get<ck::FFragment_Timer_Params>()
            .Get_Behavior() != ECk_Timer_Behavior::StopOnDone)
    { bFailed = true; return; }
    bExactWorldCallback = true;
    bHostLiveBeforeDestroy = Live(AdmittedReceipt.Host);
    DoneElapsedSeconds = InChrono.Get_TimeElapsed().Get_Seconds();
    auto MutableHost = AdmittedReceipt.Host;
    const bool Removed = MutableHost.Try_Remove<ck::FTag_Poi>();
    if (!Allocated(AdmittedReceipt.Host) || !Removed ||
        !Direct(SourceRoot, WorldRoot) ||
        !Direct(AdmittedReceipt.Host, SourceRoot) ||
        AdmittedReceipt.Host.Has<ck::FTag_Poi>())
    { bFailed = true; return; }
    FrozenReceipt = AdmittedReceipt;
    bFrozeBeforeDestroy = true;
    auto ToDestroy = AdmittedReceipt.Host;
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(ToDestroy);
    bRequestedDestroy = true;
#else
    (void)InTimer;
    (void)InChrono;
    (void)InDeltaT;
#endif
}
