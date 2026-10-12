class ACk_UnrealComponentGym_GameMode : ACkTests_Gym_Base_GameMode
{
	default GameStateClass = ACk_UnrealComponentGym_GameState;
	default PlayerControllerClass = ACk_UnrealComponentGym_PlayerController;
	default DefaultPawnClass = ACk_Gym_Base_Pawn;
}
