//============================================================================
// FLOAT ATTRIBUTE REFILL STATION (Float-exclusive feature)
//============================================================================

class UCk_EntityScript_AttributeGym_FloatRefill : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FCk_Handle_Timer DisplayTimer;
	bool DesiredEnergyRefillRunning = true;
	bool DesiredManaRefillRunning = true;
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
	private int32 _ExpectedMask = 0;
	private int32 _CompletedMask = 0;
	private int32 _PendingPasses = 0;
	private float32 _ExpectedEnergy = 100.0f;
	private float32 _ExpectedMana = 80.0f;
	private TArray<int32> _LegacyActions;
	private bool _LegacyStarted = false;
	private bool _LegacyResumeAuto = false;

	FCk_Handle_FloatAttribute EnergyAttribute;
	FCk_Handle_FloatAttribute ManaAttribute;

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
		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatRefill");

		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(4.0f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			_GateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(4.0f)); }

		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests the float-exclusive refill system with two policies.\nEnergy: Variable | Mana: AlwaysReturnToZero.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [6] Refill station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Drain energy to 20, mana to 15", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Watch refill", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Pause energy refill", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Drain again (energy 10, mana 5)", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Resume energy refill", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Reset to full", 5, 5));
		AutoConfig.ManualCommands.Add("panel [G] Refill ring - toggle refill / drain energy / drain mana");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(EnergyAttribute) || ck::Is_NOT_Valid(ManaAttribute))
		{ _AuthorityFailed = true; return; }
		Request_BindSignals(InHandle);

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_ToggleRefill,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnToggleRefill"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_DrainEnergy,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnDrainEnergy"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_DrainMana,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnDrainMana"));
	}

	void Request_SetupAttributes(FCk_Handle InHandle)
	{
		auto RefillParams = FCk_FloatAttributeRefill_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Energy.Refill"), 5.0f);
		RefillParams.Set_StartingState(WaitForWorldReady ?
			ECk_Attribute_RefillState::Paused : ECk_Attribute_RefillState::Running);

		auto EnergyParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Energy"), 100.0f);
		EnergyParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(100.0f);
		EnergyParams.Set_EnableRefill(true).Set_RefillParams(RefillParams);
		EnergyAttribute = utils_float_attribute::Add(InHandle, EnergyParams);

		auto ManaRefillParams = FCk_FloatAttributeRefill_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Mana.Refill"), 3.0f);
		ManaRefillParams.Set_RefillBehavior(ECk_Attribute_Refill_Policy::AlwaysReturnToZero);
		ManaRefillParams.Set_StartingState(WaitForWorldReady ?
			ECk_Attribute_RefillState::Paused : ECk_Attribute_RefillState::Running);

		auto ManaParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Mana"), 80.0f);
		ManaParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(80.0f);
		ManaParams.Set_EnableRefill(true).Set_RefillParams(ManaRefillParams);
		ManaAttribute = utils_float_attribute::Add(InHandle, ManaParams);
	}

	void Request_BindSignals(FCk_Handle InHandle)
	{
		utils_float_attribute::BindTo_OnValueChanged(EnergyAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
		utils_float_attribute::BindTo_OnMinClamped(EnergyAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnMinClamped"));
		utils_float_attribute::BindTo_OnMaxClamped(EnergyAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnMaxClamped"));
		utils_float_attribute::BindTo_OnValueChanged(ManaAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
	}

	UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (_PendingOverride)
		{
			_PendingPasses++;
			if (_CompletedMask == _ExpectedMask && Get_HasExpectedValues()) { _PendingOverride = false; }
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

		if (Step == 0) { utils_float_attribute::Request_Override(EnergyAttribute, 20.0f, ECk_MinMaxCurrent::Current); utils_float_attribute::Request_Override(ManaAttribute, 15.0f, ECk_MinMaxCurrent::Current); }
		else if (Step == 1) { /* watch refill */ }
		else if (Step == 2) { DesiredEnergyRefillRunning = false; if (!Request_SetRefillStates()) { _AuthorityFailed = true; return; } }
		else if (Step == 3) { utils_float_attribute::Request_Override(EnergyAttribute, 10.0f, ECk_MinMaxCurrent::Current); utils_float_attribute::Request_Override(ManaAttribute, 5.0f, ECk_MinMaxCurrent::Current); }
		else if (Step == 4) { DesiredEnergyRefillRunning = true; if (!Request_SetRefillStates()) { _AuthorityFailed = true; return; } }
		else if (Step == 5)
		{
			utils_float_attribute::Request_Override(EnergyAttribute, 100.0f, ECk_MinMaxCurrent::Current);
			utils_float_attribute::Request_Override(ManaAttribute, 80.0f, ECk_MinMaxCurrent::Current);
			DesiredEnergyRefillRunning = true; DesiredManaRefillRunning = true;
			if (!Request_SetRefillStates()) { _AuthorityFailed = true; return; }
		}

		AutoStep++;
	}

	UFUNCTION() private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacy(Typed.Enabled ? 5 : 4);
	}

	void Request_UpdateDisplay()
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
		auto TitleText = "FLOAT REFILL (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n\n";

		auto EnergyValue = utils_float_attribute::Get_FinalValue(EnergyAttribute);
		auto EnergyBar = CkGym_Attribute::Create_ProgressBar(EnergyValue, 100.0f, 25);
		DisplayText = f"{DisplayText}ENERGY (Variable Policy):\n";
		DisplayText = f"{DisplayText}  Value: {EnergyValue}/100\n";
		DisplayText = f"{DisplayText}  [{EnergyBar}]\n";
		{
			auto Refill = utils_float_attribute::TryGet_RefillAttribute(EnergyAttribute);
			if (ck::IsValid(Refill))
			{
				auto FillRate = utils_float_attribute_refill::Get_FillRate(Refill);
				auto RefillState = utils_float_attribute_refill::Get_RefillState(Refill);
				auto StateStr = (RefillState == ECk_Attribute_RefillState::Running) ? "RUNNING" : "PAUSED";
				DisplayText = f"{DisplayText}  Rate: {FillRate}/sec | State: {StateStr}\n";
			}
		}

		auto ManaValue = utils_float_attribute::Get_FinalValue(ManaAttribute);
		auto ManaBar = CkGym_Attribute::Create_ProgressBar(ManaValue, 80.0f, 25, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
		DisplayText = f"{DisplayText}\nMANA (AlwaysReturnToZero Policy):\n";
		DisplayText = f"{DisplayText}  Value: {ManaValue}/80\n";
		DisplayText = f"{DisplayText}  [{ManaBar}]\n";
		{
			auto Refill = utils_float_attribute::TryGet_RefillAttribute(ManaAttribute);
			if (ck::IsValid(Refill))
			{
				auto FillRate = utils_float_attribute_refill::Get_FillRate(Refill);
				auto RefillState = utils_float_attribute_refill::Get_RefillState(Refill);
				auto StateStr = (RefillState == ECk_Attribute_RefillState::Running) ? "RUNNING" : "PAUSED";
				DisplayText = f"{DisplayText}  Rate: {FillRate}/sec | State: {StateStr}\n";
			}
		}

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);
		OutTitle = TitleText; OutBody = DisplayText; return true;
	}

	UFUNCTION() void OnValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }
	UFUNCTION() void OnMinClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition() && !WaitForWorldReady)
		{ CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(0.0f, 0.0f, 150.0f), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)); } }
	UFUNCTION() void OnMaxClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{ if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition() && !WaitForWorldReady)
		{ CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(0.0f, 0.0f, 150.0f), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)); } }

	UFUNCTION()
	private void OnDrainEnergy(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(1); }
	}

	UFUNCTION()
	private void OnDrainMana(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(2); }
	}

	UFUNCTION()
	private void OnToggleRefill(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(0); }
	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacy(3); }
	}
	private bool Get_IsFinite(float32 V)
	{ return V == V && Math::Abs(V) < 1.0e20f; }
	private bool Get_Near(float32 A, float32 B)
	{ return Get_IsFinite(A) && Get_IsFinite(B) && Math::Abs(A - B) <= 0.001f; }
	private bool Get_HasExactRefill(FCk_Handle_FloatAttribute InAttribute,
		FName InName, float32 InRate)
	{
		auto Self = ck::ToEntity(this);
		if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(InAttribute)) ||
			!utils_float_attribute::DoCast(FCk_Handle(InAttribute)).IsSet() ||
			!utils_float_attribute::Has_RefillAttribute(InAttribute)) { return false; }
		auto R = utils_float_attribute::TryGet_RefillAttribute(InAttribute);
		auto Named = utils_float_attribute::TryGet(Self,
			utils_gameplay_tag::ResolveGameplayTag(InName));
		if (!CkAttributeFloat::IsLiveEntity(FCk_Handle(R)) ||
			!utils_float_attribute_refill::Has_Refill(FCk_Handle(R)) ||
			!utils_float_attribute::DoCast(FCk_Handle(R)).IsSet() ||
			FCk_Handle(Named) != FCk_Handle(R) ||
			!utils_float_attribute::Has_Component(Named, ECk_MinMaxCurrent::Current) ||
			utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(R)) != Self ||
			!Get_IsFinite(InRate))
		{ return false; }
		auto Rate = utils_float_attribute_refill::Get_FillRate(R);
		auto State = utils_float_attribute_refill::Get_RefillState(R);
		return Get_Near(Rate, InRate) &&
			(State == ECk_Attribute_RefillState::Running || State == ECk_Attribute_RefillState::Paused);
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
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_ToggleRefill, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_DrainEnergy, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_DrainMana, Out)) { return false; }
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
		if (_AuthorityFailed || !CkAttributeFloat::IsLiveEntity(Self) ||
			!CkAttributeFloat::IsLiveEntity(FCk_Handle(EnergyAttribute)) ||
			!CkAttributeFloat::IsLiveEntity(FCk_Handle(ManaAttribute)) ||
			!utils_float_attribute::DoCast(FCk_Handle(EnergyAttribute)).IsSet() ||
			!utils_float_attribute::DoCast(FCk_Handle(ManaAttribute)).IsSet() ||
			!utils_float_attribute::Has_RefillAttribute(EnergyAttribute) ||
			!utils_float_attribute::Has_RefillAttribute(ManaAttribute)) { return false; }
		auto EnergyRefill = utils_float_attribute::TryGet_RefillAttribute(EnergyAttribute);
		auto ManaRefill = utils_float_attribute::TryGet_RefillAttribute(ManaAttribute);
		TArray<FCk_Handle> Messengers;
		if (!Get_HasExactMessengers(Messengers)) { return false; }
		TArray<FCk_Handle> Expected;
		Expected.Add(FCk_Handle(DisplayTimer)); Expected.Add(FCk_Handle(AutoTimer));
		Expected.Add(FCk_Handle(EnergyAttribute)); Expected.Add(FCk_Handle(ManaAttribute));
		Expected.Add(FCk_Handle(EnergyRefill)); Expected.Add(FCk_Handle(ManaRefill));
		for (auto Handle : Expected)
		{
			if (!CkAttributeFloat::IsLiveEntity(Handle) ||
				utils_entity_lifetime::Get_LifetimeOwner(Handle) != Self) { return false; }
		}
		if (!utils_timer::DoCast(Expected[0]).IsSet() ||
			!utils_timer::DoCast(Expected[1]).IsSet() ||
			!utils_float_attribute::DoCast(Expected[2]).IsSet() ||
			!utils_float_attribute::DoCast(Expected[3]).IsSet()) { return false; }
		for (auto Messenger : Messengers) { Expected.Add(Messenger); }
		int32 Live = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
		{
			if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
			if (!Expected.Contains(Child)) { return false; }
			Live++;
		}
		if (Live != Expected.Num() || AutoConfig.TotalSteps != 6 || AutoStep < 0) { return false; }
		return utils_float_attribute::TryGet(Self,
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Energy")) == EnergyAttribute &&
			utils_float_attribute::TryGet(Self,
				utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Mana")) == ManaAttribute &&
			utils_float_attribute::Has_Component(EnergyAttribute, ECk_MinMaxCurrent::Current) &&
			utils_float_attribute::Has_Component(ManaAttribute, ECk_MinMaxCurrent::Current) &&
			Get_HasExactRefill(EnergyAttribute, n"FloatAttribute.Energy.Refill", 5.0f) &&
			Get_HasExactRefill(ManaAttribute, n"FloatAttribute.Mana.Refill", 3.0f);
	}
	bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
	private bool Request_SetActualRefills(bool InForcePaused)
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		auto E = utils_float_attribute::TryGet_RefillAttribute(EnergyAttribute);
		auto M = utils_float_attribute::TryGet_RefillAttribute(ManaAttribute);
		if (InForcePaused || !DesiredEnergyRefillRunning)
		{ if (utils_float_attribute_refill::Request_Pause(E) != E) { return false; } }
		else { if (utils_float_attribute_refill::Request_Resume(E) != E) { return false; } }
		if (InForcePaused || !DesiredManaRefillRunning)
		{ if (utils_float_attribute_refill::Request_Pause(M) != M) { return false; } }
		else { if (utils_float_attribute_refill::Request_Resume(M) != M) { return false; } }
		return utils_float_attribute_refill::Get_RefillState(E) ==
			((InForcePaused || !DesiredEnergyRefillRunning) ? ECk_Attribute_RefillState::Paused : ECk_Attribute_RefillState::Running) &&
			utils_float_attribute_refill::Get_RefillState(M) ==
			((InForcePaused || !DesiredManaRefillRunning) ? ECk_Attribute_RefillState::Paused : ECk_Attribute_RefillState::Running);
	}
	private bool Request_SetRefillStates()
	{ return !_GateClosed && Request_SetActualRefills(false); }
	private bool Get_ActualRefillsPaused()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		return utils_float_attribute_refill::Get_RefillState(
			utils_float_attribute::TryGet_RefillAttribute(EnergyAttribute)) == ECk_Attribute_RefillState::Paused &&
			utils_float_attribute_refill::Get_RefillState(
				utils_float_attribute::TryGet_RefillAttribute(ManaAttribute)) == ECk_Attribute_RefillState::Paused;
	}
	private bool Get_ActualRefillsMatchDesired()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		auto E = utils_float_attribute_refill::Get_RefillState(
			utils_float_attribute::TryGet_RefillAttribute(EnergyAttribute));
		auto M = utils_float_attribute_refill::Get_RefillState(
			utils_float_attribute::TryGet_RefillAttribute(ManaAttribute));
		return (E == ECk_Attribute_RefillState::Running) == DesiredEnergyRefillRunning &&
			(M == ECk_Attribute_RefillState::Running) == DesiredManaRefillRunning;
	}
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
			!Get_ActualRefillsPaused() || AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused)
		{ return false; }
		_AuthorityStarted = true; _StartSucceeded = true;
		return true;
	}
	bool Get_HasSettledAuthorityStart()
	{ return _AuthorityStarted && !_StartReleased && _StartSucceeded &&
		Get_HasValidAuthorityComposition() && Get_ActualRefillsPaused() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	bool Request_ReleaseAuthorityStart()
	{
		if (!Get_HasSettledAuthorityStart() || !Request_SetActualRefills(false) ||
			!Get_ActualRefillsMatchDesired())
		{ _AuthorityFailed = true; return false; }
		if (ck::Is_NOT_Valid(utils_timer::Request_Resume(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAutoResumeCompleted"))))
		{ _AuthorityFailed = true; return false; }
		_StartReleased = true; _GateClosed = false; return true;
	}
	private bool Get_HasExpectedValues()
	{
		if (_PendingPasses < 2 || !Get_HasValidAuthorityComposition()) { return false; }
		if ((_ExpectedMask & 1) != 0 &&
			!Get_Near(utils_float_attribute::Get_PreClampFinalValue(EnergyAttribute), _ExpectedEnergy))
		{ return false; }
		if ((_ExpectedMask & 2) != 0 &&
			!Get_Near(utils_float_attribute::Get_PreClampFinalValue(ManaAttribute), _ExpectedMana))
		{ return false; }
		return Get_IsFinite(utils_float_attribute::Get_FinalValue(EnergyAttribute)) &&
			Get_IsFinite(utils_float_attribute::Get_FinalValue(ManaAttribute));
	}
	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false; _GateClosed = true;
		if (_PendingOverride) { return false; }
		if (!_PrepareRequested)
		{
			_PrepareRequested = true; _PrepareSucceeded = false;
			if (!Request_SetActualRefills(true)) { _AuthorityFailed = true; return false; }
			if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Running)
			{
				auto R = utils_timer::Request_Pause(AutoTimer,
					FCk_Delegate_Request_OnCompleted(this, n"OnPrepareCompleted"));
				if (ck::Is_NOT_Valid(R)) { _AuthorityFailed = true; return false; }
			}
			else { _PrepareSucceeded = AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
		}
		_Prepared = _PrepareSucceeded && Get_ActualRefillsPaused() &&
			AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused;
		return _Prepared;
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
		Get_ActualRefillsPaused() && AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	private bool Request_TrackOverride(FCk_Handle_FloatAttribute InAttribute,
		float32 InValue, int32 InBit)
	{
		if (_PendingOverride || !Get_IsFinite(InValue)) { return false; }
		_ExpectedMask = InBit; _CompletedMask = 0; _PendingPasses = 0; _PendingOverride = true;
		if (InBit == 1) { _ExpectedEnergy = InValue; }
		else { _ExpectedMana = InValue; }
		auto R = utils_float_attribute::Request_Override(InAttribute, InValue, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnSingleOverrideCompleted"));
		if (ck::Is_NOT_Valid(R)) { _AuthorityFailed = true; return false; }
		return true;
	}
	UFUNCTION() private void OnSingleOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, _ExpectedMask); }
	UFUNCTION() private void OnEnergyResetCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, 1); }
	UFUNCTION() private void OnManaResetCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, 2); }
	private void Request_RecordOverride(FCk_Handle InOwner, ECk_Request_OperationResult InResult, int32 InBit)
	{
		FCk_Handle Expected = InBit == 1 ? FCk_Handle(EnergyAttribute) : FCk_Handle(ManaAttribute);
		if (!_PendingOverride || InOwner != Expected || (_ExpectedMask & InBit) == 0 ||
			(_CompletedMask & InBit) != 0 || InResult != ECk_Request_OperationResult::Succeeded)
		{ _AuthorityFailed = true; return; }
		_CompletedMask |= InBit;
	}
	bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
			InValue != 0.0f || InAction < 0 || InAction > 2) { return false; }
		AutoRunning = false; _ControlPending = true;
		if (InAction == 0)
		{ DesiredEnergyRefillRunning = !DesiredEnergyRefillRunning;
			_ExpectedMask = 0; _CompletedMask = 0; return true; }
		if (InAction == 1) { return Request_TrackOverride(EnergyAttribute, 10.0f, 1); }
		return Request_TrackOverride(ManaAttribute, 5.0f, 2);
	}
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_PendingOverride && _CompletedMask == _ExpectedMask &&
		Get_IsAuthorityTransactionPrepared() &&
		(_ExpectedMask == 0 || Get_HasExpectedValues()); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
		AutoStep = 0; ValueChangeCount = 0; AutoRunning = true;
		DesiredEnergyRefillRunning = true; DesiredManaRefillRunning = true;
		_ResetPending = true; _ExpectedMask = 3; _CompletedMask = 0;
		_PendingPasses = 0; _PendingOverride = true;
		_ExpectedEnergy = 100.0f; _ExpectedMana = 80.0f;
		auto A = utils_float_attribute::Request_Override(EnergyAttribute, 100.0f,
			ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnEnergyResetCompleted"));
		auto B = utils_float_attribute::Request_Override(ManaAttribute, 80.0f,
			ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnManaResetCompleted"));
		if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B))
		{ _AuthorityFailed = true; return false; }
		return true;
	}
	bool Get_HasSettledAuthorityReset()
	{ return _ResetPending && !_PendingOverride && _CompletedMask == 3 &&
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
		if (!Get_ActualRefillsPaused())
		{ _AuthorityFailed = true; _FinishPending = false; return false; }
		return true;
	}
	private void Request_TickFinish()
	{
		if (!_FinishPending || _PendingOverride ||
			!Get_ActualRefillsPaused() || AutoTimer.Get_CurrentState() !=
				ECk_Timer_State::Paused) { return; }
		_ControlPending = false; _ResetPending = false; _Prepared = false;
		_PrepareRequested = false; _PrepareSucceeded = false; _GateClosed = true;
		AutoRunning = _FinishResume; _FinishPending = false; _FinishCompleted = true;
	}
	bool Get_IsAuthorityFinishPending() const { return _FinishPending; }
	bool Get_HasFinishedAuthorityTransaction() const { return _FinishCompleted; }
	bool Get_CanReleaseAuthorityTransaction()
	{ return WaitForWorldReady && _FinishCompleted && !_FinishReleased && !_AuthorityFailed &&
		Get_HasValidAuthorityComposition() && Get_ActualRefillsPaused() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	private bool Request_ReleaseMutatorTimers()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		if (!Request_SetActualRefills(false) || !Get_ActualRefillsMatchDesired()) { return false; }
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
	{ if (WaitForWorldReady || InAction < 0 || InAction > 5) { return false; }
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
		if (Action == 3) { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; return; } }
		else if (Action <= 2)
		{ if (!Request_ApplyAuthorityControl(Action, 0.0f)) { _AuthorityFailed = true; return; } }
		else if (!Request_BeginFinishAuthorityTransaction(Action == 5))
		{ _AuthorityFailed = true; return; }
		_LegacyStarted = true;
	}

}
