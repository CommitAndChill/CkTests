//============================================================================
// FLOAT CLAMPING ENTITY SCRIPT
//============================================================================

USTRUCT()
struct FCkAttributeGym_FloatClampEvent
{
	UPROPERTY() int32 AttributeIndex = -1;
	UPROPERTY() int32 Serial = 0;
	UPROPERTY() float32 PreClampFinal = 0.0f;
	UPROPERTY() float32 Final = 0.0f;
	UPROPERTY() float32 Overflow = 0.0f;
}

class UCk_EntityScript_AttributeGym_FloatClamping : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FCk_Handle_Timer DisplayTimer;
	UPROPERTY() TArray<FCkAttributeGym_FloatClampEvent> ClampEvents;
	UPROPERTY() int32 BoundaryEventSerial = 0;
	private bool _AuthorityStarted = false;
	private bool _AuthorityFailed = false;
	private bool _InitialShieldOverrideRequested = false;
	private bool _InitialShieldOverrideCompleted = false;
	private bool _MutationGateClosed = false;
	private bool _PrepareRequested = false;
	private bool _PrepareAutoSucceeded = false;
	private bool _PrepareUpdateSucceeded = false;
	private bool _Prepared = false;
	private bool _ControlPending = false;
	private bool _ResetPending = false;
	private bool _FinishPending = false;
	private bool _FinishCompleted = false;
	private bool _FinishReleased = false;
	private bool _FinishResume = false;
	private bool _StartAutoSucceeded = false;
	private bool _StartReleased = false;
	private bool _StartUpdateSucceeded = false;
	private bool _PendingMutation = false;
	private int32 _PendingPasses = 0;
	private int32 _CompletedMask = 0;
	private TArray<int32> _LegacyActions;
	private bool _LegacyStarted = false;
	private bool _LegacyResumeAuto = false;

	FCk_Handle_FloatAttribute ArmorAttribute;
	FCk_Handle_FloatAttribute StaminaAttribute;
	FCk_Handle_FloatAttribute HealthAttribute;
	FCk_Handle_FloatAttribute ShieldAttribute;

	FCk_Handle_Timer AutoTimer;
	FCk_Handle_Timer UpdateTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;
	int32 ValueChangeCount = 0;
	int32 ClampedCount = 0;
	float32 LastPreClampValue = 0.0f;
	float32 LastClampedValue = 0.0f;
	float32 LastOverflow = 0.0f;

	// Auto-cycling test values (fractional to demonstrate float precision)
	float32 CurrentArmorTest = 100.5f;
	float32 CurrentStaminaTest = 150.75f;
	float32 CurrentHealthTest = 75.25f;
	bool ArmorIncreasing = true;
	bool StaminaIncreasing = true;
	bool HealthIncreasing = false;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatClamping");

		ClampEvents.Reset();
		for (int32 Index = 0; Index < 4; Index++)
		{
			FCkAttributeGym_FloatClampEvent Event; Event.AttributeIndex = Index;
			ClampEvents.Add(Event);
		}
		BoundaryEventSerial = 0;

		// Display timer
		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		// Update timer for continuous value cycling
		auto UpdateTimerParams = FCk_Timer_Spec(FCk_Time(1.8f));
		UpdateTimerParams.Set_StartingState(WaitForWorldReady ? ECk_Timer_State::Paused :
			ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		UpdateTimer = utils_timer::Add(InHandle, UpdateTimerParams);
		UpdateTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"UpdateTick"));

		// Auto timer (controls on/off for the cycling)
		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(1.8f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			_MutationGateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(1.8f)); }

		// Auto config
		AutoConfig.TotalSteps = 1;
		AutoConfig.Description = "Cycles fractional values beyond min/max to demonstrate clamping.\nShield demonstrates initial-value clamping (created with value > max).";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [2] Clamping station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Cycling Armor/Stamina/Health values", 0, 0));
		AutoConfig.ManualCommands.Add("panel [B] Test boundaries");
		AutoConfig.ManualCommands.Add("panel [R] Reset ring - clamping station / all stations");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(ArmorAttribute) || ck::Is_NOT_Valid(StaminaAttribute) ||
			ck::Is_NOT_Valid(HealthAttribute) || ck::Is_NOT_Valid(ShieldAttribute))
		{ _AuthorityFailed = true; return; }
		Request_BindSignals(InHandle);

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes, FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_TestBoundaries, FCk_Delegate_Messaging_OnBroadcast(this, n"OnTestBoundaries"));
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto ArmorParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Armor"), 100.5f);
		ArmorParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(200.0f);
		ArmorAttribute = utils_float_attribute::Add(InHandle, ArmorParams);

		auto StaminaParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Stamina"), 150.75f);
		StaminaParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(50.0f).Set_MaxValue(255.0f);
		StaminaAttribute = utils_float_attribute::Add(InHandle, StaminaParams);

		auto HealthParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Health"), 75.25f);
		HealthParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(100.0f);
		HealthAttribute = utils_float_attribute::Add(InHandle, HealthParams);

		auto ShieldParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Defense"), 200.0f);
		ShieldParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(150.0f);
		ShieldAttribute = utils_float_attribute::Add(InHandle, ShieldParams);
	}

	void
	Request_BindSignals(
		FCk_Handle InHandle)
	{
		utils_float_attribute::BindTo_OnValueChanged(ArmorAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnArmorValueChanged"));
		utils_float_attribute::BindTo_OnMinClamped(ArmorAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnArmorClamped"));
		utils_float_attribute::BindTo_OnMaxClamped(ArmorAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnArmorClamped"));
		utils_float_attribute::BindTo_OnValueChanged(StaminaAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnStaminaValueChanged"));
		utils_float_attribute::BindTo_OnMinClamped(StaminaAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnStaminaClamped"));
		utils_float_attribute::BindTo_OnMaxClamped(StaminaAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnStaminaClamped"));
		utils_float_attribute::BindTo_OnValueChanged(HealthAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnHealthValueChanged"));
		utils_float_attribute::BindTo_OnMinClamped(HealthAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnHealthClamped"));
		utils_float_attribute::BindTo_OnMaxClamped(HealthAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnHealthClamped"));
		utils_float_attribute::BindTo_OnValueChanged(ShieldAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnShieldValueChanged"));
		utils_float_attribute::BindTo_OnMinClamped(ShieldAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnShieldClamped"));
		utils_float_attribute::BindTo_OnMaxClamped(ShieldAttribute, FCk_Delegate_FloatAttribute_OnClamped(this, n"OnShieldClamped"));
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (_PendingMutation)
		{
			_PendingPasses++;
			if (_CompletedMask == 15 && Get_HasExpectedValues()) { _PendingMutation = false; }
		}
		Request_TickFinishTransaction();
		Request_TickLegacyCommands();
		if (!WaitForWorldReady && Get_HasValidAuthorityComposition()) { Request_UpdateDisplay(); }
	}

	UFUNCTION()
	private void UpdateTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != UpdateTimer || _MutationGateClosed || _PendingMutation ||
			!Get_HasValidAuthorityComposition()) { return; }
		if (AutoRunning) { Request_AutoUpdateValues(); }
	}

	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || _MutationGateClosed || _PendingMutation ||
			!Get_HasValidAuthorityComposition()) { return; }
		AutoStep++;
	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 4 : 3);
	}

	void
	Request_AutoUpdateValues()
	{
		if (ArmorIncreasing) { CurrentArmorTest += 30.7f; if (CurrentArmorTest >= 240.0f) ArmorIncreasing = false; }
		else { CurrentArmorTest -= 40.3f; if (CurrentArmorTest <= -20.0f) ArmorIncreasing = true; }

		if (StaminaIncreasing) { CurrentStaminaTest += 25.5f; if (CurrentStaminaTest >= 280.0f) StaminaIncreasing = false; }
		else { CurrentStaminaTest -= 35.25f; if (CurrentStaminaTest <= 20.0f) StaminaIncreasing = true; }

		if (HealthIncreasing) { CurrentHealthTest += 20.1f; if (CurrentHealthTest >= 120.0f) HealthIncreasing = false; }
		else { CurrentHealthTest -= 25.75f; if (CurrentHealthTest <= -10.0f) HealthIncreasing = true; }

		utils_float_attribute::Request_Override(ArmorAttribute, CurrentArmorTest, ECk_MinMaxCurrent::Current);
		utils_float_attribute::Request_Override(StaminaAttribute, CurrentStaminaTest, ECk_MinMaxCurrent::Current);
		utils_float_attribute::Request_Override(HealthAttribute, CurrentHealthTest, ECk_MinMaxCurrent::Current);
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
	{ return !_PendingMutation && Get_HasValidAuthorityComposition(); }
	bool Request_TryBuildPresentation(FString& OutTitle, FString& OutBody)
	{
		OutTitle = ""; OutBody = "";
		if (!Get_HasSettledAuthoritySample()) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "FLOAT CLAMPING (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount} | Clamps: {ClampedCount}\n";

		if (ClampedCount > 0)
		{
			DisplayText = f"{DisplayText}Last Clamp: pre={LastPreClampValue} clamped={LastClampedValue} overflow={LastOverflow}\n";
		}

		// Live polling via utility accessors - shows the Armor attribute's this-frame values.
		auto LivePre = utils_float_attribute::Get_PreClampFinalValue(ArmorAttribute);
		auto LiveOvr = utils_float_attribute::Get_ClampOverflow(ArmorAttribute);
		DisplayText = f"{DisplayText}Live Poll (Armor): pre={LivePre}  overflow={LiveOvr}\n";

		DisplayText = DisplayText + "\n";

		{
			auto ArmorValue = utils_float_attribute::Get_FinalValue(ArmorAttribute);
			auto ArmorBar = CkGym_Attribute::Create_ProgressBar(ArmorValue, 200.0f, 20, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
			auto ClampSuffix = CkGym_Attribute::Get_ClampingSuffix(ArmorValue, CurrentArmorTest);
			DisplayText = f"{DisplayText}Armor: {ArmorValue}/200{ClampSuffix}\n";
			DisplayText = f"{DisplayText}[{ArmorBar}] " + (ArmorIncreasing ? "UP" : "DOWN") + "\n\n";
		}

		{
			auto StaminaValue = utils_float_attribute::Get_FinalValue(StaminaAttribute);
			auto StaminaBar = CkGym_Attribute::Create_ProgressBar((StaminaValue - 50.0f), (255.0f - 50.0f), 20, ECk_ASCII_ProgressBar_Style::Equal_Symbol);
			auto ClampSuffix = CkGym_Attribute::Get_ClampingSuffix(StaminaValue, CurrentStaminaTest);
			DisplayText = f"{DisplayText}Stamina: {StaminaValue}/255{ClampSuffix}\n";
			DisplayText = f"{DisplayText}[{StaminaBar}] " + (StaminaIncreasing ? "UP" : "DOWN") + "\n\n";
		}

		{
			auto HealthValue = utils_float_attribute::Get_FinalValue(HealthAttribute);
			auto HealthBar = CkGym_Attribute::Create_ProgressBar(HealthValue, 100.0f, 20);
			auto ClampSuffix = CkGym_Attribute::Get_ClampingSuffix(HealthValue, CurrentHealthTest);
			DisplayText = f"{DisplayText}Health: {HealthValue}/100{ClampSuffix}\n";
			DisplayText = f"{DisplayText}[{HealthBar}] " + (HealthIncreasing ? "UP" : "DOWN") + "\n\n";
		}

		{
			auto ShieldValue = utils_float_attribute::Get_FinalValue(ShieldAttribute);
			auto ShieldBar = CkGym_Attribute::Create_ProgressBar(ShieldValue, 150.0f, 20, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
			DisplayText = f"{DisplayText}Shield: {ShieldValue}/150 (Initial: 200.0, clamped on creation)\n";
			DisplayText = f"{DisplayText}[{ShieldBar}] STATIC\n\n";
		}

		DisplayText = DisplayText + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		OutTitle = TitleText; OutBody = DisplayText; return true;
	}

	UFUNCTION() void OnArmorValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload) { if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }
	UFUNCTION() void OnStaminaValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload) { if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }
	UFUNCTION() void OnHealthValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload) { if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }
	UFUNCTION() void OnShieldValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnValueChanged InPayload) { if (InAttributeOwnerEntity == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }

	private bool Request_RecordClampEvent(int32 InIndex, FCk_Handle InAttributeOwnerEntity,
		FCk_Payload_FloatAttribute_OnClamped InPayload)
	{
		if (InAttributeOwnerEntity != ck::ToEntity(this)) { return false; }
		if (InIndex < 0 || InIndex >= 4 || ClampEvents.Num() != 4 ||
			!Get_HasValidAuthorityComposition())
		{ _AuthorityFailed = true; return false; }
		FCk_Handle_FloatAttribute ExpectedAttribute = ArmorAttribute;
		if (InIndex == 1) { ExpectedAttribute = StaminaAttribute; }
		else if (InIndex == 2) { ExpectedAttribute = HealthAttribute; }
		else if (InIndex == 3) { ExpectedAttribute = ShieldAttribute; }
		if (InPayload.Get_AttributeEntity() != ExpectedAttribute)
		{ _AuthorityFailed = true; return false; }
		auto PreClamp = InPayload.Get_PreClampFinalValue();
		auto FinalValue = InPayload.Get_FinalClampedValue();
		auto OverflowValue = InPayload.Get_ClampOverflow();
		if (!Get_IsFinite(PreClamp) || !Get_IsFinite(FinalValue) ||
			!Get_IsFinite(OverflowValue) || ClampEvents[InIndex].AttributeIndex != InIndex ||
			ClampEvents[InIndex].Serial < 0 || ClampEvents[InIndex].Serial == 2147483647 ||
			ClampedCount == 2147483647)
		{ _AuthorityFailed = true; return false; }
		FCkAttributeGym_FloatClampEvent Event;
		Event.AttributeIndex = InIndex;
		Event.Serial = ClampEvents[InIndex].Serial + 1;
		Event.PreClampFinal = PreClamp;
		Event.Final = FinalValue;
		Event.Overflow = OverflowValue;
		ClampEvents[InIndex] = Event;
		ClampedCount++;
		LastPreClampValue = PreClamp; LastClampedValue = FinalValue; LastOverflow = OverflowValue;
		return true;
	}

	UFUNCTION() void OnArmorClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{
		if (!Request_RecordClampEvent(0, InAttributeOwnerEntity, InPayload)) { return; }
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(-50.0f, 0.0f, 150.0f), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f)); }
	}

	UFUNCTION() void OnStaminaClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{
		if (!Request_RecordClampEvent(1, InAttributeOwnerEntity, InPayload)) { return; }
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(0.0f, 0.0f, 150.0f), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f)); }
	}

	UFUNCTION() void OnHealthClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{
		if (!Request_RecordClampEvent(2, InAttributeOwnerEntity, InPayload)) { return; }
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(50.0f, 0.0f, 150.0f), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)); }
	}

	UFUNCTION() void OnShieldClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_FloatAttribute_OnClamped InPayload)
	{
		if (!Request_RecordClampEvent(3, InAttributeOwnerEntity, InPayload)) { return; }
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(100.0f, 0.0f, 150.0f), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)); }
	}

	UFUNCTION()
	private void OnTestBoundaries(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacyCommand(0); }
	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacyCommand(1); }
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
		if (!Request_AppendExactMessenger(Self, FCk_Message_AttributeGym_TestBoundaries, Out)) { return false; }
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
		if (_AuthorityFailed || !CkAttributeFloat::IsLiveEntity(Self)) { return false; }
		TArray<FCk_Handle> Messengers;
		if (!Get_HasExactMessengers(Messengers)) { return false; }
		TArray<FCk_Handle> Expected;
		Expected.Add(FCk_Handle(DisplayTimer)); Expected.Add(FCk_Handle(UpdateTimer));
		Expected.Add(FCk_Handle(AutoTimer)); Expected.Add(FCk_Handle(ArmorAttribute));
		Expected.Add(FCk_Handle(StaminaAttribute)); Expected.Add(FCk_Handle(HealthAttribute));
		Expected.Add(FCk_Handle(ShieldAttribute));
		for (auto Handle : Expected)
		{
			if (!CkAttributeFloat::IsLiveEntity(Handle) ||
				utils_entity_lifetime::Get_LifetimeOwner(Handle) != Self) { return false; }
		}
		if (!utils_timer::DoCast(Expected[0]).IsSet() ||
			!utils_timer::DoCast(Expected[1]).IsSet() ||
			!utils_timer::DoCast(Expected[2]).IsSet()) { return false; }
		for (int32 Index = 3; Index < 7; Index++)
		{ if (!utils_float_attribute::DoCast(Expected[Index]).IsSet()) { return false; } }
		for (auto Messenger : Messengers) { Expected.Add(Messenger); }
		int32 Live = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
		{
			if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
			if (!Expected.Contains(Child)) { return false; }
			Live++;
		}
		if (Live != Expected.Num()) { return false; }
		TArray<FCk_Handle_FloatAttribute> Attributes;
		Attributes.Add(ArmorAttribute); Attributes.Add(StaminaAttribute);
		Attributes.Add(HealthAttribute); Attributes.Add(ShieldAttribute);
		TArray<FName> Names;
		Names.Add(n"FloatAttribute.Armor"); Names.Add(n"FloatAttribute.Stamina");
		Names.Add(n"FloatAttribute.Health"); Names.Add(n"FloatAttribute.Defense");
		for (int32 Index = 0; Index < 4; Index++)
		{
			if (utils_float_attribute::TryGet(Self,
				utils_gameplay_tag::ResolveGameplayTag(Names[Index])) != Attributes[Index] ||
				!utils_float_attribute::Has_Component(Attributes[Index], ECk_MinMaxCurrent::Min) ||
				!utils_float_attribute::Has_Component(Attributes[Index], ECk_MinMaxCurrent::Max) ||
				!utils_float_attribute::Has_Component(Attributes[Index], ECk_MinMaxCurrent::Current))
			{ return false; }
		}
		return AutoConfig.TotalSteps == 1 && AutoStep >= 0;
	}
	bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
	UFUNCTION() private void OnAutoResumeCompleted(FCk_Handle InOwner,
		ECk_Request_OperationResult InResult)
	{
		if (!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (InOwner != FCk_Handle(AutoTimer) ||
			InResult != ECk_Request_OperationResult::Succeeded) { _AuthorityFailed = true; }
	}
	UFUNCTION() private void OnUpdateResumeCompleted(FCk_Handle InOwner,
		ECk_Request_OperationResult InResult)
	{
		if (!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (InOwner != FCk_Handle(UpdateTimer) ||
			InResult != ECk_Request_OperationResult::Succeeded) { _AuthorityFailed = true; }
	}
	UFUNCTION() private void OnInitialShieldOverrideCompleted(FCk_Handle InOwner,
		ECk_Request_OperationResult InResult)
	{
		auto World = ck::TransientEntity();
		if (!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this)) ||
			!CkAttributeFloat::IsLiveEntity(World) ||
			CkGym_Admission::Get_IsClosedUnproven(World) ||
			!Get_HasValidAuthorityComposition()) { return; }
		if (!_AuthorityStarted || !_InitialShieldOverrideRequested ||
			_InitialShieldOverrideCompleted || InOwner != FCk_Handle(ShieldAttribute) ||
			InResult != ECk_Request_OperationResult::Succeeded)
		{ _AuthorityFailed = true; return; }
		_InitialShieldOverrideCompleted = true;
	}
	bool Request_StartAuthorityDemo()
	{
		auto World = ck::TransientEntity();
		if (!CkAttributeFloat::IsLiveEntity(World) ||
			CkGym_Admission::Get_IsClosedUnproven(World) ||
			!WaitForWorldReady || _AuthorityStarted || !Get_HasValidAuthorityComposition() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused ||
			UpdateTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }

		_AuthorityStarted = true; _StartAutoSucceeded = true; _StartUpdateSucceeded = true;
		_InitialShieldOverrideRequested = true;
		auto Requested = utils_float_attribute::Request_Override(ShieldAttribute,
			200.0f, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnInitialShieldOverrideCompleted"));
		if (!CkAttributeFloat::IsLiveEntity(World) ||
			CkGym_Admission::Get_IsClosedUnproven(World)) { return false; }
		if (ck::Is_NOT_Valid(Requested) || Requested != ShieldAttribute ||
			!_InitialShieldOverrideCompleted || _AuthorityFailed)
		{ _AuthorityFailed = true; return false; }
		return true;
	}
	bool Get_HasSettledAuthorityStart()
	{
		auto World = ck::TransientEntity();
		return _AuthorityStarted && !_StartReleased && _StartAutoSucceeded && _StartUpdateSucceeded &&
			_InitialShieldOverrideCompleted && CkAttributeFloat::IsLiveEntity(World) &&
			!CkGym_Admission::Get_IsClosedUnproven(World) &&
			Get_HasValidAuthorityComposition() &&
			AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused &&
			UpdateTimer.Get_CurrentState() == ECk_Timer_State::Paused &&
			Get_Near(utils_float_attribute::Get_PreClampFinalValue(ShieldAttribute), 200.0f) &&
			Get_Near(utils_float_attribute::Get_FinalValue(ShieldAttribute), 150.0f) &&
			Get_Near(utils_float_attribute::Get_ClampOverflow(ShieldAttribute), 50.0f);
	}
	bool Request_ReleaseAuthorityStart()
	{
		if (!Get_HasSettledAuthorityStart()) { return false; }
		auto A = utils_timer::Request_Resume(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAutoResumeCompleted"));
		auto B = utils_timer::Request_Resume(UpdateTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnUpdateResumeCompleted"));
		if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B))
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
		TArray<FCk_Handle_FloatAttribute> Attributes;
		Attributes.Add(ArmorAttribute); Attributes.Add(StaminaAttribute);
		Attributes.Add(HealthAttribute); Attributes.Add(ShieldAttribute);
		TArray<float32> Raw;
		TArray<float32> Final;
		if (_ResetPending)
		{
			Raw.Add(100.5f); Raw.Add(150.75f); Raw.Add(75.25f); Raw.Add(200.0f);
			Final.Add(100.5f); Final.Add(150.75f); Final.Add(75.25f); Final.Add(150.0f);
		}
		else
		{
			Raw.Add(999.9f); Raw.Add(-50.5f); Raw.Add(150.75f); Raw.Add(999.0f);
			Final.Add(200.0f); Final.Add(50.0f); Final.Add(100.0f); Final.Add(150.0f);
		}
		for (int32 Index = 0; Index < 4; Index++)
		{
			if (!Get_Near(utils_float_attribute::Get_PreClampFinalValue(Attributes[Index]), Raw[Index]) ||
				!Get_Near(utils_float_attribute::Get_FinalValue(Attributes[Index]), Final[Index]))
			{ return false; }
		}
		return true;
	}
	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false; _MutationGateClosed = true;
		if (_PendingMutation) { return false; }
		if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused &&
			UpdateTimer.Get_CurrentState() == ECk_Timer_State::Paused)
		{ _Prepared = !_PrepareRequested || (_PrepareAutoSucceeded && _PrepareUpdateSucceeded);
			return _Prepared; }
		if (_PrepareRequested || AutoTimer.Get_CurrentState() != ECk_Timer_State::Running ||
			UpdateTimer.Get_CurrentState() != ECk_Timer_State::Running) { return false; }
		_PrepareRequested = true; _PrepareAutoSucceeded = false; _PrepareUpdateSucceeded = false;
		auto A = utils_timer::Request_Pause(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnPrepareAutoCompleted"));
		auto B = utils_timer::Request_Pause(UpdateTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnPrepareUpdateCompleted"));
		if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B)) { _AuthorityFailed = true; }
		return false;
	}
	UFUNCTION() private void OnPrepareAutoCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_PrepareRequested || InOwner != FCk_Handle(AutoTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_PrepareAutoSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_PrepareAutoSucceeded) { _AuthorityFailed = true; }
	}
	UFUNCTION() private void OnPrepareUpdateCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_PrepareRequested || InOwner != FCk_Handle(UpdateTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_PrepareUpdateSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
		if (!_PrepareUpdateSucceeded) { _AuthorityFailed = true; }
	}
	bool Get_IsAuthorityTransactionPrepared()
	{ return _Prepared && !_PendingMutation && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused &&
		UpdateTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	private bool Request_ApplyOverrides(bool InReset)
	{
		_CompletedMask = 0; _PendingPasses = 0; _PendingMutation = true;
		auto A = utils_float_attribute::Request_Override(ArmorAttribute,
			InReset ? 100.5f : 999.9f, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnArmorOverrideCompleted"));
		auto B = utils_float_attribute::Request_Override(StaminaAttribute,
			InReset ? 150.75f : -50.5f, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnStaminaOverrideCompleted"));
		auto C = utils_float_attribute::Request_Override(HealthAttribute,
			InReset ? 75.25f : 150.75f, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnHealthOverrideCompleted"));
		auto D = utils_float_attribute::Request_Override(ShieldAttribute,
			InReset ? 200.0f : 999.0f, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnShieldOverrideCompleted"));
		if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B) || ck::Is_NOT_Valid(C) || ck::Is_NOT_Valid(D))
		{ _AuthorityFailed = true; return false; }
		return true;
	}
	UFUNCTION() private void OnArmorOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, FCk_Handle(ArmorAttribute), 1); }
	UFUNCTION() private void OnStaminaOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, FCk_Handle(StaminaAttribute), 2); }
	UFUNCTION() private void OnHealthOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, FCk_Handle(HealthAttribute), 4); }
	UFUNCTION() private void OnShieldOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordOverride(InOwner, InResult, FCk_Handle(ShieldAttribute), 8); }
	private void Request_RecordOverride(FCk_Handle InOwner, ECk_Request_OperationResult InResult,
		FCk_Handle InExpected, int32 InBit)
	{
		if (!_PendingMutation || InOwner != InExpected || (_CompletedMask & InBit) != 0 ||
			InResult != ECk_Request_OperationResult::Succeeded)
		{ _AuthorityFailed = true; return; }
		_CompletedMask |= InBit;
	}
	bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
	{
		if (InAction != 0 || InValue != 0.0f || !Get_IsAuthorityTransactionPrepared() ||
			_ControlPending || _ResetPending) { return false; }
		if (BoundaryEventSerial < 0 || BoundaryEventSerial == 2147483647)
		{ _AuthorityFailed = true; return false; }
		AutoRunning = false; _ControlPending = true;
		if (!WaitForWorldReady)
		{ CkGym_Common::Draw_DebugSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 300.0f),
			FLinearColor(1.0f, 1.0f, 0.0f, 1.0f), 25.0f, 3.0f, 2.0f); }
		if (!Request_ApplyOverrides(false)) { return false; }
		BoundaryEventSerial++;
		return true;
	}
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_PendingMutation && _CompletedMask == 15 &&
		Get_IsAuthorityTransactionPrepared() && Get_HasExpectedValues(); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
		AutoStep = 0; ValueChangeCount = 0; ClampedCount = 0;
		LastPreClampValue = 0.0f; LastClampedValue = 0.0f; LastOverflow = 0.0f;
		CurrentArmorTest = 100.5f; CurrentStaminaTest = 150.75f; CurrentHealthTest = 75.25f;
		ArmorIncreasing = true; StaminaIncreasing = true; HealthIncreasing = false;
		AutoRunning = true; _ResetPending = true;
		return Request_ApplyOverrides(true);
	}
	bool Get_HasSettledAuthorityReset()
	{ return _ResetPending && !_PendingMutation && _CompletedMask == 15 &&
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
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused ||
			UpdateTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return; }
		_ControlPending = false; _ResetPending = false; _Prepared = false;
		_PrepareRequested = false; _PrepareAutoSucceeded = false; _PrepareUpdateSucceeded = false;
		_MutationGateClosed = true; AutoRunning = _FinishResume;
		_FinishPending = false; _FinishCompleted = true;
	}
	bool Get_IsAuthorityFinishPending() const { return _FinishPending; }
	bool Get_HasFinishedAuthorityTransaction() const { return _FinishCompleted; }
	bool Get_CanReleaseAuthorityTransaction()
	{ return WaitForWorldReady && _FinishCompleted && !_FinishReleased && !_FinishPending &&
		!_AuthorityFailed && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused &&
		UpdateTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	private bool Request_ReleaseMutatorTimers()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		if (_FinishResume)
		{
			auto A = utils_timer::Request_Resume(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAutoResumeCompleted"));
			auto B = utils_timer::Request_Resume(UpdateTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnUpdateResumeCompleted"));
			if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B)) { return false; }
		}
		return true;
	}
	bool Request_ReleaseAuthorityTransaction()
	{
		if (!Get_CanReleaseAuthorityTransaction()) { return false; }
		if (!Request_ReleaseMutatorTimers()) { _AuthorityFailed = true; return false; }
		_FinishReleased = true; _MutationGateClosed = !_FinishResume; return true;
	}
	private bool Request_QueueLegacyCommand(int32 InAction)
	{
		if (WaitForWorldReady || InAction < 0 || InAction > 4) { return false; }
		_LegacyActions.Add(InAction); return true;
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
			if (!Request_ReleaseMutatorTimers()) { _AuthorityFailed = true; return; }
			_FinishCompleted = false; _MutationGateClosed = !AutoRunning;
			_LegacyActions.RemoveAt(0); _LegacyStarted = false; return;
		}
		if (!Request_TryPrepareAuthorityTransaction()) { return; }
		auto Action = _LegacyActions[0];
		_LegacyResumeAuto = AutoRunning;
		if (Action == 1) { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; return; } }
		else if (Action == 0)
		{ if (!Request_ApplyAuthorityControl(0, 0.0f)) { _AuthorityFailed = true; return; } }
		else if (!Request_BeginFinishAuthorityTransaction(Action == 4))
		{ _AuthorityFailed = true; return; }
		_LegacyStarted = true;
	}

}
