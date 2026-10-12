// Language=angelscript

//============================================================================
// INTEGER MODIFIERS GYM - STEP STATES
//============================================================================
//
// The demo sequence for the Integer-attribute modifier station, as a
// CkStateMachine graph. Replaces the `AutoStep % 4` + if-else dispatch that
// used to live in the station's AutoTick.
//
//   AddWeapon -> AddBuff -> ClearAll -> AddDefaults -> (back to AddWeapon)
//
// Each state acts on the STATION ENTITY through utils_*, never on the station
// script's members: a state is its own entity script and has no business
// reaching into another object's fields. The Damage attribute is looked up by
// tag off the station entity, which is also what makes the old
// `ActiveModifiers` bookkeeping array unnecessary - the framework already
// tracks modifiers, and TryGet finds them by tag.
//============================================================================

namespace integer_gym_modifiers
{
    FCk_Handle_IntegerAttribute Get_DamageAttribute(FCk_Handle InStation)
    {
        if (!CkAttributeInteger::IsLiveEntity(InStation)) { return FCk_Handle_IntegerAttribute(); }
        auto Damage = utils_integer_attribute::TryGet(InStation,
            utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.Damage"));
        auto Handle = FCk_Handle(Damage);
        if (!CkAttributeInteger::IsLiveEntity(Handle) ||
            utils_entity_lifetime::Get_LifetimeOwner(Handle) != InStation ||
            !utils_integer_attribute::DoCast(Handle).IsSet() ||
            !utils_integer_attribute::Has_Component(Damage, ECk_MinMaxCurrent::Min) ||
            !utils_integer_attribute::Has_Component(Damage, ECk_MinMaxCurrent::Current) ||
            utils_integer_attribute::Has_Component(Damage, ECk_MinMaxCurrent::Max))
        { return FCk_Handle_IntegerAttribute(); }
        return Damage;
    }

    bool HasExactModifiers(FCk_Handle_IntegerAttribute InDamage,
        FCk_Handle_IntegerAttributeModifier InWeapon,
        FCk_Handle_IntegerAttributeModifier InBuff)
    {
        auto Damage = FCk_Handle(InDamage);
        if (!CkAttributeInteger::IsLiveEntity(Damage) ||
            !utils_integer_attribute::DoCast(Damage).IsSet() ||
            !utils_integer_attribute::Has_Component(InDamage, ECk_MinMaxCurrent::Current))
        { return false; }
        auto WeaponTag = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon");
        auto BuffTag = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff");
        auto All = utils_integer_attribute_modifier::ForEach(InDamage,
            FInstancedStruct(), FCk_Lambda_InHandle());
        int32 Expected = 0;
        if (ck::IsValid(InWeapon)) { Expected++; }
        if (ck::IsValid(InBuff)) { Expected++; }
        if (All.Num() != Expected || (ck::IsValid(InWeapon) && InWeapon == InBuff))
        { return false; }
        auto Seen = TArray<FCk_Handle>();
        for (auto Modifier : All)
        {
            auto Handle = FCk_Handle(Modifier);
            if (!CkAttributeInteger::IsLiveEntity(Handle) || Seen.Contains(Handle) ||
                utils_entity_lifetime::Get_LifetimeOwner(Handle) != Damage ||
                !utils_integer_attribute_modifier::Has_Component(Handle, ECk_MinMaxCurrent::Current) ||
                !utils_gameplay_label::Has(Handle)) { return false; }
            auto Label = utils_gameplay_label::Get_Label(Handle);
            if (!((Modifier == InWeapon && Label == WeaponTag &&
                    utils_integer_attribute_modifier::TryGet(InDamage, WeaponTag,
                        ECk_MinMaxCurrent::Current) == Modifier) ||
                  (Modifier == InBuff && Label == BuffTag &&
                    utils_integer_attribute_modifier::TryGet(InDamage, BuffTag,
                        ECk_MinMaxCurrent::Current) == Modifier))) { return false; }
            Seen.Add(Handle);
        }
        return Seen.Num() == Expected;
    }

    UCk_EntityScript_IntegerGym_Modifiers Get_StationScript(FCk_Handle InStation)
    {
        if (!CkAttributeInteger::IsLiveEntity(InStation)) { return nullptr; }
        auto Script = Cast<UCk_EntityScript_IntegerGym_Modifiers>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(InStation));
        return ck::IsValid(Script) && ck::ToEntity(Script) == InStation ? Script : nullptr;
    }

    bool Request_SetModifier(FCk_Handle InStation, FName InModifierTag, int32 InDelta)
    {
        auto Damage = Get_DamageAttribute(InStation);
        if (ck::Is_NOT_Valid(Damage)) { return false; }
        if (InModifierTag != n"Modifier.Weapon" && InModifierTag != n"Modifier.Buff")
        { return false; }
        auto WeaponTag = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Weapon");
        auto BuffTag = utils_gameplay_tag::ResolveGameplayTag(n"Modifier.Buff");
        auto Tag = InModifierTag == n"Modifier.Weapon" ? WeaponTag : BuffTag;
        auto Weapon = utils_integer_attribute_modifier::TryGet(
            Damage, WeaponTag, ECk_MinMaxCurrent::Current);
        auto Buff = utils_integer_attribute_modifier::TryGet(
            Damage, BuffTag, ECk_MinMaxCurrent::Current);
        if (!HasExactModifiers(Damage, Weapon, Buff)) { return false; }
        auto Existing = InModifierTag == n"Modifier.Weapon" ? Weapon : Buff;
        if (ck::IsValid(Existing))
        {
            // HasExactModifiers proved the live Current feature, owner, label and
            // exact keyed record before Override touches the modifier fragment.
            auto Same = utils_integer_attribute_modifier::Override(Existing, InDelta);
            if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(Same)) || Same != Existing)
            { return false; }
            auto PostWeapon = utils_integer_attribute_modifier::TryGet(
                Damage, WeaponTag, ECk_MinMaxCurrent::Current);
            auto PostBuff = utils_integer_attribute_modifier::TryGet(
                Damage, BuffTag, ECk_MinMaxCurrent::Current);
            return HasExactModifiers(Damage, PostWeapon, PostBuff) &&
                (InModifierTag == n"Modifier.Weapon" ? PostWeapon == Same : PostBuff == Same) &&
                utils_integer_attribute_modifier::Get_Delta(Same) == InDelta;
        }
        auto Params = FCk_IntegerAttributeModifier_Spec();
        Params.Set_ModifierDelta(InDelta);
        auto Added = utils_integer_attribute_modifier::Add_Revocable(
            Damage, Tag, ECk_AttributeModifier_Operation::Add, Params);
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(Added))) { return false; }
        auto PostWeapon = utils_integer_attribute_modifier::TryGet(
            Damage, WeaponTag, ECk_MinMaxCurrent::Current);
        auto PostBuff = utils_integer_attribute_modifier::TryGet(
            Damage, BuffTag, ECk_MinMaxCurrent::Current);
        return HasExactModifiers(Damage, PostWeapon, PostBuff) &&
            (InModifierTag == n"Modifier.Weapon" ? PostWeapon == Added : PostBuff == Added) &&
            utils_integer_attribute_modifier::Get_Delta(Added) == InDelta;
    }
}

// Only this station's dwell can advance its graph. A transaction closes the
// station gate before waiting for an already-queued transition to settle.
UCLASS()
class UCk_IntegerGym_Dwell : UCk_SmCondition_EventDriven
{
    UPROPERTY(EditAnywhere) float32 DwellSeconds = 2.5f;

    UFUNCTION(BlueprintOverride)
    void DoEnterCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    { if (CkAttributeInteger::IsLiveEntity(FCk_Handle(InHandle)))
      { System::SetTimer(this, n"TryAdvance", DwellSeconds, false); } }

    UFUNCTION()
    private void TryAdvance()
    {
        if (!CkAttributeInteger::IsLiveEntity(ck::ToEntity(this))) { return; }
        auto OwnerSm = Get_OwningStateMachine();
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(OwnerSm)) ||
            !utils_state_machine::Has(FCk_Handle(OwnerSm))) { return; }
        auto Station = integer_gym_modifiers::Get_StationScript(Get_StateMachineContext());
        if (ck::Is_NOT_Valid(Station) || Station.Get_HasStepFailure()) { return; }
        if (OwnerSm.Get_RunStatus() != ECk_SmRunStatus::Running ||
            !Station.Request_TryBeginAutoAdvance())
        { System::SetTimer(this, n"TryAdvance", DwellSeconds, false); return; }
        MarkSatisfied();
    }
}

UCLASS()
class UCk_IntegerGym_GatedStepState : UCk_Gym_StepState
{
    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext)
    {
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(InHandle))) { return; }
        auto Station = integer_gym_modifiers::Get_StationScript(Get_StationEntity());
        if (ck::IsValid(Station)) { Station.Request_RecordStepExit(InHandle); }
    }
}

// ====================================================================================================================

UCLASS()
class UCk_IntegerGym_Step_AddWeapon : UCk_IntegerGym_GatedStepState
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Trans = AddTransition(InHandle, UCk_IntegerGym_Step_AddBuff);
        AddCondition(Trans, UCk_IntegerGym_Dwell);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext)
    {
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(InHandle))) { return; }
        auto _CkPerfScope = ck::ScopedStat();
        auto Station = integer_gym_modifiers::Get_StationScript(Get_StationEntity());
        if (ck::Is_NOT_Valid(Station) || !integer_gym_modifiers::Request_SetModifier(Get_StationEntity(), n"Modifier.Weapon", 25))
        { if (ck::IsValid(Station)) { Station.Request_ReportStepFailure(); } return; }
        Station.Request_RecordStepEntry(InHandle);
    }
}

// ----------------------------------------------------------------------------

UCLASS()
class UCk_IntegerGym_Step_AddBuff : UCk_IntegerGym_GatedStepState
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Trans = AddTransition(InHandle, UCk_IntegerGym_Step_ClearAll);
        AddCondition(Trans, UCk_IntegerGym_Dwell);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext)
    {
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(InHandle))) { return; }
        auto _CkPerfScope = ck::ScopedStat();
        auto Station = integer_gym_modifiers::Get_StationScript(Get_StationEntity());
        if (ck::Is_NOT_Valid(Station) || !integer_gym_modifiers::Request_SetModifier(Get_StationEntity(), n"Modifier.Buff", 10))
        { if (ck::IsValid(Station)) { Station.Request_ReportStepFailure(); } return; }
        Station.Request_RecordStepEntry(InHandle);
    }
}

// ----------------------------------------------------------------------------

UCLASS()
class UCk_IntegerGym_Step_ClearAll : UCk_IntegerGym_GatedStepState
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Trans = AddTransition(InHandle, UCk_IntegerGym_Step_AddDefaults);
        AddCondition(Trans, UCk_IntegerGym_Dwell);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext)
    {
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(InHandle))) { return; }
        auto _CkPerfScope = ck::ScopedStat();
        auto Damage = integer_gym_modifiers::Get_DamageAttribute(Get_StationEntity());
        auto Station = integer_gym_modifiers::Get_StationScript(Get_StationEntity());
        if (ck::Is_NOT_Valid(Station) || ck::Is_NOT_Valid(Damage))
        { if (ck::IsValid(Station)) { Station.Request_ReportStepFailure(); } return; }
        utils_integer_attribute_modifier::Request_ClearAllModifiers(Damage, ECk_MinMaxCurrent::Current);
        Station.Request_RecordStepEntry(InHandle);
    }
}

// ----------------------------------------------------------------------------

UCLASS()
class UCk_IntegerGym_Step_AddDefaults : UCk_IntegerGym_GatedStepState
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Trans = AddTransition(InHandle, UCk_IntegerGym_Step_AddWeapon);
        AddCondition(Trans, UCk_IntegerGym_Dwell);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext)
    {
        if (!CkAttributeInteger::IsLiveEntity(FCk_Handle(InHandle))) { return; }
        auto _CkPerfScope = ck::ScopedStat();
        auto Station = Get_StationEntity();
        auto Script = integer_gym_modifiers::Get_StationScript(Station);
        if (ck::Is_NOT_Valid(Script) ||
            !integer_gym_modifiers::Request_SetModifier(Station, n"Modifier.Weapon", 25) ||
            !integer_gym_modifiers::Request_SetModifier(Station, n"Modifier.Buff", 10))
        { if (ck::IsValid(Script)) { Script.Request_ReportStepFailure(); } return; }
        Script.Request_RecordStepEntry(InHandle);
    }
}
