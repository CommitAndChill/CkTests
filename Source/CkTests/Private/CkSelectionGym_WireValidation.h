#pragma once

#include "CoreMinimal.h"

struct FCkSelectionGym_WireSnapshot;

namespace ck_selection_gym_wire
{
    // Pure internal consistency only. The authority census separately proves
    // that the declared bank contains every retained source receipt.
    bool TryValidateActiveValue(const FCkSelectionGym_WireSnapshot& Snapshot,
        FString& OutReason);
}
