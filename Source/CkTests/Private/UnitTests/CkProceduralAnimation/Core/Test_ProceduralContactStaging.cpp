#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"
#include "CkProceduralAnimation/Core/CkProceduralContactStaging.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_procedural_contact_staging
{
    using FResolver = ck::FProceduralGaitNativeRecoveryResolver;
    using FTouchdown = ck::FProceduralGaitNativeTouchdown;

    struct FHarness
    {
        ck::FProceduralGaitSolver Solver;
        TArray<ck::FProceduralGaitLegInput> Inputs;
        TArray<ck::FProceduralGaitLegOutput> Outputs;

        auto Init(float InOffset = 0.0f) -> bool
        {
            auto Settings = ck::FProceduralGaitSettings{};
            Settings.Get_Cadence().Set_CycleDuration(FCk_Time{1.0}).Set_SwingWindow(0.25f)
                .Set_MaxSimultaneousSwings(1);
            Settings.Get_Step().Set_Threshold(25.0f).Set_Duration(FCk_Time{0.1});
            Settings.Get_Settle().Set_AtRest(false);
            Solver.Set_Settings(Settings);
            const auto Initial = TArray<FVector>{FVector::ZeroVector, FVector{0.0, 80.0, 0.0}};
            for (auto Index = 0; Index < Initial.Num(); ++Index)
            {
                Inputs.Add(ck::FProceduralGaitLegInput{}.Set_IdealTarget(Initial[Index])
                    .Set_Hip(Initial[Index] + FVector{0.0, 0.0, 60.0}).Set_Reach(100.0f)
                    .Set_FootContactRadius(2.0f).Set_NativeRecoveryEnabled(true)
                    .Set_PhaseOffset(Index == 0 ? InOffset : 0.5f));
            }
            Inputs[0].Set_IdealTarget(FVector{10.0, 0.0, 0.0});
            Outputs.SetNum(Initial.Num());
            return Solver.Reset(Initial);
        }

        auto Intent() const -> ck::FProceduralGaitPlannedReplantIntent
        {
            return ck::FProceduralGaitPlannedReplantIntent{}.Set_Episode(1)
                .Set_ExpectedPlant(Solver.GetLegState(0).Get_Plant().Get_Position())
                .Set_Target(Inputs[0].Get_IdealTarget()).Set_ExitHip(FVector{90.0, 0.0, 60.0})
                .Set_Frame(1).Set_ConsumerInputSolve(1);
        }

        auto Tick(TOptional<FResolver> InResolver = {}, float InSpeed = 10.0f) -> bool
        {
            return Solver.Step(FCk_Time{0.01}, InSpeed, FVector{InSpeed, 0.0, 0.0},
                Inputs, Outputs, false, {}, InResolver);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkContactStagingInvalidTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.InvalidIntentNoAuthority", ck::tests::kCkUnitTestFlags)

auto
    FCkContactStagingInvalidTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_contact_staging;
    const auto Resolve = [](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
        const FVector&, const FVector& Normal) -> TOptional<FTouchdown>
    { return FTouchdown{Point, Normal, true}; };
    for (auto Case = 0; Case < 14; ++Case)
    {
        auto H = FHarness{};
        if (NOT TestTrue(TEXT("Fixture resets through the public API"), H.Init()))
        { return false; }
        auto Intent = H.Intent();
        switch (Case)
        {
            case 0: Intent = {}; break;
            case 1: Intent.Set_ExpectedPlant(FVector{1.0, 0.0, 0.0}); break;
            case 2: Intent.Set_Target(FVector{11.0, 0.0, 0.0}); break;
            case 3: Intent.Set_ExitHip(FVector{0.0, 0.0, 60.0}); break;
            case 4: Intent.Set_ExitHip(FVector{200.0, 0.0, 60.0}); break;
            case 5: Intent.Set_ExitPosedHip(FVector{200.0, 0.0, 60.0}); break;
            case 6: Intent.Set_ExitHip(FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}); break;
            case 7: H.Inputs[0].Set_TargetValid(false); break;
            case 8: H.Inputs[0].Set_TargetTrusted(false); break;
            case 9: H.Inputs[0].Set_Enabled(false); break;
            case 10: H.Inputs[0].Set_Participating(false); break;
            case 11: H.Inputs[0].Set_NativeRecoveryEnabled(false); break;
            case 13: Intent.Set_ConsumerInputSolve(MAX_uint64); break;
            default: break;
        }
        if (Case != 12)
        { H.Inputs[0].Set_PlannedReplantIntent(Intent); }
        auto GuardCalls = 0;
        auto Denied = TArray<ck::FProceduralGaitDeniedTakeoff>{};
        TestTrue(TEXT("Malformed/absent intent preserves ordinary accepted input"), H.Solver.StepWithTakeoffGuard(
            FCk_Time{0.01}, 10.0f, FVector{10.0, 0.0, 0.0}, H.Inputs, H.Outputs, false, {},
            [&](const ck::FProceduralGaitSolver&, int32, bool) { ++GuardCalls; return true; },
            Denied, TOptional<FResolver>{Resolve}));
        TestEqual(TEXT("Rejected intent never reaches takeoff callback"), GuardCalls, 0);
        TestFalse(TEXT("No small-step override starts"), H.Solver.GetLegState(0).Get_Swing().Get_Active());
        TestTrue(TEXT("No override changes the plant"), H.Solver.GetLegState(0).Get_Plant().Get_Position() == FVector::ZeroVector);
    }
    auto Missing = FHarness{};
    TestTrue(TEXT("Missing resolver fixture initializes"), Missing.Init());
    Missing.Inputs[0].Set_PlannedReplantIntent(Missing.Intent());
    TestTrue(TEXT("Missing native resolver suppresses staging start"), Missing.Tick()
        && NOT Missing.Solver.GetLegState(0).Get_Swing().Get_Active());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkContactStagingScheduleTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.ScheduleGuardAndRenewal", ck::tests::kCkUnitTestFlags)

auto
    FCkContactStagingScheduleTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_contact_staging;
    const auto Resolve = [](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
        const FVector&, const FVector& Normal) -> TOptional<FTouchdown>
    { return FTouchdown{Point, Normal, true}; };
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Closed-window fixture initializes"), H.Init(0.5f)))
    { return false; }
    H.Inputs[0].Set_PlannedReplantIntent(H.Intent());
    TestTrue(TEXT("Closed window retains trusted plant"), H.Tick(TOptional<FResolver>{Resolve})
        && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active());
    H.Inputs[0].Set_PlannedReplantIntent({});
    for (auto Frame = 0; Frame < 55; ++Frame)
    { TestTrue(TEXT("Clock advances through normal inputs"), H.Tick(TOptional<FResolver>{Resolve})); }
    TestTrue(TEXT("Window opened without cached intent authority"), H.Solver.IsWindowOpen(0.5f)
        && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active());
    H.Inputs[0].Set_PlannedReplantIntent(H.Intent());
    auto GuardCalls = 0;
    auto Denied = TArray<ck::FProceduralGaitDeniedTakeoff>{};
    TestTrue(TEXT("Ordinary takeoff guard accepts input but denies departure"), H.Solver.StepWithTakeoffGuard(
        FCk_Time{0.01}, 10.0f, FVector{10.0, 0.0, 0.0}, H.Inputs, H.Outputs, false, {},
        [&](const ck::FProceduralGaitSolver&, int32, bool InEmergency)
        { ++GuardCalls; TestFalse(TEXT("Staging is not Emergency"), InEmergency); return false; },
        Denied, TOptional<FResolver>{Resolve}));
    TestTrue(TEXT("Denied staging has zero plant/swing authority"), GuardCalls == 1 && Denied.Num() == 1
        && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active()
        && H.Solver.GetLegState(0).Get_Plant().Get_Position() == FVector::ZeroVector);
    TestTrue(TEXT("Fresh renewed intent starts inside the ordinary window"), H.Tick(TOptional<FResolver>{Resolve}));
    const auto& Swing = H.Solver.GetLegState(0).Get_Swing();
    TestTrue(TEXT("Small staging step is ordinary and exact"), Swing.Get_Active() && Swing.Get_PlannedReplant()
        && Swing.Get_LandsOnTarget() && NOT Swing.Get_Overshoot() && NOT Swing.Get_BeyondSchedule()
        && NOT Swing.Get_CatchStep() && Swing.Get_Target() == FVector{10.0, 0.0, 0.0});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkContactStagingTouchdownTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.NativeTouchdown", ck::tests::kCkUnitTestFlags)

auto
    FCkContactStagingTouchdownTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_contact_staging;
    for (auto Reject = 0; Reject < 2; ++Reject)
    {
        auto H = FHarness{};
        if (NOT TestTrue(TEXT("Native touchdown fixture initializes"), H.Init()))
        { return false; }
        H.Inputs[0].Set_PlannedReplantIntent(H.Intent());
        auto Calls = 0;
        const auto Resolve = [&](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
            const FVector& Validated, const FVector& Normal) -> TOptional<FTouchdown>
        {
            ++Calls;
            TestTrue(TEXT("Exact native contact survives stroke/freeze prediction"),
                Point == FVector{10.0, 0.0, 0.0} && Validated == Point);
            return Reject != 0 ? TOptional<FTouchdown>{} : TOptional<FTouchdown>{FTouchdown{Point, Normal, true}};
        };
        TestTrue(TEXT("Staging begins without early touchdown callback"), H.Tick(TOptional<FResolver>{Resolve}) && Calls == 0);
        H.Inputs[0].Set_PlannedReplantIntent({});
        for (auto Frame = 0; Frame < 15 && Calls == 0; ++Frame)
        { TestTrue(TEXT("In-flight ordinary solver call succeeds"), H.Tick(TOptional<FResolver>{Resolve})); }
        TestEqual(TEXT("Only native touchdown grants the plant"), Calls, 1);
        if (Reject == 0)
        {
            TestTrue(TEXT("Native success plants trusted at the exact target"), H.Outputs[0].Get_NativeTouchdownResolved()
                && H.Outputs[0].Get_Planted() && H.Solver.GetLegState(0).Get_Plant().Get_Trusted()
                && H.Solver.GetLegState(0).Get_Plant().Get_Position() == FVector{10.0, 0.0, 0.0});
        }
        else
        {
            TestTrue(TEXT("Native failure leaves ordinary recovery pending without a fabricated plant"),
                H.Outputs[0].Get_NativeTouchdownAttempted() && NOT H.Outputs[0].Get_NativeTouchdownResolved()
                && NOT H.Outputs[0].Get_Planted() && NOT H.Solver.GetLegState(0).Get_Plant().Get_Trusted()
                && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ck::EProceduralGaitAutomaticRecovery::WaitingForContact);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkContactStagingLegacyAndBudgetTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.LegacyAndSimultaneousBudget", ck::tests::kCkUnitTestFlags)

auto
    FCkContactStagingLegacyAndBudgetTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_contact_staging;
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Legacy and simultaneous budget fixture initializes"), H.Init()))
    { return false; }
    H.Inputs[1].Set_PhaseOffset(0.0f).Set_IdealTarget(FVector{30.0, 80.0, 0.0});
    TestTrue(TEXT("Existing ordinary threshold still starts without staging or resolver"), H.Tick()
        && H.Solver.GetLegState(1).Get_Swing().Get_Active()
        && NOT H.Solver.GetLegState(1).Get_Swing().Get_PlannedReplant());
    H.Inputs[0].Set_PlannedReplantIntent(H.Intent());
    const auto Resolve = [](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
        const FVector&, const FVector& Normal) -> TOptional<FTouchdown>
    { return FTouchdown{Point, Normal, true}; };
    TestTrue(TEXT("Valid staging cannot exceed simultaneous swing budget"), H.Tick(TOptional<FResolver>{Resolve})
        && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active()
        && H.Solver.GetLegState(0).Get_Plant().Get_Position() == FVector::ZeroVector);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkContactStagingClockTravelTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.UnwrappedClockTravel", ck::tests::kCkUnitTestFlags)

auto
    FCkContactStagingClockTravelTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_contact_staging;
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Clock reporting fixture initializes"), H.Init()))
    { return false; }
    TestEqual(TEXT("Reset has no reported clock progress"), H.Solver.GetLastClockAdvance(), 0.0);
    TestTrue(TEXT("Large positive Dt follows the existing production clock path"), H.Solver.Step(
        FCk_Time{2.25}, 10.0f, FVector{10.0, 0.0, 0.0}, H.Inputs, H.Outputs));
    TestTrue(TEXT("Unwrapped progress reports all cycles rather than modulo delta"),
        FMath::IsNearlyEqual(H.Solver.GetLastClockAdvance(), 2.25, 1.0e-6)
        && FMath::IsNearlyEqual(H.Solver.GetGaitClock(), 0.25f, 1.0e-6f));
    const auto Clock = H.Solver.GetGaitClock();
    TestTrue(TEXT("Fresh zero-Dt evaluation remains accepted"), H.Solver.Step(
        FCk_Time{}, 10.0f, FVector{10.0, 0.0, 0.0}, H.Inputs, H.Outputs));
    TestTrue(TEXT("Read-only evaluation reports zero progress without changing the clock"),
        H.Solver.GetLastClockAdvance() == 0.0 && H.Solver.GetGaitClock() == Clock);
    TestTrue(TEXT("A later ordinary frame reports only its own travel"), H.Tick()
        && FMath::IsNearlyEqual(H.Solver.GetLastClockAdvance(), 0.01, 1.0e-6));
    TestFalse(TEXT("Malformed Dt still rejects"), H.Solver.Step(
        FCk_Time{std::numeric_limits<double>::quiet_NaN()}, 10.0f, FVector{10.0, 0.0, 0.0}, H.Inputs, H.Outputs));
    TestTrue(TEXT("Failed input grants no stale clock progress or clock mutation"),
        H.Solver.GetLastClockAdvance() == 0.0 && FMath::IsNearlyEqual(H.Solver.GetGaitClock(), Clock + 0.01f, 1.0e-6f));
    auto Nudge = FHarness{};
    if (NOT TestTrue(TEXT("Schedule nudge reporting fixture initializes"), Nudge.Init(0.5f)))
    { return false; }
    Nudge.Inputs[0].Set_IdealTarget(FVector{30.0, 0.0, 0.0});
    TestTrue(TEXT("Existing schedule nudge is included without counting the normalization turn"), Nudge.Tick({}, 0.0f)
        && FMath::IsNearlyEqual(Nudge.Solver.GetLastClockAdvance(), 0.015, 1.0e-6)
        && FMath::IsNearlyEqual(Nudge.Solver.GetGaitClock(), 0.015f, 1.0e-6f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkContactStagingFuturePlantReachTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.FuturePlantedExitReach", ck::tests::kCkUnitTestFlags)

auto
    FCkContactStagingFuturePlantReachTest::RunTest(const FString&)
    -> bool
{
    const auto Point = FVector{120789.0, 365029.34375, 675.0};
    const auto HipLocal = FVector{45.0, 28.0, 0.0};
    const auto Constraint = ck::FProceduralSurfaceLandingConstraint{Point, HipLocal, 92.0f, 100.0f};
    const auto Retreat = FTransform{FQuat::Identity, FVector{120793.40625, 365000.0, 698.32170235708543}};
    const auto Exit = FTransform{FQuat::Identity, FVector{120841.140625, 365000.0, 698.32170235708543}};
    const auto Pose = TOptional<FTransform>{FTransform::Identity};
    TestTrue(TEXT("Native committed landing fits current retreat admission"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Retreat, Pose, Constraint));
    TestFalse(TEXT("Ordinary landing admission does not fit future exit"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Exit, Pose, Constraint));
    TestTrue(TEXT("Future planted contact uses raw physical reach"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, Pose, Constraint));
    TestFalse(TEXT("Actual in-flight exit keeps ordinary admission"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Exit, Pose, Constraint));

    const auto RearPoint = FVector{120739.0, 365000.0, 600.0};
    const auto RearHipLocal = FVector{-45.0, -28.0, 0.0};
    const auto RearLanding = ck::FProceduralSurfaceLandingConstraint{RearPoint, RearHipLocal, 110.4f, 120.0f};
    TestTrue(TEXT("Ordinary rear future planted contact fits its own physical chain"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, Pose, RearLanding));
    TestFalse(TEXT("Ordinary rear in-flight landing still cannot move the actual body beyond admission"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Exit, Pose, RearLanding));

    const auto TooFarPoint = Point - FVector{10.0, 0.0, 0.0};
    const auto Unreachable = ck::FProceduralSurfaceLandingConstraint{TooFarPoint, HipLocal, 92.0f, 100.0f};
    TestFalse(TEXT("Required future target outside physical reach is rejected"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, Pose, Unreachable));
    const auto TooFarBody = FTransform{FQuat::Identity, Exit.GetLocation() + FVector{10.0, 0.0, 0.0}};
    TestFalse(TEXT("Required future body outside physical reach is rejected"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(TooFarBody, Pose, Constraint));
    const auto TooFarPose = TOptional<FTransform>{FTransform{FQuat::Identity, FVector{10.0, 0.0, 0.0}}};
    TestFalse(TEXT("Required future posed hip also respects physical reach"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, TooFarPose, Constraint));

    const auto BadVector = FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0};
    const auto BadPoint = ck::FProceduralSurfaceLandingConstraint{BadVector, HipLocal, 92.0f, 100.0f};
    const auto BadHip = ck::FProceduralSurfaceLandingConstraint{Point, BadVector, 92.0f, 100.0f};
    const auto BadBody = FTransform{FQuat::Identity, FVector{1.0e308}};
    const auto BadPose = TOptional<FTransform>{FTransform{FQuat::Identity, FVector{1.0e308}}};
    TestFalse(TEXT("Malformed future contact vector is rejected"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, Pose, BadPoint));
    TestFalse(TEXT("Malformed future hip vector is rejected"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, Pose, BadHip));
    TestFalse(TEXT("Malformed future body vector is rejected"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(BadBody, Pose, Constraint));
    TestFalse(TEXT("Malformed future pose vector is rejected"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, BadPose, Constraint));
    TestFalse(TEXT("Default incomplete landing constraint is rejected"),
        ck::Get_IsProceduralContactStagingFuturePlantReachValid(Exit, Pose, ck::FProceduralSurfaceLandingConstraint{}));
    TestTrue(TEXT("Read-only future checks preserve current native geometry and both original bounds"),
        Constraint.Get_PointWorld() == Point && Constraint.Get_HipLocal() == HipLocal
        && Constraint.Get_AdmissionReach() == 92.0f && Constraint.Get_Reach() == 100.0f
        && RearLanding.Get_PointWorld() == RearPoint && RearLanding.Get_HipLocal() == RearHipLocal
        && RearLanding.Get_AdmissionReach() == 110.4f && RearLanding.Get_Reach() == 120.0f
        && Retreat.GetLocation() == FVector{120793.40625, 365000.0, 698.32170235708543}
        && Exit.GetLocation() == FVector{120841.140625, 365000.0, 698.32170235708543});
    return true;
}

#endif
