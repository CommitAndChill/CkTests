struct FCk_HexGridGym_Cell
{
    FCk_HexCoord Coordinate;
    FVector Center;
    TArray<FVector> Corners;
}

class ACk_HexGridGeometryGym_PlayerController : ACk_Gym_Base_PlayerController
{
    private FString _StationTag = "Gym.HexGrid.Geometry";
    private TArray<FCk_HexGridGym_Cell> _PointyCells;
    private TArray<FCk_HexGridGym_Cell> _FlatCells;
    private FLinearColor _PointyColor = FLinearColor(0.25, 0.80, 1.0);
    private FLinearColor _FlatColor = FLinearColor(0.55, 0.95, 0.35);
    private FLinearColor _SelectionColor = FLinearColor(1.0, 0.65, 0.12);
    private FCk_HexCoord _SelectedOffset;
    private int32 _Turn = 0;
    private FString _Verdict = "Preparing";

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(FName(_StationTag));
        Station.Title = FText::FromString("Hex Grid Geometry");
        Station.Description.Add(FText::FromString("Pointy (blue) and flat (green) use the same axial coordinates."));
        Station.Description.Add(FText::FromString("Step the positive 60-degree turn to move the selected cell."));

        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        Stations.Add(Station);
        return Stations;
    }

    void Request_StartGym() override
    {
        auto Station = Get_StationHandle(_StationTag);
        if (ck::Is_NOT_Valid(Station))
        {
            _Verdict = "Waiting for the geometry station";
            return;
        }

        auto Anchors = Station.Get_Fragment(FCkGym_Station_Anchors);
        auto Pivot = Anchors.FootprintCenter + FVector(0.0, 0.0, 20.0);
        auto Domain = FCk_HexDomain();
        Domain.Set_RingRadius(1);
        auto Coordinates = TArray<FCk_HexCoord>();
        auto Failure = ECk_HexGeometryFailure::None;
        _PointyCells.Reset();
        _FlatCells.Reset();
        if (utils_hex_geometry::TryResolve_Domain(Domain, Coordinates, Failure) == false)
        {
            _Verdict = "Domain rejected";
            return;
        }

        auto Pointy = TArray<FCk_HexGridGym_Cell>();
        auto Flat = TArray<FCk_HexGridGym_Cell>();
        if (TryBuildChart(Coordinates, ECk_HexOrientation::Pointy, Pivot + FVector(0.0, -135.0, 0.0), Pointy)
            == false || TryBuildChart(Coordinates, ECk_HexOrientation::Flat, Pivot + FVector(0.0, 135.0, 0.0), Flat)
            == false)
        {
            _Verdict = "Projection failed";
            return;
        }

        _PointyCells = Pointy;
        _FlatCells = Flat;
        SetTurn(0);
        SetActorTickEnabled(true);

        auto ViewPawn = GetControlledPawn();
        if (ck::IsValid(ViewPawn))
        {
            auto Eye = Pivot + FVector(-360.0, 0.0, 500.0);
            ViewPawn.SetActorLocation(Eye);
            SetControlRotation(FRotator::MakeFromX(Pivot - Eye));
        }
    }

    private bool TryBuildChart(const TArray<FCk_HexCoord>&in InCoordinates, ECk_HexOrientation InOrientation,
        const FVector&in InPivot, TArray<FCk_HexGridGym_Cell>& OutCells)
    {
        auto Layout = FCk_HexLayout();
        Layout.Set_Orientation(InOrientation);
        Layout.Set_OuterRadius(36.0);
        auto Failure = ECk_HexGeometryFailure::None;

        for (auto Coordinate : InCoordinates)
        {
            auto Center = FVector2D();
            auto Corners = TArray<FVector2D>();
            if (utils_hex_geometry::TryGet_LocalCenter(Coordinate, Layout, Center, Failure) == false
                || utils_hex_geometry::TryGet_LocalCorners(Coordinate, Layout, Corners, Failure) == false
                || Corners.Num() != 6)
            { return false; }

            auto Cell = FCk_HexGridGym_Cell();
            Cell.Coordinate = Coordinate;
            Cell.Center = InPivot + FVector(Center.X, Center.Y, 0.0);
            for (auto Corner : Corners)
            {
                Cell.Corners.Add(InPivot + FVector(Corner.X, Corner.Y, 0.0));
            }
            OutCells.Add(Cell);
        }
        return true;
    }

    private void SetTurn(int32 InTurn)
    {
        auto Offset = FCk_HexCoord();
        Offset.Set_Q(1);
        auto Failure = ECk_HexGeometryFailure::None;
        auto Selected = FCk_HexCoord();
        if (utils_hex_geometry::TryRotate(Offset, ECk_HexRotation(InTurn), Selected, Failure) == false)
        {
            _Verdict = "Turn rejected";
            return;
        }
        _Turn = InTurn;
        _SelectedOffset = Selected;
        _Verdict = "Ready";
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float InDeltaSeconds)
    {
        if (Get_IsGeometryReady() == false)
        { return; }
        DrawChart(_PointyCells, _PointyColor);
        DrawChart(_FlatCells, _FlatColor);
    }

    private void DrawChart(const TArray<FCk_HexGridGym_Cell>&in InCells, const FLinearColor&in InColor)
    {
        for (const auto& Cell : InCells)
        {
            for (auto Index = 0; Index < Cell.Corners.Num(); ++Index)
            {
                auto Next = (Index + 1) % Cell.Corners.Num();
                utils_debug_draw::DrawDebugLine(Cell.Corners[Index], Cell.Corners[Next], InColor, 0.0f, 2.0f);
            }

            if (Cell.Coordinate._Q == _SelectedOffset._Q && Cell.Coordinate._R == _SelectedOffset._R)
            {
                // Inset the selection so it remains readable against the neighbors' common edges.
                for (auto Index = 0; Index < Cell.Corners.Num(); ++Index)
                {
                    auto Next = (Index + 1) % Cell.Corners.Num();
                    auto Start = Cell.Center + (Cell.Corners[Index] - Cell.Center) * 0.82;
                    auto End = Cell.Center + (Cell.Corners[Next] - Cell.Center) * 0.82;
                    utils_debug_draw::DrawDebugLine(Start, End, _SelectionColor, 0.0f, 4.0f);
                }
            }
        }
    }

    FString Get_ControlPanelTitle() override
    { return "Hex Grid Geometry"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        auto Ready = Get_IsGeometryReady();
        Rows.Add(CkGym_Control::Status("Verdict", _Verdict, Ready == false));
        Rows.Add(CkGym_Control::Cycle(EKeys::T, "T", "Rotate +60 degrees", Get_TurnLabel(), false, Ready));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset turn", Ready));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex == 1)
        { Ck_GymHexGrid_NextTurn(); }
        else if (InRowIndex == 2)
        { Ck_GymHexGrid_ResetTurn(); }
    }

    UFUNCTION(Exec)
    void Ck_GymHexGrid_NextTurn()
    {
        if (Get_IsGeometryReady())
        { SetTurn((_Turn + 1) % 6); }
    }

    UFUNCTION(Exec)
    void Ck_GymHexGrid_ResetTurn()
    {
        if (Get_IsGeometryReady())
        { SetTurn(0); }
    }

    UFUNCTION()
    bool Get_IsGeometryReady() const
    {
        return _Verdict == "Ready" && _PointyCells.Num() == 7 && _FlatCells.Num() == 7;
    }

    UFUNCTION()
    FString Get_TurnLabel() const
    {
        return f"{_Turn * 60} degrees ({_SelectedOffset._Q},{_SelectedOffset._R})";
    }

    UFUNCTION()
    FString Run_GeometryBindingProbe()
    {
        return CkHexGridGeometry_BindingProbe::RunChecks();
    }
}
