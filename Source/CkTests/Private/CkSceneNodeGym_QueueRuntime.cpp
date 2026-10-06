#include "CkSceneNodeGym_QueueRuntime.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkTimer/CkTimer_Fragment.h"

#include "Engine/World.h"

auto
    UCk_SceneNodeGym_QueueRuntime_UE::
    Get_AreKnownQueuesDrained(
        TArray<FCk_Handle> InKnownEntities, const FCk_Handle& InWorldRoot)
    -> bool
{
    // Get_RegistryView/Get_RegistryHandle are value reads. No entity/fragment
    // access occurs until the thread, registry and represented-root gates pass.
    if (!IsInGameThread() || InWorldRoot == FCk_Handle{} ||
        InKnownEntities.IsEmpty())
    { return false; }
    const auto Registry = InWorldRoot.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    if (ck::registry_table::Get_IsInParallelRegion(RegistryHandle) ||
        !ck::IsValid(InWorldRoot) ||
        InWorldRoot != FCk_Handle{Registry.Get_TransientEntity(), RegistryHandle} ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(InWorldRoot,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !InWorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }

    auto* const World = InWorldRoot.Get<TWeakObjectPtr<UWorld>>().Get();
    if (!IsValid(World) ||
        World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        World->bIsTearingDown || !World->IsGameWorld() ||
        World->GetNetMode() == NM_Client)
    { return false; }
    const auto* const Subsystem = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    if (!IsValid(Subsystem) ||
        Subsystem->Get_LoadHold() == ECk_EcsWorld_LoadHold::Teardown ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) != InWorldRoot)
    { return false; }

    // Preflight the entire caller-supplied set before reading any queue state.
    // A missing/duplicate/foreign/stale/pending role is failure, never an idle role.
    for (int32 Index = 0; Index < InKnownEntities.Num(); ++Index)
    {
        const FCk_Handle& Entity = InKnownEntities[Index];
        if (Entity == FCk_Handle{} || Entity == InWorldRoot ||
            Entity.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
            !ck::IsValid(Entity) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Entity,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        { return false; }
        for (int32 Previous = 0; Previous < Index; ++Previous)
        {
            if (InKnownEntities[Previous] == Entity)
            { return false; }
        }
    }

    for (const FCk_Handle& Entity : InKnownEntities)
    {
        if (Entity.Has_Any<ck::FFragment_Timer_Requests,
                ck::FFragment_SceneNode_Requests,
                ck::FFragment_Transform_Requests,
                ck::FTag_SceneNode_PropagationQueued,
                ck::FTag_SceneNode_RelativeTransformUpdated,
                ck::FTag_Timer_NeedsUpdate>())
        { return false; }
        // A correctly stopped Timer lacks NeedsUpdate. The explicit tag check
        // above also rejects a malformed partial Timer that still advertises it.
    }
    return true;
}
