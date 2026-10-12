// Language=angelscript

UCLASS()
class UCk_FloatFeatureComponent_ASProbe : UObject
{
    UFUNCTION()
    bool Probe(FCk_Handle Current, FCk_Handle Min, FCk_Handle Max,
        FCk_Handle Stale, FCk_Handle Refill, FCk_Handle NonRefill,
        FCk_Handle PartialRefill)
    {
        if (!utils_float_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent::Current) ||
            utils_float_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent::Min) ||
            utils_float_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent::Max))
        { return false; }
        if (!utils_float_attribute_modifier::Has_Component(Min, ECk_MinMaxCurrent::Min) ||
            utils_float_attribute_modifier::Has_Component(Min, ECk_MinMaxCurrent::Current) ||
            utils_float_attribute_modifier::Has_Component(Min, ECk_MinMaxCurrent::Max))
        { return false; }
        if (!utils_float_attribute_modifier::Has_Component(Max, ECk_MinMaxCurrent::Max) ||
            utils_float_attribute_modifier::Has_Component(Max, ECk_MinMaxCurrent::Current) ||
            utils_float_attribute_modifier::Has_Component(Max, ECk_MinMaxCurrent::Min))
        { return false; }
        TArray<ECk_MinMaxCurrent> Components;
        Components.Add(ECk_MinMaxCurrent::Min);
        Components.Add(ECk_MinMaxCurrent::Max);
        Components.Add(ECk_MinMaxCurrent::Current);
        for (auto Component : Components)
        {
            if (utils_float_attribute_modifier::Has_Component(FCk_Handle(), Component) ||
                utils_float_attribute_modifier::Has_Component(Stale, Component))
            { return false; }
        }
        return !utils_float_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent(255)) &&
            !utils_float_attribute_modifier::Has_Component(Stale, ECk_MinMaxCurrent(255)) &&
            !utils_float_attribute_modifier::Has_Component(FCk_Handle(), ECk_MinMaxCurrent(255)) &&
            utils_float_attribute_refill::Has_Refill(Refill) &&
            !utils_float_attribute_refill::Has_Refill(NonRefill) &&
            !utils_float_attribute_refill::Has_Refill(PartialRefill) &&
            !utils_float_attribute_refill::Has_Refill(Stale) &&
            !utils_float_attribute_refill::Has_Refill(FCk_Handle());
    }

    UFUNCTION()
    bool Probe_LifetimeOwner(FCk_Handle Attribute, FCk_Handle Modifier,
        FCk_Handle Child, FCk_Handle Transient, FCk_Handle Stale)
    {
        return utils_entity_lifetime::Has_LifetimeOwner(Attribute) &&
            utils_entity_lifetime::Has_LifetimeOwner(Modifier) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            !utils_entity_lifetime::Has_LifetimeOwner(Transient) &&
            !utils_entity_lifetime::Has_LifetimeOwner(Stale) &&
            !utils_entity_lifetime::Has_LifetimeOwner(FCk_Handle());
    }
}
