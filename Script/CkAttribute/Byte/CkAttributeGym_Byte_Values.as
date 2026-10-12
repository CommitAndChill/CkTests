//============================================================================
// BYTE ATTRIBUTE VALUE RETRIEVAL STATION
//============================================================================

class UCk_EntityScript_AttributeGym_ByteValues : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
	UPROPERTY(ExposeOnSpawn) int32 StationIndex = 4;
	bool AuthorityConstructed = false;
	bool TransactionPaused = false;
	FCk_Handle _ControlledOverrideTarget;
	bool _ControlledOverrideSucceeded = false;
	TArray<FCk_Handle_ByteAttributeModifier> PendingModifierRetirement;
	bool ModifierControlPending = false;
	bool ModifierResetNeedsWeapon = false;
	bool ModifierReplacementStarted = false;
	int32 PendingExpectedModifierCount = 0;

	FCk_Handle_ByteAttribute TestAttribute;
	TArray<FCk_Handle_ByteAttributeModifier> ActiveModifiers;

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
		AuthorityConstructed = false;
		TransactionPaused = false;
		auto State = CkAttributeByte::Get_State();
		if (!CkAttributeByte::IsLiveEntity(InHandle) || StationIndex != 4 || Generation <= 0 ||
			ck::Is_NOT_Valid(State) ||
			(State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex) == false) ||
			utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeByte::FindStation(StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Invalid Byte station construction lease"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}


		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_ByteValues");

		// Auto timer
		auto AutoTimerParams = FCk_Timer_Spec(FCk_Time(2.8f));
		AutoTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		AutoTimer = utils_timer::Add(InHandle, AutoTimerParams);
		if (CkAttributeByte::IsLiveEntity(FCk_Handle(AutoTimer)))
		{ AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick")); }

		// Auto config
		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests Base/Bonus/Final retrieval across Min/Max/Current components.\nShows calculation breakdown with live modifiers.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [3] Values station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Add base modifiers (+20, +15)", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Min/Max/Current components", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test all retrieval methods", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Add more modifiers (+12)", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Clear all modifiers", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Reset components to defaults", 5, 5));
		AutoConfig.ManualCommands.Add("panel [J] Add modifier - [K] Clear modifiers");

		AuthorityConstructed = CkAttributeByteStation::IsTimer(InHandle, AutoTimer);
		if (!AuthorityConstructed)
		{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte auto timer allocation failed"); }
		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		auto State = CkAttributeByte::Get_State();
		if (!AuthorityConstructed || ck::Is_NOT_Valid(State) ||
			!State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte BeginPlay rejected"); }
			return;
		}

		Request_SetupAttributes(InHandle);
		if (!Get_HasInitialResources())
		{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte initial composition invalid"); return; }
		Request_BindSignals(InHandle);
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto TestParams = FCk_ByteAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"Test.ValueRetrieval"), 75);
		TestParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(5).Set_MaxValue(180);
		TestAttribute = utils_byte_attribute::Add(InHandle, TestParams, ECk_Replication::DoesNotReplicate);
	}

	void
	Request_BindSignals(
		FCk_Handle InHandle)
	{
		utils_byte_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Min,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged"));
		utils_byte_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Max,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged"));
		utils_byte_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged"));
	}



	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || !Get_CanMutate()) { return; }
		if (!Get_HasValidAuthorityComposition()) { Request_FailCurrent("Byte auto composition lost"); return; }

		auto Step = AutoStep % AutoConfig.TotalSteps;

		if (Step == 0) { Request_AddModifiers(); }
		else if (Step == 1) { Request_ModifyComponents(); }
		else if (Step == 2) { /* Values retrieved in display */ }
		else if (Step == 3) { Request_AddMoreModifiers(); }
		else if (Step == 4) { Request_ClearModifiers(); }
		else if (Step == 5) { Request_ResetComponents(); }

		AutoStep++;
	}



	void Request_AddModifiers()
	{
		auto WeaponParams = FCk_ByteAttributeModifier_Spec();
		WeaponParams.Set_ModifierDelta(20);
		auto WeaponMod = utils_byte_attribute_modifier::Add_Revocable(TestAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_AttributeModifier_Operation::Add, WeaponParams);
		if (ck::IsValid(WeaponMod)) { ActiveModifiers.Add(WeaponMod); }

		auto BuffParams = FCk_ByteAttributeModifier_Spec();
		BuffParams.Set_ModifierDelta(15);
		auto BuffMod = utils_byte_attribute_modifier::Add_Revocable(TestAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_AttributeModifier_Operation::Add, BuffParams);
		if (ck::IsValid(BuffMod)) { ActiveModifiers.Add(BuffMod); }
	}

	void Request_ModifyComponents()
	{
		utils_byte_attribute::Request_Override(TestAttribute, 8, ECk_MinMaxCurrent::Min);
		utils_byte_attribute::Request_Override(TestAttribute, 200, ECk_MinMaxCurrent::Max);
		utils_byte_attribute::Request_Override(TestAttribute, 90, ECk_MinMaxCurrent::Current);
	}

	void Request_AddMoreModifiers()
	{
		auto EnchantParams = FCk_ByteAttributeModifier_Spec();
		EnchantParams.Set_ModifierDelta(12);
		auto EnchantMod = utils_byte_attribute_modifier::Add_Revocable(TestAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Enchant"), ECk_AttributeModifier_Operation::Add, EnchantParams);
		if (ck::IsValid(EnchantMod)) { ActiveModifiers.Add(EnchantMod); }
	}

	void Request_ClearModifiers()
	{
		utils_byte_attribute_modifier::Request_ClearAllModifiers(TestAttribute, ECk_MinMaxCurrent::Current);
		ActiveModifiers.Empty();
	}

	void Request_ResetComponents()
	{
		utils_byte_attribute::Request_Override(TestAttribute, 5, ECk_MinMaxCurrent::Min);
		utils_byte_attribute::Request_Override(TestAttribute, 180, ECk_MinMaxCurrent::Max);
		utils_byte_attribute::Request_Override(TestAttribute, 75, ECk_MinMaxCurrent::Current);
	}

	FString
	Request_BuildBody()
	{
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n\n";

		auto MinBase = utils_byte_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Min);
		auto MaxBase = utils_byte_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Max);
		auto CurrentBase = utils_byte_attribute::Get_BaseValue(TestAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentBonus = utils_byte_attribute::Get_BonusValue(TestAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentFinal = utils_byte_attribute::Get_FinalValue(TestAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}COMPONENT VALUES:\n";
		DisplayText = f"{DisplayText}  Min Base: {MinBase}\n";
		DisplayText = f"{DisplayText}  Max Base: {MaxBase}\n";
		DisplayText = f"{DisplayText}  Current Base: {CurrentBase}\n";
		DisplayText = f"{DisplayText}  Current Bonus: {CurrentBonus}\n";
		DisplayText = f"{DisplayText}  Current Final: {CurrentFinal}\n\n";

		DisplayText = f"{DisplayText}CALCULATION:\n";
		DisplayText = f"{DisplayText}  {CurrentBase} + {CurrentBonus} = {CurrentFinal}\n\n";

		auto ProgressPercent = (CurrentFinal - MinBase) / float32(MaxBase - MinBase);
		auto ValueBar = CkGym_Attribute::Create_ProgressBar(ProgressPercent * 100, 100.0f, 25);
		DisplayText = f"{DisplayText}[{ValueBar}]\n";
		DisplayText = f"{DisplayText}  {MinBase} <--- {CurrentFinal} ---> {MaxBase}\n\n";

		DisplayText = f"{DisplayText}MODIFIERS ({ActiveModifiers.Num()}):\n";
		for (auto i = 0; i < ActiveModifiers.Num(); i++)
		{
			auto Delta = utils_byte_attribute_modifier::Get_Delta(ActiveModifiers[i]);
			DisplayText = f"{DisplayText}  Mod {i+1}: +{Delta}\n";
		}
		if (ActiveModifiers.Num() == 0) { DisplayText = f"{DisplayText}  No active modifiers\n"; }

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		return DisplayText;
	}

	UFUNCTION() void OnValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 ValueChangeCount++; }







    bool Get_CanMutate()
    {
        auto State = CkAttributeByte::Get_State();
        return !TransactionPaused && ck::IsValid(State) &&
            State.Get_CanMutateServerChild(ck::ToEntity(this), Generation, StationIndex);
    }

    void Request_FailCurrent(FString InReason)
    {
        auto State = CkAttributeByte::Get_State();
        if (ck::IsValid(State))
        { State.Request_FailCurrentServerChild(ck::ToEntity(this), Generation, StationIndex, InReason); }
    }

    UFUNCTION()
    private void OnControlledOverrideCompleted(FCk_Handle InAttribute,
        ECk_Request_OperationResult InResult)
    {
        if (InAttribute == _ControlledOverrideTarget &&
            InResult == ECk_Request_OperationResult::Succeeded)
        { _ControlledOverrideSucceeded = true; }
    }

    bool Request_OverrideChecked(FCk_Handle_ByteAttribute& InAttribute,
        uint8 InValue, ECk_MinMaxCurrent InComponent)
    {
        if (!CkAttributeByteStation::IsConstructedAttribute(
            ck::ToEntity(this), InAttribute) ||
            !utils_byte_attribute::Has_Component(InAttribute, InComponent))
        { return false; }
        _ControlledOverrideTarget = FCk_Handle(InAttribute);
        _ControlledOverrideSucceeded = false;
        utils_byte_attribute::Request_Override(InAttribute, InValue, InComponent,
            FCk_Delegate_Request_OnCompleted(this, n"OnControlledOverrideCompleted"));
        auto Succeeded = _ControlledOverrideSucceeded;
        _ControlledOverrideTarget = FCk_Handle();
        return Succeeded;
    }

    bool Get_HasInitialResources()
    {
        auto Self = ck::ToEntity(this);
        if (!AuthorityConstructed || !CkAttributeByte::IsLiveEntity(Self) ||
            !CkAttributeByteStation::IsTimer(Self, AutoTimer)) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, TestAttribute)) { return false; }
        return true;
    }

    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (!AuthorityConstructed || StationIndex != 4 || Generation <= 0 ||
            !CkAttributeByte::IsLiveEntity(Self) ||
            !CkAttributeByteStation::IsTimer(Self, AutoTimer)) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, TestAttribute, n"Test.ValueRetrieval")) { return false; }
        TArray<FCk_Handle> Expected;
        Expected.Add(FCk_Handle(AutoTimer));
        Expected.Add(FCk_Handle(TestAttribute));
        if (!CkAttributeByteStation::HasExactDependents(Self, Expected)) { return false; }
        for (auto Modifier : ActiveModifiers)
        {
            if (!CkAttributeByte::IsLiveEntity(FCk_Handle(Modifier))) { return false; }
            auto Owner = utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Modifier));
            if (Owner != FCk_Handle(TestAttribute)) { return false; }
        }
        TArray<FCk_Handle_ByteAttribute> ModifierOwners;
        ModifierOwners.Add(TestAttribute);
        if (!CkAttributeByteStation::HasExactModifiers(ModifierOwners, ActiveModifiers)) { return false; }
        return AutoConfig.TotalSteps > 0 && AutoStep >= 0;
    }

    bool Request_SetTransactionPaused(bool InPaused)
    {
        if (!Get_HasValidAuthorityComposition()) { return false; }
        TransactionPaused = InPaused;
        if (InPaused || !AutoRunning) { utils_timer::Request_Pause(AutoTimer); }
        else { utils_timer::Request_Resume(AutoTimer); }
        return Get_HasValidAuthorityComposition();
    }

    bool Request_SetAutoRunning(bool InRunning)
    {
        auto State = CkAttributeByte::Get_State();
        if (ck::Is_NOT_Valid(State) ||
            (!State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation, StationIndex) &&
             !State.Get_CanApplyTransactionControl(ck::ToEntity(this), Generation, StationIndex)) ||
            !Get_HasValidAuthorityComposition()) { return false; }
        AutoRunning = InRunning;
        if (!TransactionPaused)
        {
            if (InRunning) { utils_timer::Request_Resume(AutoTimer); }
            else { utils_timer::Request_Pause(AutoTimer); }
        }
        return Get_HasValidAuthorityComposition();
    }

    bool Request_TryBuildSample(FCkAttributeGym_ByteStationSample& OutSample)
    {
        if (!Get_HasValidAuthorityComposition()) { return false; }
        auto Sample = FCkAttributeGym_ByteStationSample();
        Sample.StationIndex = StationIndex;
        Sample.AutoRunning = AutoRunning;
        Sample.AutoStep = AutoStep;
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(TestAttribute, n"Test.ValueRetrieval"));
        Sample.ValueChangeCount = ValueChangeCount;
        Sample.ActiveModifierCount = ActiveModifiers.Num();
        for (auto Modifier : ActiveModifiers)
        { Sample.ActiveModifierDeltas.Add(utils_byte_attribute_modifier::Get_Delta(Modifier)); }
        Sample.Title = "BYTE VALUES";
        Sample.Body = Request_BuildBody();
        if (!CkAttributeByte::IsValidSample(Sample)) { return false; }
        OutSample = Sample;
        return true;
    }

    bool Request_TryCreateControlledModifier(FCk_Handle_ByteAttribute& InAttribute,
        FName InName, uint8 InDelta, FCk_Handle_ByteAttributeModifier& OutModifier)
    {
        if (!CkAttributeByteStation::IsConstructedAttribute(ck::ToEntity(this), InAttribute))
        { return false; }
        auto Params = FCk_ByteAttributeModifier_Spec();
        Params.Set_ModifierDelta(InDelta);
        auto Staged = utils_byte_attribute_modifier::Add_Revocable(InAttribute,
            utils_gameplay_tag::ResolveGameplayTag(InName),
            ECk_AttributeModifier_Operation::Add, Params);
        if (!CkAttributeByteStation::IsRevocableModifier(InAttribute, Staged,
            InName, InDelta))
        {
            if (CkAttributeByte::IsLiveEntity(FCk_Handle(Staged)))
            { utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Staged)); }
            return false;
        }
        OutModifier = Staged;
        return true;
    }

    bool Request_BeginControlledClear(bool InReset)
    {
        if (ModifierControlPending || !Get_HasValidAuthorityComposition())
        { return false; }
        PendingModifierRetirement = ActiveModifiers;
        ModifierControlPending = true;
        ModifierResetNeedsWeapon = InReset;
        ModifierReplacementStarted = false;
        PendingExpectedModifierCount = InReset ? 1 : 0;
        utils_byte_attribute_modifier::Request_ClearAllModifiers(TestAttribute, ECk_MinMaxCurrent::Current);
        ActiveModifiers.Empty();
        return true; // GameState waits for every captured handle to become invalid.
    }

    bool Request_HasSettledModifierControl()
    {
        auto State = CkAttributeByte::Get_State();
        if (ck::Is_NOT_Valid(State) ||
            !State.Get_CanApplyTransactionControl(ck::ToEntity(this),
                Generation, StationIndex)) { return false; }
        if (!ModifierControlPending) { return Get_HasValidAuthorityComposition(); }
        for (auto Old : PendingModifierRetirement)
        { if (ck::IsValid(FCk_Handle(Old))) { return false; } }

        if (ActiveModifiers.Num() != PendingExpectedModifierCount)
        {
            Request_FailCurrent("Byte modifier membership count changed during control");
            return false;
        }
        if (!Get_HasValidAuthorityComposition()) { return false; }
        PendingModifierRetirement.Empty();
        ModifierControlPending = false;
        ModifierResetNeedsWeapon = false;
        ModifierReplacementStarted = false;
        return true;
    }

    bool Request_ApplyControl(int32 InAction, int32 InPresetIndex)
    {
        auto State = CkAttributeByte::Get_State();
        if (ck::Is_NOT_Valid(State) ||
            !((InAction >= 11 && InAction <= 15) || InAction == 18
                ? State.Get_CanApplyTransactionControl(ck::ToEntity(this), Generation, StationIndex)
                : State.Get_IsCurrentServerChild(ck::ToEntity(this), Generation, StationIndex)) ||
            !Get_HasValidAuthorityComposition()) { return false; }
        if (InPresetIndex != -1) { return false; }
        if (InAction == 3) { return Request_SetAutoRunning(!AutoRunning); }
        if (InAction == 11)
        {
            if (!Request_SetAutoRunning(false)) { return false; }
            auto ExistingWeapon = utils_byte_attribute_modifier::TryGet(TestAttribute,
                utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"),
                ECk_MinMaxCurrent::Current);
            auto ExistingBuff = utils_byte_attribute_modifier::TryGet(TestAttribute,
                utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"),
                ECk_MinMaxCurrent::Current);
            bool HasWeapon = ck::IsValid(ExistingWeapon);
            bool HasBuff = ck::IsValid(ExistingBuff);
            if (HasWeapon || HasBuff)
            {
                if (!HasWeapon || !HasBuff ||
                    !ActiveModifiers.Contains(ExistingWeapon) ||
                    !ActiveModifiers.Contains(ExistingBuff) ||
                    !CkAttributeByteStation::IsRevocableModifier(TestAttribute,
                        ExistingWeapon, n"Modifier.Weapon", uint8(20)) ||
                    !CkAttributeByteStation::IsRevocableModifier(TestAttribute,
                        ExistingBuff, n"Modifier.Buff", uint8(15)))
                {
                    Request_FailCurrent("Byte modifier pair is incomplete or malformed");
                    return false;
                }
                return true; // exact keyed repeat; preserve any Enchant modifier
            }
            int32 Expected = ActiveModifiers.Num() + 2;
            FCk_Handle_ByteAttributeModifier Weapon;
            FCk_Handle_ByteAttributeModifier Buff;
            if (!Request_TryCreateControlledModifier(TestAttribute,
                n"Modifier.Weapon", uint8(20), Weapon))
            {
                Request_FailCurrent("Byte weapon creation failed");
                return false;
            }
            if (!Request_TryCreateControlledModifier(TestAttribute,
                n"Modifier.Buff", uint8(15), Buff))
            {
                utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Weapon));
                Request_FailCurrent("Byte buff creation failed");
                return false;
            }
            ActiveModifiers.Add(Weapon);
            ActiveModifiers.Add(Buff);
            PendingModifierRetirement.Empty();
            ModifierControlPending = true;
            ModifierResetNeedsWeapon = false;
            ModifierReplacementStarted = false;
            PendingExpectedModifierCount = Expected;
            return true;
        }
        if (InAction == 12)
        {
            if (!Request_SetAutoRunning(false)) { return false; }
            return Request_BeginControlledClear(false);
        }
        if (InAction == 18)
        {
            AutoStep = 0; ValueChangeCount = 0;
            if (!Request_BeginControlledClear(false)) { return false; }
            if (!Request_OverrideChecked(TestAttribute, 5, ECk_MinMaxCurrent::Min) ||
                !Request_OverrideChecked(TestAttribute, 180, ECk_MinMaxCurrent::Max) ||
                !Request_OverrideChecked(TestAttribute, 75, ECk_MinMaxCurrent::Current))
            { return false; }
            AutoRunning = true;
            if (!TransactionPaused) { return Request_SetAutoRunning(true); }
            return true;
        }
        return false;
    }
}
