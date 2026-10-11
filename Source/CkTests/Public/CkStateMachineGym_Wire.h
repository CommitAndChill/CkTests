#pragma once

#include "CoreMinimal.h"
#include "CkStateMachineGym_Wire.generated.h"

// Value-only presentation. ECS receipts remain private to the authority world.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkStateMachineGym_StationFrame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int32 Index = INDEX_NONE;
    UPROPERTY(BlueprintReadWrite) int64 Generation = 0;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Detail;
    UPROPERTY(BlueprintReadWrite) FColor Color = FColor::White;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkStateMachineGym_WireSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    // 0 initializing, 2 active, 3 replacing one source, 4 terminal failure.
    UPROPERTY(BlueprintReadWrite) int32 Phase = 0;
    UPROPERTY(BlueprintReadWrite) int32 Replacing = INDEX_NONE;
    UPROPERTY(BlueprintReadWrite) TArray<FCkStateMachineGym_StationFrame> Stations;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkStateMachineGym_ControlAck
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int32 Serial = 0;
    UPROPERTY(BlueprintReadWrite) int32 StationIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadWrite) int64 RequestedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedSequence = 0;
    // 0 none, 1 physically completed replacement, 2 rejected.
    UPROPERTY(BlueprintReadWrite) int32 Result = 0;
};
