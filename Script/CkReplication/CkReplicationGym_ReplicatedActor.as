//============================================================================
// REPLICATION GYM - REPLICATED ACTOR (Scenario A driver)
//============================================================================
// Direct mirror of ABB_Store. Placed in the world by the PlayerController at
// the station transform. In BeginPlay it requests a replicated entity script
// through its EntityBridge component. This is the exact shape that currently
// ensures.
//============================================================================

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
        EntityBridge.Promise_OnEntityConstructed(FCk_Delegate_EntityScript_Constructed(this, n"OnEntityConstructed"));
        EntityBridge.Request_SpawnLinkedEntity(UCk_ReplicationGym_ReplicatedActor_EntityScript);
    }

    UFUNCTION()
    private void OnEntityConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        auto InEntity = FCk_Handle(InEntityScriptHandle);
        InEntity.Set_DebugName(n"ReplicatedActor");
    }
}
