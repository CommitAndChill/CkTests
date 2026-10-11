#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkGoapAutoReplanGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkGoapAutoReplanGym_Runtime.generated.h"

UCLASS()
class CKTESTS_API UCk_GoapAutoReplanGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapAutoReplanGym")
    static bool Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapAutoReplanGym")
    static bool Get_IsLiveTimer(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapAutoReplanGym")
    static bool Get_IsLivePlanner(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapAutoReplanGym")
    static bool Get_IsLiveWorldState(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapAutoReplanGym")
    static bool Get_IsLiveAction(const FCk_Handle& Handle, UWorld* World);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|GoapAutoReplanGym")
    static bool TryGet_SourceForest(UWorld* World, const FCk_Handle& Root, TArray<FCk_Handle>& OutForest);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|GoapAutoReplanGym")
    static bool Get_IsSameFrame(const FCkGoapAutoReplanGym_WireSnapshot& Left, const FCkGoapAutoReplanGym_WireSnapshot& Right);
};
