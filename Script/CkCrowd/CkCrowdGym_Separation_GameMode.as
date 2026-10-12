// Crowd Separation: one world-owned simulation with per-owner network sessions.
class ACk_CrowdGym_Separation_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_CrowdGym_Separation_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_CrowdGym_Separation_GameState;
}
