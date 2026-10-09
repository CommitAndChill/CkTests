#include "CkGym_Registry.h"

#include "CkGymStartup_Utils.h"
#include "CkGym_NetworkRuntime.h"
#include "CkTests/CkTests_Log.h"

#include "CkCore/Ensure/CkEnsure.h"

#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>
#include <Engine/Engine.h>
#include <Engine/NetDriver.h>
#include <Engine/World.h>
#include <Containers/Ticker.h>
#include <HAL/PlatformTime.h>
#include <IPAddress.h>
#include <Kismet/GameplayStatics.h>
#include <Misc/PackageName.h>

// --------------------------------------------------------------------------------------------------------------------

auto
    UCkGym_Registry_Subsystem::
    Deinitialize()
    -> void
{
    ClearPendingTravel();
    if (_LocalTicketTickerHandle.IsValid())
    { FTSTicker::GetCoreTicker().RemoveTicker(_LocalTicketTickerHandle); }
    _LocalTicketTickerHandle.Reset();
    _LocalTickets.Empty();
    Super::Deinitialize();
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCkGym_Registry_Subsystem::
    Register(
        const FCkGym_Entry& InEntry)
    -> void
{
    for (const auto& Existing : _Entries)
    {
        if (Existing.DisplayName == InEntry.DisplayName)
        { return; }
    }

    _Entries.Add(InEntry);
}

auto
    UCkGym_Registry_Subsystem::
    Get_Entries() const
    -> const TArray<FCkGym_Entry>&
{
    if (NOT HasReadableReplica())
    {
        static const TArray<FCkGym_Entry> Empty;
        return Empty;
    }
    return _Entries;
}

auto
    UCkGym_Registry_Subsystem::
    Get_CurrentGymIndex() const
    -> int32
{
    return HasReadableReplica() ? _CurrentGymIndex : INDEX_NONE;
}

auto
    UCkGym_Registry_Subsystem::
    Get_GymLevelName() const
    -> FString
{
    return HasReadableReplica() ? _GymLevelName : FString{};
}

auto
    UCkGym_Registry_Subsystem::
    Get_SuppressHUDDuringStartup() const
    -> bool
{
    return HasReadableReplica() ? _SuppressHUDDuringStartup : true;
}

auto
    UCkGym_Registry_Subsystem::
    Set_SuppressHUDDuringStartup(
        bool InSuppress)
    -> void
{
    _SuppressHUDDuringStartup = InSuppress;
}

auto
    UCkGym_Registry_Subsystem::
    Get_HasConsumedStartupResolve() const
    -> bool
{
    return HasReadableReplica() && _StartupResolveConsumed;
}

auto
    UCkGym_Registry_Subsystem::
    MarkStartupResolveConsumed()
    -> void
{
    _StartupResolveConsumed = true;
}

auto
    UCkGym_Registry_Subsystem::
    Find_GymIndexByName(
        const FString& InName) const
    -> int32
{
    if (InName.IsEmpty())
    { return INDEX_NONE; }
    if (NOT HasReadableReplica())
    { return INDEX_NONE; }

    for (auto Index = 0; Index < _Entries.Num(); ++Index)
    {
        if (_Entries[Index].DisplayName == InName)
        { return Index; }
    }

    return INDEX_NONE;
}

auto
    UCkGym_Registry_Subsystem::
    HasReadableReplica() const
    -> bool
{
    auto* GameInstance = GetGameInstance();
    auto* World = IsValid(GameInstance) ? GameInstance->GetWorld() : nullptr;
    if (NOT IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    if (World->GetNetMode() != NM_Client)
    { return true; }
    const auto* State = World->GetGameState<ACkGym_GameState_UE>();
    return IsValid(State) && State->Get_HasPublishedCatalog();
}

auto
    UCkGym_Registry_Subsystem::
    Request_TravelToGym(
        int32 InIndex)
    -> void
{
    auto* GameInstance = GetGameInstance();
    auto* World = IsValid(GameInstance) ? GameInstance->GetWorld() : nullptr;
    TryRequest_TravelToGym(World, InIndex);
}

auto
    UCkGym_Registry_Subsystem::
    IsValidCatalog(
        const TArray<FCkGym_Entry>& InEntries,
        int32 InCurrentIndex)
    -> bool
{
    if (InEntries.IsEmpty() || InCurrentIndex < INDEX_NONE || InCurrentIndex >= InEntries.Num())
    { return false; }

    TSet<FString> Names;
    for (const auto& Entry : InEntries)
    {
        const auto* GameModeClass = Entry.GameModeClass.Get();
        const auto NameIsValid = NOT Entry.DisplayName.TrimStartAndEnd().IsEmpty() &&
            Entry.DisplayName == Entry.DisplayName.TrimStartAndEnd() &&
            NOT Names.Contains(Entry.DisplayName);
        const auto ClassIsValid = IsValid(GameModeClass) &&
            GameModeClass->IsChildOf(AGameModeBase::StaticClass()) &&
            NOT GameModeClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) &&
            IsValid(GameModeClass->GetDefaultObject(false));
        auto LevelIsValid = true;
        for (const auto Character : Entry.LevelName)
        {
            if (NOT FChar::IsAlnum(Character) && Character != TEXT('_') &&
                Character != TEXT('/') && Character != TEXT('.') && Character != TEXT('-'))
            { LevelIsValid = false; break; }
        }
        if (NOT NameIsValid || NOT ClassIsValid || NOT LevelIsValid)
        { return false; }

        Names.Add(Entry.DisplayName);
    }

    return true;
}

auto
    UCkGym_Registry_Subsystem::
    TryReplaceReplica(
        const UWorld* InWorld,
        const TArray<FCkGym_Entry>& InEntries,
        int32 InCurrentIndex,
        bool InSuppressHUD)
    -> bool
{
    const auto* GameInstance = GetGameInstance();
    const auto WorldIsValid = IsValid(InWorld) && NOT InWorld->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        InWorld->IsGameWorld() && IsValid(GameInstance) &&
        InWorld->GetGameInstance() == GameInstance && GameInstance->GetWorld() == InWorld &&
        InWorld->GetNetMode() == NM_Client;
    if (NOT WorldIsValid || NOT IsValidCatalog(InEntries, InCurrentIndex))
    { return false; }

    _Entries = InEntries;
    _CurrentGymIndex = InCurrentIndex;
    _SuppressHUDDuringStartup = InSuppressHUD;
    return true;
}

auto
    UCkGym_Registry_Subsystem::
    TryRecordLandedGym(
        const UWorld* InWorld)
    -> bool
{
    const auto* GameInstance = GetGameInstance();
    const auto WorldIsValid = IsValid(InWorld) && NOT InWorld->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        InWorld->IsGameWorld() && IsValid(GameInstance) && InWorld->GetGameInstance() == GameInstance &&
        GameInstance->GetWorld() == InWorld &&
        InWorld->GetNetMode() != NM_Client;
    if (NOT WorldIsValid || NOT IsValidCatalog(_Entries, _CurrentGymIndex))
    { return false; }

    const auto* LandedGameMode = InWorld->GetAuthGameMode();
    if (NOT IsValid(LandedGameMode))
    { return false; }

    const auto LandedLevel = UGameplayStatics::GetCurrentLevelName(InWorld, true);
    const auto MatchedIndex = ChooseLandedIndex(_Entries, LandedGameMode->GetClass(),
        LandedLevel, _PendingGymName, _GymLevelName);

    if (MatchedIndex != INDEX_NONE)
    {
        const auto& Entry = _Entries[MatchedIndex];
        _CurrentGymIndex = MatchedIndex;
        if (_PendingGymName == Entry.DisplayName)
        {
            UCk_Utils_GymStartup_UE::Request_Set_LastGymName(Entry.DisplayName);
            UCk_Utils_GymStartup_UE::Request_PushRecentGym(Entry.DisplayName);
            if (auto* Requester = _PendingRequester.Get())
            { Requester->Client_ReportTravelResult(_PendingRequestId, ECkGym_TravelResult::Landed,
                Entry.DisplayName); }
        }
        else if (auto* Requester = _PendingRequester.Get())
        { Requester->Client_ReportTravelResult(_PendingRequestId, ECkGym_TravelResult::TravelFailed,
            TEXT("landed in a different gym")); }
        ClearPendingTravel();
        return true;
    }

    if (auto* Requester = _PendingRequester.Get())
    { Requester->Client_ReportTravelResult(_PendingRequestId, ECkGym_TravelResult::TravelFailed,
        TEXT("destination did not match a registered gym")); }
    ClearPendingTravel();
    _CurrentGymIndex = INDEX_NONE;
    return false;
}

auto
    UCkGym_Registry_Subsystem::
    TryBeginLocalTravel(ULocalPlayer* InPlayer, const FString& InName, int32& OutRequestId)
    -> bool
{
    OutRequestId = 0;
    auto* World = IsValid(InPlayer) ? InPlayer->GetWorld() : nullptr;
    const auto* GameInstance = GetGameInstance();
    const auto PlayerIsValid = IsValid(InPlayer) &&
        NOT InPlayer->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        IsValid(World) && NOT World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        World->IsGameWorld() && IsValid(GameInstance) && World->GetGameInstance() == GameInstance &&
        GameInstance->GetWorld() == World && GameInstance->GetLocalPlayers().Contains(InPlayer) &&
        NOT InName.IsEmpty();
    if (NOT PlayerIsValid)
    { return false; }

    if (_NextLocalRequestId >= MAX_int32)
    { return false; }
    const auto Key = TWeakObjectPtr<ULocalPlayer>{InPlayer};
    if (const auto* Existing = _LocalTickets.Find(Key))
    {
        if (Existing->Result == ECkGym_TravelResult::Queued ||
            Existing->Result == ECkGym_TravelResult::Accepted)
        { return false; }
    }
    auto& Ticket = _LocalTickets.FindOrAdd(Key);
    Ticket.RequestId = ++_NextLocalRequestId;
    Ticket.Name = InName;
    Ticket.SourceWorld = World;
    Ticket.SourceState = World->GetGameState();
    Ticket.Result = ECkGym_TravelResult::Queued;
    Ticket.Detail = InName;
    Ticket.DeadlineSeconds = FPlatformTime::Seconds() + 50.0;
    OutRequestId = Ticket.RequestId;
    if (NOT _LocalTicketTickerHandle.IsValid())
    {
        _LocalTicketTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &UCkGym_Registry_Subsystem::PollLocalTravelTickets), 0.5f);
    }
    return true;
}

auto
    UCkGym_Registry_Subsystem::
    TryApplyLocalTravelResult(ULocalPlayer* InPlayer, int32 InRequestId,
        ECkGym_TravelResult InResult, const FString& InDetail)
    -> bool
{
    switch (InResult)
    {
        case ECkGym_TravelResult::Accepted:
        case ECkGym_TravelResult::Rejected:
        case ECkGym_TravelResult::Landed:
        case ECkGym_TravelResult::TravelFailed:
        case ECkGym_TravelResult::TimedOut:
        { break; }
        default:
        { return false; }
    }
    const auto Key = TWeakObjectPtr<ULocalPlayer>{InPlayer};
    auto* Ticket = IsValid(InPlayer) ? _LocalTickets.Find(Key) : nullptr;
    if (Ticket == nullptr || Ticket->RequestId != InRequestId ||
        (Ticket->Result != ECkGym_TravelResult::Queued && Ticket->Result != ECkGym_TravelResult::Accepted))
    { return false; }
    Ticket->Result = InResult;
    Ticket->Detail = InDetail;
    return true;
}

auto
    UCkGym_Registry_Subsystem::
    GetLocalTravelResult(const ULocalPlayer* InPlayer) const
    -> ECkGym_TravelResult
{
    const auto* Ticket = IsValid(InPlayer) ? _LocalTickets.Find(TWeakObjectPtr<ULocalPlayer>{const_cast<ULocalPlayer*>(InPlayer)}) : nullptr;
    return Ticket != nullptr ? Ticket->Result : ECkGym_TravelResult::None;
}

auto
    UCkGym_Registry_Subsystem::
    GetLocalTravelDetail(const ULocalPlayer* InPlayer) const
    -> FString
{
    const auto* Ticket = IsValid(InPlayer) ? _LocalTickets.Find(TWeakObjectPtr<ULocalPlayer>{const_cast<ULocalPlayer*>(InPlayer)}) : nullptr;
    return Ticket != nullptr ? Ticket->Detail : FString{};
}

auto
    UCkGym_Registry_Subsystem::
    ResolveLocalTravelOnCatalog(const UWorld* InWorld, const AGameStateBase* InState,
        const TArray<FCkGym_Entry>& InEntries, int32 InCurrentIndex)
    -> void
{
    const auto* GameInstance = GetGameInstance();
    if (NOT IsValid(InWorld) || NOT IsValid(InState) || NOT IsValid(GameInstance) ||
        InWorld->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || NOT InWorld->IsGameWorld() ||
        InWorld->GetGameInstance() != GameInstance || GameInstance->GetWorld() != InWorld ||
        InWorld->GetGameState() != InState || NOT IsValidCatalog(InEntries, InCurrentIndex))
    { return; }
    const auto LandedName = InEntries.IsValidIndex(InCurrentIndex) ?
        InEntries[InCurrentIndex].DisplayName : FString{};
    for (auto& Pair : _LocalTickets)
    {
        auto& Ticket = Pair.Value;
        const auto* Player = Pair.Key.Get();
        if (NOT IsValid(Player) || Player->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Player->GetWorld() != InWorld || NOT GameInstance->GetLocalPlayers().Contains(Player) ||
            (Ticket.Result != ECkGym_TravelResult::Queued && Ticket.Result != ECkGym_TravelResult::Accepted) ||
            (Ticket.SourceWorld.Get() == InWorld && Ticket.SourceState.Get() == InState))
        { continue; }
        Ticket.Result = LandedName == Ticket.Name ? ECkGym_TravelResult::Landed :
            ECkGym_TravelResult::TravelFailed;
        Ticket.Detail = LandedName == Ticket.Name ? LandedName :
            TEXT("new destination catalog did not select the requested gym");
    }
}

auto
    UCkGym_Registry_Subsystem::
    PollLocalTravelTickets(float InDeltaSeconds)
    -> bool
{
    auto* GameInstance = GetGameInstance();
    auto* World = IsValid(GameInstance) ? GameInstance->GetWorld() : nullptr;
    auto* State = IsValid(World) ? World->GetGameState<ACkGym_GameState_UE>() : nullptr;
    if (IsValid(State) && State->Get_HasPublishedCatalog())
    {
        // The destination catalog can arrive before a local player switches to its new world.
        ResolveLocalTravelOnCatalog(World, State, _Entries, _CurrentGymIndex);
    }
    const auto Now = FPlatformTime::Seconds();
    auto HasActiveTicket = false;
    for (auto It = _LocalTickets.CreateIterator(); It; ++It)
    {
        if (NOT It.Key().IsValid())
        { It.RemoveCurrent(); continue; }
        auto& Ticket = It.Value();
        if (Ticket.Result != ECkGym_TravelResult::Queued && Ticket.Result != ECkGym_TravelResult::Accepted)
        { continue; }
        if (Now >= Ticket.DeadlineSeconds)
        {
            Ticket.Result = ECkGym_TravelResult::TimedOut;
            Ticket.Detail = TEXT("gym request did not complete within 50 seconds");
        }
        else
        { HasActiveTicket = true; }
    }
    if (NOT HasActiveTicket)
    { _LocalTicketTickerHandle.Reset(); }
    return HasActiveTicket;
}

auto
    UCkGym_Registry_Subsystem::
    TryRequest_TravelToGym(
        UWorld* InWorld,
        int32 InIndex,
        ACkGym_PlayerController_UE* InRequester,
        int32 InRequestId)
    -> bool
{
    const auto WorldIsValid = IsValid(InWorld) && NOT InWorld->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        InWorld->IsGameWorld() && InWorld->GetGameInstance() == GetGameInstance() &&
        InWorld->GetNetMode() != NM_Client && IsValid(InWorld->GetAuthGameMode());
    if (NOT WorldIsValid || NOT IsValid(GEngine) || NOT _PendingGymName.IsEmpty() ||
        NOT IsValidCatalog(_Entries, _CurrentGymIndex))
    { return false; }

    const auto WrappedIndex = Get_WrappedIndex(InIndex, _Entries.Num());
    const auto& Entry = _Entries[WrappedIndex];
    const auto LevelName = Entry.LevelName.IsEmpty() ? _GymLevelName : Entry.LevelName;
    if (LevelName.IsEmpty())
    { return false; }

    auto LongPackageName = FString{};
    auto Filename = FString{};
    const auto MapExists = FPackageName::SearchForPackageOnDisk(
        LevelName, &LongPackageName, &Filename) &&
        Filename.EndsWith(FPackageName::GetMapPackageExtension(), ESearchCase::IgnoreCase);
    if (NOT MapExists)
    {
        ck::tests::Warning(TEXT("[GymRegistry] registered gym [{}] has no resolvable map [{}]"),
            Entry.DisplayName, LevelName);
        return false;
    }

    const auto TravelURL = FString::Printf(TEXT("%s?game=%s"), *LongPackageName,
        *Entry.GameModeClass.Get()->GetPathName());

    // ProcessServerTravel builds its destination URL from LastURL. A listen driver may
    // have bound the next available port while LastURL still names the requested port.
    if (!InWorld->NextURL.IsEmpty() || InWorld->IsInSeamlessTravel())
    { return false; }
    auto* Context = GEngine->GetWorldContextFromWorld(InWorld);
    if (Context == nullptr || Context->World() != InWorld)
    { return false; }
    const auto NetMode = InWorld->GetNetMode();
    if (NetMode != NM_Standalone && NetMode != NM_ListenServer && NetMode != NM_DedicatedServer)
    { return false; }
    const auto PreviousPort = Context->LastURL.Port;
    auto BoundPort = PreviousPort;
    if (NetMode != NM_Standalone)
    {
        auto* Driver = InWorld->GetNetDriver();
        if (NOT IsValid(Driver) || Driver->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            Driver->GetWorld() != InWorld || Driver->ServerConnection != nullptr)
        { return false; }
        const auto LocalAddr = Driver->GetLocalAddr();
        if (NOT LocalAddr.IsValid())
        { return false; }
        BoundPort = LocalAddr->GetPort();
        if (BoundPort < 1 || BoundPort > 65535)
        { return false; }
        Context->LastURL.Port = BoundPort;
    }
    const auto RestorePort = [&]()
    {
        if (BoundPort == PreviousPort || !IsValid(GEngine) || !IsValid(InWorld)) return;
        auto* CurrentContext = GEngine->GetWorldContextFromWorld(InWorld);
        if (CurrentContext == Context && CurrentContext->World() == InWorld &&
            CurrentContext->LastURL.Port == BoundPort)
        { CurrentContext->LastURL.Port = PreviousPort; }
    };

    _PendingGymName = Entry.DisplayName;
    _PendingSourceWorld = InWorld;
    _PendingRequester = InRequester;
    _PendingRequestId = InRequestId;
    _PendingDeadlineSeconds = FPlatformTime::Seconds() + 45.0;
    _TravelFailureHandle = GEngine->OnTravelFailure().AddWeakLambda(this,
        [this](UWorld* FailedWorld, ETravelFailure::Type, const FString& Error)
        { HandlePendingTravelFailure(FailedWorld, Error); });
    if (NOT InWorld->ServerTravel(TravelURL))
    {
        RestorePort();
        ClearPendingTravel();
        return false;
    }
    if (_PendingGymName.IsEmpty())
    { RestorePort(); return false; }
    _PendingTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UCkGym_Registry_Subsystem::PollPendingTravel), 1.0f);
    ck::tests::Log(TEXT("[GymRegistry] accepted ServerTravel {}"), TravelURL);
    return true;
}

auto
    UCkGym_Registry_Subsystem::
    TryRequest_TravelToGymByName(
        UWorld* InWorld,
        const FString& InName,
        ACkGym_PlayerController_UE* InRequester,
        int32 InRequestId)
    -> bool
{
    const auto Index = Find_GymIndexByName(InName);
    return Index != INDEX_NONE && TryRequest_TravelToGym(InWorld, Index, InRequester, InRequestId);
}

auto
    UCkGym_Registry_Subsystem::
    ClearPendingTravel()
    -> void
{
    if (_TravelFailureHandle.IsValid() && IsValid(GEngine))
    { GEngine->OnTravelFailure().Remove(_TravelFailureHandle); }
    if (_PendingTickerHandle.IsValid())
    { FTSTicker::GetCoreTicker().RemoveTicker(_PendingTickerHandle); }
    _TravelFailureHandle.Reset();
    _PendingTickerHandle.Reset();
    _PendingGymName.Reset();
    _PendingSourceWorld.Reset();
    _PendingRequester.Reset();
    _PendingRequestId = 0;
    _PendingDeadlineSeconds = 0.0;
}

auto
    UCkGym_Registry_Subsystem::
    HandlePendingTravelFailure(
        UWorld* InWorld,
        const FString& InError)
    -> void
{
    if (_PendingGymName.IsEmpty() || InWorld != _PendingSourceWorld.Get())
    { return; }

    ck::tests::Warning(TEXT("[GymRegistry] gym travel failed: {}"), InError);
    if (auto* Requester = _PendingRequester.Get())
    { Requester->Client_ReportTravelResult(_PendingRequestId, ECkGym_TravelResult::TravelFailed, InError); }
    ClearPendingTravel();
}

auto
    UCkGym_Registry_Subsystem::
    PollPendingTravel(
        float InDeltaSeconds)
    -> bool
{
    if (_PendingGymName.IsEmpty())
    { return false; }
    if (FPlatformTime::Seconds() < _PendingDeadlineSeconds)
    { return true; }

    ck::tests::Warning(TEXT("[GymRegistry] accepted travel to [{}] did not land within 45 seconds"),
        _PendingGymName);
    if (auto* Requester = _PendingRequester.Get())
    { Requester->Client_ReportTravelResult(_PendingRequestId, ECkGym_TravelResult::TimedOut,
        TEXT("registered gym destination did not land within 45 seconds")); }
    ClearPendingTravel();
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCkGym_Registry_Subsystem::
    ChooseLandedIndex(const TArray<FCkGym_Entry>& InEntries,
        const UClass* InLandedGameModeClass, const FString& InLandedLevel,
        const FString& InPendingName, const FString& InDefaultLevel)
    -> int32
{
    if (NOT IsValid(InLandedGameModeClass) || InLandedLevel.IsEmpty())
    { return INDEX_NONE; }
    int32 FirstMatchingIndex = INDEX_NONE;
    for (auto Index = 0; Index < InEntries.Num(); ++Index)
    {
        const auto& Entry = InEntries[Index];
        const auto ExpectedLevel = FPackageName::GetShortName(
            Entry.LevelName.IsEmpty() ? InDefaultLevel : Entry.LevelName);
        if (Entry.GameModeClass.Get() != InLandedGameModeClass || ExpectedLevel != InLandedLevel)
        { continue; }
        if (FirstMatchingIndex == INDEX_NONE)
        { FirstMatchingIndex = Index; }
        if (Entry.DisplayName == InPendingName)
        { return Index; }
    }
    return FirstMatchingIndex;
}

auto
    UCkGym_Registry_Subsystem::
    Get_WrappedIndex(
        int32 InIndex,
        int32 InNum)
    -> int32
{
    auto Wrapped = InIndex % InNum;
    if (Wrapped < 0)
    { Wrapped += InNum; }

    return Wrapped;
}

auto
    UCkGym_Registry_Subsystem::
    Get_RecentsAfterVisit(
        const TArray<FString>& InRecents,
        const FString& InVisited,
        int32 InCap)
    -> TArray<FString>
{
    auto Result = TArray<FString>{};
    Result.Add(InVisited);

    for (const auto& Existing : InRecents)
    {
        if (Existing == InVisited)
        { continue; }

        if (Result.Num() >= InCap)
        { break; }

        Result.Add(Existing);
    }

    return Result;
}
