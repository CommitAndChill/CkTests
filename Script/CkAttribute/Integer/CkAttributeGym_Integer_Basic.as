//============================================================================
// INTEGER BASIC ATTRIBUTES ENTITY SCRIPT
//============================================================================

class UCk_EntityScript_IntegerGym_Basic : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;
	UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
	FString LastDisplayTitle;
	FString LastDisplayBody;
	int32 RejectedLegacyCommandCount = 0;

	// Attribute handles
	FCk_Handle_IntegerAttribute HealthAttribute;
	FCk_Handle_IntegerAttribute ArmorAttribute;
	FCk_Handle_IntegerAttribute ExperienceAttribute;

	// Auto mode
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
	private int32 _ExpectedOverrideMask = 0;
	private int32 _CompletedOverrideMask = 0;
	private int32 _ExpectedHealth = 100;
	private int32 _ExpectedArmor = 25;
	private int32 _ExpectedExperience = 0;
	private int32 _ExpectedHealthPre = 100;
	private int32 _ExpectedArmorPre = 25;
	private int32 _ExpectedExperiencePre = 0;
	private TArray<int32> _LegacyActions;
	private TArray<int32> _LegacyValues;
	private bool _LegacyCommandStarted = false;
	bool AutoRunning = true;
	int32 AutoStep = 0;
	FCkGym_AutoConfig AutoConfig;

	// Signal counters
	int32 HealthChangeCount = 0;
	int32 ArmorChangeCount = 0;
	int32 ExperienceChangeCount = 0;

	//------------------------------------------------------------------------
	// Construction & Initialization
	//------------------------------------------------------------------------

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
		utils_entity_tag::Add(InHandle, n"TAG_IntegerGym_Basic");

		// Display timer (every frame)
		auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
		DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
		DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

		// Auto timer (2s cycle)
		if (WaitForWorldReady)
		{
			auto Params = FCk_Timer_Spec(FCk_Time(2.0f));
			Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
			AutoTimer = utils_timer::Add(InHandle, Params);
			AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
			utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_Gym_AutoSet,
				FCk_Delegate_Messaging_OnBroadcast(this, n"OnAutoSet"));
			_MutationGateClosed = true;
		}
		else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(2.0f)); }

		// Auto config
		AutoConfig.TotalSteps = 6;
		AutoConfig.Description = "Tests integer attributes: Health (0-100), Armor (0-50), Experience (0+).";
		AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
		AutoConfig.PerStationAutoCommand = "panel [1] Basic station auto";
		AutoConfig.Steps.Add(FCkGym_AutoStep("SetHealth 50, SetArmor 25, SetExperience 500", 0, 0));
		AutoConfig.Steps.Add(FCkGym_AutoStep("SetHealth 95, SetArmor 45, SetExperience 2000", 1, 1));
		AutoConfig.Steps.Add(FCkGym_AutoStep("TestBoundaries (push past max)", 2, 2));
		AutoConfig.Steps.Add(FCkGym_AutoStep("SetHealth 10, SetArmor 5, SetExperience 100", 3, 3));
		AutoConfig.Steps.Add(FCkGym_AutoStep("SetHealth -10, SetArmor -5 (push past min)", 4, 4));
		AutoConfig.Steps.Add(FCkGym_AutoStep("ResetToDefaults", 5, 5));
		AutoConfig.ManualCommands.Add("panel [5] Health preset - [6] Armor preset - [7] Experience preset");
		AutoConfig.ManualCommands.Add("panel [N] Reset all stations");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION(BlueprintOverride)
	void DoBeginPlay(FCk_Handle InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		Request_SetupAttributes(InHandle);
		if (ck::Is_NOT_Valid(HealthAttribute) || ck::Is_NOT_Valid(ArmorAttribute) ||
			ck::Is_NOT_Valid(ExperienceAttribute)) { _AuthorityFailed = true; return; }
		Request_BindSignals();
		Request_BindMessages(InHandle);
	}

	void Request_SetupAttributes(FCk_Handle InHandle)
	{
		// Health: 0-100, starts at 100
		auto HealthParams = FCk_IntegerAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Health"),
			100);
		HealthParams.Set_MinMax(ECk_MinMax::MinMax);
		HealthParams.Set_MinValue(0);
		HealthParams.Set_MaxValue(100);
		HealthAttribute = utils_integer_attribute::Add(InHandle, HealthParams);

		// Armor: 0-50, starts at 25
		auto ArmorParams = FCk_IntegerAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Armor"),
			25);
		ArmorParams.Set_MinMax(ECk_MinMax::MinMax);
		ArmorParams.Set_MinValue(0);
		ArmorParams.Set_MaxValue(50);
		ArmorAttribute = utils_integer_attribute::Add(InHandle, ArmorParams);

		// Experience: 0-unlimited, starts at 0
		auto ExperienceParams = FCk_IntegerAttribute_Spec(
			utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Experience"),
			0);
		ExperienceParams.Set_MinMax(ECk_MinMax::Min);
		ExperienceParams.Set_MinValue(0);
		ExperienceAttribute = utils_integer_attribute::Add(InHandle, ExperienceParams);
	}

	void Request_BindSignals()
	{
		utils_integer_attribute::BindTo_OnValueChanged(HealthAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_IntegerAttribute_OnValueChanged(this, n"OnHealthChanged"));
		utils_integer_attribute::BindTo_OnValueChanged(ArmorAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_IntegerAttribute_OnValueChanged(this, n"OnArmorChanged"));
		utils_integer_attribute::BindTo_OnValueChanged(ExperienceAttribute, ECk_MinMaxCurrent::Current,
			FCk_Delegate_IntegerAttribute_OnValueChanged(this, n"OnExperienceChanged"));
	}

	void Request_BindMessages(FCk_Handle InHandle)
	{
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_SetHealth,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetHealth"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_SetArmor,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetArmor"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_IntegerGym_SetExperience,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetExperience"));
		utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
			FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
	}

	//------------------------------------------------------------------------
	// Signal Callbacks
	//------------------------------------------------------------------------

	UFUNCTION()
	void OnHealthChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnValueChanged InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(HealthAttribute))) { return; }

		HealthChangeCount++;
	}

	UFUNCTION()
	void OnArmorChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnValueChanged InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(ArmorAttribute))) { return; }

		ArmorChangeCount++;
	}

	UFUNCTION()
	void OnExperienceChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_IntegerAttribute_OnValueChanged InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (InAttributeOwnerEntity != ck::ToEntity(this) ||
            !CkAttributeInteger::IsLiveEntity(FCk_Handle(ExperienceAttribute))) { return; }

		ExperienceChangeCount++;
	}

	//------------------------------------------------------------------------
	// Message Handlers
	//------------------------------------------------------------------------

	UFUNCTION()
	private void OnSetHealth(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_IntegerGym_SetHealth);
		Request_QueueLegacyCommand(8, Typed.Value);

	}

	UFUNCTION()
	private void OnSetArmor(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_IntegerGym_SetArmor);
		Request_QueueLegacyCommand(9, Typed.Value);

	}

	UFUNCTION()
	private void OnSetExperience(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_IntegerGym_SetExperience);
		Request_QueueLegacyCommand(10, Typed.Value);

	}

	UFUNCTION()
	private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		Request_QueueLegacyCommand(26, 0);

	}

	UFUNCTION()
	private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
		Request_QueueLegacyCommand(Typed.Enabled ? 7 : 6, 0);
	}

	//------------------------------------------------------------------------
	// Auto Mode
	//------------------------------------------------------------------------

	UFUNCTION()
	private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (_MutationGateClosed || _PendingMutation || !Get_HasValidAuthorityComposition()) { return; }
		auto Step = AutoStep % AutoConfig.TotalSteps;
		AutoStep++;

		switch (Step)
		{
			case 0: // SetHealth 50, SetArmor 25, SetExperience 500
				Request_SetAllValues(50, 25, 500);
				break;

			case 1: // SetHealth 95, SetArmor 45, SetExperience 2000
				Request_SetAllValues(95, 45, 2000);
				break;

			case 2: // TestBoundaries - push past max
				Request_TestBoundariesMax();
				break;

			case 3: // SetHealth 10, SetArmor 5, SetExperience 100
				Request_SetAllValues(10, 5, 100);
				break;

			case 4: // Push past min - Health -10 (clamps to 0), Armor -5 (clamps to 0)
				Request_SetAllValues(-10, -5, -50);
				break;

			case 5: // Reset to defaults
				Request_ResetToDefaults();
				break;
		}
	}

	// Shared operations used by both auto steps and manual message handlers
	void Request_SetAllValues(int32 InHealth, int32 InArmor, int32 InExperience)
	{
		if (!Get_HasValidAuthorityComposition()) { _AuthorityFailed = true; return; }
		_ExpectedHealth = InHealth; _ExpectedArmor = InArmor; _ExpectedExperience = InExperience;
		_ExpectedHealthPre = InHealth; _ExpectedArmorPre = InArmor; _ExpectedExperiencePre = InExperience;
		_PendingMutation = true; _ExpectedRequestCompletions = 3; _SucceededRequestCompletions = 0;
		_ExpectedOverrideMask = 7; _CompletedOverrideMask = 0;
		auto H = utils_integer_attribute::Request_Override(HealthAttribute, InHealth, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnBasicOverrideCompleted"));
		auto A = utils_integer_attribute::Request_Override(ArmorAttribute, InArmor, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnBasicOverrideCompleted"));
		auto X = utils_integer_attribute::Request_Override(ExperienceAttribute, InExperience, ECk_MinMaxCurrent::Current,
			FCk_Delegate_Request_OnCompleted(this, n"OnBasicOverrideCompleted"));
		if (ck::Is_NOT_Valid(H) || ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(X)) { _AuthorityFailed = true; }
	}

	void Request_TestBoundariesMax()
	{
		Request_SetAllValues(120, 60, 99999);
	}

	void Request_ResetToDefaults()
	{
		Request_SetAllValues(100, 25, 0);
	}

	//------------------------------------------------------------------------
	// Display
	//------------------------------------------------------------------------

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

		if (_PendingMutation && _SucceededRequestCompletions == _ExpectedRequestCompletions &&
			Get_HasExpectedAuthorityValues()) { _PendingMutation = false; }
		Request_TickFinishAuthorityTransaction();
		Request_TickLegacyCommands();
		if (Get_HasValidAuthorityComposition()) { DisplayCurrentValues(); }
	}

	void DisplayCurrentValues()
	{
		auto SelfEntity = ck::ToEntity(this);
		auto NetworkRole = CkGym_Common::Get_NetworkRoleTitle(SelfEntity);
		auto TitleText = "INTEGER BASIC (" + NetworkRole + ")";

		auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

		// Attribute values
		DisplayText = DisplayText + "===== Attribute Values =====\n";

		auto HealthValue = utils_integer_attribute::Get_FinalValue(HealthAttribute);
		auto HealthBar = CkGym_Attribute::Create_ProgressBar(HealthValue, 100.0f, 20);
		DisplayText = f"{DisplayText}Health: {HealthValue}/100\n";
		DisplayText = f"{DisplayText}[{HealthBar}]\n\n";

		auto ArmorValue = utils_integer_attribute::Get_FinalValue(ArmorAttribute);
		auto ArmorBar = CkGym_Attribute::Create_ProgressBar(ArmorValue, 50.0f, 20, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
		DisplayText = f"{DisplayText}Armor: {ArmorValue}/50\n";
		DisplayText = f"{DisplayText}[{ArmorBar}]\n\n";

		auto XPValue = utils_integer_attribute::Get_FinalValue(ExperienceAttribute);
		DisplayText = f"{DisplayText}Experience: {XPValue} (no max)\n\n";

		auto TotalChanges = HealthChangeCount + ArmorChangeCount + ExperienceChangeCount;
		DisplayText = f"{DisplayText}Changes: {TotalChanges} (H:{HealthChangeCount} A:{ArmorChangeCount} XP:{ExperienceChangeCount})\n";

		DisplayText = DisplayText + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

		LastDisplayTitle = TitleText; LastDisplayBody = DisplayText;
		if (!WaitForWorldReady)
		{
			auto Owner = utils_entity_lifetime::Get_LifetimeOwner(SelfEntity);
			auto& Fragment = Owner.AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
			Fragment.Title = FText::FromString(TitleText);
			Fragment.Description = FText::FromString(DisplayText);
		}
	}
    UFUNCTION() private void OnBasicOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }

        if (!_PendingMutation) { return; }
        int32 Bit = InOwner == FCk_Handle(HealthAttribute) ? 1 :
            (InOwner == FCk_Handle(ArmorAttribute) ? 2 :
            (InOwner == FCk_Handle(ExperienceAttribute) ? 4 : 0));
        if (Bit == 0 || (_ExpectedOverrideMask & Bit) == 0 || (_CompletedOverrideMask & Bit) != 0)
        { _AuthorityFailed = true; return; }
        if (InResult != ECk_Request_OperationResult::Succeeded) { _AuthorityFailed = true; return; }
        _CompletedOverrideMask |= Bit;
        _SucceededRequestCompletions++;
        if (_SucceededRequestCompletions > _ExpectedRequestCompletions) { _AuthorityFailed = true; }
    }
    // World-owned control admission; the local authored path uses the same gate.
    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (_AuthorityFailed || !CkAttributeInteger::IsLiveEntity(Self)) { return false; }
        auto Display = FCk_Handle(DisplayTimer);
        auto Auto = FCk_Handle(AutoTimer);
        auto Health = FCk_Handle(HealthAttribute);
        auto Armor = FCk_Handle(ArmorAttribute);
        auto Experience = FCk_Handle(ExperienceAttribute);
        if (!CkAttributeInteger::IsLiveEntity(Display) ||
            !CkAttributeInteger::IsLiveEntity(Auto) ||
            !CkAttributeInteger::IsLiveEntity(Health) ||
            !CkAttributeInteger::IsLiveEntity(Armor) ||
            !CkAttributeInteger::IsLiveEntity(Experience)) { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Health) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Armor) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Experience) != Self ||
            !utils_timer::DoCast(Display).IsSet() ||
            !utils_timer::DoCast(Auto).IsSet() ||
            !utils_integer_attribute::DoCast(Health).IsSet() ||
            !utils_integer_attribute::DoCast(Armor).IsSet() ||
            !utils_integer_attribute::DoCast(Experience).IsSet()) { return false; }
        return utils_integer_attribute::Has_Component(HealthAttribute, ECk_MinMaxCurrent::Min) &&
            utils_integer_attribute::Has_Component(HealthAttribute, ECk_MinMaxCurrent::Max) &&
            utils_integer_attribute::Has_Component(HealthAttribute, ECk_MinMaxCurrent::Current) &&
            utils_integer_attribute::Has_Component(ArmorAttribute, ECk_MinMaxCurrent::Min) &&
            utils_integer_attribute::Has_Component(ArmorAttribute, ECk_MinMaxCurrent::Max) &&
            utils_integer_attribute::Has_Component(ArmorAttribute, ECk_MinMaxCurrent::Current) &&
            utils_integer_attribute::Has_Component(ExperienceAttribute, ECk_MinMaxCurrent::Min) &&
            utils_integer_attribute::Has_Component(ExperienceAttribute, ECk_MinMaxCurrent::Current) &&
            !utils_integer_attribute::Has_Component(ExperienceAttribute, ECk_MinMaxCurrent::Max) &&
            HealthAttribute == utils_integer_attribute::TryGet(Self, utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Health")) &&
            ArmorAttribute == utils_integer_attribute::TryGet(Self, utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Armor")) &&
            ExperienceAttribute == utils_integer_attribute::TryGet(Self, utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Experience"));
    }

    bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
    bool Get_HasSettledAuthorityState()
    { return Get_HasValidAuthorityComposition() && !_PendingMutation && !_FinishPending; }
    bool Request_StartAuthorityDemo()
    {
        if (!WaitForWorldReady || _AuthorityStarted || !Get_HasValidAuthorityComposition() ||
            AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }
        _AuthorityStarted = true;
        _StartRequestSucceeded = false;
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
    private bool Get_HasExpectedAuthorityValues()
    {
        if (!Get_HasValidAuthorityComposition()) { return false; }
        return utils_integer_attribute::Get_BaseValue(HealthAttribute) == Math::Clamp(_ExpectedHealth, 0, 100) &&
            utils_integer_attribute::Get_PreClampFinalValue(HealthAttribute) == _ExpectedHealthPre &&
            utils_integer_attribute::Get_FinalValue(HealthAttribute) == Math::Clamp(_ExpectedHealth, 0, 100) &&
            utils_integer_attribute::Get_BaseValue(ArmorAttribute) == Math::Clamp(_ExpectedArmor, 0, 50) &&
            utils_integer_attribute::Get_PreClampFinalValue(ArmorAttribute) == _ExpectedArmorPre &&
            utils_integer_attribute::Get_FinalValue(ArmorAttribute) == Math::Clamp(_ExpectedArmor, 0, 50) &&
            utils_integer_attribute::Get_BaseValue(ExperienceAttribute) == Math::Max(_ExpectedExperience, 0) &&
            utils_integer_attribute::Get_PreClampFinalValue(ExperienceAttribute) == _ExpectedExperiencePre &&
            utils_integer_attribute::Get_FinalValue(ExperienceAttribute) == Math::Max(_ExpectedExperience, 0);
    }
    bool Request_TryPrepareAuthorityTransaction()
    {
        if (!Get_HasValidAuthorityComposition() || (WaitForWorldReady && !_AuthorityStarted) ||
            _ControlPending || _ResetPending || _FinishPending) { return false; }
        _FinishCompleted = false;
        _MutationGateClosed = true;
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
        _ExpectedHealth = utils_integer_attribute::Get_BaseValue(HealthAttribute);
        _ExpectedArmor = utils_integer_attribute::Get_BaseValue(ArmorAttribute);
        _ExpectedExperience = utils_integer_attribute::Get_BaseValue(ExperienceAttribute);
        _ExpectedHealthPre = utils_integer_attribute::Get_PreClampFinalValue(HealthAttribute);
        _ExpectedArmorPre = utils_integer_attribute::Get_PreClampFinalValue(ArmorAttribute);
        _ExpectedExperiencePre = utils_integer_attribute::Get_PreClampFinalValue(ExperienceAttribute);
        FCk_Handle_IntegerAttribute Target;
        if (InAction == 8) { Target = HealthAttribute; _ExpectedHealth = InValue;
            _ExpectedHealthPre = InValue; _ExpectedOverrideMask = 1; }
        else if (InAction == 9) { Target = ArmorAttribute; _ExpectedArmor = InValue;
            _ExpectedArmorPre = InValue; _ExpectedOverrideMask = 2; }
        else { Target = ExperienceAttribute; _ExpectedExperience = InValue;
            _ExpectedExperiencePre = InValue; _ExpectedOverrideMask = 4; }
        AutoRunning = false; _ControlPending = true; _PendingMutation = true;
        _ExpectedRequestCompletions = 1; _SucceededRequestCompletions = 0; _CompletedOverrideMask = 0;
        auto Requested = utils_integer_attribute::Request_Override(Target, InValue, ECk_MinMaxCurrent::Current,
            FCk_Delegate_Request_OnCompleted(this, n"OnBasicOverrideCompleted"));
        if (ck::Is_NOT_Valid(Requested)) { _AuthorityFailed = true; return false; }
        return true;
    }
    private bool Get_IsValidAuthorityControl(int32 InAction, int32 InValue)
    {
        if (InAction == 8) { return InValue == 100 || InValue == 50 || InValue == 10 || InValue == -10 || InValue == 150; }
        if (InAction == 9) { return InValue == 50 || InValue == 25 || InValue == 5 || InValue == -5 || InValue == 80; }
        if (InAction == 10) { return InValue == 0 || InValue == 500 || InValue == 2000 || InValue == -100; }
        return false;
    }
    bool Get_HasSettledAuthorityControl()
    { return _ControlPending && Get_IsAuthorityTransactionPrepared() && Get_HasExpectedAuthorityValues(); }
    bool Request_BeginAuthorityReset()
    {
        if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
        AutoStep = 0; HealthChangeCount = 0; ArmorChangeCount = 0; ExperienceChangeCount = 0;
        AutoRunning = true; _ResetPending = true;
        Request_ResetToDefaults();
        return !_AuthorityFailed;
    }
    bool Get_HasSettledAuthorityReset()
    { return _ResetPending && Get_IsAuthorityTransactionPrepared() && Get_HasExpectedAuthorityValues(); }
    bool Request_BeginFinishAuthorityTransaction(bool InResumeAuto)
    {
        if (_FinishPending || !Get_HasValidAuthorityComposition()) { return false; }
        if (_ControlPending && !Get_HasSettledAuthorityControl()) { return false; }
        if (_ResetPending && (!Get_HasSettledAuthorityReset() || !InResumeAuto)) { return false; }
        if (!_ControlPending && !_ResetPending && !Get_IsAuthorityTransactionPrepared()) { return false; }
        _FinishPending = true; _FinishCompleted = false; _FinishReleased = false;
        _FinishResume = InResumeAuto;
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
