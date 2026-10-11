class ACk_SmTest_GymGameMode : ACkTests_Gym_Base_GameMode
{
    default GameStateClass = ACk_SmTest_GymGameState;
    default PlayerControllerClass = ACk_SmTest_GymPlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
};
