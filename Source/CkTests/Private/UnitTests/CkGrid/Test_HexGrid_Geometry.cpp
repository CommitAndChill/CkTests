#include "CkGrid/HexGridSystem/Geometry/CkHexGrid_Geometry_Utils.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <cmath>
#include <limits>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_hex_geometry
{
    using Geometry = UCk_Utils_HexGeometry_UE;
    using Failure = ECk_HexGeometryFailure;
    using Orientation = ECk_HexOrientation;
    using DomainKind = ECk_HexDomainKind;

    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto Sqrt3 = 1.7320508075688772935;
    constexpr auto Min = std::numeric_limits<int32>::min();
    constexpr auto Max = std::numeric_limits<int32>::max();
    constexpr auto Infinity = std::numeric_limits<double>::infinity();
    constexpr auto NaN = std::numeric_limits<double>::quiet_NaN();

    const auto Directions = TArray<FIntPoint>{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};
    const auto Sentinel = FCk_HexCoord{73, -29};

    auto
        TestCoordinate(
            FAutomationTestBase& InTest,
            const FString& InLabel,
            const FCk_HexCoord& InActual,
            const FCk_HexCoord& InExpected) -> void
    {
        InTest.TestEqual(InLabel + TEXT(" Q"), InActual.Get_Q(), InExpected.Get_Q());
        InTest.TestEqual(InLabel + TEXT(" R"), InActual.Get_R(), InExpected.Get_R());
    }

    auto
        TestPick(
            FAutomationTestBase& InTest,
            const FString& InLabel,
            const FCk_HexLayout& InLayout,
            const FVector2D& InPoint,
            const FCk_HexCoord& InExpected,
            Failure InExpectedFailure = Failure::None) -> FCk_HexCoord
    {
        auto Actual = Sentinel;
        auto Result = Failure::OutsideDomain;
        const auto Picked = Geometry::TryGet_CoordFromLocal(InPoint, InLayout, Actual, Result);
        InTest.TestEqual(InLabel + TEXT(" success"), Picked, InExpectedFailure == Failure::None);
        InTest.TestEqual(InLabel + TEXT(" failure"), Result, InExpectedFailure);
        TestCoordinate(InTest, InLabel, Actual, InExpected);
        return Actual;
    }

    auto
        Get_AnalyticCenter(
            const FIntPoint& InCell,
            Orientation InOrientation) -> FVector2D
    {
        const auto Q = static_cast<double>(InCell.X);
        const auto R = static_cast<double>(InCell.Y);
        const auto QAxis = InOrientation == Orientation::Pointy ? FVector2D{Sqrt3, 0.0} : FVector2D{1.5, Sqrt3 / 2.0};
        const auto RAxis = InOrientation == Orientation::Pointy ? FVector2D{Sqrt3 / 2.0, 1.5} : FVector2D{0.0, Sqrt3};
        return Q * QAxis + R * RAxis;
    }

    auto
        Get_AnalyticVertices(
            Orientation InOrientation) -> TArray<FVector2D>
    {
        const auto HalfSqrt3 = Sqrt3 / 2.0;
        if (InOrientation == Orientation::Pointy)
        {
            return {{HalfSqrt3, 0.5}, {0.0, 1.0}, {-HalfSqrt3, 0.5},
                    {-HalfSqrt3, -0.5}, {0.0, -1.0}, {HalfSqrt3, -0.5}};
        }
        return {{1.0, 0.0}, {0.5, HalfSqrt3}, {-0.5, HalfSqrt3},
                {-1.0, 0.0}, {-0.5, -HalfSqrt3}, {0.5, -HalfSqrt3}};
    }

    auto
        Get_PolygonContains(
            const TArray<FVector2D>& InVertices,
            const FVector2D& InPoint) -> bool
    {
        for (auto Index = 0; Index < InVertices.Num(); ++Index)
        {
            const auto& A = InVertices[Index];
            const auto& B = InVertices[(Index + 1) % InVertices.Num()];
            if (FVector2D::CrossProduct(B - A, InPoint - A) < 0.0)
            { return false; }
        }
        return true;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_HexGeometry_CoordinatesAndTurns, "Ck.HexGrid.Geometry.CoordinatesAndTurns", ck_test_hex_geometry::TestFlags)

auto
    FCkTest_HexGeometry_CoordinatesAndTurns::
    RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_geometry;

    const auto Anchor = FCk_HexCoord{-5, -3};
    TestEqual(TEXT("Negative Q"), Anchor.Get_Q(), -5);
    TestEqual(TEXT("Negative R"), Anchor.Get_R(), -3);
    TestEqual(TEXT("Derived S"), Anchor.Get_S(), int64{8});

    const auto Neighbors = TArray<FCk_HexCoord>{{-4, -3}, {-5, -2}, {-6, -2}, {-6, -3}, {-5, -4}, {-4, -4}};
    for (auto Index = 0; Index < Neighbors.Num(); ++Index)
    {
        const auto Label = FString::Printf(TEXT("Neighbor %d"), Index);
        auto Actual = Sentinel;
        auto Result = Failure::OutsideDomain;
        TestTrue(Label, Geometry::TryGet_Neighbor(Anchor, static_cast<ECk_HexDirection>(Index), Actual, Result));
        TestEqual(Label + TEXT(" result"), Result, Failure::None);
        TestCoordinate(*this, Label, Actual, Neighbors[Index]);
    }

    const auto Start = FCk_HexCoord{2, -5};
    const auto Turns = TArray<FCk_HexCoord>{{2, -5}, {5, -3}, {3, 2}, {-2, 5}, {-5, 3}, {-3, -2}};
    const auto Inverses = TArray<ECk_HexRotation>{ECk_HexRotation::Turn0, ECk_HexRotation::Turn5,
        ECk_HexRotation::Turn4, ECk_HexRotation::Turn3, ECk_HexRotation::Turn2, ECk_HexRotation::Turn1};
    auto Step = Start;
    for (auto Index = 0; Index < Turns.Num(); ++Index)
    {
        const auto Label = FString::Printf(TEXT("Turn %d"), Index);
        auto Actual = Sentinel;
        auto Result = Failure::OutsideDomain;
        TestTrue(Label, Geometry::TryRotate(Start, static_cast<ECk_HexRotation>(Index), Actual, Result));
        TestEqual(Label + TEXT(" result"), Result, Failure::None);
        TestCoordinate(*this, Label, Actual, Turns[Index]);

        auto Inverse = Sentinel;
        TestTrue(Label + TEXT(" inverse"), Geometry::TryRotate(Turns[Index], Inverses[Index], Inverse, Result));
        TestEqual(Label + TEXT(" inverse result"), Result, Failure::None);
        TestCoordinate(*this, Label + TEXT(" inverse"), Inverse, Start);

        auto NextStep = Sentinel;
        TestTrue(Label + TEXT(" positive step"), Geometry::TryRotate(Step, ECk_HexRotation::Turn1, NextStep, Result));
        TestEqual(Label + TEXT(" positive step result"), Result, Failure::None);
        TestCoordinate(*this, Label + TEXT(" positive step"), NextStep, Turns[(Index + 1) % Turns.Num()]);
        Step = NextStep;
    }
    TestCoordinate(*this, TEXT("Six positive turns return to the start"), Step, Start);

    auto Translated = Sentinel;
    auto Result = Failure::OutsideDomain;
    TestTrue(TEXT("Signed translation"), Geometry::TryTranslate(Anchor, Start, Translated, Result));
    TestEqual(TEXT("Signed translation result"), Result, Failure::None);
    TestCoordinate(*this, TEXT("Signed translation"), Translated, FCk_HexCoord{-3, -8});
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_HexGeometry_DistanceOracle, "Ck.HexGrid.Geometry.DistanceOracle", ck_test_hex_geometry::TestFlags)

auto
    FCkTest_HexGeometry_DistanceOracle::
    RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_geometry;

    constexpr auto Radius = 8;
    const auto Origin = FIntPoint{-4, 7};
    auto Frontier = TArray<FIntPoint>{Origin};
    auto Visited = TSet<FIntPoint>{};
    Visited.Add(Origin);
    const auto ExpectedShellSizes = TArray<int32>{1, 6, 12, 18, 24, 30, 36, 42, 48};

    for (auto Depth = 0; Depth <= Radius; ++Depth)
    {
        TestEqual(FString::Printf(TEXT("BFS shell %d size"), Depth), Frontier.Num(), ExpectedShellSizes[Depth]);
        auto NextFrontier = TArray<FIntPoint>{};
        for (const auto& Cell : Frontier)
        {
            const auto Label = FString::Printf(TEXT("BFS distance to (%d, %d)"), Cell.X, Cell.Y);
            auto Actual = int64{-1};
            auto Result = Failure::OutsideDomain;
            TestTrue(Label, Geometry::TryGet_Distance({Origin.X, Origin.Y}, {Cell.X, Cell.Y}, Actual, Result));
            TestEqual(Label + TEXT(" result"), Result, Failure::None);
            TestEqual(Label, Actual, static_cast<int64>(Depth));

            if (Depth == Radius)
            { continue; }

            for (const auto& Direction : Directions)
            {
                const auto NextCell = Cell + Direction;
                if (NOT Visited.Contains(NextCell))
                {
                    Visited.Add(NextCell);
                    NextFrontier.Add(NextCell);
                }
            }
        }
        Frontier = MoveTemp(NextFrontier);
    }
    TestEqual(TEXT("BFS unique cells"), Visited.Num(), 217);

    auto Actual = int64{-1};
    auto Result = Failure::OutsideDomain;
    TestTrue(TEXT("Distance between opposing int32 extremes"),
        Geometry::TryGet_Distance({Min, Min}, {Max, Max}, Actual, Result));
    TestEqual(TEXT("Extreme distance result"), Result, Failure::None);
    TestEqual(TEXT("Extreme distance uses int64"), Actual, int64{8589934590});
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_HexGeometry_DomainValidation, "Ck.HexGrid.Geometry.DomainValidation", ck_test_hex_geometry::TestFlags)

auto
    FCkTest_HexGeometry_DomainValidation::
    RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_geometry;

    const auto Center = FCk_HexCoord{-4, 9};
    const auto DiskCases = TArray<FIntPoint>{{0, 1}, {1, 7}, {2, 19}, {147, 65269}};
    auto Coordinates = TArray<FCk_HexCoord>{Sentinel};
    auto Result = Failure::OutsideDomain;
    for (const auto& Disk : DiskCases)
    {
        const auto Label = FString::Printf(TEXT("Disk radius %d"), Disk.X);
        TestTrue(Label, Geometry::TryResolve_Domain({DomainKind::Disk, Center, Disk.X, {}}, Coordinates, Result));
        TestEqual(Label + TEXT(" result"), Result, Failure::None);
        TestEqual(Label + TEXT(" count"), Coordinates.Num(), Disk.Y);
        TestTrue(Label + TEXT(" includes signed center"), Coordinates.Contains(Center));
    }

    const auto Expected = TArray<FCk_HexCoord>{{2, -2}, {-3, -1}, {-1, -1}, {-1, 0}, {1, 0}};
    const auto Explicit = FCk_HexDomain{DomainKind::Explicit, {}, 0,
        {Expected[4], Expected[1], Expected[3], Expected[0], Expected[2]}};
    TestTrue(TEXT("Explicit cells resolve"), Geometry::TryResolve_Domain(Explicit, Coordinates, Result));
    TestEqual(TEXT("Explicit domain result"), Result, Failure::None);
    TestTrue(TEXT("Explicit signed cells sorted by (r,q)"), Coordinates == Expected);
    TestFalse(TEXT("Explicit hole preserved"), Coordinates.Contains(FCk_HexCoord{}));

    const auto CheckRejected = [this, &Coordinates, &Result](const FString& InLabel, const FCk_HexDomain& InDomain,
        Failure InExpected)
    {
        Coordinates = {Sentinel};
        Result = Failure::None;
        TestFalse(InLabel, Geometry::TryResolve_Domain(InDomain, Coordinates, Result));
        TestEqual(InLabel + TEXT(" result"), Result, InExpected);
        TestTrue(InLabel + TEXT(" clears all cells"), Coordinates.IsEmpty());
    };

    CheckRejected(TEXT("Empty explicit domain"), {DomainKind::Explicit, {}, 0, {}}, Failure::InvalidDomain);
    CheckRejected(TEXT("Duplicate final explicit entry"),
        {DomainKind::Explicit, {}, 0, {{-1, -2}, {1, 0}, {0, 6}, {-1, -2}}}, Failure::DuplicateCoordinate);
    CheckRejected(TEXT("Negative disk radius"), {DomainKind::Disk, {}, -1, {}}, Failure::InvalidDomain);
    CheckRejected(TEXT("Unknown domain kind"), {static_cast<DomainKind>(255), {}, 0, {}}, Failure::InvalidDomain);
    CheckRejected(TEXT("Disk contains explicit data"), {DomainKind::Disk, {}, 0, {{0, 0}}}, Failure::InvalidDomain);
    CheckRejected(TEXT("Explicit domain contains disk center"),
        {DomainKind::Explicit, {1, 0}, 0, {{0, 0}}}, Failure::InvalidDomain);
    CheckRejected(TEXT("Explicit domain contains disk radius"),
        {DomainKind::Explicit, {}, 1, {{0, 0}}}, Failure::InvalidDomain);
    CheckRejected(TEXT("First disk radius above cell limit"), {DomainKind::Disk, {}, 148, {}}, Failure::LimitExceeded);
    CheckRejected(TEXT("Maximal disk radius preflights"), {DomainKind::Disk, {}, Max, {}}, Failure::LimitExceeded);

    auto LimitCoordinates = TArray<FCk_HexCoord>{};
    LimitCoordinates.Reserve(65537);
    for (auto Index = 0; Index < 65536; ++Index)
    { LimitCoordinates.Emplace(Index, 0); }
    TestTrue(TEXT("Explicit domain at cell limit"),
        Geometry::TryResolve_Domain({DomainKind::Explicit, {}, 0, LimitCoordinates}, Coordinates, Result));
    TestEqual(TEXT("Explicit domain at cell limit result"), Result, Failure::None);
    TestEqual(TEXT("Explicit domain retains the full cell limit"), Coordinates.Num(), 65536);
    LimitCoordinates.Emplace(65536, 0);
    CheckRejected(TEXT("Explicit domain above cell limit"),
        {DomainKind::Explicit, {}, 0, MoveTemp(LimitCoordinates)}, Failure::LimitExceeded);

    CheckRejected(TEXT("Positive disk extent overflows"), {DomainKind::Disk, {Max, 0}, 1, {}}, Failure::Overflow);
    CheckRejected(TEXT("Negative disk extent overflows"), {DomainKind::Disk, {0, Min}, 1, {}}, Failure::Overflow);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_HexGeometry_ProjectionOracle, "Ck.HexGrid.Geometry.ProjectionOracle", ck_test_hex_geometry::TestFlags)

auto
    FCkTest_HexGeometry_ProjectionOracle::
    RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_geometry;

    constexpr auto Radius = 4.0;
    const auto Coordinate = FCk_HexCoord{2, -3};
    for (const auto Chart : {Orientation::Pointy, Orientation::Flat})
    {
        const auto Layout = FCk_HexLayout{Chart, Radius};
        const auto Expected = Chart == Orientation::Pointy ? FVector2D{2.0 * Sqrt3, -18.0}
                                                         : FVector2D{12.0, -8.0 * Sqrt3};
        auto Center = FVector2D::ZeroVector;
        auto Result = Failure::OutsideDomain;
        TestTrue(TEXT("Projected center"), Geometry::TryGet_LocalCenter(Coordinate, Layout, Center, Result));
        TestEqual(TEXT("Projected center result"), Result, Failure::None);
        TestTrue(TEXT("Projected center matches literal analytic value"), Center.Equals(Expected, 1e-12));

        auto Corners = TArray<FVector2D>{};
        TestTrue(TEXT("Projected corners"), Geometry::TryGet_LocalCorners(Coordinate, Layout, Corners, Result));
        TestEqual(TEXT("Projected corners result"), Result, Failure::None);
        if (NOT TestEqual(TEXT("Six projected corners"), Corners.Num(), 6))
        { continue; }

        const auto UnitCorners = Get_AnalyticVertices(Chart);
        auto Polygon = TArray<FVector2D>{};
        for (auto Index = 0; Index < 6; ++Index)
        {
            Polygon.Add(Expected + Radius * UnitCorners[Index]);
            const auto Label = FString::Printf(TEXT("Projected corner %d"), Index);
            TestTrue(Label + TEXT(" matches literal analytic vertex"), Corners[Index].Equals(Polygon.Last(), 1e-12));
            const auto Next = (Index + 1) % 6;
            TestTrue(Label + TEXT(" advances counterclockwise"),
                FVector2D::CrossProduct(Corners[Index] - Center, Corners[Next] - Center) > 0.0);
        }

        const auto Inside = Expected + FVector2D{0.4 * Radius, 0.1 * Radius};
        const auto Outside = Expected + FVector2D{0.8 * Radius, 0.8 * Radius};
        TestTrue(TEXT("Half-planes contain the analytic interior sample"), Get_PolygonContains(Polygon, Inside));
        TestFalse(TEXT("Half-planes reject the analytic exterior sample"), Get_PolygonContains(Polygon, Outside));
        TestTrue(TEXT("Projected polygon contains the analytic interior sample"), Get_PolygonContains(Corners, Inside));
        TestFalse(TEXT("Projected polygon rejects the analytic exterior sample"), Get_PolygonContains(Corners, Outside));
        TestPick(*this, TEXT("Interior point selects polygon cell"), Layout, Inside, Coordinate);
        const auto Adjacent = FCk_HexCoord{2, -2};
        TestPick(*this, TEXT("Exterior point selects adjacent polygon"), Layout, Outside,
            Chart == Orientation::Pointy ? Adjacent : FCk_HexCoord{3, -3});

        constexpr auto HugeRadius = 1.1e308;
        const auto HugeLayout = FCk_HexLayout{Chart, HugeRadius};
        TestTrue(TEXT("Origin center remains representable at enormous radius"),
            Geometry::TryGet_LocalCenter({}, HugeLayout, Center, Result));
        TestEqual(TEXT("Enormous-radius center result"), Result, Failure::None);
        TestTrue(TEXT("Enormous-radius origin center stays zero"), Center == FVector2D::ZeroVector);
        TestTrue(TEXT("All origin corners remain representable at enormous radius"),
            Geometry::TryGet_LocalCorners({}, HugeLayout, Corners, Result));
        TestEqual(TEXT("Enormous-radius corners result"), Result, Failure::None);
        if (NOT TestEqual(TEXT("Enormous-radius corner count"), Corners.Num(), UnitCorners.Num()))
        { continue; }

        const auto NormalizeCorner = [](const FVector2D& InCorner)
        { return FVector2D{InCorner.X / HugeRadius, InCorner.Y / HugeRadius}; };
        for (auto Index = 0; Index < Corners.Num(); ++Index)
        {
            const auto Label = FString::Printf(TEXT("Enormous-radius corner %d"), Index);
            const auto Normalized = NormalizeCorner(Corners[Index]);
            const auto Next = NormalizeCorner(Corners[(Index + 1) % Corners.Num()]);
            TestFalse(Label + TEXT(" is nonfinite"), Corners[Index].ContainsNaN());
            TestTrue(Label + TEXT(" matches analytic corner"), Normalized.Equals(UnitCorners[Index], 1e-15));
            TestTrue(Label + TEXT(" advances counterclockwise"), FVector2D::CrossProduct(Normalized, Next) > 0.0);
            for (auto Other = 0; Other < Index; ++Other)
            { TestTrue(Label + TEXT(" is distinct"), Corners[Index] != Corners[Other]); }
        }
    }
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_HexGeometry_PickingBoundaries, "Ck.HexGrid.Geometry.PickingBoundaries", ck_test_hex_geometry::TestFlags)

auto
    FCkTest_HexGeometry_PickingBoundaries::
    RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_geometry;

    constexpr auto Perturbation = 1e-7;
    const auto EdgeTieWinners = TArray<FCk_HexCoord>{{0, 0}, {0, 0}, {-1, 1}, {-1, 0}, {0, -1}, {0, 0}};
    const auto PointyVertexWinners = TArray<FCk_HexCoord>{{0, 0}, {-1, 1}, {-1, 0}, {-1, 0}, {0, -1}, {0, 0}};
    const auto FlatVertexWinners = TArray<FCk_HexCoord>{{0, 0}, {0, 0}, {-1, 1}, {-1, 0}, {-1, 0}, {0, -1}};
    for (const auto Chart : {Orientation::Pointy, Orientation::Flat})
    {
        const auto Layout = FCk_HexLayout{Chart, 1.0};
        const auto Vertices = Get_AnalyticVertices(Chart);
        const auto& VertexWinners = Chart == Orientation::Pointy ? PointyVertexWinners : FlatVertexWinners;
        TestPick(*this, TEXT("Analytic origin"), Layout, FVector2D::ZeroVector, {});

        for (auto Index = 0; Index < Directions.Num(); ++Index)
        {
            const auto& Neighbor = Directions[Index];
            const auto Center = Get_AnalyticCenter(Neighbor, Chart);
            const auto Mid = Center * 0.5;
            const auto Normal = Center / Sqrt3;
            const auto EdgeLabel = FString::Printf(TEXT("Edge %d"), Index);
            TestPick(*this, EdgeLabel + TEXT(" midpoint tie"), Layout, Mid, EdgeTieWinners[Index]);
            TestPick(*this, EdgeLabel + TEXT(" inward"), Layout, Mid - Normal * Perturbation, {});
            TestPick(*this, EdgeLabel + TEXT(" outward"), Layout, Mid + Normal * Perturbation,
                {Neighbor.X, Neighbor.Y});

            const auto Vertex = Vertices[Index];
            const auto VertexLabel = FString::Printf(TEXT("Vertex %d"), Index);
            TestPick(*this, VertexLabel + TEXT(" three-cell tie"), Layout, Vertex, VertexWinners[Index]);
            const auto First = Chart == Orientation::Pointy ? Index : (Index + 5) % 6;
            const auto Second = (First + 1) % 6;
            for (const auto Cell : {FIntPoint{0, 0}, Directions[First], Directions[Second]})
            {
                const auto TowardCenter = Get_AnalyticCenter(Cell, Chart) - Vertex;
                const auto Label = VertexLabel + FString::Printf(TEXT(" toward (%d, %d)"), Cell.X, Cell.Y);
                TestPick(*this, Label, Layout, Vertex + TowardCenter * Perturbation, {Cell.X, Cell.Y});
            }
        }

        // Phase 1 exposes the lattice picker and domain resolution separately.
        auto Members = TArray<FCk_HexCoord>{};
        auto Result = Failure::OutsideDomain;
        TestTrue(TEXT("Single-cell disk resolves"), Geometry::TryResolve_Domain({}, Members, Result));
        const auto WestCenter = Get_AnalyticCenter({-1, 0}, Chart);
        const auto Boundary = WestCenter * 0.5;
        const auto Exterior = TestPick(*this, TEXT("Domain exterior wins a boundary tie"), Layout, Boundary, {-1, 0});
        TestFalse(TEXT("Exact membership misses the exterior winner"), Members.Contains(Exterior));
        const auto Interior = TestPick(*this, TEXT("Domain side of boundary"), Layout,
            Boundary - WestCenter / Sqrt3 * Perturbation, {});
        TestTrue(TEXT("Exact membership finds the interior winner"), Members.Contains(Interior));

        const auto OpenDomain = FCk_HexDomain{DomainKind::Explicit, {}, 0, {{-1, 0}, {1, 0}}};
        TestTrue(TEXT("Explicit domain with a hole resolves"), Geometry::TryResolve_Domain(OpenDomain, Members, Result));
        const auto Hole = TestPick(*this, TEXT("Hole wins a tie against a present neighbor"), Layout,
            Get_AnalyticCenter({1, 0}, Chart) * 0.5, {});
        TestFalse(TEXT("Exact membership misses the hole winner"), Members.Contains(Hole));
        TestTrue(TEXT("Tied neighbor is a member"), Members.Contains(FCk_HexCoord{1, 0}));
    }
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_HexGeometry_WorldProjection, "Ck.HexGrid.Geometry.WorldProjection", ck_test_hex_geometry::TestFlags)

auto
    FCkTest_HexGeometry_WorldProjection::
    RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_geometry;

    auto Coordinate = Sentinel;
    auto Hit = FVector{147.0, 58.0, -121.0};
    auto Result = Failure::OutsideDomain;
    constexpr auto Radius = 13.0;
    const auto Expected = FCk_HexCoord{-2, 1};

    const auto CheckPoint = [this, &Coordinate, &Result](const FString& InLabel, const FVector& InPoint,
        const FTransform& InPivot, const FCk_HexLayout& InLayout, const FCk_HexCoord& InExpected,
        Failure InExpectedFailure = Failure::None)
    {
        Coordinate = Sentinel;
        Result = Failure::OutsideDomain;
        const auto Picked = Geometry::TryGet_CoordFromWorldPoint(InPoint, InPivot, InLayout, Coordinate, Result);
        TestEqual(InLabel + TEXT(" success"), Picked, InExpectedFailure == Failure::None);
        TestEqual(InLabel + TEXT(" result"), Result, InExpectedFailure);
        TestCoordinate(*this, InLabel, Coordinate, InExpected);
    };

    const auto CheckRay = [this, &Coordinate, &Hit, &Result](const FString& InLabel, const FVector& InOrigin,
        const FVector& InDirection, const FTransform& InPivot, const FCk_HexLayout& InLayout,
        const FCk_HexCoord& InExpected, const FVector& InExpectedHit, Failure InExpectedFailure = Failure::None)
    {
        Coordinate = Sentinel;
        Hit = FVector{147.0, 58.0, -121.0};
        Result = Failure::OutsideDomain;
        const auto Picked = Geometry::TryGet_CoordFromWorldRay(InOrigin, InDirection, InPivot, InLayout,
            Coordinate, Hit, Result);
        TestEqual(InLabel + TEXT(" success"), Picked, InExpectedFailure == Failure::None);
        TestEqual(InLabel + TEXT(" result"), Result, InExpectedFailure);
        TestCoordinate(*this, InLabel, Coordinate, InExpected);
        TestFalse(InLabel + TEXT(" hit is finite"), Hit.ContainsNaN());
        if (InExpectedFailure == Failure::None)
        { TestTrue(InLabel + TEXT(" hit"), Hit.Equals(InExpectedHit, 1e-10)); }
        else
        { TestTrue(InLabel + TEXT(" clears hit"), Hit == FVector::ZeroVector); }
    };

    const auto Rotation = FQuat{FVector{1.0, 2.0, 3.0}.GetSafeNormal(), 0.83};
    const auto Translation = FVector{123.0, -76.0, 32.0};
    const auto Normal = Rotation.RotateVector(FVector::UpVector);
    const auto Tangent = Rotation.RotateVector(FVector::ForwardVector);

    for (const auto Chart : {Orientation::Pointy, Orientation::Flat})
    {
        const auto Layout = FCk_HexLayout{Chart, Radius};
        const auto Analytic = Get_AnalyticCenter({-2, 1}, Chart) * Radius;
        for (const auto Scale : {0.5, 2.0})
        {
            const auto Pivot = FTransform{Rotation, Translation, FVector{Scale, Scale, Scale}};
            const auto Label = FString::Printf(TEXT("World scale %.1f orientation %d"), Scale, static_cast<int32>(Chart));
            const auto WorldCenter = Translation + Rotation.RotateVector(FVector{Analytic.X, Analytic.Y, 0.0} * Scale);
            const auto Tolerance = 1e-7 * Radius * Scale;

            CheckPoint(Label + TEXT(" analytic point"), WorldCenter, Pivot, Layout, Expected);
            for (const auto Side : {-1.0, 1.0})
            {
                CheckPoint(Label + TEXT(" just inside plane tolerance"),
                    WorldCenter + Normal * (Side * 0.99 * Tolerance), Pivot, Layout, Expected);
                CheckPoint(Label + TEXT(" just outside plane tolerance"),
                    WorldCenter + Normal * (Side * 1.01 * Tolerance), Pivot, Layout, {}, Failure::OffPlane);
            }

            const auto Origin = WorldCenter + Normal * 21.0 - Tangent * 5.0;
            const auto Direction = (WorldCenter - Origin) * 4.0;
            CheckRay(Label + TEXT(" nonunit oblique ray"), Origin, Direction, Pivot, Layout, Expected, WorldCenter);
            CheckRay(Label + TEXT(" origin on rotated plane"), Translation, Normal, Pivot, Layout, {}, Translation);
            CheckRay(Label + TEXT(" parallel ray"), Origin, Tangent, Pivot, Layout, {}, {}, Failure::ParallelRay);
            CheckRay(Label + TEXT(" behind origin"), Origin, -Direction, Pivot, Layout, {}, {}, Failure::BehindRay);
            CheckRay(Label + TEXT(" zero direction"), Origin, FVector::ZeroVector, Pivot, Layout, {}, {},
                Failure::InvalidDirection);
        }
    }

    struct FInvalidPivot
    {
        const TCHAR* _Label;
        FTransform _Pivot;
    };
    const auto InvalidPivots = TArray<FInvalidPivot>{
        {TEXT("Nonfinite translation"), FTransform{FQuat::Identity, FVector{Infinity, 0.0, 0.0}}},
        {TEXT("Nonfinite scale"), FTransform{FQuat::Identity, FVector::ZeroVector, FVector{Infinity, 1.0, 1.0}}},
        {TEXT("Nonfinite quaternion"), FTransform{FQuat{Infinity, 0.0, 0.0, 1.0}, FVector::ZeroVector}},
        {TEXT("Degenerate quaternion"), FTransform{FQuat{0.0, 0.0, 0.0, 0.0}, FVector::ZeroVector}},
        {TEXT("Nonnormalized quaternion"), FTransform{FQuat{0.0, 0.0, 0.0, 2.0}, FVector::ZeroVector}},
        {TEXT("Singular scale"), FTransform{FQuat::Identity, FVector::ZeroVector, FVector{0.0, 0.0, 0.0}}},
        {TEXT("All-negative scale"), FTransform{FQuat::Identity, FVector::ZeroVector, FVector{-1.0, -1.0, -1.0}}},
        {TEXT("Reflected scale"), FTransform{FQuat::Identity, FVector::ZeroVector, FVector{1.0, -1.0, 1.0}}},
        {TEXT("Nonuniform scale"), FTransform{FQuat::Identity, FVector::ZeroVector, FVector{1.0, 1.0, 1.00001}}}};
    const auto Layout = FCk_HexLayout{Orientation::Pointy, 1.0};
    const auto QuarterTurn = FQuat{FVector::RightVector, FMath::DegreesToRadians(90.0)};
    const auto RotatedPivot = FTransform{QuarterTurn, FVector::ZeroVector};
    const auto PlaneOrigin = FVector{0.0, 0.0, 1.0};
    CheckRay(TEXT("Rotated-plane origin away from pivot, positive normal"), PlaneOrigin, FVector::ForwardVector,
        RotatedPivot, Layout, {-1, 0}, PlaneOrigin);
    TestTrue(TEXT("Positive normal preserves the exact origin hit"), Hit == PlaneOrigin);
    CheckRay(TEXT("Rotated-plane origin away from pivot, negative normal"), PlaneOrigin, -FVector::ForwardVector,
        RotatedPivot, Layout, {-1, 0}, PlaneOrigin);
    TestTrue(TEXT("Negative normal preserves the exact origin hit"), Hit == PlaneOrigin);
    for (const auto Offset : {1e-12, 1e-10})
    {
        const auto Label = FString::Printf(TEXT("Ray recedes from rotated plane at offset %.1g"), Offset);
        CheckRay(Label, PlaneOrigin + FVector::ForwardVector * Offset, FVector::ForwardVector,
            RotatedPivot, Layout, {}, {}, Failure::BehindRay);
    }

    constexpr auto HugeRadius = 1e308;
    Coordinate = Sentinel;
    Hit = FVector{147.0, 58.0, -121.0};
    Result = Failure::OutsideDomain;
    TestTrue(TEXT("Finite world hit survives unrepresentable ray travel distance"),
        Geometry::TryGet_CoordFromWorldRay({-HugeRadius, 0.0, HugeRadius}, {Sqrt3, 0.0, -1.0},
            FTransform::Identity, {Orientation::Pointy, HugeRadius}, Coordinate, Hit, Result));
    TestEqual(TEXT("Enormous ray result"), Result, Failure::None);
    TestCoordinate(*this, TEXT("Enormous ray selects origin cell"), Coordinate, {});
    TestFalse(TEXT("Enormous ray hit is nonfinite"), Hit.ContainsNaN());
    TestTrue(TEXT("Enormous ray hit matches normalized analytic X"), FMath::Abs(Hit.X / HugeRadius - (Sqrt3 - 1.0)) <= 1e-15);
    TestTrue(TEXT("Enormous ray hit has negligible normalized Y"), FMath::Abs(Hit.Y / HugeRadius) <= 1e-15);
    TestTrue(TEXT("Enormous ray hit has negligible normalized Z"), FMath::Abs(Hit.Z / HugeRadius) <= 1e-15);

    for (const auto& Invalid : InvalidPivots)
    {
        CheckPoint(Invalid._Label, FVector::ZeroVector, Invalid._Pivot, Layout, {}, Failure::UnsupportedTransform);
        CheckRay(Invalid._Label, FVector::UpVector, -FVector::UpVector, Invalid._Pivot, Layout, {}, {},
            Failure::UnsupportedTransform);
    }
    CheckRay(TEXT("Nonfinite ray direction"), FVector::UpVector, FVector{1.0, Infinity, -1.0}, FTransform::Identity,
        Layout, {}, {}, Failure::InvalidDirection);
    CheckRay(TEXT("Nonfinite ray origin"), FVector{NaN, 0.0, 0.0}, -FVector::UpVector, FTransform::Identity,
        Layout, {}, {}, Failure::InvalidCoordinate);
    for (const auto DirectionScale : {1e-200, 1e200})
    {
        const auto Label = FString::Printf(TEXT("Finite nonzero ray direction of magnitude %.1g"), DirectionScale);
        CheckRay(Label, FVector::UpVector, -FVector::UpVector * DirectionScale, FTransform::Identity,
            Layout, {}, FVector::ZeroVector);
    }
    CheckRay(TEXT("Conditioning rejects an almost parallel ray"), FVector::ForwardVector, FVector{1.0, 0.0, 1e-10},
        FTransform::Identity, Layout, {}, {}, Failure::PrecisionLoss);
    CheckRay(TEXT("Finite ray with out-of-range intersection clears its hit"), FVector{1e100, 0.0, 1.0},
        -FVector::UpVector, FTransform::Identity, Layout, {}, {}, Failure::Overflow);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_HexGeometry_ArithmeticAndPrecision, "Ck.HexGrid.Geometry.ArithmeticAndPrecision", ck_test_hex_geometry::TestFlags)

auto
    FCkTest_HexGeometry_ArithmeticAndPrecision::
    RunTest(const FString&) -> bool
{
    using namespace ck_test_hex_geometry;

    auto Actual = Sentinel;
    auto Result = Failure::OutsideDomain;
    const auto CheckArithmetic = [this, &Actual, &Result](const FString& InLabel, bool InSucceeded,
        Failure InExpectedFailure, const FCk_HexCoord& InExpected = FCk_HexCoord{})
    {
        TestEqual(InLabel + TEXT(" success"), InSucceeded, InExpectedFailure == Failure::None);
        TestEqual(InLabel + TEXT(" result"), Result, InExpectedFailure);
        TestCoordinate(*this, InLabel, Actual, InExpected);
        Actual = Sentinel;
        Result = Failure::None;
    };
    CheckArithmetic(TEXT("Translate past INT32_MAX"), Geometry::TryTranslate({Max, 0}, {1, 0}, Actual, Result),
        Failure::Overflow);
    CheckArithmetic(TEXT("Translate below INT32_MIN"), Geometry::TryTranslate({0, Min}, {0, -1}, Actual, Result),
        Failure::Overflow);
    CheckArithmetic(TEXT("Translate onto representable extremes"),
        Geometry::TryTranslate({Max - 1, Min + 1}, {1, -1}, Actual, Result), Failure::None, {Max, Min});
    CheckArithmetic(TEXT("Neighbor past INT32_MAX"),
        Geometry::TryGet_Neighbor({Max, 0}, static_cast<ECk_HexDirection>(0), Actual, Result), Failure::Overflow);
    CheckArithmetic(TEXT("Unknown neighbor direction"),
        Geometry::TryGet_Neighbor({}, static_cast<ECk_HexDirection>(255), Actual, Result), Failure::InvalidDirection);
    CheckArithmetic(TEXT("Unknown rotation"), Geometry::TryRotate({}, static_cast<ECk_HexRotation>(6), Actual, Result),
        Failure::InvalidRotation);
    CheckArithmetic(TEXT("Rotation rejects widened sum outside int32"),
        Geometry::TryRotate({Max, Max}, ECk_HexRotation::Turn1, Actual, Result), Failure::Overflow);
    CheckArithmetic(TEXT("Rotation rejects negation of INT32_MIN"),
        Geometry::TryRotate({0, Min}, ECk_HexRotation::Turn1, Actual, Result), Failure::Overflow);
    CheckArithmetic(TEXT("Rotation keeps out-of-range intermediate components widened"),
        Geometry::TryRotate({Max, Max}, ECk_HexRotation::Turn3, Actual, Result), Failure::None, {-Max, -Max});
    CheckArithmetic(TEXT("Zero turn preserves INT32_MIN"),
        Geometry::TryRotate({Min, Min}, ECk_HexRotation::Turn0, Actual, Result), Failure::None, {Min, Min});

    TestEqual(TEXT("Derived S stays widened"), FCk_HexCoord(Min, Min).Get_S(), int64{4294967296});
    auto Members = TArray<FCk_HexCoord>{};
    const auto ExtremeDomain = FCk_HexDomain{DomainKind::Explicit, {}, 0, {{Max, Max}, {Min, Min}}};
    TestTrue(TEXT("Integer domain accepts both extremes"), Geometry::TryResolve_Domain(ExtremeDomain, Members, Result));
    TestEqual(TEXT("Extreme domain result"), Result, Failure::None);
    const auto ExpectedMembers = TArray<FCk_HexCoord>{{Min, Min}, {Max, Max}};
    TestTrue(TEXT("Integer addresses survive domain materialization"), Members == ExpectedMembers);

    const auto CheckProjectionRejection = [this](const FString& InLabel, const FCk_HexLayout& InLayout,
        Failure InExpectedFailure, const FCk_HexCoord& InCoordinate = FCk_HexCoord{2, -1})
    {
        auto Center = FVector2D{4.0, -5.0};
        auto Corners = TArray<FVector2D>{{3.0, 1.0}};
        auto ProjectionFailure = Failure::None;
        TestFalse(InLabel + TEXT(" center"),
            Geometry::TryGet_LocalCenter(InCoordinate, InLayout, Center, ProjectionFailure));
        TestEqual(InLabel + TEXT(" center result"), ProjectionFailure, InExpectedFailure);
        TestTrue(InLabel + TEXT(" clears center"), Center == FVector2D::ZeroVector);
        ProjectionFailure = Failure::None;
        TestFalse(InLabel + TEXT(" corners"),
            Geometry::TryGet_LocalCorners(InCoordinate, InLayout, Corners, ProjectionFailure));
        TestEqual(InLabel + TEXT(" corners result"), ProjectionFailure, InExpectedFailure);
        TestTrue(InLabel + TEXT(" clears all corners"), Corners.IsEmpty());
    };

    for (const auto InvalidRadius : {0.0, -1.0, Infinity, NaN})
    {
        const auto Label = FString::Printf(TEXT("Invalid radius %.3g"), InvalidRadius);
        const auto Layout = FCk_HexLayout{Orientation::Pointy, InvalidRadius};
        TestPick(*this, Label, Layout, FVector2D::ZeroVector, {}, Failure::InvalidLayout);
        CheckProjectionRejection(Label, Layout, Failure::InvalidLayout);
    }
    const auto UnknownLayout = FCk_HexLayout{static_cast<Orientation>(255), 1.0};
    TestPick(*this, TEXT("Unknown orientation"), UnknownLayout, FVector2D::ZeroVector, {}, Failure::InvalidOrientation);
    CheckProjectionRejection(TEXT("Unknown orientation"), UnknownLayout, Failure::InvalidOrientation);
    CheckProjectionRejection(TEXT("Finite radius overflows projected coordinates"),
        {Orientation::Pointy, std::numeric_limits<double>::max()}, Failure::Overflow);
    CheckProjectionRejection(TEXT("Extreme integer address and finite radius overflow spatial coordinates"),
        {Orientation::Flat, std::numeric_limits<double>::max()}, Failure::Overflow, {Max, Max});

    const auto CheckWorldRejection = [this](const FString& InLabel, const FVector& InPoint,
        const FTransform& InPivot, const FCk_HexLayout& InLayout, Failure InExpected)
    {
        auto ActualCoordinate = Sentinel;
        auto PickFailure = Failure::None;
        TestFalse(InLabel, Geometry::TryGet_CoordFromWorldPoint(InPoint, InPivot, InLayout, ActualCoordinate, PickFailure));
        TestEqual(InLabel + TEXT(" result"), PickFailure, InExpected);
        TestCoordinate(*this, InLabel, ActualCoordinate, {});
    };

    for (const auto Chart : {Orientation::Pointy, Orientation::Flat})
    {
        const auto Layout = FCk_HexLayout{Chart, 1.0};
        const auto LargeAddress = FCk_HexCoord{1000000, -1000000};
        const auto NextAddress = FCk_HexCoord{1000001, -1000000};
        const auto A = Get_AnalyticCenter({1000000, -1000000}, Chart);
        const auto B = Get_AnalyticCenter({1000001, -1000000}, Chart);
        const auto Mid = (A + B) * 0.5;
        const auto Shift = (B - A).GetSafeNormal() * 1e-3;
        TestPick(*this, TEXT("Large address center picks"), Layout, A, LargeAddress);
        TestPick(*this, TEXT("Large address edge tie"), Layout, Mid, LargeAddress);
        TestPick(*this, TEXT("Large address edge nearer A"), Layout, Mid - Shift, LargeAddress);
        TestPick(*this, TEXT("Large address edge nearer B"), Layout, Mid + Shift, NextAddress);

        const auto ExtremeCenter = Get_AnalyticCenter({Max, Max}, Chart);
        TestPick(*this, TEXT("Extreme integer address loses spatial precision"), Layout, ExtremeCenter, {},
            Failure::PrecisionLoss);
        CheckWorldRejection(TEXT("Extreme world point loses spatial precision"),
            FVector{ExtremeCenter.X, ExtremeCenter.Y, 0.0}, FTransform::Identity, Layout, Failure::PrecisionLoss);
        TestPick(*this, TEXT("Enormous finite local point preflights"), Layout, FVector2D{1e100, 0.0}, {}, Failure::Overflow);
        TestPick(*this, TEXT("Nonfinite local point rejects"), Layout, FVector2D{Infinity, NaN}, {},
            Failure::InvalidCoordinate);
        CheckWorldRejection(TEXT("Enormous finite world point preflights"), FVector{1e100, 0.0, 0.0},
            FTransform::Identity, Layout, Failure::Overflow);
        CheckWorldRejection(TEXT("Nonfinite world point rejects"), FVector{0.0, NaN, Infinity},
            FTransform::Identity, Layout, Failure::InvalidCoordinate);

        const auto DistantPivot = FTransform{FQuat::Identity, FVector{1e20, 0.0, 0.0}};
        CheckWorldRejection(TEXT("Cancellation at distant pivot cannot select the origin cell"),
            DistantPivot.GetTranslation(), DistantPivot, {Chart, 100.0}, Failure::PrecisionLoss);
    }

    constexpr auto Cap = 1e-5;
    constexpr auto Scale = 128.0 * std::numeric_limits<double>::epsilon();
    constexpr auto BoundAtCap = Cap / Scale;
    const auto BoundAboveCap = std::nextafter(BoundAtCap, Infinity);
    const auto CapLayout = FCk_HexLayout{Orientation::Pointy, 1.0};
    // At the pivot, the local coordinates vanish. With unit radius and scale, B = 1 + 2*X.
    const auto AtCap = FVector{(BoundAtCap - 1.0) / 2.0, 0.0, 0.0};
    const auto AboveCap = FVector{(BoundAboveCap - 1.0) / 2.0, 0.0, 0.0};
    TestTrue(TEXT("Analytic uncertainty equals the cap exactly"), Scale * (1.0 + 2.0 * AtCap.X) == Cap);
    TestTrue(TEXT("Analytic bound advances one representable value"), 1.0 + 2.0 * AboveCap.X == BoundAboveCap);
    TestTrue(TEXT("Analytic uncertainty exceeds the cap"), Scale * (1.0 + 2.0 * AboveCap.X) > Cap);
    Actual = Sentinel;
    Result = Failure::OutsideDomain;
    TestTrue(TEXT("World point at uncertainty cap is admitted"),
        Geometry::TryGet_CoordFromWorldPoint(AtCap, FTransform{FQuat::Identity, AtCap}, CapLayout, Actual, Result));
    TestEqual(TEXT("World point at uncertainty cap result"), Result, Failure::None);
    TestCoordinate(*this, TEXT("World point at uncertainty cap"), Actual, {});
    CheckWorldRejection(TEXT("World point one representable bound above cap rejects"), AboveCap,
        FTransform{FQuat::Identity, AboveCap}, CapLayout, Failure::PrecisionLoss);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif
