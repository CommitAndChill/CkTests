//============================================================================
// FLOAT ATTRIBUTE SIGNAL BINDING STATION
//============================================================================

class UCk_EntityScript_AttributeGym_FloatSignals : UCk_GenericEntityScript_UE
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
	private bool _PendingOverride = false;
	private bool _OverrideSucceeded = false;
	private bool _ClampBindingsReady = false;
	private int32 _PendingPasses = 0;
	private float32 _ExpectedCurrent = 100.0f;
	private TArray<int32> _LegacyActions;
	private TArray<float32> _LegacyValues;
	private bool _LegacyStarted = false;
	private bool _LegacyResumeAuto = false;

	FCk_Handle_FloatAttribute TestAttribute;

	FCk_Handle_Timer AutoTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;
	int32 Delegate1Count = 0;
	int32 Delegate2Count = 0;
	int32 Delegate3Count = 0;
	int32 MinClampCount = 0;
	int32 MaxClampCount = 0;
	float32 LastPreviousFinal = 0.0f;
	float32 LastNewFinal = 0.0f;
	float32 LastPreClampValue = 0.0f;
	float32 LastClampedValue = 0.0f;
	float32 LastOverflow = 0.0f;
	bool Delegate1Bound = false;
	bool Delegate2Bound = false;
	bool Delegate3Bound = false;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatSignals");

		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(2.2f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			_GateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(2.2f)); }

		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests signal binding/unbinding with multiple delegates.\nShows OnValueChanged payload and OnMinClamped/OnMaxClamped events.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [5] Signals station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Bind first delegate", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Bind two more delegates", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Trigger multiple value changes", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Unbind delegate 2", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test min/max clamp signals", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Unbind all delegates", 5, 5));
		AutoConfig.ManualCommands.Add("panel [P] Signal value preset - 150 / 0 / 300 / -50");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(TestAttribute)) { _AuthorityFailed = true; return; }
		Request_BindClampSignals(InHandle);

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_SetValue,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetSignalValue"));
	}

	void Request_SetupAttributes(FCk_Handle InHandle)
	{
		auto TestParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Signal"), 100.0f);
		TestParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(255.0f);
		TestAttribute = utils_float_attribute::Add(InHandle, TestParams);
	}

	void Request_BindClampSignals(FCk_Handle InHandle)
	{
		auto A = utils_float_attribute::BindTo_OnMinClamped(TestAttribute,
			FCk_Delegate_FloatAttribute_OnClamped(this, n"OnMinClamped"));
		auto B = utils_float_attribute::BindTo_OnMaxClamped(TestAttribute,
			FCk_Delegate_FloatAttribute_OnClamped(this, n"OnMaxClamped"));
		_ClampBindingsReady = A == TestAttribute && B == TestAttribute;
		if (!_ClampBindingsReady) { _AuthorityFailed = true; }
	}

	UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (_PendingOverride)
		{
			_PendingPasses++;
			if (_OverrideSucceeded && Get_HasExpectedValue()) { _PendingOverride = false; }
		}
		Request_TickFinish(); Request_TickLegacy();
		if (!WaitForWorldReady && Get_HasValidAuthorityComposition()) { Request_UpdateDisplay(); }
	}

	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || _GateClosed || _PendingOverride ||
			!Get_HasValidAuthorityComposition()) { return; }
		auto Step = AutoStep % AutoConfig.TotalSteps;

		if (Step == 0)
		{
			if (!Request_SetValueBinding(1, true)) { _AuthorityFailed = true; return; }
			utils_float_attribute::Request_Override(TestAttribute, 120.5f, ECk_MinMaxCurrent::Current);
		}
		else if (Step == 1)
		{
			if (!Request_SetValueBinding(2, true) || !Request_SetValueBinding(3, true))
			{ _AuthorityFailed = true; return; }
			utils_float_attribute::Request_Override(TestAttribute, 80.25f, ECk_MinMaxCurrent::Current);
		}
		else if (Step == 2)
		{
			utils_float_attribute::Request_Override(TestAttribute, 150.75f, ECk_MinMaxCurrent::Current);
			utils_float_attribute::Request_Override(TestAttribute, 60.1f, ECk_MinMaxCurrent::Current);
			utils_float_attribute::Request_Override(TestAttribute, 200.5f, ECk_MinMaxCurrent::Current);
		}
		else if (Step == 3)
		{
			if (!Request_SetValueBinding(2, false)) { _AuthorityFailed = true; return; }
			utils_float_attribute::Request_Override(TestAttribute, 90.0f, ECk_MinMaxCurrent::Current);
		}
		else if (Step == 4)
		{
			utils_float_attribute::Request_Override(TestAttribute, -50.0f, ECk_MinMaxCurrent::Current);
			utils_float_attribute::Request_Override(TestAttribute, 300.0f, ECk_MinMaxCurrent::Current);
			utils_float_attribute::Request_Override(TestAttribute, 110.0f, ECk_MinMaxCurrent::Current);
		}
		else if (Step == 5)
		{
			if (!Request_UnbindAllValueDelegates()) { _AuthorityFailed = true; return; }
			utils_float_attribute::Request_Override(TestAttribute, 130.0f, ECk_MinMaxCurrent::Current);
		}

		AutoStep++;
	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacy(Typed.Enabled ? 4 : 3, 0.0f);
	}

	void
	Request_UpdateDisplay()
	{
		FString TitleText;
		FString DisplayText;
		if (Request_TryBuildPresentation(TitleText, DisplayText))
		{ CkGym_Common::Update_StationDisplay(ck::ToEntity(this), TitleText, DisplayText, ""); }
	}

	bool Get_HasSettledAuthoritySample()
	{ return !_PendingOverride && Get_HasValidAuthorityComposition(); }
	bool Request_TryBuildPresentation(FString& OutTitle, FString& OutBody)
	{
		OutTitle = ""; OutBody = "";
		if (!Get_HasSettledAuthoritySample()) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "FLOAT SIGNALS (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		auto CurrentValue = utils_float_attribute::Get_FinalValue(TestAttribute);
		DisplayText = f"{DisplayText}Current Value: {CurrentValue}\n\n";

		DisplayText = f"{DisplayText}DELEGATE STATUS:\n";
		DisplayText = f"{DisplayText}  Delegate1: " + (Delegate1Bound ? "BOUND" : "unbound") + f" (Fired: {Delegate1Count})\n";
		DisplayText = f"{DisplayText}  Delegate2: " + (Delegate2Bound ? "BOUND" : "unbound") + f" (Fired: {Delegate2Count})\n";
		DisplayText = f"{DisplayText}  Delegate3: " + (Delegate3Bound ? "BOUND" : "unbound") + f" (Fired: {Delegate3Count})\n\n";

		auto TotalBound = 0;
		if (Delegate1Bound) TotalBound++;
		if (Delegate2Bound) TotalBound++;
		if (Delegate3Bound) TotalBound++;

		DisplayText = f"{DisplayText}Active Bindings: {TotalBound}/3\n";
		DisplayText = f"{DisplayText}Total Value Signals: " + f"{Delegate1Count + Delegate2Count + Delegate3Count}\n";
		DisplayText = f"{DisplayText}Min Clamps: {MinClampCount} | Max Clamps: {MaxClampCount}\n\n";

		DisplayText = f"{DisplayText}LAST PAYLOAD:\n";
		DisplayText = f"{DisplayText}  Previous Final: {LastPreviousFinal}\n";
		DisplayText = f"{DisplayText}  New Final: {LastNewFinal}\n";
		DisplayText = f"{DisplayText}  Delta: {LastNewFinal - LastPreviousFinal}\n";
		if (LastClampedValue != 0.0f || LastPreClampValue != 0.0f)
		{
			DisplayText = f"{DisplayText}  Last Clamp: pre={LastPreClampValue} clamped={LastClampedValue} overflow={LastOverflow}\n";
		}

		// Live polling via utility accessors - updates every frame.
		auto LivePre = utils_float_attribute::Get_PreClampFinalValue(TestAttribute);
		auto LiveOvr = utils_float_attribute::Get_ClampOverflow(TestAttribute);
		DisplayText = f"{DisplayText}  Live Poll:  pre={LivePre}  overflow={LiveOvr}\n";

		DisplayText = f"{DisplayText}\n";

		auto ValueBar = CkGym_Attribute::Create_ProgressBar(CurrentValue, 255.0f, 20);
		DisplayText = f"{DisplayText}[{ValueBar}]\n\n";

		DisplayText = DisplayText + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);
		OutTitle = TitleText; OutBody = DisplayText; return true;
	}

	UFUNCTION()
	private void OnSetSignalValue(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_FloatGym_SetValue);
		Request_QueueLegacy(0, Typed.Value);
	}

	UFUNCTION() void OnValueChanged1(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload) { if (InAttributeOwnerEntity != ck::ToEntity(this) || !Delegate1Bound || !Get_HasValidAuthorityComposition()) { return; } Delegate1Count++; LastPreviousFinal = InPayload.Get_FinalValue_Previous(); LastNewFinal = InPayload.Get_FinalValue(); }
	UFUNCTION() void OnValueChanged2(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload) { if (InAttributeOwnerEntity != ck::ToEntity(this) || !Delegate2Bound || !Get_HasValidAuthorityComposition()) { return; } Delegate2Count++; LastPreviousFinal = InPayload.Get_FinalValue_Previous(); LastNewFinal = InPayload.Get_FinalValue(); }
	UFUNCTION() void OnValueChanged3(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload) { if (InAttributeOwnerEntity != ck::ToEntity(this) || !Delegate3Bound || !Get_HasValidAuthorityComposition()) { return; } Delegate3Count++; LastPreviousFinal = InPayload.Get_FinalValue_Previous(); LastNewFinal = InPayload.Get_FinalValue(); }
	UFUNCTION() void OnMinClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{
		if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_HasValidAuthorityComposition()) { return; }
		MinClampCount++;
		LastPreClampValue = InPayload.Get_PreClampFinalValue();
		LastClampedValue = InPayload.Get_FinalClampedValue();
		LastOverflow = InPayload.Get_ClampOverflow();
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(-30.0f, 0.0f, 150.0f), FLinearColor(0.0f, 0.5f, 1.0f, 1.0f)); }
	}

	UFUNCTION() void OnMaxClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{
		if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_HasValidAuthorityComposition()) { return; }
		MaxClampCount++;
		LastPreClampValue = InPayload.Get_PreClampFinalValue();
		LastClampedValue = InPayload.Get_FinalClampedValue();
		LastOverflow = InPayload.Get_ClampOverflow();
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(30.0f, 0.0f, 150.0f), FLinearColor(1.0f, 0.5f, 0.0f, 1.0f)); }
	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(1, 0.0f); }
	}
	private bool Request_SetValueBinding(int32 InIndex, bool InBound)
	{
		if (!Get_HasValidAuthorityComposition() || InIndex < 1 || InIndex > 3) { return false; }
		bool Current = InIndex == 1 ? Delegate1Bound :
			(InIndex == 2 ? Delegate2Bound : Delegate3Bound);
		if (Current == InBound) { return true; }
		FCk_Handle_FloatAttribute Result;
		if (InIndex == 1)
		{
			if (InBound) { Result = utils_float_attribute::BindTo_OnValueChanged(TestAttribute,
				ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged1")); }
			else { Result = utils_float_attribute::UnbindFrom_OnValueChanged(TestAttribute,
				ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged1")); }
		}
		else if (InIndex == 2)
		{
			if (InBound) { Result = utils_float_attribute::BindTo_OnValueChanged(TestAttribute,
				ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged2")); }
			else { Result = utils_float_attribute::UnbindFrom_OnValueChanged(TestAttribute,
				ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged2")); }
		}
		else
		{
			if (InBound) { Result = utils_float_attribute::BindTo_OnValueChanged(TestAttribute,
				ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged3")); }
			else { Result = utils_float_attribute::UnbindFrom_OnValueChanged(TestAttribute,
				ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged3")); }
		}
		if (Result != TestAttribute) { return false; }
		if (InIndex == 1) { Delegate1Bound = InBound; }
		else if (InIndex == 2) { Delegate2Bound = InBound; }
		else { Delegate3Bound = InBound; }
		return true;
	}
	private bool Request_UnbindAllValueDelegates()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		// Unbind even a delegate whose presentation flag is false. Reset must reconcile
		// the physical signal registrations with all three displayed binding bits.
		auto A = utils_float_attribute::UnbindFrom_OnValueChanged(TestAttribute,
			ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged1"));
		auto B = utils_float_attribute::UnbindFrom_OnValueChanged(TestAttribute,
			ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged2"));
		auto C = utils_float_attribute::UnbindFrom_OnValueChanged(TestAttribute,
			ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged3"));
		if (A != TestAttribute || B != TestAttribute || C != TestAttribute)
		{ _AuthorityFailed = true; return false; }
		Delegate1Bound = false; Delegate2Bound = false; Delegate3Bound = false;
		return true;
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
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_SetValue, Out)) { return false; }
		if (!WaitForWorldReady &&
			!Request_AppendExactMessenger(Self, FCk_Message_Gym_AutoSet, Out))
		{ return false; }
		return true;
	}
	bool Get_HasLiveAuthorityDependencies()
	{ return Get_HasValidAuthorityComposition(); }
	bool Get_HasValidAuthorityComposition()
	{
		auto Self = ck::ToEntity(this);
		auto Display = FCk_Handle(DisplayTimer);
		auto Auto = FCk_Handle(AutoTimer);
		auto Attribute = FCk_Handle(TestAttribute);
		if (_AuthorityFailed || !_ClampBindingsReady || !CkAttributeFloat::IsLiveEntity(Self) ||
			!CkAttributeFloat::IsLiveEntity(Display) || !CkAttributeFloat::IsLiveEntity(Auto) ||
			!CkAttributeFloat::IsLiveEntity(Attribute) ||
			utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
			utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
			utils_entity_lifetime::Get_LifetimeOwner(Attribute) != Self ||
			!utils_timer::DoCast(Display).IsSet() || !utils_timer::DoCast(Auto).IsSet() ||
			!utils_float_attribute::DoCast(Attribute).IsSet()) { return false; }
		TArray<FCk_Handle> Messengers;
		if (!Get_HasExactMessengers(Messengers)) { return false; }
		TArray<FCk_Handle> Expected;
		Expected.Add(Display); Expected.Add(Auto); Expected.Add(Attribute);
		for (auto Messenger : Messengers) { Expected.Add(Messenger); }
		int32 Live = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
		{
			if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
			if (!Expected.Contains(Child)) { return false; }
			Live++;
		}
		return Live == Expected.Num() && AutoConfig.TotalSteps == 6 && AutoStep >= 0 &&
			utils_float_attribute::TryGet(Self,
				utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Signal")) == TestAttribute &&
			utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Min) &&
			utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Max) &&
			utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Current);
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
	private bool Get_IsFinite(float32 V)
	{ return V == V && Math::Abs(V) < 1.0e20f; }
	private bool Get_HasExpectedValue()
	{
		if (!Get_HasValidAuthorityComposition() || _PendingPasses < 2) { return false; }
		auto Pre = utils_float_attribute::Get_PreClampFinalValue(TestAttribute);
		auto Final = utils_float_attribute::Get_FinalValue(TestAttribute);
		return Get_IsFinite(Pre) && Get_IsFinite(Final) &&
			Math::Abs(Pre - _ExpectedCurrent) <= 0.001f &&
			Math::Abs(Final - Math::Clamp(_ExpectedCurrent, 0.0f, 255.0f)) <= 0.001f;
	}
	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false; _GateClosed = true;
		if (_PendingOverride) { return false; }
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
	{ return _Prepared && !_PendingOverride && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	private bool Request_TrackOverride(float32 InValue)
	{
		if (!Get_IsFinite(InValue) || _PendingOverride) { return false; }
		_ExpectedCurrent = InValue; _PendingPasses = 0;
		_OverrideSucceeded = false; _PendingOverride = true;
		auto R = utils_float_attribute::Request_Override(TestAttribute, InValue,
			ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnOverrideCompleted"));
		if (ck::Is_NOT_Valid(R)) { _AuthorityFailed = true; return false; }
		return true;
	}
	UFUNCTION() private void OnOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_PendingOverride || InOwner != FCk_Handle(TestAttribute) || _OverrideSucceeded ||
			InResult != ECk_Request_OperationResult::Succeeded)
		{ _AuthorityFailed = true; return; }
		_OverrideSucceeded = true;
	}
	bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
	{
		if (InAction != 0 || !Get_IsAuthorityTransactionPrepared() || _ControlPending ||
			_ResetPending || !(InValue == 150.0f || InValue == 0.0f ||
				InValue == 300.0f || InValue == -50.0f)) { return false; }
		AutoRunning = false; _ControlPending = true; return Request_TrackOverride(InValue);
	}
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_PendingOverride && _OverrideSucceeded &&
		Get_IsAuthorityTransactionPrepared() && Get_HasExpectedValue(); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
			!Request_UnbindAllValueDelegates()) { return false; }
		AutoStep = 0; Delegate1Count = 0; Delegate2Count = 0; Delegate3Count = 0;
		MinClampCount = 0; MaxClampCount = 0;
		LastPreviousFinal = 0.0f; LastNewFinal = 0.0f;
		LastPreClampValue = 0.0f; LastClampedValue = 0.0f; LastOverflow = 0.0f;
		AutoRunning = true; _ResetPending = true; return Request_TrackOverride(100.0f);
	}
	bool Get_HasSettledAuthorityReset()
	{ return _ResetPending && !_PendingOverride && _OverrideSucceeded &&
		Get_IsAuthorityTransactionPrepared() && Get_HasExpectedValue() &&
		!Delegate1Bound && !Delegate2Bound && !Delegate3Bound; }
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
		if (!_FinishPending || !Get_HasValidAuthorityComposition() || _PendingOverride ||
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
	private bool Request_QueueLegacy(int32 InAction, float32 InValue)
	{
		if (WaitForWorldReady || !(InAction == 1 || InAction == 3 || InAction == 4 ||
			(InAction == 0 && (InValue == 150.0f || InValue == 0.0f ||
				InValue == 300.0f || InValue == -50.0f)))) { return false; }
		_LegacyActions.Add(InAction); _LegacyValues.Add(InValue); return true;
	}
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
			_LegacyActions.RemoveAt(0); _LegacyValues.RemoveAt(0); _LegacyStarted = false; return;
		}
		if (!Request_TryPrepareAuthorityTransaction()) { return; }
		auto Action = _LegacyActions[0];
		_LegacyResumeAuto = AutoRunning;
		if (Action == 1) { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; return; } }
		else if (Action == 0)
		{ if (!Request_ApplyAuthorityControl(0, _LegacyValues[0])) { _AuthorityFailed = true; return; } }
		else if (!Request_BeginFinishAuthorityTransaction(Action == 4))
		{ _AuthorityFailed = true; return; }
		_LegacyStarted = true;
	}

}
