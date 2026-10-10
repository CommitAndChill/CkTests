#include "CkStateMachineGym_Runtime.h"

#include "CkEcs/ContextOwner/CkContextOwner_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "Engine/World.h"

namespace ck_state_machine_gym_runtime
{
    bool IsCurrentWorldMember(const FCk_Handle& InEntity,
        const FCk_Handle& InWorldRoot, const FCk_Handle& InRequiredAncestor)
    {
        TSet<FCk_Handle> Seen;
        auto Current = InEntity;
        bool FoundAncestor = Current == InRequiredAncestor;
        int32 Depth = 0;
        while (Current != InWorldRoot)
        {
            if (++Depth > 256 || !ck::IsValid(Current) || Seen.Contains(Current) ||
                Current.Get_RegistryView().Get_RegistryHandle() !=
                    InWorldRoot.Get_RegistryView().Get_RegistryHandle() ||
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Current,
                    ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !Current.Has<ck::FFragment_LifetimeOwner>())
            { return false; }
            Seen.Add(Current);
            Current = Current.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
            FoundAncestor |= Current == InRequiredAncestor;
        }
        return FoundAncestor;
    }
}

auto
    UCk_StateMachineGym_Runtime_UE::
    TryGet_SourceContext(
        const FCk_Handle& InCallback, FCk_Handle& OutContext)
    -> bool
{
    // The input may alias OutContext; copy before clearing the failure output.
    const auto Callback = InCallback;
    OutContext = {};
    if (!IsInGameThread() || Callback == FCk_Handle{}) { return false; }
    const auto Registry = Callback.Get_RegistryView();
    if (ck::registry_table::Get_IsInParallelRegion(
            Registry.Get_RegistryHandle()) ||
        !ck::IsValid(Callback) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Callback,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !Callback.Has<ck::FFragment_ContextOwner>())
    { return false; }

    const auto Context = Callback.Get<ck::FFragment_ContextOwner>().Get_Entity();
    if (!ck::IsValid(Context) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Context,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        Callback.Get_RegistryView().Get_RegistryHandle() !=
            Context.Get_RegistryView().Get_RegistryHandle())
    { return false; }

    const FCk_Handle WorldRoot{Registry.Get_TransientEntity(),
        Registry.Get_RegistryHandle()};
    if (!ck::IsValid(WorldRoot) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(WorldRoot,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    auto* const World = WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get();
    if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown ||
        World->IsBeingCleanedUp() ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) !=
            WorldRoot ||
        !ck_state_machine_gym_runtime::IsCurrentWorldMember(
            Context, WorldRoot, Context) ||
        !ck_state_machine_gym_runtime::IsCurrentWorldMember(
            Callback, WorldRoot, Context))
    { return false; }

    OutContext = Context;
    return true;
}
