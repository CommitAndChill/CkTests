//============================================================================
// REPLICATION GYM - REPLICATED ACTOR (Scenario A driver)
//============================================================================
// Direct mirror of ABB_Store. Placed in the world by the PlayerController at
// the station transform. In BeginPlay it requests a replicated entity script
// through its EntityBridge component. This is the exact shape that currently
// ensures.
//============================================================================

// The Actor's script: the shared Scenario A script plus the Actor's debug name.
class UCk_ReplicationGym_ReplicatedActor_LinkedEntityScript : UCk_ReplicationGym_ReplicatedActor_EntityScript
{
    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto Flow = Super::DoConstruct(InHandle);
        InHandle.Set_DebugName(n"ReplicatedActor");
        return Flow;
    }
}

class ACk_ReplicationGym_ReplicatedActor : AActor
{
    default bReplicateMovement = false;
    default bReplicates = true;
    default bReplicateUsingRegisteredSubObjectList = true;

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent RootComponent;

    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        EntityBridge.Request_SpawnLinkedEntity(UCk_ReplicationGym_ReplicatedActor_LinkedEntityScript);
    }
}
