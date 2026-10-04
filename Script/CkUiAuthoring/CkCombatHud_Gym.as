// Language=angelscript

class ACk_CombatHud_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY()
    private UCk_CombatHudProof_UE _Hud;

    ECk_CombatHudProof_Theme Get_HudTheme()
    {
        return ECk_CombatHudProof_Theme::Futuristic;
    }

    void Request_StartGym() override
    {
        if (ck::IsValid(_Hud))
        { _Hud.Request_Close(); }
        _Hud = UCk_CombatHudProof_UE::Open(this, Get_HudTheme());
    }

    UFUNCTION(BlueprintPure)
    UCk_CombatHudProof_UE Get_ActiveHud() const
    {
        return ck::IsValid(_Hud) && _Hud.IsActive() ? _Hud : nullptr;
    }

    FString Get_ControlPanelTitle() override
    {
        return Get_HudTheme() == ECk_CombatHudProof_Theme::Futuristic ? "MASS EFFECT 3 HUD"
            : Get_HudTheme() == ECk_CombatHudProof_Theme::Tactical ? "BATTLEFIELD 4 HUD" : "SKYRIM HUD";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        const bool Active = ck::IsValid(Get_ActiveHud());
        Rows.Add(CkGym_Control::Header("AAA HUD REPLICA / LIVE FIXTURE"));
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "Damage", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "Sprint / recover", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "Fire / loose arrow", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "Reload / replenish", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Five, "5", Get_HudTheme() == ECk_CombatHudProof_Theme::Tactical
            ? "Capture flag C" : Get_HudTheme() == ECk_CombatHudProof_Theme::Fantasy ? "Cast spell" : "Use power", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Six, "6", "Restore vitals", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Seven, "7", "Reset fixture", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Eight, "8", "Low health", Active));
        Rows.Add(CkGym_Control::Action(EKeys::Nine, "9", Get_HudTheme() == ECk_CombatHudProof_Theme::Fantasy
            ? "Combat / sneak / interaction" : Get_HudTheme() == ECk_CombatHudProof_Theme::Tactical
            ? "Capture / kill feed context" : "Objective prompt context", Active));
        Rows.Add(CkGym_Control::Status("HUD", Active ? _Hud.Get_Summary() : "failed to open", !Active));
        if (Active)
        {
            const auto State = _Hud.Get_State();
            Rows.Add(CkGym_Control::Status("Vitals", f"Health {State.Get_Health():.0}  Stamina {State.Get_Stamina():.0}"));
            Rows.Add(CkGym_Control::Status("Ammo", f"{State.Get_Ammo()} / {State.Get_Reserve()}"));
            const auto Error = _Hud.Get_SourceError();
            Rows.Add(CkGym_Control::Status("Source", Error.IsEmpty() ? "valid / live reload" : Error, !Error.IsEmpty()));
        }
        Rows.Add(CkGym_Control::Status("Navigation", "Tab: gyms  H: panel  WASD: move"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!ck::IsValid(Get_ActiveHud())) { return; }
        // Header occupies zero; action rows map one-to-one to the command enum.
        if (InRowIndex >= 1 && InRowIndex <= 9)
        { _Hud.Request_Command(ECk_CombatHudProof_Command(InRowIndex - 1)); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (ck::IsValid(_Hud)) { _Hud.Request_Close(); }
        _Hud = nullptr;
    }
}

class ACk_CombatHud_Futuristic_PlayerController : ACk_CombatHud_PlayerController
{
    ECk_CombatHudProof_Theme Get_HudTheme() override
    { return ECk_CombatHudProof_Theme::Futuristic; }
}

class ACk_CombatHud_Tactical_PlayerController : ACk_CombatHud_PlayerController
{
    ECk_CombatHudProof_Theme Get_HudTheme() override
    { return ECk_CombatHudProof_Theme::Tactical; }
}

class ACk_CombatHud_Fantasy_PlayerController : ACk_CombatHud_PlayerController
{
    ECk_CombatHudProof_Theme Get_HudTheme() override
    { return ECk_CombatHudProof_Theme::Fantasy; }
}

class ACk_CombatHud_Futuristic_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_CombatHud_Futuristic_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}

class ACk_CombatHud_Tactical_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_CombatHud_Tactical_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}

class ACk_CombatHud_Fantasy_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_CombatHud_Fantasy_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACkGym_ControlPanelHUD;
}
