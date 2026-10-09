//============================================================================
// CUE GYM - RESTART STATION
// Tests: Restart() method via RestartExisting concurrency policy
//============================================================================

class UCk_EntityScript_CueGym_Restart : UCk_CueGym_SourceDemo
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	FCk_Handle_Timer DisplayTimerHandle;
	FCk_Handle_Timer RefireTimerHandle;

	int32 TriggerCount = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		if (!Get_CanRunSource())
		{
			if (ck::IsValid(SourceState)) { SourceState.Request_Fail("Restart source construction context invalid"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}
		if (!InitialTransform.IsValid() || InitialTransform.ContainsNaN())
		{ SourceState.Request_Fail("Invalid source transform"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto SourceTransform = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		if (!ck::IsValid(SourceTransform))
		{ SourceState.Request_Fail("Source transform composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto TagReceipt = utils_entity_tag::Add(InHandle, n"TAG_CueGym_Restart");
		if (!ck::IsValid(TagReceipt))
		{ SourceState.Request_Fail("Source tag request rejected"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Display tick
		DisplayTimerHandle = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick"));
		if (!ck::IsValid(DisplayTimerHandle))
		{ SourceState.Request_Fail("Display timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Fire the restartable cue immediately
		Request_FireCue();

		// Re-fire timer: every 3 seconds (triggers restart via RestartExisting)
		auto RefireParams = FCk_Timer_Spec(FCk_Time(3.0f));
		RefireParams.Set_StartingState(ECk_Timer_State::Running);
		RefireParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		RefireTimerHandle = utils_timer::Add(InHandle, RefireParams);
		if (!ck::IsValid(RefireTimerHandle))
		{ SourceState.Request_Fail("Restart cycle timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		RefireTimerHandle.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnRefireTick"));

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || InHandle != DisplayTimerHandle) { return; }
		Request_UpdateDisplay();
	}

	UFUNCTION()
	private void OnRefireTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || InHandle != RefireTimerHandle) { return; }
		Request_FireCue();
	}

	void Request_FireCue()
	{
		if (!Get_CanRunSource()) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto BaseLocation = InitialTransform.GetLocation();
		auto CueTransform = FTransform(InitialTransform.GetRotation(),
			BaseLocation + FVector(-100.0f, 0.0f, 0.0f), FVector(1.0f));

		auto PendingCue1 = utils_cue_generic::Request_ExecuteCue(SelfEntity,
			GameplayTags::ResolveGameplayTag(n"CueGym.Restart.Restartable"),
			FCkCueGym_SpawnParams(CueTransform),
			ECk_Cue_ReliabilityPolicy::Unreliable,
			ECk_Cue_MulticastPolicy::LocalOnly);
		if (!utils_pending_entity_script::Get_IsValid(PendingCue1))
		{ SourceState.Request_Fail("Local cue execution request rejected"); return; }

		TriggerCount++;
		ck::Trace(f"CueGym: Restart station fired cue (trigger #{TriggerCount})");
	}

	void Request_UpdateDisplay()
	{
		if (!Get_CanRunSource()) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "CUE RESTART (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		// Count active restartable cues
		auto RestartableEntities = utils_entity_tag::ForEach_Entity(SelfEntity, n"TAG_CueGym_Cue_Restartable");

		auto DisplayText = f"Trigger Count: {TriggerCount}\n";
		DisplayText = f"{DisplayText}Active Instances: {RestartableEntities.Num()}\n\n";
		DisplayText = f"{DisplayText}Cue has RestartExisting policy.\n";
		DisplayText = f"{DisplayText}Re-firing every 3s triggers DoRestart()\n";
		DisplayText = f"{DisplayText}instead of spawning a new instance.\n\n";
		DisplayText = f"{DisplayText}Sphere color cycles continuously\n";
		DisplayText = f"{DisplayText}through the spectrum with each restart.";

		auto Instructions = "Fires an 8s Timed cue, then re-fires every 3s.\n"
			+ "RestartExisting policy restarts the existing cue\n"
			+ "instead of creating a new one. Watch the sphere cycle colors.";

		SubmitDisplay(TitleText, DisplayText, Instructions);
	}
}
