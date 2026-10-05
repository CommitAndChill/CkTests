#include "CkOnline/CkOnlineDirectIpTest_Subsystem.h"
#include "CkOnline/CkOnlineDirectGym_Subsystem.h"
#include "CkOnline/Direct/CkOnlineDirect_Subsystem.h"
#include "CkOnline/Direct/CkOnlineDirect_Utils.h"
#include "CkOnline/Direct/CkOnlineDirect_ListenTarget.h"
#include "CkOnline/Connection/CkOnlineConnection_Subsystem.h"
#include "Misc/PackageName.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "IPAddress.h"
#include "IpNetDriver.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "SocketSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ObjectKey.h"

DEFINE_LOG_CATEGORY_STATIC(LogCkOnlineDirectIpTest, Log, All);

namespace
{
    constexpr auto GymGameOption = TEXT("game=/Script/Angelscript.Ck_OnlineGym_DirectIp_GameMode");
    constexpr auto LobbyGameOption = TEXT("game=/Script/Angelscript.Ck_OnlineGym_DirectLobby_GameMode");
    constexpr auto GymGameModePath = TEXT("/Script/Angelscript.Ck_OnlineGym_DirectIp_GameMode");
    constexpr auto LobbyGameModePath = TEXT("/Script/Angelscript.Ck_OnlineGym_DirectLobby_GameMode");
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

    auto HasIdleOwnedTravelContext(UWorld* InWorld, UGameInstance* InInstance) -> bool
    {
        if (!IsInGameThread() || GEngine == nullptr || !IsValid(InWorld) || !IsValid(InInstance)) { return false; }
        if (InWorld->GetGameInstance() != InInstance) { return false; }
        const auto* Context = GEngine->GetWorldContextFromWorld(InWorld);
        if (Context == nullptr || Context != InInstance->GetWorldContext() ||
            Context->OwningGameInstance != InInstance || Context->World() != InWorld) { return false; }
        return Context->PendingNetGame == nullptr && Context->TravelURL.IsEmpty() && InWorld->NextURL.IsEmpty();
    }
}

auto ACkOnlineDirectIpTest_GameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const -> void
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, SessionMarker);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, LobbyEpoch);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, ProofSequence);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, LastProofPlayerId);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, JoinCount);
    DOREPLIFETIME(ACkOnlineDirectIpTest_GameState, LeaveCount);
}

auto ACkOnlineDirectIpTest_GameState::Request_BeginHosting(FGuid InLobbyEpoch) -> bool
{
    auto* World = GetWorld();
    if (!HasAuthority() || SessionMarker.IsValid() || !IsValid(World) || World->GetNetMode() != NM_ListenServer
        || !IsValid(Cast<UIpNetDriver>(World->GetNetDriver()))) { return false; }
    LobbyEpoch = InLobbyEpoch;
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
    if (LobbyEpoch.IsValid())
    {
        auto* World = GetWorld();
        auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        auto* Bridge = IsValid(Instance) ? Instance->GetSubsystem<UCkOnlineDirectIpTest_Subsystem>() : nullptr;
        if (!IsValid(Bridge) || !Bridge->_BridgeActive || Bridge->_BridgeBinding.Get_Epoch() != LobbyEpoch ||
            !Bridge->Get_IsHosting()) { return false; }
    }
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
    InCollection.InitializeDependency<UCk_OnlineConnection_Subsystem_UE>();
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
    _BridgeActive = false;
    _BridgeBinding = {};
    _QueuedTravelTestPending = false;
    _QueuedTravelTestDepartureWorld.Reset();
    _QueuedTravelTestBinding = {};
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

auto UCkOnlineDirectIpTest_Subsystem::TryCaptureOwnedLobby(bool InHosting,
    ck::test::ip::FLobbyBinding& Out) const -> bool
{
    Out = {};
    if (!IsInGameThread()) { return false; }
    auto* Instance = GetGameInstance();
    if (_Deinitialized || !IsValid(Instance)) { return false; }
    auto* Gym = Instance->GetSubsystem<UCkOnlineDirectGym_Subsystem>();
    auto* Direct = Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>();
    if (!IsValid(Gym) || !IsValid(Direct) || !Gym->Get_IsOwnedProfile() || !Direct->Get_IsStarted()) { return false; }
    return ck::test::ip::FLobbyBinding::TryCapture(Direct->Get_Snapshot(), InHosting, 2, Out);
}

auto UCkOnlineDirectIpTest_Subsystem::Get_BridgeStillValid() const -> bool
{
    ck::test::ip::FLobbyBinding Current;
    return _BridgeActive && TryCaptureOwnedLobby(_BridgeBinding.Get_Hosting(), Current) &&
        Current.Get_Generation() == _BridgeBinding.Get_Generation() && Current.Get_Epoch() == _BridgeBinding.Get_Epoch() &&
        Current.Get_LocalMemberId() == _BridgeBinding.Get_LocalMemberId() && Current.Get_HostAddress() == _BridgeBinding.Get_HostAddress() &&
        Current.Get_Members() == _BridgeBinding.Get_Members();
}

auto UCkOnlineDirectIpTest_Subsystem::TryGetTestControlService(
    UCk_OnlineDirect_Subsystem_UE*& OutService) -> bool
{
    OutService = nullptr;
    if (!IsInGameThread() || _Deinitialized)
    { return DoReject(TEXT("Control fault test requires the active game thread.")); }
    if (_TestControlFaultQueued || _ReturnPending || _FailureReturnPending ||
        !_BridgeActive || !_ReturnToLobby || (_State != EState::Hosting && _State != EState::Connected))
    { return DoReject(TEXT("Control fault test requires an active bridged gameplay session.")); }
    if (!Get_BridgeStillValid())
    { return DoReject(TEXT("The owned ready control lobby no longer matches this gameplay bridge.")); }
    auto* State = Get_ProofState();
    if (!IsValid(State) || !State->SessionMarker.IsValid() || !State->LobbyEpoch.IsValid() ||
        State->LobbyEpoch != _BridgeBinding.Get_Epoch())
    { return DoReject(TEXT("Gameplay session or replicated lobby epoch is unavailable.")); }
    if ((_BridgeBinding.Get_Hosting() && !Get_IsHosting()) ||
        (!_BridgeBinding.Get_Hosting() && !Get_IsConnected()))
    { return DoReject(TEXT("Established gameplay IP state does not match the control role.")); }
    auto* Instance = GetGameInstance();
    if (!IsValid(Instance)) { return DoReject(TEXT("Game instance is unavailable.")); }
    auto* Gym = Instance->GetSubsystem<UCkOnlineDirectGym_Subsystem>();
    auto* Direct = Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>();
    if (!IsValid(Gym) || !IsValid(Direct) || !Gym->Get_IsOwnedProfile() || !Direct->Get_IsStarted())
    { return DoReject(TEXT("The active Direct control profile is not owned by this gym.")); }
    const auto Snapshot = Direct->Get_Snapshot();
    if (Snapshot.Get_ServiceGeneration() != _BridgeBinding.Get_Generation() ||
        !_BridgeBinding.Matches(Snapshot))
    { return DoReject(TEXT("The Direct control generation or ready roster changed.")); }
    OutService = Direct;
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::TryParseEndpoint(const FString& InEndpoint, FString& OutCanonical) -> bool
{
    return UCk_Utils_OnlineDirect_UE::TryParseGameplayEndpoint(InEndpoint, OutCanonical);
}

auto UCkOnlineDirectIpTest_Subsystem::TryParseLobbyEndpoint(const FString& InEndpoint,
    const ck::test::ip::FLobbyBinding& InBinding, FString& OutCanonical) -> bool
{
    return UCk_Utils_OnlineDirect_UE::TryParseReadyLobbyEndpoint(InEndpoint, InBinding, OutCanonical);
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Host(int32 InPort) -> bool
{
    return DoRequestHost(InPort, nullptr);
}

auto UCkOnlineDirectIpTest_Subsystem::Request_HostFromReadyLobby(int32 InPort) -> bool
{
    if (!IsInGameThread() || _Deinitialized) { return false; }
    if (_State != EState::Offline) { return DoReject(TEXT("Stop the current IP session first.")); }
    ck::test::ip::FLobbyBinding Binding;
    if (!TryCaptureOwnedLobby(true, Binding))
    { return DoReject(TEXT("An owned, two-member ready hosting lobby is required.")); }
    return DoRequestHost(InPort, &Binding);
}

auto UCkOnlineDirectIpTest_Subsystem::DoRequestHost(int32 InPort,
    const ck::test::ip::FLobbyBinding* InBinding) -> bool
{
    if (!IsInGameThread() || _Deinitialized) { return false; }
    if (InBinding != nullptr) { return DoRequestProductionEntry(true, InPort, FString{}, *InBinding); }
    const bool Bridged = InBinding != nullptr;
    auto* World = Get_ReadyWorld();
    if (World == nullptr) { return DoReject(TEXT("No ready game world.")); }
    if (_State != EState::Offline || World->GetNetDriver() != nullptr || World->GetNetMode() != NM_Standalone)
    { return DoReject(TEXT("Host needs an Offline Standalone instance. Automatic PIE networking is not this gate.")); }
    if (!HasIdleOwnedTravelContext(World, GetGameInstance()))
    { return DoReject(TEXT("Host needs its idle owned travel context; queued or pending travel must settle first.")); }
    auto* State = Get_ProofState();
    if ((!Bridged && (!IsValid(State) || State->SessionMarker.IsValid())) ||
        (Bridged && IsValid(State)))
    { return DoReject(TEXT("Select the expected Online gym before hosting.")); }
    if (Bridged)
    {
        ck::test::ip::FLobbyBinding Current;
        if (!TryCaptureOwnedLobby(true, Current) || Current.Get_Generation() != InBinding->Get_Generation() ||
            Current.Get_Epoch() != InBinding->Get_Epoch() || Current.Get_LocalMemberId() != InBinding->Get_LocalMemberId() ||
            Current.Get_HostAddress() != InBinding->Get_HostAddress() || Current.Get_Members() != InBinding->Get_Members() || !InBinding->Get_Hosting())
        { return DoReject(TEXT("The ready hosting lobby changed before travel.")); }
    }
    if (InPort < 1 || InPort > 65535) { return DoReject(TEXT("Port must be 1..65535.")); }
    if (!HasIpDriverDefinition()) { return DoReject(TEXT("GameNetDriver must use IpNetDriver for this transport gate.")); }

    if (GEngine == nullptr || GEngine->GetWorldContextFromWorld(World) == nullptr)
    { return DoReject(TEXT("The host world has no travel context.")); }
    const auto ReturnMap = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
    if (ReturnMap.IsEmpty()) { return DoReject(TEXT("The host world has no return map.")); }
    const auto Map = TSoftObjectPtr<UWorld>{FSoftObjectPath{
        ReturnMap + TEXT(".") + FPackageName::GetShortName(ReturnMap)}};
    const auto GameMode = TSubclassOf<AGameModeBase>{FindObject<UClass>(nullptr, GymGameModePath)};
    auto ListenTarget = FCk_OnlineDirect_ListenTarget{};
    const bool TargetIsValid = Bridged
        ? UCk_Utils_OnlineDirect_UE::TryBuildReadyLobbyListenTarget(*InBinding, Map, GameMode, InPort, ListenTarget)
        : UCk_Utils_OnlineDirect_UE::TryBuildListenTarget(Map, GameMode, TEXT("127.0.0.1"), InPort, ListenTarget);
    if (!TargetIsValid)
    { return DoReject(TEXT("Could not form a local listen travel URL for the requested port.")); }
    if (!HasIdleOwnedTravelContext(World, GetGameInstance()))
    { return DoReject(TEXT("Host travel context changed during preparation; existing travel is preserved.")); }
    _UsesFoundationOwner = false;
    _BridgeActive = Bridged;
    _TestControlFaultQueued = false;
    _ReturnToLobby = Bridged;
    _BridgeBinding = Bridged ? *InBinding : ck::test::ip::FLobbyBinding{};
    _BridgeLastBinding = _BridgeBinding;
    _BridgeReadback = Bridged ? TEXT("Hosting gameplay from ready lobby") : TEXT("none");
    _ReturnMap = ReturnMap;
    _RequestedHostPort = InPort;
    _HostingDepartureWorld = World;
    _Endpoint = ListenTarget.Get_GameplayEndpoint();
    _FailureIsolationReadback = TEXT("Not exercised");
    _FailureIsolationObservations = 0;
    _QueuedTravelIsolationReadback = TEXT("Not exercised");
    _QueuedTravelTestObservations = 0;
    _QueuedTravelTestPending = false;
    _LastError.Empty();
    _State = EState::StartingHost;
    _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    GEngine->SetClientTravel(World, *ListenTarget.Get_TravelUrl(), TRAVEL_Absolute);
    UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Hosting requested for %s (local listen travel issued; bound driver not yet proven)"), *_Endpoint);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Join(const FString& InEndpoint) -> bool
{
    return DoRequestJoin(InEndpoint, nullptr);
}

auto UCkOnlineDirectIpTest_Subsystem::Request_JoinFromReadyLobby(const FString& InEndpoint) -> bool
{
    if (!IsInGameThread() || _Deinitialized) { return false; }
    if (_State != EState::Offline) { return DoReject(TEXT("Stop the current IP session first.")); }
    ck::test::ip::FLobbyBinding Binding;
    if (!TryCaptureOwnedLobby(false, Binding))
    { return DoReject(TEXT("An owned, two-member ready joined lobby is required.")); }
    return DoRequestJoin(InEndpoint, &Binding);
}

auto UCkOnlineDirectIpTest_Subsystem::DoRequestJoin(const FString& InEndpoint,
    const ck::test::ip::FLobbyBinding* InBinding) -> bool
{
    if (!IsInGameThread() || _Deinitialized) { return false; }
    if (InBinding != nullptr) { return DoRequestProductionEntry(false, 0, InEndpoint, *InBinding); }
    const bool Bridged = InBinding != nullptr;
    FString Canonical;
    if (Bridged)
    {
        if (!TryParseLobbyEndpoint(InEndpoint, *InBinding, Canonical))
        { return DoReject(TEXT("Gameplay endpoint must use the admitted lobby's host IPv4 address and a valid port.")); }
    }
    else if (!TryParseEndpoint(InEndpoint, Canonical))
    { return DoReject(TEXT("Use a literal IPv4:port, e.g. 127.0.0.1:7787.")); }
    auto* World = Get_ReadyWorld();
    if (World == nullptr) { return DoReject(TEXT("No ready game world.")); }
    if (_State != EState::Offline || World->GetNetDriver() != nullptr || World->GetNetMode() != NM_Standalone)
    { return DoReject(TEXT("Join needs an Offline Standalone instance. Disconnect with X first.")); }
    if (!HasIdleOwnedTravelContext(World, GetGameInstance()))
    { return DoReject(TEXT("Join needs its idle owned travel context; queued or pending travel must settle first.")); }
    if ((Bridged && IsValid(Get_ProofState())) ||
        (!Bridged && !IsValid(Get_ProofState())))
    { return DoReject(TEXT("Select the expected Online gym before joining.")); }
    if (Bridged)
    {
        ck::test::ip::FLobbyBinding Current;
        if (!TryCaptureOwnedLobby(false, Current) || Current.Get_Generation() != InBinding->Get_Generation() ||
            Current.Get_Epoch() != InBinding->Get_Epoch() || Current.Get_LocalMemberId() != InBinding->Get_LocalMemberId() ||
            Current.Get_HostAddress() != InBinding->Get_HostAddress() || Current.Get_Members() != InBinding->Get_Members() || InBinding->Get_Hosting())
        { return DoReject(TEXT("The ready joined lobby changed before travel.")); }
    }
    auto* PlayerController = GetGameInstance()->GetFirstLocalPlayerController();
    if (!IsValid(PlayerController)) { return DoReject(TEXT("No local player controller.")); }
    if (!HasIpDriverDefinition()) { return DoReject(TEXT("GameNetDriver must use IpNetDriver for this transport gate.")); }

    const auto ReturnMap = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
    if (ReturnMap.IsEmpty() || GEngine == nullptr || GEngine->GetWorldContextFromWorld(World) == nullptr)
    { return DoReject(TEXT("The joining world has no return travel context.")); }
    if (!HasIdleOwnedTravelContext(World, GetGameInstance()))
    { return DoReject(TEXT("Join travel context changed during preparation; existing travel is preserved.")); }
    _UsesFoundationOwner = false;
    _BridgeActive = Bridged;
    _TestControlFaultQueued = false;
    _ReturnToLobby = Bridged;
    _BridgeBinding = Bridged ? *InBinding : ck::test::ip::FLobbyBinding{};
    _BridgeLastBinding = _BridgeBinding;
    _BridgeReadback = Bridged ? TEXT("Joining gameplay from ready lobby") : TEXT("none");
    _ReturnMap = ReturnMap;
    _Endpoint = MoveTemp(Canonical);
    _FailureIsolationReadback = TEXT("Not exercised");
    _FailureIsolationObservations = 0;
    _QueuedTravelIsolationReadback = TEXT("Not exercised");
    _QueuedTravelTestObservations = 0;
    _QueuedTravelTestPending = false;
    _HasJoinEndpoint = true;
    _LastError.Empty();
    _State = EState::Joining;
    _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    PlayerController->ClientTravel(_Endpoint, TRAVEL_Absolute);
    UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Joining %s (request issued; admission not yet proven)"), *_Endpoint);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::DoRequestProductionEntry(bool InHosting, int32 InPort,
    const FString& InEndpoint, const ck::test::ip::FLobbyBinding& InBinding) -> bool
{
    auto* Instance = GetGameInstance();
    auto* World = Get_ReadyWorld();
    auto* Owner = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineConnection_Subsystem_UE>() : nullptr;
    auto* Direct = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>() : nullptr;
    if (!IsValid(World) || !IsValid(Owner) || !IsValid(Direct) || IsValid(Get_ProofState()) ||
        _State != EState::Offline || !Owner->Get_IsIdle())
    { return DoReject(TEXT("Gameplay entry needs the idle owned Lobby; queued or pending travel must settle first.")); }
    ck::test::ip::FLobbyBinding Current;
    if (!TryCaptureOwnedLobby(InHosting, Current) || !InBinding.Matches(Direct->Get_Snapshot()))
    { return DoReject(TEXT("The owned two-member ready lobby changed before entry.")); }
    const auto ReturnMap = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
    const auto Map = TSoftObjectPtr<UWorld>{FSoftObjectPath{
        ReturnMap + TEXT(".") + FPackageName::GetShortName(ReturnMap)}};
    auto Spec = FCk_OnlineConnection_Spec{};
    Spec.Set_DirectService(Direct->Get_Service()).Set_Binding(InBinding).Set_ExpectedMembers(2)
        .Set_GameplayMap(Map).Set_GameplayMode(TSubclassOf<AGameModeBase>{FindObject<UClass>(nullptr, GymGameModePath)})
        .Set_ReturnMap(Map).Set_ReturnMode(TSubclassOf<AGameModeBase>{FindObject<UClass>(nullptr, LobbyGameModePath)})
        .Set_ListenPort(InPort).Set_Endpoint(InEndpoint);
    auto Request = FCk_Request_OnlineConnection{};
    Request.Set_Action(InHosting ? ECk_OnlineConnection_Action::Host : ECk_OnlineConnection_Action::Join).Set_Spec(Spec);
    auto Operation = FCk_OnlineConnection_Operation{};
    if (!Owner->TryRequest(Request, Operation))
    { return DoReject(TEXT("Foundation rejected the entry target, ready binding, driver or travel context.")); }
    _UsesFoundationOwner = true;
    _ProductionEntry = Operation;
    _ConfirmationQueued = false;
    _ProductionReturnQueued = false;
    _BridgeActive = true;
    _BridgeBinding = InBinding;
    _BridgeLastBinding = InBinding;
    _BridgeReadback = TEXT("Foundation entry queued; transport and gym readiness pending");
    _TestControlFaultQueued = false;
    _ReturnToLobby = true;
    _ReturnMap = ReturnMap;
    _RequestedHostPort = InPort;
    _Endpoint = InHosting ? FString::Printf(TEXT("%s:%d"), *InBinding.Get_HostAddress(), InPort) : InEndpoint;
    _HasJoinEndpoint = !InHosting;
    _State = InHosting ? EState::StartingHost : EState::Joining;
    _ReturnPending = false;
    _FailureReturnPending = false;
    _FailureIsolationReadback = TEXT("Not exercised");
    _FailureIsolationObservations = 0;
    _QueuedTravelIsolationReadback = TEXT("Not exercised");
    _QueuedTravelTestObservations = 0;
    _QueuedTravelTestPending = false;
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::DoObserveProductionOwner() -> void
{
    auto* Instance = GetGameInstance();
    auto* Owner = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineConnection_Subsystem_UE>() : nullptr;
    if (!IsValid(Owner)) { _State = EState::Failed; _LastError = TEXT("Foundation connection owner unavailable."); return; }
    const auto Snapshot = Owner->Get_Snapshot();
    const auto State = Snapshot.Get_State();
    if (!Snapshot.Get_LastError().IsEmpty()) { _LastError = Snapshot.Get_LastError(); }
    _ReturnPending = State == ECk_OnlineConnection_State::AwaitingReturn || State == ECk_OnlineConnection_State::Returning;
    _FailureReturnPending = false;
    if (_ReturnPending)
    {
        _State = EState::Leaving;
        _BridgeReadback = TEXT("Foundation is returning to the configured Lobby");
        return;
    }
    if (State == ECk_OnlineConnection_State::Idle)
    {
        if (_ProductionEntry.Get_Id().IsValid() && !Owner->Get_IsIdle()) { return; }
        if (_BridgeActive)
        {
            _BridgeReadback = Get_BridgeStillValid() ? TEXT("Returned to Online Lobby; control lobby preserved") :
                TEXT("Returned to Online Lobby; control lobby changed or was lost");
        }
        _BridgeActive = false;
        _BridgeBinding = {};
        _HasJoinEndpoint = false;
        _ProductionReturnQueued = false;
        _ConfirmationQueued = false;
        _State = EState::Offline;
        return;
    }
    if (State == ECk_OnlineConnection_State::Failed || State == ECk_OnlineConnection_State::Stopped)
    {
        _State = EState::Failed;
        _BridgeActive = false;
        _BridgeBinding = {};
        _BridgeReadback = TEXT("Foundation return failed or owner stopped; inspect its terminal result");
        return;
    }
    if (State == ECk_OnlineConnection_State::Active)
    {
        _State = Snapshot.Get_Hosting() ? EState::Hosting : EState::Connected;
        _BridgeReadback = TEXT("Foundation Active; gym replicated readiness confirmed");
        return;
    }
    _State = Snapshot.Get_Hosting() ? EState::StartingHost : EState::Joining;
    if (State != ECk_OnlineConnection_State::AwaitingGameReady || _ConfirmationQueued || !Get_BridgeStillValid()) { return; }
    auto* World = Get_ReadyWorld();
    auto* Proof = Get_ProofState();
    if (!IsValid(World) || !IsValid(Proof)) { return; }
    bool Ready = false;
    if (Snapshot.Get_Hosting())
    {
        if (World->GetNetMode() == NM_ListenServer && Proof->PlayerArray.Num() == 1)
        {
            if (!Proof->SessionMarker.IsValid()) { Proof->Request_BeginHosting(_BridgeBinding.Get_Epoch()); }
            Ready = Proof->SessionMarker.IsValid() && Proof->LobbyEpoch == _BridgeBinding.Get_Epoch();
        }
    }
    else
    {
        auto* Controller = Instance->GetFirstLocalPlayerController();
        Ready = World->GetNetMode() == NM_Client && HasOpenClientConnection(World) &&
            Proof->SessionMarker.IsValid() && Proof->LobbyEpoch == _BridgeBinding.Get_Epoch() &&
            IsValid(Controller) && IsValid(Controller->PlayerState) && Proof->PlayerArray.Contains(Controller->PlayerState);
    }
    if (!Ready) { return; }
    auto Request = FCk_Request_OnlineConnection{};
    Request.Set_Action(ECk_OnlineConnection_Action::ConfirmGameReady).Set_Target(_ProductionEntry);
    auto Confirmation = FCk_OnlineConnection_Operation{};
    _ConfirmationQueued = Owner->TryRequest(Request, Confirmation);
}

auto UCkOnlineDirectIpTest_Subsystem::Get_ConnectionOwnerText() const -> FString
{
    auto* Instance = GetGameInstance();
    auto* Owner = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineConnection_Subsystem_UE>() : nullptr;
    if (!IsValid(Owner)) { return TEXT("Foundation: Stopped"); }
    const auto Snapshot = Owner->Get_Snapshot();
    const auto Completion = Owner->Get_LastCompletion();
    const auto StateName = StaticEnum<ECk_OnlineConnection_State>()->GetNameStringByValue(static_cast<int64>(Snapshot.Get_State()));
    const auto ActionName = StaticEnum<ECk_OnlineConnection_Action>()->GetNameStringByValue(static_cast<int64>(Completion.Get_Action()));
    const auto ResultName = StaticEnum<ECk_OnlineConnection_Result>()->GetNameStringByValue(static_cast<int64>(Completion.Get_Result()));
    return FString::Printf(TEXT("Foundation: %s | op %s | session %s | completed %d | %s %s"),
        *StateName, *Snapshot.Get_Operation().Get_Id().ToString(EGuidFormats::Digits).Left(8),
        *Snapshot.Get_Session().Get_Id().ToString(EGuidFormats::Digits).Left(8), Owner->Get_CompletedCount(), *ActionName, *ResultName);
}

auto UCkOnlineDirectIpTest_Subsystem::DoReturnToGym(bool InKeepFailure) -> bool
{
    auto* World = Get_ReadyWorld();
    if (World == nullptr || _ReturnMap.IsEmpty()) { return false; }
    auto* Context = GEngine != nullptr ? GEngine->GetWorldContextFromWorld(World) : nullptr;
    if (Context == nullptr || Context->OwningGameInstance != GetGameInstance())
    {
        _State = EState::Failed;
        _LastError = TEXT("The returning world has no owned travel context.");
        _ReturnPending = false;
        _FailureReturnPending = false;
        return false;
    }
    _DepartingWorld = World;
    _HostingDepartureWorld.Reset();
    _ReturnPending = true;
    _FailureReturnPending = false;
    if (!InKeepFailure) { _State = EState::Leaving; }
    _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    // Local OpenLevel does not cancel an outstanding network browse. Let the engine
    // tear down only this GameInstance's pending driver before queuing the return.
    if (Context->PendingNetGame != nullptr) { GEngine->CancelPending(World); }
    UGameplayStatics::OpenLevel(World, FName(*_ReturnMap), true,
        _ReturnToLobby ? LobbyGameOption : GymGameOption);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Disconnect() -> bool
{
    if (!IsInGameThread() || _Deinitialized) { return false; }
    if (_UsesFoundationOwner)
    {
        auto* Owner = GetGameInstance()->GetSubsystem<UCk_OnlineConnection_Subsystem_UE>();
        if (!IsValid(Owner) || _ProductionReturnQueued) { return DoReject(TEXT("No active Foundation operation to return.")); }
        const auto Snapshot = Owner->Get_Snapshot();
        auto Request = FCk_Request_OnlineConnection{};
        Request.Set_Action(ECk_OnlineConnection_Action::Return).Set_Target(
            Snapshot.Get_State() == ECk_OnlineConnection_State::Active ? Snapshot.Get_Session() : Snapshot.Get_Operation());
        auto Operation = FCk_OnlineConnection_Operation{};
        if (!Owner->TryRequest(Request, Operation)) { return DoReject(TEXT("Foundation return was rejected; wait for the current operation.")); }
        _ProductionReturnQueued = true;
        return true;
    }
    if (_State == EState::Offline || _State == EState::Leaving) { return DoReject(TEXT("No active connection to leave.")); }
    if (Get_ReadyWorld() == nullptr || _ReturnMap.IsEmpty()) { return DoReject(TEXT("No return world available.")); }
    _LastError.Empty();
    return DoReturnToGym(false);
}

auto UCkOnlineDirectIpTest_Subsystem::Request_Rejoin() -> bool
{
    if (!_HasJoinEndpoint) { return DoReject(TEXT("Join once with K before using Rejoin.")); }
    if (_BridgeActive || _BridgeLastBinding.Get_Epoch().IsValid())
    { return DoReject(TEXT("Use the ready-lobby gameplay action for a new correlated session.")); }
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

auto UCkOnlineDirectIpTest_Subsystem::Request_TestControlReadyLoss() -> bool
{
    UCk_OnlineDirect_Subsystem_UE* Direct = nullptr;
    if (!TryGetTestControlService(Direct)) { return false; }
    if (!Direct->Request(ECk_OnlineDirect_Action::SetReady, FString(), false))
    { return DoReject(TEXT("Ready-loss control request was not queued.")); }
    _TestControlFaultQueued = true;
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_TestControlStop() -> bool
{
    UCk_OnlineDirect_Subsystem_UE* Direct = nullptr;
    if (!TryGetTestControlService(Direct)) { return false; }
    Direct->Request_Stop();
    _TestControlFaultQueued = true;
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::DoObserveQueuedLobbyReload() -> void
{
    if (!_QueuedTravelTestPending || _QueuedTravelTestActive || _Deinitialized || !IsInGameThread()) { return; }
    if (FPlatformTime::Seconds() > _QueuedTravelTestDeadline)
    {
        _QueuedTravelTestPending = false;
        _QueuedTravelTestDepartureWorld.Reset();
        _QueuedTravelTestBinding = {};
        _QueuedTravelIsolationReadback = TEXT("CHANGED: local Lobby reload did not settle before the bounded deadline.");
        return;
    }
    auto* World = Get_ReadyWorld();
    if (IsValid(World) && World != _QueuedTravelTestDepartureWorld.Get())
    {
        auto* Instance = GetGameInstance();
        const auto* Context = IsValid(Instance) ? Instance->GetWorldContext() : nullptr;
        auto* Direct = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>() : nullptr;
        auto* Gym = IsValid(Instance) ? Instance->GetSubsystem<UCkOnlineDirectGym_Subsystem>() : nullptr;
        const auto* Mode = World->GetAuthGameMode();
        const bool Preserved = Get_IsOffline() && Context != nullptr && Context->OwningGameInstance == Instance &&
            Context->World() == World && Context->PendingNetGame == nullptr && Context->TravelURL.IsEmpty() &&
            World->NextURL.IsEmpty() && UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) == _QueuedTravelTestMap &&
            IsValid(Mode) && Mode->GetClass()->GetPathName() == LobbyGameModePath &&
            IsValid(Gym) && Gym->Get_IsOwnedProfile() && IsValid(Direct) && Direct->Get_IsStarted() &&
            _QueuedTravelTestBinding.Matches(Direct->Get_Snapshot()) &&
            Direct->Get_Snapshot().Get_Revision() == _QueuedTravelTestRevision &&
            Direct->Get_CompletedCount() == _QueuedTravelTestCompletedCount;
        _QueuedTravelTestPending = false;
        _QueuedTravelTestDepartureWorld.Reset();
        _QueuedTravelTestBinding = {};
        if (!Preserved)
        {
            _QueuedTravelIsolationReadback = TEXT("CHANGED: local Lobby reload or ready control identity differed.");
            return;
        }
        if (_QueuedTravelTestObservations < MAX_int32) { ++_QueuedTravelTestObservations; }
        _QueuedTravelIsolationReadback = FString::Printf(
            TEXT("OBSERVED %d: competing Join rejected; new idle Lobby, same ready control identity."),
            _QueuedTravelTestObservations);
        return;
    }
}

auto UCkOnlineDirectIpTest_Subsystem::Tick(float InDeltaTime) -> void
{
    DoObserveQueuedLobbyReload();
    if (_UsesFoundationOwner) { DoObserveProductionOwner(); return; }
    if (_BridgeActive && !_ReturnPending && !_FailureReturnPending &&
        _State != EState::Offline && _State != EState::Leaving && !Get_BridgeStillValid())
    {
        _State = EState::Failed;
        _LastError = TEXT("Ready lobby changed or was lost; returning to Online Lobby.");
        _BridgeReadback = TEXT("Control lobby changed or was lost");
        _FailureReturnPending = true;
        _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    }
    if (_BridgeActive && !_ReturnPending && !_FailureReturnPending &&
        ((_State == EState::Connected && !Get_IsConnected()) ||
         (_State == EState::Hosting && !Get_IsHosting())))
    {
        _State = EState::Failed;
        _LastError = TEXT("Established gameplay IP state or replicated lobby epoch was lost; returning to Online Lobby.");
        _BridgeReadback = TEXT("Gameplay evidence lost");
        _FailureReturnPending = true;
        _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    }
    auto* World = Get_ReadyWorld();
    if (World == nullptr)
    {
        if (_FailureReturnPending && FPlatformTime::Seconds() > _Deadline)
        {
            _FailureReturnPending = false;
            _BridgeActive = false;
            _BridgeBinding = {};
            _BridgeReadback = TEXT("World unavailable after failure");
            return;
        }
        if ((_State == EState::StartingHost || _State == EState::Joining) && FPlatformTime::Seconds() > _Deadline)
        {
            _LastError = _State == EState::StartingHost
                ? TEXT("Host travel timed out before the IP listen was established.")
                : TEXT("Join timed out before IP admission and replicated session evidence.");
            _State = EState::Failed;
            _FailureReturnPending = true;
            _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
        }
        else if (_ReturnPending && FPlatformTime::Seconds() > _Deadline)
        {
            _State = EState::Failed;
            _LastError = TEXT("Return to the gym timed out.");
            _DepartingWorld.Reset();
            _ReturnPending = false;
            _BridgeActive = false;
            _BridgeBinding = {};
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
        const auto* ReturnMode = World->GetAuthGameMode();
        const auto* ReturnContext = GEngine != nullptr ? GEngine->GetWorldContextFromWorld(World) : nullptr;
        const auto ReturnMap = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
        const auto ExpectedMode = _ReturnToLobby ? LobbyGameModePath : GymGameModePath;
        if (ReturnContext != nullptr && ReturnContext->OwningGameInstance == GetGameInstance() &&
            HasIdleOwnedTravelContext(World, GetGameInstance()) &&
            World != _DepartingWorld.Get() && World->GetNetDriver() == nullptr &&
            World->GetNetMode() == NM_Standalone && ReturnMap == _ReturnMap &&
            IsValid(ReturnMode) && ReturnMode->GetClass()->GetPathName() == ExpectedMode)
        {
            _DepartingWorld.Reset();
            _State = EState::Offline;
            _ReturnPending = false;
            if (_ReturnToLobby)
            {
                const bool Preserved = _BridgeActive && Get_BridgeStillValid();
                _BridgeReadback = Preserved
                    ? TEXT("Returned to Online Lobby; control lobby preserved")
                    : TEXT("Returned to Online Lobby; control lobby changed or was lost");
                if (!Preserved)
                { _LastError = TEXT("Control lobby changed or was lost during gameplay or return."); }
                _BridgeActive = false;
                _BridgeBinding = {};
                _HasJoinEndpoint = false;
            }
        }
        else if (FPlatformTime::Seconds() > _Deadline)
        {
            _State = EState::Failed;
            _LastError = TEXT("Return to the gym timed out.");
            _DepartingWorld.Reset();
            _ReturnPending = false;
            _BridgeActive = false;
            _BridgeBinding = {};
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
                _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
            }
            else if (State->PlayerArray.Num() == 1 &&
                State->Request_BeginHosting(_BridgeActive ? _BridgeBinding.Get_Epoch() : FGuid{}))
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
            _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
        }
    }
    else if (_State == EState::Joining)
    {
        auto* State = Get_ProofState();
        auto* PlayerController = GetGameInstance()->GetFirstLocalPlayerController();
        if (World->GetNetMode() == NM_Client && HasOpenClientConnection(World) && IsValid(State)
            && State->SessionMarker.IsValid() && IsValid(PlayerController) && IsValid(PlayerController->PlayerState)
            && State->PlayerArray.Contains(PlayerController->PlayerState) &&
            (!_BridgeActive || State->LobbyEpoch == _BridgeBinding.Get_Epoch()))
        {
            _State = EState::Connected;
            UE_LOG(LogCkOnlineDirectIpTest, Display, TEXT("Connected %s; admitted peer %d; session %s"),
                *_Endpoint, PlayerController->PlayerState->GetPlayerId(), *State->SessionMarker.ToString());
        }
        else if (_BridgeActive && IsValid(State) && State->LobbyEpoch.IsValid() &&
            State->LobbyEpoch != _BridgeBinding.Get_Epoch())
        {
            _State = EState::Failed;
            _LastError = TEXT("Gameplay lobby epoch does not match the ready control lobby.");
            _BridgeReadback = TEXT("Replicated lobby epoch mismatch");
            DoReturnToGym(true);
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
        _LastError = _BridgeActive
            ? TEXT("The host connection closed; returning to Online Lobby. Retry Host G, wait Hosting, then Client G.")
            : TEXT("The host connection closed. X returns to Offline; R reconnects after the host restarts.");
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
    if (_UsesFoundationOwner || !IsInGameThread() || _Deinitialized ||
        (_State != EState::StartingHost && _State != EState::Joining &&
         _State != EState::Hosting && _State != EState::Connected)) { return; }
    auto* Instance = GetGameInstance();
    if (!IsValid(Instance)) { return; }
    const auto* Context = Instance->GetWorldContext();
    if (Context == nullptr || Context->OwningGameInstance != Instance) { return; }
    auto* CurrentWorld = Context->World();
    const bool CurrentWorldIsOwned = IsValid(CurrentWorld) && CurrentWorld->GetGameInstance() == Instance;
    const bool DriverIsValid = IsValid(InDriver);
    const bool PendingIsOwned = _State == EState::Joining && IsValid(Context->PendingNetGame);
    const bool IsOwnWorldDriver = DriverIsValid && CurrentWorldIsOwned && InWorld == CurrentWorld &&
        CurrentWorld->GetNetDriver() == InDriver;
    const bool IsOwnPendingDriver = DriverIsValid && PendingIsOwned &&
        Context->PendingNetGame->NetDriver == InDriver;
    // Listen creation can fail before there is a driver; pending failure can have no world.
    // Each null case needs its exact phase/type/context, never nullptr == nullptr ownership.
    const bool IsOwnCreationFailure = InDriver == nullptr && _State == EState::StartingHost &&
        InFailure == ENetworkFailure::NetDriverCreateFailure && CurrentWorldIsOwned &&
        InWorld == CurrentWorld && CurrentWorld->GetNetDriver() == nullptr;
    const bool IsOwnMissingPendingFailure = InDriver == nullptr && InWorld == nullptr && PendingIsOwned &&
        InFailure == ENetworkFailure::PendingConnectionFailure && Context->PendingNetGame->NetDriver == nullptr &&
        !Context->PendingNetGame->ConnectionError.IsEmpty();
    if (!IsOwnWorldDriver && !IsOwnPendingDriver && !IsOwnCreationFailure && !IsOwnMissingPendingFailure) { return; }
    _State = EState::Failed;
    _LastError = FString::Printf(TEXT("Network failure: %s. Returning to Offline; rejoin explicitly."), *InError);
    // Network failure can fire during the engine's own driver teardown. Defer our return to Tick.
    _FailureReturnPending = true;
    _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
}

auto UCkOnlineDirectIpTest_Subsystem::OnTravelFailure(UWorld* InWorld, ETravelFailure::Type InFailure,
    const FString& InError) -> void
{
    if (_UsesFoundationOwner) { return; }
    if (!IsValid(InWorld) || InWorld->GetGameInstance() != GetGameInstance() || _State == EState::Offline) { return; }
    const auto WasReturning = _ReturnPending;
    _State = EState::Failed;
    _LastError = FString::Printf(TEXT("Travel failure: %s.%s"), *InError,
        WasReturning ? TEXT(" Return travel failed; reopen the Direct IP gym.") : TEXT(" Returning to Offline."));
    _ReturnPending = false;
    _DepartingWorld.Reset();
    if (WasReturning)
    {
        _BridgeActive = false;
        _BridgeBinding = {};
        _BridgeReadback = TEXT("Return travel failed");
    }
    // A failed return must not create an endless travel loop.
    _FailureReturnPending = !WasReturning && !_ReturnMap.IsEmpty();
    if (_FailureReturnPending) { _Deadline = FPlatformTime::Seconds() + JoinTimeoutSeconds; }
}

auto UCkOnlineDirectIpTest_Subsystem::IsTickable() const -> bool
{ return !IsTemplate() && !_Deinitialized && IsValid(GetGameInstance()); }

auto UCkOnlineDirectIpTest_Subsystem::GetStatId() const -> TStatId
{ RETURN_QUICK_DECLARE_CYCLE_STAT(UCkOnlineDirectIpTest_Subsystem, STATGROUP_Tickables); }

auto UCkOnlineDirectIpTest_Subsystem::GetTickableGameObjectWorld() const -> UWorld*
{ return GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr; }

auto UCkOnlineDirectIpTest_Subsystem::Get_LobbyBridgeText() const -> FString
{
    if (!_BridgeLastBinding.Get_Epoch().IsValid()) { return TEXT("none"); }
    return FString::Printf(TEXT("%s / captured %s / %s"),
        _BridgeActive ? TEXT("active") : TEXT("inactive"),
        *_BridgeLastBinding.Get_Epoch().ToString(EGuidFormats::Digits), *_BridgeReadback);
}

auto UCkOnlineDirectIpTest_Subsystem::Get_LobbyEpochText() const -> FString
{
    auto* State = _BridgeActive ? Get_ProofState() : nullptr;
    return IsValid(State) && State->LobbyEpoch.IsValid()
        ? State->LobbyEpoch.ToString(EGuidFormats::Digits) : TEXT("none");
}

auto UCkOnlineDirectIpTest_Subsystem::Request_LogCheckpoint() -> bool
{
    if (!IsInGameThread() || !_BridgeActive || !Get_BridgeStillValid()) { return false; }
    const bool Hosting = Get_IsHosting();
    const bool Connected = Get_IsConnected();
    if (!Hosting && !Connected) { return false; }
    auto* State = Get_ProofState();
    if (!IsValid(State) || !State->SessionMarker.IsValid() ||
        State->LobbyEpoch != _BridgeBinding.Get_Epoch()) { return false; }
    const TCHAR* Role = Hosting ? TEXT("Host") : TEXT("Client");
    const TCHAR* IpState = Hosting ? TEXT("Hosting") : TEXT("Connected");
    UE_LOG(LogCkOnlineDirectIpTest, Display,
        TEXT("[OnlineGameplayCheckpoint] schema=1 pid=%u generation=%s epoch=%s local=%s role=%s ipState=%s session=%s replicatedEpoch=%s peers=%d proofSequence=%d proofPlayerId=%d"),
        FPlatformProcess::GetCurrentProcessId(),
        *_BridgeBinding.Get_Generation().ToString(EGuidFormats::Digits),
        *_BridgeBinding.Get_Epoch().ToString(EGuidFormats::Digits),
        *_BridgeBinding.Get_LocalMemberId().ToString(EGuidFormats::Digits), Role, IpState,
        *State->SessionMarker.ToString(EGuidFormats::Digits),
        *State->LobbyEpoch.ToString(EGuidFormats::Digits), Get_PeerCount(),
        State->ProofSequence, State->LastProofPlayerId);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_TestUnrelatedNetworkFailure() -> bool
{
    if (!IsInGameThread() || _Deinitialized || _FailureIsolationTestActive) { return false; }
    if (_TestControlFaultQueued || _ReturnPending || _FailureReturnPending || !_BridgeActive ||
        !Get_BridgeStillValid() || (!Get_IsHosting() && !Get_IsConnected()))
    {
        _FailureIsolationReadback = TEXT("Not run: requires established bridged gameplay.");
        return false;
    }
    auto* World = Get_ReadyWorld();
    auto* Instance = GetGameInstance();
    if (GEngine == nullptr || !IsValid(World) || !IsValid(Instance)) { return false; }
    const auto* Context = Instance->GetWorldContext();
    auto* Driver = World->GetNetDriver();
    auto* ProofState = Get_ProofState();
    if (Context == nullptr || Context->OwningGameInstance != Instance || Context->World() != World ||
        Context->PendingNetGame != nullptr || !Context->TravelURL.IsEmpty() || !World->NextURL.IsEmpty() ||
        !IsValid(Driver) || !IsValid(ProofState) || !ProofState->SessionMarker.IsValid())
    {
        _FailureIsolationReadback = TEXT("Not run: current session or travel context unavailable.");
        return false;
    }
    const auto WorldKey = FObjectKey{World};
    const auto DriverKey = FObjectKey{Driver};
    const auto StateBefore = _State;
    const auto ErrorBefore = _LastError;
    const auto BridgeBefore = _BridgeReadback;
    const auto BindingBefore = _BridgeBinding;
    const auto DeadlineBefore = _Deadline;
    const auto SessionBefore = ProofState->SessionMarker;
    const auto ProofBefore = ProofState->ProofSequence;
    const auto TestGuard = TGuardValue<bool>{_FailureIsolationTestActive, true};
    // Exercise the subscribed observer without creating a real connection loss or driver.
    GEngine->OnNetworkFailure().Broadcast(World, nullptr, ENetworkFailure::ConnectionLost,
        TEXT("Gym defensive observer input: unrelated null-driver ConnectionLost"));
    auto* CurrentWorld = Get_ReadyWorld();
    auto* CurrentProof = Get_ProofState();
    const auto* CurrentContext = IsValid(Instance) ? Instance->GetWorldContext() : nullptr;
    auto* CurrentDirect = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>() : nullptr;
    const bool Unchanged = !_Deinitialized && _BridgeActive && _State == StateBefore && _LastError == ErrorBefore &&
        _BridgeReadback == BridgeBefore && _Deadline == DeadlineBefore &&
        !_ReturnPending && !_FailureReturnPending && IsValid(CurrentWorld) &&
        FObjectKey{CurrentWorld} == WorldKey && IsValid(CurrentWorld->GetNetDriver()) &&
        FObjectKey{CurrentWorld->GetNetDriver()} == DriverKey && CurrentContext != nullptr &&
        CurrentContext->OwningGameInstance == Instance && CurrentContext->World() == CurrentWorld &&
        CurrentContext->PendingNetGame == nullptr && CurrentContext->TravelURL.IsEmpty() &&
        CurrentWorld->NextURL.IsEmpty() && IsValid(CurrentProof) &&
        CurrentProof->SessionMarker == SessionBefore && CurrentProof->ProofSequence == ProofBefore &&
        IsValid(CurrentDirect) && BindingBefore.Matches(CurrentDirect->Get_Snapshot()) &&
        Get_BridgeStillValid();
    if (!Unchanged)
    {
        _FailureIsolationReadback = TEXT("CHANGED: malformed event altered the session; inspect recovery.");
        return false;
    }
    if (_FailureIsolationObservations < MAX_int32) { ++_FailureIsolationObservations; }
    _FailureIsolationReadback = FString::Printf(
        TEXT("OBSERVED %d: malformed event ignored; same gameplay session and proof."), _FailureIsolationObservations);
    return true;
}

auto UCkOnlineDirectIpTest_Subsystem::Request_TestQueuedLobbyTravel() -> bool
{
    if (!IsInGameThread() || _Deinitialized || _QueuedTravelTestActive || _QueuedTravelTestPending) { return false; }
    ck::test::ip::FLobbyBinding Binding;
    if (_BridgeActive || _TestControlFaultQueued || _ReturnPending || _FailureReturnPending ||
        !Get_IsOffline() || !TryCaptureOwnedLobby(false, Binding))
    {
        _QueuedTravelIsolationReadback = TEXT("Not run: requires an idle, joined, two-member ready Lobby.");
        return false;
    }
    auto* World = Get_ReadyWorld();
    auto* Instance = GetGameInstance();
    if (GEngine == nullptr || !IsValid(World) || !IsValid(Instance)) { return false; }
    const auto* Context = Instance->GetWorldContext();
    auto* Gym = Instance->GetSubsystem<UCkOnlineDirectGym_Subsystem>();
    auto* Direct = Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>();
    const auto* Mode = World->GetAuthGameMode();
    if (Context == nullptr || GEngine->GetWorldContextFromWorld(World) != Context ||
        Context->OwningGameInstance != Instance || Context->World() != World ||
        Context->PendingNetGame != nullptr || !Context->TravelURL.IsEmpty() || !World->NextURL.IsEmpty() ||
        !IsValid(Gym) || !IsValid(Direct) || !IsValid(Mode) || Mode->GetClass()->GetPathName() != LobbyGameModePath)
    {
        _QueuedTravelIsolationReadback = TEXT("Not run: exact idle Lobby travel context unavailable.");
        return false;
    }
    const auto ReturnMap = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
    if (ReturnMap.IsEmpty()) { return false; }
    const auto ReloadUrl = FString::Printf(TEXT("%s?%s"), *ReturnMap, LobbyGameOption);
    FString Endpoint;
    if (!TryParseLobbyEndpoint(FString::Printf(TEXT("%s:%d"), *Binding.Get_HostAddress(),
        Gym->Get_GameplayPort()), Binding, Endpoint)) { return false; }
    const auto SnapshotBefore = Direct->Get_Snapshot();
    const auto CompletedBefore = Direct->Get_CompletedCount();
    const auto BridgeBefore = _BridgeReadback;
    const auto BridgeEpochBefore = _BridgeBinding.Get_Epoch();
    const auto LastBridgeEpochBefore = _BridgeLastBinding.Get_Epoch();
    const auto EndpointBefore = _Endpoint;
    const auto ReturnMapBefore = _ReturnMap;
    const auto ReturnToLobbyBefore = _ReturnToLobby;
    const auto HasJoinEndpointBefore = _HasJoinEndpoint;
    const auto DeadlineBefore = _Deadline;
    const auto TestGuard = TGuardValue<bool>{_QueuedTravelTestActive, true};
    // Queue a genuine local reload, then exercise competing entry before the engine consumes it.
    GEngine->SetClientTravel(World, *ReloadUrl, TRAVEL_Absolute);
    const bool JoinAdmitted = Request_JoinFromReadyLobby(Endpoint);
    auto* CurrentWorld = Get_ReadyWorld();
    const auto* CurrentContext = IsValid(Instance) ? Instance->GetWorldContext() : nullptr;
    auto* CurrentDirect = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>() : nullptr;
    const bool Preserved = !JoinAdmitted && !_Deinitialized && _State == EState::Offline && !_BridgeActive &&
        !_ReturnPending && !_FailureReturnPending && _BridgeReadback == BridgeBefore &&
        _BridgeBinding.Get_Epoch() == BridgeEpochBefore && _BridgeLastBinding.Get_Epoch() == LastBridgeEpochBefore &&
        _Endpoint == EndpointBefore && _ReturnMap == ReturnMapBefore && _ReturnToLobby == ReturnToLobbyBefore &&
        _HasJoinEndpoint == HasJoinEndpointBefore && _Deadline == DeadlineBefore && !Get_IsOffline() &&
        IsValid(CurrentWorld) && CurrentWorld == World && CurrentWorld->GetNetDriver() == nullptr &&
        CurrentContext != nullptr && CurrentContext->OwningGameInstance == Instance && CurrentContext->World() == World &&
        CurrentContext->PendingNetGame == nullptr && CurrentContext->TravelURL == ReloadUrl &&
        CurrentContext->TravelType == TRAVEL_Absolute && CurrentWorld->NextURL.IsEmpty() &&
        IsValid(CurrentDirect) && Binding.Matches(CurrentDirect->Get_Snapshot()) &&
        CurrentDirect->Get_Snapshot().Get_Revision() == SnapshotBefore.Get_Revision() &&
        CurrentDirect->Get_CompletedCount() == CompletedBefore;
    if (!Preserved)
    {
        _QueuedTravelIsolationReadback = TEXT("CHANGED: competing Join altered queued travel or operational state.");
        return false;
    }
    _QueuedTravelTestDepartureWorld = World;
    _QueuedTravelTestBinding = Binding;
    _QueuedTravelTestMap = ReturnMap;
    _QueuedTravelTestRevision = SnapshotBefore.Get_Revision();
    _QueuedTravelTestCompletedCount = CompletedBefore;
    _QueuedTravelTestDeadline = FPlatformTime::Seconds() + JoinTimeoutSeconds;
    _QueuedTravelTestPending = true;
    _QueuedTravelIsolationReadback = TEXT("Queued destination preserved; awaiting actual local Lobby reload.");
    return true;
}

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
    if (_UsesFoundationOwner)
    {
        auto* Instance = GetGameInstance();
        auto* Owner = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineConnection_Subsystem_UE>() : nullptr;
        if (!IsValid(Owner) || Owner->Get_Snapshot().Get_State() != ECk_OnlineConnection_State::Active ||
            !Owner->Get_Snapshot().Get_Hosting()) { return false; }
    }
    auto* World = Get_ReadyWorld();
    auto* State = Get_ProofState();
    auto* Driver = World != nullptr ? Cast<UIpNetDriver>(World->GetNetDriver()) : nullptr;
    const auto Address = Driver != nullptr ? Driver->GetLocalAddr() : TSharedPtr<const FInternetAddr>{};
    return _State == EState::Hosting && World != nullptr && World->GetNetMode() == NM_ListenServer
        && IsValid(Driver) && Address.IsValid() && Address->GetPort() == _RequestedHostPort
        && IsValid(State) && State->SessionMarker.IsValid() &&
        (!_BridgeActive || (Get_BridgeStillValid() && State->LobbyEpoch == _BridgeBinding.Get_Epoch()));
}

auto UCkOnlineDirectIpTest_Subsystem::Get_IsConnected() const -> bool
{
    if (_UsesFoundationOwner)
    {
        auto* Instance = GetGameInstance();
        auto* Owner = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineConnection_Subsystem_UE>() : nullptr;
        if (!IsValid(Owner) || Owner->Get_Snapshot().Get_State() != ECk_OnlineConnection_State::Active ||
            Owner->Get_Snapshot().Get_Hosting()) { return false; }
    }
    auto* World = Get_ReadyWorld();
    auto* State = Get_ProofState();
    return _State == EState::Connected && World != nullptr && World->GetNetMode() == NM_Client
        && HasOpenClientConnection(World) && IsValid(State) && State->SessionMarker.IsValid() &&
        (!_BridgeActive || (Get_BridgeStillValid() && State->LobbyEpoch == _BridgeBinding.Get_Epoch()));
}

auto UCkOnlineDirectIpTest_Subsystem::Get_IsOffline() const -> bool
{
    if (!IsInGameThread()) { return false; }
    if (_UsesFoundationOwner)
    {
        auto* Instance = GetGameInstance();
        auto* Owner = IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineConnection_Subsystem_UE>() : nullptr;
        return _State == EState::Offline && IsValid(Owner) && Owner->Get_IsIdle();
    }
    auto* World = Get_ReadyWorld();
    if (_State != EState::Offline || World == nullptr || World->GetNetDriver() != nullptr ||
        World->GetNetMode() != NM_Standalone) { return false; }
    return HasIdleOwnedTravelContext(World, GetGameInstance());
}

auto UCkOnlineDirectIpTest_Subsystem::Get_CanRejoin() const -> bool
{
    return _HasJoinEndpoint && !Get_HasLobbyBridgeContext() && Get_IsOffline();
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
