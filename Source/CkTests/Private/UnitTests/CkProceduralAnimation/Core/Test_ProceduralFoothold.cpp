#include "CkProceduralAnimation/Core/CkProceduralFoothold.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_foothold
{
    constexpr auto Reach = 140.0f;
    const auto Ideal = FVector{0.0, 0.0, 0.0};
    const auto Plant = FVector{-50.0, 0.0, 0.0};

    auto
        MakeLevel(
            const FVector& InPosition,
            ck::EProceduralFootholdVerdict InVerdict = ck::EProceduralFootholdVerdict::Usable)
        -> ck::FProceduralFootholdCandidate
    {
        return ck::FProceduralFootholdCandidate{InPosition, FVector::UpVector, ck::EProceduralFootholdSource::Ring, InVerdict};
    }

    auto
        Select(
            TArrayView<const ck::FProceduralFootholdCandidate> InCandidates,
            bool InPlanted,
            const ck::FProceduralFootholdSettings& InSettings = {},
            const FVector& InIdeal = Ideal)
        -> int32
    {
        return ck::SelectProceduralFoothold(InCandidates, InIdeal, Plant, InPlanted, Reach, InSettings);
    }

    auto
        Get_AreSame(
            TArrayView<const ck::FProceduralFootholdCandidate> InA,
            TArrayView<const ck::FProceduralFootholdCandidate> InB)
        -> bool
    {
        if (InA.Num() != InB.Num())
        { return false; }

        for (auto Index = 0; Index < InA.Num(); ++Index)
        {
            const auto& A = InA[Index];
            const auto& B = InB[Index];
            const auto PositionsMatch = A.Get_Position().ContainsNaN() == B.Get_Position().ContainsNaN()
                && (A.Get_Position().ContainsNaN() || A.Get_Position() == B.Get_Position());
            if (NOT PositionsMatch || A.Get_Normal() != B.Get_Normal() || A.Get_Source() != B.Get_Source()
                || A.Get_Verdict() != B.Get_Verdict())
            { return false; }
        }
        return true;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdSelectsNearestUsableTest,
    "Ck.ProceduralAnimation.Foothold.SelectsNearestUsable",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdSelectsNearestUsableTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto Swinging = false;
    const auto Nearest = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(FVector{10.0, 0.0, 0.0}), MakeLevel(FVector{0.0, 30.0, 0.0}), MakeLevel(FVector{-50.0, 0.0, 0.0})};
    TestEqual(TEXT("Three level candidates at 10, 30 and 50 cm pick the nearest"), Select(Nearest, Swinging), 0);

    const auto Shuffled = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(FVector{0.0, 30.0, 0.0}), MakeLevel(FVector{-50.0, 0.0, 0.0}), MakeLevel(FVector{10.0, 0.0, 0.0})};
    TestEqual(TEXT("The pick follows the distance, not the order"), Select(Shuffled, Swinging), 2);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdSkipsEveryNonUsableVerdictTest,
    "Ck.ProceduralAnimation.Foothold.SkipsEveryNonUsableVerdict",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdSkipsEveryNonUsableVerdictTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto Swinging = false;
    const auto Close = FVector{1.0, 0.0, 0.0};
    const auto Candidates = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(Close, ck::EProceduralFootholdVerdict::Miss),
        MakeLevel(Close, ck::EProceduralFootholdVerdict::Unreachable),
        MakeLevel(Close, ck::EProceduralFootholdVerdict::TooSteep),
        MakeLevel(Close, ck::EProceduralFootholdVerdict::Occluded),
        MakeLevel(FVector{60.0, 0.0, 0.0})};
    TestEqual(TEXT("Only the Usable candidate 60 cm away is chosen over four rejected ones at 1 cm"), Select(Candidates, Swinging), 4);

    const auto NoneUsable = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(Close, ck::EProceduralFootholdVerdict::Miss), MakeLevel(Close, ck::EProceduralFootholdVerdict::Occluded)};
    TestEqual(TEXT("Without a Usable candidate there is no pick"), Select(NoneUsable, Swinging), static_cast<int32>(INDEX_NONE));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdLevelEnoughGateTest,
    "Ck.ProceduralAnimation.Foothold.LevelEnoughGateOnEitherSide",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdLevelEnoughGateTest::
    RunTest(const FString&)
    -> bool
{
    const auto Tilt = FMath::DegreesToRadians(70.0);
    const auto SeventyDegrees = FVector{FMath::Sin(Tilt), 0.0, FMath::Cos(Tilt)};
    TestFalse(TEXT("A normal 70 degrees from up exceeds a 60 degree limit"), ck::Get_IsFootholdLevelEnough(SeventyDegrees, 60.0f));
    TestTrue(TEXT("A normal 70 degrees from up is within a 75 degree limit"), ck::Get_IsFootholdLevelEnough(SeventyDegrees, 75.0f));
    TestTrue(TEXT("A vertical face is within the default 90 degree limit"), ck::Get_IsFootholdLevelEnough(FVector::ForwardVector, 90.0f));
    TestFalse(TEXT("An overhang facing down is beyond 90 degrees"), ck::Get_IsFootholdLevelEnough(FVector{1.0, 0.0, -0.2}, 90.0f));
    TestFalse(TEXT("A zero normal is never level"), ck::Get_IsFootholdLevelEnough(FVector::ZeroVector, 90.0f));

    const auto NaN = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A NaN normal is never level"), ck::Get_IsFootholdLevelEnough(FVector{0.0, 0.0, NaN}, 90.0f));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdSlopeWeightTest,
    "Ck.ProceduralAnimation.Foothold.SlopeWeightPrefersLevelOverVertical",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdSlopeWeightTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto Swinging = false;
    const auto Vertical = ck::FProceduralFootholdCandidate{FVector{10.0, 0.0, 0.0}, FVector::BackwardVector,
        ck::EProceduralFootholdSource::Inward, ck::EProceduralFootholdVerdict::Usable};
    const auto Level = MakeLevel(FVector{0.0, 30.0, 0.0});
    const auto Candidates = TArray<ck::FProceduralFootholdCandidate>{Vertical, Level};

    const auto Defaults = ck::FProceduralFootholdSettings{};
    const auto VerticalCost = ck::ComputeProceduralFootholdCost(Vertical, Ideal, Plant, Swinging, Reach, Defaults);
    const auto LevelCost = ck::ComputeProceduralFootholdCost(Level, Ideal, Plant, Swinging, Reach, Defaults);
    TestTrue(FString::Printf(TEXT("The vertical face 10 cm away costs 10/140 + 0.5 (got %.4f)"), VerticalCost),
        FMath::IsNearlyEqual(VerticalCost, 10.0 / Reach + 0.5, 1.0e-4));
    TestTrue(FString::Printf(TEXT("The level candidate 30 cm away costs 30/140 (got %.4f)"), LevelCost),
        FMath::IsNearlyEqual(LevelCost, 30.0 / Reach, 1.0e-4));
    TestEqual(TEXT("With the default slope weight the level candidate wins"), Select(Candidates, Swinging), 1);

    const auto Flat = ck::FProceduralFootholdSettings{}.Set_SlopeWeight(0.0f);
    TestEqual(TEXT("Without a slope weight the nearer vertical face wins"), Select(Candidates, Swinging, Flat), 0);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdContinuityTest,
    "Ck.ProceduralAnimation.Foothold.ContinuityPrefersPlantHeight",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdContinuityTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto Planted = true;
    constexpr auto Swinging = false;
    const auto IdealBetween = FVector{0.0, 0.0, 20.0};
    const auto AtPlantHeight = MakeLevel(FVector{0.0, 0.0, 0.0});
    const auto Higher = MakeLevel(FVector{0.0, 0.0, 40.0});
    const auto PlantHeightFirst = TArray<ck::FProceduralFootholdCandidate>{AtPlantHeight, Higher};
    const auto HigherFirst = TArray<ck::FProceduralFootholdCandidate>{Higher, AtPlantHeight};

    TestEqual(TEXT("Planted: the candidate at the plant's height wins when listed first"),
        Select(PlantHeightFirst, Planted, {}, IdealBetween), 0);
    TestEqual(TEXT("Planted: the candidate at the plant's height wins when listed second"),
        Select(HigherFirst, Planted, {}, IdealBetween), 1);
    TestEqual(TEXT("Swinging: equal distances tie and the lower index wins"), Select(PlantHeightFirst, Swinging, {}, IdealBetween), 0);
    TestEqual(TEXT("Swinging: equal distances tie and the lower index wins in either order"), Select(HigherFirst, Swinging, {}, IdealBetween), 0);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdTransitionSlopeReferenceTest,
    "Ck.ProceduralAnimation.Foothold.TransitionSlopeReference",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdTransitionSlopeReferenceTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    // Recorded intact Spider approach: the desired ledge top is occluded, while both its face and the approach floor
    // are already usable. Ranking from the intended top with a transition normal must not keep preferring the floor.
    constexpr auto SpiderReach = 220.0f;
    constexpr auto Planted = true;
    const auto Goal = FVector{-115.906, 51.219, 150.0};
    const auto OldPlant = FVector{-161.820, 107.406, 0.0};
    const auto Face = ck::FProceduralFootholdCandidate{FVector{-150.0, 41.812, 134.804}, FVector::BackwardVector,
        ck::EProceduralFootholdSource::Front, ck::EProceduralFootholdVerdict::Usable};
    const auto Floor = MakeLevel(FVector{-169.484, 36.438, 0.0});
    const auto Candidates = TArray<ck::FProceduralFootholdCandidate>{Face, Floor};
    const auto Before = Candidates;
    const auto Defaults = ck::FProceduralFootholdSettings{};
    const auto Transition = ck::FProceduralFootholdSettings{}.Set_SlopeUp(FVector::UpVector + FVector::BackwardVector);
    TestEqual(TEXT("Control: the default floor reference retains the old floor preference"),
        ck::SelectProceduralFoothold(Candidates, Goal, OldPlant, Planted, SpiderReach, Defaults), 1);
    TestEqual(TEXT("The admitted approach face wins with the transition reference"),
        ck::SelectProceduralFoothold(Candidates, Goal, OldPlant, Planted, SpiderReach, Transition), 0);
    TestTrue(TEXT("Ranking does not mutate candidate positions, normals or verdicts"), Get_AreSame(Candidates, Before));

    const auto Rejected = TArray<ck::FProceduralFootholdCandidate>{
        ck::FProceduralFootholdCandidate{Face.Get_Position(), Face.Get_Normal(), Face.Get_Source(),
            ck::EProceduralFootholdVerdict::TooSteep}, Floor};
    TestEqual(TEXT("The slope reference cannot promote a face rejected by the unchanged angle gate"),
        ck::SelectProceduralFoothold(Rejected, Goal, OldPlant, Planted, SpiderReach, Transition), 1);
    TestFalse(TEXT("The current-support admission still rejects a vertical face below a 90-degree allowance"),
        ck::Get_IsFootholdLevelEnough(Face.Get_Normal(), 60.0f));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdRejectsMalformedInputTest,
    "Ck.ProceduralAnimation.Foothold.RejectsMalformedInput",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdRejectsMalformedInputTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto Swinging = false;
    const auto Settings = ck::FProceduralFootholdSettings{};
    const auto WellFormed = TArray<ck::FProceduralFootholdCandidate>{MakeLevel(FVector{10.0, 0.0, 0.0})};
    TestEqual(TEXT("Positive control: a well-formed input yields a pick"), Select(WellFormed, Swinging), 0);

    TestEqual(TEXT("An empty view has no pick"),
        ck::SelectProceduralFoothold({}, Ideal, Plant, Swinging, Reach, Settings), static_cast<int32>(INDEX_NONE));

    const auto WellFormedCopy = WellFormed;
    TestEqual(TEXT("A zero reach has no pick"),
        ck::SelectProceduralFoothold(WellFormed, Ideal, Plant, Swinging, 0.0f, Settings), static_cast<int32>(INDEX_NONE));
    TestTrue(TEXT("A rejected zero reach leaves the candidates unchanged"), Get_AreSame(WellFormed, WellFormedCopy));

    const auto NaN = std::numeric_limits<double>::quiet_NaN();
    const auto WithNaN = TArray<ck::FProceduralFootholdCandidate>{MakeLevel(FVector{10.0, 0.0, 0.0}), MakeLevel(FVector{NaN, 0.0, 0.0})};
    const auto WithNaNCopy = WithNaN;
    TestEqual(TEXT("A NaN position has no pick, even beside a well-formed candidate"), Select(WithNaN, Swinging),
        static_cast<int32>(INDEX_NONE));
    TestTrue(TEXT("A rejected NaN position leaves the candidates unchanged"), Get_AreSame(WithNaN, WithNaNCopy));

    const auto BadReferences = TArray<FVector>{FVector::ZeroVector, FVector{NaN, 0.0, 1.0}};
    for (const auto& Reference : BadReferences)
    {
        const auto InvalidSettings = ck::FProceduralFootholdSettings{}.Set_SlopeUp(Reference);
        TestEqual(TEXT("A zero or non-finite slope reference has no pick"), Select(WellFormed, Swinging, InvalidSettings),
            static_cast<int32>(INDEX_NONE));
        TestEqual(TEXT("A malformed slope reference has a nonselectable direct cost"),
            ck::ComputeProceduralFootholdCost(WellFormed[0], Ideal, Plant, Swinging, Reach, InvalidSettings),
            TNumericLimits<double>::Max());
        TestTrue(TEXT("A rejected slope reference leaves the candidates unchanged"), Get_AreSame(WellFormed, WellFormedCopy));
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdRejectsInboardCandidatesTest,
    "Ck.ProceduralAnimation.Foothold.RejectsInboardCandidates",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdRejectsInboardCandidatesTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto InboardTolerance = 0.1f * Reach;
    const auto Outboard = FVector{0.0, 1.0, 0.0};
    const auto FarInboard = FVector{0.0, -20.0, -30.0};
    const auto NearInboard = FVector{0.0, -10.0, -30.0};
    const auto Outward = FVector{0.0, 40.0, -30.0};

    TestFalse(TEXT("A point 20 cm inboard of the hip, beyond the 14 cm tolerance, is off the leg's own side"),
        ck::Get_IsFootholdOnOwnSide(FarInboard, Outboard, InboardTolerance));
    TestTrue(TEXT("A point 10 cm inboard, within the tolerance, is on it"),
        ck::Get_IsFootholdOnOwnSide(NearInboard, Outboard, InboardTolerance));
    TestTrue(TEXT("A point outboard of the hip is on it"), ck::Get_IsFootholdOnOwnSide(Outward, Outboard, InboardTolerance));
    TestTrue(TEXT("An unnormalised outboard direction reads the same"),
        ck::Get_IsFootholdOnOwnSide(NearInboard, Outboard * 5.0, InboardTolerance)
        && NOT ck::Get_IsFootholdOnOwnSide(FarInboard, Outboard * 5.0, InboardTolerance));
    TestTrue(TEXT("A leg with no lateral rest direction has no own side: every point is on it"),
        ck::Get_IsFootholdOnOwnSide(FarInboard, FVector::ZeroVector, InboardTolerance));
    TestFalse(TEXT("A non-finite point is on no side"),
        ck::Get_IsFootholdOnOwnSide(FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, Outboard, InboardTolerance));

    constexpr auto Swinging = false;
    const auto Candidates = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(FVector{0.0, 5.0, 0.0}, ck::EProceduralFootholdVerdict::Inboard),
        MakeLevel(FVector{0.0, 40.0, 0.0})};
    TestEqual(TEXT("An Inboard candidate is never chosen, however near the ideal"), Select(Candidates, Swinging), 1);

    const auto OnlyInboard = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(FVector{0.0, 5.0, 0.0}, ck::EProceduralFootholdVerdict::Inboard)};
    TestEqual(TEXT("Only Inboard candidates choose nothing"), Select(OnlyInboard, Swinging), int32{INDEX_NONE});

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdRejectsUnderBodyCandidatesTest,
    "Ck.ProceduralAnimation.Foothold.RejectsUnderBodyCandidates",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdRejectsUnderBodyCandidatesTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto MaxHipLateral = 30.0f;
    constexpr auto RestDrop = 60.0f;
    constexpr auto Margin = 5.0f;
    constexpr auto DeepBelowHip = 40.0f;

    TestTrue(TEXT("A point 10 cm from the centreline, 40 cm below the hip, is under the body"),
        ck::Get_IsFootholdUnderBody(10.0f, DeepBelowHip, MaxHipLateral, RestDrop, Margin));
    TestTrue(TEXT("The lateral bound is symmetric"), ck::Get_IsFootholdUnderBody(-10.0f, DeepBelowHip, MaxHipLateral, RestDrop, Margin));
    TestTrue(TEXT("Just inside the widest hip less the margin (24 cm) is under the body"),
        ck::Get_IsFootholdUnderBody(24.0f, DeepBelowHip, MaxHipLateral, RestDrop, Margin));
    TestFalse(TEXT("Just outside it (26 cm) is not"), ck::Get_IsFootholdUnderBody(26.0f, DeepBelowHip, MaxHipLateral, RestDrop, Margin));
    TestTrue(TEXT("Just deeper than a quarter rest drop below the hip (16 cm) is under the body"),
        ck::Get_IsFootholdUnderBody(10.0f, 16.0f, MaxHipLateral, RestDrop, Margin));
    TestFalse(TEXT("Just shallower (14 cm) is not: a foothold near the hip's height is beside the body"),
        ck::Get_IsFootholdUnderBody(10.0f, 14.0f, MaxHipLateral, RestDrop, Margin));
    TestFalse(TEXT("A point above the hip is not under the body"),
        ck::Get_IsFootholdUnderBody(10.0f, -20.0f, MaxHipLateral, RestDrop, Margin));
    TestFalse(TEXT("A body whose hips all lie on its centreline has no width to be under"),
        ck::Get_IsFootholdUnderBody(0.0f, DeepBelowHip, 0.0f, RestDrop, Margin));
    TestFalse(TEXT("A non-finite input is not under the body"),
        ck::Get_IsFootholdUnderBody(std::numeric_limits<float>::quiet_NaN(), DeepBelowHip, MaxHipLateral, RestDrop, Margin));

    constexpr auto Swinging = false;
    const auto Candidates = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(FVector{0.0, 5.0, 0.0}, ck::EProceduralFootholdVerdict::UnderBody),
        MakeLevel(FVector{0.0, 40.0, 0.0})};
    TestEqual(TEXT("An UnderBody candidate is never chosen, however near the ideal"), Select(Candidates, Swinging), 1);

    const auto OnlyUnderBody = TArray<ck::FProceduralFootholdCandidate>{
        MakeLevel(FVector{0.0, 5.0, 0.0}, ck::EProceduralFootholdVerdict::UnderBody)};
    TestEqual(TEXT("Only UnderBody candidates choose nothing"), Select(OnlyUnderBody, Swinging), int32{INDEX_NONE});

    {
        const auto Body = FVector{-292.540385888, 429.817898052, 252.723631190};
        const auto AxisX = FVector{-0.666425158, -0.745571934, 0.0};
        const auto Lateral = FVector{0.745571934, -0.666425158, 0.0};
        const auto Hip = Body + AxisX * 45.0 + Lateral * 28.0;
        const auto Outboard = (AxisX * 5.0 + Lateral * 27.0).GetSafeNormal();
        const auto Face = FVector{-289.125, 410.0, 107.8};
        const auto Radius = 0.3f * 100.0f;
        const auto BodyBoundary = static_cast<double>(28.0f) - 5.0f + 0.1;
        const auto OwnSideMinimum = -static_cast<double>(0.1f * 100.0f) + 0.1;
        const auto QueryPoint = ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, FVector{0.0, -1.0, 0.0},
            FVector::UpVector, Body, Hip, Lateral, Outboard, BodyBoundary, OwnSideMinimum, Radius);
        TestTrue(TEXT("The measured failed face permits one jointly bounded query proposal"), QueryPoint.IsSet());
        if (NOT QueryPoint.IsSet())
        { return false; }
        TestTrue(TEXT("Both body and own-side constraints choose the joint solution, not the failing body-only point"),
            QueryPoint->Equals(FVector{-270.131589252, 414.500000477, 107.8}, 1.0e-6)
                && QueryPoint->X > -275.249274058 && QueryPoint->Z == Face.Z
                && FVector::Dist(QueryPoint.GetValue(), Face) <= Radius);
        TestTrue(TEXT("The low query is not falsely required to fit native landing force reach"),
            FVector::Dist(QueryPoint.GetValue(), Hip) > 0.92f * 100.0f);
        TestTrue(TEXT("The proposed query preserves the original body's and leg's admission-side margins"),
            FVector::DotProduct(QueryPoint.GetValue() - Body, Lateral) >= BodyBoundary - 1.0e-9
                && FVector::DotProduct(QueryPoint.GetValue() - Hip, Outboard) >= OwnSideMinimum - 1.0e-9);
        const auto Origin = FVector{120000.0, 392000.0, 600.0};
        const auto Translated = ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face + Origin, FVector{0.0, -1.0, 0.0},
            FVector::UpVector, Body + Origin, Hip + Origin, Lateral, Outboard, BodyBoundary, OwnSideMinimum, Radius);
        TestTrue(TEXT("Joint placement is independent of the fixture's world translation"),
            Translated.IsSet() && (Translated.GetValue() - Origin).Equals(QueryPoint.GetValue(), 1.0e-7));
        const auto Rotation = FQuat{FVector::ForwardVector, UE_DOUBLE_HALF_PI};
        const auto Rotated = ck::Get_ProceduralFootholdFailedFaceQueryPoint(Rotation.RotateVector(Face),
            Rotation.RotateVector(FVector{0.0, -1.0, 0.0}), Rotation.RotateVector(FVector::UpVector),
            Rotation.RotateVector(Body), Rotation.RotateVector(Hip), Rotation.RotateVector(Lateral),
            Rotation.RotateVector(Outboard), BodyBoundary, OwnSideMinimum, Radius);
        TestTrue(TEXT("The query follows the current support frame rather than an authored world height"),
            Rotated.IsSet() && Rotated->Equals(Rotation.RotateVector(QueryPoint.GetValue()), 1.0e-6));
        const auto Parallel = ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector::ZeroVector,
            FVector{0.0, -1.0, 0.0}, FVector::UpVector, FVector::ZeroVector, FVector::ZeroVector,
            FVector::RightVector, FVector::RightVector, 1.0, 1.0, 10.0f);
        TestTrue(TEXT("Satisfied zero-coefficient constraints retain the unique nearest zero tangent offset"),
            Parallel.IsSet() && Parallel->Equals(FVector{0.0, 1.5, 0.0}, 1.0e-6));
        TestFalse(TEXT("An unsatisfied zero-coefficient body inequality cannot be repaired along this line"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector::ZeroVector, FVector{0.0, -1.0, 0.0},
                FVector::UpVector, FVector::ZeroVector, FVector::ZeroVector, FVector::RightVector,
                FVector::RightVector, 2.0, 1.0, 10.0f).IsSet());
        TestFalse(TEXT("An unsatisfied zero-coefficient own-side inequality also rejects"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector::ZeroVector, FVector{0.0, -1.0, 0.0},
                FVector::UpVector, FVector::ZeroVector, FVector::ZeroVector, FVector::RightVector,
                FVector::RightVector, 1.0, 2.0, 10.0f).IsSet());
        const auto Negative = ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector::ZeroVector,
            FVector::RightVector, FVector::UpVector, FVector{-2.0, 0.0, 0.0}, FVector{2.0, 0.0, 0.0},
            FVector::ForwardVector, FVector::ForwardVector, 1.0, 0.0, 10.0f);
        TestTrue(TEXT("Negative coefficients clip the upper interval and choose its closest endpoint"),
            Negative.IsSet() && Negative->Equals(FVector{2.0, -1.5, 0.0}, 1.0e-6));
        TestFalse(TEXT("No smaller inset or second face is tried when the radius cannot contain the joint constraints"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, FVector{0.0, -1.0, 0.0}, FVector::UpVector,
                Body, Hip, Lateral, Outboard, BodyBoundary, OwnSideMinimum, 5.0f).IsSet());
        TestFalse(TEXT("A support-parallel normal supplies no horizontal face direction"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, FVector::UpVector, FVector::UpVector,
                Body, Hip, Lateral, Outboard, BodyBoundary, OwnSideMinimum, Radius).IsSet());
        TestFalse(TEXT("A leg without a meaningful own side supplies no proposal"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, FVector{0.0, -1.0, 0.0}, FVector::UpVector,
                Body, Hip, Lateral, FVector::ZeroVector, BodyBoundary, OwnSideMinimum, Radius).IsSet());
        TestFalse(TEXT("A malformed unit-normal prerequisite is rejected rather than normalized"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, FVector{0.0, -2.0, 0.0}, FVector::UpVector,
                Body, Hip, Lateral, Outboard, BodyBoundary, OwnSideMinimum, Radius).IsSet());
        TestFalse(TEXT("A non-finite linear bound rejects before interval arithmetic"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, FVector{0.0, -1.0, 0.0}, FVector::UpVector,
                Body, Hip, Lateral, Outboard, std::numeric_limits<double>::quiet_NaN(), OwnSideMinimum, Radius).IsSet());
        for (const auto InvalidRadius : {0.0f, -1.0f, 0.5f, std::numeric_limits<float>::infinity()})
        {
            TestFalse(TEXT("A malformed radius or inset larger than the entire budget rejects"),
                ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, FVector{0.0, -1.0, 0.0}, FVector::UpVector,
                    Body, Hip, Lateral, Outboard, BodyBoundary, OwnSideMinimum, InvalidRadius).IsSet());
        }
        auto InvalidPoint = FVector::ZeroVector;
        InvalidPoint.X = std::numeric_limits<double>::quiet_NaN();
        TestFalse(TEXT("A malformed native face is rejected before query construction"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(InvalidPoint, FVector{0.0, -1.0, 0.0}, FVector::UpVector,
                Body, Hip, Lateral, Outboard, BodyBoundary, OwnSideMinimum, Radius).IsSet());
        TestFalse(TEXT("Finite scalar subtraction overflow rejects without vector diagnostics"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector{1.0e308}, FVector{0.0, -1.0, 0.0}, FVector::UpVector,
                FVector{-1.0e308}, Hip, Lateral, Outboard, BodyBoundary, OwnSideMinimum, Radius).IsSet());
        TestFalse(TEXT("A finite point outside native float representation never becomes a native query"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector{1.0e308}, FVector{0.0, -1.0, 0.0}, FVector::UpVector,
                FVector{1.0e308}, FVector{1.0e308}, Lateral, Outboard, -1.0, -1.0, Radius).IsSet());
    }

    const auto Roots = ck::Get_ProceduralFootholdLineSphereOffsets(
        FVector{1.0, 0.0, 0.0}, FVector::ForwardVector, FVector::ZeroVector, 3.0);
    TestTrue(TEXT("A unit line exposes exactly the two scalar sphere-boundary offsets"),
        Roots.IsSet() && Roots->X == -4.0 && Roots->Y == 2.0);
    const auto NearUnit = FVector{1.00002, 0.0, 0.0};
    const auto NearUnitRoots = ck::Get_ProceduralFootholdLineSphereOffsets(
        FVector{1.0, 0.0, 0.0}, NearUnit, FVector::ZeroVector, 3.0);
    TestTrue(TEXT("A direction within unit tolerance still receives roots for its actual quadratic"), NearUnitRoots.IsSet());
    if (NOT NearUnitRoots.IsSet())
    { return false; }
    for (const auto Offset : {NearUnitRoots->X, NearUnitRoots->Y})
    {
        TestTrue(TEXT("Near-unit offsets lie on the actual sphere without normalizing away input geometry"),
            FMath::IsNearlyEqual((FVector{1.0, 0.0, 0.0} + NearUnit * Offset).Size(), 3.0, 1.0e-12));
    }
    const auto Tangent = ck::Get_ProceduralFootholdLineSphereOffsets(
        FVector{0.0, 3.0, 0.0}, FVector::ForwardVector, FVector::ZeroVector, 3.0);
    TestTrue(TEXT("A tangent retains its one repeated root without inventing another direction"),
        Tangent.IsSet() && Tangent->X == 0.0 && Tangent->Y == 0.0);
    const auto Lateral = FVector{0.6, 0.8, 0.0};
    const auto Direction = FVector{-0.8, 0.6, 0.0};
    const auto Center = Lateral * 2.0 + Direction;
    const auto Oblique = ck::Get_ProceduralFootholdLineSphereOffsets(
        FVector::ZeroVector, Direction, Center, 3.1);
    TestTrue(TEXT("An oblique joint-boundary line has two roots"), Oblique.IsSet());
    if (NOT Oblique.IsSet())
    { return false; }
    for (const auto Offset : {Oblique->X, Oblique->Y})
    {
        const auto Point = Direction * Offset;
        TestTrue(TEXT("Both roots retain the native plane and body boundary while clearing the radius sum plus margin"),
            Point.Z == 0.0 && FMath::Abs(FVector::DotProduct(Point, Lateral)) < 1.0e-12
                && FMath::IsNearlyEqual(FVector::Dist(Point, Center), 3.1, 1.0e-12));
    }
    TestFalse(TEXT("A line outside the sphere has no joint-boundary proposal"),
        ck::Get_ProceduralFootholdLineSphereOffsets(FVector{0.0, 4.0, 0.0},
            FVector::ForwardVector, FVector::ZeroVector, 3.0).IsSet());
    TestFalse(TEXT("A non-unit direction is not silently normalized into different geometry"),
        ck::Get_ProceduralFootholdLineSphereOffsets(FVector::ZeroVector,
            FVector{2.0, 0.0, 0.0}, FVector::ZeroVector, 3.0).IsSet());
    for (const auto Radius : {0.0, -1.0, std::numeric_limits<double>::infinity(), 1.0e308})
    {
        TestFalse(TEXT("Invalid or overflowing radius rejects before constructing scalar roots"),
            ck::Get_ProceduralFootholdLineSphereOffsets(FVector::ZeroVector,
                FVector::ForwardVector, FVector::ZeroVector, Radius).IsSet());
    }
    auto Invalid = FVector::ZeroVector;
    Invalid.X = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A non-finite point rejects without vector arithmetic"),
        ck::Get_ProceduralFootholdLineSphereOffsets(Invalid,
            FVector::ForwardVector, FVector::ZeroVector, 3.0).IsSet());
    TestFalse(TEXT("Finite coordinate subtraction overflow rejects before any vector construction"),
        ck::Get_ProceduralFootholdLineSphereOffsets(FVector{1.0e308, 0.0, 0.0},
            FVector::ForwardVector, FVector{-1.0e308, 0.0, 0.0}, 3.0).IsSet());
    TestFalse(TEXT("Finite squared-distance overflow rejects before any vector construction"),
        ck::Get_ProceduralFootholdLineSphereOffsets(FVector{1.0e200, 0.0, 0.0},
            FVector::ForwardVector, FVector::ZeroVector, 3.0).IsSet());

    {
        const auto CapturedBody = FVector{120454.60341785253, 392479.07876568858, 814.88400861650769};
        const auto CapturedHip = FVector{120466.47413296014, 392530.73228563479, 814.88400861650769};
        const auto CapturedLateral = FVector{-0.70915926470874258, 0.70504832265438055, 0.0};
        const auto CapturedOutboard = FVector{-0.56892181619126769, 0.82239161417273066, 0.0};
        const auto CapturedNormal = FVector::RightVector;
        const auto CapturedRadius = 30.000001907348633f;
        const auto Preferred = static_cast<double>(FMath::Max(1.0f, CapturedRadius * 0.15f));
        for (const auto Face : {FVector{120448.8359375, 392490.0, 658.5802001953125},
            FVector{120448.875, 392490.0, 658.7239990234375}})
        {
            const auto Point = ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, CapturedNormal, FVector::UpVector,
                CapturedBody, CapturedHip, CapturedLateral, CapturedOutboard, 23.100000000000001, -9.9000000000000004, CapturedRadius);
            TestTrue(TEXT("The captured 04 finite empty preferred slice has a bounded geometric alternative"), Point.IsSet());
            if (NOT Point.IsSet())
            { return false; }
            const auto Delta = Point.GetValue() - Face;
            const auto Inset = -FVector::DotProduct(Delta, CapturedNormal);
            TestTrue(TEXT("The represented alternative strictly retains every original geometric constraint"),
                NOT Point->ContainsNaN() && Inset >= 1.0 && Inset <= Preferred
                    && Delta.SizeSquared() <= static_cast<double>(CapturedRadius) * CapturedRadius
                    && FVector::DotProduct(Point.GetValue() - CapturedBody, CapturedLateral) >= 23.100000000000001
                    && FVector::DotProduct(Point.GetValue() - CapturedHip, CapturedOutboard) >= -9.9000000000000004
                    && FVector::DotProduct(Delta, FVector::UpVector) == 0.0
                    && FMath::Abs(Point->X) <= TNumericLimits<float>::Max()
                    && FMath::Abs(Point->Y) <= TNumericLimits<float>::Max()
                    && FMath::Abs(Point->Z) <= TNumericLimits<float>::Max());
            const auto Repeated = ck::Get_ProceduralFootholdFailedFaceQueryPoint(Face, CapturedNormal, FVector::UpVector,
                CapturedBody, CapturedHip, CapturedLateral, CapturedOutboard, 23.100000000000001, -9.9000000000000004, CapturedRadius);
            TestTrue(TEXT("The finite construction returns the same represented point deterministically"),
                Repeated.IsSet() && Repeated.GetValue() == Point.GetValue());
        }
        const auto OldSuccess = ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector::ZeroVector,
            FVector{0.0, -1.0, 0.0}, FVector::UpVector, FVector::ZeroVector, FVector::ZeroVector,
            FVector::RightVector, FVector::RightVector, 1.0, 1.0, 10.0f);
        TestTrue(TEXT("An existing preferred-slice success retains its exact represented output"),
            OldSuccess.IsSet() && OldSuccess.GetValue() == FVector{0.0, 1.5, 0.0});
        for (const auto InvalidBound : {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
        {
            TestFalse(TEXT("Non-finite input cannot enter geometric fallback"),
                ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector::ZeroVector, CapturedNormal, FVector::UpVector,
                    CapturedBody, CapturedHip, CapturedLateral, CapturedOutboard, InvalidBound, -9.9, CapturedRadius).IsSet());
        }
        TestFalse(TEXT("Non-finite interval-bound arithmetic rejects rather than entering empty-slice fallback"),
            ck::Get_ProceduralFootholdFailedFaceQueryPoint(FVector::ZeroVector, CapturedNormal, FVector::UpVector,
                FVector{0.0, -1.0e308, 0.0}, FVector::ZeroVector, CapturedLateral, CapturedOutboard,
                1.0e308, -9.9, CapturedRadius).IsSet());
        // Geometry alone does not establish a native terrain hit, landing admission or traversal progress.
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralFootholdFaceIdealCompetesOnCostTest,
    "Ck.ProceduralAnimation.Foothold.FaceIdealCompetesOnCost",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralFootholdFaceIdealCompetesOnCostTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_foothold;

    constexpr auto Swinging = false;
    // The ideal on a vertical face, at the ideal itself: it costs the slope term alone, 0.5 with the default weight.
    const auto FaceIdeal = ck::FProceduralFootholdCandidate{Ideal, FVector::BackwardVector, ck::EProceduralFootholdSource::Ideal,
        ck::EProceduralFootholdVerdict::Usable};
    const auto NearTop = MakeLevel(Ideal + FVector{0.4 * Reach, 0.0, 0.0});
    const auto FarTop = MakeLevel(Ideal + FVector{0.6 * Reach, 0.0, 0.0});

    TestEqual(TEXT("A level candidate at 0.4 of the reach beats the face ideal"),
        Select(TArray<ck::FProceduralFootholdCandidate>{FaceIdeal, NearTop}, Swinging), 1);
    TestEqual(TEXT("The face ideal beats a level candidate at 0.6 of the reach"),
        Select(TArray<ck::FProceduralFootholdCandidate>{FaceIdeal, FarTop}, Swinging), 0);

    return true;
}

#endif
