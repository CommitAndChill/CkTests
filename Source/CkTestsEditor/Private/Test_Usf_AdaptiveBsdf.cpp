#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"
#include "CkUsfEditor/Generator/CkUsf_ShadingModelContract.h"
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "Rendering/SubstrateMaterialShared.h"
#include "ShaderCore.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_adaptive
{
    auto Drain() -> void
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr) { GShaderCompilingManager->FinishAllCompilation(); }
        FlushRenderingCommands();
    }

    auto SameInput(const FExpressionInput& A, const FExpressionInput& B) -> bool
    {
        return A.Expression == B.Expression && A.OutputIndex == B.OutputIndex && A.Mask == B.Mask
            && A.MaskR == B.MaskR && A.MaskG == B.MaskG && A.MaskB == B.MaskB && A.MaskA == B.MaskA;
    }

    struct FFixture
    {
        FScopedDisableValidateOnSave DisableValidation;
        FString Root, Directory, VirtualRoot, Source;
        FString Helper = TEXT("#pragma once\nfloat CkUsfAdaptiveGain() { return 1.0; }\n");
        TStrongObjectPtr<UCkUsf_LookDefinition> Definition;
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;

        explicit FFixture(ECk_Usf_SurfaceGraph Graph, bool BaseGlints = false)
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/CkUsfAdaptive_") + Id + TEXT("/");
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfAdaptive") / Id);
            VirtualRoot = TEXT("/CkUsf/Adaptive_") + Id;
            IFileManager::Get().MakeDirectory(*(Directory / TEXT("Packages")), true);
            FPackageName::RegisterMountPoint(Root, Directory / TEXT("Packages/"));
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
            Definition.Reset(NewObject<UCkUsf_LookDefinition>());
            Definition->_LookName = TEXT("AdaptiveFixture");
            Definition->_GeneratedPackageRoot = Root.LeftChop(1);
            Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
            Definition->_SurfaceGraph = Graph;
            Definition->_DirectionalShading = true;
            Definition->_AdaptiveBaseGlints = BaseGlints;
            Definition->_UshIncludePath = VirtualRoot / TEXT("Entry.ush");
            Definition->_UshFunctionName = TEXT("CkUsf_AdaptiveFixture");
            auto Gain = FCk_Usf_ParamDesc{};
            Gain._Name = TEXT("Gain");
            Gain._DefaultScalar = 0.6f;
            Definition->_Parameters = {Gain};
            const auto Vertical = Graph == ECk_Usf_SurfaceGraph::AdaptiveVertical;
            const auto Kind = Vertical ? FString{TEXT("AdaptiveVertical")} : FString{TEXT("AdaptiveMix")};
            Source = TEXT("// @ckusf surface_graph ") + Kind
                + TEXT("\n#pragma once\n#include \"/CkUsf/Common.ush\"\n#include \"") + VirtualRoot + TEXT("/Nested.ush\"\nFCkUsf_")
                + Kind + TEXT("Output CkUsf_AdaptiveFixture(FCkUsf_SurfaceInput In, float Gain) { FCkUsf_") + Kind
                + TEXT("Output O = CkUsf_Default") + Kind + TEXT("(); ")
                + TEXT("O.ADiffuseAlbedo = float3(0.08,0.02,0.01); O.AF0 = float3(0.04,0.04,0.04); O.ARoughness = 0.22; ")
                + TEXT("O.BDiffuseAlbedo = float3(0.01,0.03,0.08); O.BF0 = float3(0.65,0.24,0.1); O.BRoughness = 0.48; ")
                + TEXT("O.AAnisotropy = 0.55; O.ATangent = float3(1,0,0); O.BAnisotropy = 0.3; O.BTangent = float3(0,1,0); ")
                + TEXT("O.AEmissiveColor = Gain * CkUsfAdaptiveGain() * float3(0.02,0.01,0.0); ")
                + (Vertical ? TEXT("O.ATransmittanceColor = float3(0.8,0.9,0.95); O.ThicknessCm = 0.02; ") : TEXT("O.MixWeight = 0.4; "))
                + (BaseGlints ? TEXT("O.BGlintValue = 0.35; O.BGlintUV = In.UV * 3.0; ") : TEXT(""))
                + TEXT("return O; }\n");
        }

        ~FFixture()
        {
            Write(Source, Helper);
            Drain();
            if (Material.IsValid())
            {
                for (const auto& Expression : Material->GetExpressions())
                {
                    const auto Custom = TObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(Expression)};
                    if (Custom != nullptr) { Custom->IncludeFilePaths.Reset(); }
                }
                Material->UpdateCachedExpressionData();
                Material->ClearFlags(RF_Public | RF_Standalone);
                Material->MarkAsGarbage();
            }
            // Process-global shader caches may still refer to these unique Saved sources/mappings.
            FPackageName::UnRegisterMountPoint(Root, Directory / TEXT("Packages/"));
        }

        auto Write(const FString& Entry, const FString& Nested) const -> bool
        {
            return FFileHelper::SaveStringToFile(Entry, *(Directory / TEXT("Entry.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
                && FFileHelper::SaveStringToFile(Nested, *(Directory / TEXT("Nested.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        auto Generate() -> bool
        {
            if (NOT Write(Source, Helper)) { return false; }
            Material.Reset(ck::usf_editor::Generate_LookMaterial(Definition.Get()));
            if (NOT Material.IsValid()) { return false; }
            Mid.Reset(UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage()));
            if (NOT Mid.IsValid()) { return false; }
            Mid->SetScalarParameterValue(TEXT("Gain"), 0.75f);
            return true;
        }

        template<typename T> auto Find() const -> TWeakObjectPtr<T>
        {
            if (Material.IsValid())
            {
                for (const auto& Expression : Material->GetExpressions())
                { if (Expression->IsA<T>()) { return Cast<T>(Expression); } }
            }
            return {};
        }
    };

    // In the baseline profile these tests actively prove rejection, rather than silently skipping rendering.
    auto CheckUnsupportedProfile(FAutomationTestBase& Test) -> bool
    {
        const auto Support = ck::usf_editor::Get_SubstrateShadingModelSupport();
        if (Support.Enabled && NOT Support.BlendableGBuffer && Support.MaxClosuresPerPixel >= 2) { return false; }
        for (const auto Graph : {ECk_Usf_SurfaceGraph::AdaptiveVertical, ECk_Usf_SurfaceGraph::AdaptiveMix})
        {
            auto Fixture = FFixture{Graph};
            Test.TestTrue(TEXT("Write unsupported-profile fixture"), Fixture.Write(Fixture.Source, Fixture.Helper));
            const auto Result = ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get());
            Test.TestFalse(TEXT("Selected renderer rejects Adaptive definition before generation"), Result.Get_IsValid());
            Test.TestTrue(TEXT("Rejection identifies Adaptive renderer requirement"), Result.Errors.ContainsByPredicate(
                [](const FString& Error) { return Error.Contains(TEXT("Adaptive GBuffer")); }));
            Test.TestFalse(TEXT("Rejected validation creates no material or MID"), Fixture.Material.IsValid() || Fixture.Mid.IsValid());
        }
        Test.AddInfo(TEXT("Selected profile cannot retain two closures: explicit admission rejection verified."));
        return true;
    }

    auto CheckUnsupportedGlints(FAutomationTestBase& Test) -> bool
    {
        const auto Support = ck::usf_editor::Get_SubstrateShadingModelSupport();
        if (Support.Enabled && NOT Support.BlendableGBuffer && Support.MaxClosuresPerPixel >= 2 && Support.GlintsEnabled)
        { return false; }
        auto Fixture = FFixture{ECk_Usf_SurfaceGraph::AdaptiveVertical, true};
        Test.TestTrue(TEXT("Write glints fixture for unsupported renderer"), Fixture.Write(Fixture.Source, Fixture.Helper));
        const auto Result = ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get());
        Test.TestFalse(TEXT("Unsupported renderer explicitly rejects glints before generation"), Result.Get_IsValid());
        Test.TestFalse(TEXT("Glints renderer rejection supplies diagnostic errors"), Result.Errors.IsEmpty());
        Test.TestFalse(TEXT("Rejected glints admission creates no material or MID"), Fixture.Material.IsValid() || Fixture.Mid.IsValid());
        Test.AddInfo(TEXT("Selected renderer cannot retain native glints: explicit admission rejection verified."));
        return true;
    }

    struct FSnapshot
    {
        FGuid State;
        FString Code;
        TArray<FCustomOutput> Outputs;
        FExpressionInput Front;
        TArray<TWeakObjectPtr<UMaterialExpression>> Nodes;
        TArray<TArray<FExpressionInput>> Inputs;

        explicit FSnapshot(const FFixture& Fixture)
        {
            State = Fixture.Material->StateId;
            Code = Fixture.Find<UMaterialExpressionCustom>()->Code;
            Outputs = Fixture.Find<UMaterialExpressionCustom>()->AdditionalOutputs;
            Front = Fixture.Material->GetEditorOnlyData()->FrontMaterial;
            for (const auto& Node : Fixture.Material->GetExpressions())
            {
                Nodes.Add(Node);
                auto Pins = TArray<FExpressionInput>{};
                for (const auto Pin : Node->GetInputsView()) { if (Pin != nullptr) { Pins.Add(*Pin); } }
                Inputs.Add(Pins);
            }
        }

        auto Check(FAutomationTestBase& Test, const FFixture& Fixture, bool SameState) const -> void
        {
            if (SameState) { Test.TestEqual(TEXT("Rejected reload leaves StateId unchanged"), Fixture.Material->StateId, State); }
            Test.TestEqual(TEXT("Numeric reload retains generated Custom code"), Fixture.Find<UMaterialExpressionCustom>()->Code, Code);
            const auto& CurrentOutputs = Fixture.Find<UMaterialExpressionCustom>()->AdditionalOutputs;
            Test.TestEqual(TEXT("Reload retains Custom output count"), CurrentOutputs.Num(), Outputs.Num());
            for (auto Index = 0; Index < CurrentOutputs.Num() && Index < Outputs.Num(); ++Index)
            { Test.TestTrue(TEXT("Reload retains Custom output names/types"), CurrentOutputs[Index].OutputName == Outputs[Index].OutputName
                && CurrentOutputs[Index].OutputType == Outputs[Index].OutputType); }
            Test.TestTrue(TEXT("Reload retains FrontMaterial wiring"), SameInput(Front, Fixture.Material->GetEditorOnlyData()->FrontMaterial));
            Test.TestEqual(TEXT("Reload retains expression count"), Fixture.Material->GetExpressions().Num(), Nodes.Num());
            for (auto Index = 0; Index < Nodes.Num() && Index < Fixture.Material->GetExpressions().Num(); ++Index)
            {
                const auto Node = Fixture.Material->GetExpressions()[Index];
                Test.TestTrue(TEXT("Reload retains expression identity"), Node == Nodes[Index].Get());
                auto Pins = TArray<FExpressionInput>{};
                for (const auto Pin : Node->GetInputsView()) { if (Pin != nullptr) { Pins.Add(*Pin); } }
                Test.TestEqual(TEXT("Reload retains input count"), Pins.Num(), Inputs[Index].Num());
                for (auto Pin = 0; Pin < Pins.Num() && Pin < Inputs[Index].Num(); ++Pin)
                { Test.TestTrue(TEXT("Reload retains every input and mask"), SameInput(Pins[Pin], Inputs[Index][Pin])); }
            }
            Test.TestTrue(TEXT("Reload retains MID parent and authored override"), Fixture.Mid->Parent == Fixture.Material.Get()
                && FMath::IsNearlyEqual(Fixture.Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
        }
    };

    auto CheckCompile(FAutomationTestBase& Test, FFixture& Fixture) -> void
    {
        Fixture.Material->EnsureIsComplete();
        Drain();
        if (NOT Test.TestNotNull(TEXT("Adaptive material has real RHI resource"), Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform))) { return; }
        const auto& Resource = *Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform);
        for (const auto& Error : Resource.GetCompileErrors()) { Test.AddError(Error); }
        Test.TestTrue(TEXT("Adaptive game-thread shader map complete"), Resource.IsCompilationFinished()
            && Resource.IsGameThreadShaderMapComplete() && Resource.GetGameThreadShaderMap() != nullptr);
        auto RenderComplete = false;
        ENQUEUE_RENDER_COMMAND(CkUsfAdaptiveComplete)([&Resource, &RenderComplete](FRHICommandListImmediate&)
            { RenderComplete = Resource.IsRenderingThreadShaderMapComplete(); });
        FlushRenderingCommands();
        Test.TestTrue(TEXT("Adaptive render-thread shader map complete"), RenderComplete);
        Test.TestEqual(TEXT("Compiled material retains exactly two closures"), static_cast<int32>(Resource.MaterialGetSubstrateClosureCount_GameThread()), 2);
        Test.TestEqual(TEXT("Compiled native Glint feature matches explicit lower-layer opt-in"),
            EnumHasAnyFlags(Resource.MaterialGetSubstrateMaterialBsdfFeatures_GameThread(), ESubstrateBsdfFeature::Glint),
            Fixture.Definition->_AdaptiveBaseGlints);
        if (Fixture.Definition->_AdaptiveBaseGlints)
        {
            Test.TestTrue(TEXT("Compiled glints retain Complex Special material classification"),
                static_cast<uint32>(Resource.MaterialGetSubstrateMaterialType_GameThread()) == static_cast<uint32>(ESubstrateTileType::EComplexSpecial));
        }
        const auto UintCount = Resource.MaterialGetSubstrateUintPerPixel_GameThread();
        Test.TestTrue(TEXT("Retained Adaptive payload is nonempty"), UintCount > 0);
        Test.AddInfo(FString::Printf(TEXT("Adaptive graph %d: retained closures=%u, payload=%u uints (%u bytes/pixel); not a GPU timing claim."),
            static_cast<int32>(Fixture.Definition->_SurfaceGraph), static_cast<uint32>(Resource.MaterialGetSubstrateClosureCount_GameThread()),
            static_cast<uint32>(UintCount), static_cast<uint32>(UintCount) * 4u));
    }

    auto CheckPrepared(FAutomationTestBase& Test, FFixture& Fixture, bool Expected) -> void
    {
        const auto Before = FSnapshot{Fixture};
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        Test.TestEqual(TEXT("Adaptive preparation matches expected eligibility"),
            ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors), Expected);
        if (NOT Expected)
        {
            Test.TestFalse(TEXT("Rejected prepared operation is not runnable"), Prepared.Custom.IsValid());
            Test.TestFalse(TEXT("Rejected topology supplies an actionable error"), Errors.IsEmpty());
        }
        else { for (const auto& Error : Errors) { Test.AddError(Error); } }
        Before.Check(Test, Fixture, true);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdaptiveEligibility, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.RendererEligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdaptiveEligibility::RunTest(const FString&) -> bool
{
    using namespace ck::usf_editor;
    auto Support = FSubstrateShadingModelSupport{};
    Support.Enabled = true;
    Support.MaxClosuresPerPixel = 2;
    for (const auto Graph : {ECk_Usf_SurfaceGraph::AdaptiveVertical, ECk_Usf_SurfaceGraph::AdaptiveMix})
    {
        auto Contract = FShadingModelContract{};
        Contract.SurfaceGraph = Graph;
        for (auto Case = 0; Case < 4; ++Case)
        {
            auto Candidate = Support;
            if (Case == 1) { Candidate.Enabled = false; }
            if (Case == 2) { Candidate.BlendableGBuffer = true; }
            if (Case == 3) { Candidate.MaxClosuresPerPixel = 1; }
            auto Errors = TArray<FString>{};
            TestEqual(TEXT("Adaptive requires enabled renderer, Adaptive GBuffer, and two-closure budget"),
                Validate_SubstrateShadingModelSupport(Contract, Candidate, Errors), Case == 0);
            TestEqual(TEXT("Renderer admission failure supplies errors"), Errors.IsEmpty(), Case == 0);
        }
    }
    for (const auto Graph : {ECk_Usf_SurfaceGraph::Legacy, ECk_Usf_SurfaceGraph::NativeSlab,
        ECk_Usf_SurfaceGraph::NativeToon, ECk_Usf_SurfaceGraph::NativeSimpleClearCoat})
    {
        auto Contract = FShadingModelContract{};
        Contract.SurfaceGraph = Graph;
        auto Errors = TArray<FString>{};
        TestFalse(TEXT("Existing graph kinds retain their Blendable-only contract"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    }
    ck_test_usf_adaptive::CheckUnsupportedProfile(*this);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdaptiveGlintsEligibility, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.BaseGlintsAdmissionAndOptionalOutputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdaptiveGlintsEligibility::RunTest(const FString&) -> bool
{
    using namespace ck::usf_editor;
    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
    TestFalse(TEXT("Existing look definitions default to glints disabled"), Definition->_AdaptiveBaseGlints);
    auto Contract = FShadingModelContract{};
    Contract.SurfaceGraph = ECk_Usf_SurfaceGraph::AdaptiveVertical;
    TestFalse(TEXT("Existing contracts default to glints disabled"), Contract.AdaptiveBaseGlints);
    const auto OffOutputs = Get_ShadingModelOutputs(Contract);
    TestFalse(TEXT("Non-opt-in Custom output contract contains no glints outputs"), OffOutputs.ContainsByPredicate(
        [](const FShadingModelOutput& Output) { return Output.Name.Contains(TEXT("Glint")); }));
    auto Support = FSubstrateShadingModelSupport{};
    Support.Enabled = true;
    Support.MaxClosuresPerPixel = 2;
    auto Errors = TArray<FString>{};
    TestTrue(TEXT("Existing AdaptiveVertical stays eligible with renderer glints disabled"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    Contract.AdaptiveBaseGlints = true;
    Errors.Reset();
    TestFalse(TEXT("Glints opt-in rejects renderer feature disabled"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    TestTrue(TEXT("Disabled glints error names required feature"), Errors.ContainsByPredicate(
        [](const FString& Error) { return Error.Contains(TEXT("glint"), ESearchCase::IgnoreCase); }));
    Support.GlintsEnabled = true;
    Errors.Reset();
    TestTrue(TEXT("Native lower glints admit an enabled Adaptive renderer"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    TestTrue(TEXT("Accepted glints admission leaves no errors"), Errors.IsEmpty());
    const auto OnOutputs = Get_ShadingModelOutputs(Contract);
    TestEqual(TEXT("Opt-in adds exactly two Custom outputs"), OnOutputs.Num(), OffOutputs.Num() + 2);
    TestTrue(TEXT("Glint density output is scalar"), OnOutputs.ContainsByPredicate([](const FShadingModelOutput& Output)
        { return Output.Name == TEXT("CkUsf_BGlintValue") && Output.Type == CMOT_Float1; }));
    TestTrue(TEXT("Glint UV output is float2"), OnOutputs.ContainsByPredicate([](const FShadingModelOutput& Output)
        { return Output.Name == TEXT("CkUsf_BGlintUV") && Output.Type == CMOT_Float2; }));
    auto Fixture = ck_test_usf_adaptive::FFixture{ECk_Usf_SurfaceGraph::AdaptiveVertical, true};
    if (NOT TestTrue(TEXT("Write authored glints fixture"), Fixture.Write(Fixture.Source, Fixture.Helper))) { return false; }
    auto Blendable = Support;
    Blendable.BlendableGBuffer = true;
    Blendable.MaxClosuresPerPixel = 1;
    Blendable.GlintsEnabled = false;
    const auto Opposite = Classify_LookForGeneration(Fixture.Definition.Get(), Blendable);
    TestTrue(TEXT("Valid glints source remains other-profile authoring under Blendable"), Opposite.Admission == ELookGenerationAdmission::OtherRendererProfile);
    TestTrue(TEXT("Other-profile glints exclusion has no authoring errors"), Opposite.Validation.Errors.IsEmpty());
    auto DisabledGlints = Support;
    DisabledGlints.GlintsEnabled = false;
    TestTrue(TEXT("Same-profile glints disabled is invalid rather than excluded"),
        Classify_LookForGeneration(Fixture.Definition.Get(), DisabledGlints).Admission == ELookGenerationAdmission::Invalid);
    const auto WithoutHeader = Fixture.Source.Replace(TEXT("// @ckusf surface_graph AdaptiveVertical\n"), TEXT(""));
    if (NOT TestTrue(TEXT("Write asset-selected source for authoring graph restrictions"), Fixture.Write(WithoutHeader, Fixture.Helper))) { return false; }
    for (const auto Graph : {ECk_Usf_SurfaceGraph::Legacy, ECk_Usf_SurfaceGraph::NativeSlab,
        ECk_Usf_SurfaceGraph::NativeToon, ECk_Usf_SurfaceGraph::NativeSimpleClearCoat,
        ECk_Usf_SurfaceGraph::AdaptiveMix, ECk_Usf_SurfaceGraph::NativeTranslucentSlab,
        ECk_Usf_SurfaceGraph::NativeRefractiveSlab})
    {
        Fixture.Definition->_SurfaceGraph = Graph;
        Errors.Reset();
        auto Resolved = FShadingModelContract{};
        TestFalse(TEXT("Authoring lower glints rejects every graph except AdaptiveVertical"), Resolve_ShadingModelContract(*Fixture.Definition, Resolved, Errors));
        TestTrue(TEXT("Wrong graph rejection specifically identifies glints"), Errors.ContainsByPredicate(
            [](const FString& Error) { return Error.Contains(TEXT("glint"), ESearchCase::IgnoreCase); }));
    }
    ck_test_usf_adaptive::CheckUnsupportedGlints(*this);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdaptiveBulkAdmission, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.BulkAdmissionValidatesOppositeProfileAuthoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdaptiveBulkAdmission::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_adaptive;
    using namespace ck::usf_editor;
    auto Adaptive = FSubstrateShadingModelSupport{};
    Adaptive.Enabled = true;
    Adaptive.MaxClosuresPerPixel = 2;
    auto Blendable = Adaptive;
    Blendable.BlendableGBuffer = true;
    Blendable.MaxClosuresPerPixel = 1; // Effective Blendable budget is one even on an Adaptive-capable platform.
    auto Fixture = FFixture{ECk_Usf_SurfaceGraph::AdaptiveMix};
    const auto AdaptiveSource = Fixture.Source;
    const auto SlabSource = FString{TEXT("// @ckusf surface_graph Slab\n#pragma once\n#include \"/CkUsf/Common.ush\"\n"
        "FCkUsf_SlabOutput CkUsf_AdaptiveFixture(FCkUsf_SurfaceInput In, float Gain) { "
        "FCkUsf_SlabOutput O = CkUsf_DefaultSlab(); O.DiffuseAlbedo = float3(0.1,0.2,0.3) * Gain; return O; }\n")};
    const auto CheckAdmission = [&](const FSubstrateShadingModelSupport& Support, ELookGenerationAdmission Expected,
        ECk_Usf_SurfaceGraph Graph)
    {
        const auto Result = Classify_LookForGeneration(Fixture.Definition.Get(), Support);
        TestTrue(TEXT("Bulk classifier returns the exact expected category"), Result.Admission == Expected);
        TestTrue(TEXT("Bulk classifier resolves source header before asset graph enum"), Result.ResolvedGraph == Graph);
        if (Expected == ELookGenerationAdmission::Invalid)
        { TestFalse(TEXT("Invalid look carries validation errors rather than an exclusion"), Result.Validation.Errors.IsEmpty()); }
        else
        {
            for (const auto& Error : Result.Validation.Errors) { AddError(Error); }
            TestTrue(TEXT("Both compatible and opposite-profile content pass complete authoring validation"), Result.Validation.Get_IsValid());
            if (Expected == ELookGenerationAdmission::OtherRendererProfile)
            {
                TestFalse(TEXT("Opposite profile is named"), Result.RequiredProfile.IsEmpty());
                TestFalse(TEXT("Opposite profile has an explicit reason"), Result.Reason.IsEmpty());
            }
        }
        return Result;
    };

    // The asset enum intentionally contradicts the source header in both directions.
    Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeSlab;
    if (NOT TestTrue(TEXT("Write Adaptive source overriding Slab asset enum"), Fixture.Write(AdaptiveSource, Fixture.Helper))) { return false; }
    CheckAdmission(Blendable, ELookGenerationAdmission::OtherRendererProfile, ECk_Usf_SurfaceGraph::AdaptiveMix);
    CheckAdmission(Adaptive, ELookGenerationAdmission::Compatible, ECk_Usf_SurfaceGraph::AdaptiveMix);
    auto Insufficient = Adaptive;
    Insufficient.MaxClosuresPerPixel = 1;
    CheckAdmission(Insufficient, ELookGenerationAdmission::Invalid, ECk_Usf_SurfaceGraph::AdaptiveMix);
    auto Disabled = Adaptive;
    Disabled.Enabled = false;
    CheckAdmission(Disabled, ELookGenerationAdmission::Invalid, ECk_Usf_SurfaceGraph::AdaptiveMix);

    // A mode mismatch must not short-circuit the function signature or descriptor checks.
    if (NOT TestTrue(TEXT("Write malformed opposite-profile function signature"), Fixture.Write(
        AdaptiveSource.Replace(TEXT("float Gain)"), TEXT("float2 Gain)")), Fixture.Helper))) { return false; }
    CheckAdmission(Blendable, ELookGenerationAdmission::Invalid, ECk_Usf_SurfaceGraph::AdaptiveMix);
    if (NOT TestTrue(TEXT("Restore valid opposite-profile function"), Fixture.Write(AdaptiveSource, Fixture.Helper))) { return false; }
    const auto DuplicateParameter = Fixture.Definition->_Parameters[0];
    Fixture.Definition->_Parameters.Add(DuplicateParameter);
    CheckAdmission(Blendable, ELookGenerationAdmission::Invalid, ECk_Usf_SurfaceGraph::AdaptiveMix);
    Fixture.Definition->_Parameters.SetNum(1);
    if (NOT TestTrue(TEXT("Write contradictory opposite-profile declarations"), Fixture.Write(
        TEXT("// @ckusf shading_model DefaultLit\n") + AdaptiveSource, Fixture.Helper))) { return false; }
    const auto Contradictory = Classify_LookForGeneration(Fixture.Definition.Get(), Blendable);
    TestTrue(TEXT("Malformed source header is Invalid, never excluded by asset enum"), Contradictory.Admission == ELookGenerationAdmission::Invalid);
    TestFalse(TEXT("Malformed header produces errors"), Contradictory.Validation.Errors.IsEmpty());

    Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::AdaptiveMix;
    if (NOT TestTrue(TEXT("Write Slab source overriding Adaptive asset enum"), Fixture.Write(SlabSource, Fixture.Helper))) { return false; }
    CheckAdmission(Blendable, ELookGenerationAdmission::Compatible, ECk_Usf_SurfaceGraph::NativeSlab);
    CheckAdmission(Adaptive, ELookGenerationAdmission::OtherRendererProfile, ECk_Usf_SurfaceGraph::NativeSlab);
    if (NOT TestTrue(TEXT("Write malformed Slab on opposite Adaptive profile"), Fixture.Write(
        SlabSource.Replace(TEXT("float Gain)"), TEXT("float2 Gain)")), Fixture.Helper))) { return false; }
    CheckAdmission(Adaptive, ELookGenerationAdmission::Invalid, ECk_Usf_SurfaceGraph::NativeSlab);

    // Bulk exclusion does not authorize direct generation or create even an empty package.
    const auto Actual = Get_SubstrateShadingModelSupport();
    if (NOT TestTrue(TEXT("Direct rejection regression requires enabled Substrate"), Actual.Enabled)) { return false; }
    Fixture.Definition->_SurfaceGraph = Actual.BlendableGBuffer ? ECk_Usf_SurfaceGraph::NativeSlab : ECk_Usf_SurfaceGraph::AdaptiveMix;
    if (NOT TestTrue(TEXT("Write valid source belonging to the other actual profile"), Fixture.Write(
        Actual.BlendableGBuffer ? AdaptiveSource : SlabSource, Fixture.Helper))) { return false; }
    const auto Opposite = Classify_LookForGeneration(Fixture.Definition.Get());
    if (NOT TestTrue(TEXT("Actual selected profile explicitly excludes valid other-profile source"), Opposite.Admission == ELookGenerationAdmission::OtherRendererProfile)) { return false; }
    TestFalse(TEXT("Direct validation still rejects a valid other-profile look"), Validate_LookDefinition(Fixture.Definition.Get()).Get_IsValid());
    const auto PackagePath = Fixture.Definition->Get_GeneratedMasterPackagePath();
    TestNull(TEXT("Fixture package does not exist before strict generation"), FindPackage(nullptr, *PackagePath));
    AddExpectedError(TEXT("require enabled Substrate"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto Rejected = TWeakObjectPtr<UMaterial>{Generate_LookMaterial(Fixture.Definition.Get())};
    TestFalse(TEXT("Direct generation returns no opposite-profile master"), Rejected.IsValid());
    TestNull(TEXT("Direct mode rejection creates no package"), FindPackage(nullptr, *PackagePath));
    TestFalse(TEXT("Direct mode rejection saves no asset"), FPackageName::DoesPackageExist(PackagePath));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdaptiveRetained, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.ExactTopologyAndRetainedClosures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdaptiveRetained::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_adaptive;
    if (CheckUnsupportedProfile(*this)) { return true; }
    if (NOT FApp::CanEverRender()) { AddError(TEXT("Adaptive compile proof requires real RHI")); return false; }
    for (const auto Graph : {ECk_Usf_SurfaceGraph::AdaptiveVertical, ECk_Usf_SurfaceGraph::AdaptiveMix})
    {
        auto Fixture = FFixture{Graph};
        if (NOT TestTrue(TEXT("Generate two-Slab graph"), Fixture.Generate())) { return false; }
        auto Slabs = TArray<TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>>{};
        for (const auto& Node : Fixture.Material->GetExpressions())
        { if (Node->IsA<UMaterialExpressionSubstrateSlabBSDF>()) { Slabs.Add(Cast<UMaterialExpressionSubstrateSlabBSDF>(Node)); } }
        if (NOT TestEqual(TEXT("Exactly two authored Slab nodes"), Slabs.Num(), 2)) { return false; }
        const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
        if (NOT TestTrue(TEXT("Generated Custom exists"), Custom.IsValid())) { return false; }
        TestFalse(TEXT("Existing Adaptive masters retain no glints Custom outputs"), Custom->AdditionalOutputs.ContainsByPredicate(
            [](const FCustomOutput& Output) { return Output.OutputName.ToString().Contains(TEXT("Glint")); }));
        for (const auto Slab : Slabs)
        {
            TestTrue(TEXT("Existing Adaptive Slab glints pins remain disconnected"),
                Slab->GlintValue.Expression == nullptr && Slab->GlintUV.Expression == nullptr);
        }
        auto CheckBinding = [&](const FExpressionInput& Pin, const FName Name, ECustomMaterialOutputType Type)
        {
            const auto Index = Custom->AdditionalOutputs.IndexOfByPredicate([&](const FCustomOutput& Output) { return Output.OutputName == Name; });
            if (NOT TestTrue(TEXT("Named Adaptive Custom output exists"), Index != INDEX_NONE)) { return; }
            TestTrue(TEXT("Adaptive output has exact native scalar/vector type"), Custom->AdditionalOutputs[Index].OutputType == Type);
            auto Expected = FExpressionInput{};
            Expected.Connect(Index + 1, Custom.Get());
            TestTrue(TEXT("Native pin consumes exact output index and masks"), SameInput(Pin, Expected));
        };
        auto Primary = FExpressionInput{};
        Primary.Connect(0, Custom.Get());
        TestTrue(TEXT("Layer A diffuse uses primary Custom output"), SameInput(Slabs[0]->DiffuseAlbedo, Primary));
        for (auto Index = 0; Index < 2; ++Index)
        {
            const auto Prefix = Index == 0 ? FString{TEXT("CkUsf_A")} : FString{TEXT("CkUsf_B")};
            CheckBinding(Slabs[Index]->F0, FName{Prefix + TEXT("F0")}, CMOT_Float3);
            CheckBinding(Slabs[Index]->Roughness, FName{Prefix + TEXT("Roughness")}, CMOT_Float1);
            CheckBinding(Slabs[Index]->Normal, FName{Prefix + TEXT("Normal")}, CMOT_Float3);
            CheckBinding(Slabs[Index]->Anisotropy, FName{Prefix + TEXT("Anisotropy")}, CMOT_Float1);
            CheckBinding(Slabs[Index]->Tangent, FName{Prefix + TEXT("Tangent")}, CMOT_Float3);
        }
        if (Graph == ECk_Usf_SurfaceGraph::AdaptiveVertical)
        {
            const auto Node = Fixture.Find<UMaterialExpressionSubstrateVerticalLayering>();
            const auto Mfp = Fixture.Find<UMaterialExpressionSubstrateTransmittanceToMFP>();
            if (NOT TestTrue(TEXT("Vertical operator and native transmittance conversion exist"), Node.IsValid() && Mfp.IsValid())) { return false; }
            TestFalse(TEXT("Vertical retains BSDF layering, not parameter blending"), Node->bUseParameterBlending);
            TestTrue(TEXT("Vertical preserves A top / B base ordering"), Node->Top.Expression == Slabs[0].Get() && Node->Base.Expression == Slabs[1].Get());
            TestTrue(TEXT("Top uses converted MFP with native SimpleVolume"), Slabs[0]->SSSMFP.Expression == Mfp.Get() && Slabs[0]->SubSurfaceType == MSS_SimpleVolume);
            CheckBinding(Node->Thickness, TEXT("CkUsf_ThicknessCm"), CMOT_Float1);
            CheckBinding(Mfp->Thickness, TEXT("CkUsf_ThicknessCm"), CMOT_Float1);
            CheckBinding(Mfp->TransmittanceColor, TEXT("CkUsf_ATransmittanceColor"), CMOT_Float3);
        }
        else
        {
            const auto Node = Fixture.Find<UMaterialExpressionSubstrateHorizontalMixing>();
            if (NOT TestTrue(TEXT("Horizontal operator exists"), Node.IsValid())) { return false; }
            TestFalse(TEXT("Horizontal retains BSDF mixing, not parameter blending"), Node->bUseParameterBlending);
            TestTrue(TEXT("Mix weights foreground A over background B"), Node->Foreground.Expression == Slabs[0].Get() && Node->Background.Expression == Slabs[1].Get());
            CheckBinding(Node->Mix, TEXT("CkUsf_MixWeight"), CMOT_Float1);
        }
        CheckPrepared(*this, Fixture, true);
        CheckCompile(*this, Fixture);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdaptiveAtomic, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.TopologyPreflightIsAtomic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdaptiveAtomic::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_adaptive;
    if (CheckUnsupportedProfile(*this)) { return true; }
    for (const auto Graph : {ECk_Usf_SurfaceGraph::AdaptiveVertical, ECk_Usf_SurfaceGraph::AdaptiveMix})
    {
        auto Fixture = FFixture{Graph};
        if (NOT TestTrue(TEXT("Generate atomic Adaptive fixture"), Fixture.Generate())) { return false; }
        const auto Vertical = Fixture.Find<UMaterialExpressionSubstrateVerticalLayering>();
        const auto Mix = Fixture.Find<UMaterialExpressionSubstrateHorizontalMixing>();
        if (NOT TestTrue(TEXT("Expected compositing operator exists"), Vertical.IsValid() || Mix.IsValid())) { return false; }
        if (Vertical.IsValid()) { Vertical->bUseParameterBlending = true; } else { Mix->bUseParameterBlending = true; }
        CheckPrepared(*this, Fixture, false);
        if (Vertical.IsValid()) { Vertical->bUseParameterBlending = false; } else { Mix->bUseParameterBlending = false; }
        CheckPrepared(*this, Fixture, true);
        if (Vertical.IsValid()) { Swap(Vertical->Top, Vertical->Base); } else { Swap(Mix->Foreground, Mix->Background); }
        CheckPrepared(*this, Fixture, false);
        if (Vertical.IsValid()) { Swap(Vertical->Top, Vertical->Base); } else { Swap(Mix->Foreground, Mix->Background); }
        auto& Weight = Vertical.IsValid() ? Vertical->Thickness : Mix->Mix;
        const auto SavedWeight = Weight;
        Weight.Expression = nullptr;
        CheckPrepared(*this, Fixture, false);
        Weight = SavedWeight;
        Fixture.Definition->_DirectionalShading = false;
        CheckPrepared(*this, Fixture, false);
        Fixture.Definition->_DirectionalShading = true;
        Fixture.Definition->_SlabFuzz = true;
        CheckPrepared(*this, Fixture, false);
        Fixture.Definition->_SlabFuzz = false;
        const auto Extra = TObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{NewObject<UMaterialExpressionSubstrateSlabBSDF>(Fixture.Material.Get())};
        Extra->Material = Fixture.Material.Get();
        Fixture.Material->GetExpressionCollection().AddExpression(Extra);
        CheckPrepared(*this, Fixture, false);
        Fixture.Material->GetExpressionCollection().RemoveExpression(Extra);
        const auto OriginalGraph = Fixture.Definition->_SurfaceGraph;
        Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::Legacy;
        TestTrue(TEXT("Write valid legacy graph selection for rejection"), Fixture.Write(TEXT("#pragma once\n#include \"/CkUsf/Common.ush\"\n"
            "FCkUsf_SurfaceOutput CkUsf_AdaptiveFixture(FCkUsf_SurfaceInput In, float Gain) { FCkUsf_SurfaceOutput O = CkUsf_DefaultSurface(); return O; }\n"), Fixture.Helper));
        CheckPrepared(*this, Fixture, false);
        Fixture.Definition->_SurfaceGraph = OriginalGraph;
        TestTrue(TEXT("Restore exact Adaptive source graph"), Fixture.Write(Fixture.Source, Fixture.Helper));
        CheckPrepared(*this, Fixture, true);
    }
    return true;
}

namespace ck_test_usf_adaptive
{
    class FReloadSequence : public IAutomationLatentCommand, public FOutputDevice
    {
    public:
        FReloadSequence(FAutomationTestBase& InTest, TSharedRef<FFixture> InA, TSharedRef<FFixture> InB)
            : Test(InTest), A(InA), B(InB), SnapshotA(*InA), SnapshotB(*InB), MidA(InA->Mid.Get()), MidB(InB->Mid.Get()),
              GlintsOptIn(InA->Definition->_AdaptiveBaseGlints)
        { GLog->AddOutputDevice(this); }
        ~FReloadSequence() override
        {
            GLog->RemoveOutputDevice(this);
            const auto Node = A->Find<UMaterialExpressionSubstrateVerticalLayering>();
            if (Node.IsValid()) { Node->bUseParameterBlending = false; }
            A->Definition->_AdaptiveBaseGlints = GlintsOptIn;
            A->Write(A->Source, A->Helper);
            B->Write(B->Source, B->Helper);
        }
        auto Serialize(const TCHAR* Message, ELogVerbosity::Type, const FName&) -> void override
        {
            const auto Text = FString{Message};
            if (Text.Contains(TEXT("CkUsf reload rejected before material changes:"))) { Rejected.Store(true); }
            if (Text.Contains(TEXT("CkUsf reload complete:"))) { Completed.Store(true); }
        }
        auto Update() -> bool override
        {
            if (NOT Started)
            {
                Rejected.Store(false); Completed.Store(false);
                BeforeA = A->Material->StateId; BeforeB = B->Material->StateId;
                // First request includes one valid and one malformed master: neither may be applied.
                if (Stage == 0)
                {
                    if (GlintsOptIn) { A->Definition->_AdaptiveBaseGlints = false; }
                    else { A->Find<UMaterialExpressionSubstrateVerticalLayering>()->bUseParameterBlending = true; }
                }
                const auto EditEntry = Stage == 0 || Stage == 1;
                const auto EditHelper = Stage == 3;
                for (const auto& Fixture : {A, B})
                {
                    if (NOT Test.TestTrue(TEXT("Write exact owned numeric edit/recovery"), Fixture->Write(
                        EditEntry ? Fixture->Source.Replace(TEXT("float3(0.02,0.01,0.0)"), TEXT("float3(0.2,0.01,0.0)"))
                            .Replace(TEXT("O.BGlintValue = 0.35"), TEXT("O.BGlintValue = 0.15"))
                            .Replace(TEXT("In.UV * 3.0"), TEXT("In.UV * 5.0")) : Fixture->Source,
                        EditHelper ? Fixture->Helper.Replace(TEXT("return 1.0"), TEXT("return 0.25")) : Fixture->Helper))) { return true; }
                }
                IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, GEditor->GetEditorWorldContext().World());
                Deadline = FPlatformTime::Seconds() + 90.0;
                Started = true;
                return false;
            }
            const auto Acknowledged = Stage == 0 ? Rejected.Load() : Completed.Load();
            if (NOT Acknowledged)
            {
                if (FPlatformTime::Seconds() < Deadline) { return false; }
                Test.AddError(TEXT("Production Adaptive reload did not acknowledge completion/rejection"));
                return true;
            }
            if (Stage == 0)
            {
                SnapshotA.Check(Test, *A, true); SnapshotB.Check(Test, *B, true);
                if (GlintsOptIn)
                {
                    Test.TestFalse(TEXT("Rejected batch retains changed glints opt-in without partial repair"), A->Definition->_AdaptiveBaseGlints);
                    A->Definition->_AdaptiveBaseGlints = true;
                }
                else
                {
                    Test.TestTrue(TEXT("Rejected production batch retains malformed setting without partial repair"), A->Find<UMaterialExpressionSubstrateVerticalLayering>()->bUseParameterBlending);
                    A->Find<UMaterialExpressionSubstrateVerticalLayering>()->bUseParameterBlending = false;
                }
                CheckPrepared(Test, *A, true); CheckPrepared(Test, *B, true);
            }
            else
            {
                Test.TestTrue(TEXT("Production reload recompiles both existing masters"), A->Material->StateId != BeforeA && B->Material->StateId != BeforeB);
                SnapshotA.Check(Test, *A, false); SnapshotB.Check(Test, *B, false);
                Test.TestTrue(TEXT("Numeric edit/recovery preserves both MID object identities"), A->Mid.Get() == MidA.Get() && B->Mid.Get() == MidB.Get());
                CheckCompile(Test, *A); CheckCompile(Test, *B);
                CheckPrepared(Test, *A, true); CheckPrepared(Test, *B, true);
            }
            ++Stage; Started = false;
            return Stage == 5;
        }
    private:
        FAutomationTestBase& Test;
        TSharedRef<FFixture> A, B;
        FSnapshot SnapshotA, SnapshotB;
        TWeakObjectPtr<UMaterialInstanceDynamic> MidA, MidB;
        FGuid BeforeA, BeforeB;
        TAtomic<bool> Rejected{false}, Completed{false};
        double Deadline = 0.0;
        int32 Stage = 0;
        bool Started = false;
        bool GlintsOptIn = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdaptiveReload, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.ProductionBatchAtomicityAndNumericReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdaptiveReload::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_adaptive;
    if (CheckUnsupportedProfile(*this)) { return true; }
    if (NOT FApp::CanEverRender() || GEditor == nullptr) { AddError(TEXT("Adaptive reload proof requires real editor RHI")); return false; }
    const auto A = MakeShared<FFixture>(ECk_Usf_SurfaceGraph::AdaptiveVertical);
    const auto B = MakeShared<FFixture>(ECk_Usf_SurfaceGraph::AdaptiveMix);
    if (NOT TestTrue(TEXT("Generate both production batch fixtures"), A->Generate() && B->Generate())) { return false; }
    CheckCompile(*this, *A); CheckCompile(*this, *B);
    ADD_LATENT_AUTOMATION_COMMAND(FReloadSequence(*this, A, B));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdaptiveBaseGlints, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.BaseGlintsNativeTopologyAndProductionReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdaptiveBaseGlints::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_adaptive;
    if (CheckUnsupportedGlints(*this)) { return true; }
    if (NOT FApp::CanEverRender() || GEditor == nullptr) { AddError(TEXT("Glints compile/reload proof requires real editor RHI")); return false; }
    const auto A = MakeShared<FFixture>(ECk_Usf_SurfaceGraph::AdaptiveVertical, true);
    const auto B = MakeShared<FFixture>(ECk_Usf_SurfaceGraph::AdaptiveMix);
    if (NOT TestTrue(TEXT("Generate glints master and existing non-glints sibling"), A->Generate() && B->Generate())) { return false; }
    const auto Layer = A->Find<UMaterialExpressionSubstrateVerticalLayering>();
    const auto Custom = A->Find<UMaterialExpressionCustom>();
    if (NOT TestTrue(TEXT("Generated glints Custom and native Vertical operator exist"), Layer.IsValid() && Custom.IsValid())) { return false; }
    const auto Top = TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Layer->Top.Expression)};
    const auto Base = TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Layer->Base.Expression)};
    if (NOT TestTrue(TEXT("Both retained Vertical inputs are native Slabs"), Top.IsValid() && Base.IsValid())) { return false; }
    TestTrue(TEXT("Upper coat never receives glints inputs"), Top->GlintValue.Expression == nullptr && Top->GlintUV.Expression == nullptr);
    const auto CheckBinding = [&](const FExpressionInput& Pin, FName Name, ECustomMaterialOutputType Type)
    {
        const auto Index = Custom->AdditionalOutputs.IndexOfByPredicate([&](const FCustomOutput& Output) { return Output.OutputName == Name; });
        if (NOT TestTrue(TEXT("Lower native glints output exists"), Index != INDEX_NONE)) { return; }
        TestTrue(TEXT("Lower glints output retains exact scalar/vector type"), Custom->AdditionalOutputs[Index].OutputType == Type);
        auto Expected = FExpressionInput{};
        Expected.Connect(Index + 1, Custom.Get());
        TestTrue(TEXT("Lower native glints pin uses exact Custom output index and masks"), SameInput(Pin, Expected));
    };
    CheckBinding(Base->GlintValue, TEXT("CkUsf_BGlintValue"), CMOT_Float1);
    CheckBinding(Base->GlintUV, TEXT("CkUsf_BGlintUV"), CMOT_Float2);
    const auto NativeInputs = Base->GetInputsView();
    if (NOT TestTrue(TEXT("Native lower Slab exposes glints pin indices 16/17"), NativeInputs.Num() > 17)) { return false; }
    TestTrue(TEXT("Native pin 16 is lower density and pin 17 is lower UV"),
        NativeInputs[16] == &Base->GlintValue && NativeInputs[17] == &Base->GlintUV);
    CheckPrepared(*this, *A, true);
    CheckCompile(*this, *A);
    CheckCompile(*this, *B);

    const auto SavedValue = Base->GlintValue;
    const auto SavedUV = Base->GlintUV;
    Base->GlintValue.Expression = nullptr;
    CheckPrepared(*this, *A, false);
    Base->GlintValue = SavedValue;
    Base->GlintUV.OutputIndex = SavedValue.OutputIndex;
    CheckPrepared(*this, *A, false);
    Base->GlintUV = SavedUV;
    Base->GlintValue.MaskR = NOT Base->GlintValue.MaskR;
    CheckPrepared(*this, *A, false);
    Base->GlintValue = SavedValue;
    Top->GlintValue = SavedValue;
    CheckPrepared(*this, *A, false);
    Top->GlintValue = FExpressionInput{};
    Top->GlintUV = SavedUV;
    CheckPrepared(*this, *A, false);
    Top->GlintUV = FExpressionInput{};
    const auto GlintOutput = Custom->AdditionalOutputs.IndexOfByPredicate([](const FCustomOutput& Output)
        { return Output.OutputName == TEXT("CkUsf_BGlintUV"); });
    if (NOT TestTrue(TEXT("Glints UV output is available for contract tamper"), GlintOutput != INDEX_NONE)) { return false; }
    Custom->AdditionalOutputs[GlintOutput].OutputType = CMOT_Float3;
    CheckPrepared(*this, *A, false);
    Custom->AdditionalOutputs[GlintOutput].OutputType = CMOT_Float2;
    A->Definition->_AdaptiveBaseGlints = false;
    CheckPrepared(*this, *A, false);
    A->Definition->_AdaptiveBaseGlints = true;
    auto& Metadata = A->Material->GetOutermost()->GetMetaData();
    const auto Signature = Metadata.GetValue(A->Material.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"));
    TestFalse(TEXT("Glints material records a topology signature"), Signature.IsEmpty());
    Metadata.SetValue(A->Material.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"), *(Signature + TEXT("TamperedGlintsSignature\n")));
    CheckPrepared(*this, *A, false);
    Metadata.SetValue(A->Material.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"), *Signature);
    CheckPrepared(*this, *A, true);

    // Reuse the production sequence to reject a mixed batch atomically, apply entry/helper
    // numeric edits, restore original source, and check native features after every compile.
    ADD_LATENT_AUTOMATION_COMMAND(FReloadSequence(*this, A, B));
    return true;
}

#endif
