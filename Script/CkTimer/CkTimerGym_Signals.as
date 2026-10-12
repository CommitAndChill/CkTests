//============================================================================
// TIMER GYM - SIGNALS STATION
// Tests: All 8 timer signals (OnUpdate, OnDone, OnPause, OnResume,
//        OnReset, OnStop, OnJump, OnDepleted)
//============================================================================

class UCk_EntityScript_TimerGym_Signals : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY(ExposeOnSpawn) int32 SlotEpoch = 0;
    UPROPERTY(ExposeOnSpawn) bool StageOnly = false;
    UPROPERTY() bool AuthorityPrepared = false;
    UPROPERTY() bool ProjectionConstructed = false;
    UPROPERTY() int32 StationIndex = 2;
    private bool _Authority = false;
    private bool _Active = false;
    private bool _Priming = false;
    private FCk_Handle_Timer _DisplayTimer;
    private TArray<FCk_Handle_Timer> _DemoTimers;
    private TArray<FCk_Handle> _PrimePending;

	FCk_Handle_Timer TestTimer;

	// Signal fire counters
	int32 UpdateCount = 0;
	int32 DoneCount = 0;
	int32 PauseCount = 0;
	int32 ResumeCount = 0;
	int32 ResetCount = 0;
	int32 StopCount = 0;
	int32 JumpCount = 0;
	int32 DepletedCount = 0;

	FString LastSignal = "(none)";
	int32 CycleStep = 0;
	FString CurrentPhase = "Starting";

	FCk_Handle_Timer AutoTimer;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
        _Authority = false; _Active = false; _Priming = false;
        AuthorityPrepared = false; ProjectionConstructed = false;
        _DisplayTimer = FCk_Handle_Timer(); _DemoTimers.Empty(); _PrimePending.Empty();
        UpdateCount = 0;
        DoneCount = 0;
        PauseCount = 0;
        ResumeCount = 0;
        ResetCount = 0;
        StopCount = 0;
        JumpCount = 0;
        DepletedCount = 0;
        LastSignal = "(none)";
        CycleStep = 0;
        CurrentPhase = "Starting";
        TestTimer = FCk_Handle_Timer();
        AutoTimer = FCk_Handle_Timer();
        auto State = CkTimerGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !CkTimerGym::IsLiveEntity(InHandle) ||
            StationIndex != 2 || Generation <= 0 || SlotEpoch <= 0)
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if ((Mode != ECk_Net_NetModeType::Host && Mode != ECk_Net_NetModeType::ClientAndHost &&
             Mode != ECk_Net_NetModeType::Client) ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) !=
                CkTimerGym::FindStation(n"Gym.Timer.Signals"))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Authority = Mode != ECk_Net_NetModeType::Client;
        if (_Authority && (!State.HasAuthority() ||
            !State.Get_IsCurrentConstruction(Generation, SlotEpoch, StationIndex, StageOnly)))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        if (!_Authority && (State.HasAuthority() || !State.Snapshot.Ready ||
            State.Snapshot.Generation != Generation || State.Snapshot.Stations.Num() != 5 ||
            State.Snapshot.Stations[StationIndex].SlotEpoch != SlotEpoch))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Transform = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (!CkTimerGym::IsLiveEntity(FCk_Handle(Transform)) || !utils_transform::Has(InHandle))
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        if (!_Authority)
        {
            _DisplayTimer = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick"));
            ProjectionConstructed = CkTimerGym::IsLiveEntity(FCk_Handle(_DisplayTimer));
            return ECk_EntityScript_ConstructionFlow::Finished;
        }

        // Test timer: 4 seconds, ResetOnDone
		auto TestParams = FCk_Timer_Spec(FCk_Time(4.0f));
		TestParams.Set_StartingState(ECk_Timer_State::Paused);
		TestParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		TestTimer = utils_timer::Add(InHandle, TestParams);

		// Bind all 8 signals
		TestTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"OnTimerUpdate"));
		TestTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnTimerDone"));
		TestTimer.BindTo_OnPause(FCk_Delegate_Timer(this, n"OnTimerPause"));
		TestTimer.BindTo_OnResume(FCk_Delegate_Timer(this, n"OnTimerResume"));
		TestTimer.BindTo_OnReset(FCk_Delegate_Timer(this, n"OnTimerReset"));
		TestTimer.BindTo_OnStop(FCk_Delegate_Timer(this, n"OnTimerStop"));
		TestTimer.BindTo_OnJump(FCk_Delegate_Timer_Jump(this, n"OnTimerJump"));
		TestTimer.BindTo_OnDepleted(FCk_Delegate_Timer(this, n"OnTimerDepleted"));

		// Automation timer: step every 2 seconds
		auto AutoParams = FCk_Timer_Spec(FCk_Time(2.0f));
		AutoParams.Set_StartingState(ECk_Timer_State::Paused);
		AutoParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		AutoTimer = utils_timer::Add(InHandle, AutoParams);
		AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnAutoStep"));

        _DemoTimers.Add(TestTimer);
        _DemoTimers.Add(AutoTimer);
        AuthorityPrepared = Get_HasPreparedAuthorityComposition();
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	// Signal callbacks
	UFUNCTION()
	private void OnTimerUpdate(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		UpdateCount++;
	}

	UFUNCTION()
	private void OnTimerDone(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		DoneCount++;
		LastSignal = "OnDone";
	}

	UFUNCTION()
	private void OnTimerPause(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		PauseCount++;
		LastSignal = "OnPause";
	}

	UFUNCTION()
	private void OnTimerResume(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		ResumeCount++;
		LastSignal = "OnResume";
	}

	UFUNCTION()
	private void OnTimerReset(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		ResetCount++;
		LastSignal = "OnReset";
	}

	UFUNCTION()
	private void OnTimerStop(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		StopCount++;
		LastSignal = "OnStop";
	}

	UFUNCTION()
	private void OnTimerJump(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT, ECk_Timer_JumpDirection InJumpDirection, FCk_Time InJumpAmount)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		JumpCount++;
		LastSignal = "OnJump";
	}

	UFUNCTION()
	private void OnTimerDepleted(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		DepletedCount++;
		LastSignal = "OnDepleted";
	}

	// Automation
	UFUNCTION()
	private void OnAutoStep(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		CycleStep++;

		switch (CycleStep)
		{
			case 1:
				CurrentPhase = "Pause";
				TestTimer.Request_Pause();
				break;
			case 2:
				CurrentPhase = "Resume";
				TestTimer.Request_Resume();
				break;
			case 3:
				CurrentPhase = "Jump +1s";
				TestTimer.Request_Jump(FCk_Request_Timer_Jump(FCk_Time(1.0f)));
				break;
			case 4:
				CurrentPhase = "Reset";
				TestTimer.Request_Reset();
				TestTimer.Request_Resume();
				break;
			case 5:
				CurrentPhase = "Stop";
				TestTimer.Request_Stop();
				break;
			case 6:
				CurrentPhase = "Resume after stop";
				TestTimer.Request_Resume();
				break;
			case 7:
				CurrentPhase = "Consume 3s";
				TestTimer.Request_Consume(FCk_Request_Timer_Consume(FCk_Time(3.0f)));
				break;
			default:
				// Reset all counters and restart cycle
				CycleStep = 0;
				CurrentPhase = "Cycle restart";
				UpdateCount = 0;
				DoneCount = 0;
				PauseCount = 0;
				ResumeCount = 0;
				ResetCount = 0;
				StopCount = 0;
				JumpCount = 0;
				DepletedCount = 0;
				LastSignal = "(reset)";
				TestTimer.Request_Reset();
				TestTimer.Request_Resume();
				break;
		}
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (FCk_Handle(InHandle) != FCk_Handle(_DisplayTimer)) { return; }
        auto State = CkTimerGym::Get_State();
        if (ck::Is_NOT_Valid(State)) { return; }
        auto Self = ck::ToEntity(this);
        if (!_Authority)
        {
            if (!Get_HasValidProjectionComposition()) { return; }
            State.Request_DrawProjection(Self, Generation, SlotEpoch, StationIndex,
                "TIMER SIGNALS", "Tests all 8 timer signal bindings.\nAutomation cycles through Pause/Resume/Jump/Reset/Stop/Consume\nto trigger each signal type. [+] = fired, [-] = not yet fired.");
            return;
        }
        if (!State.Get_IsCurrentServerChild(Self, Generation, SlotEpoch, StationIndex) ||
            State.ServerQuintetCount != 5) { return; }
        if (!Get_HasValidAuthorityComposition())
        { State.Request_FailCurrentServerChild(Self, Generation, SlotEpoch, StationIndex,
            "Station lost a timer dependency"); return; }
        Request_UpdateDisplay();
    }

	FString Get_CheckMark(int32 InCount)
	{
		return (InCount > 0) ? "+" : "-";
	}

	void Request_UpdateDisplay()
	{
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "TIMER SIGNALS (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		auto DisplayText = f"Phase: {CurrentPhase} (Step {CycleStep}/7)\n";
		DisplayText = f"{DisplayText}Last Signal: {LastSignal}\n\n";

		DisplayText = f"{DisplayText}SIGNAL FIRE COUNTS:\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(UpdateCount)}] OnUpdate:   {UpdateCount}\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(DoneCount)}] OnDone:     {DoneCount}\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(PauseCount)}] OnPause:    {PauseCount}\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(ResumeCount)}] OnResume:   {ResumeCount}\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(ResetCount)}] OnReset:    {ResetCount}\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(StopCount)}] OnStop:     {StopCount}\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(JumpCount)}] OnJump:     {JumpCount}\n";
		DisplayText = f"{DisplayText}  [{Get_CheckMark(DepletedCount)}] OnDepleted: {DepletedCount}\n\n";

		auto Chrono = TestTimer.Get_CurrentTimerValue();
		auto State = TestTimer.Get_CurrentState();
		DisplayText = f"{DisplayText}Timer: {Chrono.Conv_ChronoToString()} | State: {State}";

		auto Instructions = "Tests all 8 timer signal bindings.\n"
			+ "Automation cycles through Pause/Resume/Jump/Reset/Stop/Consume\n"
			+ "to trigger each signal type. [+] = fired, [-] = not yet fired.";

		auto SharedState = CkTimerGym::Get_State();
        if (ck::Is_NOT_Valid(SharedState) || !SharedState.Get_IsCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex))
        { return; }
        auto Sample = FCkTimerGym_StationSample();
        Sample.SlotEpoch = SlotEpoch; Sample.Body = DisplayText;
        if (!CkTimerGym::AppendTimer(SelfEntity, TestTimer, Sample))
        { SharedState.Request_FailCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex, "Timer telemetry invalid"); return; }
        if (!CkTimerGym::AppendTimer(SelfEntity, AutoTimer, Sample))
        { SharedState.Request_FailCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex, "Timer telemetry invalid"); return; }
        Sample.Counters.Add(UpdateCount);
        Sample.Counters.Add(DoneCount);
        Sample.Counters.Add(PauseCount);
        Sample.Counters.Add(ResumeCount);
        Sample.Counters.Add(ResetCount);
        Sample.Counters.Add(StopCount);
        Sample.Counters.Add(JumpCount);
        Sample.Counters.Add(DepletedCount);
        SharedState.SubmitStationSample(SelfEntity, Generation, SlotEpoch, StationIndex, Sample);
        if (SharedState.Snapshot.Failed) { return; }
        if (GetWorld().GetNetMode() != ENetMode::NM_DedicatedServer)
        { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, DisplayText, Instructions); }
	}
    private bool Get_HasTimerComposition(bool InRequireDisplay)
    {
        auto Self = ck::ToEntity(this);
        if (!_Authority || !CkTimerGym::IsLiveEntity(Self) || !utils_transform::Has(Self) ||
            _DemoTimers.Num() != 2) { return false; }
        auto Dependents = utils_entity_lifetime::Get_LifetimeDependents(Self);
        int32 TimerCount = 0;
        for (auto Dependent : Dependents)
        {
            if (CkTimerGym::IsLiveEntity(Dependent) && utils_timer::DoCast(Dependent).IsSet())
            { TimerCount++; }
        }
        if (TimerCount != 2 + (InRequireDisplay ? 1 : 0)) { return false; }
        for (int32 i = 0; i < _DemoTimers.Num(); i++)
        {
            auto Handle = FCk_Handle(_DemoTimers[i]);
            if (!CkTimerGym::IsLiveEntity(Handle) || !utils_timer::DoCast(Handle).IsSet() ||
                utils_entity_lifetime::Get_LifetimeOwner(Handle) != Self) { return false; }
            for (int32 j = 0; j < i; j++)
            { if (_DemoTimers[j] == _DemoTimers[i]) { return false; } }
        }
        if (InRequireDisplay)
        {
            auto Tick = FCk_Handle(_DisplayTimer);
            if (!CkTimerGym::IsLiveEntity(Tick) || !utils_timer::DoCast(Tick).IsSet() ||
                utils_entity_lifetime::Get_LifetimeOwner(Tick) != Self ||
                _DisplayTimer.Get_CurrentState() != ECk_Timer_State::Running) { return false; }
        }
        return true;
    }
    bool Get_HasPreparedAuthorityComposition()
    {
        if (!_Authority || _Active || _Priming || !Get_HasTimerComposition(false)) { return false; }
        for (auto Timer : _DemoTimers)
        { if (Timer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; } }
        return true;
    }
    private bool Get_InitiallyRunning(int32 InIndex)
    { return true; }
    bool Request_PrimeAuthority()
    {
        if (!AuthorityPrepared || !Get_HasPreparedAuthorityComposition()) { return false; }
        _Priming = true;
        for (int32 i = 0; i < _DemoTimers.Num(); i++)
        {
            if (!Get_InitiallyRunning(i)) { continue; }
            _PrimePending.Add(FCk_Handle(_DemoTimers[i]));
            _DemoTimers[i].Request_Resume(
                FCk_Delegate_Request_OnCompleted(this, n"OnPrimeResumeCompleted"));
        }
        return true;
    }
    UFUNCTION() private void OnPrimeResumeCompleted(FCk_Handle InOwner,
                                                     ECk_Request_OperationResult InResult)
    {
        if (!_Authority || !_Priming || !_PrimePending.Contains(InOwner)) { return; }
        auto State = CkTimerGym::Get_State();
        auto Self = ck::ToEntity(this);
        if (ck::Is_NOT_Valid(State) || !CkTimerGym::IsLiveEntity(Self)) { return; }
        if (InResult != ECk_Request_OperationResult::Succeeded ||
            !CkTimerGym::IsLiveEntity(InOwner) || !utils_timer::DoCast(InOwner).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(InOwner) != Self)
        { State.Request_FailCurrentPrimingChild(Self, Generation, SlotEpoch, StationIndex); return; }
        for (int32 i = 0; i < _PrimePending.Num(); i++)
        { if (_PrimePending[i] == InOwner) { _PrimePending.RemoveAt(i); break; } }
        if (_PrimePending.Num() == 0)
        { State.Request_OnChildPrimed(Self, Generation, SlotEpoch, StationIndex); }
    }
    bool Get_HasPrimedAuthorityComposition()
    {
        if (!_Priming || _PrimePending.Num() != 0 || !Get_HasTimerComposition(false)) { return false; }
        for (int32 i = 0; i < _DemoTimers.Num(); i++)
        {
            auto Required = Get_InitiallyRunning(i) ? ECk_Timer_State::Running : ECk_Timer_State::Paused;
            if (_DemoTimers[i].Get_CurrentState() != Required) { return false; }
        }
        return true;
    }
    bool Request_ActivateAuthority()
    {
        if (!Get_HasPrimedAuthorityComposition()) { return false; }
        auto Self = ck::ToEntity(this);
        _DisplayTimer = utils_timer::Create_Tick(Self, FCk_Delegate_Timer(this, n"DisplayTick"));
        if (!CkTimerGym::IsLiveEntity(FCk_Handle(_DisplayTimer))) { return false; }
        _Priming = false; _Active = true;
        utils_entity_tag::Add(Self, n"TAG_TimerGym_Signals");
        return Get_HasValidAuthorityComposition();
    }
    bool Get_HasValidAuthorityComposition()
    { return _Authority && _Active && !_Priming && AuthorityPrepared && Get_HasTimerComposition(true); }
    bool Get_HasValidProjectionComposition()
    {
        auto Self = ck::ToEntity(this);
        auto Tick = FCk_Handle(_DisplayTimer);
        if (_Authority || !ProjectionConstructed || !CkTimerGym::IsLiveEntity(Self) ||
            !utils_transform::Has(Self) || _DemoTimers.Num() != 0 ||
            !CkTimerGym::IsLiveEntity(Tick) || !utils_timer::DoCast(Tick).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(Tick) != Self ||
            _DisplayTimer.Get_CurrentState() != ECk_Timer_State::Running) { return false; }
        int32 TimerCount = 0;
        for (auto Dependent : utils_entity_lifetime::Get_LifetimeDependents(Self))
        { if (CkTimerGym::IsLiveEntity(Dependent) && utils_timer::DoCast(Dependent).IsSet())
           { TimerCount++; } }
        if (TimerCount != 1) { return false; }
        return true;
    }
    private bool Get_CanMutate(FCk_Handle_Timer InTimer)
    {
        if (!_Authority || !_Active || _Priming) { return false; }
        bool Known = false;
        for (auto Timer : _DemoTimers) { if (Timer == InTimer) { Known = true; break; } }
        if (!Known) { return false; }
        auto State = CkTimerGym::Get_State();
        auto Self = ck::ToEntity(this);
        if (ck::Is_NOT_Valid(State) ||
            !State.Get_IsCurrentServerChild(Self, Generation, SlotEpoch, StationIndex)) { return false; }
        if (!Get_HasValidAuthorityComposition())
        { State.Request_FailCurrentServerChild(Self, Generation, SlotEpoch, StationIndex,
            "Timer callback lost a dependency"); return false; }
        return true;
    }

}
