class ACk_HexPersistenceGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_HexPersistenceGym_Controller;
}

class ACk_HexPersistenceGym_Controller : ACk_Gym_Base_PlayerController
{
    private bool _StartedStations = false;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto Snapshot = Subsystem::GetGameInstanceSubsystem(UCk_Snapshot_Subsystem_UE);
        if (Snapshot.Get_IsLoadInProgress())
        {
            utils_snapshot::Promise_OnLoadComplete(ck::TransientEntity(),
                FCk_Delegate_Snapshot_OnLoadComplete(this, n"OnWorldRestored"));
            return;
        }
        StartStations();
    }

    UFUNCTION()
    void OnWorldRestored(FCk_Handle InHandle, FCk_Snapshot_LoadReport InReport)
    {
        StartStations();
    }

    private void StartStations()
    {
        if (_StartedStations) { return; }
        _StartedStations = true;
        // The base creates the controller's bridge and station entities. These ordinary world spawns
        // must happen after the load releases its rebuild hold, not during the new world's BeginPlay.
        Super::BeginPlay();
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        FCkGym_Station_SpawnParams_Payload Station;
        Station.Tags.Add(n"Gym.HexPersistence");
        Station.Title = FText::FromString("Hex Persistence");
        Station.Description.Add(FText::FromString("Place a piece, save, then reload the level. The restored board is reacquired from its saved recipe."));
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        Stations.Add(Station);
        return Stations;
    }

    void Request_StartGym() override
    {
        auto Station = Get_StationHandle("Gym.HexPersistence");
        if (!ck::IsValid(Station)) { return; }
        auto Pivot = Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter + FVector(0,0,25);
        auto Session = Subsystem::GetGameInstanceSubsystem(UCk_HexPersistenceGymSession);
        Session.Request_Start(FTransform(Pivot));
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
        auto Session = Subsystem::GetGameInstanceSubsystem(UCk_HexPersistenceGymSession);
        auto Board = Session.Get_Board();
        if (!ck::IsValid(Board)) { return; }
        FCk_HexGrid_DebugDraw_Options Options;
        Options.Set_CellColor(FLinearColor(0.1,0.85,1.0));
        Options.Set_LineThickness(2.0f);
        Board.DebugDraw_Grid(Options);
        auto Entries = utils_hex_grid_occupancy::Get_Placements(Board);
        if (!Entries._Succeeded) { return; }
        auto Failure = ECk_HexBoardFailure::None;
        for (auto Placement : Entries._Placements)
        {
            for (auto Cell : Placement._Cells)
            {
                TArray<FVector> Corners;
                FVector Center;
                if (!Board.TryGet_CellWorldCorners(Cell,Corners,Failure) || !Board.TryGet_CellWorldCenter(Cell,Center,Failure)) { continue; }
                for (int I = 0; I < Corners.Num(); ++I)
                {
                    auto A = Center + (Corners[I]-Center)*0.85;
                    auto B = Center + (Corners[(I+1)%Corners.Num()]-Center)*0.85;
                    utils_debug_draw::DrawDebugLine(A,B,FLinearColor::Yellow,0.0f,5.0f);
                }
            }
        }
    }

    FString Get_ControlPanelTitle() override { return "Hex Persistence"; }

    UFUNCTION()
    FString Get_PersistenceStatus() const
    {
        auto Session = Subsystem::GetGameInstanceSubsystem(UCk_HexPersistenceGymSession);
        return Session.Get_Status();
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Session = Subsystem::GetGameInstanceSubsystem(UCk_HexPersistenceGymSession);
        auto Board = Session.Get_Board();
        bool Ready = utils_hex_grid::Get_IsReady(Board) && !Session.Get_IsBusy();
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Status("Board", Session.Get_Status(), false));
        Rows.Add(CkGym_Control::Status("Snapshot", Session.Get_Report(), false));
        Rows.Add(CkGym_Control::Action(EKeys::P,"P","Place piece",Ready));
        Rows.Add(CkGym_Control::Action(EKeys::S,"S","Save board",Ready));
        Rows.Add(CkGym_Control::Action(EKeys::L,"L","Reload saved board",Ready && Session.Get_HasSave()));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        auto Session = Subsystem::GetGameInstanceSubsystem(UCk_HexPersistenceGymSession);
        if (InRowIndex == 2) { Session.Request_Place(); }
        else if (InRowIndex == 3) { Session.Request_Save(); }
        else if (InRowIndex == 4) { Session.Request_Load(); }
    }

    UFUNCTION(Exec)
    void Ck_HexPersistence_Place() { Request_ControlActivated(2); }
    UFUNCTION(Exec)
    void Ck_HexPersistence_Save() { Request_ControlActivated(3); }
    UFUNCTION(Exec)
    void Ck_HexPersistence_Load() { Request_ControlActivated(4); }
}
