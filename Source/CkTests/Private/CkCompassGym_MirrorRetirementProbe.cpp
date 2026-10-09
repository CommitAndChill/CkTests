#include "CkCompassGym_MirrorRetirementProbe.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityScriptGym_Runtime.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

bool UCkCompassGym_MirrorRetirementProbe::Arm(UWorld* World,
    const FCk_Handle& InMirrorRoot, const FCk_Handle& InHost,
    const FCk_Handle& InMarker)
{
    const auto WorldRoot = IsValid(World) ?
        UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World) : FCk_Handle{};
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
        !World->IsNetMode(NM_Client) || !IsValid(World->GetGameState()) ||
        CallbackCount != 0 || bFailed ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(WorldRoot) ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InMirrorRoot) ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InHost) ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InMarker) ||
        !ck::IsValid(WorldRoot) || !ck::IsValid(InMirrorRoot) ||
        !ck::IsValid(InHost) || !ck::IsValid(InMarker) ||
        InMirrorRoot.Get_RegistryView().Get_RegistryHandle() !=
            WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        InHost.Get_RegistryView().Get_RegistryHandle() !=
            WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        InMarker.Get_RegistryView().Get_RegistryHandle() !=
            WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        !InMirrorRoot.Has<ck::FFragment_LifetimeOwner>() ||
        InMirrorRoot.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != WorldRoot ||
        !InHost.Has<ck::FFragment_LifetimeOwner>() ||
        InHost.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != InMirrorRoot ||
        !InMarker.Has<ck::FFragment_LifetimeOwner>() ||
        InMarker.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != InHost)
    { return false; }
    ExactWorld = World;
    MirrorRoot = InMirrorRoot;
    Host = InHost;
    Marker = InMarker;
    BeginDelegate.BindDynamic(this,
        &UCkCompassGym_MirrorRetirementProbe::OnMarkerBeginDestroy);
    if (!BeginDelegate.IsBound()) { return false; }
    auto MutableMarker = Marker;
    UCk_Utils_EntityLifetime_UE::BindTo_OnBeginDestroy(MutableMarker,
        BeginDelegate, ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
    return true;
}

void UCkCompassGym_MirrorRetirementProbe::OnMarkerBeginDestroy(
    FCk_Handle InMarker)
{
    ++CallbackCount;
    auto* World = ExactWorld.Get();
    if (CallbackCount != 1 || InMarker != Marker || !IsValid(World) ||
        !World->IsNetMode(NM_Client) ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(MirrorRoot) ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Host) ||
        !ck::IsValid(MirrorRoot) || !ck::IsValid(Host) ||
        !Host.Has<ck::FFragment_LifetimeOwner>() ||
        Host.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != MirrorRoot)
    { bFailed = true; return; }
    UnknownChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Host);
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(UnknownChild) ||
        !ck::IsValid(UnknownChild) ||
        !UnknownChild.Has<ck::FFragment_LifetimeOwner>() ||
        UnknownChild.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Host)
    { bFailed = true; }
}
