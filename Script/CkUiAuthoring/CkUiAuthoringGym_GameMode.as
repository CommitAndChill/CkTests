// Language=angelscript

class ACk_UiAuthoringGym_HUD : ACkGym_ControlPanelHUD
{
    // The authored menu owns keyboard focus while active. Keep the gym panel's
    // shortcut dormant even if an input event reaches its lower-priority layer.
    bool Get_PanelKeysSuspended() override
    {
        auto PC = Cast<ACk_UiAuthoringGym_PlayerController>(GetOwningPlayerController());
        return ck::IsValid(PC) && PC.Get_IsPreviewActive();
    }

    UFUNCTION(BlueprintOverride)
    void DrawHUD(int32 SizeX, int32 SizeY)
    {
        auto PC = Cast<ACk_UiAuthoringGym_PlayerController>(GetOwningPlayerController());
        if (ck::IsValid(PC) && PC.Get_IsPreviewActive())
        {
            auto Switchboard = UCkGym_Switchboard_Subsystem::Get(PC);
            if (ck::IsValid(Switchboard))
            {
                const auto Style = Get_ControlPanelStyle();
                const float MaxWidth = Math::Min(Style.MaxWidth, float(SizeX) * 0.45f);
                Switchboard.Request_SetControlPanel(PC.Get_ControlPanelTitle(),
                    TArray<FCkGym_ControlRow>(), FVector2D(Style.X, Style.Y),
                    MaxWidth, Get_EffectivePanelMode(PC), true);
            }
            return;
        }

        Super::DrawHUD(SizeX, SizeY);
    }
}

class ACk_UiAuthoringGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_UiAuthoringGym_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACk_UiAuthoringGym_HUD;
}
