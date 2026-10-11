#pragma once
#include "CoreMinimal.h"
#include "CkVoxelNavGym_Wire.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkVoxelNavGym_AgentFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Index = -1;
    UPROPERTY(BlueprintReadWrite) FTransform Transform;
    UPROPERTY(BlueprintReadWrite) FVector Goal = FVector::ZeroVector;
    UPROPERTY(BlueprintReadWrite) float TargetPitch = 0.0f;
    UPROPERTY(BlueprintReadWrite) int32 MovementState = 0;
    UPROPERTY(BlueprintReadWrite) int32 MoveEpisode = 0;
    UPROPERTY(BlueprintReadWrite) int32 MoveCorrelation = 0;
    UPROPERTY(BlueprintReadWrite) bool GoalReached = false;
    UPROPERTY(BlueprintReadWrite) TArray<FVector> Waypoints;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkVoxelNavGym_StationFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 PairGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 ControlCount = 0;
    UPROPERTY(BlueprintReadWrite) bool Built = false;
    UPROPERTY(BlueprintReadWrite) bool GoalsAreOutbound = true;
    UPROPERTY(BlueprintReadWrite) float BuildProgress = 0.0f;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkVoxelNavGym_WireSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 ControlGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    // 1 retains the last stable pair while a control completes; 2 active; 4 closed.
    UPROPERTY(BlueprintReadWrite) int32 Phase = 0;
    UPROPERTY(BlueprintReadWrite) int32 Replacing = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FCkVoxelNavGym_StationFrame> Stations;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkVoxelNavGym_ControlAck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Serial = 0;
    UPROPERTY(BlueprintReadWrite) int32 Action = -1;
    UPROPERTY(BlueprintReadWrite) int64 RequestedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedSequence = 0;
    UPROPERTY(BlueprintReadWrite) int32 Result = 0;
};
