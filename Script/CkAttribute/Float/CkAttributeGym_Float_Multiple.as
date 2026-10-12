//============================================================================
// FLOAT ATTRIBUTE MULTIPLE ATTRIBUTES STATION
//============================================================================

class UCk_EntityScript_AttributeGym_FloatMultiple : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FCk_Handle_Timer DisplayTimer;
	private bool _AuthorityStarted = false;
	private bool _AuthorityFailed = false;
	private bool _GateClosed = false;
	private bool _PrepareRequested = false;
	private bool _PrepareSucceeded = false;
	private bool _Prepared = false;
	private bool _ControlPending = false;
	private bool _ResetPending = false;
	private bool _FinishPending = false;
	private bool _FinishCompleted = false;
	private bool _FinishReleased = false;
	private bool _FinishResume = false;
	private bool _StartSucceeded = false;
	private bool _StartReleased = false;
	private bool _RetirementPending = false;
	private bool _RetirementReset = false;
	private bool _ReplacementStarted = false;
	private int32 _PendingAutoAction = -1;
	private bool _AutoPauseSucceeded = false;
	private bool _BatchPending = false;
	private bool _ExpectFreshCombat = false;
	private int32 _BatchCompletedMask = 0;
	private TArray<float32> _ExpectedBatchValues;
	private TArray<float32> _ExpectedBatchFinalValues;
	private int32 _PendingPasses = 0;
	private TArray<FCk_Handle_FloatAttribute> _RetiredAttributes;
	private TArray<int32> _LegacyActions;
	private bool _LegacyStarted = false;
	private bool _LegacyResumeAuto = false;

	TArray<FCk_Handle_FloatAttribute> AllAttributes;
	TArray<FCk_Handle_FloatAttribute> RPGAttributes;
	TArray<FCk_Handle_FloatAttribute> CombatAttributes;

	FCk_Handle_Timer AutoTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;
	int32 TotalAttributes = 0;
	int32 ValueChangeCount = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatMultiple");

		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(3.5f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			_GateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(3.5f)); }

		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Creates multiple float attributes in batches.\nTests ForEach iteration, name-based lookups, and filtering.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [8] Multiple station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Add combat batch (Attack + Defense)", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test ForEach operations", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test name-based lookup", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Batch value update (+15.5)", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test iteration filtering (>100)", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Clear combat batch", 5, 5));
		AutoConfig.ManualCommands.Add("panel [M] Multiple ring - add batch / clear batch");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_CreateInitialBatch(InHandle);
		if (RPGAttributes.Num() != 3) { _AuthorityFailed = true; return; }
		Request_BindSignals(InHandle);

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_AddBatch,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualAddBatch"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_ClearBatch,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualClearBatch"));
	}

	void Request_CreateInitialBatch(FCk_Handle InHandle)
	{
		auto RPGParams = FCk_MultipleFloatAttribute_Spec();
		auto& RPGList = RPGParams._FloatAttributeParams;

		auto StrengthParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"RPG.Strength"), 85.5f);
		StrengthParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(10.0f).Set_MaxValue(255.0f);
		RPGList.Add(StrengthParams);

		auto AgilityParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"RPG.Agility"), 120.25f);
		AgilityParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(20.0f).Set_MaxValue(200.0f);
		RPGList.Add(AgilityParams);

		auto IntelligenceParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"RPG.Intelligence"), 95.75f);
		IntelligenceParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(15.0f).Set_MaxValue(255.0f);
		RPGList.Add(IntelligenceParams);

		RPGAttributes = utils_float_attribute::AddMultiple(InHandle, RPGParams, ECk_Replication::Replicates);
		AllAttributes.Append(RPGAttributes);
		if (RPGAttributes.Num() == 3) { TotalAttributes = AllAttributes.Num(); }
	}

	void Request_BindSignals(FCk_Handle InHandle)
	{
		for (auto Attribute : AllAttributes)
		{
			utils_float_attribute::BindTo_OnValueChanged(Attribute, ECk_MinMaxCurrent::Current,
				FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
		}
	}

	UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		Request_TickRetirement();
		if ((_ControlPending || _ResetPending) && !_RetirementPending) { _PendingPasses++; }
		Request_TickFinish(); Request_TickLegacy();
		if (!WaitForWorldReady && Get_HasValidAuthorityComposition()) { Request_UpdateDisplay(); }
	}

	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || _GateClosed || _RetirementPending ||
			!Get_HasValidAuthorityComposition()) { return; }
		auto Step = AutoStep % AutoConfig.TotalSteps;

		if (Step == 0)
		{
			if (WaitForWorldReady) { Request_QueueAuthorityAutoAction(0); return; }
			if (!Request_AddCombatBatch()) { _AuthorityFailed = true; return; }
		}
		else if (Step == 1) { Request_TestForEachOperations(); }
		else if (Step == 2) { Request_TestNameBasedLookup(); }
		else if (Step == 3)
		{
			if (WaitForWorldReady) { Request_QueueAuthorityAutoAction(2); return; }
			Request_BatchValueUpdate();
		}
		else if (Step == 4) { Request_TestIterationFiltering(); }
		else if (Step == 5)
		{
			if (WaitForWorldReady) { Request_QueueAuthorityAutoAction(1); return; }
			if (!Request_BeginRetirement(false)) { _AuthorityFailed = true; return; }
		}

		AutoStep++;
	}

	private void Request_QueueAuthorityAutoAction(int32 InAction)
	{
		if (!WaitForWorldReady || _PendingAutoAction != -1 || _GateClosed ||
			(InAction < 0 || InAction > 2)) { _AuthorityFailed = true; return; }
		_PendingAutoAction = InAction; _AutoPauseSucceeded = false; _GateClosed = true;
		auto R = utils_timer::Request_Pause(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAutoPauseCompleted"));
		if (ck::Is_NOT_Valid(R)) { _AuthorityFailed = true; }
	}
	UFUNCTION() private void OnAutoPauseCompleted(FCk_Handle InOwner,
		ECk_Request_OperationResult InResult)
	{
		if (_PendingAutoAction == -1 || InOwner != FCk_Handle(AutoTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_AutoPauseSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_AutoPauseSucceeded) { _AuthorityFailed = true; }
	}
	int32 Get_PendingAuthorityAutoAction()
	{
		if (!Get_HasValidAuthorityComposition()) { return -1; }
		return WaitForWorldReady && _PendingAutoAction != -1 && _AutoPauseSucceeded &&
			AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused ? _PendingAutoAction : -1;
	}
	bool Request_ConsumeAuthorityAutoAction()
	{
		if (_PendingAutoAction == -1 || !_AutoPauseSucceeded || !_FinishCompleted || !_FinishReleased ||
			!AutoRunning || !Get_HasValidAuthorityComposition()) { return false; }
		AutoStep++;
		_PendingAutoAction = -1; _AutoPauseSucceeded = false;
		_BatchPending = false; _BatchCompletedMask = 0;
		_ExpectedBatchValues.Empty(); _ExpectedBatchFinalValues.Empty();
		return true;
	}

	private bool Request_BeginAuthorityBatchUpdate()
	{
		if (!Get_IsAuthorityTransactionPrepared() || RPGAttributes.Num() != 3 ||
			_BatchPending) { return false; }
		_BatchPending = true; _BatchCompletedMask = 0;
		_ExpectedBatchValues.Empty(); _ExpectedBatchFinalValues.Empty();
		for (auto Attribute : RPGAttributes)
		{
			auto Current = utils_float_attribute::Get_FinalValue(Attribute);
			auto Min = utils_float_attribute::Get_FinalValue(Attribute, ECk_MinMaxCurrent::Min);
			auto Max = utils_float_attribute::Get_FinalValue(Attribute, ECk_MinMaxCurrent::Max);
			if (!Get_IsFinite(Current) || !Get_IsFinite(Min) || !Get_IsFinite(Max) || Min > Max)
			{ _AuthorityFailed = true; return false; }
			auto Raw = Math::Clamp(Current + 15.5f, 0.0f, 255.0f);
			_ExpectedBatchValues.Add(Raw);
			_ExpectedBatchFinalValues.Add(Math::Clamp(Raw, Min, Max));
		}
		for (int32 Index = 0; Index < 3; Index++)
		{
			auto R = utils_float_attribute::Request_Override(RPGAttributes[Index],
				_ExpectedBatchValues[Index], ECk_MinMaxCurrent::Current,
				FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityBatchCompleted"));
			if (ck::Is_NOT_Valid(R)) { _AuthorityFailed = true; return false; }
		}
		return true;
	}
	UFUNCTION() private void OnAuthorityBatchCompleted(FCk_Handle InOwner,
		ECk_Request_OperationResult InResult)
	{
		if (!_BatchPending || InResult != ECk_Request_OperationResult::Succeeded ||
			RPGAttributes.Num() != 3) { _AuthorityFailed = true; return; }
		for (int32 Index = 0; Index < 3; Index++)
		{
			int32 Bit = 1 << Index;
			if (InOwner != FCk_Handle(RPGAttributes[Index])) { continue; }
			if ((_BatchCompletedMask & Bit) != 0) { _AuthorityFailed = true; return; }
			_BatchCompletedMask |= Bit; return;
		}
		_AuthorityFailed = true;
	}
	private bool Get_HasSettledBatchUpdate()
	{
		if (!_BatchPending || _BatchCompletedMask != 7 || _ExpectedBatchValues.Num() != 3 ||
			_ExpectedBatchFinalValues.Num() != 3 ||
			_PendingPasses < 2 || !Get_HasValidAuthorityComposition()) { return false; }
		for (int32 Index = 0; Index < 3; Index++)
		{
			auto Value = utils_float_attribute::Get_FinalValue(RPGAttributes[Index]);
			auto Raw = utils_float_attribute::Get_PreClampFinalValue(RPGAttributes[Index]);
			if (!Get_Near(Value, _ExpectedBatchFinalValues[Index]) ||
				!Get_Near(Raw, _ExpectedBatchValues[Index])) { return false; }
		}
		return true;
	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacy(Typed.Enabled ? 4 : 3);
	}

	bool Request_AddCombatBatch()
	{
		if (CombatAttributes.Num() == 2) { return Get_HasValidAuthorityComposition(); }
		if (CombatAttributes.Num() != 0 || _RetirementPending) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto CombatParams = FCk_MultipleFloatAttribute_Spec();
		auto& CombatList = CombatParams._FloatAttributeParams;

		auto AttackParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"Combat.Attack"), 75.5f);
		AttackParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(5.0f).Set_MaxValue(150.0f);
		CombatList.Add(AttackParams);

		auto DefenseParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"Combat.Defense"), 60.25f);
		DefenseParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(100.0f);
		CombatList.Add(DefenseParams);

		CombatAttributes = utils_float_attribute::AddMultiple(SelfEntity, CombatParams, ECk_Replication::Replicates);
		if (CombatAttributes.Num() != 2) { return false; }
		AllAttributes.Append(CombatAttributes);
		TotalAttributes = AllAttributes.Num();

		for (auto Attribute : CombatAttributes)
		{
			utils_float_attribute::BindTo_OnValueChanged(Attribute, ECk_MinMaxCurrent::Current,
				FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
		}
		return true;
	}

	void Request_TestForEachOperations()
	{
		if (!Get_HasValidAuthorityComposition())
		{
			System::PrintString("[AttributeFloatForEach] stage=composition", false, true, FLinearColor::White, 0.0f, NAME_None);
			_AuthorityFailed = true; return;
		}
		auto SelfEntity = ck::ToEntity(this);
		auto AllFound = utils_float_attribute::ForEach(SelfEntity, FInstancedStruct(), FCk_Lambda_InHandle());
		if (AllFound.Num() != AllAttributes.Num())
		{
			System::PrintString(f"[AttributeFloatForEach] stage=count found={AllFound.Num()} expected={AllAttributes.Num()} step={AutoStep}",
				false, true, FLinearColor::White, 0.0f, NAME_None);
			_AuthorityFailed = true; return;
		}
		for (int32 Index = 0; Index < AllAttributes.Num(); Index++)
		{
			if (!AllFound.Contains(AllAttributes[Index]))
			{
				System::PrintString(f"[AttributeFloatForEach] stage=membership index={Index} step={AutoStep}",
					false, true, FLinearColor::White, 0.0f, NAME_None);
				_AuthorityFailed = true; return;
			}
		}
		TotalAttributes = AllFound.Num();
	}

	void Request_TestNameBasedLookup()
	{
		auto SelfEntity = ck::ToEntity(this);
		utils_float_attribute::ForEach_ByName(SelfEntity,
			utils_gameplay_tag::ResolveGameplayTag(n"RPG.Strength"), FInstancedStruct(),
			FCk_Lambda_InHandle(this, n"ModifyStrength"));
	}

	UFUNCTION() private void ModifyStrength(FCk_Handle InHandle, FInstancedStruct InOptionalPayload)
	{
		auto StrengthAttr = InHandle.As_FloatAttribute();
		utils_float_attribute::Request_Override(StrengthAttr, 100.5f, ECk_MinMaxCurrent::Current);
	}

	void Request_BatchValueUpdate()
	{
		for (auto Attribute : RPGAttributes)
		{
			auto CurrentValue = utils_float_attribute::Get_FinalValue(Attribute);
			auto NewValue = Math::Clamp(CurrentValue + 15.5f, 0.0f, 255.0f);
			utils_float_attribute::Request_Override(Attribute, NewValue, ECk_MinMaxCurrent::Current);
		}
	}

	void Request_TestIterationFiltering()
	{
		auto SelfEntity = ck::ToEntity(this);
		utils_float_attribute::ForEach_If(SelfEntity, FInstancedStruct(),
			FCk_Lambda_InHandle(this, n"ProcessHighValue"),
			FCk_Predicate_InHandle_OutResult(this, n"FilterHighValue"));
	}

	UFUNCTION() private void ProcessHighValue(FCk_Handle InHandle, FInstancedStruct InOptionalPayload) { }

	UFUNCTION()
	private void FilterHighValue(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InOptionalPayload)
	{
		auto Attr = InHandle.As_FloatAttribute();
		auto Value = utils_float_attribute::Get_FinalValue(Attr);
		auto Res = OutResult;
		Res.Set(Value > 100.0f);
	}

	void Request_ClearCombatBatch()
	{
		if (!Request_BeginRetirement(false)) { _AuthorityFailed = true; }
	}

	void Request_UpdateDisplay()
	{
		FString TitleText;
		FString DisplayText;
		if (Request_TryBuildPresentation(TitleText, DisplayText))
		{ CkGym_Common::Update_StationDisplay(ck::ToEntity(this), TitleText, DisplayText, ""); }
	}

	bool Get_HasSettledAuthoritySample()
	{
		if (!Get_HasValidAuthorityComposition() || _RetirementPending || _BatchPending ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		if (_PendingAutoAction == -1) { return true; }
		// A shared auto tick records its structural intent before the GameState opens the
		// all-station transaction. Nothing has changed yet, and the initial complete
		// snapshot must be allowed to publish so that GameState can consume the intent.
		return WaitForWorldReady && _PendingAutoAction >= 0 && _PendingAutoAction <= 2 &&
			_AutoPauseSucceeded && _GateClosed &&
			AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused;
	}
	bool Request_TryBuildPresentation(FString& OutTitle, FString& OutBody)
	{
		OutTitle = ""; OutBody = "";
		if (!Get_HasSettledAuthoritySample()) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "FLOAT MULTIPLE (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount} | Total: {AllAttributes.Num()}\n\n";

		DisplayText = f"{DisplayText}RPG ATTRIBUTES ({RPGAttributes.Num()}):\n";
		for (auto Attribute : RPGAttributes)
		{
			auto Value = utils_float_attribute::Get_FinalValue(Attribute);
			DisplayText = f"{DisplayText}  RPG Attr: {Value}\n";
		}

		if (CombatAttributes.Num() > 0)
		{
			DisplayText = f"{DisplayText}\nCOMBAT ATTRIBUTES ({CombatAttributes.Num()}):\n";
			for (auto Attribute : CombatAttributes)
			{
				auto Value = utils_float_attribute::Get_FinalValue(Attribute);
				DisplayText = f"{DisplayText}  Combat Attr: {Value}\n";
			}
		}

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);
		OutTitle = TitleText; OutBody = DisplayText; return true;
	}

	UFUNCTION() void OnValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }

	UFUNCTION()
	private void OnManualAddBatch(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(0); }
	}

	UFUNCTION()
	private void OnManualClearBatch(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(1); }
	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(2); }
	}

	private bool Get_IsFinite(float32 InValue)
	{ return InValue == InValue && Math::Abs(InValue) < 1.0e20f; }
	private bool Get_Near(float32 InA, float32 InB)
	{ return Get_IsFinite(InA) && Get_IsFinite(InB) && Math::Abs(InA - InB) <= 0.001f; }
	private bool Get_HasInitialRPGValues()
	{
		return RPGAttributes.Num() == 3 &&
			Get_Near(utils_float_attribute::Get_FinalValue(RPGAttributes[0]), 85.5f) &&
			Get_Near(utils_float_attribute::Get_FinalValue(RPGAttributes[1]), 120.25f) &&
			Get_Near(utils_float_attribute::Get_FinalValue(RPGAttributes[2]), 95.75f);
	}
	private bool Get_HasInitialCombatValues()
	{
		return CombatAttributes.Num() == 2 &&
			Get_Near(utils_float_attribute::Get_FinalValue(CombatAttributes[0]), 75.5f) &&
			Get_Near(utils_float_attribute::Get_FinalValue(CombatAttributes[1]), 60.25f);
	}

	private bool Get_IsNamedAttribute(FCk_Handle_FloatAttribute InAttribute, FName InName)
	{
		auto Self = ck::ToEntity(this);
		if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(InAttribute)) ||
			utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(InAttribute)) != Self ||
			utils_float_attribute::TryGet(Self,
				utils_gameplay_tag::ResolveGameplayTag(InName)) != InAttribute ||
			!utils_float_attribute::Has_Component(InAttribute, ECk_MinMaxCurrent::Current))
		{ return false; }
		return Get_IsFinite(utils_float_attribute::Get_FinalValue(InAttribute));
	}
	private bool Get_HasAuthoredBounds(FCk_Handle_FloatAttribute InAttribute,
		float32 InMin, float32 InMax)
	{
		if (!utils_float_attribute::Has_Component(InAttribute, ECk_MinMaxCurrent::Min) ||
			!utils_float_attribute::Has_Component(InAttribute, ECk_MinMaxCurrent::Max))
		{ return false; }
		return Get_Near(utils_float_attribute::Get_FinalValue(InAttribute, ECk_MinMaxCurrent::Min), InMin) &&
			Get_Near(utils_float_attribute::Get_FinalValue(InAttribute, ECk_MinMaxCurrent::Max), InMax);
	}

	private bool Request_AppendExactMessenger(FCk_Handle InOwner, UScriptStruct InMessageTemplate,
		TArray<FCk_Handle>& InOutExpected)
	{
		if (!CkAttributeFloat::IsLiveEntity(InOwner) || !ck::IsValid(InMessageTemplate)) { return false; }
		auto Messenger = utils_messaging::TryGet_MessengerEntity(InOwner, InMessageTemplate);
		if (!CkAttributeFloat::IsLiveEntity(Messenger) ||
			utils_entity_lifetime::Get_LifetimeOwner(Messenger) != InOwner ||
			InOutExpected.Contains(Messenger)) { return false; }
		InOutExpected.Add(Messenger); return true;
	}
	private bool Get_HasExactMessengers(TArray<FCk_Handle>& Out)
	{
		Out.Empty();
		auto Self = ck::ToEntity(this);
		if (!CkAttributeFloat::IsLiveEntity(Self)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_AttributeGym_ResetAttributes, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_AddBatch, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_ClearBatch, Out)) { return false; }
		if (!WaitForWorldReady &&
			!Request_AppendExactMessenger(Self, FCk_Message_Gym_AutoSet, Out))
		{ return false; }
		return true;
	}
	bool Get_HasLiveAuthorityDependencies()
	{
		if (!_RetirementPending) { return Get_HasValidAuthorityComposition(); }
		// Only an admitted shared U or R may temporarily retire captured attributes.
		if (!WaitForWorldReady || _AuthorityFailed || !_GateClosed ||
			(!_ResetPending && !_ControlPending) || _RetirementReset != _ResetPending)
		{ return false; }
		auto Self = ck::ToEntity(this);
		auto Display = FCk_Handle(DisplayTimer);
		auto Auto = FCk_Handle(AutoTimer);
		if (!CkAttributeFloat::IsLiveEntity(Self) ||
			!CkAttributeFloat::IsLiveEntity(Display) || !CkAttributeFloat::IsLiveEntity(Auto) ||
			Display == Auto || utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
			utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
			!utils_timer::DoCast(Display).IsSet() || !utils_timer::DoCast(Auto).IsSet())
		{ return false; }
		if (_ReplacementStarted) { return Get_HasReplacementComposition(); }
		if (RPGAttributes.Num() != 3 ||
			(CombatAttributes.Num() != 0 && CombatAttributes.Num() != 2) ||
			AllAttributes.Num() != RPGAttributes.Num() + CombatAttributes.Num() ||
			_RetiredAttributes.Num() != CombatAttributes.Num() + (_RetirementReset ? 3 : 0) ||
			AutoConfig.TotalSteps != 6 || AutoStep < 0 || ValueChangeCount < 0 ||
			TotalAttributes < 0) { return false; }
		TArray<FCk_Handle> Messengers;
		if (!Get_HasExactMessengers(Messengers)) { return false; }
		TArray<FCk_Handle> Expected;
		Expected.Add(Display); Expected.Add(Auto);
		for (int32 Index = 0; Index < RPGAttributes.Num(); Index++)
		{
			auto Attribute = FCk_Handle(RPGAttributes[Index]);
			if (FCk_Handle(AllAttributes[Index]) != Attribute ||
				(CkAttributeFloat::IsLiveEntity(Attribute) && Expected.Contains(Attribute)))
			{ return false; }
			Expected.Add(Attribute);
			if (!_RetirementReset &&
				(!Get_IsNamedAttribute(RPGAttributes[Index], Index == 0 ? n"RPG.Strength" :
					(Index == 1 ? n"RPG.Agility" : n"RPG.Intelligence")) ||
				!Get_HasAuthoredBounds(RPGAttributes[Index], Index == 0 ? 10.0f :
					(Index == 1 ? 20.0f : 15.0f), Index == 1 ? 200.0f : 255.0f)))
			{ return false; }
		}
		for (int32 Index = 0; Index < CombatAttributes.Num(); Index++)
		{
			auto Attribute = FCk_Handle(CombatAttributes[Index]);
			if (FCk_Handle(AllAttributes[Index + 3]) != Attribute ||
				(CkAttributeFloat::IsLiveEntity(Attribute) && Expected.Contains(Attribute)) ||
				_RetiredAttributes[Index] != CombatAttributes[Index]) { return false; }
			Expected.Add(Attribute);
		}
		if (_RetirementReset)
		{ for (int32 Index = 0; Index < 3; Index++)
			{ if (_RetiredAttributes[Index + CombatAttributes.Num()] != RPGAttributes[Index])
			  { return false; } } }
		for (auto Messenger : Messengers) { Expected.Add(Messenger); }
		int32 Live = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
		{
			if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
			if (!Expected.Contains(Child) || utils_entity_lifetime::Get_LifetimeOwner(Child) != Self)
			{ return false; }
			Live++;
		}
		return Live >= 2 && Live <= Expected.Num();
	}
	bool Get_HasValidAuthorityComposition()
	{
		auto Self = ck::ToEntity(this);
		if (_AuthorityFailed || _RetirementPending || !CkAttributeFloat::IsLiveEntity(Self) ||
			RPGAttributes.Num() != 3 || (CombatAttributes.Num() != 0 && CombatAttributes.Num() != 2) ||
			AllAttributes.Num() != RPGAttributes.Num() + CombatAttributes.Num() ||
			AutoConfig.TotalSteps != 6 || AutoStep < 0 || ValueChangeCount < 0 || TotalAttributes < 0 ||
			!Get_IsNamedAttribute(RPGAttributes[0], n"RPG.Strength") ||
			!Get_IsNamedAttribute(RPGAttributes[1], n"RPG.Agility") ||
			!Get_IsNamedAttribute(RPGAttributes[2], n"RPG.Intelligence") ||
			!Get_HasAuthoredBounds(RPGAttributes[0], 10.0f, 255.0f) ||
			!Get_HasAuthoredBounds(RPGAttributes[1], 20.0f, 200.0f) ||
			!Get_HasAuthoredBounds(RPGAttributes[2], 15.0f, 255.0f)) { return false; }
		if (CombatAttributes.Num() == 2 &&
			(!Get_IsNamedAttribute(CombatAttributes[0], n"Combat.Attack") ||
			 !Get_IsNamedAttribute(CombatAttributes[1], n"Combat.Defense") ||
			 !Get_HasAuthoredBounds(CombatAttributes[0], 5.0f, 150.0f) ||
			 !Get_HasAuthoredBounds(CombatAttributes[1], 0.0f, 100.0f))) { return false; }
		TArray<FCk_Handle> Messengers;
		if (!Get_HasExactMessengers(Messengers)) { return false; }
		TArray<FCk_Handle> Expected;
		Expected.Add(FCk_Handle(DisplayTimer)); Expected.Add(FCk_Handle(AutoTimer));
		for (auto Attribute : RPGAttributes) { Expected.Add(FCk_Handle(Attribute)); }
		for (auto Attribute : CombatAttributes) { Expected.Add(FCk_Handle(Attribute)); }
		for (int32 i = 0; i < Expected.Num(); i++)
		{
			if (!CkAttributeFloat::IsLiveEntity(Expected[i]) ||
				utils_entity_lifetime::Get_LifetimeOwner(Expected[i]) != Self) { return false; }
			for (int32 j = i + 1; j < Expected.Num(); j++)
			{ if (Expected[i] == Expected[j]) { return false; } }
		}
		if (!utils_timer::DoCast(Expected[0]).IsSet() ||
			!utils_timer::DoCast(Expected[1]).IsSet()) { return false; }
		for (int32 i = 0; i < AllAttributes.Num(); i++)
		{ if (FCk_Handle(AllAttributes[i]) != Expected[i + 2]) { return false; } }
		for (auto Messenger : Messengers) { Expected.Add(Messenger); }
		int32 Live = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
		{
			if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
			if (!Expected.Contains(Child)) { return false; }
			Live++;
		}
		return Live == Expected.Num();
	}

	bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
	UFUNCTION() private void OnAutoResumeCompleted(FCk_Handle InOwner,
		ECk_Request_OperationResult InResult)
	{
		if (!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (InOwner != FCk_Handle(AutoTimer) ||
			InResult != ECk_Request_OperationResult::Succeeded) { _AuthorityFailed = true; }
	}
	bool Request_StartAuthorityDemo()
	{
		if (!WaitForWorldReady || _AuthorityStarted || !Get_HasValidAuthorityComposition() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }

		_AuthorityStarted = true; _StartSucceeded = true;
		return true;
	}
	bool Get_HasSettledAuthorityStart()
	{ return _AuthorityStarted && !_StartReleased && _StartSucceeded &&
		Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	bool Request_ReleaseAuthorityStart()
	{
		if (!Get_HasSettledAuthorityStart()) { return false; }
		if (ck::Is_NOT_Valid(utils_timer::Request_Resume(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAutoResumeCompleted"))))
		{ _AuthorityFailed = true; return false; }
		_StartReleased = true; _GateClosed = false; return true;
	}

	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false; _GateClosed = true;
		if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused)
		{ _Prepared = !_PrepareRequested || _PrepareSucceeded; return _Prepared; }
		if (_PrepareRequested || AutoTimer.Get_CurrentState() != ECk_Timer_State::Running) { return false; }
		_PrepareRequested = true; _PrepareSucceeded = false;
		auto R = utils_timer::Request_Pause(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnPrepareCompleted"));
		if (ck::Is_NOT_Valid(R)) { _AuthorityFailed = true; }
		return false;
	}
	UFUNCTION() private void OnPrepareCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_PrepareRequested || InOwner != FCk_Handle(AutoTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_PrepareSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_PrepareSucceeded) { _AuthorityFailed = true; }
	}
	bool Get_IsAuthorityTransactionPrepared()
	{ return _Prepared && !_RetirementPending && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }

	private bool Request_BeginRetirement(bool InReset)
	{
		if (_RetirementPending || _AuthorityFailed ||
			(WaitForWorldReady && _GateClosed && !_Prepared) ||
			!Get_HasValidAuthorityComposition()) { return false; }
		_RetiredAttributes.Empty();
		for (auto Attribute : CombatAttributes) { _RetiredAttributes.Add(Attribute); }
		if (InReset)
		{ for (auto Attribute : RPGAttributes) { _RetiredAttributes.Add(Attribute); } }
		_RetirementPending = true; _RetirementReset = InReset; _ReplacementStarted = false;
		for (auto Attribute : _RetiredAttributes)
		{
			if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(Attribute)))
			{ _AuthorityFailed = true; return false; }
			utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Attribute));
		}
		return true;
	}

	private void Request_TickRetirement()
	{
		if (!_RetirementPending || _AuthorityFailed) { return; }
		for (auto Attribute : _RetiredAttributes)
		{ if (ck::IsValid(Attribute)) { return; } }
		if (!_ReplacementStarted)
		{
			if (_RetirementReset)
			{
				AllAttributes.Empty(); RPGAttributes.Empty(); CombatAttributes.Empty();
				AutoStep = 0; ValueChangeCount = 0; TotalAttributes = 0;
				Request_CreateInitialBatch(ck::ToEntity(this));
				if (RPGAttributes.Num() != 3 || AllAttributes.Num() != 3)
				{ _AuthorityFailed = true; return; }
				Request_BindSignals(ck::ToEntity(this));
				AutoRunning = true;
			}
			else
			{
				for (auto Attribute : CombatAttributes) { AllAttributes.Remove(Attribute); }
				CombatAttributes.Empty(); TotalAttributes = AllAttributes.Num();
			}
			_ReplacementStarted = true; _PendingPasses = 0;
		}
		if (!Get_HasReplacementComposition()) { return; }
		_RetiredAttributes.Empty(); _RetirementPending = false;
		_RetirementReset = false; _ReplacementStarted = false;
	}

	private bool Get_HasReplacementComposition()
	{
		if (!_RetirementPending || !_ReplacementStarted) { return false; }
		_RetirementPending = false;
		auto Valid = Get_HasValidAuthorityComposition();
		_RetirementPending = true;
		return Valid;
	}

	bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
			(_PendingAutoAction != -1 && _PendingAutoAction != InAction) ||
			(InAction != 0 && InAction != 1 &&
				!(InAction == 2 && _PendingAutoAction == 2)) || InValue != 0.0f) { return false; }
		AutoRunning = false; _ControlPending = true; _PendingPasses = 0;
		_ExpectFreshCombat = InAction == 0 && CombatAttributes.Num() == 0;
		if (InAction == 2)
		{ if (!Request_BeginAuthorityBatchUpdate()) { _AuthorityFailed = true; return false; } return true; }
		if (InAction == 0)
		{ if (!Request_AddCombatBatch()) { _AuthorityFailed = true; return false; } return true; }
		if (!Request_BeginRetirement(false)) { _AuthorityFailed = true; return false; }
		return true;
	}
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_RetirementPending && _PendingPasses >= 2 &&
		Get_IsAuthorityTransactionPrepared() && Get_HasValidAuthorityComposition() &&
		(!_ExpectFreshCombat || Get_HasInitialCombatValues()) &&
		(_PendingAutoAction != 2 || Get_HasSettledBatchUpdate()); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
		_ResetPending = true; _PendingPasses = 0;
		if (!Request_BeginRetirement(true)) { _AuthorityFailed = true; return false; }
		return true;
	}
	bool Get_HasSettledAuthorityReset()
	{ return _ResetPending && !_RetirementPending && _PendingPasses >= 2 &&
		Get_IsAuthorityTransactionPrepared() && CombatAttributes.Num() == 0 &&
		Get_HasValidAuthorityComposition() && Get_HasInitialRPGValues(); }

	bool Request_BeginFinishAuthorityTransaction(bool InResumeAuto)
	{
		if (_FinishPending || !Get_HasValidAuthorityComposition() ||
			(_ControlPending && !Get_HasSettledAuthorityControl()) ||
			(_ResetPending && !Get_HasSettledAuthorityReset()) ||
			(!_ControlPending && !_ResetPending && !Get_IsAuthorityTransactionPrepared()))
		{ return false; }
		_FinishPending = true; _FinishCompleted = false; _FinishReleased = false;
		_FinishResume = InResumeAuto;
		return true;
	}
	private void Request_TickFinish()
	{
		if (!_FinishPending || !Get_HasValidAuthorityComposition() || _RetirementPending ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return; }
		_ControlPending = false; _ResetPending = false; _Prepared = false;
		_PrepareRequested = false; _PrepareSucceeded = false; _GateClosed = true;
		AutoRunning = _FinishResume; _FinishPending = false; _FinishCompleted = true;
	}
	bool Get_IsAuthorityFinishPending() const { return _FinishPending; }
	bool Get_HasFinishedAuthorityTransaction() const { return _FinishCompleted; }
	bool Get_CanReleaseAuthorityTransaction()
	{ return WaitForWorldReady && _FinishCompleted && !_FinishReleased && !_AuthorityFailed &&
		Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	private bool Request_ReleaseMutatorTimers()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		if (_FinishResume)
		{
			if (ck::Is_NOT_Valid(utils_timer::Request_Resume(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAutoResumeCompleted")))) { return false; }
		}
		return true;
	}
	bool Request_ReleaseAuthorityTransaction()
	{
		if (!Get_CanReleaseAuthorityTransaction()) { return false; }
		if (!Request_ReleaseMutatorTimers()) { _AuthorityFailed = true; return false; }
		_FinishReleased = true; _GateClosed = !_FinishResume; return true;
	}
	private bool Request_QueueLegacy(int32 InAction)
	{ if (WaitForWorldReady || InAction < 0 || InAction > 4) { return false; }
		_LegacyActions.Add(InAction); return true; }
	private void Request_TickLegacy()
	{
		if (WaitForWorldReady || _AuthorityFailed || _LegacyActions.Num() == 0) { return; }
		if (_LegacyStarted)
		{
			if (_FinishPending) { return; }
			if (_ControlPending && Get_HasSettledAuthorityControl())
			{ if (!Request_BeginFinishAuthorityTransaction(false)) { _AuthorityFailed = true; } return; }
			if (_ResetPending && Get_HasSettledAuthorityReset())
			{ if (!Request_BeginFinishAuthorityTransaction(_LegacyResumeAuto)) { _AuthorityFailed = true; } return; }
			if (_ControlPending || _ResetPending || !_FinishCompleted) { return; }
			if (!Request_ReleaseMutatorTimers()) { _AuthorityFailed = true; return; }
			_FinishCompleted = false; _GateClosed = !AutoRunning;
			_LegacyActions.RemoveAt(0); _LegacyStarted = false; return;
		}
		if (!Request_TryPrepareAuthorityTransaction()) { return; }
		auto Action = _LegacyActions[0];
		_LegacyResumeAuto = AutoRunning;
		if (Action == 2) { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; return; } }
		else if (Action <= 1)
		{ if (!Request_ApplyAuthorityControl(Action, 0.0f)) { _AuthorityFailed = true; return; } }
		else if (!Request_BeginFinishAuthorityTransaction(Action == 4))
		{ _AuthorityFailed = true; return; }
		_LegacyStarted = true;
	}
}
