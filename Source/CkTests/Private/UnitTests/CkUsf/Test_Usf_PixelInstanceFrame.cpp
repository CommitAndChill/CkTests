// Contract gate for UCkUsf_LookDefinition::_PixelInstanceFrame: the opt-in that makes the pixel stage carry each
// instance's world-to-local transform, so GetWorldToInstanceDF(Parameters) answers per instance.
//
//   LookValidator.PixelInstanceFrameRequiresSurface — the flag is an error off a Surface domain, and the probe
//     input's name is reserved so no param can take it.
//   PixelInstanceFrame.SetsTheInstanceFrameDefine — the round trip on a real renderer: the master carries a
//     World->Instance Transform feeding the pixel Custom node's CkUsf_InstanceFrameProbe input, and a forced
//     compile of InstanceFrameProbe.ush (which #errors unless NEEDS_INSTANCE_WORLD_TO_LOCAL_PS is set) is clean.
//     That #error is what proves the translator saw the probe; the node check alone would not.

#include "Misc/AutomationTest.h"

#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "CkCore/Macros/CkMacros.h"

#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"

#include "../CkUnitTest_Common.h"

#if WITH_EDITOR
#include "Misc/App.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTransform.h"

#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"

#include "CkUsf_TestLookMasters.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_usf_pixel_instance_frame
{
    constexpr auto kProbeIncludePath = TEXT("/CkUsf/Looks/InstanceFrameProbe.ush");
    constexpr auto kProbeFunctionName = TEXT("CkUsf_Look_InstanceFrameProbe");
    constexpr auto kProbeParamName = TEXT("ProbeAmount");
    constexpr auto kProbeInputName = TEXT("CkUsf_InstanceFrameProbe");

    constexpr auto kValidatorLookName = TEXT("PixelInstanceFrameValidatorProbe");
    constexpr auto kGenerationLookName = TEXT("PixelInstanceFrameProbe");

    constexpr auto kSurfaceErrorMarker = TEXT("_PixelInstanceFrame requires a Surface domain");

    auto
        Make_ProbeLookDefinition(
            const TCHAR* InLookName,
            const ECk_Usf_Domain InDomain)
        -> UCkUsf_LookDefinition*
    {
        auto* Definition = NewObject<UCkUsf_LookDefinition>(GetTransientPackage());
        Definition->_LookName = FName(InLookName);
        Definition->_UshIncludePath = kProbeIncludePath;
        Definition->_UshFunctionName = FName(kProbeFunctionName);
        Definition->_Domain = InDomain;
        Definition->_PixelInstanceFrame = true;

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
        Find_ProbeInputSource(
            const UMaterial* InMaterial)
        -> const UMaterialExpression*
    {
        for (const auto& Expression : InMaterial->GetExpressions())
        {
            const auto* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (Custom == nullptr)
            { continue; }

            for (const auto& Input : Custom->Inputs)
            {
                if (Input.InputName == FName(kProbeInputName))
                { return Input.Input.Expression; }
            }
        }
        return nullptr;
    }
}

// --------------------------------------------------------------------------------------------------------------------

using ck::tests::kCkUnitTestFlags;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_LookValidator_PixelInstanceFrameRequiresSurface,
    "CkTests.UnitTests.CkUsf.LookValidator.PixelInstanceFrameRequiresSurface",
    kCkUnitTestFlags)

bool FCkTest_Usf_LookValidator_PixelInstanceFrameRequiresSurface::RunTest(const FString& Parameters)
{
    using namespace ck_test_usf_pixel_instance_frame;

    for (const auto Domain : { ECk_Usf_Domain::SurfaceLit, ECk_Usf_Domain::SurfaceUnlit })
    {
        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
            Make_ProbeLookDefinition(kValidatorLookName, Domain)};
        TestFalse(TEXT("a Surface look may ask for the pixel instance frame"),
            Get_HasErrorContaining(ck::usf_editor::Validate_LookDefinition(Definition.Get()), kSurfaceErrorMarker));
    }

    {
        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
            Make_ProbeLookDefinition(kValidatorLookName, ECk_Usf_Domain::PostProcess)};
        TestTrue(TEXT("a PostProcess look asking for the pixel instance frame is a validation error"),
            Get_HasErrorContaining(ck::usf_editor::Validate_LookDefinition(Definition.Get()), kSurfaceErrorMarker));
    }

    {
        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
            Make_ProbeLookDefinition(kValidatorLookName, ECk_Usf_Domain::SurfaceLit)};
        Definition->_Parameters[0]._Name = FName(kProbeInputName);
        TestTrue(TEXT("a param named like the probe input is rejected as reserved"),
            Get_HasErrorContaining(ck::usf_editor::Validate_LookDefinition(Definition.Get()),
                TEXT("collides with the generated Custom-node input")));
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_PixelInstanceFrame_SetsTheInstanceFrameDefine,
    "CkTests.UnitTests.CkUsf.PixelInstanceFrame.SetsTheInstanceFrameDefine",
    kCkUnitTestFlags | EAutomationTestFlags::NonNullRHI)

bool FCkTest_Usf_PixelInstanceFrame_SetsTheInstanceFrameDefine::RunTest(const FString& Parameters)
{
    auto GeneratedMasters = ck_test_usf::FGeneratedMasterScope{*this};
    if (!GeneratedMasters.Initialize()) { return false; }
    const auto Succeeded = [&]() -> bool
    {
        using namespace ck_test_usf_pixel_instance_frame;

        if (FApp::CanEverRender() == false)
        {
            // Flagged EAutomationTestFlags::NonNullRHI, so a -nullrhi editor never lists this test and the
            // toolbox runs it in its real-renderer pass. Reaching here headless would test nothing, which
            // must never read as a pass.
            AddError(TEXT("Requires a real renderer (this test is flagged NonNullRHI); run it through the "
                          "toolbox gate or with --no-nullrhi."));
            return false;
        }

        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
            Make_ProbeLookDefinition(kGenerationLookName, ECk_Usf_Domain::SurfaceLit)};
        const auto LookName = FName(kGenerationLookName);

        auto* Master = ck::usf_editor::Generate_LookMaterial(Definition.Get(), GeneratedMasters.Get_PackageRoot());
        if (TestNotNull(TEXT("the instance-frame look generates a master"), Master) == false)
        { return false; }

        const auto* Frame = Cast<UMaterialExpressionTransform>(Find_ProbeInputSource(Master));
        if (TestNotNull(TEXT("the pixel Custom node's probe input is fed by a Transform"), Frame))
        {
            TestTrue(TEXT("the probe transforms from world space"),
                Frame->TransformSourceType == TRANSFORMSOURCE_World);
            TestTrue(TEXT("the probe transforms into instance space"),
                Frame->TransformType == TRANSFORM_Instance);
        }

        // Throwaway master, so the destructive forced compile is allowed. InstanceFrameProbe.ush #errors unless the
        // translator set NEEDS_INSTANCE_WORLD_TO_LOCAL_PS, so a clean compile is the define check.
        {
            constexpr auto ForceSynchronousCompile = true;
            auto CompileErrors = TArray<FString>{};
            ck::usf_editor::Validate_LookShaderCompile(Master, LookName, CompileErrors, ForceSynchronousCompile);
            for (const auto& CompileError : CompileErrors)
            { AddError(CompileError); }
        }

        return true;
    }();
    const auto CleanupSucceeded = GeneratedMasters.Close();
    return Succeeded && CleanupSucceeded;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR
