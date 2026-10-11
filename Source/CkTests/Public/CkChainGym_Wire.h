#pragma once

#include "CoreMinimal.h"
#include "CkChainGym_Wire.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkChainGym_LinkFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FTransform Pose;
    UPROPERTY(BlueprintReadWrite) FVector TargetLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadWrite) bool HasTargetPose = false;
    UPROPERTY(BlueprintReadWrite) bool IsHeld = false;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkChainGym_StationFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Index = -1;
    UPROPERTY(BlueprintReadWrite) int64 Generation = 0;
    UPROPERTY(BlueprintReadWrite) FVector Origin = FVector::ZeroVector;
    UPROPERTY(BlueprintReadWrite) FTransform Head;
    UPROPERTY(BlueprintReadWrite) TArray<FCkChainGym_LinkFrame> Links;
    UPROPERTY(BlueprintReadWrite) TArray<FTransform> Lamps;
    UPROPERTY(BlueprintReadWrite) TArray<FVector> History;
    UPROPERTY(BlueprintReadWrite) TArray<FVector> SplitHistory;
    UPROPERTY(BlueprintReadWrite) int32 LinkCount = 0;
    UPROPERTY(BlueprintReadWrite) int32 SplitLinkCount = 0;
    UPROPERTY(BlueprintReadWrite) bool HasSplit = false;
    UPROPERTY(BlueprintReadWrite) bool Moving = false;
    UPROPERTY(BlueprintReadWrite) bool FollowingPawn = false;
    UPROPERTY(BlueprintReadWrite) float Spacing = 10.0f;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkChainGym_WireSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 ControlGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    // 0 initial, 2 active, 3 replacing, 4 terminal failure.
    UPROPERTY(BlueprintReadWrite) int32 Phase = 0;
    // -1 all stations or none; 0 a spacing-driven Train replacement.
    UPROPERTY(BlueprintReadWrite) int32 Replacing = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FCkChainGym_StationFrame> Stations;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkChainGym_ControlAck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Serial = 0;
    UPROPERTY(BlueprintReadWrite) int32 Action = -1;
    UPROPERTY(BlueprintReadWrite) int64 RequestedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedSequence = 0;
    UPROPERTY(BlueprintReadWrite) int32 Result = 0;
};
