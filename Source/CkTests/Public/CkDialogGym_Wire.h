#pragma once

#include "CoreMinimal.h"
#include "CkDialogGym_Wire.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkDialogGym_LineResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FName LineID;
    UPROPERTY(BlueprintReadWrite) FString Text;
    UPROPERTY(BlueprintReadWrite) int32 Result = -1;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkDialogGym_StationFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Index = -1;
    UPROPERTY(BlueprintReadWrite) int64 Generation = 0;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Detail;
    UPROPERTY(BlueprintReadWrite) FString Footer;
    UPROPERTY(BlueprintReadWrite) int64 QueryCount = 0;
    UPROPERTY(BlueprintReadWrite) TArray<FCkDialogGym_LineResult> Results;
    UPROPERTY(BlueprintReadWrite) TArray<FCkDialogGym_LineResult> OtherResults;
    UPROPERTY(BlueprintReadWrite) float CooldownRemaining = 0.0f;
    UPROPERTY(BlueprintReadWrite) int32 ChainStep = 0;
    UPROPERTY(BlueprintReadWrite) FName ActiveLineID;
    UPROPERTY(BlueprintReadWrite) TArray<int32> GraphNodeStates;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkDialogGym_WireSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 ControlGeneration = 1;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    // 0 initial, 2 active, 3 replacing, 4 terminal failure.
    UPROPERTY(BlueprintReadWrite) int32 Phase = 0;
    UPROPERTY(BlueprintReadWrite) int32 Replacing = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FCkDialogGym_StationFrame> Stations;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkDialogGym_ControlAck
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 Serial = 0;
    UPROPERTY(BlueprintReadWrite) int32 Action = -1;
    UPROPERTY(BlueprintReadWrite) int64 RequestedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 CompletedSequence = 0;
    UPROPERTY(BlueprintReadWrite) int32 Result = 0;
};
