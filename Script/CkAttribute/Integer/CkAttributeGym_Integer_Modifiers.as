//============================================================================
// INTEGER MODIFIERS ENTITY SCRIPT
//============================================================================
//
// Demo sequence is a CkStateMachine graph (see
// CkAttributeGym_Integer_Modifiers_Steps.as), not an AutoStep counter. The
// station owns the SM; the step states own what each step DOES.
//
// The panel's [3] per-station auto row still arrives as an
// FCk_Message_Gym_AutoSet broadcast - that transport is shared with every
// not-yet-migrated station - it just routes to an SM pause/resume now.
//============================================================================

class UCk_EntityScript_IntegerGym_Modifiers : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	// A world-owned spawn sets this before construction; the old local gym leaves it false.
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FString LastDisplayTitle;
	FString LastDisplayBody;
	FCk_Handle_IntegerAttribute DamageAttribute;
	FCk_Handle_Timer DisplayTimer;

	FCk_Handle_StateMachine StepMachine;
	FCkGym_SmConfig StepConfig;
	int32 ValueChangeCount = 0;
	private bool _StartRequested = false;
	private bool _StartupSettled = false;
	private bool _StepEntryFailed = false;
	private bool _TransitionGateClosed = false;
	private int32 _GateDisplayPasses = 0;
	private bool _DwellSatisfactionPending = false;
	private FCk_Handle_SmState _EnteredState;
	private TArray<FCk_Handle> _RetiringStates;
	private bool _EntryPending = false;
	private int32 _EntrySerial = 0;
	private bool _PauseRequested = false;
	private bool _PauseRequestCompleted = false;
	private bool _PauseRequestSucceeded = false;
	private bool _TransactionPrepared = false;
	private bool _FinishPending = false;
	private bool _FinishCompleted = false;
	private bool _FinishReleased = false;
	private bool _FinishResumeAuto = false;
	private bool _ResumeRequestCompleted = false;
	private bool _ResumeRequestSucceeded = false;
	private bool _ModifierControlPending = false;
	private bool _ResetPending = false;
	private int32 _ResetPhase = 0;
	private bool _ResetResumeSucceeded = false;
	private bool _ResetTransitionSucceeded = false;
	private int32 _ResetBeforeEntrySerial = 0;
	private bool _ExpectWeapon = false;
	private bool _ExpectBuff = false;
	private int32 _ExpectedWeaponDelta = 0;
	private int32 _ExpectedBuffDelta = 0;
	private TArray<int32> _LegacyActions;
	private TArray<int32> _LegacyDeltas;
	private bool _LegacyCommandStarted = false;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		if (!CkAttributeInteger::IsLiveEntity(InHandle))
		{ Request_ReportStepFailure(); return ECk_EntityScript_ConstructionFlow::Finished; }

		// Steps and dwell conditions resolve their station through the inherited
		// context. A shared world root owns this station's lifetime, while the
		// station itself supplies the state machine's context.
		InHandle.Request_OverrideToSelf();
		if (InHandle.Get_ContextOwner() != InHandle)
		{ Request_ReportStepFailure(); return ECk_EntityScript_ConstructionFlow::Finished; }

		_RetiringStates.Empty();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_IntegerGym_Modifiers");

		// Display timer
		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		// Step sequence. AddDefaults is the initial state so the station opens
		// with both modifiers applied, matching what BeginPlay used to seed
		// before the first auto tick landed.
		auto SmParams = FCk_StateMachine_Spec(UCk_IntegerGym_Step_AddDefaults);
		SmParams.Set_AutoStart(ECk_SmAutoStart::Disabled);
		StepMachine = utils_state_machine::Add(InHandle, SmParams);
		_TransitionGateClosed = WaitForWorldReady;

		StepConfig.Description = "Tests attribute modifier system with add/remove operations.\nDemonstrates weapon and buff modifier stacking.";
		StepConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		StepConfig.PerStationAutoCommand = "panel [3] Modifiers station auto";
		StepConfig.Steps.Add(FCkGym_SmStep(UCk_IntegerGym_Step_AddDefaults, "Re-add default modifiers"));
		StepConfig.Steps.Add(FCkGym_SmStep(UCk_IntegerGym_Step_AddWeapon,   "Add weapon bonus (+25)"));
		StepConfig.Steps.Add(FCkGym_SmStep(UCk_IntegerGym_Step_AddBuff,     "Add buff bonus (+10)"));
		StepConfig.Steps.Add(FCkGym_SmStep(UCk_IntegerGym_Step_ClearAll,    "Clear all modifiers"));
		StepConfig.ManualCommands.Add("panel [I] Weapon bonus ring - +25 / +50 / -25 / -200");
		StepConfig.ManualCommands.Add("panel [O] Buff bonus ring - +10 / +40 / -10 / -150");
		StepConfig.ManualCommands.Add("panel [J] Remove weapon - [K] Remove buff - [M] Clear all modifiers");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		if (_StepEntryFailed || !CkAttributeInteger::IsLiveEntity(InHandle)) { return; }
		Request_SetupAttributes(InHandle);
		if (ck::IsValid(DamageAttribute)) { Request_BindSignals(); }
		else { _StepEntryFailed = true; return; }

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_AddModifier,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnAddModifier"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_RemoveModifier,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnRemoveModifier"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_ClearModifiers,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnClearModifiers"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_Gym_AutoSet,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnAutoSet"));
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto DamageParams = FCk_IntegerAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Damage"),
			100);
		DamageParams.Set_MinMax(ECk_MinMax::Min);
		DamageParams.Set_MinValue(0);
		DamageAttribute = utils_integer_attribute::Add(InHandle, DamageParams);
	}

	void
	Request_BindSignals()
	{
		utils_integer_attribute::BindTo_OnValueChanged(DamageAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_IntegerAttribute_OnValueChanged(this, n"OnDamageChanged"));
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (_TransitionGateClosed) { _GateDisplayPasses++; }
		if (!WaitForWorldReady && !_StartRequested) { Request_StartAuthorityDemo(); }
		Request_TickAuthorityReset();
		Request_TryAcknowledgeStepEntry();
		Request_TickFinishAuthorityTransaction();
		if (!_StartupSettled && Get_HasSettledInitialDefaults()) { _StartupSettled = true; }
		Request_TickLegacyCommand();
		if (Get_HasValidAuthorityComposition()) { Request_UpdateDisplay(); }
	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 7 : 6);
	}

	// Called by the world GameState only after all four server children have composed.
	bool Get_HasValidAuthorityComposition()
	{
		auto Self = ck::ToEntity(this);
		if (_StepEntryFailed || !CkAttributeInteger::IsLiveEntity(Self) ||
			ck::Is_NOT_Valid(StepMachine) || FCk_Handle(StepMachine) != Self ||
			!utils_state_machine::Has(Self)) { return false; }
		auto Damage = FCk_Handle(DamageAttribute);
		if (!CkAttributeInteger::IsLiveEntity(Damage) ||
			utils_entity_lifetime::Get_LifetimeOwner(Damage) != Self ||
			!utils_integer_attribute::DoCast(Damage).IsSet() ||
			!utils_integer_attribute::Has_Component(DamageAttribute, ECk_MinMaxCurrent::Min) ||
			!utils_integer_attribute::Has_Component(DamageAttribute, ECk_MinMaxCurrent::Current) ||
			utils_integer_attribute::Has_Component(DamageAttribute, ECk_MinMaxCurrent::Max))
		{ return false; }
		auto Display = FCk_Handle(DisplayTimer);
		if (!CkAttributeInteger::IsLiveEntity(Display) ||
			utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
			!utils_timer::DoCast(Display).IsSet()) { return false; }
		auto Found = integer_gym_modifiers::Get_DamageAttribute(Self);
		return ck::IsValid(Found) && Found == DamageAttribute;
	}

	bool Request_StartAuthorityDemo()
	{
		if (!Get_HasValidAuthorityComposition() || _StartRequested ||
			StepMachine.Get_RunStatus() != ECk_SmRunStatus::Stopped) { return false; }
		_StartRequested = true;
		StepMachine.Request_Start();
		return true;
	}

	bool Get_HasSettledInitialDefaults()
	{
		if (!Get_HasValidAuthorityComposition() || !_StartRequested ||
			StepMachine.Get_RunStatus() != ECk_SmRunStatus::Running ||
			StepMachine.Get_CurrentStateClass() != UCk_IntegerGym_Step_AddDefaults ||
			_EntryPending ||
			ck::Is_NOT_Valid(_EnteredState) ||
			_EnteredState != StepMachine.Get_CurrentStateHandle()) { return false; }
		auto Weapon = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
		auto Buff = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
		return integer_gym_modifiers::HasExactModifiers(DamageAttribute, Weapon, Buff) &&
			ck::IsValid(Weapon) && ck::IsValid(Buff) &&
			utils_integer_attribute_modifier::Get_Delta(Weapon) == 25 &&
			utils_integer_attribute_modifier::Get_Delta(Buff) == 10 &&
			utils_integer_attribute::Get_BaseValue(DamageAttribute) == 100 &&
			utils_integer_attribute::Get_BonusValue(DamageAttribute) == 35 &&
			utils_integer_attribute::Get_FinalValue(DamageAttribute) == 135;
	}

	bool Request_ReleaseAuthorityStart()
	{
		if (!WaitForWorldReady || !_StartupSettled || !Get_HasSettledInitialDefaults() ||
			!_TransitionGateClosed) { return false; }
		_TransitionGateClosed = false; return true;
	}
	bool Get_HasSettledAuthorityState()
	{
		return Get_HasValidAuthorityComposition() && _StartupSettled &&
			!_EntryPending && !_DwellSatisfactionPending &&
			ck::IsValid(_EnteredState) &&
			_EnteredState == StepMachine.Get_CurrentStateHandle() &&
			(StepMachine.Get_RunStatus() == ECk_SmRunStatus::Running ||
			 StepMachine.Get_RunStatus() == ECk_SmRunStatus::Paused);
	}
	bool Get_IsTransitionGateClosed() const { return _TransitionGateClosed; }
	bool Get_HasStepFailure() const { return _StepEntryFailed; }
	void Request_ReportStepFailure()
	{ _StepEntryFailed = true; _TransitionGateClosed = true;
	  _FinishPending = false; _FinishCompleted = false; }
	bool Request_TryBeginAutoAdvance()
	{
		if (_TransitionGateClosed || _DwellSatisfactionPending || _EntryPending ||
			!Get_HasValidAuthorityComposition() || !_StartupSettled ||
			StepMachine.Get_RunStatus() != ECk_SmRunStatus::Running ||
			ck::Is_NOT_Valid(_EnteredState) ||
			_EnteredState != StepMachine.Get_CurrentStateHandle())
		{ return false; }
		_DwellSatisfactionPending = true;
		return true;
	}

	void Request_RecordStepEntry(FCk_Handle_SmState InState)
	{
		if (!Get_HasValidAuthorityComposition() || ck::Is_NOT_Valid(InState) ||
			StepMachine.Get_CurrentStateHandle() != InState)
		{ Request_ReportStepFailure(); return; }
		_EnteredState = InState;
		_EntryPending = true;
	}

	private void Request_TryAcknowledgeStepEntry()
	{
		if (!_EntryPending || !Get_HasValidAuthorityComposition() ||
			StepMachine.Get_RunStatus() != ECk_SmRunStatus::Running ||
			ck::Is_NOT_Valid(_EnteredState) ||
			_EnteredState != StepMachine.Get_CurrentStateHandle()) { return; }
		auto Weapon = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
		auto Buff = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
		int32 Bonus = 0;
		if (!integer_gym_modifiers::HasExactModifiers(DamageAttribute, Weapon, Buff)) { return; }
		if (ck::IsValid(Weapon)) { Bonus += utils_integer_attribute_modifier::Get_Delta(Weapon); }
		if (ck::IsValid(Buff)) { Bonus += utils_integer_attribute_modifier::Get_Delta(Buff); }
		auto ExpectedPre = 100 + Bonus;
		auto ExpectedFinal = Math::Max(0, ExpectedPre);
		if (utils_integer_attribute::Get_BaseValue(DamageAttribute) != 100 ||
			utils_integer_attribute::Get_BonusValue(DamageAttribute) != ExpectedFinal - 100 ||
			utils_integer_attribute::Get_PreClampFinalValue(DamageAttribute) != ExpectedPre ||
			utils_integer_attribute::Get_FinalValue(DamageAttribute) != ExpectedFinal ||
			utils_integer_attribute::Get_ClampOverflow(DamageAttribute) != ExpectedPre - ExpectedFinal)
		{ return; }
		_EntryPending = false;
		_EntrySerial++;
		_DwellSatisfactionPending = false;
	}

	void Request_RecordStepExit(FCk_Handle_SmState InState)
	{
		if (_EnteredState == InState)
		{ if (_EntryPending) { Request_ReportStepFailure(); }
		  TArray<FCk_Handle> StillRetiring;
		  TArray<FCk_Handle> Dependents;
		  auto Self = ck::ToEntity(this);
		  if (CkAttributeInteger::IsLiveEntity(Self))
		  { Dependents = utils_entity_lifetime::Get_LifetimeDependents(Self); }
		  for (auto Old : _RetiringStates)
		  { if (Dependents.Contains(Old)) { StillRetiring.Add(Old); } }
		  auto Departing = FCk_Handle(InState);
		  if (Dependents.Contains(Departing) && !StillRetiring.Contains(Departing))
		  { StillRetiring.Add(Departing); }
		  _RetiringStates = StillRetiring;
		  _EnteredState = FCk_Handle_SmState(); _EntryPending = false; }
	}
	bool Get_IsExpectedRetiringState(FCk_Handle InState)
	{
		// The census already proved this is a non-live direct dependent. Keep only
		// identities recorded by this station's actual exit callback, through final
		// registry removal; reading fragments of a retired state is unnecessary.
		return _RetiringStates.Contains(InState);
	}

	// The station-specific dwell condition refuses new transitions once the gate closes.
	// GameState polls this; it never mutates while a transition or pause is settling.
	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() || !_StartupSettled ||
			_ModifierControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false;
		if (!_TransitionGateClosed)
		{ _TransitionGateClosed = true; _GateDisplayPasses = 0; return false; }
		if (_GateDisplayPasses < 2 || _DwellSatisfactionPending || _EntryPending) { return false; }
		auto Status = StepMachine.Get_RunStatus();
		if (Status == ECk_SmRunStatus::Paused)
		{
			_TransactionPrepared = (!_PauseRequested ||
				(_PauseRequestCompleted && _PauseRequestSucceeded)) &&
				ck::IsValid(_EnteredState) &&
				_EnteredState == StepMachine.Get_CurrentStateHandle();
			return _TransactionPrepared;
		}
		if (Status != ECk_SmRunStatus::Running || _PauseRequested ||
			ck::Is_NOT_Valid(_EnteredState) ||
			_EnteredState != StepMachine.Get_CurrentStateHandle()) { return false; }
		_PauseRequested = true;
		_PauseRequestCompleted = false;
		_PauseRequestSucceeded = false;
		auto Requested = utils_state_machine::Request_Pause(StepMachine,
			FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityPauseCompleted"));
		if (ck::Is_NOT_Valid(Requested)) { Request_ReportStepFailure(); }
		return false;
	}

	UFUNCTION()
	private void OnAuthorityPauseCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (!_PauseRequested || InOwner != ck::ToEntity(this)) { return; }
		_PauseRequestCompleted = true;
		_PauseRequestSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_PauseRequestSucceeded) { Request_ReportStepFailure(); }
	}

	bool Get_IsAuthorityTransactionPrepared()
	{
		return _TransactionPrepared && Get_HasValidAuthorityComposition() &&
			(!_PauseRequested || (_PauseRequestCompleted && _PauseRequestSucceeded)) &&
			StepMachine.Get_RunStatus() == ECk_SmRunStatus::Paused &&
			ck::IsValid(_EnteredState) &&
			_EnteredState == StepMachine.Get_CurrentStateHandle();
	}

	bool Request_ApplyAuthorityModifierControl(int32 InAction, int32 InDelta)
	{
		// 0/1 add-or-override Weapon/Buff; 2/3 remove; 4 clear.
		if (!Get_IsAuthorityTransactionPrepared() || InAction < 0 || InAction > 4 ||
			(InAction >= 2 && InDelta != 0)) { return false; }
		if (InAction == 0 && InDelta != 25 && InDelta != 50 &&
			InDelta != -25 && InDelta != -200) { return false; }
		if (InAction == 1 && InDelta != 10 && InDelta != 40 &&
			InDelta != -10 && InDelta != -150) { return false; }
		auto WeaponTag = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon");
		auto BuffTag = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff");
		auto Weapon = utils_integer_attribute_modifier::TryGet(DamageAttribute, WeaponTag, ECk_MinMaxCurrent::Current);
		auto Buff = utils_integer_attribute_modifier::TryGet(DamageAttribute, BuffTag, ECk_MinMaxCurrent::Current);
		if (!integer_gym_modifiers::HasExactModifiers(DamageAttribute, Weapon, Buff))
		{ return false; }
		_ExpectWeapon = ck::IsValid(Weapon);
		_ExpectBuff = ck::IsValid(Buff);
		_ExpectedWeaponDelta = _ExpectWeapon ? utils_integer_attribute_modifier::Get_Delta(Weapon) : 0;
		_ExpectedBuffDelta = _ExpectBuff ? utils_integer_attribute_modifier::Get_Delta(Buff) : 0;
		if (InAction == 0 || InAction == 1)
		{
			auto Target = InAction == 0 ? n"Modifier.Weapon" : n"Modifier.Buff";
			if (!integer_gym_modifiers::Request_SetModifier(ck::ToEntity(this), Target, InDelta))
			{ Request_ReportStepFailure(); return false; }
			if (InAction == 0) { _ExpectWeapon = true; _ExpectedWeaponDelta = InDelta; }
			else { _ExpectBuff = true; _ExpectedBuffDelta = InDelta; }
		}
		else if (InAction == 2 || InAction == 3)
		{
			auto Existing = InAction == 2 ? Weapon : Buff;
			if (ck::IsValid(Existing)) { utils_integer_attribute_modifier::Remove(Existing); }
			if (InAction == 2) { _ExpectWeapon = false; _ExpectedWeaponDelta = 0; }
			else { _ExpectBuff = false; _ExpectedBuffDelta = 0; }
		}
		else
		{ if (!Request_ClearAllModifiers()) { return false; }
		  _ExpectWeapon = false; _ExpectBuff = false;
		  _ExpectedWeaponDelta = 0; _ExpectedBuffDelta = 0; }
		_ModifierControlPending = true;
		return true;
	}

	bool Get_HasSettledAuthorityModifierControl()
	{
		if (!_ModifierControlPending || !Get_IsAuthorityTransactionPrepared()) { return false; }
		return Get_HasExpectedAuthorityModifierValues();
	}

	private bool Get_HasExpectedAuthorityModifierValues()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		auto Weapon = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
		auto Buff = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
		if (!integer_gym_modifiers::HasExactModifiers(DamageAttribute, Weapon, Buff) ||
			ck::IsValid(Weapon) != _ExpectWeapon || ck::IsValid(Buff) != _ExpectBuff)
		{ return false; }
		if (_ExpectWeapon && utils_integer_attribute_modifier::Get_Delta(Weapon) != _ExpectedWeaponDelta) { return false; }
		if (_ExpectBuff && utils_integer_attribute_modifier::Get_Delta(Buff) != _ExpectedBuffDelta) { return false; }
		auto ExpectedPre = 100 + _ExpectedWeaponDelta + _ExpectedBuffDelta;
		auto ExpectedFinal = Math::Max(0, ExpectedPre);
		return utils_integer_attribute::Get_BaseValue(DamageAttribute) == 100 &&
			utils_integer_attribute::Get_BonusValue(DamageAttribute) == ExpectedFinal - 100 &&
			utils_integer_attribute::Get_PreClampFinalValue(DamageAttribute) == ExpectedPre &&
			utils_integer_attribute::Get_FinalValue(DamageAttribute) == ExpectedFinal &&
			utils_integer_attribute::Get_ClampOverflow(DamageAttribute) == ExpectedPre - ExpectedFinal;
	}

	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ModifierControlPending || _ResetPending) { return false; }
		if (!Request_ClearAllModifiers()) { return false; }
		_ResetBeforeEntrySerial = _EntrySerial;
		_ResetPending = true;
		_ResetPhase = 1;
		_ResetResumeSucceeded = false;
		_ResetTransitionSucceeded = false;
		ValueChangeCount = 0;
		_TransactionPrepared = false;
		return true;
	}

	private void Request_TickAuthorityReset()
	{
		if (!_ResetPending || _ResetPhase != 1 || !Get_HasValidAuthorityComposition() ||
			StepMachine.Get_RunStatus() != ECk_SmRunStatus::Paused) { return; }
		auto Modifiers = utils_integer_attribute_modifier::ForEach(DamageAttribute, FInstancedStruct(), FCk_Lambda_InHandle());
		if (Modifiers.Num() != 0 || utils_integer_attribute::Get_BonusValue(DamageAttribute) != 0 ||
			utils_integer_attribute::Get_FinalValue(DamageAttribute) != 100) { return; }
		// Only after clear has recomputed may AddDefaults re-enter. Keep dwell gated.
		_ResetPhase = 2;
		auto Resumed = utils_state_machine::Request_Resume(StepMachine,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetResumeCompleted"));
		if (ck::Is_NOT_Valid(Resumed) || _StepEntryFailed)
		{ Request_ReportStepFailure(); return; }
		auto Transitioned = utils_state_machine::Request_Transition(StepMachine, UCk_IntegerGym_Step_AddDefaults,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetTransitionCompleted"));
		if (ck::Is_NOT_Valid(Transitioned)) { Request_ReportStepFailure(); }
	}

	UFUNCTION()
	private void OnResetResumeCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (!_ResetPending || _ResetPhase != 2 || InOwner != ck::ToEntity(this)) { return; }
		_ResetResumeSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_ResetResumeSucceeded) { Request_ReportStepFailure(); }
	}

	UFUNCTION()
	private void OnResetTransitionCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (!_ResetPending || _ResetPhase != 2 || InOwner != ck::ToEntity(this)) { return; }
		_ResetTransitionSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_ResetTransitionSucceeded) { Request_ReportStepFailure(); }
	}

	bool Get_HasSettledAuthorityReset()
	{
		return _ResetPending && _ResetPhase == 2 &&
			_ResetResumeSucceeded && _ResetTransitionSucceeded &&
			_EntrySerial > _ResetBeforeEntrySerial &&
			_TransitionGateClosed && Get_HasSettledInitialDefaults();
	}

	bool Request_BeginFinishAuthorityTransaction(bool InResumeAuto)
	{
		if (_FinishPending || _StepEntryFailed) { return false; }
		if (_ResetPending)
		{ if (!Get_HasSettledAuthorityReset() || !InResumeAuto) { return false; } }
		else if (_ModifierControlPending)
		{ if (!Get_HasSettledAuthorityModifierControl()) { return false; } }
		else if (!Get_IsAuthorityTransactionPrepared()) { return false; }
		_FinishPending = true;
		_FinishCompleted = false;
		_FinishReleased = false;
		_FinishResumeAuto = InResumeAuto;
		_ResumeRequestCompleted = !InResumeAuto || StepMachine.Get_RunStatus() == ECk_SmRunStatus::Running;
		_ResumeRequestSucceeded = _ResumeRequestCompleted;
		if (InResumeAuto && StepMachine.Get_RunStatus() == ECk_SmRunStatus::Paused)
		{
			auto Requested = utils_state_machine::Request_Resume(StepMachine,
				FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityResumeCompleted"));
			if (ck::Is_NOT_Valid(Requested)) { Request_ReportStepFailure(); return false; }
		}
		return !_StepEntryFailed;
	}

	UFUNCTION()
	private void OnAuthorityResumeCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (!_FinishPending || !_FinishResumeAuto || InOwner != ck::ToEntity(this)) { return; }
		_ResumeRequestCompleted = true;
		_ResumeRequestSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_ResumeRequestSucceeded) { Request_ReportStepFailure(); }
	}

	private void Request_TickFinishAuthorityTransaction()
	{
		if (!_FinishPending || _StepEntryFailed || !_ResumeRequestCompleted ||
			!_ResumeRequestSucceeded || !Get_HasValidAuthorityComposition() ||
			_EntryPending || _DwellSatisfactionPending ||
			ck::Is_NOT_Valid(_EnteredState) ||
			_EnteredState != StepMachine.Get_CurrentStateHandle()) { return; }
		if (StepMachine.Get_RunStatus() !=
			(_FinishResumeAuto ? ECk_SmRunStatus::Running : ECk_SmRunStatus::Paused)) { return; }
		// A request callback alone is not a committed attribute projection.
		if (_ResetPending && !Get_HasSettledAuthorityReset()) { return; }
		if (_ModifierControlPending && !Get_HasExpectedAuthorityModifierValues()) { return; }
		_ModifierControlPending = false;
		_ResetPending = false;
		_ResetPhase = 0;
		_ResetResumeSucceeded = false;
		_ResetTransitionSucceeded = false;
		_TransactionPrepared = false;
		_PauseRequested = false;
		_PauseRequestCompleted = false;
		_PauseRequestSucceeded = false;
		_TransitionGateClosed = WaitForWorldReady || !_FinishResumeAuto;
		_GateDisplayPasses = 0;
		_FinishPending = false;
		_FinishCompleted = true;
	}

	bool Get_IsAuthorityFinishPending() const { return _FinishPending; }
	bool Get_HasFinishedAuthorityTransaction() const { return _FinishCompleted; }
	bool Get_CanReleaseAuthorityTransaction()
	{
		return WaitForWorldReady && _FinishCompleted && !_FinishReleased && !_FinishPending &&
			!_StepEntryFailed && Get_HasSettledAuthorityState() &&
			_TransitionGateClosed && StepMachine.Get_RunStatus() ==
				(_FinishResumeAuto ? ECk_SmRunStatus::Running : ECk_SmRunStatus::Paused);
	}
	bool Request_ReleaseAuthorityTransaction()
	{
		if (!Get_CanReleaseAuthorityTransaction()) { return false; }
		_FinishReleased = true; _TransitionGateClosed = !_FinishResumeAuto; return true;
	}

	private void Request_TickLegacyCommand()
	{
		if (WaitForWorldReady || !_StartRequested || _StepEntryFailed) { return; }
		if (_LegacyActions.Num() != _LegacyDeltas.Num())
		{ Request_ReportStepFailure(); return; }
		if (_LegacyActions.Num() == 0) { return; }
		if (_FinishPending) { return; }
		if (_FinishCompleted)
		{
			_LegacyActions.RemoveAt(0);
			_LegacyDeltas.RemoveAt(0);
			_LegacyCommandStarted = false;
			_FinishCompleted = false;
			return;
		}
		auto Action = _LegacyActions[0];
		auto Delta = _LegacyDeltas[0];
		if (_LegacyCommandStarted)
		{
			if (_ResetPending && Get_HasSettledAuthorityReset())
			{ if (!Request_BeginFinishAuthorityTransaction(true)) { Request_ReportStepFailure(); } }
			else if (_ModifierControlPending && Get_HasSettledAuthorityModifierControl())
			{ if (!Request_BeginFinishAuthorityTransaction(false)) { Request_ReportStepFailure(); } }
			return;
		}
		if (Action == 7 && StepMachine.Get_RunStatus() == ECk_SmRunStatus::Running &&
			!_TransitionGateClosed && !_DwellSatisfactionPending && !_EntryPending)
		{ _LegacyActions.RemoveAt(0); _LegacyDeltas.RemoveAt(0); return; }
		if (!Request_TryPrepareAuthorityTransaction()) { return; }
		if (Action >= 6)
		{ if (!Request_BeginFinishAuthorityTransaction(Action == 7)) { Request_ReportStepFailure(); } }
		else if (Action == 5)
		{ if (!Request_BeginAuthorityReset()) { Request_ReportStepFailure(); }
		  else { _LegacyCommandStarted = true; } }
		else if (!Request_ApplyAuthorityModifierControl(Action, Delta))
		{ Request_ReportStepFailure(); }
		else { _LegacyCommandStarted = true; }
	}

	private bool Request_QueueLegacyCommand(int32 InAction, int32 InDelta = 0)
	{
		if (WaitForWorldReady || _StepEntryFailed || InAction < 0 || InAction > 7 ||
			(InAction >= 2 && InDelta != 0) ||
			(InAction == 0 && InDelta != 25 && InDelta != 50 &&
			 InDelta != -25 && InDelta != -200) ||
			(InAction == 1 && InDelta != 10 && InDelta != 40 &&
			 InDelta != -10 && InDelta != -150)) { return false; }
		_LegacyActions.Add(InAction);
		_LegacyDeltas.Add(InDelta);
		return true;
	}

	bool Request_ClearAllModifiers()
	{
		if (!Get_HasValidAuthorityComposition())
		{ Request_ReportStepFailure(); return false; }
		auto Weapon = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
		auto Buff = utils_integer_attribute_modifier::TryGet(DamageAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
		if (!integer_gym_modifiers::HasExactModifiers(DamageAttribute, Weapon, Buff))
		{ Request_ReportStepFailure(); return false; }
		utils_integer_attribute_modifier::Request_ClearAllModifiers(DamageAttribute, ECk_MinMaxCurrent::Current);
		return true;
	}

	void
	Request_UpdateDisplay()
	{
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = f"INTEGER MODIFIERS ({CkGym_Common::Get_NetworkRoleTitle(SelfEntity)})";
		auto DisplayText = gym_sm::FormatHeader(StepConfig, StepMachine);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n\n";

		auto BaseValue = utils_integer_attribute::Get_BaseValue(DamageAttribute);
		auto BonusValue = utils_integer_attribute::Get_BonusValue(DamageAttribute);
		auto FinalValue = utils_integer_attribute::Get_FinalValue(DamageAttribute);

		DisplayText = f"{DisplayText}Damage: {BaseValue} + {BonusValue} = {FinalValue}\n\n";

		auto DamageBar = CkGym_Attribute::Create_ProgressBar(FinalValue, 200.0f, 20);
		DisplayText = f"{DisplayText}[{DamageBar}]\n\n";

		// Count from the live modifier list rather than a parallel bookkeeping
		// array - the old ActiveModifiers count and this loop's count could
		// disagree after a clear that only one of them saw.
		auto ModifierLines = "";
		auto ModifierCount = 0;
		auto Modifiers = utils_integer_attribute_modifier::ForEach(DamageAttribute, FInstancedStruct(), FCk_Lambda_InHandle());
		for (auto InModifier : Modifiers)
		{
			auto Delta = utils_integer_attribute_modifier::Get_Delta(InModifier);
			ModifierCount++;
			ModifierLines = f"{ModifierLines}  Mod {ModifierCount}: +{Delta}\n";
		}
		if (ModifierCount == 0) { ModifierLines = "  No active modifiers\n"; }

		DisplayText = f"{DisplayText}MODIFIERS ({ModifierCount}):\n{ModifierLines}";

		auto Trailer = gym_sm::FormatAutoAndCommands(StepConfig, StepMachine);
		DisplayText = f"{DisplayText}\n{Trailer}";

		LastDisplayTitle = TitleText; LastDisplayBody = DisplayText;
		if (!WaitForWorldReady)
		{ CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, DisplayText, ""); }
	}

	UFUNCTION() void OnDamageChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnValueChanged InPayload) {
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (InAttributeOwnerEntity != ck::ToEntity(this) ||
			!CkAttributeInteger::IsLiveEntity(FCk_Handle(DamageAttribute))) { return; }
 ValueChangeCount++; }

	UFUNCTION()
	private void OnAddModifier(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto TypedPayload = InPayload.Get(FCk_Message_IntegerGym_AddModifier);
		if (TypedPayload.Component != ECk_MinMaxCurrent::Current) { return; }
		auto Weapon = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon");
		auto Buff = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff");
		if (TypedPayload.ModifierName == Weapon) { Request_QueueLegacyCommand(0, TypedPayload.Delta); }
		else if (TypedPayload.ModifierName == Buff) { Request_QueueLegacyCommand(1, TypedPayload.Delta); }
	}

	UFUNCTION()
	private void OnRemoveModifier(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto TypedPayload = InPayload.Get(FCk_Message_IntegerGym_RemoveModifier);
		if (TypedPayload.Component != ECk_MinMaxCurrent::Current) { return; }
		auto Weapon = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon");
		auto Buff = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff");
		if (TypedPayload.ModifierName == Weapon) { Request_QueueLegacyCommand(2); }
		else if (TypedPayload.ModifierName == Buff) { Request_QueueLegacyCommand(3); }
	}

	UFUNCTION()
	private void OnClearModifiers(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		Request_QueueLegacyCommand(4);
	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		Request_QueueLegacyCommand(5);
	}
}
