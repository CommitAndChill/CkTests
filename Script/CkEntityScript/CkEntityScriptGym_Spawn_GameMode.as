// Language=angelscript

class ACk_EntityScriptGym_Spawn_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_EntityScriptGym_Spawn_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_EntityScriptGym_Spawn_GameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        auto State = CkEntityScriptGym_Spawn::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasPublishedCatalog())
        { ck::Error("[EntityScriptGym] Published catalog unavailable"); return; }
        State.Request_EnsureSharedDemo();
    }
}
