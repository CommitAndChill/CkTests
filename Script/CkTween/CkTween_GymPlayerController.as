class ACk_TweenTest_GymPlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastResetSerial = 0;
    UPROPERTY(Replicated) int32 LastResetAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastResetResult = 0;
    private int32 _RequestedSerial = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    { return CkTweenGym::Get_Stations(); }

    void Request_StartGym() override
    {}

    //--------------------------------------------------------------------------------------------------------------------------
    // CONTROL PANEL (Script/Common/CkGym_ControlPanel.as)
    //
    // One row. Everything here plays once and is then over, so re-running it IS the gym - and the console
    // command's name was the only documentation that the control existed at all.
    //--------------------------------------------------------------------------------------------------------------------------

    FString Get_ControlPanelTitle() override
    {
        return "TWEEN";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Replay every tween"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex != 0 || !IsLocalController()) { return; }
        _RequestedSerial = Math::Max(_RequestedSerial, LastResetSerial) + 1;
        Server_RequestTweenReset(_RequestedSerial);
    }

    UFUNCTION(Server)
    void Server_RequestTweenReset(int32 InSerial)
    {
        if (!HasAuthority()) { return; }
        LastResetAttemptSerial = InSerial;
        LastResetResult = 0;
        if (InSerial <= 0 || InSerial <= LastResetSerial)
        { LastResetResult = 4; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this)
        { LastResetResult = 2; return; }
        TArray<ACk_TweenTest_GymGameMode> Modes;
        GetAllActorsOfClass(ACk_TweenTest_GymGameMode, Modes);
        TArray<ACk_TweenTest_GymGameState> States;
        GetAllActorsOfClass(ACk_TweenTest_GymGameState, States);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto CurrentGym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Modes.Num() != 1 || States.Num() != 1 || ck::Is_NOT_Valid(States[0]) ||
            !States[0].Get_HasPublishedCatalog() || CurrentGym < 0 || CurrentGym >= Registry.Num() ||
            Registry[CurrentGym].DisplayName != "Tween")
        { LastResetResult = 3; return; }
        if (!States[0].Request_ResetTweenSet())
        { LastResetResult = 4; return; }
        LastResetSerial = InSerial;
        LastResetResult = 1;
    }
};
