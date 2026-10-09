//============================================================================
// CUE GYM - OWNER DESTRUCTION STATION
// Tests: Cues automatically destroyed when owning entity is destroyed
//============================================================================

class UCk_EntityScript_CueGym_OwnerDestruction : UCk_CueGym_SourceDemo
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	FCk_Handle_Timer DisplayTimerHandle;
	FCk_Handle_Timer CycleTimerHandle;
	FCk_Handle_Timer InitialDestroyTimerHandle;
	FCk_Handle_Timer DeferTimerHandle;
	FCk_Handle_Timer ActiveDestroyTimerHandle;
	FCk_Handle DestructionOwnerHandle;

	int32 CycleCount = 0;
	bool OwnerAlive = false;
	int32 CuesSpawned = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		if (!Get_CanRunSource())
		{
			if (ck::IsValid(SourceState)) { SourceState.Request_Fail("OwnerDestruction source construction context invalid"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}
		InHandle.Set_DebugName(n"OwnerDestruction");
		if (!InitialTransform.IsValid() || InitialTransform.ContainsNaN())
		{ SourceState.Request_Fail("Invalid source transform"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto SourceTransform = utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		if (!ck::IsValid(SourceTransform))
		{ SourceState.Request_Fail("Source transform composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto TagReceipt = utils_entity_tag::Add(InHandle, n"TAG_CueGym_OwnerDestruction");
		if (!ck::IsValid(TagReceipt))
		{ SourceState.Request_Fail("Source tag request rejected"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Display tick
		DisplayTimerHandle = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick"));
		if (!ck::IsValid(DisplayTimerHandle))
		{ SourceState.Request_Fail("Display timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Cycle timer: 7 seconds (5s alive + 2s pause before next cycle)
		auto CycleParams = FCk_Timer_Spec(FCk_Time(7.0f));
		CycleParams.Set_StartingState(ECk_Timer_State::Running);
		CycleParams.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		CycleTimerHandle = utils_timer::Add(InHandle, CycleParams);
		if (!ck::IsValid(CycleTimerHandle))
		{ SourceState.Request_Fail("OwnerDestruction cycle timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		CycleTimerHandle.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnCycleTick"));

		// Destroy timer: 5s after spawn, destroy the owner
		auto DestroyParams = FCk_Timer_Spec(FCk_Time(5.0f));
		DestroyParams.Set_StartingState(ECk_Timer_State::Paused);
		DestroyParams.Set_Behavior(ECk_Timer_Behavior::PauseOnDone);
		InitialDestroyTimerHandle = utils_timer::Add(InHandle, DestroyParams);
		if (!ck::IsValid(InitialDestroyTimerHandle))
		{ SourceState.Request_Fail("Initial destroy timer composition failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		InitialDestroyTimerHandle.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnDestroyOwner"));

		// Start the first cycle
		Request_SpawnOwnerAndCues();

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
		CycleCount++;
		Request_SpawnOwnerAndCues();
	}

	UFUNCTION()
	private void OnDestroyOwner(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || InHandle != ActiveDestroyTimerHandle) { return; }
		// Destroy the owner entity - its cues will automatically be destroyed too
		auto SelfEntity = ck::ToEntity(this);
		if (!ck::IsValid(DestructionOwnerHandle))
		{ SourceState.Request_Fail("Destruction owner lost before cleanup"); return; }
		utils_entity_lifetime::Request_DestroyEntity(DestructionOwnerHandle);
		DestructionOwnerHandle = FCk_Handle();
		OwnerAlive = false;
		ck::Trace("CueGym: Owner destroyed - all attached cues should vanish");
	}

	void Request_SpawnOwnerAndCues()
	{
		if (!Get_CanRunSource()) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto BaseLocation = InitialTransform.GetLocation();

		// Spawn a visible owner entity
		auto OwnerTransform = FTransform(InitialTransform.GetRotation(),
			BaseLocation + FVector(-100.0f, 0.0f, 0.0f), FVector(1.0f));
		auto SpawnParams = FCk_Gym_TransformSpawnParams(OwnerTransform);
		auto PendingOwner = utils_entity_script::Request_SpawnEntity(
			SelfEntity, UCk_EntityScript_CueGym_DestructionOwner, FInstancedStruct::Make(SpawnParams));
		if (!utils_pending_entity_script::Get_IsValid(PendingOwner))
		{ SourceState.Request_Fail("Destruction owner spawn rejected"); return; }
		DestructionOwnerHandle = PendingOwner.Get_EntityUnderConstruction();
		if (!ck::IsValid(DestructionOwnerHandle))
		{ SourceState.Request_Fail("Destruction owner receipt invalid"); return; }
		OwnerAlive = true;

		// Wait a frame for the owner to be constructed, then fire cues
		// Use a very short timer to defer cue execution
		auto DeferParams = FCk_Timer_Spec(FCk_Time(0.1f));
		DeferParams.Set_StartingState(ECk_Timer_State::Running);
		DeferParams.Set_Behavior(ECk_Timer_Behavior::PauseOnDone);
		DeferTimerHandle = utils_timer::Add(SelfEntity, DeferParams);
		if (!ck::IsValid(DeferTimerHandle))
		{ SourceState.Request_Fail("Destruction owner defer timer failed"); return; }
		DeferTimerHandle.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnDeferredSpawnCues"));
	}

	UFUNCTION()
	private void OnDeferredSpawnCues(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!Get_CanRunSource() || InHandle != DeferTimerHandle) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto BaseLocation = InitialTransform.GetLocation();

		// Find the owner entity
		if (!ck::IsValid(DestructionOwnerHandle) ||
			!utils_entity_tag::Has(DestructionOwnerHandle, n"TAG_CueGym_DestructionOwner"))
		{ SourceState.Request_Fail("Destruction owner did not materialize"); return; }
		auto OwnerHandle = DestructionOwnerHandle;

		// Fire 3 persistent cues owned by that entity, at different positions around it
		auto CueTag = GameplayTags::ResolveGameplayTag(n"CueGym.OwnerDestruction.Attached");

		auto Cue1Transform = FTransform(InitialTransform.GetRotation(),
			BaseLocation + FVector(-100.0f, -80.0f, 0.0f), FVector(1.0f));
		auto PendingCue1 = utils_cue_generic::Request_ExecuteCue(OwnerHandle, CueTag, FCkCueGym_SpawnParams(Cue1Transform),
			ECk_Cue_ReliabilityPolicy::Unreliable, ECk_Cue_MulticastPolicy::LocalOnly);
		if (!utils_pending_entity_script::Get_IsValid(PendingCue1))
		{ SourceState.Request_Fail("Local cue execution request rejected"); return; }

		auto Cue2Transform = FTransform(InitialTransform.GetRotation(),
			BaseLocation + FVector(-100.0f, 80.0f, 0.0f), FVector(1.0f));
		auto PendingCue2 = utils_cue_generic::Request_ExecuteCue(OwnerHandle, CueTag, FCkCueGym_SpawnParams(Cue2Transform),
			ECk_Cue_ReliabilityPolicy::Unreliable, ECk_Cue_MulticastPolicy::LocalOnly);
		if (!utils_pending_entity_script::Get_IsValid(PendingCue2))
		{ SourceState.Request_Fail("Local cue execution request rejected"); return; }

		auto Cue3Transform = FTransform(InitialTransform.GetRotation(),
			BaseLocation + FVector(-200.0f, 0.0f, 0.0f), FVector(1.0f));
		auto PendingCue3 = utils_cue_generic::Request_ExecuteCue(OwnerHandle, CueTag, FCkCueGym_SpawnParams(Cue3Transform),
			ECk_Cue_ReliabilityPolicy::Unreliable, ECk_Cue_MulticastPolicy::LocalOnly);
		if (!utils_pending_entity_script::Get_IsValid(PendingCue3))
		{ SourceState.Request_Fail("Local cue execution request rejected"); return; }

		CuesSpawned = 3;

		// Start the destroy timer
		// Find the destroy timer (second timer added, index-based isn't reliable, so use a separate approach)
		// Re-create a destroy timer
		auto DestroyParams = FCk_Timer_Spec(FCk_Time(5.0f));
		DestroyParams.Set_StartingState(ECk_Timer_State::Running);
		DestroyParams.Set_Behavior(ECk_Timer_Behavior::PauseOnDone);
		ActiveDestroyTimerHandle = utils_timer::Add(SelfEntity, DestroyParams);
		if (!ck::IsValid(ActiveDestroyTimerHandle))
		{ SourceState.Request_Fail("Destruction timer composition failed"); return; }
		ActiveDestroyTimerHandle.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnDestroyOwner"));

		ck::Trace("CueGym: Spawned 3 persistent cues attached to owner");
	}

	void Request_UpdateDisplay()
	{
		if (!Get_CanRunSource()) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "CUE OWNER DESTRUCTION (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		// Count active cues
		auto AttachedCues = utils_entity_tag::ForEach_Entity(SelfEntity, n"TAG_CueGym_Cue_OwnerDestruction");
		auto OwnerEntities = utils_entity_tag::ForEach_Entity(SelfEntity, n"TAG_CueGym_DestructionOwner");

		auto OwnerStatus = (OwnerEntities.Num() > 0) ? "ALIVE (white sphere)" : "DESTROYED";

		auto DisplayText = f"Cycle: {CycleCount}\n";
		DisplayText = f"{DisplayText}Owner: {OwnerStatus}\n";
		DisplayText = f"{DisplayText}Active Cues: {AttachedCues.Num()}\n\n";

		if (OwnerEntities.Num() > 0)
		{
			DisplayText = f"{DisplayText}Owner alive... will be destroyed soon.\n";
			DisplayText = f"{DisplayText}All 3 cue shapes will vanish with it.";
		}
		else
		{
			DisplayText = f"{DisplayText}Owner destroyed! Cues cleaned up automatically\n";
			DisplayText = f"{DisplayText}by the ECS lifetime dependents system.";
		}

		auto Instructions = "Spawns an owner entity (white box) with 3 persistent cues (blue spheres).\n"
			+ "After 5s the owner is destroyed - all shapes vanish instantly.\n"
			+ "Demonstrates automatic cleanup via lifetime dependents.";

		SubmitDisplay(TitleText, DisplayText, Instructions);
	}
}

//============================================================================
// DESTRUCTION OWNER ENTITY (visible owner for OwnerDestruction station)
//============================================================================

class UCk_EntityScript_CueGym_DestructionOwner : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	float ElapsedTime = 0.0f;
	float LifetimeSeconds = 5.0f;
	FCk_Handle_Pmg_DebugShape ShapeHandle;
	private bool _VisualDrawAttempted = false;

	// An invalid receipt after this flag is set is a failed draw, not startup.
	UFUNCTION(BlueprintPure)
	bool Get_HasVisualDrawAttempted() const
	{
		return _VisualDrawAttempted;
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_VisualShape() const
	{
		return FCk_Handle(ShapeHandle);
	}

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		InHandle.Set_DebugName(n"DestructionOwner");
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_CueGym_DestructionOwner");

		// Tick timer to draw shrinking owner sphere
		utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DrawTick"));

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DrawTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CueGym_Pmg::TryRetirePreviousShape(ck::ToEntity(this), ShapeHandle, _VisualDrawAttempted)) { return; }
		_VisualDrawAttempted = true;
		ElapsedTime = ElapsedTime + float(InDeltaT.Get_Seconds());
		auto Progress = Math::Clamp(ElapsedTime / LifetimeSeconds, 0.0, 1.0);

		// Large white sphere that shrinks over lifetime
		auto Radius = float32(Math::Lerp(35.0, 8.0, Progress));
		auto Brightness = float32(1.0 - Progress * 0.6);

		auto Extent = FVector(Radius, Radius, Radius);
		ShapeHandle = CueGym_Pmg::DrawBox(ck::ToEntity(this), FVector(0.0f, 0.0f, 150.0f),
			FLinearColor(Brightness, Brightness, Brightness, 0.3f), Extent);
	}
}
