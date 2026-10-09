//============================================================================
// CUE GYM - CONCURRENCY STATION
// Tests: AllowMultiple vs RestartExisting concurrency policies
//============================================================================

class UCk_EntityScript_CueGym_Concurrency : UCk_CueGym_SourceDemo
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	FCk_Handle_Timer DisplayTimerHandle;
	FCk_Handle_Timer CycleTimerHandle;

	int32 MultipleTriggerCount = 0;
	int32 RestartTriggerCount = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		if (!Get_CanRunSource())
		{
			if (ck::IsValid(SourceState)) { SourceState.Request_Fail("Concurrency source construction context invalid"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}
		if (!InitialTransform.IsValid() || InitialTransform.ContainsNaN())
		{ SourceState.Request_Fail("Invalid source transform"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto SourceTransform = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		if (!ck::IsValid(SourceTransform))
		{ SourceState.Request_Fail("Source transform composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto TagReceipt = utils_entity_tag::Add(InHandle, n"TAG_CueGym_Concurrency");
		if (!ck::IsValid(TagReceipt))
		{ SourceState.Request_Fail("Source tag request rejected"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Display tick
		DisplayTimerHandle = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick"));
		if (!ck::IsValid(DisplayTimerHandle))
		{ SourceState.Request_Fail("Display timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Auto-demo timer: fire cues every 2 seconds
		auto CycleParams = FCk_Timer_Spec(FCk_Time(2.0f));
		CycleParams.Set_StartingState(ECk_Timer_State::Running);
		CycleParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		CycleTimerHandle = utils_timer::Add(InHandle, CycleParams);
		if (!ck::IsValid(CycleTimerHandle))
		{ SourceState.Request_Fail("Concurrency cycle timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		CycleTimerHandle.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnCycleTick"));

		// Fire initial cues
		Request_FireBothCues();

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || InHandle != DisplayTimerHandle) { return; }
		Request_UpdateDisplay();
	}

	UFUNCTION()
	private void OnCycleTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || InHandle != CycleTimerHandle) { return; }
		Request_FireBothCues();
	}

	void Request_FireBothCues()
	{
		if (!Get_CanRunSource()) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto BaseLocation = InitialTransform.GetLocation();

		// Left side: AllowMultiple - each instance gets a vertical offset so they stack
		auto MultipleOffset = FVector(-100.0f, -100.0f, float(MultipleTriggerCount % 5) * 40.0f);
		auto MultipleTransform = FTransform(InitialTransform.GetRotation(), BaseLocation + MultipleOffset, FVector(1.0f));
		auto PendingCue1 = utils_cue_generic::Request_ExecuteCue(SelfEntity,
			GameplayTags::ResolveGameplayTag(n"CueGym.Concurrency.Multiple"),
			FCkCueGym_SpawnParams(MultipleTransform),
			ECk_Cue_ReliabilityPolicy::Unreliable,
			ECk_Cue_MulticastPolicy::LocalOnly);
		if (!utils_pending_entity_script::Get_IsValid(PendingCue1))
		{ SourceState.Request_Fail("Local cue execution request rejected"); return; }
		MultipleTriggerCount++;

		// Right side: RestartExisting - always same position, restarts instead of stacking
		auto RestartOffset = FVector(-100.0f, 100.0f, 0.0f);
		auto RestartTransform = FTransform(InitialTransform.GetRotation(), BaseLocation + RestartOffset, FVector(1.0f));
		auto PendingCue2 = utils_cue_generic::Request_ExecuteCue(SelfEntity,
			GameplayTags::ResolveGameplayTag(n"CueGym.Concurrency.Restart"),
			FCkCueGym_SpawnParams(RestartTransform),
			ECk_Cue_ReliabilityPolicy::Unreliable,
			ECk_Cue_MulticastPolicy::LocalOnly);
		if (!utils_pending_entity_script::Get_IsValid(PendingCue2))
		{ SourceState.Request_Fail("Local cue execution request rejected"); return; }
		RestartTriggerCount++;
	}

	void Request_UpdateDisplay()
	{
		if (!Get_CanRunSource()) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "CUE CONCURRENCY (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		// Count active instances
		auto MultipleEntities = utils_entity_tag::ForEach_Entity(SelfEntity, n"TAG_CueGym_Cue_Multiple");
		auto RestartEntities = utils_entity_tag::ForEach_Entity(SelfEntity, n"TAG_CueGym_Cue_Restart");

		auto DisplayText = "=== AllowMultiple (Left) ===\n";
		DisplayText = f"{DisplayText}Triggers: {MultipleTriggerCount}\n";
		DisplayText = f"{DisplayText}Active Instances: {MultipleEntities.Num()}\n\n";
		DisplayText = f"{DisplayText}=== RestartExisting (Right) ===\n";
		DisplayText = f"{DisplayText}Triggers: {RestartTriggerCount}\n";
		DisplayText = f"{DisplayText}Active Instances: {RestartEntities.Num()}\n";

		auto Instructions = "Fires both cue types every 2s:\n"
			+ "  Left: AllowMultiple - spheres shrink and fade\n"
			+ "    green->red over 10s lifetime (new=large, old=small)\n"
			+ "  Right: RestartExisting - single sphere snaps back to\n"
			+ "    large/bright on each restart (never fully shrinks)";

		SubmitDisplay(TitleText, DisplayText, Instructions);
	}
}
