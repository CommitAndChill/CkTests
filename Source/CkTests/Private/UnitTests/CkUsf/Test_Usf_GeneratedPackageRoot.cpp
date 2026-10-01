// Contract gate for UCkUsf_LookDefinition::_GeneratedPackageRoot: where a look's generated master is saved and
// where the runtime looks for it.
//
//   DefaultRootIsUnchanged / CustomRootIsUsed / TestOverrideWins — the path resolution. An empty root must give
//     exactly the path every shipped look has always had, so no existing master moves.
//   InvalidRootIsRejected — the validator's root rule. A rejected look must never reach the point where
//     generation creates a package.
//   LookValidator.CustomPrimitiveDataIndexOutOfRangeIsRejected — the validator's custom primitive data range rule,
//     kept here because it shares this file's probe look.
//   CustomRootGeneratesAndResolves — the round trip on a real renderer: the generator writes the master under
//     the definition's root and the runtime lookup finds it there.

#include "Misc/AutomationTest.h"

#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "CkCore/Macros/CkMacros.h"

#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsf/LookDefinition/CkUsf_LookDefinition_Naming.h"

#include "../CkUnitTest_Common.h"

#if WITH_EDITOR
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Materials/Material.h"

#include "CkUsf/Apply/CkUsf_Utils.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"

#include "CkUsf_TestLookMasters.h"
#endif // WITH_EDITOR

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_usf_generated_package_root
{
    constexpr auto kLookName = TEXT("X");
    constexpr auto kCustomRoot = TEXT("/Game/__CkUsfTest");
    constexpr auto kOverrideRoot = TEXT("/Game/__CkUsfTestOverride");

    auto
        Make_LookDefinition(
            const TCHAR* InLookName,
            const FString& InGeneratedPackageRoot)
        -> UCkUsf_LookDefinition*
    {
        auto* Definition = NewObject<UCkUsf_LookDefinition>(GetTransientPackage());
        Definition->_LookName = FName(InLookName);
        Definition->_GeneratedPackageRoot = InGeneratedPackageRoot;
        return Definition;
    }
}

// --------------------------------------------------------------------------------------------------------------------

using ck::tests::kCkUnitTestFlags;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_GeneratedPackageRoot_DefaultRootIsUnchanged,
    "CkTests.UnitTests.CkUsf.GeneratedPackageRoot.DefaultRootIsUnchanged",
    kCkUnitTestFlags)

bool FCkTest_Usf_GeneratedPackageRoot_DefaultRootIsUnchanged::RunTest(const FString& Parameters)
{
    using namespace ck_test_usf_generated_package_root;

    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{Make_LookDefinition(kLookName, FString{})};

    TestEqual(TEXT("an empty root resolves to the framework root"),
        Definition->Get_EffectiveGeneratedPackageRoot(), FString{TEXT("/CkFoundation/CkUsf/GeneratedLooks")});

    TestEqual(TEXT("an empty root gives the package path every shipped look has always had"),
        Definition->Get_GeneratedMasterPackagePath(),
        FString{TEXT("/CkFoundation/CkUsf/GeneratedLooks/M_CkUsf_Look_X")});

    TestEqual(TEXT("an empty root gives the object path every shipped look has always had"),
        Definition->Get_GeneratedMasterObjectPath(),
        FString{TEXT("/CkFoundation/CkUsf/GeneratedLooks/M_CkUsf_Look_X.M_CkUsf_Look_X")});

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_GeneratedPackageRoot_CustomRootIsUsed,
    "CkTests.UnitTests.CkUsf.GeneratedPackageRoot.CustomRootIsUsed",
    kCkUnitTestFlags)

bool FCkTest_Usf_GeneratedPackageRoot_CustomRootIsUsed::RunTest(const FString& Parameters)
{
    using namespace ck_test_usf_generated_package_root;

    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{Make_LookDefinition(kLookName, kCustomRoot)};

    TestEqual(TEXT("a set root is the effective root"),
        Definition->Get_EffectiveGeneratedPackageRoot(), FString{kCustomRoot});

    TestEqual(TEXT("the package path sits under the definition's root"),
        Definition->Get_GeneratedMasterPackagePath(), FString{TEXT("/Game/__CkUsfTest/M_CkUsf_Look_X")});

    TestEqual(TEXT("the object path sits under the definition's root"),
        Definition->Get_GeneratedMasterObjectPath(), FString{TEXT("/Game/__CkUsfTest/M_CkUsf_Look_X.M_CkUsf_Look_X")});

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_GeneratedPackageRoot_TestOverrideWins,
    "CkTests.UnitTests.CkUsf.GeneratedPackageRoot.TestOverrideWins",
    kCkUnitTestFlags)

bool FCkTest_Usf_GeneratedPackageRoot_TestOverrideWins::RunTest(const FString& Parameters)
{
    using namespace ck_test_usf_generated_package_root;

    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{Make_LookDefinition(kLookName, kCustomRoot)};

    TestEqual(TEXT("the override beats the definition's root for the package path"),
        Definition->Get_GeneratedMasterPackagePath(kOverrideRoot),
        FString{TEXT("/Game/__CkUsfTestOverride/M_CkUsf_Look_X")});

    TestEqual(TEXT("the override beats the definition's root for the object path"),
        Definition->Get_GeneratedMasterObjectPath(kOverrideRoot),
        FString{TEXT("/Game/__CkUsfTestOverride/M_CkUsf_Look_X.M_CkUsf_Look_X")});

    TestEqual(TEXT("the override does not change what the definition reports as its own root"),
        Definition->Get_EffectiveGeneratedPackageRoot(), FString{kCustomRoot});

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#if WITH_EDITOR

namespace ck_test_usf_generated_package_root
{
    // The smallest shipped entry point: one Scalar after In, no opted-in inputs. Borrowed so the subjects carry
    // no content of their own.
    constexpr auto kProbeIncludePath = TEXT("/CkUsf/Looks/StylizeProbe.ush");
    constexpr auto kProbeFunctionName = TEXT("CkUsf_Look_StylizeProbeMinimal");
    constexpr auto kProbeParamName = TEXT("ProbeAmount");

    constexpr auto kGenerationProbeLookName = TEXT("GeneratedPackageRootProbe");

    // Matched against the validator's wording, so a rule that stops firing cannot hide behind an unrelated error.
    constexpr auto kRootErrorMarker = TEXT("_GeneratedPackageRoot");
    constexpr auto kCustomPrimitiveDataRangeErrorMarker = TEXT("past the last custom primitive data index");

    auto
        Make_ProbeLookDefinition(
            const TCHAR* InLookName,
            const FString& InGeneratedPackageRoot)
        -> UCkUsf_LookDefinition*
    {
        auto* Definition = Make_LookDefinition(InLookName, InGeneratedPackageRoot);
        Definition->_UshIncludePath = kProbeIncludePath;
        Definition->_UshFunctionName = FName(kProbeFunctionName);
        Definition->_Domain = ECk_Usf_Domain::PostProcess;

        FCk_Usf_ParamDesc Param;
        Param._Name = FName(kProbeParamName);
        Param._Type = ECk_Usf_ParamType::Scalar;
        Param._DefaultScalar = 1.0f;
        Definition->_Parameters = { Param };

        return Definition;
    }

    auto
        Get_HasErrorContaining(
            const ck::usf_editor::FLookValidationResult& InResult,
            const TCHAR* InMarker)
        -> bool
    {
        return InResult.Errors.ContainsByPredicate([&](const FString& InError) -> bool
        {
            return InError.Contains(InMarker);
        });
    }

    auto
        Make_CustomPrimitiveDataParam(
            const ECk_Usf_ParamType InType,
            const int32 InIndex)
        -> FCk_Usf_ParamDesc
    {
        FCk_Usf_ParamDesc Param;
        Param._Name = FName(kProbeParamName);
        Param._Type = InType;
        Param._CustomPrimitiveData = true;
        Param._CustomPrimitiveDataIndex = InIndex;
        return Param;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_GeneratedPackageRoot_InvalidRootIsRejected,
    "CkTests.UnitTests.CkUsf.GeneratedPackageRoot.InvalidRootIsRejected",
    kCkUnitTestFlags)

bool FCkTest_Usf_GeneratedPackageRoot_InvalidRootIsRejected::RunTest(const FString& Parameters)
{
    using namespace ck_test_usf_generated_package_root;

    // The generation attempt below logs the same validation error it returns.
    AddExpectedErrorPlain(kRootErrorMarker, EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/-1);

    for (const auto* InvalidRoot : {
             TEXT("NoLeadingSlash"),
             TEXT("/Game/Trailing/"),
             TEXT("/NotAMountedRoot/X"),
             TEXT("/Engine/__CkUsfTest"),
             TEXT("/Game/Bad!Character"),
             TEXT("/Game//DoubleSlash") })
    {
        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
            Make_ProbeLookDefinition(kLookName, InvalidRoot)};

        const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());

        TestTrue(*FString::Printf(TEXT("root [%s] is a validation error"), InvalidRoot),
            Get_HasErrorContaining(Validation, kRootErrorMarker));

        TestTrue(*FString::Printf(TEXT("the error for root [%s] names the root"), InvalidRoot),
            Get_HasErrorContaining(Validation, InvalidRoot));

        TestTrue(*FString::Printf(TEXT("the error for root [%s] names the look"), InvalidRoot),
            Get_HasErrorContaining(Validation, *FString::Printf(TEXT("Look [%s]"), kLookName)));

        const auto WouldBePackagePath = Definition->Get_GeneratedMasterPackagePath();

        TestNull(*FString::Printf(TEXT("generation refuses a look whose root is [%s]"), InvalidRoot),
            ck::usf_editor::Generate_LookMaterial(Definition.Get()));

        TestNull(*FString::Printf(TEXT("no package exists at [%s] after the refused generation"), *WouldBePackagePath),
            FindPackage(nullptr, *WouldBePackagePath));
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_LookValidator_CustomPrimitiveDataIndexOutOfRangeIsRejected,
    "CkTests.UnitTests.CkUsf.LookValidator.CustomPrimitiveDataIndexOutOfRangeIsRejected",
    kCkUnitTestFlags)

bool FCkTest_Usf_LookValidator_CustomPrimitiveDataIndexOutOfRangeIsRejected::RunTest(const FString& Parameters)
{
    using namespace ck_test_usf_generated_package_root;

    struct FCase
    {
        ECk_Usf_ParamType Type;
        int32 Index;
        bool ExpectRejected;
        const TCHAR* Description;
    };

    const FCase Cases[] =
    {
        { ECk_Usf_ParamType::Scalar, 36, true,  TEXT("a Scalar at index 36") },
        { ECk_Usf_ParamType::Vector, 33, true,  TEXT("a Vector at index 33 (reads 33..36)") },
        { ECk_Usf_ParamType::Scalar, 35, false, TEXT("a Scalar at index 35") },
        { ECk_Usf_ParamType::Vector, 32, false, TEXT("a Vector at index 32 (reads 32..35)") },
    };

    for (const auto& Case : Cases)
    {
        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
            Make_ProbeLookDefinition(kLookName, FString{})};
        Definition->_Parameters = { Make_CustomPrimitiveDataParam(Case.Type, Case.Index) };

        // Only the range rule is under test: an accepted Vector still fails the probe's float signature, which
        // is a different error and says nothing about the index.
        const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());
        const auto RangeRejected = Get_HasErrorContaining(Validation, kCustomPrimitiveDataRangeErrorMarker);

        TestEqual(*FString::Printf(TEXT("%s is %s by the custom primitive data range rule"),
            Case.Description, Case.ExpectRejected ? TEXT("rejected") : TEXT("accepted")),
            RangeRejected, Case.ExpectRejected);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_GeneratedPackageRoot_CustomRootGeneratesAndResolves,
    "CkTests.UnitTests.CkUsf.GeneratedPackageRoot.CustomRootGeneratesAndResolves",
    kCkUnitTestFlags | EAutomationTestFlags::NonNullRHI)

bool FCkTest_Usf_GeneratedPackageRoot_CustomRootGeneratesAndResolves::RunTest(const FString& Parameters)
{
    using namespace ck_test_usf_generated_package_root;

    if (FApp::CanEverRender() == false)
    {
        // Flagged EAutomationTestFlags::NonNullRHI, so a -nullrhi editor never lists this test and the
        // toolbox runs it in its real-renderer pass. Reaching here headless would test nothing, which
        // must never read as a pass.
        AddError(TEXT("Requires a real renderer (this test is flagged NonNullRHI); run it through the "
                      "toolbox gate or with --no-nullrhi."));
        return false;
    }

    // The lane-unique test root is set ON THE DEFINITION and no override is passed: the generator must find the
    // root through the definition alone, and the master still never lands in shipped content.
    const auto TestPackageRoot = ck_test_usf::Get_TestPackageRoot();
    const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
        Make_ProbeLookDefinition(kGenerationProbeLookName, TestPackageRoot)};

    const auto LookName = FName(kGenerationProbeLookName);
    const auto ExpectedPackagePath = ck::usf::Get_GeneratedMasterPackagePath(LookName, TestPackageRoot);

    TestEqual(TEXT("the definition resolves its master under its own root"),
        Definition->Get_GeneratedMasterPackagePath(), ExpectedPackagePath);

    auto* Master = ck::usf_editor::Generate_LookMaterial(Definition.Get());
    if (TestNotNull(TEXT("the custom-rooted look generates a master"), Master) == false)
    { return false; }

    TestTrue(*FString::Printf(TEXT("the master's package is saved at [%s]"), *ExpectedPackagePath),
        FPackageName::DoesPackageExist(ExpectedPackagePath));

    const auto FrameworkPackagePath = ck::usf::Get_GeneratedMasterPackagePath(LookName);
    TestFalse(*FString::Printf(TEXT("nothing was saved at the framework root [%s]"), *FrameworkPackagePath),
        FPackageName::DoesPackageExist(FrameworkPackagePath));

    // Lets the master's shader jobs finish before the runtime lookup inspects it, without the destructive
    // force-compile; the lookup is what is under test here, not the HLSL.
    {
        constexpr auto ForceSynchronousCompile = false;
        auto CompileErrors = TArray<FString>{};
        ck::usf_editor::Validate_LookShaderCompile(Master, LookName, CompileErrors, ForceSynchronousCompile);
        for (const auto& CompileError : CompileErrors)
        { AddError(CompileError); }
    }

    const auto* Resolved = UCk_Utils_Usf_UE::Get_LookMasterMaterial(Definition.Get());
    TestTrue(TEXT("the runtime lookup resolves the master generated under the definition's root"),
        Resolved != nullptr && Resolved == Master);

    if (ck_test_usf::Delete_TestGeneratedMaster(LookName) == false)
    { AddInfo(TEXT("Could not delete the probe's generated master file — harmless, but it is a stray asset on disk.")); }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR
