#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEqsGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkEqsGym_Runtime.generated.h"

UCLASS()
class CKTESTS_API UCk_EqsGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|EqsGym")
    static bool Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|EqsGym")
    static bool Get_IsLiveTimer(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|EqsGym")
    static bool Get_IsLiveQuery(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|EqsGym")
    static bool Get_IsQueryOwnedBy(const FCk_Handle& Query, const FCk_Handle& Root, UWorld* World);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|EqsGym")
    static bool TryGet_SourceForest(UWorld* World, const FCk_Handle& Root, TArray<FCk_Handle>& OutForest);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|EqsGym")
    static bool Get_IsSameFrame(const FCkEqsGym_WireSnapshot& Left, const FCkEqsGym_WireSnapshot& Right);
};
