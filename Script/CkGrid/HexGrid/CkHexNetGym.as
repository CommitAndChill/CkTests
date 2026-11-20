class ACk_HexNetGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_HexNetGym_Controller;
}

class ACk_HexNetGym_Controller : ACk_Gym_Base_PlayerController
{
    default Replicates = true;

    private ACk_AutoTest_NetSubject_MixedGrid_UE _BoardActor;
    private ACk_AutoTest_NetSubject_MixedGridObject_UE _PieceActor;
    private bool _Framed = false;
    private FString _BoardStatus = "Waiting for board";
    private FString _Cells = "";
    private FString _Binding = "";
    private FGuid _PlacementId;

    private bool IsHostWorld() const { return GetWorld().GetNetMode() != ENetMode::NM_Client; }

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        if (IsHostWorld() && IsLocalController()) { Super::BeginPlay(); }
        else { SetActorTickEnabled(IsLocalController()); }
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        FCkGym_Station_SpawnParams_Payload Station;
        Station.Tags.Add(n"Gym.HexNet");
        Station.Title = FText::FromString("Hex Replication");
        Station.Description.Add(FText::FromString("Place or move the piece on the host. Join another client and compare its placement ID and occupied cells."));
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        Stations.Add(Station);
        return Stations;
    }

    private void FindSubjects()
    {
        if (!ck::IsValid(_BoardActor))
        {
            TArray<ACk_AutoTest_NetSubject_MixedGrid_UE> Boards;
            GetAllActorsOfClass(ACk_AutoTest_NetSubject_MixedGrid_UE, Boards);
            if (!Boards.IsEmpty()) { _BoardActor = Boards[0]; }
        }
        if (!ck::IsValid(_PieceActor))
        {
            TArray<ACk_AutoTest_NetSubject_MixedGridObject_UE> Pieces;
            GetAllActorsOfClass(ACk_AutoTest_NetSubject_MixedGridObject_UE, Pieces);
            if (!Pieces.IsEmpty()) { _PieceActor = Pieces[0]; }
        }
    }

    void Request_StartGym() override
    {
        SetActorTickEnabled(IsLocalController());
        if (!IsHostWorld() || !IsLocalController()) { return; }
        auto Station = Get_StationHandle("Gym.HexNet");
        if (!ck::IsValid(Station)) { return; }
        auto Pivot = Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter + FVector(0,0,25);
        FindSubjects();
        if (!ck::IsValid(_BoardActor)) { SpawnActor(ACk_AutoTest_NetSubject_MixedGrid_UE, Pivot); }
        if (!ck::IsValid(_PieceActor)) { SpawnActor(ACk_AutoTest_NetSubject_MixedGridObject_UE, Pivot); }
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!IsLocalController()) { return; }
        FindSubjects();
        _PlacementId = FGuid();
        _Cells = "";
        _Binding = "";
        _BoardStatus = "Waiting for board";
        if (!ck::IsValid(_BoardActor)) { return; }
        auto Board = _BoardActor.Get_HexGrid();
        if (!ck::IsValid(Board)) { return; }
        auto Readiness = utils_hex_grid::Get_Readiness(Board);
        if (Readiness._State != ECk_HexGridReadiness::Ready)
        {
            _BoardStatus = f"{Readiness._State} ({Readiness._Failure})";
            return;
        }
        _BoardStatus = "Ready";
        auto Pawn = GetControlledPawn();
        if (!_Framed && ck::IsValid(Pawn))
        {
            auto Pivot = _BoardActor.GetActorLocation();
            auto Eye = Pivot + FVector(-160,0,600);
            Pawn.SetActorLocation(Eye);
            SetControlRotation(FRotator::MakeFromX(Pivot - Eye));
            _Framed = true;
        }
        FCk_HexGrid_DebugDraw_Options Options;
        Options.Set_LineThickness(2.0f);
        Board.DebugDraw_Grid(Options);
        auto Entries = utils_hex_grid_occupancy::Get_Placements(Board);
        if (!Entries._Succeeded || Entries._Placements.IsEmpty()) { return; }
        auto Piece = Entries._Placements[0];
        _PlacementId = Piece._PlacementId;
        _Binding = f"{Piece._OccupantState}";
        auto Failure = ECk_HexBoardFailure::None;
        for (auto Cell : Piece._Cells)
        {
            if (!_Cells.IsEmpty()) { _Cells += ", "; }
            _Cells += f"({Cell._Q}, {Cell._R})";
            TArray<FVector> Corners;
            if (!Board.TryGet_CellWorldCorners(Cell, Corners, Failure)) { continue; }
            for (int I = 0; I < Corners.Num(); ++I)
            {
                utils_debug_draw::DrawDebugLine(Corners[I], Corners[(I+1)%Corners.Num()], FLinearColor::Yellow, 0.0f, 5.0f);
            }
        }
    }

    FString Get_ControlPanelTitle() override { return "Hex Replication"; }
    UFUNCTION()
    FString Get_NetBoardStatus() const { return _BoardStatus; }
    UFUNCTION()
    FGuid Get_NetPlacementId() const { return _PlacementId; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Status("Role", IsHostWorld() ? "Authority" : "Client", false));
        Rows.Add(CkGym_Control::Status("Board", _BoardStatus, false));
        Rows.Add(CkGym_Control::Status("Placement ID", _PlacementId.IsValid() ? _PlacementId.ToString(EGuidFormats::Digits) : "None", false));
        Rows.Add(CkGym_Control::Status("Occupied cells", _Cells, false));
        Rows.Add(CkGym_Control::Status("Occupant", _Binding, false));
        if (IsHostWorld())
        {
            bool CanDrive = _BoardStatus == "Ready" && ck::IsValid(_PieceActor) && ck::IsValid(_PieceActor.Get_HexObject());
            Rows.Add(CkGym_Control::Action(EKeys::P, "P", "Place piece", CanDrive && !_PlacementId.IsValid()));
            Rows.Add(CkGym_Control::Action(EKeys::M, "M", "Move piece", CanDrive && _PlacementId.IsValid()));
            Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Remove piece", CanDrive && _PlacementId.IsValid()));
        }
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsHostWorld() || !ck::IsValid(_BoardActor) || !ck::IsValid(_PieceActor)) { return; }
        auto Board = _BoardActor.Get_HexGrid();
        if (!utils_hex_grid::Get_IsReady(Board) || !ck::IsValid(_PieceActor.Get_HexObject())) { return; }
        auto Entries = utils_hex_grid_occupancy::Get_Placements(Board);
        if (!Entries._Succeeded) { return; }
        if (Entries._Placements.IsEmpty())
        {
            if (InRowIndex == 5) { _BoardActor.Request_PlaceHex(_PieceActor, FCk_HexCoord(), ECk_HexRotation::Turn0); }
            return;
        }
        auto Piece = Entries._Placements[0];
        if (InRowIndex == 6)
        {
            FCk_HexCoord Anchor;
            Anchor.Set_Q(Piece._Anchor._Q == 0 ? 1 : 0);
            utils_hex_grid_placement::Request_Move(Board, FCk_Request_HexGridPlacement_Move(Piece._PlacementId, Anchor, ECk_HexRotation::Turn0));
        }
        else if (InRowIndex == 7)
        {
            utils_hex_grid_placement::Request_Remove(Board, FCk_Request_HexGridPlacement_Remove(Piece._PlacementId));
        }
    }

    UFUNCTION(Exec)
    void Ck_HexNet_Place() { Request_ControlActivated(5); }
    UFUNCTION(Exec)
    void Ck_HexNet_Move() { Request_ControlActivated(6); }
    UFUNCTION(Exec)
    void Ck_HexNet_Remove() { Request_ControlActivated(7); }
}
