// Language=angelscript

class ACk_AggroGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastResetSerial = 0;
    UPROPERTY(Replicated) int32 LastResetAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastResetResult = 0;
    private int32 _RequestedSerial = 0;
    private int32 _StateWaitAttempts = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkAggroGym::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkAggroGym::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if (IsLocalController() && _StateWaitAttempts++ < 200)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority() && State.Get_HasPublishedCatalog())
        { State.Request_EnsureSharedDemo(); }
        if (IsLocalController()) { State.Request_EnsureLocalProjection(); }
    }

    UFUNCTION() private void OnStateWait() { Request_StartGym(); }

    FString Get_ControlPanelTitle() override { return "AGGRO"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Re-run every station"));
        return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex != 0 || !IsLocalController()) { return; }
        _RequestedSerial = Math::Max(_RequestedSerial, LastResetSerial) + 1;
        Server_RequestAggroReset(_RequestedSerial);
    }

    UFUNCTION(Server)
    void Server_RequestAggroReset(int32 InSerial)
    {
        if (!HasAuthority()) { return; }
        LastResetAttemptSerial = InSerial;
        LastResetResult = 0;
        if (InSerial > 0 && InSerial <= LastResetSerial)
        { LastResetResult = 4; return; }
        if (InSerial <= 0) { LastResetResult = 4; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this) { LastResetResult = 2; return; }
        auto State = CkAggroGym::Get_State();
        TArray<ACk_AggroGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_AggroGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto CurrentGym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || Modes.Num() != 1 || !State.Get_HasPublishedCatalog() ||
            CurrentGym < 0 || CurrentGym >= Registry.Num() || Registry[CurrentGym].DisplayName != "Aggro" ||
            ck::Is_NOT_Valid(CkAggroGym::FindStation(n"Gym.Aggro.Chase")) ||
            ck::Is_NOT_Valid(CkAggroGym::FindStation(n"Gym.Aggro.Perception")) ||
            ck::Is_NOT_Valid(CkAggroGym::FindStation(n"Gym.Aggro.Stress")))
        { LastResetResult = 3; return; }
        if (!State.Request_ResetSharedDemo())
        { LastResetResult = 4; return; }
        LastResetSerial = InSerial;
        LastResetResult = 1;
    }
}
