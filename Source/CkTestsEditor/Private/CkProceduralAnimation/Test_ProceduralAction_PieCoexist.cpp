#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModel.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModes.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_EditorToolkit.h"

#include "CoreGlobals.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/PlatformTime.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"

namespace ck_test_procedural_action_pie_coexist
{
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

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<ULevelEditorPlaySettings> PlaySettings;
        TStrongObjectPtr<UWorld> EntrySourceWorld;
        FCk_ProceduralActionEditor_Subject PreviousSubject;
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

        auto Toolkit() const -> FCk_ProceduralAction_EditorToolkit*
        { return FindToolkit(Editors.Get(), Clip.Get()); }

        auto IsTransportPlaying() const -> bool
        {
            const auto* Editor = Toolkit();
            const auto* Modes = Editor != nullptr ? Editor->Get_Modes() : nullptr;
            return Editor != nullptr && Editor->Get_Model()->Get_IsPlaying() && Modes != nullptr && Modes->Get_Solver() != nullptr;
        }

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
                UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(Clip.Get())->Set_Subject(PreviousSubject);
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

        // Plays the Solver from the toolkit while PIE runs, then waits for the toolkit's ticker to drive it.
        auto PlayDuringPie(const TCHAR* InAssertion) -> bool
        {
            auto* PieWorld = GetPieWorld();
            if (PieWorld == nullptr || NOT PieWorld->HasBegunPlay()
                || GetSolverWorld() != _State->SolverWorld)
            { return false; }
            auto* Editor = _State->Toolkit();
            if (Editor == nullptr || NOT _State->EditorTab.IsValid())
            {
                BeginFailureTeardown(TEXT("action toolkit closed while PIE was starting"));
                return false;
            }
            if (_State->PlayRouteStage == 0)
            {
                _State->SolverTimeBeforePie = _State->SolverWorld->GetTimeSeconds();
                if (NOT Editor->Get_Model()->Get_IsPlaying())
                { Editor->TogglePlay(); }
                _State->PlayRouteFrame = GFrameCounter;
                _State->PlayRouteStage = 1;
                return false;
            }
            constexpr auto TickerFrames = uint64{30};
            const auto Playing = _State->IsTransportPlaying();
            if (NOT Playing && GFrameCounter <= _State->PlayRouteFrame + TickerFrames)
            { return false; }
            if (NOT _State->Test->TestTrue(InAssertion, Playing))
            {
                BeginFailureTeardown(FString::Printf(
                    TEXT("Play did not reach the Solver transport within %llu frames; PIE begun=%d status=%s"),
                    static_cast<unsigned long long>(TickerFrames), PieWorld->HasBegunPlay(),
                    *Editor->Get_StatusText().ToString()));
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
                    TEXT("Action toolkit PIE coexistence timed out at stage %d; solver=%d PIE=%d; frames=%llu->%llu; solverTime=%.3f->%.3f; transportPlaying=%d."),
                    _State->Stage, GetSolverWorld() != nullptr, GetPieWorld() != nullptr,
                    static_cast<unsigned long long>(_State->StageStartedFrame),
                    static_cast<unsigned long long>(GFrameCounter),
                    _State->StageStartedSolverTime,
                    GetSolverWorld() != nullptr ? GetSolverWorld()->GetTimeSeconds() : -1.0,
                    _State->IsTransportPlaying()));
                return false;
            }
            if (_State->Stage == 0)
            {
                auto* Editor = _State->Toolkit();
                if (Editor == nullptr || Editor->GetAssociatedTabManager() == nullptr)
                { return false; }
                _State->EditorTab = Editor->GetAssociatedTabManager()->FindExistingLiveTab(
                    FTabId{FCk_ProceduralAction_EditorToolkit::ViewportTabId});
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
                auto* Editor = _State->Toolkit();
                if (Editor == nullptr)
                { BeginFailureTeardown(TEXT("action toolkit closed before the Solver started")); return false; }
                Editor->Get_Model()->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Solved);
                if (NOT Editor->RestartSolver())
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
                if (NOT PlayDuringPie(TEXT("Play enables Solver transport during PIE")))
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
                auto* Editor = _State->Toolkit();
                if (NOT _State->Test->TestTrue(TEXT("PIE stop preserves the authored toolkit"),
                    Editor != nullptr && _State->EditorTab.IsValid())
                    || NOT _State->Test->TestTrue(TEXT("PIE stop displays a truthful Solver restart status"),
                        Editor->Get_StatusText().ToString().Contains(TEXT("Restart Solver"))))
                { BeginFailureTeardown(TEXT("toolkit or restart status missing after PIE stop")); return false; }
                if (NOT Editor->RestartSolver())
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
                if (NOT PlayDuringPie(TEXT("Play resumes Solver during second PIE session")))
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
    State->PreviousSubject = ck::procedural_action_editor::Get_Subject(*State->Clip);
    auto Subject = FCk_ProceduralActionEditor_Subject{};
    Subject.Set_Scenario(TSoftObjectPtr<UCk_ProceduralActionPreview_Config>{FSoftObjectPath{PreviewPath}});
    UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(State->Clip.Get())->Set_Subject(Subject);
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
