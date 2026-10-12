//============================================================================
// BYTE MIN/MAX/CURRENT ENTITY SCRIPT
//============================================================================

class UCk_EntityScript_AttributeGym_ByteMinMaxCurrent : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
	UPROPERTY(ExposeOnSpawn) int32 StationIndex = 2;
	bool AuthorityConstructed = false;
	bool TransactionPaused = false;
	FCk_Handle _ControlledOverrideTarget;
	bool _ControlledOverrideSucceeded = false;

	FCk_Handle_ByteAttribute PowerLevelAttribute;

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
		AuthorityConstructed = false;
		TransactionPaused = false;
		auto State = CkAttributeByte::Get_State();
		if (!CkAttributeByte::IsLiveEntity(InHandle) || StationIndex != 2 || Generation <= 0 ||
			ck::Is_NOT_Valid(State) ||
			(State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex) == false) ||
			utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeByte::FindStation(StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Invalid Byte station construction lease"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}


		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_ByteMinMaxCurrent");

		// Auto timer
		auto AutoTimerParams = FCk_Timer_Spec(FCk_Time(3.0f));
		AutoTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		AutoTimer = utils_timer::Add(InHandle, AutoTimerParams);
		if (CkAttributeByte::IsLiveEntity(FCk_Handle(AutoTimer)))
		{ AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick")); }

		// Auto config
		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests individual component manipulation (Min/Max/Current).\nShows component presence detection and separate change counters.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [4] Min/Max/Current station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Min component (10 -> 20)", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Max component (200 -> 180)", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Current component (100 -> 150)", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test component detection", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test value retrieval", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Reset to defaults", 5, 5));
		AutoConfig.ManualCommands.Add("panel [7] Min preset - [8] Max preset - [9] Current preset");

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
		auto PowerParams = FCk_ByteAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"ByteAttribute.PowerLevel"), 100);
		PowerParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(10).Set_MaxValue(200);
		PowerLevelAttribute = utils_byte_attribute::Add(InHandle, PowerParams, ECk_Replication::DoesNotReplicate);
	}

	void
	Request_BindSignals(
		FCk_Handle InHandle)
	{
		utils_byte_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Min,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnMinChanged"));
		utils_byte_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Max,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnMaxChanged"));
		utils_byte_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnCurrentChanged"));
	}



	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || !Get_CanMutate()) { return; }
		if (!Get_HasValidAuthorityComposition()) { Request_FailCurrent("Byte auto composition lost"); return; }

		auto Step = AutoStep % AutoConfig.TotalSteps;

		if (Step == 0) { Request_ModifyMin(); }
		else if (Step == 1) { Request_ModifyMax(); }
		else if (Step == 2) { Request_ModifyCurrent(); }
		else if (Step == 3) { /* component detection shown in display */ }
		else if (Step == 4) { /* value retrieval shown in display */ }
		else if (Step == 5) { Request_ResetToDefaults(); }

		AutoStep++;
	}



	void Request_ModifyMin() { utils_byte_attribute::Request_Override(PowerLevelAttribute, 20, ECk_MinMaxCurrent::Min); }
	void Request_ModifyMax() { utils_byte_attribute::Request_Override(PowerLevelAttribute, 180, ECk_MinMaxCurrent::Max); }
	void Request_ModifyCurrent() { utils_byte_attribute::Request_Override(PowerLevelAttribute, 150, ECk_MinMaxCurrent::Current); }

	void Request_ResetToDefaults()
	{
		utils_byte_attribute::Request_Override(PowerLevelAttribute, 10, ECk_MinMaxCurrent::Min);
		utils_byte_attribute::Request_Override(PowerLevelAttribute, 200, ECk_MinMaxCurrent::Max);
		utils_byte_attribute::Request_Override(PowerLevelAttribute, 100, ECk_MinMaxCurrent::Current);
	}

	FString
	Request_BuildBody()
	{
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Min Changes: {MinChangeCount} | Max Changes: {MaxChangeCount}\n";
		DisplayText = f"{DisplayText}Current Changes: {CurrentChangeCount}\n\n";

		auto HasMin = utils_byte_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
		auto HasMax = utils_byte_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
		auto HasCurrent = utils_byte_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}Components Present:\n";
		DisplayText = f"{DisplayText}  Min: " + (HasMin ? "YES" : "NO") + "\n";
		DisplayText = f"{DisplayText}  Max: " + (HasMax ? "YES" : "NO") + "\n";
		DisplayText = f"{DisplayText}  Current: " + (HasCurrent ? "YES" : "NO") + "\n\n";

		auto MinValue = utils_byte_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
		auto MaxValue = utils_byte_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
		auto CurrentBase = utils_byte_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentBonus = utils_byte_attribute::Get_BonusValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);
		auto CurrentFinal = utils_byte_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}PowerLevel Values:\n";
		DisplayText = f"{DisplayText}  Min: {MinValue}\n";
		DisplayText = f"{DisplayText}  Max: {MaxValue}\n";
		DisplayText = f"{DisplayText}  Current Base: {CurrentBase}\n";
		DisplayText = f"{DisplayText}  Current Bonus: {CurrentBonus}\n";
		DisplayText = f"{DisplayText}  Current Final: {CurrentFinal}\n\n";

		auto PowerBar = CkGym_Attribute::Create_ProgressBar((CurrentFinal - MinValue), (MaxValue - MinValue), 25);
		DisplayText = f"{DisplayText}[{PowerBar}]\n";
		DisplayText = f"{DisplayText}  {MinValue} <--- {CurrentFinal} ---> {MaxValue}\n";

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		return DisplayText;
	}

	UFUNCTION() void OnMinChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 MinChangeCount++; }
	UFUNCTION() void OnMaxChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 MaxChangeCount++; }
	UFUNCTION() void OnCurrentChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 CurrentChangeCount++; }





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
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, PowerLevelAttribute)) { return false; }
        return true;
    }

    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (!AuthorityConstructed || StationIndex != 2 || Generation <= 0 ||
            !CkAttributeByte::IsLiveEntity(Self) ||
            !CkAttributeByteStation::IsTimer(Self, AutoTimer)) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, PowerLevelAttribute, n"ByteAttribute.PowerLevel")) { return false; }
        TArray<FCk_Handle> Expected;
        Expected.Add(FCk_Handle(AutoTimer));
        Expected.Add(FCk_Handle(PowerLevelAttribute));
        if (!CkAttributeByteStation::HasExactDependents(Self, Expected)) { return false; }
        return AutoConfig.TotalSteps > 0 && AutoStep >= 0;
    }

    bool Get_IsCrossedBoundPreset(int32 InAction, int32 InPresetIndex)
    {
        if ((InAction != 7 && InAction != 8) || InPresetIndex < 0 ||
            InPresetIndex >= 4 || !Get_HasValidAuthorityComposition() ||
            !utils_byte_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Min) ||
            !utils_byte_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Max))
        { return false; }
        TArray<uint8> Values;
        if (InAction == 7)
        {
            Values.Add(uint8(10)); Values.Add(uint8(20));
            Values.Add(uint8(120)); Values.Add(uint8(220));
            return Values[InPresetIndex] >
                utils_byte_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
        }
        Values.Add(uint8(200)); Values.Add(uint8(180));
        Values.Add(uint8(80)); Values.Add(uint8(5));
        return Values[InPresetIndex] <
            utils_byte_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
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
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(PowerLevelAttribute, n"ByteAttribute.PowerLevel"));
        Sample.MinChangeCount = MinChangeCount;
        Sample.MaxChangeCount = MaxChangeCount;
        Sample.CurrentChangeCount = CurrentChangeCount;
        Sample.Title = "BYTE MIN/MAX/CURRENT";
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
        if (InAction == 4 && InPresetIndex == -1)
        { return Request_SetAutoRunning(!AutoRunning); }
        if (InAction >= 7 && InAction <= 9 && InPresetIndex >= 0 && InPresetIndex < 4)
        {
            if (Get_IsCrossedBoundPreset(InAction, InPresetIndex)) { return false; }
            if (!Request_SetAutoRunning(false)) { return false; }
            TArray<uint8> Values;
            if (InAction == 7)
            {
                Values.Add(uint8(10)); Values.Add(uint8(20)); Values.Add(uint8(120)); Values.Add(uint8(220));
                if (!Request_OverrideChecked(PowerLevelAttribute, Values[InPresetIndex], ECk_MinMaxCurrent::Min)) { return false; }
            }
            else if (InAction == 8)
            {
                Values.Add(uint8(200)); Values.Add(uint8(180)); Values.Add(uint8(80)); Values.Add(uint8(5));
                if (!Request_OverrideChecked(PowerLevelAttribute, Values[InPresetIndex], ECk_MinMaxCurrent::Max)) { return false; }
            }
            else
            {
                Values.Add(uint8(100)); Values.Add(uint8(150)); Values.Add(uint8(255)); Values.Add(uint8(0));
                if (!Request_OverrideChecked(PowerLevelAttribute, Values[InPresetIndex], ECk_MinMaxCurrent::Current)) { return false; }
            }
            return true;
        }
        if (InAction == 18 && InPresetIndex == -1)
        {
            AutoStep = 0; MinChangeCount = 0; MaxChangeCount = 0; CurrentChangeCount = 0;
            if (!Request_OverrideChecked(PowerLevelAttribute, 10, ECk_MinMaxCurrent::Min) ||
                !Request_OverrideChecked(PowerLevelAttribute, 200, ECk_MinMaxCurrent::Max) ||
                !Request_OverrideChecked(PowerLevelAttribute, 100, ECk_MinMaxCurrent::Current))
            { return false; }
            AutoRunning = true;
            if (!TransactionPaused) { return Request_SetAutoRunning(true); }
            return true;
        }
        return false;
    }
}
