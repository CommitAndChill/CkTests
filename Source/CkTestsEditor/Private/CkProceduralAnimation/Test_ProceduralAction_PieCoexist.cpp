#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditor_UserSettings.h"

#include "CoreGlobals.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_test_procedural_action_pie_coexist
{
    const FName EditorTabId{TEXT("CkProceduralAction_Editor")};
    const TCHAR* ClipPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* PreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    const TCHAR* EntryMapPath = TEXT("/Engine/Maps/Entry.Entry");

    auto
        GetPieWorld()
        -> UWorld*
    {
        if (GEngine == nullptr)
        { return nullptr; }
        for (const auto& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE && Context.World() != nullptr)
            { return Context.World(); }
        }
        return nullptr;
    }

    auto
        GetSolverWorld()
        -> UWorld*
    {
        if (GEngine == nullptr)
        { return nullptr; }
        for (const auto& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::GamePreview && Context.World() != nullptr
                && Context.World()->GetName() == TEXT("CkProceduralActionSolverWorld"))
            { return Context.World(); }
        }
        return nullptr;
    }

    auto
        TraceRemainingWorlds(
            FAutomationTestBase& InTest)
        -> void
    {
        for (const auto& Context : GEngine->GetWorldContexts())
        {
            InTest.AddInfo(FString::Printf(TEXT("PIE teardown context: type=%d world=%s"),
                static_cast<int32>(Context.WorldType),
                Context.World() != nullptr ? *Context.World()->GetPathName() : TEXT("none")));
        }
        for (TObjectIterator<UWorld> World; World; ++World)
        {
            if (World->GetPathName().Contains(TEXT("/Engine/Maps/Entry"))
                || World->GetName() == TEXT("CkProceduralActionSolverWorld"))
            {
                InTest.AddInfo(FString::Printf(
                    TEXT("PIE teardown retained world: %s type=%d initialized=%d flags=%u"),
                    *World->GetPathName(), static_cast<int32>(World->WorldType),
                    World->IsInitialized(),
                    static_cast<uint32>(World->GetFlags())));
            }
        }
        for (TObjectIterator<UWorldSubsystem> Subsystem; Subsystem; ++Subsystem)
        {
            if (Subsystem->GetPathName().Contains(TEXT("/Engine/Maps/Entry")))
            {
                InTest.AddInfo(FString::Printf(TEXT("PIE teardown retained world subsystem: %s"),
                    *Subsystem->GetPathName()));
            }
        }
    }

    auto
        HasText(
            const TSharedRef<SWidget>& InWidget,
            const FString& InText,
            int32 InDepth = 0)
        -> bool
    {
        if (InDepth > 64)
        { return false; }
        if (InWidget->GetType() == FName{TEXT("STextBlock")}
            && StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString() == InText)
        { return true; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (HasText(Children->GetChildAt(Index), InText, InDepth + 1))
            { return true; }
        }
        return false;
    }

    auto
        HasTextContaining(
            const TSharedRef<SWidget>& InWidget,
            const FString& InText,
            int32 InDepth = 0)
        -> bool
    {
        if (InDepth > 64)
        { return false; }
        if (InWidget->GetType() == FName{TEXT("STextBlock")}
            && StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString().Contains(InText))
        { return true; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (HasTextContaining(Children->GetChildAt(Index), InText, InDepth + 1))
            { return true; }
        }
        return false;
    }

    auto
        FindButton(
            const TSharedRef<SWidget>& InWidget,
            const FString& InText,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetType() == FName{TEXT("SButton")}
            && HasText(InWidget, InText))
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindButton(Children->GetChildAt(Index), InText, InDepth + 1))
            { return Found; }
        }
        return {};
    }

    auto
        ClickButton(
            const TSharedRef<SWidget>& InContent,
            const FString& InText,
            FAutomationTestBase* InTest = nullptr)
        -> bool
    {
        const auto Button = FindButton(InContent, InText);
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
        if (InTest != nullptr)
        {
            const auto HitPath = Slate.LocateWindowUnderMouse(Position,
                Slate.GetInteractiveTopLevelWindows(), false, 0);
            const auto CursorUser = Slate.GetCursorUser();
            const auto Captor = CursorUser->GetCursorCaptor();
            const auto ActiveWindow = Slate.GetActiveTopLevelWindow();
            InTest->AddInfo(FString::Printf(
                TEXT("PIE routed %s hit: targetInPath=%d capture=%d captor=%s hitWindow=%s activeWindow=%s buttonWindow=%s center=(%.1f,%.1f)"),
                *InText, HitPath.ContainsWidget(Button.Get()),
                CursorUser->HasCapture(FSlateApplication::CursorPointerIndex),
                Captor.IsValid() ? *Captor->GetType().ToString() : TEXT("none"),
                HitPath.IsValid() ? *HitPath.GetWindow()->GetTitle().ToString() : TEXT("none"),
                ActiveWindow.IsValid() ? *ActiveWindow->GetTitle().ToString() : TEXT("none"),
                *Window->GetTitle().ToString(), Position.X, Position.Y));
            if (NOT HitPath.ContainsWidget(Button.Get()))
            { return false; }
        }
        const auto Released = TSet<FKey>{};
        const auto Pressed = TSet<FKey>{EKeys::LeftMouseButton};
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released, EKeys::Invalid, 0.0f, FModifierKeysState{}});
        const auto Down = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto Up = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const bool bDownHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const bool bUpHandled = Slate.ProcessMouseButtonUpEvent(Up);
        return bDownHandled && bUpHandled;
    }

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<ULevelEditorPlaySettings> PlaySettings;
        TStrongObjectPtr<UWorld> EntrySourceWorld;
        FString PreviousPreviewPath;
        UWorld* SolverWorld = nullptr;
        TSharedPtr<SDockTab> EditorTab;
        double SolverTimeBeforePie = 0.0;
        double StageStartedSolverTime = -1.0;
        double DeadlineSeconds = 0.0;
        uint64 StageStartedFrame = 0;
        uint64 PlayRouteFrame = 0;
        uint64 TeardownClearFrame = 0;
        int32 Stage = 0;
        int32 PlayRouteStage = 0;
        bool PlayRequested = false;
        bool OwnsEditor = false;
        bool PreviewOverrideApplied = false;
        bool OwnsEntrySourceWorld = false;
        bool ForcedTeardown = false;

        auto AdvanceStage() -> void
        {
            ++Stage;
            StageStartedFrame = GFrameCounter;
            StageStartedSolverTime = GetSolverWorld() != nullptr
                ? GetSolverWorld()->GetTimeSeconds() : -1.0;
            Test->AddInfo(FString::Printf(
                TEXT("PIE coexist stage %d: solver=%d PIE=%d solverTime=%.3f frame=%llu"),
                Stage, GetSolverWorld() != nullptr, GetPieWorld() != nullptr,
                StageStartedSolverTime,
                static_cast<unsigned long long>(GFrameCounter)));
        }

        auto IsPieTeardownSettled() -> bool
        {
            if (GetPieWorld() != nullptr || (GUnrealEd != nullptr
                && (GUnrealEd->IsPlaySessionRequestQueued() || GUnrealEd->ShouldEndPlayMap())))
            { TeardownClearFrame = 0; return false; }
            if (TeardownClearFrame == 0)
            { TeardownClearFrame = GFrameCounter; return false; }
            return GFrameCounter > TeardownClearFrame;
        }

        auto Cleanup() -> void
        {
            if (OwnsEditor && Editors.IsValid() && Clip.IsValid())
            { Editors->CloseAllEditorsForAsset(Clip.Get()); OwnsEditor = false; }
            if (PlayRequested && GUnrealEd != nullptr && GUnrealEd->IsPlaySessionRequestQueued())
            { GUnrealEd->CancelRequestPlaySession(); }
            if (PlayRequested && GetPieWorld() != nullptr && GUnrealEd != nullptr)
            { GUnrealEd->RequestEndPlayMap(); }
            if (PreviewOverrideApplied && Clip.IsValid())
            {
                GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>()->SetPreviewPath(
                    Clip->GetPathName(), PreviousPreviewPath);
                PreviewOverrideApplied = false;
            }
            EditorTab.Reset();
            SolverWorld = nullptr;
            PlaySettings.Reset();
            Clip.Reset();
            if (EntrySourceWorld.IsValid())
            {
                if (OwnsEntrySourceWorld)
                {
                    checkf(GetPieWorld() == nullptr && GUnrealEd != nullptr
                        && NOT GUnrealEd->IsPlaySessionRequestQueued(),
                        TEXT("The fixture-owned Entry source map cannot be cleaned while PIE is live."));
                    Test->AddInfo(FString::Printf(
                        TEXT("PIE Entry source before owned cleanup: %s type=%d initialized=%d"),
                        *EntrySourceWorld->GetPathName(),
                        static_cast<int32>(EntrySourceWorld->WorldType),
                        EntrySourceWorld->IsInitialized()));
                    if (EntrySourceWorld->IsInitialized())
                    { EntrySourceWorld->CleanupWorld(false, true); }
                    Test->AddInfo(FString::Printf(
                        TEXT("PIE Entry source after owned cleanup: initialized=%d"),
                        EntrySourceWorld->IsInitialized()));
                }
                EntrySourceWorld.Reset();
                OwnsEntrySourceWorld = false;
            }
        }

        ~FState() { Cleanup(); }
    };

    class FRunPieCoexist final : public IAutomationLatentCommand
    {
    public:
        explicit FRunPieCoexist(TSharedRef<FState> InState) : _State(InState) {}

        auto BeginFailureTeardown(const FString& InError) -> void
        {
            _State->Test->AddError(InError);
            if (_State->OwnsEditor && _State->Editors.IsValid() && _State->Clip.IsValid())
            {
                _State->Editors->CloseAllEditorsForAsset(_State->Clip.Get());
                _State->OwnsEditor = false;
            }
            if (_State->PlayRequested && GUnrealEd != nullptr)
            {
                if (GUnrealEd->IsPlaySessionRequestQueued())
                { GUnrealEd->CancelRequestPlaySession(); }
                if (GetPieWorld() != nullptr)
                { GUnrealEd->RequestEndPlayMap(); }
            }
            _State->Stage = 99;
            _State->TeardownClearFrame = 0;
            _State->DeadlineSeconds = FPlatformTime::Seconds() + 10.0;
        }

        auto RoutePlayDuringPie(const TCHAR* InAssertion) -> bool
        {
            auto* PieWorld = GetPieWorld();
            if (PieWorld == nullptr || NOT PieWorld->HasBegunPlay()
                || GetSolverWorld() != _State->SolverWorld)
            { return false; }
            auto* Editor = _State->Editors.IsValid() && _State->Clip.IsValid()
                ? _State->Editors->FindEditorForAsset(_State->Clip.Get(), false) : nullptr;
            if (Editor == nullptr || Editor->GetAssociatedTabManager() == nullptr
                || NOT _State->EditorTab.IsValid())
            {
                BeginFailureTeardown(TEXT("action toolkit closed while PIE was starting"));
                return false;
            }
            auto& Slate = FSlateApplication::Get();
            if (_State->PlayRouteStage == 0)
            {
                Editor->FocusWindow(_State->Clip.Get());
                if (const auto OwnerTab = Editor->GetAssociatedTabManager()->GetOwnerTab())
                { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                _State->EditorTab->ActivateInParent(ETabActivationCause::SetDirectly);
                const auto Window = Slate.FindWidgetWindow(_State->EditorTab.ToSharedRef());
                if (NOT Window.IsValid())
                { BeginFailureTeardown(TEXT("action toolkit has no Slate window during PIE")); return false; }
                Window->BringToFront(true);
                _State->SolverTimeBeforePie = _State->SolverWorld->GetTimeSeconds();
                _State->PlayRouteFrame = GFrameCounter;
                _State->PlayRouteStage = 1;
                return false;
            }
            if (GFrameCounter <= _State->PlayRouteFrame)
            { return false; }
            if (_State->PlayRouteStage == 1)
            {
                const auto Window = Slate.FindWidgetWindow(_State->EditorTab.ToSharedRef());
                if (NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid())
                { BeginFailureTeardown(TEXT("action toolkit Slate window disappeared during PIE")); return false; }
                // PIE can leave its window above the toolkit in Slate's hit-test order even
                // after the native BringToFront call. Route the same activation a user performs when
                // selecting the toolkit, then let the normal pointer path prove the button is exposed.
                Window->BringToFront(true);
                Slate.ProcessWindowActivatedEvent(FWindowActivateEvent{
                    FWindowActivateEvent::EA_Activate, Window.ToSharedRef()});
                if (NOT ClickButton(_State->EditorTab->GetContent(), TEXT("Play solved preview"), _State->Test))
                { return false; }
                _State->PlayRouteFrame = GFrameCounter;
                _State->PlayRouteStage = 2;
                return false;
            }
            if (NOT _State->Test->TestTrue(InAssertion,
                HasText(_State->EditorTab->GetContent(), TEXT("Pause"))))
            {
                const auto ActiveWindow = Slate.GetActiveTopLevelWindow();
                BeginFailureTeardown(FString::Printf(
                    TEXT("routed Play did not change transport after a Slate frame; PIE begun=%d activeWindow=%s"),
                    PieWorld->HasBegunPlay(),
                    ActiveWindow.IsValid() ? *ActiveWindow->GetTitle().ToString() : TEXT("none")));
                return false;
            }
            _State->PlayRouteStage = 0;
            return true;
        }

        auto Update() -> bool override
        {
            if (_State->Stage == 99)
            {
                if (_State->IsPieTeardownSettled())
                {
                    _State->Cleanup();
                    TraceRemainingWorlds(*_State->Test);
                    CollectGarbage(RF_NoFlags);
                    return true;
                }
                if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
                {
                    if (NOT _State->ForcedTeardown && GUnrealEd != nullptr)
                    {
                        _State->ForcedTeardown = true;
                        _State->Test->AddError(TEXT("requested PIE teardown did not finish; forcing EndPlayMap before allowing another test."));
                        if (GetPieWorld() != nullptr)
                        { GUnrealEd->EndPlayMap(); }
                        _State->DeadlineSeconds = FPlatformTime::Seconds() + 10.0;
                    }
                    else if (_State->ForcedTeardown)
                    {
                        checkf(false, TEXT("PIE teardown remained incomplete after synchronous EndPlayMap; stop this test process before another automation fixture touches the contaminated editor world."));
                    }
                }
                return false;
            }
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                BeginFailureTeardown(FString::Printf(
                    TEXT("Action toolkit PIE coexistence timed out at stage %d; solver=%d PIE=%d; frames=%llu->%llu; solverTime=%.3f->%.3f; transportPause=%d."),
                    _State->Stage, GetSolverWorld() != nullptr, GetPieWorld() != nullptr,
                    static_cast<unsigned long long>(_State->StageStartedFrame),
                    static_cast<unsigned long long>(GFrameCounter),
                    _State->StageStartedSolverTime,
                    GetSolverWorld() != nullptr ? GetSolverWorld()->GetTimeSeconds() : -1.0,
                    _State->EditorTab.IsValid()
                        && HasText(_State->EditorTab->GetContent(), TEXT("Pause"))));
                return false;
            }
            if (_State->Stage == 0)
            {
                auto* Editor = _State->Editors.IsValid() && _State->Clip.IsValid()
                    ? _State->Editors->FindEditorForAsset(_State->Clip.Get(), false) : nullptr;
                if (Editor == nullptr || Editor->GetAssociatedTabManager() == nullptr)
                { return false; }
                _State->EditorTab = Editor->GetAssociatedTabManager()->FindExistingLiveTab(
                    FTabId{EditorTabId});
                if (NOT _State->EditorTab.IsValid())
                { return false; }
                Editor->FocusWindow(_State->Clip.Get());
                if (const auto OwnerTab = Editor->GetAssociatedTabManager()->GetOwnerTab())
                { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                _State->EditorTab->ActivateInParent(ETabActivationCause::SetDirectly);
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 1)
            {
                if (NOT ClickButton(_State->EditorTab->GetContent(), TEXT("Solver (restart)")))
                { return false; }
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 2)
            {
                _State->SolverWorld = GetSolverWorld();
                if (_State->SolverWorld == nullptr)
                { return false; }
                auto Params = FRequestPlaySessionParams{};
                Params.WorldType = EPlaySessionWorldType::PlayInEditor;
                Params.EditorPlaySettings = _State->PlaySettings.Get();
                Params.GameModeOverride = AGameModeBase::StaticClass();
                Params.GlobalMapOverride = TEXT("/Engine/Maps/Entry");
                Params.bAllowOnlineSubsystem = false;
                GUnrealEd->RequestPlaySession(Params);
                _State->PlayRequested = true;
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 3)
            {
                if (GetPieWorld() == nullptr || NOT GetPieWorld()->HasBegunPlay())
                { return false; }
                _State->Test->TestTrue(TEXT("PIE and private Solver world coexist"),
                    GetSolverWorld() == _State->SolverWorld
                    && GetPieWorld() != _State->SolverWorld);
                if (NOT RoutePlayDuringPie(TEXT("routed Play enables Solver transport during PIE")))
                { return false; }
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 4)
            {
                if (GetSolverWorld() != _State->SolverWorld || GetPieWorld() == nullptr)
                { BeginFailureTeardown(TEXT("one world disappeared during PIE coexistence")); return false; }
                if (_State->SolverWorld->GetTimeSeconds() <= _State->SolverTimeBeforePie + 0.01)
                { return false; }
                _State->Test->TestTrue(TEXT("private production world ticks during PIE"),
                    _State->SolverWorld->HasBegunPlay());
                GUnrealEd->RequestEndPlayMap();
                _State->TeardownClearFrame = 0;
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 5)
            {
                if (NOT _State->IsPieTeardownSettled() || GetSolverWorld() != nullptr)
                { return false; }
                _State->PlayRequested = false;
                auto* Editor = _State->Editors->FindEditorForAsset(_State->Clip.Get(), false);
                if (NOT _State->Test->TestTrue(TEXT("PIE stop preserves the authored toolkit"),
                    Editor != nullptr && _State->EditorTab.IsValid())
                    || NOT _State->Test->TestTrue(TEXT("PIE stop displays a truthful Solver restart status"),
                        HasTextContaining(_State->EditorTab->GetContent(), TEXT("Restart Solver"))))
                { BeginFailureTeardown(TEXT("toolkit or restart status missing after PIE stop")); return false; }
                if (NOT ClickButton(_State->EditorTab->GetContent(), TEXT("Solver (restart)")))
                { return false; }
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 6)
            {
                _State->SolverWorld = GetSolverWorld();
                if (_State->SolverWorld == nullptr)
                { return false; }
                auto Params = FRequestPlaySessionParams{};
                Params.WorldType = EPlaySessionWorldType::PlayInEditor;
                Params.EditorPlaySettings = _State->PlaySettings.Get();
                Params.GameModeOverride = AGameModeBase::StaticClass();
                Params.GlobalMapOverride = TEXT("/Engine/Maps/Entry");
                Params.bAllowOnlineSubsystem = false;
                GUnrealEd->RequestPlaySession(Params);
                _State->PlayRequested = true;
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 7)
            {
                if (GetPieWorld() == nullptr || NOT GetPieWorld()->HasBegunPlay())
                { return false; }
                if (NOT RoutePlayDuringPie(TEXT("routed Play resumes Solver during second PIE session")))
                { return false; }
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 8)
            {
                if (GetSolverWorld() != _State->SolverWorld || GetPieWorld() == nullptr)
                { BeginFailureTeardown(TEXT("one world disappeared during second PIE coexistence")); return false; }
                if (_State->SolverWorld->GetTimeSeconds() <= _State->SolverTimeBeforePie + 0.01)
                { return false; }
                _State->Test->TestTrue(TEXT("private world also ticks in the second PIE session"),
                    _State->SolverWorld->HasBegunPlay());
                _State->Editors->CloseAllEditorsForAsset(_State->Clip.Get());
                _State->OwnsEditor = false;
                _State->AdvanceStage();
                return false;
            }
            if (_State->Stage == 9)
            {
                if (GetSolverWorld() != nullptr)
                { return false; }
                _State->Test->TestNotNull(TEXT("closing toolkit during PIE preserves PIE world"),
                    GetPieWorld());
                GUnrealEd->RequestEndPlayMap();
                _State->TeardownClearFrame = 0;
                _State->AdvanceStage();
                return false;
            }
            if (NOT _State->IsPieTeardownSettled())
            { return false; }
            _State->PlayRequested = false;
            _State->Test->TestNull(TEXT("both world contexts have been independently released"),
                GetSolverWorld());
            _State->Cleanup();
            TraceRemainingWorlds(*_State->Test);
            CollectGarbage(RF_NoFlags);
            return true;
        }

    private:
        TSharedRef<FState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PieCoexist,
    "Ck.ProceduralAnimation.Action.Editor.PieCoexist.IndependentWorlds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

auto
    FCkTest_ProceduralAction_PieCoexist::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pie_coexist;
    if (GEditor == nullptr || GUnrealEd == nullptr || GEngine == nullptr
        || NOT FSlateApplication::IsInitialized() || GetPieWorld() != nullptr
        || GetSolverWorld() != nullptr)
    { AddError(TEXT("PIE coexistence requires no pre-existing PIE or action Solver world.")); return false; }
    auto State = MakeShared<FState>();
    State->Test = this;
    State->Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    State->Clip.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, ClipPath));
    if (NOT TestTrue(TEXT("saved BodyPulse clip and editor subsystem are available"),
        State->Clip.IsValid() && State->Editors.IsValid()))
    { return false; }
    if (State->Editors->FindEditorForAsset(State->Clip.Get(), false) != nullptr)
    { AddError(TEXT("PIE coexistence refuses to borrow an already open asset toolkit.")); return false; }
    auto* Settings = GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>();
    State->PreviousPreviewPath = Settings->GetPreviewPath(State->Clip->GetPathName());
    Settings->SetPreviewPath(State->Clip->GetPathName(), PreviewPath);
    State->PreviewOverrideApplied = true;
    State->PlaySettings.Reset(NewObject<ULevelEditorPlaySettings>(GetTransientPackage()));
    if (NOT TestTrue(TEXT("isolated PIE settings exist"), State->PlaySettings.IsValid()))
    { return false; }
    AddInfo(FString::Printf(TEXT("PIE transient settings inherited GameGetsMouseControl=%d"),
        State->PlaySettings->GameGetsMouseControl));
    State->PlaySettings->GameGetsMouseControl = false;
    State->PlaySettings->SetPlayNumberOfClients(1);
    State->PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
    State->PlaySettings->bLaunchSeparateServer = false;
    const auto EntryObjectPath = FSoftObjectPath{EntryMapPath};
    auto* ExistingEntryWorld = Cast<UWorld>(EntryObjectPath.ResolveObject());
    AddInfo(FString::Printf(TEXT("PIE Entry source before fixture load: present=%d initialized=%d"),
        ExistingEntryWorld != nullptr,
        ExistingEntryWorld != nullptr && ExistingEntryWorld->IsInitialized()));
    State->OwnsEntrySourceWorld = ExistingEntryWorld == nullptr;
    State->EntrySourceWorld.Reset(ExistingEntryWorld != nullptr
        ? ExistingEntryWorld : Cast<UWorld>(EntryObjectPath.TryLoad()));
    if (NOT TestTrue(TEXT("Entry source map is available for isolated PIE"),
        State->EntrySourceWorld.IsValid()))
    { return false; }
    AddInfo(FString::Printf(TEXT("PIE Entry source after fixture load: owned=%d type=%d initialized=%d"),
        State->OwnsEntrySourceWorld,
        static_cast<int32>(State->EntrySourceWorld->WorldType),
        State->EntrySourceWorld->IsInitialized()));
    State->OwnsEditor = State->Editors->OpenEditorForAsset(State->Clip.Get(),
        EToolkitMode::Standalone);
    if (NOT TestTrue(TEXT("BodyPulse toolkit opens for PIE coexistence"), State->OwnsEditor))
    { return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 60.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunPieCoexist(State));
    return true;
}
