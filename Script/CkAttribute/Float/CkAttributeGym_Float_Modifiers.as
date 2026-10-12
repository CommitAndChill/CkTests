//============================================================================
// FLOAT MODIFIERS ENTITY SCRIPT
//============================================================================

// Shared by the eight Float station scripts. Keep this helper pure: it must
// reject BeginDestroy before any feature, record or component getter.
namespace CkAttributeFloat
{
    bool IsLiveEntity(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }
}

class UCk_EntityScript_AttributeGym_FloatModifiers : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;
    UPROPERTY(ExposeOnSpawn) bool WaitForWorldReady = false;
    FCk_Handle_Timer DisplayTimer;
    private bool _AuthorityStarted = false, _AuthorityFailed = false;
    private bool _MutationGateClosed = false, _StartSucceeded = false, _StartReleased = false;
    private bool _PrepareRequested = false, _PrepareSucceeded = false, _Prepared = false;
    private bool _ControlPending = false, _ResetPending = false;
    private bool _FinishPending = false, _FinishCompleted = false, _FinishReleased = false;
    private bool _FinishResume = false, _ReleaseResumeRequested = false;
    private bool _ReleaseResumeCompleted = false;
    private bool _PendingMutation = false;
    private int32 _PendingPasses = 2;
    private float32 _ExpectedDamageBase = 50.0f, _ExpectedDefenseBase = 30.0f;
    private float32 _ExpectedDamageBonus = 0.0f, _ExpectedDefenseBonus = 0.0f;
    private TArray<float32> _ExpectedDeltas;
    private TArray<FGameplayTag> _ExpectedLabels;
    private TArray<FCk_Handle_FloatAttributeModifier> _RetiringModifiers;
    private FCk_Handle_FloatAttributeModifier _PermanentDamage;
    private FCk_Handle_FloatAttributeModifier _ResetDamagePermanent, _ResetDefensePermanent;
    private bool _PermanentDamageAdded = false;
    private bool _ResetDamagePermanentAdded = false, _ResetDefensePermanentAdded = false;
    private TArray<int32> _LegacyActions;
    private bool _LegacyStarted = false, _LegacyResumeAuto = false;

    FCk_Handle_FloatAttribute DamageAttribute;
    FCk_Handle_FloatAttribute DefenseAttribute;
    TArray<FCk_Handle_FloatAttributeModifier> ActiveModifiers;

    FCk_Handle_Timer AutoTimer;
    bool AutoRunning = true;
    int32 AutoStep = 0;
    FCkGym_AutoConfig AutoConfig;
    int32 ValueChangeCount = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
        utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_FloatModifiers");

        auto DisplayTimerParams = FCk_Timer_Spec(FCk_Time(0.0f));
        DisplayTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        DisplayTimer = utils_timer::Add(InHandle, DisplayTimerParams);
        DisplayTimer.BindTo_OnUpdate(FCk_Delegate_Timer(this, n"DisplayTick"));

        if (WaitForWorldReady)
        {
            auto Params = FCk_Timer_Spec(FCk_Time(2.5f));
            Params.Set_StartingState(ECk_Timer_State::Paused).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
            AutoTimer = utils_timer::Add(InHandle, Params);
            AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick"));
            _MutationGateClosed = true;
        }
        else { AutoTimer = gym_auto::Setup(InHandle, this, FCk_Time(2.5f)); }

        AutoConfig.TotalSteps = 8;
        AutoConfig.Description = "Tests modifier system with add/multiply operations.\nShows Base + Bonus = Final with stacking modifiers.";
        AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
        AutoConfig.PerStationAutoCommand = "panel [3] Modifiers station auto";
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add weapon modifier (+25.5)", 0, 0));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add armor modifier (+15.75)", 1, 1));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add buff modifier (+20.25)", 2, 2));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add shield + enchant modifiers", 3, 3));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Remove weapon modifier", 4, 4));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Modify existing buff (20.25 -> 35.75)", 5, 5));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Clear all modifiers", 6, 6));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add non-revocable modifier (+12.5)", 7, 7));
        AutoConfig.ManualCommands.Add("panel [K] Modifiers ring - add weapon / revoke weapon / clear all");

        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        Request_SetupAttributes(InHandle);
        if (ck::Is_NOT_Valid(DamageAttribute) || ck::Is_NOT_Valid(DefenseAttribute))
        { _AuthorityFailed = true; return; }
        Request_BindSignals(InHandle);

        utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_AttributeGym_ResetAttributes,
            FCk_Delegate_Messaging_OnBroadcast(this, n"OnResetAttributes"));
        utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_AddModifier,
            FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualAddWeapon"));
        utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_RevokeAll,
            FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualRemoveWeapon"));
        utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_FloatGym_ClearModifiers,
            FCk_Delegate_Messaging_OnBroadcast(this, n"OnManualClearMods"));
    }

    void Request_SetupAttributes(FCk_Handle InHandle)
    {
        auto DamageParams = FCk_FloatAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Damage"), 50.0f);
        DamageParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(10.0f).Set_MaxValue(200.0f);
        DamageAttribute = utils_float_attribute::Add(InHandle, DamageParams);

        auto DefenseParams = FCk_FloatAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Defense"), 30.0f);
        DefenseParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0.0f).Set_MaxValue(100.0f);
        DefenseAttribute = utils_float_attribute::Add(InHandle, DefenseParams);
    }

    void Request_BindSignals(FCk_Handle InHandle)
    {
        if (ck::IsValid(DamageAttribute))
        {
            utils_float_attribute::BindTo_OnValueChanged(DamageAttribute, ECk_MinMaxCurrent::Current,
                FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnDamageValueChanged"));
        }
        if (ck::IsValid(DefenseAttribute))
        {
            utils_float_attribute::BindTo_OnValueChanged(DefenseAttribute, ECk_MinMaxCurrent::Current,
                FCk_Delegate_FloatAttribute_OnValueChanged(this, n"OnDefenseValueChanged"));
        }
    }

    UFUNCTION() private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (InHandle != DisplayTimer || !CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
        if (_PendingMutation)
        {
            _PendingPasses++;
            if (Get_HasSettledAuthorityProjection()) { _PendingMutation = false; }
        }
        Request_TickFinishAuthorityTransaction();
        Request_TickLegacyCommands();
        if (!WaitForWorldReady && Get_HasValidAuthorityComposition() &&
            Get_HasValidModifierRecords()) { Request_UpdateDisplay(); }
    }
    UFUNCTION() private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (InHandle != AutoTimer || _MutationGateClosed || _PendingMutation ||
            !Get_HasValidAuthorityComposition() || !Get_HasValidModifierRecords())
        { if (WaitForWorldReady) { _AuthorityFailed = true; } return; }
        auto Step = AutoStep % AutoConfig.TotalSteps;
        bool Good = false;
        if (Step == 0) { Good = Request_AddModifier(DamageAttribute, n"Modifier.Weapon", 25.5f); }
        else if (Step == 1) { Good = Request_AddModifier(DefenseAttribute, n"Modifier.Armor", 15.75f); }
        else if (Step == 2) { Good = Request_AddModifier(DamageAttribute, n"Modifier.Buff", 20.25f); }
        else if (Step == 3) { Good = Request_AddMultipleModifiers(); }
        else if (Step == 4) { Good = Request_RemoveWeaponModifier(); }
        else if (Step == 5) { Good = Request_ModifyExistingModifier(); }
        else if (Step == 6) { Good = Request_ClearAllModifiers(); }
        else if (Step == 7) { Good = Request_TestNotRevocableModifiers(); }
        if (!Good) { _AuthorityFailed = true; _MutationGateClosed = true; return; }
        AutoStep++; _PendingPasses = 0; _PendingMutation = true;
    }
    UFUNCTION() private void OnAutoSet(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
    {
        if (WaitForWorldReady) { return; }
        auto Typed = InPayload.Get(FCk_Message_Gym_AutoSet);
        Request_QueueLegacyCommand(Typed.Enabled ? 4 : 3);
    }
    private bool Request_AddModifier(FCk_Handle_FloatAttribute InAttribute, FName InLabel, float32 InDelta)
    {
        if (!Get_HasValidModifierRecords() || !Get_IsFinite(InDelta)) { return false; }
        auto Tag = utils_gameplay_tag::ResolveGameplayTag(InLabel);
        auto Params = FCk_FloatAttributeModifier_Spec(); Params.Set_ModifierDelta(InDelta);
        auto Added = utils_float_attribute_modifier::Add_Revocable(InAttribute, Tag,
            ECk_AttributeModifier_Operation::Add, Params);
        auto Handle = FCk_Handle(Added);
        if (!CkAttributeFloat::IsLiveEntity(Handle) ||
            utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(InAttribute) ||
            !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
            !utils_gameplay_label::Has(Handle) ||
            utils_gameplay_label::Get_Label(Handle) != Tag) { return false; }
        ActiveModifiers.Add(Added); _ExpectedDeltas.Add(InDelta); _ExpectedLabels.Add(Tag);
        if (InAttribute == DamageAttribute) { _ExpectedDamageBonus += InDelta; }
        else { _ExpectedDefenseBonus += InDelta; }
        return true;
    }
    private bool Request_AddMultipleModifiers()
    { return Request_AddModifier(DefenseAttribute, n"Modifier.Shield", 10.5f) &&
             Request_AddModifier(DefenseAttribute, n"Modifier.Enchantment", 8.3f); }
    private int32 FindActiveModifier(FCk_Handle_FloatAttributeModifier InModifier)
    {
        for (int32 i = 0; i < ActiveModifiers.Num(); i++)
        { if (ActiveModifiers[i] == InModifier) { return i; } }
        return -1;
    }
    private bool Request_RemoveWeaponModifier()
    {
        if (!Get_HasValidModifierRecords()) { return false; }
        auto Weapon = utils_float_attribute_modifier::TryGet(DamageAttribute,
            utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
        if (ck::Is_NOT_Valid(Weapon)) { return true; }
        auto Index = FindActiveModifier(Weapon);
        if (Index < 0 || _ExpectedLabels[Index] != utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"))
        { return false; }
        _RetiringModifiers.Add(Weapon); utils_float_attribute_modifier::Remove(Weapon);
        _ExpectedDamageBonus -= _ExpectedDeltas[Index];
        ActiveModifiers.RemoveAt(Index); _ExpectedDeltas.RemoveAt(Index); _ExpectedLabels.RemoveAt(Index);
        return true;
    }
    private bool Request_ModifyExistingModifier()
    {
        if (!Get_HasValidModifierRecords()) { return false; }
        auto Buff = utils_float_attribute_modifier::TryGet(DamageAttribute,
            utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
        if (ck::Is_NOT_Valid(Buff)) { return true; }
        auto Index = FindActiveModifier(Buff);
        if (Index < 0 || _ExpectedLabels[Index] != utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"))
        { return false; }
        auto Result = utils_float_attribute_modifier::Override(Buff, 35.75f);
        if (ck::Is_NOT_Valid(Result) || Result != Buff) { return false; }
        _ExpectedDamageBonus += 35.75f - _ExpectedDeltas[Index];
        _ExpectedDeltas[Index] = 35.75f; return true;
    }
    private bool Request_ClearAllModifiers()
    {
        if (!Get_HasValidModifierRecords()) { return false; }
        for (auto Mod : ActiveModifiers) { _RetiringModifiers.Add(Mod); }
        utils_float_attribute_modifier::Request_ClearAllModifiers(DamageAttribute, ECk_MinMaxCurrent::Current);
        utils_float_attribute_modifier::Request_ClearAllModifiers(DefenseAttribute, ECk_MinMaxCurrent::Current);
        ActiveModifiers.Empty(); _ExpectedDeltas.Empty(); _ExpectedLabels.Empty();
        _ExpectedDamageBonus = 0.0f; _ExpectedDefenseBonus = 0.0f;
        return true;
    }
    private bool Request_TestNotRevocableModifiers()
    {
        if (!Get_HasValidModifierRecords()) { return false; }
        auto Before = utils_float_attribute_modifier::ForEach(DamageAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        auto Params = FCk_FloatAttributeModifier_Spec(); Params.Set_ModifierDelta(12.5f);
        utils_float_attribute_modifier::Add_NotRevocable(DamageAttribute,
            ECk_AttributeModifier_Operation::Add, Params);
        auto After = utils_float_attribute_modifier::ForEach(DamageAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        for (auto PriorMember : Before)
        { if (!After.Contains(PriorMember)) { return false; } }
        if (_PermanentDamageAdded)
        {
            if (After.Num() != Before.Num() || !After.Contains(_PermanentDamage)) { return false; }
        }
        else
        {
            if (After.Num() != Before.Num() + 1) { return false; }
            for (auto Mod : After)
            {
                if (Before.Contains(Mod)) { continue; }
                auto Handle = FCk_Handle(Mod);
                if (!CkAttributeFloat::IsLiveEntity(Handle) ||
                    utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(DamageAttribute) ||
                    !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current))
                { return false; }
                _PermanentDamage = Mod; _PermanentDamageAdded = true; break;
            }
            if (ck::Is_NOT_Valid(_PermanentDamage)) { return false; }
        }
        _ExpectedDamageBase = Math::Clamp(_ExpectedDamageBase + 12.5f, 10.0f, 200.0f);
        return true;
    }

    bool Get_HasSettledAuthoritySample()
    { return !_PendingMutation && Get_HasSettledAuthorityProjection(); }
    bool Request_TryBuildPresentation(FString& OutTitle, FString& OutBody)
    {
        OutTitle = ""; OutBody = "";
        if (!Get_HasSettledAuthoritySample()) { return false; }
        auto SelfEntity = ck::ToEntity(this);
        auto TitleText = "FLOAT MODIFIERS (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";
        auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

        DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n\n";

        if (ck::IsValid(DamageAttribute))
        {
            auto BaseValue = utils_float_attribute::Get_BaseValue(DamageAttribute);
            auto BonusValue = utils_float_attribute::Get_BonusValue(DamageAttribute);
            auto FinalValue = utils_float_attribute::Get_FinalValue(DamageAttribute);
            auto DamageBar = CkGym_Attribute::Create_ProgressBar(FinalValue, 200.0f, 20);
            DisplayText = f"{DisplayText}Damage: {BaseValue} + {BonusValue} = {FinalValue}/200\n";
            DisplayText = f"{DisplayText}[{DamageBar}]\n\n";
        }

        if (ck::IsValid(DefenseAttribute))
        {
            auto BaseValue = utils_float_attribute::Get_BaseValue(DefenseAttribute);
            auto BonusValue = utils_float_attribute::Get_BonusValue(DefenseAttribute);
            auto FinalValue = utils_float_attribute::Get_FinalValue(DefenseAttribute);
            auto DefenseBar = CkGym_Attribute::Create_ProgressBar(FinalValue, 100.0f, 20, ECk_ASCII_ProgressBar_Style::HashTag_Symbol);
            DisplayText = f"{DisplayText}Defense: {BaseValue} + {BonusValue} = {FinalValue}/100\n";
            DisplayText = f"{DisplayText}[{DefenseBar}]\n\n";
        }

        DisplayText = f"{DisplayText}Active Modifiers: {ActiveModifiers.Num()}\n";
        auto ModifierCount = 0;
        for (auto Modifier : ActiveModifiers)
        {
            if (ck::IsValid(Modifier))
            {
                auto Delta = utils_float_attribute_modifier::Get_Delta(Modifier);
                ModifierCount++;
                DisplayText = f"{DisplayText}  Mod {ModifierCount}: +{Delta}\n";
            }
        }

        DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);
        OutTitle = TitleText; OutBody = DisplayText; return true;
    }
    void Request_UpdateDisplay()
    {
        FString Title, Body;
        if (!Request_TryBuildPresentation(Title, Body)) { return; }
        if (!WaitForWorldReady)
        { CkGym_Common::Update_StationDisplay(ck::ToEntity(this), Title, Body, ""); }
    }

    UFUNCTION() void OnDamageValueChanged(FCk_Handle InOwner, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
    { if (InOwner == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }
    UFUNCTION() void OnDefenseValueChanged(FCk_Handle InOwner, FCk_Payload_FloatAttribute_OnValueChanged InPayload)
    { if (InOwner == ck::ToEntity(this) && Get_HasValidAuthorityComposition()) { ValueChangeCount++; } }
    UFUNCTION() private void OnManualAddWeapon(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
    { if (!WaitForWorldReady) { Request_QueueLegacyCommand(0); } }
    UFUNCTION() private void OnManualRemoveWeapon(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
    { if (!WaitForWorldReady) { Request_QueueLegacyCommand(1); } }
    UFUNCTION() private void OnManualClearMods(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
    { if (!WaitForWorldReady) { Request_QueueLegacyCommand(2); } }
    UFUNCTION() private void OnResetAttributes(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
    { if (!WaitForWorldReady) { Request_QueueLegacyCommand(5); } }

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
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_AddModifier, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_RevokeAll, Out)) { return false; }
		if (!Request_AppendExactMessenger(Self, FCk_Message_FloatGym_ClearModifiers, Out)) { return false; }
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
        auto Display = FCk_Handle(DisplayTimer), Auto = FCk_Handle(AutoTimer);
        auto Damage = FCk_Handle(DamageAttribute), Defense = FCk_Handle(DefenseAttribute);
        if (_AuthorityFailed || !CkAttributeFloat::IsLiveEntity(Self) ||
            !CkAttributeFloat::IsLiveEntity(Display) || !CkAttributeFloat::IsLiveEntity(Auto) ||
            !CkAttributeFloat::IsLiveEntity(Damage) || !CkAttributeFloat::IsLiveEntity(Defense))
        { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(Display) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Auto) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Damage) != Self ||
            utils_entity_lifetime::Get_LifetimeOwner(Defense) != Self ||
            !utils_timer::DoCast(Display).IsSet() || !utils_timer::DoCast(Auto).IsSet() ||
            !utils_float_attribute::DoCast(Damage).IsSet() ||
            !utils_float_attribute::DoCast(Defense).IsSet() ||
            utils_float_attribute::TryGet(Self,
                utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Damage")) != DamageAttribute ||
            utils_float_attribute::TryGet(Self,
                utils_gameplay_tag::ResolveGameplayTag(n"FloatAttribute.Defense")) != DefenseAttribute)
        { return false; }
        if (!utils_float_attribute::Has_Component(DamageAttribute, ECk_MinMaxCurrent::Min) ||
            !utils_float_attribute::Has_Component(DamageAttribute, ECk_MinMaxCurrent::Max) ||
            !utils_float_attribute::Has_Component(DamageAttribute, ECk_MinMaxCurrent::Current) ||
            !utils_float_attribute::Has_Component(DefenseAttribute, ECk_MinMaxCurrent::Min) ||
            !utils_float_attribute::Has_Component(DefenseAttribute, ECk_MinMaxCurrent::Max) ||
            !utils_float_attribute::Has_Component(DefenseAttribute, ECk_MinMaxCurrent::Current))
        { return false; }
        if (Math::Abs(utils_float_attribute::Get_FinalValue(DamageAttribute, ECk_MinMaxCurrent::Min) - 10.0f) > 0.001f ||
            Math::Abs(utils_float_attribute::Get_FinalValue(DamageAttribute, ECk_MinMaxCurrent::Max) - 200.0f) > 0.001f ||
            Math::Abs(utils_float_attribute::Get_FinalValue(DefenseAttribute, ECk_MinMaxCurrent::Min)) > 0.001f ||
            Math::Abs(utils_float_attribute::Get_FinalValue(DefenseAttribute, ECk_MinMaxCurrent::Max) - 100.0f) > 0.001f)
        { return false; }
        TArray<FCk_Handle> Messengers;
        if (!Get_HasExactMessengers(Messengers)) { return false; }
        auto Expected = TArray<FCk_Handle>();
        Expected.Add(Display); Expected.Add(Auto); Expected.Add(Damage); Expected.Add(Defense);
        for (auto Messenger : Messengers) { Expected.Add(Messenger); }
        int32 LiveCount = 0;
        for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Self))
        { if (!CkAttributeFloat::IsLiveEntity(Child)) { continue; }
          if (!Expected.Contains(Child)) { return false; } LiveCount++; }
        return LiveCount == Expected.Num() && AutoConfig.TotalSteps == 8 && AutoStep >= 0;
    }
	bool Get_HasAuthorityFailure() const { return _AuthorityFailed; }
	private bool Get_IsFinite(float32 InValue)
	{ return InValue == InValue && Math::Abs(InValue) < 1.0e20f; }
    bool Get_HasValidModifierRecords()
    {
        if (!Get_HasValidAuthorityComposition() ||
            ActiveModifiers.Num() != _ExpectedDeltas.Num() ||
            ActiveModifiers.Num() != _ExpectedLabels.Num()) { return false; }
        auto DamageAll = utils_float_attribute_modifier::ForEach(DamageAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        auto DefenseAll = utils_float_attribute_modifier::ForEach(DefenseAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        int32 DamageCount = (_PermanentDamageAdded ? 1 : 0) +
            (_ResetDamagePermanentAdded ? 1 : 0);
        int32 DefenseCount = _ResetDefensePermanentAdded ? 1 : 0;
        auto Seen = TArray<FCk_Handle>();
        float32 DamageBonus = 0.0f, DefenseBonus = 0.0f;
        for (int32 i = 0; i < ActiveModifiers.Num(); i++)
        {
            auto Mod = ActiveModifiers[i]; auto Handle = FCk_Handle(Mod);
            if (!CkAttributeFloat::IsLiveEntity(Handle) || Seen.Contains(Handle) ||
                !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
                !utils_gameplay_label::Has(Handle) ||
                utils_gameplay_label::Get_Label(Handle) != _ExpectedLabels[i] ||
                !Get_IsFinite(_ExpectedDeltas[i])) { return false; }
            auto Owner = utils_entity_lifetime::Get_LifetimeOwner(Handle);
            if (Owner == FCk_Handle(DamageAttribute) && DamageAll.Contains(Mod))
            { DamageCount++; DamageBonus += _ExpectedDeltas[i]; }
            else if (Owner == FCk_Handle(DefenseAttribute) && DefenseAll.Contains(Mod))
            { DefenseCount++; DefenseBonus += _ExpectedDeltas[i]; }
            else { return false; }
            auto Delta = utils_float_attribute_modifier::Get_Delta(Mod);
            if (!Get_IsFinite(Delta) || Math::Abs(Delta - _ExpectedDeltas[i]) > 0.001f)
            { return false; }
            Seen.Add(Handle);
        }
        if (_PermanentDamageAdded)
        {
            auto Handle = FCk_Handle(_PermanentDamage);
            if (!CkAttributeFloat::IsLiveEntity(Handle) || Seen.Contains(Handle) ||
                utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(DamageAttribute) ||
                !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
                !DamageAll.Contains(_PermanentDamage)) { return false; }
            Seen.Add(Handle);
        }
        if (_ResetDamagePermanentAdded)
        {
            auto Handle = FCk_Handle(_ResetDamagePermanent);
            if (!CkAttributeFloat::IsLiveEntity(Handle) || Seen.Contains(Handle) ||
                utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(DamageAttribute) ||
                !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
                !DamageAll.Contains(_ResetDamagePermanent)) { return false; }
            Seen.Add(Handle);
        }
        if (_ResetDefensePermanentAdded)
        {
            auto Handle = FCk_Handle(_ResetDefensePermanent);
            if (!CkAttributeFloat::IsLiveEntity(Handle) || Seen.Contains(Handle) ||
                utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(DefenseAttribute) ||
                !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
                !DefenseAll.Contains(_ResetDefensePermanent)) { return false; }
            Seen.Add(Handle);
        }
        return DamageAll.Num() == DamageCount && DefenseAll.Num() == DefenseCount &&
            Get_IsFinite(DamageBonus) && Get_IsFinite(DefenseBonus) &&
            Math::Abs(DamageBonus - _ExpectedDamageBonus) <= 0.001f &&
            Math::Abs(DefenseBonus - _ExpectedDefenseBonus) <= 0.001f;
    }
    bool Get_HasSettledAuthorityProjection()
    {
        if (!Get_HasValidModifierRecords() || _PendingPasses < 2) { return false; }
        for (auto Old : _RetiringModifiers) { if (ck::IsValid(Old)) { return false; } }
        auto DamageBase = utils_float_attribute::Get_BaseValue(DamageAttribute);
        auto DamageBonus = utils_float_attribute::Get_BonusValue(DamageAttribute);
        auto DamagePre = utils_float_attribute::Get_PreClampFinalValue(DamageAttribute);
        auto DamageFinal = utils_float_attribute::Get_FinalValue(DamageAttribute);
        auto DamageOverflow = utils_float_attribute::Get_ClampOverflow(DamageAttribute);
        auto DefenseBase = utils_float_attribute::Get_BaseValue(DefenseAttribute);
        auto DefenseBonus = utils_float_attribute::Get_BonusValue(DefenseAttribute);
        auto DefensePre = utils_float_attribute::Get_PreClampFinalValue(DefenseAttribute);
        auto DefenseFinal = utils_float_attribute::Get_FinalValue(DefenseAttribute);
        auto DefenseOverflow = utils_float_attribute::Get_ClampOverflow(DefenseAttribute);
        return Get_IsFinite(DamageBase) && Get_IsFinite(DamageBonus) &&
            Get_IsFinite(DamagePre) && Get_IsFinite(DamageFinal) && Get_IsFinite(DamageOverflow) &&
            Get_IsFinite(DefenseBase) && Get_IsFinite(DefenseBonus) &&
            Get_IsFinite(DefensePre) && Get_IsFinite(DefenseFinal) && Get_IsFinite(DefenseOverflow) &&
            Math::Abs(DamageBase - _ExpectedDamageBase) <= 0.001f &&
            Math::Abs(DamageBonus - _ExpectedDamageBonus) <= 0.001f &&
            Math::Abs(DamagePre - (DamageBase + DamageBonus)) <= 0.001f &&
            Math::Abs(DamageFinal - Math::Clamp(DamagePre, 10.0f, 200.0f)) <= 0.001f &&
            Math::Abs(DamageOverflow - (DamagePre - DamageFinal)) <= 0.001f &&
            Math::Abs(DefenseBase - _ExpectedDefenseBase) <= 0.001f &&
            Math::Abs(DefenseBonus - _ExpectedDefenseBonus) <= 0.001f &&
            Math::Abs(DefensePre - (DefenseBase + DefenseBonus)) <= 0.001f &&
            Math::Abs(DefenseFinal - Math::Clamp(DefensePre, 0.0f, 100.0f)) <= 0.001f &&
            Math::Abs(DefenseOverflow - (DefensePre - DefenseFinal)) <= 0.001f;
    }
    private bool Request_ResetToDefault()
    {
        if (!Get_HasValidModifierRecords()) { return false; }
        if (!Request_ClearAllModifiers()) { return false; }
        auto DamageBefore = utils_float_attribute_modifier::ForEach(DamageAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        auto DefenseBefore = utils_float_attribute_modifier::ForEach(DefenseAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        auto Damage = utils_float_attribute::Request_Override(DamageAttribute, 50.0f, ECk_MinMaxCurrent::Current);
        auto Defense = utils_float_attribute::Request_Override(DefenseAttribute, 30.0f, ECk_MinMaxCurrent::Current);
        if (ck::Is_NOT_Valid(Damage) || ck::Is_NOT_Valid(Defense)) { return false; }
        auto DamageAfter = utils_float_attribute_modifier::ForEach(DamageAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        auto DefenseAfter = utils_float_attribute_modifier::ForEach(DefenseAttribute,
            FInstancedStruct(), FCk_Lambda_InHandle());
        for (auto Prior : DamageBefore) { if (!DamageAfter.Contains(Prior)) { return false; } }
        for (auto Prior : DefenseBefore) { if (!DefenseAfter.Contains(Prior)) { return false; } }
        if (_ResetDamagePermanentAdded)
        {
            if (DamageAfter.Num() != DamageBefore.Num() ||
                !DamageAfter.Contains(_ResetDamagePermanent) ||
                !CkAttributeFloat::IsLiveEntity(FCk_Handle(_ResetDamagePermanent)))
            { return false; }
        }
        else
        {
            if (DamageAfter.Num() != DamageBefore.Num() + 1) { return false; }
            bool Recorded = false;
            for (auto Candidate : DamageAfter)
            {
                if (DamageBefore.Contains(Candidate)) { continue; }
                auto Handle = FCk_Handle(Candidate);
                if (Recorded || !CkAttributeFloat::IsLiveEntity(Handle) ||
                    utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(DamageAttribute) ||
                    !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current))
                { return false; }
                _ResetDamagePermanent = Candidate; Recorded = true;
            }
            if (!Recorded) { return false; }
            _ResetDamagePermanentAdded = true;
        }
        if (_ResetDefensePermanentAdded)
        {
            if (DefenseAfter.Num() != DefenseBefore.Num() ||
                !DefenseAfter.Contains(_ResetDefensePermanent) ||
                !CkAttributeFloat::IsLiveEntity(FCk_Handle(_ResetDefensePermanent)))
            { return false; }
        }
        else
        {
            if (DefenseAfter.Num() != DefenseBefore.Num() + 1) { return false; }
            bool Recorded = false;
            for (auto Candidate : DefenseAfter)
            {
                if (DefenseBefore.Contains(Candidate)) { continue; }
                auto Handle = FCk_Handle(Candidate);
                if (Recorded || !CkAttributeFloat::IsLiveEntity(Handle) ||
                    utils_entity_lifetime::Get_LifetimeOwner(Handle) != FCk_Handle(DefenseAttribute) ||
                    !utils_float_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current))
                { return false; }
                _ResetDefensePermanent = Candidate; Recorded = true;
            }
            if (!Recorded) { return false; }
            _ResetDefensePermanentAdded = true;
        }
        _ExpectedDamageBase = 50.0f; _ExpectedDefenseBase = 30.0f;
        return true;
    }
	bool Request_StartAuthorityDemo()
	{
		if (!WaitForWorldReady || _AuthorityStarted || !Get_HasValidAuthorityComposition() ||
			!Get_HasValidModifierRecords() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }
		_AuthorityStarted = true; _StartReleased = false; _StartSucceeded = false;
		return true;
	}
	UFUNCTION() private void OnAuthorityStartCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_StartReleased || InOwner != FCk_Handle(AutoTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_StartSucceeded = InResult == ECk_Request_OperationResult::Succeeded &&
			Get_HasValidAuthorityComposition() &&
			AutoTimer.Get_CurrentState() == ECk_Timer_State::Running;
		if (!_StartSucceeded) { _AuthorityFailed = true; _MutationGateClosed = true; }
	}
	bool Get_HasSettledAuthorityStart()
	{ return _AuthorityStarted && !_StartReleased && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	bool Request_ReleaseAuthorityStart()
	{
		if (!Get_HasSettledAuthorityStart()) { return false; }
		auto Request = utils_timer::Request_Resume(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityStartCompleted"));
		if (ck::Is_NOT_Valid(Request)) { _AuthorityFailed = true; return false; }
		_StartReleased = true; _MutationGateClosed = false;
		return true;
	}
	bool Request_TryPrepareAuthorityTransaction()
	{
		if (!Get_HasValidAuthorityComposition() ||
			(WaitForWorldReady && (!_AuthorityStarted || !_StartReleased || !_StartSucceeded)) ||
			(_ReleaseResumeRequested && !_ReleaseResumeCompleted) ||
			_ControlPending || _ResetPending || _FinishPending) { return false; }
		_FinishCompleted = false; _MutationGateClosed = true;
		if (_PendingMutation || !Get_HasSettledAuthorityProjection()) { return false; }
		if (AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused)
		{ _Prepared = !_PrepareRequested || _PrepareSucceeded; return _Prepared; }
		if (_PrepareRequested || AutoTimer.Get_CurrentState() != ECk_Timer_State::Running)
		{ return false; }
		_PrepareRequested = true; _PrepareSucceeded = false;
		auto Request = utils_timer::Request_Pause(AutoTimer,
			FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityPrepareCompleted"));
		if (ck::Is_NOT_Valid(Request)) { _AuthorityFailed = true; }
		return false;
	}
	UFUNCTION() private void OnAuthorityPrepareCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{ if (!_PrepareRequested || InOwner != FCk_Handle(AutoTimer) ||
		!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
	  _PrepareSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
	  if (!_PrepareSucceeded) { _AuthorityFailed = true; } }
	bool Get_IsAuthorityTransactionPrepared()
	{ return _Prepared && !_PendingMutation && Get_HasValidAuthorityComposition() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
    bool Request_ApplyAuthorityControl(int32 InAction, float32 InValue)
    {
        if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending ||
            InAction < 0 || InAction > 2 || InValue != 0.0f) { return false; }
        AutoRunning = false; _ControlPending = true; _PendingPasses = 0;
        bool Good = InAction == 0 ? Request_AddModifier(DamageAttribute, n"Modifier.Weapon", 25.5f) :
            (InAction == 1 ? Request_RemoveWeaponModifier() : Request_ClearAllModifiers());
        if (!Good) { _AuthorityFailed = true; return false; }
        _PendingMutation = true; return true;
    }
	bool Get_HasSettledAuthorityControl()
	{ return _ControlPending && !_PendingMutation && Get_IsAuthorityTransactionPrepared() &&
		Get_HasSettledAuthorityProjection(); }
	bool Request_BeginAuthorityReset()
	{
		if (!Get_IsAuthorityTransactionPrepared() || _ControlPending || _ResetPending) { return false; }
		_ResetPending = true; _PendingPasses = 0;
		if (!Request_ResetToDefault()) { _AuthorityFailed = true; return false; }
		AutoStep = 0; ValueChangeCount = 0; _PendingMutation = true;
		return true;
	}
	bool Get_HasSettledAuthorityReset()
	{ return _ResetPending && !_PendingMutation && Get_IsAuthorityTransactionPrepared() &&
		Get_HasSettledAuthorityProjection(); }
	bool Request_BeginFinishAuthorityTransaction(bool InResumeAuto)
	{
		if (_FinishPending || !Get_HasValidAuthorityComposition() ||
			(_ControlPending && !Get_HasSettledAuthorityControl()) ||
			(_ResetPending && !Get_HasSettledAuthorityReset()) ||
			(!_ControlPending && !_ResetPending && !Get_IsAuthorityTransactionPrepared()))
		{ return false; }
		// Keep the physical timer paused until the whole cohort can release
		// synchronously. Running status here would permit a skipped gated tick.
		_FinishPending = true; _FinishCompleted = false; _FinishReleased = false;
		_FinishResume = InResumeAuto; _ReleaseResumeRequested = false;
		_ReleaseResumeCompleted = false;
		return true;
	}
	UFUNCTION() private void OnAuthorityReleaseCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
	{
		if (!_FinishReleased || !_ReleaseResumeRequested ||
			InOwner != FCk_Handle(AutoTimer) ||
			!CkAttributeFloat::IsLiveEntity(ck::ToEntity(this))) { return; }
		_ReleaseResumeCompleted = true;
		if (InResult != ECk_Request_OperationResult::Succeeded ||
			!Get_HasValidAuthorityComposition() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Running)
		{ _AuthorityFailed = true; _MutationGateClosed = true; }
	}
	private void Request_TickFinishAuthorityTransaction()
	{
		if (!_FinishPending || _PendingMutation ||
			!Get_HasSettledAuthorityProjection() ||
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
		!_AuthorityFailed && Get_HasSettledAuthorityProjection() &&
		AutoTimer.Get_CurrentState() == ECk_Timer_State::Paused; }
	bool Request_ReleaseAuthorityTransaction()
	{
		if (!Get_CanReleaseAuthorityTransaction() ||
			AutoTimer.Get_CurrentState() != ECk_Timer_State::Paused) { return false; }
		if (_FinishResume)
		{
			_ReleaseResumeRequested = true;
			auto Request = utils_timer::Request_Resume(AutoTimer,
				FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityReleaseCompleted"));
			if (ck::Is_NOT_Valid(Request))
			{ _AuthorityFailed = true; _ReleaseResumeRequested = false; return false; }
		}
		_FinishReleased = true; _MutationGateClosed = !_FinishResume;
		return true;
	}
	private bool Request_QueueLegacyCommand(int32 InAction)
	{ if (WaitForWorldReady || InAction < 0 || InAction > 5) { return false; }
	  _LegacyActions.Add(InAction); return true; }
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
			if (_ControlPending || _ResetPending || !_FinishCompleted) { return; }
			if (_FinishResume && !_ReleaseResumeRequested)
			{
				_ReleaseResumeRequested = true; _FinishReleased = true;
				auto Request = utils_timer::Request_Resume(AutoTimer,
					FCk_Delegate_Request_OnCompleted(this, n"OnAuthorityReleaseCompleted"));
				if (ck::Is_NOT_Valid(Request))
				{ _AuthorityFailed = true; _MutationGateClosed = true; return; }
				return;
			}
			if (_ReleaseResumeRequested &&
				(!_ReleaseResumeCompleted ||
				AutoTimer.Get_CurrentState() != ECk_Timer_State::Running)) { return; }
			_FinishCompleted = false; _MutationGateClosed = !AutoRunning;
			_LegacyActions.RemoveAt(0); _LegacyStarted = false; return;
		}
		if (!Request_TryPrepareAuthorityTransaction()) { return; }
		auto Action = _LegacyActions[0]; _LegacyResumeAuto = AutoRunning;
		if (Action <= 2)
		{ if (!Request_ApplyAuthorityControl(Action, 0.0f)) { _AuthorityFailed = true; return; } }
		else if (Action == 5)
		{ if (!Request_BeginAuthorityReset()) { _AuthorityFailed = true; return; } }
		else if (!Request_BeginFinishAuthorityTransaction(Action == 4))
		{ _AuthorityFailed = true; return; }
		_LegacyStarted = true;
	}

}
