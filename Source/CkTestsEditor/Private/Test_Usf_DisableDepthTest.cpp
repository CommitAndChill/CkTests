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
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ShaderCore.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------
// _DisableDepthTest reaches the generated master as bDisableDepthTest on a translucent surface look, stays off when
// unset, and is never baked onto an opaque look (the engine ignores it there), where the validator warns instead.
// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_usf_disable_depth_test
{
    // A unique shader mount and package root, so parallel lanes never generate into each other's files.
    struct FFixture
    {
        FString Directory;
        FString VirtualRoot;
        FString PackageRoot;
        FString PackageDirectory;

        FFixture()
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/CkUsfDepthTest") / Id);
            VirtualRoot = TEXT("/CkUsf/DepthTest_") + Id;
            PackageRoot = TEXT("/CkUsfDepthTest_") + Id + TEXT("/");
            PackageDirectory = Directory / TEXT("Packages/");

            IFileManager::Get().MakeDirectory(*PackageDirectory, true);
            // Retain this unique mount and its Saved files; never remove global shader mappings.
            AddShaderSourceDirectoryMapping(VirtualRoot, Directory);
            FPackageName::RegisterMountPoint(PackageRoot, PackageDirectory);
        }

        ~FFixture()
        {
            FPackageName::UnRegisterMountPoint(PackageRoot, PackageDirectory);
        }

        auto Write_Look() const -> bool
        {
            const auto Source = FString{TEXT(
                "#pragma once\n#include \"/CkUsf/Common.ush\"\n"
                "FCkUsf_SurfaceOutput CkUsf_Look_DepthTest(FCkUsf_SurfaceInput In, float3 Tint) "
                "{ FCkUsf_SurfaceOutput O = CkUsf_DefaultSurface(); O.EmissiveColor = Tint; return O; }\n")};
            return FFileHelper::SaveStringToFile(Source, *(Directory / TEXT("DepthTest.ush")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        auto Make_Definition(
            const TCHAR* InLookName,
            ECk_Usf_BlendMode InBlendMode,
            bool InDisableDepthTest) const
            -> TStrongObjectPtr<UCkUsf_LookDefinition>
        {
            auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{NewObject<UCkUsf_LookDefinition>()};
            Definition->_LookName = InLookName;
            Definition->_GeneratedPackageRoot = PackageRoot.LeftChop(1);
            Definition->_UshIncludePath = VirtualRoot / TEXT("DepthTest.ush");
            Definition->_UshFunctionName = TEXT("CkUsf_Look_DepthTest");
            Definition->_Domain = ECk_Usf_Domain::SurfaceUnlit;
            Definition->_BlendMode = InBlendMode;
            Definition->_DisableDepthTest = InDisableDepthTest;

            auto Tint = FCk_Usf_ParamDesc{};
            Tint._Name = TEXT("Tint");
            Tint._Type = ECk_Usf_ParamType::Vector;
            Definition->_Parameters.Add(Tint);
            return Definition;
        }
    };

    auto Retire(UMaterial& InMaterial) -> void
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (GShaderCompilingManager != nullptr)
        { GShaderCompilingManager->FinishAllCompilation(); }
        FlushRenderingCommands();

        // Loaded-object iteration can still visit garbage before GC; remove fixture reload dependencies first.
        for (const auto& Expression : InMaterial.GetExpressions())
        {
            if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
            { Custom->IncludeFilePaths.Reset(); }
        }

        InMaterial.UpdateCachedExpressionData();
        InMaterial.ClearFlags(RF_Public | RF_Standalone);
        InMaterial.MarkAsGarbage();
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Usf_DisableDepthTest,
    "CkTests.UnitTests.CkUsf.DisableDepthTestReachesATranslucentMaster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto FCkTest_Usf_DisableDepthTest::RunTest(const FString& Parameters) -> bool
{
    using namespace ck_test_usf_disable_depth_test;

    const auto Fixture = FFixture{};
    const FScopedDisableValidateOnSave DisableFixtureValidation;

    if (TestTrue(TEXT("Write the look source"), Fixture.Write_Look()) == false)
    { return false; }

    struct FCase
    {
        const TCHAR* _LookName;
        ECk_Usf_BlendMode _BlendMode;
        bool _DisableDepthTest;
        bool _ExpectedOnMaster;
        bool _ExpectsWarning;
    };

    const auto Cases = TArray<FCase>{
        {TEXT("DepthTestTranslucentOn"), ECk_Usf_BlendMode::Translucent, true, true, false},
        {TEXT("DepthTestTranslucentOff"), ECk_Usf_BlendMode::Translucent, false, false, false},
        {TEXT("DepthTestOpaqueOn"), ECk_Usf_BlendMode::Opaque, true, false, true},
    };

    for (const auto& Case : Cases)
    {
        const auto Definition = Fixture.Make_Definition(Case._LookName, Case._BlendMode, Case._DisableDepthTest);

        const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());
        TestTrue(FString::Printf(TEXT("[%s] passes validation"), Case._LookName), Validation.Errors.IsEmpty());
        for (const auto& Error : Validation.Errors)
        { AddInfo(FString::Printf(TEXT("[%s] validation error: %s"), Case._LookName, *Error)); }

        auto HasDepthTestWarning = false;
        for (const auto& Warning : Validation.Warnings)
        { HasDepthTestWarning |= Warning.Contains(TEXT("_DisableDepthTest")); }
        TestEqual(FString::Printf(TEXT("[%s] warns about the depth test only where the engine ignores it"), Case._LookName),
            HasDepthTestWarning, Case._ExpectsWarning);

        const auto Material = TStrongObjectPtr<UMaterial>{ck::usf_editor::Generate_LookMaterial(Definition.Get())};
        if (TestNotNull(FString::Printf(TEXT("[%s] generates a master"), Case._LookName), Material.Get()) == false)
        { continue; }

        ON_SCOPE_EXIT { Retire(*Material); };

        TestEqual(FString::Printf(TEXT("[%s] master's Disable Depth Test"), Case._LookName),
            static_cast<bool>(Material->bDisableDepthTest), Case._ExpectedOnMaster);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
