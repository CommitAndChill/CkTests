#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"
#include "CkUsfEditor/Generator/CkUsf_ShadingModelContract.h"
#include "CkUsf_TestLookMasters.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_toon_contract
{
#if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
    constexpr auto NativeToonSupported = true;
#else
    constexpr auto NativeToonSupported = false;
#endif

    auto MakeUnresolvedContract() -> ck::usf_editor::FShadingModelContract
    {
        auto Contract = ck::usf_editor::FShadingModelContract{};
        Contract.Model = ECk_Usf_ShadingModel::Unlit;
        Contract.NativeModel = MSM_Unlit;
        Contract.BlendMode = BLEND_Additive;
        Contract.TranslucencyLighting = TLM_Surface;
        Contract.TwoSided = true;
        Contract.TangentSpaceNormal = false;
        Contract.SourceOverride = true;
        return Contract;
    }

    auto Get_IsSameContract(const ck::usf_editor::FShadingModelContract& InContract,
        const ck::usf_editor::FShadingModelContract& InExpected) -> bool
    {
        return InContract.Model == InExpected.Model && InContract.NativeModel == InExpected.NativeModel
            && InContract.BlendMode == InExpected.BlendMode
            && InContract.TranslucencyLighting == InExpected.TranslucencyLighting
            && InContract.TwoSided == InExpected.TwoSided
            && InContract.TangentSpaceNormal == InExpected.TangentSpaceNormal
            && InContract.Profile == InExpected.Profile && InContract.SourceOverride == InExpected.SourceOverride;
    }

    auto MakeDefinition(const FString& InPackageRoot) -> TStrongObjectPtr<UCkUsf_LookDefinition>
    {
        auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>(GetTransientPackage())};
        Definition->_LookName = FName{*FString::Printf(TEXT("ToonContract_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))};
        Definition->_GeneratedPackageRoot = InPackageRoot;
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
    auto GeneratedMasters = ck_test_usf::FGeneratedMasterScope{*this};
    if (!GeneratedMasters.Initialize()) { return false; }
    const auto Succeeded = [&]() -> bool
    {
    #if defined(UE_CK_USF_SHADING_MODEL_VERSION) && UE_CK_USF_SHADING_MODEL_VERSION >= 1
        if (TestTrue(TEXT("Native Toon compile requires real RHI"), FApp::CanEverRender()) == false)
        { return false; }
        const auto Definition = ck_test_usf_toon_contract::MakeDefinition(GeneratedMasters.Get_PackageRoot());
        const auto PackagePath = Definition->Get_GeneratedMasterPackagePath();
        if (TestNull(TEXT("Unique test package is absent from memory"), FindPackage(nullptr, *PackagePath)) == false ||
            TestFalse(TEXT("Unique test package is absent from disk"), FPackageName::DoesPackageExist(PackagePath)) == false)
        { return false; }
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
        const auto Definition = ck_test_usf_toon_contract::MakeDefinition(GeneratedMasters.Get_PackageRoot());
        const auto PackagePath = Definition->Get_GeneratedMasterPackagePath();
        if (TestNull(TEXT("Unique unsupported-engine fixture package is absent from memory"), FindPackage(nullptr, *PackagePath)) == false ||
            TestFalse(TEXT("Unique unsupported-engine fixture package is absent from disk"), FPackageName::DoesPackageExist(PackagePath)) == false)
        { return false; }
        const auto Marker = FString{TEXT("Unsupported shading model for this engine")};
        auto Contract = ck_test_usf_toon_contract::MakeUnresolvedContract();
        const auto OriginalContract = Contract;
        auto ContractErrors = TArray<FString>{};
        TestFalse(TEXT("Engine without native Toon rejects the valid source-declared Toon contract"),
            ck::usf_editor::Resolve_ShadingModelContract(*Definition, Contract, ContractErrors));
        TestTrue(TEXT("Native Toon capability rejection is explicit"), ContractErrors.Contains(Marker));
        TestTrue(TEXT("Native Toon capability rejection leaves every contract field unchanged"),
            ck_test_usf_toon_contract::Get_IsSameContract(Contract, OriginalContract));
        const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());
        TestFalse(TEXT("Engine without native Toon rejects valid Toon authoring"), Validation.Get_IsValid());
        TestTrue(TEXT("Authoring validation reports missing native Toon capability"), Validation.Errors.Contains(Marker));
        AddExpectedError(*Marker, EAutomationExpectedErrorFlags::Contains, 1);
        TestNull(TEXT("Engine without native Toon generates no fallback material"),
            ck::usf_editor::Generate_LookMaterial(Definition.Get()));
        TestNull(TEXT("Native Toon capability rejection creates no package in memory"), FindPackage(nullptr, *PackagePath));
        TestFalse(TEXT("Native Toon capability rejection writes no package"), FPackageName::DoesPackageExist(PackagePath));
        return true;
    #endif
    }();
    const auto CleanupSucceeded = GeneratedMasters.Close();
    return Succeeded && CleanupSucceeded;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ToonRejectsUnsupportedAuthoring,
    "CkTests.UnitTests.CkUsf.ToonRejectsUnsupportedAuthoringAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ToonRejectsUnsupportedAuthoring::RunTest(const FString& Parameters) -> bool
{
    auto GeneratedMasters = ck_test_usf::FGeneratedMasterScope{*this};
    if (!GeneratedMasters.Initialize()) { return false; }
    const auto Succeeded = [&]() -> bool
    {
        const auto CaseCount = ck_test_usf_toon_contract::NativeToonSupported ? 2 : 3;
        const auto EngineMarker = FString{TEXT("Unsupported shading model for this engine")};
        const auto DomainMarker = FString{TEXT("Toon requires the SurfaceLit domain")};
        const auto BlendMarker = FString{TEXT("Toon and SingleLayerWater require Opaque blend")};
        if (ck_test_usf_toon_contract::NativeToonSupported)
        { AddExpectedError(*DomainMarker, EAutomationExpectedErrorFlags::Contains, 1); }
        else
        { AddExpectedError(*EngineMarker, EAutomationExpectedErrorFlags::Contains, CaseCount); }
        AddExpectedError(*BlendMarker, EAutomationExpectedErrorFlags::Contains, 1);
        for (auto Case = 0; Case != CaseCount; ++Case)
        {
            const auto Definition = ck_test_usf_toon_contract::MakeDefinition(GeneratedMasters.Get_PackageRoot());
            if (Case == 0)
            {
                Definition->_Domain = ECk_Usf_Domain::SurfaceUnlit;
                // Isolate the domain rejection from SurfaceUnlit's inherited translucent blend.
                Definition->_BlendMode = ECk_Usf_BlendMode::Opaque;
            }
            else if (Case == 1)
            { Definition->_BlendMode = ECk_Usf_BlendMode::Translucent; }
            const auto Marker = NOT ck_test_usf_toon_contract::NativeToonSupported
                ? EngineMarker : Case == 0 ? DomainMarker : BlendMarker;
            auto Contract = ck_test_usf_toon_contract::MakeUnresolvedContract();
            const auto OriginalContract = Contract;
            auto ContractErrors = TArray<FString>{};
            const auto Resolved = ck::usf_editor::Resolve_ShadingModelContract(*Definition, Contract, ContractErrors);
            if (Case == 0 && ck_test_usf_toon_contract::NativeToonSupported)
            {
                TestTrue(TEXT("Domain fixture resolves its source-declared Toon model"), Resolved);
                TestTrue(TEXT("Domain fixture isolates an opaque source-declared Toon contract"),
                    Contract.Model == ECk_Usf_ShadingModel::Toon && Contract.SourceOverride && Contract.BlendMode == BLEND_Opaque);
            }
            else
            {
                TestFalse(TEXT("Unsupported Toon is rejected by contract resolution"), Resolved);
                TestTrue(TEXT("Rejected contract resolution leaves its output unchanged"),
                    ck_test_usf_toon_contract::Get_IsSameContract(Contract, OriginalContract));
            }
            if (NOT ck_test_usf_toon_contract::NativeToonSupported)
            {
                TestTrue(TEXT("Contract resolution explicitly rejects the missing native engine model"),
                    ContractErrors.Contains(Marker));
                if (Case == 1)
                {
                    TestTrue(TEXT("Unsupported engine still reports incompatible Toon blending"), ContractErrors.Contains(BlendMarker));
                }
            }
            const auto Path = Definition->Get_GeneratedMasterPackagePath();
            if (TestNull(TEXT("Unique rejection fixture package is absent from memory"), FindPackage(nullptr, *Path)) == false ||
                TestFalse(TEXT("Unique rejection fixture package is absent from disk"), FPackageName::DoesPackageExist(Path)) == false)
            { return false; }
            const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());
            TestFalse(TEXT("Unsupported Toon declaration is invalid"), Validation.Get_IsValid());
            TestTrue(TEXT("Unsupported Toon declaration reports the specific contract violation"),
                Validation.Errors.ContainsByPredicate([&](const FString& InError) { return InError.Contains(Marker); }));
            TestNull(TEXT("Production generator rejects unsupported Toon authoring"),
                ck::usf_editor::Generate_LookMaterial(Definition.Get()));
            TestNull(TEXT("Rejection creates no material package in memory"), FindPackage(nullptr, *Path));
            TestFalse(TEXT("Rejection writes no material package"), FPackageName::DoesPackageExist(Path));
        }
        return true;
    }();
    const auto CleanupSucceeded = GeneratedMasters.Close();
    return Succeeded && CleanupSucceeded;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_ToonReservedNames,
    "CkTests.UnitTests.CkUsf.ToonReservedOutputNamesAreScoped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_ToonReservedNames::RunTest(const FString& Parameters) -> bool
{
    auto GeneratedMasters = ck_test_usf::FGeneratedMasterScope{*this};
    if (!GeneratedMasters.Initialize()) { return false; }
    const auto Succeeded = [&]() -> bool
    {
        const auto RejectionMarker = ck_test_usf_toon_contract::NativeToonSupported
            ? FString{TEXT("collides with the generated Toon output")}
            : FString{TEXT("Unsupported shading model for this engine")};
        AddExpectedError(*RejectionMarker, EAutomationExpectedErrorFlags::Contains, 2);
        for (const auto Name : {FName{TEXT("ToonThreshold")}, FName{TEXT("toonsoftness")}})
        {
            const auto Definition = ck_test_usf_toon_contract::MakeDefinition(GeneratedMasters.Get_PackageRoot());
            // Toon.ush declares Toon in its header, which takes precedence over the asset model.
            // RimGlow has no model declaration and accepts the same vector/scalar parameter types.
            Definition->_UshIncludePath = TEXT("/CkUsf/Looks/RimGlow.ush");
            Definition->_UshFunctionName = TEXT("CkUsf_Look_RimGlow");
            Definition->_Parameters.SetNum(2);
            Definition->_Parameters[1]._Name = Name;
            Definition->_ShadingModel = ECk_Usf_ShadingModel::DefaultLit;
            auto Contract = ck::usf_editor::FShadingModelContract{};
            auto ContractErrors = TArray<FString>{};
            TestTrue(TEXT("Non-Toon fixture resolves its asset-selected contract"),
                ck::usf_editor::Resolve_ShadingModelContract(*Definition, Contract, ContractErrors));
            TestTrue(TEXT("Non-Toon fixture has no source override and resolves DefaultLit"),
                Contract.Model == ECk_Usf_ShadingModel::DefaultLit && !Contract.SourceOverride);
            const auto Compatible = ck::usf_editor::Validate_LookDefinition(Definition.Get());
            for (const auto& Error : Compatible.Errors)
            { AddError(Error); }
            TestTrue(TEXT("Existing non-Toon scalar names remain valid"), Compatible.Get_IsValid());

            Definition->_ShadingModel = ECk_Usf_ShadingModel::Toon;
            Contract = ck_test_usf_toon_contract::MakeUnresolvedContract();
            const auto OriginalContract = Contract;
            ContractErrors.Reset();
            const auto Resolved = ck::usf_editor::Resolve_ShadingModelContract(*Definition, Contract, ContractErrors);
            if (ck_test_usf_toon_contract::NativeToonSupported)
            {
                TestTrue(TEXT("Toon fixture resolves its asset-selected contract"), Resolved);
                TestTrue(TEXT("Reserved-name scope is tested against the resolved Toon model"),
                    Contract.Model == ECk_Usf_ShadingModel::Toon && !Contract.SourceOverride);
            }
            else
            {
                TestFalse(TEXT("Asset-selected Toon is rejected without native engine support"), Resolved);
                TestTrue(TEXT("Missing native engine support is reported explicitly"), ContractErrors.Contains(RejectionMarker));
                TestTrue(TEXT("Unsupported engine leaves every output contract field unchanged"),
                    ck_test_usf_toon_contract::Get_IsSameContract(Contract, OriginalContract));
            }
            const auto Rejected = ck::usf_editor::Validate_LookDefinition(Definition.Get());
            TestFalse(TEXT("Unsupported Toon declaration is invalid"), Rejected.Get_IsValid());
            TestTrue(TEXT("Validation reports the applicable Toon contract violation"),
                Rejected.Errors.ContainsByPredicate([&](const FString& InError) { return InError.Contains(RejectionMarker); }));
            const auto Path = Definition->Get_GeneratedMasterPackagePath();
            if (TestNull(TEXT("Unique reserved-name fixture package is absent from memory"), FindPackage(nullptr, *Path)) == false ||
                TestFalse(TEXT("Unique reserved-name fixture package is absent from disk"), FPackageName::DoesPackageExist(Path)) == false)
            { return false; }
            TestNull(TEXT("Reserved Toon parameter prevents generation"),
                ck::usf_editor::Generate_LookMaterial(Definition.Get()));
            TestNull(TEXT("Reserved-name rejection creates no package"), FindPackage(nullptr, *Path));
            TestFalse(TEXT("Reserved-name rejection writes no package"), FPackageName::DoesPackageExist(Path));
        }
        return true;
    }();
    const auto CleanupSucceeded = GeneratedMasters.Close();
    return Succeeded && CleanupSucceeded;
}

#endif // WITH_EDITOR
