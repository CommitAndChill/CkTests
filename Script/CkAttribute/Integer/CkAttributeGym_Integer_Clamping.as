//============================================================================
// INTEGER CLAMPING & SIGNALS ENTITY SCRIPT
//============================================================================

class UCk_EntityScript_IntegerGym_Clamping : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FString LastDisplayTitle;
	FString LastDisplayBody;
	int32 RejectedLegacyCommandCount = 0;

	FCk_Handle_IntegerAttribute ResourceAttribute;

	FCk_Handle_Timer AutoTimer;
	FCk_Handle_Timer DisplayTimer;
	private bool _AuthorityStarted = false;
	private bool _AuthorityFailed = false;
	private bool _MutationGateClosed = false;
	private bool _PendingMutation = false;
	private bool _OverrideSucceeded = false;
	private int32 _PendingPasses = 2;
	private int32 _ExpectedInput = 50;
	private int32 _ExpectedPre = 50;
	private bool _PreparePauseRequested = false;
	private bool _AutoPauseSucceeded = false;
	private bool _UpdatePauseSucceeded = false;
	private bool _TransactionPrepared = false;
	private bool _ControlPending = false;
	private bool _ResetPending = false;
	private int32 _ControlAction = -1;
	private bool _FinishPending = false;
	private bool _FinishCompleted = false;
	private bool _FinishReleased = false;
	private bool _FinishResume = false;
	private bool _AutoResumeSucceeded = false;
	private bool _UpdateResumeSucceeded = false;
	private bool _StartAutoSucceeded = false;
	private bool _StartUpdateSucceeded = false;
	private int32 _DisplayPass = 0;
	private int32 _BoundaryPhase = 0;
	private int32 _BoundaryMinObservedPass = 0;
	int32 ClampEventSerial = 0;
	int32 BoundaryMinEventSerial = 0;
	int32 BoundaryMaxEventSerial = 0;
	int32 BoundaryMinPre = 0;
	int32 BoundaryMinFinal = 0;
	int32 BoundaryMinOverflow = 0;
	int32 BoundaryMaxPre = 0;
	int32 BoundaryMaxFinal = 0;
	int32 BoundaryMaxOverflow = 0;
	private TArray<int32> _LegacyActions;
	private TArray<int32> _LegacyValues;
	private bool _LegacyCommandStarted = false;
	FCk_Handle_Timer UpdateTimer;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;

	int32 MinClampCount = 0;
	int32 MaxClampCount = 0;
	int32 LastInputValue = 50;
	int32 LastPreClampValue = 0;
	int32 LastClampedValue = 0;
	int32 LastOverflow = 0;

	// Auto-cycling test values
	int32 CurrentTestValue = 50;
	bool IsIncreasing = true;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow
	DoConstruct(
		FCk_Handle& InHandle)
	{
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_IntegerGym_Clamping");

		// Display timer
		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		// Update timer for continuous value cycling
		auto UpdateTimerParams = FCk_Timer_Spec(FCk_Time(1.5f));
		UpdateTimerParams.Set_StartingState(WaitForWorldReady ? ECk_Timer_State::Paused : ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		UpdateTimer = utils_timer::Add(InHandle, UpdateTimerParams);
		UpdateTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"UpdateTick"));

		// Auto timer (controls on/off for the cycling)
		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(1.5f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_Gym_AutoSet,
				FCk_Delegate_Messaging_OnBroadcast(this, n"OnAutoSet"));
			_MutationGateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(1.5f)); }

		// Auto config
		AutoConfig.TotalSteps = 1;
		AutoConfig.Description = "Auto-cycles values beyond min/max to trigger OnMinClamped and OnMaxClamped.";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [4] Clamping station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("Cycling Resource value (0-100 range)", 0, 0));
		AutoConfig.ManualCommands.Add("panel [U] Resource preset - 50 / 0 / 100 / 150 / -50");
		AutoConfig.ManualCommands.Add("panel [B] Test boundaries - [R] Reset clamping station");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void
	DoBeginPlay(
		FCk_Handle InHandle)
	{
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(ResourceAttribute)) { _AuthorityFailed = true; return; }
		Request_BindSignals();

		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_TestBoundaries,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnTestBoundaries"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_SetResource,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetResource"));
	}

	void
	Request_SetupAttributes(
		FCk_Handle InHandle)
	{
		auto ResourceParams = FCk_IntegerAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Resource"),
			50);
		ResourceParams.Set_MinMax(ECk_MinMax::MinMax);
		ResourceParams.Set_MinValue(0);
		ResourceParams.Set_MaxValue(100);
		ResourceAttribute = utils_integer_attribute::Add(InHandle, ResourceParams);
	}

	void
	Request_BindSignals()
	{
		utils_integer_attribute::BindTo_OnMinClamped(ResourceAttribute,
			FCk_Delegate_IntegerAttribute_OnClamped(this, n"OnMinClamped"));
		utils_integer_attribute::BindTo_OnMaxClamped(ResourceAttribute,
			FCk_Delegate_IntegerAttribute_OnClamped(this, n"OnMaxClamped"));
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		_DisplayPass++;
		if (_PendingMutation)
		{ _PendingPasses++; if (_OverrideSucceeded && Get_HasExpectedResource()) { _PendingMutation = false; } }
		Request_TickBoundary();
		Request_TickFinishAuthorityTransaction();
		Request_TickLegacyCommands();
		if (Get_HasValidAuthorityComposition()) { Request_UpdateDisplay(); }
	}

	UFUNCTION()
	private void UpdateTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (!_MutationGateClosed && AutoRunning && !_PendingMutation &&
			Get_HasValidAuthorityComposition()) { Request_AutoUpdateValue(); }
	}

	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (!_MutationGateClosed && AutoRunning && Get_HasValidAuthorityComposition()) { AutoStep++; }
	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 7 : 6, 0);
	}

	void Request_AutoUpdateValue()
	{
		if (ck::Is_NOT_Valid(ResourceAttribute))
			return;

		if (IsIncreasing)
		{
			CurrentTestValue += 20;
			if (CurrentTestValue >= 120) { IsIncreasing = false; }
		}
		else
		{
			CurrentTestValue -= 25;
			if (CurrentTestValue <= -20) { IsIncreasing = true; }
		}

		LastInputValue = CurrentTestValue;
		Request_TrackResourceChange(CurrentTestValue);
	}

	void
	Request_UpdateDisplay()
	{
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "INTEGER CLAMPING (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		DisplayText = f"{DisplayText}Min Clamps: {MinClampCount} | Max Clamps: {MaxClampCount}\n";

		if (MinClampCount > 0 || MaxClampCount > 0)
		{
			DisplayText = f"{DisplayText}Last Clamp: pre={LastPreClampValue} clamped={LastClampedValue} overflow={LastOverflow}\n";
		}

		// Live polling via utility accessors - updates every frame (this-frame values).
		auto LivePre = utils_integer_attribute::Get_PreClampFinalValue(ResourceAttribute);
		auto LiveOvr = utils_integer_attribute::Get_ClampOverflow(ResourceAttribute);
		DisplayText = f"{DisplayText}Live Poll:  pre={LivePre}  overflow={LiveOvr}\n";

		DisplayText = DisplayText + "\n";

		auto ResourceValue = utils_integer_attribute::Get_FinalValue(ResourceAttribute);
		auto ClampStatus = "";

		if (ResourceValue == 0 && LastInputValue < 0) { ClampStatus = " [MIN CLAMPED]"; }
		else if (ResourceValue == 100 && LastInputValue > 100) { ClampStatus = " [MAX CLAMPED]"; }
		else { ClampStatus = " [NORMAL]"; }

		DisplayText = f"{DisplayText}Resource: {ResourceValue}/100\n";
		DisplayText = f"{DisplayText}Input Value: {LastInputValue}\n";
		DisplayText = f"{DisplayText}Status: " + ClampStatus + "\n";
		DisplayText = f"{DisplayText}Direction: " + (IsIncreasing ? "INCREASING" : "DECREASING") + "\n\n";

		auto ResourceBar = CkGym_Attribute::Create_ProgressBar(ResourceValue, 100.0f, 20);
		DisplayText = f"{DisplayText}[{ResourceBar}]\n";

		DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		LastDisplayTitle = TitleText; LastDisplayBody = DisplayText;
		if (!WaitForWorldReady) { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, DisplayText, ""); }
	}

	UFUNCTION() void OnMinClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnClamped InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(ResourceAttribute))) { return; }

		MinClampCount++; ClampEventSerial++;
		if (_BoundaryPhase == 1 && InPayload.Get_PreClampFinalValue() == -50 &&
			InPayload.Get_FinalClampedValue() == 0 && InPayload.Get_ClampOverflow() == -50)
		{ BoundaryMinEventSerial = ClampEventSerial; BoundaryMinPre = -50; BoundaryMinFinal = 0;
		  BoundaryMinOverflow = -50; _BoundaryMinObservedPass = _DisplayPass; _BoundaryPhase = 2; }
		LastPreClampValue = InPayload.Get_PreClampFinalValue();
		LastClampedValue = InPayload.Get_FinalClampedValue();
		LastOverflow = InPayload.Get_ClampOverflow();
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(-50.0f, 0.0f, 150.0f), FLinearColor(0.0f, 0.0f, 1.0f, 1.0f)); }
	}

	UFUNCTION() void OnMaxClamped(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnClamped InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(ResourceAttribute))) { return; }

		MaxClampCount++; ClampEventSerial++;
		if (_BoundaryPhase == 3 && InPayload.Get_PreClampFinalValue() == 150 &&
			InPayload.Get_FinalClampedValue() == 100 && InPayload.Get_ClampOverflow() == 50)
		{ BoundaryMaxEventSerial = ClampEventSerial; BoundaryMaxPre = 150; BoundaryMaxFinal = 100;
		  BoundaryMaxOverflow = 50; _BoundaryPhase = 4; }
		LastPreClampValue = InPayload.Get_PreClampFinalValue();
		LastClampedValue = InPayload.Get_FinalClampedValue();
		LastOverflow = InPayload.Get_ClampOverflow();
		if (!WaitForWorldReady) { CkGym_Attribute::Draw_ClampIndicator(ck::ToEntity(this), FVector(50.0f, 0.0f, 150.0f), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)); }
	}

	UFUNCTION()
	private void OnSetResource(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_IntegerGym_SetResource);
		Request_QueueLegacyCommand(22, Typed.Value);
	}

	UFUNCTION()
	private void OnTestBoundaries(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		Request_QueueLegacyCommand(23, 0);
	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		Request_QueueLegacyCommand(24, 0);
	}
    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (_AuthorityFailed || !CkAttributeInteger::IsLiveEntity(Self)) { return false; }
        auto Display = FCk_Handle(DisplayTimer);
        auto Auto = FCk_Handle(AutoTimer);
        auto Update = FCk_Handle(UpdateTimer);
        auto Resource = FCk_Handle(ResourceAttribute);
        if (!CkAttributeInteger::IsLiveEntity(Display) ||
            !CkAttributeInteger::IsLiveEntity(Auto) ||
            !CkAttributeInteger::IsLiveEntity(Update) ||
            !CkAttributeInteger::IsLiveEntity(Resource)) { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Update) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Resource) != Self ||
            !utils_timer::DoCast(Display).IsSet() ||
            !utils_timer::DoCast(Auto).IsSet() ||
            !utils_timer::DoCast(Update).IsSet() ||
            !utils_integer_attribute::DoCast(Resource).IsSet()) { return false; }
        return utils_integer_attribute::Has_Component(ResourceAttribute, ECk_MinMaxCurrent::Min) &&
            utils_integer_attribute::Has_Component(ResourceAttribute, ECk_MinMaxCurrent::Max) &&
            utils_integer_attribute::Has_Component(ResourceAttribute, ECk_MinMaxCurrent::Current) &&
            ResourceAttribute == utils_integer_attribute::TryGet(Self,
                utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Resource"));
    }
    bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
    bool Get_HasSettledAuthorityState()
    { return Get_HasValidAuthorityComposition() && !_PendingMutation && !_FinishPending; }
    bool Request_StartAuthorityDemo()
    {
        if (!WaitForWorldReady || _AuthorityStarted || !Get_HasValidAuthorityComposition() ||
            AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused ||
            UpdateTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }
        _AuthorityStarted = true; _StartAutoSucceeded = false; _StartUpdateSucceeded = false;
        auto A = utils_timer::Request_Resume(AutoTimer,
            FCk_Delegate_Request_OnCompleted(this, n"OnStartAutoCompleted"));
        auto B = utils_timer::Request_Resume(UpdateTimer,
            FCk_Delegate_Request_OnCompleted(this, n"OnStartUpdateCompleted"));
        if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B)) { _AuthorityFailed = true; return false; }
        return !_AuthorityFailed;
    }
    UFUNCTION() private void OnStartAutoCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (!_AuthorityStarted || InOwner != FCk_Handle(AutoTimer)) { return; }
        _StartAutoSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
        if (!_StartAutoSucceeded) { _AuthorityFailed = true; }
    }
    UFUNCTION() private void OnStartUpdateCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (!_AuthorityStarted || InOwner != FCk_Handle(UpdateTimer)) { return; }
        _StartUpdateSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
        if (!_StartUpdateSucceeded) { _AuthorityFailed = true; }
    }
    bool Get_HasSettledAuthorityStart()
    {
        if (!_AuthorityStarted || !_StartAutoSucceeded || !_StartUpdateSucceeded ||
            !Get_HasValidAuthorityComposition() ||
            AutoTimer.Get_CurrentState() != ECk_Timer_State::Running ||
            UpdateTimer.Get_CurrentState() != ECk_Timer_State::Running) { return false; }
        return true;
    }
    bool Request_ReleaseAuthorityStart()
    {
        if (!Get_HasSettledAuthorityStart()) { return false; }
        _MutationGateClosed = false; return true;
    }
    private bool Request_TrackResourceChange(int32 InValue)
    {
        if (!Get_HasValidAuthorityComposition() || _PendingMutation) { _AuthorityFailed = true; return false; }
        _ExpectedInput = InValue;
        _ExpectedPre = InValue;
        _PendingMutation = true; _OverrideSucceeded = false; _PendingPasses = 0;
        auto Requested = utils_integer_attribute::Request_Override(ResourceAttribute, InValue,
            ECk_MinMaxCurrent::Current, FCk_Delegate_Request_OnCompleted(this, n"OnResourceOverrideCompleted"));
        if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; return false; }
        return true;
    }
    UFUNCTION() private void OnResourceOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (!_PendingMutation || InOwner != FCk_Handle(ResourceAttribute) || _OverrideSucceeded)
        { _AuthorityFailed = true; return; }
        _OverrideSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
        if (!_OverrideSucceeded) { _AuthorityFailed = true; }
    }
    private bool Get_HasExpectedResource()
    {
        if (!Get_HasValidAuthorityComposition() || _PendingPasses < 2) { return false; }
        auto Final = Math::Clamp(_ExpectedInput, 0, 100);
        return utils_integer_attribute::Get_BaseValue(ResourceAttribute) == Final &&
            utils_integer_attribute::Get_PreClampFinalValue(ResourceAttribute) == _ExpectedPre &&
            utils_integer_attribute::Get_FinalValue(ResourceAttribute) == Final &&
            utils_integer_attribute::Get_ClampOverflow(ResourceAttribute) == _ExpectedPre - Final;
    }
    bool Request_TryPrepareAuthorityTransaction()
    {
        if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
            _ControlPending || _ResetPending || _FinishPending) { return false; }
        _FinishCompleted = false; _MutationGateClosed = true;
        if (_PendingMutation) { return false; }
        if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused &&
            UpdateTimer.Get_CurrentState() == ECk_Timer_State::Paused)
        { _TransactionPrepared = !_PreparePauseRequested || (_AutoPauseSucceeded && _UpdatePauseSucceeded);
          return _TransactionPrepared; }
        if (_PreparePauseRequested) { return false; }
        _PreparePauseRequested = true;
        _AutoPauseSucceeded = AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused;
        _UpdatePauseSucceeded = UpdateTimer.Get_CurrentState() == ECk_Timer_State::Paused;
        if (!_AutoPauseSucceeded)
        { auto A = utils_timer::Request_Pause(AutoTimer,
            FCk_Delegate_Request_OnCompleted(this, n"OnAutoPauseCompleted"));
          if (ck::Is_NOT_Valid(A)) { _AuthorityFailed = true; } }
        if (!_UpdatePauseSucceeded)
        { auto B = utils_timer::Request_Pause(UpdateTimer,
            FCk_Delegate_Request_OnCompleted(this, n"OnUpdatePauseCompleted"));
          if (ck::Is_NOT_Valid(B)) { _AuthorityFailed = true; } }
        return false;
    }
    UFUNCTION() private void OnAutoPauseCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
 if (!_PreparePauseRequested || InOwner != FCk_Handle(AutoTimer)) { return; }
      _AutoPauseSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
      if (!_AutoPauseSucceeded) { _AuthorityFailed = true; } }
    UFUNCTION() private void OnUpdatePauseCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
 if (!_PreparePauseRequested || InOwner != FCk_Handle(UpdateTimer)) { return; }
      _UpdatePauseSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
      if (!_UpdatePauseSucceeded) { _AuthorityFailed = true; } }
    bool Get_IsAuthorityTransactionPrepared()
    { return _TransactionPrepared && Get_HasValidAuthorityComposition() && !_PendingMutation &&
        AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused &&
        UpdateTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
    bool Request_ApplyAuthorityControl(int32 InAction, int32 InValue)
    {
        if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
        if (InAction == 22)
        {
            if (InValue != 50 && InValue != 0 && InValue != 100 && InValue != 150 && InValue != -50) { return false; }
            _ControlPending = true; _ControlAction = 22; AutoRunning = false;
            CurrentTestValue = InValue; LastInputValue = InValue;
            return Request_TrackResourceChange(InValue);
        }
        if (InAction != 23 || InValue != 0) { return false; }
        _ControlPending = true; _ControlAction = 23; AutoRunning = false;
        // A fresh min-clamp signal needs a transition from an interior value.
        // If auto/manual input left Resource at min, first settle 50 before -50.
        _BoundaryPhase = utils_integer_attribute::Get_FinalValue(ResourceAttribute) == 0 ? 0 : 1;
        BoundaryMinEventSerial = 0; BoundaryMaxEventSerial = 0;
        BoundaryMinPre = 0; BoundaryMinFinal = 0; BoundaryMinOverflow = 0;
        BoundaryMaxPre = 0; BoundaryMaxFinal = 0; BoundaryMaxOverflow = 0;
        if (!WaitForWorldReady)
        { CkGym_Common::Draw_DebugSphere(ck::ToEntity(this), FVector(0.0f, 0.0f, 250.0f),
            FLinearColor(1.0f, 1.0f, 0.0f, 1.0f), 25.0f, 3.0f, 2.0f); }
        return Request_TrackResourceChange(_BoundaryPhase == 0 ? 50 : -50);
    }
    private void Request_TickBoundary()
    {
        if (_ControlPending && _ControlAction == 23 && _BoundaryPhase == 0 &&
            !_PendingMutation && Get_HasExpectedResource())
        { _BoundaryPhase = 1; Request_TrackResourceChange(-50); return; }
        if (!_ControlPending || _ControlAction != 23 || _BoundaryPhase != 2 ||
            _PendingMutation || BoundaryMinEventSerial <= 0 || _DisplayPass <= _BoundaryMinObservedPass ||
            !Get_HasExpectedResource()) { return; }
        _BoundaryPhase = 3;
        Request_TrackResourceChange(150);
    }
    bool Get_HasSettledAuthorityControl()
    {
        if (!_ControlPending || !Get_IsAuthorityTransactionPrepared() || _PendingMutation ||
            !Get_HasExpectedResource()) { return false; }
        if (_ControlAction == 22) { return true; }
        return _ControlAction == 23 && _BoundaryPhase == 4 &&
            BoundaryMinEventSerial > 0 && BoundaryMaxEventSerial > BoundaryMinEventSerial &&
            BoundaryMinPre == -50 && BoundaryMinFinal == 0 && BoundaryMinOverflow == -50 &&
            BoundaryMaxPre == 150 && BoundaryMaxFinal == 100 && BoundaryMaxOverflow == 50;
    }
    bool Request_BeginAuthorityReset()
    {
        if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
        AutoStep = 0; MinClampCount = 0; MaxClampCount = 0;
        CurrentTestValue = 50; LastInputValue = 50; LastPreClampValue = 0;
        LastClampedValue = 0; LastOverflow = 0; IsIncreasing = true;
        _ResetPending = true; AutoRunning = true;
        return Request_TrackResourceChange(50);
    }
    bool Get_HasSettledAuthorityReset()
    { return _ResetPending && Get_IsAuthorityTransactionPrepared() && !_PendingMutation &&
        Get_HasExpectedResource(); }
    bool Request_BeginFinishAuthorityTransaction(bool InResumeAuto)
    {
        if (_FinishPending || !Get_HasValidAuthorityComposition()) { return false; }
        if (_ControlPending && !Get_HasSettledAuthorityControl()) { return false; }
        if (_ResetPending && (!Get_HasSettledAuthorityReset() || !InResumeAuto)) { return false; }
        if (!_ControlPending && !_ResetPending && !Get_IsAuthorityTransactionPrepared()) { return false; }
        _FinishPending = true; _FinishCompleted = false; _FinishReleased = false; _FinishResume = InResumeAuto;
        _AutoResumeSucceeded = !InResumeAuto; _UpdateResumeSucceeded = !InResumeAuto;
        if (InResumeAuto)
        {
            auto A = utils_timer::Request_Resume(AutoTimer,
                FCk_Delegate_Request_OnCompleted(this, n"OnAutoResumeCompleted"));
            auto B = utils_timer::Request_Resume(UpdateTimer,
                FCk_Delegate_Request_OnCompleted(this, n"OnUpdateResumeCompleted"));
            if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B))
            { _AuthorityFailed = true; _FinishPending = false; return false; }
        }
        return true;
    }
    UFUNCTION() private void OnAutoResumeCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
 if (!_FinishPending || !_FinishResume || InOwner != FCk_Handle(AutoTimer)) { return; }
      _AutoResumeSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
      if (!_AutoResumeSucceeded) { _AuthorityFailed = true; _FinishPending = false; } }
    UFUNCTION() private void OnUpdateResumeCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
 if (!_FinishPending || !_FinishResume || InOwner != FCk_Handle(UpdateTimer)) { return; }
      _UpdateResumeSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
      if (!_UpdateResumeSucceeded) { _AuthorityFailed = true; _FinishPending = false; } }
    private void Request_TickFinishAuthorityTransaction()
    {
        if (!_FinishPending || !_AutoResumeSucceeded || !_UpdateResumeSucceeded ||
            _PendingMutation || !Get_HasExpectedResource() ||
            AutoTimer.Get_CurrentState() != (_FinishResume ? ECk_Timer_State::Running : ECk_Timer_State::Paused) ||
            UpdateTimer.Get_CurrentState() != (_FinishResume ? ECk_Timer_State::Running : ECk_Timer_State::Paused)) { return; }
        _ControlPending = false; _ResetPending = false; _ControlAction = -1;
        _TransactionPrepared = false; _PreparePauseRequested = false;
        _AutoPauseSucceeded = false; _UpdatePauseSucceeded = false;
        _MutationGateClosed = WaitForWorldReady || !_FinishResume; AutoRunning = _FinishResume;
        _FinishPending = false; _FinishCompleted = true;
    }
    bool Get_IsAuthorityFinishPending() const { return _FinishPending; }
    bool Get_HasFinishedAuthorityTransaction() const { return _FinishCompleted; }
    bool Get_CanReleaseAuthorityTransaction()
    {
        return WaitForWorldReady && _FinishCompleted && !_FinishReleased && !_FinishPending &&
            !_AuthorityFailed && !_PendingMutation && Get_HasExpectedResource() &&
            AutoTimer.Get_CurrentState() ==
                (_FinishResume ? ECk_Timer_State::Running : ECk_Timer_State::Paused) &&
            UpdateTimer.Get_CurrentState() ==
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
            !(InAction == 6 || InAction == 7 || InAction == 23 || InAction == 24 ||
              (InAction == 22 && (InValue == 50 || InValue == 0 || InValue == 100 || InValue == 150 || InValue == -50))) ||
            ((InAction == 6 || InAction == 7 || InAction == 23 || InAction == 24) && InValue != 0))
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
        if (Action == 7 && AutoTimer.Get_CurrentState() == ECk_Timer_State::Running &&
            UpdateTimer.Get_CurrentState() == ECk_Timer_State::Running && !_MutationGateClosed)
        { _LegacyActions.RemoveAt(0); _LegacyValues.RemoveAt(0); AutoRunning = true; return; }
        if (!Request_TryPrepareAuthorityTransaction()) { return; }
        if (Action == 6 || Action == 7)
        { AutoRunning = Action == 7;
          if (!Request_BeginFinishAuthorityTransaction(Action == 7)) { _AuthorityFailed = true; } }
        else if (Action == 24)
        { if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; } else { _LegacyCommandStarted = true; } }
        else if (!Request_ApplyAuthorityControl(Action, Value)) { _AuthorityFailed = true; }
        else { _LegacyCommandStarted = true; }
    }
}
