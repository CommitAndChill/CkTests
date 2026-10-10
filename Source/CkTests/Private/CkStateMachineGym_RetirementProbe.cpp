#include "CkStateMachineGym_RetirementProbe.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkStateMachineGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
}

bool UCkStateMachineGym_RetirementProbe::Arm(UWorld* World, AActor* Actor,
    const FCk_Handle& InPrimary, const FCk_Handle& InFirstLeaf)
{
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
        World->GetNetMode() == NM_Client || World->bIsTearingDown ||
        World->IsBeingCleanedUp() || !IsValid(Actor) ||
        Actor->GetWorld() != World || CallbackCount != 0 || bFailed ||
        !Allocated(InPrimary) || !ck::IsValid(InPrimary) ||
        !Allocated(InFirstLeaf) || !ck::IsValid(InFirstLeaf) ||
        InPrimary == InFirstLeaf ||
        !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
            InPrimary, Actor) ||
        InFirstLeaf.Get_RegistryView().Get_RegistryHandle() !=
            InPrimary.Get_RegistryView().Get_RegistryHandle() ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(InFirstLeaf,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy))
    { return false; }
    const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
        TryGet_TransientEntityForWorld(*World);
    if (!Allocated(WorldRoot) || !ck::IsValid(WorldRoot) ||
        WorldRoot.Get_RegistryView().Get_RegistryHandle() !=
            InPrimary.Get_RegistryView().Get_RegistryHandle())
    { return false; }
    FCk_Handle Context;
    if (!UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
            InFirstLeaf, Context) || Context != InPrimary)
    { return false; }
    ExactWorld = World;
    ExactActor = Actor;
    Primary = InPrimary;
    FirstLeaf = InFirstLeaf;
    BeginDelegate.BindDynamic(this,
        &UCkStateMachineGym_RetirementProbe::OnFirstLeafBeginDestroy);
    if (!BeginDelegate.IsBound()) { return false; }
    auto MutableLeaf = FirstLeaf;
    UCk_Utils_EntityLifetime_UE::BindTo_OnBeginDestroy(MutableLeaf,
        BeginDelegate, ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
    return true;
}

void UCkStateMachineGym_RetirementProbe::OnFirstLeafBeginDestroy(
    FCk_Handle InLeaf)
{
    ++CallbackCount;
    auto* World = ExactWorld.Get();
    auto* Actor = ExactActor.Get();
    if (!IsInGameThread() || CallbackCount != 1 ||
        InLeaf != FirstLeaf || !IsValid(World) ||
        World->bIsTearingDown || World->IsBeingCleanedUp() ||
        !IsValid(Actor) || Actor->GetWorld() != World ||
        !Allocated(Primary) || !ck::IsValid(Primary) ||
        !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(
            Primary, Actor))
    { bFailed = true; return; }
    UnknownChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Primary);
    if (!Allocated(UnknownChild) || !ck::IsValid(UnknownChild) ||
        !UnknownChild.Has<ck::FFragment_LifetimeOwner>() ||
        UnknownChild.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Primary)
    { bFailed = true; }
}
