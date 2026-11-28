// End-to-end editor test for CkUsf:
//   - runs the generator over all UCkUsf_LookDefinition assets
//   - validates every look; for each renderer-compatible look, the generated master resolves and a MID can be created
// Editor-only: the generator lives in CkUsfEditor.
//
// Generation writes to a lane-unique root (CkUsf_TestLookMasters.h), never the shipped GeneratedLooks —
// concurrent test editors saving the same .uasset take each other down. The shipped masters are still
// resolved through the RUNTIME lookup below, which is the API game code actually calls, so that arm reads
// committed content rather than something this run wrote.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Misc/App.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/Package.h"
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

    // 1. Generate compatible masters. Opposite-profile definitions must still pass full authoring
    //    validation, and their exclusion must be explicit and reconciled against the registry below.
    //    Generation reports available compile errors; it does not destructively force recompilation.
    const auto TestPackageRoot = ck_test_usf::Get_TestPackageRoot();

    const auto Result = ck::usf_editor::Generate_AllLookMaterials(TestPackageRoot);
    TestTrue(TEXT("at least one look material generated"), Result.NumGenerated > 0);
    TestEqual(TEXT("no looks skipped"), Result.NumSkipped, 0);

    for (const auto& ShaderError : Result.Errors)
    { AddError(ShaderError); }
    TestEqual(TEXT("no shader compile errors across looks"), Result.Errors.Num(), 0);

    // 2. Discover the look definitions.
    const auto& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    TArray<FAssetData> Assets;
    ARM.GetAssetsByClass(UCkUsf_LookDefinition::StaticClass()->GetClassPathName(), Assets);
    TestTrue(TEXT("found at least one UCkUsf_LookDefinition"), Assets.Num() > 0);
    auto CompatibleCount = 0;
    auto IncompatibleCount = 0;
    TestEqual(TEXT("every profile exclusion has a named receipt"), Result.IncompatibleLooks.Num(), Result.NumIncompatible);

    // 3. Each compatible look: the master this run generated resolves, the shipped one resolves through the runtime
    //    lookup, and a MID can be created from it.
    for (const auto& A : Assets)
    {
        const TWeakObjectPtr<UCkUsf_LookDefinition> Def = Cast<UCkUsf_LookDefinition>(A.GetAsset());
        if (NOT TestTrue(*FString::Printf(TEXT("registered look definition loads: %s"), *A.GetSoftObjectPath().ToString()), Def.IsValid()))
        { continue; }

        const auto LookName = Def->Get_EffectiveLookName();
        const auto Name = LookName.ToString();
        const auto Admission = ck::usf_editor::Classify_LookForGeneration(Def.Get());
        if (Admission.Admission == ck::usf_editor::ELookGenerationAdmission::Invalid)
        {
            AddError(FString::Printf(TEXT("Invalid registered look [%s] is not a renderer-profile exclusion"), *Def->GetPathName()));
            for (const auto& Error : Admission.Validation.Errors) { AddError(Error); }
            continue;
        }
        if (Admission.Admission == ck::usf_editor::ELookGenerationAdmission::OtherRendererProfile)
        {
            ++IncompatibleCount;
            TestTrue(TEXT("Opposite-profile look still passed full authoring validation"), Admission.Validation.Get_IsValid());
            TestFalse(TEXT("Profile exclusion identifies required profile"), Admission.RequiredProfile.IsEmpty());
            TestFalse(TEXT("Profile exclusion supplies reason"), Admission.Reason.IsEmpty());
            const auto Path = Def->GetPathName();
            TestEqual(*FString::Printf(TEXT("Exactly one bulk exclusion receipt for [%s]"), *Path),
                Result.IncompatibleLooks.FilterByPredicate([&](const FString& Receipt) { return Receipt.Contains(Path); }).Num(), 1);
            AddInfo(FString::Printf(TEXT("Validated opposite-profile look [%s], resolved graph %d, requires %s: %s"),
                *Path, static_cast<int32>(Admission.ResolvedGraph), *Admission.RequiredProfile, *Admission.Reason));
            continue;
        }
        ++CompatibleCount;

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

        if (ck_test_usf::Delete_TestGeneratedMaster(LookName) == false)
        {
            AddInfo(FString::Printf(
                TEXT("Could not delete the generated master for look [%s] — harmless, but it is a stray asset on disk."),
                *Name));
        }
    }

    TestEqual(TEXT("Bulk generated every compatible registered look"), Result.NumGenerated, CompatibleCount);
    TestEqual(TEXT("Bulk reported exactly the independently classified opposite-profile looks"), Result.NumIncompatible, IncompatibleCount);
    TestEqual(TEXT("All registered definitions are accounted for without invalid or silent skips"), CompatibleCount + IncompatibleCount, Assets.Num());
    TestEqual(TEXT("Bulk result accounts for the complete registry roster"), Result.NumGenerated + Result.NumIncompatible, Assets.Num());

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR
