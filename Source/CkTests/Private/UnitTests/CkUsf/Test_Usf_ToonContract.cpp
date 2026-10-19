#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"
#include "CkUsf_TestLookMasters.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_toon_contract
{
    auto MakeDefinition() -> TStrongObjectPtr<UCkUsf_LookDefinition>
    {
        auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>(GetTransientPackage())};
        Definition->_LookName = FName{*FString::Printf(TEXT("ToonContract_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))};
        Definition->_GeneratedPackageRoot = ck_test_usf::Get_TestPackageRoot();
        Definition->_UshIncludePath = TEXT("/CkUsf/Looks/Toon.ush");
        Definition->_UshFunctionName = TEXT("CkUsf_Look_Toon");
        Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
        Definition->_ShadingModel = ECk_Usf_ShadingModel::Toon;
        auto Tint = FCk_Usf_ParamDesc{};
        Tint._Name = TEXT("TintColor");
        Tint._Type = ECk_Usf_ParamType::Vector;
        Tint._DefaultVector = FLinearColor::White;
        auto Threshold = FCk_Usf_ParamDesc{};
        Threshold._Name = TEXT("BandThreshold");
        Threshold._DefaultScalar = 0.5f;
        auto Softness = FCk_Usf_ParamDesc{};
        Softness._Name = TEXT("BandSoftness");
        Softness._DefaultScalar = 0.05f;
        Definition->_Parameters = {Tint, Threshold, Softness};
        return Definition;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ToonContract,
    "CkTests.UnitTests.CkUsf.ToonAuthoringGeneratesNativeMaster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

auto FCkTest_Usf_ToonContract::RunTest(const FString& Parameters) -> bool
{
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
    if (TestTrue(TEXT("Native Toon compile requires real RHI"), FApp::CanEverRender()) == false)
    { return false; }
    const auto Definition = ck_test_usf_toon_contract::MakeDefinition();
    const auto PackagePath = Definition->Get_GeneratedMasterPackagePath();
    if (TestNull(TEXT("Unique test package is absent from memory"), FindPackage(nullptr, *PackagePath)) == false ||
        TestFalse(TEXT("Unique test package is absent from disk"), FPackageName::DoesPackageExist(PackagePath)) == false)
    { return false; }
    ON_SCOPE_EXIT
    {
        TestTrue(TEXT("Only the test's generated master is cleaned up"),
            ck_test_usf::Delete_TestGeneratedMaster(Definition->_LookName));
    };
    const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());
    for (const auto& Error : Validation.Errors)
    { AddError(Error); }
    if (TestTrue(TEXT("Shipped Toon authoring contract validates"), Validation.Get_IsValid()) == false)
    { return false; }
    const auto Master = TStrongObjectPtr<UMaterial>{ck::usf_editor::Generate_LookMaterial(Definition.Get())};
    if (TestNotNull(TEXT("Production generator creates Toon master"), Master.Get()) == false)
    { return false; }
    TestTrue(TEXT("Generated master uses the native deferred CkUsf model"), Master->GetShadingModels().HasShadingModel(MSM_CkUsf));
    TestEqual(TEXT("Generated master is opaque"), Master->GetBlendMode(), BLEND_Opaque);
    const auto CheckOutput = [this](const FExpressionInput& InInput, const FName InOutput) -> void
    {
        const auto Custom = TObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(InInput.Expression)};
        if (TestNotNull(TEXT("Toon custom-data pin is wired to the production Custom node"), Custom.Get()) == false)
        { return; }
        const auto& Outputs = Custom->GetOutputs();
        if (TestTrue(TEXT("Toon custom-data output index is valid"), Outputs.IsValidIndex(InInput.OutputIndex)) == false)
        { return; }
        TestEqual(TEXT("Toon custom-data pin uses the named Toon output"), Outputs[InInput.OutputIndex].OutputName, InOutput);
        TestTrue(TEXT("Custom node includes shipped Toon HLSL"), Custom->IncludeFilePaths.Contains(TEXT("/CkUsf/Looks/Toon.ush")));
    };
    CheckOutput(Master->GetEditorOnlyData()->ClearCoat, TEXT("ToonThreshold"));
    CheckOutput(Master->GetEditorOnlyData()->ClearCoatRoughness, TEXT("ToonSoftness"));
    auto CompileErrors = TArray<FString>{};
    TestTrue(TEXT("Production compile validator accepts real Toon shaders"),
        ck::usf_editor::Validate_LookShaderCompile(Master.Get(), Definition->_LookName, CompileErrors, true));
    for (const auto& Error : CompileErrors)
    { AddError(Error); }
    return true;
#else
    AddError(TEXT("Toon authoring requires UE_CK_USF_SHADING_MODEL_VERSION >= 1"));
    return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ToonRejectsUnsupportedAuthoring,
    "CkTests.UnitTests.CkUsf.ToonRejectsUnsupportedAuthoringAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ToonRejectsUnsupportedAuthoring::RunTest(const FString& Parameters) -> bool
{
    for (auto Case = 0; Case != 2; ++Case)
    {
        const auto Definition = ck_test_usf_toon_contract::MakeDefinition();
        if (Case == 0)
        { Definition->_Domain = ECk_Usf_Domain::SurfaceUnlit; }
        else
        { Definition->_BlendMode = ECk_Usf_BlendMode::Translucent; }
        const auto Marker = Case == 0 ? FString{TEXT("Toon requires the SurfaceLit domain")} :
            FString{TEXT("Toon currently supports only Opaque blending")};
        const auto Path = Definition->Get_GeneratedMasterPackagePath();
        const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());
        TestTrue(TEXT("Unsupported Toon declaration reports the specific contract violation"),
            Validation.Errors.ContainsByPredicate([&](const FString& InError) { return InError.Contains(Marker); }));
        AddExpectedError(*Marker, EAutomationExpectedErrorFlags::Contains, 1);
        TestNull(TEXT("Production generator rejects unsupported Toon authoring"),
            ck::usf_editor::Generate_LookMaterial(Definition.Get()));
        TestNull(TEXT("Rejection creates no material package in memory"), FindPackage(nullptr, *Path));
        TestFalse(TEXT("Rejection writes no material package"), FPackageName::DoesPackageExist(Path));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ToonReservedNames,
    "CkTests.UnitTests.CkUsf.ToonReservedOutputNamesAreScoped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ToonReservedNames::RunTest(const FString& Parameters) -> bool
{
    const auto Marker = FString{TEXT("collides with the generated Toon output")};
    for (const auto Name : {FName{TEXT("ToonThreshold")}, FName{TEXT("toonsoftness")}})
    {
        const auto Definition = ck_test_usf_toon_contract::MakeDefinition();
        Definition->_Parameters[1]._Name = Name;
        Definition->_ShadingModel = ECk_Usf_ShadingModel::DefaultLit;
        const auto Compatible = ck::usf_editor::Validate_LookDefinition(Definition.Get());
        TestTrue(TEXT("Existing non-Toon scalar names remain valid"), Compatible.Get_IsValid());

        Definition->_ShadingModel = ECk_Usf_ShadingModel::Toon;
        const auto Rejected = ck::usf_editor::Validate_LookDefinition(Definition.Get());
        TestTrue(TEXT("Toon rejects its generated output names case-insensitively"),
            Rejected.Errors.ContainsByPredicate([&](const FString& InError) { return InError.Contains(Marker); }));
        AddExpectedError(*Marker, EAutomationExpectedErrorFlags::Contains, 1);
        TestNull(TEXT("Reserved Toon parameter prevents generation"),
            ck::usf_editor::Generate_LookMaterial(Definition.Get()));
        const auto Path = Definition->Get_GeneratedMasterPackagePath();
        TestNull(TEXT("Reserved-name rejection creates no package"), FindPackage(nullptr, *Path));
        TestFalse(TEXT("Reserved-name rejection writes no package"), FPackageName::DoesPackageExist(Path));
    }
    return true;
}

#endif // WITH_EDITOR
