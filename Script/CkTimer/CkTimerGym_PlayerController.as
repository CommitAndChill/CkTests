// Language=angelscript

class ACk_TimerGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    private int32 _RequestedSerial = 0;
    private int32 _StateWaitAttempts = 0;
    private int32 _PendingControlSerial = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkTimerGym::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkTimerGym::Get_State();
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

    FString Get_ControlPanelTitle() override { return "TIMER"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Header("RE-RUN A STATION"));
        Rows.Add(CkGym_Control::Numbered(0, "Basics", false));
        Rows.Add(CkGym_Control::Numbered(1, "Behaviors", false));
        Rows.Add(CkGym_Control::Numbered(2, "Signals", false));
        Rows.Add(CkGym_Control::Numbered(3, "Control", false));
        Rows.Add(CkGym_Control::Numbered(4, "Countdown", false));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Re-run every station"));
        return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || InRowIndex < 1 || InRowIndex > 6) { return; }
        _RequestedSerial = Math::Max(_RequestedSerial, LastControlSerial) + 1;
        Server_RequestTimerControl(_RequestedSerial, InRowIndex - 1);
    }

    UFUNCTION(Server)
    void Server_RequestTimerControl(int32 InSerial, int32 InAction)
    {
        if (!HasAuthority()) { return; }
        if (InSerial <= 0 || InSerial <= LastControlSerial ||
            InSerial <= LastControlAttemptSerial || InSerial <= _PendingControlSerial ||
            InAction < 0 || InAction > 5)
        { LastControlAttemptSerial = InSerial; LastControlResult = 4; return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this)
        { LastControlResult = 2; return; }
        auto State = CkTimerGym::Get_State();
        TArray<ACk_TimerGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_TimerGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto CurrentGym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || Modes.Num() != 1 || !State.Get_HasPublishedCatalog() ||
            CurrentGym < 0 || CurrentGym >= Registry.Num() ||
            Registry[CurrentGym].DisplayName != "Timer" || !State.Get_HasReadyServerQuintet())
        { LastControlResult = 3; return; }
        _PendingControlSerial = InSerial;
        if (!(InAction == 5 ? State.Request_ResetSharedDemo(this, InSerial) :
                              State.Request_RerunStation(InAction, this, InSerial)))
        { _PendingControlSerial = 0; LastControlResult = 4; return; }
    }
    void Request_CompleteTimerControl(int32 InSerial, int32 InResult)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial != _PendingControlSerial ||
            (InResult != 1 && InResult != 3 && InResult != 4)) { return; }
        _PendingControlSerial = 0;
        LastControlAttemptSerial = InSerial;
        if (InResult == 1) { LastControlSerial = InSerial; }
        LastControlResult = InResult;
    }
}
