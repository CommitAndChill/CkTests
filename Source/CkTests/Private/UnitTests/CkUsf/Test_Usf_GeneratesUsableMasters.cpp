// End-to-end editor test for CkUsf:
//   - runs the generator over all UCkUsf_LookDefinition assets
//   - for each look, the generated master resolves and a MID can be created
// Editor-only: the generator lives in CkUsfEditor.
//
// Generation writes to a invocation-unique Saved root (CkUsf_TestLookMasters.h), never the shipped GeneratedLooks —
// concurrent test editors saving the same .uasset take each other down. The shipped masters are still
// resolved through the RUNTIME lookup below, which is the API game code actually calls, so that arm reads
// committed content rather than something this run wrote.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Misc/App.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "CkUsf/LookDefinition/CkUsf_LookDefinition.h"
#include "CkUsf/Apply/CkUsf_Utils.h"
#include "CkUsfEditor/Generator/CkUsf_Generator.h"
#include "CkUsfEditor/Generator/CkUsf_LookValidator.h"

#include "CkUsf_TestLookMasters.h"

// --------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr auto kUsfTestFlags =
        EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Usf_GeneratesUsableMasters,
    "CkTests.UnitTests.CkUsf.GeneratesUsableMasters",
    kUsfTestFlags)

bool FCkTest_Usf_GeneratesUsableMasters::RunTest(const FString& Parameters)
{
    auto GeneratedMasters = ck_test_usf::FGeneratedMasterScope{*this};
    if (!GeneratedMasters.Initialize()) { return false; }
    const auto Succeeded = [&]() -> bool
    {
        // In a process that cannot render (-nullrhi CI) materials never build shader maps, so the
        // shader-compile gate reads every look as failed — environmental, not a shader bug.
        if (FApp::CanEverRender() == false)
        {
            // Flagged EAutomationTestFlags::NonNullRHI, so a -nullrhi editor never lists this test and the
            // toolbox runs it in its real-renderer pass. Reaching here headless would test nothing, which
            // must never read as a pass (it used to: AddInfo("Skipped") + return true).
            AddError(TEXT("Requires a real renderer (this test is flagged NonNullRHI); run it through the "
                          "toolbox gate or with --no-nullrhi."));
            return false;
        }

        // 1. Generate masters for every declared look. Generation now also force-compiles each
        //    master's shaders and reports any HLSL compile failures (the silent fallback-to-default
        //    case that a bare object/MID check misses — e.g. a broken PostProcess permutation).
        const auto TestPackageRoot = GeneratedMasters.Get_PackageRoot();
        const auto& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        TArray<FAssetData> Assets;
        ARM.GetAssetsByClass(UCkUsf_LookDefinition::StaticClass()->GetClassPathName(), Assets);
        TestTrue(TEXT("found at least one UCkUsf_LookDefinition"), Assets.Num() > 0);
        const auto EngineRejection = FString{TEXT("Unsupported shading model for this engine")};
        auto UnsupportedLookPaths = TSet<FString>{};
    #if !defined(UE_CK_USF_SHADING_MODEL_VERSION) || UE_CK_USF_SHADING_MODEL_VERSION < 1
        for (const auto& Asset : Assets)
        {
            const auto* Definition = Cast<UCkUsf_LookDefinition>(Asset.GetAsset());
            if (NOT TestNotNull(TEXT("Look definition loads for capability validation"), Definition))
            { continue; }
            const auto Validation = ck::usf_editor::Validate_LookDefinition(Definition);
            if (Validation.Errors.Num() == 1 && Validation.Errors[0] == EngineRejection)
            { UnsupportedLookPaths.Add(Definition->GetPathName()); }
        }
        if (NOT UnsupportedLookPaths.IsEmpty())
        { AddExpectedError(*EngineRejection, EAutomationExpectedErrorFlags::Contains, UnsupportedLookPaths.Num()); }
    #endif

        const auto Result = ck::usf_editor::Generate_AllLookMaterials(TestPackageRoot);
        TestTrue(TEXT("at least one look material generated"), Result.NumGenerated > 0);
        TestEqual(TEXT("Every supported look generates a master"), Result.NumGenerated, Assets.Num() - UnsupportedLookPaths.Num());
        TestEqual(TEXT("Only explicitly unsupported engine looks are rejected"), Result.NumSkipped, UnsupportedLookPaths.Num());

        for (const auto& ShaderError : Result.Errors)
        {
            if (UnsupportedLookPaths.IsEmpty() || ShaderError != EngineRejection)
            { AddError(ShaderError); }
        }
        TestEqual(TEXT("No errors beyond the explicit engine capability rejections"),
            Result.Errors.Num(), UnsupportedLookPaths.Num());

        // 3. Each look: the master this run generated resolves, the shipped one resolves through the runtime
        //    lookup, and a MID can be created from it.
        for (const auto& A : Assets)
        {
            const TWeakObjectPtr<UCkUsf_LookDefinition> Def = Cast<UCkUsf_LookDefinition>(A.GetAsset());
            if (NOT Def.IsValid()) { continue; }

            const auto LookName = Def->Get_EffectiveLookName();
            const auto Name = LookName.ToString();
            if (UnsupportedLookPaths.Contains(Def->GetPathName()))
            {
                const auto Validation = ck::usf_editor::Validate_LookDefinition(Def.Get());
                TestFalse(TEXT("Unsupported engine continues to reject the authored look explicitly"), Validation.Get_IsValid());
                TestTrue(TEXT("Engine rejection has exactly the expected capability diagnostic"),
                    Validation.Errors.Num() == 1 && Validation.Errors[0] == EngineRejection);
                const auto PackagePath = Def->Get_GeneratedMasterPackagePath(TestPackageRoot);
                TestNull(TEXT("Rejected look creates no generated material package in memory"), FindPackage(nullptr, *PackagePath));
                TestFalse(TEXT("Rejected look writes no generated material package"), FPackageName::DoesPackageExist(PackagePath));
                continue;
            }

            const auto GeneratedPath = Def->Get_GeneratedMasterObjectPath(TestPackageRoot);
            const TWeakObjectPtr<UMaterial> GeneratedMaster = LoadObject<UMaterial>(nullptr, *GeneratedPath);
            if (TestNotNull(*FString::Printf(TEXT("the master generated this run resolves for look [%s]"), *Name),
                GeneratedMaster.Get()))
            {
                TestEqual(*FString::Printf(TEXT("generated Nanite usage matches look [%s]"), *Name),
                    GeneratedMaster->bUsedWithNanite != 0, Def->_UsedWithNanite);
            }

            // Resolved where the look's own definition says its master lives, so a game-rooted look is checked at
            // its game root rather than at the framework's.
            const TWeakObjectPtr<UMaterial> Master = Cast<UMaterial>(UCk_Utils_Usf_UE::Get_LookMasterMaterial(Def.Get()));
            if (TestNotNull(*FString::Printf(TEXT("shipped master resolves for look [%s] at [%s]"),
                *Name, *Def->Get_GeneratedMasterObjectPath()), Master.Get()))
            {
                TestEqual(*FString::Printf(TEXT("shipped Nanite usage matches look [%s]"), *Name),
                    Master->bUsedWithNanite != 0, Def->_UsedWithNanite);
            }

            const TWeakObjectPtr<UMaterialInstanceDynamic> MID =
                UCk_Utils_Usf_UE::Create_MID_ForLook(Def.Get(), GetTransientPackage());
            TestNotNull(*FString::Printf(TEXT("MID created for look [%s]"), *Name), MID.Get());

        }

        return true;
    }();
    const auto CleanupSucceeded = GeneratedMasters.Close();
    return Succeeded && CleanupSucceeded;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR
