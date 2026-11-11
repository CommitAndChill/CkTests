// Language=angelscript

class ACk_PowerWheel_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY()
    private UCk_PowerWheelProof_UE _Wheel;

    void Request_StartGym() override
    {
        if (ck::IsValid(_Wheel)) { _Wheel.Request_Shutdown(); }
        _Wheel = UCk_PowerWheelProof_UE::Create(this);
    }

    UFUNCTION(BlueprintPure)
    UCk_PowerWheelProof_UE Get_PowerWheel() const
    { return _Wheel; }

    FString Get_ControlPanelTitle() override
    { return "MASS EFFECT 3 / SQUAD POWERS"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = ck::IsValid(_Wheel);
        const bool Open = Ready && _Wheel.IsWheelOpen();
        if (Open) { return Rows; }
        Rows.Add(CkGym_Control::Header("RADIAL POWER WHEEL / LIVE FIXTURE"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Open preview / Space deploys", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Toggle shielded target", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Reset powers / cooldowns", Ready));
        Rows.Add(CkGym_Control::Status("Hold", "Hold Shift: open / release: deploy"));
        Rows.Add(CkGym_Control::Status("Select", "Mouse hover or arrows; click / Enter queues"));
        Rows.Add(CkGym_Control::Status("Cancel", "Esc discards all queued powers"));
        Rows.Add(CkGym_Control::Status("Fixture", Ready ? _Wheel.Get_Summary() : "failed to create", !Ready));
        if (Ready) { const auto Error = _Wheel.Get_SourceError(); Rows.Add(CkGym_Control::Status("Source", Error.IsEmpty() ? "valid / live reload" : Error, !Error.IsEmpty())); }
        Rows.Add(CkGym_Control::Status("Navigation", "Tab: gyms  H: panel  WASD: move"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!ck::IsValid(_Wheel)) { return; }
        if (InRowIndex == 1) { _Wheel.Request_Open(); }
        else if (InRowIndex == 2) { _Wheel.Request_ToggleTarget(); }
        else if (InRowIndex == 3) { _Wheel.Request_ResetFixture(); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    { if (ck::IsValid(_Wheel)) { _Wheel.Request_Shutdown(); } _Wheel = nullptr; }
}

class ACk_PowerWheel_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_PowerWheel_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
