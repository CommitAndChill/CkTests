#pragma once

#include "CoreMinimal.h"
#include "CkCore/Macros/CkMacros.h"

#include "CkInventoryMenuProof.generated.h"

/** Persisted fixture inventory. An empty quantity map requests the initial fixture. */
USTRUCT(BlueprintType)
struct CKTESTS_API FCk_UiAuthoringProof_InventoryState
{
    GENERATED_BODY()
    CK_GENERATED_BODY(FCk_UiAuthoringProof_InventoryState);

private:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    TMap<FString, int32> _ItemQuantities;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    FString _EquippedKey;

public:
    CK_PROPERTY(_ItemQuantities);
    CK_PROPERTY(_EquippedKey);
};
