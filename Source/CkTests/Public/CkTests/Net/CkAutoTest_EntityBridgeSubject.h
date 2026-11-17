#pragma once

#include "CkEcs/EntityBridge/CkEntityBridge_Fragment_Data.h"
#include "CkEcs/EntityScript/CkGenericEntityScript.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"

#include "CkAutoTest_EntityBridgeSubject.generated.h"

class UWorld;

// --------------------------------------------------------------------------------------------------------------------
//
// Subjects for the Ck Entity Bridge tests (Ck.EntityBridge.*).
//
// ACk_AutoTest_EntityBridgeSubject carries a UCk_EntityBridge_ActorComponent_UE. Its BeginPlay binds both of the
// bridge's promises, each appending to a per-instance event list ("Constructed", "Ready"), then requests its Entity
// with FCk_AutoTest_EntityBridge_SpawnParams. The param value is 1 on the authority and 2 elsewhere, so a test can
// tell whose params the script instance received. The script's BeginPlay appends "ScriptBeginPlay" to the same list.
//
// The variants each change one thing: the script's replication, the Actor's replication, the request timing, whether
// the Actor requests at all, whether the script ever finishes construction, and whether the Actor also records the
// C++-only Linked promise.
//
// --------------------------------------------------------------------------------------------------------------------

namespace ck::auto_test::entity_bridge
{
    constexpr auto EventName_Constructed = TEXT("Constructed");
    constexpr auto EventName_ScriptBeginPlay = TEXT("ScriptBeginPlay");
    constexpr auto EventName_Ready = TEXT("Ready");

    constexpr auto ParamValue_Authority = 1;
    constexpr auto ParamValue_NonAuthority = 2;

    // Entities in InWorld carrying ck::FFragment_EntityBridge whose Actor is InActor, pending destruction or not.
    // Matches by object identity, so an entity linked to an Actor that has since been destroyed is still counted.
    CKTESTS_API auto
    Get_LinkedEntityCount(
        UWorld* InWorld,
        const TWeakObjectPtr<const AActor>& InActor) -> int32;

    // Entities in InWorld whose EntityScript instance is exactly of InScriptClass.
    CKTESTS_API auto
    Get_ScriptEntityCount(
        UWorld* InWorld,
        const UClass* InScriptClass) -> int32;

    // Ensures fired so far in this play session from source files whose path contains InFileNameFragment. Read from the
    // ensure occurrence tracker, which records every fire, including a repeat at a site whose log line is suppressed.
    CKTESTS_API auto
    Get_EnsureCount_FromFiles(
        const FString& InFileNameFragment) -> int64;
}

// --------------------------------------------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CKTESTS_API FCk_AutoTest_EntityBridge_SpawnParams
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(FCk_AutoTest_EntityBridge_SpawnParams);

private:
    UPROPERTY(EditAnywhere, BlueprintReadWrite,
              meta = (AllowPrivateAccess = true))
    int32 _ParamValue = 0;

public:
    CK_PROPERTY_GET(_ParamValue);

    CK_DEFINE_CONSTRUCTORS(FCk_AutoTest_EntityBridge_SpawnParams, _ParamValue);
};

// --------------------------------------------------------------------------------------------------------------------

// Actor-bound script of the default subject. Instanced per entity without pooling, so a script instance can never carry
// a _ParamValue injected for another machine's entity.
UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_EntityBridge_EntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCk_AutoTest_EntityBridge_EntityScript_UE);

public:
    UCk_AutoTest_EntityBridge_EntityScript_UE();

protected:
    auto
    BeginPlay() -> void override;

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
              meta = (ExposeOnSpawn = true, AllowPrivateAccess = true))
    int32 _ParamValue = 0;

public:
    CK_PROPERTY_GET(_ParamValue);
};

// --------------------------------------------------------------------------------------------------------------------

// The script asks for a local Entity on every machine, even on a replicated Actor.
UCLASS()
class CKTESTS_API UCk_AutoTest_EntityBridge_LocalEntityScript_UE : public UCk_AutoTest_EntityBridge_EntityScript_UE
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCk_AutoTest_EntityBridge_LocalEntityScript_UE);

public:
    UCk_AutoTest_EntityBridge_LocalEntityScript_UE();
};

// --------------------------------------------------------------------------------------------------------------------

// Construct returns Continue and the script never calls DoFinishConstruction, so it never begins play.
UCLASS()
class CKTESTS_API UCk_AutoTest_EntityBridge_NeverFinishesEntityScript_UE : public UCk_AutoTest_EntityBridge_EntityScript_UE
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCk_AutoTest_EntityBridge_NeverFinishesEntityScript_UE);

public:
    [[nodiscard]]
    auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

// --------------------------------------------------------------------------------------------------------------------

// Not actor-bound: the bridge must refuse it.
UCLASS()
class CKTESTS_API UCk_AutoTest_EntityBridge_PlainEntityScript_UE : public UCk_GenericEntityScript_UE
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCk_AutoTest_EntityBridge_PlainEntityScript_UE);
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API ACk_AutoTest_EntityBridgeSubject : public AActor
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(ACk_AutoTest_EntityBridgeSubject);

public:
    ACk_AutoTest_EntityBridgeSubject();

    // The first Actor in InWorld whose class is exactly InClass (subclasses do not match), or nullptr.
    static auto
    Find(
        UWorld* InWorld,
        TSubclassOf<ACk_AutoTest_EntityBridgeSubject> InClass) -> ACk_AutoTest_EntityBridgeSubject*;

    // Called by UCk_AutoTest_EntityBridge_EntityScript_UE::BeginPlay.
    auto
    Request_RecordEvent(
        FName InEvent) -> void;

protected:
    auto
    BeginPlay() -> void override;

    virtual auto
    DoBind_Promises() -> void;

    virtual auto
    DoRequest_Entity() -> void;

private:
    UFUNCTION()
    void
    OnEntityConstructed(
        FCk_Handle_EntityScript InEntity);

    UFUNCTION()
    void
    OnEntityReady(
        AActor* InActor,
        FCk_Handle InEntity);

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
              Category = "Ck|AutoTest",
              meta = (AllowPrivateAccess = true))
    TObjectPtr<UCk_EntityBridge_ActorComponent_UE> _EntityBridge;

    UPROPERTY()
    TSubclassOf<UCk_EntityScript_UE> _EntityScriptClass;

private:
    UPROPERTY(Transient)
    TArray<FName> _Events;

    UPROPERTY(Transient)
    int32 _ParamValue = 0;

    UPROPERTY(Transient)
    bool _WasReplicationCompleteAtReady = false;

public:
    CK_PROPERTY_GET(_EntityBridge);
    CK_PROPERTY_GET(_EntityScriptClass);
    CK_PROPERTY_GET(_Events);
    CK_PROPERTY_GET(_ParamValue);
    CK_PROPERTY_GET(_WasReplicationCompleteAtReady);
};

// --------------------------------------------------------------------------------------------------------------------

// The script is UCk_AutoTest_EntityBridge_LocalEntityScript_UE.
UCLASS()
class CKTESTS_API ACk_AutoTest_EntityBridgeSubject_LocalScript : public ACk_AutoTest_EntityBridgeSubject
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(ACk_AutoTest_EntityBridgeSubject_LocalScript);

public:
    ACk_AutoTest_EntityBridgeSubject_LocalScript();
};

// --------------------------------------------------------------------------------------------------------------------

// bReplicates = false.
UCLASS()
class CKTESTS_API ACk_AutoTest_EntityBridgeSubject_NotReplicated : public ACk_AutoTest_EntityBridgeSubject
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(ACk_AutoTest_EntityBridgeSubject_NotReplicated);

public:
    ACk_AutoTest_EntityBridgeSubject_NotReplicated();
};

// --------------------------------------------------------------------------------------------------------------------

// Request Timing is Deferred and the Actor never requests: tests drive the bridge themselves.
UCLASS()
class CKTESTS_API ACk_AutoTest_EntityBridgeSubject_Deferred : public ACk_AutoTest_EntityBridgeSubject
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(ACk_AutoTest_EntityBridgeSubject_Deferred);

public:
    ACk_AutoTest_EntityBridgeSubject_Deferred();

protected:
    auto
    DoRequest_Entity() -> void override;
};

// --------------------------------------------------------------------------------------------------------------------

// Default Request Timing, but BeginPlay never requests.
UCLASS()
class CKTESTS_API ACk_AutoTest_EntityBridgeSubject_NeverRequests : public ACk_AutoTest_EntityBridgeSubject
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(ACk_AutoTest_EntityBridgeSubject_NeverRequests);

protected:
    auto
    DoRequest_Entity() -> void override;
};

// --------------------------------------------------------------------------------------------------------------------

// The script is UCk_AutoTest_EntityBridge_NeverFinishesEntityScript_UE.
UCLASS()
class CKTESTS_API ACk_AutoTest_EntityBridgeSubject_NeverFinishes : public ACk_AutoTest_EntityBridgeSubject
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(ACk_AutoTest_EntityBridgeSubject_NeverFinishes);

public:
    ACk_AutoTest_EntityBridgeSubject_NeverFinishes();
};

// --------------------------------------------------------------------------------------------------------------------

// The default subject that also binds the C++-only Promise_OnEntityLinked from BeginPlay, before it requests. Linked is
// recorded apart from the event list, so the list keeps the same contents as the default subject's.
UCLASS()
class CKTESTS_API ACk_AutoTest_EntityBridgeSubject_LinkedProbe : public ACk_AutoTest_EntityBridgeSubject
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(ACk_AutoTest_EntityBridgeSubject_LinkedProbe);

protected:
    auto
    DoBind_Promises() -> void override;

private:
    auto
    DoRecord_Linked(
        AActor* InActor,
        const FCk_Handle& InEntity) -> void;

private:
    int32 _LinkedFireCount = 0;
    FCk_Handle _LinkedEntity;
    TWeakObjectPtr<AActor> _LinkedActor;
    int32 _EventCountAtLinked = INDEX_NONE;
    ECk_EntityBridge_State _StateAtLinked = ECk_EntityBridge_State::NotRequested;

public:
    CK_PROPERTY_GET(_LinkedFireCount);
    CK_PROPERTY_GET(_LinkedEntity);
    CK_PROPERTY_GET(_LinkedActor);
    CK_PROPERTY_GET(_EventCountAtLinked);
    CK_PROPERTY_GET(_StateAtLinked);
};

// --------------------------------------------------------------------------------------------------------------------

// Bound by the specs to the bridge's dynamic delegates and to a spawn request's completion delegate. Latent commands
// are not UObjects, so they cannot bind these themselves. The specs own each instance through a TStrongObjectPtr and
// read the fields directly.
UCLASS()
class CKTESTS_API UCk_AutoTest_EntityBridge_Capturer_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void
    OnEntityConstructed(
        FCk_Handle_EntityScript InEntity);

    UFUNCTION()
    void
    OnEntityReady(
        AActor* InActor,
        FCk_Handle InEntity);

    UFUNCTION()
    void
    OnRequestCompleted(
        FCk_Handle InRequestOwner,
        ECk_Request_OperationResult InResult);

public:
    int32 _ConstructedCount = 0;
    FCk_Handle _ConstructedEntity;

    int32 _ReadyCount = 0;
    FCk_Handle _ReadyEntity;
    TWeakObjectPtr<AActor> _ReadyActor;

    int32 _RequestCompletedCount = 0;
    ECk_Request_OperationResult _RequestResult = ECk_Request_OperationResult::Succeeded;
};

// --------------------------------------------------------------------------------------------------------------------
