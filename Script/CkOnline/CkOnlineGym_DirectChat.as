// Language=angelscript

// Switching to this gym travels the gameplay map. The native Direct TCP profile
// stays in GameInstance and its socket, roster, epoch, and transcript stay live.
class ACk_OnlineGym_DirectChat_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_OnlineGym_DirectChat_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default HUDClass = ACk_OnlineDirectGym_HUD;
}

class ACk_OnlineGym_DirectChat_PlayerController : ACk_Gym_Base_PlayerController
{
    FString Get_ControlPanelTitle() override { return "ONLINE CHAT - NATIVE DIRECT TCP"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override { return CkOnlineDirectGym::BuildRows(true); }
    void Request_ControlActivated(int32 InRowIndex) override
    { CkOnlineDirectGym::Activate(InRowIndex, true, Cast<ACk_OnlineDirectGym_HUD>(GetHUD())); }
}
