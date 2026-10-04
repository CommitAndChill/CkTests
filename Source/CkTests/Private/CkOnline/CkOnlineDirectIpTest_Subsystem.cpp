#include "CkOnline/CkOnlineDirectIpTest_Subsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "IPAddress.h"
#include "IpNetDriver.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogCkOnlineDirectIpTest, Log, All);

namespace
{
    constexpr auto GymGameOption = TEXT("game=/Script/Angelscript.Ck_OnlineGym_DirectIp_GameMode");
    constexpr double JoinTimeoutSeconds = 15.0;

    auto HasIpDriverDefinition() -> bool
    {
        if (GEngine == nullptr) { return false; }
        const auto* Definition = GEngine->NetDriverDefinitions.FindByPredicate(
            [](const FNetDriverDefinition& InDefinition) { return InDefinition.DefName == NAME_GameNetDriver; });
        if (Definition == nullptr) { return false; }
        const auto* DriverClass = LoadClass<UNetDriver>(nullptr, *Definition->DriverClassName.ToString());
        return DriverClass != nullptr && DriverClass->IsChildOf(UIpNetDriver::StaticClass());
    }

    auto HasOpenClientConnection(UWorld* InWorld) -> bool
    {
        if (!IsValid(InWorld)) { return false; }
        const auto* Driver = Cast<UIpNetDriver>(InWorld->GetNetDriver());
        return Driver != nullptr && IsValid(Driver->ServerConnection)
            && Driver->ServerConnection->GetConnectionState() == USOCK_Open;
    }
}

auto ACkOnlineDirectIpTest_GameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const -> void
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, SessionMarker);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, ProofSequence);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, LastProofPlayerId);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, JoinCount);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, LeaveCount);
}

auto ACkOnlineDirectIpTest_GameState::Request_BeginHosting() -> bool
{
    auto* World = GetWorld();
    if (!HasAuthority() || SessionMarker.IsValid() || !IsValid(World) || World->GetNetMode() != NM_ListenServer
        || !IsValid(Cast<UIpNetDriver>(World->GetNetDriver()))) { return false; }
    SessionMarker = FGuid::NewGuid();
    JoinCount = PlayerArray.Num();
    ForceNetUpdate();
    return true;
}

auto ACkOnlineDirectIpTest_GameState::AddPlayerState(APlayerState* InPlayerState) -> void
{
    if (!IsValid(InPlayerState)) { return; }
    const auto WasPresent = PlayerArray.Contains(InPlayerState);
    Super::AddPlayerState(InPlayerState);
    if (HasAuthority() && SessionMarker.IsValid() && !WasPresent && PlayerArray.Contains(InPlayerState))
    { ++JoinCount; ForceNetUpdate(); }
}

auto ACkOnlineDirectIpTest_GameState::RemovePlayerState(APlayerState* InPlayerState) -> void
{
    // The engine calls this during PlayerState destruction. Removal compares addresses only.
    if (InPlayerState == nullptr) { return; }
    const auto WasPresent = PlayerArray.Contains(InPlayerState);
    Super::RemovePlayerState(InPlayerState);
    if (HasAuthority() && SessionMarker.IsValid() && WasPresent && !PlayerArray.Contains(InPlayerState))
    { ++LeaveCount; ForceNetUpdate(); }
}

auto ACkOnlineDirectIpTest_GameState::Request_RecordProof(APlayerState* InPlayerState) -> bool
{
    if (!HasAuthority() || !SessionMarker.IsValid() || !IsValid(InPlayerState)
        || !PlayerArray.Contains(InPlayerState) || ProofSequence == MAX_int32) { return false; }
    ++ProofSequence;
    LastProofPlayerId = InPlayerState->GetPlayerId();
    ForceNetUpdate();
    UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Proof %d from peer %d; session %s"),
        ProofSequence, LastProofPlayerId, *SessionMarker.ToString());
    return true;
}

UCkOnlineDirectIpTest_ProofComponent::UCkOnlineDirectIpTest_ProofComponent()
{
    SetIsReplicatedByDefault(true);
}

void UCkOnlineDirectIpTest_ProofComponent::Server_RecordProof_Implementation()
{
    auto* PlayerController = Cast<APlayerController>(GetOwner());
    if (!IsValid(PlayerController) || !PlayerController->HasAuthority()
        || !IsValid(PlayerController->GetNetConnection())) { return; }
    auto* World = PlayerController->GetWorld();
    if (!IsValid(World)) { return; }
    auto* State = World->GetGameState<ACkOnlineDirectIpTest_GameState>();
    if (!IsValid(State)) { return; }
    State->Request_RecordProof(PlayerController->PlayerState);
}

auto UCkOnlineDirectIpTest_Subsystem::Initialize(FSubsystemCollectionBase& InCollection) -> void
{
    Super::Initialize(InCollection);
    if (GEngine == nullptr) { return; }
    _NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnNetworkFailure);
    _TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnTravelFailure);
}

auto UCkOnlineDirectIpTest_Subsystem::Deinitialize() -> void
{
    _Deinitialized = true;
    if (GEngine != nullptr)
    {
        GEngine->OnNetworkFailure().Remove(_NetworkFailureHandle);
        GEngine->OnTravelFailure().Remove(_TravelFailureHandle);
    }
    // GameInstance/world teardown owns the driver; never retain or manually destroy it here.
    _DepartingWorld.Reset();
    _HostingDepartureWorld.Reset();
    Super::Deinitialize();
}

auto UCkOnlineDirectIpTest_Subsystem::Get_ReadyWorld() const -> UWorld*
{
    auto* Instance = GetGameInstance();
    if (_Deinitialized || !IsValid(Instance)) { return nullptr; }
    auto* World = Instance->GetWorld();
    return IsValid(World) && World->IsGameWorld() && World->HasBegunPlay() ? World : nullptr;
}

auto UCkOnlineDirectIpTest_Subsystem::Get_ProofState() const -> ACkOnlineDirectIpTest_GameState*
{
    auto* World = Get_ReadyWorld();
    return World != nullptr ? World->GetGameState<ACkOnlineDirectIpTest_GameState>() : nullptr;
}

auto UCkOnlineDirectIpTest_Subsystem::DoReject(const FString& InReason) -> bool
{
    _LastError = InReason;
    return false;
}

auto UCkOnlineDirectIpTest_Subsystem::TryParseEndpoint(const FString& InEndpoint, FString& OutCanonical) -> bool
{
    // Literal IPv4 and a port only: no options, map paths, DNS, whitespace or arbitrary travel URLs.
    FString Address;
    FString PortText;
    if (InEndpoint.Len() > 21 || !InEndpoint.Split(TEXT(":"), &Address, &PortText)) { return false; }
    auto IsDecimal = [](const FString& InText)
    {
        if (InText.IsEmpty()) { return false; }
        for (const auto Character : InText)
        { if (Character < TEXT('0') || Character > TEXT('9')) { return false; } }
        return true;
    };
    TArray<FString> Parts;
    Address.ParseIntoArray(Parts, TEXT("."), false);
    if (Parts.Num() != 4 || PortText.Len() > 5 || !IsDecimal(PortText)) { return false; }
    const auto Port = FCString::Atoi(*PortText);
    if (Port < 1 || Port > 65535) { return false; }
    int32 Octets[4];
    for (auto Index = 0; Index < 4; ++Index)
    {
        if (Parts[Index].Len() > 3 || !IsDecimal(Parts[Index])) { return false; }
        Octets[Index] = FCString::Atoi(*Parts[Index]);
        if (Octets[Index] > 255) { return false; }
    }
    if (Octets[0] == 0 || Octets[0] >= 224) { return false; }
    OutCanonical = FString::Printf(TEXT("%d.%d.%d.%d:%d"), Octets[0], Octets[1], Octets[2], Octets[3], Port);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Host(int32 InPort) -> bool
{
    auto* World = Get_ReadyWorld();
    if (World == nullptr) { return DoReject(TEXT("No ready game world.")); }
    if (_State != EState::Offline || World->GetNetDriver() != nullptr || World->GetNetMode() != NM_Standalone)
    { return DoReject(TEXT("Host needs an Offline Standalone instance. Automatic PIE networking is not this gate.")); }
    auto* State = Get_ProofState();
    if (!IsValid(State) || State->SessionMarker.IsValid()) { return DoReject(TEXT("Select Online Direct IP first.")); }
    if (InPort < 1 || InPort > 65535) { return DoReject(TEXT("Port must be 1..65535.")); }
    if (!HasIpDriverDefinition()) { return DoReject(TEXT("GameNetDriver must use IpNetDriver for this transport gate.")); }

    if (GEngine == nullptr || GEngine->GetWorldContextFromWorld(World) == nullptr)
    { return DoReject(TEXT("The host world has no travel context.")); }
    const auto ReturnMap = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
    if (ReturnMap.IsEmpty()) { return DoReject(TEXT("The host world has no return map.")); }
    FURL ListenUrl(nullptr, *ReturnMap, TRAVEL_Absolute);
    ListenUrl.Port = InPort;
    ListenUrl.AddOption(GymGameOption);
    ListenUrl.AddOption(TEXT("listen"));
    const auto TravelUrl = ListenUrl.ToString();
    const FURL RoundTrip(nullptr, *TravelUrl, TRAVEL_Absolute);
    if (!ListenUrl.Valid || !RoundTrip.Valid || !RoundTrip.IsLocalInternal()
        || RoundTrip.Map != ReturnMap || RoundTrip.Port != InPort
        || !RoundTrip.HasOption(TEXT("listen")) || !RoundTrip.HasOption(TEXT("game=")))
    { return DoReject(TEXT("Could not form a local listen travel URL for the requested port.")); }
    _ReturnMap = ReturnMap;
    _RequestedHostPort = InPort;
    _HostingDepartureWorld = World;
    _Endpoint = FString::Printf(TEXT("127.0.0.1:%d"), InPort);
    _LastError.Empty();
    _State = EState::StartingHost;
    _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    GEngine->SetClientTravel(World, *TravelUrl, TRAVEL_Absolute);
    UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Hosting requested for %s (local listen travel issued; bound driver not yet proven)"), *_Endpoint);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Join(const FString& InEndpoint) -> bool
{
    FString Canonical;
    if (!TryParseEndpoint(InEndpoint, Canonical)) { return DoReject(TEXT("Use a literal IPv4:port, e.g. 127.0.0.1:7787.")); }
    auto* World = Get_ReadyWorld();
    if (World == nullptr) { return DoReject(TEXT("No ready game world.")); }
    if (_State != EState::Offline || World->GetNetDriver() != nullptr || World->GetNetMode() != NM_Standalone)
    { return DoReject(TEXT("Join needs an Offline Standalone instance. Disconnect with X first.")); }
    if (!IsValid(Get_ProofState())) { return DoReject(TEXT("Select Online Direct IP first.")); }
    auto* PlayerController = GetGameInstance()->GetFirstLocalPlayerController();
    if (!IsValid(PlayerController)) { return DoReject(TEXT("No local player controller.")); }
    if (!HasIpDriverDefinition()) { return DoReject(TEXT("GameNetDriver must use IpNetDriver for this transport gate.")); }

    _ReturnMap = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
    _Endpoint = MoveTemp(Canonical);
    _HasJoinEndpoint = true;
    _LastError.Empty();
    _State = EState::Joining;
    _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    PlayerController->ClientTravel(_Endpoint, TRAVEL_Absolute);
    UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Joining %s (request issued; admission not yet proven)"), *_Endpoint);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::DoReturnToGym(bool InKeepFailure) -> void
{
    auto* World = Get_ReadyWorld();
    if (World == nullptr || _ReturnMap.IsEmpty()) { return; }
    _DepartingWorld = World;
    _HostingDepartureWorld.Reset();
    _ReturnPending = true;
    _FailureReturnPending = false;
    if (!InKeepFailure) { _State = EState::Leaving; }
    _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    UGameplayStatics::OpenLevel(World, FName(*_ReturnMap), true, GymGameOption);
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Disconnect() -> bool
{
    if (_State == EState::Offline || _State == EState::Leaving) { return DoReject(TEXT("No active connection to leave.")); }
    if (Get_ReadyWorld() == nullptr || _ReturnMap.IsEmpty()) { return DoReject(TEXT("No return world available.")); }
    _LastError.Empty();
    DoReturnToGym(false);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Rejoin() -> bool
{
    if (!_HasJoinEndpoint) { return DoReject(TEXT("Join once with K before using Rejoin.")); }
    return Request_Join(_Endpoint);
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Proof() -> bool
{
    if (!Get_IsConnected()) { return DoReject(TEXT("P sends a proof from the connected client only.")); }
    auto* PlayerController = GetGameInstance()->GetFirstLocalPlayerController();
    if (!IsValid(PlayerController)) { return DoReject(TEXT("No local player controller.")); }
    auto* Component = PlayerController->FindComponentByClass<UCkOnlineDirectIpTest_ProofComponent>();
    if (!IsValid(Component) || !Component->GetIsReplicated()) { return DoReject(TEXT("The owned proof component is unavailable.")); }
    Component->Server_RecordProof();
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Tick(float InDeltaTime) -> void
{
    auto* World = Get_ReadyWorld();
    if (World == nullptr)
    {
        if ((_State == EState::StartingHost || _State == EState::Joining) && FPlatformTime::Seconds() > _Deadline)
        {
            _LastError = _State == EState::StartingHost
                ? TEXT("Host travel timed out before the IP listen was established.")
                : TEXT("Join timed out before IP admission and replicated session evidence.");
            _State = EState::Failed;
            _FailureReturnPending = true;
        }
        else if (_ReturnPending && FPlatformTime::Seconds() > _Deadline)
        {
            _State = EState::Failed;
            _LastError = TEXT("Return to the gym timed out.");
            _DepartingWorld.Reset();
            _ReturnPending = false;
        }
        return;
    }
    if (_FailureReturnPending)
    {
        DoReturnToGym(true);
        return;
    }
    if (_ReturnPending)
    {
        if (World != _DepartingWorld.Get() && World->GetNetDriver() == nullptr && IsValid(Get_ProofState()))
        {
            _DepartingWorld.Reset();
            _State = EState::Offline;
            _ReturnPending = false;
        }
        else if (FPlatformTime::Seconds() > _Deadline)
        {
            _State = EState::Failed;
            _LastError = TEXT("Return to the gym timed out.");
            _DepartingWorld.Reset();
            _ReturnPending = false;
        }
        return;
    }
    if (_State == EState::StartingHost)
    {
        if (World != _HostingDepartureWorld.Get())
        {
            auto* Driver = Cast<UIpNetDriver>(World->GetNetDriver());
            const auto Address = Driver != nullptr ? Driver->GetLocalAddr() : TSharedPtr<const FInternetAddr>{};
            auto* State = Get_ProofState();
            if (World->GetNetMode() != NM_ListenServer || Driver == nullptr || !Address.IsValid()
                || Address->GetPort() != _RequestedHostPort || !IsValid(State))
            {
                _State = EState::Failed;
                _LastError = TEXT("Host travel did not establish the requested IP port and fixture; returning to the gym.");
                _FailureReturnPending = true;
            }
            else if (State->PlayerArray.Num() == 1 && State->Request_BeginHosting())
            {
                _State = EState::Hosting;
                _HostingDepartureWorld.Reset();
                UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Hosting %s; session %s"),
                    *_Endpoint, *State->SessionMarker.ToString());
            }
        }
        if (_State == EState::StartingHost && FPlatformTime::Seconds() > _Deadline)
        {
            _State = EState::Failed;
            _LastError = TEXT("Host travel timed out before the IP listen and local peer were ready.");
            _FailureReturnPending = true;
        }
    }
    else if (_State == EState::Joining)
    {
        auto* State = Get_ProofState();
        auto* PlayerController = GetGameInstance()->GetFirstLocalPlayerController();
        if (World->GetNetMode() == NM_Client && HasOpenClientConnection(World) && IsValid(State)
            && State->SessionMarker.IsValid() && IsValid(PlayerController) && IsValid(PlayerController->PlayerState)
            && State->PlayerArray.Contains(PlayerController->PlayerState))
        {
            _State = EState::Connected;
            UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Connected %s; admitted peer %d; session %s"),
                *_Endpoint, PlayerController->PlayerState->GetPlayerId(), *State->SessionMarker.ToString());
        }
        else if (FPlatformTime::Seconds() > _Deadline)
        {
            _State = EState::Failed;
            _LastError = TEXT("Join timed out before IP admission and replicated session evidence.");
            DoReturnToGym(true);
        }
    }
    else if (_State == EState::Connected && !HasOpenClientConnection(World))
    {
        _State = EState::Failed;
        _LastError = TEXT("The host connection closed. X returns to Offline; R reconnects after the host restarts.");
        DoReturnToGym(true);
    }
    else if (_State == EState::Hosting && !IsValid(Cast<UIpNetDriver>(World->GetNetDriver())))
    {
        _State = EState::Failed;
        _LastError = TEXT("The listening IP driver closed.");
    }
}

auto UCkOnlineDirectIpTest_Subsystem::OnNetworkFailure(UWorld* InWorld, UNetDriver* InDriver,
    ENetworkFailure::Type InFailure, const FString& InError) -> void
{
    const auto* Context = GetGameInstance() != nullptr ? GetGameInstance()->GetWorldContext() : nullptr;
    const auto IsOwnWorld = IsValid(InWorld) && InWorld->GetGameInstance() == GetGameInstance();
    const auto IsOwnPendingDriver = Context != nullptr && IsValid(Context->PendingNetGame)
        && Context->PendingNetGame->NetDriver == InDriver;
    if ((!IsOwnWorld && !IsOwnPendingDriver)
        || _State == EState::Offline || _State == EState::Leaving || _State == EState::Failed) { return; }
    _State = EState::Failed;
    _LastError = FString::Printf(TEXT("Network failure: %s. Returning to Offline; rejoin explicitly."), *InError);
    // Network failure can fire during the engine's own driver teardown. Defer our return to Tick.
    _FailureReturnPending = true;
}

auto UCkOnlineDirectIpTest_Subsystem::OnTravelFailure(UWorld* InWorld, ETravelFailure::Type InFailure,
    const FString& InError) -> void
{
    if (!IsValid(InWorld) || InWorld->GetGameInstance() != GetGameInstance() || _State == EState::Offline) { return; }
    const auto WasReturning = _ReturnPending;
    _State = EState::Failed;
    _LastError = FString::Printf(TEXT("Travel failure: %s.%s"), *InError,
        WasReturning ? TEXT(" Return travel failed; reopen the Direct IP gym.") : TEXT(" Returning to Offline."));
    _ReturnPending = false;
    _DepartingWorld.Reset();
    // A failed return must not create an endless travel loop.
    _FailureReturnPending = !WasReturning && !_ReturnMap.IsEmpty();
}

auto UCkOnlineDirectIpTest_Subsystem::IsTickable() const -> bool
{ return !IsTemplate() && !_Deinitialized && IsValid(GetGameInstance()); }

auto UCkOnlineDirectIpTest_Subsystem::GetStatId() const -> TStatId
{ RETURN_QUICK_DECLARE_CYCLE_STAT(UCkOnlineDirectIpTest_Subsystem, STATGROUP_Tickables); }

auto UCkOnlineDirectIpTest_Subsystem::GetTickableGameObjectWorld() const -> UWorld*
{ return GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr; }

auto UCkOnlineDirectIpTest_Subsystem::Get_StateText() const -> FString
{
    switch (_State)
    {
        case EState::Offline: return TEXT("Offline");
        case EState::StartingHost: return TEXT("Starting host (awaiting IP listen)");
        case EState::Hosting: return Get_IsHosting() ? TEXT("Hosting (IP listen)") : TEXT("Hosting (driver unavailable)");
        case EState::Joining: return TEXT("Joining (awaiting admission)");
        case EState::Connected: return Get_IsConnected() ? TEXT("Connected (IP admitted)") : TEXT("Connected (driver unavailable)");
        case EState::Leaving: return TEXT("Leaving (world teardown)");
        case EState::Failed: return TEXT("Failed / disconnected");
    }
    return TEXT("Unknown");
}

auto UCkOnlineDirectIpTest_Subsystem::Get_IsHosting() const -> bool
{
    auto* World = Get_ReadyWorld();
    auto* State = Get_ProofState();
    auto* Driver = World != nullptr ? Cast<UIpNetDriver>(World->GetNetDriver()) : nullptr;
    const auto Address = Driver != nullptr ? Driver->GetLocalAddr() : TSharedPtr<const FInternetAddr>{};
    return _State == EState::Hosting && World != nullptr && World->GetNetMode() == NM_ListenServer
        && IsValid(Driver) && Address.IsValid() && Address->GetPort() == _RequestedHostPort
        && IsValid(State) && State->SessionMarker.IsValid();
}

auto UCkOnlineDirectIpTest_Subsystem::Get_IsConnected() const -> bool
{
    auto* World = Get_ReadyWorld();
    auto* State = Get_ProofState();
    return _State == EState::Connected && World != nullptr && World->GetNetMode() == NM_Client
        && HasOpenClientConnection(World) && IsValid(State) && State->SessionMarker.IsValid();
}

auto UCkOnlineDirectIpTest_Subsystem::Get_IsOffline() const -> bool
{
    auto* World = Get_ReadyWorld();
    return _State == EState::Offline && World != nullptr && World->GetNetDriver() == nullptr
        && World->GetNetMode() == NM_Standalone;
}

auto UCkOnlineDirectIpTest_Subsystem::Get_SessionMarkerText() const -> FString
{
    auto* State = Get_ProofState();
    return IsValid(State) && State->SessionMarker.IsValid() ? State->SessionMarker.ToString() : TEXT("none (no hosted session)");
}

auto UCkOnlineDirectIpTest_Subsystem::Get_PeerRosterText() const -> FString
{
    auto* State = Get_ProofState();
    if (!IsValid(State)) { return TEXT("none"); }
    TArray<FString> Entries;
    for (const auto& Player : State->PlayerArray)
    {
        if (IsValid(Player)) { Entries.Add(FString::Printf(TEXT("%d:%s"), Player->GetPlayerId(), *Player->GetPlayerName())); }
    }
    Entries.Sort();
    return FString::Join(Entries, TEXT(" | "));
}

auto UCkOnlineDirectIpTest_Subsystem::Get_PeerCount() const -> int32
{
    auto* State = Get_ProofState();
    if (!IsValid(State)) { return 0; }
    int32 Count = 0;
    for (const auto& Player : State->PlayerArray) { if (IsValid(Player)) { ++Count; } }
    return Count;
}

auto UCkOnlineDirectIpTest_Subsystem::Get_ProofSequence() const -> int32
{ auto* State = Get_ProofState(); return IsValid(State) ? State->ProofSequence : 0; }

auto UCkOnlineDirectIpTest_Subsystem::Get_LastProofPlayerId() const -> int32
{ auto* State = Get_ProofState(); return IsValid(State) ? State->LastProofPlayerId : INDEX_NONE; }

auto UCkOnlineDirectIpTest_Subsystem::Get_JoinCount() const -> int32
{ auto* State = Get_ProofState(); return IsValid(State) ? State->JoinCount : 0; }

auto UCkOnlineDirectIpTest_Subsystem::Get_LeaveCount() const -> int32
{ auto* State = Get_ProofState(); return IsValid(State) ? State->LeaveCount : 0; }
