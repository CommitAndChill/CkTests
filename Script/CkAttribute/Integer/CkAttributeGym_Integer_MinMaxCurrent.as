//============================================================================
// INTEGER MIN/MAX/CURRENT COMPONENTS ENTITY SCRIPT
//============================================================================

class UCk_EntityScript_IntegerGym_MinMaxCurrent : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FString LastDisplayTitle;
	FString LastDisplayBody;
	int32 RejectedLegacyCommandCount = 0;

	FCk_Handle_IntegerAttribute PowerLevelAttribute;

	FCk_Handle_Timer AutoTimer;
	FCk_Handle_Timer DisplayTimer;
	private bool _AuthorityStarted = false;
	private bool _AuthorityFailed = false;
	private bool _MutationGateClosed = false;
	private bool _PreparePauseRequested = false;
	private bool _PreparePauseSucceeded = false;
	private bool _TransactionPrepared = false;
	private bool _ControlPending = false;
	private bool _ResetPending = false;
	private bool _FinishPending = false;
	private bool _FinishCompleted = false;
	private bool _FinishReleased = false;
	private bool _FinishResume = false;
	private bool _ResumeRequestSucceeded = false;
	private bool _StartRequestSucceeded = false;
	private bool _PendingMutation = false;
	private int32 _ExpectedRequestCompletions = 0;
	private int32 _SucceededRequestCompletions = 0;
	private int32 _ExpectedMin = 10;
	private int32 _ExpectedOverrideMask = 0;
	private int32 _CompletedOverrideMask = 0;
	private int32 _ExpectedMax = 100;
	private int32 _ExpectedCurrent = 50;
	private int32 _ExpectedCurrentPre = 50;
	private int32 _PendingPasses = 2;
	private int32 _ControlComponent = -1;
	private TArray<int32> _LegacyActions;
	private TArray<int32> _LegacyValues;
	private bool _LegacyCommandStarted = false;
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
		utils_entity_tag::Add(InHandle, n"TAG_IntegerGym_MinMaxCurrent");

		// Display timer
		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		// Auto timer
		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(3.0f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_Gym_AutoSet,
				FCk_Delegate_Messaging_OnBroadcast(this, n"OnAutoSet"));
			_MutationGateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(3.0f)); }

		// Auto config
		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests individual component manipulation (Min/Max/Current).\nShows component presence detection and separate change counters.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [2] Min/Max/Current station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Min component (10 -> 20)", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Max component (100 -> 80)", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Modify Current component (50 -> 75)", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test component detection", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Test value retrieval", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("Reset to defaults", 5, 5));
		AutoConfig.ManualCommands.Add("panel [8] Min preset - [9] Max preset - [0] Current preset");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(PowerLevelAttribute)) { _AuthorityFailed = true; return; }
		Request_BindSignals();

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_SetValue,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetValue"));
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto PowerParams = FCk_IntegerAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.PowerLevel"),
			50);
		PowerParams.Set_MinMax(ECk_MinMax::MinMax);
		PowerParams.Set_MinValue(10);
		PowerParams.Set_MaxValue(100);
		PowerLevelAttribute = utils_integer_attribute::Add(InHandle, PowerParams);
	}

	void
	Request_BindSignals()
	{
		utils_integer_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Min,
			FCk_Delegate_IntegerAttribute_OnValueChanged(this, n"OnMinChanged"));
		utils_integer_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Max,
			FCk_Delegate_IntegerAttribute_OnValueChanged(this, n"OnMaxChanged"));
		utils_integer_attribute::BindTo_OnValueChanged(PowerLevelAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_IntegerAttribute_OnValueChanged(this, n"OnCurrentChanged"));
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (_PendingMutation)
		{ _PendingPasses++; if (_SucceededRequestCompletions == _ExpectedRequestCompletions &&
			Get_HasExpectedAuthorityValues()) { _PendingMutation = false; } }
		Request_TickFinishAuthorityTransaction();
		Request_TickLegacyCommands();
		if (Get_HasValidAuthorityComposition()) { Request_UpdateDisplay(); }
	}

	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (_MutationGateClosed || _PendingMutation || !Get_HasValidAuthorityComposition()) { return; }
		auto Step = AutoStep % AutoConfig.TotalSteps;

		if (Step == 0) { Request_ModifyMin(); }
		else if (Step == 1) { Request_ModifyMax(); }
		else if (Step == 2) { Request_ModifyCurrent(); }
		else if (Step == 3) { /* component detection shown in display */ }
		else if (Step == 4) { /* value retrieval shown in display */ }
		else if (Step == 5) { Request_ResetToDefaults(); }

		AutoStep++;
	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 7 : 6, 0);
	}

	void Request_ModifyMin()
	{
		Request_TrackPowerChange(20, ECk_MinMaxCurrent::Min);
	}

	void Request_ModifyMax()
	{
		Request_TrackPowerChange(80, ECk_MinMaxCurrent::Max);
	}

	void Request_ModifyCurrent()
	{
		Request_TrackPowerChange(75, ECk_MinMaxCurrent::Current);
	}

	void Request_ResetToDefaults()
	{
		Request_TrackPowerReset();
	}

	void
	Request_UpdateDisplay()
	{
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "INTEGER MIN/MAX/CURRENT (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Min Changes: {MinChangeCount} | Max Changes: {MaxChangeCount}\n";
		DisplayText = f"{DisplayText}Current Changes: {CurrentChangeCount}\n\n";

		auto HasMin = utils_integer_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
		auto HasMax = utils_integer_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
		auto HasCurrent = utils_integer_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}Components Present:\n";
		DisplayText = f"{DisplayText}  Min: " + (HasMin ? "YES" : "NO") + "\n";
		DisplayText = f"{DisplayText}  Max: " + (HasMax ? "YES" : "NO") + "\n";
		DisplayText = f"{DisplayText}  Current: " + (HasCurrent ? "YES" : "NO") + "\n\n";

		auto MinValue = utils_integer_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
		auto MaxValue = utils_integer_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
		auto CurrentValue = utils_integer_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);

		DisplayText = f"{DisplayText}Power Level:\n";
		DisplayText = f"{DisplayText}  Min: {MinValue} (Changes: {MinChangeCount})\n";
		DisplayText = f"{DisplayText}  Max: {MaxValue} (Changes: {MaxChangeCount})\n";
		DisplayText = f"{DisplayText}  Current: {CurrentValue} (Changes: {CurrentChangeCount})\n\n";

		auto PowerBar = CkGym_Attribute::Create_ProgressBar((CurrentValue - MinValue), float32(MaxValue - MinValue), 25);
		DisplayText = f"{DisplayText}[{PowerBar}]\n";
		DisplayText = f"{DisplayText}  {MinValue} <--- {CurrentValue} ---> {MaxValue}\n";

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		LastDisplayTitle = TitleText; LastDisplayBody = DisplayText;
		if (!WaitForWorldReady) { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, DisplayText, ""); }
	}

	UFUNCTION() void OnMinChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnValueChanged InPayload) {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(PowerLevelAttribute))) { return; }
 MinChangeCount++; }
	UFUNCTION() void OnMaxChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnValueChanged InPayload) {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(PowerLevelAttribute))) { return; }
 MaxChangeCount++; }
	UFUNCTION() void OnCurrentChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnValueChanged InPayload) {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(PowerLevelAttribute))) { return; }
 CurrentChangeCount++; }

	UFUNCTION()
	private void OnSetValue(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_IntegerGym_SetValue);
		if (Typed.Component != ECk_MinMaxCurrent::Min &&
			Typed.Component != ECk_MinMaxCurrent::Max &&
			Typed.Component != ECk_MinMaxCurrent::Current)
		{ if (!WaitForWorldReady) { RejectedLegacyCommandCount++; } return; }
		int32 Action = Typed.Component == ECk_MinMaxCurrent::Min ? 12 :
			(Typed.Component == ECk_MinMaxCurrent::Max ? 13 : 14);
		Request_QueueLegacyCommand(Action, Typed.Value);
	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		Request_QueueLegacyCommand(26, 0);

	}
    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (_AuthorityFailed || !CkAttributeInteger::IsLiveEntity(Self)) { return false; }
        auto Display = FCk_Handle(DisplayTimer);
        auto Auto = FCk_Handle(AutoTimer);
        auto Power = FCk_Handle(PowerLevelAttribute);
        if (!CkAttributeInteger::IsLiveEntity(Display) ||
            !CkAttributeInteger::IsLiveEntity(Auto) ||
            !CkAttributeInteger::IsLiveEntity(Power)) { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Power) != Self ||
            !utils_timer::DoCast(Display).IsSet() ||
            !utils_timer::DoCast(Auto).IsSet() ||
            !utils_integer_attribute::DoCast(Power).IsSet()) { return false; }
        return utils_integer_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Min) &&
            utils_integer_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Max) &&
            utils_integer_attribute::Has_Component(PowerLevelAttribute, ECk_MinMaxCurrent::Current) &&
            PowerLevelAttribute == utils_integer_attribute::TryGet(Self,
                utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.PowerLevel"));
    }
    bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
    bool Get_HasSettledAuthorityState()
    { return Get_HasValidAuthorityComposition() && !_PendingMutation && !_FinishPending; }
    bool Request_StartAuthorityDemo()
    {
        if (!WaitForWorldReady || _AuthorityStarted || !Get_HasValidAuthorityComposition() ||
            AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }
        _AuthorityStarted = true; _StartRequestSucceeded = false;
        auto Requested = utils_timer::Request_Resume(AutoTimer,
            FCk_Delegate_Request_OnCompleted(this, n"OnStartResumeCompleted"));
        if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; return false; }
        return !_AuthorityFailed;
    }
    UFUNCTION() private void OnStartResumeCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (!_AuthorityStarted || InOwner != FCk_Handle(AutoTimer)) { return; }
        _StartRequestSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
        if (!_StartRequestSucceeded) { _AuthorityFailed = true; }
    }
    bool Get_HasSettledAuthorityStart()
    {
        if (!_AuthorityStarted || !_StartRequestSucceeded || !Get_HasValidAuthorityComposition() ||
            AutoTimer.Get_CurrentState() != ECk_Timer_State::Running) { return false; }
        return true;
    }
    bool Request_ReleaseAuthorityStart()
    {
        if (!Get_HasSettledAuthorityStart()) { return false; }
        _MutationGateClosed = false; return true;
    }
    private bool Request_TrackPowerChange(int32 InValue, ECk_MinMaxCurrent InComponent)
    {
        if (!Get_HasValidAuthorityComposition()) { _AuthorityFailed = true; return false; }
        _ExpectedMin = utils_integer_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
        _ExpectedMax = utils_integer_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
        _ExpectedCurrent = utils_integer_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Current);
        // A bound-only recompute starts from the prior settled Current Final,
        // not from the previous raw PreClamp value.
        _ExpectedCurrentPre = utils_integer_attribute::Get_FinalValue(PowerLevelAttribute);
        if (InComponent == ECk_MinMaxCurrent::Min) { _ExpectedMin = InValue; }
        else if (InComponent == ECk_MinMaxCurrent::Max) { _ExpectedMax = InValue; }
        else if (InComponent == ECk_MinMaxCurrent::Current)
        { _ExpectedCurrent = InValue; _ExpectedCurrentPre = InValue; }
        else { return false; }
        _ControlComponent = int32(InComponent); _PendingMutation = true; _PendingPasses = 0;
        _ExpectedRequestCompletions = 1; _SucceededRequestCompletions = 0;
        _ExpectedOverrideMask = InComponent == ECk_MinMaxCurrent::Min ? 1 :
            (InComponent == ECk_MinMaxCurrent::Max ? 2 : 4); _CompletedOverrideMask = 0;
        auto Requested = utils_integer_attribute::Request_Override(PowerLevelAttribute, InValue, InComponent,
            FCk_Delegate_Request_OnCompleted(this, n"OnPowerOverrideCompleted"));
        if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; return false; }
        return true;
    }
    UFUNCTION() private void OnPowerOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        Request_RecordPowerOverride(InOwner, InResult, _ExpectedOverrideMask);
    }
    UFUNCTION() private void OnPowerMinResetCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
 Request_RecordPowerOverride(InOwner, InResult, 1); }
    UFUNCTION() private void OnPowerMaxResetCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
 Request_RecordPowerOverride(InOwner, InResult, 2); }
    UFUNCTION() private void OnPowerCurrentResetCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
 Request_RecordPowerOverride(InOwner, InResult, 4); }
    private void Request_RecordPowerOverride(FCk_Handle InOwner,
        ECk_Request_OperationResult InResult, int32 InBit)
    {
        if (!_PendingMutation) { return; }
        if (InOwner != FCk_Handle(PowerLevelAttribute) || InBit == 0 ||
            (_ExpectedOverrideMask & InBit) == 0 || (_CompletedOverrideMask & InBit) != 0)
        { _AuthorityFailed = true; return; }
        if (InResult != ECk_Request_OperationResult::Succeeded) { _AuthorityFailed = true; return; }
        _CompletedOverrideMask |= InBit; _SucceededRequestCompletions++;
        if (_SucceededRequestCompletions > _ExpectedRequestCompletions) { _AuthorityFailed = true; }
    }
    private void Request_TrackPowerReset()
    {
        if (!Get_HasValidAuthorityComposition()) { _AuthorityFailed = true; return; }
        _ExpectedMin = 10; _ExpectedMax = 100; _ExpectedCurrent = 50; _ExpectedCurrentPre = 50;
        _PendingMutation = true; _PendingPasses = 0;
        _ExpectedRequestCompletions = 3; _SucceededRequestCompletions = 0;
        _ExpectedOverrideMask = 7; _CompletedOverrideMask = 0;
        auto A = utils_integer_attribute::Request_Override(PowerLevelAttribute, 10, ECk_MinMaxCurrent::Min,
            FCk_Delegate_Request_OnCompleted(this, n"OnPowerMinResetCompleted"));
        auto B = utils_integer_attribute::Request_Override(PowerLevelAttribute, 100, ECk_MinMaxCurrent::Max,
            FCk_Delegate_Request_OnCompleted(this, n"OnPowerMaxResetCompleted"));
        auto C = utils_integer_attribute::Request_Override(PowerLevelAttribute, 50, ECk_MinMaxCurrent::Current,
            FCk_Delegate_Request_OnCompleted(this, n"OnPowerCurrentResetCompleted"));
        if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B) || ck::Is_NOT_Valid(C)) { _AuthorityFailed = true; }
    }
    private bool Get_HasExpectedAuthorityValues()
    {
        if (!Get_HasValidAuthorityComposition() || _PendingPasses < 2) { return false; }
        auto Min = utils_integer_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min);
        auto Max = utils_integer_attribute::Get_FinalValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max);
        auto Current = utils_integer_attribute::Get_FinalValue(PowerLevelAttribute);
        return utils_integer_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Min) == _ExpectedMin &&
            utils_integer_attribute::Get_BaseValue(PowerLevelAttribute, ECk_MinMaxCurrent::Max) == _ExpectedMax &&
            Min == _ExpectedMin && Max == _ExpectedMax &&
            utils_integer_attribute::Get_BaseValue(PowerLevelAttribute) == Math::Min(Math::Max(_ExpectedCurrent, Min), Max) &&
            utils_integer_attribute::Get_PreClampFinalValue(PowerLevelAttribute) == _ExpectedCurrentPre &&
            Current == Math::Min(Math::Max(_ExpectedCurrentPre, Min), Max);
    }
    bool Request_TryPrepareAuthorityTransaction()
    {
        if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
            _ControlPending || _ResetPending || _FinishPending) { return false; }
        _FinishCompleted = false; _MutationGateClosed = true;
        if (_PendingMutation) { return false; }
        if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused)
        { _TransactionPrepared = !_PreparePauseRequested || _PreparePauseSucceeded; return _TransactionPrepared; }
        if (_PreparePauseRequested || AutoTimer.Get_CurrentState() != ECk_Timer_State::Running) { return false; }
        _PreparePauseRequested = true; _PreparePauseSucceeded = false;
        auto Requested = utils_timer::Request_Pause(AutoTimer,
            FCk_Delegate_Request_OnCompleted(this, n"OnPreparePauseCompleted"));
        if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; }
        return false;
    }
    UFUNCTION() private void OnPreparePauseCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (!_PreparePauseRequested || InOwner != FCk_Handle(AutoTimer)) { return; }
        _PreparePauseSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
        if (!_PreparePauseSucceeded) { _AuthorityFailed = true; }
    }
    bool Get_IsAuthorityTransactionPrepared()
    { return _TransactionPrepared && Get_HasValidAuthorityComposition() && !_PendingMutation &&
        AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
    bool Request_ApplyAuthorityControl(int32 InAction, int32 InValue)
    {
        if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
            !Get_IsValidAuthorityControl(InAction, InValue)) { return false; }
        auto Component = InAction == 12 ? ECk_MinMaxCurrent::Min :
            (InAction == 13 ? ECk_MinMaxCurrent::Max : ECk_MinMaxCurrent::Current);
        AutoRunning = false; _ControlPending = true;
        if (!Request_TrackPowerChange(InValue, Component)) { _AuthorityFailed = true; return false; }
        return true;
    }
    private bool Get_IsValidAuthorityControl(int32 InAction, int32 InValue)
    {
        if (InAction == 12) { return InValue == 10 || InValue == 20 || InValue == 60 || InValue == 120; }
        if (InAction == 13) { return InValue == 100 || InValue == 80 || InValue == 40 || InValue == 5; }
        if (InAction == 14) { return InValue == 50 || InValue == 75 || InValue == 150 || InValue == -20; }
        return false;
    }
    bool Get_HasSettledAuthorityControl()
    { return _ControlPending && _SucceededRequestCompletions == _ExpectedRequestCompletions &&
        Get_IsAuthorityTransactionPrepared() &&
        Get_HasExpectedAuthorityValues(); }
    bool Request_BeginAuthorityReset()
    {
        if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
        AutoStep = 0; MinChangeCount = 0; MaxChangeCount = 0; CurrentChangeCount = 0;
        AutoRunning = true; _ResetPending = true; Request_ResetToDefaults(); return !_AuthorityFailed;
    }
    bool Get_HasSettledAuthorityReset()
    { return _ResetPending && Get_IsAuthorityTransactionPrepared() && Get_HasExpectedAuthorityValues(); }
    bool Request_BeginFinishAuthorityTransaction(bool InResumeAuto)
    {
        if (_FinishPending || !Get_HasValidAuthorityComposition()) { return false; }
        if (_ControlPending && !Get_HasSettledAuthorityControl()) { return false; }
        if (_ResetPending && (!Get_HasSettledAuthorityReset() || !InResumeAuto)) { return false; }
        if (!_ControlPending && !_ResetPending && !Get_IsAuthorityTransactionPrepared()) { return false; }
        _FinishPending = true; _FinishCompleted = false; _FinishReleased = false; _FinishResume = InResumeAuto;
        _ResumeRequestSucceeded = !InResumeAuto;
        if (InResumeAuto)
        {
            auto Requested = utils_timer::Request_Resume(AutoTimer,
                FCk_Delegate_Request_OnCompleted(this, n"OnFinishResumeCompleted"));
            if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; _FinishPending = false; return false; }
        }
        return true;
    }
    UFUNCTION() private void OnFinishResumeCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (!_FinishPending || !_FinishResume || InOwner != FCk_Handle(AutoTimer)) { return; }
        _ResumeRequestSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
        if (!_ResumeRequestSucceeded) { _AuthorityFailed = true; _FinishPending = false; }
    }
    private void Request_TickFinishAuthorityTransaction()
    {
        if (!_FinishPending || !_ResumeRequestSucceeded || _PendingMutation ||
            !Get_HasExpectedAuthorityValues() ||
            AutoTimer.Get_CurrentState() != (_FinishResume ? ECk_Timer_State::Running : ECk_Timer_State::Paused)) { return; }
        _ControlPending = false; _ResetPending = false; _TransactionPrepared = false;
        _PreparePauseRequested = false; _PreparePauseSucceeded = false;
        _MutationGateClosed = WaitForWorldReady || !_FinishResume; AutoRunning = _FinishResume;
        _FinishPending = false; _FinishCompleted = true;
    }
    bool Get_IsAuthorityFinishPending() const { return _FinishPending; }
    bool Get_HasFinishedAuthorityTransaction() const { return _FinishCompleted; }
    bool Get_CanReleaseAuthorityTransaction()
    {
        return WaitForWorldReady && _FinishCompleted && !_FinishReleased && !_FinishPending &&
            !_AuthorityFailed && !_PendingMutation && Get_HasExpectedAuthorityValues() &&
            AutoTimer.Get_CurrentState() ==
                (_FinishResume ? ECk_Timer_State::Running : ECk_Timer_State::Paused);
    }
    bool Request_ReleaseAuthorityTransaction()
    {
        if (!Get_CanReleaseAuthorityTransaction()) { return false; }
        _FinishReleased = true; _MutationGateClosed = !_FinishResume; return true;
    }
    private bool Request_QueueLegacyCommand(int32 InAction, int32 InValue)
    {
        if (WaitForWorldReady) { return false; }
        if (_AuthorityFailed ||
            !(InAction == 6 || InAction == 7 || InAction == 26 || Get_IsValidAuthorityControl(InAction, InValue)) ||
            ((InAction == 6 || InAction == 7 || InAction == 26) && InValue != 0))
        { RejectedLegacyCommandCount++; return false; }
        _LegacyActions.Add(InAction); _LegacyValues.Add(InValue); return true;
    }
    private void Request_TickLegacyCommands()
    {
        if (WaitForWorldReady || _AuthorityFailed) { return; }
        if (_LegacyActions.Num() != _LegacyValues.Num()) { _AuthorityFailed = true; return; }
        if (_LegacyActions.Num() == 0 || _FinishPending) { return; }
        if (_FinishCompleted)
        { _LegacyActions.RemoveAt(0); _LegacyValues.RemoveAt(0); _LegacyCommandStarted = false;
          _FinishCompleted = false; return; }
        auto Action = _LegacyActions[0]; auto Value = _LegacyValues[0];
        if (_LegacyCommandStarted)
        {
            if (_ControlPending && Get_HasSettledAuthorityControl())
            { if (!Request_BeginFinishAuthorityTransaction(false)) { _AuthorityFailed = true; } }
            else if (_ResetPending && Get_HasSettledAuthorityReset())
            { if (!Request_BeginFinishAuthorityTransaction(true)) { _AuthorityFailed = true; } }
            return;
        }
        if (Action == 7 && AutoTimer.Get_CurrentState() == ECk_Timer_State::Running && !_MutationGateClosed)
        { _LegacyActions.RemoveAt(0); _LegacyValues.RemoveAt(0); AutoRunning = true; return; }
        if (!Request_TryPrepareAuthorityTransaction()) { return; }
        if (Action == 6 || Action == 7)
        { AutoRunning = Action == 7;
          if (!Request_BeginFinishAuthorityTransaction(Action == 7)) { _AuthorityFailed = true; } }
        else if (Action == 26)
        { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; } else { _LegacyCommandStarted = true; } }
        else if (!Request_ApplyAuthorityControl(Action, Value)) { _AuthorityFailed = true; }
        else { _LegacyCommandStarted = true; }
    }
}
