#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_ShadingModelContract.h"

#include "AssetCompilingManager.h"
#include "EditorValidatorSubsystem.h"
#include "Engine/SubsurfaceProfile.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "ShaderCore.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_declaration
{
    struct FModelRow
    {
        const TCHAR* Name;
        ECk_Usf_ShadingModel Model;
        EMaterialShadingModel Native;
    };

    auto GetModels() -> TArray<FModelRow>
    {
        return {
            {TEXT("Unlit"), ECk_Usf_ShadingModel::Unlit, MSM_Unlit},
            {TEXT("DefaultLit"), ECk_Usf_ShadingModel::DefaultLit, MSM_DefaultLit},
            {TEXT("Subsurface"), ECk_Usf_ShadingModel::Subsurface, MSM_Subsurface},
            {TEXT("PreintegratedSkin"), ECk_Usf_ShadingModel::PreintegratedSkin, MSM_PreintegratedSkin},
            {TEXT("ClearCoat"), ECk_Usf_ShadingModel::ClearCoat, MSM_ClearCoat},
            {TEXT("SubsurfaceProfile"), ECk_Usf_ShadingModel::SubsurfaceProfile, MSM_SubsurfaceProfile},
            {TEXT("TwoSidedFoliage"), ECk_Usf_ShadingModel::TwoSidedFoliage, MSM_TwoSidedFoliage},
            {TEXT("Hair"), ECk_Usf_ShadingModel::Hair, MSM_Hair},
            {TEXT("Cloth"), ECk_Usf_ShadingModel::Cloth, MSM_Cloth},
            {TEXT("Eye"), ECk_Usf_ShadingModel::Eye, MSM_Eye},
            {TEXT("SingleLayerWater"), ECk_Usf_ShadingModel::SingleLayerWater, MSM_SingleLayerWater},
            {TEXT("ThinTranslucent"), ECk_Usf_ShadingModel::ThinTranslucent, MSM_ThinTranslucent},
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
            {TEXT("Toon"), ECk_Usf_ShadingModel::Toon, MSM_CkUsf},
#endif
        };
    }

    struct FSourceFixture
    {
        FString Directory;
        FString VirtualRoot;

        FSourceFixture()
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfDeclaration") / Id);
            VirtualRoot = TEXT("/CkUsf/Declaration_") + Id;
            IFileManager::Get().MakeDirectory(*Directory, true);
            // Retain this unique mount and its Saved files; never remove global shader mappings.
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
        }

        auto Write(const FString& InSource, FString& OutVirtualPath) const -> bool
        {
            const auto Name = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ush");
            OutVirtualPath = VirtualRoot / Name;
            return FFileHelper::SaveStringToFile(InSource, *(Directory / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }
    };

    auto ConfigureOutputs(UMaterialExpressionCustom& InCustom, const ck::usf_editor::FShadingModelContract& InContract) -> void
    {
        InCustom.AdditionalOutputs.Reset();
        for (const auto& Output : ck::usf_editor::Get_ShadingModelOutputs(InContract))
        { InCustom.AdditionalOutputs.Add(FCustomOutput{FName{*Output.Name}, Output.Type}); }
        InCustom.RebuildOutputs();
    }

    auto DrainFixtureCompilation() -> void
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr)
        { GShaderCompilingManager->FinishAllCompilation(); }
        FlushRenderingCommands();
    }

    auto RetireOwnedMaterial(UMaterial& InMaterial) -> void
    {
        // Loaded-object iteration can still visit garbage before GC; remove fixture reload dependencies first.
        for (const auto& Expression : InMaterial.GetExpressions())
        {
            const auto Custom = TObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(Expression)};
            if (Custom)
            { Custom->IncludeFilePaths.Reset(); }
        }
        InMaterial.UpdateCachedExpressionData();
        InMaterial.ClearFlags(RF_Public | RF_Standalone);
        InMaterial.MarkAsGarbage();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ShadingModelParser,
    "CkTests.UnitTests.CkUsf.ShadingModelDeclarationParser",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ShadingModelParser::RunTest(const FString& Parameters) -> bool
{
    using namespace ck::usf_editor;
    for (const auto& Row : ck_test_usf_declaration::GetModels())
    {
        auto Model = TOptional<ECk_Usf_ShadingModel>{};
        auto Error = FString{};
        TestTrue(*FString::Printf(TEXT("Parse %s"), Row.Name), Parse_ShadingModelDeclaration(
            FString::Printf(TEXT("// Look description\r\n// @ckusf shading_model %s\r\nvoid Look() {}"), Row.Name), Model, Error));
        TestTrue(TEXT("Parsed override matches the named model"), Model.IsSet() && Model.GetValue() == Row.Model);
        TestTrue(TEXT("Valid declaration has no diagnostic"), Error.IsEmpty());
    }
    for (const auto Source : {TEXT(""), TEXT("// no declaration\nvoid Look() {}"), TEXT("#include \"Nested.ush\"\nvoid Look() {}")})
    {
        auto Model = TOptional<ECk_Usf_ShadingModel>{ECk_Usf_ShadingModel::Hair};
        auto Error = FString{TEXT("stale diagnostic")};
        TestTrue(TEXT("Missing metadata is compatible"), Parse_ShadingModelDeclaration(Source, Model, Error));
        TestFalse(TEXT("Missing metadata clears an earlier override"), Model.IsSet());
        TestTrue(TEXT("Success clears earlier diagnostics"), Error.IsEmpty());
    }
    const TArray<FString> InvalidSources{
        TEXT("// @ckusf shading_model 7\nvoid Look() {}"),
        TEXT("// @ckusf shading_model UnknownModel\nvoid Look() {}"),
        TEXT("// @ckusf shading_model Inherit\nvoid Look() {}"),
        TEXT("// @ckusf shading_model\nvoid Look() {}"),
        TEXT("// @ckusf shading_model Cloth trailing\nvoid Look() {}"),
        TEXT("// @ckusf shading_model Cloth\n// @ckusf shading_model Cloth\nvoid Look() {}"),
        TEXT("// @ckusf shading_model Cloth\n// @ckusf shading_model Hair\nvoid Look() {}"),
        TEXT("#if 1\n// @ckusf shading_model Cloth\n#endif\nvoid Look() {}"),
        TEXT("void Look() {\n// @ckusf shading_model Cloth\n}"),
        TEXT("void Look() {}\n// @ckusf shading_model Cloth"),
        TEXT("/*\n// @ckusf shading_model Cloth\n*/\nvoid Look() {}")};
    for (const auto& Source : InvalidSources)
    {
        auto Model = TOptional<ECk_Usf_ShadingModel>{ECk_Usf_ShadingModel::DefaultLit};
        auto Error = FString{};
        TestFalse(*FString::Printf(TEXT("Reject malformed declaration: %s"), *Source), Parse_ShadingModelDeclaration(Source, Model, Error));
        TestFalse(TEXT("Rejection exposes no partial override"), Model.IsSet());
        TestFalse(TEXT("Rejection explains the invalid metadata"), Error.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ShadingModelResolution,
    "CkTests.UnitTests.CkUsf.ShadingModelDeclarationResolvesAndWiresAllModels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ShadingModelResolution::RunTest(const FString& Parameters) -> bool
{
    using namespace ck::usf_editor;
    const auto Fixture = ck_test_usf_declaration::FSourceFixture{};
    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
    const auto Profile = TStrongObjectPtr<USubsurfaceProfile>{NewObject<USubsurfaceProfile>()};
    Definition->_UshFunctionName = TEXT("Look");
    Definition->_SubsurfaceProfile = Profile.Get();
    // The file's selection must take precedence over a conflicting legacy asset override.
    Definition->_ShadingModel = ECk_Usf_ShadingModel::ClearCoat;
    for (const auto& Row : ck_test_usf_declaration::GetModels())
    {
        if (TestTrue(TEXT("Write owned entrypoint fixture"), Fixture.Write(
            FString::Printf(TEXT("// @ckusf shading_model %s\nvoid Look() {}\n"), Row.Name), Definition->_UshIncludePath)) == false)
        { return false; }
        auto Contract = FShadingModelContract{};
        auto Errors = TArray<FString>{};
        const auto Resolved = Resolve_ShadingModelContract(*Definition, Contract, Errors);
        for (const auto& Error : Errors) { AddError(Error); }
        if (TestTrue(*FString::Printf(TEXT("Resolve %s"), Row.Name), Resolved) == false)
        { continue; }
        TestTrue(TEXT("File declaration wins over the asset's legacy selection"), Contract.Model == Row.Model);
        TestTrue(TEXT("File origin is recorded"), Contract.SourceOverride);
        TestTrue(TEXT("Named model resolves to the engine model"), Contract.NativeModel == Row.Native);
        const auto Material = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>()};
        ON_SCOPE_EXIT
        {
            ck_test_usf_declaration::RetireOwnedMaterial(*Material);
            TestTrue(TEXT("Wiring-only fixture is excluded from later global material recompiles"),
                Material->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
        };
        const auto Custom = TStrongObjectPtr<UMaterialExpressionCustom>{NewObject<UMaterialExpressionCustom>(Material.Get())};
        Material->GetExpressionCollection().AddExpression(Custom.Get());
        ck_test_usf_declaration::ConfigureOutputs(*Custom, Contract);
        Apply_ShadingModelContract(*Material, *Custom, Contract);
        TestTrue(TEXT("Exactly the selected native model is active"), Material->GetShadingModels().CountShadingModels() == 1 &&
            Material->GetShadingModels().HasShadingModel(Row.Native));
        TestTrue(TEXT("Only Thin Translucent forces translucent blending"), Material->BlendMode ==
            (Row.Native == MSM_ThinTranslucent ? BLEND_Translucent : BLEND_Opaque));
        TestTrue(TEXT("Only Hair uses a world-space tangent"), Material->bTangentSpaceNormal == (Row.Native != MSM_Hair));
        TestTrue(TEXT("Foliage enables two-sided rendering"), Material->TwoSided == (Row.Native == MSM_TwoSidedFoliage));
        TestTrue(TEXT("Scattering profile is assigned only where required"), Material->SubsurfaceProfile ==
            ((Row.Native == MSM_SubsurfaceProfile || Row.Native == MSM_Eye) ? Profile.Get() : nullptr));
        const auto Outputs = Get_ShadingModelOutputs(Contract);
        const auto Code = Build_ShadingModelOutputCode(Contract);
        TestTrue(TEXT("Legacy surface output indices remain available"), Outputs.Num() >= 12);
        const TArray<FString> LegacyNames{TEXT("EmissiveColor"), TEXT("Roughness"), TEXT("Metallic"), TEXT("Specular"),
            TEXT("AmbientOcclusion"), TEXT("Normal"), TEXT("Opacity"), TEXT("OpacityMask"), TEXT("Refraction"),
            TEXT("SubsurfaceColor"), TEXT("ClearCoat"), TEXT("ClearCoatRoughness")};
        for (auto Index = 0; Index < LegacyNames.Num() && Index < Outputs.Num(); ++Index)
        { TestEqual(TEXT("Existing additional output indices do not move"), Outputs[Index].Name, LegacyNames[Index]); }
        TestEqual(TEXT("Custom exposes every resolved additional output"), Custom->AdditionalOutputs.Num(), Outputs.Num());
        for (auto Index = 0; Index < Outputs.Num(); ++Index)
        {
            if (Custom->AdditionalOutputs.IsValidIndex(Index))
            {
                TestEqual(TEXT("Generated output name matches its stable index"), Custom->AdditionalOutputs[Index].OutputName, FName{*Outputs[Index].Name});
                TestTrue(TEXT("Generated output has the declared HLSL type"), Custom->AdditionalOutputs[Index].OutputType == Outputs[Index].Type);
            }
        }
        const auto CheckPin = [this, &Custom](const FExpressionInput& InInput, const TCHAR* InName)
        {
            TestTrue(*FString::Printf(TEXT("%s pin uses the production Custom node"), InName), InInput.Expression == Custom.Get());
            const auto& Pins = Custom->GetOutputs();
            if (TestTrue(TEXT("Connected output index exists"), Pins.IsValidIndex(InInput.OutputIndex)))
            { TestEqual(TEXT("Model-specific input uses its named output"), Pins[InInput.OutputIndex].OutputName, FName{InName}); }
        };
        const auto& Data = *Material->GetEditorOnlyData();
        switch (Row.Native)
        {
            case MSM_Subsurface:
            case MSM_PreintegratedSkin:
            case MSM_TwoSidedFoliage:
                CheckPin(Data.SubsurfaceColor, TEXT("SubsurfaceColor"));
                CheckPin(Data.Opacity, TEXT("Opacity"));
                break;
            case MSM_SubsurfaceProfile:
                CheckPin(Data.Opacity, TEXT("Opacity"));
                break;
            case MSM_ClearCoat:
                CheckPin(Data.ClearCoat, TEXT("ClearCoat"));
                CheckPin(Data.ClearCoatRoughness, TEXT("ClearCoatRoughness"));
                break;
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
            case MSM_CkUsf:
                CheckPin(Data.ClearCoat, TEXT("ToonThreshold"));
                CheckPin(Data.ClearCoatRoughness, TEXT("ToonSoftness"));
                break;
#endif
            case MSM_Hair:
                CheckPin(Data.Normal, TEXT("CkUsf_HairTangent"));
                CheckPin(Data.Metallic, TEXT("CkUsf_HairScatter"));
                CheckPin(Data.ClearCoat, TEXT("CkUsf_HairBacklit"));
                TestTrue(TEXT("Hair code consumes its tangent field"), Code.Contains(TEXT("O.HairTangent")));
                break;
            case MSM_Cloth:
                CheckPin(Data.SubsurfaceColor, TEXT("SubsurfaceColor"));
                CheckPin(Data.ClearCoat, TEXT("CkUsf_ClothMask"));
                break;
            case MSM_Eye:
                CheckPin(Data.ClearCoat, TEXT("CkUsf_EyeIrisMask"));
                CheckPin(Data.ClearCoatRoughness, TEXT("CkUsf_EyeIrisDistance"));
                break;
            case MSM_ThinTranslucent:
                TestTrue(TEXT("Thin transmission uses per-pixel surface lighting"), Material->TranslucencyLightingMode == TLM_SurfacePerPixelLighting);
                break;
            default: break;
        }
        auto WaterCount = 0;
        auto ThinCount = 0;
        for (const auto& Expression : Material->GetExpressions())
        {
            const auto Water = TObjectPtr<UMaterialExpressionSingleLayerWaterMaterialOutput>{Cast<UMaterialExpressionSingleLayerWaterMaterialOutput>(Expression)};
            const auto Thin = TObjectPtr<UMaterialExpressionThinTranslucentMaterialOutput>{Cast<UMaterialExpressionThinTranslucentMaterialOutput>(Expression)};
            if (Water)
            {
                ++WaterCount;
                CheckPin(Water->ScatteringCoefficients, TEXT("CkUsf_WaterScattering"));
                CheckPin(Water->AbsorptionCoefficients, TEXT("CkUsf_WaterAbsorption"));
                CheckPin(Water->PhaseG, TEXT("CkUsf_WaterPhaseG"));
                CheckPin(Water->ColorScaleBehindWater, TEXT("CkUsf_WaterColorScaleBehindWater"));
            }
            if (Thin)
            {
                ++ThinCount;
                CheckPin(Thin->TransmittanceColor, TEXT("CkUsf_ThinTransmittance"));
                CheckPin(Thin->SurfaceCoverage, TEXT("CkUsf_ThinSurfaceCoverage"));
            }
        }
        TestEqual(TEXT("Only water creates exactly one water output node"), WaterCount, Row.Native == MSM_SingleLayerWater ? 1 : 0);
        TestEqual(TEXT("Only thin transmission creates exactly one transmission node"), ThinCount, Row.Native == MSM_ThinTranslucent ? 1 : 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ShadingModelTransitions,
    "CkTests.UnitTests.CkUsf.ShadingModelDeclarationTransitionsRemovePreviousSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ShadingModelTransitions::RunTest(const FString& Parameters) -> bool
{
    using namespace ck::usf_editor;
    const auto Fixture = ck_test_usf_declaration::FSourceFixture{};
    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
    const auto Profile = TStrongObjectPtr<USubsurfaceProfile>{NewObject<USubsurfaceProfile>()};
    Definition->_SubsurfaceProfile = Profile.Get();
    const auto Material = TStrongObjectPtr<UMaterial>{NewObject<UMaterial>()};
    ON_SCOPE_EXIT
    {
        ck_test_usf_declaration::RetireOwnedMaterial(*Material);
        TestTrue(TEXT("Transition fixture is excluded from later global material recompiles"),
            Material->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
    };
    const auto Custom = TStrongObjectPtr<UMaterialExpressionCustom>{NewObject<UMaterialExpressionCustom>(Material.Get())};
    Material->GetExpressionCollection().AddExpression(Custom.Get());
    for (const auto& Row : ck_test_usf_declaration::GetModels())
    {
        for (const auto Name : {FString{Row.Name}, FString{TEXT("DefaultLit")}})
        {
            if (TestTrue(TEXT("Write transition fixture"), Fixture.Write(
                TEXT("// @ckusf shading_model ") + Name + TEXT("\nvoid Look() {}"), Definition->_UshIncludePath)) == false)
            { return false; }
            auto Contract = FShadingModelContract{};
            auto Errors = TArray<FString>{};
            if (TestTrue(TEXT("Resolve transition contract"), Resolve_ShadingModelContract(*Definition, Contract, Errors)) == false)
            { for (const auto& Error : Errors) { AddError(Error); } return false; }
            ck_test_usf_declaration::ConfigureOutputs(*Custom, Contract);
            Apply_ShadingModelContract(*Material, *Custom, Contract);
            // Applying twice must not accumulate special output expressions.
            Apply_ShadingModelContract(*Material, *Custom, Contract);
            auto SpecialCount = 0;
            for (const auto& Expression : Material->GetExpressions())
            {
                if (Expression->IsA<UMaterialExpressionSingleLayerWaterMaterialOutput>() ||
                    Expression->IsA<UMaterialExpressionThinTranslucentMaterialOutput>())
                { ++SpecialCount; }
            }
            TestEqual(TEXT("Repeated application never duplicates model output expressions"), SpecialCount,
                Name == TEXT("SingleLayerWater") || Name == TEXT("ThinTranslucent") ? 1 : 0);
        }
        TestTrue(TEXT("Transition ends with Default Lit as its sole model"), Material->GetShadingModels().CountShadingModels() == 1 &&
            Material->GetShadingModels().HasShadingModel(MSM_DefaultLit));
        TestTrue(TEXT("Thin transition resets opaque blending"), Material->BlendMode == BLEND_Opaque);
        TestTrue(TEXT("Thin transition resets per-pixel translucency lighting"), Material->TranslucencyLightingMode == TLM_VolumetricNonDirectional);
        TestTrue(TEXT("Thin transition resets translucency pass"), Material->TranslucencyPass == MTP_AfterDOF);
        TestTrue(TEXT("Hair transition restores tangent-space normals"), Material->bTangentSpaceNormal);
        TestFalse(TEXT("Foliage transition restores one-sided rendering"), Material->TwoSided);
        TestNull(TEXT("Eye/profile transition clears the scattering profile"), Material->SubsurfaceProfile.Get());
        const auto& Data = *Material->GetEditorOnlyData();
        TestNull(TEXT("Transition clears model-specific subsurface pin"), Data.SubsurfaceColor.Expression);
        TestNull(TEXT("Transition clears model-specific custom-data zero pin"), Data.ClearCoat.Expression);
        TestNull(TEXT("Transition clears model-specific custom-data one pin"), Data.ClearCoatRoughness.Expression);
        for (const auto& Expression : Material->GetExpressions())
        {
            TestFalse(TEXT("Transition removes obsolete water output expression"), Expression->IsA<UMaterialExpressionSingleLayerWaterMaterialOutput>());
            TestFalse(TEXT("Transition removes obsolete thin output expression"), Expression->IsA<UMaterialExpressionThinTranslucentMaterialOutput>());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ShadingModelRejection,
    "CkTests.UnitTests.CkUsf.ShadingModelDeclarationRejectsInvalidContracts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ShadingModelRejection::RunTest(const FString& Parameters) -> bool
{
    using namespace ck::usf_editor;
    const auto Fixture = ck_test_usf_declaration::FSourceFixture{};
    for (auto Case = 0; Case < 7; ++Case)
    {
        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
        auto Name = FString{TEXT("Cloth")};
        if (Case == 0) { Definition->_Domain = ECk_Usf_Domain::UI; }
        if (Case == 1) { Definition->_Domain = ECk_Usf_Domain::PostProcess; }
        if (Case == 2) { Definition->_Domain = ECk_Usf_Domain::Decal; }
        if (Case == 3) { Name = TEXT("SubsurfaceProfile"); }
        if (Case == 4) { Name = TEXT("Eye"); }
        if (Case == 5) { Name = TEXT("SingleLayerWater"); Definition->_BlendMode = ECk_Usf_BlendMode::Translucent; }
        if (Case == 6) { Name = TEXT("ThinTranslucent"); Definition->_BlendMode = ECk_Usf_BlendMode::Opaque; }
        if (TestTrue(TEXT("Write invalid-contract fixture"), Fixture.Write(
            TEXT("// @ckusf shading_model ") + Name + TEXT("\nvoid Look() {}"), Definition->_UshIncludePath)) == false)
        { return false; }
        auto Contract = FShadingModelContract{};
        Contract.Model = ECk_Usf_ShadingModel::Hair;
        Contract.NativeModel = MSM_Hair;
        Contract.BlendMode = BLEND_Masked;
        auto Errors = TArray<FString>{};
        TestFalse(*FString::Printf(TEXT("Reject incompatible domain/profile/blend case %d"), Case), Resolve_ShadingModelContract(*Definition, Contract, Errors));
        TestFalse(TEXT("Rejected contract explains incompatibility"), Errors.IsEmpty());
        TestTrue(TEXT("Rejected resolution publishes no partial candidate over the prior contract"),
            Contract.Model == ECk_Usf_ShadingModel::Hair && Contract.NativeModel == MSM_Hair && Contract.BlendMode == BLEND_Masked);
    }
    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
    if (TestTrue(TEXT("Write legacy entrypoint fixture"), Fixture.Write(TEXT("void Look() {}"), Definition->_UshIncludePath)) == false)
    { return false; }
    Definition->_ShadingModel = ECk_Usf_ShadingModel::Subsurface;
    auto Contract = FShadingModelContract{};
    auto Errors = TArray<FString>{};
    TestTrue(TEXT("Metadata-free entrypoint preserves legacy asset selection"), Resolve_ShadingModelContract(*Definition, Contract, Errors));
    TestTrue(TEXT("Legacy Subsurface resolves without file metadata"), Contract.NativeModel == MSM_Subsurface && NOT Contract.SourceOverride);
    Definition->_ShadingModel = ECk_Usf_ShadingModel::Inherit;
    TestTrue(TEXT("Legacy Inherit resolves without file metadata"), Resolve_ShadingModelContract(*Definition, Contract, Errors));
    TestTrue(TEXT("SurfaceLit Inherit preserves DefaultLit"), Contract.NativeModel == MSM_DefaultLit);
    Definition->_Domain = ECk_Usf_Domain::SurfaceUnlit;
    TestTrue(TEXT("Legacy unlit domain resolves without metadata"), Resolve_ShadingModelContract(*Definition, Contract, Errors));
    TestTrue(TEXT("SurfaceUnlit Inherit preserves Unlit and translucent blending"), Contract.NativeModel == MSM_Unlit && Contract.BlendMode == BLEND_Translucent);
    auto NestedPath = FString{};
    if (TestTrue(TEXT("Write nested source with an unrelated model declaration"), Fixture.Write(
        TEXT("// @ckusf shading_model Hair\nvoid Nested() {}"), NestedPath)) == false)
    { return false; }
    if (TestTrue(TEXT("Write root source that only includes nested metadata"), Fixture.Write(
        FString::Printf(TEXT("#include \"%s\"\nvoid Look() {}"), *NestedPath), Definition->_UshIncludePath)) == false)
    { return false; }
    Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
    TestTrue(TEXT("Entrypoint without metadata resolves independently of nested include metadata"), Resolve_ShadingModelContract(*Definition, Contract, Errors));
    TestTrue(TEXT("Nested include cannot change the root shading model"), Contract.NativeModel == MSM_DefaultLit && NOT Contract.SourceOverride);
    Definition->_UshIncludePath = Fixture.VirtualRoot / TEXT("MissingEntrypoint.ush");
    Errors.Reset();
    TestFalse(TEXT("Unreadable entrypoint rejects before material mutation"), Resolve_ShadingModelContract(*Definition, Contract, Errors));
    TestFalse(TEXT("Unreadable source has actionable diagnostics"), Errors.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ShadingModelCompilation,
    "CkTests.UnitTests.CkUsf.ShadingModelDeclarationCompilesAllModels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

auto FCkTest_Usf_ShadingModelCompilation::RunTest(const FString& Parameters) -> bool
{
    if (TestTrue(TEXT("Source-declared model compilation requires a real RHI"), FApp::CanEverRender()) == false)
    { return false; }
    const auto Fixture = ck_test_usf_declaration::FSourceFixture{};
    const auto Root = TEXT("/CkUsfDeclarationCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/");
    const auto PackageDirectory = Fixture.Directory / TEXT("Packages/");
    if (TestTrue(TEXT("Create owned Saved package directory"), IFileManager::Get().MakeDirectory(*PackageDirectory, true)) == false)
    { return false; }
    FPackageName::RegisterMountPoint(Root, PackageDirectory);
    ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(Root, PackageDirectory); };
    // Generated fixtures are deliberately outside production asset-validation targets.
    const FScopedDisableValidateOnSave DisableFixtureValidation;
    AddInfo(FString::Printf(TEXT("Source-declared model fixtures retained at %s"), *Fixture.Directory));
    auto Materials = TArray<TStrongObjectPtr<UMaterial>>{};
    ON_SCOPE_EXIT
    {
        ck_test_usf_declaration::DrainFixtureCompilation();
        for (const auto& Material : Materials)
        {
            if (Material.IsValid())
            { ck_test_usf_declaration::RetireOwnedMaterial(*Material); }
        }
    };
    const auto Rows = ck_test_usf_declaration::GetModels();
    for (const auto& Row : Rows)
    {
        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
        Definition->_LookName = FName{Row.Name};
        Definition->_GeneratedPackageRoot = Root.LeftChop(1);
        Definition->_UshFunctionName = TEXT("CkUsf_Look_Declaration");
        const auto Source = FString::Printf(TEXT("// @ckusf shading_model %s\n#pragma once\n#include \"/CkUsf/Common.ush\"\n"
            "FCkUsf_SurfaceOutput CkUsf_Look_Declaration(FCkUsf_SurfaceInput In) { return CkUsf_DefaultSurface(); }\n"), Row.Name);
        if (TestTrue(TEXT("Write source-declared model compile fixture"), Fixture.Write(Source, Definition->_UshIncludePath)) == false)
        { return false; }
        // Keep the required profile inside its new material package so SavePackage has no private external reference.
        const auto Package = TStrongObjectPtr<UPackage>{CreatePackage(*Definition->Get_GeneratedMasterPackagePath())};
        const auto Profile = TStrongObjectPtr<USubsurfaceProfile>{NewObject<USubsurfaceProfile>(Package.Get(), TEXT("FixtureProfile"), RF_Public)};
        Definition->_SubsurfaceProfile = Profile.Get();
        auto Material = TStrongObjectPtr<UMaterial>{ck::usf_editor::Generate_LookMaterial(Definition.Get())};
        if (TestNotNull(*FString::Printf(TEXT("Production generator creates %s"), Row.Name), Material.Get()) == false)
        { return false; }
        TestTrue(TEXT("Generated master uses exactly the source-selected model"), Material->GetShadingModels().HasOnlyShadingModel(Row.Native));
        TestTrue(TEXT("Production generator saved the owned fixture package"), FPackageName::DoesPackageExist(Definition->Get_GeneratedMasterPackagePath()));
        Material->EnsureIsComplete();
        Materials.Add(MoveTemp(Material));
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager != nullptr)
    { GShaderCompilingManager->FinishAllCompilation(); }
    FlushRenderingCommands();
    for (auto Index = 0; Index < Materials.Num(); ++Index)
    {
        const auto& Material = *Materials[Index];
        if (TestNotNull(TEXT("Generated material has an active-platform resource"), Material.GetMaterialResource(GMaxRHIShaderPlatform)) == false)
        { continue; }
        const auto& Resource = *Material.GetMaterialResource(GMaxRHIShaderPlatform);
        TestTrue(TEXT("Active platform preserves the declared model"), Resource.GetShadingModels().HasOnlyShadingModel(Rows[Index].Native));
        for (const auto& Error : Resource.GetCompileErrors())
        { AddError(FString::Printf(TEXT("[%s] %s"), Rows[Index].Name, *Error)); }
        TestTrue(TEXT("Generated material compilation finishes"), Resource.IsCompilationFinished());
        TestTrue(TEXT("Generated material has a complete game-thread shader map"), Resource.IsGameThreadShaderMapComplete() && Resource.GetGameThreadShaderMap() != nullptr);
        auto RenderComplete = false;
        ENQUEUE_RENDER_COMMAND(CkUsfDeclarationShaderMapComplete)([&Resource, &RenderComplete](FRHICommandListImmediate&)
        { RenderComplete = Resource.IsRenderingThreadShaderMapComplete(); });
        FlushRenderingCommands();
        TestTrue(TEXT("Generated material has a complete render-thread shader map"), RenderComplete);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ShadingModelReloadPreparation,
    "CkTests.UnitTests.CkUsf.ShadingModelDeclarationReloadPreservesExistingObjects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ShadingModelReloadPreparation::RunTest(const FString& Parameters) -> bool
{
    using namespace ck::usf_editor;
    const auto Fixture = ck_test_usf_declaration::FSourceFixture{};
    const auto Root = TEXT("/CkUsfDeclarationReload_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/");
    const auto PackageDirectory = Fixture.Directory / TEXT("Packages/");
    if (TestTrue(TEXT("Create reload fixture package directory"), IFileManager::Get().MakeDirectory(*PackageDirectory, true)) == false)
    { return false; }
    FPackageName::RegisterMountPoint(Root, PackageDirectory);
    ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(Root, PackageDirectory); };
    const FScopedDisableValidateOnSave DisableFixtureValidation;
    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
    Definition->_LookName = TEXT("ReloadFixture");
    Definition->_GeneratedPackageRoot = Root.LeftChop(1);
    Definition->_UshIncludePath = Fixture.VirtualRoot / TEXT("Reload.ush");
    Definition->_UshFunctionName = TEXT("CkUsf_Look_Declaration");
    auto Parameter = FCk_Usf_ParamDesc{};
    Parameter._Name = TEXT("Tint");
    Parameter._DefaultScalar = 0.4f;
    Definition->_Parameters.Add(Parameter);
    const auto WriteSource = [&Fixture](const FString& InModel, bool InVector = false) -> bool
    {
        const auto Annotation = InModel.IsEmpty() ? FString{} : TEXT("// @ckusf shading_model ") + InModel + TEXT("\n");
        const auto Source = Annotation + FString::Printf(TEXT("#pragma once\n#include \"/CkUsf/Common.ush\"\n"
            "FCkUsf_SurfaceOutput CkUsf_Look_Declaration(FCkUsf_SurfaceInput In, %s Tint) "
            "{ FCkUsf_SurfaceOutput O = CkUsf_DefaultSurface(); O.BaseColor = Tint; return O; }\n"), InVector ? TEXT("float3") : TEXT("float"));
        return FFileHelper::SaveStringToFile(Source, *(Fixture.Directory / TEXT("Reload.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    if (TestTrue(TEXT("Write initial Cloth source"), WriteSource(TEXT("Cloth"))) == false)
    { return false; }
    const auto Material = TStrongObjectPtr<UMaterial>{Generate_LookMaterial(Definition.Get())};
    if (TestNotNull(TEXT("Production generator creates reload fixture"), Material.Get()) == false)
    { return false; }
    ON_SCOPE_EXIT
    {
        ck_test_usf_declaration::DrainFixtureCompilation();
        ck_test_usf_declaration::RetireOwnedMaterial(*Material);
    };
    const auto Mid = TStrongObjectPtr<UMaterialInstanceDynamic>{UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage())};
    if (TestNotNull(TEXT("Existing MID created"), Mid.Get()) == false)
    { return false; }
    Mid->SetScalarParameterValue(TEXT("Tint"), 0.75f);
    auto Custom = TWeakObjectPtr<UMaterialExpressionCustom>{};
    auto Scalar = TWeakObjectPtr<UMaterialExpressionScalarParameter>{};
    for (const auto& Expression : Material->GetExpressions())
    {
        if (const auto Candidate = TWeakObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(Expression)}; Candidate.IsValid())
        { Custom = Candidate; }
        if (const auto Candidate = TWeakObjectPtr<UMaterialExpressionScalarParameter>{Cast<UMaterialExpressionScalarParameter>(Expression)};
            Candidate.IsValid() && Candidate->ParameterName == TEXT("Tint"))
        { Scalar = Candidate; }
    }
    if (TestTrue(TEXT("Generated primary Custom and scalar parameter exist"), Custom.IsValid() && Scalar.IsValid()) == false)
    { return false; }
    for (const auto Model : {FString{TEXT("ThinTranslucent")}, FString{}})
    {
        if (TestTrue(TEXT("Rewrite only entrypoint model annotation"), WriteSource(Model)) == false)
        { return false; }
        auto Prepared = FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        if (TestTrue(TEXT("Production preflight accepts annotation transition"), Prepare_LookShadingModelReload(*Material, Prepared, Errors)) == false)
        { for (const auto& Error : Errors) { AddError(Error); } return false; }
        TestTrue(TEXT("Preparation targets the existing Custom expression"), Prepared.Custom == Custom);
        Apply_LookShadingModelReload(*Material, Prepared);
        TestTrue(TEXT("Production application retains MID parent"), Mid->Parent == Material.Get());
        TestTrue(TEXT("Production application retains the scalar parameter object"), Material->GetExpressions().Contains(Scalar.Get()));
        TestTrue(TEXT("Existing MID keeps parameter overrides"), FMath::IsNearlyEqual(Mid->K2_GetScalarParameterValue(TEXT("Tint")), 0.75f));
        TestTrue(TEXT("Annotation removal restores the definition's default model"), Material->GetShadingModels().HasOnlyShadingModel(
            Model.IsEmpty() ? MSM_DefaultLit : MSM_ThinTranslucent));
    }
    const auto BeforeState = Material->StateId;
    const auto BeforeCode = Custom->Code;
    const auto BeforeBlend = Material->BlendMode;
    const auto BeforeLighting = Material->TranslucencyLightingMode;
    const auto BeforeTwoSided = Material->TwoSided;
    const auto BeforeTangentSpace = Material->bTangentSpaceNormal;
    const auto BeforeProfile = TWeakObjectPtr<USubsurfaceProfile>{Material->SubsurfaceProfile};
    const TArray<EMaterialProperty> Properties{MP_BaseColor, MP_Normal, MP_Metallic, MP_Specular, MP_Roughness,
        MP_AmbientOcclusion, MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_Refraction,
        MP_SubsurfaceColor, MP_CustomData0, MP_CustomData1};
    auto BeforeInputs = TArray<FExpressionInput>{};
    for (const auto Property : Properties) { BeforeInputs.Add(*Material->GetExpressionInputForProperty(Property)); }
    auto BeforeExpressions = TArray<TWeakObjectPtr<UMaterialExpression>>{};
    for (const auto& Expression : Material->GetExpressions()) { BeforeExpressions.Add(Expression); }
    const auto BeforeOutputCount = Custom->AdditionalOutputs.Num();
    for (auto Case = 0; Case < 3; ++Case)
    {
        if (Case == 2)
        { Definition->_Parameters[0]._Type = ECk_Usf_ParamType::Vector; }
        if (TestTrue(TEXT("Write invalid reload candidate"), WriteSource(Case == 0 ? TEXT("UnknownModel") :
            Case == 1 ? TEXT("Eye") : TEXT("Cloth"), Case == 2)) == false)
        { return false; }
        auto Prepared = FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        TestFalse(TEXT("Production preflight rejects invalid metadata, missing profile, or changed parameter type"),
            Prepare_LookShadingModelReload(*Material, Prepared, Errors));
        TestFalse(TEXT("Rejected reload never publishes a runnable Custom expression"), Prepared.Custom.IsValid());
        TestFalse(TEXT("Rejected reload reports its reason"), Errors.IsEmpty());
        TestEqual(TEXT("Rejected preflight leaves material state ID unchanged"), Material->StateId, BeforeState);
        TestEqual(TEXT("Rejected preflight leaves HLSL unchanged"), Custom->Code, BeforeCode);
        TestTrue(TEXT("Rejected preflight leaves shading settings unchanged"), Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit)
            && Material->BlendMode == BeforeBlend && Material->TranslucencyLightingMode == BeforeLighting
            && Material->TwoSided == BeforeTwoSided && Material->bTangentSpaceNormal == BeforeTangentSpace
            && Material->SubsurfaceProfile == BeforeProfile.Get());
        for (auto Index = 0; Index < Properties.Num(); ++Index)
        {
            const auto& Before = BeforeInputs[Index];
            const auto& After = *Material->GetExpressionInputForProperty(Properties[Index]);
            TestTrue(TEXT("Rejected preflight preserves every material pin connection"), Before.Expression == After.Expression
                && Before.OutputIndex == After.OutputIndex && Before.Mask == After.Mask
                && Before.MaskR == After.MaskR && Before.MaskG == After.MaskG && Before.MaskB == After.MaskB && Before.MaskA == After.MaskA);
        }
        TestEqual(TEXT("Rejected preflight leaves output layout unchanged"), Custom->AdditionalOutputs.Num(), BeforeOutputCount);
        TestEqual(TEXT("Rejected preflight leaves graph size unchanged"), Material->GetExpressions().Num(), BeforeExpressions.Num());
        for (auto Index = 0; Index < BeforeExpressions.Num() && Index < Material->GetExpressions().Num(); ++Index)
        { TestTrue(TEXT("Rejected preflight preserves every expression identity"), Material->GetExpressions()[Index] == BeforeExpressions[Index].Get()); }
        TestTrue(TEXT("Rejected preflight preserves the MID parent and scalar override"), Mid->Parent == Material.Get()
            && FMath::IsNearlyEqual(Mid->K2_GetScalarParameterValue(TEXT("Tint")), 0.75f));
    }
    return true;
}

#endif
