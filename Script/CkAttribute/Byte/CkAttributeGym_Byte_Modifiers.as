//============================================================================
// BYTE MODIFIERS ENTITY SCRIPT
//============================================================================

class UCk_EntityScript_AttributeGym_ByteModifiers : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;

    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;
    UPROPERTY(ExposeOnSpawn) int32 StationIndex = 1;
    bool AuthorityConstructed = false;
    bool TransactionPaused = false;
    FCk_Handle _ControlledOverrideTarget;
    bool _ControlledOverrideSucceeded = false;
    bool ModifierControlPending = false;
    bool BuffNormalizationPending = false;
    FCk_Handle_ByteAttributeModifier PendingNormalizedBuff;
    TArray<FCk_Handle_ByteAttributeModifier> PendingModifierRetirement;
    bool ModifierResetNeedsWeapon = false;
    bool ModifierReplacementStarted = false;
    int32 PendingExpectedModifierCount = 0;

    FCk_Handle_ByteAttribute DamageAttribute;
    FCk_Handle_ByteAttribute DefenseAttribute;
    TArray<FCk_Handle_ByteAttributeModifier> ActiveModifiers;

    FCk_Handle_Timer AutoTimer;
    bool AutoRunning = true;
    int32 AutoStep = 0;
    FCkGym_AutoConfig AutoConfig;
    int32 ValueChangeCount = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
		AuthorityConstructed = false;
		TransactionPaused = false;
        BuffNormalizationPending = false;
        PendingNormalizedBuff = FCk_Handle_ByteAttributeModifier();
		auto State = CkAttributeByte::Get_State();
		if (!CkAttributeByte::IsLiveEntity(InHandle) || StationIndex != 1 || Generation <= 0 ||
			ck::Is_NOT_Valid(State) ||
			(State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex) == false) ||
			utils_entity_lifetime::Get_LifetimeOwner(InHandle) != CkAttributeByte::FindStation(StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Invalid Byte station construction lease"); }
			return ECk_EntityScript_ConstructionFlow::Finished;
		}

        auto _CkPerfScope = ck::ScopedStat();

        utils_entity_tag::Add(InHandle, n"TAG_AttributeGym_ByteModifiers");

        // Auto timer
        auto AutoTimerParams = FCk_Timer_Spec(FCk_Time(2.5f));
		AutoTimerParams.Set_StartingState(ECk_Timer_State::Running).Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
		AutoTimer = utils_timer::Add(InHandle, AutoTimerParams);
		if (CkAttributeByte::IsLiveEntity(FCk_Handle(AutoTimer)))
		{ AutoTimer.BindTo_OnDone(FCk_Delegate_Timer(this, n"AutoTick")); }

        // Auto config
        AutoConfig.TotalSteps = 8;
        AutoConfig.Description = "Cycles through modifier operations: add/remove/modify/clear.\nDemonstrates revocable and non-revocable modifier patterns.";
        AutoConfig.GlobalAutoCommand = "panel [T] Auto-cycle all stations";
        AutoConfig.PerStationAutoCommand = "panel [2] Modifiers station auto";
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add weapon modifier", 0, 0));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add armor modifier", 1, 1));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add buff modifier", 2, 2));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add multiple modifiers", 3, 3));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Remove weapon modifier", 4, 4));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Modify existing buff", 5, 5));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Clear all modifiers", 6, 6));
        AutoConfig.Steps.Add(FCkGym_AutoStep("Add non-revocable modifier", 7, 7));
        AutoConfig.ManualCommands.Add("panel [G] Add modifier - [N] Clear modifiers");

        AuthorityConstructed = CkAttributeByteStation::IsTimer(InHandle, AutoTimer);
        if (!AuthorityConstructed)
        { State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte auto timer allocation failed"); }
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
		auto State = CkAttributeByte::Get_State();
		if (!AuthorityConstructed || ck::Is_NOT_Valid(State) ||
			!State.Get_IsExpectedServerChild(InHandle, Generation, StationIndex))
		{
			if (ck::IsValid(State))
			{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte BeginPlay rejected"); }
			return;
		}

        auto _CkPerfScope = ck::ScopedStat();
        Request_SetupAttributes(InHandle);
        if (!CkAttributeByteStation::IsConstructedAttribute(InHandle, DamageAttribute) ||
			!CkAttributeByteStation::IsConstructedAttribute(InHandle, DefenseAttribute))
		{ State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte modifier attributes invalid"); return; }
		Request_BindSignals(InHandle);
        if (!Request_StartAutomationCycle())
        { State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte initial weapon admission or composition invalid"); return; }
        if (ActiveModifiers.Num() != 1)
        { State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte initial weapon count invalid"); return; }
        if (!Get_HasValidAuthorityComposition())
        { State.Request_FailExpectedServerChild(InHandle, Generation, StationIndex, "Byte initial composition after weapon invalid"); return; }
    }

    void Request_SetupAttributes(FCk_Handle InHandle)
    {
        auto DamageParams = FCk_ByteAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"ByteAttribute.Damage"), 50);
        DamageParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(10).Set_MaxValue(200);
        DamageAttribute = utils_byte_attribute::Add(InHandle, DamageParams, ECk_Replication::DoesNotReplicate);

        auto DefenseParams = FCk_ByteAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"ByteAttribute.Defense"), 30);
        DefenseParams.Set_MinMax(ECk_MinMax::MinMax).Set_MinValue(0).Set_MaxValue(100);
        DefenseAttribute = utils_byte_attribute::Add(InHandle, DefenseParams, ECk_Replication::DoesNotReplicate);
    }

    void Request_BindSignals(FCk_Handle InHandle)
    {
        if (ck::IsValid(DamageAttribute))
        {
            utils_byte_attribute::BindTo_OnValueChanged(DamageAttribute, ECk_MinMaxCurrent::Current,
                FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnDamageValueChanged"));
        }
        if (ck::IsValid(DefenseAttribute))
        {
            utils_byte_attribute::BindTo_OnValueChanged(DefenseAttribute, ECk_MinMaxCurrent::Current,
                FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnDefenseValueChanged"));
        }
    }

    bool Request_StartAutomationCycle()
    {
        // This runs once during construction and again at authored auto step zero.
        if (!Get_HasValidAuthorityComposition()) { return false; }
        auto Existing = utils_byte_attribute_modifier::TryGet(DamageAttribute,
            utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"),
            ECk_MinMaxCurrent::Current);
        if (ck::IsValid(Existing))
        {
            return ActiveModifiers.Contains(Existing) &&
                CkAttributeByteStation::IsRevocableModifier(DamageAttribute,
                    Existing, n"Modifier.Weapon", uint8(25));
        }
        FCk_Handle_ByteAttributeModifier Created;
        if (!Request_TryCreateControlledModifier(DamageAttribute,
            n"Modifier.Weapon", uint8(25), Created)) { return false; }
        ActiveModifiers.Add(Created);
        if (!Get_HasValidAuthorityComposition())
        {
            ActiveModifiers.Remove(Created);
            utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(Created));
            return false;
        }
        return true;
    }



    UFUNCTION()
    private void AutoTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
		if (InHandle != AutoTimer || !Get_CanMutate()) { return; }
		if (!Get_HasValidAuthorityComposition()) { Request_FailCurrent("Byte auto composition lost"); return; }

        auto Step = AutoStep % AutoConfig.TotalSteps;

        if (Step == 0 && !Request_StartAutomationCycle())
        { Request_FailCurrent("Byte weapon identity changed during auto step"); return; }
        else if (Step == 1) { Request_AddArmorModifier(); }
        else if (Step == 2) { Request_AddBuffModifier(); }
        else if (Step == 3) { Request_AddMultipleModifiers(); }
        else if (Step == 4) { Request_RemoveWeaponModifier(); }
        else if (Step == 5) { Request_ModifyExistingModifier(); }
        else if (Step == 6) { Request_ClearAllModifiers(); }
        else if (Step == 7) { Request_TestNotRevocableModifiers(); }

        AutoStep++;
    }



    void Request_AddArmorModifier()
    {
        if (ck::Is_NOT_Valid(DefenseAttribute)) return;
        auto ArmorParams = FCk_ByteAttributeModifier_Spec();
        ArmorParams.Set_ModifierDelta(15);
        auto ArmorMod = utils_byte_attribute_modifier::Add_Revocable(DefenseAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Armor"), ECk_AttributeModifier_Operation::Add, ArmorParams);
        if (ck::IsValid(ArmorMod)) { ActiveModifiers.Add(ArmorMod); }
    }

    void Request_AddBuffModifier()
    {
        if (ck::Is_NOT_Valid(DamageAttribute)) return;
        auto BuffParams = FCk_ByteAttributeModifier_Spec();
        BuffParams.Set_ModifierDelta(20);
        auto BuffMod = utils_byte_attribute_modifier::Add_Revocable(DamageAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_AttributeModifier_Operation::Add, BuffParams);
        if (ck::IsValid(BuffMod)) { ActiveModifiers.Add(BuffMod); }
    }

    void Request_AddMultipleModifiers()
    {
        if (ck::Is_NOT_Valid(DefenseAttribute)) return;
        auto ShieldParams = FCk_ByteAttributeModifier_Spec();
        ShieldParams.Set_ModifierDelta(10);
        auto ShieldMod = utils_byte_attribute_modifier::Add_Revocable(DefenseAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Shield"), ECk_AttributeModifier_Operation::Add, ShieldParams);
        if (ck::IsValid(ShieldMod)) { ActiveModifiers.Add(ShieldMod); }

        auto EnchantParams = FCk_ByteAttributeModifier_Spec();
        EnchantParams.Set_ModifierDelta(8);
        auto EnchantMod = utils_byte_attribute_modifier::Add_Revocable(DefenseAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Enchantment"), ECk_AttributeModifier_Operation::Add, EnchantParams);
        if (ck::IsValid(EnchantMod)) { ActiveModifiers.Add(EnchantMod); }
    }

    void Request_RemoveWeaponModifier()
    {
        auto WeaponMod = utils_byte_attribute_modifier::TryGet(DamageAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon"), ECk_MinMaxCurrent::Current);
        if (ck::IsValid(WeaponMod)) { utils_byte_attribute_modifier::Remove(WeaponMod); ActiveModifiers.Remove(WeaponMod); }
    }

    void Request_ModifyExistingModifier()
    {
        auto BuffMod = utils_byte_attribute_modifier::TryGet(DamageAttribute, utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"), ECk_MinMaxCurrent::Current);
        if (ck::IsValid(BuffMod)) { utils_byte_attribute_modifier::Override(BuffMod, 35); }
    }

    void Request_ClearAllModifiers()
    {
        utils_byte_attribute_modifier::Request_ClearAllModifiers(DamageAttribute, ECk_MinMaxCurrent::Current);
        utils_byte_attribute_modifier::Request_ClearAllModifiers(DefenseAttribute, ECk_MinMaxCurrent::Current);
        ActiveModifiers.Empty();
    }

    void Request_TestNotRevocableModifiers()
    {
        if (ck::Is_NOT_Valid(DamageAttribute)) return;
        auto PermanentParams = FCk_ByteAttributeModifier_Spec();
        PermanentParams.Set_ModifierDelta(12);
        utils_byte_attribute_modifier::Add_NotRevocable(DamageAttribute, ECk_AttributeModifier_Operation::Add, PermanentParams);
    }

    FString Request_BuildBody()
    {
        auto DisplayText = gym_auto::FormatHeader(AutoConfig, AutoRunning);

        DisplayText = f"{DisplayText}Changes: {ValueChangeCount}\n\n";

        if (ck::IsValid(DamageAttribute))
        {
            auto BaseValue = utils_byte_attribute::Get_BaseValue(DamageAttribute);
            auto BonusValue = utils_byte_attribute::Get_BonusValue(DamageAttribute);
            auto FinalValue = utils_byte_attribute::Get_FinalValue(DamageAttribute);
            auto DamageBar = CkGym_Attribute::Create_ProgressBar(FinalValue, 200.0f, 20);
            DisplayText = f"{DisplayText}Damage: {BaseValue} + {BonusValue} = {FinalValue}/200\n";
            DisplayText = f"{DisplayText}[{DamageBar}]\n\n";
        }

        if (ck::IsValid(DefenseAttribute))
        {
            auto BaseValue = utils_byte_attribute::Get_BaseValue(DefenseAttribute);
            auto BonusValue = utils_byte_attribute::Get_BonusValue(DefenseAttribute);
            auto FinalValue = utils_byte_attribute::Get_FinalValue(DefenseAttribute);
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
                auto Delta = utils_byte_attribute_modifier::Get_Delta(Modifier);
                ModifierCount++;
                DisplayText = f"{DisplayText}  Mod {ModifierCount}: +{Delta}\n";
            }
        }

        DisplayText = DisplayText + "\n" + gym_auto::FormatAutoAndCommands(AutoConfig, AutoStep, AutoRunning);

        return DisplayText;
    }





    UFUNCTION() void OnDamageValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
        if (InAttributeOwnerEntity != ck::ToEntity(this) || !Get_CanMutate()) { return; }
        if (!Get_HasValidAuthorityComposition())
        { Request_FailCurrent("Byte signal composition lost"); return; }
 ValueChangeCount++; }
    UFUNCTION() void OnDefenseValueChanged(FCk_Handle InAttributeOwnerEntity, FCk_Payload_ByteAttribute_OnValueChanged InPayload) {
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
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, DamageAttribute)) { return false; }
        if (!CkAttributeByteStation::IsConstructedAttribute(Self, DefenseAttribute)) { return false; }
        return true;
    }

    bool Get_HasValidAuthorityComposition()
    {
        auto Self = ck::ToEntity(this);
        if (!AuthorityConstructed || StationIndex != 1 || Generation <= 0 ||
            !CkAttributeByte::IsLiveEntity(Self) ||
            !CkAttributeByteStation::IsTimer(Self, AutoTimer)) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, DamageAttribute, n"ByteAttribute.Damage")) { return false; }
        if (!CkAttributeByteStation::IsAttribute(Self, DefenseAttribute, n"ByteAttribute.Defense")) { return false; }
        TArray<FCk_Handle> Expected;
        Expected.Add(FCk_Handle(AutoTimer));
        Expected.Add(FCk_Handle(DamageAttribute));
        Expected.Add(FCk_Handle(DefenseAttribute));
        if (!CkAttributeByteStation::HasExactDependents(Self, Expected)) { return false; }
        for (auto Modifier : ActiveModifiers)
        {
            if (!CkAttributeByte::IsLiveEntity(FCk_Handle(Modifier))) { return false; }
            auto Owner = utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Modifier));
            if (Owner != FCk_Handle(DamageAttribute) && Owner != FCk_Handle(DefenseAttribute)) { return false; }
        }
        TArray<FCk_Handle_ByteAttribute> ModifierOwners;
        ModifierOwners.Add(DamageAttribute); ModifierOwners.Add(DefenseAttribute);
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
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(DamageAttribute, n"ByteAttribute.Damage"));
        Sample.Attributes.Add(CkAttributeByteStation::Attribute(DefenseAttribute, n"ByteAttribute.Defense"));
        Sample.ValueChangeCount = ValueChangeCount;
        Sample.ActiveModifierCount = ActiveModifiers.Num();
        for (auto Modifier : ActiveModifiers)
        { Sample.ActiveModifierDeltas.Add(utils_byte_attribute_modifier::Get_Delta(Modifier)); }
        Sample.Title = "BYTE MODIFIERS";
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
        BuffNormalizationPending = false;
        PendingNormalizedBuff = FCk_Handle_ByteAttributeModifier();
        ModifierControlPending = true;
        ModifierResetNeedsWeapon = InReset;
        ModifierReplacementStarted = false;
        PendingExpectedModifierCount = InReset ? 1 : 0;
        utils_byte_attribute_modifier::Request_ClearAllModifiers(DamageAttribute, ECk_MinMaxCurrent::Current);
        utils_byte_attribute_modifier::Request_ClearAllModifiers(DefenseAttribute, ECk_MinMaxCurrent::Current);
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
        if (BuffNormalizationPending)
        {
            if (!Get_HasValidAuthorityComposition())
            {
                Request_FailCurrent("Byte buff composition lost during normalization");
                return false;
            }
            if (!ActiveModifiers.Contains(PendingNormalizedBuff) ||
                !CkAttributeByteStation::IsRevocableModifier(DamageAttribute,
                    PendingNormalizedBuff, n"Modifier.Buff", uint8(20)))
            {
                Request_FailCurrent("Byte normalized buff identity changed");
                return false;
            }
            int32 Raw = int32(utils_byte_attribute::Get_BaseValue(DamageAttribute));
            for (auto Modifier : ActiveModifiers)
            {
                if (utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Modifier)) ==
                    FCk_Handle(DamageAttribute))
                { Raw += int32(utils_byte_attribute_modifier::Get_Delta(Modifier)); }
            }
            uint8 ExpectedPreClamp = uint8(Raw % 256);
            uint8 Min = utils_byte_attribute::Get_FinalValue(DamageAttribute, ECk_MinMaxCurrent::Min);
            uint8 Max = utils_byte_attribute::Get_FinalValue(DamageAttribute, ECk_MinMaxCurrent::Max);
            uint8 ExpectedFinal = ExpectedPreClamp < Min ? Min :
                (ExpectedPreClamp > Max ? Max : ExpectedPreClamp);
            if (utils_byte_attribute::Get_PreClampFinalValue(DamageAttribute) != ExpectedPreClamp ||
                utils_byte_attribute::Get_FinalValue(DamageAttribute) != ExpectedFinal)
            { return false; }
        }

        if (ModifierResetNeedsWeapon && !ModifierReplacementStarted)
        {
            FCk_Handle_ByteAttributeModifier Replacement;
            if (!Request_TryCreateControlledModifier(DamageAttribute,
                n"Modifier.Weapon", uint8(25), Replacement))
            {
                Request_FailCurrent("Byte weapon replacement failed");
                return false;
            }
            ActiveModifiers.Add(Replacement);
            ModifierReplacementStarted = true;
        }

        if (ActiveModifiers.Num() != PendingExpectedModifierCount)
        {
            Request_FailCurrent("Byte modifier membership count changed during control");
            return false;
        }
        if (!Get_HasValidAuthorityComposition()) { return false; }
        PendingModifierRetirement.Empty();
        ModifierControlPending = false;
        BuffNormalizationPending = false;
        PendingNormalizedBuff = FCk_Handle_ByteAttributeModifier();
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
        if (InAction == 2) { return Request_SetAutoRunning(!AutoRunning); }
        if (InAction == 13)
        {
            if (!Request_SetAutoRunning(false)) { return false; }
            auto Existing = utils_byte_attribute_modifier::TryGet(DamageAttribute,
                utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff"),
                ECk_MinMaxCurrent::Current);
            if (ck::IsValid(Existing))
            {
                bool Exact20 = CkAttributeByteStation::IsRevocableModifier(
                    DamageAttribute, Existing, n"Modifier.Buff", uint8(20));
                bool Authored35 = CkAttributeByteStation::IsRevocableModifier(
                    DamageAttribute, Existing, n"Modifier.Buff", uint8(35));
                if (!ActiveModifiers.Contains(Existing) || (!Exact20 && !Authored35))
                {
                    Request_FailCurrent("Byte buff identity changed");
                    return false;
                }
                if (Authored35)
                {
                    PendingNormalizedBuff = Existing;
                    BuffNormalizationPending = true;
                    PendingModifierRetirement.Empty();
                    PendingExpectedModifierCount = ActiveModifiers.Num();
                    ModifierControlPending = true;
                    utils_byte_attribute_modifier::Override(Existing, 20);
                    if (!CkAttributeByteStation::IsRevocableModifier(DamageAttribute,
                        Existing, n"Modifier.Buff", uint8(20)))
                    {
                        Request_FailCurrent("Byte buff normalization failed");
                        return false;
                    }
                }
                return true; // exact keyed repeat or authored +35 normalized in place
            }
            int32 Expected = ActiveModifiers.Num() + 1;
            FCk_Handle_ByteAttributeModifier Added;
            if (!Request_TryCreateControlledModifier(DamageAttribute,
                n"Modifier.Buff", uint8(20), Added))
            {
                Request_FailCurrent("Byte buff creation failed");
                return false;
            }
            ActiveModifiers.Add(Added);
            PendingModifierRetirement.Empty();
            ModifierControlPending = true;
            ModifierResetNeedsWeapon = false;
            ModifierReplacementStarted = false;
            PendingExpectedModifierCount = Expected;
            return true;
        }
        if (InAction == 14)
        {
            if (!Request_SetAutoRunning(false)) { return false; }
            return Request_BeginControlledClear(false);
        }
        if (InAction == 18)
        {
            AutoStep = 0; ValueChangeCount = 0;
            if (!Request_BeginControlledClear(true)) { return false; }
            if (!Request_OverrideChecked(DamageAttribute, 50, ECk_MinMaxCurrent::Current)) { return false; }
            if (!Request_OverrideChecked(DefenseAttribute, 30, ECk_MinMaxCurrent::Current)) { return false; }
            AutoRunning = true;
            if (!TransactionPaused) { return Request_SetAutoRunning(true); }
            return true;
        }
        return false;
    }
}
