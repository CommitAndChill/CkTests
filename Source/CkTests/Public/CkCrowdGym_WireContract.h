#pragma once

#include "CkCrowd/Debug/CkCrowd_RemoteDebugContract.h"

#include "CkCrowdGym_WireContract.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkCrowdGym_RowsSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int64 WorldGeneration = 0;
    UPROPERTY(BlueprintReadOnly) int64 SampleSequence = 0;
    UPROPERTY(BlueprintReadOnly) bool bReady = false;
    UPROPERTY(BlueprintReadOnly) TArray<FCkCrowdRemoteAgentRow> Rows;
};

UENUM(BlueprintType)
enum class ECkCrowdGym_RequestKind : uint8 { Control, Selection, Command };

UENUM(BlueprintType)
enum class ECkCrowdGym_RequestOutcome : uint8
{
    Accepted, Invalid, Stale, Busy, Unauthorized, Gone, Failed
};

// One frozen owner result, retained by the authority until explicit local consumer acknowledgment.
// Its rows travel with detail/result so a skipped latest GameState property cannot strand a commit.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkCrowdGym_OwnerCommit
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int64 DeliverySerial = 0;
    UPROPERTY(BlueprintReadOnly) int64 RequestSerial = 0;
    UPROPERTY(BlueprintReadOnly) ECkCrowdGym_RequestKind Kind = ECkCrowdGym_RequestKind::Control;
    UPROPERTY(BlueprintReadOnly) ECkCrowdGym_RequestOutcome Outcome = ECkCrowdGym_RequestOutcome::Failed;
    UPROPERTY(BlueprintReadOnly) FCkCrowdGym_RowsSnapshot Snapshot;
    UPROPERTY(BlueprintReadOnly) int64 SelectionRequestSerial = 0;
    UPROPERTY(BlueprintReadOnly) FCkCrowdRemoteAgentId SelectedId;
    UPROPERTY(BlueprintReadOnly) bool bHasDetail = false;
    UPROPERTY(BlueprintReadOnly) FCkCrowdRemoteAgentDetail Detail;
    UPROPERTY(BlueprintReadOnly) bool bHasCommandResult = false;
    UPROPERTY(BlueprintReadOnly) FCkCrowdRemoteCommandResult CommandResult;
};

// Total, pure validation before publishing a complete wire value. No ECS or provider dispatch.
class CKTESTS_API FCkCrowdGymWireValidation
{
public:
    static auto IsValidReadySnapshot(const FCkCrowdGym_RowsSnapshot& Snapshot) -> bool;
    static auto IsValidCommit(const FCkCrowdGym_OwnerCommit& Commit) -> bool;
};
