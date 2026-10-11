#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkGoapEmpireGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkGoapEmpireGym_Runtime.generated.h"

UCLASS()
class CKTESTS_API UCk_GoapEmpireGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapEmpireGym")
    static bool Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapEmpireGym")
    static bool Get_IsLivePlanner(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapEmpireGym")
    static bool Get_IsLiveWorldState(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapEmpireGym")
    static bool Get_IsLiveAction(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|GoapEmpireGym")
    static bool TryGet_SourceForest(UWorld* World, const FCk_Handle& Root, TArray<FCk_Handle>& OutForest);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapEmpireGym")
    static bool Get_IsSameFrame(const FCkGoapEmpireGym_WireSnapshot& Left, const FCkGoapEmpireGym_WireSnapshot& Right);
};
