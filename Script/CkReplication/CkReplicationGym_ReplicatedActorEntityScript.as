//============================================================================
// REPLICATION GYM - REPLICATED ACTOR ENTITY SCRIPT (Scenario A)
//============================================================================
// Mirror of UBB_Store_EntityScript. Replicates, and in DoConstructWithActor
// adds a replicated Integer Attribute - this is the call path that currently
// triggers:
//   Ensure: No container fragment entry found for type
//           [Ck_RepData_IntegerAttributes] on Entity ...
//============================================================================

class UCk_ReplicationGym_ReplicatedActor_EntityScript : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::Replicates;

    // Hardcoded to keep the spawn params a plain FCk_EntityScript_WithActor_SpawnParams.
    const int32 StartingValue = 100;

    // Server-side message handler uses this handle; local PC presentation reads its replica.
    FCk_Handle_IntegerAttribute ValueAttribute;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        utils_entity_tag::Add(InHandle, n"TAG_ReplicationGym_ReplicatedActor");

        // *** THIS IS THE REPRO LINE ***
        // Adding a replicated Integer Attribute on an entity whose replication
        // driver has no container entry for FCk_RepData_IntegerAttributes yet.
        auto Params = FCk_IntegerAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.ActorValue"),
            StartingValue);
        Params.Set_MinMax(ECk_MinMax::Min);
        Params.Set_MinValue(0);
        ValueAttribute = utils_integer_attribute::Add(InHandle, Params, ECk_Replication::Replicates);

        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        utils_messaging::BindTo_OnBroadcast(InHandle, FCk_Message_ReplicationGym_SetAttribute,
            FCk_Delegate_Messaging_OnBroadcast(this, n"OnSetAttribute"));


        ck::Warning("[ReplicationGym] ReplicatedActor entity construction done - "
                  + "if 'No container fragment entry found' appears above this line, bug reproduced");
    }

    UFUNCTION()
    private void OnSetAttribute(FCk_Handle InHandle, FGameplayTag InMessageName, FInstancedStruct InPayload)
    {
        auto Typed = InPayload.Get(FCk_Message_ReplicationGym_SetAttribute);
        if (ck::IsValid(ValueAttribute))
        {
            utils_integer_attribute::Request_Override(ValueAttribute, Typed.Value, ECk_MinMaxCurrent::Current);
        }
    }

}
