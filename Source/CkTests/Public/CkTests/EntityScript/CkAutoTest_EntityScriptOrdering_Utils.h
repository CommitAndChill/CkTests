#pragma once

#include "CoreMinimal.h"

#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkEcs/Handle/CkHandle.h"

#include "CkAutoTest_EntityScriptOrdering_Utils.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Test-only reads of an entity script's ordering state, which is C++-only (ck::FFragment_EntityScript_OrderingSequence)
// and has no reflected accessor. Used by the CkEntityScript ordering autotests.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable)
class CKTESTS_API UCk_Utils_AutoTest_EntityScriptOrdering_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // True when the entity carries an ordering sequence (its script class is ordered).
    UFUNCTION(BlueprintPure,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Get Has Ordering Sequence")
    static bool
    Get_HasOrderingSequence(
        const FCk_Handle& InScriptEntity);

    // The entity's ordering stamp: its group (the empty tag is the default group) and its sequence in that group.
    // False, with both outputs left at their defaults, when the entity carries none.
    UFUNCTION(BlueprintPure,
              Category = "Ck|Utils|AutoTest",
              DisplayName = "[Ck][AutoTest] Try Get Ordering")
    static bool
    TryGet_Ordering(
        const FCk_Handle& InScriptEntity,
        FGameplayTag& OutGroup,
        int64& OutSequence);
};

// --------------------------------------------------------------------------------------------------------------------
