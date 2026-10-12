//============================================================================
// BYTE ATTRIBUTE MULTIPLE ATTRIBUTES STATION
//============================================================================

class UCk_EntityScript_AttributeGym_ByteMultiple : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
	UPROPERTY(ExposeOnSpawn) int32 StationIndex = 3;
	bool AuthorityConstructed = false;
	bool TransactionPaused = false;
	FCk_Handle _ControlledOverrideTarget;
	bool _ControlledOverrideSucceeded = false;

	TArray<FCk_Handle_ByteAttribute> AllAttributes;
	TArray<FCk_Handle_ByteAttribute> RPGAttributes;
	TArray<FCk_Handle_ByteAttribute> CombatAttributes;

	FCk_Handle_Timer AutoTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;

	int32 TotalAttributes = 0;
	int32 BatchEpoch = 0;
	bool RetirementPending = false;
	bool RetirementResetsRPG = false;
	bool RetirementReplacementStarted = false;
	TArray<FCk_Handle_ByteAttribute> PendingRetirement;
	int32 ValueChangeCount = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		AuthorityConstructed = false;
		TransactionPaused = false;
		auto State = CkAttributeByte::Get_State();
		if (!CkAttributeByte::IsLiveEntity(InHandle) || StationIndex != 3 || Generation <= 0 ||
			ck::Is_NOT_Valid(State) ||
			(State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex) == false) ||
			utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeByte::FindStation(StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Invalid Byte station construction lease"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}


		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_ByteMultiple");

		// Auto timer
		auto AutoTimerParams = FCk_Timer_Spec(FCk_Time(3.5f));
		AutoTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		AutoTimer = utils_timer::Add(InHandle, AutoTimerParams);
		if (CkAttributeByte::IsLiveEntity(FCk_Handle(AutoTimer)))
		{ AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick")); }

		// Auto config
		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Creates multiple attributes in batches.\nDemonstrates ForEach iteration, name-based lookups, and batch updates.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [5] Multiple station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Add combat attribute batch", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test ForEach operations", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test name-based lookup", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Batch value update (+15)", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test iteration filtering", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Clear combat batch", 5, 5));
		AutoConfig.ManualCommands.Add("panel [M] Add batch - [U] Clear batch");

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

		Request_CreateInitialBatch(InHandle);
		if (!Get_HasInitialResources())
		{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte initial composition invalid"); return; }
		Request_BindSignals(InHandle);
	}

	void
	Request_CreateInitialBatch(
		FCk_Handle InHandle)
	{
		auto RPGParams = FCk_MultipleByteAttribute_Spec();
		auto& RPGList = RPGParams._ByteAttributeParams;

		auto StrengthParams = FCk_ByteAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"RPG.Strength"), 85);
		StrengthParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(10).Set_MaxValue(255);
		RPGList.Add(StrengthParams);

		auto AgilityParams = FCk_ByteAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"RPG.Agility"), 120);
		AgilityParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(20).Set_MaxValue(200);
		RPGList.Add(AgilityParams);

		auto IntelligenceParams = FCk_ByteAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"RPG.Intelligence"), 95);
		IntelligenceParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(15).Set_MaxValue(255);
		RPGList.Add(IntelligenceParams);

		RPGAttributes = utils_byte_attribute::AddMultiple(InHandle, RPGParams, ECk_Replication::DoesNotReplicate);
		AllAttributes.Append(RPGAttributes);
	}

	void
	Request_BindSignals(
		FCk_Handle InHandle)
	{
		for (auto Attribute : AllAttributes)
		{
			utils_byte_attribute::BindTo_OnValueChanged(Attribute, ECk_MinMaxCurrent::Current,
				FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged"));
		}
	}



	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (InHandle != AutoTimer || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte Multiple auto composition lost"); return; }
        auto Step = AutoStep % AutoConfig.TotalSteps;
        if (Step == 0 || Step == 5)
        {
            auto State = CkAttributeByte::Get_State();
            if (ck::Is_NOT_Valid(State) ||
                !State.Request_AutoCombatStep(ck::ToEntity(this), Generation, StationIndex, Step == 0))
            { return; }
        }
        else if (Step == 1)
        {
            if (!Request_TestForEachOperations() || TotalAttributes != AllAttributes.Num())
            { Request_FailCurrent("Byte Multiple ForEach count disagrees with authored record"); return; }
        }
        else if (Step == 2) { Request_TestNameBasedLookup(); }
        else if (Step == 3) { Request_BatchValueUpdate(); }
        else if (Step == 4) { Request_TestIterationFiltering(); }
        AutoStep++;
    }



	bool Request_AddCombatBatch()
    {
        if (!TransactionPaused || RetirementPending || !Get_HasValidAuthorityComposition())
        { return false; }
        if (CombatAttributes.Num() == 2) { return true; } // exact idempotent M
        if (CombatAttributes.Num() != 0) { return false; }
        auto SelfEntity = ck::ToEntity(this);
        auto CombatParams = FCk_MultipleByteAttribute_Spec();
        auto& CombatList = CombatParams._ByteAttributeParams;
        auto AttackParams = FCk_ByteAttribute_Spec(utils_gameplay_tag::ResolveGameplayTag(n"Combat.Attack"), 75);
        AttackParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(5).Set_MaxValue(150);
        CombatList.Add(AttackParams);
        auto DefenseParams = FCk_ByteAttribute_Spec(utils_gameplay_tag::ResolveGameplayTag(n"Combat.Defense"), 60);
        DefenseParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0).Set_MaxValue(100);
        CombatList.Add(DefenseParams);
        auto Staged = utils_byte_attribute::AddMultiple(SelfEntity, CombatParams, ECk_Replication::DoesNotReplicate);
        if (Staged.Num() != 2 || Staged[0] == Staged[1] ||
            !CkAttributeByte::IsLiveEntity(FCk_Handle(Staged[0])) ||
            !CkAttributeByte::IsLiveEntity(FCk_Handle(Staged[1])) ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Staged[0])) != SelfEntity ||
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Staged[1])) != SelfEntity ||
            !utils_byte_attribute::Has_Component(Staged[0], ECk_MinMaxCurrent::Current) ||
            !utils_byte_attribute::Has_Component(Staged[1], ECk_MinMaxCurrent::Current))
        {
            for (auto Attribute : Staged)
            { if (CkAttributeByte::IsLiveEntity(FCk_Handle(Attribute)))
              { utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Attribute)); } }
            return false;
        }
        CombatAttributes = Staged;
        AllAttributes.Append(CombatAttributes);
        for (auto Attribute : CombatAttributes)
        {
            utils_byte_attribute::BindTo_OnValueChanged(Attribute, ECk_MinMaxCurrent::Current,
                FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged"));
        }
        TotalAttributes = AllAttributes.Num();
        BatchEpoch++;
        return true; // GameState retries the complete typed snapshot until labels settle.
    }

	bool Request_TestForEachOperations()
	{
		if (!Get_HasValidAuthorityComposition()) { return false; }
		auto SelfEntity = ck::ToEntity(this);
		auto AllFound = utils_byte_attribute::ForEach(SelfEntity, FInstancedStruct(), FCk_Lambda_InHandle());
		if (AllFound.Num() != AllAttributes.Num()) { return false; }
		for (auto Attribute : AllAttributes)
		{
			if (!AllFound.Contains(Attribute)) { return false; }
		}
		TotalAttributes = AllFound.Num();
		return true;
	}

	void Request_TestNameBasedLookup()
	{
		auto SelfEntity = ck::ToEntity(this);
		utils_byte_attribute::ForEach_ByName(SelfEntity, utils_gameplay_tag::ResolveGameplayTag(n"RPG.Strength"), FInstancedStruct(), FCk_Lambda_InHandle(this, n"ModifyStrength"));
	}

	UFUNCTION()
	private void ModifyStrength(FCk_Handle InHandle, FInstancedStruct InOptionalPayload)
	{
        if (!Get_CanMutate() || !Get_HasValidAuthorityComposition()) { return; }

		auto StrengthAttr = InHandle.As_ByteAttribute();
		utils_byte_attribute::Request_Override(StrengthAttr, 100, ECk_MinMaxCurrent::Current);
	}

	void Request_BatchValueUpdate()
	{
		for (auto Attribute : RPGAttributes)
		{
			auto CurrentValue = utils_byte_attribute::Get_FinalValue(Attribute);
			auto NewValue = uint8(Math::Clamp(CurrentValue + 15, 0, 255));
			utils_byte_attribute::Request_Override(Attribute, NewValue, ECk_MinMaxCurrent::Current);
		}
	}

	void Request_TestIterationFiltering()
	{
		auto SelfEntity = ck::ToEntity(this);
		utils_byte_attribute::ForEach_If(SelfEntity, FInstancedStruct(), FCk_Lambda_InHandle(this, n"ProcessHighValue"), FCk_Predicate_InHandle_OutResult(this, n"FilterHighValue"));
	}

	UFUNCTION()
	private void ProcessHighValue(FCk_Handle InHandle, FInstancedStruct InOptionalPayload) { }

	UFUNCTION()
	private void FilterHighValue(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InOptionalPayload)
	{
        if (!Get_CanMutate() || !Get_HasValidAuthorityComposition())
        { auto Res = OutResult; Res.Set(false); return; }

		auto Attr = InHandle.As_ByteAttribute();
		auto Value = utils_byte_attribute::Get_FinalValue(Attr);
		auto Res = OutResult;
		Res.Set(Value > 100);
	}



	FString
	Request_BuildBody()
	{
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n";
		DisplayText = f"{DisplayText}Total Attributes: {AllAttributes.Num()}\n\n";

		DisplayText = f"{DisplayText}RPG ATTRIBUTES ({RPGAttributes.Num()}):\n";
		for (auto Attribute : RPGAttributes)
		{
			auto Value = utils_byte_attribute::Get_FinalValue(Attribute);
			DisplayText = f"{DisplayText}  RPG Attr: {Value}\n";
		}

		if (CombatAttributes.Num() > 0)
		{
			DisplayText = f"{DisplayText}\nCOMBAT ATTRIBUTES ({CombatAttributes.Num()}):\n";
			for (auto Attribute : CombatAttributes)
			{
				auto Value = utils_byte_attribute::Get_FinalValue(Attribute);
				DisplayText = f"{DisplayText}  Combat Attr: {Value}\n";
			}
		}

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
        if (RPGAttributes.Num() != 3 || AllAttributes.Num() != 3) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, RPGAttributes[0])) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, RPGAttributes[1])) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, RPGAttributes[2])) { return false; }
        return true;
    }

    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (!AuthorityConstructed || StationIndex != 3 || Generation <= 0 ||
            !CkAttributeByte::IsLiveEntity(Self) ||
            !CkAttributeByteStation::IsTimer(Self, AutoTimer)) { return false; }
        if (RPGAttributes.Num() != 3 || (CombatAttributes.Num() != 0 && CombatAttributes.Num() != 2) ||
            AllAttributes.Num() != RPGAttributes.Num() + CombatAttributes.Num()) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, RPGAttributes[0], n"RPG.Strength")) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, RPGAttributes[1], n"RPG.Agility")) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, RPGAttributes[2], n"RPG.Intelligence")) { return false; }
        if (CombatAttributes.Num() == 2 && !CkAttributeByteStation::IsAttribute(Self, CombatAttributes[0], n"Combat.Attack")) { return false; }
        if (CombatAttributes.Num() == 2 && !CkAttributeByteStation::IsAttribute(Self, CombatAttributes[1], n"Combat.Defense")) { return false; }
        for (auto Attribute : RPGAttributes)
        { if (!AllAttributes.Contains(Attribute)) { return false; } }
        for (auto Attribute : CombatAttributes)
        { if (!AllAttributes.Contains(Attribute)) { return false; } }
        TArray<FCk_Handle> Expected;
        Expected.Add(FCk_Handle(AutoTimer));
        for (auto Attribute : AllAttributes) { Expected.Add(FCk_Handle(Attribute)); }
        if (!CkAttributeByteStation::HasExactDependents(Self, Expected)) { return false; }
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
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(RPGAttributes[0], n"RPG.Strength"));
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(RPGAttributes[1], n"RPG.Agility"));
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(RPGAttributes[2], n"RPG.Intelligence"));
        if (CombatAttributes.Num() == 2)
        {
            Sample.Attributes.Add(CkAttributeByteStation::Attribute(CombatAttributes[0], n"Combat.Attack"));
            Sample.Attributes.Add(CkAttributeByteStation::Attribute(CombatAttributes[1], n"Combat.Defense"));
        }
        Sample.ValueChangeCount = ValueChangeCount;
        Sample.TotalAttributes = AllAttributes.Num();
        Sample.CombatPresent = CombatAttributes.Num() == 2;
        Sample.CombatBatchCount = Sample.CombatPresent ? 1 : 0;
        Sample.BatchEpoch = BatchEpoch;
        Sample.Title = "BYTE MULTIPLE";
        Sample.Body = Request_BuildBody();
        if (!CkAttributeByte::IsValidSample(Sample)) { return false; }
        OutSample = Sample;
        return true;
    }

    bool Request_BeginCombatRetirement(bool InResetRPG,
        TArray<FCk_Handle_ByteAttribute>& OutRetired)
    {
        auto State = CkAttributeByte::Get_State();
        if (!TransactionPaused || RetirementPending || ck::Is_NOT_Valid(State) ||
            !State.Get_CanApplyTransactionControl(ck::ToEntity(this), Generation, StationIndex) ||
            !Get_HasValidAuthorityComposition()) { return false; }
        PendingRetirement.Empty();
        for (auto Attribute : CombatAttributes) { PendingRetirement.Add(Attribute); }
        if (InResetRPG)
        { for (auto Attribute : RPGAttributes) { PendingRetirement.Add(Attribute); } }
        OutRetired = PendingRetirement;
        RetirementPending = true;
        RetirementResetsRPG = InResetRPG;
        RetirementReplacementStarted = false;
        for (auto Attribute : PendingRetirement)
        {
            if (!CkAttributeByte::IsLiveEntity(FCk_Handle(Attribute))) { return false; }
            utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Attribute));
        }
        return true;
    }

    bool Request_CompleteCombatRetirement(bool InResetRPG)
    {
        auto State = CkAttributeByte::Get_State();
        if (!TransactionPaused || !RetirementPending ||
            RetirementResetsRPG != InResetRPG || ck::Is_NOT_Valid(State) ||
            !State.Get_CanApplyTransactionControl(ck::ToEntity(this), Generation, StationIndex))
        { return false; }
        for (auto Attribute : PendingRetirement)
        { if (CkAttributeByte::IsLiveEntity(FCk_Handle(Attribute))) { return false; } }
        if (!RetirementReplacementStarted && InResetRPG)
        {
            CombatAttributes.Empty(); RPGAttributes.Empty(); AllAttributes.Empty();
            AutoStep = 0; ValueChangeCount = 0; TotalAttributes = 0; BatchEpoch = 0;
            Request_CreateInitialBatch(ck::ToEntity(this));
            if (RPGAttributes.Num() != 3 || AllAttributes.Num() != 3 ||
                RPGAttributes[0] == RPGAttributes[1] || RPGAttributes[0] == RPGAttributes[2] ||
                RPGAttributes[1] == RPGAttributes[2] ||
                !CkAttributeByte::IsLiveEntity(FCk_Handle(RPGAttributes[0])) ||
                !CkAttributeByte::IsLiveEntity(FCk_Handle(RPGAttributes[1])) ||
                !CkAttributeByte::IsLiveEntity(FCk_Handle(RPGAttributes[2])) ||
                !utils_byte_attribute::Has_Component(RPGAttributes[0], ECk_MinMaxCurrent::Current) ||
                !utils_byte_attribute::Has_Component(RPGAttributes[1], ECk_MinMaxCurrent::Current) ||
                !utils_byte_attribute::Has_Component(RPGAttributes[2], ECk_MinMaxCurrent::Current))
            {
                State.Request_FailCurrentServerChild(ck::ToEntity(this), Generation,
                    StationIndex, "Byte RPG replacement allocation failed");
                return false;
            }
            Request_BindSignals(ck::ToEntity(this));
            AutoRunning = true;
            RetirementReplacementStarted = true;
        }
        else if (!RetirementReplacementStarted)
        {
            for (auto Attribute : CombatAttributes) { AllAttributes.Remove(Attribute); }
            CombatAttributes.Empty();
            TotalAttributes = AllAttributes.Num();
            RetirementReplacementStarted = true;
        }
        if (!Get_HasValidAuthorityComposition()) { return false; }
        PendingRetirement.Empty();
        RetirementPending = false;
        RetirementResetsRPG = false;
        RetirementReplacementStarted = false;
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
        if (InAction == 5) { return Request_SetAutoRunning(!AutoRunning); }
        if (InAction == 15) { return Request_AddCombatBatch(); }
        return false;
    }
}
