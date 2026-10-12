// Language=angelscript

UCLASS()
class UCk_IntegerModifierComponent_ASProbe : UObject
{
    UFUNCTION()
    bool Probe(FCk_Handle Current, FCk_Handle Min, FCk_Handle Max, FCk_Handle Stale)
    {
        if (!utils_integer_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent::Current) ||
            utils_integer_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent::Min) ||
            utils_integer_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent::Max))
        { return false; }
        if (!utils_integer_attribute_modifier::Has_Component(Min, ECk_MinMaxCurrent::Min) ||
            utils_integer_attribute_modifier::Has_Component(Min, ECk_MinMaxCurrent::Current) ||
            utils_integer_attribute_modifier::Has_Component(Min, ECk_MinMaxCurrent::Max))
        { return false; }
        if (!utils_integer_attribute_modifier::Has_Component(Max, ECk_MinMaxCurrent::Max) ||
            utils_integer_attribute_modifier::Has_Component(Max, ECk_MinMaxCurrent::Current) ||
            utils_integer_attribute_modifier::Has_Component(Max, ECk_MinMaxCurrent::Min))
        { return false; }
        TArray<ECk_MinMaxCurrent> Components;
        Components.Add(ECk_MinMaxCurrent::Min);
        Components.Add(ECk_MinMaxCurrent::Max);
        Components.Add(ECk_MinMaxCurrent::Current);
        for (auto Component : Components)
        {
            if (utils_integer_attribute_modifier::Has_Component(FCk_Handle(), Component) ||
                utils_integer_attribute_modifier::Has_Component(Stale, Component))
            { return false; }
        }
        return !utils_integer_attribute_modifier::Has_Component(Current, ECk_MinMaxCurrent(255)) &&
            !utils_integer_attribute_modifier::Has_Component(FCk_Handle(), ECk_MinMaxCurrent(255)) &&
            !utils_integer_attribute_modifier::Has_Component(Stale, ECk_MinMaxCurrent(255));
    }
}
