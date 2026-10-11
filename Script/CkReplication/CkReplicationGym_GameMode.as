//============================================================================
// REPLICATION GYM - GAME MODE
//============================================================================

class ACk_ReplicationGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_ReplicationGym_PlayerController;
    default DefaultPawnClass = ACk_ReplicationGym_Pawn;
    default GameStateClass = ACk_ReplicationGym_GameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        TArray<ACk_ReplicationGym_GameState> States;
        GetAllActorsOfClass(ACk_ReplicationGym_GameState, States);
        if (States.Num() != 1 || !States[0].Get_HasPublishedCatalog())
        {
            ck::Error("[ReplicationGym] Authored GameState/catalog unavailable; shared demo rejected");
            return;
        }
        States[0].Request_EnsureSharedActor();
    }
}
