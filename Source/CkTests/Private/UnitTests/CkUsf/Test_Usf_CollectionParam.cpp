// Contract gate for FCk_Usf_ParamDesc::_FromCollection: a look param read from a material parameter collection,
// the fourth source beside a uniform, per-instance custom data and custom primitive data.
//
//   LookValidator.CollectionParamIsValidated — the validator's collection rules: the collection must be set, the
//     named parameter must exist in it as the param's own kind, only Scalar/Vector qualify, and the source is
//     exclusive with _PerInstance and _CustomPrimitiveData. A well-formed param raises none of them.
//   CollectionParam.GeneratesCollectionNodes — the round trip on a real renderer: the master carries one
//     CollectionParameter node per collection param, pointing at the collection with the collection parameter's
//     Id (a node without it reads nothing), wired to the Custom input of the param's name, and the HLSL compiles.

#include "Misc/AutomationTest.h"

#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "CkCore/Macros/CkMacros.h"

#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"

#include "../CkUnitTest_Common.h"

#if WITH_EDITOR
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialParameterCollection.h"

#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"

#include "CkUsf_TestLookMasters.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_usf_collection_param
{
    // A shipped entry point with one float3 and two floats, so both collection kinds and a uniform ride one look.
    constexpr auto kProbeIncludePath = TEXT("/CkUsf/Looks/CarPaint.ush");
    constexpr auto kProbeFunctionName = TEXT("CkUsf_Look_CarPaint");

    constexpr auto kValidatorLookName = TEXT("CollectionParamValidatorProbe");
    constexpr auto kGenerationLookName = TEXT("CollectionParamProbe");

    // PaintColor reads a differently named collection parameter (the _CollectionParameterName path); Flake reads
    // the collection parameter of its own name (the NAME_None default).
    constexpr auto kPaintColorParam = TEXT("PaintColor");
    constexpr auto kPaintColorCollectionParam = TEXT("ProbePaintColor");
    constexpr auto kFlakeParam = TEXT("Flake");
    constexpr auto kCoatRoughnessParam = TEXT("CoatRoughness");

    // Every collection rule's message names the source, so this catches all of them and nothing else.
    constexpr auto kCollectionErrorMarker = TEXT("_FromCollection");
    constexpr auto kMissingParameterErrorMarker = TEXT("has no");

    // In memory only, under the test's generated-master mount: a soft reference resolves it without a file, and a
    // real (non-transient) package is what a saved master may hold a hard import of. Release it before the scope
    // closes, so the scope can retire every package under its mount.
    auto
        Make_Collection(
            const FString& InPackageRoot)
        -> UMaterialParameterCollection*
    {
        auto* Package = CreatePackage(*(InPackageRoot / TEXT("MPC_CkUsfCollectionProbe")));
        const auto ObjectName = MakeUniqueObjectName(
            Package, UMaterialParameterCollection::StaticClass(), FName(TEXT("MPC_CkUsfCollectionProbe")));
        auto* Collection = NewObject<UMaterialParameterCollection>(Package, ObjectName, RF_Public | RF_Standalone);

        Collection->PreEditChange(nullptr);
        {
            FCollectionVectorParameter PaintColor;
            PaintColor.ParameterName = FName(kPaintColorCollectionParam);
            PaintColor.DefaultValue = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
            Collection->VectorParameters.Add(PaintColor);

            FCollectionScalarParameter Flake;
            Flake.ParameterName = FName(kFlakeParam);
            Flake.DefaultValue = 0.25f;
            Collection->ScalarParameters.Add(Flake);
        }
        // Rebuilds the uniform buffer layout the generated node compiles against.
        Collection->PostEditChange();

        return Collection;
    }

    auto
        Release_Collection(
            UMaterialParameterCollection* InCollection)
        -> void
    {
        InCollection->ClearFlags(RF_Public | RF_Standalone);
    }

    auto
        Make_ProbeLookDefinition(
            const TCHAR* InLookName,
            UMaterialParameterCollection* InCollection)
        -> UCkUsf_LookDefinition*
    {
        auto* Definition = NewObject<UCkUsf_LookDefinition>(GetTransientPackage());
        Definition->_LookName = FName(InLookName);
        Definition->_UshIncludePath = kProbeIncludePath;
        Definition->_UshFunctionName = FName(kProbeFunctionName);
        Definition->_Domain = ECk_Usf_Domain::SurfaceLit;
        Definition->_ShadingModel = ECk_Usf_ShadingModel::ClearCoat;

        FCk_Usf_ParamDesc PaintColor;
        PaintColor._Name = FName(kPaintColorParam);
        PaintColor._Type = ECk_Usf_ParamType::Vector;
        PaintColor._FromCollection = true;
        PaintColor._Collection = InCollection;
        PaintColor._CollectionParameterName = FName(kPaintColorCollectionParam);

        FCk_Usf_ParamDesc Flake;
        Flake._Name = FName(kFlakeParam);
        Flake._Type = ECk_Usf_ParamType::Scalar;
        Flake._FromCollection = true;
        Flake._Collection = InCollection;

        FCk_Usf_ParamDesc CoatRoughness;
        CoatRoughness._Name = FName(kCoatRoughnessParam);
        CoatRoughness._Type = ECk_Usf_ParamType::Scalar;
        CoatRoughness._DefaultScalar = 0.08f;

        Definition->_Parameters = { PaintColor, Flake, CoatRoughness };
        return Definition;
    }

    auto
        Get_ErrorsContaining(
            const ck::usf_editor::FLookValidationResult& InResult,
            const TCHAR* InMarker)
        -> TArray<FString>
    {
        return InResult.Errors.FilterByPredicate([&](const FString& InError) -> bool
        {
            return InError.Contains(InMarker);
        });
    }

    auto
        Find_CollectionNode(
            const UMaterial* InMaterial,
            const FName InParameterName)
        -> const UMaterialExpressionCollectionParameter*
    {
        for (const auto& Expression : InMaterial->GetExpressions())
        {
            if (const auto* CollectionNode = Cast<UMaterialExpressionCollectionParameter>(Expression);
                CollectionNode != nullptr && CollectionNode->ParameterName == InParameterName)
            { return CollectionNode; }
        }
        return nullptr;
    }

    auto
        Find_CustomInputSource(
            const UMaterial* InMaterial,
            const FName InInputName)
        -> const UMaterialExpression*
    {
        for (const auto& Expression : InMaterial->GetExpressions())
        {
            const auto* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (Custom == nullptr)
            { continue; }

            for (const auto& Input : Custom->Inputs)
            {
                if (Input.InputName == InInputName)
                { return Input.Input.Expression; }
            }
        }
        return nullptr;
    }
}

// --------------------------------------------------------------------------------------------------------------------

using ck::tests::kCkUnitTestFlags;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_LookValidator_CollectionParamIsValidated,
    "CkTests.UnitTests.CkUsf.LookValidator.CollectionParamIsValidated",
    kCkUnitTestFlags)

bool FCkTest_Usf_LookValidator_CollectionParamIsValidated::RunTest(const FString& Parameters)
{
    auto GeneratedMasters = ck_test_usf::FGeneratedMasterScope{*this};
    if (!GeneratedMasters.Initialize()) { return false; }
    const auto Succeeded = [&]() -> bool
    {
        using namespace ck_test_usf_collection_param;

        auto* Collection = Make_Collection(GeneratedMasters.Get_PackageRoot());
        ON_SCOPE_EXIT { Release_Collection(Collection); };

        {
            const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
                Make_ProbeLookDefinition(kValidatorLookName, Collection)};
            const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());

            TestEqual(TEXT("a well-formed look raises no collection error"),
                Get_ErrorsContaining(Validation, kCollectionErrorMarker).Num(), 0);
            TestTrue(TEXT("a well-formed look reads its collection parameters"),
                Get_ErrorsContaining(Validation, TEXT("material parameter collection")).IsEmpty());
        }

        struct FCase
        {
            const TCHAR* Description;
            TFunction<void(FCk_Usf_ParamDesc& InOutPaintColor)> Break;
            const TCHAR* Marker;
        };

        const FCase Cases[] =
        {
            { TEXT("a collection parameter name the collection does not have"),
              [](FCk_Usf_ParamDesc& P) { P._CollectionParameterName = FName(TEXT("NoSuchParameter")); },
              kMissingParameterErrorMarker },
            { TEXT("a Vector reading a scalar collection parameter"),
              [](FCk_Usf_ParamDesc& P) { P._CollectionParameterName = FName(kFlakeParam); },
              TEXT("(it is a scalar parameter)") },
            { TEXT("an unset collection"),
              [](FCk_Usf_ParamDesc& P) { P._Collection.Reset(); },
              TEXT("_Collection is not set") },
            { TEXT("a texture param from a collection"),
              [](FCk_Usf_ParamDesc& P) { P._Type = ECk_Usf_ParamType::Texture2D; },
              TEXT("_FromCollection but not Scalar/Vector") },
            { TEXT("a collection param that is also per-instance"),
              [](FCk_Usf_ParamDesc& P) { P._PerInstance = true; },
              TEXT("both _FromCollection and _PerInstance") },
            { TEXT("a collection param that is also custom primitive data"),
              [](FCk_Usf_ParamDesc& P) { P._CustomPrimitiveData = true; P._CustomPrimitiveDataIndex = 0; },
              TEXT("both _FromCollection and _CustomPrimitiveData") },
        };

        for (const auto& Case : Cases)
        {
            const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
                Make_ProbeLookDefinition(kValidatorLookName, Collection)};
            Case.Break(Definition->_Parameters[0]);

            const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition.Get());
            const auto Errors = Get_ErrorsContaining(Validation, Case.Marker);

            if (TestEqual(*FString::Printf(TEXT("%s is one validation error"), Case.Description), Errors.Num(), 1) == false)
            { continue; }

            TestTrue(*FString::Printf(TEXT("the error for %s names the look"), Case.Description),
                Errors[0].Contains(FString::Printf(TEXT("Look [%s]"), kValidatorLookName)));
            TestTrue(*FString::Printf(TEXT("the error for %s names the param"), Case.Description),
                Errors[0].Contains(FString::Printf(TEXT("param [%s]"), kPaintColorParam)));
        }

        // The translator refuses a material reading more than MaxNumParameterCollectionsPerMaterial (2) distinct
        // collections, and a headless generation never compiles the master, so the validator holds the limit.
        {
            constexpr auto kCollectionCountMarker = TEXT("distinct material parameter collections");

            auto* SecondCollection = Make_Collection(GeneratedMasters.Get_PackageRoot());
            ON_SCOPE_EXIT { Release_Collection(SecondCollection); };
            auto* ThirdCollection = Make_Collection(GeneratedMasters.Get_PackageRoot());
            ON_SCOPE_EXIT { Release_Collection(ThirdCollection); };

            const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
                Make_ProbeLookDefinition(kValidatorLookName, Collection)};
            Definition->_Parameters[1]._Collection = SecondCollection;

            TestEqual(TEXT("a look reading two distinct collections raises no collection-count error"),
                Get_ErrorsContaining(ck::usf_editor::Validate_LookDefinition(Definition.Get()), kCollectionCountMarker).Num(), 0);

            auto& CoatRoughness = Definition->_Parameters[2];
            CoatRoughness._FromCollection = true;
            CoatRoughness._Collection = ThirdCollection;
            CoatRoughness._CollectionParameterName = FName(kFlakeParam);

            const auto Errors = Get_ErrorsContaining(
                ck::usf_editor::Validate_LookDefinition(Definition.Get()), kCollectionCountMarker);
            if (TestEqual(TEXT("a look reading three distinct collections is one validation error"), Errors.Num(), 1))
            {
                TestTrue(TEXT("the collection-count error names the look"),
                    Errors[0].Contains(FString::Printf(TEXT("Look [%s]"), kValidatorLookName)));
            }
        }

        return true;
    }();
    const auto CleanupSucceeded = GeneratedMasters.Close();
    return Succeeded && CleanupSucceeded;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_CollectionParam_GeneratesCollectionNodes,
    "CkTests.UnitTests.CkUsf.CollectionParam.GeneratesCollectionNodes",
    kCkUnitTestFlags | EAutomationTestFlags::NonNullRHI)

bool FCkTest_Usf_CollectionParam_GeneratesCollectionNodes::RunTest(const FString& Parameters)
{
    auto GeneratedMasters = ck_test_usf::FGeneratedMasterScope{*this};
    if (!GeneratedMasters.Initialize()) { return false; }
    const auto Succeeded = [&]() -> bool
    {
        using namespace ck_test_usf_collection_param;

        if (FApp::CanEverRender() == false)
        {
            // Flagged EAutomationTestFlags::NonNullRHI, so a -nullrhi editor never lists this test and the
            // toolbox runs it in its real-renderer pass. Reaching here headless would test nothing, which
            // must never read as a pass.
            AddError(TEXT("Requires a real renderer (this test is flagged NonNullRHI); run it through the "
                          "toolbox gate or with --no-nullrhi."));
            return false;
        }

        auto* Collection = Make_Collection(GeneratedMasters.Get_PackageRoot());
        ON_SCOPE_EXIT { Release_Collection(Collection); };

        const auto Definition = TStrongObjectPtr<UCkUsf_LookDefinition>{
            Make_ProbeLookDefinition(kGenerationLookName, Collection)};
        const auto LookName = FName(kGenerationLookName);

        auto* Master = ck::usf_editor::Generate_LookMaterial(Definition.Get(), GeneratedMasters.Get_PackageRoot());
        if (TestNotNull(TEXT("the collection-sourced look generates a master"), Master) == false)
        { return false; }

        struct FExpectedNode
        {
            const TCHAR* Param;
            const TCHAR* CollectionParam;
            FGuid Id;
        };

        const FExpectedNode ExpectedNodes[] =
        {
            { kPaintColorParam, kPaintColorCollectionParam,
              Collection->GetVectorParameterByName(FName(kPaintColorCollectionParam))->Id },
            { kFlakeParam, kFlakeParam,
              Collection->GetScalarParameterByName(FName(kFlakeParam))->Id },
        };

        for (const auto& Expected : ExpectedNodes)
        {
            const auto* Node = Find_CollectionNode(Master, FName(Expected.CollectionParam));
            if (TestNotNull(*FString::Printf(TEXT("param [%s] is a CollectionParameter node reading [%s]"),
                    Expected.Param, Expected.CollectionParam), Node) == false)
            { continue; }

            TestTrue(*FString::Printf(TEXT("param [%s]'s node points at the collection"), Expected.Param),
                Node->Collection == Collection);
            TestEqual(*FString::Printf(TEXT("param [%s]'s node carries the collection parameter's Id"), Expected.Param),
                Node->ParameterId, Expected.Id);
            TestTrue(*FString::Printf(TEXT("param [%s]'s node feeds the Custom input of the same name"), Expected.Param),
                Find_CustomInputSource(Master, FName(Expected.Param)) == Node);
        }

        TestNull(TEXT("a uniform param beside the collection params stays a uniform"),
            Find_CollectionNode(Master, FName(kCoatRoughnessParam)));

        // Throwaway master, so the destructive forced compile is allowed: it is what gives the verdict teeth.
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
