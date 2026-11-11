// Language=angelscript

class ACk_UiAuthoringGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY()
    private UCk_PauseMenuProof_UE _PauseMenu;

    UPROPERTY()
    private UCk_ScriptedPause_Widget _ScriptPause;

    UPROPERTY()
    private FCk_UiAuthoringProof_Settings _SavedSettings;

    UPROPERTY()
    private FCk_UiAuthoringProof_InventoryState _SavedInventory;

    UPROPERTY()
    private UCk_GameplayInventoryProof_UE _GameplayInventory;

    UPROPERTY()
    private UCk_GameplayInventoryProof_UE _GameplayLoadCandidate;

    private FString _GameplayStatus = "Not created";
    private bool _OpenGameplayWhenReady = false;

    UFUNCTION(BlueprintOverride)
    void Tick(float InDeltaTime)
    {
        if (ck::IsValid(_GameplayLoadCandidate))
        {
            if (_GameplayLoadCandidate.Get_IsReady())
            {
                if (ck::IsValid(_PauseMenu)) { _PauseMenu.Request_Close(); }
                if (ck::IsValid(_GameplayInventory)) { _GameplayInventory.Release(); }
                _GameplayInventory = _GameplayLoadCandidate;
                _GameplayLoadCandidate = nullptr;
                _GameplayStatus = "Loaded committed inventory";
                _OpenGameplayWhenReady = true;
            }
            else if (!_GameplayLoadCandidate.Get_LastError().IsEmpty())
            {
                _GameplayStatus = _GameplayLoadCandidate.Get_LastError();
                _GameplayLoadCandidate.Release();
                _GameplayLoadCandidate = nullptr;
            }
        }
        if (_OpenGameplayWhenReady && ck::IsValid(_GameplayInventory))
        {
            if (_GameplayInventory.Get_IsReady())
            { _OpenGameplayWhenReady = false; Request_ShowGameplayInventory(); }
            else if (!_GameplayInventory.Get_LastError().IsEmpty())
            { _GameplayStatus = _GameplayInventory.Get_LastError(); _OpenGameplayWhenReady = false; }
        }
    }

    void Request_StartGym() override
    {
        // The panel is the entry point; previews open from its numbered shortcuts.
    }

    UFUNCTION(BlueprintPure)
    bool Get_IsPreviewActive() const
    {
        return (ck::IsValid(_ScriptPause) && _ScriptPause.Get_IsCurrent())
            || (ck::IsValid(_PauseMenu) && _PauseMenu.IsActivated());
    }

    UFUNCTION(BlueprintPure)
    UCk_PauseMenuProof_UE Get_ActivePauseMenu() const
    {
        if (ck::IsValid(_ScriptPause) && _ScriptPause.Get_IsCurrent())
        {
            auto Child = _ScriptPause.Get_SettingsChild();
            return ck::IsValid(Child) && Child.IsActivated() ? Child : nullptr;
        }
        return ck::IsValid(_PauseMenu) && _PauseMenu.IsActivated() ? _PauseMenu : nullptr;
    }

    UFUNCTION(BlueprintPure)
    UCk_ScriptedPause_Widget Get_ActiveScriptPause() const
    { return ck::IsValid(_ScriptPause) && _ScriptPause.Get_IsCurrent() ? _ScriptPause : nullptr; }

    UFUNCTION(BlueprintCallable)
    void Request_ShowPauseMenu()
    {
        Request_ShowUi(ECk_UiAuthoringProof_Screen::Pause);
    }

    UFUNCTION(BlueprintCallable)
    void Request_ShowSettings()
    {
        Request_ShowUi(ECk_UiAuthoringProof_Screen::Settings);
    }

    UFUNCTION(BlueprintCallable)
    void Request_ShowInventory()
    {
        Request_ShowUi(ECk_UiAuthoringProof_Screen::Inventory);
    }

    UFUNCTION(BlueprintCallable)
    void Request_ShowGameplayInventory()
    {
        if (ck::IsValid(_ScriptPause) && _ScriptPause.Get_IsCurrent()) { _ScriptPause.Request_Close(); }
        if (ck::IsValid(_PauseMenu))
        {
            if (!_PauseMenu.Get_UsesGameplayInventory()) { _SavedInventory = _PauseMenu.Get_InventoryState(); }
            _SavedSettings = _PauseMenu.Get_AppliedSettings();
            _PauseMenu.Request_Close();
            _PauseMenu = nullptr;
        }
        if (ck::IsValid(_GameplayInventory) && !_GameplayInventory.Get_LastError().IsEmpty())
        { _GameplayInventory.Release(); _GameplayInventory = nullptr; }
        if (!ck::IsValid(_GameplayInventory))
        { _GameplayInventory = UCk_GameplayInventoryProof_UE::TryCreate(_GameplayStatus); }
        if (!ck::IsValid(_GameplayInventory)) { return; }
        if (!_GameplayInventory.Get_IsReady())
        { _OpenGameplayWhenReady = true; _GameplayStatus = "Preparing real ECS inventory"; return; }
        _PauseMenu = UCk_PauseMenuProof_UE::OpenWithGameplayInventory(this, _GameplayInventory.Get_Inventory());
        _GameplayStatus = ck::IsValid(_PauseMenu) ? "Live ECS inventory / discard removes a stack" : "Open rejected";
    }

    UFUNCTION(BlueprintCallable)
    void Request_SaveGameplayInventory()
    {
        if (ck::IsValid(_GameplayInventory)
            && _GameplayInventory.Request_Save("CkUiGameplayInventoryProof", _GameplayStatus))
        { _GameplayStatus = "Saved committed inventory"; }
        else if (!ck::IsValid(_GameplayInventory)) { _GameplayStatus = "Create the inventory before saving"; }
    }

    UFUNCTION(BlueprintCallable)
    void Request_LoadGameplayInventory()
    {
        if (ck::IsValid(_GameplayLoadCandidate)) { return; }
        // Keep the accepted owner until the entire replacement has hydrated successfully.
        _GameplayLoadCandidate = UCk_GameplayInventoryProof_UE::TryLoad(
            "CkUiGameplayInventoryProof", _GameplayStatus);
        if (ck::IsValid(_GameplayLoadCandidate))
        {
            if (ck::IsValid(_ScriptPause)) { _ScriptPause.Request_Close(); }
            if (ck::IsValid(_PauseMenu)) { _PauseMenu.Request_Close(); }
            _GameplayStatus = "Loading candidate inventory";
        }
    }

    private void Request_ShowUi(ECk_UiAuthoringProof_Screen InScreen)
    {
        if (ck::IsValid(_ScriptPause) && _ScriptPause.Get_IsCurrent())
        {
            if (InScreen == ECk_UiAuthoringProof_Screen::Settings)
            {
                if (_ScriptPause.IsActivated()) { _ScriptPause.Request_ShowSettings(); }
                return;
            }
            if (InScreen == ECk_UiAuthoringProof_Screen::Pause) { return; }
            _SavedSettings = _ScriptPause.Get_AppliedSettings();
            _ScriptPause.Request_Close();
        }
        if (ck::IsValid(_PauseMenu) && _PauseMenu.IsActivated() && !_PauseMenu.Get_UsesGameplayInventory()
            && InScreen != ECk_UiAuthoringProof_Screen::Pause)
        {
            _PauseMenu.Request_ShowScreen(InScreen);
            return;
        }
        if (ck::IsValid(_ScriptPause))
        { _SavedSettings = _ScriptPause.Get_AppliedSettings(); }
        if (ck::IsValid(_PauseMenu))
        {
            _SavedSettings = _PauseMenu.Get_AppliedSettings();
            if (!_PauseMenu.Get_UsesGameplayInventory()) { _SavedInventory = _PauseMenu.Get_InventoryState(); }
            _PauseMenu.Request_Close();
            _PauseMenu = nullptr;
        }
        if (InScreen == ECk_UiAuthoringProof_Screen::Pause)
        {
            _ScriptPause = Cast<UCk_ScriptedPause_Widget>(WidgetBlueprint::CreateWidget(UCk_ScriptedPause_Widget, this));
            if (ck::IsValid(_ScriptPause) && !_ScriptPause.TryOpenWithSettings(_SavedSettings))
            { _ScriptPause.Request_Close(); }
        }
        else
        { _PauseMenu = UCk_PauseMenuProof_UE::OpenWithProofState(this, InScreen, _SavedSettings, _SavedInventory); }
        if (Get_IsPreviewActive())
        {
            // Hide the gym hint immediately: the world pauses in Open, so do not
            // rely on a subsequent DrawHUD before the first paused frame.
            auto Switchboard = UCkGym_Switchboard_Subsystem::Get(this);
            if (ck::IsValid(Switchboard))
            {
                const auto Style = FCkGym_ControlPanel_Style();
                Switchboard.Request_SetControlPanel(Get_ControlPanelTitle(),
                    TArray<FCkGym_ControlRow>(), FVector2D(Style.X, Style.Y),
                    Style.MaxWidth, ECkGym_ControlPanel_Mode::Full, true);
            }
        }
    }

    FString Get_ControlPanelTitle() override
    {
        return "UI AUTHORING";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool MenuActive = Get_IsPreviewActive();
        auto NativeMenu = Get_ActivePauseMenu();
        const bool ScriptActive = ck::IsValid(Get_ActiveScriptPause());

        Rows.Add(CkGym_Control::Header("SOURCE-AUTHORED UI"));
        const bool SettingsActive = ck::IsValid(NativeMenu) && NativeMenu.Get_Screen() == ECk_UiAuthoringProof_Screen::Settings;
        const bool InventoryActive = ck::IsValid(NativeMenu) && NativeMenu.Get_Screen() == ECk_UiAuthoringProof_Screen::Inventory;
        const bool GameplayActive = ck::IsValid(NativeMenu) && NativeMenu.Get_UsesGameplayInventory();
        Rows.Add(CkGym_Control::Numbered(0, "Pause menu", MenuActive && !SettingsActive && !InventoryActive));
        Rows.Add(CkGym_Control::Numbered(1, "Settings", SettingsActive));
        Rows.Add(CkGym_Control::Numbered(2, "Inventory", InventoryActive && !GameplayActive));
        Rows.Add(CkGym_Control::Numbered(3, "Gameplay inventory", GameplayActive));
        Rows.Add(CkGym_Control::Numbered(4, "Save gameplay inventory", false));
        Rows.Add(CkGym_Control::Numbered(5, "Load gameplay inventory", false));
        Rows.Add(CkGym_Control::Status("Gameplay backend", _GameplayStatus));
        Rows.Add(CkGym_Control::Status("Screen", MenuActive ? (GameplayActive ? "OPEN / LIVE ECS" : "OPEN / PAUSED") : "closed"));

        if (ScriptActive)
        {
            const int32 Resume = _ScriptPause.Get_ActionCount(n"ResumeRequested");
            const int32 Settings = _ScriptPause.Get_ActionCount(n"SettingsRequested");
            const int32 ReturnToTitle = _ScriptPause.Get_ActionCount(n"ReturnToTitleRequested");
            Rows.Add(CkGym_Control::Status("Authoring", "AngelScript + HTML/CSS"));
            Rows.Add(CkGym_Control::Status("Actions", f"Resume {Resume}  Settings {Settings}  Title {ReturnToTitle}"));
            const FString SourceError = _ScriptPause.Get_SourceError();
            Rows.Add(CkGym_Control::Status("Source", SourceError.IsEmpty() ? "valid" : SourceError,
                !SourceError.IsEmpty()));
        }
        if (ck::IsValid(NativeMenu))
        {
            const int32 Apply = NativeMenu.Get_ActionCount(n"ApplySettingsRequested");
            const int32 Cancel = NativeMenu.Get_ActionCount(n"CancelSettingsRequested");
            Rows.Add(CkGym_Control::Status("Settings actions", f"Apply {Apply}  Cancel {Cancel}"));
            Rows.Add(CkGym_Control::Status("Applied", NativeMenu.Get_SettingsSummary()));
            if (InventoryActive) { Rows.Add(CkGym_Control::Status("Inventory", NativeMenu.Get_InventorySummary())); }

            const FString SourceError = NativeMenu.Get_SourceError();
            Rows.Add(CkGym_Control::Status("Source", SourceError.IsEmpty() ? "valid" : SourceError,
                !SourceError.IsEmpty()));
        }
        else if (!ScriptActive)
        {
            Rows.Add(CkGym_Control::Status("Actions", "Open the menu to exercise its actions"));
            Rows.Add(CkGym_Control::Status("Source", "not loaded"));
        }

        Rows.Add(CkGym_Control::Status("Navigate", "Tab / Shift+Tab; arrows edit controls or move among cards"));
        Rows.Add(CkGym_Control::Status("Activate", "Enter / Space; Esc resumes, cancels, or closes"));
        Rows.Add(CkGym_Control::Status("Gyms", "Tab opens the selector while the menu is closed"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        // The header occupies row zero; keyed rows are Pause, Settings, Inventory.
        if (InRowIndex == 1)
        { Request_ShowPauseMenu(); }
        else if (InRowIndex == 2)
        { Request_ShowSettings(); }
        else if (InRowIndex == 3)
        { Request_ShowInventory(); }
        else if (InRowIndex == 4)
        { Request_ShowGameplayInventory(); }
        else if (InRowIndex == 5)
        { Request_SaveGameplayInventory(); }
        else if (InRowIndex == 6)
        { Request_LoadGameplayInventory(); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_ScriptPause)) { _ScriptPause.Request_Close(); }
        if (ck::IsValid(_PauseMenu)) { _PauseMenu.Request_Close(); }
        _ScriptPause = nullptr;
        _PauseMenu = nullptr;
        if (ck::IsValid(_GameplayLoadCandidate)) { _GameplayLoadCandidate.Release(); }
        if (ck::IsValid(_GameplayInventory)) { _GameplayInventory.Release(); }
        _GameplayLoadCandidate = nullptr;
        _GameplayInventory = nullptr;
    }
}
