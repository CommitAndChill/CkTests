#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorData.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_editor_data
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FName Motion{TEXT("Motion")};
    const FName Claw{TEXT("Claw")};
    const FName Leg0{TEXT("Leg0")};
    const TCHAR* ScenarioPath = TEXT("/Game/CkTests/Missing/Scenario_DA.Scenario_DA");

    auto
        FindClipProperty(
            const TCHAR* InName)
        -> FProperty*
    { return UCk_ProceduralAction_Data::StaticClass()->FindPropertyByName(FName{InName}); }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Asset_EditorDataLivesInSidecar,
    "Ck.ProceduralAnimation.Action.Editor.Asset.EditorDataLivesInSidecar",
    ck_test_procedural_action_editor_data::TestFlags)

auto
    FCkTest_ProceduralAction_Editor_Asset_EditorDataLivesInSidecar::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_data;
    if (NOT TestTrue(TEXT("editor transaction system is idle"), GEditor != nullptr && GEditor->Trans != nullptr
        && NOT GEditor->Trans->IsActive() && NOT GEditor->IsPlaySessionInProgress()))
    { return false; }

    const auto* LegacySubject = FindClipProperty(TEXT("_EditorSubject"));
    TestTrue(TEXT("the runtime asset has no live editor subject: at most a load-only deprecated one"),
        LegacySubject == nullptr || LegacySubject->HasAnyPropertyFlags(CPF_Deprecated));
    const auto* LegacyGroups = FindClipProperty(TEXT("_EditorPoseGroups"));
    TestTrue(TEXT("the runtime asset has no live pose groups: at most a load-only deprecated one"),
        LegacyGroups == nullptr || LegacyGroups->HasAnyPropertyFlags(CPF_Deprecated));

    const auto Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto MountRoot = FString::Printf(TEXT("/CkActionEditorData_%s/"), *Unique);
    const auto MountDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ProceduralAction"),
        TEXT("EditorData"), Unique) + TEXT("/");
    const auto PackageName = MountRoot + TEXT("Clip");
    const auto ObjectPath = PackageName + TEXT(".Clip");
    if (NOT TestTrue(TEXT("disposable directory"), IFileManager::Get().MakeDirectory(*MountDirectory, true)))
    { return false; }
    FPackageName::RegisterMountPoint(MountRoot, MountDirectory);
    const auto PackageFile = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

    // A private transaction buffer, so unloading the disposable package never resets the editor's undo history.
    auto PreviousTransactions = TStrongObjectPtr<UTransactor>{GEditor->Trans.Get()};
    auto PrivateTransactions = TStrongObjectPtr<UTransBuffer>{NewObject<UTransBuffer>()};
    PrivateTransactions->Initialize(8 * 1024 * 1024);
    GEditor->Trans = PrivateTransactions.Get();
    ON_SCOPE_EXIT
    {
        if (GEditor->Trans.Get() == PrivateTransactions.Get())
        { GEditor->Trans = PreviousTransactions.Get(); }
        if (auto* Package = FindPackage(nullptr, *PackageName))
        {
            auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
            Unload.bResetTransBuffer = false;
            UPackageTools::UnloadPackages(Unload);
        }
        CollectGarbage(RF_NoFlags);
        FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory);
        IFileManager::Get().Delete(*PackageFile);
        IFileManager::Get().DeleteDirectory(*MountDirectory, false, true);
    };

    {
        auto* Package = CreatePackage(*PackageName);
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(
            Package, TEXT("Clip"), RF_Public | RF_Standalone | RF_Transactional)};
        TestNull(TEXT("a new clip has no editor data until something needs it"),
            UCk_Utils_ProceduralActionEditor_UE::TryGet_EditorData(Clip.Get()));
        auto* EditorData = UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(Clip.Get());
        if (NOT TestNotNull(TEXT("the clip's editor data is created on first use"), EditorData))
        { return false; }
        TestTrue(TEXT("asking again returns the same editor data"),
            UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(Clip.Get()) == EditorData);
        auto Subject = FCk_ProceduralActionEditor_Subject{};
        Subject.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{FCk_ProceduralAction_SlotBinding{Claw, Leg0}});
        Subject.Set_Scenario(TSoftObjectPtr<UCk_ProceduralActionPreview_Config>{FSoftObjectPath{ScenarioPath}});
        EditorData->Set_Subject(Subject);
        EditorData->Set_PoseLabels(TArray<FCk_ProceduralActionEditor_PoseLabel>{
            FCk_ProceduralActionEditor_PoseLabel{Motion, FCk_FloatRange_0to1{0.5}, FName{TEXT("Raised")}}});
        Package->MarkPackageDirty();
        auto SaveArgs = FSavePackageArgs{};
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GLog;
        if (NOT TestTrue(TEXT("save the disposable clip"), UPackage::SavePackage(Package, Clip.Get(), *PackageFile, SaveArgs)))
        { return false; }
    }

    PrivateTransactions->Reset(FText::FromString(TEXT("Discard disposable editor data test transactions")));
    {
        auto* Package = FindPackage(nullptr, *PackageName);
        auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
        Unload.bResetTransBuffer = false;
        if (NOT TestTrue(TEXT("unload the disposable clip"), Package != nullptr && UPackageTools::UnloadPackages(Unload)))
        { return false; }
        CollectGarbage(RF_NoFlags);
    }
    if (NOT TestNull(TEXT("the clip left memory before reopening"), FindObject<UCk_ProceduralAction_Data>(nullptr, *ObjectPath)))
    { return false; }

    auto* Reopened = LoadObject<UCk_ProceduralAction_Data>(nullptr, *ObjectPath);
    if (NOT TestNotNull(TEXT("reopen the saved clip"), Reopened))
    { return false; }
    TestFalse(TEXT("a reopened clip holds no legacy editor fields"), Reopened->Get_HasLegacyEditorData());
    const auto* EditorData = UCk_Utils_ProceduralActionEditor_UE::TryGet_EditorData(Reopened);
    if (NOT TestNotNull(TEXT("the editor data saved with the clip"), EditorData))
    { return false; }
    const auto Subject = EditorData->Get_Subject();
    TestTrue(TEXT("the subject's binding survived"), Subject.Get_SlotBindings().Num() == 1
        && Subject.Get_SlotBindings()[0].Get_Slot() == Claw && Subject.Get_SlotBindings()[0].Get_LimbId() == Leg0);
    TestEqual(TEXT("the subject's scenario survived"), Subject.Get_Scenario().ToSoftObjectPath().ToString(), FString{ScenarioPath});
    const auto Labels = EditorData->Get_PoseLabels();
    TestTrue(TEXT("the pose label survived"), Labels.Num() == 1 && Labels[0].Get_PhaseId() == Motion
        && Labels[0].Get_Time().Get_Value() == 0.5 && Labels[0].Get_Label() == FName{TEXT("Raised")});
    TestEqual(TEXT("the reopened clip is at the current compatibility version"), Reopened->Get_CompatibilityVersion(),
        UCk_ProceduralAction_Data::CurrentCompatibilityVersion);
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Asset_PostLoadMigratesLegacyEditorFields,
    "Ck.ProceduralAnimation.Action.Editor.Asset.PostLoadMigratesLegacyEditorFields",
    ck_test_procedural_action_editor_data::TestFlags)

auto
    FCkTest_ProceduralAction_Editor_Asset_PostLoadMigratesLegacyEditorFields::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_data;
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};

    // Written the way an old package loads them: by property name, with no accessor in between.
    auto* SubjectProperty = FindClipProperty(TEXT("_EditorSubject"));
    auto* GroupsProperty = FindClipProperty(TEXT("_EditorPoseGroups"));
    auto* VersionProperty = CastField<FIntProperty>(FindClipProperty(TEXT("_CompatibilityVersion")));
    if (NOT TestNotNull(TEXT("the legacy subject still loads"), SubjectProperty)
        || NOT TestNotNull(TEXT("the legacy pose groups still load"), GroupsProperty)
        || NOT TestNotNull(TEXT("the compatibility version is reflected"), VersionProperty))
    { return false; }
    const auto SubjectText = FString::Printf(TEXT("(_SlotBindings=((_Slot=\"%s\",_LimbId=\"%s\")),_Scenario=\"%s\")"),
        *Claw.ToString(), *Leg0.ToString(), ScenarioPath);
    const auto GroupsText = FString::Printf(
        TEXT("((_Label=\"Raised\",_PhaseId=\"%s\",_Time=(_Value=0.500000)),(_Label=\"Down\",_PhaseId=\"%s\",_Time=(_Value=1.000000)))"),
        *Motion.ToString(), *Motion.ToString());
    SubjectProperty->ImportText_InContainer(*SubjectText, Clip.Get(), Clip.Get(), PPF_None);
    GroupsProperty->ImportText_InContainer(*GroupsText, Clip.Get(), Clip.Get(), PPF_None);
    VersionProperty->SetPropertyValue_InContainer(Clip.Get(), 2);
    if (NOT TestEqual(TEXT("fixture: two legacy pose groups"), Clip->Get_LegacyEditorPoseGroups().Num(), 2)
        || NOT TestEqual(TEXT("fixture: one legacy binding"), Clip->Get_LegacyEditorSubject().Get_SlotBindings().Num(), 1))
    { return false; }
    TestNull(TEXT("fixture: no editor data yet"), UCk_Utils_ProceduralActionEditor_UE::TryGet_EditorData(Clip.Get()));

    Clip->SetFlags(RF_NeedPostLoad);
    Clip->ConditionalPostLoad();

    TestFalse(TEXT("the legacy fields are empty after migration"), Clip->Get_HasLegacyEditorData());
    TestEqual(TEXT("the clip is at version 3"), Clip->Get_CompatibilityVersion(), 3);
    const auto* EditorData = UCk_Utils_ProceduralActionEditor_UE::TryGet_EditorData(Clip.Get());
    if (NOT TestNotNull(TEXT("migration created the editor data"), EditorData))
    { return false; }
    const auto Subject = EditorData->Get_Subject();
    TestTrue(TEXT("the subject's binding moved"), Subject.Get_SlotBindings().Num() == 1
        && Subject.Get_SlotBindings()[0].Get_Slot() == Claw && Subject.Get_SlotBindings()[0].Get_LimbId() == Leg0);
    TestEqual(TEXT("the subject's scenario moved"), Subject.Get_Scenario().ToSoftObjectPath().ToString(), FString{ScenarioPath});
    const auto Labels = EditorData->Get_PoseLabels();
    if (NOT TestEqual(TEXT("one label per legacy pose group"), Labels.Num(), 2))
    { return false; }
    const auto* Raised = ck::procedural_action_editor::FindPoseLabel(Labels, Motion, 0.5);
    const auto* Down = ck::procedural_action_editor::FindPoseLabel(Labels, Motion, 1.0);
    TestTrue(TEXT("a group became a label at its phase and time, keeping its name"),
        Raised != nullptr && Raised->Get_Label() == FName{TEXT("Raised")});
    TestTrue(TEXT("every group became a label"), Down != nullptr && Down->Get_Label() == FName{TEXT("Down")});
    return NOT HasAnyErrors();
}

#endif
