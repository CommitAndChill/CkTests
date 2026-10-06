#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkGym_WireTransformRuntime.generated.h"

// Exact received representation for FTransform's Iris quaternion member.
UCLASS()
class CKTESTS_API UCk_Gym_WireTransform_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Source and OutExpected must be distinct. Other failures clear OutExpected to Identity.
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gym|Wire")
    static bool TryGet_ExpectedIrisTransform(const FTransform& Source, FTransform& OutExpected);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gym|Wire")
    static bool Get_IsExactIrisTransform(const FTransform& Source, const FTransform& Actual);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gym|Wire")
    static bool Get_IsExactAuthoredOrIrisTransform(const FTransform& Authored, const FTransform& Actual);

    // Reflected FRotator uses Iris's short-axis serializer, not raw IEEE doubles.
    // Finite source axes must have |angle| <= 1,000,000 degrees before Iris int32 quantization.
    // Source and OutExpected must be distinct; other failures clear OutExpected.
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gym|Wire")
    static bool TryGet_ExpectedIrisRotator(const FRotator& Source, FRotator& OutExpected);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gym|Wire")
    static bool Get_IsExactIrisRotator(const FRotator& Source, const FRotator& Actual);
};
