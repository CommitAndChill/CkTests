// Language=angelscript

class ACk_OnlineGym_Lifetime_GameMode : ACkTests_Gym_Base_GameMode
{
    default PlayerControllerClass = ACk_OnlineGym_Lifetime_PlayerController;
    default DefaultPawnClass = ACk_Gym_Base_Pawn;
}

class ACk_OnlineGym_Lifetime_PlayerController : ACk_Gym_Base_PlayerController
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(n"Gym.Online.Lifetime");
        Station.Title = FText::FromString("ONLINE LIFETIME");
        auto Description = TArray<FText>();
        Description.Add(FText::FromString("Pin a running generation, travel to another gym, then return."));
        Description.Add(FText::FromString("Arm stop-on-completion to test reentrant shutdown and late delivery."));
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
        return "ONLINE LIFETIME - FAKE BACKEND";
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
        bool TravelPass = Probe.Get_HasPinnedGeneration()
            && Probe.Get_VisitsSincePin() >= 2
            && Probe.Get_IsPinnedGenerationCurrent()
            && Probe.Get_HasTickedSincePin()
            && Diag.Get_State() == ECk_OnlineService_State::Running;
        bool ReentrantPass = Probe.Get_StopsFromCompletion() >= 1
            && Diag.Get_Queued() == 0 && Diag.Get_Pending() == 0 && Diag.Get_Cancelled() >= 1
            && Probe.Get_DomainLateReleaseObserved()
            && Probe.Get_NoCompletionAfterLateRelease()
            && Probe.Get_AdapterShutdowns() >= 1
            && Diag.Get_State() == ECk_OnlineService_State::Stopped;
        bool GenericPass = Probe.Get_StopsFromGenericCompletion() >= 1
            && Diag.Get_Queued() == 0 && Diag.Get_Pending() == 0 && Diag.Get_Cancelled() >= 1
            && Probe.Get_GenericLateReleaseObserved()
            && Probe.Get_NoCompletionAfterLateRelease()
            && Probe.Get_AdapterShutdowns() >= 2
            && Diag.Get_State() == ECk_OnlineService_State::Stopped;

        Rows.Add(CkGym_Control::Header("SERVICE LIFETIME"));
        Rows.Add(CkGym_Control::Status("Verdict", TravelPass ? "PASS: service survived map travel" : "PENDING: pin, travel away, return", !TravelPass));
        Rows.Add(CkGym_Control::Status("Reentrant verdict", ReentrantPass ? "PASS: stopped in listener; late callback silent" : "PENDING: arm, complete, then release held late", !ReentrantPass));
        Rows.Add(CkGym_Control::Status("Generic verdict", GenericPass ? "PASS: stopped in request callback; late callback silent" : "PENDING: restart, hold, press G, release held late", !GenericPass));
        Rows.Add(CkGym_Control::Status("State / generation", Probe.Get_StateText() + " / " + Probe.Get_GenerationText()));
        Rows.Add(CkGym_Control::Status("Visits since pin", f"{Probe.Get_VisitsSincePin()}"));
        Rows.Add(CkGym_Control::Status("Ticks / pending", f"{Diag.Get_TickCount()} / {Diag.Get_Pending()}"));
        Rows.Add(CkGym_Control::Status("Observed / signal stop / generic stop", f"{Probe.Get_ObservedCompletions()} / {Probe.Get_StopsFromCompletion()} / {Probe.Get_StopsFromGenericCompletion()}"));
        Rows.Add(CkGym_Control::Status("Shutdowns / late releases", f"{Probe.Get_AdapterShutdowns()} / {Probe.Get_LateReleaseAttempts()}"));
        Rows.Add(CkGym_Control::Header("ACTIONS"));
        Rows.Add(CkGym_Control::Action(EKeys::J, "J", "Start / restart service"));
        Rows.Add(CkGym_Control::Action(EKeys::P, "P", "Pin generation before travel"));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Submit held operation"));
        Rows.Add(CkGym_Control::Toggle(EKeys::B, "B", "Stop on next completion", Probe.Get_StopOnNextCompletion()));
        Rows.Add(CkGym_Control::Action(EKeys::I, "I", "Submit inline success"));
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Submit inline; stop from generic callback"));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Release held callback, even after stop", Probe.Get_HasHeldOperation()));
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
        else if (InRowIndex == 11) { Probe.Request_PinGenerationForTravel(); }
        else if (InRowIndex == 12) { Probe.Request_SubmitHeld(); }
        else if (InRowIndex == 13) { Probe.Request_SetStopOnNextCompletion(!Probe.Get_StopOnNextCompletion()); }
        else if (InRowIndex == 14) { Probe.Request_SubmitImmediate(); }
        else if (InRowIndex == 15) { Probe.Request_SubmitImmediateStopFromGeneric(); }
        else if (InRowIndex == 16) { Probe.Request_ReleaseHeld(); }
        else if (InRowIndex == 17) { Probe.Request_Stop(); }
        else if (InRowIndex == 18) { Probe.Request_ResetRecord(); }
    }
}
