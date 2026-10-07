#include "CkRotateTowardsGym_Runtime.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkRotateTowards/CkRotateTowards_Fragment.h"
#include "CkTween/CkTween_Fragment.h"

#include "Engine/World.h"

namespace ck_rotate_towards_passive_initial
{
    auto Live(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto HasTweenRole(const FCk_Handle& H) -> bool
    {
        return H.Has<ck::FFragment_Tween_Params>() ||
            H.Has<ck::FFragment_Tween>() ||
            H.Has<ck::FFragment_Tween_Requests>() ||
            H.Has<ck::FFragment_Tween_Chain>() ||
            H.Has<ck::FFragment_Tween_SplineFollow>() ||
            H.Has<ck::FFragment_Tween_CurveDrive>() ||
            H.Has<ck::FFragment_Tween_EasingCurve>() ||
            H.Has<ck::FTag_Tween_Playing>() ||
            H.Has<ck::FTag_Tween_Paused>() ||
            H.Has<ck::FTag_Tween_Completed>() ||
            H.Has<ck::FTag_Tween_InYoyoDelay>();
    }

    auto HasRotateRole(const FCk_Handle& H) -> bool
    {
        return H.Has<ck::FFragment_RotateTowards_Tunables>() ||
            H.Has<ck::FFragment_RotateTowards>() ||
            H.Has<ck::FFragment_RotateTowards_SolveResult>() ||
            H.Has<ck::FFragment_RotateTowards_RangeClamp>() ||
            H.Has<ck::FFragment_RotateTowards_Requests>() ||
            H.Has<ck::FTag_RotateTowards_Disabled>() ||
            H.Has<ck::FTag_RotateTowards_HasTarget>() ||
            H.Has<ck::FTag_RotateTowards_AtTarget>();
    }
}

bool UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveMotionEntity(
    const FCk_Handle& Entity, const FCk_Handle& WorldRoot)
{
    if (!ck_rotate_towards_passive_initial::Live(WorldRoot) ||
        WorldRoot != FCk_Handle{WorldRoot.Get_RegistryView().Get_TransientEntity(),
            WorldRoot.Get_RegistryView().Get_RegistryHandle()} ||
        !WorldRoot.Has<TWeakObjectPtr<UWorld>>() ||
        !IsValid(WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get()) ||
        !ck_rotate_towards_passive_initial::Live(Entity) || Entity == WorldRoot ||
        Entity.Get_RegistryView().Get_RegistryHandle() !=
            WorldRoot.Get_RegistryView().Get_RegistryHandle())
    { return false; }
    return !ck_rotate_towards_passive_initial::HasTweenRole(Entity) &&
        !ck_rotate_towards_passive_initial::HasRotateRole(Entity);
}

bool UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(
    const FCk_Handle& Entity, const FCk_Handle& WorldRoot)
{
    // First establish allocated registry-backed values; Has<T> checks only
    // registry validity and is safe on a represented pending-destroy entity.
    if (!ck::IsValid(WorldRoot, ck::IsValid_Policy_IncludePendingKill{}) ||
        !ck::IsValid(Entity, ck::IsValid_Policy_IncludePendingKill{}))
    { return false; }
    const auto Registry = WorldRoot.Get_RegistryView();
    if (WorldRoot != FCk_Handle{Registry.Get_TransientEntity(), Registry.Get_RegistryHandle()} ||
        Entity == WorldRoot ||
        Entity.Get_RegistryView().Get_RegistryHandle() != Registry.Get_RegistryHandle() ||
        !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    auto* const World = WorldRoot.Get<TWeakObjectPtr<UWorld>,
        ck::IsValid_Policy_IncludePendingKill>().Get();
    if (!IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) != WorldRoot)
    { return false; }
    return !ck_rotate_towards_passive_initial::HasTweenRole(Entity) &&
        !ck_rotate_towards_passive_initial::HasRotateRole(Entity);
}
