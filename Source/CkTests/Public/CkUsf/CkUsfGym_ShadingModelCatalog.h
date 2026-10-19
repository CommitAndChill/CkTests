#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "CkUsfGym_ShadingModelCatalog.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCk_UsfGym_ShadingModelEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "USF Gym")
    FString DisplayName;

    UPROPERTY(BlueprintReadOnly, Category = "USF Gym")
    FString Description;

    UPROPERTY(BlueprintReadOnly, Category = "USF Gym")
    TObjectPtr<UMaterialInterface> Material;

    UPROPERTY(BlueprintReadOnly, Category = "USF Gym")
    TEnumAsByte<EMaterialShadingModel> ShadingModel = MSM_DefaultLit;
};

/** Generated comparison assets for the desktop deferred legacy-material gym. */
UCLASS(BlueprintType)
class CKTESTS_API UCk_UsfGym_ShadingModelCatalog : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "USF Gym")
    TArray<FCk_UsfGym_ShadingModelEntry> Models;
};
