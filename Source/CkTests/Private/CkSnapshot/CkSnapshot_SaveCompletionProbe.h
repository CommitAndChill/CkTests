#pragma once

#include "CoreMinimal.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Delegates.h"

#include "CkSnapshot_SaveCompletionProbe.generated.h"

// Test-only dynamic-delegate target. A fresh recorder is cleared before every save attempt.
UCLASS()
class UCkSnapshot_SaveCompletionProbe : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void
    OnSaveComplete(ECk_SnapshotResult InResult);

    auto Clear() -> void
    {
        CallbackCount = 0;
        Result = ECk_SnapshotResult::Failed_IO;
    }

    auto HasFreshSuccess() const -> bool
    { return CallbackCount == 1 && Result == ECk_SnapshotResult::Success; }

    int32 CallbackCount = 0;
    ECk_SnapshotResult Result = ECk_SnapshotResult::Failed_IO;
};
