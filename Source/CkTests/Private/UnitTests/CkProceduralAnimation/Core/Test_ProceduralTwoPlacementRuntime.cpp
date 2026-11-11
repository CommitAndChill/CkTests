#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceContactStaging.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Public solver contracts only. Resolver replies are test inputs, not native-world admission evidence.
namespace ck_test_two_placement_runtime
{
    using FResolver = ck::FProceduralGaitNativeRecoveryResolver;
    using FTouchdown = ck::FProceduralGaitNativeTouchdown;

    struct FHarness
    {
        ck::FProceduralGaitSolver Solver;
        TArray<ck::FProceduralGaitLegInput> Inputs;
        TArray<ck::FProceduralGaitLegOutput> Outputs;

        auto Init(float InPhase = 0.0f) -> bool
        {
            auto Settings = ck::FProceduralGaitSettings{};
            Settings.Get_Cadence().Set_CycleDuration(FCk_Time{1.0}).Set_SwingWindow(0.25f)
                .Set_MaxSimultaneousSwings(1);
            Settings.Get_Step().Set_Threshold(25.0f).Set_Duration(FCk_Time{0.1});
            Settings.Get_Settle().Set_AtRest(false);
            Solver.Set_Settings(Settings);
            for (auto Index = 0; Index < 2; ++Index)
            {
                const auto Plant = FVector{0.0, Index * 80.0, 0.0};
                Inputs.Add(ck::FProceduralGaitLegInput{}.Set_IdealTarget(Plant)
                    .Set_Hip(Plant + FVector{0.0, 0.0, 60.0}).Set_Reach(100.0f)
                    .Set_FootContactRadius(2.0f).Set_NativeRecoveryEnabled(true)
                    .Set_PhaseOffset(Index == 0 ? InPhase : 0.5f));
            }
            Inputs[0].Set_IdealTarget(FVector{10.0, 0.0, 0.0});
            Outputs.SetNum(2);
            const auto Initial = TArray<FVector>{FVector::ZeroVector, FVector{0.0, 80.0, 0.0}};
            return Solver.Reset(Initial);
        }

        auto Intent() const -> ck::FProceduralGaitPlannedReplantIntent
        {
            return ck::FProceduralGaitPlannedReplantIntent{}.Set_Episode(1).Set_Frame(1)
                .Set_ConsumerInputSolve(1).Set_ExpectedPlant(FVector::ZeroVector)
                .Set_Target(Inputs[0].Get_IdealTarget()).Set_ExitHip(FVector{105.0, 0.0, 60.0})
                .Set_Milestone(ck::EProceduralGaitReplantMilestone::Intermediate).Set_PlacementSlot(0)
                .Set_FinalQuery(FVector{40.0, 0.0, 0.0}).Set_Forward(FVector::ForwardVector)
                .Set_SearchRadius(30.0f);
        }

        auto Tick(TOptional<FResolver> InResolver = {}, FCk_Time InDt = FCk_Time{0.01}, bool InAirborne = false) -> bool
        {
            return Solver.Step(InDt, 10.0f, FVector{10.0, 0.0, 0.0}, Inputs, Outputs,
                InAirborne, {}, InResolver);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTwoPlacementIntermediateAuthorityTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.IntermediateAuthorityAndActualStart", ck::tests::kCkUnitTestFlags)

auto
    FCkTwoPlacementIntermediateAuthorityTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_two_placement_runtime;
    const auto Resolve = [](const ck::FProceduralGaitSolver&, int32, const FVector& InPoint,
        const FVector&, const FVector& InNormal) -> TOptional<FTouchdown>
    { return FTouchdown{InPoint, InNormal, true}; };
    for (auto Mode = 0; Mode < 4; ++Mode)
    {
        auto H = FHarness{};
        if (NOT TestTrue(TEXT("Public fixture resets"), H.Init(Mode == 3 ? 0.5f : 0.0f)))
        { return false; }
        auto Intent = H.Intent();
        TestTrue(TEXT("Actual intermediate deliberately fails locked Exit reach"),
            FVector::Dist(Intent.Get_Target(), Intent.Get_ExitHip()) > 100.001);
        TestTrue(TEXT("Final query fits locked Exit physical reach"),
            FVector::Dist(Intent.Get_FinalQuery(), Intent.Get_ExitHip()) <= 100.001);
        if (Mode == 1)
        { Intent.Set_Milestone(ck::EProceduralGaitReplantMilestone::Final); }
        H.Inputs[0].Set_PlannedReplantIntent(Intent);
        TestTrue(TEXT("Intent preserves ordinary accepted inputs"),
            H.Tick(Mode == 2 ? TOptional<FResolver>{} : TOptional<FResolver>{Resolve}));
        const auto& State = H.Solver.GetLegState(0);
        TestEqual(TEXT("Only typed intermediate with native capability inside ordinary window starts"),
            static_cast<int32>(State.Get_StartsThisStep()), Mode == 0 ? 1 : 0);
        if (Mode != 0)
        { continue; }
        TestTrue(TEXT("Intermediate is an exact ordinary scheduled replant"),
            State.Get_Swing().Get_Active() && State.Get_Swing().Get_PlannedReplant()
            && NOT State.Get_Swing().Get_CatchStep() && NOT State.Get_Swing().Get_BeyondSchedule()
            && State.Get_LastStartedTarget() == Intent.Get_Target());
        for (auto Renewal = 0; Renewal < 3; ++Renewal)
        {
            H.Inputs[0].Set_PlannedReplantIntent(Intent);
            TestTrue(TEXT("Renewed intent during flight remains accepted"), H.Tick(TOptional<FResolver>{Resolve}));
            TestEqual(TEXT("Renewal does not produce another actual start"),
                static_cast<int32>(H.Solver.GetLegState(0).Get_StartsThisStep()), 0);
        }
        TestTrue(TEXT("Intermediate completion resolves through the production native resolver boundary"),
            H.Tick(TOptional<FResolver>{Resolve}, FCk_Time{0.08}));
        TestTrue(TEXT("Only actual touchdown changes the intermediate plant"),
            H.Outputs[0].Get_NativeTouchdownResolved() && H.Outputs[0].Get_Planted()
            && H.Solver.GetLegState(0).Get_Plant().Get_Trusted()
            && H.Solver.GetLegState(0).Get_Plant().Get_Position() == Intent.Get_Target()
            && H.Solver.GetLegState(0).Get_StartsThisStep() == 0);
        H.Inputs[0].Set_IdealTarget(Intent.Get_FinalQuery());
        auto Final = Intent;
        Final.Set_Milestone(ck::EProceduralGaitReplantMilestone::Final).Set_PlacementSlot(1)
            .Set_ExpectedPlant(H.Solver.GetLegState(0).Get_Plant().Get_Position())
            .Set_Target(Intent.Get_FinalQuery());
        H.Inputs[0].Set_PlannedReplantIntent(Final);
        TestTrue(TEXT("Final intent starts from the actual intermediate plant in the same normal window"),
            H.Tick(TOptional<FResolver>{Resolve}));
        TestTrue(TEXT("Second actual departure records the final query exactly once"),
            H.Solver.GetLegState(0).Get_StartsThisStep() == 1
            && H.Solver.GetLegState(0).Get_Swing().Get_PlannedReplant()
            && H.Solver.GetLegState(0).Get_LastStartedTarget() == Intent.Get_FinalQuery());
    }
    auto Denied = FHarness{};
    if (NOT TestTrue(TEXT("Guard fixture resets"), Denied.Init()))
    { return false; }
    Denied.Inputs[0].Set_PlannedReplantIntent(Denied.Intent());
    auto Calls = 0;
    auto Rejections = TArray<ck::FProceduralGaitDeniedTakeoff>{};
    TestTrue(TEXT("Takeoff denial still accepts the public solve"), Denied.Solver.StepWithTakeoffGuard(
        FCk_Time{0.01}, 10.0f, FVector{10.0, 0.0, 0.0}, Denied.Inputs, Denied.Outputs, false, {},
        [&](const ck::FProceduralGaitSolver&, int32, bool InEmergency)
        { ++Calls; TestFalse(TEXT("Intermediate gains no Emergency authority"), InEmergency); return false; },
        Rejections, TOptional<FResolver>{Resolve}));
    TestTrue(TEXT("Denied start consumes no actual departure and retains its real plant"),
        Calls == 1 && Rejections.Num() == 1 && Denied.Solver.GetLegState(0).Get_StartsThisStep() == 0
        && NOT Denied.Solver.GetLegState(0).Get_Swing().Get_Active()
        && Denied.Solver.GetLegState(0).Get_Plant().Get_Position() == FVector::ZeroVector);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTwoPlacementInvalidAuthorityTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.IntermediateInvalidIntentPreservesOrdinary", ck::tests::kCkUnitTestFlags)

auto
    FCkTwoPlacementInvalidAuthorityTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_two_placement_runtime;
    const auto Resolve = [](const ck::FProceduralGaitSolver&, int32, const FVector& InPoint,
        const FVector&, const FVector& InNormal) -> TOptional<FTouchdown>
    { return FTouchdown{InPoint, InNormal, true}; };
    for (auto Case = 0; Case < 13; ++Case)
    {
        auto H = FHarness{};
        if (NOT TestTrue(TEXT("Malformed-intent fixture resets"), H.Init()))
        { return false; }
        auto Intent = H.Intent();
        switch (Case)
        {
            case 0: Intent.Set_PlacementSlot(1); break;
            case 1: Intent.Set_Forward(-FVector::ForwardVector); break;
            case 2: Intent.Set_SearchRadius(29.0f); break;
            case 3: Intent.Set_FinalQuery(FVector{40.0, 1.0, 0.0}); break;
            case 4: Intent.Set_FinalQuery(FVector{40.0, 0.0, 1.0}); break;
            case 5: Intent.Set_Target(FVector{11.0, 0.0, 0.0}); break;
            case 6: Intent.Set_ExpectedPlant(FVector{1.0, 0.0, 0.0}); break;
            case 7: Intent.Set_SearchRadius(std::numeric_limits<float>::quiet_NaN()); break;
            case 8: Intent.Set_Forward(FVector::ZeroVector); break;
            case 9: Intent.Set_Milestone(static_cast<ck::EProceduralGaitReplantMilestone>(255)); break;
            case 10: Intent.Set_FinalQuery(FVector{std::numeric_limits<double>::infinity(), 0.0, 0.0}); break;
            case 11: Intent.Set_FinalQuery(FVector{5.0, 0.0, 0.0}); break;
            case 12: Intent.Set_Episode(0); break;
            default: break;
        }
        H.Inputs[0].Set_PlannedReplantIntent(Intent);
        TestTrue(TEXT("Rejected intermediate does not reject the ordinary solve"), H.Tick(TOptional<FResolver>{Resolve}));
        TestTrue(TEXT("Malformed intermediate grants neither a small step nor plant mutation"),
            H.Solver.GetLegState(0).Get_StartsThisStep() == 0
            && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active()
            && H.Solver.GetLegState(0).Get_Plant().Get_Position() == FVector::ZeroVector);
        H.Inputs[0].Set_IdealTarget(FVector{30.0, 0.0, 0.0});
        TestTrue(TEXT("Invalid stale intent leaves an ordinary above-threshold target available"), H.Tick());
        TestTrue(TEXT("Ordinary start remains independent of invalid staging metadata"),
            H.Solver.GetLegState(0).Get_StartsThisStep() == 1
            && H.Solver.GetLegState(0).Get_Swing().Get_Active()
            && NOT H.Solver.GetLegState(0).Get_Swing().Get_PlannedReplant());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTwoPlacementAllActualStartsTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.AllActualStartsObservable", ck::tests::kCkUnitTestFlags)

auto
    FCkTwoPlacementAllActualStartsTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_two_placement_runtime;
    for (auto Mode = 0; Mode < 3; ++Mode)
    {
        auto H = FHarness{};
        if (NOT TestTrue(TEXT("Actual-start fixture resets"), H.Init(Mode == 1 ? 0.5f : 0.0f)))
        { return false; }
        const auto Target = FVector{30.0, 0.0, 0.0};
        if (Mode == 1)
        { TestTrue(TEXT("Explicit catch request is accepted while the ordinary window is closed"), H.Solver.RequestStep(0, Target)); }
        if (Mode == 2)
        {
            H.Inputs[0].Set_Enabled(false);
            TestTrue(TEXT("Administrative disable reconciles through ordinary Step"), H.Tick());
            TestEqual(TEXT("Disable is not a takeoff"), static_cast<int32>(H.Solver.GetLegState(0).Get_StartsThisStep()), 0);
            H.Inputs[0].Set_Enabled(true);
        }
        H.Inputs[0].Set_IdealTarget(Target);
        TestTrue(TEXT("Ordinary, catch and re-enable starts use the production transition"), H.Tick());
        const auto& State = H.Solver.GetLegState(0);
        TestTrue(TEXT("Actual transition records one start and its exact target"),
            State.Get_StartsThisStep() == 1 && State.Get_LastStartedTarget() == Target
            && State.Get_Swing().Get_Active() && NOT State.Get_Swing().Get_PlannedReplant());
        TestEqual(TEXT("Only the explicit request is a catch step"), State.Get_Swing().Get_CatchStep(), Mode == 1);
        TestTrue(TEXT("Next accepted positive solve resets the start count"), H.Tick());
        TestEqual(TEXT("Continuing a swing is not another actual departure"),
            static_cast<int32>(H.Solver.GetLegState(0).Get_StartsThisStep()), 0);
    }
    auto Recovery = FHarness{};
    if (NOT TestTrue(TEXT("Same-solve native recovery fixture resets"), Recovery.Init()))
    { return false; }
    TestTrue(TEXT("Real solver flight transition creates waiting-for-contact state"),
        Recovery.Tick({}, FCk_Time{0.01}, true));
    auto NativeCalls = 0;
    const auto Resolve = [&](const ck::FProceduralGaitSolver& InSolver, int32 InLeg,
        const FVector& InPoint, const FVector&, const FVector& InNormal) -> TOptional<FTouchdown>
    {
        ++NativeCalls;
        TestEqual(TEXT("Resolver observes the actual start before same-solve native completion"),
            static_cast<int32>(InSolver.GetLegState(InLeg).Get_StartsThisStep()), 1);
        return FTouchdown{InPoint, InNormal, true};
    };
    TestTrue(TEXT("Positive public reground solve can start and resolve within one call"),
        Recovery.Tick(TOptional<FResolver>{Resolve}, FCk_Time{0.2}));
    TestTrue(TEXT("Finished swing retains actual-start evidence despite planted final output"),
        NativeCalls == 2 && Recovery.Outputs[0].Get_NativeTouchdownResolved()
        && Recovery.Outputs[0].Get_Planted() && NOT Recovery.Solver.GetLegState(0).Get_Swing().Get_Active()
        && Recovery.Solver.GetLegState(0).Get_StartsThisStep() == 1
        && Recovery.Solver.GetLegState(0).Get_LastStartedTarget() == Recovery.Inputs[0].Get_IdealTarget());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTwoPlacementFenceArithmeticTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.FenceArithmeticAndRecordedExit", ck::tests::kCkUnitTestFlags)

auto
    FCkTwoPlacementFenceArithmeticTest::RunTest(const FString&)
    -> bool
{
    // These are private-record arithmetic contracts. Owner/world/stance joins and feature teardown require runtime coverage.
    const auto MakeFence = [](uint8 InRequired = 1)
    {
        auto Fence = ck::FFragment_ProceduralContactStagingFence{};
        Fence._Spent = true;
        Fence._RequiredCount = InRequired;
        Fence._CycleLimit = 2.0 * InRequired + 1.0;
        Fence._ElapsedLimit = Fence._CycleLimit;
        Fence._LastSurfaceFrame = 10;
        Fence._LastAcknowledgedFrame = 9;
        Fence._LastAcknowledgedSolve = 20;
        Fence._Forward = FVector::ForwardVector;
        Fence._Up = FVector::UpVector;
        Fence._Exit = FTransform{FQuat::Identity, FVector{100.0, 0.0, 40.0}};
        Fence._Legs[0]._StartedCount = 1;
        Fence._Legs[0]._Placements[0]._Started = true;
        Fence._Legs[0]._Placements[0]._Completed = true;
        Fence._Legs[0]._Placements[0]._Target = FVector{20.0, 0.0, 0.0};
        return Fence;
    };
    const auto HasUnchangedLedger = [](const ck::FFragment_ProceduralContactStagingFence& InFence)
    {
        return InFence._Spent && InFence._Legs[0]._StartedCount == 1
            && InFence._Legs[0]._Placements[0]._Started && InFence._Legs[0]._Placements[0]._Completed
            && InFence._Legs[0]._Placements[0]._Target == FVector{20.0, 0.0, 0.0};
    };
    for (uint8 Required = 1; Required <= 2; ++Required)
    {
        auto Time = MakeFence(Required);
        Time._LastAcknowledgedFrame = 10;
        TestTrue(TEXT("Positive elapsed time below the derived one/two-leg cap is accepted"),
            Time.TryAdvanceTime(11, Time._ElapsedLimit - 0.25));
        TestTrue(TEXT("The charged invocation is acknowledged before the next elapsed charge"),
            Time.TryAcknowledge(11, 20, 21, 0.1));
        TestFalse(TEXT("Elapsed equality records the charged time and stops future work"), Time.TryAdvanceTime(12, 0.25));
        TestTrue(TEXT("Exact elapsed cap is recorded without replenishing placements or the spent fence"),
            Time._Elapsed == Time._ElapsedLimit && Time._LastSurfaceFrame == 12 && HasUnchangedLedger(Time));
        auto OvershotTime = MakeFence(Required);
        OvershotTime._LastAcknowledgedFrame = 10;
        TestFalse(TEXT("A finite elapsed crossing is recorded and rejected"),
            OvershotTime.TryAdvanceTime(11, OvershotTime._ElapsedLimit + 0.5));
        TestTrue(TEXT("Elapsed crossing does not clamp or conceal the actual charged amount"),
            OvershotTime._Elapsed == OvershotTime._ElapsedLimit + 0.5 && HasUnchangedLedger(OvershotTime));
        auto Cycles = MakeFence(Required);
        TestTrue(TEXT("First chronological acknowledgement stays below the derived cycle cap"),
            Cycles.TryAcknowledge(10, 20, 21, Cycles._CycleLimit - 0.25));
        TestTrue(TEXT("Next owned invocation charges elapsed time normally"), Cycles.TryAdvanceTime(11, 0.01));
        TestFalse(TEXT("Cycle equality records the acknowledgement and revokes subsequent staging work"),
            Cycles.TryAcknowledge(11, 21, 22, 0.25));
        TestTrue(TEXT("One-leg three-cycle and two-leg five-cycle equality preserve spent slots"),
            Cycles._Cycles == 2.0 * Required + 1.0 && Cycles._LastAcknowledgedSolve == 22
            && HasUnchangedLedger(Cycles));
        auto OvershotCycles = MakeFence(Required);
        TestFalse(TEXT("An actual finite last acknowledgement may overshoot the cycle cap"),
            OvershotCycles.TryAcknowledge(10, 20, 21, OvershotCycles._CycleLimit + 0.5));
        TestTrue(TEXT("Cycle crossing records its full actual advance rather than predicting or clamping it"),
            OvershotCycles._Cycles == OvershotCycles._CycleLimit + 0.5
            && OvershotCycles._LastAcknowledgedSolve == 21 && HasUnchangedLedger(OvershotCycles));
    }
    for (auto Case = 0; Case < 9; ++Case)
    {
        auto Fence = MakeFence();
        Fence._LastAcknowledgedFrame = 10;
        auto Frame = uint64{11};
        auto Delta = 0.1;
        switch (Case)
        {
            case 0: Frame = 10; break;
            case 1: Frame = 9; break;
            case 2: Delta = 0.0; break;
            case 3: Delta = -0.1; break;
            case 4: Delta = std::numeric_limits<double>::quiet_NaN(); break;
            case 5: Delta = std::numeric_limits<double>::infinity(); break;
            case 6: Fence._RequiredCount = 3; break;
            case 7: Fence._CycleLimit = 5.0; break;
            case 8:
                Fence._Elapsed = std::numeric_limits<double>::max();
                Fence._ElapsedLimit = std::numeric_limits<double>::max();
                Delta = std::numeric_limits<double>::max();
                break;
            default: break;
        }
        const auto Before = Fence._Elapsed;
        TestFalse(TEXT("Duplicate/reordered time, malformed delta, wrong limits and overflow reject"),
            Fence.TryAdvanceTime(Frame, Delta));
        TestTrue(TEXT("Rejected time arithmetic neither changes chronology nor erases spent placement history"),
            Fence._Elapsed == Before && Fence._LastSurfaceFrame == 10 && HasUnchangedLedger(Fence));
    }
    for (auto Case = 0; Case < 11; ++Case)
    {
        auto Fence = MakeFence();
        auto Frame = uint64{10};
        auto Input = uint64{20};
        auto Produced = uint64{21};
        auto Advance = 0.1;
        switch (Case)
        {
            case 0: Frame = 9; break;
            case 1: Frame = 11; break;
            case 2: Input = 19; Produced = 20; break;
            case 3: Produced = 22; break;
            case 4: Advance = 0.0; break;
            case 5: Advance = -0.1; break;
            case 6: Advance = std::numeric_limits<double>::quiet_NaN(); break;
            case 7: Advance = std::numeric_limits<double>::infinity(); break;
            case 8: Fence._RequiredCount = 0; break;
            case 9:
                Fence._Cycles = std::numeric_limits<double>::max();
                Advance = std::numeric_limits<double>::max();
                break;
            case 10: Input = MAX_uint64; Produced = 0; break;
            default: break;
        }
        const auto Before = Fence._Cycles;
        TestFalse(TEXT("Missing/reordered solve or frame, nonpositive/nonfinite advance and overflow reject"),
            Fence.TryAcknowledge(Frame, Input, Produced, Advance));
        TestTrue(TEXT("Rejected acknowledgement retains chronology and immutable placement evidence"),
            Fence._Cycles == Before && Fence._LastAcknowledgedSolve == 20 && HasUnchangedLedger(Fence));
    }
    auto Duplicate = MakeFence();
    TestTrue(TEXT("One real chronological acknowledgement is accepted"), Duplicate.TryAcknowledge(10, 20, 21, 0.25));
    TestFalse(TEXT("Duplicate solve acknowledgement cannot charge the same advance twice"),
        Duplicate.TryAcknowledge(10, 20, 21, 0.25));
    TestTrue(TEXT("Duplicate rejection keeps the first actual charge"), Duplicate._Cycles == 0.25 && HasUnchangedLedger(Duplicate));
    TestFalse(TEXT("Consecutive solves cannot acknowledge the same Surface invocation twice"),
        Duplicate.TryAcknowledge(10, 21, 22, 0.25));
    TestTrue(TEXT("Second same-frame solve keeps the first frame/solve/cycle charge intact"),
        Duplicate._LastAcknowledgedFrame == 10 && Duplicate._LastAcknowledgedSolve == 21
        && Duplicate._Cycles == 0.25 && HasUnchangedLedger(Duplicate));
    auto Missing = MakeFence();
    TestFalse(TEXT("A new Surface invocation cannot skip the preceding acknowledgement"),
        Missing.TryAdvanceTime(11, 0.1));
    TestTrue(TEXT("Missing acknowledgement neither advances elapsed time nor replenishes spent slots"),
        Missing._LastSurfaceFrame == 10 && Missing._LastAcknowledgedFrame == 9
        && Missing._Elapsed == 0.0 && HasUnchangedLedger(Missing));
    auto Unspent = MakeFence();
    Unspent._Spent = false;
    TestFalse(TEXT("Unspent record cannot charge episode time"), Unspent.TryAdvanceTime(11, 0.1));
    TestFalse(TEXT("Unspent record cannot acknowledge episode cycles"), Unspent.TryAcknowledge(10, 20, 21, 0.1));
    auto Passage = MakeFence();
    TestFalse(TEXT("Equality with the immutable recorded Exit is not passage"),
        Passage.Get_IsBeyondExit(Passage._Exit, FVector::UpVector));
    auto Actual = Passage._Exit;
    Actual.SetLocation(Actual.GetLocation() + FVector{1.0, 0.0, 0.0});
    TestTrue(TEXT("Strict actual displacement beyond the recorded Forward plane passes the arithmetic predicate"),
        Passage.Get_IsBeyondExit(Actual, FVector::UpVector));
    TestFalse(TEXT("Changed support Up does not release the recorded frame"),
        Passage.Get_IsBeyondExit(Actual, FVector::RightVector));
    TestFalse(TEXT("Nonfinite current Up rejects"),
        Passage.Get_IsBeyondExit(Actual, FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0}));
    Actual.SetLocation(FVector{std::numeric_limits<double>::infinity(), 0.0, 40.0});
    TestFalse(TEXT("Nonfinite actual body cannot prove passage"), Passage.Get_IsBeyondExit(Actual, FVector::UpVector));
    TestTrue(TEXT("Passage arithmetic itself never clears the fence or modifies placements"), HasUnchangedLedger(Passage));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkPreparationFenceLedgerTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.PreparationFenceFourActualStarts", ck::tests::kCkUnitTestFlags)

auto
    FCkPreparationFenceLedgerTest::RunTest(const FString&)
    -> bool
{
    auto Fence = ck::FFragment_ProceduralContactStagingFence{};
    Fence._Mode = ck::EProceduralContactStagingFenceMode::Preparation;
    Fence._Spent = true;
    Fence._CycleLimit = 5.0;
    Fence._ElapsedLimit = 3.5;
    Fence._LastAcknowledgedSolve = 20;
    Fence._Forward = FVector::ForwardVector;
    Fence._Up = FVector::UpVector;
    Fence._Exit.SetLocation(FVector{100.0, 0.0, 40.0});
    auto& Prep = Fence._Preparation;
    Prep._Plan._AuxiliaryQuery = FVector{1.0, 0.0, 0.0};
    Prep._Plan._SpanPreparationQuery = FVector{2.0, 0.0, 0.0};
    Prep._Plan._OtherPreparationQuery = FVector{3.0, 0.0, 0.0};
    Prep._Plan._LegacyPlan._RequiredCount = 1;
    Prep._Plan._LegacyPlan._Required[0]._ProposedQuery = FVector{4.0, 0.0, 0.0};
    TestTrue(TEXT("Preparation has its explicit five-cycle limit without inventing legacy required legs"),
        Fence.Get_HasValidLimits() && Fence._RequiredCount == 0);
    auto Rejected = Fence;
    TestFalse(TEXT("A charged native failure may consume the complete elapsed opportunity before any start"),
        Rejected.TryAdvanceTime(10, 3.5));
    TestTrue(TEXT("First failure preserves spent status and zero actual placements"),
        Rejected._Spent && Rejected._Elapsed == 3.5 && Rejected._Preparation._StartedCount == 0);
    TestTrue(TEXT("First positive Surface invocation charges time"), Fence.TryAdvanceTime(10, 0.01));
    TestFalse(TEXT("Missing previous Gait acknowledgement cannot advance another Surface frame"),
        Fence.TryAdvanceTime(11, 0.01));
    TestTrue(TEXT("Actual Gait advance acknowledges the same frame/input/produced solve"),
        Fence.TryAcknowledge(10, 20, 21, 0.1));
    TestFalse(TEXT("Duplicate acknowledgement grants no additional cycles"), Fence.TryAcknowledge(10, 20, 21, 0.1));
    for (uint8 Slot = 0; Slot < 4; ++Slot)
    {
        const auto Point = Prep.Get_Query(Slot).GetValue();
        if (Slot == 3)
        {
            TestFalse(TEXT("Fourth actual start requires explicit same-episode legacy adoption"),
                Prep.TryRecordStart(Slot, 20, 30, FTransform::Identity, Point, FVector::UpVector, {}));
            Prep._LegacyAdopted = true;
            Fence._RequiredCount = 1;
        }
        TestTrue(TEXT("Exactly the next fully guarded actual start commits one slot"),
            Prep.TryRecordStart(Slot, 20, 30, FTransform::Identity, Point, FVector::UpVector, {}));
        TestFalse(TEXT("A duplicate begin cannot charge or replace an in-flight slot"),
            Prep.TryRecordStart(Slot, 20, 30, FTransform::Identity, Point, FVector::UpVector, {}));
        TestFalse(TEXT("A plan point cannot substitute for a produced native touchdown"),
            Prep.TryRecordTouchdown(Slot, 20, 30, Point, FVector::UpVector, {}));
        TestFalse(TEXT("Changed actual touchdown cannot regenerate the fixed certificate"),
            Prep.TryRecordTouchdown(Slot, 20, 31, Point + FVector::ForwardVector, FVector::UpVector, {}));
        TestFalse(TEXT("A new native normal at the same point aborts the fixed horizontal certificate"),
            Prep.TryRecordTouchdown(Slot, 20, 31, Point, FVector::RightVector, {}));
        TestTrue(TEXT("Same-solve native publication can complete a previously charged start"),
            Prep.TryRecordTouchdown(Slot, 20, 31, Point, FVector::UpVector, {}));
        TestFalse(TEXT("Duplicate touchdown grants no completion credit"),
            Prep.TryRecordTouchdown(Slot, 20, 31, Point, FVector::UpVector, {}));
    }
    TestTrue(TEXT("Four ordered starts/completions preserve the same outer five-cycle cap"),
        Prep.Get_IsLedgerValid() && Prep._StartedCount == 4 && Prep._CompletedCount == 4
        && Fence.Get_HasValidLimits() && Fence._Cycles == 0.1 && Fence._Elapsed == 0.01);
    TestFalse(TEXT("A fifth placement is rejected without indexing beyond fixed storage"),
        Prep.TryRecordStart(4, 21, 31, FTransform::Identity, FVector::ZeroVector, FVector::UpVector, {}));
    TestTrue(TEXT("Positive next frame remains joined after legacy adoption"), Fence.TryAdvanceTime(11, 0.01));
    TestFalse(TEXT("Actual five-cycle equality records exhaustion without refund"),
        Fence.TryAcknowledge(11, 21, 22, 4.9));
    TestTrue(TEXT("Exhaustion preserves spent status, four starts and actual clock total"),
        Fence._Spent && Prep._StartedCount == 4 && Fence._Cycles == 5.0);
    TestFalse(TEXT("Exit equality never releases spent preparation"), Fence.Get_IsBeyondExit(Fence._Exit, FVector::UpVector));
    auto Invalid = Fence;
    Invalid._RequiredCount = 2;
    TestFalse(TEXT("Preparation cannot masquerade as a two-required-leg legacy child"), Invalid.Get_HasValidLimits());
    Invalid = Fence;
    Invalid._Preparation._Placements[0]._Completed = false;
    TestFalse(TEXT("Noncontiguous ledger rejects continued clock authority"), Invalid.Get_HasValidLimits());
    return true;
}

#endif
