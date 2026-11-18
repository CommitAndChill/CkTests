// Language=angelscript

//============================================================================
// CK AUTOTEST - ACTOR <-> ENTITY HELPER
//============================================================================
//
// Reusable scaffold for AutoTests that need an entity with the
// OwningActor fragment (i.e., a feature module whose Add ensures on
// missing OwningActor - CkActor, CkCamera, CkOverlapBody, CkPhysics,
// CkProjectile, CkChaos, etc.).
//
// USAGE:
//   class UCk_AutoTest_MyModule_Foo : UCk_AutoTest_Base
//   {
//       private ACkAutoTest_ActorEntity_Helper _Helper;
//
//       UFUNCTION(BlueprintOverride)
//       void DoBeginPlay(FCk_Handle InHandle)
//       {
//           _Helper = Cast<ACkAutoTest_ActorEntity_Helper>(SpawnActor(
//               ACkAutoTest_ActorEntity_Helper, FVector::ZeroVector, FRotator::ZeroRotator));
//           if (ck::Is_NOT_Valid(_Helper))
//           { FinishFailure("Failed to spawn ActorEntity helper"); return; }
//
//           _Helper.EntityBridge.Promise_OnEntityConstructed(
//               FCk_Delegate_EntityScript_Constructed(this, n"OnEntityReady"));
//       }
//
//       UFUNCTION()
//       private void OnEntityReady(FCk_Handle_EntityScript InEntityScriptHandle)
//       {
//           if (IsFinished()) { return; }
//           auto OwnedEntity = FCk_Handle(InEntityScriptHandle);
//           // OwnedEntity now has the OwningActor fragment - safe for
//           // CkActor / CkCamera / CkOverlapBody / etc. Add calls.
//           ...
//       }
//   }
//
// The helper actor:
//   - Has a USceneComponent root (Movable) so SceneNode/Transform-driven
//     features that touch the actor's RootComponent work.
//   - Requests its entity (a UCk_EntityScript_WithActor_UE subclass)
//     through its UCk_EntityBridge_ActorComponent_UE. The resulting
//     entity carries FFragment_EntityBridge.
//   - bReplicates = false. AutoTests run as authority; replication adds
//     spurious entity creation work that complicates assertions.
//
// PATTERN ORIGIN: Mirrors the inline helper in
// CkAutoTest_SceneNode_ActorAttachedToActor.as - that test landed
// the pattern; this file extracts it so every actor-backed module seed
// can reuse it without copy-pasting the actor class.
//============================================================================

// Non-replicating EntityScript subclass - UCk_EntityScript_WithActor_UE
// defaults to Replicates, which ensure-fails against a non-replicated
// owner actor (no remote authority). AutoTests run as authority on a
// single instance, so we opt out of replication entirely.
class UCkAutoTest_ActorEntity_EntityScript : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
}

UCLASS()
class ACkAutoTest_ActorEntity_Helper : AActor
{
    default bReplicates = false;

    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent SceneRoot;

    default SceneRoot.Mobility = EComponentMobility::Movable;

    UPROPERTY(DefaultComponent)
    UCk_EntityBridge_ActorComponent_UE EntityBridge;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        auto _CkPerfScope = ck::ScopedStat();
        EntityBridge.Request_SpawnLinkedEntity(UCkAutoTest_ActorEntity_EntityScript);
    }
}
