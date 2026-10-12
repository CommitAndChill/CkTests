//============================================================================
// ENTITY LIFECYCLE GYM - ENTITY TAGS
// Tests: utils_entity_tag
//============================================================================

class UCk_EntityScript_EntityLifecycleGym_TagSystem : UCkLifecycleGym_DemoBase
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	// Phase tracking
	UPROPERTY() int32 CurrentPhase = 0;
	UPROPERTY() int32 CycleCount = 0;

	// Test results (updated each cycle)
	UPROPERTY() bool Pass_AddTag = false;
	UPROPERTY() bool Pass_TryGetTag = false;
	UPROPERTY() bool Pass_ForEachFound = false;
	UPROPERTY() bool Pass_RemoveTag = false;
	UPROPERTY() bool Pass_ForEachEmpty = false;

	// Display values
	FString RetrievedTagName = "";
	UPROPERTY() int32 ForEachCount_BeforeRemove = 0;
	UPROPERTY() int32 ForEachCount_AfterRemove = 0;

	// Child entity for tag operations
	UPROPERTY() FCk_Handle ChildEntity;
	UPROPERTY() FCk_Handle SelfHandle;

	// Phase timer
	UPROPERTY() FCk_Handle_Timer PhaseTimer;

	// Tag used for testing
	FName TestTagName = n"TAG_LifecycleGym_TestAlpha";

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
        if (!CheckConstruct(InHandle)) { return ECk_EntityScript_ConstructionFlow::Finished; }
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_LifecycleGym_TagSystem");

		SelfHandle = InHandle;

		// Display tick
		if (!SharedAuthority) { utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick")); }

		// Phase timer: 1 second per phase
		auto TimerParams = FCk_Timer_Spec(FCk_Time(1.0f));
		TimerParams.Set_StartingState(ECk_Timer_State::Running);
		TimerParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		PhaseTimer = utils_timer::Add(InHandle, TimerParams);
		if ((!CkLifecycleGym::IsLive(FCk_Handle(PhaseTimer)) || !utils_timer::DoCast(FCk_Handle(PhaseTimer)).IsSet())) { FailDemo("Phase timer allocation failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		PhaseTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnPhaseTimerDone"));

		// Create child entity for tag testing
		ChildEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        if (!CkLifecycleGym::IsLive(ChildEntity)) { FailDemo("Tag child allocation failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		utils_handle::Set_DebugName(ChildEntity, n"TagTestChild");

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
		CurrentPhase = (CurrentPhase + 1) % 4;

		if (CurrentPhase == 0)
		{
			CycleCount++;
		}

		Request_ExecutePhase();
	}

	void Request_ExecutePhase()
	{
        if (!CanRun(SelfHandle)) { return; }
        if (!CkLifecycleGym::IsDirect(ChildEntity, SelfHandle)) { FailDemo("Tag child lost"); return; }
		if (CurrentPhase == 0)
		{
			// Phase 0: Add tag to child
			utils_entity_tag::Add(ChildEntity, TestTagName);
			Pass_AddTag = false;

            Pass_TryGetTag = false; RetrievedTagName = "<pending>";
		}
		else if (CurrentPhase == 1)
		{
			// Phase 1: settled tag observation
			// Verify with Has
			auto HasTag = utils_entity_tag::Has(ChildEntity, TestTagName);
			RetrievedTagName = HasTag ? TestTagName.ToString() : FString("<absent>");
			Pass_TryGetTag = HasTag;
            Pass_AddTag = HasTag;
			// ForEach is registry-wide; explicitly count only our actual child.
			auto Found = utils_entity_tag::ForEach_Entity(SelfHandle, TestTagName);
			ForEachCount_BeforeRemove = 0;
            for (auto H : Found) { if (H == ChildEntity && CkLifecycleGym::IsDirect(H, SelfHandle)) { ForEachCount_BeforeRemove++; } }
			Pass_ForEachFound = (ForEachCount_BeforeRemove >= 1);
		}
		else if (CurrentPhase == 2)
		{
			// Phase 2: Remove tag
			auto RemoveResult = utils_entity_tag::Request_TryRemove(ChildEntity, TestTagName);
			Pass_RemoveTag = (RemoveResult == ECk_SucceededFailed::Succeeded);
		}
		else if (CurrentPhase == 3)
		{
			// Phase 3: Verify tag removed - ForEach should return empty
			auto Found = utils_entity_tag::ForEach_Entity(SelfHandle, TestTagName);
			ForEachCount_AfterRemove = 0;
            for (auto H : Found) { if (H == ChildEntity && CkLifecycleGym::IsDirect(H, SelfHandle)) { ForEachCount_AfterRemove++; } }
			Pass_ForEachEmpty = (ForEachCount_AfterRemove == 0);
            LastCompletedCycle = CycleCount; LastCycleCallbackCount = 0;
		}
	}

	void Request_UpdateDisplay()
	{
        if (!CanRun(ck::ToEntity(this))) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "ENTITY TAGS (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		auto PhaseNames = TArray<FString>();
		PhaseNames.Add("ADD TAG");
		PhaseNames.Add("SEARCH BY TAG");
		PhaseNames.Add("REMOVE TAG");
		PhaseNames.Add("VERIFY REMOVED");
		if (CurrentPhase < 0 || CurrentPhase >= PhaseNames.Num() || (!CkLifecycleGym::IsLive(FCk_Handle(PhaseTimer)) || !utils_timer::DoCast(FCk_Handle(PhaseTimer)).IsSet())) { FailDemo("Invalid phase or lost timer"); return; }
		auto PhaseName = PhaseNames[CurrentPhase];

		auto D = "";
		D = f"{D}Cycle: {CycleCount}    Phase: {CurrentPhase} ({PhaseName})\n\n";

		D = f"{D}--- Tag Operations ---\n";
		D = f"{D}" + (Pass_AddTag ? "[+] " : "[-] ") + f"Add tag '{TestTagName}'\n";
		D = f"{D}" + (Pass_TryGetTag ? "[+] " : "[-] ") + f"Has tag = '{RetrievedTagName}'\n";
		D = f"{D}" + (Pass_ForEachFound ? "[+] " : "[-] ") + f"ForEach_Entity found >= 1 (count: {ForEachCount_BeforeRemove})\n";
		D = f"{D}" + (Pass_RemoveTag ? "[+] " : "[-] ") + "Request_TryRemove succeeded\n";
		D = f"{D}" + (Pass_ForEachEmpty ? "[+] " : "[-] ") + f"ForEach_Entity after remove == 0 (count: {ForEachCount_AfterRemove})\n";

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

		auto AllPassed = Pass_AddTag && Pass_TryGetTag && Pass_ForEachFound
			&& Pass_RemoveTag && Pass_ForEachEmpty;

		D = f"{D}\n\nRESULT: " + (AllPassed ? "ALL TESTS PASSED" : (CycleCount > 0 ? "SOME TESTS FAILED" : "RUNNING..."));

		auto Instructions = "Tests adding, querying, removing, and searching\n"
			+ "by entity tags in automated cycles.";

		        Sample = FCkLifecycleGym_RowSample();
        Sample.BranchGeneration = BranchGeneration;
        Sample.Title = TitleText; Sample.Body = D; Sample.Instructions = Instructions;
        Sample.ActualCallbackCount = ActualCallbackCount;
        Sample.LastCompletedCycle = LastCompletedCycle;
        Sample.LastCycleCallbackCount = LastCycleCallbackCount;
        Sample.Results.Add(Pass_AddTag);
        Sample.Results.Add(Pass_TryGetTag);
        Sample.Results.Add(Pass_ForEachFound);
        Sample.Results.Add(Pass_RemoveTag);
        Sample.Results.Add(Pass_ForEachEmpty);
        Sample.Counters.Add(CurrentPhase);
        Sample.Counters.Add(CycleCount);
        Sample.Counters.Add(ForEachCount_BeforeRemove);
        Sample.Counters.Add(ForEachCount_AfterRemove);
        Sample.Phase = CurrentPhase; Sample.Cycle = CycleCount;
        Sample.ElapsedMilliseconds = float32(ElapsedMs); Sample.GoalMilliseconds = float32(GoalMs);
        if (!SharedAuthority) { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, D, Instructions); }
	}
}
