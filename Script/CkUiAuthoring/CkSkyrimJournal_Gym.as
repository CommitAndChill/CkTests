// Language=angelscript

class ACk_SkyrimJournal_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY()
    private UCk_JournalProof_UE _Journal;

    void Request_StartGym() override
    {
        if (ck::IsValid(_Journal)) { _Journal.Request_Shutdown(); }
        _Journal = UCk_JournalProof_UE::Create(this);
    }

    UFUNCTION(BlueprintPure)
    UCk_JournalProof_UE Get_Journal() const
    { return _Journal; }

    FString Get_ControlPanelTitle() override
    { return "SKYRIM / QUEST JOURNAL"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Ready = ck::IsValid(_Journal);
        if (Ready && _Journal.IsJournalOpen()) { return Rows; }
        Rows.Add(CkGym_Control::Header("JOURNAL / LIVE QUEST FIXTURE"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Open journal", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Complete next selected objective", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Toggle 130-entry stress fixture", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "Reset quests", Ready));
        Rows.Add(CkGym_Control::Status("Open / close", "J: journal   Esc: close"));
        Rows.Add(CkGym_Control::Status("Browse", "Mouse / arrows; Q / E: categories"));
        Rows.Add(CkGym_Control::Status("Track", "Enter: toggle the selected quest or errand"));
        Rows.Add(CkGym_Control::Status("Fixture", Ready ? _Journal.Get_Summary() : "failed to create", !Ready));
        if (Ready)
        {
            const auto Error = _Journal.Get_SourceError();
            Rows.Add(CkGym_Control::Status("Source", Error.IsEmpty() ? "valid / live reload" : Error, !Error.IsEmpty()));
        }
        Rows.Add(CkGym_Control::Status("Navigation", "Tab: gyms  H: panel  WASD: move"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!ck::IsValid(_Journal)) { return; }
        if (InRowIndex == 1) { _Journal.Request_Open(); }
        else if (InRowIndex == 2) { _Journal.Request_AdvanceSelected(); }
        else if (InRowIndex == 3) { _Journal.Request_ToggleStress(); }
        else if (InRowIndex == 4) { _Journal.Request_Reset(); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    { if (ck::IsValid(_Journal)) { _Journal.Request_Shutdown(); } _Journal = nullptr; }
}

class ACk_SkyrimJournal_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_SkyrimJournal_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
