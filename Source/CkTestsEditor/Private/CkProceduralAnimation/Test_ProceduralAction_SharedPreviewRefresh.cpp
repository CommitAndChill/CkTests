#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditor_UserSettings.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_test_procedural_action_shared_preview_refresh
{
    const FName TabId{TEXT("CkProceduralAction_Editor")};
    const TCHAR* BodyPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* LimbPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    const TCHAR* PreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");

    auto
        ContainsText(
            const TSharedRef<SWidget>& InWidget,
            const FString& InFragment,
            int32 InDepth = 0)
        -> bool
    {
        if (InDepth > 64)
        { return false; }
        if (InWidget->GetType() == FName{TEXT("STextBlock")}
            && StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString().Contains(InFragment))
        { return true; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (ContainsText(Children->GetChildAt(Index), InFragment, InDepth + 1))
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
            if (const auto Button = FindButton(Children->GetChildAt(Index), InLabel, InDepth + 1))
            { return Button; }
        }
        return {};
    }

    auto
        ClickButton(
            const TSharedRef<SWidget>& InContent,
            const FString& InLabel)
        -> bool
    {
        const auto Button = FindButton(InContent, InLabel);
        if (NOT Button.IsValid() || NOT Button->IsEnabled())
        { return false; }
        auto& Slate = FSlateApplication::Get();
        auto Path = FWidgetPath{};
        if (NOT Slate.FindPathToWidget(Button.ToSharedRef(), Path) || NOT Path.IsValid())
        { return false; }
        const auto Window = Slate.FindWidgetWindow(Button.ToSharedRef());
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
        const auto DownHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const auto UpHandled = Slate.ProcessMouseButtonUpEvent(Up);
        return DownHandled && UpHandled;
    }

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Body;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Limb;
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Preview;
        TArray<FCk_ProceduralActionPreview_Obstacle> OriginalObstacles;
        TArray<FCk_ProceduralAction_Phase> OriginalBodyPhases;
        FString OriginalBodyPath;
        FString OriginalLimbPath;
        TSharedPtr<SDockTab> BodyTab;
        TSharedPtr<SDockTab> LimbTab;
        double DeadlineSeconds = 0.0;
        uint64 BodyActivationFrame = 0;
        int32 Stage = 0;
        bool OriginalDirty = false;
        bool OriginalBodyDirty = false;
        bool OwnsEditors = false;
        bool SettingsChanged = false;
        bool ObstaclesChanged = false;
        bool BodyChanged = false;
        UWorld* SolverWorld = nullptr; // Identity only; never dereferenced after teardown.

        auto NotifyChanged() const -> void
        {
            auto Event = FPropertyChangedEvent{nullptr};
            FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Preview.Get(), Event);
        }

        auto Cleanup() -> void
        {
            if (BodyChanged && Body.IsValid())
            {
                Body->Set_Phases(OriginalBodyPhases);
                Body->PostEditChange();
                Body->GetOutermost()->SetDirtyFlag(OriginalBodyDirty);
                BodyChanged = false;
            }
            if (ObstaclesChanged && Preview.IsValid())
            {
                Preview->Set_StaticObstacles(OriginalObstacles);
                NotifyChanged();
                Preview->GetOutermost()->SetDirtyFlag(OriginalDirty);
                ObstaclesChanged = false;
            }
            if (OwnsEditors && Editors.IsValid())
            {
                if (Body.IsValid()) { Editors->CloseAllEditorsForAsset(Body.Get()); }
                if (Limb.IsValid()) { Editors->CloseAllEditorsForAsset(Limb.Get()); }
                OwnsEditors = false;
            }
            if (SettingsChanged && Body.IsValid() && Limb.IsValid())
            {
                auto* Settings = GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>();
                Settings->SetPreviewPath(Body->GetPathName(), OriginalBodyPath);
                Settings->SetPreviewPath(Limb->GetPathName(), OriginalLimbPath);
                SettingsChanged = false;
            }
            BodyTab.Reset();
            LimbTab.Reset();
        }

        ~FState() { Cleanup(); }
    };

    class FRunSharedPreviewRefresh final : public IAutomationLatentCommand
    {
    public:
        explicit FRunSharedPreviewRefresh(TSharedRef<FState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                _State->Test->AddError(FString::Printf(
                    TEXT("two-tab shared preview refresh timed out at stage %d"), _State->Stage));
                _State->Cleanup();
                return true;
            }
            if (_State->Stage == 0)
            {
                auto* BodyEditor = _State->Editors->FindEditorForAsset(_State->Body.Get(), false);
                auto* LimbEditor = _State->Editors->FindEditorForAsset(_State->Limb.Get(), false);
                if (BodyEditor == nullptr || LimbEditor == nullptr
                    || BodyEditor->GetAssociatedTabManager() == nullptr
                    || LimbEditor->GetAssociatedTabManager() == nullptr)
                { return false; }
                _State->BodyTab = BodyEditor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{TabId});
                _State->LimbTab = LimbEditor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{TabId});
                if (NOT _State->BodyTab.IsValid() || NOT _State->LimbTab.IsValid())
                { return false; }
                _State->Test->TestFalse(TEXT("shared preview begins with valid obstacle settings"),
                    ContainsText(_State->BodyTab->GetContent(), TEXT("obstacle 0"))
                    || ContainsText(_State->LimbTab->GetContent(), TEXT("obstacle 0")));
                auto Invalid = _State->OriginalObstacles;
                auto Obstacle = FCk_ProceduralActionPreview_Obstacle{};
                Obstacle.Set_HalfExtents(FVector{0.0, 5.0, 5.0});
                Invalid.Add(Obstacle);
                _State->Preview->Set_StaticObstacles(Invalid);
                _State->ObstaclesChanged = true;
                _State->NotifyChanged();
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 1)
            {
                if (NOT ContainsText(_State->BodyTab->GetContent(), TEXT("positive half extents"))
                    || NOT ContainsText(_State->LimbTab->GetContent(), TEXT("positive half extents")))
                { return false; }
                _State->Preview->Set_StaticObstacles(_State->OriginalObstacles);
                _State->NotifyChanged();
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 2)
            {
                if (ContainsText(_State->BodyTab->GetContent(), TEXT("positive half extents")))
                { return false; }
                auto* BodyEditor = _State->Editors->FindEditorForAsset(_State->Body.Get(), false);
                if (NOT _State->Test->TestNotNull(TEXT("Body editor remains mounted"), BodyEditor))
                { _State->Cleanup(); return true; }
                BodyEditor->FocusWindow(_State->Body.Get());
                if (const auto OwnerTab = BodyEditor->GetAssociatedTabManager()->GetOwnerTab())
                { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                _State->BodyTab->ActivateInParent(ETabActivationCause::SetDirectly);
                _State->BodyActivationFrame = GFrameCounter;
                _State->Stage = 21;
                return false;
            }
            if (_State->Stage == 21)
            {
                if (GFrameCounter <= _State->BodyActivationFrame)
                { return false; }
                const auto Button = FindButton(_State->BodyTab->GetContent(), TEXT("Solver (restart)"));
                if (NOT _State->Test->TestTrue(TEXT("Solver transport exists"), Button.IsValid())
                    || NOT _State->Test->TestTrue(TEXT("routed click starts Body Solver"),
                        ClickButton(_State->BodyTab->GetContent(), TEXT("Solver (restart)"))))
                { _State->Cleanup(); return true; }
                _State->Stage = 3;
                return false;
            }
            if (_State->Stage == 3)
            {
                if (NOT ContainsText(_State->BodyTab->GetContent(), TEXT("SOLVER:")))
                { return false; }
                for (const auto& Context : GEngine->GetWorldContexts())
                {
                    if (Context.WorldType == EWorldType::GamePreview && Context.World() != nullptr
                        && Context.World()->GetName().StartsWith(TEXT("CkProceduralActionSolverWorld")))
                    { _State->SolverWorld = Context.World(); break; }
                }
                if (NOT _State->Test->TestTrue(TEXT("Solver owns a private production world"),
                    _State->SolverWorld != nullptr))
                { _State->Cleanup(); return true; }
                auto Phases = _State->Body->Get_Phases();
                if (NOT _State->Test->TestTrue(TEXT("saved body clip has a phase to edit"),
                    NOT Phases.IsEmpty()))
                { _State->Cleanup(); return true; }
                Phases[0].Set_Duration(FCk_Time{Phases[0].Get_Duration().Get_Seconds() + 0.01});
                _State->Body->Set_Phases(Phases);
                _State->BodyChanged = true;
                _State->Body->PostEditChange();
                ++_State->Stage;
                return false;
            }
            if (NOT ContainsText(_State->BodyTab->GetContent(), TEXT("Restart Solver")))
            { return false; }
            auto WorldContextStillPresent = false;
            for (const auto& Context : GEngine->GetWorldContexts())
            { WorldContextStillPresent |= Context.World() == _State->SolverWorld; }
            _State->Test->TestFalse(TEXT("clip edit releases the old private world context"),
                WorldContextStillPresent);
            _State->Cleanup();
            return true;
        }

    private:
        TSharedRef<FState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SharedPreviewRefresh,
    "Ck.ProceduralAnimation.Action.Editor.MountedToolkit.SharedPreviewRefresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

auto
    FCkTest_ProceduralAction_SharedPreviewRefresh::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_shared_preview_refresh;
    if (GEditor == nullptr)
    { AddError(TEXT("shared preview refresh requires the real editor")); return false; }
    auto State = MakeShared<FState>();
    State->Test = this;
    State->Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    State->Body.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPath));
    State->Limb.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbPath));
    State->Preview.Reset(LoadObject<UCk_ProceduralActionPreview_Config>(nullptr, PreviewPath));
    if (NOT TestTrue(TEXT("two saved clips and shared preview config load"),
        State->Editors.IsValid() && State->Body.IsValid() && State->Limb.IsValid()
        && State->Preview.IsValid()))
    { return false; }
    if (State->Editors->FindEditorForAsset(State->Body.Get(), false) != nullptr
        || State->Editors->FindEditorForAsset(State->Limb.Get(), false) != nullptr)
    { AddError(TEXT("shared preview fixture refuses to borrow an already open editor")); return false; }
    State->OriginalObstacles = State->Preview->Get_StaticObstacles();
    State->OriginalDirty = State->Preview->GetOutermost()->IsDirty();
    State->OriginalBodyPhases = State->Body->Get_Phases();
    State->OriginalBodyDirty = State->Body->GetOutermost()->IsDirty();
    auto* Settings = GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>();
    State->OriginalBodyPath = Settings->GetPreviewPath(State->Body->GetPathName());
    State->OriginalLimbPath = Settings->GetPreviewPath(State->Limb->GetPathName());
    Settings->SetPreviewPath(State->Body->GetPathName(), PreviewPath);
    Settings->SetPreviewPath(State->Limb->GetPathName(), PreviewPath);
    State->SettingsChanged = true;
    State->OwnsEditors = true;
    if (NOT TestTrue(TEXT("two independent toolkits open with one referenced preview"),
        State->Editors->OpenEditorForAsset(State->Body.Get(), EToolkitMode::Standalone)
        && State->Editors->OpenEditorForAsset(State->Limb.Get(), EToolkitMode::Standalone)))
    { State->Cleanup(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 30.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunSharedPreviewRefresh(State));
    return true;
}

#endif
