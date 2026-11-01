#include "CkProceduralAnimation/Core/CkProceduralSurfaceMotion.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_committed_landing_pace
{
    const auto Step = FCk_Time{1.0 / 90.0};
    const auto NoFeet = TOptional<ck::FProceduralSurfaceFeetSupport>{};
    using FLanding = ck::FProceduralSurfaceLandingConstraint;
    using FAnchor = ck::FProceduralSurfaceReachPaceAnchor;

    auto
        MakeSettings()
        -> ck::FProceduralSurfaceMotionSettings
    {
        return ck::FProceduralSurfaceMotionSettings{}.Set_Clearance(75.0f).Set_ProbeReach(200.0f);
    }

    auto
        MakeState()
        -> ck::FProceduralSurfaceMotionState
    {
        return ck::FProceduralSurfaceMotionState{}.Set_Grounded(true).Set_ContactTrusted(true)
            .Set_SupportNormal(FVector::UpVector).Set_TravelTangent(FVector::ForwardVector);
    }

    // Segment intersection with the actual half-space Z <= 0; no scripted contact or state transition.
    auto
        CastFloor(
            const FVector& InStart,
            const FVector& InEnd)
        -> ck::FProceduralSurfaceHit
    {
        if (InStart.Z <= 0.0 || InEnd.Z > 0.0)
        { return {}; }
        const auto Fraction = InStart.Z / (InStart.Z - InEnd.Z);
        return ck::FProceduralSurfaceHit{}.Set_Hit(true).Set_Position(FMath::Lerp(InStart, InEnd, Fraction))
            .Set_Normal(FVector::UpVector).Set_Fraction(static_cast<float>(Fraction));
    }

    auto
        SameState(
            const ck::FProceduralSurfaceMotionState& InA,
            const ck::FProceduralSurfaceMotionState& InB)
        -> bool
    {
        return InA.Get_SupportNormal() == InB.Get_SupportNormal() && InA.Get_SupportPoint() == InB.Get_SupportPoint()
            && InA.Get_TravelTangent() == InB.Get_TravelTangent() && InA.Get_StepForward() == InB.Get_StepForward()
            && InA.Get_Velocity() == InB.Get_Velocity() && InA.Get_MissingContact() == InB.Get_MissingContact()
            && InA.Get_Grounded() == InB.Get_Grounded() && InA.Get_ContactTrusted() == InB.Get_ContactTrusted()
            && InA.Get_ContactSource() == InB.Get_ContactSource() && InA.Get_CandidateNormal() == InB.Get_CandidateNormal()
            && InA.Get_CandidatePoint() == InB.Get_CandidatePoint() && InA.Get_CandidateSeen() == InB.Get_CandidateSeen()
            && InA.Get_Obstruction() == InB.Get_Obstruction() && InA.Get_ObstructionPoint() == InB.Get_ObstructionPoint()
            && InA.Get_ObstructionNormal() == InB.Get_ObstructionNormal()
            && InA.Get_ObstructionStandoff() == InB.Get_ObstructionStandoff()
            && NOT InA.Get_ClimbAcquisition().IsSet() && NOT InB.Get_ClimbAcquisition().IsSet();
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkCommittedLandingAdmissionTest,
    "Ck.ProceduralAnimation.SurfaceMotion.CommittedLanding.StrictSimulationAndPosedPhysical",
    ck::tests::kCkUnitTestFlags)

auto
    FCkCommittedLandingAdmissionTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_committed_landing_pace;
    const auto Body = FTransform{FVector{0.0, 0.0, 75.0}};
    const auto Exact = FLanding{FVector{-92.0, 0.0, 75.0}, FVector::ZeroVector, 92.0f, 100.0f};
    TestTrue(TEXT("Simulation exactly at admission reach is admitted"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Body, {}, Exact));
    const auto Outside = FLanding{FVector{-92.0000001, 0.0, 75.0}, FVector::ZeroVector, 92.0f, 100.0f};
    TestFalse(TEXT("Simulation admission borrows neither physical reach nor a numerical epsilon"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Body, {}, Outside));
    TestTrue(TEXT("Only the posed physical endpoint receives the existing 0.001 tolerance"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Body, TOptional<FTransform>{FTransform{FVector{8.0005, 0.0, 0.0}}}, Exact));
    TestFalse(TEXT("Posed physical excess above 0.001 is rejected despite exact simulation admission"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Body, TOptional<FTransform>{FTransform{FVector{8.0011, 0.0, 0.0}}}, Exact));
    auto InvalidVector = FVector::ZeroVector;
    InvalidVector.X = std::numeric_limits<double>::quiet_NaN();
    const auto Invalid = TArray<FLanding>{FLanding{},
        FLanding{InvalidVector, FVector::ZeroVector, 92.0f, 100.0f},
        FLanding{Exact.Get_PointWorld(), InvalidVector, 92.0f, 100.0f},
        FLanding{Exact.Get_PointWorld(), FVector::ZeroVector, 101.0f, 100.0f},
        FLanding{Exact.Get_PointWorld(), FVector::ZeroVector, 92.0f, std::numeric_limits<float>::infinity()}};
    for (const auto& Landing : Invalid)
    {
        TestFalse(TEXT("Malformed or inconsistent landing snapshots reject without transform work or an ensure"),
            ck::Get_IsProceduralSurfaceLandingReachValid(Body, {}, Landing));
    }
    // All components are finite. Their transform/composition/distance intermediates are not safe; reject
    // before diagnostic-bearing FVector/FTransform arithmetic, with no ray callback or downstream native query.
    const auto Extreme = std::numeric_limits<double>::max() / 4.0;
    const auto FarBody = FTransform{FVector{Extreme, 0.0, 75.0}};
    TestFalse(TEXT("Finite translation whose squared distance overflows is rejected before vector distance"),
        ck::Get_IsProceduralSurfaceLandingReachValid(FarBody, {}, Exact));
    const auto HugeLocal = FLanding{Exact.Get_PointWorld(), FVector{Extreme, 0.0, 0.0}, 92.0f, 100.0f};
    const auto HugeScale = FTransform{FQuat::Identity, Body.GetTranslation(), FVector{8.0, 1.0, 1.0}};
    TestFalse(TEXT("Finite local and non-unit scale whose transform product overflows are rejected first"),
        ck::Get_IsProceduralSurfaceLandingReachValid(HugeScale, {}, HugeLocal));
    TestFalse(TEXT("Finite pose translation cannot overflow composition before rejection"),
        ck::Get_IsProceduralSurfaceLandingReachValid(HugeScale, TOptional<FTransform>{FTransform{FVector{Extreme, 0.0, 0.0}}}, Exact));
    const auto HugeBodyScale = FTransform{FQuat::Identity, Body.GetTranslation(), FVector{Extreme, 1.0, 1.0}};
    const auto ScalePose = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{8.0, 1.0, 1.0}};
    TestFalse(TEXT("Finite body/pose scales cannot overflow the composed scale even for a zero local hip"),
        ck::Get_IsProceduralSurfaceLandingReachValid(HugeBodyScale, TOptional<FTransform>{ScalePose}, Exact));
    const auto FarPoint = FLanding{FVector{-Extreme, 0.0, 75.0}, FVector::ZeroVector, 92.0f, 100.0f};
    TestFalse(TEXT("Finite point-to-hip subtraction and squared-distance overflow reject safely"),
        ck::Get_IsProceduralSurfaceLandingReachValid(FarBody, {}, FarPoint));
    const auto ScaledBody = FTransform{FQuat::Identity, Body.GetTranslation(), FVector{2.0, 1.0, 1.0}};
    const auto ScaledLanding = FLanding{FVector{2.0, 0.0, 75.0}, FVector{1.0, 0.0, 0.0}, 92.0f, 100.0f};
    TestTrue(TEXT("Safe finite non-unit simulation and posed scales retain their existing transform semantics"),
        ck::Get_IsProceduralSurfaceLandingReachValid(ScaledBody, TOptional<FTransform>{ScalePose}, ScaledLanding));
    const auto NegativePose = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{-2.0, 1.0, 1.0}};
    TestTrue(TEXT("Safe negative-scale pose composition is not prohibited by overflow preflight"),
        ck::Get_IsProceduralSurfaceLandingReachValid(ScaledBody, TOptional<FTransform>{NegativePose}, ScaledLanding));
    TestFalse(TEXT("Extreme finite negative-scale matrix composition rejects before extraction overflow"),
        ck::Get_IsProceduralSurfaceLandingReachValid(HugeBodyScale, TOptional<FTransform>{NegativePose}, Exact));
    volatile double RuntimeTiny = -2.0e-162;
    const auto RuntimeTinySquared = RuntimeTiny * RuntimeTiny;
    const auto UnnormalizablePose = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{RuntimeTiny, 1.0, 1.0}};
    TestTrue(TEXT("Tiny negative row follows actual FP mode: FTZ zero-axis identity is safe; gradual nonunit normalization rejects"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Body, TOptional<FTransform>{UnnormalizablePose}, Exact)
            == (RuntimeTinySquared == 0.0));
    const auto ExactTinyScale = FMath::Sqrt(std::numeric_limits<double>::denorm_min());
    const auto NormalizableTinyPose = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{-ExactTinyScale, 1.0, 1.0}};
    TestTrue(TEXT("Engine-safe exactly normalizable or zero-rounded subnormal rows remain admissible"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Body, TOptional<FTransform>{NormalizableTinyPose}, Exact));
    const auto ZeroRowPose = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{-2.0, 0.0, 1.0}};
    TestTrue(TEXT("The engine's safe zero-axis identity path remains admissible"),
        ck::Get_IsProceduralSurfaceLandingReachValid(Body, TOptional<FTransform>{ZeroRowPose}, Exact));
    volatile double RuntimeBasisScale = FMath::Sqrt(2.0 * std::numeric_limits<double>::denorm_min());
    const auto RuntimeBasisSquared = RuntimeBasisScale * RuntimeBasisScale;
    const auto BasisStart = FTransform{FQuat::Identity, Body.GetTranslation(), FVector{RuntimeBasisScale, 1.0, 1.0}};
    const auto BasisPose = TOptional<FTransform>{FTransform{FQuat::Identity, FVector::ZeroVector, FVector{-1.0, 1.0, 1.0}}};
    const auto BasisLanding = TArray<FLanding>{{Body.GetTranslation(), FVector::ZeroVector, 100.0f, 120.0f}};
    const auto Direction = FVector{FMath::Sqrt(0.3), FMath::Sqrt(0.3), FMath::Sqrt(0.4)};
    const auto Normal = FVector{FMath::Sqrt(0.5), -FMath::Sqrt(0.5), 0.0};
    const auto PlanePoint = Body.GetTranslation() - Normal * 75.0;
    const auto PlaneCast = [&](const FVector& A, const FVector& B) -> ck::FProceduralSurfaceHit
    {
        const auto AHeight = FVector::DotProduct(A - PlanePoint, Normal);
        const auto BHeight = FVector::DotProduct(B - PlanePoint, Normal);
        if (AHeight <= 0.0 || BHeight > 0.0)
        { return {}; }
        const auto Fraction = AHeight / (AHeight - BHeight);
        return ck::FProceduralSurfaceHit{}.Set_Hit(true).Set_Position(FMath::Lerp(A, B, Fraction))
            .Set_Normal(Normal).Set_Fraction(static_cast<float>(Fraction));
    };
    const auto BasisSettings = MakeSettings().Set_SurfaceTurnRateDegrees(1000000.0f);
    const auto BasisInitial = MakeState().Set_SupportNormal(Normal).Set_TravelTangent(Direction);
    auto BasisFull = BasisStart;
    auto BasisFullState = BasisInitial;
    ck::StepProceduralSurfaceMotion(BasisSettings, Direction, 0.0f, Step, PlaneCast, NoFeet, BasisFull, BasisFullState);
    TestTrue(TEXT("The geometric contact actually turns Full's X axis into the three-component underflow discriminator"),
        BasisFull.GetRotation().GetAxisX().Equals(Direction, 1.0e-6)
        && ck::Get_IsProceduralSurfaceLandingReachValid(BasisStart, BasisPose, BasisLanding[0])
        && ck::Get_IsProceduralSurfaceLandingReachValid(BasisFull, BasisPose, BasisLanding[0]) == (RuntimeBasisSquared == 0.0));
    auto BasisAccepted = BasisStart;
    auto BasisAcceptedState = BasisInitial;
    auto BasisCallbacks = 0;
    const auto BasisAdmit = [&](const FTransform&) { ++BasisCallbacks; return true; };
    const auto BasisOutcome = ck::StepProceduralSurfaceMotionPaced(BasisSettings, Direction, 0.0f, Step,
        PlaneCast, NoFeet, {}, BasisPose, BasisAccepted, BasisAcceptedState, {},
        TOptional<ck::FProceduralSurfaceContactCorrectionAdmission>{ck::FProceduralSurfaceContactCorrectionAdmission{BasisAdmit}},
        TArrayView<const FLanding>{BasisLanding});
    TestTrue(TEXT("Pending-only changed-orientation trial cannot eagerly compose an unsafe pose or publish malformed metrics"),
        BasisCallbacks == 0 && NOT BasisOutcome.Get_PhysicalOverride() && BasisOutcome.Get_WithdrawnLandingIndices().IsEmpty()
        && FMath::IsFinite(BasisOutcome.Get_AttemptedStanceSpeed())
        && ck::Get_IsProceduralSurfaceLandingReachValid(BasisAccepted, BasisPose, BasisLanding[0])
        && (RuntimeBasisSquared == 0.0 ? BasisOutcome.Get_Scale() == 1.0f : BasisOutcome.Get_Scale() == 0.0f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkCommittedLandingMotionTest,
    "Ck.ProceduralAnimation.SurfaceMotion.CommittedLanding.TranslationTurnAndMultipleConstraints",
    ck::tests::kCkUnitTestFlags)

auto
    FCkCommittedLandingMotionTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_committed_landing_pace;
    const auto Start = FTransform{FVector{0.0, 0.0, 75.0}};
    const auto Initial = MakeState();
    const auto Settings = MakeSettings();
    const auto Run = [&](const TArray<FLanding>& InLandings, const FVector& InSteer, float InSpeed,
        const TOptional<FTransform>& InPose, const TCHAR* InLabel) -> void
    {
        auto Body = Start;
        auto State = Initial;
        auto Rays = 0;
        const auto Cast = [&](const FVector& A, const FVector& B) { ++Rays; return CastFloor(A, B); };
        const auto Outcome = ck::StepProceduralSurfaceMotionPaced(Settings, InSteer, InSpeed, Step, Cast, NoFeet,
            {}, InPose, Body, State, {}, {}, TArrayView<const FLanding>{InLandings});
        TestTrue(InLabel, Outcome.Get_Scale() > 0.0f && Outcome.Get_Scale() < 1.0f && Outcome.Get_Trials() == 8
            && NOT Outcome.Get_PhysicalOverride() && Outcome.Get_AttemptedStanceSpeed() > 0.0f
            && Outcome.Get_RejectedAnchorIndices().IsEmpty() && NOT Outcome.Get_RejectedFullBody().IsSet()
            && Outcome.Get_WithdrawnLandingIndices().IsEmpty());
        for (const auto& Landing : InLandings)
        {
            TestTrue(TEXT("Every retained landing fits the actually published simulation and posed body"),
                ck::Get_IsProceduralSurfaceLandingReachValid(Body, InPose, Landing));
        }
        auto Direct = Start;
        auto DirectState = Initial;
        auto DirectRays = 0;
        const auto DirectCast = [&](const FVector& A, const FVector& B) { ++DirectRays; return CastFloor(A, B); };
        ck::StepProceduralSurfaceMotion(Settings, InSteer, InSpeed, Step, DirectCast, NoFeet, Direct, DirectState);
        TestEqual(TEXT("Landing admission adds no queries beyond the eight existing replay trials"), Rays, DirectRays * 8);
        TestEqual(TEXT("Accepted support/grace time advances once, not eight times"), State.Get_MissingContact(), DirectState.Get_MissingContact());
    };
    Run(TArray<FLanding>{{FVector{-99.0, 0.0, 75.0}, FVector::ZeroVector, 100.0f, 120.0f}},
        FVector::ForwardVector, 180.0f, {}, TEXT("A landing alone paces translation without becoming a planted rejection"));
    Run(TArray<FLanding>{{FVector{40.0, -99.0, 75.0}, FVector{40.0, 0.0, 0.0}, 100.0f, 120.0f}},
        FVector::RightVector, 0.0f, {}, TEXT("A displaced landing hip paces a pure turn and reports attempted stance speed"));
    Run(TArray<FLanding>{{FVector{-98.0, 0.0, 75.0}, FVector::ZeroVector, 100.0f, 100.0f}},
        FVector::ForwardVector, 180.0f, TOptional<FTransform>{FTransform{FVector{1.0, 0.0, 0.0}}},
        TEXT("Posed physical reach alone can reject the exact-admissible full simulation endpoint"));
    Run(TArray<FLanding>{{FVector{-99.0, 0.0, 75.0}, FVector::ZeroVector, 100.0f, 120.0f},
            {FVector{0.0, -99.0, 75.0}, FVector::ZeroVector, 100.0f, 120.0f}},
        FVector{1.0, 1.0, 0.0}.GetSafeNormal(), 180.0f, {}, TEXT("All committed landings jointly bound the accepted diagonal trial"));
    // Reach rejection must survive even when its finite double hip displacement cannot fit the float cadence metric.
    const auto HugeHipLanding = TArray<FLanding>{{FVector{1.0e150, 0.0, 75.0}, FVector{1.0e150, 0.0, 0.0}, 100.0f, 120.0f}};
    auto HugeHipBody = Start;
    auto HugeHipState = Initial;
    const auto HugeHipOutcome = ck::StepProceduralSurfaceMotionPaced(Settings, FVector::RightVector, 0.0f, Step,
        CastFloor, NoFeet, {}, {}, HugeHipBody, HugeHipState, {}, {}, TArrayView<const FLanding>{HugeHipLanding});
    TestTrue(TEXT("Pending-only finite double displacement cannot publish an infinite float attempted speed or relax admission"),
        FMath::IsFinite(HugeHipOutcome.Get_AttemptedStanceSpeed()) && HugeHipOutcome.Get_AttemptedStanceSpeed() == 0.0f
        && HugeHipOutcome.Get_Scale() == 0.0f && HugeHipOutcome.Get_Trials() <= 8
        && NOT HugeHipOutcome.Get_PhysicalOverride() && HugeHipOutcome.Get_WithdrawnLandingIndices().IsEmpty()
        && ck::Get_IsProceduralSurfaceLandingReachValid(HugeHipBody, {}, HugeHipLanding[0]));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkCommittedLandingWithdrawalTest,
    "Ck.ProceduralAnimation.SurfaceMotion.CommittedLanding.ZeroWithdrawalPreservesPhysicalStance",
    ck::tests::kCkUnitTestFlags)

auto
    FCkCommittedLandingWithdrawalTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_committed_landing_pace;
    const auto Start = FTransform{FVector{0.0, 0.0, 75.0}};
    const auto Settings = MakeSettings().Set_ContactGrace(FCk_Time{});
    const auto Initial = MakeState();
    const auto Miss = [](const FVector&, const FVector&) { return ck::FProceduralSurfaceHit{}; };
    const auto Landings = TArray<FLanding>{{FVector{0.0, 0.0, 175.0}, FVector::ZeroVector, 100.0f, 120.0f}, FLanding{}};
    auto DirectFull = Start;
    auto DirectFullState = Initial;
    auto DirectZero = Start;
    auto DirectZeroState = Initial;
    ck::StepProceduralSurfaceMotion(Settings, FVector::ForwardVector, 180.0f, Step, Miss, NoFeet, DirectFull, DirectFullState);
    ck::StepProceduralSurfaceMotion(Settings, FVector::ForwardVector, 180.0f, Step, Miss, NoFeet, DirectZero, DirectZeroState, 0.0f);
    TestTrue(TEXT("This actual all-miss fixture falls from zero velocity identically at full and zero voluntary scale"),
        DirectFull.Equals(DirectZero, 0.0) && SameState(DirectFullState, DirectZeroState)
        && DirectZero.GetLocation().Z < Start.GetLocation().Z);
    for (const auto HasPlant : {false, true})
    {
        const auto Anchors = HasPlant ? TArray<FAnchor>{{FVector{0.0, 0.0, 175.0}, FVector::ZeroVector, 100.0f}} : TArray<FAnchor>{};
        auto Baseline = Start;
        auto BaselineState = Initial;
        const auto BaselineOutcome = ck::StepProceduralSurfaceMotionPaced(Settings, FVector::ForwardVector, 180.0f, Step,
            Miss, NoFeet, TArrayView<const FAnchor>{Anchors}, {}, Baseline, BaselineState);
        auto Body = Start;
        auto State = Initial;
        const auto Outcome = ck::StepProceduralSurfaceMotionPaced(Settings, FVector::ForwardVector, 180.0f, Step,
            Miss, NoFeet, TArrayView<const FAnchor>{Anchors}, {}, Body, State, {}, {}, TArrayView<const FLanding>{Landings});
        TestTrue(TEXT("Withdrawing the landing preserves the identical actual-stance physical-override baseline"),
            Body.Equals(Baseline, 0.0) && SameState(State, BaselineState)
            && Outcome.Get_Scale() == BaselineOutcome.Get_Scale()
            && Outcome.Get_PhysicalOverride() == BaselineOutcome.Get_PhysicalOverride());
        if (HasPlant)
        {
            const auto PhysicalLimit = FMath::Max(100.0, FVector::Dist(DirectZero.GetLocation(), Anchors[0].Get_FootWorld())) + 0.001;
            TestTrue(TEXT("Unavoidable gravity retains the actual planted zero-distance bound without worsening it"),
                FVector::Dist(Body.GetLocation(), Anchors[0].Get_FootWorld()) <= PhysicalLimit);
        }
        TestTrue(TEXT("Gravity invalidates a future landing independently and never creates a planted rejection"),
            Body.GetLocation().Z < Start.GetLocation().Z && NOT State.Get_Grounded()
            && Outcome.Get_WithdrawnLandingIndices().Num() == 2
            && Outcome.Get_WithdrawnLandingIndices().Contains(0) && Outcome.Get_WithdrawnLandingIndices().Contains(1)
            && Outcome.Get_RejectedAnchorIndices().IsEmpty() && NOT Outcome.Get_RejectedFullBody().IsSet()
            && Outcome.Get_Trials() == 2 && Outcome.Get_Scale() == 1.0f
            && Outcome.Get_PhysicalOverride() == HasPlant && State.Get_MissingContact() == Step);
    }
    // Malformed membership is removed independently while a second, valid landing still paces.
    const auto Mixed = TArray<FLanding>{FLanding{}, FLanding{FVector{-99.0, 0.0, 75.0}, FVector::ZeroVector, 100.0f, 120.0f}};
    auto Body = Start;
    auto State = Initial;
    const auto Outcome = ck::StepProceduralSurfaceMotionPaced(MakeSettings(), FVector::ForwardVector, 180.0f, Step,
        CastFloor, NoFeet, {}, {}, Body, State, {}, {}, TArrayView<const FLanding>{Mixed});
    TestTrue(TEXT("Withdrawing one invalid index preserves the other landing's exact admission constraint"),
        Outcome.Get_WithdrawnLandingIndices().Num() == 1 && Outcome.Get_WithdrawnLandingIndices()[0] == 0
        && Outcome.Get_Scale() < 1.0f && NOT Outcome.Get_PhysicalOverride()
        && ck::Get_IsProceduralSurfaceLandingReachValid(Body, {}, Mixed[1]));
    // No actual anchors: malformed future evidence must retire before FitsReach can compose an unsafe pose.
    auto InvalidPose = FTransform::Identity;
    auto InvalidTranslation = FVector::ZeroVector;
    InvalidTranslation.X = std::numeric_limits<double>::max() / 4.0;
    InvalidPose.SetTranslation(InvalidTranslation);
    InvalidPose.SetScale3D(FVector{8.0, 1.0, 1.0});
    const auto HugeLocal = FLanding{FVector::ZeroVector,
        FVector{std::numeric_limits<double>::max() / 4.0, 0.0, 0.0}, 100.0f, 120.0f};
    volatile double RuntimeTiny = -2.0e-162;
    const auto RuntimeTinySquared = RuntimeTiny * RuntimeTiny;
    const auto TinyNegativePose = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{RuntimeTiny, 1.0, 1.0}};
    const auto Poses = TArray<TOptional<FTransform>>{TOptional<FTransform>{InvalidPose}, TOptional<FTransform>{},
        TOptional<FTransform>{TinyNegativePose}};
    for (auto Case = 0; Case < Poses.Num(); ++Case)
    {
        const auto InvalidLanding = Case == 1 ? HugeLocal
            : (Case == 2 ? FLanding{Start.GetTranslation(), FVector::ZeroVector, 100.0f, 120.0f} : Mixed[1]);
        const auto ExpectWithdraw = Case != 2 || RuntimeTinySquared != 0.0;
        const auto InvalidLandings = TArray<FLanding>{InvalidLanding};
        auto Direct = Start;
        auto DirectState = Initial;
        auto DirectRays = 0;
        const auto DirectCast = [&](const FVector& A, const FVector& B) { ++DirectRays; return CastFloor(A, B); };
        ck::StepProceduralSurfaceMotion(MakeSettings(), FVector::ForwardVector, 180.0f, Step,
            DirectCast, NoFeet, Direct, DirectState);
        auto Guarded = Start;
        auto GuardedState = Initial;
        auto Rays = 0;
        auto Calls = 0;
        const auto Cast = [&](const FVector& A, const FVector& B) { ++Rays; return CastFloor(A, B); };
        const auto Admit = [&](const FTransform&) { ++Calls; return true; };
        const auto Retired = ck::StepProceduralSurfaceMotionPaced(MakeSettings(), FVector::ForwardVector, 180.0f, Step,
            Cast, NoFeet, {}, Poses[Case], Guarded, GuardedState, {},
            TOptional<ck::FProceduralSurfaceContactCorrectionAdmission>{ck::FProceduralSurfaceContactCorrectionAdmission{Admit}},
            TArrayView<const FLanding>{InvalidLandings});
        TestTrue(TEXT("Unsafe pending evidence retires while FTZ-safe zero-axis remains active; both commit one actual Full trial without admission callback"),
            Retired.Get_WithdrawnLandingIndices().Num() == (ExpectWithdraw ? 1 : 0)
            && (NOT ExpectWithdraw || Retired.Get_WithdrawnLandingIndices()[0] == 0)
            && Retired.Get_Trials() == 1 && Retired.Get_Scale() == 1.0f && NOT Retired.Get_PhysicalOverride()
            && Calls == 0 && Rays == DirectRays && Guarded.Equals(Direct, 0.0) && SameState(GuardedState, DirectState));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkCommittedLandingCorrectionTest,
    "Ck.ProceduralAnimation.SurfaceMotion.CommittedLanding.ContactCorrectionRequiresLandingFit",
    ck::tests::kCkUnitTestFlags)

auto
    FCkCommittedLandingCorrectionTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_committed_landing_pace;
    const auto Start = FTransform{FVector{0.0, 0.0, 75.0}};
    const auto Initial = MakeState();
    const auto Settings = MakeSettings().Set_Clearance(65.0f);
    const auto Anchors = TArray<FAnchor>{{FVector{-FMath::Sqrt(100.0 * 100.0 - 25.0), 0.0, 70.0}, FVector::ZeroVector, 100.0f}};
    auto ExpectedZero = Start;
    auto ExpectedState = Initial;
    ck::StepProceduralSurfaceMotion(Settings, FVector::ForwardVector, 100000.0f, Step, CastFloor, NoFeet, ExpectedZero, ExpectedState, 0.0f);
    auto BaselineRays = 0;
    for (const auto ConstrainCorrection : {false, true})
    {
        const auto Landings = ConstrainCorrection
            ? TArray<FLanding>{{FVector{0.0, 0.0, 175.0}, FVector::ZeroVector, 100.0f, 120.0f}}
            : TArray<FLanding>{};
        auto Calls = 0;
        const auto Admit = [&](const FTransform&) { ++Calls; return true; };
        auto Rays = 0;
        const auto Cast = [&](const FVector& A, const FVector& B) { ++Rays; return CastFloor(A, B); };
        auto Body = Start;
        auto State = Initial;
        const auto Outcome = ck::StepProceduralSurfaceMotionPaced(Settings, FVector::ForwardVector, 100000.0f, Step,
            Cast, NoFeet, TArrayView<const FAnchor>{Anchors}, {}, Body, State, {},
            TOptional<ck::FProceduralSurfaceContactCorrectionAdmission>{ck::FProceduralSurfaceContactCorrectionAdmission{Admit}},
            TArrayView<const FLanding>{Landings});
        TestTrue(TEXT("Existing eight trials, zero pace and planted physical ownership are unchanged"),
            Outcome.Get_Trials() == 8 && Outcome.Get_Scale() == 0.0f && NOT Outcome.Get_PhysicalOverride()
            && Outcome.Get_WithdrawnLandingIndices().IsEmpty());
        if (ConstrainCorrection)
        {
            TestTrue(TEXT("A cached correction that loses a landing rejects before collision admission and preserves zero atomically"),
                Calls == 0 && Body.Equals(ExpectedZero, 0.0) && SameState(State, ExpectedState) && Rays == BaselineRays);
        }
        else
        {
            BaselineRays = Rays;
            TestTrue(TEXT("Positive control actually offers and publishes the existing contact-only descent"),
                Calls == 1 && Body.GetLocation().Z < ExpectedZero.GetLocation().Z);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkCommittedLandingCompatibilityTest,
    "Ck.ProceduralAnimation.SurfaceMotion.CommittedLanding.EmptyCompatibilityAndFullFastPath",
    ck::tests::kCkUnitTestFlags)

auto
    FCkCommittedLandingCompatibilityTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_committed_landing_pace;
    const auto Start = FTransform{FVector{0.0, 0.0, 75.0}};
    for (const auto ConstrainPlant : {false, true})
    {
        const auto Anchors = ConstrainPlant
            ? TArray<FAnchor>{{FVector{-99.0, 0.0, 75.0}, FVector::ZeroVector, 100.0f}} : TArray<FAnchor>{};
        auto Omitted = Start;
        auto Explicit = Start;
        auto OmittedState = MakeState();
        auto ExplicitState = OmittedState;
        auto RaysA = 0;
        auto RaysB = 0;
        const auto CastA = [&](const FVector& A, const FVector& B) { ++RaysA; return CastFloor(A, B); };
        const auto CastB = [&](const FVector& A, const FVector& B) { ++RaysB; return CastFloor(A, B); };
        const auto A = ck::StepProceduralSurfaceMotionPaced(MakeSettings(), FVector::ForwardVector, 180.0f, Step,
            CastA, NoFeet, TArrayView<const FAnchor>{Anchors}, {}, Omitted, OmittedState);
        const auto B = ck::StepProceduralSurfaceMotionPaced(MakeSettings(), FVector::ForwardVector, 180.0f, Step,
            CastB, NoFeet, TArrayView<const FAnchor>{Anchors}, {}, Explicit, ExplicitState, {}, {}, {});
        TestTrue(TEXT("Default-empty and explicit-empty keep identical body, complete floor state, pace and query counts"),
            Omitted.Equals(Explicit, 0.0) && SameState(OmittedState, ExplicitState) && RaysA == RaysB
            && A.Get_Trials() == B.Get_Trials() && A.Get_Scale() == B.Get_Scale()
            && A.Get_PhysicalOverride() == B.Get_PhysicalOverride()
            && A.Get_AttemptedStanceSpeed() == B.Get_AttemptedStanceSpeed()
            && A.Get_RejectedAnchorIndices() == B.Get_RejectedAnchorIndices()
            && A.Get_WithdrawnLandingIndices().IsEmpty() && B.Get_WithdrawnLandingIndices().IsEmpty());
    }
    const auto Loose = TArray<FLanding>{{FVector{0.0, 0.0, 75.0}, FVector::ZeroVector, 100.0f, 120.0f}};
    auto Body = Start;
    auto State = MakeState();
    const auto Full = ck::StepProceduralSurfaceMotionPaced(MakeSettings(), FVector::ForwardVector, 180.0f, Step,
        CastFloor, NoFeet, {}, {}, Body, State, {}, {}, TArrayView<const FLanding>{Loose});
    TestTrue(TEXT("A full endpoint fitting landings without planted anchors keeps the one-trial fast path"),
        Full.Get_Trials() == 1 && Full.Get_Scale() == 1.0f && Full.Get_WithdrawnLandingIndices().IsEmpty()
        && Body.GetLocation().X > Start.GetLocation().X);
    const auto IntoReach = TArray<FLanding>{{FVector{101.0, 0.0, 75.0}, FVector::ZeroVector, 100.0f, 120.0f}};
    Body = Start;
    State = MakeState();
    const auto Entered = ck::StepProceduralSurfaceMotionPaced(MakeSettings(), FVector::ForwardVector, 180.0f, Step,
        CastFloor, NoFeet, {}, {}, Body, State, {}, {}, TArrayView<const FLanding>{IntoReach});
    TestTrue(TEXT("Start math safety does not impose a start-reach gate when the actual Full endpoint enters admission reach"),
        NOT ck::Get_IsProceduralSurfaceLandingReachValid(Start, {}, IntoReach[0])
        && ck::Get_IsProceduralSurfaceLandingReachValid(Body, {}, IntoReach[0])
        && Entered.Get_Trials() == 1 && Entered.Get_Scale() == 1.0f && Entered.Get_WithdrawnLandingIndices().IsEmpty());
    return true;
}

#endif
