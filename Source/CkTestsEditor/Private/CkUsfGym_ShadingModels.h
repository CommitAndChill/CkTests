#pragma once

#include "CoreMinimal.h"
#include "CkUsf/CkUsfGym_ShadingModelCatalog.h"
#include "Engine/SubsurfaceProfile.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"

namespace ck::usf_gym
{
    // Version 1 batches omitted transient editor data; keep those files intact and never load them.
    inline const FString CatalogPath = TEXT("/CkTests/CkUsf/GeneratedShadingModels/DA_UsfGym_ShadingModels_v2.DA_UsfGym_ShadingModels_v2");

    struct FEnvironment
    {
        bool IsPIE = false;
        bool CanRender = true;
        bool IsPlatformReady = true;
        bool IsMobile = false;
        bool IsForward = false;
        bool IsSubstrate = false;
        bool HasNativeToon = true;
    };

    struct FBatch
    {
        TArray<TStrongObjectPtr<UMaterial>> Materials;
        TStrongObjectPtr<USubsurfaceProfile> Profile;
        TArray<FCk_UsfGym_ShadingModelEntry> Models;
    };

    auto Get_Environment() -> FEnvironment;
    auto Validate_Environment(const FEnvironment& InEnvironment, TArray<FString>& OutErrors) -> bool;
    auto Build_TransientBatch(const FEnvironment& InEnvironment, FBatch& OutBatch, TArray<FString>& OutErrors) -> bool;
    auto Validate_Models(const TArray<FCk_UsfGym_ShadingModelEntry>& InModels, TArray<FString>& OutErrors) -> bool;
    auto Validate_ShaderMaps(const TArray<FCk_UsfGym_ShadingModelEntry>& InModels, TArray<FString>& OutErrors) -> bool;
    auto Publish_NewAsset(UObject& InAsset, const FString& InPackagePath) -> bool;
}
