// Language=angelscript

class ACk_AggroGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_AggroGym_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_AggroGym_GameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        TArray<ACk_AggroGym_GameState> States;
        GetAllActorsOfClass(ACk_AggroGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0]) || !States[0].Get_HasPublishedCatalog())
        { ck::Error("[AggroGym] Authored GameState/catalog unavailable"); return; }
        States[0].Request_EnsureSharedDemo();
    }
};
