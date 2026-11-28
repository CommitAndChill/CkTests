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
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "ShaderCore.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_advanced
{
    auto Drain() -> void
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr)
        { GShaderCompilingManager->FinishAllCompilation(); }
        FlushRenderingCommands();
    }

    struct FFixture
    {
        FScopedDisableValidateOnSave DisableValidation;
        FString Root;
        FString Directory;
        FString VirtualRoot;
        FString Source;
        TStrongObjectPtr<UCkUsf_LookDefinition> Definition;
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;

        FFixture()
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/CkUsfAdvanced_") + Id + TEXT("/");
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfAdvanced") / Id);
            VirtualRoot = TEXT("/CkUsf/Advanced_") + Id;
            IFileManager::Get().MakeDirectory(*(Directory / TEXT("Packages")), true);
            FPackageName::RegisterMountPoint(Root, Directory / TEXT("Packages/"));
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
            Definition.Reset(NewObject<UCkUsf_LookDefinition>());
            Definition->_LookName = TEXT("AdvancedFixture");
            Definition->_GeneratedPackageRoot = Root.LeftChop(1);
            Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
            Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeSlab;
            Definition->_UshIncludePath = VirtualRoot / TEXT("Entry.ush");
            Definition->_UshFunctionName = TEXT("CkUsf_AdvancedFixture");
            auto Weight = FCk_Usf_ParamDesc{};
            Weight._Name = TEXT("FuzzWeight");
            Weight._DefaultScalar = 0.6f;
            Definition->_Parameters = {Weight};
            Source = TEXT("#pragma once\n#include \"/CkUsf/Common.ush\"\n"
                "FCkUsf_SlabOutput CkUsf_AdvancedFixture(FCkUsf_SurfaceInput In, float FuzzWeight) { "
                "FCkUsf_SlabOutput O = CkUsf_DefaultSlab(); O.DiffuseAlbedo = float3(0.08, 0.03, 0.02); "
                "O.FuzzAmount = saturate(FuzzWeight * 0.25); O.FuzzColor = float3(0.6, 0.2, 0.1); return O; }\n");
        }

        ~FFixture()
        {
            Write(Source);
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
            // Unique Saved source and its shader mapping remain valid for process-global shader caches.
            FPackageName::UnRegisterMountPoint(Root, Directory / TEXT("Packages/"));
        }

        auto Write(const FString& InSource) const -> bool
        { return FFileHelper::SaveStringToFile(InSource, *(Directory / TEXT("Entry.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); }

        auto Generate(bool InFuzz, bool InCoat = false) -> bool
        {
            Definition->_SlabFuzz = InFuzz;
            if (InCoat)
            {
                Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeSimpleClearCoat;
                Definition->_Parameters[0]._Name = TEXT("CoatWeight");
                Source = Source.Replace(TEXT("FCkUsf_SlabOutput"), TEXT("FCkUsf_CoatedOutput"))
                    .Replace(TEXT("CkUsf_DefaultSlab"), TEXT("CkUsf_DefaultCoated"))
                    .Replace(TEXT("FuzzWeight"), TEXT("CoatWeight"))
                    .Replace(TEXT("O.FuzzAmount"), TEXT("O.CoatCoverage"))
                    .Replace(TEXT("O.FuzzColor = float3(0.6, 0.2, 0.1);"), TEXT("O.CoatRoughness = 0.12;"));
            }
            if (NOT Write(Source))
            { return false; }
            Material.Reset(ck::usf_editor::Generate_LookMaterial(Definition.Get()));
            if (NOT Material.IsValid())
            { return false; }
            Mid.Reset(UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage()));
            if (NOT Mid.IsValid())
            { return false; }
            Mid->SetScalarParameterValue(Definition->_Parameters[0]._Name, 0.75f);
            return true;
        }

        auto Custom() const -> TWeakObjectPtr<UMaterialExpressionCustom>
        {
            for (const auto& Expression : Material->GetExpressions())
            {
                if (Expression->IsA<UMaterialExpressionCustom>())
                { return Cast<UMaterialExpressionCustom>(Expression); }
            }
            return {};
        }

        auto Slab() const -> TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF>
        { return Cast<UMaterialExpressionSubstrateSlabBSDF>(Material->GetEditorOnlyData()->FrontMaterial.Expression); }
    };

    auto SameInput(const FExpressionInput& A, const FExpressionInput& B) -> bool
    {
        return A.Expression == B.Expression && A.OutputIndex == B.OutputIndex && A.Mask == B.Mask
            && A.MaskR == B.MaskR && A.MaskG == B.MaskG && A.MaskB == B.MaskB && A.MaskA == B.MaskA;
    }

    auto CheckRejectedReload(FAutomationTestBase& Test, FFixture& Fixture) -> void
    {
        const auto Custom = Fixture.Custom();
        const auto Slab = Fixture.Slab();
        const auto Coat = TObjectPtr<UMaterialExpressionSubstrateSimpleClearCoatBSDF>{
            Cast<UMaterialExpressionSubstrateSimpleClearCoatBSDF>(Fixture.Material->GetEditorOnlyData()->FrontMaterial.Expression)};
        if (NOT Test.TestTrue(TEXT("Advanced fixture has native nodes"), Custom.IsValid() && (Slab.IsValid() || Coat != nullptr)))
        { return; }
        const auto State = Fixture.Material->StateId;
        const auto Code = Custom->Code;
        const auto Outputs = Custom->AdditionalOutputs;
        const auto Inputs = Custom->Inputs;
        const auto Defines = Custom->AdditionalDefines;
        const auto Front = Fixture.Material->GetEditorOnlyData()->FrontMaterial;
        const auto NativeInputs = [&]() -> TArray<FExpressionInput>
        {
            if (Slab.IsValid())
            { return {Slab->DiffuseAlbedo, Slab->F0, Slab->Roughness, Slab->Normal, Slab->EmissiveColor, Slab->FuzzAmount, Slab->FuzzColor}; }
            return {Coat->DiffuseAlbedo, Coat->F0, Coat->Roughness, Coat->Normal, Coat->EmissiveColor,
                Coat->ClearCoatCoverage, Coat->ClearCoatRoughness, Coat->BottomNormal};
        };
        const auto NativeBefore = NativeInputs();
        auto Identities = TArray<TWeakObjectPtr<UMaterialExpression>>{};
        for (const auto& Expression : Fixture.Material->GetExpressions())
        { Identities.Add(Expression); }
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        Test.TestFalse(TEXT("Advanced layout mutation requires regeneration"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
        Test.TestFalse(TEXT("Rejected advanced preparation is not runnable"), Prepared.Custom.IsValid());
        Test.TestFalse(TEXT("Advanced rejection supplies errors"), Errors.IsEmpty());
        Test.TestEqual(TEXT("Rejected advanced reload preserves material state"), Fixture.Material->StateId, State);
        Test.TestEqual(TEXT("Rejected advanced reload preserves code"), Custom->Code, Code);
        Test.TestEqual(TEXT("Rejected advanced reload preserves graph size"), Fixture.Material->GetExpressions().Num(), Identities.Num());
        for (auto Index = 0; Index < Identities.Num() && Index < Fixture.Material->GetExpressions().Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected advanced reload preserves graph identity"), Fixture.Material->GetExpressions()[Index] == Identities[Index].Get()); }
        Test.TestTrue(TEXT("Rejected advanced reload preserves FrontMaterial"), SameInput(Front, Fixture.Material->GetEditorOnlyData()->FrontMaterial));
        const auto NativeAfter = NativeInputs();
        for (auto Index = 0; Index < NativeBefore.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected advanced reload preserves native wiring"), SameInput(NativeBefore[Index], NativeAfter[Index])); }
        Test.TestEqual(TEXT("Rejected advanced reload preserves Custom inputs"), Custom->Inputs.Num(), Inputs.Num());
        for (auto Index = 0; Index < Inputs.Num() && Index < Custom->Inputs.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected advanced reload preserves Custom input wiring"), Inputs[Index].InputName == Custom->Inputs[Index].InputName && SameInput(Inputs[Index].Input, Custom->Inputs[Index].Input)); }
        Test.TestEqual(TEXT("Rejected advanced reload preserves outputs"), Custom->AdditionalOutputs.Num(), Outputs.Num());
        for (auto Index = 0; Index < Outputs.Num() && Index < Custom->AdditionalOutputs.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected advanced reload preserves output types/order"), Outputs[Index].OutputName == Custom->AdditionalOutputs[Index].OutputName && Outputs[Index].OutputType == Custom->AdditionalOutputs[Index].OutputType); }
        Test.TestEqual(TEXT("Rejected advanced reload preserves defines"), Custom->AdditionalDefines.Num(), Defines.Num());
        for (auto Index = 0; Index < Defines.Num() && Index < Custom->AdditionalDefines.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected advanced reload preserves define values"), Defines[Index].DefineName == Custom->AdditionalDefines[Index].DefineName && Defines[Index].DefineValue == Custom->AdditionalDefines[Index].DefineValue); }
        Test.TestTrue(TEXT("Rejected advanced reload preserves MID parent/override"), Fixture.Mid->Parent == Fixture.Material.Get()
            && FMath::IsNearlyEqual(Fixture.Mid->K2_GetScalarParameterValue(Fixture.Definition->_Parameters[0]._Name), 0.75f));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedFuzzPins, "CkTests.UnitTests.CkUsf.AdvancedBsdf.FuzzPinsAndDefaultCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdvancedFuzzPins::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_advanced;
    for (const auto Fuzz : {false, true})
    {
        auto Fixture = FFixture{};
        TestFalse(TEXT("New definitions leave fuzz disabled"), Fixture.Definition->_SlabFuzz);
        if (NOT TestTrue(TEXT("Generate optional fuzz graph"), Fixture.Generate(Fuzz)))
        { return false; }
        const auto Slab = Fixture.Slab();
        const auto Custom = Fixture.Custom();
        if (NOT TestTrue(TEXT("Native Slab and Custom exist"), Slab.IsValid() && Custom.IsValid()))
        { return false; }
        TestEqual(TEXT("Fuzz adds only two outputs to the existing four-output Slab contract"), Custom->AdditionalOutputs.Num(), Fuzz ? 6 : 4);
        for (const auto& Binding : TArray<TPair<FName, FExpressionInput>>{{TEXT("CkUsf_FuzzAmount"), Slab->FuzzAmount}, {TEXT("CkUsf_FuzzColor"), Slab->FuzzColor}})
        {
            const auto Index = Custom->AdditionalOutputs.IndexOfByPredicate([&](const FCustomOutput& Output) { return Output.OutputName == Binding.Key; });
            if (Fuzz)
            {
                if (NOT TestTrue(TEXT("Enabled fuzz has a named additional output"), Index != INDEX_NONE))
                { return false; }
                TestTrue(TEXT("Fuzz output uses its native scalar/vector type"), Custom->AdditionalOutputs[Index].OutputType ==
                    (Binding.Key == TEXT("CkUsf_FuzzAmount") ? CMOT_Float1 : CMOT_Float3));
                auto Expected = FExpressionInput{};
                Expected.Connect(Index + 1, Custom.Get());
                TestTrue(TEXT("Fuzz pin uses exact output index and masks"), SameInput(Binding.Value, Expected));
            }
            else
            {
                TestEqual(TEXT("Default graph emits no fuzz output"), Index, INDEX_NONE);
                TestNull(TEXT("Default graph connects no fuzz feature pin"), Binding.Value.Expression);
            }
        }
        TestNull(TEXT("Blendable fuzz roughness stays unconnected"), Slab->FuzzRoughness.Expression);
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        TestTrue(TEXT("Unchanged default/optional fuzz graph remains reload compatible"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
        for (const auto& Error : Errors)
        { AddError(Error); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedFuzzInvalid, "CkTests.UnitTests.CkUsf.AdvancedBsdf.FuzzRejectsIncompatibleOptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdvancedFuzzInvalid::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_advanced;
    for (auto Case = 0; Case < 3; ++Case)
    {
        auto Fixture = FFixture{};
        Fixture.Definition->_SlabFuzz = true;
        if (Case == 0) { Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::Legacy; }
        if (Case == 1) { Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeToon; }
        if (Case == 2) { Fixture.Definition->_DirectionalShading = true; }
        if (NOT TestTrue(TEXT("Write invalid-option source"), Fixture.Write(Fixture.Source)))
        { return false; }
        const auto Validation = ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get());
        TestTrue(TEXT("Invalid fuzz option has a specific fuzz diagnostic"), Validation.Errors.ContainsByPredicate([](const FString& Error)
        { return Error.Contains(TEXT("fuzz"), ESearchCase::IgnoreCase); }));
        TestFalse(TEXT("Rejected options create no package"), FPackageName::DoesPackageExist(Fixture.Definition->Get_GeneratedMasterPackagePath()));
        TestNull(TEXT("Rejected options create no loaded master"), FindObject<UMaterial>(nullptr, *Fixture.Definition->Get_GeneratedMasterObjectPath()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedFuzzAtomic, "CkTests.UnitTests.CkUsf.AdvancedBsdf.FuzzReloadRejectsChangedLayoutsAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdvancedFuzzAtomic::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_advanced;
    for (auto Case = 0; Case < 4; ++Case)
    {
        auto Fixture = FFixture{};
        const auto Enabled = Case != 0;
        if (NOT TestTrue(TEXT("Generate atomic fuzz fixture"), Fixture.Generate(Enabled)))
        { return false; }
        const auto Slab = Fixture.Slab();
        if (NOT TestTrue(TEXT("Atomic fuzz fixture contains native Slab"), Slab.IsValid()))
        { return false; }
        if (Case < 2)
        { Fixture.Definition->_SlabFuzz = NOT Enabled; }
        else if (Case == 2)
        { Swap(Slab->FuzzColor, Slab->F0); }
        else
        { Swap(Slab->FuzzAmount, Slab->Roughness); }
        CheckRejectedReload(*this, Fixture);
        if (Case < 2) { Fixture.Definition->_SlabFuzz = Enabled; }
        else if (Case == 2) { Swap(Slab->FuzzColor, Slab->F0); }
        else { Swap(Slab->FuzzAmount, Slab->Roughness); }
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        TestTrue(TEXT("Restoring the exact fuzz topology restores reload compatibility"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
        for (const auto& Error : Errors)
        { AddError(Error); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedFuzzCompile, "CkTests.UnitTests.CkUsf.AdvancedBsdf.FuzzCompilesOnRealRHI",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdvancedFuzzCompile::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_advanced;
    if (NOT FApp::CanEverRender())
    { AddError(TEXT("Native fuzz compile proof requires real RHI")); return false; }
    for (const auto Enabled : {false, true})
    {
        auto Fixture = FFixture{};
        if (NOT TestTrue(TEXT("Generate native fuzz compile fixture"), Fixture.Generate(Enabled)))
        { return false; }
        Fixture.Material->EnsureIsComplete();
        Drain();
        if (NOT TestNotNull(TEXT("Native fuzz resource exists"), Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform)))
        { return false; }
        const auto& Resource = *Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform);
        for (const auto& Error : Resource.GetCompileErrors())
        { AddError(Error); }
        TestTrue(TEXT("Native fuzz shader map is complete"), Resource.IsCompilationFinished() && Resource.IsGameThreadShaderMapComplete() && Resource.GetGameThreadShaderMap() != nullptr);
        auto RenderComplete = false;
        ENQUEUE_RENDER_COMMAND(CkUsfAdvancedComplete)([&Resource, &RenderComplete](FRHICommandListImmediate&) { RenderComplete = Resource.IsRenderingThreadShaderMapComplete(); });
        FlushRenderingCommands();
        TestTrue(TEXT("Native fuzz render-thread shader map is complete"), RenderComplete);
    }
    return true;
}

namespace ck_test_usf_advanced
{
    class FReloadSequence final : public IAutomationLatentCommand
    {
    public:
        FReloadSequence(FAutomationTestBase& InTest, TSharedRef<FFixture> InFixture)
            : Test(InTest), Fixture(InFixture), OriginalCustom(InFixture->Custom()), OriginalSlab(InFixture->Slab()),
              OriginalMaterial(InFixture->Material.Get()), OriginalMid(InFixture->Mid.Get()),
              OriginalFront(InFixture->Material->GetEditorOnlyData()->FrontMaterial.Expression)
        {}

        auto Update() -> bool override
        {
            if (NOT Started)
            {
                BeforeState = Fixture->Material->StateId;
                const auto Source = Stage == 0 ? Fixture->Source.Replace(TEXT(" * 0.25"), TEXT(" * 0.8"))
                    .Replace(TEXT("O.CoatRoughness = 0.12;"), TEXT("O.CoatRoughness = 0.6;")) : Fixture->Source;
                if (NOT Test.TestTrue(TEXT("Write numeric advanced edit/recovery"), Fixture->Write(Source)))
                { return true; }
                if (NOT Test.TestTrue(TEXT("Dispatch production numeric advanced reload"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, GEditor->GetEditorWorldContext().World())))
                { return true; }
                Deadline = FPlatformTime::Seconds() + 90.0;
                Started = true;
                return false;
            }
            if (Fixture->Material->StateId == BeforeState)
            {
                if (FPlatformTime::Seconds() < Deadline)
                { return false; }
                Test.AddError(TEXT("Numeric native advanced edit failed to reload the existing master"));
                return true;
            }
            Drain();
            Test.TestTrue(TEXT("Numeric advanced edit retains every native UObject"), Fixture->Custom() == OriginalCustom && Fixture->Slab() == OriginalSlab
                && Fixture->Material.Get() == OriginalMaterial.Get() && Fixture->Mid.Get() == OriginalMid.Get()
                && Fixture->Material->GetEditorOnlyData()->FrontMaterial.Expression == OriginalFront.Get());
            Test.TestTrue(TEXT("Numeric advanced reload retains MID parent and override"), Fixture->Mid->Parent == OriginalMaterial.Get()
                && FMath::IsNearlyEqual(Fixture->Mid->K2_GetScalarParameterValue(Fixture->Definition->_Parameters[0]._Name), 0.75f));
            auto Errors = TArray<FString>{};
            Test.TestTrue(TEXT("Numeric native advanced shader compiles"), ck::usf_editor::Validate_LookShaderCompile(Fixture->Material.Get(), TEXT("AdvancedFixture"), Errors, false));
            for (const auto& Error : Errors)
            { Test.AddError(Error); }
            ++Stage;
            Started = false;
            return Stage == 2;
        }
    private:
        FAutomationTestBase& Test;
        TSharedRef<FFixture> Fixture;
        TWeakObjectPtr<UMaterialExpressionCustom> OriginalCustom;
        TWeakObjectPtr<UMaterialExpressionSubstrateSlabBSDF> OriginalSlab;
        TWeakObjectPtr<UMaterial> OriginalMaterial;
        TWeakObjectPtr<UMaterialInstanceDynamic> OriginalMid;
        TWeakObjectPtr<UMaterialExpression> OriginalFront;
        FGuid BeforeState;
        double Deadline = 0.0;
        int32 Stage = 0;
        bool Started = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedFuzzReload, "CkTests.UnitTests.CkUsf.AdvancedBsdf.FuzzNumericReloadPreservesExistingMID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdvancedFuzzReload::RunTest(const FString&) -> bool
{
    if (NOT FApp::CanEverRender() || GEditor == nullptr)
    { AddError(TEXT("Numeric native fuzz reload requires real editor RHI")); return false; }
    const auto Fixture = MakeShared<ck_test_usf_advanced::FFixture>();
    if (NOT TestTrue(TEXT("Generate numeric fuzz reload fixture"), Fixture->Generate(true)))
    { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(ck_test_usf_advanced::FReloadSequence(*this, Fixture));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedCoatPins, "CkTests.UnitTests.CkUsf.AdvancedBsdf.CoatUsesExactNativePins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdvancedCoatPins::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_advanced;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("Generate native simple clear coat"), Fixture.Generate(false, true)))
    { return false; }
    const auto Coat = TObjectPtr<UMaterialExpressionSubstrateSimpleClearCoatBSDF>{
        Cast<UMaterialExpressionSubstrateSimpleClearCoatBSDF>(Fixture.Material->GetEditorOnlyData()->FrontMaterial.Expression)};
    const auto Custom = Fixture.Custom();
    if (NOT TestTrue(TEXT("FrontMaterial is the native SimpleClearCoat node"), Coat != nullptr && Custom.IsValid()))
    { return false; }
    TestFalse(TEXT("Coated graph has no Slab front node"), Fixture.Slab().IsValid());
    TestEqual(TEXT("Coated contract has exactly six additional outputs"), Custom->AdditionalOutputs.Num(), 6);
    auto Primary = FExpressionInput{};
    Primary.Connect(0, Custom.Get());
    TestTrue(TEXT("Diffuse albedo uses the primary output"), SameInput(Coat->DiffuseAlbedo, Primary));
    const auto Names = TArray<FName>{TEXT("EmissiveColor"), TEXT("Roughness"), TEXT("Normal"), TEXT("F0"), TEXT("CkUsf_CoatCoverage"), TEXT("CkUsf_CoatRoughness")};
    const auto Pins = TArray<FExpressionInput>{Coat->EmissiveColor, Coat->Roughness, Coat->Normal, Coat->F0, Coat->ClearCoatCoverage, Coat->ClearCoatRoughness};
    for (auto Index = 0; Index < Names.Num() && Index < Custom->AdditionalOutputs.Num(); ++Index)
    {
        TestTrue(TEXT("Coat output ordering matches its distinct contract"), Custom->AdditionalOutputs[Index].OutputName == Names[Index]);
        TestTrue(TEXT("Coat output scalar/vector types match native pins"), Custom->AdditionalOutputs[Index].OutputType ==
            ((Index == 1 || Index >= 4) ? CMOT_Float1 : CMOT_Float3));
        auto Expected = FExpressionInput{};
        Expected.Connect(Index + 1, Custom.Get());
        TestTrue(TEXT("Coat pin uses exact output index and masks"), SameInput(Pins[Index], Expected));
    }
    TestNull(TEXT("Bottom normal remains disconnected for the supported single-normal coat"), Coat->BottomNormal.Expression);
    auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
    auto Errors = TArray<FString>{};
    TestTrue(TEXT("Unchanged coated graph remains reload compatible"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
    for (const auto& Error : Errors) { AddError(Error); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedCoatAtomic, "CkTests.UnitTests.CkUsf.AdvancedBsdf.CoatRejectsUnsupportedOptionsAndChangedLayoutsAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_AdvancedCoatAtomic::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_advanced;
    for (auto Case = 0; Case < 4; ++Case)
    {
        auto Fixture = FFixture{};
        if (NOT TestTrue(TEXT("Generate atomic coat fixture"), Fixture.Generate(false, true)))
        { return false; }
        const auto Coat = TObjectPtr<UMaterialExpressionSubstrateSimpleClearCoatBSDF>{
            Cast<UMaterialExpressionSubstrateSimpleClearCoatBSDF>(Fixture.Material->GetEditorOnlyData()->FrontMaterial.Expression)};
        if (NOT TestNotNull(TEXT("Coat node exists"), Coat.Get())) { return false; }
        if (Case == 0)
        {
            Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeSlab;
            const auto SlabSource = Fixture.Source.Replace(TEXT("FCkUsf_CoatedOutput"), TEXT("FCkUsf_SlabOutput"))
                .Replace(TEXT("CkUsf_DefaultCoated"), TEXT("CkUsf_DefaultSlab"))
                .Replace(TEXT("O.CoatCoverage = saturate(CoatWeight * 0.25);"), TEXT("O.Roughness = saturate(CoatWeight * 0.25);"))
                .Replace(TEXT("O.CoatRoughness = 0.12;"), TEXT(""));
            if (NOT TestTrue(TEXT("Write valid Slab source for graph-kind change"), Fixture.Write(SlabSource))) { return false; }
        }
        if (Case == 1) { Fixture.Definition->_DirectionalShading = true; }
        if (Case == 2) { Fixture.Definition->_SlabFuzz = true; }
        if (Case == 3) { Swap(Coat->ClearCoatCoverage, Coat->Roughness); }
        CheckRejectedReload(*this, Fixture);
        if (Case == 0)
        {
            Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeSimpleClearCoat;
            if (NOT TestTrue(TEXT("Restore exact coated source"), Fixture.Write(Fixture.Source))) { return false; }
        }
        if (Case == 1) { Fixture.Definition->_DirectionalShading = false; }
        if (Case == 2) { Fixture.Definition->_SlabFuzz = false; }
        if (Case == 3) { Swap(Coat->ClearCoatCoverage, Coat->Roughness); }
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        TestTrue(TEXT("Restored coat contract is reload compatible"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
        for (const auto& Error : Errors) { AddError(Error); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedCoatCompile, "CkTests.UnitTests.CkUsf.AdvancedBsdf.CoatCompilesOnRealRHI",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdvancedCoatCompile::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_advanced;
    if (NOT FApp::CanEverRender()) { AddError(TEXT("Native coat compile proof requires real RHI")); return false; }
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("Generate native coat compile fixture"), Fixture.Generate(false, true))) { return false; }
    Fixture.Material->EnsureIsComplete();
    Drain();
    if (NOT TestNotNull(TEXT("Native coat resource exists"), Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform))) { return false; }
    const auto& Resource = *Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform);
    for (const auto& Error : Resource.GetCompileErrors()) { AddError(Error); }
    TestTrue(TEXT("Native coat game-thread shader map is complete"), Resource.IsCompilationFinished() && Resource.IsGameThreadShaderMapComplete() && Resource.GetGameThreadShaderMap() != nullptr);
    auto RenderComplete = false;
    ENQUEUE_RENDER_COMMAND(CkUsfAdvancedCoatComplete)([&Resource, &RenderComplete](FRHICommandListImmediate&) { RenderComplete = Resource.IsRenderingThreadShaderMapComplete(); });
    FlushRenderingCommands();
    TestTrue(TEXT("Native coat render-thread shader map is complete"), RenderComplete);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_AdvancedCoatReload, "CkTests.UnitTests.CkUsf.AdvancedBsdf.CoatNumericReloadPreservesExistingMID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_AdvancedCoatReload::RunTest(const FString&) -> bool
{
    if (NOT FApp::CanEverRender() || GEditor == nullptr) { AddError(TEXT("Numeric native coat reload requires real editor RHI")); return false; }
    const auto Fixture = MakeShared<ck_test_usf_advanced::FFixture>();
    if (NOT TestTrue(TEXT("Generate numeric coat reload fixture"), Fixture->Generate(false, true))) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(ck_test_usf_advanced::FReloadSequence(*this, Fixture));
    return true;
}

#endif
