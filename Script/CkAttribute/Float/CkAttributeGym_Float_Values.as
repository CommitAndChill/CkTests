//============================================================================
// FLOAT ATTRIBUTE VALUE RETRIEVAL STATION
//============================================================================

class UCk_EntityScript_AttributeGym_FloatValues : UCk_GenericEntityScript_UE
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
	private bool _PendingAutoMutation = false;
	private int32 _PendingPasses = 0;
	private int32 _ExpectedModifierCount = 0;
	private int32 _CompletedOverrideMask = 0;
	private TArray<FCk_Handle_FloatAttributeModifier> _RetiringModifiers;
	private TArray<FCk_Handle_FloatAttributeModifier> _PermanentOverrides; // Min, Max, Current
	private TArray<bool> _PermanentOverrideCreated;
	private TArray<int32> _LegacyActions;
	private bool _LegacyStarted = false;
	private bool _LegacyResumeAuto = false;

	FCk_Handle_FloatAttribute TestAttribute;
	TArray<FCk_Handle_FloatAttributeModifier> ActiveModifiers;

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
		_PermanentOverrides.Empty();
		_PermanentOverrideCreated.Empty();
		for (int32 i = 0; i < 3; i++)
		{
			_PermanentOverrides.Add(FCk_Handle_FloatAttributeModifier());
			_PermanentOverrideCreated.Add(false);
		}
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatValues");

		// Display timer
		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		// Auto timer
		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(2.8f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			_MutationGateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(2.8f)); }

		// Auto config
		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests Base/Bonus/Final retrieval, percentage, and magnitude.\nShows calculation breakdown with live modifiers.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [1] Values station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Add base modifiers (+20.5, +15.25)", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Min/Max/Current components", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test all retrieval methods", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Add more modifiers (+12.75)", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Clear all modifiers", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Reset components to defaults", 5, 5));
		AutoConfig.ManualCommands.Add("panel [J] Modifiers ring - add modifier / clear modifiers");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(TestAttribute)) { _AuthorityFailed = true; return; }
		Request_BindSignals(InHandle);

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_AddModifier,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnAddModifier"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_ClearModifiers,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnClearModifiers"));
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto TestParams = FCk_FloatAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Test"), 75.5f);
		TestParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(5.0f).Set_MaxValue(180.0f);
		TestAttribute = utils_float_attribute::Add(InHandle, TestParams);
	}

	void
	Request_BindSignals(
		FCk_Handle InHandle)
	{
		utils_float_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Min,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
		utils_float_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Max,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
		utils_float_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnValueChanged"));
	}

	UFUNCTION()
	private void
	DisplayTick(
		FCk_Handle_Timer InHandle,
		FCk_Chrono InChrono,
		FCk_Time InDeltaT)
	{
		if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		if (_PendingMutation)
		{
			_PendingPasses++;
			if (Get_HasSettledModifierMutation())
			{ _PendingMutation = false; _PendingAutoMutation = false; }
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

		if (Step == 0)
		{
			auto Before = ActiveModifiers.Num(); Request_AddModifiers();
			if (ActiveModifiers.Num() != Before + 2) { _AuthorityFailed = true; return; }
		}
		else if (Step == 1) { if (!Request_ModifyComponents()) { _AuthorityFailed = true; return; } }
		else if (Step == 2) { /* Values retrieved in display */ }
		else if (Step == 3)
		{
			auto Before = ActiveModifiers.Num(); Request_AddMoreModifiers();
			if (ActiveModifiers.Num() != Before + 1) { _AuthorityFailed = true; return; }
		}
		else if (Step == 4) { Request_ClearModifiers(); }
		else if (Step == 5) { if (!Request_ResetComponents()) { _AuthorityFailed = true; return; } }

		AutoStep++;
		if (Step == 0 || Step == 1 || Step == 3 || Step == 4 || Step == 5)
		{
			_ExpectedModifierCount = ActiveModifiers.Num(); _PendingPasses = 0;
			_PendingMutation = true; _PendingAutoMutation = true;
		}
	}

	UFUNCTION()
	private void
	OnAutoSet(
		FCk_Handle InHandle,
		FGameplayTag InMessageName,
		FInstancedStruct InPayload)
	{
		if (WaitForWorldReady) { return; }
		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 4 : 3);
	}

	void
	Request_AddModifiers()
	{
		auto WeaponParams = FCk_FloatAttributeModifier_Spec();
		WeaponParams.Set_ModifierDelta(20.5f);

		auto WeaponMod = utils_float_attribute_modifier::Add_Revocable(
			TestAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"),
			ECk_AttributeModifier_Operation::Add,
			WeaponParams);

		if (ck::IsValid(WeaponMod))
		{
			ActiveModifiers.Add(WeaponMod);
		}

		auto BuffParams = FCk_FloatAttributeModifier_Spec();
		BuffParams.Set_ModifierDelta(15.25f);

		auto BuffMod = utils_float_attribute_modifier::Add_Revocable(
			TestAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"),
			ECk_AttributeModifier_Operation::Add,
			BuffParams);

		if (ck::IsValid(BuffMod))
		{
			ActiveModifiers.Add(BuffMod);
		}
	}

	bool Request_ModifyComponents()
	{
		return Request_TrackOverride(ECk_MinMaxCurrent::Min, 8.0f, FCk_Delegate_Request_OnCompleted()) &&
			Request_TrackOverride(ECk_MinMaxCurrent::Max, 200.0f, FCk_Delegate_Request_OnCompleted()) &&
			Request_TrackOverride(ECk_MinMaxCurrent::Current, 90.5f, FCk_Delegate_Request_OnCompleted());
	}

	void
	Request_AddMoreModifiers()
	{
		auto EnchantParams = FCk_FloatAttributeModifier_Spec();
		EnchantParams.Set_ModifierDelta(12.75f);

		auto EnchantMod = utils_float_attribute_modifier::Add_Revocable(
			TestAttribute,
			utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Enchant"),
			ECk_AttributeModifier_Operation::Add,
			EnchantParams);

		if (ck::IsValid(EnchantMod))
		{
			ActiveModifiers.Add(EnchantMod);
		}
	}

	void
	Request_ClearModifiers()
	{
		_RetiringModifiers = ActiveModifiers;
		utils_float_attribute_modifier::Request_ClearAllModifiers(TestAttribute, ECk_MinMaxCurrent::Current);
		ActiveModifiers.Empty();
	}

	bool Request_ResetComponents()
	{
		return Request_TrackOverride(ECk_MinMaxCurrent::Min, 5.0f, FCk_Delegate_Request_OnCompleted()) &&
			Request_TrackOverride(ECk_MinMaxCurrent::Max, 180.0f, FCk_Delegate_Request_OnCompleted()) &&
			Request_TrackOverride(ECk_MinMaxCurrent::Current, 75.5f, FCk_Delegate_Request_OnCompleted());
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
	{
		if (_PendingMutation || !Get_HasValidAuthorityComposition()) { return false; }
		if (_PendingPasses != 0) { return Get_HasSettledModifierMutation(); }
		if (ActiveModifiers.Num() != 0 || _RetiringModifiers.Num() != 0) { return false; }
		auto Bonus = utils_float_attribute::Get_BonusValue(TestAttribute);
		return Get_IsFinite(Bonus) && Math::Abs(Bonus) <= 0.001f;
	}
	bool Request_TryBuildPresentation(FString& OutTitle, FString& OutBody)
	{
		OutTitle = ""; OutBody = "";
		if (!Get_HasSettledAuthoritySample()) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "FLOAT VALUES (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n\n";

		auto MinBase = utils_float_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Min);
		auto MaxBase = utils_float_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Max);
		auto CurrentBase = utils_float_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentBonus = utils_float_attribute::Get_BonusValue(TestAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentFinal = utils_float_attribute::Get_FinalValue(TestAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}COMPONENT VALUES:\n";
		DisplayText = f"{DisplayText}  Min Base: {MinBase}\n";
		DisplayText = f"{DisplayText}  Max Base: {MaxBase}\n";
		DisplayText = f"{DisplayText}  Current Base: {CurrentBase}\n";
		DisplayText = f"{DisplayText}  Current Bonus: {CurrentBonus}\n";
		DisplayText = f"{DisplayText}  Current Final: {CurrentFinal}\n\n";

		DisplayText = f"{DisplayText}CALCULATION:\n";
		DisplayText = f"{DisplayText}  {CurrentBase} + {CurrentBonus} = {CurrentFinal}\n\n";

		// Float-specific: percentage and magnitude (exercise both overloads)
		auto PercentByHandle = utils_float_attribute::Get_Value_AsPercentage(TestAttribute);
		auto TestTag = utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Test");
		auto PercentByName = utils_float_attribute::Get_Value_AsPercentage(SelfEntity, TestTag);
		auto Magnitude = utils_float_attribute::Calculate_Attribute_Magnitude(TestAttribute);
		DisplayText = f"{DisplayText}FLOAT-SPECIFIC:\n";
		DisplayText = f"{DisplayText}  Percentage (by handle): {PercentByHandle * 100.0f}%\n";
		DisplayText = f"{DisplayText}  Percentage (by name):   {PercentByName * 100.0f}%\n";
		DisplayText = f"{DisplayText}  Magnitude (Max-Min): {Magnitude}\n\n";

		// Progress bar
		auto MinFinal = utils_float_attribute::Get_FinalValue(TestAttribute, ECk_MinMaxCurrent::Min);
		auto MaxFinal = utils_float_attribute::Get_FinalValue(TestAttribute, ECk_MinMaxCurrent::Max);
		auto ProgressPercent = (CurrentFinal - MinFinal) / (MaxFinal - MinFinal);
		auto ValueBar = CkGym_Attribute::Create_ProgressBar(ProgressPercent * 100.0f, 100.0f, 25);
		DisplayText = f"{DisplayText}[{ValueBar}]\n";
		DisplayText = f"{DisplayText}  {MinFinal} <--- {CurrentFinal} ---> {MaxFinal}\n\n";

		// Active modifiers
		DisplayText = f"{DisplayText}MODIFIERS ({ActiveModifiers.Num()}):\n";
		for (auto i = 0; i < ActiveModifiers.Num(); i++)
		{
			auto Delta = utils_float_attribute_modifier::Get_Delta(ActiveModifiers[i]);
			DisplayText = f"{DisplayText}  Mod {i+1}: +{Delta}\n";
		}

		if (ActiveModifiers.Num() == 0)
		{
			DisplayText = f"{DisplayText}  No active modifiers\n";
		}

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		OutTitle = TitleText; OutBody = DisplayText; return true;
	}

	UFUNCTION()
	void
	OnValueChanged(
		FCk_Handle InAttributeOwnerEntity,
		FCk_Payload_FloatAttribute_OnValueChanged InPayload)
	{
		if (InAttributeOwnerEntity == ck::ToEntity(this) &&
			CkAttributeFloat::IsLiveEntity(FCk_Handle(TestAttribute)) &&
			utils_entity_lifetime::Has_LifetimeOwner(FCk_Handle(TestAttribute)) &&
			utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(TestAttribute)) == ck::ToEntity(this))
		{ ValueChangeCount++; }
	}

	UFUNCTION()
	private void
	OnAddModifier(
		FCk_Handle InHandle,
		FGameplayTag InMessageName,
		FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacyCommand(0); }
	}

	UFUNCTION()
	private void
	OnClearModifiers(
		FCk_Handle InHandle,
		FGameplayTag InMessageName,
		FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacyCommand(1); }
	}

	UFUNCTION()
	private void
	OnResetAttributes(
		FCk_Handle InHandle,
		FGameplayTag InMessageName,
		FInstancedStruct InPayload)
	{
		if (!WaitForWorldReady) { Request_QueueLegacyCommand(2); }
	}
	private bool Request_AppendExactMessenger(FCk_Handle InOwner, UScriptStruct InMessageTemplate,
		TArray<FCk_Handle>& InOutExpected)
	{
		if (!CkAttributeFloat::IsLiveEntity(InOwner) || !ck::IsValid(InMessageTemplate)) { return false; }
		auto Messenger = utils_messaging::TryGet_MessengerEntity(InOwner, InMessageTemplate);
		if (!CkAttributeFloat::IsLiveEntity(Messenger) ||
			!utils_entity_lifetime::Has_LifetimeOwner(Messenger) ||
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
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_AddModifier, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_ClearModifiers, Out)) { return false; }
		if (!WaitForWorldReady &&
			!Request_AppendExactMessenger(Self, FCk_Message_Gym_AutoSet, Out))
		{ return false; }
		return true;
	}
	private int32 Get_ComponentIndex(ECk_MinMaxCurrent InComponent)
	{
		if (InComponent == ECk_MinMaxCurrent::Min) { return 0; }
		if (InComponent == ECk_MinMaxCurrent::Max) { return 1; }
		if (InComponent == ECk_MinMaxCurrent::Current) { return 2; }
		return -1;
	}
	private int32 Get_ModifierComponent(FCk_Handle InHandle)
	{
		if (!CkAttributeFloat::IsLiveEntity(InHandle)) { return -1; }
		bool Min = utils_float_attribute_modifier::Has_Component(InHandle, ECk_MinMaxCurrent::Min);
		bool Max = utils_float_attribute_modifier::Has_Component(InHandle, ECk_MinMaxCurrent::Max);
		bool Current = utils_float_attribute_modifier::Has_Component(InHandle, ECk_MinMaxCurrent::Current);
		if ((Min ? 1 : 0) + (Max ? 1 : 0) + (Current ? 1 : 0) != 1) { return -1; }
		return Min ? 0 : Max ? 1 : 2;
	}
	private bool Get_IsTrackedRetiringModifier(FCk_Handle InHandle)
	{
		for (auto Old : _RetiringModifiers)
		{ if (FCk_Handle(Old) == InHandle) { return true; } }
		return false;
	}
	// Record and direct-child identities must agree. The three ForEach calls can expose the
	// same backing record; classify each entry by its actual component before counting it.
	private bool Request_TryCollectExactModifierTree(bool InAllowRetiring,
		bool InAllowOneNew, ECk_MinMaxCurrent InNewComponent,
		TArray<FCk_Handle>& OutAll, FCk_Handle_FloatAttributeModifier& OutNew)
	{
		OutAll.Empty(); OutNew = FCk_Handle_FloatAttributeModifier();
		auto Attribute = FCk_Handle(TestAttribute);
		if (!CkAttributeFloat::IsLiveEntity(Attribute) ||
			!utils_float_attribute::DoCast(Attribute).IsSet() ||
			_PermanentOverrides.Num() != 3 || _PermanentOverrideCreated.Num() != 3) { return false; }
		auto Known = TArray<FCk_Handle>();
		for (auto Modifier : ActiveModifiers)
		{
			auto Handle = FCk_Handle(Modifier);
			if (!CkAttributeFloat::IsLiveEntity(Handle) || Known.Contains(Handle) ||
				!utils_entity_lifetime::Has_LifetimeOwner(Handle) ||
				utils_entity_lifetime::Get_LifetimeOwner(Handle) != Attribute ||
				Get_ModifierComponent(Handle) != 2 || !utils_gameplay_label::Has(Handle) ||
				utils_float_attribute_modifier::TryGet(TestAttribute,
					utils_gameplay_label::Get_Label(Handle), ECk_MinMaxCurrent::Current) != Modifier)
			{ return false; }
			Known.Add(Handle);
		}
		for (int32 i = 0; i < 3; i++)
		{
			auto Handle = FCk_Handle(_PermanentOverrides[i]);
			if (!_PermanentOverrideCreated[i])
			{
				if (Handle != FCk_Handle()) { return false; }
				continue;
			}
			if (!CkAttributeFloat::IsLiveEntity(Handle) || Known.Contains(Handle) ||
				!utils_entity_lifetime::Has_LifetimeOwner(Handle) ||
				utils_entity_lifetime::Get_LifetimeOwner(Handle) != Attribute ||
				Get_ModifierComponent(Handle) != i || utils_gameplay_label::Has(Handle))
			{ return false; }
			Known.Add(Handle);
		}
		if (InAllowRetiring)
		{
			auto SeenRetiring = TArray<FCk_Handle>();
			for (auto Modifier : _RetiringModifiers)
			{
				auto Handle = FCk_Handle(Modifier);
				if (Handle == FCk_Handle() || SeenRetiring.Contains(Handle) || Known.Contains(Handle))
				{ return false; }
				SeenRetiring.Add(Handle);
				if (!CkAttributeFloat::IsLiveEntity(Handle)) { continue; }
				if (!utils_entity_lifetime::Has_LifetimeOwner(Handle) ||
					utils_entity_lifetime::Get_LifetimeOwner(Handle) != Attribute ||
					Get_ModifierComponent(Handle) != 2 || !utils_gameplay_label::Has(Handle))
				{ return false; }
				Known.Add(Handle);
			}
		}
		else
		{
			for (auto Old : _RetiringModifiers)
			{ if (CkAttributeFloat::IsLiveEntity(FCk_Handle(Old))) { return false; } }
		}
		auto Records = TArray<FCk_Handle>();
		for (int32 i = 0; i < 3; i++)
		{
			ECk_MinMaxCurrent Component = i == 0 ? ECk_MinMaxCurrent::Min :
				i == 1 ? ECk_MinMaxCurrent::Max : ECk_MinMaxCurrent::Current;
			auto Entries = utils_float_attribute_modifier::ForEach(TestAttribute,
				FInstancedStruct(), FCk_Lambda_InHandle(), Component);
			for (auto Modifier : Entries)
			{
				auto Handle = FCk_Handle(Modifier);
				// ForEach_ValidEntry includes BeginDestroy. Only previously captured retiring
				// identities may be skipped without reading fragments on the dying entity.
				if (InAllowRetiring && Get_IsTrackedRetiringModifier(Handle) &&
					!CkAttributeFloat::IsLiveEntity(Handle)) { continue; }
				if (!CkAttributeFloat::IsLiveEntity(Handle) ||
					!utils_entity_lifetime::Has_LifetimeOwner(Handle) ||
					utils_entity_lifetime::Get_LifetimeOwner(Handle) != Attribute)
				{ return false; }
				auto ActualComponent = Get_ModifierComponent(Handle);
				if (ActualComponent < 0) { return false; }
				if (ActualComponent != i) { continue; }
				if (Records.Contains(Handle)) { return false; }
				Records.Add(Handle);
				if (!Known.Contains(Handle))
				{
					if (!InAllowOneNew || ActualComponent != Get_ComponentIndex(InNewComponent) ||
						utils_gameplay_label::Has(Handle) || ck::IsValid(OutNew)) { return false; }
					OutNew = Modifier;
				}
			}
		}
		int32 DirectCount = 0;
		for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Attribute))
		{
			if (!CkAttributeFloat::IsLiveEntity(Child))
			{
				if (!InAllowRetiring || !Get_IsTrackedRetiringModifier(Child)) { return false; }
				continue;
			}
			if (!Records.Contains(Child) ||
				!utils_entity_lifetime::Has_LifetimeOwner(Child) ||
				utils_entity_lifetime::Get_LifetimeOwner(Child) != Attribute)
			{ return false; }
			DirectCount++;
		}
		if (DirectCount != Records.Num() ||
			Records.Num() != Known.Num() + (ck::IsValid(OutNew) ? 1 : 0)) { return false; }
		for (auto Handle : Known) { if (!Records.Contains(Handle)) { return false; } }
		OutAll = Records; return true;
	}
	private bool Request_TrackOverride(ECk_MinMaxCurrent InComponent, float32 InValue,
		FCk_Delegate_Request_OnCompleted InCompletion)
	{
		int32 Index = Get_ComponentIndex(InComponent);
		if (Index < 0 || !CkAttributeFloat::IsLiveEntity(FCk_Handle(TestAttribute)) ||
			!utils_float_attribute::Has_Component(TestAttribute, InComponent) ||
			InValue != InValue || Math::Abs(InValue) >= 1.0e20f)
		{ _AuthorityFailed = true; return false; }
		bool AllowRetiring = _PendingMutation || _ResetPending;
		auto Before = TArray<FCk_Handle>();
		FCk_Handle_FloatAttributeModifier Unexpected;
		if (!Request_TryCollectExactModifierTree(AllowRetiring, false,
			InComponent, Before, Unexpected)) { _AuthorityFailed = true; return false; }
		bool Existing = _PermanentOverrideCreated[Index];
		if (utils_float_attribute::Request_Override(TestAttribute, InValue,
			InComponent, InCompletion) != TestAttribute)
		{ _AuthorityFailed = true; return false; }
		auto After = TArray<FCk_Handle>();
		FCk_Handle_FloatAttributeModifier NewModifier;
		if (!Request_TryCollectExactModifierTree(AllowRetiring, true,
			InComponent, After, NewModifier)) { _AuthorityFailed = true; return false; }
		if (After.Num() != Before.Num() + (Existing ? 0 : 1) ||
			ck::IsValid(NewModifier) == Existing)
		{ _AuthorityFailed = true; return false; }
		for (auto Handle : Before)
		{ if (!After.Contains(Handle)) { _AuthorityFailed = true; return false; } }
		if (!Existing)
		{
			_PermanentOverrides[Index] = NewModifier;
			_PermanentOverrideCreated[Index] = true;
		}
		return true;
	}
	bool Get_HasLiveAuthorityDependencies()
	{
		auto Self = ck::ToEntity(this);
		auto Display = FCk_Handle(DisplayTimer);
		auto Auto = FCk_Handle(AutoTimer);
		auto Attribute = FCk_Handle(TestAttribute);
		if (_AuthorityFailed || !CkAttributeFloat::IsLiveEntity(Self) ||
			!CkAttributeFloat::IsLiveEntity(Display) || !CkAttributeFloat::IsLiveEntity(Auto) ||
			!CkAttributeFloat::IsLiveEntity(Attribute)) { return false; }
		if (Display == Auto || Display == Attribute || Auto == Attribute ||
			!utils_entity_lifetime::Has_LifetimeOwner(Display) ||
			!utils_entity_lifetime::Has_LifetimeOwner(Auto) ||
			!utils_entity_lifetime::Has_LifetimeOwner(Attribute) ||
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
		if (Live != Expected.Num() || utils_float_attribute::TryGet(Self,
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Test")) != TestAttribute ||
			!utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Min) ||
			!utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Max) ||
			!utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Current) ||
			AutoConfig.TotalSteps != 6 || AutoStep < 0) { return false; }
		if (!_PendingMutation) { return Get_HasValidAuthorityComposition(); }
		if (!WaitForWorldReady || !_AuthorityStarted ||
			!(_ControlPending || _ResetPending || _PendingAutoMutation)) { return false; }
		auto Exact = TArray<FCk_Handle>();
		FCk_Handle_FloatAttributeModifier Unexpected;
		return Request_TryCollectExactModifierTree(true, false,
			ECk_MinMaxCurrent::Current, Exact, Unexpected);
	}
	bool Get_HasValidAuthorityComposition()
	{
		auto Self = ck::ToEntity(this);
		auto Display = FCk_Handle(DisplayTimer);
		auto Auto = FCk_Handle(AutoTimer);
		auto Attribute = FCk_Handle(TestAttribute);
		if (_AuthorityFailed || !CkAttributeFloat::IsLiveEntity(Self) ||
			!CkAttributeFloat::IsLiveEntity(Display) || !CkAttributeFloat::IsLiveEntity(Auto) ||
			!CkAttributeFloat::IsLiveEntity(Attribute)) { return false; }
		if (!utils_entity_lifetime::Has_LifetimeOwner(Display) ||
			!utils_entity_lifetime::Has_LifetimeOwner(Auto) ||
			!utils_entity_lifetime::Has_LifetimeOwner(Attribute) ||
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
		if (Live != Expected.Num() || utils_float_attribute::TryGet(Self,
			utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Test")) != TestAttribute ||
			!utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Min) ||
			!utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Max) ||
			!utils_float_attribute::Has_Component(TestAttribute, ECk_MinMaxCurrent::Current))
		{ return false; }
		auto Exact = TArray<FCk_Handle>();
		FCk_Handle_FloatAttributeModifier Unexpected;
		if (!Request_TryCollectExactModifierTree(false, false,
			ECk_MinMaxCurrent::Current, Exact, Unexpected)) { return false; }
		return AutoConfig.TotalSteps == 6 && AutoStep >= 0;
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
	private bool Get_HasSettledModifierMutation()
	{
		if (_PendingPasses < 2 || !Get_HasValidAuthorityComposition() ||
			ActiveModifiers.Num() != _ExpectedModifierCount) { return false; }
		for (auto Old : _RetiringModifiers)
		{ if (ck::IsValid(Old)) { return false; } }
		float32 Bonus = 0.0f;
		for (auto Modifier : ActiveModifiers)
		{
			auto Delta = utils_float_attribute_modifier::Get_Delta(Modifier);
			if (!Get_IsFinite(Delta)) { return false; }
			Bonus += Delta;
		}
		return Get_IsFinite(Bonus) && Get_IsFinite(utils_float_attribute::Get_FinalValue(TestAttribute)) &&
			Math::Abs(utils_float_attribute::Get_BonusValue(TestAttribute) - Bonus) <= 0.001f;
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
	bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
			(InAction != 0 && InAction != 1) || InValue != 0.0f) { return false; }
		AutoRunning = false; _ControlPending = true; _PendingPasses = 0;
		_RetiringModifiers.Empty();
		if (InAction == 0)
		{
			auto Weapon = utils_float_attribute_modifier::TryGet(TestAttribute,
				utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
			auto Buff = utils_float_attribute_modifier::TryGet(TestAttribute,
				utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
			if (ck::IsValid(Weapon) != ck::IsValid(Buff)) { _AuthorityFailed = true; return false; }
			if (ck::Is_NOT_Valid(Weapon))
			{
				auto Before = ActiveModifiers.Num(); Request_AddModifiers();
				if (ActiveModifiers.Num() != Before + 2)
				{ _AuthorityFailed = true; return false; }
			}
		}
		else { Request_ClearModifiers(); }
		_ExpectedModifierCount = ActiveModifiers.Num();
		_PendingAutoMutation = false; _PendingMutation = true;
		return true;
	}
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_PendingMutation && Get_IsAuthorityTransactionPrepared() &&
		Get_HasSettledModifierMutation(); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
		AutoStep = 0; ValueChangeCount = 0; AutoRunning = true;
		_ResetPending = true; _PendingPasses = 0;
		Request_ClearModifiers(); _ExpectedModifierCount = 0;
		_PendingAutoMutation = false; _PendingMutation = true;
		_CompletedOverrideMask = 0;
		if (!Request_TrackOverride(ECk_MinMaxCurrent::Min, 5.0f,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetMinCompleted")) ||
			!Request_TrackOverride(ECk_MinMaxCurrent::Max, 180.0f,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetMaxCompleted")) ||
			!Request_TrackOverride(ECk_MinMaxCurrent::Current, 75.5f,
			FCk_Delegate_Request_OnCompleted(this, n"OnResetCurrentCompleted")))
		{ _AuthorityFailed = true; return false; }
		return true;
	}
	UFUNCTION() private void OnResetMinCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordResetOverride(InOwner, InResult, 1); }
	UFUNCTION() private void OnResetMaxCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordResetOverride(InOwner, InResult, 2); }
	UFUNCTION() private void OnResetCurrentCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ Request_RecordResetOverride(InOwner, InResult, 4); }
	private void Request_RecordResetOverride(FCk_Handle InOwner,
		ECk_Request_OperationResult InResult, int32 InBit)
	{
		if (!_ResetPending || InOwner != FCk_Handle(TestAttribute) ||
			(_CompletedOverrideMask & InBit) != 0 ||
			InResult != ECk_Request_OperationResult::Succeeded)
		{ _AuthorityFailed = true; return; }
		_CompletedOverrideMask |= InBit;
	}
	bool Get_HasSettledAuthorityReset()
	{
		return _ResetPending && _CompletedOverrideMask == 7 && !_PendingMutation &&
			Get_IsAuthorityTransactionPrepared() &&
			Get_HasSettledModifierMutation() &&
			Math::Abs(utils_float_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Min) - 5.0f) <= 0.001f &&
			Math::Abs(utils_float_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Max) - 180.0f) <= 0.001f &&
			Math::Abs(utils_float_attribute::Get_PreClampFinalValue(TestAttribute) - 75.5f) <= 0.001f;
	}
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
		if (Action == 2) { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; return; } }
		else if (Action <= 1)
		{ if (!Request_ApplyAuthorityControl(Action, 0.0f)) { _AuthorityFailed = true; return; } }
		else if (!Request_BeginFinishAuthorityTransaction(Action == 4))
		{ _AuthorityFailed = true; return; }
		_LegacyStarted = true;
	}

}
