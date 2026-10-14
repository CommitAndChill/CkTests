#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSolverWorld.h"

#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_preview_scenario
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const auto LegId = FName{TEXT("Leg0")};

    auto
        Event(
            int64 InStep,
            ECk_ProceduralActionPreview_ScenarioEventKind InKind,
            TArray<FName> InIds = {})
        -> FCk_ProceduralActionPreview_ScenarioEvent
    {
        auto Result = FCk_ProceduralActionPreview_ScenarioEvent{};
        Result.Set_Step(InStep);
        Result.Set_Kind(InKind);
        Result.Set_LimbIds(MoveTemp(InIds));
        return Result;
    }

    auto
        MakeWalkingConfig()
        -> TStrongObjectPtr<UCk_ProceduralActionPreview_Config>
    {
        auto Config = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
            NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
        Config->Set_SubjectMode(ECk_ProceduralActionPreview_SubjectMode::Walking);
        Config->Set_RootFrame(FTransform{FVector{0.0, 0.0, 60.0}});
        Config->Set_HasStaticFloor(true);
        // The same world-space box supplies planted contacts on a tilted surface.
        Config->Set_StaticFloorFrame(FTransform{FQuat{FVector::RightVector,
            FMath::DegreesToRadians(3.0)}, FVector{0.0, 0.0, -25.0}});
        Config->Set_StaticFloorHalfExtents(FVector{800.0, 800.0, 25.0});
        auto* Rig = NewObject<UCk_ProceduralRig_Data>(Config.Get());
        auto* Gait = NewObject<UCk_ProceduralGait_Data>(Config.Get());
        auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
        const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0}, {-40.0, 30.0}, {-40.0, -30.0}};
        for (auto Index = 0; Index < Corners.Num(); ++Index)
        {
            const auto& Corner = Corners[Index];
            auto Placement = FCk_ProceduralLeg_Placement{
                FVector{Corner.X, Corner.Y, 0.0}, FVector{Corner.X, Corner.Y * 2.0, -60.0}};
            Placement.Set_PhaseOffset(Index == 0 || Index == 3 ? 0.0f : 0.5f);
            Legs.Emplace(FName{*FString::Printf(TEXT("Leg%d"), Index)}, Placement,
                FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 80.0f}});
        }
        Rig->Set_Legs(Legs);
        Config->Set_WalkingRig(Rig);
        Config->Set_WalkingGait(Gait);
        auto Binding = FCk_ProceduralActionPreview_LimbBinding{};
        Binding.Set_LimbId(LegId);
        Binding.Set_SegmentFrames(TArray<FTransform>{
            FTransform{FVector{40.0, 30.0, 60.0}}, FTransform{FVector{40.0, 45.0, 30.0}}});
        Binding.Set_TerminalFrame(FTransform{FVector{40.0, 60.0, 0.0}});
        Config->Set_LimbBindings(TArray<FCk_ProceduralActionPreview_LimbBinding>{Binding});
        auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
        Locomotion.Set_Support(FCk_ProceduralLimbControl_Support{
            ECk_ProceduralLimbControl_SupportMode::CountOnly, 3});
        auto Return = FCk_ProceduralLimbControl_Return{};
        Return.Set_Duration(FCk_Time{0.2});
        Return.Set_Timeout(FCk_Time{1.0});
        Locomotion.Set_Return(Return);
        Config->Set_WalkingLocomotion(Locomotion);
        return Config;
    }

    // Borrows the walking leg and holds it at its rest endpoint for InDuration, then returns it to the gait.
    auto
        MakeHoldClip(
            FCk_Time InDuration)
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
        auto Slot = FCk_ProceduralAction_Slot{LegId};
        Slot.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
        auto Track = FCk_ProceduralAction_LimbTrack{LegId};
        Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector},
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, FVector::ZeroVector}});
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Hold")}, InDuration};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Clip;
    }

    auto
        MakeSlotBindings()
        -> TArray<FCk_ProceduralAction_SlotBinding>
    { return TArray<FCk_ProceduralAction_SlotBinding>{FCk_ProceduralAction_SlotBinding{LegId, LegId}}; }

    struct FState
    {
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Config;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TUniquePtr<FCk_ProceduralActionSolverWorld> Solver;
        double Deadline = 0.0;
        int32 Pass = 0;
        bool Scheduled = false;
        bool ObservedLoss = false;
        bool ObservedReturnBeforeLoss = false;
        bool ObservedTerminalBeforeRestore = false;
        bool ObservedRecovery = false;
        bool CheckedSingleTick = false;
        ECk_ProceduralAction_Outcome FirstOutcome = ECk_ProceduralAction_Outcome::None;
    };

    auto
        TerminalOutcome(
            const FCk_ProceduralActionSolverWorld& InSolver)
        -> ECk_ProceduralAction_Outcome
    {
        for (const auto& Record : InSolver.GetRecording().Get_Records())
        {
            if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal)
            { return Record.Get_Result().Get_Outcome(); }
        }
        return ECk_ProceduralAction_Outcome::None;
    }

    class FRunScenario final : public IAutomationLatentCommand
    {
    public:
        FRunScenario(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        auto
            Update()
            -> bool override
        {
            if (FPlatformTime::Seconds() > _State->Deadline)
            {
                _Test->AddError(FString::Printf(TEXT("preview scenario timed out at pass %d step %lld: %s"),
                    _State->Pass, _State->Solver->GetStep(), *_State->Solver->GetError()));
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            if (NOT _State->CheckedSingleTick)
            {
                const auto FrameBefore = GFrameCounter;
                _Test->TestFalse(TEXT("same editor frame cannot double tick the private world"),
                    _State->Solver->TickOncePerEditorFrame());
                _Test->TestEqual(TEXT("private Solver never mutates global frame counter"),
                    GFrameCounter, FrameBefore);
                _State->CheckedSingleTick = true;
            }
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            if (NOT _State->Scheduled)
            {
                if (_State->Solver->GetStatus() != FCk_ProceduralActionSolverWorld::EStatus::Ready)
                { return false; }
                _Test->TestEqual(TEXT("preparation consumes no preview scenario steps"),
                    _State->Solver->GetStep(), int64{0});
                _Test->TestEqual(TEXT("first seeded environment event remains pending"),
                    _State->Solver->GetScenarioNextEventStep(), int64{10});
                if (_State->Pass > 0 && NOT _State->Solver->Resume())
                { _Test->AddError(TEXT("backward scenario replay did not resume after ready seed")); return true; }
                _State->Scheduled = true;
                return false;
            }
            const auto Step = _State->Solver->GetStep();
            if (Step >= 5 && Step < 10
                && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                    _State->Solver->GetSubject(), LegId).Get_State()
                    == ECk_ProceduralLimbControl_State::Returning)
            { _State->ObservedReturnBeforeLoss = true; }
            if (Step >= 10 && Step < 120 && NOT _State->Solver->GetIsFloorReady())
            { _State->ObservedLoss = true; }
            if (Step < 120 && _State->Solver->GetIsActionTerminal()
                && NOT _State->ObservedTerminalBeforeRestore)
            {
                _State->ObservedTerminalBeforeRestore = true;
                _Test->TestEqual(TEXT("unsupported walking return reports failed recovery"),
                    TerminalOutcome(*_State->Solver), ECk_ProceduralAction_Outcome::RecoveryFailed);
            }
            if (Step >= 130 && Step < 210
                && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                    _State->Solver->GetSubject(), LegId).Get_State()
                    == ECk_ProceduralLimbControl_State::Default)
            { _State->ObservedRecovery = true; }
            if (Step < 210)
            { return false; }
            _Test->TestTrue(TEXT("scenario removed its own floor"), _State->ObservedLoss);
            _Test->TestTrue(TEXT("floor loss interrupts an already returning borrowed leg"),
                _State->ObservedReturnBeforeLoss);
            _Test->TestTrue(TEXT("action terminal precedes preview observation horizon"),
                _State->ObservedTerminalBeforeRestore);
            _Test->TestTrue(TEXT("restored floor became Jolt-ready"), _State->Solver->GetIsFloorReady());
            _Test->TestTrue(TEXT("explicit public recovery returns the named leg"), _State->ObservedRecovery);
            _Test->TestEqual(TEXT("preview ends only at its observation horizon"),
                _State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Finished);
            if (_State->Pass == 0)
            {
                _State->FirstOutcome = TerminalOutcome(*_State->Solver);
                // A replay uses the Start snapshot even if Details changes the source asset.
                _State->Config->Set_ScenarioEvents({});
                _State->Config->Set_ObservationHorizonStep(0);
                _State->Pass = 1;
                _State->Scheduled = false;
                _State->ObservedLoss = false;
                _State->ObservedReturnBeforeLoss = false;
                _State->ObservedTerminalBeforeRestore = false;
                _State->ObservedRecovery = false;
                if (NOT _State->Solver->SeekToStep(0))
                { _Test->AddError(TEXT("backward scenario seek rejected")); return true; }
                _Test->TestEqual(TEXT("backward seek rebuilds the seeded event cursor"),
                    _State->Solver->GetScenarioNextEventStep(), int64{10});
                _Test->TestEqual(TEXT("backward seek keeps the pinned observation horizon"),
                    _State->Solver->GetObservationHorizonStep(), int64{210});
                return false;
            }
            _Test->TestEqual(TEXT("backward replay retains terminal outcome"),
                TerminalOutcome(*_State->Solver), _State->FirstOutcome);
            _State->Solver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };

    class FRunDetach final : public IAutomationLatentCommand
    {
    public:
        FRunDetach(FAutomationTestBase* InTest, TSharedRef<FState> InState)
            : _Test(InTest), _State(InState) {}

        auto
            Update()
            -> bool override
        {
            if (FPlatformTime::Seconds() > _State->Deadline)
            {
                _Test->AddError(TEXT("named preview detach did not complete before its deadline"));
                _State->Solver->Reset();
                return true;
            }
            _State->Solver->TickOncePerEditorFrame();
            if (_State->Solver->GetStatus() == FCk_ProceduralActionSolverWorld::EStatus::Error)
            {
                _Test->AddError(_State->Solver->GetError());
                _State->Solver->Reset();
                return true;
            }
            if (_State->Solver->GetStep() < 20)
            { return false; }
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(
                _State->Solver->GetSubject(), LegId);
            _Test->TestTrue(TEXT("scenario detach resolves the named leg"), ck::IsValid(Leg));
            if (ck::IsValid(Leg))
            {
                _Test->TestEqual(TEXT("public detach request changes the production leg state"),
                    UCk_Utils_ProceduralLeg_UE::Get_Status(Leg), ECk_ProceduralLeg_Status::Detached);
            }
            _Test->TestEqual(TEXT("separate detach scenario stops at its horizon"),
                _State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Finished);
            _State->Solver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewScenario_Replay,
    "Ck.ProceduralAnimation.Action.Editor.PreviewScenario.ReplayFloorLossRecovery",
    ck_test_procedural_action_preview_scenario::Flags)

auto
    FCkTest_ProceduralAction_PreviewScenario_Replay::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_preview_scenario;
    auto State = MakeShared<FState>();
    State->Config = MakeWalkingConfig();
    State->Config->Set_ObservationHorizonStep(210);
    State->Config->Set_ScenarioEvents({
        Event(10, ECk_ProceduralActionPreview_ScenarioEventKind::RemoveFloor),
        Event(120, ECk_ProceduralActionPreview_ScenarioEventKind::RestoreFloor),
        Event(130, ECk_ProceduralActionPreview_ScenarioEventKind::RecoverLimbs, {LegId})});
    auto Error = FString{};
    TestTrue(TEXT("authored preview scenario validates atomically"),
        State->Config->ValidateScenario(Error));
    // The clip plays its five fixed steps from the first Ready step, so the leg is returning when the floor goes.
    State->Clip = MakeHoldClip(FCk_Time{5.0 / 60.0});
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    if (NOT TestTrue(TEXT("private walking scenario starts"),
        State->Solver->StartClip(State->Config.Get(), State->Clip.Get(), MakeSlotBindings(), 1)))
    { AddError(State->Solver->GetError()); return false; }
    TestEqual(TEXT("walking preparation starts before scenario origin"),
        State->Solver->GetStatus(), FCk_ProceduralActionSolverWorld::EStatus::Preparing);
    State->Deadline = FPlatformTime::Seconds() + 35.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunScenario(this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewScenario_Invalid,
    "Ck.ProceduralAnimation.Action.Editor.PreviewScenario.InvalidAndDetach",
    ck_test_procedural_action_preview_scenario::Flags)

auto
    FCkTest_ProceduralAction_PreviewScenario_Invalid::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_preview_scenario;
    auto Config = MakeWalkingConfig();
    auto Error = FString{};
    Config->Set_ObservationHorizonStep(20);
    Config->Set_ScenarioEvents({Event(5, ECk_ProceduralActionPreview_ScenarioEventKind::RemoveFloor),
        Event(5, ECk_ProceduralActionPreview_ScenarioEventKind::RestoreFloor)});
    TestFalse(TEXT("same-step events reject before private world creation"), Config->ValidateScenario(Error));
    TestTrue(TEXT("invalid ordering names the event"), Error.Contains(TEXT("event 1")));
    Config->Set_ScenarioEvents({Event(20, ECk_ProceduralActionPreview_ScenarioEventKind::RemoveFloor)});
    TestFalse(TEXT("event on horizon rejects rather than silently skipping"), Config->ValidateScenario(Error));
    Config->Set_ScenarioEvents({Event(5, ECk_ProceduralActionPreview_ScenarioEventKind::DetachLimb,
        {FName{TEXT("Unknown")}})});
    TestFalse(TEXT("unknown walking limb rejects"), Config->ValidateScenario(Error));
    {
        auto RejectedSolver = FCk_ProceduralActionSolverWorld{};
        const auto RejectedClip = MakeHoldClip(FCk_Time{0.5});
        TestFalse(TEXT("invalid event prevents world creation"),
            RejectedSolver.StartClip(Config.Get(), RejectedClip.Get(), MakeSlotBindings(), 1));
        TestNull(TEXT("invalid event leaves no private world"), RejectedSolver.GetWorld());
        TestTrue(TEXT("invalid event has an actionable diagnostic"),
            RejectedSolver.GetError().Contains(TEXT("Unknown")));
    }
    Config->Set_ScenarioEvents({Event(5, ECk_ProceduralActionPreview_ScenarioEventKind::DetachLimb,
        {LegId}), Event(10, ECk_ProceduralActionPreview_ScenarioEventKind::RecoverLimbs, {LegId})});
    TestFalse(TEXT("a detached limb cannot be recovered by a later scenario event"),
        Config->ValidateScenario(Error));
    TestTrue(TEXT("detached recovery rejection identifies the event and limb"),
        Error.Contains(TEXT("event 1")) && Error.Contains(LegId.ToString()));
    Config->Set_ScenarioEvents({Event(5, ECk_ProceduralActionPreview_ScenarioEventKind::DetachLimb,
        {LegId})});
    TestTrue(TEXT("named walking detach validates"), Config->ValidateScenario(Error));
    auto State = MakeShared<FState>();
    State->Config = MoveTemp(Config);
    State->Clip = MakeHoldClip(FCk_Time{0.5});
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    if (NOT TestTrue(TEXT("detach scenario composes without private mutation hooks"),
        State->Solver->StartClip(State->Config.Get(), State->Clip.Get(), MakeSlotBindings(), 2)))
    { AddError(State->Solver->GetError()); return false; }
    TestEqual(TEXT("detach event is scheduled after admission"),
        State->Solver->GetScenarioNextEventStep(), int64{5});
    State->Deadline = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunDetach(this, State));
    return true;
}

#endif
