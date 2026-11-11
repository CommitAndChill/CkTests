#include "CkProceduralAnimation/Core/CkProceduralFoothold.h"
#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_procedural_recovery_boundary
{
    auto
        MakeQuery()
        -> ck::FProceduralFootholdUnderBodyBoundaryQuery
    {
        return {.BodyLocation = FVector::ZeroVector, .Hip = FVector{0.0, 20.0, 50.0},
            .Lateral = FVector::RightVector, .Outboard = FVector::RightVector, .BodyBoundary = 15.1,
            .SearchRadius = 20.0f, .ForceReach = 100.0f, .OwnSideTolerance = 10.0f,
            .FootContactRadius = 2.0f, .LegIndex = 0};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralRecoveryBoundaryOriginalGeometryTest,
    "Ck.ProceduralAnimation.Foothold.RecoveryBoundaryOriginalGeometry",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralRecoveryBoundaryOriginalGeometryTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_recovery_boundary;
    for (const auto Side : {-1.0, 1.0})
    {
        auto Query = MakeQuery();
        Query.Outboard *= Side;
        Query.Hip.Y *= Side;
        for (const auto Normal : {FVector::UpVector, FVector{0.6, 0.0, 0.8}, FVector{0.0, 0.6, 0.8}})
        {
            auto JointAttempted = false;
            auto TotalPoints = 0;
            const FVector Seeds[] = {FVector{2.0, Side * 10.0, 0.0}, FVector{-1.0, Side * 11.0, 0.0}};
            for (auto Index = 0; Index < 2; ++Index)
            {
                const auto& Seed = Seeds[Index];
                const auto Tangent = FVector::VectorPlaneProject(Query.Lateral, Normal.GetSafeNormal());
                const auto Expected = Seed + Tangent * ((Side * Query.BodyBoundary
                    - FVector::DotProduct(Seed - Query.BodyLocation, Query.Lateral)) / Tangent.SizeSquared());
                auto Points = 0;
                const auto Point = ck::Get_ProceduralFootholdUnderBodyBoundary(Query, Seed, Normal, JointAttempted, &Points);
                TestTrue(TEXT("Each seed retains the original boundary constructor's represented point"),
                    Point.IsSet() && Point.GetValue() == Expected);
                TestTrue(TEXT("The seed order and observed plane are preserved"), Point.IsSet()
                    && Point->X == Seed.X && FMath::Abs(FVector::DotProduct(Point.GetValue() - Seed, Normal)) < 1.0e-12);
                TestEqual(TEXT("An unreserved seed constructs exactly its base point"), Points, 1);
                TotalPoints += Points;
            }
            TestEqual(TEXT("Two seeds have exactly two base points"), TotalPoints, 2);
            TestFalse(TEXT("No reservation refinement was spent"), JointAttempted);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralRecoveryBoundaryReservationBatchTest,
    "Ck.ProceduralAnimation.Foothold.RecoveryBoundaryReservationBatch",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralRecoveryBoundaryReservationBatchTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_recovery_boundary;
    auto Query = MakeQuery();
    const auto Reservations = TArray<ck::FProceduralFootReservation>{
        ck::FProceduralFootReservation{FVector{0.0, Query.BodyBoundary, 0.0}, 3.0f, 1}};
    Query.Reservations = Reservations;
    auto JointAttempted = false;
    auto FirstPoints = 0;
    const auto First = ck::Get_ProceduralFootholdUnderBodyBoundary(Query, FVector{0.0, 10.0, 0.0},
        FVector::UpVector, JointAttempted, &FirstPoints);
    TestTrue(TEXT("A conflicting reservation spends the shared refinement"), JointAttempted);
    TestEqual(TEXT("One base and the two reservation roots exhaust three pure points"), FirstPoints, 3);
    TestTrue(TEXT("Equal seed distances choose the lesser signed line offset"),
        First.IsSet() && First->X > 0.0 && FMath::IsNearlyEqual(First->X, 5.1, 1.0e-12)
            && First->Y == Query.BodyBoundary && First->Z == 0.0);
    auto SecondPoints = 0;
    const auto Second = ck::Get_ProceduralFootholdUnderBodyBoundary(Query, FVector{1.0, 10.0, 0.0},
        FVector::UpVector, JointAttempted, &SecondPoints);
    TestTrue(TEXT("The second seed receives its own base without another reservation refinement"),
        Second.IsSet() && Second.GetValue() == FVector{1.0, Query.BodyBoundary, 0.0});
    TestEqual(TEXT("The shared two-seed batch has at most four pure points"), FirstPoints + SecondPoints, 4);
    TestTrue(TEXT("A geometry proposal retains the reservation rejection for native Search to judge"),
        Second.IsSet() && NOT ck::Get_IsProceduralFootContactAvailable(Second.GetValue(), Query.FootContactRadius,
            Query.LegIndex, Query.Reservations));

    JointAttempted = false;
    const auto Nearest = ck::Get_ProceduralFootholdUnderBodyBoundary(Query, FVector{2.0, 10.0, 0.0},
        FVector::UpVector, JointAttempted);
    TestTrue(TEXT("Unequal distances choose the root nearest the original seed"), Nearest.IsSet() && Nearest->X > 0.0);

    const auto BlockedTie = TArray<ck::FProceduralFootReservation>{Reservations[0],
        ck::FProceduralFootReservation{FVector{5.1, Query.BodyBoundary, 0.0}, 1.0f, 2}};
    Query.Reservations = BlockedTie;
    JointAttempted = false;
    auto BlockedPoints = 0;
    const auto ClearRoot = ck::Get_ProceduralFootholdUnderBodyBoundary(Query, FVector{0.0, 10.0, 0.0},
        FVector::UpVector, JointAttempted, &BlockedPoints);
    TestTrue(TEXT("Both roots retain all reservations while the first conflicting reservation owns construction"),
        ClearRoot.IsSet() && ClearRoot->X < 0.0 && ck::Get_IsProceduralFootContactAvailable(
            ClearRoot.GetValue(), Query.FootContactRadius, Query.LegIndex, Query.Reservations));
    TestEqual(TEXT("Another conflicting reservation adds no roots"), BlockedPoints, 3);

    Query.ForceReach = 1.0f;
    JointAttempted = false;
    const auto NoReachRoot = ck::Get_ProceduralFootholdUnderBodyBoundary(Query, FVector{0.0, 10.0, 0.0},
        FVector::UpVector, JointAttempted);
    TestTrue(TEXT("Unreachable reservation roots preserve the original rejected base, without another construction"),
        NoReachRoot.IsSet() && NoReachRoot.GetValue() == FVector{0.0, Query.BodyBoundary, 0.0} && JointAttempted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralRecoveryBoundaryDeclinesInvalidGeometryTest,
    "Ck.ProceduralAnimation.Foothold.RecoveryBoundaryDeclinesInvalidGeometry",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralRecoveryBoundaryDeclinesInvalidGeometryTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_recovery_boundary;
    const auto Seed = FVector{0.0, 10.0, 0.0};
    const auto Declines = [&](const ck::FProceduralFootholdUnderBodyBoundaryQuery& InQuery,
        const FVector& InSeed, const FVector& InNormal) -> bool
    {
        auto JointAttempted = false;
        return NOT ck::Get_ProceduralFootholdUnderBodyBoundary(InQuery, InSeed, InNormal, JointAttempted).IsSet();
    };
    auto Query = MakeQuery();
    TestTrue(TEXT("A normal parallel to the lateral axis has no boundary tangent"), Declines(Query, Seed, FVector::RightVector));
    Query.Outboard = FVector::ForwardVector;
    TestTrue(TEXT("Zero lateral side cannot acquire a boundary"), Declines(Query, Seed, FVector::UpVector));
    Query = MakeQuery();
    Query.SearchRadius = 5.0f;
    TestTrue(TEXT("The original radius rejects a point 5.1 cm from the paid seed"), Declines(Query, Seed, FVector::UpVector));
    for (const auto Radius : {0.0f, -1.0f, std::numeric_limits<float>::infinity()})
    {
        Query = MakeQuery(); Query.SearchRadius = Radius;
        TestTrue(TEXT("Nonpositive or non-finite search radius declines"), Declines(Query, Seed, FVector::UpVector));
    }
    Query = MakeQuery(); Query.FootContactRadius = 0.0f;
    TestTrue(TEXT("A zero foot radius preserves the original constructor's domain"), Declines(Query, Seed, FVector::UpVector));
    Query = MakeQuery(); Query.BodyBoundary = -0.1;
    TestTrue(TEXT("A nonpositive body boundary declines"), Declines(Query, Seed, FVector::UpVector));
    auto Invalid = Seed; Invalid.X = std::numeric_limits<double>::quiet_NaN();
    Query = MakeQuery();
    TestTrue(TEXT("A non-finite seed is rejected before vector arithmetic"), Declines(Query, Invalid, FVector::UpVector));
    Query.BodyLocation.X = -1.0e308;
    TestTrue(TEXT("Finite subtraction overflow is rejected before vector construction"),
        Declines(Query, FVector{1.0e308, 10.0, 0.0}, FVector::UpVector));
    const auto NativeLimit = static_cast<double>(TNumericLimits<float>::Max());
    TestTrue(TEXT("Native coordinate endpoints remain representable"),
        ck::Get_IsProceduralFootholdNativePointRepresentable(FVector{NativeLimit, -NativeLimit, 0.0}));
    TestFalse(TEXT("An unrepresentable finite point cannot reach a native boundary call"),
        ck::Get_IsProceduralFootholdNativePointRepresentable(FVector{NativeLimit * 2.0, 0.0, 0.0}));
    TestFalse(TEXT("A non-finite native point is rejected"), ck::Get_IsProceduralFootholdNativePointRepresentable(Invalid));
    return true;
}

#endif
