#pragma once

#include "CoreMinimal.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkEcs/Handle/CkHandle.h"

#include "CkAutoTest_SceneNodeLatePose_Utils.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Test-only reads of the late-pose pass's C++-only state (its tags, and what its processors did in the last frame the
// scheduler recorded). Used by the CkSceneNode animated-socket-anchor autotests.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable)
class CKTESTS_API UCk_Utils_AutoTest_SceneNodeLatePose_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // True when the entity is a late anchor (ck::FTag_SceneNode_LateAnchor).
    UFUNCTION(BlueprintPure,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Get Has Late Anchor Tag")
    static bool
    Get_HasLateAnchorTag(
        const FCk_Handle& InHandle);

    // True when the entity carries a late write not yet promoted (ck::FTag_Transform_LateUpdated).
    UFUNCTION(BlueprintPure,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Get Has Transform Late Updated Tag")
    static bool
    Get_HasTransformLateUpdatedTag(
        const FCk_Handle& InHandle);

    // True when the entity carries this frame's transform-updated marker (ck::FTag_Transform_Updated).
    UFUNCTION(BlueprintPure,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Get Has Transform Updated Tag")
    static bool
    Get_HasTransformUpdatedTag(
        const FCk_Handle& InHandle);

    // The entity's FFragment_Transform_Previous, the start of the (Previous, Current) pair this frame's consumers read.
    // False, leaving OutPrevious unchanged, when the entity has none.
    UFUNCTION(BlueprintPure,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Try Get Previous Transform")
    static bool
    TryGet_PreviousTransform(
        const FCk_Handle& InHandle,
        FTransform& OutPrevious);

    // What the processor whose name contains InProcessorName did in the main pass of the newest frame the scheduler
    // recorded, in whichever tick-group partition it runs: the entities it visited, and whether the empty-view skip
    // bypassed it. False when no partition of InWorldContext's world has such a processor.
    UFUNCTION(BlueprintCallable,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Get Last Frame Processor Visits",
              meta = (DevelopmentOnly))
    static bool
    Get_LastFrameProcessorVisits(
        const UObject* InWorldContext,
        const FString& InProcessorName,
        int32& OutVisited,
        bool& OutSkippedEmptyView);
};

// --------------------------------------------------------------------------------------------------------------------
