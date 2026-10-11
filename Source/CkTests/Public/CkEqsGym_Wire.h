#pragma once

#include "CoreMinimal.h"
#include "CkEqsGym_Wire.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkEqsGym_StationFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Index = -1;
    UPROPERTY(BlueprintReadWrite) int64 Generation = 0;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Detail;
    UPROPERTY(BlueprintReadWrite) FString Footer;
    UPROPERTY(BlueprintReadWrite) int64 RunCount = 0;
    UPROPERTY(BlueprintReadWrite) int32 CandidateCount = 0;
    UPROPERTY(BlueprintReadWrite) bool HasResult = false;
    UPROPERTY(BlueprintReadWrite) bool Failed = false;
    UPROPERTY(BlueprintReadWrite) bool IsImmediate = false;
    UPROPERTY(BlueprintReadWrite) FVector BestLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadWrite) float SecondsLeft = 5.0f;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkEqsGym_WireSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 ControlGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    // 0 initial, 2 active, 3 replacing, 4 terminal failure.
    UPROPERTY(BlueprintReadWrite) int32 Phase = 0;
    UPROPERTY(BlueprintReadWrite) int32 Replacing = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FCkEqsGym_StationFrame> Stations;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkEqsGym_ControlAck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Serial = 0;
    UPROPERTY(BlueprintReadWrite) int32 Action = -1;
    UPROPERTY(BlueprintReadWrite) int64 RequestedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedSequence = 0;
    UPROPERTY(BlueprintReadWrite) int32 Result = 0;
};
