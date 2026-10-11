#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkDialogGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkDialogGym_Runtime.generated.h"

UCLASS()
class CKTESTS_API UCk_DialogGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|DialogGym")
    static bool Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|DialogGym")
    static bool Get_IsLiveEmitter(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|DialogGym")
    static bool Get_IsLiveTimer(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|DialogGym")
    static bool Get_IsLiveLine(const FCk_Handle& Handle, UWorld* World);

    // The source is a direct world child. Reuses the complete allocated forest census.
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|DialogGym")
    static bool TryGet_SourceForest(UWorld* World, const FCk_Handle& Root,
        TArray<FCk_Handle>& OutForest);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|DialogGym")
    static bool Get_IsSameFrame(const FCkDialogGym_WireSnapshot& Left,
        const FCkDialogGym_WireSnapshot& Right);
};
