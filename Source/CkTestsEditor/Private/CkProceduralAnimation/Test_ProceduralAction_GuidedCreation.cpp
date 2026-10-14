#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditor_UserSettings.h"
#include "CkProceduralAnimationEditor/CkProceduralActionGuidedCreation.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_Factory.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"

namespace ck_test_procedural_action_guided_creation
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // A disposable content root, a private undo buffer and the cleanup that removes both.
    struct FDisposableRoot
    {
        FString Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FString MountRoot;
        FString MountDirectory;
        TArray<FString> Packages;
        TArray<FString> ClipPaths;
        TStrongObjectPtr<UTransactor> PreviousTransactions;
        TStrongObjectPtr<UTransBuffer> PrivateTransactions;
        bool Mounted = false;

        explicit FDisposableRoot(
            const TCHAR* InName)
        {
            MountRoot = FString::Printf(TEXT("/CkAction%s_%s/"), InName, *Unique);
            MountDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ProceduralAction"),
                InName, Unique) + TEXT("/");
        }

        auto Mount() -> bool
        {
            if (GEditor == nullptr || GEditor->Trans == nullptr || GEditor->Trans->IsActive()
                || NOT IFileManager::Get().MakeDirectory(*MountDirectory, true))
            { return false; }
            FPackageName::RegisterMountPoint(MountRoot, MountDirectory);
            Mounted = true;
            PreviousTransactions.Reset(GEditor->Trans.Get());
            PrivateTransactions.Reset(NewObject<UTransBuffer>());
            PrivateTransactions->Initialize(8 * 1024 * 1024);
            GEditor->Trans = PrivateTransactions.Get();
            return true;
        }

        ~FDisposableRoot()
        {
            if (PrivateTransactions.IsValid())
            { PrivateTransactions->Reset(FText::FromString(TEXT("Discard disposable creation test transactions"))); }
            if (GEditor != nullptr && PrivateTransactions.IsValid() && GEditor->Trans.Get() == PrivateTransactions.Get())
            { GEditor->Trans = PreviousTransactions.Get(); }
            auto* Settings = GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>();
            auto SettingsChanged = false;
            for (const auto& ClipPath : ClipPaths)
            { SettingsChanged |= Settings->SetPreviewPath(ClipPath, FString{}); }
            if (SettingsChanged)
            { Settings->SaveConfig(); }
            auto Loaded = TArray<UPackage*>{};
            for (const auto& PackageName : Packages)
            {
                auto* Package = FindPackage(nullptr, *PackageName);
                if (Package == nullptr)
                { continue; }
                if (PackageName.StartsWith(MountRoot))
                {
                    Loaded.Add(Package);
                    continue;
                }
                // An unmounted package has no file; it is released by dropping its standalone objects.
                ForEachObjectWithPackage(Package, [](UObject* InObject)
                {
                    InObject->ClearFlags(RF_Public | RF_Standalone);
                    InObject->MarkAsGarbage();
                    return true;
                });
            }
            if (NOT Loaded.IsEmpty())
            {
                auto Unload = UPackageTools::FUnloadPackageParams{Loaded};
                Unload.bResetTransBuffer = false;
                UPackageTools::UnloadPackages(Unload);
            }
            CollectGarbage(RF_NoFlags);
            if (Mounted)
            { FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory); }
            IFileManager::Get().DeleteDirectory(*MountDirectory, false, true);
        }
    };

    auto
        PackageFileExists(
            const FString& InPackageName)
        -> bool
    {
        auto Filename = FString{};
        return FPackageName::TryConvertLongPackageNameToFilename(InPackageName, Filename,
                FPackageName::GetAssetPackageExtension())
            && IFileManager::Get().FileExists(*Filename);
    }

    // No object, no registered asset and no file: nothing was created for this package.
    auto
        HasNoAsset(
            const FString& InPackageName)
        -> bool
    {
        const auto AssetName = FPackageName::GetLongPackageAssetName(InPackageName);
        const auto ObjectPath = FSoftObjectPath{InPackageName + TEXT(".") + AssetName};
        auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        return ObjectPath.ResolveObject() == nullptr
            && NOT Registry.GetAssetByObjectPath(ObjectPath).IsValid()
            && NOT PackageFileExists(InPackageName);
    }

    auto
        MakeChoice(
            ECk_ProceduralActionCreation_Subject InSubject,
            ECk_ProceduralAction_Recipe InRecipe)
        -> FCk_ProceduralActionCreation_Choice
    {
        auto Choice = FCk_ProceduralActionCreation_Choice{};
        Choice.Set_Subject(InSubject);
        auto Recipe = FCk_ProceduralAction_RecipeSpec{InRecipe, TArray<FName>{}};
        Recipe.Set_AmplitudeCm(12.0f);
        Choice.Set_Recipe(Recipe);
        return Choice;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_RecipeCreationCancelLeavesNoAssets,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.RecipeCreationCancelLeavesNoAssets",
    ck_test_procedural_action_guided_creation::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_RecipeCreationCancelLeavesNoAssets::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_guided_creation;
    auto Root = FDisposableRoot{TEXT("Creation")};
    if (NOT TestTrue(TEXT("disposable content root and private undo buffer"), Root.Mount()))
    { return false; }
    const auto ClipPackage = Root.MountRoot + TEXT("Lean_DA");
    const auto PreviewPackage = ck::procedural_action_editor::Get_PreviewSiblingPackageName(ClipPackage);
    Root.Packages = TArray<FString>{ClipPackage, PreviewPackage};
    Root.ClipPaths.Add(ClipPackage + TEXT(".Lean_DA"));
    TestEqual(TEXT("the preview is created next to the clip"), PreviewPackage, Root.MountRoot + TEXT("Lean_Preview_DA"));

    auto Factory = TStrongObjectPtr<UCk_ProceduralAction_Factory>{NewObject<UCk_ProceduralAction_Factory>()};
    auto Error = FString{};
    TestFalse(TEXT("a cancelled dialog does not configure the factory"),
        Factory->ConfigureFromChoice(TOptional<FCk_ProceduralActionCreation_Choice>{}, Error));
    TestFalse(TEXT("cancel leaves no confirmed choice"), Factory->Get_HasConfirmedChoice());
    TestTrue(TEXT("cancel created no clip"), HasNoAsset(ClipPackage));
    TestTrue(TEXT("cancel created no preview config"), HasNoAsset(PreviewPackage));

    const auto UnslottedReach = MakeChoice(ECk_ProceduralActionCreation_Subject::NewBodyOnly, ECk_ProceduralAction_Recipe::Reach);
    TestFalse(TEXT("a recipe the builder rejects is refused before any write"),
        Factory->ConfigureFromChoice(UnslottedReach, Error));
    TestTrue(TEXT("the recipe refusal carries the builder's field"), Error.Contains(TEXT("Slots")));
    TestFalse(TEXT("a refused recipe leaves no confirmed choice"), Factory->Get_HasConfirmedChoice());
    TestTrue(TEXT("a refused recipe created no preview config"), HasNoAsset(PreviewPackage));

    const auto MissingSubject = MakeChoice(ECk_ProceduralActionCreation_Subject::UseExisting, ECk_ProceduralAction_Recipe::BodyLean);
    TestFalse(TEXT("reusing a subject requires choosing one"), Factory->ConfigureFromChoice(MissingSubject, Error));
    TestTrue(TEXT("the missing subject refusal says so"), Error.Contains(TEXT("preview")));

    const auto BodyLean = MakeChoice(ECk_ProceduralActionCreation_Subject::NewBodyOnly, ECk_ProceduralAction_Recipe::BodyLean);
    TestTrue(TEXT("a valid choice configures the factory"), Factory->ConfigureFromChoice(BodyLean, Error));
    TestTrue(TEXT("configuring alone writes nothing"), HasNoAsset(ClipPackage) && HasNoAsset(PreviewPackage));
    TestFalse(TEXT("a later cancel withdraws the earlier choice"),
        Factory->ConfigureFromChoice(TOptional<FCk_ProceduralActionCreation_Choice>{}, Error));
    TestFalse(TEXT("the withdrawn choice cannot leak into a later creation"), Factory->Get_HasConfirmedChoice());

    if (NOT TestTrue(TEXT("confirm the body lean choice"), Factory->ConfigureFromChoice(BodyLean, Error)))
    { AddInfo(Error); return false; }
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    auto* Created = Cast<UCk_ProceduralAction_Data>(Tools.CreateAsset(TEXT("Lean_DA"), FPackageName::GetLongPackagePath(ClipPackage),
        UCk_ProceduralAction_Data::StaticClass(), Factory.Get()));
    if (NOT TestNotNull(TEXT("a confirmed choice creates the clip"), Created))
    { return false; }
    const auto* Preview = FindObject<UCk_ProceduralActionPreview_Config>(nullptr,
        *(PreviewPackage + TEXT(".") + FPackageName::GetLongPackageAssetName(PreviewPackage)));
    if (NOT TestNotNull(TEXT("the confirmed choice created the sibling preview config"), Preview))
    { return false; }
    TestEqual(TEXT("a body-only subject is standalone"), Preview->Get_SubjectMode(), ECk_ProceduralActionPreview_SubjectMode::Standalone);
    TestTrue(TEXT("a body-only subject has no limbs"), Preview->Get_Layout().Get_Limbs().IsEmpty());
    TestEqual(TEXT("the clip remembers its new preview"),
        GetDefault<UCk_ProceduralActionEditor_UserSettings>()->GetPreviewPath(Created->GetPathName()), Preview->GetPathName());
    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    TestTrue(TEXT("the recipe clip compiles"), ck::procedural_action::Compile(*Created, Compiled, Diagnostics));
    TestEqual(TEXT("the body lean recipe writes three phases"), Created->Get_Phases().Num(), 3);
    TestFalse(TEXT("the recipe keys are named poses"), Created->Get_EditorPoseGroups().IsEmpty());
    TestFalse(TEXT("creation saves nothing: the clip file is not written"), PackageFileExists(ClipPackage));
    TestFalse(TEXT("creation saves nothing: the preview file is not written"), PackageFileExists(PreviewPackage));
    TestFalse(TEXT("a creation consumes the confirmed choice"), Factory->Get_HasConfirmedChoice());
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringCommands_PairedSaveFailureReportsWhichAsset,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.PairedSaveFailureReportsWhichAsset",
    ck_test_procedural_action_guided_creation::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringCommands_PairedSaveFailureReportsWhichAsset::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_guided_creation;
    auto Root = FDisposableRoot{TEXT("PairedSave")};
    if (NOT TestTrue(TEXT("disposable content root and private undo buffer"), Root.Mount()))
    { return false; }
    const auto UnmountedRoot = FString::Printf(TEXT("/CkActionUnmounted_%s/"), *Root.Unique);
    const auto MountedClip = Root.MountRoot + TEXT("Clip");
    const auto MountedPreview = Root.MountRoot + TEXT("Preview");
    const auto UnmountedClip = UnmountedRoot + TEXT("Clip");
    const auto UnmountedPreview = UnmountedRoot + TEXT("Preview");
    Root.Packages = TArray<FString>{MountedClip, MountedPreview, UnmountedClip, UnmountedPreview};

    const auto MakeAsset = [](const FString& InPackageName, UClass* InClass) -> UObject*
    {
        auto* Package = CreatePackage(*InPackageName);
        return NewObject<UObject>(Package, InClass, FName{*FPackageName::GetLongPackageAssetName(InPackageName)},
            RF_Public | RF_Standalone);
    };

    auto* SavedClip = MakeAsset(MountedClip, UCk_ProceduralAction_Data::StaticClass());
    auto* StrandedPreview = MakeAsset(UnmountedPreview, UCk_ProceduralActionPreview_Config::StaticClass());
    const auto First = ck::procedural_action_editor::SaveAssets(TArray<UObject*>{SavedClip, StrandedPreview});
    if (NOT TestEqual(TEXT("one result per asset"), First.Num(), 2))
    { return false; }
    TestTrue(TEXT("the clip result names the clip"), First[0].Get_AssetPath() == SavedClip->GetPathName());
    TestTrue(TEXT("the clip saved"), First[0].Get_Saved());
    TestTrue(TEXT("the clip file was written"), PackageFileExists(MountedClip));
    TestTrue(TEXT("the preview result names the preview"), First[1].Get_AssetPath() == StrandedPreview->GetPathName());
    TestFalse(TEXT("the preview did not save"), First[1].Get_Saved());
    TestFalse(TEXT("the failed save says why"), First[1].Get_Reason().IsEmpty());
    const auto FirstSummary = ck::procedural_action_editor::DescribeSaveResults(First);
    TestTrue(TEXT("the summary names the asset that failed"), FirstSummary.Contains(StrandedPreview->GetPathName()));
    TestTrue(TEXT("the summary still reports the asset that saved"), FirstSummary.Contains(SavedClip->GetPathName()));

    auto* StrandedClip = MakeAsset(UnmountedClip, UCk_ProceduralAction_Data::StaticClass());
    auto* SavedPreview = MakeAsset(MountedPreview, UCk_ProceduralActionPreview_Config::StaticClass());
    const auto Second = ck::procedural_action_editor::SaveAssets(TArray<UObject*>{StrandedClip, SavedPreview});
    if (NOT TestEqual(TEXT("one result per asset, reversed failure"), Second.Num(), 2))
    { return false; }
    TestFalse(TEXT("a failed clip save is reported against the clip"), Second[0].Get_Saved());
    TestTrue(TEXT("the failing clip is named"), Second[0].Get_AssetPath() == StrandedClip->GetPathName());
    TestTrue(TEXT("a failed clip save does not stop the preview save"), Second[1].Get_Saved());
    TestTrue(TEXT("the preview file was written"), PackageFileExists(MountedPreview));

    const auto Missing = ck::procedural_action_editor::SaveAssets(TArray<UObject*>{nullptr});
    TestTrue(TEXT("a missing asset is reported, not skipped"), Missing.Num() == 1 && NOT Missing[0].Get_Saved()
        && NOT Missing[0].Get_Reason().IsEmpty());
    return NOT HasAnyErrors();
}

#endif
