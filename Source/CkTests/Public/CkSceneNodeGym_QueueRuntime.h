#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_QueueRuntime.generated.h"

// Readback of only the exact identities supplied by a caller. It does not prove
// their completeness, lifetime ownership, closed callbacks, Stop receipts,
// stable visible poses, or permission to destroy any entity.
// Call only on the game thread after ECS has finished its frame, with no
// concurrent registry mutation. The parallel-region check is debug-only and
// cannot establish this boundary in Shipping.
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_QueueRuntime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool
    Get_AreKnownQueuesDrained(
        TArray<FCk_Handle> InKnownEntities, const FCk_Handle& InWorldRoot);
};
