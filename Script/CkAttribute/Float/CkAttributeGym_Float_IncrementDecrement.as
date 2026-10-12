//============================================================================
// FLOAT ATTRIBUTE INCREMENT/DECREMENT STATION
//============================================================================

class UCk_EntityScript_AttributeGym_FloatIncrementDecrement : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FCk_Handle_Timer DisplayTimer;
	private bool _AuthorityStarted = false, _AuthorityFailed = false;
	private bool _MutationGateClosed = false, _StartSucceeded = false, _StartReleased = false;
	private bool _PrepareRequested = false, _PrepareSucceeded = false, _Prepared = false;
	private bool _ControlPending = false, _ResetPending = false;
	private bool _FinishPending = false, _FinishCompleted = false, _FinishReleased = false;
	private bool _FinishResume = false, _ReleaseResumeRequested = false;
	private bool _ReleaseResumeCompleted = false;
	private bool _PendingMutation = false;
	private int32 _PendingPasses = 2;
	private float32 _ExpectedBase = 25.0f, _ExpectedBonus = 0.0f;
	private TArray<float32> _ExpectedDeltas;
	private TArray<float32> _ExpectedContributions;
	private TArray<FCk_Handle_FloatAttributeModifier> _RetiringModifiers;
	private TArray<FCk_Handle_FloatAttributeModifier> _PermanentModifiers;
	private FCk_Handle_FloatAttributeModifier _IncrementPermanent, _DecrementPermanent, _ResetPermanent;
	private bool _IncrementPermanentAdded = false, _DecrementPermanentAdded = false;
	private bool _ResetPermanentAdded = false;
	private TArray<int32> _LegacyActions;
	private bool _LegacyStarted = false, _LegacyResumeAuto = false;

	private void Request_RecordAuthorityFailure(int32 InSite)
	{
		if (!_AuthorityFailed)
		{
			System::PrintString(
				f"[AttributeFloatIncDec] site={InSite} step={AutoStep} gate={_MutationGateClosed} pending={_PendingMutation} passes={_PendingPasses} start={_StartSucceeded} prepared={_Prepared} control={_ControlPending} reset={_ResetPending} finish={_FinishPending} released={_FinishReleased} resume={_ReleaseResumeCompleted}",
				false, true, FLinearColor::White, 0.0f, NAME_None);
		}
		_AuthorityFailed = true;
	}

	FCk_Handle_FloatAttribute CounterAttribute;
	TArray<FCk_Handle_FloatAttributeModifier> RevocableModifiers;

	FCk_Handle_Timer AutoTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;
	int32 ValueChangeCount = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatIncrementDecrement");

		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(2.0f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			_MutationGateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(2.0f)); }

		AutoConfig.TotalSteps = 12;
		AutoConfig.Description = "Tests the float mixin increment/decrement helpers.\nRevocable vs non-revocable +1/-1 operations.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [7] Inc/Dec station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("IncrementNotRevocable (x3)", 0, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("IncrementRevocable (x2)", 3, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("DecrementNotRevocable (x2)", 5, 6));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Revoke all revocable", 7, 7));
		AutoConfig.Steps.Add(FCkGym_AutoStep("DecrementRevocable (x2)", 8, 9));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Revoke all revocable", 10, 10));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Reset to default", 11, 11));
		AutoConfig.ManualCommands.Add("panel [N] Inc/Dec ring - increment / decrement / revoke all");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(CounterAttribute)) { Request_RecordAuthorityFailure(86); return; }
		Request_BindSignals(InHandle);

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_Increment,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualIncrement"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_Decrement,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualDecrement"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_RevokeAll,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualRevokeAll"));
	}

	void Request_SetupAttributes(FCk_Handle InHandle)
	{
		auto CounterParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Counter"), 25.0f);
		CounterParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(50.0f);
		CounterAttribute = utils_float_attribute::Add(InHandle, CounterParams);
	}

	void Request_BindSignals(FCk_Handle InHandle)
	{
		utils_float_attribute::BindTo_OnValueChanged(CounterAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
		utils_float_attribute::BindTo_OnMinClamped(CounterAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnClamped"));
		utils_float_attribute::BindTo_OnMaxClamped(CounterAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnClamped"));
	}

	UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (_PendingMutation)
		{
			_PendingPasses++;
			if (Get_HasSettledAuthorityProjection()) { _PendingMutation = false; }
		}
		Request_TickFinishAuthorityTransaction();
		Request_TickLegacyCommands();
		if (!WaitForWorldReady && Get_HasValidAuthorityComposition() &&
			Get_HasValidRevocableRecords()) { Request_UpdateDisplay(); }
	}

	UFUNCTION() private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || _MutationGateClosed || _PendingMutation) { return; }
		if (!Get_HasValidAuthorityComposition() || !Get_HasValidRevocableRecords())
		{ if (WaitForWorldReady) { Request_RecordAuthorityFailure(133); } return; }
		auto Step = AutoStep % AutoConfig.TotalSteps;
		if (Step >= 0 && Step <= 2)
		{ if (!Request_AddPermanent(true)) { Request_RecordAuthorityFailure(136); return; }
		  _ExpectedBase = Math::Clamp(_ExpectedBase + 1.0f, 0.0f, 50.0f); }
		else if (Step >= 3 && Step <= 4)
		{ if (!Request_AddRevocable(true)) { Request_RecordAuthorityFailure(139); return; } }
		else if (Step >= 5 && Step <= 6)
		{ if (!Request_AddPermanent(false)) { Request_RecordAuthorityFailure(141); return; }
		  _ExpectedBase = Math::Clamp(_ExpectedBase - 1.0f, 0.0f, 50.0f); }
		else if (Step == 7 || Step == 10)
		{ if (!Request_RevokeAllRevocable()) { Request_RecordAuthorityFailure(144); return; } }
		else if (Step >= 8 && Step <= 9)
		{ if (!Request_AddRevocable(false)) { Request_RecordAuthorityFailure(146); return; } }
		else if (Step == 11)
		{ if (!Request_ResetToDefault()) { Request_RecordAuthorityFailure(148); return; } }
		AutoStep++;
		_PendingPasses = 0; _PendingMutation = true;
	}

	UFUNCTION() private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 4 : 3);
	}

	private bool Request_AddRevocable(bool InIncrement)
	{
		if (!Get_HasValidAuthorityComposition() || !Get_HasValidRevocableRecords()) { return false; }
		auto Mod = InIncrement ? CounterAttribute.IncrementRevocable() :
			CounterAttribute.DecrementRevocable();
		auto Handle = FCk_Handle(Mod);
		if (!CkAttributeFloat::IsLiveEntity(Handle) ||
			utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(CounterAttribute) ||
			!utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current))
		{ return false; }
		RevocableModifiers.Add(Mod);
		auto Contribution = InIncrement ? 1.0f : -1.0f;
		_ExpectedDeltas.Add(1.0f);
		_ExpectedContributions.Add(Contribution); _ExpectedBonus += Contribution;
		return true;
	}
	private bool Request_AddPermanent(bool InIncrement)
	{
		if (!Get_HasValidRevocableRecords()) { return false; }
		auto Before = utils_float_attribute_modifier::ForEach(CounterAttribute,
			FInstancedStruct(), FCk_Lambda_InHandle());
		if (InIncrement) { CounterAttribute.IncrementNotRevocable(); }
		else { CounterAttribute.DecrementNotRevocable(); }
		auto After = utils_float_attribute_modifier::ForEach(CounterAttribute,
			FInstancedStruct(), FCk_Lambda_InHandle());
		if (After.Num() < Before.Num() || After.Num() > Before.Num() + 1) { return false; }
		for (auto PriorMember : Before)
		{ if (!After.Contains(PriorMember)) { return false; } }
		auto Prior = InIncrement ? _IncrementPermanent : _DecrementPermanent;
		bool WasAdded = InIncrement ? _IncrementPermanentAdded : _DecrementPermanentAdded;
		if (WasAdded) { return After.Num() == Before.Num() &&
			CkAttributeFloat::IsLiveEntity(FCk_Handle(Prior)) && After.Contains(Prior); }
		if (After.Num() != Before.Num() + 1) { return false; }
		for (auto Candidate : After)
		{
			if (Before.Contains(Candidate)) { continue; }
			auto Handle = FCk_Handle(Candidate);
			if (!CkAttributeFloat::IsLiveEntity(Handle) ||
				utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(CounterAttribute) ||
				!utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current))
			{ return false; }
			_PermanentModifiers.Add(Candidate);
			if (InIncrement) { _IncrementPermanent = Candidate; _IncrementPermanentAdded = true; }
			else { _DecrementPermanent = Candidate; _DecrementPermanentAdded = true; }
			return true;
		}
		return false;
	}
	bool Request_RevokeAllRevocable()
	{
		if (!Get_HasValidAuthorityComposition() || !Get_HasValidRevocableRecords()) { return false; }
		for (auto Mod : RevocableModifiers)
		{ _RetiringModifiers.Add(Mod); utils_float_attribute_modifier::Remove(Mod); }
		RevocableModifiers.Empty(); _ExpectedDeltas.Empty();
		_ExpectedContributions.Empty(); _ExpectedBonus = 0.0f;
		return true;
	}
	bool Request_ResetToDefault()
	{
		if (!Get_HasValidAuthorityComposition() || !Get_HasValidRevocableRecords()) { return false; }
		for (auto Mod : RevocableModifiers) { _RetiringModifiers.Add(Mod); }
		utils_float_attribute_modifier::Request_ClearAllModifiers(CounterAttribute, ECk_MinMaxCurrent::Current);
		RevocableModifiers.Empty(); _ExpectedDeltas.Empty();
		_ExpectedContributions.Empty(); _ExpectedBonus = 0.0f;
		auto Before = utils_float_attribute_modifier::ForEach(CounterAttribute,
			FInstancedStruct(), FCk_Lambda_InHandle());
		auto Requested = utils_float_attribute::Request_Override(CounterAttribute, 25.0f, ECk_MinMaxCurrent::Current);
		if (ck::Is_NOT_Valid(Requested)) { return false; }
		auto After = utils_float_attribute_modifier::ForEach(CounterAttribute,
			FInstancedStruct(), FCk_Lambda_InHandle());
		for (auto PriorMember : Before)
		{ if (!After.Contains(PriorMember)) { return false; } }
		if (_ResetPermanentAdded)
		{
			if (After.Num() != Before.Num() || !After.Contains(_ResetPermanent) ||
				!CkAttributeFloat::IsLiveEntity(FCk_Handle(_ResetPermanent)))
			{ return false; }
		}
		else
		{
			if (After.Num() != Before.Num() + 1) { return false; }
			bool Recorded = false;
			for (auto Candidate : After)
			{
				if (Before.Contains(Candidate)) { continue; }
				auto Handle = FCk_Handle(Candidate);
				if (Recorded || !CkAttributeFloat::IsLiveEntity(Handle) ||
					utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(CounterAttribute) ||
					!utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current))
				{ return false; }
				_ResetPermanent = Candidate; Recorded = true;
			}
			if (!Recorded) { return false; }
			_PermanentModifiers.Add(_ResetPermanent); _ResetPermanentAdded = true;
		}
		_ExpectedBase = 25.0f; return true;
	}

	bool Get_HasSettledAuthoritySample()
	{ return !_PendingMutation && Get_HasSettledAuthorityProjection(); }
	bool Request_TryBuildPresentation(FString& OutTitle, FString& OutBody)
	{
		OutTitle = ""; OutBody = "";
		if (!Get_HasSettledAuthoritySample()) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "FLOAT INC/DEC (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n\n";

		auto BaseValue = utils_float_attribute::Get_BaseValue(CounterAttribute);
		auto BonusValue = utils_float_attribute::Get_BonusValue(CounterAttribute);
		auto FinalValue = utils_float_attribute::Get_FinalValue(CounterAttribute);

		DisplayText = f"{DisplayText}Counter: {BaseValue} + {BonusValue} = {FinalValue}\n";
		DisplayText = f"{DisplayText}Range: 0 to 50\n\n";

		auto CounterBar = CkGym_Attribute::Create_ProgressBar(FinalValue, 50.0f, 25);
		DisplayText = f"{DisplayText}[{CounterBar}]\n\n";

		DisplayText = f"{DisplayText}Revocable Modifiers: {RevocableModifiers.Num()}\n";
		for (auto i = 0; i < RevocableModifiers.Num(); i++)
		{
			if (ck::IsValid(RevocableModifiers[i]))
			{
				auto Delta = utils_float_attribute_modifier::Get_Delta(RevocableModifiers[i]);
				auto Sign = (Delta >= 0.0f) ? "+" : "";
				DisplayText = f"{DisplayText}  [{i}] {Sign}{Delta}\n";
			}
		}

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);
		OutTitle = TitleText; OutBody = DisplayText;
		return true;
	}
	void Request_UpdateDisplay()
	{
		FString Title, Body;
		if (!Request_TryBuildPresentation(Title, Body)) { return; }
		if (!WaitForWorldReady)
		{ CkGym_Common::Update_StationDisplay(ck::ToEntity(this), Title, Body, ""); }
	}

	UFUNCTION() private void OnManualIncrement(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{ if (!WaitForWorldReady) { Request_QueueLegacyCommand(0); } }
	UFUNCTION() private void OnManualDecrement(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{ if (!WaitForWorldReady) { Request_QueueLegacyCommand(1); } }
	UFUNCTION() private void OnManualRevokeAll(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{ if (!WaitForWorldReady) { Request_QueueLegacyCommand(2); } }
	UFUNCTION() void OnValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }
	UFUNCTION() void OnClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{ if (!WaitForWorldReady && InAttributeOwnerEntity == ck::ToEntity(this) &&
		Get_HasValidAuthorityComposition())
	  { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this),
		FVector(0.0f, 0.0f, 150.0f), FLinearColor(1.0f, 0.5f, 0.0f, 1.0f)); } }

	UFUNCTION() private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{ if (!WaitForWorldReady) { Request_QueueLegacyCommand(5); } }

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
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_Increment, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_Decrement, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_RevokeAll, Out)) { return false; }
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
		auto Display = FCk_Handle(DisplayTimer), Auto = FCk_Handle(AutoTimer);
		auto Counter = FCk_Handle(CounterAttribute);
		if (_AuthorityFailed || !CkAttributeFloat::IsLiveEntity(Self) ||
			!CkAttributeFloat::IsLiveEntity(Display) || !CkAttributeFloat::IsLiveEntity(Auto) ||
			!CkAttributeFloat::IsLiveEntity(Counter)) { return false; }
		if (utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
			utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
			utils_entity_lifetime::Get_LifetimeOwner(Counter) != Self ||
			!utils_timer::DoCast(Display).IsSet() || !utils_timer::DoCast(Auto).IsSet() ||
			!utils_float_attribute::DoCast(Counter).IsSet() ||
			utils_float_attribute::TryGet(Self,
				utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Counter")) != CounterAttribute ||
			!utils_float_attribute::Has_Component(CounterAttribute, ECk_MinMaxCurrent::Min) ||
			!utils_float_attribute::Has_Component(CounterAttribute, ECk_MinMaxCurrent::Max) ||
			!utils_float_attribute::Has_Component(CounterAttribute, ECk_MinMaxCurrent::Current))
		{ return false; }
		if (Math::Abs(utils_float_attribute::Get_FinalValue(CounterAttribute, ECk_MinMaxCurrent::Min)) > 0.001f ||
			Math::Abs(utils_float_attribute::Get_FinalValue(CounterAttribute, ECk_MinMaxCurrent::Max) - 50.0f) > 0.001f)
		{ return false; }
		TArray<FCk_Handle> Messengers;
		if (!Get_HasExactMessengers(Messengers)) { return false; }
		auto Expected = TArray<FCk_Handle>();
		Expected.Add(Display); Expected.Add(Auto); Expected.Add(Counter);
		for (auto Messenger : Messengers) { Expected.Add(Messenger); }
		int32 LiveCount = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
		{ if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
		  if (!Expected.Contains(Child)) { return false; } LiveCount++; }
		return LiveCount == Expected.Num() && AutoConfig.TotalSteps == 12 && AutoStep >= 0;
	}
	bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
	private bool Get_IsFinite(float32 InValue)
	{ return InValue == InValue && Math::Abs(InValue) < 1.0e20f; }
	bool Get_HasValidRevocableRecords()
	{
		if (!Get_HasValidAuthorityComposition() ||
			RevocableModifiers.Num() != _ExpectedDeltas.Num() ||
			RevocableModifiers.Num() != _ExpectedContributions.Num()) { return false; }
		auto All = utils_float_attribute_modifier::ForEach(CounterAttribute,
			FInstancedStruct(), FCk_Lambda_InHandle());
		if (All.Num() != RevocableModifiers.Num() + _PermanentModifiers.Num() ||
			_PermanentModifiers.Num() != (_IncrementPermanentAdded ? 1 : 0) +
				(_DecrementPermanentAdded ? 1 : 0) + (_ResetPermanentAdded ? 1 : 0))
		{ return false; }
		auto Seen = TArray<FCk_Handle>();
		float32 Total = 0.0f;
		for (int32 i = 0; i < RevocableModifiers.Num(); i++)
		{
			auto Mod = RevocableModifiers[i]; auto Handle = FCk_Handle(Mod);
			if (!CkAttributeFloat::IsLiveEntity(Handle) || Seen.Contains(Handle) ||
				utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(CounterAttribute) ||
				!utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
				!All.Contains(Mod) || !Get_IsFinite(_ExpectedDeltas[i]) ||
				!Get_IsFinite(_ExpectedContributions[i])) { return false; }
			auto Delta = utils_float_attribute_modifier::Get_Delta(Mod);
			if (!Get_IsFinite(Delta) || Math::Abs(Delta - _ExpectedDeltas[i]) > 0.001f)
			{ return false; }
			Total += _ExpectedContributions[i]; Seen.Add(Handle);
		}
		for (auto Permanent : _PermanentModifiers)
		{
			auto Handle = FCk_Handle(Permanent);
			if (!CkAttributeFloat::IsLiveEntity(Handle) || Seen.Contains(Handle) ||
				utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(CounterAttribute) ||
				!utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
				!All.Contains(Permanent)) { return false; }
			Seen.Add(Handle);
		}
		if (_IncrementPermanentAdded && _DecrementPermanentAdded &&
			_IncrementPermanent == _DecrementPermanent) { return false; }
		if (_IncrementPermanentAdded && !_PermanentModifiers.Contains(_IncrementPermanent))
		{ return false; }
		if (_DecrementPermanentAdded && !_PermanentModifiers.Contains(_DecrementPermanent))
		{ return false; }
		if (_ResetPermanentAdded && !_PermanentModifiers.Contains(_ResetPermanent))
		{ return false; }
		return Seen.Num() == All.Num() && Get_IsFinite(Total) &&
			Math::Abs(Total - _ExpectedBonus) <= 0.001f;
	}
	bool Get_HasSettledAuthorityProjection()
	{
		if (!Get_HasValidRevocableRecords() || _PendingPasses < 2) { return false; }
		for (auto Old : _RetiringModifiers) { if (ck::IsValid(Old)) { return false; } }
		auto Base = utils_float_attribute::Get_BaseValue(CounterAttribute);
		auto Bonus = utils_float_attribute::Get_BonusValue(CounterAttribute);
		auto Final = utils_float_attribute::Get_FinalValue(CounterAttribute);
		auto Pre = utils_float_attribute::Get_PreClampFinalValue(CounterAttribute);
		auto Overflow = utils_float_attribute::Get_ClampOverflow(CounterAttribute);
		return Get_IsFinite(Base) && Get_IsFinite(Bonus) && Get_IsFinite(Final) &&
			Get_IsFinite(Pre) && Get_IsFinite(Overflow) &&
			Math::Abs(Base - _ExpectedBase) <= 0.001f &&
			Math::Abs(Bonus - _ExpectedBonus) <= 0.001f &&
			Math::Abs(Pre - (_ExpectedBase + _ExpectedBonus)) <= 0.001f &&
			Math::Abs(Final - Math::Clamp(Pre, 0.0f, 50.0f)) <= 0.001f &&
			Math::Abs(Overflow - (Pre - Final)) <= 0.001f;
	}
	bool Request_StartAuthorityDemo()
	{
		if (!WaitForWorldReady || _AuthorityStarted || !Get_HasValidAuthorityComposition() ||
			!Get_HasValidRevocableRecords() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }
		_AuthorityStarted = true; _StartReleased = false; _StartSucceeded = false;
		return true;
	}
	UFUNCTION() private void OnAuthorityStartCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_StartReleased || InOwner != FCk_Handle(AutoTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_StartSucceeded = InResult == ECk_Request_OperationResult::Succeeded &&
			Get_HasValidAuthorityComposition() &&
			AutoTimer.Get_CurrentState() == ECk_Timer_State::Running;
		if (!_StartSucceeded) { Request_RecordAuthorityFailure(429); _MutationGateClosed = true; }
	}
	bool Get_HasSettledAuthorityStart()
	{ return _AuthorityStarted && !_StartReleased && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	bool Request_ReleaseAuthorityStart()
	{
		if (!Get_HasSettledAuthorityStart()) { return false; }
		auto Request = utils_timer::Request_Resume(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityStartCompleted"));
		if (ck::Is_NOT_Valid(Request)) { Request_RecordAuthorityFailure(439); return false; }
		_StartReleased = true; _MutationGateClosed = false;
		return true;
	}
	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() ||
			(WaitForWorldReady && (!_AuthorityStarted || !_StartReleased || !_StartSucceeded)) ||
			(_ReleaseResumeRequested && !_ReleaseResumeCompleted) ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false; _MutationGateClosed = true;
		if (_PendingMutation || !Get_HasSettledAuthorityProjection()) { return false; }
		if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused)
		{ _Prepared = !_PrepareRequested || _PrepareSucceeded; return _Prepared; }
		if (_PrepareRequested || AutoTimer.Get_CurrentState() != ECk_Timer_State::Running)
		{ return false; }
		_PrepareRequested = true; _PrepareSucceeded = false;
		auto Request = utils_timer::Request_Pause(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityPrepareCompleted"));
		if (ck::Is_NOT_Valid(Request)) { Request_RecordAuthorityFailure(458); }
		return false;
	}
	UFUNCTION() private void OnAuthorityPrepareCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ if (!_PrepareRequested || InOwner != FCk_Handle(AutoTimer) ||
		!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
	  _PrepareSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
	  if (!_PrepareSucceeded) { Request_RecordAuthorityFailure(465); } }
	bool Get_IsAuthorityTransactionPrepared()
	{ return _Prepared && !_PendingMutation && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
			InAction < 0 || InAction > 2 || InValue != 0.0f) { return false; }
		AutoRunning = false; _ControlPending = true; _PendingPasses = 0;
		bool Good = InAction == 0 ? Request_AddRevocable(true) :
			(InAction == 1 ? Request_AddRevocable(false) : Request_RevokeAllRevocable());
		if (!Good) { Request_RecordAuthorityFailure(476); return false; }
		_PendingMutation = true; return true;
	}
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_PendingMutation && Get_IsAuthorityTransactionPrepared() &&
		Get_HasSettledAuthorityProjection(); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
		_ResetPending = true; _PendingPasses = 0;
		if (!Request_ResetToDefault()) { Request_RecordAuthorityFailure(486); return false; }
		AutoStep = 0; ValueChangeCount = 0; _PendingMutation = true;
		return true;
	}
	bool Get_HasSettledAuthorityReset()
	{ return _ResetPending && !_PendingMutation && Get_IsAuthorityTransactionPrepared() &&
		Get_HasSettledAuthorityProjection(); }
	bool Request_BeginFinishAuthorityTransaction(bool InResumeAuto)
	{
		if (_FinishPending || !Get_HasValidAuthorityComposition() ||
			(_ControlPending && !Get_HasSettledAuthorityControl()) ||
			(_ResetPending && !Get_HasSettledAuthorityReset()) ||
			(!_ControlPending && !_ResetPending && !Get_IsAuthorityTransactionPrepared()))
		{ return false; }
		// Keep the physical timer paused until the whole cohort can release
		// synchronously. Running status here would permit a skipped gated tick.
		_FinishPending = true; _FinishCompleted = false; _FinishReleased = false;
		_FinishResume = InResumeAuto; _ReleaseResumeRequested = false;
		_ReleaseResumeCompleted = false;
		return true;
	}
	UFUNCTION() private void OnAuthorityReleaseCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_FinishReleased || !_ReleaseResumeRequested ||
			InOwner != FCk_Handle(AutoTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_ReleaseResumeCompleted = true;
		if (InResult != ECk_Request_OperationResult::Succeeded ||
			!Get_HasValidAuthorityComposition() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Running)
		{ Request_RecordAuthorityFailure(516); _MutationGateClosed = true; }
	}
	private void Request_TickFinishAuthorityTransaction()
	{
		if (!_FinishPending || _PendingMutation ||
			!Get_HasSettledAuthorityProjection() ||
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
		!_AuthorityFailed && Get_HasSettledAuthorityProjection() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	bool Request_ReleaseAuthorityTransaction()
	{
		if (!Get_CanReleaseAuthorityTransaction() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }
		if (_FinishResume)
		{
			_ReleaseResumeRequested = true;
			auto Request = utils_timer::Request_Resume(AutoTimer,
				FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityReleaseCompleted"));
			if (ck::Is_NOT_Valid(Request))
			{ Request_RecordAuthorityFailure(544); _ReleaseResumeRequested = false; return false; }
		}
		_FinishReleased = true; _MutationGateClosed = !_FinishResume;
		return true;
	}
	private bool Request_QueueLegacyCommand(int32 InAction)
	{ if (WaitForWorldReady || InAction < 0 || InAction > 5) { return false; }
	  _LegacyActions.Add(InAction); return true; }
	private void Request_TickLegacyCommands()
	{
		if (WaitForWorldReady || _AuthorityFailed || _LegacyActions.Num() == 0) { return; }
		if (_LegacyStarted)
		{
			if (_FinishPending) { return; }
			if (_ControlPending && Get_HasSettledAuthorityControl())
			{ if (!Request_BeginFinishAuthorityTransaction(false)) { Request_RecordAuthorityFailure(559); } return; }
			if (_ResetPending && Get_HasSettledAuthorityReset())
			{ if (!Request_BeginFinishAuthorityTransaction(_LegacyResumeAuto)) { Request_RecordAuthorityFailure(561); } return; }
			if (_ControlPending || _ResetPending || !_FinishCompleted) { return; }
			if (_FinishResume && !_ReleaseResumeRequested)
			{
				_ReleaseResumeRequested = true; _FinishReleased = true;
				auto Request = utils_timer::Request_Resume(AutoTimer,
					FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityReleaseCompleted"));
				if (ck::Is_NOT_Valid(Request))
				{ Request_RecordAuthorityFailure(569); _MutationGateClosed = true; return; }
				return;
			}
			if (_ReleaseResumeRequested &&
				(!_ReleaseResumeCompleted ||
				AutoTimer.Get_CurrentState() != ECk_Timer_State::Running)) { return; }
			_FinishCompleted = false; _MutationGateClosed = !AutoRunning;
			_LegacyActions.RemoveAt(0); _LegacyStarted = false; return;
		}
		if (!Request_TryPrepareAuthorityTransaction()) { return; }
		auto Action = _LegacyActions[0]; _LegacyResumeAuto = AutoRunning;
		if (Action <= 2)
		{ if (!Request_ApplyAuthorityControl(Action, 0.0f)) { Request_RecordAuthorityFailure(581); return; } }
		else if (Action == 5)
		{ if (!Request_BeginAuthorityReset()) { Request_RecordAuthorityFailure(583); return; } }
		else if (!Request_BeginFinishAuthorityTransaction(Action == 4))
		{ Request_RecordAuthorityFailure(585); return; }
		_LegacyStarted = true;
	}

}
