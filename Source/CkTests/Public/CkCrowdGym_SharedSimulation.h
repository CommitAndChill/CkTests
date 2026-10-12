#pragma once

#include "CkCrowd/Agent/CkCrowdAgent_Fragment_Data.h"
#include "CkCrowd/Debug/CkCrowd_RemoteDebugContract.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"

#include <Components/ActorComponent.h>

#include "CkCrowdGym_SharedSimulation.generated.h"

// Authority-only roster. The owning GameState, rather than a player session, owns every ECS entity.
UCLASS(ClassGroup = (Ck), meta = (BlueprintSpawnableComponent))
class CKTESTS_API UCkCrowdGym_SharedSimulation : public UActorComponent
{
    GENERATED_BODY()

public:
    UCkCrowdGym_SharedSimulation();

    // StationOrigin is the authored FootprintCenter anchor transform. Its world location is raised
    // 100 cm, as in the original Separation gym. Only the server GameState may initialize once.
    UFUNCTION(BlueprintCallable, Category = "Ck|Gym|Crowd")
    bool TryInitialize(const FTransform& StationOrigin);

    // 0=N/S, 1=E/W, 2=all four cardinals, 3=five-agent converging cluster.
    // Returning true admits a pending operation; Get_LastCompletedBatchSerial proves its commit.
    UFUNCTION(BlueprintCallable, Category = "Ck|Gym|Crowd")
    bool BeginAppendScenario(int32 Scenario, int64& OutBatchSerial);

    UFUNCTION(BlueprintCallable, Category = "Ck|Gym|Crowd")
    bool BeginClear(int64& OutBatchSerial);

    UFUNCTION(BlueprintPure, Category = "Ck|Gym|Crowd")
    bool Get_IsReady() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Gym|Crowd")
    bool Get_IsBusy() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Gym|Crowd")
    bool Get_HasFailed() const { return bFailed; }

    // C++ owner terminalization. Retains all creator identities; grants no root cleanup.
    void Request_CloseUnproven();
    bool Request_RetireHealthyRoot();

    UFUNCTION(BlueprintPure, Category = "Ck|Gym|Crowd")
    int64 Get_WorldGeneration() const { return WorldGeneration; }

    UFUNCTION(BlueprintPure, Category = "Ck|Gym|Crowd")
    int64 Get_LastCompletedBatchSerial() const { return LastCompletedBatchSerial; }

    UFUNCTION(BlueprintPure, Category = "Ck|Gym|Crowd")
    FString Get_FailureReason() const { return FailureReason; }

    // Native provider path. The director assigns InSequence, gathers world rows and all requested
    // owner details synchronously in one game-thread pass, then caches/publishes that epoch.
    // Reusing a number in a later asynchronous call does not establish coherent data.
    // A pending append or clear withholds sampling until the complete operation commits.
    auto TrySampleRosterAtSequence(int64 InSequence,
                         const FCk_Handle& RequestingPawnEntity,
                         const FCkCrowdRemoteAgentId* SelectedId,
                         int64 SelectionRequestSerial,
                         TArray<FCkCrowdRemoteAgentRow>& OutRows,
                         FCkCrowdRemoteAgentDetail& OutDetail) const -> bool;

    // Command integration must still validate its PC/pawn, serial, revision and action first.
    auto TryResolveAgent(const FCkCrowdRemoteAgentId& Id,
                         int64 ExpectedRevision,
                         FCk_Handle_CrowdAgent& OutAgent) const -> bool;

    // Called only after an authoritative command has completed its production mutation.
    // The caller must publish its next complete sampled state before acknowledging that command.
    auto TryAdvanceConfigurationRevision(const FCkCrowdRemoteAgentId& Id,
                                         int64 ExpectedRevision,
                                         int64& OutNewRevision) -> bool;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    struct FSpawnSpec
    {
        FVector Spawn = FVector::ZeroVector;
        FVector Target = FVector::ZeroVector;
        FLinearColor Color = FLinearColor::White;
    };

    struct FAgent
    {
        FCkCrowdRemoteAgentId Id;
        FCk_Handle_CrowdAgent Handle;
        FCk_Handle Raw;
        FCk_Handle BatchRoot;
        FLinearColor Color = FLinearColor::White;
        int64 Revision = 0;
        bool bMoveCompleted = false;
        bool bStopIssued = false;
        bool bStopCallbackSeen = false;
        bool bStopSucceeded = false;
    };

    enum class EPending : uint8 { None, Append, Clear };

    UFUNCTION()
    void OnMoveCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult);

    UFUNCTION()
    void OnKnownStopCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult);

    auto HasAuthorityWorld() const -> bool;
    auto HasLiveRoot() const -> bool;
    auto HasExactAgent(const FAgent& Agent) const -> bool;
    auto HasExactRoster() const -> bool;
    auto HasExactProbeRoster() const -> bool;
    auto BuildScenario(int32 Scenario, TArray<FSpawnSpec>& OutSpecs) const -> bool;
    auto TryCreateAgent(const FSpawnSpec& Spec, int64 Serial, FAgent& OutAgent) -> bool;
    void AdvancePending();
    void FailAndRetire(const TCHAR* Reason);
    void StopKnownMotion();
    bool HasExactStructuralRoster() const;

    TWeakObjectPtr<UWorld> OwningWorld;
    FCk_Handle WorldRoot;
    FCk_Handle PendingBatchRoot;
    TArray<FCk_Handle> CommittedBatchRoots;
    TArray<FCk_Handle> RetiringBatchRoots;
    TArray<FAgent> Admitted;
    TArray<FAgent> PendingAgents;
    FVector Origin = FVector::ZeroVector;
    EPending Pending = EPending::None;
    int64 WorldGeneration = 0;
    int64 NextAgentSerial = 1;
    int64 NextBatchSerial = 1;
    int64 LastCompletedBatchSerial = 0;
    int64 PendingBatchSerial = 0;
    double PendingSinceSeconds = 0.0;
    FString FailureReason;
    bool bFailed = false;
    bool bHealthyRetirementUnproven = false;
    bool bMoveCallbackFailed = false;
    bool bStoppingKnownMotion = false;
};
