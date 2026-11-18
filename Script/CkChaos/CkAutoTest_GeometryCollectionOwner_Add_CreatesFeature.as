// Language=angelscript

//============================================================================
// CK CHAOS - AUTOMATION TEST: GEOMETRY COLLECTION OWNER ADD
//============================================================================
//
// First-coverage seed for CkChaos. GeometryCollectionOwner Add ensures
// the entity has FFragment_EntityBridge, so the test spawns an
// actor-backed entity via ACkAutoTest_ActorEntity_Helper, waits for the
// entity to be ready, then verifies the owner Add succeeds.
//============================================================================

class UCk_AutoTest_GeometryCollectionOwner_Add_CreatesFeature : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 4.0f;

    private ACkAutoTest_ActorEntity_Helper _Helper;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        _Helper = Cast<ACkAutoTest_ActorEntity_Helper>(SpawnActor(
            ACkAutoTest_ActorEntity_Helper, FVector::ZeroVector, FRotator::ZeroRotator));
        if (ck::Is_NOT_Valid(_Helper))
        {
            FinishFailure("Failed to spawn ActorEntity helper");
            return;
        }

        _Helper.EntityBridge.Promise_OnEntityReady(
            FCk_Delegate_EntityBridge_OnEntityReady(this, n"OnEntityReady"));
    }

    UFUNCTION()
    private void OnEntityReady(AActor InActor, FCk_Handle InEntity)
    {
        if (IsFinished()) { return; }

        auto OwnedEntity = InEntity;
        auto Owner = utils_geometry_collection_owner::Add(OwnedEntity, ECk_Replication::DoesNotReplicate);

        Assert_True(ck::IsValid(Owner),
            "utils_geometry_collection_owner::Add should return a valid handle once the entity has an OwningActor");
        Assert_True(utils_geometry_collection_owner::Has(OwnedEntity),
            "After Add, Has on the owning entity should report true");

        FinishSuccess();
    }
}
