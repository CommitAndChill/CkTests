#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkGoapGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkGoapGym_Runtime.generated.h"

UCLASS()
class CKTESTS_API UCk_GoapGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapGym")
    static bool Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapGym")
    static bool Get_IsLivePlanner(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapGym")
    static bool Get_IsLiveWorldState(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapGym")
    static bool Get_IsLiveAction(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|GoapGym")
    static bool TryGet_SourceForest(UWorld* World, const FCk_Handle& Root, TArray<FCk_Handle>& OutForest);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapGym")
    static bool Get_IsSameFrame(const FCkGoapGym_WireSnapshot& Left, const FCkGoapGym_WireSnapshot& Right);
};
