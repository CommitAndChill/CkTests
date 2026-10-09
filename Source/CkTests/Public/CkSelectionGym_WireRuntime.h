#pragma once

#include "CoreMinimal.h"
#include "CkSelectionGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSelectionGym_WireRuntime.generated.h"

UCLASS()
class CKTESTS_API UCkSelectionGym_WireRuntime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|SelectionGym|Wire")
    static bool Get_IsSameFrame(const FCkSelectionGym_WireSnapshot& A,
        const FCkSelectionGym_WireSnapshot& B);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|SelectionGym|Wire")
    static bool Get_IsDefaultFrame(const FCkSelectionGym_WireSnapshot& Frame);
};
