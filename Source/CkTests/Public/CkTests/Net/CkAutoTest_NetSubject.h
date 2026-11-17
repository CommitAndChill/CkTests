#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Fragment_Data.h"
#include "CkTagSet/CkTagSet_Fragment_Data.h"
#include "CkPhysics/Acceleration/CkAcceleration_Fragment_Data.h"

#include "CkAutoTest_NetSubject.generated.h"

class UCk_EntityScript_WithActor_UE;

// --------------------------------------------------------------------------------------------------------------------
//
// Subject actor used by the multi-client AutoTest harness as the "subject" of a replication test.
//
// CkFoundation's replicated entities must live under a replicated AActor — the replication driver
// walks the entity's ownership chain looking for a replicated outer Actor to bind the driver
// UObject to.
//
// IMPORTANT — the actor carries a `UCk_EntityBridge_ActorComponent_UE` and requests a
// `UCk_EntityScript_WithActor_UE` entity through it on `BeginPlay`, the same pattern `ACk_ActorRelay_UE` uses.
// On the server the bridge spawns the entity; on a client it waits for the server's replica, which links to
// this actor through the same bridge, so the `Actor -> Entity` reverse lookup works on both machines. The bare
// `Request_CreateEntity` + manual fragment pattern (used by the SM ServerAuthReplay test) has no bridge on the
// client; that's why those tests use per-world recorders instead of direct cross-world entity lookup.
//
// The actor is otherwise minimal — a root and the bridge, no Tick. The test spec spawns it on the
// server, waits a few ticks for ECS construction to complete, then attaches whatever replicated
// fragment the test exercises (e.g. a Float attribute) to the bridged entity via
// `UCk_Utils_EntityBridge_UE::Get_Entity(Subject)`.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API ACk_AutoTest_NetSubject : public AActor
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject();

    // Discovery helper — walks the world for the first ACk_AutoTest_NetSubject and returns it,
    // or nullptr if none exists yet. Test specs use this on both the server world and each
    // client world to locate the subject before resolving its bridged entity and attribute(s).
    // There is at most one subject per world in the harness's current design (one server-spawned
    // actor that replicates to clients); if a test ever needs multiple subjects, this helper
    // needs to grow a discriminator argument.
    static auto
    Find(
        class UWorld* InWorld) -> ACk_AutoTest_NetSubject*;

protected:
    virtual auto BeginPlay() -> void override;

    // Which entity-script class to spawn on BeginPlay (authority side). Subclasses override the
    // default via their constructor (e.g. ACk_AutoTest_NetSubject_Refill swaps in a refill-
    // enabled entity script). Kept as a UPROPERTY rather than a virtual so test code can inspect
    // the configured class via reflection if needed, and so a hypothetical "configure-by-data-
    // asset" path remains open. Must be a UCk_EntityScript_WithActor_UE subclass — the actor↔entity
    // bridge contract depends on the WithActor `Construct` running on both worlds.
    UPROPERTY()
    TSubclassOf<UCk_EntityScript_WithActor_UE> _EntityScriptClass;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCk_EntityBridge_ActorComponent_UE> _EntityBridge;

public:
    // Stashed by the default entity-script (UCk_AutoTest_NetSubject_EntityScript_UE) which adds a
    // Replicates TagSet alongside the Float/Byte/Integer/Vector attributes. Transient, local-only;
    // each world's entity-script populates its own. Used by the CkTagSet net test to drive
    // Request_AddTag on the server and poll HasTag on the client without a by-name lookup (a
    // TagSet is single-per-entity). Subjects spawned with a non-default entity-script leave this
    // unset, which is fine — only the TagSet test reads it.
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_TagSet _TestTagSet;

    // Stashed by the default entity-script alongside the TagSet. The CkPhysics Acceleration net test
    // drives Request_OverrideAcceleration on the server and polls Get_CurrentAcceleration on the
    // client. Acceleration is single-per-entity (no by-tag TryGet), so the actor-stash mirrors
    // _TestTagSet. Transient, local-only; each world's entity-script populates its own.
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_Acceleration _TestAcceleration;
};
