#pragma once

#include "CoreMinimal.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkAutoTest_SceneNodeDepth_Utils.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Test-only reads for the CkSceneNode depth autotests: the scheduler's own per-frame wall-clock record, which the
// layer-cost readout compares between builds with different layer counts.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable)
class CKTESTS_API UCk_Utils_AutoTest_SceneNodeDepth_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // For every frame the scheduler recorded with a number in (InAfterFrame, InUpToFrame], in frame order: its whole
    // tick in ms (main pass, local settle and pump), summed over every tick-group partition of InWorldContext's world.
    // The history holds the last 300 frames. Reading it switches the scheduler's per-processor clock reads on for the
    // next frames (its debug-timing keep-alive), so a caller that wants frames measured without them starts its next
    // window past that keep-alive.
    UFUNCTION(BlueprintCallable,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Get Scheduler Frame Times (ms)",
              meta = (DevelopmentOnly))
    static TArray<double>
    Get_SchedulerFrameTimesMs(
        const UObject* InWorldContext,
        int64 InAfterFrame,
        int64 InUpToFrame);
};

// --------------------------------------------------------------------------------------------------------------------
