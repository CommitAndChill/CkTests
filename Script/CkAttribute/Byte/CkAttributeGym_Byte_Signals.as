//============================================================================
// BYTE ATTRIBUTE SIGNAL BINDING STATION
//============================================================================

class UCk_EntityScript_AttributeGym_ByteSignals : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
	UPROPERTY(ExposeOnSpawn) int32 StationIndex = 5;
	bool AuthorityConstructed = false;
	bool TransactionPaused = false;
	FCk_Handle _ControlledOverrideTarget;
	bool _ControlledOverrideSucceeded = false;

	FCk_Handle_ByteAttribute TestAttribute;

	FCk_Handle_Timer AutoTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;

	int32 Delegate1Count = 0;
	int32 Delegate2Count = 0;
	int32 Delegate3Count = 0;
	bool Delegate1Bound = false;
	bool Delegate2Bound = false;
	bool Delegate3Bound = false;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		AuthorityConstructed = false;
		TransactionPaused = false;
		auto State = CkAttributeByte::Get_State();
		if (!CkAttributeByte::IsLiveEntity(InHandle) || StationIndex != 5 || Generation <= 0 ||
			ck::Is_NOT_Valid(State) ||
			(State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex) == false) ||
			utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeByte::FindStation(StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Invalid Byte station construction lease"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}


		utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_ByteSignals");

		// Auto timer
		auto AutoTimerParams = FCk_Timer_Spec(FCk_Time(2.2f));
		AutoTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		AutoTimer = utils_timer::Add(InHandle, AutoTimerParams);
		if (CkAttributeByte::IsLiveEntity(FCk_Handle(AutoTimer)))
		{ AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick")); }

		// Auto config
		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests signal binding/unbinding with multiple delegates.\nShows dynamic bind/unbind and signal firing verification.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [6] Signals station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Bind first delegate", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Bind multiple delegates", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Trigger value changes", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Unbind selective delegates", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test rebinding", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Unbind all delegates", 5, 5));
		AutoConfig.ManualCommands.Add("panel [0] Signal value preset - 150 / 0 / 255 / 200");

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
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto TestParams = FCk_ByteAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"Test.SignalBinding"), 100);
		TestParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0).Set_MaxValue(255);
		TestAttribute = utils_byte_attribute::Add(InHandle, TestParams, ECk_Replication::DoesNotReplicate);
	}



	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
		if (InHandle != AutoTimer || !Get_CanMutate()) { return; }
		if (!Get_HasValidAuthorityComposition()) { Request_FailCurrent("Byte auto composition lost"); return; }

		auto Step = AutoStep % AutoConfig.TotalSteps;

		if (Step == 0) { Request_BindFirstDelegate(); }
		else if (Step == 1) { Request_BindMultipleDelegates(); }
		else if (Step == 2) { Request_TriggerValueChanges(); }
		else if (Step == 3) { Request_UnbindSelectiveDelegates(); }
		else if (Step == 4) { Request_TestRebinding(); }
		else if (Step == 5) { Request_UnbindAllDelegates(); }

		AutoStep++;
	}



	void Request_BindFirstDelegate()
	{
		utils_byte_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged1"));
		Delegate1Bound = true;
		utils_byte_attribute::Request_Override(TestAttribute, 120, ECk_MinMaxCurrent::Current);
	}

	void Request_BindMultipleDelegates()
	{
		utils_byte_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged2"));
		utils_byte_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged3"));
		Delegate2Bound = true;
		Delegate3Bound = true;
		utils_byte_attribute::Request_Override(TestAttribute, 80, ECk_MinMaxCurrent::Current);
	}

	void Request_TriggerValueChanges()
	{
		utils_byte_attribute::Request_Override(TestAttribute, 150, ECk_MinMaxCurrent::Current);
		utils_byte_attribute::Request_Override(TestAttribute, 60, ECk_MinMaxCurrent::Current);
		utils_byte_attribute::Request_Override(TestAttribute, 200, ECk_MinMaxCurrent::Current);
	}

	void Request_UnbindSelectiveDelegates()
	{
		utils_byte_attribute::UnbindFrom_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged2"));
		Delegate2Bound = false;
		utils_byte_attribute::Request_Override(TestAttribute, 90, ECk_MinMaxCurrent::Current);
	}

	void Request_TestRebinding()
	{
		utils_byte_attribute::BindTo_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged2"));
		Delegate2Bound = true;
		utils_byte_attribute::Request_Override(TestAttribute, 110, ECk_MinMaxCurrent::Current);
	}

	void Request_UnbindAllDelegates()
	{
		utils_byte_attribute::UnbindFrom_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged1"));
		utils_byte_attribute::UnbindFrom_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged2"));
		utils_byte_attribute::UnbindFrom_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged3"));
		Delegate1Bound = false;
		Delegate2Bound = false;
		Delegate3Bound = false;
		utils_byte_attribute::Request_Override(TestAttribute, 130, ECk_MinMaxCurrent::Current);
	}

	FString
	Request_BuildBody()
	{
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		auto CurrentValue = utils_byte_attribute::Get_FinalValue(TestAttribute);
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
		DisplayText = f"{DisplayText}Total Signals Fired: " + f"{Delegate1Count + Delegate2Count + Delegate3Count}\n\n";

		auto ValueBar = CkGym_Attribute::Create_ProgressBar(CurrentValue, 255.0f, 20);
		DisplayText = f"{DisplayText}[{ValueBar}]\n";

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		return DisplayText;
	}



	UFUNCTION() void OnValueChanged1(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 Delegate1Count++; }
	UFUNCTION() void OnValueChanged2(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 Delegate2Count++; }
	UFUNCTION() void OnValueChanged3(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 Delegate3Count++; }



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
        if (!AuthorityConstructed || StationIndex != 5 || Generation <= 0 ||
            !CkAttributeByte::IsLiveEntity(Self) ||
            !CkAttributeByteStation::IsTimer(Self, AutoTimer)) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, TestAttribute, n"Test.SignalBinding")) { return false; }
        TArray<FCk_Handle> Expected;
        Expected.Add(FCk_Handle(AutoTimer));
        Expected.Add(FCk_Handle(TestAttribute));
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
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(TestAttribute, n"Test.SignalBinding"));
        Sample.Delegate1Count = Delegate1Count;
        Sample.Delegate2Count = Delegate2Count;
        Sample.Delegate3Count = Delegate3Count;
        Sample.DelegateBoundMask = (Delegate1Bound ? 1 : 0) | (Delegate2Bound ? 2 : 0) | (Delegate3Bound ? 4 : 0);
        Sample.Title = "BYTE SIGNALS";
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
        if (InAction == 6 && InPresetIndex == -1)
        { return Request_SetAutoRunning(!AutoRunning); }
        if (InAction == 10 && InPresetIndex >= 0 && InPresetIndex < 4)
        {
            if (!Request_SetAutoRunning(false)) { return false; }
            TArray<uint8> Values;
            Values.Add(uint8(150)); Values.Add(uint8(0)); Values.Add(uint8(255)); Values.Add(uint8(200));
            if (!Request_OverrideChecked(TestAttribute, Values[InPresetIndex], ECk_MinMaxCurrent::Current)) { return false; }
            return true;
        }
        if (InAction == 18 && InPresetIndex == -1)
        {
            if (Delegate1Bound) { utils_byte_attribute::UnbindFrom_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged1")); }
            if (Delegate2Bound) { utils_byte_attribute::UnbindFrom_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged2")); }
            if (Delegate3Bound) { utils_byte_attribute::UnbindFrom_OnValueChanged(TestAttribute, ECk_MinMaxCurrent::Current, FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnValueChanged3")); }
            Delegate1Bound = false; Delegate2Bound = false; Delegate3Bound = false;
            AutoStep = 0; Delegate1Count = 0; Delegate2Count = 0; Delegate3Count = 0;
            if (!Request_OverrideChecked(TestAttribute, 100, ECk_MinMaxCurrent::Current)) { return false; }
            AutoRunning = true;
            if (!TransactionPaused) { return Request_SetAutoRunning(true); }
            return true;
        }
        return false;
    }
}
