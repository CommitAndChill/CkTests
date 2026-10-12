//============================================================================
// ENTITY LIFECYCLE GYM - DEFERRED ENTITY
// Tests: utils_deferred_entity
//============================================================================

class UCk_EntityScript_EntityLifecycleGym_DeferredSetup : UCkLifecycleGym_DemoBase
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	// Phase tracking
	UPROPERTY() int32 CurrentPhase = 0;
	UPROPERTY() int32 CycleCount = 0;

	// Test results
	UPROPERTY() bool Pass_Create = false;
	UPROPERTY() bool Pass_IsDeferred = false;
	UPROPERTY() bool Pass_HasDeferred = false;
	UPROPERTY() bool Pass_PendingCount = false;
	UPROPERTY() bool Pass_CompleteSetup = false;
	UPROPERTY() bool Pass_SetupCompleteCallback = false;
	UPROPERTY() bool Pass_FullyCompleteCallback = false;
	UPROPERTY() bool Pass_DoCast = false;

	// Display values
	UPROPERTY() int32 PendingCount = 0;
    UPROPERTY() int32 CycleStartCallbacks = 0;
	UPROPERTY() bool SetupCompleteCallbackFired = false;
	UPROPERTY() bool FullyCompleteCallbackFired = false;

	// Deferred entity handle
	UPROPERTY() FCk_Handle_DeferredEntity DeferredHandle;
	UPROPERTY() FCk_Handle SelfHandle;

	// Phase timer
	UPROPERTY() FCk_Handle_Timer PhaseTimer;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
        if (!CheckConstruct(InHandle)) { return ECk_EntityScript_ConstructionFlow::Finished; }
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_LifecycleGym_DeferredSetup");

		SelfHandle = InHandle;

		// Display tick
		if (!SharedAuthority) { utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick")); }

		// Phase timer: 2 seconds per phase
		auto TimerParams = FCk_Timer_Spec(FCk_Time(2.0f));
		TimerParams.Set_StartingState(ECk_Timer_State::Running);
		TimerParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		PhaseTimer = utils_timer::Add(InHandle, TimerParams);
		if ((!CkLifecycleGym::IsLive(FCk_Handle(PhaseTimer)) || !utils_timer::DoCast(FCk_Handle(PhaseTimer)).IsSet())) { FailDemo("Phase timer allocation failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		PhaseTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnPhaseTimerDone"));

		// Start first phase
		Request_ExecutePhase();

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (SharedAuthority || !CkLifecycleGym::IsLive(FCk_Handle(InHandle)) || !utils_timer::DoCast(FCk_Handle(InHandle)).IsSet()) { return; }
		Request_UpdateDisplay();
	}

	UFUNCTION()
	private void OnPhaseTimerDone(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CanRun(SelfHandle) || !CkLifecycleGym::IsLive(FCk_Handle(InHandle)) || !utils_timer::DoCast(FCk_Handle(InHandle)).IsSet() || FCk_Handle(InHandle) != FCk_Handle(PhaseTimer)) { return; }
		CurrentPhase = (CurrentPhase + 1) % 3;

		if (CurrentPhase == 0)
		{
			CycleCount++;
		}

		Request_ExecutePhase();
	}

	void Request_ExecutePhase()
	{
        if (!CanRun(SelfHandle)) { return; }
		if (CurrentPhase == 0)
		{
			// Phase 0: Create deferred entity, query state, bind callbacks
			SetupCompleteCallbackFired = false;
			FullyCompleteCallbackFired = false;
            CycleStartCallbacks = ActualCallbackCount;

			if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(DeferredHandle))) { FailDemo("Prior deferred entity has not retired"); return; }
            DeferredHandle = utils_deferred_entity::Create(SelfHandle);
            Remember(FCk_Handle(DeferredHandle));
			Pass_Create = DeferredHandle.IsValid();
            if (!CkLifecycleGym::IsLive(FCk_Handle(DeferredHandle)) || !utils_deferred_entity::Has(FCk_Handle(DeferredHandle))) { FailDemo("Deferred allocation failed"); return; }

			// Check if deferred
			Pass_IsDeferred = utils_deferred_entity::Get_IsDeferred(DeferredHandle);

			// Check Has
			auto DeferredAsHandle = FCk_Handle(DeferredHandle);
			Pass_HasDeferred = utils_deferred_entity::Has(DeferredAsHandle);

			// Query pending count
			PendingCount = utils_deferred_entity::Get_PendingCount(DeferredHandle);
			Pass_PendingCount = (PendingCount == 1);

			// Bind callbacks
			utils_deferred_entity::BindTo_OnSetupComplete(DeferredHandle,
				FCk_Delegate_DeferredEntity_OnComplete(this, n"OnSetupComplete"));
			utils_deferred_entity::BindTo_OnFullyComplete(DeferredHandle,
				FCk_Delegate_DeferredEntity_OnFullyComplete(this, n"OnFullyComplete"));

			// Test DoCast
			auto CastResult = utils_deferred_entity::DoCast(DeferredAsHandle);
			Pass_DoCast = CastResult.IsSet();
		}
		else if (CurrentPhase == 1)
		{
			// Phase 1: Complete setup
			if (!CkLifecycleGym::IsLive(FCk_Handle(DeferredHandle)) || !utils_deferred_entity::Has(FCk_Handle(DeferredHandle)) || !utils_deferred_entity::Get_IsDeferred(DeferredHandle)) { FailDemo("Deferred completion context lost"); return; }
			utils_deferred_entity::Request_CompleteSetup(DeferredHandle);
			Pass_CompleteSetup = SetupCompleteCallbackFired && FullyCompleteCallbackFired;
		}
		else if (CurrentPhase == 2)
		{
			// Phase 2: Check callback results, cleanup
			Pass_SetupCompleteCallback = SetupCompleteCallbackFired;
			Pass_FullyCompleteCallback = FullyCompleteCallbackFired;

			LastCompletedCycle = CycleCount; LastCycleCallbackCount = ActualCallbackCount - CycleStartCallbacks;
            if (!SetupCompleteCallbackFired || !FullyCompleteCallbackFired || LastCycleCallbackCount != 2) { FailDemo("Deferred callback evidence incomplete"); return; }
			// Destroy the deferred entity's underlying entity
			auto DeferredAsHandle = FCk_Handle(DeferredHandle);
			if (utils_handle::Get_IsValid(DeferredAsHandle))
			{
				utils_entity_lifetime::Request_DestroyEntity(DeferredAsHandle);
			}
		}
	}

	UFUNCTION()
	private void OnSetupComplete(FCk_Handle_DeferredEntity InDeferredEntity)
	{
		if (!CanRun(SelfHandle) || CurrentPhase != 1 || !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(InDeferredEntity)) || FCk_Handle(InDeferredEntity) != FCk_Handle(DeferredHandle)) { return; }
        SetupCompleteCallbackFired = true; ActualCallbackCount++;
	}

	UFUNCTION()
	private void OnFullyComplete(FCk_Handle_DeferredEntity InDeferredEntity)
	{
		if (!CanRun(SelfHandle) || CurrentPhase != 1 || !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FCk_Handle(InDeferredEntity)) || FCk_Handle(InDeferredEntity) != FCk_Handle(DeferredHandle)) { return; }
        FullyCompleteCallbackFired = true; ActualCallbackCount++;
	}

	void Request_UpdateDisplay()
	{
        if (!CanRun(ck::ToEntity(this))) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "DEFERRED ENTITY (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		auto PhaseNames = TArray<FString>();
		PhaseNames.Add("CREATE & QUERY");
		PhaseNames.Add("COMPLETE SETUP");
		PhaseNames.Add("CHECK CALLBACKS");
		if (CurrentPhase < 0 || CurrentPhase >= PhaseNames.Num() || (!CkLifecycleGym::IsLive(FCk_Handle(PhaseTimer)) || !utils_timer::DoCast(FCk_Handle(PhaseTimer)).IsSet())) { FailDemo("Invalid phase or lost timer"); return; }
		auto PhaseName = PhaseNames[CurrentPhase];

		auto D = "";
		D = f"{D}Cycle: {CycleCount}    Phase: {CurrentPhase} ({PhaseName})\n\n";

		D = f"{D}--- Creation ---\n";
		D = f"{D}" + (Pass_Create ? "[+] " : "[-] ") + "Create deferred entity\n";
		D = f"{D}" + (Pass_IsDeferred ? "[+] " : "[-] ") + "Get_IsDeferred = true\n";
		D = f"{D}" + (Pass_HasDeferred ? "[+] " : "[-] ") + "Has(deferred) = true\n";
		D = f"{D}" + (Pass_DoCast ? "[+] " : "[-] ") + "DoCast succeeded\n";
		D = f"{D}    Pending count: {PendingCount}\n\n";

		D = f"{D}--- Completion ---\n";
		D = f"{D}" + (Pass_CompleteSetup ? "[+] " : "[-] ") + "Request_CompleteSetup\n";
		D = f"{D}" + (Pass_SetupCompleteCallback ? "[+] " : "[-] ") + "OnSetupComplete fired\n";
		D = f"{D}" + (Pass_FullyCompleteCallback ? "[+] " : "[-] ") + "OnFullyComplete fired\n";

		// Progress bar
		auto Chrono = PhaseTimer.Get_CurrentTimerValue();
		auto Goal = FCk_Time();
		auto Elapsed = FCk_Time();
		auto Remaining = FCk_Time();
		Chrono.Break_Chrono(Goal, Elapsed, Remaining);
		auto ElapsedMs = Elapsed.Get_Milliseconds();
		auto GoalMs = Goal.Get_Milliseconds();
		auto Ratio = (GoalMs > 0.0) ? float32(ElapsedMs / GoalMs) : 0.0f;
		auto ProgressBar = utils_debug_draw::Create_ASCII_ProgressBar(
			FCk_FloatRange_0to1(Ratio), 25, ECk_ForwardReverse::Forward, ECk_ASCII_ProgressBar_Style::Equal_Symbol);
		D = f"{D}\n[{ProgressBar}]";

		auto AllPassed = Pass_Create && Pass_IsDeferred && Pass_HasDeferred && Pass_PendingCount
			&& Pass_DoCast && Pass_CompleteSetup
			&& Pass_SetupCompleteCallback && Pass_FullyCompleteCallback;

		D = f"{D}\n\nRESULT: " + (AllPassed ? "ALL TESTS PASSED" : (CycleCount > 0 ? "SOME TESTS FAILED" : "RUNNING..."));

		auto Instructions = "Tests deferred entity creation, pending state queries,\n"
			+ "setup completion, and completion callbacks.";

		        Sample = FCkLifecycleGym_RowSample();
        Sample.BranchGeneration = BranchGeneration;
        Sample.Title = TitleText; Sample.Body = D; Sample.Instructions = Instructions;
        Sample.ActualCallbackCount = ActualCallbackCount;
        Sample.LastCompletedCycle = LastCompletedCycle;
        Sample.LastCycleCallbackCount = LastCycleCallbackCount;
        Sample.Results.Add(Pass_Create);
        Sample.Results.Add(Pass_IsDeferred);
        Sample.Results.Add(Pass_HasDeferred);
        Sample.Results.Add(Pass_PendingCount);
        Sample.Results.Add(Pass_CompleteSetup);
        Sample.Results.Add(Pass_SetupCompleteCallback);
        Sample.Results.Add(Pass_FullyCompleteCallback);
        Sample.Results.Add(Pass_DoCast);
        Sample.Counters.Add(CurrentPhase);
        Sample.Counters.Add(CycleCount);
        Sample.Counters.Add(PendingCount);
        Sample.Phase = CurrentPhase; Sample.Cycle = CycleCount;
        Sample.ElapsedMilliseconds = float32(ElapsedMs); Sample.GoalMilliseconds = float32(GoalMs);
        if (!SharedAuthority) { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, D, Instructions); }
	}
}
