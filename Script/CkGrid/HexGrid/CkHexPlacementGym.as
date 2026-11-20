class ACk_HexPlacementGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_HexPlacementGym_Controller;
}

class ACk_HexPlacementGym_Controller : ACk_Gym_Base_PlayerController
{
    private FCk_Handle_HexGrid _Board;
    private FCk_Handle_HexGridObject _Piece;
    private FGuid _Id;
    private bool _Pending = false;
    private bool _Right = false;
    private FString _Status = "Preparing";

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        FCkGym_Station_SpawnParams_Payload Station;
        Station.Tags.Add(n"Gym.HexPlacement");
        Station.Title = FText::FromString("Hex Placement");
        Station.Description.Add(FText::FromString("Place or move the two-cell piece. Destroy its occupant to release both cells."));
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        Stations.Add(Station);
        return Stations;
    }

    void Request_StartGym() override
    {
        auto Station = Get_StationHandle("Gym.HexPlacement");
        if (!ck::IsValid(Station)) { return; }
        auto Pivot = Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter + FVector(0,0,25);
        FCk_HexDomain Domain;
        Domain.Set_RingRadius(2);
        FCk_HexLayout Layout;
        Layout.Set_OuterRadius(46.0);
        FCk_HexGrid_Spec Spec;
        Spec.Set_Domain(Domain);
        Spec.Set_Layout(Layout);
        auto Failure = ECk_HexBoardFailure::None;
        if (!utils_hex_grid::TryCreate(Station, FTransform(Pivot), Spec, _Board, Failure))
        { _Status = "Board creation failed"; return; }
        auto Pawn = GetControlledPawn();
        if (ck::IsValid(Pawn))
        {
            auto Eye = Pivot + FVector(-160,0,600);
            Pawn.SetActorLocation(Eye);
            SetControlRotation(FRotator::MakeFromX(Pivot - Eye));
        }
        SetActorTickEnabled(true);
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!ck::IsValid(_Board)) { return; }
        FCk_HexGrid_DebugDraw_Options Options;
        Options.Set_CellColor(FLinearColor(0.1,0.85,1.0));
        Options.Set_LineThickness(2.0f);
        _Board.DebugDraw_Grid(Options);
        auto Entries = utils_hex_grid_occupancy::Get_Placements(_Board);
        if (!Entries._Succeeded) { _Status = "Unavailable"; return; }
        if (Entries._Placements.Num() == 0)
        {
            _Id = FGuid();
            _Right = false;
            _Status = "Open";
            return;
        }
        auto View = Entries._Placements[0];
        _Id = View._PlacementId;
        _Right = View._Anchor._Q == 1;
        _Status = _Right ? "Placed at (1,0)" : "Placed at (0,0)";
        auto Failure = ECk_HexBoardFailure::None;
        for (auto Cell : View._Cells)
        {
            TArray<FVector> Corners;
            FVector Center;
            if (!_Board.TryGet_CellWorldCorners(Cell,Corners,Failure) || !_Board.TryGet_CellWorldCenter(Cell,Center,Failure)) { continue; }
            for (int I = 0; I < Corners.Num(); ++I)
            {
                auto A = Center + (Corners[I]-Center)*0.85;
                auto B = Center + (Corners[(I+1)%Corners.Num()]-Center)*0.85;
                utils_debug_draw::DrawDebugLine(A,B,FLinearColor::Yellow,0.0f,5.0f);
            }
        }
    }

    FString Get_ControlPanelTitle() override { return "Hex Placement"; }
    UFUNCTION()
    FString Get_PlacementStatus() const { return _Status; }
    UFUNCTION(Exec)
    void Ck_HexPlacement_Place() { Request_ControlActivated(1); }
    UFUNCTION(Exec)
    void Ck_HexPlacement_Move() { Request_ControlActivated(2); }
    UFUNCTION(Exec)
    void Ck_HexPlacement_Remove() { Request_ControlActivated(3); }
    UFUNCTION(Exec)
    void Ck_HexPlacement_Destroy() { Request_ControlActivated(4); }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Status("Board",_Status,false));
        Rows.Add(CkGym_Control::Action(EKeys::P,"P","Place",!_Pending && !_Id.IsValid()));
        Rows.Add(CkGym_Control::Action(EKeys::M,"M","Move",!_Pending && _Id.IsValid()));
        Rows.Add(CkGym_Control::Action(EKeys::R,"R","Remove",!_Pending && _Id.IsValid()));
        Rows.Add(CkGym_Control::Action(EKeys::X,"X","Destroy occupant",!_Pending && ck::IsValid(_Piece)));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (_Pending || !ck::IsValid(_Board)) { return; }
        auto Done = FCk_Delegate_HexGridPlacement_OnResult(this,n"OnTerminal");
        if (InRowIndex == 1 && !_Id.IsValid())
        {
            if (!ck::IsValid(_Piece))
            {
                TArray<FCk_HexCoord> Offsets;
                Offsets.Add(FCk_HexCoord());
                FCk_HexCoord Tip;
                Tip.Set_Q(1);
                Offsets.Add(Tip);
                FCk_HexGridObject_Spec Shape;
                Shape.Set_FootprintOffsets(Offsets);
                auto PieceOwner = _Board.H();
                auto Reason = ECk_HexGridObject_Failure::None;
                if (!utils_hex_grid_object::TryCreate(PieceOwner,Shape,_Piece,Reason)) { return; }
            }
            FCk_HexGridPlacement_Spec Spec;
            Spec.Set_Object(_Piece);
            auto Request = FCk_Request_HexGridPlacement_Place(Spec);
            Request.Set_OnResult(Done);
            _Pending = true;
            utils_hex_grid_placement::Request_Place(_Board,Request);
        }
        else if (InRowIndex == 2 && _Id.IsValid())
        {
            FCk_HexCoord Anchor;
            Anchor.Set_Q(_Right ? 0 : 1);
            auto Request = FCk_Request_HexGridPlacement_Move(_Id,Anchor,ECk_HexRotation::Turn0);
            Request.Set_OnResult(Done);
            _Pending = true;
            utils_hex_grid_placement::Request_Move(_Board,Request);
        }
        else if (InRowIndex == 3 && _Id.IsValid())
        {
            auto Request = FCk_Request_HexGridPlacement_Remove(_Id);
            Request.Set_OnResult(Done);
            _Pending = true;
            utils_hex_grid_placement::Request_Remove(_Board,Request);
        }
        else if (InRowIndex == 4 && ck::IsValid(_Piece))
        {
            auto Entity = _Piece.H();
            utils_entity_lifetime::Request_DestroyEntity(Entity);
        }
    }

    UFUNCTION()
    void OnTerminal(const FCk_HexGridPlacement_Result&in InResult)
    {
        _Pending = false;
        if (InResult._Succeeded) { _Id = InResult._PlacementId; }
    }
}
