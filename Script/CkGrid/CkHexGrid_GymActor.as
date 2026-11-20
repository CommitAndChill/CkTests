class ACk_HexBoardGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_HexBoardGym_Controller;
}

class ACk_HexBoardGym_Controller : ACk_Gym_Base_PlayerController
{
    private FCk_Handle_HexGrid _Board;
    private FVector _Base;
    private FVector2D _TargetCenter;
    private bool _Flat = false;
    private bool _Moved = false;
    private bool _Saved = false;
    private FTransform _LocalPivot;
    private UCk_HexGrid_AuthoringSpec _SavedAsset;
    private TArray<FCk_HexCoord> _SavedCoordinates;
    private FCk_HexLayout _ActiveLayout;
    private bool _Ready = false;
    private bool _SavedFramePending = false;
    private bool _HaveProceduralView = false;
    private FVector _ProceduralEye;
    private FRotator _ProceduralLook;
    private FString _CellState = "";
    private int _Probe = 0;
    private FString _Status = "Preparing";

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.HexBoard");
        Station.Title = FText::FromString("Hex Board");
        Station.Description.Add(FText::FromString("Six edges, a missing center cell and an exterior point."));
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        Stations.Add(Station);
        return Stations;
    }

    void Request_StartGym() override
    {
        auto Station = Get_StationHandle("Gym.HexBoard");
        if (!ck::IsValid(Station)) { return; }
        _Base = Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter + FVector(0,0,25);
        Rebuild();
        auto Pawn = GetControlledPawn();
        if (ck::IsValid(Pawn))
        {
            auto Eye = _Base + FVector(-120,0,630);
            Pawn.SetActorLocation(Eye);
            SetControlRotation(FRotator::MakeFromX(_Base - Eye));
        }
        SetActorTickEnabled(true);
    }

    private FVector Origin() const
    { return _Moved ? _Base + FVector(60, 75, 0) : _Base; }

    private void Rebuild()
    {
        _Ready = false;
        _CellState = "";
        auto StationEntity = Get_StationHandle("Gym.HexBoard");
        auto Layout = FCk_HexLayout();
        Layout.Set_Orientation(_Flat ? ECk_HexOrientation::Flat : ECk_HexOrientation::Pointy);
        Layout.Set_OuterRadius(48.0);
        auto Domain = FCk_HexDomain();
        Domain.Set_RingRadius(1);
        TArray<FCk_HexCoord> Disk;
        auto GeometryFailure = ECk_HexGeometryFailure::None;
        auto Target = FCk_HexCoord();
        Target.Set_Q(1);
        if (!utils_hex_geometry::TryResolve_Domain(Domain, Disk, GeometryFailure)) { return; }
        TArray<FCk_HexCoord> Ring;
        for (auto C : Disk)
        { if (C._Q != 0 || C._R != 0) { Ring.Add(C); } }
        Domain.Set_RingRadius(0);
        Domain.Set_Kind(ECk_HexDomainKind::Explicit);
        Domain.Set_Coordinates(Ring);
        auto Spec = FCk_HexGrid_Spec();
        Spec.Set_Domain(Domain);
        Spec.Set_Layout(Layout);
        auto Failure = ECk_HexBoardFailure::None;
        if (_Saved)
        {
            if (!ck::IsValid(_SavedAsset))
            {
                _SavedAsset = Cast<UCk_HexGrid_AuthoringSpec>(LoadObject(this,
                    "/CkTests/CkGrid/HexGrid/DA_HexGrid_PaintGym.DA_HexGrid_PaintGym"));
            }
            if (!ck::IsValid(_SavedAsset) || !_SavedAsset.TryResolve(Spec, Failure))
            { _Status = "Saved recipe unavailable"; return; }
            if (!utils_hex_geometry::TryResolve_Domain(Spec._Domain, _SavedCoordinates, GeometryFailure)
                || _SavedCoordinates.IsEmpty())
            { _Status = "Saved recipe has no cells"; return; }
            Target = _SavedCoordinates[_Probe % _SavedCoordinates.Num()];
        }
        _LocalPivot = Spec._Pivot;
        _ActiveLayout = Spec._Layout;
        if (!utils_hex_geometry::TryGet_LocalCenter(Target, Spec._Layout, _TargetCenter, GeometryFailure)) { return; }
        FCk_Handle_HexGrid NewBoard;
        if (!utils_hex_grid::TryCreate(StationEntity, FTransform(Origin()), Spec, NewBoard, Failure))
        { _Status = "Construction failed"; return; }
        if (ck::IsValid(_Board))
        {
            auto Previous = _Board.H();
            utils_entity_lifetime::Request_DestroyEntity(Previous);
        }
        _Board = NewBoard;
        _Ready = true;
        _SavedFramePending = _Saved;
    }

    private bool FrameSavedBoard()
    {
        auto Pawn = GetControlledPawn();
        if (!ck::IsValid(Pawn) || !ck::IsValid(PlayerCameraManager)) { return false; }
        TArray<FVector> Points;
        auto Failure = ECk_HexBoardFailure::None;
        auto Center = FVector::ZeroVector;
        for (auto Coordinate : _SavedCoordinates)
        {
            TArray<FVector> Corners;
            if (!_Board.TryGet_CellWorldCorners(Coordinate, Corners, Failure)) { return false; }
            for (auto Corner : Corners) { Points.Add(Corner); Center += Corner; }
        }
        if (Points.IsEmpty()) { return false; }
        Center /= Points.Num();
        auto Radius = 1.0;
        for (auto Point : Points) { Radius = Math::Max(Radius, (Point - Center).Size()); }
        int32 Width = 0;
        int32 Height = 0;
        GetViewportSize(Width, Height);
        if (Width <= 0 || Height <= 0) { return false; }
        auto HalfFov = Math::DegreesToRadians(PlayerCameraManager.GetFOVAngle() * 0.5);
        auto VerticalHalfFov = Math::Atan(Math::Tan(HalfFov) * float(Height) / float(Width));
        auto Distance = Radius * 1.65 / Math::Sin(Math::Min(HalfFov, VerticalHalfFov));
        auto ViewAxis = _LocalPivot.GetRotation().RotateVector(FVector(-0.15, 0.0, 1.0)).GetSafeNormal();
        auto Eye = Center + ViewAxis * Distance;
        if (!_HaveProceduralView)
        {
            _ProceduralEye = Pawn.GetActorLocation();
            _ProceduralLook = GetControlRotation();
            _HaveProceduralView = true;
        }
        Pawn.SetActorLocation(Eye);
        SetControlRotation(FRotator::MakeFromX(Center - Eye));
        return true;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!_Ready || !ck::IsValid(_Board)) { return; }
        auto Options = FCk_HexGrid_DebugDraw_Options();
        Options.Set_CellColor(FLinearColor(0.1, 0.85, 1.0));
        Options.Set_LineThickness(2.5f);
        _Board.DebugDraw_Grid(Options);
        auto Target = FCk_HexCoord();
        Target.Set_Q(1);
        if (_Saved)
        {
            Target = _SavedCoordinates[_Probe % _SavedCoordinates.Num()];
            auto GeometryFailure = ECk_HexGeometryFailure::None;
            if (!utils_hex_geometry::TryGet_LocalCenter(Target, _ActiveLayout, _TargetCenter, GeometryFailure))
            { _Status = "Saved coordinate unavailable"; return; }
        }
        TArray<FVector> Corners;
        auto Failure = ECk_HexBoardFailure::None;
        if (!_Board.TryGet_CellWorldCorners(Target, Corners, Failure) || Corners.Num() != 6) { return; }
        FVector ActualCenter;
        auto CenterOffset = FVector(_TargetCenter.X, _TargetCenter.Y, 0.0);
        if (!_Board.TryGet_CellWorldCenter(Target, ActualCenter, Failure)
            || !ActualCenter.Equals((_LocalPivot * FTransform(Origin())).TransformPosition(CenterOffset), 0.001))
        { _Status = "Moving"; return; }
        if (_SavedFramePending)
        {
            if (!FrameSavedBoard()) { _Status = "Framing saved board"; return; }
            _SavedFramePending = false;
        }
        auto Point = (_LocalPivot * FTransform(Origin())).TransformPosition(FVector::ZeroVector);
        if (_Saved)
        { Point = ActualCenter; }
        else if (_Probe < 6)
        { Point = (Corners[_Probe] + Corners[(_Probe + 1) % 6]) * 0.5; }
        else if (_Probe == 7)
        { Point += FVector(230,0,0); }
        utils_debug_draw::DrawDebugLine(Point, Point + FVector(0,0,35), FLinearColor::Red, 0.0f, 4.0f);
        FCk_Handle_HexCell Cell;
        auto Spatial = ECk_HexGeometryFailure::None;
        if (!_Board.TryGet_CellFromWorldPoint(Point, Cell, Spatial, Failure))
        { _Status = Failure == ECk_HexBoardFailure::MissingCell ? "Outside domain" : "Spatial query failed"; return; }
        auto Pick = FCk_HexCoord();
        if (!Cell.TryGet_Coordinate(Pick) || !_Board.TryGet_CellWorldCorners(Pick, Corners, Failure)) { return; }
        _Status = f"Cell ({Pick._Q},{Pick._R})";
        if (_Saved)
        {
            bool Disabled = false;
            FGameplayTagContainer Tags;
            if (!Cell.TryGet_IsDisabled(Disabled) || !Cell.TryGet_Tags(Tags))
            { _CellState = "Cell state unavailable"; }
            else
            {
                auto TagNames = "";
                for (auto Tag : Tags.GameplayTags)
                { TagNames = TagNames.IsEmpty() ? f"{Tag}" : f"{TagNames}, {Tag}"; }
                if (TagNames.IsEmpty()) { TagNames = "none"; }
                auto EnabledLabel = Disabled ? "Disabled" : "Enabled";
                _CellState = f"{EnabledLabel} | Tags: {TagNames}";
            }
        }
        for (int I = 0; I < Corners.Num(); ++I)
        { utils_debug_draw::DrawDebugLine(Corners[I], Corners[(I+1)%6], FLinearColor::Yellow, 0.0f, 5.0f); }
    }

    FString Get_ControlPanelTitle() override { return "Hex Board"; }

    UFUNCTION()
    FString Get_PickStatus() const { return _Status; }

    UFUNCTION(Exec)
    void Ck_HexBoard_Rebuild() { Request_ControlActivated(1); }

    UFUNCTION(Exec)
    void Ck_HexBoard_Move() { Request_ControlActivated(2); }

    UFUNCTION(Exec)
    void Ck_HexBoard_NextProbe() { Request_ControlActivated(3); }

    UFUNCTION(Exec)
    void Ck_HexBoard_SavedRecipe() { Request_ControlActivated(4); }

    UFUNCTION()
    FCk_Handle_HexGrid Get_Board() const { return _Board; }

    UFUNCTION()
    bool Get_UsesSavedRecipe() const { return _Saved; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Status("Pick", _Status, false));
        Rows.Add(CkGym_Control::Cycle(EKeys::T, "T", "Rebuild", _Saved ? "Reload saved" : (_Flat ? "Flat" : "Pointy"), false, true));
        Rows.Add(CkGym_Control::Action(EKeys::M, "M", "Move board", ck::IsValid(_Board)));
        auto Label = _Probe == 6 ? "Hole" : (_Probe == 7 ? "Exterior" : f"Edge {_Probe+1}");
        if (_Saved && !_SavedCoordinates.IsEmpty())
        { Label = f"Saved cell {(_Probe % _SavedCoordinates.Num()) + 1}/{_SavedCoordinates.Num()}"; }
        Rows.Add(CkGym_Control::Cycle(EKeys::E, "E", "Pick", Label, false, true));
        Rows.Add(CkGym_Control::Cycle(EKeys::P, "P", "Recipe", _Saved ? "Saved paint asset" : "Procedural", false, true));
        if (_Saved) { Rows.Add(CkGym_Control::Status("Saved cell", _CellState, false)); }
        return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        _Status = "Preparing";
        if (InRowIndex == 1) { _Flat = !_Flat; Rebuild(); }
        else if (InRowIndex == 2 && ck::IsValid(_Board))
        {
            _Moved = !_Moved;
            _Board.H().Request_SetLocation(FCk_Request_Transform_SetLocation(Origin()));
        }
        else if (InRowIndex == 3)
        { _Probe = (_Probe + 1) % (_Saved && !_SavedCoordinates.IsEmpty() ? _SavedCoordinates.Num() : 8); }
        else if (InRowIndex == 4)
        {
            _Saved = !_Saved;
            Rebuild();
            if (!_Saved && _HaveProceduralView)
            {
                auto Pawn = GetControlledPawn();
                if (ck::IsValid(Pawn)) { Pawn.SetActorLocation(_ProceduralEye); }
                SetControlRotation(_ProceduralLook);
                _HaveProceduralView = false;
            }
        }
    }
}
