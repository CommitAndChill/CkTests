#include "Misc/AutomationTest.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModel.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_EditorToolkit.h"

#include "CoreGlobals.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Editor/Transactor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

namespace ck_test_procedural_action_save_reopen
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    const TCHAR* SourcePath = TEXT(
        "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const FName TimelineTag{TEXT("CkActionEditor.Timeline")};

    auto
        FindTaggedWidget(
            const TSharedRef<SWidget>& InWidget,
            FName InTag,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetTag() == InTag)
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindTaggedWidget(Children->GetChildAt(Index), InTag, InDepth + 1))
            { return Found; }
        }
        return {};
    }

    auto
        FindToolkit(
            UAssetEditorSubsystem* InEditors,
            UObject* InClip)
        -> FCk_ProceduralAction_EditorToolkit*
    {
        auto* Instance = InEditors != nullptr && InClip != nullptr ? InEditors->FindEditorForAsset(InClip, false) : nullptr;
        if (Instance == nullptr || Instance->GetEditorName() != FCk_ProceduralAction_EditorToolkit::ToolkitName)
        { return nullptr; }
        return static_cast<FCk_ProceduralAction_EditorToolkit*>(Instance);
    }

    auto
        FindTimelineTab(
            FCk_ProceduralAction_EditorToolkit& InToolkit)
        -> TSharedPtr<SDockTab>
    {
        const auto TabManager = InToolkit.GetAssociatedTabManager();
        return TabManager.IsValid()
            ? TabManager->FindExistingLiveTab(FTabId{FCk_ProceduralAction_EditorToolkit::TimelineTabId})
            : TSharedPtr<SDockTab>{};
    }

    // The id the toolkit gives a new item: InPrefix followed by the first unused number from 1.
    auto
        FirstFreeId(
            const TCHAR* InPrefix,
            const TArray<FName>& InTaken)
        -> FName
    {
        for (auto Index = 1; Index <= InTaken.Num() + 1; ++Index)
        {
            const auto Candidate = FName{*FString::Printf(TEXT("%s%d"), InPrefix, Index)};
            if (NOT InTaken.Contains(Candidate))
            { return Candidate; }
        }
        return NAME_None;
    }

    auto
        PhaseIds(
            const UCk_ProceduralAction_Data& InClip)
        -> TArray<FName>
    {
        auto Ids = TArray<FName>{};
        for (const auto& Phase : InClip.Get_Phases())
        { Ids.Add(Phase.Get_Id()); }
        return Ids;
    }

    auto
        EventIds(
            const UCk_ProceduralAction_Data& InClip)
        -> TArray<FName>
    {
        auto Ids = TArray<FName>{};
        for (const auto& Marker : InClip.Get_Markers())
        { Ids.Add(Marker.Get_Id()); }
        for (const auto& Window : InClip.Get_Windows())
        { Ids.Add(Window.Get_Id()); }
        return Ids;
    }

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        FString MountRoot;
        FString MountDirectory;
        FString PackageName;
        FString PackageFile;
        FSoftObjectPath ClipPath;
        FName MarkerId;
        FName NewSectionId;
        FName NewMarkerId;
        FName NewWindowId;
        TArray<FName> SectionOrder;
        int32 SectionCountBefore = 0;
        FString LastWaitReason;
        TStrongObjectPtr<UTransactor> PreviousTransactions;
        TStrongObjectPtr<UTransBuffer> PrivateTransactions;
        bool Mounted = false;
        int32 Stage = 0;
        double DeadlineSeconds = 0.0;

        auto InstallPrivateTransactions() -> bool
        {
            if (GEditor == nullptr || GEditor->Trans == nullptr
                || GEditor->Trans->IsActive() || GEditor->IsPlaySessionInProgress())
            { return false; }
            PreviousTransactions.Reset(GEditor->Trans.Get());
            PrivateTransactions.Reset(NewObject<UTransBuffer>());
            if (NOT PrivateTransactions.IsValid())
            { PreviousTransactions.Reset(); return false; }
            PrivateTransactions->Initialize(8 * 1024 * 1024);
            GEditor->Trans = PrivateTransactions.Get();
            return true;
        }

        auto RestoreTransactions() -> void
        {
            if (GEditor != nullptr && PrivateTransactions.IsValid()
                && GEditor->Trans.Get() == PrivateTransactions.Get())
            { GEditor->Trans = PreviousTransactions.Get(); }
            PrivateTransactions.Reset();
            PreviousTransactions.Reset();
        }

        auto CloseAndUnload() -> bool
        {
            if (FSlateApplication::IsInitialized())
            { FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly); }
            if (auto* Clip = Cast<UCk_ProceduralAction_Data>(ClipPath.ResolveObject()))
            {
                if (Editors.IsValid())
                { Editors->CloseAllEditorsForAsset(Clip); }
                if (Editors.IsValid() && Editors->FindEditorForAsset(Clip, false) != nullptr)
                { Test->AddInfo(TEXT("Disposable action toolkit remains registered after close request.")); }
            }
            if (PrivateTransactions.IsValid())
            {
                PrivateTransactions->Reset(FText::FromString(
                    TEXT("Discard disposable action editor test transactions")));
            }
            if (auto* Package = FindPackage(nullptr, *PackageName))
            {
                auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
                Unload.bResetTransBuffer = false;
                if (NOT UPackageTools::UnloadPackages(Unload))
                {
                    Test->AddInfo(FString::Printf(TEXT("Disposable package unload failed: %s"),
                        *Unload.OutErrorMessage.ToString()));
                    return false;
                }
            }
            CollectGarbage(RF_NoFlags);
            const auto Released = ClipPath.ResolveObject() == nullptr;
            if (NOT Released)
            { Test->AddInfo(TEXT("Disposable package unload returned success, but its asset remains loaded.")); }
            return Released;
        }

        auto Cleanup() -> void
        {
            auto Released = true;
            if (NOT PackageName.IsEmpty())
            { Released = CloseAndUnload(); }
            RestoreTransactions();
            if (NOT Released)
            { return; }
            if (Mounted)
            { FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory); Mounted = false; }
            if (NOT PackageFile.IsEmpty())
            { IFileManager::Get().Delete(*PackageFile); }
            if (NOT MountDirectory.IsEmpty())
            { IFileManager::Get().DeleteDirectory(*MountDirectory); }
        }

        ~FState() { Cleanup(); }
    };

    auto
        MakeDisposableClip(
            FState& InState)
        -> bool
    {
        const auto Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        InState.MountRoot = FString::Printf(TEXT("/CkActionEditorSave_%s/"), *Unique);
        InState.MountDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
            TEXT("Automation"), TEXT("ProceduralAction"), TEXT("SaveReopen"), Unique) + TEXT("/");
        if (NOT IFileManager::Get().MakeDirectory(*InState.MountDirectory, true))
        { return false; }
        FPackageName::RegisterMountPoint(InState.MountRoot, InState.MountDirectory);
        InState.Mounted = true;
        InState.PackageName = InState.MountRoot + TEXT("Clip");
        auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, SourcePath);
        auto* Package = CreatePackage(*InState.PackageName);
        if (Source == nullptr || Package == nullptr || Source->Get_Markers().IsEmpty())
        { return false; }
        auto* Clip = DuplicateObject<UCk_ProceduralAction_Data>(Source, Package, TEXT("Clip"));
        if (Clip == nullptr)
        { return false; }
        Clip->SetFlags(RF_Public | RF_Standalone);
        InState.MarkerId = Clip->Get_Markers()[0].Get_Id();
        InState.ClipPath = FSoftObjectPath{InState.PackageName + TEXT(".Clip")};
        InState.PackageFile = FPackageName::LongPackageNameToFilename(
            InState.PackageName, FPackageName::GetAssetPackageExtension());
        Package->MarkPackageDirty();
        auto SaveArgs = FSavePackageArgs{};
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GLog;
        return UPackage::SavePackage(Package, Clip, *InState.PackageFile, SaveArgs);
    }

    class FRunSaveReopen final : public IAutomationLatentCommand
    {
    public:
        explicit FRunSaveReopen(TSharedRef<FState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            {
                State.Test->AddError(FString::Printf(
                    TEXT("Disposable action editor save/reopen timed out at stage %d (%s); dirty=%d, fileBytes=%lld, focus=%s."),
                    State.Stage, *State.LastWaitReason,
                    Cast<UCk_ProceduralAction_Data>(State.ClipPath.ResolveObject()) != nullptr
                        && Cast<UCk_ProceduralAction_Data>(State.ClipPath.ResolveObject())
                            ->GetOutermost()->IsDirty(),
                    State.PackageFile.IsEmpty() ? -1LL
                        : IFileManager::Get().FileSize(*State.PackageFile),
                    FSlateApplication::IsInitialized()
                        && FSlateApplication::Get().GetKeyboardFocusedWidget().IsValid()
                        ? *FSlateApplication::Get().GetKeyboardFocusedWidget()->GetType().ToString()
                        : TEXT("None")));
                return true;
            }
            auto* Editors = State.Editors.Get();
            auto* Clip = Cast<UCk_ProceduralAction_Data>(State.ClipPath.ResolveObject());
            if (Editors == nullptr || (State.Stage != 7 && Clip == nullptr))
            { State.Test->AddError(TEXT("Disposable authored clip disappeared before Save/Reopen.")); return true; }
            auto* Toolkit = State.Stage != 7 ? FindToolkit(Editors, Clip) : nullptr;
            if (State.Stage == 0)
            {
                State.LastWaitReason = TEXT("toolkit timeline tab not mounted");
                if (Toolkit == nullptr)
                { return false; }
                const auto Tab = FindTimelineTab(*Toolkit);
                if (NOT Tab.IsValid())
                { return false; }
                Toolkit->FocusWindow(Clip);
                if (const auto OwnerTab = Toolkit->GetAssociatedTabManager()->GetOwnerTab())
                { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                Tab->ActivateInParent(ETabActivationCause::SetDirectly);
                ++State.Stage;
                return false;
            }
            if (State.Stage <= 6 && Toolkit == nullptr)
            { State.Test->AddError(TEXT("The action toolkit closed before Save.")); return true; }
            if (State.Stage == 1)
            {
                const auto Tab = FindTimelineTab(*Toolkit);
                State.LastWaitReason = TEXT("timeline tab has no Slate window");
                if (NOT Tab.IsValid() || NOT FSlateApplication::Get().FindWidgetWindow(Tab.ToSharedRef()).IsValid())
                { return false; }
                State.NewSectionId = FirstFreeId(TEXT("Section"), PhaseIds(*Clip));
                State.SectionCountBefore = Clip->Get_Phases().Num();
                const auto Added = Toolkit->AddSection();
                State.Test->AddInfo(FString::Printf(TEXT("Add Section: %s"), *Toolkit->Get_Message()));
                if (NOT State.Test->TestTrue(TEXT("Add Section adds a section"), Added))
                { return true; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 2)
            {
                const auto Ids = PhaseIds(*Clip);
                if (NOT State.Test->TestTrue(FString::Printf(TEXT("mounted section creation adds %s to the authored clip"),
                    *State.NewSectionId.ToString()), Ids.Contains(State.NewSectionId) && Ids.Num() == State.SectionCountBefore + 1))
                { return true; }
                const auto* Section = Clip->Get_Phases().FindByPredicate([Id = State.NewSectionId](const FCk_ProceduralAction_Phase& InPhase)
                    { return InPhase.Get_Id() == Id; });
                auto Start = FCk_Time{};
                State.LastWaitReason = TEXT("the session does not know the new section's start yet");
                if (NOT Toolkit->Get_Session()->Get_PhaseStart(State.NewSectionId, Start))
                { return false; }
                State.SectionOrder = Ids;
                Toolkit->Get_Model()->SetTime(FCk_Time{Start.Get_Seconds() + 0.5 * Section->Get_Duration().Get_Seconds()});
                ++State.Stage;
                return false;
            }
            if (State.Stage == 3)
            {
                State.NewMarkerId = FirstFreeId(TEXT("Marker"), EventIds(*Clip));
                const auto Added = Toolkit->AddMarkerAtPlayhead();
                State.Test->AddInfo(FString::Printf(TEXT("Add Marker: %s"), *Toolkit->Get_Message()));
                if (NOT State.Test->TestTrue(TEXT("Add Marker at the playhead is accepted"), Added))
                { return true; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 4)
            {
                const auto* Marker = Clip->Get_Markers().FindByPredicate([Id = State.NewMarkerId](const FCk_ProceduralAction_Marker& InMarker)
                    { return InMarker.Get_Id() == Id; });
                if (NOT State.Test->TestTrue(FString::Printf(TEXT("mounted marker creation adds %s at the playhead's section (editor says: %s)"),
                    *State.NewMarkerId.ToString(), *Toolkit->Get_Message()),
                    Marker != nullptr && Marker->Get_At().Get_PhaseId() == State.NewSectionId))
                { return true; }
                State.NewWindowId = FirstFreeId(TEXT("Window"), EventIds(*Clip));
                const auto Added = Toolkit->AddWindowAtPlayhead();
                State.Test->AddInfo(FString::Printf(TEXT("Add Window: %s"), *Toolkit->Get_Message()));
                if (NOT State.Test->TestTrue(TEXT("Add Window from the playhead is accepted"), Added))
                { return true; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 5)
            {
                const auto* Window = Clip->Get_Windows().FindByPredicate([Id = State.NewWindowId](const FCk_ProceduralAction_Window& InWindow)
                    { return InWindow.Get_Id() == Id; });
                if (NOT State.Test->TestTrue(FString::Printf(TEXT("mounted window creation adds %s from the playhead's section"),
                    *State.NewWindowId.ToString()),
                    Window != nullptr && Window->Get_Start().Get_PhaseId() == State.NewSectionId))
                { return true; }
                const auto Tab = FindTimelineTab(*Toolkit);
                const auto Timeline = Tab.IsValid() ? FindTaggedWidget(Tab->GetContent(), TimelineTag) : TSharedPtr<SWidget>{};
                auto& Slate = FSlateApplication::Get();
                if (NOT State.Test->TestTrue(TEXT("routed Ctrl+S starts from the focused timeline"),
                    Timeline.IsValid() && Slate.SetKeyboardFocus(Timeline, EFocusCause::SetDirectly)
                    && Timeline->HasKeyboardFocus()))
                { return true; }
                auto Markers = Clip->Get_Markers();
                const auto MarkerIndex = Markers.IndexOfByPredicate([Id = State.MarkerId](const FCk_ProceduralAction_Marker& InMarker)
                    { return InMarker.Get_Id() == Id; });
                if (NOT State.Test->TestTrue(TEXT("the saved marker is still on the clip"), Markers.IsValidIndex(MarkerIndex)))
                { return true; }
                Markers[MarkerIndex].Set_At(FCk_ProceduralAction_TimeRef{
                    Markers[MarkerIndex].Get_At().Get_PhaseId(), FCk_FloatRange_0to1{0.75f}});
                Clip->Modify();
                Clip->Set_Markers(Markers);
                Clip->MarkPackageDirty();
                State.Test->TestTrue(TEXT("disposable authored edit marks its package dirty"),
                    Clip->GetOutermost()->IsDirty());
                const FModifierKeysState Control{false, false, true, false, false, false, false, false, false};
                const auto SaveKey = FKeyEvent{EKeys::S, Control, 0, false, 0, 0};
                {
                    TGuardValue<bool> ScopedUnattendedSave{GIsRunningUnattendedScript, true};
                    State.Test->TestTrue(TEXT("mounted editor routes Ctrl+S from the focused timeline"),
                        Slate.ProcessKeyDownEvent(SaveKey));
                    Slate.ProcessKeyUpEvent(SaveKey);
                }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 6)
            {
                State.LastWaitReason = TEXT("waiting for Ctrl+S to clear the package's dirty flag");
                if (Clip->GetOutermost()->IsDirty())
                { return false; }
                if (NOT State.Test->TestTrue(TEXT("Ctrl+S saved the disposable authored package"),
                    IFileManager::Get().FileSize(*State.PackageFile) > 0))
                { return true; }
                if (NOT State.Test->TestTrue(TEXT("closing the saved toolkit releases its package"),
                    State.CloseAndUnload()))
                { return true; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 7)
            {
                auto* Reloaded = LoadObject<UCk_ProceduralAction_Data>(nullptr,
                    *State.ClipPath.ToString());
                if (NOT State.Test->TestNotNull(TEXT("saved clip reloads from the unique package"), Reloaded))
                { return true; }
                const auto Markers = Reloaded->Get_Markers();
                const auto* Marker = Markers.FindByPredicate(
                    [Id = State.MarkerId](const FCk_ProceduralAction_Marker& InMarker)
                    { return InMarker.Get_Id() == Id; });
                if (NOT State.Test->TestTrue(TEXT("reopened marker retains its stable ID"), Marker != nullptr))
                { return true; }
                State.Test->TestTrue(TEXT("reopened clip retains the saved marker time"),
                    FMath::IsNearlyEqual(Marker->Get_At().Get_Time().Get_Value(), 0.75f));
                State.Test->TestTrue(TEXT("reopened clip retains the added section's ID and order"),
                    PhaseIds(*Reloaded) == State.SectionOrder && State.SectionOrder.Contains(State.NewSectionId));
                State.Test->TestTrue(TEXT("reopened clip retains the added marker"),
                    Reloaded->Get_Markers().ContainsByPredicate(
                        [Id = State.NewMarkerId](const FCk_ProceduralAction_Marker& InMarker)
                        { return InMarker.Get_Id() == Id; }));
                State.Test->TestTrue(TEXT("reopened clip retains the added window"),
                    Reloaded->Get_Windows().ContainsByPredicate(
                        [Id = State.NewWindowId](const FCk_ProceduralAction_Window& InWindow)
                        { return InWindow.Get_Id() == Id; }));
                State.Test->TestTrue(TEXT("reloaded clip opens in the registered action toolkit"),
                    Editors->OpenEditorForAsset(Reloaded, EToolkitMode::Standalone));
                ++State.Stage;
                return false;
            }
            auto* Reopened = FindToolkit(Editors, Clip);
            State.Test->TestTrue(TEXT("saved clip reopens as a mounted editor instance"),
                Reopened != nullptr && FindTimelineTab(*Reopened).IsValid());
            State.Test->TestTrue(TEXT("reopened disposable package tears down cleanly"),
                State.CloseAndUnload());
            return true;
        }

    private:
        TSharedRef<FState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SaveReopen,
    "Ck.ProceduralAnimation.Action.Editor.SaveReopen.FocusedAssetIdentity",
    ck_test_procedural_action_save_reopen::TestFlags)

auto
    FCkTest_ProceduralAction_SaveReopen::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_save_reopen;
    if (NOT FSlateApplication::IsInitialized() || GEditor == nullptr
        || NOT FApp::IsUnattended())
    { AddError(TEXT("Save/Reopen requires an isolated unattended mounted Slate editor.")); return false; }
    auto State = MakeShared<FState>();
    State->Test = this;
    State->Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (NOT TestTrue(TEXT("unique saved action clip is available"),
        State->Editors.IsValid() && MakeDisposableClip(*State)))
    { return false; }
    if (NOT TestTrue(TEXT("disposable authoring transactions are isolated from editor undo"),
        State->InstallPrivateTransactions()))
    { return false; }
    auto* Clip = Cast<UCk_ProceduralAction_Data>(State->ClipPath.ResolveObject());
    if (NOT TestTrue(TEXT("disposable package has a live authored clip"), Clip != nullptr)
        || NOT TestTrue(TEXT("disposable clip opens through its asset definition"),
            State->Editors->OpenEditorForAsset(Clip, EToolkitMode::Standalone)))
    { return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 15.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunSaveReopen(State));
    return true;
}
