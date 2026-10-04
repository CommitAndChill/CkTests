// Language=angelscript

class ACk_OnlineGym_Requests_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_OnlineGym_Requests_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
}

class ACk_OnlineGym_Requests_PlayerController : ACk_Gym_Base_PlayerController
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.Online.Requests");
        Station.Title = FText::FromString("ONLINE REQUESTS");
        auto Description = TArray<FText>();
        Description.Add(FText::FromString("The service belongs to GameInstance; this board is a live view."));
        Description.Add(FText::FromString("Submit immediate and held work, then replay late callbacks."));
        Station.Description = Description;
        Stations.Add(Station);
        return Stations;
    }

    void Request_StartGym() override
    {
        Super::Request_StartGym();
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineTest_Subsystem);
        if (ck::IsValid(Probe))
        { Probe.Request_RecordGymVisit(); }
    }

    FString Get_ControlPanelTitle() override
    {
        return "ONLINE REQUESTS - FAKE BACKEND";
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineTest_Subsystem);
        if (ck::Is_NOT_Valid(Probe))
        {
            Rows.Add(CkGym_Control::Status("Verdict", "FAIL: test subsystem unavailable", true));
            return Rows;
        }

        auto Diag = Probe.Get_Diagnostics();
        bool Pass = Diag.Get_State() == ECk_OnlineService_State::Running
            && Diag.Get_Queued() == 0 && Diag.Get_Pending() == 0
            && Diag.Get_Completed() == 4
            && Diag.Get_Cancelled() >= 1
            && Diag.Get_TimedOut() >= 1
            && Diag.Get_IgnoredCallbacks() >= 2
            && Probe.Get_ObservedSuccesses() >= 2
            && Probe.Get_ObservedCompletions() == Diag.Get_Completed()
            && Probe.Get_AdapterExecutions() >= 4
            && Probe.Get_AdapterCancels() >= 2;

        Rows.Add(CkGym_Control::Header("FAKE BACKEND: OPERATION LIFECYCLE"));
        Rows.Add(CkGym_Control::Status("Verdict", Pass ? "PASS: all four outcomes and two stale callbacks observed" : "PENDING: run the four cases and replay both callbacks", !Pass));
        Rows.Add(CkGym_Control::Status("State", Probe.Get_StateText()));
        Rows.Add(CkGym_Control::Status("Generation", Probe.Get_GenerationText()));
        Rows.Add(CkGym_Control::Status("Queue / pending", f"{Diag.Get_Queued()} / {Diag.Get_Pending()}"));
        Rows.Add(CkGym_Control::Status("Completed / cancel / timeout", f"{Diag.Get_Completed()} / {Diag.Get_Cancelled()} / {Diag.Get_TimedOut()}"));
        Rows.Add(CkGym_Control::Status("Ignored callbacks", f"{Diag.Get_IgnoredCallbacks()}"));
        Rows.Add(CkGym_Control::Status("Observed completions / success", f"{Probe.Get_ObservedCompletions()} / {Probe.Get_ObservedSuccesses()}"));
        Rows.Add(CkGym_Control::Status("Adapter execute / cancel", f"{Probe.Get_AdapterExecutions()} / {Probe.Get_AdapterCancels()}"));
        Rows.Add(CkGym_Control::Header("ACTIONS"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Start service"));
        Rows.Add(CkGym_Control::Action(EKeys::I, "I", "Submit inline success"));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Submit held operation"));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Release held success", Probe.Get_HasHeldOperation()));
        Rows.Add(CkGym_Control::Action(EKeys::O, "O", "Replay held callback", Probe.Get_HasHeldOperation()));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Submit 0.2-second timeout"));
        Rows.Add(CkGym_Control::Action(EKeys::L, "L", "Release timed callback late", Probe.Get_HasTimedOperation()));
        Rows.Add(CkGym_Control::Action(EKeys::N, "N", "Cancel held operation", Probe.Get_HasHeldOperation()));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Stop service"));
        Rows.Add(CkGym_Control::Action(EKeys::Z, "Z", "Reset record while stopped"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineTest_Subsystem);
        if (ck::Is_NOT_Valid(Probe))
        { return; }

        if (InRowIndex == 10) { Probe.Request_Start(); }
        else if (InRowIndex == 11) { Probe.Request_SubmitImmediate(); }
        else if (InRowIndex == 12) { Probe.Request_SubmitHeld(); }
        else if (InRowIndex == 13) { Probe.Request_ReleaseHeld(); }
        else if (InRowIndex == 14) { Probe.Request_RepeatHeldCallback(); }
        else if (InRowIndex == 15) { Probe.Request_SubmitShortTimeout(); }
        else if (InRowIndex == 16) { Probe.Request_ReleaseTimedAfterTimeout(); }
        else if (InRowIndex == 17) { Probe.Request_CancelHeld(); }
        else if (InRowIndex == 18) { Probe.Request_Stop(); }
        else if (InRowIndex == 19) { Probe.Request_ResetRecord(); }
    }
}
