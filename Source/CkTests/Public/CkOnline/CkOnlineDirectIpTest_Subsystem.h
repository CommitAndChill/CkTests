#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Components/ActorComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "CkOnline/CkOnlineDirectIpTest_Bridge.h"

#include "CkOnlineDirectIpTest_Subsystem.generated.h"

class APlayerState;
class UNetDriver;
class UCk_OnlineDirect_Subsystem_UE;

/** Replicated evidence for the transport gym. This marker is not a provider lobby ID. */
UCLASS(BlueprintType, Blueprintable)
class CKTESTS_API ACkOnlineDirectIpTest_GameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    auto GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const -> void override;
    auto AddPlayerState(APlayerState* InPlayerState) -> void override;
    auto RemovePlayerState(APlayerState* InPlayerState) -> void override;
    auto Request_BeginHosting(FGuid InLobbyEpoch = {}) -> bool;
    auto Request_RecordProof(APlayerState* InPlayerState) -> bool;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Ck|Tests|Online Direct IP")
    FGuid SessionMarker;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Ck|Tests|Online Direct IP")
    FGuid LobbyEpoch;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Ck|Tests|Online Direct IP")
    int32 ProofSequence = 0;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Ck|Tests|Online Direct IP")
    int32 LastProofPlayerId = INDEX_NONE;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Ck|Tests|Online Direct IP")
    int32 JoinCount = 0;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Ck|Tests|Online Direct IP")
    int32 LeaveCount = 0;
};

/** Owned by the gym's player controller: client RPC -> host -> replicated GameState. */
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class CKTESTS_API UCkOnlineDirectIpTest_ProofComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCkOnlineDirectIpTest_ProofComponent();

    UFUNCTION(Server, Reliable)
    void Server_RecordProof();
};

/** Stock IP transport fixture only. Does not supply a production CkOnline provider. */
UCLASS(BlueprintType)
class CKTESTS_API UCkOnlineDirectIpTest_Subsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()

public:
    auto Initialize(FSubsystemCollectionBase& InCollection) -> void override;
    auto Deinitialize() -> void override;
    auto Tick(float InDeltaTime) -> void override;
    auto IsTickable() const -> bool override;
    auto IsTickableWhenPaused() const -> bool override { return true; }
    auto GetStatId() const -> TStatId override;
    auto GetTickableGameObjectWorld() const -> UWorld* override;

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_Host(int32 InPort = 7787);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_Join(const FString& InEndpoint);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_HostFromReadyLobby(int32 InPort = 7787);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_JoinFromReadyLobby(const FString& InEndpoint);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_Disconnect();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_Rejoin();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_Proof();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_LogCheckpoint();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_TestControlReadyLoss();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online Direct IP")
    bool Request_TestControlStop();

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    FString Get_StateText() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    FString Get_LobbyBridgeText() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    FString Get_LobbyEpochText() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    bool Get_IsLobbyBridge() const { return _BridgeActive; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    bool Get_HasLobbyBridgeContext() const { return _BridgeActive || _BridgeLastBinding.Epoch.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    bool Get_CanRejoin() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    FString Get_Endpoint() const { return _Endpoint; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    FString Get_SessionMarkerText() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    FString Get_PeerRosterText() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    int32 Get_PeerCount() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    int32 Get_ProofSequence() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    int32 Get_LastProofPlayerId() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    int32 Get_JoinCount() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    int32 Get_LeaveCount() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    FString Get_LastError() const { return _LastError; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    bool Get_IsHosting() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    bool Get_IsConnected() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online Direct IP")
    bool Get_IsOffline() const;

    static auto TryParseEndpoint(const FString& InEndpoint, FString& OutCanonical) -> bool;
    static auto TryParseLobbyEndpoint(const FString& InEndpoint,
        const ck::test::ip::FLobbyBinding& InBinding, FString& OutCanonical) -> bool;

private:
    friend class ACkOnlineDirectIpTest_GameState;
    enum class EState : uint8 { Offline, StartingHost, Hosting, Joining, Connected, Leaving, Failed };
    auto DoRequestHost(int32 InPort, const ck::test::ip::FLobbyBinding* InBinding) -> bool;
    auto DoRequestJoin(const FString& InEndpoint, const ck::test::ip::FLobbyBinding* InBinding) -> bool;
    auto TryCaptureOwnedLobby(bool InHosting, ck::test::ip::FLobbyBinding& Out) const -> bool;
    auto Get_BridgeStillValid() const -> bool;
    auto TryGetTestControlService(UCk_OnlineDirect_Subsystem_UE*& OutService) -> bool;
    auto Get_ReadyWorld() const -> UWorld*;
    auto Get_ProofState() const -> ACkOnlineDirectIpTest_GameState*;
    auto DoReject(const FString& InReason) -> bool;
    auto DoReturnToGym(bool InKeepFailure) -> bool;
    auto OnNetworkFailure(UWorld* InWorld, UNetDriver* InDriver,
        ENetworkFailure::Type InFailure, const FString& InError) -> void;
    auto OnTravelFailure(UWorld* InWorld, ETravelFailure::Type InFailure, const FString& InError) -> void;

    EState _State = EState::Offline;
    FString _Endpoint = TEXT("127.0.0.1:7787");
    FString _ReturnMap;
    FString _LastError;
    FString _BridgeReadback = TEXT("none");
    ck::test::ip::FLobbyBinding _BridgeBinding;
    ck::test::ip::FLobbyBinding _BridgeLastBinding;
    bool _BridgeActive = false;
    bool _TestControlFaultQueued = false;
    bool _ReturnToLobby = false;
    double _Deadline = 0.0;
    int32 _RequestedHostPort = 0;
    bool _HasJoinEndpoint = false;
    bool _Deinitialized = false;
    bool _ReturnPending = false;
    bool _FailureReturnPending = false;
    TWeakObjectPtr<UWorld> _DepartingWorld;
    TWeakObjectPtr<UWorld> _HostingDepartureWorld;
    FDelegateHandle _NetworkFailureHandle;
    FDelegateHandle _TravelFailureHandle;
};
