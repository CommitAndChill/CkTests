class ACk_HexRulesGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_HexRulesGym_Controller;
}

class ACk_HexRulesGym_Controller : ACk_Gym_Base_PlayerController
{
    private FCk_Handle_HexGrid _Board;
    private FCk_Handle_HexGridObject _Object;
    private FCk_Handle_HexGridBlocker _A;
    private FCk_Handle_HexGridBlocker _B;
    private bool _WantA = false;
    private bool _WantB = false;
    private int _Turn = 0;
    private FString _Readback = "Preparing";

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        FCkGym_Station_SpawnParams_Payload Station;
        Station.Tags.Add(n"Gym.HexRules");
        Station.Title = FText::FromString("Hex Footprints and Blockers");
        Station.Description.Add(FText::FromString("Yellow outlines show the rotated footprint. Red crosses mark blocked cells."));
        Station.Description.Add(FText::FromString("Blockers A and B each vote on the center and one neighboring cell."));
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        Stations.Add(Station);
        return Stations;
    }

    private FCk_HexCoord Offset(int Q, int R = 0) const
    {
        FCk_HexCoord Coordinate;
        Coordinate.Set_Q(Q);
        Coordinate.Set_R(R);
        return Coordinate;
    }

    private bool CreateContributor(FCk_Handle LifetimeOwner, int Side, FCk_Handle_HexGridBlocker& OutBlocker)
    {
        FCk_HexGridBlocker_Spec Spec;
        Spec.Set_Grid(_Board);
        TArray<FCk_HexCoord> Addresses;
        Addresses.Add(Offset(0));
        Addresses.Add(Offset(Side));
        Spec.Set_Cells(Addresses);
        Spec.Set_InitiallyActive(false);
        auto Failure = ECk_HexGridBlocker_Failure::None;
        return utils_hex_grid_blocker::TryCreate(LifetimeOwner, Spec, OutBlocker, Failure);
    }

    void Request_StartGym() override
    {
        auto Station = Get_StationHandle("Gym.HexRules");
        if (!ck::IsValid(Station)) { return; }
        auto Pivot = Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter + FVector(0,0,25);
        FCk_HexDomain Domain;
        Domain.Set_RingRadius(1);
        FCk_HexLayout Layout;
        Layout.Set_OuterRadius(60.0);
        FCk_HexGrid_Spec Spec;
        Spec.Set_Domain(Domain);
        Spec.Set_Layout(Layout);
        auto BoardFailure = ECk_HexBoardFailure::None;
        if (!utils_hex_grid::TryCreate(Station, FTransform(Pivot), Spec, _Board, BoardFailure))
        { _Readback = "Board creation failed"; return; }
        auto BoardEntity = _Board.H();
        FCk_HexGridObject_Spec Shape;
        TArray<FCk_HexCoord> Offsets;
        Offsets.Add(Offset(0));
        Offsets.Add(Offset(1));
        Shape.Set_FootprintOffsets(Offsets);
        auto ShapeFailure = ECk_HexGridObject_Failure::None;
        if (!utils_hex_grid_object::TryCreate(BoardEntity, Shape, _Object, ShapeFailure)
            || !CreateContributor(BoardEntity, 1, _A) || !CreateContributor(BoardEntity, -1, _B))
        {
            utils_entity_lifetime::Request_DestroyEntity(BoardEntity);
            _Readback = "Rule creation failed";
            return;
        }
        auto Pawn = GetControlledPawn();
        if (ck::IsValid(Pawn))
        {
            auto Eye = Pivot + FVector(-120,0,480);
            Pawn.SetActorLocation(Eye);
            SetControlRotation(FRotator::MakeFromX(Pivot - Eye));
        }
        SetActorTickEnabled(true);
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!ck::IsValid(_Object) || !ck::IsValid(_Board)) { return; }
        FCk_HexGrid_DebugDraw_Options Options;
        Options.Set_CellColor(FLinearColor(0.1, 0.85, 1.0));
        Options.Set_LineThickness(2.0f);
        _Board.DebugDraw_Grid(Options);
        auto Resolved = _Object.TryResolve_Cells(Offset(0), ECk_HexRotation(_Turn), ECk_HexGridConnectivity::RequireConnected);
        if (!Resolved._Succeeded || Resolved._Cells.Num() != 2) { _Readback = "Footprint resolution failed"; return; }
        FCk_HexCoord Tip;
        auto Failure = ECk_HexBoardFailure::None;
        for (auto Address : Resolved._Cells)
        {
            if (Address._Q != 0 || Address._R != 0) { Tip = Address; }
            TArray<FVector> Corners;
            FVector Center;
            if (!_Board.TryGet_CellWorldCorners(Address, Corners, Failure)
                || !_Board.TryGet_CellWorldCenter(Address, Center, Failure)) { return; }
            for (int Edge = 0; Edge < Corners.Num(); ++Edge)
            {
                auto Start = Center + (Corners[Edge] - Center) * 0.84;
                auto End = Center + (Corners[(Edge + 1) % Corners.Num()] - Center) * 0.84;
                utils_debug_draw::DrawDebugLine(Start, End, FLinearColor::Yellow, 0.0f, 4.0f);
            }
        }
        int BlockedCells = 0;
        bool CenterBlocked = false;
        for (int Q = -1; Q <= 1; ++Q)
        {
            auto Address = Offset(Q);
            FCk_Handle_HexCell Cell;
            bool Blocked = false;
            if (!_Board.TryGet_CellAt(Address, Cell, Failure) || !Cell.TryGet_IsDisabled(Blocked))
            { _Readback = "Cell readback failed"; return; }
            if (Q == 0) { CenterBlocked = Blocked; }
            if (!Blocked) { continue; }
            ++BlockedCells;
            FVector Center;
            if (!_Board.TryGet_CellWorldCenter(Address, Center, Failure)) { return; }
            auto D = FVector(18,18,0);
            auto E = FVector(-18,18,0);
            utils_debug_draw::DrawDebugLine(Center-D, Center+D, FLinearColor::Red, 0.0f, 5.0f);
            utils_debug_draw::DrawDebugLine(Center-E, Center+E, FLinearColor::Red, 0.0f, 5.0f);
        }
        auto A = _A.Get_IsActive() ? "on" : "off";
        auto B = _B.Get_IsActive() ? "on" : "off";
        auto Shared = CenterBlocked ? "blocked" : "open";
        _Readback = f"Tip ({Tip._Q},{Tip._R}) | A {A}, B {B} | center {Shared} | blocked {BlockedCells}";
    }

    FString Get_ControlPanelTitle() override { return "Hex Footprints and Blockers"; }
    UFUNCTION()
    FString Get_RuleStatus() const { return _Readback; }
    UFUNCTION(Exec)
    void Ck_HexRules_Rotate() { Request_ControlActivated(1); }
    UFUNCTION(Exec)
    void Ck_HexRules_ToggleA() { Request_ControlActivated(2); }
    UFUNCTION(Exec)
    void Ck_HexRules_ToggleB() { Request_ControlActivated(3); }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Status("Committed", _Readback, false));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Rotate footprint", ck::IsValid(_Object)));
        Rows.Add(CkGym_Control::Action(EKeys::F, "F", "Toggle blocker A", ck::IsValid(_A)));
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Toggle blocker B", ck::IsValid(_B)));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex == 1 && ck::IsValid(_Object)) { _Turn = (_Turn + 1) % 6; }
        else if (InRowIndex == 2 && ck::IsValid(_A)) { _WantA = !_WantA; _A.Request_SetActive(_WantA); }
        else if (InRowIndex == 3 && ck::IsValid(_B)) { _WantB = !_WantB; _B.Request_SetActive(_WantB); }
    }
}
