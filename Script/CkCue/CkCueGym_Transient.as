//============================================================================
// CUE GYM - TRANSIENT STATION
// Tests: Cues executed without a persistent owner entity
//============================================================================

class UCk_EntityScript_CueGym_Transient : UCk_CueGym_SourceDemo
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	FCk_Handle_Timer DisplayTimerHandle;
	FCk_Handle_Timer CycleTimerHandle;

	int32 ExecutionCount = 0;
	private bool _IssuedFirstCue = false;
	private double _RelayPendingSince = -1.0;

	private bool Get_HasLiveCycleTimer()
	{
		const auto Timer = FCk_Handle(CycleTimerHandle);
		return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Timer) &&
			ck::IsValid(Timer) &&
			!utils_entity_lifetime::Get_IsPendingDestroy(
				Timer, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
			!utils_entity_lifetime::Get_IsPendingDestroy(
				Timer, ECk_EntityLifetime_DestructionPhase::Teardown) &&
			!utils_entity_lifetime::Get_IsPendingDestroy(
				Timer, ECk_EntityLifetime_DestructionPhase::Destroyed) &&
			utils_timer::DoCast(Timer).IsSet() &&
			utils_entity_lifetime::Has_LifetimeOwner(Timer) &&
			utils_entity_lifetime::Get_LifetimeOwner(Timer) == ck::ToEntity(this);
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		if (!Get_CanRunSource())
		{
			if (ck::IsValid(SourceState)) { SourceState.Request_Fail("Transient source construction context invalid"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}
		if (!InitialTransform.IsValid() || InitialTransform.ContainsNaN())
		{ SourceState.Request_Fail("Invalid source transform"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto SourceTransform = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		if (!ck::IsValid(SourceTransform))
		{ SourceState.Request_Fail("Source transform composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto TagReceipt = utils_entity_tag::Add(InHandle, n"TAG_CueGym_Transient");
		if (!ck::IsValid(TagReceipt))
		{ SourceState.Request_Fail("Source tag request rejected"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Display tick
		DisplayTimerHandle = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick"));
		if (!ck::IsValid(DisplayTimerHandle))
		{ SourceState.Request_Fail("Display timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Auto-demo timer: fire transient cue every 5 seconds
		auto CycleParams = FCk_Timer_Spec(FCk_Time(5.0f));
		CycleParams.Set_StartingState(ECk_Timer_State::Paused);
		CycleParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		CycleTimerHandle = utils_timer::Add(InHandle, CycleParams);
		if (!ck::IsValid(CycleTimerHandle))
		{ SourceState.Request_Fail("Transient cycle timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		CycleTimerHandle.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnCycleTick"));

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || InHandle != DisplayTimerHandle) { return; }
		if (!_IssuedFirstCue && TryFireTransientCue())
		{
			// Begin the normal five-second cycle only after the first real
			// relay-backed request; a cold-travel wait creates no event serial.
			if (ck::Is_NOT_Valid(utils_timer::Request_Resume(CycleTimerHandle)))
			{ SourceState.Request_Fail("Transient cycle timer resume rejected"); return; }
			_IssuedFirstCue = true;
		}
		Request_UpdateDisplay();
	}

	UFUNCTION()
	private void OnCycleTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || !_IssuedFirstCue ||
			InHandle != CycleTimerHandle) { return; }
		TryFireTransientCue();
	}

	private bool TryFireTransientCue()
	{
		if (!Get_CanRunSource()) { return false; }
		// Request_Resume uses AddOrGet on this handle. Prove its existing
		// composed timer and exact owner before issuing any serial or event.
		if (!Get_HasLiveCycleTimer())
		{ SourceState.Request_Fail("Transient cycle timer context lost"); return false; }
		const double Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
		// A single wall-clock interval is retained across every Pending poll.
		// Expiry wins over a relay that becomes ready after the deadline.
		if (_RelayPendingSince >= 0.0 && Now - _RelayPendingSince >= 50.0)
		{ SourceState.Request_Fail("Transient CueRelay readiness timed out"); return false; }
		auto Readiness = UCkCueGym_SourceVisuals::Get_RelayReadiness(
			SourceState.GetWorld(), SourceState);
		if (Readiness == ECkCueGym_RelayReadiness::Invalid)
		{ SourceState.Request_Fail("Transient CueRelay context invalid"); return false; }
		if (Readiness == ECkCueGym_RelayReadiness::Pending)
		{
			if (_RelayPendingSince < 0.0) { _RelayPendingSince = Now; }
			return false;
		}
		if (Readiness != ECkCueGym_RelayReadiness::Ready)
		{ SourceState.Request_Fail("Transient CueRelay readiness rejected"); return false; }
		auto BaseLocation = InitialTransform.GetLocation();
		auto CueTransform = FTransform(InitialTransform.GetRotation(),
			BaseLocation + FVector(-100.0f, 0.0f, 0.0f), FVector(1.0f));

		if (ExecutionCount >= 2147483646)
		{ SourceState.Request_Fail("Transient request count exhausted"); return false; }
		FCkCueGym_SpawnParams EventParams;
		if (!SourceState.TryMakeTransientParams(this, CueTransform, EventParams))
		{ return false; }
		utils_cue_generic::Request_ExecuteCue_Transient(
			GameplayTags::ResolveGameplayTag(n"CueGym.Transient.NoOwner"), EventParams);

		_RelayPendingSince = -1.0;
		ExecutionCount++;
		ck::Trace(f"CueGym: Transient cue requested (count: {ExecutionCount})");
		return true;
	}

	void Request_UpdateDisplay()
	{
		if (!Get_CanRunSource()) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "CUE TRANSIENT (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		// Count active transient cues
		auto TransientEntities = utils_entity_tag::ForEach_Entity(SelfEntity, n"TAG_CueGym_Cue_Transient");

		auto DisplayText = f"Requests issued: {ExecutionCount}\n";
		DisplayText = f"{DisplayText}Active Transient Cues: {TransientEntities.Num()}\n\n";
		DisplayText = f"{DisplayText}Uses Request_ExecuteCue_Transient()\n";
		DisplayText = f"{DisplayText}which requires no owner entity handle.\n";
		DisplayText = f"{DisplayText}The system provides a transient owner internally.";

		auto Instructions = "Fires a transient cue every 5s via\n"
			+ "Request_ExecuteCue_Transient (no owner needed).\n"
			+ "Each cue lives for 5s (purple sphere).";

		SubmitDisplay(TitleText, DisplayText, Instructions);
	}
}
