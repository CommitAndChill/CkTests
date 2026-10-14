#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringCommands.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSolvedCapture.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSolverWorld.h"

#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

namespace ck_test_procedural_action_solved_capture
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto Revision = uint64{7};
    const FName Reach{TEXT("Reach")};
    const FName Arm{TEXT("Arm")};
    const FName Hand{TEXT("Hand")};
    const FName PhysicalArm{TEXT("PhysicalArm")};
    const FName PhysicalHand{TEXT("PhysicalHand")};

    auto
        RootFrame()
        -> FTransform
    { return FTransform{FRotator{0.0, 90.0, 0.0}, FVector{100.0, 0.0, 0.0}}; }

    auto
        MakeConfig()
        -> TStrongObjectPtr<UCk_ProceduralActionPreview_Config>
    {
        auto Config = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{NewObject<UCk_ProceduralActionPreview_Config>()};
        Config->Set_RootFrame(RootFrame());
        Config->Set_PresentationFrame(RootFrame());
        Config->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
            FCk_ProceduralAction_SlotBinding{Arm, PhysicalArm}, FCk_ProceduralAction_SlotBinding{Hand, PhysicalHand}});
        Config->Set_Layout(FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{
            FCk_ProceduralLimb_Layout{PhysicalArm, FTransform::Identity, TArray<float>{40.0f, 60.0f},
                FTransform{FVector{20.0, 0.0, 0.0}}, FVector::UpVector},
            FCk_ProceduralLimb_Layout{PhysicalHand, FTransform::Identity, TArray<float>{40.0f, 60.0f},
                FTransform{FVector{20.0, 0.0, 0.0}}, FVector::UpVector}}});
        return Config;
    }

    auto
        Track(
            FName InSlot)
        -> FCk_ProceduralAction_LimbTrack
    {
        auto Result = FCk_ProceduralAction_LimbTrack{InSlot};
        Result.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector},
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, FVector::ZeroVector}});
        return Result;
    }

    auto
        MakeClip()
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        Draft.Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{Arm},
            FCk_ProceduralAction_Slot{Hand}.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized)});
        auto Phase = FCk_ProceduralAction_Phase{Reach, FCk_Time{1.0}};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track(Arm), Track(Hand)});
        Draft.Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, Clip.Get());
        return Clip;
    }

    auto
        Limb(
            FName InId,
            const FVector& InWorld)
        -> FCk_ProceduralArticulation_LimbPose
    {
        return FCk_ProceduralArticulation_LimbPose{InId, FTransform{InWorld}, FTransform{InWorld}, TArray<FTransform>{},
            0.0f, ECk_ProceduralRig_ChainState::Clear, ECk_ProceduralArticulation_ContactEvidence::None};
    }

    auto
        MakeSnapshot(
            const TArray<FCk_ProceduralArticulation_LimbPose>& InLimbs)
        -> FCk_ProceduralActionSolvedSnapshot
    {
        auto Snapshot = FCk_ProceduralActionSolvedSnapshot{};
        Snapshot.Set_Ready(true);
        Snapshot.Set_Settled(true);
        Snapshot.Set_PreviewRevision(Revision);
        Snapshot.Set_Pose(FCk_ProceduralArticulation_Pose{RootFrame(), FTransform::Identity, false, InLimbs});
        return Snapshot;
    }

    auto
        WholeSnapshot()
        -> FCk_ProceduralActionSolvedSnapshot
    {
        return MakeSnapshot(TArray<FCk_ProceduralArticulation_LimbPose>{
            Limb(PhysicalArm, FVector{100.0, 30.0, 0.0}), Limb(PhysicalHand, FVector{100.0, 70.0, 0.0})});
    }

    auto
        Target(
            const UCk_ProceduralActionPreview_Config& InConfig,
            const TArray<FName>& InChannels)
        -> FCk_ProceduralActionSolvedCaptureTarget
    { return FCk_ProceduralActionSolvedCaptureTarget{&InConfig, Revision, Reach, FCk_FloatRange_0to1{0.5}, InChannels}; }

    auto
        PositionAt(
            const UCk_ProceduralAction_Data& InClip,
            FName InSlot,
            double InTime,
            FVector& OutValue)
        -> bool
    {
        const auto* Found = InClip.Get_Phases()[0].Get_LimbTracks().FindByPredicate([InSlot](const auto& InTrack)
            { return InTrack.Get_Slot() == InSlot; });
        const auto* Key = Found == nullptr ? nullptr : Found->Get_PositionKeys().FindByPredicate([InTime](const auto& InKey)
            { return InKey.Get_Time().Get_Value() == InTime; });
        if (Key != nullptr)
        { OutValue = Key->Get_Value(); }
        return Key != nullptr;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolvedCapture_RejectsStalePartialAndMalformed,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.SolvedCapture.RejectsStalePartialAndMalformed",
    ck_test_procedural_action_solved_capture::TestFlags)

auto
    FCkTest_ProceduralAction_SolvedCapture_RejectsStalePartialAndMalformed::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solved_capture;
    auto Config = MakeConfig();
    auto Clip = MakeClip();
    if (NOT TestEqual(TEXT("fixture clip has its phase"), Clip->Get_Phases().Num(), 1))
    { return false; }
    const auto PhasesBefore = Clip->Get_Phases();
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    const auto Both = TArray<FName>{Arm, Hand};
    auto Error = FString{};
    const auto ExpectRejected = [&](const TCHAR* InWhat, const FCk_ProceduralActionSolvedSnapshot& InSnapshot,
        const FCk_ProceduralActionSolvedCaptureTarget& InTarget, const TCHAR* InReason)
    {
        Error.Reset();
        TestFalse(InWhat, Commands.CaptureSolvedPose(InSnapshot, InTarget, Error));
        TestTrue(FString::Printf(TEXT("%s: the refusal names %s (got '%s')"), InWhat, InReason, *Error),
            Error.Contains(InReason));
        TestEqual(FString::Printf(TEXT("%s: nothing written"), InWhat), Clip->Get_Phases()[0].Get_LimbTracks()[0]
            .Get_PositionKeys().Num(), PhasesBefore[0].Get_LimbTracks()[0].Get_PositionKeys().Num());
        TestTrue(FString::Printf(TEXT("%s: no label written"), InWhat), ck::procedural_action_editor::Get_PoseLabels(*Clip).IsEmpty());
    };

    const auto Idle = FCk_ProceduralActionSolverWorld{};
    ExpectRejected(TEXT("an idle Solver world"), FCk_ProceduralActionSolvedSnapshot::Make(Idle),
        Target(*Config, Both), TEXT("ready"));

    auto Unsettled = WholeSnapshot();
    Unsettled.Set_Settled(false);
    ExpectRejected(TEXT("an unsettled snapshot"), Unsettled, Target(*Config, Both), TEXT("settled"));

    auto Stale = WholeSnapshot();
    Stale.Set_PreviewRevision(Revision - 1);
    ExpectRejected(TEXT("a recording of an older revision"), Stale, Target(*Config, Both), TEXT("revision"));

    const auto Partial = MakeSnapshot(TArray<FCk_ProceduralArticulation_LimbPose>{Limb(PhysicalArm, FVector{100.0, 30.0, 0.0})});
    ExpectRejected(TEXT("a snapshot missing a requested limb"), Partial, Target(*Config, Both), *Hand.ToString());

    ExpectRejected(TEXT("a body channel the snapshot did not observe"), WholeSnapshot(),
        Target(*Config, TArray<FName>{ck::procedural_action_editor::Get_BodyChannelName(), Arm}),
        *ck::procedural_action_editor::Get_BodyChannelName().ToString());

    const auto Malformed = MakeSnapshot(TArray<FCk_ProceduralArticulation_LimbPose>{
        Limb(PhysicalArm, FVector{std::numeric_limits<double>::quiet_NaN(), 30.0, 0.0}), Limb(PhysicalHand, FVector{100.0, 70.0, 0.0})});
    ExpectRejected(TEXT("a non-finite endpoint"), Malformed, Target(*Config, Both), TEXT("malformed"));

    auto Unbound = MakeConfig();
    Unbound->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{FCk_ProceduralAction_SlotBinding{Arm, PhysicalArm}});
    ExpectRejected(TEXT("a slot without a preview binding"), WholeSnapshot(), Target(*Unbound, Both), *Hand.ToString());
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolvedCapture_ConvertsToDeclaredSpace,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.SolvedCapture.ConvertsToDeclaredSpace",
    ck_test_procedural_action_solved_capture::TestFlags)

auto
    FCkTest_ProceduralAction_SolvedCapture_ConvertsToDeclaredSpace::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solved_capture;
    auto Config = MakeConfig();
    auto Clip = MakeClip();
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("capture a settled, current solved pose"),
        Commands.CaptureSolvedPose(WholeSnapshot(), Target(*Config, TArray<FName>{Arm, Hand}), Error)))
    { AddInfo(Error); return false; }
    auto Value = FVector::ZeroVector;
    TestTrue(TEXT("body-local key is the world endpoint in the turned root frame"),
        PositionAt(*Clip, Arm, 0.5, Value) && Value.Equals(FVector{30.0, 0.0, 0.0}, 1.0e-4));
    AddInfo(FString::Printf(TEXT("Arm key %s"), *Value.ToString()));
    TestTrue(TEXT("rest-normalized key is divided by the chain length in the rest endpoint frame"),
        PositionAt(*Clip, Hand, 0.5, Value) && Value.Equals(FVector{0.5, 0.0, 0.0}, 1.0e-5));
    AddInfo(FString::Printf(TEXT("Hand key %s"), *Value.ToString()));
    const auto Labels = ck::procedural_action_editor::Get_PoseLabels(*Clip);
    TestTrue(TEXT("solved capture labels its time"),
        Labels.Num() == 1 && ck::procedural_action_editor::FindPoseLabel(Labels, Reach, 0.5) != nullptr);
    TestEqual(TEXT("solved capture keys both channels"),
        ck::procedural_action_editor::Get_ChannelsKeyedAt(Clip->Get_Phases()[0], 0.5).Num(), 2);
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_solved_capture
{
    const TCHAR* BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* BodyPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    constexpr auto SeekRevision = uint64{21};

    struct FSeekState
    {
        TStrongObjectPtr<UCk_ProceduralAction_Data> Source;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> Config;
        TUniquePtr<FCk_ProceduralActionSolverWorld> Solver;
        int64 TargetStep = INDEX_NONE;
        FName TargetPhase;
        double TargetTime = 0.0;
        int32 Stage = 0;
        int32 SeekAttempts = 0;
        double DeadlineSeconds = 0.0;
    };

    // The phase and normalized time of a 60 Hz preview step; false when the step lands on an existing body key time.
    auto
        LocateStep(
            const UCk_ProceduralAction_Data& InClip,
            int64 InStep,
            FName& OutPhase,
            double& OutTime)
        -> bool
    {
        const auto Seconds = static_cast<double>(InStep) / 60.0;
        auto Start = 0.0;
        for (const auto& Phase : InClip.Get_Phases())
        {
            const auto Duration = Phase.Get_Duration().Get_Seconds();
            if (Seconds < Start + Duration)
            {
                OutPhase = Phase.Get_Id();
                OutTime = (Seconds - Start) / Duration;
                const auto Time = OutTime;
                return Time > 0.0 && NOT Phase.Get_BodyKeys().ContainsByPredicate([Time](const auto& InKey)
                    { return InKey.Get_Time().Get_Value() == Time; });
            }
            Start += Duration;
        }
        return false;
    }

    auto
        BodyOnly()
        -> TArray<FName>
    { return TArray<FName>{ck::procedural_action_editor::Get_BodyChannelName()}; }

    class FRunSatisfiedSeek final : public IAutomationLatentCommand
    {
    public:
        FRunSatisfiedSeek(FAutomationTestBase* InTest, TSharedRef<FSeekState> InState)
            : _Test(InTest), _State(InState) {}

        virtual bool Update() override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            {
                _Test->AddError(FString::Printf(TEXT("satisfied-seek capture timed out at stage %d (step %lld, status %d): %s"),
                    State.Stage, State.Solver->Get_Step(), static_cast<int32>(State.Solver->Get_Status()), *State.Solver->Get_Error()));
                State.Solver->Reset();
                return true;
            }
            const auto Status = State.Solver->Get_Status();
            if (State.Stage == 0)
            {
                if (Status == FCk_ProceduralActionSolverWorld::EStatus::Preparing)
                {
                    State.Solver->TickOncePerEditorFrame();
                    return false;
                }
                if (NOT _Test->TestTrue(TEXT("the BodyPulse preview prepares"), Status == FCk_ProceduralActionSolverWorld::EStatus::Ready))
                { State.Solver->Reset(); return true; }
                for (auto Step = int64{8}; Step < 30; ++Step)
                {
                    if (LocateStep(*State.Clip, Step, State.TargetPhase, State.TargetTime))
                    { State.TargetStep = Step; break; }
                }
                if (NOT _Test->TestTrue(TEXT("an interior, unkeyed step exists"), State.TargetStep != INDEX_NONE)
                    || NOT _Test->TestTrue(TEXT("seek to an interior step"), State.Solver->SeekToStep(State.TargetStep)))
                { State.Solver->Reset(); return true; }
                State.Stage = 1;
                return false;
            }
            if (State.Stage == 1)
            {
                if (State.Solver->Get_Step() < State.TargetStep
                    || Status == FCk_ProceduralActionSolverWorld::EStatus::Seeking
                    || Status == FCk_ProceduralActionSolverWorld::EStatus::Preparing)
                {
                    State.Solver->TickOncePerEditorFrame();
                    return false;
                }
                _Test->TestEqual(TEXT("the seek reached its target step"), State.Solver->Get_Step(), State.TargetStep);
                _Test->TestEqual(TEXT("the reached seek target is still recorded"), State.Solver->Get_SeekTargetStep(), State.TargetStep);
                const auto Snapshot = FCk_ProceduralActionSolvedSnapshot::Make(*State.Solver);
                _Test->TestTrue(TEXT("a satisfied seek is ready"), Snapshot.Get_Ready());
                ++State.SeekAttempts;
                if (NOT Snapshot.Get_Settled() && State.SeekAttempts < 8)
                {
                    // Not every step publishes an accepted pose; the author steps on to one that did.
                    _Test->AddInfo(FString::Printf(TEXT("step %lld has no settled accepted pose yet; stepping on"), State.TargetStep));
                    auto Next = State.TargetStep + 1;
                    while (Next < State.TargetStep + 8 && NOT LocateStep(*State.Clip, Next, State.TargetPhase, State.TargetTime))
                    { ++Next; }
                    State.TargetStep = Next;
                    if (NOT _Test->TestTrue(TEXT("step the seek forward"), State.Solver->SeekToStep(State.TargetStep)))
                    { State.Solver->Reset(); return true; }
                    return false;
                }
                _Test->AddInfo(FString::Printf(TEXT("satisfied seek captured at step %lld after %d seek(s)"), State.TargetStep, State.SeekAttempts));
                _Test->TestTrue(TEXT("the last accepted pose at the satisfied seek is settled"), Snapshot.Get_Settled());
                _Test->TestEqual(TEXT("the snapshot carries the Solver revision"), Snapshot.Get_PreviewRevision(), SeekRevision);
                auto Commands = FCk_ProceduralActionAuthoringCommands{State.Clip.Get()};
                auto Error = FString{};
                const auto Stale = FCk_ProceduralActionSolvedCaptureTarget{State.Config.Get(), SeekRevision + 1,
                    State.TargetPhase, FCk_FloatRange_0to1{State.TargetTime}, BodyOnly()};
                _Test->TestFalse(TEXT("a satisfied seek of an older revision is refused"),
                    Commands.CaptureSolvedPose(Snapshot, Stale, Error));
                _Test->TestTrue(TEXT("the stale refusal names the revision"), Error.Contains(TEXT("revision")));
                const auto Current = FCk_ProceduralActionSolvedCaptureTarget{State.Config.Get(), SeekRevision,
                    State.TargetPhase, FCk_FloatRange_0to1{State.TargetTime}, BodyOnly()};
                if (NOT _Test->TestTrue(TEXT("capturing at a satisfied seek is accepted"),
                    Commands.CaptureSolvedPose(Snapshot, Current, Error)))
                { _Test->AddInfo(Error); }
                _Test->TestNotNull(TEXT("the satisfied-seek capture labels its time"), ck::procedural_action_editor::FindPoseLabel(
                    ck::procedural_action_editor::Get_PoseLabels(*State.Clip), State.TargetPhase, State.TargetTime));
                if (NOT _Test->TestTrue(TEXT("play on to terminal"), State.Solver->Resume()))
                { State.Solver->Reset(); return true; }
                State.Stage = 2;
                return false;
            }
            if (Status != FCk_ProceduralActionSolverWorld::EStatus::Finished)
            {
                State.Solver->TickOncePerEditorFrame();
                return false;
            }
            const auto Finished = FCk_ProceduralActionSolvedSnapshot::Make(*State.Solver);
            _Test->TestFalse(TEXT("a finished preview is not ready"), Finished.Get_Ready());
            auto Commands = FCk_ProceduralActionAuthoringCommands{State.Clip.Get()};
            auto Error = FString{};
            const auto LabelsBefore = ck::procedural_action_editor::Get_PoseLabels(*State.Clip).Num();
            _Test->TestFalse(TEXT("capturing a finished preview is refused"), Commands.CaptureSolvedPose(Finished,
                FCk_ProceduralActionSolvedCaptureTarget{State.Config.Get(), SeekRevision, State.TargetPhase,
                    FCk_FloatRange_0to1{State.TargetTime * 0.5}, BodyOnly()}, Error));
            _Test->TestTrue(TEXT("the finished refusal says the preview is not ready"), Error.Contains(TEXT("ready")));
            _Test->TestEqual(TEXT("the refused capture wrote nothing"), ck::procedural_action_editor::Get_PoseLabels(*State.Clip).Num(),
                LabelsBefore);
            State.Solver->Reset();
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FSeekState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_SolvedCapture_AcceptsSatisfiedSeek,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringCommands.SolvedCapture.AcceptsSatisfiedSeek",
    ck_test_procedural_action_solved_capture::TestFlags)

auto
    FCkTest_ProceduralAction_SolvedCapture_AcceptsSatisfiedSeek::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_solved_capture;
    auto State = MakeShared<FSeekState>();
    State->Source.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath));
    State->Config.Reset(LoadObject<UCk_ProceduralActionPreview_Config>(nullptr, BodyPreviewPath));
    if (NOT TestTrue(TEXT("the BodyPulse example and its preview load"), State->Source.IsValid() && State->Config.IsValid()))
    { return false; }
    State->Clip.Reset(DuplicateObject<UCk_ProceduralAction_Data>(State->Source.Get(), GetTransientPackage()));
    State->Solver = MakeUnique<FCk_ProceduralActionSolverWorld>();
    if (NOT TestTrue(TEXT("the production Solver starts the BodyPulse clip"),
        State->Solver->StartClip(State->Config.Get(), State->Source.Get(), State->Config->Get_SlotBindings(), SeekRevision)))
    { AddError(State->Solver->Get_Error()); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunSatisfiedSeek(this, State));
    return true;
}

#endif
