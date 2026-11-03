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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkContactStagingTwoPlacementGeometryTest,
    "Ck.ProceduralAnimation.ContactStaging.Core.TwoPlacementGeometry", ck::tests::kCkUnitTestFlags)

auto
    FCkContactStagingTwoPlacementGeometryTest::RunTest(const FString&)
    -> bool
{
    {
        auto SideLeg = ck::FProceduralContactStagingLeg{};
        SideLeg._RosterIndex = 0;
        SideLeg._Anchor = ck::FProceduralSurfaceReachPaceAnchor{FVector{1.0, 1.0, -10.0}, FVector::ZeroVector, 20.0f};
        SideLeg._ForceReach = 20.0f;
        SideLeg._SearchRadius = 2.0f;
        SideLeg._OutboardWorld = FVector::RightVector;
        auto SideLegs = TArray<ck::FProceduralContactStagingLeg>{SideLeg};
        auto SideInput = ck::FProceduralContactStagingInput{};
        SideInput._Up = FVector::UpVector;
        SideInput._Forward = FVector::ForwardVector;
        SideInput._PosedBody = FTransform::Identity;
        SideInput._Legs = SideLegs;
        auto SidePlan = ck::FProceduralContactStagingPlan{};
        SidePlan._TwoPlacement = true;
        SidePlan._RequiredCount = 1;
        SidePlan._Required[0] = {0, FVector{1.0, 1.0, -10.0}, FVector{3.0, 1.0, -10.0}, TOptional<FVector>{FVector{2.0, 1.0, -10.0}}};
        TestTrue(TEXT("Identity presentation certifies the same-side current/intermediate/final baseline"),
            ck::Get_IsProceduralContactStagingHoldValid(SideInput, SidePlan));
        SideInput._PosedBody = FTransform{FQuat{FVector::ForwardVector, FMath::DegreesToRadians(10.0)}};
        TestFalse(TEXT("Common hold uses the actual posed outboard plane before a charged attempt"),
            ck::Get_IsProceduralContactStagingHoldValid(SideInput, SidePlan));
    }
    auto Legs = TArray<ck::FProceduralContactStagingLeg>{};
    for (auto Index = 0; Index < 4; ++Index)
    {
        const auto Front = Index < 2;
        const auto Side = Index % 2 == 0 ? -1.0 : 1.0;
        auto Leg = ck::FProceduralContactStagingLeg{};
        Leg._RosterIndex = Index;
        Leg._Anchor = ck::FProceduralSurfaceReachPaceAnchor{
            FVector{Front ? 0.0 : -20.0, Side * 65.0, 0.0},
            FVector{Front ? 20.0 : -20.0, Side * 15.0, 0.0}, Front ? 125.0f : 180.0f};
        Leg._ForceReach = Front ? 115.0f : 165.6f;
        Leg._SearchRadius = Front ? 31.25f : 45.0f;
        Leg._OwnSideTolerance = Front ? 12.5f : 18.0f;
        Leg._OutboardWorld = FVector{Front ? 20.0 : -20.0, Side * 50.0, 0.0}.GetSafeNormal();
        Leg._NativeTop = ck::FProceduralContactStagingPlane{FVector::ZeroVector, FVector::UpVector};
        Legs.Add(Leg);
    }
    auto Input = ck::FProceduralContactStagingInput{};
    Input._Body = FTransform{FQuat::Identity, FVector{48.0, 0.0, 40.0}};
    Input._PosedBody = Input._Body;
    Input._Up = FVector::UpVector;
    Input._Forward = FVector::ForwardVector;
    Input._Face = ck::FProceduralContactStagingPlane{FVector{41.0, 0.0, 0.0}, FVector::ForwardVector};
    Input._BodyHalfExtents = FVector{68.0, 12.0, 10.0};
    Input._CollisionMargin = 1.0f;
    Input._Legs = Legs;
    TestFalse(TEXT("Legacy selector retains its unchanged direct sphere rejection"),
        ck::Get_ProceduralContactStagingPlan(Input).IsSet());
    const auto Plan = ck::Get_ProceduralContactStagingTwoPlacementPlan(Input);
    if (NOT TestTrue(TEXT("Synthetic common-hold input admits exactly two required legs"),
        Plan.IsSet() && Plan->_TwoPlacement && Plan->_RequiredCount == 2))
    { return false; }
    for (auto Required = 0; Required < 2; ++Required)
    {
        const auto& Leg = Plan->_Required[Required];
        TestTrue(TEXT("Exactly one forward midpoint preserves native plane and lateral coordinate"),
            Leg._IntermediateQuery.IsSet() && Leg._IntermediateQuery->X == 20.0
            && Leg._IntermediateQuery->Y == Leg._OriginalPlant.Y && Leg._IntermediateQuery->Z == 0.0
            && Leg._ProposedQuery == FVector(40.0, Leg._OriginalPlant.Y, 0.0));
        TestTrue(TEXT("Both represented three-dimensional hops respect the unchanged radius"),
            FVector::Dist(Leg._OriginalPlant, Leg._IntermediateQuery.GetValue()) <= Legs[Required]._SearchRadius
            && FVector::Dist(Leg._IntermediateQuery.GetValue(), Leg._ProposedQuery) <= Legs[Required]._SearchRadius);
        const auto Intermediate = ck::FProceduralSurfaceLandingConstraint{Leg._IntermediateQuery.GetValue(),
            Legs[Required]._Anchor.Get_HipLocal(), 115.0f, 125.0f};
        TestFalse(TEXT("Useful intermediate remains outside final Exit physical reach"),
            ck::Get_IsProceduralContactStagingFuturePlantReachValid(Plan->_ExitBody,
                TOptional<FTransform>{FTransform::Identity}, Intermediate));
    }
    const auto Original = Legs;
    const auto IsIntermediate = [&](const FVector& InPoint, const FVector& InForward = FVector::ForwardVector,
        float InRadius = 31.25f)
    {
        return ck::Get_IsProceduralContactStagingIntermediateTargetValid(InPoint, FVector::ZeroVector,
            FVector{40.0, 0.0, 0.0}, InForward, InRadius);
    };
    TestTrue(TEXT("Actual ordinary intermediate may differ from the proposed midpoint"),
        IsIntermediate(FVector{25.0, 0.0, 0.0}));
    TestFalse(TEXT("Final query belongs to separate final classification"), IsIntermediate(FVector{40.0, 0.0, 0.0}));
    TestFalse(TEXT("Actual plant cannot renew a zero-length intermediate"), IsIntermediate(FVector::ZeroVector));
    TestFalse(TEXT("An intermediate must preserve exact lateral coordinate"), IsIntermediate(FVector{20.0, 0.001, 0.0}));
    TestFalse(TEXT("An intermediate must preserve exact plane coordinate"), IsIntermediate(FVector{20.0, 0.0, 0.001}));
    TestFalse(TEXT("Both next and current spheres remain decisive"), IsIntermediate(FVector{5.0, 0.0, 0.0}));
    TestFalse(TEXT("No radius widening or third placement is permitted"), IsIntermediate(FVector{20.0, 0.0, 0.0},
        FVector::ForwardVector, 19.0f));
    TestFalse(TEXT("Noncardinal frame is rejected"), IsIntermediate(FVector{20.0, 0.0, 0.0}, FVector{0.5, 0.5, 0.0}));
    TestFalse(TEXT("Nonfinite point is rejected before arithmetic"), IsIntermediate(FVector{1.0e308}));
    TestFalse(TEXT("Nonfinite radius is rejected before arithmetic"), IsIntermediate(FVector{20.0, 0.0, 0.0},
        FVector::ForwardVector, std::numeric_limits<float>::infinity()));
    for (auto Combination = 0; Combination < 9; ++Combination)
    {
        Legs = Original;
        for (auto Required = 0; Required < 2; ++Required)
        {
            const auto State = Required == 0 ? Combination % 3 : Combination / 3;
            const auto& Milestones = Plan->_Required[Required];
            const auto Point = State == 0 ? Milestones._OriginalPlant
                : State == 1 ? Milestones._IntermediateQuery.GetValue() : Milestones._ProposedQuery;
            Legs[Required]._Anchor = ck::FProceduralSurfaceReachPaceAnchor{
                Point, Original[Required]._Anchor.Get_HipLocal(), Original[Required]._Anchor.Get_Reach()};
            Legs[Required]._NativeTop.Reset();
        }
        Input._Legs = Legs;
        TestTrue(TEXT("All nine real-current combinations share the immutable hold without a new top hit"),
            ck::Get_IsProceduralContactStagingHoldValid(Input, Plan.GetValue()));
    }
    Legs = Original;
    Input._Legs = Legs;
    for (auto Case = 0; Case < 9; ++Case)
    {
        Legs = Original;
        Input._Legs = Legs;
        auto Invalid = Input;
        auto InvalidPlan = Plan.GetValue();
        switch (Case)
        {
            case 0: Legs[0]._SearchRadius = 19.0f; break;
            case 1: Legs[0]._NativeTop->_Point.Z = 1.0; break;
            case 2: Legs[0]._NativeTop.Reset(); break;
            case 3: Legs[0]._SearchRadius = std::numeric_limits<float>::quiet_NaN(); break;
            case 4: Invalid._Body.SetLocation(FVector{1.0e308}); break;
            case 5: Invalid._Face._Point.X = std::numeric_limits<double>::infinity(); break;
            case 6: Legs[0]._ForceReach = 10.0f; break;
            case 7: Invalid._PosedBody = FTransform{FQuat::Identity, FVector{48.0, 1000.0, 40.0}}; break;
            case 8: Invalid._Face._Point.X = 41.00000001; break;
            default: break;
        }
        TestFalse(TEXT("Malformed, over-two-radius, missing/split plane, force, pose or unrepresented final declines atomically"),
            ck::Get_ProceduralContactStagingTwoPlacementPlan(Invalid).IsSet());
        InvalidPlan._Required[0]._IntermediateQuery = FVector{1.0e308};
        TestFalse(TEXT("Malformed midpoint cannot pass any common-state certificate"),
            ck::Get_IsProceduralContactStagingHoldValid(Input, InvalidPlan));
    }
    Legs = Original;
    Input._Legs = Legs;
    auto BadHold = Plan.GetValue();
    BadHold._Required[1]._IntermediateQuery = FVector{20.0, 1000.0, 0.0};
    TestFalse(TEXT("A single intermediate outside hold rejects despite valid old/final endpoints"),
        ck::Get_IsProceduralContactStagingHoldValid(Input, BadHold));
    auto Landings = TArray<ck::FProceduralSurfaceLandingConstraint>{
        ck::FProceduralSurfaceLandingConstraint{FVector{1000.0, 0.0, 0.0}, FVector::ZeroVector, 100.0f, 125.0f}};
    Input._Landings = Landings;
    TestFalse(TEXT("A committed landing cannot be omitted from common hold"),
        ck::Get_IsProceduralContactStagingHoldValid(Input, Plan.GetValue()));
    Input._Landings = {};
    Legs[0]._SearchRadius = Legs[1]._SearchRadius = 50.0f;
    const auto Legacy = ck::Get_ProceduralContactStagingPlan(Input);
    const auto Priority = ck::Get_ProceduralContactStagingTwoPlacementPlan(Input);
    TestTrue(TEXT("Old success retains exact selected bodies, queries and single-placement priority"),
        Legacy.IsSet() && Priority.IsSet() && NOT Priority->_TwoPlacement
        && Legacy->_RetreatBody.Equals(Priority->_RetreatBody, 0.0)
        && Legacy->_ExitBody.Equals(Priority->_ExitBody, 0.0)
        && Legacy->_RequiredCount == Priority->_RequiredCount
        && Legacy->_Required[0]._ProposedQuery == Priority->_Required[0]._ProposedQuery
        && NOT Priority->_Required[0]._IntermediateQuery.IsSet());
    for (auto Coarse = 0; Coarse < 2; ++Coarse)
    {
        const auto Offset = Coarse == 0 ? 1048576.0 : 268435456.0;
        Legs = Original;
        for (auto Index = 0; Index < Legs.Num(); ++Index)
        {
            const auto& Anchor = Original[Index]._Anchor;
            Legs[Index]._Anchor = ck::FProceduralSurfaceReachPaceAnchor{
                Anchor.Get_FootWorld() + FVector{Offset, 0.0, 0.0}, Anchor.Get_HipLocal(), Anchor.Get_Reach()};
            if (Index < 2)
            { Legs[Index]._SearchRadius = 20.0f; }
        }
        auto Translated = Input;
        Translated._Legs = Legs;
        Translated._Body.SetLocation(Input._Body.GetLocation() + FVector{Offset, 0.0, 0.0});
        Translated._PosedBody = Translated._Body;
        Translated._Face._Point.X = Offset + 33.0;
        const auto Represented = ck::Get_ProceduralContactStagingTwoPlacementPlan(Translated);
        if (Coarse == 0)
        {
            TestTrue(TEXT("Exactly represented translated forward midpoint admits both unchanged spheres"),
                Represented.IsSet() && Represented->_Required[0]._IntermediateQuery.IsSet()
                && Represented->_Required[0]._IntermediateQuery->X == Offset + 16.0);
        }
        else
        {
            TestFalse(TEXT("Midpoint float rounding rejects even when mathematical span is below two radii"),
                Represented.IsSet());
        }
    }
    return true;
}

#endif
