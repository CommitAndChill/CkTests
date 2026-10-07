#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkRotateTowardsGym_Runtime.generated.h"

UCLASS()
class CKTESTS_API UCk_RotateTowardsGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Pure client readback. A live entity in this exact world must have no
    // Rotate or Tween fragments/tags, including partially assembled roles.
    // This grants no ownership, mutation or cleanup permission.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool Get_IsPassiveMotionEntity(const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot);

    // Pure allocated-inclusive role check for a frozen retirement ledger.
    // Accepts an allocated BeginDestroy/Teardown member, but never grants
    // ownership or permission to mutate or destroy that member.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool Get_IsPassiveAllocatedMotionEntity(const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot);
};
