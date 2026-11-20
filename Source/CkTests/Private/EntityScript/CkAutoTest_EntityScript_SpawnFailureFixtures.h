#pragma once

#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/Handle/CkHandle.h"

#include "CkAutoTest_EntityScript_SpawnFailureFixtures.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Fixtures for the ordered-spawn failure spec (CkEntityScript_OrderedSpawnFailure.spec.cpp).
//
// UCk_AutoTest_EntityScript_OrderedBoundedPool_UE is an ordered (default group) script whose instance pool is bounded
// at PoolSize instances: once that many are live, a request for it cannot create its script object and fails. Its
// entities leaving play return their instances to the pool, which frees a slot for the next request.
// UCk_AutoTest_EntityScript_UnorderedRetry_UE is an unordered script with no pool.
//
// Both record their Construct and BeginPlay, and whether their entity carried an ordering sequence at that step, into
// one process-wide list. Specs MUST reset it at the start of each RunTest.
//
// --------------------------------------------------------------------------------------------------------------------

namespace ck::auto_test::entity_script_spawn_failure
{
    enum class EStep : uint8
    {
        Construct,
        OnConstructed,
        BeginPlay
    };

    struct FStepRecord
    {
        EStep _Step = EStep::Construct;
        FCk_Handle _Entity;
        bool _HadOrderingSequence = false;
    };

    auto
    Get_Records() -> TArray<FStepRecord>&;

    auto
    Request_Record(
        EStep InStep,
        const FCk_Handle& InEntity) -> void;
}

// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable)
class UCk_AutoTest_EntityScript_OrderedBoundedPool_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCk_AutoTest_EntityScript_OrderedBoundedPool_UE);

public:
    static constexpr auto PoolSize = 2;

public:
    UCk_AutoTest_EntityScript_OrderedBoundedPool_UE();

    auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;

    auto
    BeginPlay() -> void override;
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable)
class UCk_AutoTest_EntityScript_UnorderedRetry_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    CK_GENERATED_BODY(UCk_AutoTest_EntityScript_UnorderedRetry_UE);

public:
    UCk_AutoTest_EntityScript_UnorderedRetry_UE();

    auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;

    auto
    BeginPlay() -> void override;
};

// --------------------------------------------------------------------------------------------------------------------
