#pragma once

#include "CoreMinimal.h"
#include "CkGoapFEARGym_Wire.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGoapFEARGym_StationFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Index = -1;
    UPROPERTY(BlueprintReadWrite) int64 Generation = 0;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Detail;
    UPROPERTY(BlueprintReadWrite) FString Footer;
    UPROPERTY(BlueprintReadWrite) int32 WorldStateMask = 0;
    UPROPERTY(BlueprintReadWrite) int64 PlanCount = 0;
    UPROPERTY(BlueprintReadWrite) int64 SubPlanCount = 0;
    UPROPERTY(BlueprintReadWrite) int64 ControlCount = 0;
    UPROPERTY(BlueprintReadWrite) int32 PlanStatus = 0;
    UPROPERTY(BlueprintReadWrite) int32 SubPlanStatus = 0;
    UPROPERTY(BlueprintReadWrite) TArray<int32> PlanActions;
    UPROPERTY(BlueprintReadWrite) TArray<int32> SubPlanActions;
    UPROPERTY(BlueprintReadWrite) TArray<int32> ActiveChain;
    UPROPERTY(BlueprintReadWrite) float PlanCost = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SubPlanCost = 0.0f;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGoapFEARGym_WireSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 ControlGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    // 0 initial, 2 active, 4 terminal failure; world-state controls retain source identity.
    UPROPERTY(BlueprintReadWrite) int32 Phase = 0;
    UPROPERTY(BlueprintReadWrite) int32 Replacing = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FCkGoapFEARGym_StationFrame> Stations;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGoapFEARGym_ControlAck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Serial = 0;
    UPROPERTY(BlueprintReadWrite) int32 Action = -1;
    UPROPERTY(BlueprintReadWrite) int64 RequestedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedSequence = 0;
    UPROPERTY(BlueprintReadWrite) int32 Result = 0;
};
