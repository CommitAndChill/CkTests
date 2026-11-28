#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"

#include "AssetCompilingManager.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Engine/ToonProfile.h"
#include "Factories/Factory.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
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
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_usf_native
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
        FString NestedSource = TEXT("float CkUsfNativeGain() { return 1.0; }\n");
        TStrongObjectPtr<UCkUsf_LookDefinition> Definition;
        TStrongObjectPtr<UToonProfile> Profile;
        TStrongObjectPtr<UMaterial> Material;
        TStrongObjectPtr<UMaterialInstanceDynamic> Mid;

        FFixture()
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/CkUsfNative_") + Id + TEXT("/");
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfNative") / Id);
            VirtualRoot = TEXT("/CkUsf/Native_") + Id;
            IFileManager::Get().MakeDirectory(*(Directory / TEXT("Packages")), true);
            FPackageName::RegisterMountPoint(Root, Directory / TEXT("Packages/"));
            // Shader mappings cannot be removed individually. Retain this unique mount and its source files.
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
            Definition.Reset(NewObject<UCkUsf_LookDefinition>());
            Definition->_LookName = TEXT("NativeFixture");
            Definition->_GeneratedPackageRoot = Root.LeftChop(1);
            Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
            Definition->_DirectionalShading = true;
            Definition->_UshIncludePath = VirtualRoot / TEXT("Entry.ush");
            Definition->_UshFunctionName = TEXT("CkUsf_NativeFixture");
            auto Gain = FCk_Usf_ParamDesc{};
            Gain._Name = TEXT("Gain");
            Gain._DefaultScalar = 0.4f;
            Definition->_Parameters = {Gain};
        }

        ~FFixture()
        {
            Write(Source, NestedSource);
            Drain();
            if (Material.IsValid())
            {
                // A garbage object can still be visited by a later global reload before GC.
                for (const auto& Expression : Material->GetExpressions())
                {
                    const auto Custom = TObjectPtr<UMaterialExpressionCustom>{Cast<UMaterialExpressionCustom>(Expression)};
                    if (Custom)
                    { Custom->IncludeFilePaths.Reset(); }
                }
                Material->UpdateCachedExpressionData();
                Material->ClearFlags(RF_Public | RF_Standalone);
                Material->MarkAsGarbage();
            }
            FPackageName::UnRegisterMountPoint(Root, Directory / TEXT("Packages/"));
        }

        auto Write(const FString& InSource, const FString& InNested) const -> bool
        {
            return FFileHelper::SaveStringToFile(InNested, *(Directory / TEXT("Nested.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
                && FFileHelper::SaveStringToFile(InSource, *(Directory / TEXT("Entry.ush")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        auto SaveProfile(const FString& InName, bool InSave = true) -> TStrongObjectPtr<UToonProfile>
        {
            const auto FactoryClass = TObjectPtr<UClass>{LoadClass<UFactory>(nullptr, TEXT("/Script/UnrealEd.ToonProfileFactory"))};
            if (FactoryClass == nullptr)
            { return {}; }
            // Use UFactory's exported virtual interface; the concrete editor factory has no module API export.
            const auto Factory = TStrongObjectPtr<UFactory>{NewObject<UFactory>(GetTransientPackage(), FactoryClass.Get())};
            const auto Package = TStrongObjectPtr<UPackage>{CreatePackage(*(Root + InName))};
            auto Result = TStrongObjectPtr<UToonProfile>{Cast<UToonProfile>(Factory->FactoryCreateNew(
                UToonProfile::StaticClass(), Package.Get(), FName{*InName}, RF_Public | RF_Standalone, nullptr, GWarn))};
            if (NOT Result.IsValid())
            { return {}; }
            if (NOT InSave)
            { return Result; }
            auto Args = FSavePackageArgs{};
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            Args.SaveFlags = SAVE_NoError;
            const auto Filename = FPackageName::LongPackageNameToFilename(Root + InName, FPackageName::GetAssetPackageExtension());
            if (NOT UPackage::SavePackage(Package.Get(), Result.Get(), *Filename, Args))
            { return {}; }
            return Result;
        }

        auto Configure(bool InToon, bool InTexture = false) -> bool
        {
            Definition->_SurfaceGraph = InToon ? ECk_Usf_SurfaceGraph::NativeToon : ECk_Usf_SurfaceGraph::NativeSlab;
            if (InToon)
            {
                Profile = SaveProfile(TEXT("Profile"));
                if (NOT Profile.IsValid())
                { return false; }
                Definition->_ToonProfile = Profile.Get();
            }
            if (InTexture)
            {
                auto Texture = FCk_Usf_ParamDesc{};
                Texture._Name = TEXT("Detail");
                Texture._Type = ECk_Usf_ParamType::Texture2D;
                Texture._DefaultTexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
                Definition->_Parameters.Add(Texture);
            }
            Source = FString::Printf(TEXT("// @ckusf surface_graph %s\n#pragma once\n#include \"/CkUsf/Common.ush\"\n#include \"%s/Nested.ush\"\n"
                "%s CkUsf_NativeFixture(FCkUsf_SurfaceInput In, float Gain%s) { %s O = %s(); "
                "O.%s = float3(0.2, 0.3, 0.4); O.EmissiveColor = Gain * CkUsfNativeGain() * float3(0.1, 0.2, 0.3); return O; }\n"),
                InToon ? TEXT("Toon") : TEXT("Slab"), *VirtualRoot,
                InToon ? TEXT("FCkUsf_ToonOutput") : TEXT("FCkUsf_SlabOutput"),
                InTexture ? TEXT(", Texture2D Detail, SamplerState DetailSampler") : TEXT(""),
                InToon ? TEXT("FCkUsf_ToonOutput") : TEXT("FCkUsf_SlabOutput"),
                InToon ? TEXT("CkUsf_DefaultToon") : TEXT("CkUsf_DefaultSlab"),
                InToon ? TEXT("BaseColor") : TEXT("DiffuseAlbedo"));
            return Write(Source, NestedSource);
        }

        auto Generate() -> bool
        {
            Material.Reset(ck::usf_editor::Generate_LookMaterial(Definition.Get()));
            if (NOT Material.IsValid())
            { return false; }
            Mid.Reset(UMaterialInstanceDynamic::Create(Material.Get(), GetTransientPackage()));
            if (NOT Mid.IsValid())
            { return false; }
            Mid->SetScalarParameterValue(TEXT("Gain"), 0.75f);
            return true;
        }

        auto Custom() const -> TWeakObjectPtr<UMaterialExpressionCustom>
        {
            if (Material.IsValid())
            {
                for (const auto& Expression : Material->GetExpressions())
                {
                    if (Expression->IsA<UMaterialExpressionCustom>())
                    { return Cast<UMaterialExpressionCustom>(Expression); }
                }
            }
            return {};
        }
    };

    auto SameInput(const FExpressionInput& A, const FExpressionInput& B) -> bool
    {
        return A.Expression == B.Expression && A.OutputIndex == B.OutputIndex && A.Mask == B.Mask
            && A.MaskR == B.MaskR && A.MaskG == B.MaskG && A.MaskB == B.MaskB && A.MaskA == B.MaskA;
    }

    auto NativeInputs(const UMaterial& Material) -> TArray<FExpressionInput>
    {
        const auto Front = TObjectPtr<UMaterialExpression>{Material.GetEditorOnlyData()->FrontMaterial.Expression};
        const auto Slab = TObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Front.Get())};
        if (Slab != nullptr)
        { return {Slab->DiffuseAlbedo, Slab->F0, Slab->Roughness, Slab->Normal, Slab->EmissiveColor, Slab->Anisotropy, Slab->Tangent}; }
        const auto Toon = TObjectPtr<UMaterialExpressionSubstrateToonBSDF>{Cast<UMaterialExpressionSubstrateToonBSDF>(Front.Get())};
        if (Toon != nullptr)
        { return {Toon->BaseColor, Toon->Metallic, Toon->Specular, Toon->Roughness, Toon->Normal, Toon->EmissiveColor, Toon->PatternUVs, Toon->Anisotropy, Toon->Tangent}; }
        return {};
    }

    auto RejectReload(FAutomationTestBase& Test, FFixture& Fixture) -> void
    {
        const auto Custom = Fixture.Custom();
        if (NOT Test.TestTrue(TEXT("Native primary Custom exists"), Custom.IsValid()))
        { return; }
        const auto State = Fixture.Material->StateId;
        const auto Code = Custom->Code;
        const auto Inputs = Custom->Inputs;
        const auto Outputs = Custom->AdditionalOutputs;
        const auto Defines = Custom->AdditionalDefines;
        const auto Front = Fixture.Material->GetEditorOnlyData()->FrontMaterial;
        const auto RootWpo = Fixture.Material->GetEditorOnlyData()->WorldPositionOffset;
        const auto BeforeNativeInputs = NativeInputs(*Fixture.Material);
        const auto Toon = TObjectPtr<UMaterialExpressionSubstrateToonBSDF>{Cast<UMaterialExpressionSubstrateToonBSDF>(Front.Expression)};
        const auto BeforeProfile = TWeakObjectPtr<UToonProfile>{Toon != nullptr ? Toon->ToonProfile.Get() : nullptr};
        auto Identities = TArray<TWeakObjectPtr<UMaterialExpression>>{};
        for (const auto& Expression : Fixture.Material->GetExpressions())
        { Identities.Add(Expression); }
        auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
        auto Errors = TArray<FString>{};
        Test.TestFalse(TEXT("Changed native layout is rejected"), ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
        Test.TestFalse(TEXT("Rejected preparation cannot execute"), Prepared.Custom.IsValid());
        Test.TestFalse(TEXT("Rejected layout has actionable errors"), Errors.IsEmpty());
        Test.TestEqual(TEXT("Rejected reload preserves state"), Fixture.Material->StateId, State);
        Test.TestEqual(TEXT("Rejected reload preserves code"), Custom->Code, Code);
        Test.TestEqual(TEXT("Rejected reload preserves expression count"), Fixture.Material->GetExpressions().Num(), Identities.Num());
        for (auto Index = 0; Index < Identities.Num() && Index < Fixture.Material->GetExpressions().Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected reload preserves expression identities"), Fixture.Material->GetExpressions()[Index] == Identities[Index].Get()); }
        Test.TestTrue(TEXT("Rejected reload preserves FrontMaterial"), SameInput(Front, Fixture.Material->GetEditorOnlyData()->FrontMaterial));
        Test.TestTrue(TEXT("Rejected reload preserves root WPO connection"), SameInput(RootWpo, Fixture.Material->GetEditorOnlyData()->WorldPositionOffset));
        const auto AfterNativeInputs = NativeInputs(*Fixture.Material);
        Test.TestEqual(TEXT("Rejected reload preserves native input count"), AfterNativeInputs.Num(), BeforeNativeInputs.Num());
        for (auto Index = 0; Index < BeforeNativeInputs.Num() && Index < AfterNativeInputs.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected reload preserves every native connection"), SameInput(BeforeNativeInputs[Index], AfterNativeInputs[Index])); }
        if (Toon != nullptr)
        { Test.TestTrue(TEXT("Rejected reload preserves native profile identity"), Toon->ToonProfile == BeforeProfile.Get()); }
        Test.TestEqual(TEXT("Rejected reload preserves Custom input count"), Custom->Inputs.Num(), Inputs.Num());
        for (auto Index = 0; Index < Inputs.Num() && Index < Custom->Inputs.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected reload preserves input binding"), Inputs[Index].InputName == Custom->Inputs[Index].InputName && SameInput(Inputs[Index].Input, Custom->Inputs[Index].Input)); }
        Test.TestEqual(TEXT("Rejected reload preserves output count"), Custom->AdditionalOutputs.Num(), Outputs.Num());
        for (auto Index = 0; Index < Outputs.Num() && Index < Custom->AdditionalOutputs.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected reload preserves output contract"), Outputs[Index].OutputName == Custom->AdditionalOutputs[Index].OutputName && Outputs[Index].OutputType == Custom->AdditionalOutputs[Index].OutputType); }
        Test.TestEqual(TEXT("Rejected reload preserves defines"), Custom->AdditionalDefines.Num(), Defines.Num());
        for (auto Index = 0; Index < Defines.Num() && Index < Custom->AdditionalDefines.Num(); ++Index)
        { Test.TestTrue(TEXT("Rejected reload preserves define values"), Defines[Index].DefineName == Custom->AdditionalDefines[Index].DefineName && Defines[Index].DefineValue == Custom->AdditionalDefines[Index].DefineValue); }
        Test.TestTrue(TEXT("Rejected reload preserves MID parent and scalar"), Fixture.Mid->Parent == Fixture.Material.Get()
            && FMath::IsNearlyEqual(Fixture.Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_NativePins, "CkTests.UnitTests.CkUsf.NativeBsdf.GeneratesAndWiresNativePins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_NativePins::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_native;
    for (const auto Toon : {false, true})
    {
        auto Fixture = FFixture{};
        if (NOT TestTrue(TEXT("Write native entrypoint"), Fixture.Configure(Toon)))
        { return false; }
        Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::Legacy;
        if (NOT TestTrue(TEXT("Source declaration selects native graph over asset default"), Fixture.Generate()))
        { return false; }
        const auto Custom = Fixture.Custom();
        if (NOT TestTrue(TEXT("Generated Custom exists"), Custom.IsValid()))
        { return false; }
        const auto Front = Fixture.Material->GetEditorOnlyData()->FrontMaterial;
        const auto Slab = TObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Front.Expression)};
        const auto NativeToon = TObjectPtr<UMaterialExpressionSubstrateToonBSDF>{Cast<UMaterialExpressionSubstrateToonBSDF>(Front.Expression)};
        TestTrue(TEXT("FrontMaterial connects directly to requested native BSDF"), Toon ? NativeToon != nullptr && Slab == nullptr : Slab != nullptr && NativeToon == nullptr);
        TestEqual(TEXT("FrontMaterial uses native output zero"), Front.OutputIndex, 0);
        if ((Toon && NativeToon == nullptr) || (NOT Toon && Slab == nullptr))
        { return false; }
        const auto Bindings = Toon ? TArray<TPair<FString, FExpressionInput>>{
            {TEXT("BaseColor"), NativeToon->BaseColor}, {TEXT("Metallic"), NativeToon->Metallic}, {TEXT("Specular"), NativeToon->Specular},
            {TEXT("Roughness"), NativeToon->Roughness}, {TEXT("Normal"), NativeToon->Normal}, {TEXT("EmissiveColor"), NativeToon->EmissiveColor},
            {TEXT("PatternUVs"), NativeToon->PatternUVs}, {TEXT("Anisotropy"), NativeToon->Anisotropy}, {TEXT("Tangent"), NativeToon->Tangent}}
            : TArray<TPair<FString, FExpressionInput>>{{TEXT("DiffuseAlbedo"), Slab->DiffuseAlbedo}, {TEXT("F0"), Slab->F0},
                {TEXT("Roughness"), Slab->Roughness}, {TEXT("Normal"), Slab->Normal}, {TEXT("EmissiveColor"), Slab->EmissiveColor},
                {TEXT("Anisotropy"), Slab->Anisotropy}, {TEXT("Tangent"), Slab->Tangent}};
        for (const auto& Binding : Bindings)
        {
            auto ExpectedIndex = 0;
            if (Binding.Key != (Toon ? TEXT("BaseColor") : TEXT("DiffuseAlbedo")))
            {
                const auto OutputName = Binding.Key == TEXT("Anisotropy") || Binding.Key == TEXT("Tangent") ? TEXT("CkUsf_") + Binding.Key : Binding.Key;
                const auto Index = Custom->AdditionalOutputs.IndexOfByPredicate([&](const FCustomOutput& Output) { return Output.OutputName == FName{*OutputName}; });
                TestTrue(TEXT("Native field has a named Custom output"), Index != INDEX_NONE);
                ExpectedIndex = Index + 1;
            }
            auto Expected = FExpressionInput{};
            Expected.Connect(ExpectedIndex, Custom.Get());
            TestTrue(*FString::Printf(TEXT("Native %s uses exact Custom output and mask"), *Binding.Key), SameInput(Binding.Value, Expected));
        }
        if (Toon)
        { TestTrue(TEXT("Native Toon uses persistent authored profile"), NativeToon->ToonProfile == Fixture.Profile.Get()); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_NativeInvalid, "CkTests.UnitTests.CkUsf.NativeBsdf.RejectsInvalidDeclarationsBeforeMutation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_NativeInvalid::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_native;
    for (auto Case = 0; Case < 7; ++Case)
    {
        auto Fixture = FFixture{};
        if (NOT TestTrue(TEXT("Write rejection entrypoint"), Fixture.Configure(false)))
        { return false; }
        if (Case == 0)
        { TestTrue(TEXT("Write mixed directives"), Fixture.Write(TEXT("// @ckusf shading_model DefaultLit\n") + Fixture.Source, Fixture.NestedSource)); }
        if (Case == 1)
        { Fixture.Definition->_Domain = ECk_Usf_Domain::UI; }
        if (Case == 2)
        { Fixture.Definition->_BlendMode = ECk_Usf_BlendMode::Translucent; }
        if (Case == 3)
        { Fixture.Definition->_ShadingModel = ECk_Usf_ShadingModel::DefaultLit; }
        if (Case == 4)
        { TestTrue(TEXT("Write unknown graph directive"), Fixture.Write(Fixture.Source.Replace(TEXT("surface_graph Slab"), TEXT("surface_graph Unknown")), Fixture.NestedSource)); }
        if (Case == 5)
        { TestTrue(TEXT("Write duplicate graph directive"), Fixture.Write(TEXT("// @ckusf surface_graph Slab\n") + Fixture.Source, Fixture.NestedSource)); }
        if (Case == 6)
        {
            Fixture.Definition->_WpoFunctionName = TEXT("CkUsf_NativeFixtureWpo");
            TestTrue(TEXT("Write otherwise valid native WPO function"), Fixture.Write(Fixture.Source
                + TEXT("float3 CkUsf_NativeFixtureWpo(FCkUsf_SurfaceInput In, float Gain) { return float3(0, 0, 0); }\n"), Fixture.NestedSource));
        }
        const auto Validation = ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get());
        TestFalse(TEXT("Invalid native contract fails preflight"), Validation.Errors.IsEmpty());
        TestFalse(TEXT("Preflight creates no output package"), FPackageName::DoesPackageExist(Fixture.Definition->Get_GeneratedMasterPackagePath()));
        TestNull(TEXT("Preflight creates no loaded master"), FindObject<UMaterial>(nullptr, *Fixture.Definition->Get_GeneratedMasterObjectPath()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_NativeProfiles, "CkTests.UnitTests.CkUsf.NativeBsdf.ValidatesPersistentProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_NativeProfiles::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_native;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("Create factory-authored saved Toon profile"), Fixture.Configure(true)))
    { return false; }
    Fixture.Definition->_ToonProfile = nullptr;
    TestFalse(TEXT("Missing Toon profile is rejected"), ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get()).Errors.IsEmpty());
    const auto Transient = TStrongObjectPtr<UToonProfile>{NewObject<UToonProfile>(GetTransientPackage(), NAME_None, RF_Transient)};
    Fixture.Definition->_ToonProfile = Transient.Get();
    TestFalse(TEXT("Transient profile cannot be persisted by a generated master"), ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get()).Errors.IsEmpty());
    const auto Unsaved = Fixture.SaveProfile(TEXT("UnsavedProfile"), false);
    if (NOT TestTrue(TEXT("Unsaved factory profile has otherwise valid data"), Unsaved.IsValid()))
    { return false; }
    Fixture.Definition->_ToonProfile = Unsaved.Get();
    TestFalse(TEXT("Valid but unsaved profile cannot be persisted by the master"), ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get()).Errors.IsEmpty());
    Fixture.Definition->_ToonProfile = Fixture.Profile.Get();
    TestTrue(TEXT("Saved factory profile satisfies native contract"), ck::usf_editor::Validate_LookDefinition(Fixture.Definition.Get()).Errors.IsEmpty());
    TestTrue(TEXT("Profile package exists on disk"), FPackageName::DoesPackageExist(Fixture.Profile->GetOutermost()->GetName()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_NativeAtomic, "CkTests.UnitTests.CkUsf.NativeBsdf.ReloadRejectsChangedLayoutsAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
auto FCkTest_Usf_NativeAtomic::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_native;
    for (auto Case = 0; Case < 6; ++Case)
    {
        auto Fixture = FFixture{};
        if (NOT TestTrue(TEXT("Write atomic native fixture"), Fixture.Configure(Case == 3, Case == 2)) || NOT TestTrue(TEXT("Generate atomic native master"), Fixture.Generate()))
        { return false; }
        if (Case == 0)
        {
            Fixture.Definition->_SurfaceGraph = ECk_Usf_SurfaceGraph::NativeToon;
            Fixture.Profile = Fixture.SaveProfile(TEXT("ChangedKindProfile"));
            Fixture.Definition->_ToonProfile = Fixture.Profile.Get();
            const auto ChangedSource = Fixture.Source.Replace(TEXT("Slab"), TEXT("Toon")).Replace(TEXT("DiffuseAlbedo"), TEXT("BaseColor"));
            TestTrue(TEXT("Change source-selected native graph kind with valid new return type"), Fixture.Write(ChangedSource, Fixture.NestedSource));
        }
        if (Case == 1)
        {
            const auto Slab = TObjectPtr<UMaterialExpressionSubstrateSlabBSDF>{Cast<UMaterialExpressionSubstrateSlabBSDF>(Fixture.Material->GetEditorOnlyData()->FrontMaterial.Expression)};
            if (NOT TestTrue(TEXT("Swap fixture has native Slab"), Slab != nullptr))
            { return false; }
            Swap(Slab->DiffuseAlbedo, Slab->F0);
        }
        if (Case == 2)
        {
            Fixture.Definition->_Parameters.Last()._Type = ECk_Usf_ParamType::TextureCube;
            Fixture.Definition->_Parameters.Last()._DefaultTexturePath = TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap");
            TestTrue(TEXT("Keep new source texture type consistent with new definition"), Fixture.Write(Fixture.Source.Replace(TEXT("Texture2D Detail"), TEXT("TextureCube Detail")), Fixture.NestedSource));
        }
        if (Case == 3)
        {
            const auto Other = Fixture.SaveProfile(TEXT("OtherProfile"));
            if (NOT TestTrue(TEXT("Second valid persistent profile exists"), Other.IsValid()))
            { return false; }
            Fixture.Definition->_ToonProfile = Other.Get();
            RejectReload(*this, Fixture);
            continue;
        }
        if (Case == 4)
        {
            auto Coordinates = TWeakObjectPtr<UMaterialExpressionTextureCoordinate>{};
            for (const auto& Expression : Fixture.Material->GetExpressions())
            {
                if (Expression->IsA<UMaterialExpressionTextureCoordinate>())
                { Coordinates = Cast<UMaterialExpressionTextureCoordinate>(Expression); break; }
            }
            if (NOT TestTrue(TEXT("Native source input has texture coordinates"), Coordinates.IsValid()))
            { return false; }
            const auto OriginalIndex = Coordinates->CoordinateIndex;
            Coordinates->CoordinateIndex = OriginalIndex + 1;
            RejectReload(*this, Fixture);
            TestEqual(TEXT("Rejected UV-index change is not silently repaired"), Coordinates->CoordinateIndex, OriginalIndex + 1);
            Coordinates->CoordinateIndex = OriginalIndex;
            auto Prepared = ck::usf_editor::FPreparedLookShadingModelReload{};
            auto Errors = TArray<FString>{};
            TestTrue(TEXT("Restoring the recorded UV index restores reload compatibility"),
                ck::usf_editor::Prepare_LookShadingModelReload(*Fixture.Material, Prepared, Errors));
            for (const auto& Error : Errors)
            { AddError(Error); }
            continue;
        }
        if (Case == 5)
        {
            Fixture.Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0, Fixture.Custom().Get());
        }
        RejectReload(*this, Fixture);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_NativeCompile, "CkTests.UnitTests.CkUsf.NativeBsdf.CompilesSlabAndToon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_NativeCompile::RunTest(const FString&) -> bool
{
    using namespace ck_test_usf_native;
    if (NOT FApp::CanEverRender())
    { AddError(TEXT("Native BSDF shader-map proof requires real RHI")); return false; }
    for (const auto Toon : {false, true})
    {
        auto Fixture = FFixture{};
        if (NOT TestTrue(TEXT("Write compile fixture"), Fixture.Configure(Toon)) || NOT TestTrue(TEXT("Generate compile fixture"), Fixture.Generate()))
        { return false; }
        Fixture.Material->EnsureIsComplete();
        Drain();
        if (NOT TestNotNull(TEXT("Native master has active-platform resource"), Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform)))
        { return false; }
        const auto& Resource = *Fixture.Material->GetMaterialResource(GMaxRHIShaderPlatform);
        for (const auto& Error : Resource.GetCompileErrors())
        { AddError(Error); }
        TestTrue(TEXT("Native shader map finishes and is complete"), Resource.IsCompilationFinished() && Resource.IsGameThreadShaderMapComplete() && Resource.GetGameThreadShaderMap() != nullptr);
        auto RenderComplete = false;
        ENQUEUE_RENDER_COMMAND(CkUsfNativeComplete)([&Resource, &RenderComplete](FRHICommandListImmediate&) { RenderComplete = Resource.IsRenderingThreadShaderMapComplete(); });
        FlushRenderingCommands();
        TestTrue(TEXT("Native rendering-thread shader map is complete"), RenderComplete);
    }
    return true;
}

namespace ck_test_usf_native
{
    class FReloadSequence final : public IAutomationLatentCommand
    {
    public:
        FReloadSequence(FAutomationTestBase& InTest, TSharedRef<FFixture> InFixture)
            : Test(InTest), Fixture(InFixture), OriginalMaterial(InFixture->Material.Get()), OriginalMid(InFixture->Mid.Get()),
              OriginalCustom(InFixture->Custom()), OriginalFront(InFixture->Material->GetEditorOnlyData()->FrontMaterial.Expression)
        {}

        auto Update() -> bool override
        {
            if (NOT Started)
            {
                BeforeState = Fixture->Material->StateId;
                const auto Entry = Stage < 2 ? Fixture->Source.Replace(TEXT("float3(0.1, 0.2, 0.3)"), TEXT("float3(0.3, 0.2, 0.1)")) : Fixture->Source;
                const auto Nested = Stage == 1 ? FString{TEXT("float CkUsfNativeGain() { return 0.25; }\n")} : Fixture->NestedSource;
                if (NOT Test.TestTrue(TEXT("Write actual native entry/include edit"), Fixture->Write(Entry, Nested)))
                { return true; }
                if (NOT Test.TestTrue(TEXT("Production shader reload command accepts edit"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Ck_Usf_ReloadShaders"), *GLog, GEditor->GetEditorWorldContext().World())))
                { return true; }
                Deadline = FPlatformTime::Seconds() + 90.0;
                Started = true;
                return false;
            }
            if (Fixture->Material->StateId == BeforeState)
            {
                if (FPlatformTime::Seconds() < Deadline)
                { return false; }
                Test.AddError(TEXT("Native entry/include edit never recompiled the existing master"));
                return true;
            }
            Drain();
            Test.TestTrue(TEXT("Native reload preserves all existing UObject identities"), Fixture->Material.Get() == OriginalMaterial.Get()
                && Fixture->Mid.Get() == OriginalMid.Get() && Fixture->Custom() == OriginalCustom
                && Fixture->Material->GetEditorOnlyData()->FrontMaterial.Expression == OriginalFront.Get());
            Test.TestTrue(TEXT("Native reload preserves MID parent and scalar override"), Fixture->Mid->Parent == OriginalMaterial.Get()
                && FMath::IsNearlyEqual(Fixture->Mid->K2_GetScalarParameterValue(TEXT("Gain")), 0.75f));
            auto Errors = TArray<FString>{};
            Test.TestTrue(TEXT("Reloaded native source compiles"), ck::usf_editor::Validate_LookShaderCompile(Fixture->Material.Get(), TEXT("NativeFixture"), Errors, false));
            for (const auto& Error : Errors)
            { Test.AddError(Error); }
            ++Stage;
            Started = false;
            return Stage == 3;
        }
    private:
        FAutomationTestBase& Test;
        TSharedRef<FFixture> Fixture;
        TWeakObjectPtr<UMaterial> OriginalMaterial;
        TWeakObjectPtr<UMaterialInstanceDynamic> OriginalMid;
        TWeakObjectPtr<UMaterialExpressionCustom> OriginalCustom;
        TWeakObjectPtr<UMaterialExpression> OriginalFront;
        FGuid BeforeState;
        double Deadline = 0.0;
        int32 Stage = 0;
        bool Started = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_NativeReload, "CkTests.UnitTests.CkUsf.NativeBsdf.ReloadUpdatesExistingNativeMID",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
auto FCkTest_Usf_NativeReload::RunTest(const FString&) -> bool
{
    if (NOT FApp::CanEverRender() || GEditor == nullptr)
    { AddError(TEXT("Native shader reload requires real editor RHI")); return false; }
    const auto Fixture = MakeShared<ck_test_usf_native::FFixture>();
    if (NOT TestTrue(TEXT("Write native reload fixture"), Fixture->Configure(false)) || NOT TestTrue(TEXT("Generate native reload master"), Fixture->Generate()))
    { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(ck_test_usf_native::FReloadSequence(*this, Fixture));
    return true;
}

#endif
