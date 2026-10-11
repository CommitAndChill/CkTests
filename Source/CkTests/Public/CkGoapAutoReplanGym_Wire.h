#pragma once

#include "CoreMinimal.h"
#include "CkGoapAutoReplanGym_Wire.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGoapAutoReplanGym_StationFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Index = -1;
    UPROPERTY(BlueprintReadWrite) int64 Generation = 0;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Detail;
    UPROPERTY(BlueprintReadWrite) FString Footer;
    UPROPERTY(BlueprintReadWrite) int32 Mode = 0;
    UPROPERTY(BlueprintReadWrite) int64 PlanCount = 0;
    UPROPERTY(BlueprintReadWrite) int64 WorldStateChangeCount = 0;
    UPROPERTY(BlueprintReadWrite) int32 PlanStatus = 0;
    UPROPERTY(BlueprintReadWrite) int32 PlanLength = 0;
    UPROPERTY(BlueprintReadWrite) int32 FirstAction = 0;
    UPROPERTY(BlueprintReadWrite) bool Flip = false;
    UPROPERTY(BlueprintReadWrite) bool AltCostLowered = false;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGoapAutoReplanGym_WireSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 ControlGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    // 0 initial, 2 active, 4 terminal failure; manual plans retain source identity.
    UPROPERTY(BlueprintReadWrite) int32 Phase = 0;
    UPROPERTY(BlueprintReadWrite) int32 Replacing = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FCkGoapAutoReplanGym_StationFrame> Stations;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGoapAutoReplanGym_ControlAck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Serial = 0;
    UPROPERTY(BlueprintReadWrite) int32 Action = -1;
    UPROPERTY(BlueprintReadWrite) int64 RequestedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedSequence = 0;
    UPROPERTY(BlueprintReadWrite) int32 Result = 0;
};
