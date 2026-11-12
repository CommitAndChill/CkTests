#include "Misc/AutomationTest.h"

#include "CkPmg/CkPmg_Polyline_Geometry.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_pmg_polyline_geometry_spec
{
    constexpr auto Sides = 6;
    constexpr auto MaxPoints = 64;
    constexpr auto RingSize = Sides + 1;

    auto
        Get_RingCentre(
            const ck::pmg::polyline::FTubeBuffers& InBuffers,
            int32 InRing)
        -> FVector
    {
        auto Sum = FVector::ZeroVector;
        for (auto Around = 0; Around < Sides; ++Around)
        {
            Sum += InBuffers._Positions[InRing * RingSize + Around];
        }

        return Sum / Sides;
    }

    auto
        Get_StraightLine(
            int32 InNumPoints,
            double InSpacing)
        -> TArray<FVector>
    {
        auto Points = TArray<FVector>{};
        for (auto Index = 0; Index < InNumPoints; ++Index)
        {
            Points.Add(FVector{Index * InSpacing, 0.0, 0.0});
        }

        return Points;
    }

    auto
        Get_IsAllZero(
            const ck::pmg::polyline::FTubeBuffers& InBuffers)
        -> bool
    {
        for (const auto& Position : InBuffers._Positions)
        {
            if (NOT Position.IsZero())
            { return false; }
        }

        return true;
    }

    // The direction Unreal renders triangle InTriangle from. Its front face is clockwise seen from the front, so the
    // normal is (C - A) x (B - A): KismetProceduralMeshLibrary::GenerateBoxMesh winds its +Z face that way. Zero for a
    // degenerate triangle.
    auto
        Get_FrontNormal(
            const TArray<FVector>& InPositions,
            const TArray<int32>& InTriangles,
            int32 InTriangle)
        -> FVector
    {
        const auto& A = InPositions[InTriangles[InTriangle * 3 + 0]];
        const auto& B = InPositions[InTriangles[InTriangle * 3 + 1]];
        const auto& C = InPositions[InTriangles[InTriangle * 3 + 2]];
        return FVector::CrossProduct(C - A, B - A);
    }

    auto
        Make_Shape(
            ECk_Pmg_Polyline_Profile InProfile,
            float InRadius,
            int32 InMaxPoints,
            float InArrowLength,
            float InArrowRadius)
        -> ck::pmg::polyline::FShape
    {
        auto Shape = ck::pmg::polyline::FShape{};
        Shape._Profile = InProfile;
        Shape._Radius = InRadius;
        Shape._Sides = Sides;
        Shape._MaxPoints = InMaxPoints;
        Shape._ArrowLength = InArrowLength;
        Shape._ArrowRadius = InArrowRadius;
        return Shape;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_VertexCountIsFixed,
    "Ck.Pmg.Polyline.Geometry.VertexCountIsFixed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_VertexCountIsFixed::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    TestEqual(TEXT("vertex count is MaxPoints * (Sides + 1)"),
        ck::pmg::polyline::Get_NumVertices(MaxPoints, Sides), 448);

    for (const auto NumPoints : {2, 10, 64})
    {
        const auto Buffers = ck::pmg::polyline::Build_Tube(Get_StraightLine(NumPoints, 10.0), 2.0f, Sides, MaxPoints);
        TestEqual(FString::Printf(TEXT("%d input points produce 448 positions"), NumPoints), Buffers._Positions.Num(), 448);
        TestEqual(FString::Printf(TEXT("%d input points produce 448 normals"), NumPoints), Buffers._Normals.Num(), 448);
        TestEqual(FString::Printf(TEXT("%d input points produce 448 UVs"), NumPoints), Buffers._UV0.Num(), 448);
        TestEqual(FString::Printf(TEXT("%d input points produce 448 colours"), NumPoints), Buffers._Colors.Num(), 448);
        TestEqual(FString::Printf(TEXT("%d input points produce 448 tangents"), NumPoints), Buffers._Tangents.Num(), 448);
        TestEqual(FString::Printf(TEXT("%d input points are all drawn"), NumPoints), Buffers._NumDrawnPoints, NumPoints);
    }

    const auto Triangles = ck::pmg::polyline::Build_Triangles(MaxPoints, Sides);
    TestEqual(TEXT("index count is (MaxPoints - 1) * Sides quads * 6"), Triangles.Num(), 63 * 6 * 6);

    auto AllIndicesInRange = true;
    for (const auto Index : Triangles)
    {
        AllIndicesInRange &= Index >= 0 && Index < 448;
    }
    TestTrue(TEXT("every index addresses a vertex of the fixed buffer"), AllIndicesInRange);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_RingsSitAtTheRadius,
    "Ck.Pmg.Polyline.Geometry.RingsSitAtTheRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_RingsSitAtTheRadius::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto Radius = 2.0f;
    const auto Points = TArray<FVector>{FVector{0.0, 0.0, 0.0}, FVector{50.0, 0.0, 0.0}, FVector{120.0, 0.0, 0.0}};
    const auto Buffers = ck::pmg::polyline::Build_Tube(Points, Radius, Sides, MaxPoints);

    for (auto Ring = 0; Ring < 3; ++Ring)
    {
        for (auto Around = 0; Around < RingSize; ++Around)
        {
            const auto& Position = Buffers._Positions[Ring * RingSize + Around];
            const auto DistanceFromAxis = FVector2D{Position.Y, Position.Z}.Size();

            TestNearlyEqual(FString::Printf(TEXT("ring %d vertex %d sits at the radius"), Ring, Around),
                DistanceFromAxis, static_cast<double>(Radius), 0.001);
            TestNearlyEqual(FString::Printf(TEXT("ring %d vertex %d shares its point's X"), Ring, Around),
                Position.X, Points[Ring].X, 0.001);
        }
    }

    // Outward-facing as Unreal renders it: every triangle between the first two rings shows its front to the outside.
    const auto Triangles = ck::pmg::polyline::Build_Triangles(MaxPoints, Sides);
    for (auto Triangle = 0; Triangle < Sides * 2; ++Triangle)
    {
        const auto& A = Buffers._Positions[Triangles[Triangle * 3 + 0]];
        const auto& B = Buffers._Positions[Triangles[Triangle * 3 + 1]];
        const auto& C = Buffers._Positions[Triangles[Triangle * 3 + 2]];
        const auto FaceCentre = (A + B + C) / 3.0;
        const auto Outward = FVector{0.0, FaceCentre.Y, FaceCentre.Z};
        const auto FrontNormal = Get_FrontNormal(Buffers._Positions, Triangles, Triangle);

        TestTrue(FString::Printf(TEXT("triangle %d faces outward"), Triangle),
            FVector::DotProduct(FrontNormal, Outward) > 0.0);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_UvAndColourCarryArcLength,
    "Ck.Pmg.Polyline.Geometry.UvAndColourCarryArcLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_UvAndColourCarryArcLength::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    const auto Points = TArray<FVector>{FVector{0.0, 0.0, 0.0}, FVector{100.0, 0.0, 0.0}, FVector{250.0, 0.0, 0.0}};
    const auto Buffers = ck::pmg::polyline::Build_Tube(Points, 1.0f, Sides, MaxPoints);

    const double ExpectedUvX[] = {0.0, 100.0, 250.0};
    const double ExpectedR[] = {0.0, 0.4, 1.0};

    for (auto Ring = 0; Ring < 3; ++Ring)
    {
        for (auto Around = 0; Around < RingSize; ++Around)
        {
            const auto VertexIndex = Ring * RingSize + Around;
            TestNearlyEqual(FString::Printf(TEXT("ring %d vertex %d UV0.X is the arc length"), Ring, Around),
                Buffers._UV0[VertexIndex].X, ExpectedUvX[Ring], 0.001);
            TestNearlyEqual(FString::Printf(TEXT("ring %d vertex %d UV0.Y runs around the tube"), Ring, Around),
                Buffers._UV0[VertexIndex].Y, static_cast<double>(Around) / Sides, 0.0001);
            TestNearlyEqual(FString::Printf(TEXT("ring %d vertex %d colour R is the arc-length fraction"), Ring, Around),
                static_cast<double>(Buffers._Colors[VertexIndex].R), ExpectedR[Ring], 0.0001);
            TestTrue(FString::Printf(TEXT("ring %d vertex %d colour G = B = 0, A = 1"), Ring, Around),
                Buffers._Colors[VertexIndex].G == 0.0f && Buffers._Colors[VertexIndex].B == 0.0f &&
                Buffers._Colors[VertexIndex].A == 1.0f);
        }
    }

    TestNearlyEqual(TEXT("length is the sum of the segments"), Buffers._Length, 250.0, 0.001);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_UnusedRingsCollapse,
    "Ck.Pmg.Polyline.Geometry.UnusedRingsCollapse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_UnusedRingsCollapse::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    const auto Points = TArray<FVector>{FVector{0.0, 0.0, 0.0}, FVector{40.0, 30.0, 0.0}, FVector{80.0, 30.0, 20.0}};
    const auto Buffers = ck::pmg::polyline::Build_Tube(Points, 1.5f, Sides, MaxPoints);

    TestEqual(TEXT("three points are drawn"), Buffers._NumDrawnPoints, 3);
    TestFalse(TEXT("ring 2 is not at the origin (the collapse target is the real last ring)"),
        Get_RingCentre(Buffers, 2).IsNearlyZero(1.0));

    auto AllCollapsed = true;
    for (auto Ring = 3; Ring < MaxPoints; ++Ring)
    {
        for (auto Around = 0; Around < RingSize; ++Around)
        {
            AllCollapsed &= Buffers._Positions[Ring * RingSize + Around].Equals(Buffers._Positions[2 * RingSize + Around], 0.0);
        }
    }
    TestTrue(TEXT("rings 3..63 equal ring 2 vertex for vertex"), AllCollapsed);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_PlanarArcDoesNotTwist,
    "Ck.Pmg.Polyline.Geometry.PlanarArcDoesNotTwist",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_PlanarArcDoesNotTwist::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto Radius = 3.0f;
    constexpr auto NumPoints = 20;

    auto Points = TArray<FVector>{};
    for (auto Index = 0; Index < NumPoints; ++Index)
    {
        const auto X = Index * 50.0;
        const auto Z = 400.0 - 0.004 * FMath::Square(X - 475.0);
        Points.Add(FVector{X, 0.0, Z});
    }

    const auto Buffers = ck::pmg::polyline::Build_Tube(Points, Radius, Sides, MaxPoints);
    TestEqual(TEXT("every arc point is drawn"), Buffers._NumDrawnPoints, NumPoints);

    const auto FirstRingYRatio = FMath::Abs((Buffers._Positions[0] - Get_RingCentre(Buffers, 0)).Y) / Radius;
    TestTrue(TEXT("the first ring's side vector is out of the arc's plane"), FirstRingYRatio > 0.5);

    for (auto Ring = 0; Ring < NumPoints; ++Ring)
    {
        const auto Offset = Buffers._Positions[Ring * RingSize] - Get_RingCentre(Buffers, Ring);
        const auto YRatio = FMath::Abs(Offset.Y) / Radius;

        TestNearlyEqual(FString::Printf(TEXT("ring %d's around-index-0 vertex keeps the first ring's out-of-plane ratio"), Ring),
            YRatio, FirstRingYRatio, 0.001);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_DuplicatePointsMerge,
    "Ck.Pmg.Polyline.Geometry.DuplicatePointsMerge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_DuplicatePointsMerge::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    const auto Points = TArray<FVector>{
        FVector{0.0, 0.0, 0.0},
        FVector{0.005, 0.0, 0.0},
        FVector{100.0, 0.0, 0.0},
        FVector{100.0, 0.0, 0.0},
        FVector{100.0, 0.009, 0.0},
        FVector{200.0, 0.0, 0.0}};
    const auto Buffers = ck::pmg::polyline::Build_Tube(Points, 1.0f, Sides, MaxPoints);

    TestEqual(TEXT("points closer than 0.01 cm to their predecessor are merged"), Buffers._NumDrawnPoints, 3);
    TestNearlyEqual(TEXT("ring 1 sits on the first surviving point near X = 100"), Get_RingCentre(Buffers, 1).X, 100.0, 0.001);
    TestNearlyEqual(TEXT("ring 2 sits on the last point"), Get_RingCentre(Buffers, 2).X, 200.0, 0.001);
    TestNearlyEqual(TEXT("the merged path keeps its length"), Buffers._Length, 200.0, 0.001);

    // A closed loop's last point coincides with its first but is not consecutive with it, so it survives.
    const auto Loop = TArray<FVector>{
        FVector{0.0, 0.0, 0.0}, FVector{100.0, 0.0, 0.0}, FVector{100.0, 100.0, 0.0}, FVector{0.0, 0.0, 0.0}};
    const auto LoopBuffers = ck::pmg::polyline::Build_Tube(Loop, 1.0f, Sides, MaxPoints);
    TestEqual(TEXT("a closed path keeps its closing point"), LoopBuffers._NumDrawnPoints, 4);
    TestTrue(TEXT("a closed path's last ring centre coincides with its first"),
        Get_RingCentre(LoopBuffers, 3).Equals(Get_RingCentre(LoopBuffers, 0), 0.001));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_FewerThanTwoDrawsNothing,
    "Ck.Pmg.Polyline.Geometry.FewerThanTwoDrawsNothing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_FewerThanTwoDrawsNothing::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    const auto Empty = ck::pmg::polyline::Build_Tube(TArray<FVector>{}, 1.0f, Sides, MaxPoints);
    TestEqual(TEXT("no points draws nothing"), Empty._NumDrawnPoints, 0);
    TestEqual(TEXT("no points keeps the fixed vertex count"), Empty._Positions.Num(), 448);
    TestTrue(TEXT("no points leaves every position at zero"), Get_IsAllZero(Empty));

    const auto Single = ck::pmg::polyline::Build_Tube(TArray<FVector>{FVector{30.0, 40.0, 50.0}}, 1.0f, Sides, MaxPoints);
    TestEqual(TEXT("one point draws nothing"), Single._NumDrawnPoints, 0);
    TestTrue(TEXT("one point leaves every position at zero"), Get_IsAllZero(Single));

    const auto NoLength = ck::pmg::polyline::Build_Tube(
        TArray<FVector>{FVector{30.0, 40.0, 50.0}, FVector{30.0, 40.0, 50.004}, FVector{30.0, 40.0, 50.008}},
        1.0f, Sides, MaxPoints);
    TestEqual(TEXT("a path of no length draws nothing"), NoLength._NumDrawnPoints, 0);
    TestTrue(TEXT("a path of no length leaves every position at zero"), Get_IsAllZero(NoLength));
    TestNearlyEqual(TEXT("a path that draws nothing has no length"), NoLength._Length, 0.0, 0.0);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_TooManyPointsResampleByArcLength,
    "Ck.Pmg.Polyline.Geometry.TooManyPointsResampleByArcLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_TooManyPointsResampleByArcLength::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    const auto Points = Get_StraightLine(200, 10.0);
    TestNearlyEqual(TEXT("the input runs to X = 1990"), Points.Last().X, 1990.0, 0.0);

    const auto Buffers = ck::pmg::polyline::Build_Tube(Points, 1.0f, Sides, MaxPoints);
    TestEqual(TEXT("200 points resample to MaxPoints"), Buffers._NumDrawnPoints, 64);
    TestTrue(TEXT("the first ring centre is at the first point (positions are relative to it)"),
        Get_RingCentre(Buffers, 0).IsNearlyZero(0.001));
    TestNearlyEqual(TEXT("the last ring centre is at the last point: the tail is kept"),
        Get_RingCentre(Buffers, 63).X, 1990.0, 0.001);

    const auto ExpectedSpacing = 1990.0 / 63.0;
    auto MaxSpacingError = 0.0;
    for (auto Ring = 1; Ring < 64; ++Ring)
    {
        const auto Spacing = Get_RingCentre(Buffers, Ring).X - Get_RingCentre(Buffers, Ring - 1).X;
        MaxSpacingError = FMath::Max(MaxSpacingError, FMath::Abs(Spacing - ExpectedSpacing));
    }
    TestTrue(FString::Printf(TEXT("spacing is uniform within 0.01 cm (max error %f)"), MaxSpacingError), MaxSpacingError <= 0.01);
    TestNearlyEqual(TEXT("resampling keeps the length"), Buffers._Length, 1990.0, 0.001);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_VerticalStartHasAFrame,
    "Ck.Pmg.Polyline.Geometry.VerticalStartHasAFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_VerticalStartHasAFrame::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto Radius = 2.0f;
    const auto Points = TArray<FVector>{FVector{0.0, 0.0, 0.0}, FVector{0.0, 0.0, 100.0}, FVector{0.0, 0.0, 250.0}};
    const auto Buffers = ck::pmg::polyline::Build_Tube(Points, Radius, Sides, MaxPoints);

    TestEqual(TEXT("a vertical path is drawn"), Buffers._NumDrawnPoints, 3);

    for (auto Ring = 0; Ring < 3; ++Ring)
    {
        const auto Centre = Get_RingCentre(Buffers, Ring);
        for (auto Around = 0; Around < RingSize; ++Around)
        {
            const auto Offset = Buffers._Positions[Ring * RingSize + Around] - Centre;
            TestFalse(FString::Printf(TEXT("ring %d vertex %d is finite"), Ring, Around), Offset.ContainsNaN());
            TestNearlyEqual(FString::Printf(TEXT("ring %d vertex %d has a non-zero offset at the radius"), Ring, Around),
                Offset.Size(), static_cast<double>(Radius), 0.001);
            TestNearlyEqual(FString::Printf(TEXT("ring %d vertex %d lies in the ring's horizontal plane"), Ring, Around),
                Offset.Z, 0.0, 0.001);
        }
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_ShapeVertexCountIsFixed,
    "Ck.Pmg.Polyline.Geometry.ShapeVertexCountIsFixed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_ShapeVertexCountIsFixed::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto ShapeMaxPoints = 16;

    struct FCase
    {
        const TCHAR* _Name;
        ck::pmg::polyline::FShape _Shape;
        int32 _Vertices;
        int32 _Indices;
    };

    const auto Cases = TArray<FCase>{
        {TEXT("ribbon"), Make_Shape(ECk_Pmg_Polyline_Profile::Ribbon, 1.0f, ShapeMaxPoints, 0.0f, 0.0f),
            ShapeMaxPoints * 2, (ShapeMaxPoints - 1) * 6},
        {TEXT("ribbon with a head"), Make_Shape(ECk_Pmg_Polyline_Profile::Ribbon, 1.0f, ShapeMaxPoints, 10.0f, 4.0f),
            (ShapeMaxPoints + 2) * 2, (ShapeMaxPoints + 1) * 6},
        {TEXT("tube with a head"), Make_Shape(ECk_Pmg_Polyline_Profile::Tube, 1.0f, ShapeMaxPoints, 10.0f, 4.0f),
            (ShapeMaxPoints + 2) * RingSize, (ShapeMaxPoints + 1) * Sides * 6},
    };

    for (const auto& Case : Cases)
    {
        TestEqual(FString::Printf(TEXT("%s: vertex count"), Case._Name),
            ck::pmg::polyline::Get_NumVertices(Case._Shape), Case._Vertices);

        const auto Triangles = ck::pmg::polyline::Build_Triangles(Case._Shape);
        TestEqual(FString::Printf(TEXT("%s: index count"), Case._Name), Triangles.Num(), Case._Indices);

        auto AllIndicesInRange = true;
        for (const auto Index : Triangles)
        { AllIndicesInRange &= Index >= 0 && Index < Case._Vertices; }
        TestTrue(FString::Printf(TEXT("%s: every index addresses a vertex"), Case._Name), AllIndicesInRange);

        for (const auto NumPoints : {0, 2, 10, 200})
        {
            const auto Buffers = ck::pmg::polyline::Build(Get_StraightLine(NumPoints, 10.0), Case._Shape);
            TestEqual(FString::Printf(TEXT("%s: %d input points keep the vertex count"), Case._Name, NumPoints),
                Buffers._Positions.Num(), Case._Vertices);
            TestEqual(FString::Printf(TEXT("%s: %d input points keep the normal count"), Case._Name, NumPoints),
                Buffers._Normals.Num(), Case._Vertices);
        }
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_RibbonLiesLevelAndFacesUp,
    "Ck.Pmg.Polyline.Geometry.RibbonLiesLevelAndFacesUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_RibbonLiesLevelAndFacesUp::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto HalfWidth = 2.0f;
    constexpr auto ShapeMaxPoints = 8;
    const auto Shape = Make_Shape(ECk_Pmg_Polyline_Profile::Ribbon, HalfWidth, ShapeMaxPoints, 0.0f, 0.0f);
    const auto Points = TArray<FVector>{FVector{0.0, 0.0, 0.0}, FVector{100.0, 0.0, 0.0}, FVector{250.0, 0.0, 0.0}};
    const auto Buffers = ck::pmg::polyline::Build(Points, Shape);

    TestEqual(TEXT("three points are drawn"), Buffers._NumDrawnPoints, 3);

    for (auto Ring = 0; Ring < 3; ++Ring)
    {
        for (auto Across = 0; Across < 2; ++Across)
        {
            const auto& Position = Buffers._Positions[Ring * 2 + Across];
            TestNearlyEqual(FString::Printf(TEXT("ring %d edge %d shares its point's X"), Ring, Across),
                Position.X, Points[Ring].X, 0.001);
            TestNearlyEqual(FString::Printf(TEXT("ring %d edge %d is level with the path"), Ring, Across),
                Position.Z, 0.0, 0.001);
            TestNearlyEqual(FString::Printf(TEXT("ring %d edge %d sits half the width to the side"), Ring, Across),
                FMath::Abs(Position.Y), static_cast<double>(HalfWidth), 0.001);
            TestTrue(FString::Printf(TEXT("ring %d edge %d has an upward normal"), Ring, Across),
                Buffers._Normals[Ring * 2 + Across].Equals(FVector::UpVector, 0.001));
        }

        TestNearlyEqual(FString::Printf(TEXT("ring %d spans the full width"), Ring),
            FVector::Dist(Buffers._Positions[Ring * 2], Buffers._Positions[Ring * 2 + 1]), 2.0 * HalfWidth, 0.001);
    }

    const auto Triangles = ck::pmg::polyline::Build_Triangles(Shape);
    for (auto Triangle = 0; Triangle < 4; ++Triangle)
    {
        const auto FrontNormal = Get_FrontNormal(Buffers._Positions, Triangles, Triangle).GetSafeNormal();
        TestTrue(FString::Printf(TEXT("triangle %d shows its front upward"), Triangle),
            FrontNormal.Equals(FVector::UpVector, 0.001));
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_RibbonOnAnArcStaysLevelAcross,
    "Ck.Pmg.Polyline.Geometry.RibbonOnAnArcStaysLevelAcross",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_RibbonOnAnArcStaysLevelAcross::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto HalfWidth = 1.5f;
    constexpr auto NumPoints = 20;
    const auto Shape = Make_Shape(ECk_Pmg_Polyline_Profile::Ribbon, HalfWidth, MaxPoints, 0.0f, 0.0f);

    // An arc in the XZ plane that rises, then falls well below its start.
    auto Points = TArray<FVector>{};
    for (auto Index = 0; Index < NumPoints; ++Index)
    {
        const auto X = Index * 40.0;
        Points.Add(FVector{X, 0.0, 300.0 * (X / 400.0) - 300.0 * FMath::Square(X / 400.0)});
    }

    const auto Buffers = ck::pmg::polyline::Build(Points, Shape);
    TestEqual(TEXT("every point is drawn"), Buffers._NumDrawnPoints, NumPoints);

    for (auto Ring = 0; Ring < NumPoints; ++Ring)
    {
        const auto Across = Buffers._Positions[Ring * 2] - Buffers._Positions[Ring * 2 + 1];
        TestTrue(FString::Printf(TEXT("ring %d runs across the path, out of the plane of the arc"), Ring),
            FMath::Abs(Across.X) < 0.001 && FMath::Abs(Across.Z) < 0.001);
        TestNearlyEqual(FString::Printf(TEXT("ring %d keeps the full width"), Ring),
            Across.Size(), 2.0 * HalfWidth, 0.001);
    }

    // Rising and falling, the ribbon shows its front to the sky, where an eye above and behind the start sees it.
    const auto Triangles = ck::pmg::polyline::Build_Triangles(Shape);
    for (auto Triangle = 0; Triangle < (NumPoints - 1) * 2; ++Triangle)
    {
        const auto FrontNormal = Get_FrontNormal(Buffers._Positions, Triangles, Triangle);
        TestTrue(FString::Printf(TEXT("triangle %d faces upward"), Triangle), FrontNormal.Z > 0.0);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_ArrowHeadEndsAtTheLastPoint,
    "Ck.Pmg.Polyline.Geometry.ArrowHeadEndsAtTheLastPoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_ArrowHeadEndsAtTheLastPoint::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto ShaftRadius = 1.0f;
    constexpr auto ArrowLength = 20.0f;
    constexpr auto ArrowRadius = 4.0f;
    constexpr auto ShapeMaxPoints = 8;
    const auto Points = Get_StraightLine(5, 50.0);

    for (const auto Profile : {ECk_Pmg_Polyline_Profile::Tube, ECk_Pmg_Polyline_Profile::Ribbon})
    {
        const auto IsRibbon = Profile == ECk_Pmg_Polyline_Profile::Ribbon;
        const auto Name = IsRibbon ? TEXT("ribbon") : TEXT("tube");
        const auto ProfileRingSize = IsRibbon ? 2 : RingSize;
        const auto Shape = Make_Shape(Profile, ShaftRadius, ShapeMaxPoints, ArrowLength, ArrowRadius);
        const auto Buffers = ck::pmg::polyline::Build(Points, Shape);

        TestEqual(FString::Printf(TEXT("%s: the head adds no drawn point"), Name), Buffers._NumDrawnPoints, 5);
        TestNearlyEqual(FString::Printf(TEXT("%s: the drawn length is the length of the path"), Name),
            Buffers._Length, 200.0, 0.001);

        const auto BaseRing = ShapeMaxPoints;
        const auto TipRing = ShapeMaxPoints + 1;
        for (auto Around = 0; Around < ProfileRingSize; ++Around)
        {
            const auto& Tip = Buffers._Positions[TipRing * ProfileRingSize + Around];
            TestTrue(FString::Printf(TEXT("%s: tip vertex %d is the last point"), Name, Around),
                Tip.Equals(FVector{200.0, 0.0, 0.0}, 0.001));

            const auto& Base = Buffers._Positions[BaseRing * ProfileRingSize + Around];
            TestNearlyEqual(FString::Printf(TEXT("%s: base vertex %d is the arrow length short of the end"), Name, Around),
                Base.X, 180.0, 0.001);
            TestNearlyEqual(FString::Printf(TEXT("%s: base vertex %d sits at the arrow radius"), Name, Around),
                FVector2D{Base.Y, Base.Z}.Size(), static_cast<double>(ArrowRadius), 0.001);

            const auto& ShaftEnd = Buffers._Positions[(ShapeMaxPoints - 1) * ProfileRingSize + Around];
            TestNearlyEqual(FString::Printf(TEXT("%s: the shaft stops at the base of the head (vertex %d)"), Name, Around),
                ShaftEnd.X, 180.0, 0.001);
            TestNearlyEqual(FString::Printf(TEXT("%s: the shaft keeps its own radius at the base (vertex %d)"), Name, Around),
                FVector2D{ShaftEnd.Y, ShaftEnd.Z}.Size(), static_cast<double>(ShaftRadius), 0.001);
        }

        TestNearlyEqual(FString::Printf(TEXT("%s: the tip UV carries the full length"), Name),
            Buffers._UV0[TipRing * ProfileRingSize].X, 200.0, 0.001);
        TestNearlyEqual(FString::Printf(TEXT("%s: the base UV carries its distance"), Name),
            Buffers._UV0[BaseRing * ProfileRingSize].X, 180.0, 0.001);

        // The first face between the base and the tip shows its front outward on a tube, upward on a ribbon.
        const auto Triangles = ck::pmg::polyline::Build_Triangles(Shape);
        const auto FacesPerGap = IsRibbon ? 1 : Sides;
        const auto FirstHeadTriangle = BaseRing * FacesPerGap * 2;
        const auto FrontNormal = Get_FrontNormal(Buffers._Positions, Triangles, FirstHeadTriangle);
        const auto& HeadA = Buffers._Positions[Triangles[FirstHeadTriangle * 3]];
        const auto Expected = IsRibbon ? FVector::UpVector : FVector{0.0, HeadA.Y, HeadA.Z}.GetSafeNormal();
        TestTrue(FString::Printf(TEXT("%s: the first face of the head shows its front %s"), Name,
            IsRibbon ? TEXT("upward") : TEXT("outward")), FVector::DotProduct(FrontNormal, Expected) > 0.0);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkPmg_Polyline_Geometry_AnArrowLongerThanThePathIsAllHead,
    "Ck.Pmg.Polyline.Geometry.AnArrowLongerThanThePathIsAllHead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPmg_Polyline_Geometry_AnArrowLongerThanThePathIsAllHead::RunTest(const FString& Parameters)
{
    using namespace ck_pmg_polyline_geometry_spec;

    constexpr auto ArrowRadius = 3.0f;
    constexpr auto ShapeMaxPoints = 6;
    const auto Shape = Make_Shape(ECk_Pmg_Polyline_Profile::Ribbon, 1.0f, ShapeMaxPoints, 20.0f, ArrowRadius);
    const auto Points = TArray<FVector>{FVector{0.0, 0.0, 0.0}, FVector{10.0, 0.0, 0.0}};
    const auto Buffers = ck::pmg::polyline::Build(Points, Shape);

    TestEqual(TEXT("the path is drawn"), Buffers._NumDrawnPoints, 2);
    TestNearlyEqual(TEXT("its length is the length of the path"), Buffers._Length, 10.0, 0.001);

    for (auto Ring = 0; Ring < ShapeMaxPoints; ++Ring)
    {
        for (auto Across = 0; Across < 2; ++Across)
        {
            TestNearlyEqual(FString::Printf(TEXT("shaft ring %d edge %d sits at the start: no shaft"), Ring, Across),
                Buffers._Positions[Ring * 2 + Across].X, 0.0, 0.001);
        }
    }

    TestNearlyEqual(TEXT("the base spans the width of the arrow at the start"),
        FVector::Dist(Buffers._Positions[ShapeMaxPoints * 2], Buffers._Positions[ShapeMaxPoints * 2 + 1]),
        2.0 * ArrowRadius, 0.001);
    TestTrue(TEXT("the tip is the last point"),
        Buffers._Positions[(ShapeMaxPoints + 1) * 2].Equals(FVector{10.0, 0.0, 0.0}, 0.001));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------
