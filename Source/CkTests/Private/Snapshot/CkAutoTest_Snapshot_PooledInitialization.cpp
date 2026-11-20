#include "CkTests/Snapshot/CkAutoTest_Snapshot_PooledInitialization.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.inl.h"
#include "CkEcs/Snapshot/CkSaveKey_Fragment.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>

namespace ck_autotest_pooled_initialization
{
    struct FRegistrar
    {
        FRegistrar()
        {
            FCk_PersistenceHandlerRegistry::Register_SaveOnly<FCk_SaveData_AutoTest_PooledValue>({
                .Posture = ECk_Snapshot_Posture::Durable,
                .Produce = [](FCk_Handle& InEntity) -> TOptional<FInstancedStruct>
                {
                    if (NOT InEntity.Has<ck::FFragment_AutoTest_PooledValue>())
                    { return {}; }

                    const auto& Value = InEntity.Get<ck::FFragment_AutoTest_PooledValue>();
                    if (NOT Value._Produces)
                    { return {}; }

                    auto Payload = FCk_SaveData_AutoTest_PooledValue{};
                    Payload.Set_Value(Value._Value);
                    return FInstancedStruct::Make(Payload);
                },
                .HydrationApply = [](FCk_Handle& InEntity, const FInstancedStruct& InNew,
                    const TOptional<FInstancedStruct>&) -> ECk_Persistence_ApplyResult
                {
                    if (NOT InEntity.Has<ck::FFragment_AutoTest_PooledValue>())
                    { return ECk_Persistence_ApplyResult::NotReady; }

                    auto& Value = InEntity.Get<ck::FFragment_AutoTest_PooledValue>();
                    Value._Value = InNew.Get<FCk_SaveData_AutoTest_PooledValue>().Get_Value();
                    Value._AppliedValues.Add(Value._Value);
                    return ECk_Persistence_ApplyResult::Applied;
                },
            });

            FCk_PersistenceHandlerRegistry::Register_SaveOnly<FCk_SaveData_AutoTest_PooledReferences>({
                .Posture = ECk_Snapshot_Posture::Durable,
                .Produce = [](FCk_Handle& InEntity) -> TOptional<FInstancedStruct>
                {
                    if (NOT InEntity.Has<ck::FFragment_AutoTest_PooledRoot>())
                    { return {}; }

                    const auto& Root = InEntity.Get<ck::FFragment_AutoTest_PooledRoot>();
                    auto Payload = FCk_SaveData_AutoTest_PooledReferences{};
                    Payload.Set_Sources(Root._RestoredReferences.IsEmpty()
                        ? Root._ConstructedChildren : Root._RestoredReferences);
                    return FInstancedStruct::Make(Payload);
                },
                .HydrationApply = [](FCk_Handle& InEntity, const FInstancedStruct& InNew,
                    const TOptional<FInstancedStruct>&) -> ECk_Persistence_ApplyResult
                {
                    if (NOT InEntity.Has<ck::FFragment_AutoTest_PooledRoot>())
                    { return ECk_Persistence_ApplyResult::NotReady; }

                    InEntity.Get<ck::FFragment_AutoTest_PooledRoot>()._RestoredReferences =
                        InNew.Get<FCk_SaveData_AutoTest_PooledReferences>().Get_Sources();
                    return ECk_Persistence_ApplyResult::Applied;
                },
            });
        }
    };

    const FRegistrar GPooledInitializationRegistrar{};
}

UCk_AutoTest_Snapshot_PooledInitialization_UE::UCk_AutoTest_Snapshot_PooledInitialization_UE()
{
    _Replication = ECk_Replication::DoesNotReplicate;
    _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity;
}

auto
    UCk_AutoTest_Snapshot_PooledInitialization_UE::Construct(
        FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    using FLifetime = UCk_Utils_EntityLifetime_UE;
    const auto* Params = InSpawnParams.GetPtr<FCk_AutoTest_PooledInitializationParams>();
    if (Params == nullptr)
    { return ECk_EntityScript_ConstructionFlow::Finished; }

    const auto* World = FLifetime::Get_WorldForEntity(InHandle);
    const auto* GameInstance = World ? World->GetGameInstance() : nullptr;
    const auto* Snapshot = GameInstance ? GameInstance->GetSubsystem<UCk_Snapshot_Subsystem_UE>() : nullptr;
    const auto Restoring = Snapshot && Snapshot->Get_IsLoadInProgress();
    auto& Root = InHandle.Add<ck::FFragment_AutoTest_PooledRoot>();
    const auto ChildCount = Restoring ? 1 : 2;
    for (auto Index = 0; Index < ChildCount; ++Index)
    {
        auto Child = FLifetime::Request_CreateEntity(InHandle);
        if (ck::Is_NOT_Valid(Child))
        { continue; }

        ck::save_key::AssignSharedRendezvousGroup(Child, TEXT("Ck.AutoTest.PooledInitialization.Group"));
        auto& Value = Child.Add<ck::FFragment_AutoTest_PooledValue>();
        // The first saved owner deliberately has no value in the single-producer case.
        Value._Produces = Restoring || Index == 1 || Params->TwoProducers;
        if (Params->TrackInitialization)
        {
            Value._DuringConstruct = ck::persistence::RequestInitialization(
                Child, FCk_SaveData_AutoTest_PooledValue::StaticStruct());
        }
        Root._ConstructedChildren.Add(Child);
    }
    return ECk_EntityScript_ConstructionFlow::Finished;
}

auto
    UCk_AutoTest_Snapshot_PooledInitialization_UE::Get_IsSnapshotRespawnable() const
    -> bool
{
    return true;
}
