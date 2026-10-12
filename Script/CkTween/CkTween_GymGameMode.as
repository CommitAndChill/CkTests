class ACk_TweenTest_GymGameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_TweenTest_GymPlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_TweenTest_GymGameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        TArray<ACk_TweenTest_GymGameState> States;
        GetAllActorsOfClass(ACk_TweenTest_GymGameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0]) || !States[0].Get_HasPublishedCatalog())
        { ck::Error("[TweenGym] Shared GameState catalog was not published"); return; }
        States[0].Request_EnsureTweenSet();
    }
};
