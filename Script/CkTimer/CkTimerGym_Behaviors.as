//============================================================================
// TIMER GYM - BEHAVIORS STATION
// Tests: ResetOnDone vs PauseOnDone vs StopOnDone side-by-side
//============================================================================

class UCk_EntityScript_TimerGym_Behaviors : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY(ExposeOnSpawn) int32 SlotEpoch = 0;
    UPROPERTY(ExposeOnSpawn) bool StageOnly = false;
    UPROPERTY() bool AuthorityPrepared = false;
    UPROPERTY() bool ProjectionConstructed = false;
    UPROPERTY() int32 StationIndex = 1;
    private bool _Authority = false;
    private bool _Active = false;
    private bool _Priming = false;
    private FCk_Handle_Timer _DisplayTimer;
    private TArray<FCk_Handle_Timer> _DemoTimers;
    private TArray<FCk_Handle> _PrimePending;

	FCk_Handle_Timer ResetTimer;
	FCk_Handle_Timer PauseTimer;
	FCk_Handle_Timer StopTimer;

	int32 ResetDoneCount = 0;
	int32 PauseDoneCount = 0;
	int32 StopDoneCount = 0;

	// Auto-restart cycle
	FCk_Handle_Timer CycleTimer;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
        _Authority = false; _Active = false; _Priming = false;
        AuthorityPrepared = false; ProjectionConstructed = false;
        _DisplayTimer = FCk_Handle_Timer(); _DemoTimers.Empty(); _PrimePending.Empty();
        ResetDoneCount = 0;
        PauseDoneCount = 0;
        StopDoneCount = 0;
        ResetTimer = FCk_Handle_Timer();
        PauseTimer = FCk_Handle_Timer();
        StopTimer = FCk_Handle_Timer();
        CycleTimer = FCk_Handle_Timer();
        auto State = CkTimerGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !CkTimerGym::IsLiveEntity(InHandle) ||
            StationIndex != 1 || Generation <= 0 || SlotEpoch <= 0)
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Mode = utils_net::Get_EntityNetMode(InHandle);
        if ((Mode != ECk_Net_NetModeType::Host && Mode != ECk_Net_NetModeType::ClientAndHost &&
             Mode != ECk_Net_NetModeType::Client) ||
            utils_entity_lifetime::Get_LifetimeOwner(InHandle) !=
                CkTimerGym::FindStation(n"Gym.Timer.Behaviors"))
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

        Request_CreateDemoTimers(InHandle);

		// Cycle reset timer: every 18 seconds, reset non-looping timers
		auto CycleParams = FCk_Timer_Spec(FCk_Time(18.0f));
		CycleParams.Set_StartingState(ECk_Timer_State::Paused);
		CycleParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		CycleTimer = utils_timer::Add(InHandle, CycleParams);
		CycleTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnCycleReset"));

        _DemoTimers.Add(ResetTimer);
        _DemoTimers.Add(PauseTimer);
        _DemoTimers.Add(StopTimer);
        _DemoTimers.Add(CycleTimer);
        AuthorityPrepared = Get_HasPreparedAuthorityComposition();
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	void Request_CreateDemoTimers(FCk_Handle InHandle)
	{
		// Timer 1: ResetOnDone (loops continuously)
		auto ResetParams = FCk_Timer_Spec(FCk_Time(3.0f));
		ResetParams.Set_StartingState(ECk_Timer_State::Paused);
		ResetParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		ResetTimer = utils_timer::Add(InHandle, ResetParams);
		ResetTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnResetDone"));

		// Timer 2: PauseOnDone (freezes at end)
		auto PauseParams = FCk_Timer_Spec(FCk_Time(3.0f));
		PauseParams.Set_StartingState(ECk_Timer_State::Paused);
		PauseParams.Set_Behavior(ECk_Timer_Behavior::PauseOnDone);
		PauseTimer = utils_timer::Add(InHandle, PauseParams);
		PauseTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnPauseDone"));

		// Timer 3: StopOnDone (resets and stops)
		auto StopParams = FCk_Timer_Spec(FCk_Time(3.0f));
		StopParams.Set_StartingState(ECk_Timer_State::Paused);
		StopParams.Set_Behavior(ECk_Timer_Behavior::StopOnDone);
		StopTimer = utils_timer::Add(InHandle, StopParams);
		StopTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnStopDone"));
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
                "TIMER BEHAVIORS", "Compares three timer behaviors running simultaneously.\nResetOnDone loops, PauseOnDone freezes at end, StopOnDone resets and stops.\nNon-looping timers auto-restart every 18 seconds.");
            return;
        }
        if (!State.Get_IsCurrentServerChild(Self, Generation, SlotEpoch, StationIndex) ||
            State.ServerQuintetCount != 5) { return; }
        if (!Get_HasValidAuthorityComposition())
        { State.Request_FailCurrentServerChild(Self, Generation, SlotEpoch, StationIndex,
            "Station lost a timer dependency"); return; }
        Request_UpdateDisplay();
    }

	UFUNCTION()
	private void OnResetDone(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		ResetDoneCount++;
	}

	UFUNCTION()
	private void OnPauseDone(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		PauseDoneCount++;
	}

	UFUNCTION()
	private void OnStopDone(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		StopDoneCount++;
	}

	UFUNCTION()
	private void OnCycleReset(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!Get_CanMutate(InHandle)) { return; }
		// Reset PauseOnDone and StopOnDone timers so the demo restarts
		PauseDoneCount = 0;
		StopDoneCount = 0;

		PauseTimer.Request_Reset();
		PauseTimer.Request_Resume();

		StopTimer.Request_Reset();
		StopTimer.Request_Resume();
	}

	FString Get_TimerSection(FString InLabel, FCk_Handle_Timer InTimer, int32 InDoneCount)
	{
		auto Chrono = InTimer.Get_CurrentTimerValue();
		auto State = InTimer.Get_CurrentState();
		auto Elapsed = Chrono.Get_TimeElapsed();
		auto Goal = FCk_Time();
		auto ElapsedOut = FCk_Time();
		auto Remaining = FCk_Time();
		Chrono.Break_Chrono(Goal, ElapsedOut, Remaining);

		auto ElapsedMs = Elapsed.Get_Milliseconds();
		auto GoalMs = Goal.Get_Milliseconds();
		auto Ratio = (GoalMs > 0.0) ? float32(ElapsedMs / GoalMs) : 0.0f;
		auto Bar = utils_debug_draw::Create_ASCII_ProgressBar(
			FCk_FloatRange_0to1(Ratio), 20, ECk_ForwardReverse::Forward, ECk_ASCII_ProgressBar_Style::Equal_Symbol);

		auto Text = f"--- {InLabel} ---\n";
		Text = f"{Text}State: {State} | Done: {InDoneCount}\n";
		Text = f"{Text}[{Bar}] {Elapsed.Conv_TimeToString()}/{Goal.Conv_TimeToString()}\n";
		return Text;
	}

	void Request_UpdateDisplay()
	{
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "TIMER BEHAVIORS (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		auto DisplayText = Get_TimerSection("ResetOnDone (loops)", ResetTimer, ResetDoneCount);
		DisplayText = f"{DisplayText}\n";
		DisplayText = f"{DisplayText}" + Get_TimerSection("PauseOnDone (freezes)", PauseTimer, PauseDoneCount);
		DisplayText = f"{DisplayText}\n";
		DisplayText = f"{DisplayText}" + Get_TimerSection("StopOnDone (resets+stops)", StopTimer, StopDoneCount);

		auto Instructions = "Compares three timer behaviors running simultaneously.\n"
			+ "ResetOnDone loops, PauseOnDone freezes at end, StopOnDone resets and stops.\n"
			+ "Non-looping timers auto-restart every 18 seconds.";

		auto SharedState = CkTimerGym::Get_State();
        if (ck::Is_NOT_Valid(SharedState) || !SharedState.Get_IsCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex))
        { return; }
        auto Sample = FCkTimerGym_StationSample();
        Sample.SlotEpoch = SlotEpoch; Sample.Body = DisplayText;
        if (!CkTimerGym::AppendTimer(SelfEntity, ResetTimer, Sample))
        { SharedState.Request_FailCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex, "Timer telemetry invalid"); return; }
        if (!CkTimerGym::AppendTimer(SelfEntity, PauseTimer, Sample))
        { SharedState.Request_FailCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex, "Timer telemetry invalid"); return; }
        if (!CkTimerGym::AppendTimer(SelfEntity, StopTimer, Sample))
        { SharedState.Request_FailCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex, "Timer telemetry invalid"); return; }
        if (!CkTimerGym::AppendTimer(SelfEntity, CycleTimer, Sample))
        { SharedState.Request_FailCurrentServerChild(SelfEntity, Generation, SlotEpoch, StationIndex, "Timer telemetry invalid"); return; }
        Sample.Counters.Add(ResetDoneCount);
        Sample.Counters.Add(PauseDoneCount);
        Sample.Counters.Add(StopDoneCount);
        SharedState.SubmitStationSample(SelfEntity, Generation, SlotEpoch, StationIndex, Sample);
        if (SharedState.Snapshot.Failed) { return; }
        if (GetWorld().GetNetMode() != ENetMode::NM_DedicatedServer)
        { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, DisplayText, Instructions); }
	}
    private bool Get_HasTimerComposition(bool InRequireDisplay)
    {
        auto Self = ck::ToEntity(this);
        if (!_Authority || !CkTimerGym::IsLiveEntity(Self) || !utils_transform::Has(Self) ||
            _DemoTimers.Num() != 4) { return false; }
        auto Dependents = utils_entity_lifetime::Get_LifetimeDependents(Self);
        int32 TimerCount = 0;
        for (auto Dependent : Dependents)
        {
            if (CkTimerGym::IsLiveEntity(Dependent) && utils_timer::DoCast(Dependent).IsSet())
            { TimerCount++; }
        }
        if (TimerCount != 4 + (InRequireDisplay ? 1 : 0)) { return false; }
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
        utils_entity_tag::Add(Self, n"TAG_TimerGym_Behaviors");
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
