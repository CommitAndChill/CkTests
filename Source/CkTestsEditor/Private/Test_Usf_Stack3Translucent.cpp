#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"
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
#include "Rendering/SubstrateMaterialShared.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "ShaderCore.h"
#include "ShaderCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_stack3
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
        FString Helper = TEXT("#pragma once\nfloat Stack3Gain() { return 1.0; }\n");
        TStrongObjectPtr<UCkUsf_LookDefinition> Definition;
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;

        FFixture()
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/CkUsfStack3_") + Id + TEXT("/");
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfStack3") / Id);
            VirtualRoot = TEXT("/CkUsf/Stack3_") + Id;
            IFileManager::Get().MakeDirectory(*(Directory / TEXT("Packages")), true);
            FPackageName::RegisterMountPoint(Root, Directory / TEXT("Packages/"));
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
            Definition.Reset(NewObject<UCkUsf_LookDefinition>());
            Definition->_LookName = TEXT("Stack3Fixture");
            Definition->_GeneratedPackageRoot = Root.LeftChop(1);
            Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeTranslucentStack3;
            Definition->_UshIncludePath = VirtualRoot / TEXT("Entry.ush");
            Definition->_UshFunctionName = TEXT("CkUsf_Stack3Fixture");
            auto Gain = FCk_Usf_ParamDesc{};
            Gain._Name = TEXT("Gain"); Gain._DefaultScalar = 0.6f;
            Definition->_Parameters = {Gain};
            Source = TEXT("// @ckusf surface_graph NativeTranslucentStack3\n#pragma once\n#include \"/CkUsf/Common.ush\"\n#include \"")
                + VirtualRoot + TEXT("/Nested.ush\"\nFCkUsf_TranslucentStack3Output CkUsf_Stack3Fixture(FCkUsf_SurfaceInput In, float Gain) { ")
                + TEXT("FCkUsf_TranslucentStack3Output O=CkUsf_DefaultTranslucentStack3(); ")
                + TEXT("O.AF0=float3(0.04,0.04,0.04); O.ARoughness=0.12; O.BF0=float3(0.12,0.06,0.03); O.BRoughness=0.35; ")
                + TEXT("O.ATransmittanceColor=float3(0.92,0.95,0.98); O.AReferenceThicknessCm=0.1; O.AThicknessCm=0.03; ")
                + TEXT("O.BNormal=normalize(float3(0.3,0.2,1.0)); O.BTransmittanceColor=float3(0.999,0.999,0.999); O.BReferenceThicknessCm=0.2; O.BThicknessCm=0.01; O.CTransmittanceColor=float3(0.8,0.6,0.4); O.CReferenceThicknessCm=0.3; O.CThicknessCm=0.2*Gain*Stack3Gain(); ")
                + TEXT("O.RefractionStrength=1.02; return O; }\n");
        }

        ~FFixture()
        {
            Write(Source, Helper);
            Drain();
            if (Material.IsValid())
            {
                const auto Custom = Find<UMaterialExpressionCustom>();
                if (Custom.IsValid()) { Custom->IncludeFilePaths.Reset(); }
                Material->UpdateCachedExpressionData();
                Material->ClearFlags(RF_Public | RF_Standalone);
                Material->MarkAsGarbage();
            }
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
                for (const auto& Node : Material->GetExpressions())
                { if (Node->IsA<T>()) { return Cast<T>(Node); } }
            }
            return {};
        }
    };

    auto Unsupported(FAutomationTestBase& Test) -> bool
    {
        const auto Support = ck::usf_editor::Get_SubstrateShadingModelSupport();
        if (Support.Enabled && NOT Support.BlendableGBuffer && Support.MaxClosuresPerPixel >= 3) { return false; }
        auto Fixture = FFixture{};
        Test.TestTrue(TEXT("Write unsupported three-layer optical fixture"), Fixture.Write(Fixture.Source, Fixture.Helper));
        const auto Result = ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get());
        Test.TestFalse(TEXT("Unsupported renderer rejects transmitting three-Slab authoring"), Result.Get_IsValid());
        Test.TestTrue(TEXT("Rejection names the three-closure Adaptive requirement"), Result.Errors.ContainsByPredicate(
            [](const FString& Error) { return Error.Contains(TEXT("at least three")); }));
        Test.TestFalse(TEXT("Rejected renderer admission creates no package"), FPackageName::DoesPackageExist(Fixture.Definition->Get_GeneratedMasterPackagePath()));
        return true;
    }

    struct FSnapshot
    {
        FGuid State;
        FString Code;
        TArray<FCustomOutput> Outputs;
        FExpressionInput Front, Thickness, Refraction;
        TArray<TWeakObjectPtr<UMaterialExpression>> Nodes;
        TArray<TArray<FExpressionInput>> Inputs;
        TWeakObjectPtr<UMaterialInstanceDynamic> Mid;

        explicit FSnapshot(const FFixture& Fixture)
        {
            State = Fixture.Material->StateId;
            Code = Fixture.Find<UMaterialExpressionCustom>()->Code;
            Outputs = Fixture.Find<UMaterialExpressionCustom>()->AdditionalOutputs;
            Front = Fixture.Material->GetEditorOnlyData()->FrontMaterial;
            Thickness = Fixture.Material->GetEditorOnlyData()->SurfaceThickness;
            Refraction = Fixture.Material->GetEditorOnlyData()->Refraction;
            Mid = Fixture.Mid.Get();
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
            if (SameState) { Test.TestEqual(TEXT("Rejected three-layer reload preserves StateId"), Fixture.Material->StateId, State); }
            Test.TestEqual(TEXT("Stack3 numeric reload preserves Custom call code"), Fixture.Find<UMaterialExpressionCustom>()->Code, Code);
            const auto& CurrentOutputs = Fixture.Find<UMaterialExpressionCustom>()->AdditionalOutputs;
            Test.TestEqual(TEXT("Stack3 reload preserves exact output count"), CurrentOutputs.Num(), Outputs.Num());
            for (auto Index = 0; Index < CurrentOutputs.Num() && Index < Outputs.Num(); ++Index)
            { Test.TestTrue(TEXT("Stack3 reload preserves output order/types"), CurrentOutputs[Index].OutputName == Outputs[Index].OutputName && CurrentOutputs[Index].OutputType == Outputs[Index].OutputType); }
            Test.TestTrue(TEXT("Stack3 reload preserves all material root connections"),
                SameInput(Front, Fixture.Material->GetEditorOnlyData()->FrontMaterial)
                && SameInput(Thickness, Fixture.Material->GetEditorOnlyData()->SurfaceThickness)
                && SameInput(Refraction, Fixture.Material->GetEditorOnlyData()->Refraction));
            Test.TestEqual(TEXT("Stack3 reload preserves node count"), Fixture.Material->GetExpressions().Num(), Nodes.Num());
            for (auto Index = 0; Index < Nodes.Num() && Index < Fixture.Material->GetExpressions().Num(); ++Index)
            {
                const auto Node = Fixture.Material->GetExpressions()[Index];
                Test.TestTrue(TEXT("Stack3 reload preserves node identities"), Node == Nodes[Index].Get());
                auto Pins = TArray<FExpressionInput>{};
                for (const auto Pin : Node->GetInputsView()) { if (Pin != nullptr) { Pins.Add(*Pin); } }
                Test.TestEqual(TEXT("Stack3 reload preserves input count"), Pins.Num(), Inputs[Index].Num());
                for (auto Pin = 0; Pin < Pins.Num() && Pin < Inputs[Index].Num(); ++Pin)
                { Test.TestTrue(TEXT("Stack3 reload preserves exact input indices and masks"), SameInput(Pins[Pin], Inputs[Index][Pin])); }
            }
            Test.TestTrue(TEXT("Stack3 reload preserves MID identity, parent and override"), Fixture.Mid.Get() == Mid.Get()
                && Fixture.Mid->Parent == Fixture.Material.Get() && FMath::IsNearlyEqual(Fixture.Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
        }
    };

    auto Prepare(FAutomationTestBase& Test, FFixture& Fixture, bool Expected) -> void
    {
        const auto Before = FSnapshot{Fixture};
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        Test.TestEqual(TEXT("Stack3 preflight matches expected eligibility"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors), Expected);
        if (Expected) { for (const auto& Error : Errors) { Test.AddError(Error); } }
        else { Test.TestTrue(TEXT("Rejected three-layer operation is empty, diagnosed and not runnable"), NOT Prepared.Custom.IsValid() && Prepared.Code.IsEmpty() && NOT Errors.IsEmpty()); }
        Before.Check(Test, Fixture, true);
    }

    auto Compile(FAutomationTestBase& Test, FFixture& Fixture) -> void
    {
        Fixture.Material->EnsureIsComplete();
        Drain();
        if (NOT Test.TestNotNull(TEXT("Stack3 optical real-RHI material resource exists"), Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform))) { return; }
        const auto& Resource = *Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform);
        for (const auto& Error : Resource.GetCompileErrors()) { Test.AddError(Error); }
        if (NOT Test.TestTrue(TEXT("Stack3 game-thread shader map is complete"), Resource.IsCompilationFinished()
            && Resource.IsGameThreadShaderMapComplete() && Resource.GetGameThreadShaderMap() != nullptr)) { return; }
        auto RenderComplete = false;
        ENQUEUE_RENDER_COMMAND(CkUsfStack3Complete)([&Resource, &RenderComplete](FRHICommandListImmediate&)
            { RenderComplete = Resource.IsRenderingThreadShaderMapComplete(); });
        FlushRenderingCommands();
        Test.TestTrue(TEXT("Stack3 render-thread shader map is complete"), RenderComplete);
        const auto& Output = Resource.GetGameThreadShaderMap()->GetSubstrateMaterialCompilationOutput();
        Test.TestEqual(TEXT("Stack3 optical compile requests three closures"), static_cast<int32>(Output.RequestedClosurePerPixel), 3);
        Test.TestEqual(TEXT("Stack3 optical compile retains three effective closures"), static_cast<int32>(Resource.MaterialGetSubstrateClosureCount_GameThread()), 3);
        Test.TestFalse(TEXT("Stack3 optical material was not simplified to meet budget"), Output.bMaterialOutOfBudgetHasBeenSimplified != 0);
        Test.TestTrue(TEXT("Stack3 payload retains transmitting MFP features"), EnumHasAnyFlags(Resource.MaterialGetSubstrateMaterialBsdfFeatures_GameThread(), ESubstrateBsdfFeature::MFPPluggedIn));
        Test.TestFalse(TEXT("Stack3 optical contract introduces no native glints"), EnumHasAnyFlags(Resource.MaterialGetSubstrateMaterialBsdfFeatures_GameThread(), ESubstrateBsdfFeature::Glint));
        Test.AddInfo(FString::Printf(TEXT("NativeTranslucentStack3 requested=%u retained=%u simplified=%u payload=%u uints; no GPU timing claim."),
            static_cast<uint32>(Output.RequestedClosurePerPixel), static_cast<uint32>(Output.SubstrateClosureCount),
            static_cast<uint32>(Output.bMaterialOutOfBudgetHasBeenSimplified), static_cast<uint32>(Output.SubstrateUintPerPixel)));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_Stack3Admission, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.Stack3Translucent.RendererAndAuthoringAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_Stack3Admission::RunTest(const FString&) -> bool
{
    using namespace ck::usf_editor;
    TestEqual(TEXT("Stack3 optical appends enum9"), static_cast<int32>(ECk_Usf_SurfaceGraph::NativeTranslucentStack3), 9);
    TestEqual(TEXT("Existing refractive enum7 is unchanged"), static_cast<int32>(ECk_Usf_SurfaceGraph::NativeRefractiveSlab), 7);
    auto Contract = FShadingModelContract{};
    Contract.SurfaceGraph = ECk_Usf_SurfaceGraph::NativeTranslucentStack3;
    TestTrue(TEXT("Stack3 optical uses its own predicate and Adaptive renderer"), Is_Stack3OpticalSurfaceGraph(Contract.SurfaceGraph)
        && Requires_AdaptiveGBuffer(Contract.SurfaceGraph) && NOT Is_OpticalSurfaceGraph(Contract.SurfaceGraph) && NOT Is_AdaptiveSurfaceGraph(Contract.SurfaceGraph));
    TestEqual(TEXT("Separate three-layer optical output type"), Get_SurfaceOutputType(Contract), FString{TEXT("FCkUsf_TranslucentStack3Output")});
    TestEqual(TEXT("Stack3 primary output is A diffuse"), Get_SurfacePrimaryField(Contract), FString{TEXT("ADiffuseAlbedo")});
    TestEqual(TEXT("Exactly twenty-four additional optical layer outputs"), Get_ShadingModelOutputs(Contract).Num(), 24);
    const auto NewOutputs = Get_ShadingModelOutputs(Contract);
    const auto Fields = TArray<FString>{TEXT("AF0"), TEXT("ARoughness"), TEXT("ANormal"), TEXT("AEmissiveColor"),
        TEXT("BDiffuseAlbedo"), TEXT("BF0"), TEXT("BRoughness"), TEXT("BNormal"), TEXT("BEmissiveColor"),
        TEXT("CDiffuseAlbedo"), TEXT("CF0"), TEXT("CRoughness"), TEXT("CNormal"), TEXT("CEmissiveColor"),
        TEXT("ATransmittanceColor"), TEXT("AReferenceThicknessCm"), TEXT("AThicknessCm"),
        TEXT("BTransmittanceColor"), TEXT("BReferenceThicknessCm"), TEXT("BThicknessCm"),
        TEXT("CTransmittanceColor"), TEXT("CReferenceThicknessCm"), TEXT("CThicknessCm"), TEXT("RefractionStrength")};
    for (auto Index = 0; Index < NewOutputs.Num(); ++Index)
    {
        const auto& Output = NewOutputs[Index];
        TestTrue(TEXT("Exact ordered Stack3 field names"), Fields.IsValidIndex(Index) && Output.Name == TEXT("CkUsf_") + Fields[Index]);
        const auto Scalar = Output.Name.EndsWith(TEXT("Roughness")) || Output.Name.EndsWith(TEXT("ThicknessCm"))
            || Output.Name == TEXT("CkUsf_RefractionStrength");
        TestTrue(TEXT("All three-layer outputs retain exact scalar/vector types and native-only properties"),
            Output.Type == (Scalar ? CMOT_Float1 : CMOT_Float3) && Output.Property == MP_MAX);
    }
    const auto NewCode = Build_ShadingModelOutputCode(Contract);
    TestTrue(TEXT("Stack3 references and actual thicknesses stay positive; PNO keeps engine convention"),
        NewCode.Contains(TEXT("CkUsf_AReferenceThicknessCm = max(O.AReferenceThicknessCm, 0.0001);"))
        && NewCode.Contains(TEXT("CkUsf_BThicknessCm = max(O.BThicknessCm, 0.0001);"))
        && NewCode.Contains(TEXT("CkUsf_CReferenceThicknessCm = max(O.CReferenceThicknessCm, 0.0001);"))
        && NewCode.Contains(TEXT("CkUsf_CThicknessCm = max(O.CThicknessCm, 0.0001);"))
        && NewCode.Contains(TEXT("CkUsf_RefractionStrength = clamp(O.RefractionStrength, 1.0, 1.1);")));
    auto Existing = FShadingModelContract{};
    TestTrue(TEXT("Default graph and glints option remain unchanged"), Existing.SurfaceGraph == ECk_Usf_SurfaceGraph::Legacy && NOT Existing.AdaptiveBaseGlints);
    for (const auto Graph : {ECk_Usf_SurfaceGraph::NativeTranslucentSlab, ECk_Usf_SurfaceGraph::NativeRefractiveSlab})
    {
        Existing.SurfaceGraph = Graph;
        TestEqual(TEXT("Existing optical output layout remains eight additional fields"), Get_ShadingModelOutputs(Existing).Num(), 8);
        TestEqual(TEXT("Existing optical return type remains single Slab"), Get_SurfaceOutputType(Existing), FString{TEXT("FCkUsf_TranslucentSlabOutput")});
    }
    Existing.SurfaceGraph = ECk_Usf_SurfaceGraph::NativeTranslucentVertical;
    TestEqual(TEXT("Existing two-layer enum8 is unchanged"), static_cast<int32>(Existing.SurfaceGraph), 8);
    TestEqual(TEXT("Existing two-layer output count is unchanged"), Get_ShadingModelOutputs(Existing).Num(), 16);
    TestEqual(TEXT("Existing two-layer return type is unchanged"), Get_SurfaceOutputType(Existing), FString{TEXT("FCkUsf_TranslucentVerticalOutput")});
    TestEqual(TEXT("Stack3 required closure budget"), Get_RequiredSubstrateClosureCount(Contract.SurfaceGraph), uint32{3});
    auto Support = FSubstrateShadingModelSupport{};
    Support.Enabled = true; Support.MaxClosuresPerPixel = 3;
    for (auto Case = 0; Case < 6; ++Case)
    {
        auto Candidate = Support;
        if (Case == 1) { Candidate.Enabled = false; }
        if (Case == 2) { Candidate.BlendableGBuffer = true; }
        if (Case >= 3) { Candidate.MaxClosuresPerPixel = Case - 3; }
        auto Errors = TArray<FString>{};
        TestEqual(TEXT("Stack3 optical requires enabled Adaptive and three closures"), Validate_SubstrateShadingModelSupport(Contract, Candidate, Errors), Case == 0);
        TestEqual(TEXT("Renderer admission errors match rejection"), Errors.IsEmpty(), Case == 0);
    }
    auto Fixture = ck_test_usf_stack3::FFixture{};
    auto RuntimeSupport = Support; RuntimeSupport.MaxClosuresPerPixel = 4;
    auto RuntimeErrors = TArray<FString>{};
    TestTrue(TEXT("Four-closure runtime profile admits three-layer graph"), Validate_SubstrateShadingModelSupport(Contract, RuntimeSupport, RuntimeErrors) && RuntimeErrors.IsEmpty());
    TestTrue(TEXT("Write exact three-layer source"), Fixture.Write(Fixture.Source, Fixture.Helper));
    auto Blendable = Support; Blendable.BlendableGBuffer = true; Blendable.MaxClosuresPerPixel = 1;
    TestTrue(TEXT("Bulk classification validates other-profile three-layer authoring"), Classify_LookForGeneration(Fixture.Definition.Get(), Blendable).Admission == ELookGenerationAdmission::OtherRendererProfile);
    auto TooSmall = Support; TooSmall.MaxClosuresPerPixel = 2;
    TestTrue(TEXT("Active Adaptive profile with insufficient budget is invalid"), Classify_LookForGeneration(Fixture.Definition.Get(), TooSmall).Admission == ELookGenerationAdmission::Invalid);
    TestFalse(TEXT("Classification creates no generated asset"), FPackageName::DoesPackageExist(Fixture.Definition->Get_GeneratedMasterPackagePath()));
    for (auto Case = 0; Case < 5; ++Case)
    {
        Fixture.Definition->_DirectionalShading = Case == 0;
        Fixture.Definition->_SlabFuzz = Case == 1;
        Fixture.Definition->_AdaptiveBaseGlints = Case == 2;
        Fixture.Definition->_SurfaceSceneColor = Case == 3;
        Fixture.Definition->_WpoFunctionName = Case == 4 ? FName{TEXT("ForbiddenWpo")} : NAME_None;
        TestTrue(TEXT("Unsupported three-layer authoring is invalid before renderer exclusion"), Classify_LookForGeneration(Fixture.Definition.Get(), Blendable).Admission == ELookGenerationAdmission::Invalid);
    }
    Fixture.Definition->_WpoFunctionName = NAME_None;
    Fixture.Definition->_SurfaceSceneColor = false;
    TestTrue(TEXT("Write malformed three-layer entry signature"), Fixture.Write(Fixture.Source.Replace(TEXT("float Gain)"), TEXT("float2 Gain)")), Fixture.Helper));
    TestTrue(TEXT("Malformed three-layer signature is invalid before generation"), Classify_LookForGeneration(Fixture.Definition.Get(), Support).Admission == ELookGenerationAdmission::Invalid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_Stack3Pins, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.Stack3Translucent.ExactTopologyAndRetainedClosures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_Stack3Pins::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_stack3;
    if (Unsupported(*this)) { return true; }
    if (NOT FApp::CanEverRender()) { AddError(TEXT("Stack3 closure proof requires real RHI")); return false; }
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("Generate three-layer transmitting graph"), Fixture.Generate())) { return false; }
    const auto Vertical = Fixture.Find<UMaterialExpressionSubstrateVerticalLayering>();
    const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
    if (NOT TestTrue(TEXT("Vertical and Custom nodes exist"), Vertical.IsValid() && Custom.IsValid())) { return false; }
    const auto Inner = TWeakObjectPtr<UMaterialExpressionSubstrateVerticalLayering>{Cast<UMaterialExpressionSubstrateVerticalLayering>(Vertical->Base.Expression)};
    if (NOT TestTrue(TEXT("Outer base is a distinct inner Vertical"), Inner.IsValid() && Inner != Vertical)) { return false; }
    auto Slabs = 0, Conversions = 0, Verticals = 0;
    for (const auto& Node : Fixture.Material->GetExpressions())
    {
        Slabs += Node->IsA<UMaterialExpressionSubstrateSlabBSDF>() ? 1 : 0;
        Conversions += Node->IsA<UMaterialExpressionSubstrateTransmittanceToMFP>() ? 1 : 0;
        Verticals += Node->IsA<UMaterialExpressionSubstrateVerticalLayering>() ? 1 : 0;
    }
    TestEqual(TEXT("Exactly three native Slabs"), Slabs, 3);
    TestEqual(TEXT("Exactly three independent native MFP conversions"), Conversions, 3);
    TestEqual(TEXT("Exactly two native Vertical operators"), Verticals, 2);
    TestFalse(TEXT("Outer retains BSDFs without parameter blending"), Vertical->bUseParameterBlending);
    TestFalse(TEXT("Inner retains BSDFs without parameter blending"), Inner->bUseParameterBlending);
    const auto Binding = [&](const FExpressionInput& Pin, const FString& Name)
    {
        const auto Index = Custom->AdditionalOutputs.IndexOfByPredicate([&](const FCustomOutput& Output) { return Output.OutputName.ToString() == Name; });
        if (NOT TestTrue(TEXT("Exact named stack output exists"), Index != INDEX_NONE)) { return; }
        auto Expected = FExpressionInput{}; Expected.Connect(Index+1, Custom.Get());
        TestTrue(TEXT("Stack output consumes exact native index/masks"), SameInput(Pin, Expected));
    };
    const auto Ordered = TArray<TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>>{
        Cast<UMaterialExpressionSubstrateSlabBSDF>(Vertical->Top.Expression),
        Cast<UMaterialExpressionSubstrateSlabBSDF>(Inner->Top.Expression),
        Cast<UMaterialExpressionSubstrateSlabBSDF>(Inner->Base.Expression)};
    auto MfPs = TArray<TWeakObjectPtr<UMaterialExpressionSubstrateTransmittanceToMFP>>{};
    for (auto Index = 0; Index < Ordered.Num(); ++Index)
    {
        const auto Slab = Ordered[Index];
        if (NOT TestTrue(TEXT("All ordered layers are native distinct Slabs"), Slab.IsValid()
            && Ordered[0] != Ordered[1] && Ordered[0] != Ordered[2] && Ordered[1] != Ordered[2])) { return false; }
        const auto Mfp = TWeakObjectPtr<UMaterialExpressionSubstrateTransmittanceToMFP>{Cast<UMaterialExpressionSubstrateTransmittanceToMFP>(Slab->SSSMFP.Expression)};
        if (NOT TestTrue(TEXT("Each transmitting layer owns an independent MFP conversion"), Mfp.IsValid() && NOT MfPs.Contains(Mfp))) { return false; }
        MfPs.Add(Mfp);
        const auto Prefix = FString::Printf(TEXT("CkUsf_%c"), TCHAR('A' + Index));
        TestTrue(TEXT("All native layers use SimpleVolume"), Slab->SubSurfaceType == MSS_SimpleVolume);
        Binding(Mfp->TransmittanceColor, Prefix + TEXT("TransmittanceColor"));
        Binding(Mfp->Thickness, Prefix + TEXT("ReferenceThicknessCm"));
        Binding(Slab->F0, Prefix + TEXT("F0")); Binding(Slab->Roughness, Prefix + TEXT("Roughness"));
        Binding(Slab->Normal, Prefix + TEXT("Normal")); Binding(Slab->EmissiveColor, Prefix + TEXT("EmissiveColor"));
        if (Index == 0)
        {
            auto Primary = FExpressionInput{}; Primary.Connect(0, Custom.Get());
            TestTrue(TEXT("A diffuse is the primary output"), SameInput(Slab->DiffuseAlbedo, Primary));
        }
        else { Binding(Slab->DiffuseAlbedo, Prefix + TEXT("DiffuseAlbedo")); }
        TestTrue(TEXT("No fuzz, anisotropy, tangent or glints inputs"), Slab->FuzzAmount.Expression == nullptr
            && Slab->Anisotropy.Expression == nullptr && Slab->Tangent.Expression == nullptr && Slab->GlintValue.Expression == nullptr && Slab->GlintUV.Expression == nullptr);
    }
    Binding(Vertical->Thickness, TEXT("CkUsf_AThicknessCm"));
    Binding(Inner->Thickness, TEXT("CkUsf_BThicknessCm"));
    Binding(Fixture.Material->GetEditorOnlyData()->SurfaceThickness, TEXT("CkUsf_CThicknessCm"));
    Binding(Fixture.Material->GetEditorOnlyData()->Refraction, TEXT("CkUsf_RefractionStrength"));
    TestTrue(TEXT("Stack3 optical material fixes colored transmission, forward lighting, thin-surface PNO before DOF"),
        Fixture.Material->BlendMode == BLEND_TranslucentColoredTransmittance && Fixture.Material->TranslucencyLightingMode == TLM_SurfacePerPixelLighting
        && Fixture.Material->bIsThinSurface && Fixture.Material->RefractionMethod == RM_PixelNormalOffset && Fixture.Material->TranslucencyPass == MTP_BeforeDOF);
    Prepare(*this, Fixture, true);
    Compile(*this, Fixture);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_Stack3Atomic, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.Stack3Translucent.TopologyPreflightIsAtomic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_Stack3Atomic::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_stack3;
    if (Unsupported(*this)) { return true; }
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("Generate owned three-layer rejection fixture"), Fixture.Generate())) { return false; }
    const auto Vertical = Fixture.Find<UMaterialExpressionSubstrateVerticalLayering>();
    const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
    if (NOT TestTrue(TEXT("Stack3 tamper nodes exist"), Vertical.IsValid() && Custom.IsValid())) { return false; }
    const auto Inner = TWeakObjectPtr<UMaterialExpressionSubstrateVerticalLayering>{Cast<UMaterialExpressionSubstrateVerticalLayering>(Vertical->Base.Expression)};
    if (NOT TestTrue(TEXT("Inner Vertical exists"), Inner.IsValid())) { return false; }
    const auto A = TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Vertical->Top.Expression)};
    const auto B = TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Inner->Top.Expression)};
    const auto C = TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Inner->Base.Expression)};
    if (NOT TestTrue(TEXT("Three transmitting Slabs exist"), A.IsValid() && B.IsValid() && C.IsValid())) { return false; }
    const auto MfpA = TWeakObjectPtr<UMaterialExpressionSubstrateTransmittanceToMFP>{Cast<UMaterialExpressionSubstrateTransmittanceToMFP>(A->SSSMFP.Expression)};
    const auto MfpB = TWeakObjectPtr<UMaterialExpressionSubstrateTransmittanceToMFP>{Cast<UMaterialExpressionSubstrateTransmittanceToMFP>(B->SSSMFP.Expression)};
    const auto MfpC = TWeakObjectPtr<UMaterialExpressionSubstrateTransmittanceToMFP>{Cast<UMaterialExpressionSubstrateTransmittanceToMFP>(C->SSSMFP.Expression)};
    if (NOT TestTrue(TEXT("Three calibration nodes exist"), MfpA.IsValid() && MfpB.IsValid() && MfpC.IsValid())) { return false; }
    Vertical->bUseParameterBlending = true; Prepare(*this, Fixture, false); Vertical->bUseParameterBlending = false;
    Inner->bUseParameterBlending = true; Prepare(*this, Fixture, false); Inner->bUseParameterBlending = false;
    Swap(Vertical->Top, Vertical->Base); Prepare(*this, Fixture, false); Swap(Vertical->Top, Vertical->Base);
    Swap(Inner->Top, Inner->Base); Prepare(*this, Fixture, false); Swap(Inner->Top, Inner->Base);
    const auto SavedBase = Vertical->Base;
    Vertical->Base = Inner->Base; Prepare(*this, Fixture, false); Vertical->Base = SavedBase;
    const auto SavedC = Inner->Base;
    Inner->Base = Inner->Top; Prepare(*this, Fixture, false); Inner->Base = SavedC;
    Swap(MfpA->Thickness, MfpC->Thickness); Prepare(*this, Fixture, false); Swap(MfpA->Thickness, MfpC->Thickness);
    const auto SavedMfp = C->SSSMFP; C->SSSMFP = B->SSSMFP; Prepare(*this, Fixture, false); C->SSSMFP = SavedMfp;
    const auto SavedRoot = Fixture.Material->GetEditorOnlyData()->SurfaceThickness;
    Fixture.Material->GetEditorOnlyData()->SurfaceThickness = FScalarMaterialInput{};
    Prepare(*this, Fixture, false); Fixture.Material->GetEditorOnlyData()->SurfaceThickness = SavedRoot;
    const auto SavedThickness = Inner->Thickness;
    Inner->Thickness = SavedRoot; Prepare(*this, Fixture, false); Inner->Thickness = SavedThickness;
    Fixture.Material->GetEditorOnlyData()->Refraction.MaskR ^= 1;
    Prepare(*this, Fixture, false); Fixture.Material->GetEditorOnlyData()->Refraction.MaskR ^= 1;
    C->SubSurfaceType = MSS_None; Prepare(*this, Fixture, false); C->SubSurfaceType = MSS_SimpleVolume;
    Fixture.Material->bIsThinSurface = false; Prepare(*this, Fixture, false); Fixture.Material->bIsThinSurface = true;
    Fixture.Material->RefractionMethod = RM_IndexOfRefractionFromF0; Prepare(*this, Fixture, false); Fixture.Material->RefractionMethod = RM_PixelNormalOffset;
    const auto SavedType = Custom->AdditionalOutputs.Last().OutputType;
    Custom->AdditionalOutputs.Last().OutputType = CMOT_Float2; Prepare(*this, Fixture, false); Custom->AdditionalOutputs.Last().OutputType = SavedType;
    const auto Extra = TObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{NewObject<UMaterialExpressionSubstrateSlabBSDF>(Fixture.Material.Get())};
    Extra->Material = Fixture.Material.Get();
    Fixture.Material->GetExpressionCollection().AddExpression(Extra);
    Prepare(*this, Fixture, false); Fixture.Material->GetExpressionCollection().RemoveExpression(Extra);
    auto& Metadata = Fixture.Material->GetOutermost()->GetMetaData();
    const auto Signature = FString{Metadata.GetValue(Fixture.Material.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"))};
    Metadata.SetValue(Fixture.Material.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"), *(Signature+TEXT("Tampered\n")));
    Prepare(*this, Fixture, false); Metadata.SetValue(Fixture.Material.Get(), TEXT("CkUsf.NativeSurfaceGraph.v1"), *Signature);
    Prepare(*this, Fixture, true);
    return true;
}

namespace ck_test_usf_stack3
{
    class FReloadSequence : public IAutomationLatentCommand, public FOutputDevice
    {
    public:
        FReloadSequence(FAutomationTestBase& InTest, TSharedRef<FFixture> InA, TSharedRef<FFixture> InB)
            : Test(InTest), A(InA), B(InB), SnapshotA(*InA), SnapshotB(*InB)
        { GLog->AddOutputDevice(this); }
        ~FReloadSequence() override
        {
            GLog->RemoveOutputDevice(this);
            B->Find<UMaterialExpressionSubstrateVerticalLayering>()->bUseParameterBlending = false;
            A->Write(A->Source, A->Helper); B->Write(B->Source, B->Helper);
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
                if (Stage == 0) { B->Find<UMaterialExpressionSubstrateVerticalLayering>()->bUseParameterBlending = true; }
                for (const auto& Fixture : {A, B})
                {
                    const auto Entry = Stage <= 1 ? Fixture->Source.Replace(TEXT("O.AThicknessCm=0.03"), TEXT("O.AThicknessCm=0.05"))
                        .Replace(TEXT("0.2*Gain"), TEXT("0.35*Gain")) : Fixture->Source;
                    const auto Helper = Stage == 2 ? Fixture->Helper.Replace(TEXT("return 1.0"), TEXT("return 0.7")) : Fixture->Helper;
                    if (NOT Test.TestTrue(TEXT("Write exact three-layer numeric source phase"), Fixture->Write(Entry, Helper))) { return true; }
                }
                IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, GEditor->GetEditorWorldContext().World());
                Started = true; Deadline = FPlatformTime::Seconds()+90.;
                return false;
            }
            if (NOT (Stage == 0 ? Rejected.Load() : Completed.Load()))
            {
                if (FPlatformTime::Seconds() < Deadline) { return false; }
                Test.AddError(TEXT("Production three-layer reload did not acknowledge completion/rejection")); return true;
            }
            if (Stage == 0)
            {
                SnapshotA.Check(Test, *A, true); SnapshotB.Check(Test, *B, true);
                Test.TestTrue(TEXT("Rejected mixed batch leaves malformed master untouched"), B->Find<UMaterialExpressionSubstrateVerticalLayering>()->bUseParameterBlending);
                B->Find<UMaterialExpressionSubstrateVerticalLayering>()->bUseParameterBlending = false;
            }
            else
            {
                Test.TestTrue(TEXT("Stack3 reload recompiles both existing masters"), A->Material->StateId != BeforeA && B->Material->StateId != BeforeB);
                SnapshotA.Check(Test, *A, false); SnapshotB.Check(Test, *B, false);
                Compile(Test, *A); Compile(Test, *B);
            }
            Prepare(Test, *A, true); Prepare(Test, *B, true);
            ++Stage; Started = false;
            return Stage == 4;
        }
    private:
        FAutomationTestBase& Test;
        TSharedRef<FFixture> A, B;
        FSnapshot SnapshotA, SnapshotB;
        FGuid BeforeA, BeforeB;
        TAtomic<bool> Rejected{false}, Completed{false};
        double Deadline = 0.;
        int32 Stage = 0;
        bool Started = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_Stack3Reload, "CkTests.UnitTests.CkUsf.AdaptiveBsdf.Stack3Translucent.ProductionBatchAtomicityAndNumericReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_Stack3Reload::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_stack3;
    if (Unsupported(*this)) { return true; }
    if (NOT FApp::CanEverRender() || GEditor == nullptr) { AddError(TEXT("Stack3 production reload requires editor real RHI")); return false; }
    const auto A = MakeShared<FFixture>(), B = MakeShared<FFixture>();
    if (NOT TestTrue(TEXT("Generate both three-layer production batch fixtures"), A->Generate() && B->Generate())) { return false; }
    Compile(*this, *A); Compile(*this, *B);
    ADD_LATENT_AUTOMATION_COMMAND(FReloadSequence(*this, A, B));
    return true;
}

#endif


