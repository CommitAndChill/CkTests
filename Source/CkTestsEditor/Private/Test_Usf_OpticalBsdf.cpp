#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"
#include "AssetCompilingManager.h"
#include "EditorValidatorSubsystem.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "ShaderCore.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_optical
{
    auto Drain() -> void
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr)
        { GShaderCompilingManager->FinishAllCompilation(); }
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
        FString Helper = TEXT("#pragma once\nfloat OpticalGain() { return 1.0; }\n");
        TStrongObjectPtr<UCkUsf_LookDefinition> Definition;
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;

        explicit FFixture(ECk_Usf_SurfaceGraph InGraph = ECk_Usf_SurfaceGraph::NativeTranslucentSlab)
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/CkUsfOptical_") + Id + TEXT("/");
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfOptical") / Id);
            VirtualRoot = TEXT("/CkUsf/Optical_") + Id;
            IFileManager::Get().MakeDirectory(*(Directory / TEXT("Packages")), true);
            FPackageName::RegisterMountPoint(Root, Directory / TEXT("Packages/"));
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
            Definition.Reset(NewObject<UCkUsf_LookDefinition>());
            Definition->_LookName = TEXT("OpticalFixture");
            Definition->_GeneratedPackageRoot = Root.LeftChop(1);
            Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
            Definition->_SurfaceGraph = InGraph;
            Definition->_UshIncludePath = VirtualRoot / TEXT("Entry.ush");
            Definition->_UshFunctionName = TEXT("CkUsf_OpticalFixture");
            auto Gain = FCk_Usf_ParamDesc{};
            Gain._Name = TEXT("Gain");
            Gain._DefaultScalar = 0.6f;
            Definition->_Parameters = {Gain};
            Source = TEXT("// @ckusf surface_graph NativeTranslucentSlab\n#pragma once\n#include \"/CkUsf/Common.ush\"\n#include \"")
                + VirtualRoot + TEXT("/Nested.ush\"\nFCkUsf_TranslucentSlabOutput CkUsf_OpticalFixture(FCkUsf_SurfaceInput In, float Gain) { ")
                + TEXT("FCkUsf_TranslucentSlabOutput O=CkUsf_DefaultTranslucentSlab(); O.ThicknessCm=0.2*Gain*OpticalGain(); ")
                + TEXT("O.ReferenceThicknessCm=0.1; O.RefractionStrength=1.02; O.TransmittanceColor=float3(0.7,0.1,0.03); return O; }\n");
            if (InGraph == ECk_Usf_SurfaceGraph::NativeRefractiveSlab)
            { Source.ReplaceInline(TEXT("surface_graph NativeTranslucentSlab"), TEXT("surface_graph NativeRefractiveSlab")); }
        }

        auto Write(const FString& Entry, const FString& Nested) const -> bool
        {
            return FFileHelper::SaveStringToFile(Entry, *(Directory / TEXT("Entry.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
                && FFileHelper::SaveStringToFile(Nested, *(Directory / TEXT("Nested.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        auto Generate() -> bool
        {
            if (NOT Write(Source, Helper))
            { return false; }
            Material.Reset(ck::usf_editor::Generate_LookMaterial(Definition.Get()));
            if (NOT Material.IsValid())
            { return false; }
            Mid.Reset(UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage()));
            if (NOT Mid.IsValid())
            { return false; }
            Mid->SetScalarParameterValue(TEXT("Gain"), 0.75f);
            return true;
        }

        template<typename T> auto Find() const -> TWeakObjectPtr<T>
        {
            if (Material.IsValid())
            {
                for (const auto& Expression : Material->GetExpressions())
                {
                    if (Expression->IsA<T>())
                    { return Cast<T>(Expression); }
                }
            }
            return {};
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
                    if (Custom != nullptr)
                    { Custom->IncludeFilePaths.Reset(); }
                }
                Material->UpdateCachedExpressionData();
                Material->ClearFlags(RF_Public | RF_Standalone);
                Material->MarkAsGarbage();
            }
            // Leave unique source mappings available to process-global shader caches.
            FPackageName::UnRegisterMountPoint(Root, Directory / TEXT("Packages/"));
        }
    };

    auto CheckUnsupportedProfile(FAutomationTestBase& Test, FFixture& Fixture) -> bool
    {
        const auto Support = ck::usf_editor::Get_SubstrateShadingModelSupport();
        if (Support.Enabled && NOT Support.BlendableGBuffer && Support.MaxClosuresPerPixel >= 1)
        { return false; }
        Test.TestTrue(TEXT("Write opposite-profile optical fixture"), Fixture.Write(Fixture.Source, Fixture.Helper));
        const auto Validation = ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get());
        Test.TestFalse(TEXT("Strict validation rejects optical graph under the baseline profile"), Validation.Get_IsValid());
        Test.TestTrue(TEXT("Rejection identifies renderer requirement"), Validation.Errors.ContainsByPredicate(
            [](const FString& Error) { return Error.Contains(TEXT("Adaptive GBuffer")); }));
        Test.TestFalse(TEXT("Rejected optical definition creates no material"), FPackageName::DoesPackageExist(Fixture.Definition->Get_GeneratedMasterPackagePath()));
        return true;
    }

    auto CheckCompile(FAutomationTestBase& Test, FFixture& Fixture) -> void
    {
        Fixture.Material->EnsureIsComplete();
        Drain();
        if (NOT Test.TestNotNull(TEXT("Optical master has a real RHI resource"), Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform)))
        { return; }
        const auto& Resource = *Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform);
        for (const auto& Error : Resource.GetCompileErrors())
        { Test.AddError(Error); }
        Test.TestTrue(TEXT("Optical shader map is complete"), Resource.IsCompilationFinished()
            && Resource.IsGameThreadShaderMapComplete() && Resource.GetGameThreadShaderMap() != nullptr);
        auto RenderComplete = false;
        ENQUEUE_RENDER_COMMAND(CkUsfOpticalComplete)([&Resource, &RenderComplete](FRHICommandListImmediate&)
            { RenderComplete = Resource.IsRenderingThreadShaderMapComplete(); });
        FlushRenderingCommands();
        Test.TestTrue(TEXT("Optical render-thread shader map is complete"), RenderComplete);
        Test.TestEqual(TEXT("Optical material retains one closure"), static_cast<int32>(Resource.MaterialGetSubstrateClosureCount_GameThread()), 1);
    }

    auto CheckRejected(FAutomationTestBase& Test, FFixture& Fixture) -> void
    {
        const auto BeforeState = Fixture.Material->StateId;
        const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
        const auto BeforeCode = Custom->Code;
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        Test.TestFalse(TEXT("Changed optical contract rejects reload"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
        Test.TestTrue(TEXT("Rejected optical preparation is empty and diagnosed"), NOT Prepared.Custom.IsValid() && Prepared.Code.IsEmpty() && NOT Errors.IsEmpty());
        Test.TestEqual(TEXT("Rejected preflight retains material StateId"), Fixture.Material->StateId, BeforeState);
        Test.TestEqual(TEXT("Rejected preflight retains Custom code"), Custom->Code, BeforeCode);
        Test.TestTrue(TEXT("Rejected preflight retains MID parent and override"), Fixture.Mid->Parent == Fixture.Material.Get()
            && FMath::IsNearlyEqual(Fixture.Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_OpticalAdmission, "CkTests.UnitTests.CkUsf.OpticalTailLamp.RendererAndAuthoringAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_OpticalAdmission::RunTest(const FString&) -> bool
{
    using namespace ck::usf_editor;
    auto Fixture = ck_test_usf_optical::FFixture{};
    if (NOT TestTrue(TEXT("Write optical fixture"), Fixture.Write(Fixture.Source, Fixture.Helper)))
    { return false; }
    auto Support = FSubstrateShadingModelSupport{};
    Support.Enabled = true;
    Support.MaxClosuresPerPixel = 1;
    auto Contract = FShadingModelContract{};
    Contract.SurfaceGraph = ECk_Usf_SurfaceGraph::NativeTranslucentSlab;
    auto Errors = TArray<FString>{};
    TestTrue(TEXT("Full authoring admits optical graph with compatible synthetic support"), Classify_LookForGeneration(Fixture.Definition.Get(), Support).Admission == ELookGenerationAdmission::Compatible);
    TestTrue(TEXT("Optical graph requires Adaptive without becoming a two-Slab tree"), Requires_AdaptiveGBuffer(Contract.SurfaceGraph) && NOT Is_AdaptiveSurfaceGraph(Contract.SurfaceGraph));
    TestTrue(TEXT("One available closure admits optical Slab"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    Support.MaxClosuresPerPixel = 0;
    Errors.Reset();
    TestFalse(TEXT("Zero available closures reject optical Slab"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    Support.MaxClosuresPerPixel = 1;
    Support.BlendableGBuffer = true;
    Errors.Reset();
    TestFalse(TEXT("Blendable rejects direct optical admission"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    TestTrue(TEXT("Valid opposite-profile optical authoring can be counted separately"), Classify_LookForGeneration(Fixture.Definition.Get(), Support).Admission == ELookGenerationAdmission::OtherRendererProfile);
    Support.Enabled = false;
    Errors.Reset();
    TestFalse(TEXT("Disabled Substrate rejects optical admission"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    Support.Enabled = true;
    const auto CheckInvalid = [&]()
    {
        TestTrue(TEXT("Malformed optical authoring is never a profile exclusion"), Classify_LookForGeneration(Fixture.Definition.Get(), Support).Admission == ELookGenerationAdmission::Invalid);
        TestFalse(TEXT("Invalid authoring creates no package"), FPackageName::DoesPackageExist(Fixture.Definition->Get_GeneratedMasterPackagePath()));
    };
    Fixture.Definition->_Domain = ECk_Usf_Domain::PostProcess; CheckInvalid(); Fixture.Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
    Fixture.Definition->_BlendMode = ECk_Usf_BlendMode::Translucent; CheckInvalid(); Fixture.Definition->_BlendMode = ECk_Usf_BlendMode::Inherit;
    Fixture.Definition->_DirectionalShading = true; CheckInvalid(); Fixture.Definition->_DirectionalShading = false;
    Fixture.Definition->_SlabFuzz = true; CheckInvalid(); Fixture.Definition->_SlabFuzz = false;
    Fixture.Definition->_SurfaceSceneColor = true; CheckInvalid(); Fixture.Definition->_SurfaceSceneColor = false;
    Fixture.Definition->_WpoFunctionName = TEXT("UnexpectedWpo"); CheckInvalid(); Fixture.Definition->_WpoFunctionName = NAME_None;
    const auto DuplicateParameter = Fixture.Definition->_Parameters[0];
    Fixture.Definition->_Parameters.Add(DuplicateParameter); CheckInvalid(); Fixture.Definition->_Parameters.SetNum(1);
    TestTrue(TEXT("Write conflicting native and legacy directives"), Fixture.Write(TEXT("// @ckusf shading_model DefaultLit\n") + Fixture.Source, Fixture.Helper));
    CheckInvalid();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_OpticalPins, "CkTests.UnitTests.CkUsf.OpticalTailLamp.GeneratesNativeOpticalPinsAndCompiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_OpticalPins::RunTest(const FString&) -> bool
{
    auto Fixture = ck_test_usf_optical::FFixture{};
    if (ck_test_usf_optical::CheckUnsupportedProfile(*this, Fixture))
    { return true; }
    if (NOT TestTrue(TEXT("Generate optical native master"), Fixture.Generate()))
    { return false; }
    const auto Slab = Fixture.Find<UMaterialExpressionSubstrateSlabBSDF>();
    const auto Mfp = Fixture.Find<UMaterialExpressionSubstrateTransmittanceToMFP>();
    const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
    if (NOT TestTrue(TEXT("Native Slab, MFP utility and Custom exist"), Slab.IsValid() && Mfp.IsValid() && Custom.IsValid()))
    { return false; }
    TestTrue(TEXT("Native FrontMaterial is the single Slab"), Fixture.Material->GetEditorOnlyData()->FrontMaterial.Expression == Slab.Get());
    TestTrue(TEXT("Colored transmission uses fixed native settings"), Fixture.Material->BlendMode == BLEND_TranslucentColoredTransmittance
        && Fixture.Material->TranslucencyLightingMode == TLM_SurfacePerPixelLighting && Fixture.Material->bIsThinSurface
        && Fixture.Material->bTangentSpaceNormal && Fixture.Material->RefractionMethod == RM_PixelNormalOffset
        && Slab->SubSurfaceType == MSS_SimpleVolume);
    const auto Outputs = TArray<FName>{TEXT("EmissiveColor"), TEXT("Roughness"), TEXT("Normal"), TEXT("F0"),
        TEXT("CkUsf_TransmittanceColor"), TEXT("CkUsf_ReferenceThicknessCm"), TEXT("CkUsf_ThicknessCm"), TEXT("CkUsf_RefractionStrength")};
    TestEqual(TEXT("Optical output count"), Custom->AdditionalOutputs.Num(), Outputs.Num());
    for (auto Index = 0; Index < Outputs.Num() && Index < Custom->AdditionalOutputs.Num(); ++Index)
    { TestTrue(TEXT("Ordered optical output name and type"), Custom->AdditionalOutputs[Index].OutputName == Outputs[Index]
        && Custom->AdditionalOutputs[Index].OutputType == (Index == 1 || Index >= 5 ? CMOT_Float1 : CMOT_Float3)); }
    TestTrue(TEXT("Primary diffuse/F0/roughness/normal/emission feed actual native Slab pins"), Slab->DiffuseAlbedo.Expression == Custom.Get()
        && Slab->DiffuseAlbedo.OutputIndex == 0 && Slab->F0.OutputIndex == 4 && Slab->Roughness.OutputIndex == 2
        && Slab->Normal.OutputIndex == 3 && Slab->EmissiveColor.OutputIndex == 1);
    TestTrue(TEXT("Native MFP uses independent reference thickness"), Slab->SSSMFP.Expression == Mfp.Get()
        && Mfp->TransmittanceColor.Expression == Custom.Get() && Mfp->TransmittanceColor.OutputIndex == 5
        && Mfp->Thickness.Expression == Custom.Get() && Mfp->Thickness.OutputIndex == 6);
    TestTrue(TEXT("Actual surface thickness uses a distinct root output"), Fixture.Material->GetEditorOnlyData()->SurfaceThickness.Expression == Custom.Get()
        && Fixture.Material->GetEditorOnlyData()->SurfaceThickness.OutputIndex == 7);
    TestTrue(TEXT("Optical root refraction receives the dedicated PNO strength output"), Fixture.Material->GetEditorOnlyData()->Refraction.Expression == Custom.Get()
        && Fixture.Material->GetEditorOnlyData()->Refraction.OutputIndex == 8);
    TestTrue(TEXT("PNO output preserves values above neutral one"), Custom->Code.Contains(TEXT("CkUsf_RefractionStrength = clamp(O.RefractionStrength, 1.0, 1.1);")));
    ck_test_usf_optical::CheckCompile(*this, Fixture);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_OpticalAtomic, "CkTests.UnitTests.CkUsf.OpticalTailLamp.ReloadRejectsChangedOpticsAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_OpticalAtomic::RunTest(const FString&) -> bool
{
    auto Fixture = ck_test_usf_optical::FFixture{};
    if (ck_test_usf_optical::CheckUnsupportedProfile(*this, Fixture))
    { return true; }
    if (NOT TestTrue(TEXT("Generate optical master"), Fixture.Generate()))
    { return false; }
    const auto Slab = Fixture.Find<UMaterialExpressionSubstrateSlabBSDF>();
    const auto Mfp = Fixture.Find<UMaterialExpressionSubstrateTransmittanceToMFP>();
    const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
    if (NOT TestTrue(TEXT("Optical graph nodes exist"), Slab.IsValid() && Mfp.IsValid() && Custom.IsValid()))
    { return false; }
    const auto Reject = [&]() { ck_test_usf_optical::CheckRejected(*this, Fixture); };
    Fixture.Material->bIsThinSurface = false; Reject(); Fixture.Material->bIsThinSurface = true;
    Fixture.Material->BlendMode = BLEND_Translucent; Reject(); Fixture.Material->BlendMode = BLEND_TranslucentColoredTransmittance;
    Fixture.Material->TranslucencyLightingMode = TLM_Surface; Reject(); Fixture.Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
    Fixture.Material->RefractionMethod = RM_IndexOfRefractionFromF0; Reject(); Fixture.Material->RefractionMethod = RM_PixelNormalOffset;
    Fixture.Material->TranslucencyPass = MTP_AfterDOF; Reject(); Fixture.Material->TranslucencyPass = MTP_BeforeDOF;
    Slab->SubSurfaceType = MSS_Diffusion; Reject(); Slab->SubSurfaceType = MSS_SimpleVolume;
    const auto Reference = Mfp->Thickness;
    Mfp->Thickness.Connect(7, Custom.Get()); Reject(); Mfp->Thickness = Reference;
    const auto Thickness = Fixture.Material->GetEditorOnlyData()->SurfaceThickness;
    Fixture.Material->GetEditorOnlyData()->SurfaceThickness.Connect(6, Custom.Get()); Reject(); Fixture.Material->GetEditorOnlyData()->SurfaceThickness = Thickness;
    Fixture.Material->GetEditorOnlyData()->SurfaceThickness.MaskR ^= 1; Reject(); Fixture.Material->GetEditorOnlyData()->SurfaceThickness = Thickness;
    Custom->AdditionalOutputs.Swap(5, 6); Reject(); Custom->AdditionalOutputs.Swap(5, 6);
    const auto OriginalOutputs = Custom->AdditionalOutputs;
    Custom->AdditionalOutputs.RemoveAt(7); Reject(); Custom->AdditionalOutputs = OriginalOutputs;
    const auto Refraction = Fixture.Material->GetEditorOnlyData()->Refraction;
    Fixture.Material->GetEditorOnlyData()->Refraction.Connect(7, Custom.Get()); Reject(); Fixture.Material->GetEditorOnlyData()->Refraction = Refraction;
    Fixture.Material->GetEditorOnlyData()->Refraction.Expression = nullptr; Reject(); Fixture.Material->GetEditorOnlyData()->Refraction = Refraction;
    Fixture.Material->GetEditorOnlyData()->Refraction.MaskR ^= 1; Reject(); Fixture.Material->GetEditorOnlyData()->Refraction = Refraction;
    Fixture.Material->GetEditorOnlyData()->Normal.Connect(3, Custom.Get()); Reject(); Fixture.Material->GetEditorOnlyData()->Normal.Expression = nullptr;
    const auto Coordinates = Fixture.Find<UMaterialExpressionTextureCoordinate>();
    if (TestTrue(TEXT("Optical input has UV coordinates"), Coordinates.IsValid()))
    { Coordinates->CoordinateIndex = 1; Reject(); Coordinates->CoordinateIndex = 0; }
    const auto OriginalDefault = Fixture.Definition->_Parameters[0]._DefaultScalar;
    Fixture.Definition->_Parameters[0]._DefaultScalar += 0.1f; Reject(); Fixture.Definition->_Parameters[0]._DefaultScalar = OriginalDefault;
    auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
    auto Errors = TArray<FString>{};
    TestTrue(TEXT("Restored graph is reloadable"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
    for (const auto& Error : Errors)
    { AddError(Error); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_OpticalReload, "CkTests.UnitTests.CkUsf.OpticalTailLamp.EntryAndHelperReloadPreserveIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_OpticalReload::RunTest(const FString&) -> bool
{
    using namespace ck::usf_editor;
    auto Fixture = ck_test_usf_optical::FFixture{};
    if (ck_test_usf_optical::CheckUnsupportedProfile(*this, Fixture))
    { return true; }
    if (NOT TestTrue(TEXT("Generate reload fixture"), Fixture.Generate()))
    { return false; }
    ck_test_usf_optical::CheckCompile(*this, Fixture);
    const auto Master = TWeakObjectPtr<UMaterial>{Fixture.Material.Get()};
    const auto Mid = TWeakObjectPtr<UMaterialInstanceDynamic>{Fixture.Mid.Get()};
    const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
    const auto Slab = Fixture.Find<UMaterialExpressionSubstrateSlabBSDF>();
    const auto Mfp = Fixture.Find<UMaterialExpressionSubstrateTransmittanceToMFP>();
    if (NOT TestTrue(TEXT("Reload graph nodes exist"), Custom.IsValid() && Slab.IsValid() && Mfp.IsValid()))
    { return false; }
    const auto OriginalCode = Custom->Code;
    const auto OriginalThickness = Fixture.Material->GetEditorOnlyData()->SurfaceThickness;
    const auto OriginalReference = Mfp->Thickness;
    const auto OriginalRefraction = Fixture.Material->GetEditorOnlyData()->Refraction;
    const auto ChangedSource = Fixture.Source.Replace(TEXT("0.2*Gain"), TEXT("0.35*Gain"))
        .Replace(TEXT("O.RefractionStrength=1.02"), TEXT("O.RefractionStrength=1.04"));
    const auto ChangedHelper = Fixture.Helper.Replace(TEXT("return 1.0"), TEXT("return 0.7"));
    for (auto Phase = 0; Phase < 3; ++Phase)
    {
        const auto State = Fixture.Material->StateId;
        const auto Entry = Phase == 2 ? Fixture.Source : ChangedSource;
        const auto Helper = Phase == 1 ? ChangedHelper : Fixture.Helper;
        if (NOT TestTrue(TEXT("Write owned numeric optical source phase"), Fixture.Write(Entry, Helper)))
        { return false; }
        FlushShaderFileCache();
        auto Prepared = FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        if (NOT TestTrue(TEXT("Numeric optical reload preflights"), Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors)))
        {
            for (const auto& Error : Errors)
            { AddError(Error); }
            return false;
        }
        {
            FMaterialUpdateContext Context;
            Context.AddMaterial(Fixture.Material.Get());
            Fixture.Material->PreEditChange(nullptr);
            Apply_LookShadingModelReload(*Fixture.Material, Prepared);
            Fixture.Material->PostEditChange();
        }
        ck_test_usf_optical::CheckCompile(*this, Fixture);
        TestTrue(TEXT("Optical source phase compiles into existing material"), Fixture.Material->StateId != State);
        TestTrue(TEXT("Master/MID/native nodes retain identity"), Master.Get() == Fixture.Material.Get() && Mid.Get() == Fixture.Mid.Get()
            && Slab.Get() == Fixture.Find<UMaterialExpressionSubstrateSlabBSDF>().Get() && Mfp.Get() == Fixture.Find<UMaterialExpressionSubstrateTransmittanceToMFP>().Get());
        TestEqual(TEXT("Numeric source changes retain generated Custom code"), Custom->Code, OriginalCode);
        TestTrue(TEXT("Calibration and surface thickness bindings remain distinct and unchanged"), ck_test_usf_optical::SameInput(OriginalThickness, Fixture.Material->GetEditorOnlyData()->SurfaceThickness)
            && ck_test_usf_optical::SameInput(OriginalReference, Mfp->Thickness));
        TestTrue(TEXT("Numeric reload retains the exact PNO root binding and mode"), ck_test_usf_optical::SameInput(OriginalRefraction, Fixture.Material->GetEditorOnlyData()->Refraction)
            && Fixture.Material->RefractionMethod == RM_PixelNormalOffset);
        TestTrue(TEXT("Live MID retains parent and optical scalar override"), Fixture.Mid->Parent == Fixture.Material.Get()
            && FMath::IsNearlyEqual(Fixture.Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_RefractiveAdmission, "CkTests.UnitTests.CkUsf.OpticalTailLamp.RefractiveAmber.RendererAndAuthoringAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_RefractiveAdmission::RunTest(const FString&) -> bool
{
    using namespace ck::usf_editor;
    auto Fixture = ck_test_usf_optical::FFixture{ECk_Usf_SurfaceGraph::NativeRefractiveSlab};
    if (NOT TestTrue(TEXT("Write refractive fixture"), Fixture.Write(Fixture.Source, Fixture.Helper)))
    { return false; }
    TestEqual(TEXT("Existing optical enum value is stable"), static_cast<int32>(ECk_Usf_SurfaceGraph::NativeTranslucentSlab), 6);
    TestEqual(TEXT("Refractive graph appends enum value seven"), static_cast<int32>(ECk_Usf_SurfaceGraph::NativeRefractiveSlab), 7);
    auto Declared = TOptional<ECk_Usf_SurfaceGraph>{};
    auto ParseError = FString{};
    TestTrue(TEXT("Refractive source declaration parses"), Parse_SurfaceGraphDeclaration(Fixture.Source, Declared, ParseError)
        && Declared.IsSet() && Declared.GetValue() == ECk_Usf_SurfaceGraph::NativeRefractiveSlab);
    auto Support = FSubstrateShadingModelSupport{};
    Support.Enabled = true;
    Support.MaxClosuresPerPixel = 1;
    auto Contract = FShadingModelContract{};
    Contract.SurfaceGraph = ECk_Usf_SurfaceGraph::NativeRefractiveSlab;
    auto Errors = TArray<FString>{};
    TestTrue(TEXT("Refractive graph needs Adaptive and one optical closure"), Requires_AdaptiveGBuffer(Contract.SurfaceGraph)
        && Is_OpticalSurfaceGraph(Contract.SurfaceGraph) && NOT Is_AdaptiveSurfaceGraph(Contract.SurfaceGraph));
    TestTrue(TEXT("Adaptive admits refractive authoring"), Classify_LookForGeneration(Fixture.Definition.Get(), Support).Admission == ELookGenerationAdmission::Compatible);
    TestTrue(TEXT("One closure admits refractive native Slab"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    TestEqual(TEXT("Refractive graph shares optical numeric output type"), Get_SurfaceOutputType(Contract), FString{TEXT("FCkUsf_TranslucentSlabOutput")});
    const auto RefractiveCode = Build_ShadingModelOutputCode(Contract);
    Contract.SurfaceGraph = ECk_Usf_SurfaceGraph::NativeTranslucentSlab;
    TestEqual(TEXT("Existing optical numeric code is reused unchanged"), RefractiveCode, Build_ShadingModelOutputCode(Contract));
    Contract.SurfaceGraph = ECk_Usf_SurfaceGraph::NativeRefractiveSlab;
    Support.MaxClosuresPerPixel = 0;
    Errors.Reset();
    TestFalse(TEXT("Zero closures reject refractive native Slab"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    Support.MaxClosuresPerPixel = 1;
    Support.BlendableGBuffer = true;
    Errors.Reset();
    TestFalse(TEXT("Blendable rejects direct refractive admission"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    TestTrue(TEXT("Valid opposite-profile refractive authoring is excluded"), Classify_LookForGeneration(Fixture.Definition.Get(), Support).Admission == ELookGenerationAdmission::OtherRendererProfile);
    Support.Enabled = false;
    Errors.Reset();
    TestFalse(TEXT("Disabled Substrate rejects refractive admission"), Validate_SubstrateShadingModelSupport(Contract, Support, Errors));
    Support.Enabled = true;
    Fixture.Definition->_BlendMode = ECk_Usf_BlendMode::Translucent;
    TestTrue(TEXT("Malformed refractive authoring is not a profile exclusion"), Classify_LookForGeneration(Fixture.Definition.Get(), Support).Admission == ELookGenerationAdmission::Invalid);
    TestFalse(TEXT("Rejected refractive authoring creates no package"), FPackageName::DoesPackageExist(Fixture.Definition->Get_GeneratedMasterPackagePath()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_RefractivePins, "CkTests.UnitTests.CkUsf.OpticalTailLamp.RefractiveAmber.GeneratesNativeRefractivePinsAndCompiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_RefractivePins::RunTest(const FString&) -> bool
{
    auto Fixture = ck_test_usf_optical::FFixture{ECk_Usf_SurfaceGraph::NativeRefractiveSlab};
    if (ck_test_usf_optical::CheckUnsupportedProfile(*this, Fixture))
    { return true; }
    if (NOT TestTrue(TEXT("Generate refractive native master"), Fixture.Generate()))
    { return false; }
    const auto Slab = Fixture.Find<UMaterialExpressionSubstrateSlabBSDF>();
    const auto Mfp = Fixture.Find<UMaterialExpressionSubstrateTransmittanceToMFP>();
    const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
    if (NOT TestTrue(TEXT("Refractive native nodes exist"), Slab.IsValid() && Mfp.IsValid() && Custom.IsValid()))
    { return false; }
    TestTrue(TEXT("Refractive Slab uses exact fixed native settings"), Fixture.Material->BlendMode == BLEND_TranslucentColoredTransmittance
        && Fixture.Material->TranslucencyLightingMode == TLM_SurfacePerPixelLighting && Fixture.Material->bIsThinSurface
        && Fixture.Material->bTangentSpaceNormal && Fixture.Material->RefractionMethod == RM_IndexOfRefractionFromF0
        && Fixture.Material->TranslucencyPass == MTP_BeforeDOF && Slab->SubSurfaceType == MSS_SimpleVolume);
    TestTrue(TEXT("Refractive FrontMaterial is the single Slab"), Fixture.Material->GetEditorOnlyData()->FrontMaterial.Expression == Slab.Get());
    TestTrue(TEXT("Live numeric F0 and normal feed native refraction"), Slab->F0.Expression == Custom.Get() && Slab->F0.OutputIndex == 4
        && Slab->Normal.Expression == Custom.Get() && Slab->Normal.OutputIndex == 3);
    TestTrue(TEXT("Refractive root has no inactive Refraction input"), Fixture.Material->GetEditorOnlyData()->Refraction.Expression == nullptr);
    TestTrue(TEXT("Refractive MFP retains calibration independent of body thickness"), Slab->SSSMFP.Expression == Mfp.Get()
        && Mfp->TransmittanceColor.Expression == Custom.Get() && Mfp->TransmittanceColor.OutputIndex == 5
        && Mfp->Thickness.Expression == Custom.Get() && Mfp->Thickness.OutputIndex == 6
        && Fixture.Material->GetEditorOnlyData()->SurfaceThickness.Expression == Custom.Get()
        && Fixture.Material->GetEditorOnlyData()->SurfaceThickness.OutputIndex == 7);
    TestEqual(TEXT("Refractive graph retains the shared optical output layout"), Custom->AdditionalOutputs.Num(), 8);
    ck_test_usf_optical::CheckCompile(*this, Fixture);
    TestTrue(TEXT("F0 refraction enables native distortion without a root expression"), Fixture.Material->bUsesDistortion);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_RefractiveAtomic, "CkTests.UnitTests.CkUsf.OpticalTailLamp.RefractiveAmber.ReloadRejectsChangedRefractionAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_RefractiveAtomic::RunTest(const FString&) -> bool
{
    using namespace ck::usf_editor;
    auto Fixture = ck_test_usf_optical::FFixture{ECk_Usf_SurfaceGraph::NativeRefractiveSlab};
    if (ck_test_usf_optical::CheckUnsupportedProfile(*this, Fixture))
    { return true; }
    if (NOT TestTrue(TEXT("Generate refractive reload fixture"), Fixture.Generate()))
    { return false; }
    const auto Slab = Fixture.Find<UMaterialExpressionSubstrateSlabBSDF>();
    const auto Custom = Fixture.Find<UMaterialExpressionCustom>();
    if (NOT TestTrue(TEXT("Refractive reload nodes exist"), Slab.IsValid() && Custom.IsValid()))
    { return false; }
    const auto Reject = [&]() { ck_test_usf_optical::CheckRejected(*this, Fixture); };
    for (const auto WrongMode : {RM_PixelNormalOffset, RM_IndexOfRefraction, RM_None})
    {
        Fixture.Material->RefractionMethod = WrongMode;
        Reject();
        Fixture.Material->RefractionMethod = RM_IndexOfRefractionFromF0;
    }
    const auto Refraction = Fixture.Material->GetEditorOnlyData()->Refraction;
    Fixture.Material->GetEditorOnlyData()->Refraction.Connect(8, Custom.Get());
    Reject();
    Fixture.Material->GetEditorOnlyData()->Refraction = Refraction;
    const auto F0 = Slab->F0;
    Slab->F0.Connect(0, Custom.Get());
    Reject();
    Slab->F0 = F0;
    const auto ChangedSource = Fixture.Source.Replace(TEXT("O.ReferenceThicknessCm=0.1;"),
        TEXT("O.F0=float3(0.065,0.065,0.065); O.ReferenceThicknessCm=0.1;"));
    if (NOT TestTrue(TEXT("Write a live F0 numeric edit"), Fixture.Write(ChangedSource, Fixture.Helper)))
    { return false; }
    FlushShaderFileCache();
    auto Prepared = FPreparedLookShadingModelReload{};
    auto Errors = TArray<FString>{};
    if (NOT TestTrue(TEXT("Restored native graph accepts the numeric F0 edit"), Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors)))
    {
        for (const auto& Error : Errors)
        { AddError(Error); }
        return false;
    }
    const auto State = Fixture.Material->StateId;
    const auto Master = TWeakObjectPtr<UMaterial>{Fixture.Material.Get()};
    const auto Mid = TWeakObjectPtr<UMaterialInstanceDynamic>{Fixture.Mid.Get()};
    {
        FMaterialUpdateContext Context;
        Context.AddMaterial(Fixture.Material.Get());
        Fixture.Material->PreEditChange(nullptr);
        Apply_LookShadingModelReload(*Fixture.Material, Prepared);
        Fixture.Material->PostEditChange();
    }
    ck_test_usf_optical::CheckCompile(*this, Fixture);
    TestTrue(TEXT("Numeric F0 edit recompiles the existing refractive material"), Fixture.Material->StateId != State);
    TestTrue(TEXT("Refractive numeric reload retains master MID and Slab identity"), Master.Get() == Fixture.Material.Get()
        && Mid.Get() == Fixture.Mid.Get() && Slab.Get() == Fixture.Find<UMaterialExpressionSubstrateSlabBSDF>().Get());
    TestTrue(TEXT("Numeric reload preserves F0-derived mode and disconnected root"), Fixture.Material->RefractionMethod == RM_IndexOfRefractionFromF0
        && ck_test_usf_optical::SameInput(Refraction, Fixture.Material->GetEditorOnlyData()->Refraction));
    TestTrue(TEXT("Numeric refractive reload retains MID parent and scalar override"), Fixture.Mid->Parent == Fixture.Material.Get()
        && FMath::IsNearlyEqual(Fixture.Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
    return true;
}

#endif
