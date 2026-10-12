#pragma once

#include "CkGym_NetworkRuntime.h"
#include "CkCrowdGym_WireContract.h"
#include "CkCrowdGym_SharedSimulation.h"
#include "CkCrowd/Debug/CkCrowd_RemoteDebugRegistry.h"

#include <Components/ActorComponent.h>

#include "CkCrowdGym_NetworkRuntime.generated.h"

class UCkCrowdRemoteBodyProjector;
class APlayerController;
class APawn;
class AGameModeBase;
class ULocalPlayer;

// One replicated terminal value; an empty latest snapshot alone is not a retirement signal.
USTRUCT()
struct FCkCrowdGym_WorldTerminal
{
    GENERATED_BODY()
    UPROPERTY() int64 WorldGeneration = 0;
    UPROPERTY() bool bRetired = false;
};

UCLASS(BlueprintType, Blueprintable)
class CKTESTS_API ACkCrowdGym_GameState_UE : public ACkGym_GameState_UE,
    public ICkCrowdRemoteDebugProvider
{
    GENERATED_BODY()
public:
    ACkCrowdGym_GameState_UE();

    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd")
    bool TryInitializeShared(const FCk_Handle& Station, const FTransform& FootprintCenter,
        TSubclassOf<APlayerController> ExpectedPCClass, TSubclassOf<APawn> ExpectedPawnClass);
    UFUNCTION(BlueprintPure, Category="Ck|Gym|Crowd")
    bool Get_HasCompleteSnapshot() const;
    UFUNCTION(BlueprintPure, Category="Ck|Gym|Crowd")
    FCkCrowdGym_RowsSnapshot Get_RowsSnapshot() const { return AcceptedSnapshot; }
    UFUNCTION(BlueprintPure, Category="Ck|Gym|Crowd")
    bool Get_HasReadyServerSimulation() const;
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd")
    bool TryBindLocalController(APlayerController* LocalPC);
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd")
    void Request_RetireShared();
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd")
    void Request_CloseFailedShared();

    // Synchronous authority admission-close handoff: the AS owner closes its exact Common world.
    UFUNCTION(BlueprintImplementableEvent, Category="Ck|Gym|Crowd")
    bool OnAuthorityTerminalFailure(bool bFailure);

    bool IsForWorld(const UWorld& World) const override;
    bool CopyRows(const UWorld& World, const APlayerController& PC,
        const ULocalPlayer& LocalPlayer, TArray<FCkCrowdRemoteAgentRow>& Out) const override;
    bool CopySelectedDetail(const UWorld& World, const APlayerController& PC,
        const ULocalPlayer& LocalPlayer, const FCkCrowdRemoteAgentId& Id,
        int64 SelectionSerial, FCkCrowdRemoteAgentDetail& Out) const override;
    bool RequestSelectionFromOwningLocalController(const UWorld& World,
        APlayerController& PC, const ULocalPlayer& LocalPlayer,
        const FCkCrowdRemoteAgentId& Id, int64 SelectionSerial, bool bClear) override;
    bool CopySelectionEnvelope(const UWorld& World, const APlayerController& PC,
        const ULocalPlayer& LocalPlayer, FCkCrowdRemoteSelectionEnvelope& Out) const override;
    bool AcknowledgeSelectionFromOwningLocalController(const UWorld& World,
        APlayerController& PC, const ULocalPlayer& LocalPlayer,
        const FCkCrowdRemoteSelectionReceipt& Receipt) override;
    bool RequestNextSelectionFromOwningLocalController(const UWorld& World,
        APlayerController& PC, const ULocalPlayer& LocalPlayer,
        const FCkCrowdRemoteAgentId& Id, bool bClear, int64& OutIssuedSerial) override;
    bool SubmitFromOwningLocalController(const UWorld& World, APlayerController& PC,
        const ULocalPlayer& LocalPlayer, const FCkCrowdRemoteCommandRequest& Request) override;
    bool CopyCommandResult(const UWorld& World, const APlayerController& PC,
        const ULocalPlayer& LocalPlayer, int64 ClientSerial,
        FCkCrowdRemoteCommandResult& Out) const override;

    bool TrySubmitControl(class UCkCrowdGym_Session_UE& Session, int64 Serial, int32 Row);
    bool TrySubmitSelection(class UCkCrowdGym_Session_UE& Session, int64 Serial,
        const FCkCrowdRemoteAgentId& Id, bool bClear);
    bool HasServerRequestContext(const class UCkCrowdGym_Session_UE& Session) const;
    bool HasServerTransportContext(const class UCkCrowdGym_Session_UE& Session) const;
    bool HasLocalTransportContext(const class UCkCrowdGym_Session_UE& Session) const;
    bool HasLocalRequestContext(const class UCkCrowdGym_Session_UE& Session) const;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

private:
    friend class UCkCrowdGym_Session_UE;
    UFUNCTION() void OnRep_Snapshot();
    UFUNCTION() void OnRep_WorldTerminal();
    bool HasExactAuthorityContext() const;
    bool HasExactSession(const class UCkCrowdGym_Session_UE& Session) const;
    bool HasLocalContext(const UWorld& World, const APlayerController& PC,
        const ULocalPlayer& LocalPlayer) const;
    bool TryPublishEpoch();
    bool TryBuildEpoch(FCkCrowdGym_RowsSnapshot& OutSnapshot,
        TMap<TWeakObjectPtr<class UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail>& OutDetails,
        TArray<TWeakObjectPtr<class UCkCrowdGym_Session_UE>>& OutWithdrawals,
        class UCkCrowdGym_Session_UE* OverrideSession = nullptr,
        const FCkCrowdRemoteAgentId* OverrideId = nullptr, int64 OverrideSerial = 0);
    void PublishPreparedEpoch(FCkCrowdGym_RowsSnapshot&& Candidate,
        TMap<TWeakObjectPtr<class UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail>&& CandidateDetails,
        const TArray<TWeakObjectPtr<class UCkCrowdGym_Session_UE>>& Withdrawals);
    bool HasFrozenCatalog() const;
    bool HasFrozenStation() const;
    bool TryRegisterProvider();
    void FailAndRetire();
    void FinishTerminalPresentation();

    UPROPERTY(VisibleAnywhere) TObjectPtr<UCkCrowdGym_SharedSimulation> SharedSimulation;
    UPROPERTY(Transient) TObjectPtr<UCkCrowdRemoteBodyProjector> Projector;
    UPROPERTY(ReplicatedUsing=OnRep_Snapshot) FCkCrowdGym_RowsSnapshot Snapshot;
    UPROPERTY(ReplicatedUsing=OnRep_WorldTerminal) FCkCrowdGym_WorldTerminal WorldTerminal;
    FCkCrowdGym_RowsSnapshot AcceptedSnapshot;
    FCkCrowdGym_RowsSnapshot ProjectedSnapshot;
    TWeakObjectPtr<APlayerController> LocalController;
    TWeakObjectPtr<class UCkCrowdGym_Session_UE> PendingOwner;
    TWeakObjectPtr<APlayerController> PendingController;
    TWeakObjectPtr<APawn> PendingPawn;
    UPROPERTY(Replicated) TSubclassOf<APlayerController> PCClass;
    UPROPERTY(Replicated) TSubclassOf<APawn> PawnClass;
    UPROPERTY(Replicated) TSubclassOf<AGameModeBase> CatalogModeClass;
    UPROPERTY(Replicated) int32 CatalogGymIndex = INDEX_NONE;
    UPROPERTY(Replicated) TArray<FCkGym_Entry> FrozenCatalog;
    UPROPERTY(Replicated) FTransform FrozenFootprint;
    FCk_Handle FrozenStation;
    FCk_Handle FrozenStationOwner;
    FTransform FrozenStationTransform;
    FCkCrowdRemoteProviderToken ProviderToken;
    TMap<TWeakObjectPtr<class UCkCrowdGym_Session_UE>, FCkCrowdRemoteAgentDetail> SampledDetails;
    int64 NextSampleSequence = 1;
    int64 ProjectorGeneration = 0;
    int64 ProjectorSequence = 0;
    int64 PendingBatchSerial = 0;
    int64 PendingRequestSerial = 0;
    int64 PendingSessionGeneration = 0;
    int32 PendingRow = 0;
    double NextSampleAtSeconds = 0.0;
    double FirstSampleFailureSeconds = 0.0;
    bool bInitialized = false;
    bool bHasHadReadySimulation = false;
    bool bTerminal = false;
    bool bAuthorityAdmissionClosed = false;
};

UCLASS(BlueprintType, ClassGroup=(Ck), meta=(BlueprintSpawnableComponent))
class CKTESTS_API UCkCrowdGym_Session_UE : public UActorComponent
{
    GENERATED_BODY()
public:
    UCkCrowdGym_Session_UE();

    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd") bool TryQueueControl(int32 Row);
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd")
    bool TryQueueSelection(const FCkCrowdRemoteAgentId& Id, int64 Serial, bool bClear);
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd")
    bool TryQueueNextSelection(const FCkCrowdRemoteAgentId& Id, bool bClear, int64& OutIssuedSerial);
    UFUNCTION(BlueprintPure, Category="Ck|Gym|Crowd")
    bool TryCopySelectionEnvelope(FCkCrowdRemoteSelectionEnvelope& Out) const;
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd")
    bool TryAcknowledgeSelection(const FCkCrowdRemoteSelectionReceipt& Receipt);
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd") bool TryAcknowledgeCommit(int64 DeliverySerial);
    UFUNCTION(BlueprintCallable, Category="Ck|Gym|Crowd") bool TryTeardownObsoletePawnSession();
    UFUNCTION(BlueprintPure, Category="Ck|Gym|Crowd") bool TryCopyRetainedCommit(FCkCrowdGym_OwnerCommit& Out) const;
    bool TryCopyLatestView(FCkCrowdGym_RowsSnapshot& OutRows,
        FCkCrowdRemoteAgentId& OutId, int64& OutSelectionSerial,
        FCkCrowdRemoteAgentDetail& OutDetail, bool& bOutHasDetail) const;
    UFUNCTION(BlueprintPure, Category="Ck|Gym|Crowd") int64 Get_ControlAcceptedWatermark() const { return ControlAccepted; }
    UFUNCTION(BlueprintPure, Category="Ck|Gym|Crowd") int64 Get_SelectionAcceptedWatermark() const { return SelectionAccepted; }

    UFUNCTION(Server, Reliable) void Server_RequestControl(int64 Serial, int32 Row, int64 Generation,
        int64 SessionGeneration);
    UFUNCTION(Server, Reliable) void Server_RequestSelection(int64 Serial,
        FCkCrowdRemoteAgentId Id, bool bClear, int64 Generation, int64 SessionGeneration);
    UFUNCTION(Server, Reliable) void Server_AcknowledgeCommit(int64 Generation, int64 DeliverySerial,
        int64 SessionGeneration);
    UFUNCTION(Server, Reliable) void Server_TeardownSession(int64 Generation, int64 SessionGeneration);
    UFUNCTION(Client, Reliable) void Client_SessionTeardownComplete(int64 Generation,
        int64 PreviousSessionGeneration, int64 NewSessionGeneration);
    UFUNCTION(Client, Reliable) void Client_ReceiveCommit(const FCkCrowdGym_OwnerCommit& Commit,
        int64 SessionGeneration);
    UFUNCTION(Client, Unreliable) void Client_ReceiveView(
        const FCkCrowdGym_RowsSnapshot& InSnapshot,
        FCkCrowdRemoteAgentId InSelectedId, int64 InSelectionSerial,
        bool bInHasDetail, const FCkCrowdRemoteAgentDetail& InDetail, int64 SessionGeneration);

    bool HasServerRetained() const { return bServerRetained; }
    int64 Get_TransportSessionGeneration() const { return TransportSessionGeneration; }

    // Pure, per-instance local send diagnostics. Saturation invalidates observations; it never gates RPCs.
    int64 Get_LocalSelectionAttemptWatermark() const { return LocalSelectionAttemptHigh; }
    int64 Get_NextLocalControlSerial() const { return NextControlSerial; }
    int64 Get_LocalAcknowledgeSendCount() const { return LocalAcknowledgeSendCount; }
    int64 Get_LocalTeardownSendCount() const { return LocalTeardownSendCount; }
    bool Get_LocalSendDiagnosticsValid() const { return bLocalSendDiagnosticsValid; }

private:
    friend class ACkCrowdGym_GameState_UE;
    void RecordLocalSend(int64& Count)
    {
        if (Count < TNumericLimits<int64>::Max()) { ++Count; }
        else { bLocalSendDiagnosticsValid = false; }
    }
    int64 LocalAcknowledgeSendCount = 0;
    int64 LocalTeardownSendCount = 0;
    bool bLocalSendDiagnosticsValid = true;
    bool CanAdmit(int64 Serial, ECkCrowdGym_RequestKind Kind) const;
    void AdmitAttempt(int64 Serial, ECkCrowdGym_RequestKind Kind);
    bool DeliverAccepted(int64 Serial, ECkCrowdGym_RequestKind Kind, int32 Row,
        const FCkCrowdGym_RowsSnapshot& InSnapshot,
        const FCkCrowdRemoteAgentId& SelectedId,
        int64 SelectionSerial, const FCkCrowdRemoteAgentDetail* Detail);
    bool TryPrepareAccepted(int64 Serial, ECkCrowdGym_RequestKind Kind,
        const FCkCrowdGym_RowsSnapshot& InSnapshot,
        const FCkCrowdRemoteAgentId& SelectedId, int64 SelectionSerial,
        const FCkCrowdRemoteAgentDetail* Detail, FCkCrowdGym_OwnerCommit& Out) const;
    bool TryRetainPreparedAccepted(const FCkCrowdGym_OwnerCommit& Candidate);
    void SendRetainedCommit();
    void RequestTerminalGone(int64 Serial, ECkCrowdGym_RequestKind Kind);
    void RetireForWorld(const ACkCrowdGym_GameState_UE& State, int64 Generation);
    void WithdrawSelection();
    void SetSelection(const FCkCrowdRemoteAgentId& Id, int64 Serial);
    const FCkCrowdRemoteAgentId& Get_SelectedId() const { return ServerSelectedId; }
    int64 Get_SelectionSerial() const { return ServerSelectionSerial; }
    bool IsServerPending(int64 Serial, ECkCrowdGym_RequestKind Kind) const;
    bool HasFreshAttempt(int64 Serial, ECkCrowdGym_RequestKind Kind) const;
    bool DeliverRejected(int64 Serial, ECkCrowdGym_RequestKind Kind,
        ECkCrowdGym_RequestOutcome Outcome);
    void ClearServerPending();
    void MarkServerPending(int64 Serial, ECkCrowdGym_RequestKind Kind);

private:
    ACkCrowdGym_GameState_UE* GetCrowdState() const;
    bool HasExactLocalOwner() const;
    bool HasExactServerOwner() const;
    bool HasLocalTransportOwner() const;
    bool HasServerTransportOwner() const;
    bool TryInstallView(const FCkCrowdGym_RowsSnapshot& InSnapshot,
        const FCkCrowdRemoteAgentId& InId, int64 InSelectionSerial,
        bool bInHasDetail, const FCkCrowdRemoteAgentDetail& InDetail);
    void EndPlay(const EEndPlayReason::Type Reason) override;

    int64 NextControlSerial = 1;
    int64 LocalSelectionAttemptHigh = 0;
    int64 LocalSelectionViewHigh = 0;
    int64 LocalDeliveryHigh = 0;
    int64 LocalPendingSerial = 0;
    int64 LocalPendingGeneration = 0;
    int64 TransportSessionGeneration = 1;
    int64 LocalTeardownWorldGeneration = 0;
    int64 LocalTeardownSessionGeneration = 0;
    bool bSessionTeardownPending = false;
    TWeakObjectPtr<APawn> LocalPendingPawn;
    TWeakObjectPtr<APawn> LocalRetainedPawn;
    TWeakObjectPtr<APawn> ServerRetainedPawn;
    TWeakObjectPtr<APlayerController> ServerRetainedController;
    ECkCrowdGym_RequestKind LocalPendingKind = ECkCrowdGym_RequestKind::Control;
    int64 ControlAttemptHigh = 0;
    int64 SelectionAttemptHigh = 0;
    int64 ControlAccepted = 0;
    int64 SelectionAccepted = 0;
    int64 NextDeliverySerial = 1;
    int64 ServerPendingSerial = 0;
    TWeakObjectPtr<APlayerController> ServerPendingController;
    TWeakObjectPtr<APawn> ServerPendingPawn;
    ECkCrowdGym_RequestKind ServerPendingKind = ECkCrowdGym_RequestKind::Control;
    FCkCrowdRemoteAgentId ServerSelectedId;
    int64 ServerSelectionSerial = 0;
    FCkCrowdGym_OwnerCommit ServerRetained;
    FCkCrowdGym_OwnerCommit LocalRetained;
    FCkCrowdGym_RowsSnapshot LatestViewRows;
    FCkCrowdRemoteAgentId LatestViewId;
    int64 LatestViewSelectionSerial = 0;
    FCkCrowdRemoteAgentDetail LatestViewDetail;
    bool bLatestViewHasDetail = false;
    bool bHasLatestView = false;
    bool bServerRetained = false;
    bool bLocalRetained = false;
};
