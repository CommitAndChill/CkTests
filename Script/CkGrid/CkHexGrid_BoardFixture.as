class UCk_HexGrid_BoardFixture : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private TArray<FCk_Handle_HexGrid> _RetiredBoards;
    private TArray<FCk_Handle_HexCell> _RetainedCells;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        for (int Mask = 0; Mask < 2; ++Mask)
        {
            for (int Orientation = 0; Orientation < 2; ++Orientation)
            {
                for (int Pose = 0; Pose < 2; ++Pose)
                {
                    auto Pivot = FTransform::Identity;
                    if (Pose == 1)
                    { Pivot = FTransform(FRotator(15.0, 40.0, 20.0), FVector(310,-90,72), FVector(2,2,2)); }
                    if (!ExerciseBoard(InHandle, Mask == 1, ECk_HexOrientation(Orientation), Pivot))
                    {
                        FinishFailure(f"Board API failed for explicit={Mask}, orientation={Orientation}, pose={Pose}");
                        return;
                    }
                }
            }
        }
        for (int Index = 0; Index < _RetiredBoards.Num(); ++Index)
        {
            FCk_Handle_HexGrid Board = _RetiredBoards[Index];
            auto Entity = Board.H();
            utils_entity_lifetime::Request_DestroyEntity(Entity);
        }
        utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"CheckRetired"));
    }

    private bool ExerciseBoard(FCk_Handle Owner, bool Explicit, ECk_HexOrientation Orientation, FTransform Pivot)
    {
        auto Coordinate = FCk_HexCoord();
        Coordinate.Set_Q(1);
        auto Domain = FCk_HexDomain();
        if (Explicit)
        {
            auto Cells = TArray<FCk_HexCoord>();
            Cells.Add(Coordinate);
            Domain.Set_Kind(ECk_HexDomainKind::Explicit);
            Domain.Set_Coordinates(Cells);
        }
        else
        { Domain.Set_RingRadius(2); }
        auto Layout = FCk_HexLayout();
        Layout.Set_Orientation(Orientation);
        auto Spec = FCk_HexGrid_Spec();
        Spec.Set_Domain(Domain);
        Spec.Set_Layout(Layout);
        auto Disabled = TArray<FCk_HexCoord>();
        Disabled.Add(Coordinate);
        Spec.Set_InitialDisabledCells(Disabled);
        auto Board = FCk_Handle_HexGrid();
        auto Reason = ECk_HexBoardFailure::InvalidHandle;
        if (!utils_hex_grid::TryCreate(Owner, Pivot, Spec, Board, Reason) || !ck::IsValid(Board)
            || !ck::IsValid(Board.H()) || Board.Get_Cells().Num() != (Explicit ? 1 : 19))
        { return false; }
        _RetiredBoards.Add(Board);
        auto Cell = FCk_Handle_HexCell();
        auto Actual = FCk_HexCoord();
        bool IsDisabled = false;
        auto Tags = FGameplayTagContainer();
        auto Parent = FCk_Handle_HexGrid();
        if (!Board.TryGet_CellAt(Coordinate, Cell, Reason) || !ck::IsValid(Cell)
            || !Cell.TryGet_Coordinate(Actual) || Actual._Q != 1 || Actual._R != 0
            || !Cell.TryGet_Grid(Parent) || Parent.H() != Board.H()
            || !Cell.TryGet_Tags(Tags) || !Cell.TryGet_IsDisabled(IsDisabled) || !IsDisabled)
        { return false; }
        _RetainedCells.Add(Cell);
        auto Local = Orientation == ECk_HexOrientation::Pointy
            ? FVector(Math::Sqrt(3.0) * 100.0, 0, 0) : FVector(150, Math::Sqrt(3.0) * 50.0, 0);
        auto Center = Pivot.TransformPosition(Local);
        auto Normal = Pivot.TransformVector(FVector(0,0,1));
        auto Corners = TArray<FVector>();
        auto Spatial = ECk_HexGeometryFailure::InvalidCoordinate;
        auto Pick = FCk_Handle_HexCell();
        auto Hit = FVector::ZeroVector;
        if (!Board.TryGet_CellWorldCorners(Coordinate, Corners, Reason) || Corners.Num() != 6
            || !Board.TryGet_CellFromWorldPoint(Center, Pick, Spatial, Reason) || Pick.H() != Cell.H()
            || !Board.TryGet_CellFromWorldRay(Center + Normal * 20, -Normal, Pick, Hit, Spatial, Reason)
            || Pick.H() != Cell.H() || !Hit.Equals(Center, 0.000001))
        { return false; }
        return !Explicit || (!Board.TryGet_CellFromWorldPoint(Pivot.GetLocation(), Pick, Spatial, Reason)
            && Reason == ECk_HexBoardFailure::MissingCell && Spatial == ECk_HexGeometryFailure::OutsideDomain);
    }

    UFUNCTION()
    private void CheckRetired(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        for (auto Board : _RetiredBoards)
        { if (ck::IsValid(Board)) { return; } }
        for (auto Cell : _RetainedCells)
        {
            // Generic board validity ends at teardown; its registry is released at final destruction.
            // The AutoTest deadline rejects a cell that stays alive indefinitely.
            if (ck::IsValid(Cell)) { return; }
            auto Coordinate = FCk_HexCoord();
            if (Cell.TryGet_Coordinate(Coordinate))
            { FinishFailure("A stale cell still returned a coordinate"); return; }
        }
        FinishSuccess();
    }
}
