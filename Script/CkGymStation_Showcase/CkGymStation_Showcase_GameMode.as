//============================================================================
// Station Showcase Gym - GameMode
//
// Sandbox gym for iterating on UCk_EntityScript_GymStation. Spawns no nav
// infrastructure, no test agents - just a row of GymStation instances with
// varied tuners (see CkStationShowcase::Get_Params).
//============================================================================

class ACk_StationShowcaseGym_GameMode : ACkTests_Gym_Base_GameMode
{
	default PlayerControllerClass = ACk_StationShowcaseGym_PlayerController;
	default GameStateClass = ACk_StationShowcaseGym_GameState;
	default DefaultPawnClass = ACk_Gym_Base_Pawn;
};
