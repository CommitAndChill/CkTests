#include "CkAutoTest_EntityScript_SpawnFailureFixtures.h"

#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck::auto_test::entity_script_spawn_failure
{
    auto
        Get_Records()
        -> TArray<FStepRecord>&
    {
        static auto Records = TArray<FStepRecord>{};
        return Records;
    }

    auto
        Request_Record(
            EStep InStep,
            const FCk_Handle& InEntity)
        -> void
    {
        Get_Records().Add(FStepRecord{
            InStep,
            InEntity,
            ck::IsValid(InEntity) && InEntity.Has<ck::FFragment_EntityScript_OrderingSequence>()});
    }
}

// --------------------------------------------------------------------------------------------------------------------

UCk_AutoTest_EntityScript_OrderedBoundedPool_UE::
    UCk_AutoTest_EntityScript_OrderedBoundedPool_UE()
{
    _Replication = ECk_Replication::DoesNotReplicate;
    _OrderingPolicy = ECk_EntityScript_OrderingPolicy::DefaultGroup;
    _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity_Poolable;
    _PoolParams = FCk_ObjectPooling_PoolParams{}
        .Set_PrewarmCount(0)
        .Set_CapacityPolicy(ECk_ObjectPooling_CapacityPolicy::Bounded)
        .Set_MaxSize(PoolSize);
}

auto
    UCk_AutoTest_EntityScript_OrderedBoundedPool_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    ck::auto_test::entity_script_spawn_failure::Request_Record(
        ck::auto_test::entity_script_spawn_failure::EStep::Construct, InHandle);

    return Super::Construct(InHandle, InSpawnParams);
}

auto
    UCk_AutoTest_EntityScript_OrderedBoundedPool_UE::
    BeginPlay()
    -> void
{
    Super::BeginPlay();

    ck::auto_test::entity_script_spawn_failure::Request_Record(
        ck::auto_test::entity_script_spawn_failure::EStep::BeginPlay, Get_AssociatedEntity());
}

// --------------------------------------------------------------------------------------------------------------------

UCk_AutoTest_EntityScript_UnorderedRetry_UE::
    UCk_AutoTest_EntityScript_UnorderedRetry_UE()
{
    _Replication = ECk_Replication::DoesNotReplicate;
    _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity;
}

auto
    UCk_AutoTest_EntityScript_UnorderedRetry_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    ck::auto_test::entity_script_spawn_failure::Request_Record(
        ck::auto_test::entity_script_spawn_failure::EStep::Construct, InHandle);

    return Super::Construct(InHandle, InSpawnParams);
}

auto
    UCk_AutoTest_EntityScript_UnorderedRetry_UE::
    BeginPlay()
    -> void
{
    Super::BeginPlay();

    ck::auto_test::entity_script_spawn_failure::Request_Record(
        ck::auto_test::entity_script_spawn_failure::EStep::BeginPlay, Get_AssociatedEntity());
}

// --------------------------------------------------------------------------------------------------------------------
