#include "CkStateMachineGym_Runtime.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/ContextOwner/CkContextOwner_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment.h"
#include "CkEcs/Persistence/CkPersistenceHydration.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor_Data.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_divergence_pending_primary
{
    // Pending cancellation uses the same complete DNR policy as constructed
    // primary/pass validation. Call only with live handles; do not query an
    // invalid or released registry through FCk_Handle::Has.
    bool HasExactDnrNoDriver(const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot)
    {
        if (!ck::IsValid(Entity) || !ck::IsValid(WorldRoot) ||
            !Entity.Has<ck::FFragment_Net_Params>() ||
            !WorldRoot.Has<ck::FFragment_Net_Params>()) { return false; }
        const auto& Actual = Entity.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        const auto& World = WorldRoot.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        return !Entity.Has<ck::FFragment_PendingHydration>() &&
            !Entity.Has<ck::FTag_Hydration_Quarantine>() &&
            Actual.Get_Replication() == ECk_Replication::DoesNotReplicate &&
            Actual.Get_NetMode() == World.Get_NetMode() &&
            Actual.Get_NetRole() == ECk_Net_EntityNetRole::Authority &&
            Entity.Has<ck::FTag_HasAuthority>() &&
            !UCk_Utils_EntityReplicationDriver_UE::Has(Entity) &&
            !Entity.Has<TWeakObjectPtr<UCk_Ecs_ReplicatedObject_UE>>() &&
            !Entity.Has<ck::FRequest_EntityScript_Replicate>();
    }

    // TryAdmit creates the request carrier before stamping Target net params.
    // It is a pure request child, not a second DNR script entity. The handle
    // must still be allocated (including BeginDestroy on the second proof).
    bool HasOnlyRequestCarrierCapabilities(const FCk_Handle& Carrier,
        const FCk_Handle& Target)
    {
        if (!ck::IsValid(Carrier, ck::IsValid_Policy_IncludePendingKill{}))
        { return false; }
        return Carrier.Has<ck::FFragment_ContextOwner>() &&
            Carrier.Get<ck::FFragment_ContextOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == Target &&
            !Carrier.Has<ck::FFragment_PendingHydration>() &&
            !Carrier.Has<ck::FTag_Hydration_Quarantine>() &&
            !Carrier.Has<ck::FFragment_Net_Params>() &&
            !Carrier.Has<ck::FTag_HasAuthority>() &&
            !Carrier.Has<ck::FTag_NetMode_IsHost>() &&
            !Carrier.Has<ck::FTag_NetMode_IsClient>() &&
            !Carrier.Has<ck::FFragment_OwningActor>() &&
            !Carrier.Has<ck::FFragment_EntityScript>() &&
            !UCk_Utils_EntityReplicationDriver_UE::Has(Carrier) &&
            !Carrier.Has<TWeakObjectPtr<UCk_Ecs_ReplicatedObject_UE>>() &&
            !Carrier.Has<ck::FRequest_EntityScript_Replicate>();
    }

    bool ClosedSource(AActor* Actor)
    {
        if (!IsInGameThread() || !IsValid(Actor) ||
            Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject |
                RF_BeginDestroyed | RF_FinishDestroyed)) { return false; }
        const auto Path = Actor->GetClass()->GetPathName();
        if (Path != TEXT("/Script/Angelscript.Ck_SmTest_DivergenceFirstBranch_GymActor") &&
            Path != TEXT("/Script/Angelscript.Ck_SmTest_DivergenceTimed_GymActor"))
        { return false; }
        auto* Function = Actor->FindFunction(TEXT("Get_SourceClosed"));
        const auto* Return = IsValid(Function) ?
            FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!Return || Function->NumParms != 1 ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm)) { return false; }
        const int32 Offset = Return->GetOffset_ForUFunction();
        const int32 Size = Return->GetSize();
        if (Offset < 0 || Size <= 0 || Offset > Function->ParmsSize ||
            Size > Function->ParmsSize - Offset) { return false; }
        FStructOnScope Args(Function);
        if (!Args.GetStructMemory()) { return false; }
        Actor->ProcessEvent(Function, Args.GetStructMemory());
        return IsValid(Actor) &&
            Return->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    bool HasExactRetainedTarget(AActor* Actor, const FCk_Handle& Target)
    {
        if (!IsValid(Actor)) { return false; }
        auto* Function = Actor->FindFunction(TEXT("Get_SourcePrimaryTarget"));
        const auto* Return = IsValid(Function) ?
            FindFProperty<FStructProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!Return || Return->Struct != FCk_Handle::StaticStruct() ||
            Function->NumParms != 1 ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm)) { return false; }
        const int32 Offset = Return->GetOffset_ForUFunction();
        const int32 Size = Return->GetSize();
        if (Offset < 0 || Size != sizeof(FCk_Handle) ||
            Offset > Function->ParmsSize || Size > Function->ParmsSize - Offset)
        { return false; }
        FStructOnScope Args(Function);
        if (!Args.GetStructMemory()) { return false; }
        Actor->ProcessEvent(Function, Args.GetStructMemory());
        const auto* Receipt = Return->ContainerPtrToValuePtr<FCk_Handle>(
            Args.GetStructMemory());
        return IsValid(Actor) && Receipt && *Receipt == Target;
    }
}

auto
    UCk_StateMachineGym_Runtime_UE::
    TryCancel_DivergencePendingPrimary(
        const FCk_Handle_PendingEntityScript& InPending, AActor* InActor)
    -> bool
{
    auto Pending = InPending;
    const auto Target = Pending.Get_EntityUnderConstruction();
    if (!ck_divergence_pending_primary::ClosedSource(InActor) ||
        !ck_divergence_pending_primary::HasExactRetainedTarget(InActor, Target) ||
        !InActor->HasAuthority()) { return false; }
    auto* const World = InActor->GetWorld();
    if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown ||
        World->IsBeingCleanedUp() || World->GetNetMode() == NM_Client)
    { return false; }
    const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    FCk_Handle Context;
    if (!TryGet_SourceContext(Target, Context) || Context != Target ||
        !ck::IsValid(WorldRoot) ||
        Target.Get_RegistryView().Get_RegistryHandle() != WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        !Target.Has<ck::FFragment_LifetimeOwner>() ||
        Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != WorldRoot ||
        Target.Has<ck::FFragment_EntityScript>() ||
        Target.Has<ck::FFragment_OwningActor>() ||
        !ck_divergence_pending_primary::HasExactDnrNoDriver(Target, WorldRoot))
    { return false; }

    TArray<FCk_Handle> Children;
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
            Target, WorldRoot, Children) || Children.Num() != 1)
    { return false; }
    const auto Carrier = Children[0];
    if (!ck::IsValid(Carrier) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Carrier,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        Carrier.Get_RegistryView().Get_RegistryHandle() != WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        !Carrier.Has<ck::FFragment_LifetimeOwner>() ||
        Carrier.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Target ||
        !ck_divergence_pending_primary::HasOnlyRequestCarrierCapabilities(Carrier, Target) ||
        !Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
    { return false; }
    const auto& Request = Carrier.Get<ck::FFragment_EntityScript_RequestSpawnEntity>();
    auto* const Archetype = Request.Get_EntityScriptClassArchetype().Get();
    const auto* Params = Request.Get_SpawnParams().GetScriptStruct() ==
        FCk_EntityScript_WithActor_SpawnParams::StaticStruct() ?
        Request.Get_SpawnParams().GetPtr<FCk_EntityScript_WithActor_SpawnParams>() : nullptr;
    if (!Request.Get_DestroyTargetOnFailure() ||
        Request.Get_NewEntity() != Target || Request.Get_Owner() != WorldRoot ||
        Request.Get_ContextOwner() != WorldRoot || !IsValid(Archetype) ||
        !Get_IsDivergenceSourceClassNonReplicating(Archetype->GetClass(), true) ||
        Archetype != Archetype->GetClass()->GetDefaultObject(false) ||
        !Params || Params->Get_OwningActor().Get() != InActor)
    { return false; }
    // Keep the proof independent of the filtered reverse list used to find the
    // carrier: the complete owner census and raw recursive read-set must agree.
    const TArray<FCk_Handle> Frozen{Target, Carrier};
    auto Prove = [&]()
    {
        if (!IsValid(InActor) || InActor->GetWorld() != World ||
            !IsValid(World) || World->bIsTearingDown || World->IsBeingCleanedUp() ||
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) != WorldRoot ||
            !ck::IsValid(WorldRoot)) { return false; }
        for (int32 Index = 0; Index < Frozen.Num(); ++Index)
        {
            const auto& Node = Frozen[Index];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Node)) { continue; }
            if (!Node.Has<ck::FFragment_LifetimeOwner>() ||
                Node.Get<ck::FFragment_LifetimeOwner, ck::IsValid_Policy_IncludePendingKill>().Get_Entity() !=
                    (Index == 0 ? WorldRoot : Target)) { return false; }
        }
        return UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
            Frozen, Frozen, WorldRoot);
    };
    if (!Prove()) { return false; }
    auto RetiringCarrier = Carrier;
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(RetiringCarrier);
    // Carrier BeginDestroy can synchronously run external callbacks. Never
    // cancel a target that ceased to be this actor's unconstructed request.
    if (!Prove() ||
        !ck_divergence_pending_primary::ClosedSource(InActor) ||
        !ck_divergence_pending_primary::HasExactRetainedTarget(InActor, Target) ||
        !InActor->HasAuthority() ||
        !ck::IsValid(Target) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Target,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        !TryGet_SourceContext(Target, Context) || Context != Target ||
        !Target.Has<ck::FFragment_LifetimeOwner>() ||
        Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != WorldRoot ||
        Target.Has<ck::FFragment_EntityScript>() ||
        Target.Has<ck::FFragment_OwningActor>() ||
        !ck_divergence_pending_primary::HasExactDnrNoDriver(Target, WorldRoot) ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Carrier) ||
        !ck::IsValid(Carrier, ck::IsValid_Policy_IncludePendingKill{}) ||
        !Carrier.Has<ck::FFragment_LifetimeOwner>() ||
        Carrier.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != Target ||
        !ck_divergence_pending_primary::HasOnlyRequestCarrierCapabilities(Carrier, Target) ||
        !Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
    { return false; }
    const auto& After = Carrier.Get<ck::FFragment_EntityScript_RequestSpawnEntity,
        ck::IsValid_Policy_IncludePendingKill>();
    auto* const AfterArchetype = After.Get_EntityScriptClassArchetype().Get();
    const auto* AfterParams = After.Get_SpawnParams().GetScriptStruct() ==
        FCk_EntityScript_WithActor_SpawnParams::StaticStruct() ?
        After.Get_SpawnParams().GetPtr<FCk_EntityScript_WithActor_SpawnParams>() : nullptr;
    if (!After.Get_DestroyTargetOnFailure() ||
        After.Get_NewEntity() != Target || After.Get_Owner() != WorldRoot ||
        After.Get_ContextOwner() != WorldRoot || !IsValid(AfterArchetype) ||
        !Get_IsDivergenceSourceClassNonReplicating(AfterArchetype->GetClass(), true) ||
        AfterArchetype != AfterArchetype->GetClass()->GetDefaultObject(false) ||
        !AfterParams || AfterParams->Get_OwningActor().Get() != InActor)
    { return false; }
    UCk_Utils_PendingEntityScript_UE::Request_DestroyEntity(Pending);
    return Prove();
}
