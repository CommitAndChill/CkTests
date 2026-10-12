class ACk_TimerGym_GameMode : ACkTests_Gym_Base_GameMode
{
	default PlayerControllerClass = ACk_TimerGym_PlayerController;
	default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_TimerGym_GameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        auto State = CkTimerGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasPublishedCatalog())
        { ck::Error("[TimerGym] Authored GameState/catalog unavailable"); return; }
        State.Request_EnsureSharedDemo();
    }
}
