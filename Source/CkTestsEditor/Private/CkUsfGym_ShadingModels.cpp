#include "CkUsfGym_ShadingModels.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "MaterialDomain.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionSphereMask.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogCkUsfGymShadingModels, Log, All);

namespace ck::usf_gym
{
    namespace authoring
    {
        struct FModel
        {
            EMaterialShadingModel Model;
            FString Name;
            FString Description;
        };

        auto Get_Models() -> TArray<FModel>
        {
            auto Models = TArray<FModel>{
                {MSM_Unlit, TEXT("Unlit"), TEXT("Emissive color; ignores scene lighting.")},
                {MSM_DefaultLit, TEXT("Default Lit"), TEXT("Standard opaque physically based surface.")},
                {MSM_Subsurface, TEXT("Subsurface"), TEXT("Subsurface color and partial opacity demonstrate local scattering.")},
                {MSM_PreintegratedSkin, TEXT("Preintegrated Skin"), TEXT("Legacy skin scattering; a sphere only compares the shading response.")},
                {MSM_ClearCoat, TEXT("Clear Coat"), TEXT("A smooth clear layer over the common rough base surface.")},
                {MSM_SubsurfaceProfile, TEXT("Subsurface Profile"), TEXT("Profile scattering with nonzero opacity; needs screen-space scattering enabled.")},
                {MSM_TwoSidedFoliage, TEXT("Two Sided Foliage"), TEXT("Two-sided subsurface transmission; a thin leaf mesh demonstrates this better than a sphere.")},
                {MSM_Hair, TEXT("Hair"), TEXT("Hair tangent, scatter and backlit inputs; the sphere is a BxDF comparison, not groom geometry.")},
                {MSM_Cloth, TEXT("Cloth"), TEXT("Subsurface color supplies fuzz color; the cloth mask enables the fuzz lobe.")},
                {MSM_Eye, TEXT("Eye"), TEXT("Procedural UV iris mask/depth and a scattering profile; a sphere lacks authored cornea/iris geometry and eye-specific UVs.")},
                {MSM_SingleLayerWater, TEXT("Single Layer Water"), TEXT("Opaque water with volume scattering and absorption; a closed sphere is not an ocean surface.")},
                {MSM_ThinTranslucent, TEXT("Thin Translucent"), TEXT("Per-pixel forward-lit transmission; a closed sphere is not a single thin pane.")}};
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
            Models.Add({MSM_CkUsf, TEXT("CkUsf Toon"), TEXT("Native deferred Toon bands using threshold and softness custom data.")});
#endif
            return Models;
        }

        template <typename TExpression>
        auto MakeExpression(UMaterial& InMaterial) -> TObjectPtr<TExpression>
        {
            const auto Expression = TObjectPtr<TExpression>{NewObject<TExpression>(&InMaterial, NAME_None, RF_Transactional)};
            InMaterial.GetExpressionCollection().AddExpression(Expression.Get());
            Expression->Material = &InMaterial;
            Expression->UpdateMaterialExpressionGuid(true, false);
            Expression->UpdateParameterGuid(true, false);
            return Expression;
        }

        auto Scalar(UMaterial& InMaterial, FName InName, float InValue) -> TObjectPtr<UMaterialExpressionScalarParameter>
        {
            const auto Expression = MakeExpression<UMaterialExpressionScalarParameter>(InMaterial);
            Expression->ParameterName = InName;
            Expression->DefaultValue = InValue;
            InMaterial.AddExpressionParameter(Expression.Get(), InMaterial.EditorParameters);
            return Expression;
        }

        auto Vector(UMaterial& InMaterial, FName InName, const FLinearColor& InValue) -> TObjectPtr<UMaterialExpressionVectorParameter>
        {
            const auto Expression = MakeExpression<UMaterialExpressionVectorParameter>(InMaterial);
            Expression->ParameterName = InName;
            Expression->DefaultValue = InValue;
            InMaterial.AddExpressionParameter(Expression.Get(), InMaterial.EditorParameters);
            return Expression;
        }

        auto Configure(UMaterial& InMaterial, EMaterialShadingModel InModel, USubsurfaceProfile& InProfile) -> void
        {
            InMaterial.MaterialDomain = MD_Surface;
            InMaterial.SetShadingModel(InModel);
            InMaterial.BlendMode = BLEND_Opaque;
            auto& Data = *InMaterial.GetEditorOnlyData();
            Data.BaseColor.Connect(0, Vector(InMaterial, TEXT("BaseColor"), FLinearColor{0.08f, 0.32f, 0.8f}).Get());
            Data.Roughness.Connect(0, Scalar(InMaterial, TEXT("Roughness"), 0.35f).Get());
            Data.Specular.Connect(0, Scalar(InMaterial, TEXT("Specular"), 0.5f).Get());
            Data.Metallic.Connect(0, Scalar(InMaterial, TEXT("Metallic"), 0.0f).Get());

            switch (InModel)
            {
                case MSM_Unlit:
                    Data.EmissiveColor.Connect(0, Vector(InMaterial, TEXT("EmissiveColor"), FLinearColor{0.08f, 0.32f, 0.8f}).Get());
                    break;
                case MSM_Subsurface:
                case MSM_PreintegratedSkin:
                case MSM_TwoSidedFoliage:
                    Data.SubsurfaceColor.Connect(0, Vector(InMaterial, TEXT("SubsurfaceColor"), FLinearColor{0.3f, 0.65f, 0.8f}).Get());
                    Data.Opacity.Connect(0, Scalar(InMaterial, TEXT("Opacity"), 0.5f).Get());
                    InMaterial.TwoSided = InModel == MSM_TwoSidedFoliage;
                    break;
                case MSM_ClearCoat:
                    Data.ClearCoat.Connect(0, Scalar(InMaterial, TEXT("CoatAmount"), 1.0f).Get());
                    Data.ClearCoatRoughness.Connect(0, Scalar(InMaterial, TEXT("CoatRoughness"), 0.08f).Get());
                    break;
                case MSM_SubsurfaceProfile:
                    InMaterial.SubsurfaceProfile = &InProfile;
                    Data.Opacity.Connect(0, Scalar(InMaterial, TEXT("Opacity"), 1.0f).Get());
                    break;
                case MSM_Hair:
                    InMaterial.bTangentSpaceNormal = false;
                    Data.Normal.Connect(0, Vector(InMaterial, TEXT("HairTangent"), FLinearColor{0.0f, 0.0f, 1.0f}).Get());
                    Data.Metallic.Connect(0, Scalar(InMaterial, TEXT("Scatter"), 0.5f).Get());
                    Data.ClearCoat.Connect(0, Scalar(InMaterial, TEXT("Backlit"), 1.0f).Get());
                    break;
                case MSM_Cloth:
                    Data.SubsurfaceColor.Connect(0, Vector(InMaterial, TEXT("FuzzColor"), FLinearColor{0.25f, 0.6f, 0.9f}).Get());
                    Data.ClearCoat.Connect(0, Scalar(InMaterial, TEXT("ClothMask"), 1.0f).Get());
                    break;
                case MSM_Eye:
                {
                    InMaterial.SubsurfaceProfile = &InProfile;
                    const auto UV = MakeExpression<UMaterialExpressionTextureCoordinate>(InMaterial);
                    const auto Center = MakeExpression<UMaterialExpressionConstant2Vector>(InMaterial);
                    Center->R = 0.5f;
                    Center->G = 0.5f;
                    const auto Iris = MakeExpression<UMaterialExpressionSphereMask>(InMaterial);
                    Iris->A.Connect(0, UV.Get());
                    Iris->B.Connect(0, Center.Get());
                    Iris->Radius.Connect(0, Scalar(InMaterial, TEXT("IrisRadiusUV"), 0.2f).Get());
                    Iris->HardnessPercent = 80.0f;
                    Data.ClearCoat.Connect(0, Iris.Get());
                    Data.ClearCoatRoughness.Connect(0, Scalar(InMaterial, TEXT("IrisDistance"), 0.1f).Get());
                    break;
                }
                case MSM_SingleLayerWater:
                {
                    const auto Water = MakeExpression<UMaterialExpressionSingleLayerWaterMaterialOutput>(InMaterial);
                    Water->ScatteringCoefficients.Connect(0, Vector(InMaterial, TEXT("WaterScattering"), FLinearColor{0.002f, 0.006f, 0.01f}).Get());
                    Water->AbsorptionCoefficients.Connect(0, Vector(InMaterial, TEXT("WaterAbsorption"), FLinearColor{0.03f, 0.01f, 0.005f}).Get());
                    Water->PhaseG.Connect(0, Scalar(InMaterial, TEXT("WaterPhaseG"), 0.1f).Get());
                    Data.Opacity.Connect(0, Scalar(InMaterial, TEXT("WaterSurfaceOpacity"), 0.15f).Get());
                    break;
                }
                case MSM_ThinTranslucent:
                {
                    InMaterial.BlendMode = BLEND_Translucent;
                    InMaterial.TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
                    InMaterial.TranslucencyPass = MTP_BeforeDOF;
                    const auto Thin = MakeExpression<UMaterialExpressionThinTranslucentMaterialOutput>(InMaterial);
                    Thin->TransmittanceColor.Connect(0, Vector(InMaterial, TEXT("TransmittanceColor"), FLinearColor{0.5f, 0.75f, 0.9f}).Get());
                    Thin->SurfaceCoverage.Connect(0, Scalar(InMaterial, TEXT("SurfaceCoverage"), 1.0f).Get());
                    Data.Opacity.Connect(0, Scalar(InMaterial, TEXT("Opacity"), 0.25f).Get());
                    break;
                }
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
                case MSM_CkUsf:
                    Data.ClearCoat.Connect(0, Scalar(InMaterial, TEXT("BandThreshold"), 0.5f).Get());
                    Data.ClearCoatRoughness.Connect(0, Scalar(InMaterial, TEXT("BandSoftness"), 0.05f).Get());
                    break;
#endif
                default:
                    break;
            }
        }

        auto Save(UObject& InAsset) -> bool
        {
            const auto Filename = FPackageName::LongPackageNameToFilename(
                InAsset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
            if (IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true) == false)
            { return false; }
            auto Args = FSavePackageArgs{};
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            return UPackage::SavePackage(InAsset.GetOutermost(), &InAsset, *Filename, Args);
        }

    }

    auto Publish_NewAsset(UObject& InAsset, const FString& InPackagePath) -> bool
    {
        if (FPackageName::IsValidLongPackageName(InPackagePath) == false || FPackageName::DoesPackageExist(InPackagePath)
            || InAsset.GetOutermost() != GetTransientPackage() || InAsset.HasAnyFlags(RF_Transient) == false)
        { return false; }
        const auto Material = TWeakObjectPtr<UMaterial>{Cast<UMaterial>(&InAsset)};
        auto EditorData = TWeakObjectPtr<UMaterialEditorOnlyData>{};
        if (Material.IsValid())
        {
            EditorData = Material->GetEditorOnlyData();
            if (EditorData.IsValid() == false)
            { return false; }
        }
        auto Package = TStrongObjectPtr<UPackage>{FindPackage(nullptr, *InPackagePath)};
        if (Package.IsValid())
        {
            if (Package->IsDirty())
            { return false; }
            auto HasObjects = false;
            ForEachObjectWithOuterBreakable(Package.Get(), [&HasObjects](TObjectPtr<UObject>)
            {
                HasObjects = true;
                return false;
            });
            if (HasObjects)
            { return false; }
        }
        else
        { Package = TStrongObjectPtr<UPackage>{CreatePackage(*InPackagePath)}; }
        if (InAsset.Rename(*FPackageName::GetLongPackageAssetName(InPackagePath), Package.Get(), REN_DontCreateRedirectors | REN_NonTransactional) == false)
        { return false; }
        InAsset.ClearFlags(RF_Transient);
        if (EditorData.IsValid())
        { EditorData->ClearFlags(RF_Transient); }
        InAsset.SetFlags(RF_Public | RF_Standalone);
        if (authoring::Save(InAsset) == false)
        { return false; }
        FAssetRegistryModule::AssetCreated(&InAsset);
        return true;
    }

    auto Get_Environment() -> FEnvironment
    {
        auto Environment = FEnvironment{};
        Environment.IsPIE = GEditor != nullptr && GEditor->PlayWorld != nullptr;
        Environment.CanRender = FApp::CanEverRender() && GUsingNullRHI == false;
        Environment.IsPlatformReady = static_cast<uint32>(GMaxRHIShaderPlatform) < static_cast<uint32>(SP_NumPlatforms);
        if (Environment.IsPlatformReady)
        { Environment.IsPlatformReady = FDataDrivenShaderPlatformInfo::IsValid(GMaxRHIShaderPlatform); }
        if (Environment.IsPlatformReady)
        {
            Environment.IsMobile = IsMobilePlatform(GMaxRHIShaderPlatform);
            Environment.IsForward = IsForwardShadingEnabled(GMaxRHIShaderPlatform);
        }
        Environment.IsSubstrate = Substrate::IsSubstrateEnabled();
#if !defined(UE_CK_USF_SHADING_MODEL_VERSION) || UE_CK_USF_SHADING_MODEL_VERSION < 1
        Environment.HasNativeToon = false;
#endif
        return Environment;
    }

    auto Validate_Environment(const FEnvironment& InEnvironment, TArray<FString>& OutErrors) -> bool
    {
        if (InEnvironment.IsPIE)
        { OutErrors.Add(TEXT("Stop PIE before generating comparison materials, then run Ck_GymUsf_GenerateShadingModels in the editor console.")); }
        if (InEnvironment.CanRender == false)
        { OutErrors.Add(TEXT("Shading-model generation requires a real RHI; restart without -nullrhi.")); }
        if (InEnvironment.IsPlatformReady == false)
        { OutErrors.Add(TEXT("Shading-model generation requires initialized desktop shader-platform metadata.")); }
        if (InEnvironment.IsMobile || InEnvironment.IsForward || InEnvironment.IsSubstrate)
        { OutErrors.Add(TEXT("Shading-model comparison requires desktop deferred rendering with Substrate disabled.")); }
        if (InEnvironment.HasNativeToon == false)
        { OutErrors.Add(TEXT("Shading-model comparison requires UE_CK_USF_SHADING_MODEL_VERSION >= 1.")); }
        return OutErrors.IsEmpty();
    }

    auto Build_TransientBatch(const FEnvironment& InEnvironment, FBatch& OutBatch, TArray<FString>& OutErrors) -> bool
    {
        if (Validate_Environment(InEnvironment, OutErrors) == false)
        { return false; }
        auto Batch = FBatch{};
        Batch.Profile = TStrongObjectPtr<USubsurfaceProfile>{NewObject<USubsurfaceProfile>(GetTransientPackage(), NAME_None, RF_Transient)};
        for (const auto& Model : authoring::Get_Models())
        {
            auto Material = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient)};
            authoring::Configure(*Material, Model.Model, *Batch.Profile);
            auto Entry = FCk_UsfGym_ShadingModelEntry{};
            Entry.DisplayName = Model.Name;
            Entry.Description = Model.Description;
            Entry.ShadingModel = Model.Model;
            Entry.Material = Material.Get();
            Batch.Models.Add(MoveTemp(Entry));
            Batch.Materials.Add(MoveTemp(Material));
        }
        if (Validate_Models(Batch.Models, OutErrors) == false)
        { return false; }
        for (const auto& Material : Batch.Materials)
        { Material->PostEditChange(); }
        OutBatch = MoveTemp(Batch);
        return true;
    }

    auto Validate_Models(const TArray<FCk_UsfGym_ShadingModelEntry>& InModels, TArray<FString>& OutErrors) -> bool
    {
        const auto Required = authoring::Get_Models();
        if (InModels.Num() != Required.Num())
        { OutErrors.Add(TEXT("Comparison catalog must contain the entire shading-model roster.")); return false; }
        for (auto Index = 0; Index < Required.Num(); ++Index)
        {
            const auto& Entry = InModels[Index];
            const auto Material = TObjectPtr<UMaterial>{Cast<UMaterial>(Entry.Material)};
            if (Material == nullptr || Entry.ShadingModel != Required[Index].Model || Entry.DisplayName != Required[Index].Name || Entry.Description.IsEmpty())
            { OutErrors.Add(FString::Printf(TEXT("Invalid comparison catalog entry at index %d."), Index)); continue; }
            const auto& Data = *Material->GetEditorOnlyData();
            auto Valid = Material->MaterialDomain == MD_Surface && Material->GetShadingModels().CountShadingModels() == 1
                && Material->GetShadingModels().HasShadingModel(Required[Index].Model)
                && Data.BaseColor.Expression != nullptr && Data.Roughness.Expression != nullptr;
            const auto Model = Required[Index].Model;
            Valid &= Material->BlendMode == (Model == MSM_ThinTranslucent ? BLEND_Translucent : BLEND_Opaque);
            if (Model == MSM_Unlit)
            { Valid &= Data.EmissiveColor.Expression != nullptr; }
            if (Model == MSM_Subsurface || Model == MSM_PreintegratedSkin || Model == MSM_TwoSidedFoliage || Model == MSM_Cloth)
            { Valid &= Data.SubsurfaceColor.Expression != nullptr; }
            if (Model == MSM_Subsurface || Model == MSM_PreintegratedSkin || Model == MSM_SubsurfaceProfile)
            {
                const auto Opacity = TObjectPtr<UMaterialExpressionScalarParameter>{Cast<UMaterialExpressionScalarParameter>(Data.Opacity.Expression)};
                Valid &= Opacity != nullptr && Opacity->DefaultValue > 0.0f;
            }
            if (Model == MSM_SubsurfaceProfile || Model == MSM_Eye)
            { Valid &= Material->SubsurfaceProfile != nullptr; }
            if (Model == MSM_TwoSidedFoliage)
            { Valid &= Material->TwoSided; }
            if (Model == MSM_Hair)
            { Valid &= Data.Normal.Expression != nullptr && Data.Metallic.Expression != nullptr && Data.ClearCoat.Expression != nullptr && Material->bTangentSpaceNormal == false; }
            if (Model == MSM_ClearCoat || Model == MSM_Eye
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
                || Model == MSM_CkUsf
#endif
                )
            { Valid &= Data.ClearCoat.Expression != nullptr && Data.ClearCoatRoughness.Expression != nullptr; }
            if (Model == MSM_Cloth)
            { Valid &= Data.ClearCoat.Expression != nullptr; }
            if (Model == MSM_SingleLayerWater || Model == MSM_ThinTranslucent)
            {
                auto HasOutput = false;
                for (const auto& Expression : Material->GetExpressionCollection().Expressions)
                {
                    if (Model == MSM_SingleLayerWater)
                    {
                        const auto Water = TObjectPtr<UMaterialExpressionSingleLayerWaterMaterialOutput>{Cast<UMaterialExpressionSingleLayerWaterMaterialOutput>(Expression)};
                        HasOutput |= Water != nullptr && Water->ScatteringCoefficients.Expression != nullptr && Water->AbsorptionCoefficients.Expression != nullptr && Water->PhaseG.Expression != nullptr;
                    }
                    else
                    {
                        const auto Thin = TObjectPtr<UMaterialExpressionThinTranslucentMaterialOutput>{Cast<UMaterialExpressionThinTranslucentMaterialOutput>(Expression)};
                        HasOutput |= Thin != nullptr && Thin->TransmittanceColor.Expression != nullptr && Thin->SurfaceCoverage.Expression != nullptr;
                    }
                }
                Valid &= HasOutput;
            }
            if (Model == MSM_ThinTranslucent)
            { Valid &= Material->TranslucencyLightingMode == TLM_SurfacePerPixelLighting && Material->TranslucencyPass != MTP_AfterMotionBlur; }
            if (Valid == false)
            { OutErrors.Add(FString::Printf(TEXT("Comparison material [%s] violates its model input/settings contract."), *Entry.DisplayName)); }
        }
        return OutErrors.IsEmpty();
    }

    auto Validate_ShaderMaps(const TArray<FCk_UsfGym_ShadingModelEntry>& InModels, TArray<FString>& OutErrors) -> bool
    {
        if (Validate_Environment(Get_Environment(), OutErrors) == false || Validate_Models(InModels, OutErrors) == false)
        { return false; }
        for (const auto& Entry : InModels)
        {
            auto& Material = *CastChecked<UMaterial>(Entry.Material);
            Material.EnsureIsComplete();
        }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr)
        { GShaderCompilingManager->FinishAllCompilation(); }
        FlushRenderingCommands();
        for (const auto& Entry : InModels)
        {
            const auto& Material = *CastChecked<UMaterial>(Entry.Material);
            if (Material.GetMaterialResource(GMaxRHIShaderPlatform) == nullptr)
            { OutErrors.Add(FString::Printf(TEXT("[%s] has no material resource."), *Entry.DisplayName)); continue; }
            const auto& Resource = *Material.GetMaterialResource(GMaxRHIShaderPlatform);
            if (Resource.GetShadingModels().HasOnlyShadingModel(Entry.ShadingModel) == false)
            { OutErrors.Add(FString::Printf(TEXT("[%s] was filtered to a different shading model on the active shader platform."), *Entry.DisplayName)); }
            for (const auto& Error : Resource.GetCompileErrors())
            { OutErrors.Add(FString::Printf(TEXT("[%s] %s"), *Entry.DisplayName, *Error)); }
            auto RenderMapComplete = false;
            ENQUEUE_RENDER_COMMAND(CkUsfGymShaderMapComplete)([&Resource, &RenderMapComplete](FRHICommandListImmediate&)
            { RenderMapComplete = Resource.IsRenderingThreadShaderMapComplete(); });
            FlushRenderingCommands();
            if (Resource.IsCompilationFinished() == false || Resource.IsGameThreadShaderMapComplete() == false || Resource.GetGameThreadShaderMap() == nullptr || RenderMapComplete == false)
            { OutErrors.Add(FString::Printf(TEXT("[%s] does not have complete game/render-thread shader maps."), *Entry.DisplayName)); }
        }
        return OutErrors.IsEmpty();
    }

    namespace authoring
    {
        auto Generate(const TArray<FString>& InArguments) -> void
        {
            auto Errors = TArray<FString>{};
            const auto Report = [&Errors]()
            {
                for (const auto& Error : Errors)
                { UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("%s"), *Error); }
            };
            if (Validate_Environment(Get_Environment(), Errors) == false)
            { Report(); return; }
            if (InArguments.Num() > 1 || (InArguments.Num() == 1 && InArguments[0].Equals(TEXT("regenerate"), ESearchCase::IgnoreCase) == false))
            { UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("Usage: Ck_GymUsf_GenerateShadingModels [regenerate]")); return; }
            const auto CatalogPackage = FPackageName::ObjectPathToPackageName(CatalogPath);
            auto ExistingObject = TStrongObjectPtr<UObject>{FindObject<UObject>(nullptr, *CatalogPath)};
            if (ExistingObject.IsValid() == false && FPackageName::DoesPackageExist(CatalogPackage))
            { ExistingObject = TStrongObjectPtr<UObject>{LoadObject<UObject>(nullptr, *CatalogPath, nullptr, LOAD_NoWarn | LOAD_Quiet)}; }
            auto Catalog = TStrongObjectPtr<UCk_UsfGym_ShadingModelCatalog>{Cast<UCk_UsfGym_ShadingModelCatalog>(ExistingObject.Get())};
            if ((ExistingObject.IsValid() && Catalog.IsValid() == false) || (Catalog.IsValid() == false && FPackageName::DoesPackageExist(CatalogPackage)))
            { UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("Catalog path is occupied or unreadable; existing data was preserved: %s"), *CatalogPath); return; }
            if (Catalog.IsValid() && InArguments.IsEmpty())
            {
                if (Validate_ShaderMaps(Catalog->Models, Errors))
                { UE_LOG(LogCkUsfGymShadingModels, Display, TEXT("Reusing validated comparison catalog: %s"), *CatalogPath); }
                else
                { Report(); UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("Existing catalog preserved. Run Ck_GymUsf_GenerateShadingModels regenerate to explicitly replace it.")); }
                return;
            }
            if (Catalog.IsValid() && Catalog->GetOutermost()->IsDirty())
            { UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("Catalog has unsaved edits. Save or resolve them before regeneration.")); return; }
            auto Batch = FBatch{};
            if (Build_TransientBatch(Get_Environment(), Batch, Errors) == false || Validate_ShaderMaps(Batch.Models, Errors) == false)
            { Report(); return; }

            const auto Root = FString{TEXT("/CkTests/CkUsf/GeneratedShadingModels/Batch_")} + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/");
            if (Publish_NewAsset(*Batch.Profile, Root + TEXT("SP_Comparison")) == false)
            { UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("Profile save failed; previous catalog preserved.")); return; }
            for (auto Index = 0; Index < Batch.Materials.Num(); ++Index)
            {
                if (Publish_NewAsset(*Batch.Materials[Index], Root + FString::Printf(TEXT("M_Model_%02d"), Index)) == false)
                { UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("Material batch save failed; previous catalog preserved. Unreferenced batch assets are retained at %s"), *Root); return; }
            }

            const auto IsNew = Catalog.IsValid() == false;
            if (IsNew)
            { Catalog = TStrongObjectPtr<UCk_UsfGym_ShadingModelCatalog>{NewObject<UCk_UsfGym_ShadingModelCatalog>(GetTransientPackage(), NAME_None, RF_Transient)}; }
            const auto PreviousModels = Catalog->Models;
            Catalog->Models = Batch.Models;
            const auto Saved = IsNew ? Publish_NewAsset(*Catalog, CatalogPackage) : Save(*Catalog);
            if (Saved == false)
            {
                Catalog->Models = PreviousModels;
                if (IsNew == false)
                { Catalog->GetOutermost()->SetDirtyFlag(false); }
                UE_LOG(LogCkUsfGymShadingModels, Error, TEXT("Catalog save failed; previous in-memory catalog restored. Unreferenced batch assets retained at %s"), *Root);
                return;
            }
            UE_LOG(LogCkUsfGymShadingModels, Display, TEXT("Saved %d validated comparison materials and catalog: %s"), Catalog->Models.Num(), *CatalogPath);
        }

        FAutoConsoleCommand GenerateCommand{
            TEXT("Ck_GymUsf_GenerateShadingModels"),
            TEXT("Create/reuse the real-RHI desktop deferred comparison catalog outside PIE. Add regenerate to explicitly publish a new immutable batch."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&Generate)};
    }
}
