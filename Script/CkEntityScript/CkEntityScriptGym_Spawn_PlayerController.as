// Language=angelscript

class ACk_EntityScriptGym_Spawn_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    private int32 _RequestedSerial = 0;
    private int32 _WaitAttempts = 0;
    private int32 _ShownGeneration = 0;
    private int32 _ShownSequence = 0;
    private int32 _ShownReplicatedEpoch = 0;
    private FCk_Handle _ShownReplica;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkEntityScriptGym_Spawn::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkEntityScriptGym_Spawn::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if (IsLocalController() && _WaitAttempts++ < 200)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority() && State.Get_HasPublishedCatalog())
        { State.Request_EnsureSharedDemo(); }
        if (IsLocalController())
        {
            State.Request_EnsureLocalPresentation();
            System::SetTimer(this, n"OnPresentationTick", 0.1, true);
        }
    }

    UFUNCTION() private void OnStateWait() { Request_StartGym(); }

    UFUNCTION() private void OnPresentationTick()
    {
        if (!IsLocalController()) { return; }
        auto State = CkEntityScriptGym_Spawn::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasAdmittedLocalPresentation()) { return; }
        auto Snapshot = State.Snapshot;
        if (_ShownGeneration == Snapshot.Generation && _ShownSequence == Snapshot.Sequence) { return; }
        if (Snapshot.Generation != _ShownGeneration) { _ShownReplica = FCk_Handle(); }
        auto Replica = State.Get_LocalVerifiedReplica();
        if (ck::Is_NOT_Valid(Replica)) { return; }
        if (Snapshot.ReplicatedEpoch != _ShownReplicatedEpoch && Replica == _ShownReplica)
        { return; }
        _ShownReplica = Replica;
        _ShownGeneration = Snapshot.Generation;
        _ShownSequence = Snapshot.Sequence;
        _ShownReplicatedEpoch = Snapshot.ReplicatedEpoch;
        Request_Show(0, Snapshot.Plain);
        Request_Show(1, Snapshot.Replicated);
    }

    private void Request_Show(int32 InIndex, FCkEntityScriptGym_SpawnSample InSample)
    {
        auto Stations = CkEntityScriptGym_Spawn::Get_Stations();
        if (InIndex < 0 || InIndex >= Stations.Num() || Stations[InIndex].Tags.Num() != 1) { return; }
        auto Text = "";
        for (int32 i = 0; i < Stations[InIndex].Description.Num() - 2; i++)
        { Text = f"{Text}{Stations[InIndex].Description[i]}\n"; }
        Text = f"{Text}\n";
        Text = f"{Text}FName TestName:    {InSample.TestName}  (Expected: GymTestEntity)  " + (InSample.NamePass ? "[PASS]" : "[FAIL]") + "\n";
        Text = f"{Text}int32 TestInt:     {InSample.TestInt}  (Expected: 42)  " + (InSample.IntPass ? "[PASS]" : "[FAIL]") + "\n";
        Text = f"{Text}float TestFloat:   {InSample.TestFloat}  (Expected: 3.14)  " + (InSample.FloatPass ? "[PASS]" : "[FAIL]") + "\n";
        Text = f"{Text}\nRESULT: " + (InSample.NamePass && InSample.IntPass && InSample.FloatPass ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
        auto Display = FCkGym_Station_TitleAndDescription();
        Display.Title = Stations[InIndex].Title;
        Display.Description = FText::FromString(Text);
        Set_StationTitleAndDescription(InIndex == 0 ?
            "Gym.EntityScript.Spawn" : "Gym.EntityScript.SpawnReplicated", Display);
    }

    FString Get_ControlPanelTitle() override { return "ENTITY SCRIPT SPAWN"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Numbered(0, "Re-run spawn test", false));
        Rows.Add(CkGym_Control::Numbered(1, "Re-run replicated spawn test", false));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || InRowIndex < 0 || InRowIndex > 1) { return; }
        auto HighWater = Math::Max(_RequestedSerial,
            Math::Max(LastControlSerial, LastControlAttemptSerial));
        if (HighWater >= 2147483647) { return; }
        _RequestedSerial = HighWater + 1;
        Server_RequestEntityScriptSpawnControl(_RequestedSerial, InRowIndex);
    }

    UFUNCTION(Server)
    void Server_RequestEntityScriptSpawnControl(int32 InSerial, int32 InBranch)
    {
        if (!HasAuthority()) { return; }
        if (InSerial <= 0 || InSerial <= LastControlAttemptSerial ||
            InSerial <= LastControlSerial)
        { return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        if (InBranch < 0 || InBranch > 1)
        { LastControlResult = 4; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            Pawn.GetWorld() != GetWorld())
        { LastControlResult = 2; return; }
        auto State = CkEntityScriptGym_Spawn::Get_State();
        TArray<ACk_EntityScriptGym_Spawn_GameMode> Modes;
        GetAllActorsOfClass(ACk_EntityScriptGym_Spawn_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) ||
            Modes[0].GetWorld() != GetWorld() ||
            !State.Get_HasPublishedCatalog() || !State.Get_HasReadyServerCohort() ||
            Current < 0 || Current >= Registry.Num() ||
            Registry[Current].DisplayName != "Entity Script")
        { LastControlResult = 3; return; }
        if (!State.Request_RerunBranch(InBranch, this, InSerial))
        { LastControlResult = 4; return; }
    }

    void Request_OnSharedControlCommitted(int32 InSerial, bool InSucceeded)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlSerial) { return; }
        if (InSucceeded) { LastControlSerial = InSerial; }
        if (LastControlAttemptSerial == InSerial)
        { LastControlResult = InSucceeded ? 1 : 4; }
    }
}
