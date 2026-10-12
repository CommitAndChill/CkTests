//============================================================================
// FLOAT ATTRIBUTE MIN/MAX/CURRENT COMPONENTS STATION
//============================================================================

class UCk_EntityScript_AttributeGym_FloatMinMaxCurrent : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FCk_Handle_Timer DisplayTimer;
	private bool _AuthorityStarted = false;
	private bool _AuthorityFailed = false;
	private bool _MutationGateClosed = false;
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
	private bool _PendingMutation = false;
	private int32 _PendingPasses = 0;
	private int32 _ExpectedMask = 0;
	private int32 _CompletedMask = 0;
	private float32 _ExpectedMin = 10.0f;
	private float32 _ExpectedMax = 200.0f;
	private float32 _ExpectedCurrent = 100.0f;
	private TArray<int32> _LegacyActions;
	private TArray<float32> _LegacyValues;
	private bool _LegacyStarted = false;
	private bool _LegacyResumeAuto = false;

	FCk_Handle_FloatAttribute PowerLevelAttribute;

	FCk_Handle_Timer AutoTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;
	int32 MinChangeCount = 0;
	int32 MaxChangeCount = 0;
	int32 CurrentChangeCount = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatMinMaxCurrent");

		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(3.0f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			_MutationGateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(3.0f)); }

		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests individual Min/Max/Current component manipulation.\nShows component presence detection and separate change counters.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [4] Min/Max/Current station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Min component (10 -> 25.5)", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Max component (200 -> 175.75)", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Current component (100 -> 142.3)", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test component detection", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test value retrieval", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Reset to defaults", 5, 5));
		AutoConfig.ManualCommands.Add("panel [U] Min preset - [I] Max preset - [O] Current preset");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(PowerLevelAttribute)) { _AuthorityFailed = true; return; }
		Request_BindSignals(InHandle);

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_SetValue,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetValue"));
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto PowerParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.PowerLevel"), 100.0f);
		PowerParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(10.0f).Set_MaxValue(200.0f);
		PowerLevelAttribute = utils_float_attribute::Add(InHandle, PowerParams);
	}

	void
	Request_BindSignals(
		FCk_Handle InHandle)
	{
		utils_float_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Min,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnMinChanged"));
		utils_float_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Max,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnMaxChanged"));
		utils_float_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnCurrentChanged"));
	}

	UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (_PendingMutation)
		{
			_PendingPasses++;
			if (_CompletedMask == _ExpectedMask && Get_HasExpectedValues()) { _PendingMutation = false; }
		}
		Request_TickFinishTransaction();
		Request_TickLegacyCommands();
		if (!WaitForWorldReady && Get_HasValidAuthorityComposition()) { Request_UpdateDisplay(); }
	}

	UFUNCTION()
	private void
	AutoTick(
		FCk_Handle_Timer InHandle,
		FCk_Chrono InChrono,
		FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || _MutationGateClosed || _PendingMutation ||
			!Get_HasValidAuthorityComposition()) { return; }
		auto Step = AutoStep % AutoConfig.TotalSteps;

		bool Good = true;
		if (Step == 0)
		{ Good = Request_TrackOverride(25.5f, ECk_MinMaxCurrent::Min, 1); }
		else if (Step == 1)
		{ Good = Request_TrackOverride(175.75f, ECk_MinMaxCurrent::Max, 2); }
		else if (Step == 2)
		{ Good = Request_TrackOverride(142.3f, ECk_MinMaxCurrent::Current, 4); }
		else if (Step == 3) { /* component detection shown in display */ }
		else if (Step == 4) { /* value retrieval shown in display */ }
		else if (Step == 5)
		{
			_ExpectedMin = 10.0f; _ExpectedMax = 200.0f; _ExpectedCurrent = 100.0f;
			_ExpectedMask = 7; _CompletedMask = 0; _PendingPasses = 0; _PendingMutation = true;
			auto A = utils_float_attribute::Request_Override(PowerLevelAttribute, 10.0f,
				ECk_MinMaxCurrent::Min,
				FCk_Delegate_Request_OnCompleted(this, n"OnResetMinCompleted"));
			auto B = utils_float_attribute::Request_Override(PowerLevelAttribute, 200.0f,
				ECk_MinMaxCurrent::Max,
				FCk_Delegate_Request_OnCompleted(this, n"OnResetMaxCompleted"));
			auto C = utils_float_attribute::Request_Override(PowerLevelAttribute, 100.0f,
				ECk_MinMaxCurrent::Current,
				FCk_Delegate_Request_OnCompleted(this, n"OnResetCurrentCompleted"));
			Good = ck::IsValid(A) && ck::IsValid(B) && ck::IsValid(C);
		}
		if (!Good) { _AuthorityFailed = true; _MutationGateClosed = true; return; }
		AutoStep++;
	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 5 : 4, 0.0f);
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
	{ return !_PendingMutation && Get_HasValidAuthorityComposition() && (_PendingPasses == 0 || Get_HasExpectedValues()); }
	bool Request_TryBuildPresentation(FString& OutTitle, FString& OutBody)
	{
		OutTitle = ""; OutBody = "";
		if (!Get_HasSettledAuthoritySample()) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "FLOAT MIN/MAX/CURRENT (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Min Changes: {MinChangeCount} | Max Changes: {MaxChangeCount}\n";
		DisplayText = f"{DisplayText}Current Changes: {CurrentChangeCount}\n\n";

		auto HasMin = utils_float_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
		auto HasMax = utils_float_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
		auto HasCurrent = utils_float_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}Components Present:\n";
		DisplayText = f"{DisplayText}  Min: " + (HasMin ? "YES" : "NO") + "\n";
		DisplayText = f"{DisplayText}  Max: " + (HasMax ? "YES" : "NO") + "\n";
		DisplayText = f"{DisplayText}  Current: " + (HasCurrent ? "YES" : "NO") + "\n\n";

		auto MinValue = utils_float_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
		auto MaxValue = utils_float_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
		auto CurrentBase = utils_float_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentBonus = utils_float_attribute::Get_BonusValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentFinal = utils_float_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}PowerLevel Values:\n";
		DisplayText = f"{DisplayText}  Min: {MinValue}\n";
		DisplayText = f"{DisplayText}  Max: {MaxValue}\n";
		DisplayText = f"{DisplayText}  Current Base: {CurrentBase}\n";
		DisplayText = f"{DisplayText}  Current Bonus: {CurrentBonus}\n";
		DisplayText = f"{DisplayText}  Current Final: {CurrentFinal}\n\n";

		auto Range = MaxValue - MinValue;
		auto BarValue = (Range > 0.0f) ? (CurrentFinal - MinValue) : 0.0f;
		auto PowerBar = CkGym_Attribute::Create_ProgressBar(BarValue, Range, 25);
		DisplayText = f"{DisplayText}[{PowerBar}]\n";
		DisplayText = f"{DisplayText}  {MinValue} <--- {CurrentFinal} ---> {MaxValue}\n\n";

		DisplayText = DisplayText + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);
		OutTitle = TitleText; OutBody = DisplayText; return true;
	}

	UFUNCTION()
	private void OnSetValue(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		auto Typed = InPayload.Get(FCk_Message_FloatGym_SetValue);
		if (WaitForWorldReady) { return; }
		if (Typed.Component != ECk_MinMaxCurrent::Min &&
			Typed.Component != ECk_MinMaxCurrent::Max &&
			Typed.Component != ECk_MinMaxCurrent::Current) { return; }
		int32 Action = Typed.Component == ECk_MinMaxCurrent::Min ? 0 :
			(Typed.Component == ECk_MinMaxCurrent::Max ? 1 : 2);
		Request_QueueLegacyCommand(Action, Typed.Value);
	}

	UFUNCTION() void OnMinChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { MinChangeCount++; } }
	UFUNCTION() void OnMaxChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { MaxChangeCount++; } }
	UFUNCTION() void OnCurrentChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { CurrentChangeCount++; } }

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacyCommand(3, 0.0f); }
	}
	// The shared world owns the station lease. The original script never reads a client snapshot.
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
		auto Power = FCk_Handle(PowerLevelAttribute);
		if (_AuthorityFailed || !CkAttributeFloat::IsLiveEntity(Self) ||
			!CkAttributeFloat::IsLiveEntity(Display) ||
			!CkAttributeFloat::IsLiveEntity(Auto) ||
			!CkAttributeFloat::IsLiveEntity(Power)) { return false; }
		if (utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
			utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
			utils_entity_lifetime::Get_LifetimeOwner(Power) != Self ||
			!utils_timer::DoCast(Display).IsSet() || !utils_timer::DoCast(Auto).IsSet() ||
			!utils_float_attribute::DoCast(Power).IsSet()) { return false; }
		TArray<FCk_Handle> Messengers;
		if (!Get_HasExactMessengers(Messengers)) { return false; }
		TArray<FCk_Handle> Expected;
		Expected.Add(Display); Expected.Add(Auto); Expected.Add(Power);
		for (auto Messenger : Messengers) { Expected.Add(Messenger); }
		int32 Live = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
		{
			if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
			if (!Expected.Contains(Child)) { return false; }
			Live++;
		}
		if (Live != Expected.Num()) { return false; }
		return utils_float_attribute::TryGet(Self,
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.PowerLevel")) == PowerLevelAttribute &&
			utils_float_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Min) &&
			utils_float_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Max) &&
			utils_float_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Current) &&
			AutoConfig.TotalSteps == 6 && AutoStep >= 0;
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
		_StartReleased = true; _MutationGateClosed = false; return true;
	}
	private bool Get_IsFinite(float32 InValue)
	{ return InValue == InValue && Math::Abs(InValue) < 1.0e20f; }
	private bool Get_Near(float32 A, float32 B)
	{ return Get_IsFinite(A) && Get_IsFinite(B) && Math::Abs(A - B) <= 0.001f; }
	private bool Get_HasExpectedValues()
	{
		if (!Get_HasValidAuthorityComposition() || _PendingPasses < 2) { return false; }
		if ((_ExpectedMask & 1) != 0 && !Get_Near(
			utils_float_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min), _ExpectedMin))
		{ return false; }
		if ((_ExpectedMask & 2) != 0 && !Get_Near(
			utils_float_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max), _ExpectedMax))
		{ return false; }
		if ((_ExpectedMask & 4) != 0 && !Get_Near(
			utils_float_attribute::Get_PreClampFinalValue(PowerLevelAttribute), _ExpectedCurrent))
		{ return false; }
		return Get_IsFinite(utils_float_attribute::Get_FinalValue(PowerLevelAttribute));
	}
	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false; _MutationGateClosed = true;
		if (_PendingMutation) { return false; }
		if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused)
		{ _Prepared = !_PrepareRequested || _PrepareSucceeded; return _Prepared; }
		if (_PrepareRequested || AutoTimer.Get_CurrentState() != ECk_Timer_State::Running)
		{ return false; }
		_PrepareRequested = true; _PrepareSucceeded = false;
		auto Requested = utils_timer::Request_Pause(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnPrepareCompleted"));
		if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; }
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
	{ return _Prepared && !_PendingMutation && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	private bool Get_IsValidControl(int32 InAction, float32 InValue)
	{
		if (!Get_IsFinite(InValue)) { return false; }
		if (InAction == 0) { return InValue == 10.0f || InValue == 25.5f ||
			InValue == 60.0f || InValue == 220.0f; }
		if (InAction == 1) { return InValue == 200.0f || InValue == 175.75f ||
			InValue == 90.0f || InValue == 5.0f; }
		if (InAction == 2) { return InValue == 100.0f || InValue == 142.3f ||
			InValue == 250.0f || InValue == -40.0f; }
		return false;
	}
	private bool Request_TrackOverride(float32 InValue, ECk_MinMaxCurrent InComponent, int32 InBit)
	{
		if (_PendingMutation || !Get_HasValidAuthorityComposition()) { return false; }
		_ExpectedMask = InBit; _CompletedMask = 0; _PendingPasses = 0; _PendingMutation = true;
		if (InBit == 1) { _ExpectedMin = InValue; }
		else if (InBit == 2) { _ExpectedMax = InValue; }
		else { _ExpectedCurrent = InValue; }
		auto Requested = utils_float_attribute::Request_Override(PowerLevelAttribute, InValue, InComponent,
			FCk_Delegate_Request_OnCompleted(this, n"OnControlCompleted"));
		if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; return false; }
		return true;
	}
	UFUNCTION() private void OnControlCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, _ExpectedMask); }
	UFUNCTION() private void OnResetMinCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, 1); }
	UFUNCTION() private void OnResetMaxCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, 2); }
	UFUNCTION() private void OnResetCurrentCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, 4); }
	private void Request_RecordOverride(FCk_Handle InOwner, ECk_Request_OperationResult InResult, int32 InBit)
	{
		if (!_PendingMutation || InOwner != FCk_Handle(PowerLevelAttribute) ||
			(_ExpectedMask & InBit) == 0 || (_CompletedMask & InBit) != 0 ||
			InResult != ECk_Request_OperationResult::Succeeded)
		{ _AuthorityFailed = true; return; }
		_CompletedMask |= InBit;
	}
	bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
			!Get_IsValidControl(InAction, InValue)) { return false; }
		AutoRunning = false; _ControlPending = true;
		return Request_TrackOverride(InValue,
			InAction == 0 ? ECk_MinMaxCurrent::Min :
				(InAction == 1 ? ECk_MinMaxCurrent::Max : ECk_MinMaxCurrent::Current),
			1 << InAction);
	}
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_PendingMutation && _CompletedMask == _ExpectedMask &&
		Get_IsAuthorityTransactionPrepared() && Get_HasExpectedValues(); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
		AutoStep = 0; MinChangeCount = 0; MaxChangeCount = 0; CurrentChangeCount = 0;
		AutoRunning = true; _ResetPending = true; _PendingMutation = true; _PendingPasses = 0;
		_ExpectedMin = 10.0f; _ExpectedMax = 200.0f; _ExpectedCurrent = 100.0f;
		_ExpectedMask = 7; _CompletedMask = 0;
		auto A = utils_float_attribute::Request_Override(PowerLevelAttribute, 10.0f, ECk_MinMaxCurrent::Min,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetMinCompleted"));
		auto B = utils_float_attribute::Request_Override(PowerLevelAttribute, 200.0f, ECk_MinMaxCurrent::Max,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetMaxCompleted"));
		auto C = utils_float_attribute::Request_Override(PowerLevelAttribute, 100.0f, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetCurrentCompleted"));
		if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B) || ck::Is_NOT_Valid(C))
		{ _AuthorityFailed = true; return false; }
		return true;
	}
	bool Get_HasSettledAuthorityReset()
	{ return _ResetPending && !_PendingMutation && _CompletedMask == 7 &&
		Get_IsAuthorityTransactionPrepared() && Get_HasExpectedValues(); }
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
	private void Request_TickFinishTransaction()
	{
		if (!_FinishPending || !Get_HasValidAuthorityComposition() || _PendingMutation ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return; }
		_ControlPending = false; _ResetPending = false; _Prepared = false;
		_PrepareRequested = false; _PrepareSucceeded = false;
		_MutationGateClosed = true; AutoRunning = _FinishResume;
		_FinishPending = false; _FinishCompleted = true;
	}
	bool Get_IsAuthorityFinishPending() const { return _FinishPending; }
	bool Get_HasFinishedAuthorityTransaction() const { return _FinishCompleted; }
	bool Get_CanReleaseAuthorityTransaction()
	{ return WaitForWorldReady && _FinishCompleted && !_FinishReleased && !_FinishPending &&
		!_AuthorityFailed && Get_HasValidAuthorityComposition() &&
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
		_FinishReleased = true; _MutationGateClosed = !_FinishResume; return true;
	}
	private bool Request_QueueLegacyCommand(int32 InAction, float32 InValue)
	{
		if (WaitForWorldReady ||
			!(InAction == 3 || InAction == 4 || InAction == 5 || Get_IsValidControl(InAction, InValue)))
		{ return false; }
		_LegacyActions.Add(InAction); _LegacyValues.Add(InValue); return true;
	}
	private void Request_TickLegacyCommands()
	{
		if (WaitForWorldReady || _AuthorityFailed || _LegacyActions.Num() == 0) { return; }
		if (_LegacyStarted)
		{
			if (_FinishPending) { return; }
			if (_ControlPending && Get_HasSettledAuthorityControl())
			{ if (!Request_BeginFinishAuthorityTransaction(false)) { _AuthorityFailed = true; } return; }
			if (_ResetPending && Get_HasSettledAuthorityReset())
			{ if (!Request_BeginFinishAuthorityTransaction(_LegacyResumeAuto)) { _AuthorityFailed = true; } return; }
			if (_FinishPending || _ControlPending || _ResetPending || !_FinishCompleted) { return; }
			Request_ReleaseLegacyTransaction();
			_LegacyActions.RemoveAt(0); _LegacyValues.RemoveAt(0); _LegacyStarted = false; return;
		}
		if (!Request_TryPrepareAuthorityTransaction()) { return; }
		auto Action = _LegacyActions[0];
		_LegacyResumeAuto = AutoRunning; auto Value = _LegacyValues[0];
		if (Action == 3) { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; return; } }
		else if (Action <= 2)
		{ if (!Request_ApplyAuthorityControl(Action, Value)) { _AuthorityFailed = true; return; } }
		else
		{
			if (!Request_BeginFinishAuthorityTransaction(Action == 5)) { _AuthorityFailed = true; return; }
			_LegacyStarted = true; return;
		}
		_LegacyStarted = true;
		// Completion and processor settlement are polled on later display ticks.
	}
	private void Request_ReleaseLegacyTransaction()
	{ if (!Request_ReleaseMutatorTimers()) { _AuthorityFailed = true; return; }
		_FinishCompleted = false; _MutationGateClosed = !AutoRunning; }

}
