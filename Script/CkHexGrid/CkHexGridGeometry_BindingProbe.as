// Invoked from the live geometry gym by Ck.HexGrid.Geometry.AngelScriptAndGym.
namespace CkHexGridGeometry_BindingProbe
{
    FString RunChecks()
    {
        auto Offset = FCk_HexCoord();
        Offset.Set_Q(1);
        Offset.Set_R(-2);
        auto Turned = FCk_HexCoord();
        auto Failure = ECk_HexGeometryFailure::InvalidRotation;
        if (utils_hex_geometry::TryRotate(Offset, ECk_HexRotation::Turn1, Turned, Failure) == false
            || Turned._Q != 2 || Turned._R != -1 || Failure != ECk_HexGeometryFailure::None)
        { return "TryRotate did not return (2,-1) and None."; }

        auto Domain = FCk_HexDomain();
        Domain.Set_RingRadius(1);
        auto Cells = TArray<FCk_HexCoord>();
        if (utils_hex_geometry::TryResolve_Domain(Domain, Cells, Failure) == false
            || Cells.Num() != 7 || Failure != ECk_HexGeometryFailure::None)
        { return "TryResolve_Domain did not return seven cells and None."; }
        if (Cells[0]._Q != 0 || Cells[0]._R != -1 || Cells[6]._Q != 0 || Cells[6]._R != 1)
        { return "TryResolve_Domain did not preserve canonical signed endpoints."; }

        Domain.Set_RingRadius(-1);
        if (utils_hex_geometry::TryResolve_Domain(Domain, Cells, Failure)
            || Cells.Num() != 0 || Failure != ECk_HexGeometryFailure::InvalidDomain)
        { return "TryResolve_Domain did not reject a negative radius and clear populated output."; }

        auto Layout = FCk_HexLayout();
        Layout.Set_Orientation(ECk_HexOrientation::Flat);
        Layout.Set_OuterRadius(40.0);
        auto Pivot = FTransform::Identity;
        Pivot.SetLocation(FVector(120.0, -300.0, 15.0));
        auto ExpectedHit = FVector(180.0, -300.0 + 20.0 * Math::Sqrt(3.0), 15.0);
        auto ActualHit = FVector::ZeroVector;
        if (utils_hex_geometry::TryGet_CoordFromWorldRay(ExpectedHit + FVector(0.0, 0.0, 50.0),
            FVector(0.0, 0.0, -10.0), Pivot, Layout, Turned, ActualHit, Failure) == false
            || Turned._Q != 1 || Turned._R != 0 || ActualHit.Equals(ExpectedHit, 0.000001) == false
            || Failure != ECk_HexGeometryFailure::None)
        { return "TryGet_CoordFromWorldRay did not return the flat (1,0) cell and plane hit."; }

        if (utils_hex_geometry::TryGet_CoordFromWorldRay(ExpectedHit + FVector(0.0, 0.0, 50.0),
            FVector(1.0, 0.0, 0.0), Pivot, Layout, Turned, ActualHit, Failure)
            || Turned._Q != 0 || Turned._R != 0 || ActualHit != FVector::ZeroVector
            || Failure != ECk_HexGeometryFailure::ParallelRay)
        { return "TryGet_CoordFromWorldRay did not reject a parallel ray and clear populated outputs."; }

        return "Geometry binding checks passed";
    }
}
