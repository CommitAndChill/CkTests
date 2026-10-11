//============================================================================
// REPLICATION GYM - PAWN EXTRA ENTITY SCRIPT (Scenario B)
//============================================================================
// A second UCk_EntityScript_WithActor_UE attached to the replicated pawn,
// with ck::TransientEntity() as the lifetime owner. Mirrors the shape that
// caused ensures on BB_PlayerCharacter after re-ordering attribute setup.
//============================================================================

class UCk_ReplicationGym_PawnExtra_EntityScript : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::Replicates;

    const int32 StartingValue = 50;

    FCk_Handle_IntegerAttribute ValueAttribute;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Pawn = Cast<ACk_ReplicationGym_Pawn>(utils_owning_actor::TryGet_EntityOwningActor(InHandle));
        if (ck::Is_NOT_Valid(Pawn))
        {
            ck::Error("[ReplicationGym] Pawn extra script has no authored owning pawn");
            utils_entity_lifetime::Request_DestroyEntity(InHandle);
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        if (!Pawn.HasAuthority() && !Pawn.Request_RegisterExtraReplica(InHandle))
        {
            ck::Error("[ReplicationGym] Pawn extra client match registration rejected");
            utils_entity_lifetime::Request_DestroyEntity(InHandle);
            return ECk_EntityScript_ConstructionFlow::Finished;
        }

        utils_entity_tag::Add(InHandle, n"TAG_ReplicationGym_PawnExtra");

        // *** REPRO LINE - Scenario B ***
        auto Params = FCk_IntegerAttribute_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"IntegerAttribute.ReplicationGym.PawnValue"),
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


        ck::Warning("[ReplicationGym] PawnExtra entity construction done - "
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
