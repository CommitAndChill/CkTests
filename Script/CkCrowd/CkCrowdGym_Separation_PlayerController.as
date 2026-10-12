// One world simulation; each owning controller keeps only its local control/selection session.
namespace CkCrowdSeparation
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.Crowd.Separation");
        Station.Transform = FTransform(FRotator(0.0, 180.0, 0.0), FVector(500.0, 0.0, 0.0));
        Station.Title = FText::FromString("CROWD SEPARATION (Gate 3)");
        auto Description = TArray<FText>();
        Description.Add(FText::FromString("Panel: 1 head-on N/S, 2 head-on E/W, 3 all four, 4 cluster of 5, Z clear"));
        Description.Add(FText::FromString("In-world visuals:  ck.Crowd.Debug 1"));
        Description.Add(FText::FromString("Data panel:        ck.CrowdDebugger 1"));
        Description.Add(FText::FromString("Head-on pairs should pass without clipping; converging cluster should not pile."));
        Station.Description = Description;
        Stations.Add(Station);
        return Stations;
    }

    ACk_CrowdGym_Separation_GameState Get_State()
    {
        TArray<ACk_CrowdGym_Separation_GameState> States;
        GetAllActorsOfClass(ACk_CrowdGym_Separation_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }
}

class ACk_CrowdGym_Separation_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(DefaultComponent) UCkCrowdGym_Session_UE CrowdSession;
    UPROPERTY() int64 LastControlSerial = 0;
    UPROPERTY() int32 LastControlOutcome = -1;
    private int32 _StateWaitAttempts = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkCrowdSeparation::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkCrowdSeparation::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if (IsLocalController() && _StateWaitAttempts++ < 400)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority())
        {
            if (State.Get_HasPublishedCatalog()) { State.Request_EnsureSharedDemo(); }
            else if (_StateWaitAttempts++ < 400)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
        }
        if (IsLocalController() && !State.TryBindLocalController(this) &&
            _StateWaitAttempts++ < 400)
        { System::SetTimer(this, n"OnStateWait", 0.1, false); }
    }

    UFUNCTION() private void OnStateWait() { Request_StartGym(); }

    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    {
        if (!IsLocalController() || ck::Is_NOT_Valid(CrowdSession)) { return; }
        CrowdSession.TryTeardownObsoletePawnSession();
        auto State = CkCrowdSeparation::Get_State();
        if (ck::IsValid(State)) { State.TryBindLocalController(this); }
        FCkCrowdGym_OwnerCommit Commit;
        if (!CrowdSession.TryCopyRetainedCommit(Commit) ||
            Commit.Kind != ECkCrowdGym_RequestKind::Control) { return; }
        LastControlSerial = Commit.RequestSerial;
        LastControlOutcome = int32(Commit.Outcome);
        CrowdSession.TryAcknowledgeCommit(Commit.DeliverySerial);
    }

    FString Get_ControlPanelTitle() override { return "CROWD: SEPARATION"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Header("RUN A SCENARIO"));
        Rows.Add(CkGym_Control::Numbered(0, "Head-on, north / south", false));
        Rows.Add(CkGym_Control::Numbered(1, "Head-on, east / west", false));
        Rows.Add(CkGym_Control::Numbered(2, "All four cardinals", false));
        Rows.Add(CkGym_Control::Numbered(3, "Cluster of 5 to centre", false));
        Rows.Add(CkGym_Control::Action(EKeys::Z, "Z", "Clear every agent"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || ck::Is_NOT_Valid(CrowdSession)) { return; }
        if (InRowIndex >= 1 && InRowIndex <= 5)
        { CrowdSession.TryQueueControl(InRowIndex); }
    }
}
