// Shared authority-only validation and typed sample helpers for the six Byte stations.
namespace CkAttributeByteStation
{
    bool IsTimer(FCk_Handle InOwner, FCk_Handle_Timer InTimer)
    {
        auto Handle = FCk_Handle(InTimer);
        if (!CkAttributeByte::IsLiveEntity(Handle) ||
            utils_entity_lifetime::Get_LifetimeOwner(Handle) != InOwner ||
            !utils_timer::DoCast(Handle).IsSet()) { return false; }
        return InTimer.Get_CurrentState() == ECk_Timer_State::Running ||
            InTimer.Get_CurrentState() == ECk_Timer_State::Paused;
    }

    bool IsConstructedAttribute(FCk_Handle InOwner, FCk_Handle_ByteAttribute InAttribute)
    {
        auto Handle = FCk_Handle(InAttribute);
        if (!CkAttributeByte::IsLiveEntity(Handle) ||
            utils_entity_lifetime::Get_LifetimeOwner(Handle) != InOwner)
        { return false; }
        return utils_byte_attribute::Has_Component(InAttribute, ECk_MinMaxCurrent::Min) &&
            utils_byte_attribute::Has_Component(InAttribute, ECk_MinMaxCurrent::Max) &&
            utils_byte_attribute::Has_Component(InAttribute, ECk_MinMaxCurrent::Current);
    }

    bool IsAttribute(FCk_Handle InOwner, FCk_Handle_ByteAttribute InAttribute, FName InName)
    {
        return IsConstructedAttribute(InOwner, InAttribute) &&
            utils_byte_attribute::TryGet(InOwner,
                utils_gameplay_tag::ResolveGameplayTag(InName)) == InAttribute;
    }

    bool HasExactDependents(FCk_Handle InOwner, TArray<FCk_Handle> InExpected)
    {
        // BeginPlay can still see its spawn request after that request entered retirement.
        // Composition describes live resources; every unexpected live dependent is rejected.
        TArray<FCk_Handle> Actual;
        for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InOwner))
        { if (CkAttributeByte::IsLiveEntity(Child)) { Actual.Add(Child); } }
        if (Actual.Num() != InExpected.Num()) { return false; }
        for (auto Handle : InExpected)
        {
            if (!CkAttributeByte::IsLiveEntity(Handle) || !Actual.Contains(Handle))
            { return false; }
        }
        return true;
    }

    bool HasExactModifiers(TArray<FCk_Handle_ByteAttribute> InAttributes,
        TArray<FCk_Handle_ByteAttributeModifier> InModifiers)
    {
        int32 Found = 0;
        for (auto Attribute : InAttributes)
        {
            if (!CkAttributeByte::IsLiveEntity(FCk_Handle(Attribute)) ||
                !utils_byte_attribute::Has_Component(Attribute, ECk_MinMaxCurrent::Current))
            { return false; }
            auto Children = utils_entity_lifetime::Get_LifetimeDependents(FCk_Handle(Attribute));
            for (auto Child : Children)
            {
                if (!CkAttributeByte::IsLiveEntity(Child) || !utils_gameplay_label::Has(Child))
                { continue; } // permanent nonrevocable modifiers have no label/owned handle
                bool Matched = false;
                for (auto Modifier : InModifiers)
                { if (Child == FCk_Handle(Modifier)) { Matched = true; break; } }
                if (!Matched) { return false; }
                Found++;
            }
        }
        if (Found != InModifiers.Num()) { return false; }
        for (auto Modifier : InModifiers)
        {
            auto Handle = FCk_Handle(Modifier);
            if (!CkAttributeByte::IsLiveEntity(Handle) ||
                !utils_gameplay_label::Has(Handle)) { return false; }
            auto Owner = utils_entity_lifetime::Get_LifetimeOwner(Handle);
            bool Recorded = false;
            for (auto Attribute : InAttributes)
            {
                if (Owner == FCk_Handle(Attribute) &&
                    utils_byte_attribute_modifier::TryGet(Attribute,
                        utils_gameplay_label::Get_Label(Handle),
                        ECk_MinMaxCurrent::Current) == Modifier)
                { Recorded = true; break; }
            }
            if (!Recorded) { return false; }
        }
        return true;
    }

    bool IsRevocableModifier(FCk_Handle_ByteAttribute InOwner,
        FCk_Handle_ByteAttributeModifier InModifier, FName InName, uint8 InDelta)
    {
        auto Handle = FCk_Handle(InModifier);
        return CkAttributeByte::IsLiveEntity(FCk_Handle(InOwner)) &&
            CkAttributeByte::IsLiveEntity(Handle) &&
            utils_entity_lifetime::Get_LifetimeOwner(Handle) == FCk_Handle(InOwner) &&
            utils_gameplay_label::Has(Handle) &&
            utils_gameplay_label::Get_Label(Handle) ==
                utils_gameplay_tag::ResolveGameplayTag(InName) &&
            utils_byte_attribute::Has_Component(InOwner, ECk_MinMaxCurrent::Current) &&
            utils_byte_attribute_modifier::TryGet(InOwner,
                utils_gameplay_tag::ResolveGameplayTag(InName),
                ECk_MinMaxCurrent::Current) == InModifier &&
            utils_byte_attribute_modifier::Get_Delta(InModifier) == InDelta;
    }

    FCkAttributeGym_ByteComponentSample Component(FCk_Handle_ByteAttribute InAttribute,
        ECk_MinMaxCurrent InComponent)
    {
        auto Sample = FCkAttributeGym_ByteComponentSample();
        Sample.Present = utils_byte_attribute::Has_Component(InAttribute, InComponent);
        if (Sample.Present)
        {
            Sample.Base = utils_byte_attribute::Get_BaseValue(InAttribute, InComponent);
            Sample.Bonus = utils_byte_attribute::Get_BonusValue(InAttribute, InComponent);
            Sample.Final = utils_byte_attribute::Get_FinalValue(InAttribute, InComponent);
        }
        return Sample;
    }

    FCkAttributeGym_ByteAttributeSample Attribute(FCk_Handle_ByteAttribute InAttribute, FName InName)
    {
        auto Sample = FCkAttributeGym_ByteAttributeSample();
        Sample.Name = InName;
        Sample.Min = Component(InAttribute, ECk_MinMaxCurrent::Min);
        Sample.Max = Component(InAttribute, ECk_MinMaxCurrent::Max);
        Sample.Current = Component(InAttribute, ECk_MinMaxCurrent::Current);
        Sample.PreClampFinal = utils_byte_attribute::Get_PreClampFinalValue(InAttribute);
        Sample.ClampOverflow = utils_byte_attribute::Get_ClampOverflow(InAttribute);
        return Sample;
    }
}

//============================================================================
// BYTE CLAMPING ENTITY SCRIPT
//============================================================================

class UCk_EntityScript_AttributeGym_ByteClamping : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
	UPROPERTY(ExposeOnSpawn) int32 StationIndex = 0;
	bool AuthorityConstructed = false;
	bool TransactionPaused = false;
	FCk_Handle _ControlledOverrideTarget;
	bool _ControlledOverrideSucceeded = false;

	FCk_Handle_ByteAttribute ArmorAttribute;
	FCk_Handle_ByteAttribute StaminaAttribute;
	FCk_Handle_ByteAttribute HealthAttribute;
	FCk_Handle_ByteAttribute ShieldAttribute;

	FCk_Handle_Timer AutoTimer;
	FCk_Handle_Timer UpdateTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;

	int32 ValueChangeCount = 0;
	int32 ClampedCount = 0;
	TArray<int32> ClampSerials;
	int32 BoundarySerial = 0;
	uint8 LastPreClampValue = 0;
	uint8 LastClampedValue = 0;
	uint8 LastOverflow = 0;

	// Auto-cycling test values
	uint8 CurrentArmorTest = 100;
	uint8 CurrentStaminaTest = 150;
	uint8 CurrentHealthTest = 75;
	bool ArmorIncreasing = true;
	bool StaminaIncreasing = true;
	bool HealthIncreasing = false;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		AuthorityConstructed = false;
		TransactionPaused = false;
		ClampSerials.Empty();
		for (int32 i = 0; i < 4; i++) { ClampSerials.Add(0); }
		BoundarySerial = 0;
		auto State = CkAttributeByte::Get_State();
		if (!CkAttributeByte::IsLiveEntity(InHandle) || StationIndex != 0 || Generation <= 0 ||
			ck::Is_NOT_Valid(State) ||
			(State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex) == false) ||
			utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeByte::FindStation(StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Invalid Byte station construction lease"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}


		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_ByteClamping");

		// Update timer for continuous value cycling
		auto UpdateTimerParams = FCk_Timer_Spec(FCk_Time(1.8f));
		UpdateTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		UpdateTimer = utils_timer::Add(InHandle, UpdateTimerParams);
		UpdateTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"UpdateTick"));

		// Auto timer (controls on/off for the cycling)
		auto AutoTimerParams = FCk_Timer_Spec(FCk_Time(1.8f));
		AutoTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		AutoTimer = utils_timer::Add(InHandle, AutoTimerParams);
		if (CkAttributeByte::IsLiveEntity(FCk_Handle(AutoTimer)))
		{ AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick")); }

		// Auto config
		AutoConfig.TotalSteps = 1;
		AutoConfig.Description = "Cycles attribute values beyond min/max to demonstrate clamping behavior.\nShield demonstrates initial-value clamping (created with value > max).";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [1] Clamping station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Cycling Armor/Stamina/Health values", 0, 0));
		AutoConfig.ManualCommands.Add("panel [B] Test boundaries - [R] Reset all stations");

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
		auto ArmorParams = FCk_ByteAttribute_Spec(utils_gameplay_tag::ResolveGameplayTag(n"ByteAttribute.Armor"), 100);
		ArmorParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0).Set_MaxValue(200);
		ArmorAttribute = utils_byte_attribute::Add(InHandle, ArmorParams, ECk_Replication::DoesNotReplicate);

		auto StaminaParams = FCk_ByteAttribute_Spec(utils_gameplay_tag::ResolveGameplayTag(n"ByteAttribute.Stamina"), 150);
		StaminaParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(50).Set_MaxValue(255);
		StaminaAttribute = utils_byte_attribute::Add(InHandle, StaminaParams, ECk_Replication::DoesNotReplicate);

		auto HealthParams = FCk_ByteAttribute_Spec(utils_gameplay_tag::ResolveGameplayTag(n"ByteAttribute.Health"), 75);
		HealthParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0).Set_MaxValue(100);
		HealthAttribute = utils_byte_attribute::Add(InHandle, HealthParams, ECk_Replication::DoesNotReplicate);

		auto ShieldParams = FCk_ByteAttribute_Spec(utils_gameplay_tag::ResolveGameplayTag(n"ByteAttribute.Shield"), 200);
		ShieldParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0).Set_MaxValue(150);
		ShieldAttribute = utils_byte_attribute::Add(InHandle, ShieldParams, ECk_Replication::DoesNotReplicate);
	}

	void
	Request_BindSignals(
		FCk_Handle InHandle)
	{
		utils_byte_attribute::BindTo_OnValueChanged(ArmorAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnArmorValueChanged"));
		utils_byte_attribute::BindTo_OnMinClamped(ArmorAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnArmorClamped"));
		utils_byte_attribute::BindTo_OnMaxClamped(ArmorAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnArmorClamped"));
		utils_byte_attribute::BindTo_OnValueChanged(StaminaAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnStaminaValueChanged"));
		utils_byte_attribute::BindTo_OnMinClamped(StaminaAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnStaminaClamped"));
		utils_byte_attribute::BindTo_OnMaxClamped(StaminaAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnStaminaClamped"));
		utils_byte_attribute::BindTo_OnValueChanged(HealthAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnHealthValueChanged"));
		utils_byte_attribute::BindTo_OnMinClamped(HealthAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnHealthClamped"));
		utils_byte_attribute::BindTo_OnMaxClamped(HealthAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnHealthClamped"));
		utils_byte_attribute::BindTo_OnValueChanged(ShieldAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnShieldValueChanged"));
		utils_byte_attribute::BindTo_OnMinClamped(ShieldAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnShieldClamped"));
		utils_byte_attribute::BindTo_OnMaxClamped(ShieldAttribute, FCk_Delegate_ByteAttribute_OnClamped(this, n"OnShieldClamped"));
	}



	UFUNCTION()
	private void UpdateTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != UpdateTimer || !Get_CanMutate()) { return; }
		if (!Get_HasValidAuthorityComposition()) { Request_FailCurrent("Byte update composition lost"); return; }

		if (AutoRunning) { Request_AutoUpdateValues(); }
	}

	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT) {
		if (InHandle != AutoTimer || !Get_CanMutate()) { return; }
		if (!Get_HasValidAuthorityComposition()) { Request_FailCurrent("Byte auto composition lost"); return; }
 AutoStep++; }



	void
	Request_AutoUpdateValues()
	{
		if (ArmorIncreasing) { CurrentArmorTest = uint8(CurrentArmorTest + 30); if (CurrentArmorTest >= 240) ArmorIncreasing = false; }
		else { CurrentArmorTest = uint8(CurrentArmorTest - 40); if (CurrentArmorTest <= 0) ArmorIncreasing = true; }

		if (StaminaIncreasing) { CurrentStaminaTest = uint8(CurrentStaminaTest + 25); if (CurrentStaminaTest >= 280) StaminaIncreasing = false; }
		else { CurrentStaminaTest = uint8(CurrentStaminaTest - 35); if (CurrentStaminaTest <= 20) StaminaIncreasing = true; }

		if (HealthIncreasing) { CurrentHealthTest = uint8(CurrentHealthTest + 20); if (CurrentHealthTest >= 120) HealthIncreasing = false; }
		else { CurrentHealthTest = uint8(CurrentHealthTest - 25); if (CurrentHealthTest <= 0) HealthIncreasing = true; }

		utils_byte_attribute::Request_Override(ArmorAttribute, CurrentArmorTest, ECk_MinMaxCurrent::Current);
		utils_byte_attribute::Request_Override(StaminaAttribute, CurrentStaminaTest, ECk_MinMaxCurrent::Current);
		utils_byte_attribute::Request_Override(HealthAttribute, CurrentHealthTest, ECk_MinMaxCurrent::Current);
	}

	FString
    Request_BuildBody()
	{
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount} | Clamps: {ClampedCount}\n";

		if (ClampedCount > 0)
		{
			DisplayText = f"{DisplayText}Last Clamp: pre={LastPreClampValue} clamped={LastClampedValue} overflow={LastOverflow}\n";
		}

		// Live polling via utility accessors - shows the Armor attribute's this-frame values.
		auto LivePre = utils_byte_attribute::Get_PreClampFinalValue(ArmorAttribute);
		auto LiveOvr = utils_byte_attribute::Get_ClampOverflow(ArmorAttribute);
		DisplayText = f"{DisplayText}Live Poll (Armor): pre={LivePre}  overflow={LiveOvr}\n";

		DisplayText = DisplayText + "\n";

		{
			auto ArmorValue = utils_byte_attribute::Get_FinalValue(ArmorAttribute);
			auto ArmorBar = CkGym_Attribute::Create_ProgressBar(ArmorValue, 200.0f, 20, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
			auto ClampSuffix = CkGym_Attribute::Get_ClampingSuffix(ArmorValue, CurrentArmorTest);
			DisplayText = f"{DisplayText}Armor: {ArmorValue}/200{ClampSuffix}\n";
			DisplayText = f"{DisplayText}[{ArmorBar}] " + (ArmorIncreasing ? "UP" : "DOWN") + "\n\n";
		}

		{
			auto StaminaValue = utils_byte_attribute::Get_FinalValue(StaminaAttribute);
			auto StaminaBar = CkGym_Attribute::Create_ProgressBar((StaminaValue - 50), (255 - 50), 20, ECk_ASCII_ProgressBar_Style::Equal_Symbol);
			auto ClampSuffix = CkGym_Attribute::Get_ClampingSuffix(StaminaValue, CurrentStaminaTest);
			DisplayText = f"{DisplayText}Stamina: {StaminaValue}/255{ClampSuffix}\n";
			DisplayText = f"{DisplayText}[{StaminaBar}] " + (StaminaIncreasing ? "UP" : "DOWN") + "\n\n";
		}

		{
			auto HealthValue = utils_byte_attribute::Get_FinalValue(HealthAttribute);
			auto HealthBar = CkGym_Attribute::Create_ProgressBar(HealthValue, 100.0f, 20);
			auto ClampSuffix = CkGym_Attribute::Get_ClampingSuffix(HealthValue, CurrentHealthTest);
			DisplayText = f"{DisplayText}Health: {HealthValue}/100{ClampSuffix}\n";
			DisplayText = f"{DisplayText}[{HealthBar}] " + (HealthIncreasing ? "UP" : "DOWN") + "\n\n";
		}

		{
			auto ShieldValue = utils_byte_attribute::Get_FinalValue(ShieldAttribute);
			auto ShieldBar = CkGym_Attribute::Create_ProgressBar(ShieldValue, 150.0f, 20, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
			DisplayText = f"{DisplayText}Shield: {ShieldValue}/150 (Initial: 200, clamped on creation)\n";
			DisplayText = f"{DisplayText}[{ShieldBar}] STATIC\n\n";
		}

		DisplayText = DisplayText + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		return DisplayText;
	}

	UFUNCTION() void OnArmorValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 ValueChangeCount++; }
	UFUNCTION() void OnStaminaValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 ValueChangeCount++; }
	UFUNCTION() void OnHealthValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 ValueChangeCount++; }
	UFUNCTION() void OnShieldValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 ValueChangeCount++; }

	UFUNCTION() void OnArmorClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnClamped InPayload)
	{
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }

		ClampSerials[0]++; ClampedCount++; LastPreClampValue = InPayload.Get_PreClampFinalValue(); LastClampedValue = InPayload.Get_FinalClampedValue(); LastOverflow = InPayload.Get_ClampOverflow();
	}

	UFUNCTION() void OnStaminaClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnClamped InPayload)
	{
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }

		ClampSerials[1]++; ClampedCount++; LastPreClampValue = InPayload.Get_PreClampFinalValue(); LastClampedValue = InPayload.Get_FinalClampedValue(); LastOverflow = InPayload.Get_ClampOverflow();
	}

	UFUNCTION() void OnHealthClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnClamped InPayload)
	{
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }

		ClampSerials[2]++; ClampedCount++; LastPreClampValue = InPayload.Get_PreClampFinalValue(); LastClampedValue = InPayload.Get_FinalClampedValue(); LastOverflow = InPayload.Get_ClampOverflow();
	}

	UFUNCTION() void OnShieldClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnClamped InPayload)
	{
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }

		ClampSerials[3]++; ClampedCount++; LastPreClampValue = InPayload.Get_PreClampFinalValue(); LastClampedValue = InPayload.Get_FinalClampedValue(); LastOverflow = InPayload.Get_ClampOverflow();
	}





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
        if (!CkAttributeByteStation::IsTimer(Self, UpdateTimer)) { return false; }
        if (ClampSerials.Num() != 4 || BoundarySerial < 0) { return false; }
        for (auto Serial : ClampSerials) { if (Serial < 0) { return false; } }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, ArmorAttribute)) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, StaminaAttribute)) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, HealthAttribute)) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, ShieldAttribute)) { return false; }
        return true;
    }

    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (!AuthorityConstructed || StationIndex != 0 || Generation <= 0 ||
            !CkAttributeByte::IsLiveEntity(Self) ||
            !CkAttributeByteStation::IsTimer(Self, AutoTimer)) { return false; }
        if (!CkAttributeByteStation::IsTimer(Self, UpdateTimer)) { return false; }
        if (ClampSerials.Num() != 4 || BoundarySerial < 0) { return false; }
        for (auto Serial : ClampSerials) { if (Serial < 0) { return false; } }
        if (!CkAttributeByteStation::IsAttribute(Self, ArmorAttribute, n"ByteAttribute.Armor")) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, StaminaAttribute, n"ByteAttribute.Stamina")) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, HealthAttribute, n"ByteAttribute.Health")) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, ShieldAttribute, n"ByteAttribute.Shield")) { return false; }
        TArray<FCk_Handle> Expected;
        Expected.Add(FCk_Handle(AutoTimer));
        Expected.Add(FCk_Handle(UpdateTimer));
        Expected.Add(FCk_Handle(ArmorAttribute));
        Expected.Add(FCk_Handle(StaminaAttribute));
        Expected.Add(FCk_Handle(HealthAttribute));
        Expected.Add(FCk_Handle(ShieldAttribute));
        if (!CkAttributeByteStation::HasExactDependents(Self, Expected)) { return false; }
        return AutoConfig.TotalSteps > 0 && AutoStep >= 0;
    }

    bool Request_SetTransactionPaused(bool InPaused)
    {
        if (!Get_HasValidAuthorityComposition()) { return false; }
        TransactionPaused = InPaused;
        if (InPaused || !AutoRunning) { utils_timer::Request_Pause(AutoTimer); }
        else { utils_timer::Request_Resume(AutoTimer); }
        if (InPaused || !AutoRunning) { utils_timer::Request_Pause(UpdateTimer); }
        else { utils_timer::Request_Resume(UpdateTimer); }
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
            if (InRunning) { utils_timer::Request_Resume(UpdateTimer); }
            else { utils_timer::Request_Pause(UpdateTimer); }
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
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(ArmorAttribute, n"ByteAttribute.Armor"));
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(StaminaAttribute, n"ByteAttribute.Stamina"));
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(HealthAttribute, n"ByteAttribute.Health"));
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(ShieldAttribute, n"ByteAttribute.Shield"));
        Sample.ValueChangeCount = ValueChangeCount;
        Sample.ClampedCount = ClampedCount;
        Sample.LastPreClampValue = LastPreClampValue;
        Sample.LastClampedValue = LastClampedValue;
        Sample.LastOverflow = LastOverflow;
        Sample.ClampSerials = ClampSerials;
        Sample.BoundarySerial = BoundarySerial;
        Sample.Title = "BYTE CLAMPING";
        Sample.Body = Request_BuildBody();
        if (!CkAttributeByte::IsValidSample(Sample)) { return false; }
        OutSample = Sample;
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
        if (InAction == 1) { return Request_SetAutoRunning(!AutoRunning); }
        if (InAction == 17)
        {
            if (!Request_SetAutoRunning(false)) { return false; }
            BoundarySerial++;
            if (!Request_OverrideChecked(ArmorAttribute, 255, ECk_MinMaxCurrent::Current)) { return false; }
            if (!Request_OverrideChecked(StaminaAttribute, 30, ECk_MinMaxCurrent::Current)) { return false; }
            if (!Request_OverrideChecked(HealthAttribute, 150, ECk_MinMaxCurrent::Current)) { return false; }
            if (!Request_OverrideChecked(ShieldAttribute, 255, ECk_MinMaxCurrent::Current)) { return false; }
            return true;
        }
        if (InAction == 18)
        {
            AutoStep = 0; ValueChangeCount = 0; ClampedCount = 0;
            LastPreClampValue = 0; LastClampedValue = 0; LastOverflow = 0;
            CurrentArmorTest = 100; CurrentStaminaTest = 150; CurrentHealthTest = 75;
            ArmorIncreasing = true; StaminaIncreasing = true; HealthIncreasing = false;
            if (!Request_OverrideChecked(ArmorAttribute, 100, ECk_MinMaxCurrent::Current)) { return false; }
            if (!Request_OverrideChecked(StaminaAttribute, 150, ECk_MinMaxCurrent::Current)) { return false; }
            if (!Request_OverrideChecked(HealthAttribute, 75, ECk_MinMaxCurrent::Current)) { return false; }
            if (!Request_OverrideChecked(ShieldAttribute, 200, ECk_MinMaxCurrent::Current)) { return false; }
            AutoRunning = true;
            if (!TransactionPaused) { return Request_SetAutoRunning(true); }
            return true;
        }
        return false;
    }
}
