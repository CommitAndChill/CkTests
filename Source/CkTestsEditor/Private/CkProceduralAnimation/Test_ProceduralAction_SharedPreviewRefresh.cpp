#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModes.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_EditorToolkit.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformTime.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_test_procedural_action_shared_preview_refresh
{
    const FName StatusTag{TEXT("CkActionEditor.Status")};
    const FName TransportTag{TEXT("CkActionEditor.Transport")};
    const TCHAR* BodyPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* LimbPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    const TCHAR* PreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");

    auto
        FindTagged(
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
            if (const auto Found = FindTagged(Children->GetChildAt(Index), InTag, InDepth + 1))
            { return Found; }
        }
        return {};
    }

    auto
        FindToolkit(
            UAssetEditorSubsystem* InEditors,
            UCk_ProceduralAction_Data* InClip)
        -> FCk_ProceduralAction_EditorToolkit*
    {
        auto* Instance = InEditors != nullptr && InClip != nullptr ? InEditors->FindEditorForAsset(InClip, false) : nullptr;
        if (Instance == nullptr || Instance->GetEditorName() != FCk_ProceduralAction_EditorToolkit::ToolkitName)
        { return nullptr; }
        return static_cast<FCk_ProceduralAction_EditorToolkit*>(Instance);
    }

    auto
        FindTab(
            FCk_ProceduralAction_EditorToolkit& InToolkit,
            FName InTabId)
        -> TSharedPtr<SDockTab>
    {
        const auto TabManager = InToolkit.GetAssociatedTabManager();
        return TabManager.IsValid() ? TabManager->FindExistingLiveTab(FTabId{InTabId}) : TSharedPtr<SDockTab>{};
    }

    auto
        StatusOf(
            const FCk_ProceduralAction_EditorToolkit* InToolkit)
        -> FString
    { return InToolkit != nullptr ? InToolkit->Get_StatusText().ToString() : FString{}; }

    // The viewport HUD line, which shows the same status the toolkit reports.
    auto
        HudStatusOf(
            FCk_ProceduralAction_EditorToolkit& InToolkit)
        -> FString
    {
        const auto Tab = FindTab(InToolkit, FCk_ProceduralAction_EditorToolkit::ViewportTabId);
        const auto Hud = Tab.IsValid() ? FindTagged(Tab->GetContent(), StatusTag) : TSharedPtr<SWidget>{};
        return Hud.IsValid() && Hud->GetType() == FName{TEXT("STextBlock")}
            ? StaticCastSharedPtr<STextBlock>(Hud)->GetText().ToString() : FString{};
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
        FCk_ProceduralActionEditor_Subject OriginalBodySubject;
        FCk_ProceduralActionEditor_Subject OriginalLimbSubject;
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
                if (Body.IsValid())
                { Editors->CloseAllEditorsForAsset(Body.Get()); }
                if (Limb.IsValid())
                { Editors->CloseAllEditorsForAsset(Limb.Get()); }
                OwnsEditors = false;
            }
            if (SettingsChanged && Body.IsValid() && Limb.IsValid())
            {
                UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(Body.Get())->Set_Subject(OriginalBodySubject);
                UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(Limb.Get())->Set_Subject(OriginalLimbSubject);
                SettingsChanged = false;
            }
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
            auto* Body = FindToolkit(_State->Editors.Get(), _State->Body.Get());
            auto* Limb = FindToolkit(_State->Editors.Get(), _State->Limb.Get());
            if (_State->Stage == 0)
            {
                if (Body == nullptr || Limb == nullptr
                    || NOT FindTab(*Body, FCk_ProceduralAction_EditorToolkit::ViewportTabId).IsValid()
                    || NOT FindTab(*Limb, FCk_ProceduralAction_EditorToolkit::ViewportTabId).IsValid())
                { return false; }
                _State->Test->TestFalse(TEXT("shared preview begins with valid obstacle settings"),
                    StatusOf(Body).Contains(TEXT("positive half extents"))
                    || StatusOf(Limb).Contains(TEXT("positive half extents")));
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
            if (Body == nullptr || Limb == nullptr)
            {
                _State->Test->AddError(TEXT("a toolkit closed during the shared preview refresh"));
                _State->Cleanup();
                return true;
            }
            if (_State->Stage == 1)
            {
                if (NOT StatusOf(Body).Contains(TEXT("positive half extents"))
                    || NOT StatusOf(Limb).Contains(TEXT("positive half extents")))
                { return false; }
                _State->Test->TestTrue(TEXT("the viewport HUD shows the invalid preview status"),
                    HudStatusOf(*Body).Contains(TEXT("positive half extents")));
                _State->Preview->Set_StaticObstacles(_State->OriginalObstacles);
                _State->NotifyChanged();
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 2)
            {
                if (StatusOf(Body).Contains(TEXT("positive half extents")))
                { return false; }
                Body->FocusWindow(_State->Body.Get());
                if (const auto OwnerTab = Body->GetAssociatedTabManager()->GetOwnerTab())
                { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                if (const auto ViewportTab = FindTab(*Body, FCk_ProceduralAction_EditorToolkit::ViewportTabId))
                { ViewportTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                _State->BodyActivationFrame = GFrameCounter;
                _State->Stage = 21;
                return false;
            }
            if (_State->Stage == 21)
            {
                if (GFrameCounter <= _State->BodyActivationFrame)
                { return false; }
                const auto TimelineTab = FindTab(*Body, FCk_ProceduralAction_EditorToolkit::TimelineTabId);
                if (NOT _State->Test->TestTrue(TEXT("Solver transport exists"),
                        TimelineTab.IsValid() && FindTagged(TimelineTab->GetContent(), TransportTag).IsValid())
                    || NOT _State->Test->TestTrue(TEXT("RestartSolver starts the Body Solver"), Body->RestartSolver()))
                { _State->Cleanup(); return true; }
                _State->Stage = 3;
                return false;
            }
            if (_State->Stage == 3)
            {
                if (NOT StatusOf(Body).Contains(TEXT("SOLVER:")))
                { return false; }
                // Each open tab in Solved runs its own Solver world; take the Body tab's.
                const auto* BodyModes = Body->Get_Modes();
                _State->SolverWorld = BodyModes != nullptr && BodyModes->Get_Solver() != nullptr
                    ? BodyModes->Get_Solver()->Get_World() : nullptr;
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
            // A clip edit restarts the Solver on the new revision: the old private world goes and a new one solves.
            auto WorldContextStillPresent = false;
            for (const auto& Context : GEngine->GetWorldContexts())
            { WorldContextStillPresent |= Context.World() == _State->SolverWorld; }
            const auto* Modes = Body->Get_Modes();
            if (WorldContextStillPresent || Modes == nullptr || Modes->Get_Solver() == nullptr
                || NOT StatusOf(Body).Contains(TEXT("SOLVER:")))
            { return false; }
            _State->Test->TestTrue(TEXT("the viewport HUD shows the restarted Solver"),
                HudStatusOf(*Body).Contains(TEXT("SOLVER:")));
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
    State->OriginalBodySubject = ck::procedural_action_editor::Get_Subject(*State->Body);
    State->OriginalLimbSubject = ck::procedural_action_editor::Get_Subject(*State->Limb);
    auto Shared = FCk_ProceduralActionEditor_Subject{};
    Shared.Set_Scenario(TSoftObjectPtr<UCk_ProceduralActionPreview_Config>{State->Preview.Get()});
    UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(State->Body.Get())->Set_Subject(Shared);
    UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(State->Limb.Get())->Set_Subject(Shared);
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
