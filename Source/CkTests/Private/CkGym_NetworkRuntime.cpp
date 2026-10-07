#include "CkGym_NetworkRuntime.h"

#include "CkTests/CkTests_Log.h"

#include "CkCore/Macros/CkMacros.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <Containers/Ticker.h>
#include <HAL/PlatformTime.h>
#include <Net/UnrealNetwork.h>
#include <UObject/SoftObjectPath.h>

auto
    ACkGym_GameState_UE::
    PublishGymCatalog()
    -> bool
{
    auto* World = GetWorld();
    const auto WorldIsValid = HasAuthority() && IsValid(World) &&
        NOT World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && World->IsGameWorld() &&
        IsValid(World->GetAuthGameMode()) && World->GetGameState() == this;
    if (NOT WorldIsValid)
    { return false; }

    auto* GameInstance = World->GetGameInstance();
    if (NOT IsValid(GameInstance) || GameInstance->GetWorld() != World)
    { return false; }
    auto* Registry = IsValid(GameInstance) ? GameInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    if (NOT IsValid(Registry) ||
        NOT UCkGym_Registry_Subsystem::IsValidCatalog(Registry->Get_Entries(), Registry->Get_CurrentGymIndex()))
    { return false; }

    // This may resolve an accepted pending travel, or identify a directly opened gym map.
    Registry->TryRecordLandedGym(World);

    auto Next = FCkGym_CatalogSnapshot{};
    Next.Entries = Registry->Get_Entries();
    for (const auto& Entry : Next.Entries)
    { Next.GameModeClassPaths.Add(Entry.GameModeClass.Get()->GetPathName()); }
    Next.CurrentGymIndex = Registry->Get_CurrentGymIndex();
    Next.SuppressHUDDuringStartup = Registry->Get_SuppressHUDDuringStartup();
    Next.Published = true;
    _Catalog = MoveTemp(Next);
    _CatalogAccepted = true;
    Registry->ResolveLocalTravelOnCatalog(World, this, _Catalog.Entries, _Catalog.CurrentGymIndex);
    ForceNetUpdate();
    return true;
}

auto
    ACkGym_GameState_UE::
    Get_HasPublishedCatalog() const
    -> bool
{
    return _Catalog.Published && _CatalogAccepted;
}

auto
    ACkGym_GameState_UE::
    OnRep_Catalog()
    -> void
{
    StopCatalogRetry();
    _CatalogAccepted = false;
    _CatalogRetryDeadline = FPlatformTime::Seconds() + 10.0;
    _NextCatalogRetryTime = FPlatformTime::Seconds() + 0.5;
    if (TryApplyCatalog())
    { return; }

    if (_Catalog.Published)
    {
        ck::tests::Warning(TEXT("[GymRegistry] catalog not ready; retrying for up to 10 seconds"));
        _CatalogRetryHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &ACkGym_GameState_UE::PollCatalogRetry), 0.5f);
    }
    else
    { ck::tests::Warning(TEXT("[GymRegistry] rejected unpublished catalog")); }
}

auto
    ACkGym_GameState_UE::
    TryApplyCatalog()
    -> bool
{
    if (NOT _Catalog.Published || _Catalog.Entries.IsEmpty() ||
        _Catalog.GameModeClassPaths.Num() != _Catalog.Entries.Num())
    { return false; }

    auto* World = GetWorld();
    const auto WorldIsValid = IsValid(World) &&
        NOT World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && World->IsGameWorld();
    auto* GameInstance = WorldIsValid ? World->GetGameInstance() : nullptr;
    auto* Registry = IsValid(GameInstance) ? GameInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    if (NOT IsValid(Registry) || GameInstance->GetWorld() != World || World->GetGameState() != this)
    { return false; }

    auto Candidate = _Catalog.Entries;
    for (auto Index = 0; Index < Candidate.Num(); ++Index)
    {
        const auto& ClassPath = _Catalog.GameModeClassPaths[Index];
        if (ClassPath.IsEmpty())
        { return false; }

        auto* Class = Candidate[Index].GameModeClass.Get();
        if (IsValid(Class) && Class->GetPathName() != ClassPath)
        { return false; }
        if (NOT IsValid(Class))
        { Candidate[Index].GameModeClass = FSoftClassPath{ClassPath}.TryLoadClass<AGameModeBase>(); }
    }

    if (NOT Registry->TryReplaceReplica(World, Candidate,
        _Catalog.CurrentGymIndex, _Catalog.SuppressHUDDuringStartup))
    { return false; }

    _CatalogAccepted = true;
    Registry->ResolveLocalTravelOnCatalog(World, this, Candidate, _Catalog.CurrentGymIndex);
    return true;
}

auto
    ACkGym_GameState_UE::
    PollCatalogRetry(float InDeltaSeconds)
    -> bool
{
    const auto Now = FPlatformTime::Seconds();
    if (Now >= _CatalogRetryDeadline)
    {
        _CatalogRetryHandle.Reset();
        ck::tests::Warning(TEXT("[GymRegistry] catalog readiness timed out; client menu remains disabled"));
        return false;
    }
    if (Now < _NextCatalogRetryTime)
    { return true; }
    _NextCatalogRetryTime = Now + 0.5;
    if (TryApplyCatalog())
    { _CatalogRetryHandle.Reset(); return false; }
    return true;
}

auto
    ACkGym_GameState_UE::
    StopCatalogRetry()
    -> void
{
    if (_CatalogRetryHandle.IsValid())
    { FTSTicker::GetCoreTicker().RemoveTicker(_CatalogRetryHandle); }
    _CatalogRetryHandle.Reset();
}

auto
    ACkGym_GameState_UE::
    EndPlay(const EEndPlayReason::Type EndPlayReason)
    -> void
{
    StopCatalogRetry();
    Super::EndPlay(EndPlayReason);
}

auto
    ACkGym_GameState_UE::
    GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps) const
    -> void
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACkGym_GameState_UE, _Catalog);
}

ACkGym_PlayerController_UE::ACkGym_PlayerController_UE()
{
    bReplicates = true;
}

auto
    ACkGym_PlayerController_UE::
    GetLocalRegistry() const
    -> UCkGym_Registry_Subsystem*
{
    auto* World = GetWorld();
    auto* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
    auto* OwningLocalPlayer = GetLocalPlayer();
    const auto OwnerIsValid = NOT HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        IsValid(World) && NOT World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        World->IsGameWorld() && IsLocalController() && IsValid(OwningLocalPlayer) && IsValid(GameInstance) &&
        GameInstance->GetWorld() == World && GameInstance->GetLocalPlayers().Contains(OwningLocalPlayer) &&
        OwningLocalPlayer->GetWorld() == World && OwningLocalPlayer->GetPlayerController(World) == this;
    return OwnerIsValid ? GameInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
}

auto
    ACkGym_PlayerController_UE::
    Get_LastTravelResult() const
    -> ECkGym_TravelResult
{
    auto* Registry = GetLocalRegistry();
    return IsValid(Registry) ? Registry->GetLocalTravelResult(GetLocalPlayer()) : ECkGym_TravelResult::None;
}

auto
    ACkGym_PlayerController_UE::
    Get_LastTravelDetail() const
    -> FString
{
    auto* Registry = GetLocalRegistry();
    return IsValid(Registry) ? Registry->GetLocalTravelDetail(GetLocalPlayer()) : FString{};
}

auto
    ACkGym_PlayerController_UE::
    TryQueueTravelRequest(
        const FString& InRegisteredName)
    -> bool
{
    auto* Registry = GetLocalRegistry();
    if (NOT IsValid(Registry) || InRegisteredName.IsEmpty())
    { return false; }

    auto RequestId = 0;
    if (NOT Registry->TryBeginLocalTravel(GetLocalPlayer(), InRegisteredName, RequestId))
    { return false; }
    Server_RequestTravelToGym(InRegisteredName, RequestId);
    return true;
}

auto
    ACkGym_PlayerController_UE::
    Client_ReportTravelResult_Implementation(
        int32 InRequestId,
        ECkGym_TravelResult InResult,
        const FString& InDetail)
    -> void
{
    if (auto* Registry = GetLocalRegistry())
    { Registry->TryApplyLocalTravelResult(GetLocalPlayer(), InRequestId, InResult, InDetail); }
}

auto
    ACkGym_PlayerController_UE::
    Server_RequestTravelToGym_Implementation(
        const FString& InRegisteredName,
        int32 InRequestId)
    -> void
{
    auto* World = GetWorld();
    const auto WorldIsValid = IsValid(World) &&
        NOT World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && World->IsGameWorld();
    if (NOT HasAuthority() || NOT WorldIsValid)
    { Client_ReportTravelResult(InRequestId, ECkGym_TravelResult::Rejected, TEXT("invalid world or authority")); return; }

    auto* State = World->GetGameState<ACkGym_GameState_UE>();
    auto* GameInstance = World->GetGameInstance();
    auto* Registry = IsValid(GameInstance) ? GameInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    const auto RequestIsValid = IsValid(State) && State->Get_HasPublishedCatalog() && IsValid(Registry) &&
        Registry->Find_GymIndexByName(InRegisteredName) != INDEX_NONE;
    if (NOT RequestIsValid)
    { Client_ReportTravelResult(InRequestId, ECkGym_TravelResult::Rejected, TEXT("unregistered gym or unpublished catalog")); return; }

    if (Registry->TryRequest_TravelToGymByName(World, InRegisteredName, this, InRequestId))
    { Client_ReportTravelResult(InRequestId, ECkGym_TravelResult::Accepted, InRegisteredName); }
    else
    { Client_ReportTravelResult(InRequestId, ECkGym_TravelResult::Rejected, TEXT("travel admission failed")); }
}
