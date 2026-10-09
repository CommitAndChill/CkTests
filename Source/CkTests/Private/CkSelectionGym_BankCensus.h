#pragma once

#include "CkEcs/Handle/CkHandle.h"

class UWorld;

namespace ck_selection_gym_census
{
    // Returns every allocated entity in the exact retained lifetime forest, roots included.
    // No type filtering or state mutation. False leaves handles empty and supplies a reason.
    auto TryCensusAllocatedBank(
        UWorld* World,
        const FCk_Handle& ScenarioRoot,
        const TArray<FCk_Handle>& DirectWorldTransientRoots,
        TArray<FCk_Handle>& OutHandles,
        FString& OutReason) -> bool;

    // Positive opaque key for a live entity in the exact current world registry.
    // The complete EnTT ID contains both entity number and version.
    auto TryGetOpaqueEntityKey(
        const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot,
        int64& OutKey) -> bool;
}
