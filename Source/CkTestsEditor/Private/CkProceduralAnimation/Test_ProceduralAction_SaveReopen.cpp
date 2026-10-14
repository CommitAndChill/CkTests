#include "Misc/AutomationTest.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"

#include "CoreGlobals.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Editor/Transactor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
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
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_test_procedural_action_save_reopen
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    const FName EditorTabId{TEXT("CkProceduralAction_Editor")};
    const TCHAR* SourcePath = TEXT(
        "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");

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
        FindTaggedEntry(
            const TSharedRef<SWidget>& InWidget,
            FName InTag,
            int32 InDepth = 0)
        -> TSharedPtr<SEditableTextBox>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetTag() == InTag
            && InWidget->GetType() == FName{TEXT("SEditableTextBox")})
        { return StaticCastSharedRef<SEditableTextBox>(InWidget); }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Entry = FindTaggedEntry(Children->GetChildAt(Index), InTag, InDepth + 1))
            { return Entry; }
        }
        return {};
    }

    auto
        ContainsText(
            const TSharedRef<SWidget>& InWidget,
            const FString& InLabel,
            int32 InDepth = 0)
        -> bool
    {
        if (InDepth > 64)
        { return false; }
        if (InWidget->GetType() == FName{TEXT("STextBlock")}
            && StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString() == InLabel)
        { return true; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (ContainsText(Children->GetChildAt(Index), InLabel, InDepth + 1))
            { return true; }
        }
        return false;
    }

    auto
        FindButton(
            const TSharedRef<SWidget>& InWidget,
            const FString& InLabel,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetType() == FName{TEXT("SButton")}
            && ContainsText(InWidget, InLabel))
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindButton(Children->GetChildAt(Index), InLabel, InDepth + 1))
            { return Found; }
        }
        return {};
    }

    auto
        ClickWidget(
            const TSharedRef<SWidget>& InWidget)
        -> bool
    {
        auto& Slate = FSlateApplication::Get();
        auto Path = FWidgetPath{};
        if (NOT InWidget->IsEnabled() || NOT Slate.FindPathToWidget(InWidget, Path)
            || NOT Path.IsValid())
        { return false; }
        const auto Window = Slate.FindWidgetWindow(InWidget);
        if (NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid())
        { return false; }
        const auto Position = FVector2D{Path.Widgets.Last().Geometry.GetAbsolutePositionAtCoordinates(
            FVector2D{0.5, 0.5})};
        const auto Released = TSet<FKey>{};
        const auto Pressed = TSet<FKey>{EKeys::LeftMouseButton};
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released, EKeys::Invalid, 0.0f, FModifierKeysState{}});
        const auto Down = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto Up = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto PressHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const auto ReleaseHandled = Slate.ProcessMouseButtonUpEvent(Up);
        return PressHandled && ReleaseHandled;
    }

    // The per-track tools sit in the pose workspace's collapsed Advanced area, in their own scroll box.
    auto
        Reveal(
            const TSharedRef<SWidget>& InRoot,
            const TSharedRef<SWidget>& InTarget)
        -> bool
    {
        auto& Slate = FSlateApplication::Get();
        auto Path = FWidgetPath{};
        if (NOT Slate.FindPathToWidget(InTarget, Path) || NOT Path.IsValid())
        {
            const auto Header = FindTaggedWidget(InRoot, FName{TEXT("CkActionEditorAdvancedHeader")});
            if (Header.IsValid() && Slate.FindPathToWidget(Header.ToSharedRef(), Path) && Path.IsValid())
            { ClickWidget(Header.ToSharedRef()); }
            return false;
        }
        for (auto Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
        {
            const auto& Widget = Path.Widgets[Index].Widget;
            if (Widget->GetType() == FName{TEXT("SScrollBox")})
            {
                StaticCastSharedRef<SScrollBox>(Widget)->ScrollDescendantIntoView(InTarget, false);
                break;
            }
        }
        return true;
    }

    auto
        TypeEntry(
            const TSharedRef<SEditableTextBox>& InEntry,
            const FString& InText)
        -> bool
    {
        auto& Slate = FSlateApplication::Get();
        if (NOT Slate.SetKeyboardFocus(InEntry, EFocusCause::SetDirectly)
            || NOT InEntry->HasKeyboardFocus())
        { return false; }
        const FModifierKeysState Control{false, false, true, false, false, false, false, false, false};
        const auto SelectAll = FKeyEvent{EKeys::A, Control, 0, false, 0, 0};
        if (NOT Slate.ProcessKeyDownEvent(SelectAll))
        { return false; }
        Slate.ProcessKeyUpEvent(SelectAll);
        for (const auto Character : InText)
        {
            if (NOT Slate.ProcessKeyCharEvent(FCharacterEvent{
                Character, FModifierKeysState{}, 0, false}))
            { return false; }
        }
        return InEntry->GetText().ToString() == InText;
    }

    auto
        FindTimelineMessage(
            const TSharedRef<SWidget>& InWidget,
            int32 InDepth = 0)
        -> FString
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetTag() == FName{TEXT("CkActionEditorTimelineMessage")} && InWidget->GetType() == FName{TEXT("STextBlock")})
        { return StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString(); }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            const auto Found = FindTimelineMessage(Children->GetChildAt(Index), InDepth + 1);
            if (NOT Found.IsEmpty())
            { return Found; }
        }
        return {};
    }

    auto
        TimelineMessage(
            UAssetEditorSubsystem* InEditors,
            UObject* InClip)
        -> FString
    {
        auto* Editor = InEditors != nullptr && InClip != nullptr ? InEditors->FindEditorForAsset(InClip, false) : nullptr;
        const auto Tab = Editor != nullptr && Editor->GetAssociatedTabManager() != nullptr
            ? Editor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId}) : TSharedPtr<SDockTab>{};
        return Tab.IsValid() ? FindTimelineMessage(Tab->GetContent()) : FString{TEXT("<no tab>")};
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
        FName PhaseId{TEXT("Cooldown")};
        FName NewMarkerId{TEXT("CooldownStart")};
        FName NewWindowId{TEXT("CooldownWindow")};
        TStrongObjectPtr<UTransactor> PreviousTransactions;
        TStrongObjectPtr<UTransBuffer> PrivateTransactions;
        bool Mounted = false;
        bool Revealed = false;
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
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _State->Test->AddError(FString::Printf(
                    TEXT("Disposable action editor save/reopen timed out at stage %d; dirty=%d, fileBytes=%lld, focus=%s."),
                    _State->Stage,
                    Cast<UCk_ProceduralAction_Data>(_State->ClipPath.ResolveObject()) != nullptr
                        && Cast<UCk_ProceduralAction_Data>(_State->ClipPath.ResolveObject())
                            ->GetOutermost()->IsDirty(),
                    _State->PackageFile.IsEmpty() ? -1LL
                        : IFileManager::Get().FileSize(*_State->PackageFile),
                    FSlateApplication::IsInitialized()
                        && FSlateApplication::Get().GetKeyboardFocusedWidget().IsValid()
                        ? *FSlateApplication::Get().GetKeyboardFocusedWidget()->GetType().ToString()
                        : TEXT("None")));
                return true;
            }
            auto* Editors = _State->Editors.Get();
            auto* Clip = Cast<UCk_ProceduralAction_Data>(_State->ClipPath.ResolveObject());
            if (Editors == nullptr || (_State->Stage != 6 && Clip == nullptr))
            { _State->Test->AddError(TEXT("Disposable authored clip disappeared before Save/Reopen.")); return true; }
            if (_State->Stage == 0)
            {
                auto* Editor = Editors->FindEditorForAsset(Clip, false);
                if (Editor == nullptr || Editor->GetAssociatedTabManager() == nullptr)
                { return false; }
                const auto Tab = Editor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId});
                if (NOT Tab.IsValid())
                { return false; }
                Editor->FocusWindow(Clip);
                if (const auto OwnerTab = Editor->GetAssociatedTabManager()->GetOwnerTab())
                { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                Tab->ActivateInParent(ETabActivationCause::SetDirectly);
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 1)
            {
                auto* Editor = Editors->FindEditorForAsset(Clip, false);
                if (Editor == nullptr || Editor->GetAssociatedTabManager() == nullptr)
                { return false; }
                const auto Tab = Editor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId});
                if (NOT Tab.IsValid() || NOT FSlateApplication::Get().FindWidgetWindow(Tab.ToSharedRef()).IsValid())
                { return false; }
                const auto Entry = FindTaggedEntry(Tab->GetContent(),
                    FName{TEXT("CkActionEditorNewPhaseId")});
                if (NOT _State->Test->TestTrue(TEXT("mounted phase ID input is available"),
                    Entry.IsValid()))
                { return true; }
                if (NOT _State->Revealed)
                {
                    _State->Revealed = Reveal(Tab->GetContent(), Entry.ToSharedRef());
                    return false;
                }
                auto& Slate = FSlateApplication::Get();
                if (NOT _State->Test->TestTrue(TEXT("Slate focuses the mounted phase input"),
                    Slate.SetKeyboardFocus(Entry, EFocusCause::SetDirectly)
                    && Entry->HasKeyboardFocus()))
                { return true; }
                for (const auto Character : _State->PhaseId.ToString())
                {
                    if (NOT Slate.ProcessKeyCharEvent(FCharacterEvent{
                        Character, FModifierKeysState{}, 0, false}))
                    { _State->Test->AddError(TEXT("Slate rejected routed phase ID text")); return true; }
                }
                _State->Test->TestEqual(TEXT("routed text entered the phase ID"),
                    Entry->GetText().ToString(), _State->PhaseId.ToString());
                const auto AddPhase = FindButton(Tab->GetContent(), TEXT("Add phase"));
                if (NOT _State->Test->TestTrue(TEXT("mounted Add phase control is available"),
                        AddPhase.IsValid())
                    || NOT _State->Test->TestTrue(TEXT("routed Slate click adds a phase"),
                        ClickWidget(AddPhase.ToSharedRef())))
                { return true; }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 2)
            {
                const auto Added = Clip->Get_Phases().ContainsByPredicate(
                    [Id = _State->PhaseId](const FCk_ProceduralAction_Phase& InPhase)
                    { return InPhase.Get_Id() == Id; });
                if (NOT _State->Test->TestTrue(TEXT("mounted phase creation changes the authored clip"),
                    Added && Clip->Get_Phases().Num() == 3))
                { return true; }
                auto* Editor = Editors->FindEditorForAsset(Clip, false);
                const auto Tab = Editor != nullptr && Editor->GetAssociatedTabManager() != nullptr
                    ? Editor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId})
                    : TSharedPtr<SDockTab>{};
                const auto Entry = Tab.IsValid() ? FindTaggedEntry(Tab->GetContent(),
                    FName{TEXT("CkActionEditorNewEventId")}) : TSharedPtr<SEditableTextBox>{};
                const auto MarkerButton = Tab.IsValid() ? FindButton(Tab->GetContent(),
                    TEXT("Marker at cursor")) : TSharedPtr<SWidget>{};
                if (MarkerButton.IsValid())
                { Reveal(Tab->GetContent(), MarkerButton.ToSharedRef()); }
                if (NOT _State->Test->TestTrue(TEXT("mounted marker lane controls are available"),
                    Entry.IsValid() && MarkerButton.IsValid())
                    || NOT _State->Test->TestTrue(TEXT("routed marker ID and click are accepted"),
                        TypeEntry(Entry.ToSharedRef(), _State->NewMarkerId.ToString())
                        && ClickWidget(MarkerButton.ToSharedRef())))
                { return true; }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 3)
            {
                const auto MarkerAdded = Clip->Get_Markers().ContainsByPredicate(
                    [Id = _State->NewMarkerId](const FCk_ProceduralAction_Marker& InMarker)
                    { return InMarker.Get_Id() == Id; });
                if (NOT _State->Test->TestTrue(FString::Printf(TEXT("mounted marker creation changes the authored clip (timeline says: %s)"),
                    *TimelineMessage(Editors, Clip)), MarkerAdded))
                { return true; }
                auto* Editor = Editors->FindEditorForAsset(Clip, false);
                const auto Tab = Editor != nullptr && Editor->GetAssociatedTabManager() != nullptr
                    ? Editor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId})
                    : TSharedPtr<SDockTab>{};
                const auto Entry = Tab.IsValid() ? FindTaggedEntry(Tab->GetContent(),
                    FName{TEXT("CkActionEditorNewEventId")}) : TSharedPtr<SEditableTextBox>{};
                const auto WindowButton = Tab.IsValid() ? FindButton(Tab->GetContent(),
                    TEXT("Window from cursor")) : TSharedPtr<SWidget>{};
                if (WindowButton.IsValid())
                { Reveal(Tab->GetContent(), WindowButton.ToSharedRef()); }
                if (NOT _State->Test->TestTrue(TEXT("mounted window lane controls are available"),
                    Entry.IsValid() && WindowButton.IsValid())
                    || NOT _State->Test->TestTrue(TEXT("routed window ID and click are accepted"),
                        TypeEntry(Entry.ToSharedRef(), _State->NewWindowId.ToString())
                        && ClickWidget(WindowButton.ToSharedRef())))
                { return true; }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 4)
            {
                const auto WindowAdded = Clip->Get_Windows().ContainsByPredicate(
                    [Id = _State->NewWindowId](const FCk_ProceduralAction_Window& InWindow)
                    { return InWindow.Get_Id() == Id; });
                if (NOT _State->Test->TestTrue(TEXT("mounted window creation changes the authored clip"),
                    WindowAdded))
                { return true; }
                auto* Editor = Editors->FindEditorForAsset(Clip, false);
                const auto Tab = Editor != nullptr && Editor->GetAssociatedTabManager() != nullptr
                    ? Editor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId})
                    : TSharedPtr<SDockTab>{};
                const auto Entry = Tab.IsValid() ? FindTaggedEntry(Tab->GetContent(),
                    FName{TEXT("CkActionEditorNewPhaseId")}) : TSharedPtr<SEditableTextBox>{};
                auto& Slate = FSlateApplication::Get();
                if (NOT _State->Test->TestTrue(TEXT("routed Ctrl+S starts from focused authoring input"),
                    Entry.IsValid() && Slate.SetKeyboardFocus(Entry, EFocusCause::SetDirectly)
                    && Entry->HasKeyboardFocus()))
                { return true; }
                auto Markers = Clip->Get_Markers();
                Markers[0].Set_At(FCk_ProceduralAction_TimeRef{
                    Markers[0].Get_At().Get_PhaseId(), FCk_FloatRange_0to1{0.75f}});
                Clip->Modify();
                Clip->Set_Markers(Markers);
                Clip->MarkPackageDirty();
                _State->Test->TestTrue(TEXT("disposable authored edit marks its package dirty"),
                    Clip->GetOutermost()->IsDirty());
                const FModifierKeysState Control{false, false, true, false, false, false, false, false, false};
                const auto SaveKey = FKeyEvent{EKeys::S, Control, 0, false, 0, 0};
                {
                    TGuardValue<bool> ScopedUnattendedSave{GIsRunningUnattendedScript, true};
                    _State->Test->TestTrue(TEXT("mounted editor routes Ctrl+S from focused authoring input"),
                        Slate.ProcessKeyDownEvent(SaveKey));
                    Slate.ProcessKeyUpEvent(SaveKey);
                }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 5)
            {
                if (Clip->GetOutermost()->IsDirty())
                { return false; }
                if (NOT _State->Test->TestTrue(TEXT("Ctrl+S saved the disposable authored package"),
                    IFileManager::Get().FileSize(*_State->PackageFile) > 0))
                { return true; }
                if (NOT _State->Test->TestTrue(TEXT("closing the saved toolkit releases its package"),
                    _State->CloseAndUnload()))
                { return true; }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 6)
            {
                auto* Reloaded = LoadObject<UCk_ProceduralAction_Data>(nullptr,
                    *_State->ClipPath.ToString());
                if (NOT _State->Test->TestNotNull(TEXT("saved clip reloads from the unique package"), Reloaded))
                { return true; }
                const auto Markers = Reloaded->Get_Markers();
                const auto* Marker = Markers.FindByPredicate(
                    [Id = _State->MarkerId](const FCk_ProceduralAction_Marker& InMarker)
                    { return InMarker.Get_Id() == Id; });
                if (NOT _State->Test->TestTrue(TEXT("reopened marker retains its stable ID"), Marker != nullptr))
                { return true; }
                _State->Test->TestTrue(TEXT("reopened clip retains the saved marker time"),
                    FMath::IsNearlyEqual(Marker->Get_At().Get_Time().Get_Value(), 0.75f));
                _State->Test->TestTrue(TEXT("reopened clip retains routed phase ID and order"),
                    Reloaded->Get_Phases().Num() == 3
                    && Reloaded->Get_Phases()[2].Get_Id() == _State->PhaseId);
                _State->Test->TestTrue(TEXT("reopened clip retains routed marker"),
                    Reloaded->Get_Markers().ContainsByPredicate(
                        [Id = _State->NewMarkerId](const FCk_ProceduralAction_Marker& InMarker)
                        { return InMarker.Get_Id() == Id; }));
                _State->Test->TestTrue(TEXT("reopened clip retains routed window"),
                    Reloaded->Get_Windows().ContainsByPredicate(
                        [Id = _State->NewWindowId](const FCk_ProceduralAction_Window& InWindow)
                        { return InWindow.Get_Id() == Id; }));
                _State->Test->TestTrue(TEXT("reloaded clip opens in the registered action toolkit"),
                    Editors->OpenEditorForAsset(Reloaded, EToolkitMode::Standalone));
                ++_State->Stage;
                return false;
            }
            auto* Reopened = Editors->FindEditorForAsset(Clip, false);
            _State->Test->TestTrue(TEXT("saved clip reopens as a mounted editor instance"),
                Reopened != nullptr && Reopened->GetAssociatedTabManager() != nullptr
                && Reopened->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId}).IsValid());
            _State->Test->TestTrue(TEXT("reopened disposable package tears down cleanly"),
                _State->CloseAndUnload());
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
