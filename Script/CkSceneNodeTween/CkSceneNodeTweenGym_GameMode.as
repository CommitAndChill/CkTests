// Language=angelscript

class ACk_SceneNodeTweenGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    private int32 _RequestedSerial = 0;
    private int32 _StateWaitAttempts = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkSceneNodeTweenGym::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkSceneNodeTweenGym::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if (IsLocalController() && _StateWaitAttempts++ < 200)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority() && State.Get_HasPublishedCatalog())
        { State.Request_EnsureSharedDemo(); }
        if (IsLocalController()) { State.Request_EnsureLocalProjection(); }
    }

    UFUNCTION() private void OnStateWait() { Request_StartGym(); }

    FString Get_ControlPanelTitle() override { return "SCENE NODE + TWEEN"; }
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        auto State = CkSceneNodeTweenGym::Get_State();
        auto Running = ck::IsValid(State) && State.Snapshot.AutoRunning;
        Rows.Add(CkGym_Control::Toggle(EKeys::J, "J", "Auto (all stations)", Running));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset all stations"));
        return Rows;
    }
    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || (InRowIndex != 0 && InRowIndex != 1)) { return; }
        auto State = CkSceneNodeTweenGym::Get_State();
        if (ck::Is_NOT_Valid(State)) { return; }
        _RequestedSerial = Math::Max(_RequestedSerial, LastControlSerial) + 1;
        Server_RequestSceneNodeTweenControl(_RequestedSerial, InRowIndex,
            InRowIndex == 0 ? !State.Snapshot.AutoRunning : State.Snapshot.AutoRunning);
    }

    UFUNCTION(Server)
    void Server_RequestSceneNodeTweenControl(int32 InSerial, int32 InAction, bool InAutoRunning)
    {
        if (!HasAuthority()) { return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        if (InSerial <= 0 || InSerial <= LastControlSerial || InAction < 0 || InAction > 1)
        { LastControlResult = 4; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this)
        { LastControlResult = 2; return; }
        auto State = CkSceneNodeTweenGym::Get_State();
        TArray<ACk_SceneNodeTweenGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_SceneNodeTweenGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto CurrentGym = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || Modes.Num() != 1 || !State.Get_HasPublishedCatalog() ||
            CurrentGym < 0 || CurrentGym >= Registry.Num() ||
            Registry[CurrentGym].DisplayName != "Scene Node + Tween" ||
            !State.Get_HasReadyServerTrio() ||
            ck::Is_NOT_Valid(CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Simple")) ||
            ck::Is_NOT_Valid(CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Chain")) ||
            ck::Is_NOT_Valid(CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Deep")))
        { LastControlResult = 3; return; }
        if (InAction == 0)
        {
            if (!State.Request_SetSharedAuto(InAutoRunning))
            { LastControlResult = 4; return; }
        }
        else if (!State.Request_ResetSharedDemo())
        { LastControlResult = 4; return; }
        LastControlSerial = InSerial;
        LastControlResult = 1;
    }
}

class ACk_SceneNodeTweenGym_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_SceneNodeTweenGym_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
    default GameStateClass = ACk_SceneNodeTweenGym_GameState;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        Super::BeginPlay();
        TArray<ACk_SceneNodeTweenGym_GameState> States;
        GetAllActorsOfClass(ACk_SceneNodeTweenGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0]) ||
            !States[0].Get_HasPublishedCatalog())
        { ck::Error("[SceneNodeTweenGym] Authored GameState/catalog unavailable"); return; }
        States[0].Request_EnsureSharedDemo();
    }
}