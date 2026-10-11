#pragma once

#include "CoreMinimal.h"
#include "CkChain/CkChain_Fragment_Data.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcsExt/Transform/CkTransform_Fragment_Data.h"
#include "CkChainGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkChainGym_Runtime.generated.h"

// Copies only the debug values missing from the script-facing Chain API.
// Links are the fixture's fixed creator returns, in visual slot order.
UCLASS()
class CKTESTS_API UCk_ChainGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|ChainGym")
    static bool Get_IsLiveChain(const FCk_Handle& Chain);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|ChainGym")
    static bool Get_IsSameFrame(
        const FCkChainGym_WireSnapshot& Left,
        const FCkChainGym_WireSnapshot& Right);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|ChainGym")
    static bool TryGet_DebugValues(
        const FCk_Handle_Chain& Chain,
        const FCk_Handle_Chain& SplitChain,
        const TArray<FCk_Handle_Transform>& Links,
        TArray<FVector>& OutHistory,
        TArray<FVector>& OutSplitHistory,
        TArray<FVector>& OutTargets,
        TArray<bool>& OutHasTargets,
        TArray<bool>& OutHeld);
};
