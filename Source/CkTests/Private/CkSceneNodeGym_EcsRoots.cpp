#include "CkSceneNodeGym_EcsRoots.h"

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/ContextOwner/CkContextOwner_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment_Data.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor_Data.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "StructUtils/InstancedStruct.h"

namespace CkSceneNodeEcsRootsPrivate
{
    constexpr int32 RootRoles[6] = {1, 3, 5, 10, 13, 16};
    constexpr int32 NodeRoles[11] = {2, 4, 6, 7, 8, 9, 11, 12, 14, 15, 17};
    constexpr int32 NodeRootSlots[11] = {0, 1, 2, 2, 2, 2, 3, 3, 4, 4, 5};
    constexpr int32 NodeParentSlots[11] = {
        INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE,
        INDEX_NONE, INDEX_NONE, 6, INDEX_NONE, 8, INDEX_NONE};
    constexpr int32 VisualCount = 17;

    bool IsLiveObject(const UObject* Value)
    {
        return ck::IsValid(Value) &&
            !Value->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool IsLiveActorRoot(const AActor* Actor, const UWorld* World)
    {
        if (!IsLiveObject(Actor) || Actor->IsActorBeingDestroyed() ||
            Actor->GetWorld() != World || !Actor->GetIsReplicated() ||
            Actor->IsReplicatingMovement())
        { return false; }
        const auto* Root = Actor->GetRootComponent();
        return IsLiveObject(Root) && Root->GetOwner() == Actor &&
            Root->IsRegistered() && Root->GetWorld() == World;
    }

    bool IsFreshChild(const FCk_Handle& Child, const FCk_Handle& Parent)
    {
        return Child != FCk_Handle{} && ck::IsValid(Child) &&
            Child.Get_RegistryView().Get_RegistryHandle() ==
                Parent.Get_RegistryView().Get_RegistryHandle() &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Parent &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Child,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    bool IsFreshClearedTarget(const FCk_Handle& Target)
    {
        return !Target.Has_Any<ck::FFragment_Net_Params,
            ck::FTag_HasAuthority, ck::FTag_NetMode_IsHost,
            ck::FTag_NetMode_IsClient>();
    }

    bool IsAdmittedReplicatedTarget(const FCk_Handle& Target)
    {
        if (!Target.Has<ck::FFragment_Net_Params>())
        { return false; }
        const auto& Settings = Target.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings();
        const bool WantAuthority =
            Settings.Get_NetRole() == ECk_Net_EntityNetRole::Authority;
        const bool WantHost =
            Settings.Get_NetMode() == ECk_Net_NetModeType::Host ||
            Settings.Get_NetMode() == ECk_Net_NetModeType::ClientAndHost;
        const bool WantClient =
            Settings.Get_NetMode() == ECk_Net_NetModeType::Client ||
            Settings.Get_NetMode() == ECk_Net_NetModeType::ClientAndHost;
        return Settings.Get_Replication() == ECk_Replication::Replicates &&
            Target.Has<ck::FTag_HasAuthority>() == WantAuthority &&
            Target.Has<ck::FTag_NetMode_IsHost>() == WantHost &&
            Target.Has<ck::FTag_NetMode_IsClient>() == WantClient;
    }

    bool HasExactRetiredCarrier(const FCk_Handle& Target,
        const FCk_Handle& Carrier)
    {
        if (Target == FCk_Handle{} || Carrier == FCk_Handle{} ||
            !ck::IsValid(Target) ||
            Target.Get_RegistryView().Get_RegistryHandle() !=
                Carrier.Get_RegistryView().Get_RegistryHandle() ||
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Carrier))
        { return false; }
        if (!Target.Has<ck::FFragment_LifetimeDependents>())
        { return false; }
        int32 Count = 0;
        for (const auto& Dependent : Target.Get<
            ck::FFragment_LifetimeDependents>().Get_Entities())
        { Count += Dependent == Carrier ? 1 : 0; }
        return Count == 1;
    }
}

void UCk_SceneNodeGym_EcsRootReceiver_UE::OnCompleted(
    FCk_Handle Target, ECk_Request_OperationResult Result)
{
    if (!IsInGameThread() || !CkSceneNodeEcsRootsPrivate::IsLiveObject(this)) { return; }
    auto* const R = Receipt.Get();
    if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(R)) { return; }
    if (Slot < 0 || Slot >= R->Slots.Num() ||
        R->Receivers.Num() != CkSceneNodeEcsRootsPrivate::VisualCount ||
        R->Receivers[Slot].Get() != this ||
        !UCk_SceneNodeGym_EcsRootsRuntime_UE::HasExactReceipt(R))
    { UCk_SceneNodeGym_EcsRootsRuntime_UE::Fail(R, false); return; }
    auto& Entry = R->Slots[Slot];
    if (!Entry.bIssued || Entry.bCallback || Target != Entry.Target ||
        Result != ECk_Request_OperationResult::Succeeded)
    { UCk_SceneNodeGym_EcsRootsRuntime_UE::Fail(R, false); return; }
    Entry.bCallback = true;
    Entry.bSucceeded = true;
}

bool UCk_SceneNodeGym_EcsRootsRuntime_UE::HasContext(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger)
{
    if (!IsInGameThread() || !CkSceneNodeEcsRootsPrivate::IsLiveObject(Ledger) ||
        Ledger->Source.IsStale() || Ledger->SourceWorld.IsStale())
    { return false; }
    auto* const Source = Ledger->Source.Get();
    auto* const World = Ledger->SourceWorld.Get();
    if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(Source) || !CkSceneNodeEcsRootsPrivate::IsLiveObject(World) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasCurrentSource(Ledger) ||
        Ledger->ActiveRole != -1 || Ledger->Generation <= 0 ||
        World->bIsTearingDown || !World->IsGameWorld() ||
        World->GetNetMode() == NM_Client ||
        !CkSceneNodeEcsRootsPrivate::IsLiveObject(World->GetAuthGameMode()))
    { return false; }
    auto* const Subsystem = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    return CkSceneNodeEcsRootsPrivate::IsLiveObject(Subsystem) &&
        Subsystem->Get_LoadHold() == ECk_EcsWorld_LoadHold::None;
}

bool UCk_SceneNodeGym_EcsRootsRuntime_UE::HasSafeWorldRoot(
    const UCk_SceneNodeGym_ActorLedger_UE* Ledger,
    const FCk_Handle& WorldRoot)
{
    if (!HasContext(Ledger) || WorldRoot == FCk_Handle{}) { return false; }
    const auto Registry = WorldRoot.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    if (ck::registry_table::Get_IsInParallelRegion(RegistryHandle) ||
        !ck::IsValid(WorldRoot) ||
        WorldRoot != FCk_Handle{Registry.Get_TransientEntity(), RegistryHandle} ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(WorldRoot,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy))
    { return false; }
    return UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(
        *Ledger->SourceWorld.Get()) == WorldRoot;
}

bool UCk_SceneNodeGym_EcsRootsRuntime_UE::HasExactReceipt(
    const UCk_SceneNodeGym_EcsRoots_UE* R)
{
    if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(R) || R->Ledger.IsStale() ||
        !HasSafeWorldRoot(R->Ledger.Get(), R->WorldRoot)) { return false; }
    const auto* Ledger = R->Ledger.Get();
    return Ledger->EcsRoots == R && Ledger->bEcsRootsAttempted &&
        R->Generation == Ledger->Generation &&
        R->Slots.Num() == CkSceneNodeEcsRootsPrivate::VisualCount &&
        R->Receivers.Num() == CkSceneNodeEcsRootsPrivate::VisualCount &&
        CkSceneNodeEcsRootsPrivate::IsLiveObject(R->ClassCDO.Get());
}

void UCk_SceneNodeGym_EcsRootsRuntime_UE::Fail(
    UCk_SceneNodeGym_EcsRoots_UE* R, bool bOpaque)
{
    if (!IsInGameThread() || !CkSceneNodeEcsRootsPrivate::IsLiveObject(R)) { return; }
    R->bFailed = true;
    R->bUnproven |= bOpaque;
    auto* Ledger = R->Ledger.Get();
    if (CkSceneNodeEcsRootsPrivate::IsLiveObject(Ledger)) { Ledger->Close(TEXT("EcsRootsFailure")); }
}

UCk_SceneNodeGym_EcsRoots_UE* UCk_SceneNodeGym_EcsRootsRuntime_UE::TryBegin(
    UCk_SceneNodeGym_ActorLedger_UE* Ledger,
    TSubclassOf<UCk_SceneNodeGym_WithActorScript_UE> ScriptClass)
{
    if (!HasContext(Ledger) || Ledger->bEcsRootsAttempted ||
        Ledger->EcsRoots != nullptr ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(Ledger, true) ||
        !CkSceneNodeEcsRootsPrivate::IsLiveObject(ScriptClass.Get()) ||
        ScriptClass.Get()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    { return nullptr; }
    auto* const CDO = Cast<UCk_SceneNodeGym_WithActorScript_UE>(
        ScriptClass.Get()->GetDefaultObject(false));
    if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(CDO) || CDO->GetClass() != ScriptClass.Get() ||
        CDO->Get_EffectiveReplication() != ECk_Replication::Replicates)
    { return nullptr; }
    UWorld* const World = Ledger->SourceWorld.Get();
    AActor* Actors[CkSceneNodeEcsRootsPrivate::VisualCount]{};
    for (int32 I = 0; I < CkSceneNodeEcsRootsPrivate::VisualCount; ++I)
    {
        const int32 ActorRole = I < 6
            ? CkSceneNodeEcsRootsPrivate::RootRoles[I]
            : CkSceneNodeEcsRootsPrivate::NodeRoles[I - 6];
        const auto& Role = Ledger->Roles[ActorRole];
        if (!Role.bCaptured || !Role.bFinished || Role.bDestroyedObserved ||
            Role.bRemovedObserved) { return nullptr; }
        Actors[I] = Role.Actor.Get();
        if (!CkSceneNodeEcsRootsPrivate::IsLiveActorRoot(Actors[I], World) ||
            Actors[I]->GetOwner() != Ledger->Roles[0].Actor.Get() ||
            Actors[I]->GetClass() != Ledger->VisualClasses[ActorRole - 1].Get() ||
            Actors[I]->GetComponentByClass<UCk_EntityOwningActor_ActorComponent_UE>() != nullptr)
        { return nullptr; }
    }
    const FCk_Handle WorldRoot =
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!HasSafeWorldRoot(Ledger, WorldRoot) ||
        !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(WorldRoot))
    { return nullptr; }

    // Reserve once even if UObject allocation fails. No second creation may
    // adopt an unknown allocation from this generation.
    Ledger->bEcsRootsAttempted = true;
    auto* const R = NewObject<UCk_SceneNodeGym_EcsRoots_UE>(Ledger);
    if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(R)) { Ledger->Close(TEXT("EcsRootsOpaqueReceipt")); return nullptr; }
    Ledger->EcsRoots = R; // strong retention before the first ECS allocation
    R->Ledger = Ledger;
    R->ClassCDO = CDO;
    R->WorldRoot = WorldRoot;
    R->Generation = Ledger->Generation;
    R->Slots.SetNum(CkSceneNodeEcsRootsPrivate::VisualCount);
    R->Receivers.Reserve(CkSceneNodeEcsRootsPrivate::VisualCount);
    TArray<FCk_Delegate_Request_OnCompleted> Delegates;
    Delegates.Reserve(CkSceneNodeEcsRootsPrivate::VisualCount);
    for (int32 I = 0; I < CkSceneNodeEcsRootsPrivate::VisualCount; ++I)
    {
        auto* Receiver = NewObject<UCk_SceneNodeGym_EcsRootReceiver_UE>(R);
        if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(Receiver)) { Fail(R, false); return R; }
        Receiver->Receipt = R;
        Receiver->Slot = I;
        R->Receivers.Add(Receiver);
        FCk_Delegate_Request_OnCompleted D;
        D.BindDynamic(Receiver, &UCk_SceneNodeGym_EcsRootReceiver_UE::OnCompleted);
        if (!D.IsBound()) { Fail(R, false); return R; }
        Delegates.Add(D);
    }

    if (!HasExactReceipt(R) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(Ledger, true))
    { Fail(R, false); return R; }
    for (int32 I = 0; I < 6; ++I)
    {
        if (Ledger->Roles[CkSceneNodeEcsRootsPrivate::RootRoles[I]].Actor.Get() != Actors[I] ||
            !CkSceneNodeEcsRootsPrivate::IsLiveActorRoot(Actors[I], World) ||
            Actors[I]->GetComponentByClass<
                UCk_EntityOwningActor_ActorComponent_UE>() != nullptr)
        { Fail(R, false); return R; }
    }

    R->GenerationOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldRoot);
    if (!CkSceneNodeEcsRootsPrivate::IsFreshChild(R->GenerationOwner, WorldRoot))
    { Fail(R, true); return R; }
    for (int32 I = 0; I < CkSceneNodeEcsRootsPrivate::VisualCount; ++I)
    {
        const int32 ActorRole = I < 6
            ? CkSceneNodeEcsRootsPrivate::RootRoles[I]
            : CkSceneNodeEcsRootsPrivate::NodeRoles[I - 6];
        const int32 NodeIndex = I - 6;
        const FCk_Handle Parent = I < 6 ? R->GenerationOwner :
            CkSceneNodeEcsRootsPrivate::NodeParentSlots[NodeIndex] == INDEX_NONE
                ? R->Slots[CkSceneNodeEcsRootsPrivate::NodeRootSlots[NodeIndex]].Target
                : R->Slots[6 + CkSceneNodeEcsRootsPrivate::NodeParentSlots[NodeIndex]].Target;
        if (!HasExactReceipt(R) || R->bFailed ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(Ledger, true) ||
            Ledger->Roles[ActorRole].Actor.Get() != Actors[I] ||
            !CkSceneNodeEcsRootsPrivate::IsLiveActorRoot(Actors[I], World) ||
            Actors[I]->GetComponentByClass<
                UCk_EntityOwningActor_ActorComponent_UE>() != nullptr ||
            !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Parent))
        { Fail(R, true); return R; }
        auto& Slot = R->Slots[I];
        Slot.Target = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Parent);
        if (!CkSceneNodeEcsRootsPrivate::IsFreshChild(Slot.Target, Parent) ||
            !UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Slot.Target).IsEmpty())
        { Fail(R, true); return R; }
        // Request_CreateEntity inherits owner net fragments. This source-only
        // path removes the same four copied fields as generic
        // Request_SpawnEntity before calling native captured Add.
        if (Slot.Target.Has<ck::FFragment_Net_Params>())
        { Slot.Target.Remove<ck::FFragment_Net_Params>(); }
        if (Slot.Target.Has<ck::FTag_HasAuthority>())
        { Slot.Target.Remove<ck::FTag_HasAuthority>(); }
        if (Slot.Target.Has<ck::FTag_NetMode_IsHost>())
        { Slot.Target.Remove<ck::FTag_NetMode_IsHost>(); }
        if (Slot.Target.Has<ck::FTag_NetMode_IsClient>())
        { Slot.Target.Remove<ck::FTag_NetMode_IsClient>(); }
        if (!CkSceneNodeEcsRootsPrivate::IsFreshClearedTarget(Slot.Target) || !HasExactReceipt(R) ||
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(Ledger, true) ||
            Ledger->Roles[ActorRole].Actor.Get() != Actors[I] ||
            !CkSceneNodeEcsRootsPrivate::IsLiveActorRoot(Actors[I], World) ||
            Actors[I]->GetComponentByClass<
                UCk_EntityOwningActor_ActorComponent_UE>() != nullptr)
        { Fail(R, true); return R; }
        UCk_Utils_ContextOwner_UE::Request_OverrideToSelf(Slot.Target, {});
        if (!CkSceneNodeEcsRootsPrivate::IsFreshChild(Slot.Target, Parent) ||
            !UCk_Utils_ContextOwner_UE::Has(Slot.Target) ||
            UCk_Utils_ContextOwner_UE::Get_ContextOwner(Slot.Target) != Slot.Target)
        { Fail(R, true); return R; }
        const auto Params = FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Actors[I]);
        if (!Params.IsValid()) { Fail(R, false); return R; }
        Slot.bIssued = true; // a completion can be synchronous
        auto Capture = UCk_Utils_EntityScript_UE::TryAddCaptured(
            Slot.Target, ScriptClass, Params, nullptr, Delegates[I]);
        Slot.Pending = Capture.Pending;
        Slot.Carrier = Capture.RequestCarrier;
        Slot.bAllocationAttempted = Capture.bAllocationAttempted;
        if (!HasExactReceipt(R) || R->bFailed)
        { Fail(R, true); return R; }
        Slot.bReturned = ck::IsValid(Slot.Pending) &&
            Slot.Pending.Get_EntityUnderConstruction() == Slot.Target &&
            Slot.Carrier != FCk_Handle{} &&
            Slot.Carrier.Get_RegistryView().Get_RegistryHandle() ==
                WorldRoot.Get_RegistryView().Get_RegistryHandle();
        if (!Slot.bAllocationAttempted || !Slot.bReturned || R->bFailed ||
            !CkSceneNodeEcsRootsPrivate::IsAdmittedReplicatedTarget(Slot.Target))
        { Fail(R, Slot.bAllocationAttempted && !Slot.bReturned); return R; }
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Slot.Carrier))
        {
            if (!Slot.Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>() ||
                !Slot.Carrier.Has<ck::FFragment_LifetimeOwner>() ||
                Slot.Carrier.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Slot.Target)
            { Fail(R, true); return R; }
            const auto& Request = Slot.Carrier.Get<
                ck::FFragment_EntityScript_RequestSpawnEntity>();
            if (Request.Get_NewEntity() != Slot.Target ||
                Request.Get_Owner() != Parent ||
                Request.Get_ContextOwner() != Parent ||
                Request.Get_EntityScriptClassArchetype().Get() != CDO ||
                Request.Get_DestroyTargetOnFailure())
            { Fail(R, true); return R; }
        }
        else if (!Slot.bCallback) { Fail(R, true); return R; }
    }
    return R;
}

bool UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(
    const UCk_SceneNodeGym_EcsRoots_UE* R)
{
    if (!HasExactReceipt(R) || R->bFailed || R->bUnproven ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(R->Ledger.Get(), true) ||
        !CkSceneNodeEcsRootsPrivate::IsFreshChild(R->GenerationOwner, R->WorldRoot))
    { return false; }
    auto* const World = R->Ledger.Get()->SourceWorld.Get();
    auto* const CDO = R->ClassCDO.Get();
    if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(CDO) ||
        CDO->Get_EffectiveReplication() != ECk_Replication::Replicates)
    { return false; }
    for (int32 I = 0; I < CkSceneNodeEcsRootsPrivate::VisualCount; ++I)
    {
        const auto& Slot = R->Slots[I];
        const int32 ActorRole = I < 6
            ? CkSceneNodeEcsRootsPrivate::RootRoles[I]
            : CkSceneNodeEcsRootsPrivate::NodeRoles[I - 6];
        const int32 NodeIndex = I - 6;
        const FCk_Handle Parent = I < 6 ? R->GenerationOwner :
            CkSceneNodeEcsRootsPrivate::NodeParentSlots[NodeIndex] == INDEX_NONE
                ? R->Slots[CkSceneNodeEcsRootsPrivate::NodeRootSlots[NodeIndex]].Target
                : R->Slots[6 + CkSceneNodeEcsRootsPrivate::NodeParentSlots[NodeIndex]].Target;
        auto* const Actor = R->Ledger.Get()->Roles[ActorRole].Actor.Get();
        auto* const Receiver = R->Receivers[I].Get();
        if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(Receiver) || Receiver->Receipt.Get() != R ||
            Receiver->Slot != I || !Slot.bIssued || !Slot.bReturned ||
            !Slot.bCallback || !Slot.bSucceeded || !Slot.bAllocationAttempted ||
            !CkSceneNodeEcsRootsPrivate::IsLiveActorRoot(Actor, World) ||
            !CkSceneNodeEcsRootsPrivate::IsFreshChild(Slot.Target, Parent) ||
            !CkSceneNodeEcsRootsPrivate::IsAdmittedReplicatedTarget(Slot.Target) ||
            !UCk_Utils_ContextOwner_UE::Has(Slot.Target) ||
            UCk_Utils_ContextOwner_UE::Get_ContextOwner(Slot.Target) != Slot.Target ||
            !UCk_Utils_EntityReplicationDriver_UE::Has(Slot.Target) ||
            !Slot.Target.Has<ck::FFragment_OwningActor>() ||
            !ck::IsValid(Slot.Pending) ||
            Slot.Pending.Get_EntityUnderConstruction() != Slot.Target ||
            Slot.Carrier == FCk_Handle{} ||
            Slot.Carrier.Get_RegistryView().Get_RegistryHandle() !=
                R->WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            !CkSceneNodeEcsRootsPrivate::HasExactRetiredCarrier(
                Slot.Target, Slot.Carrier) ||
            !Slot.Target.Has<ck::FFragment_EntityScript>() ||
            !Slot.Target.Has<ck::FFragment_Transform>() ||
            !Slot.Target.Has<ck::FFragment_Transform_RootComponent>())
        { return false; }
        auto* const Script = Cast<UCk_SceneNodeGym_WithActorScript_UE>(
            Slot.Target.Get<ck::FFragment_EntityScript>().Get_Script().Get());
        auto* const Link = Actor->GetComponentByClass<
            UCk_EntityOwningActor_ActorComponent_UE>();
        if (!CkSceneNodeEcsRootsPrivate::IsLiveObject(Script) || Script->GetClass() != CDO->GetClass() ||
            Script->Get_AssociatedEntity() != Slot.Target ||
            Script->Get_OwningActor().Get() != Actor ||
            Script->Get_EffectiveReplication() != ECk_Replication::Replicates ||
            !CkSceneNodeEcsRootsPrivate::IsLiveObject(Link) || !Link->IsRegistered() ||
            Link->GetOwner() != Actor || Link->Get_EntityHandle() != Slot.Target ||
            Slot.Target.Get<ck::FFragment_OwningActor>()
                .Get_EntityOwningActor().Get() != Actor ||
            Slot.Target.Get<ck::FFragment_Transform_RootComponent>()
                .Get_RootComponent().Get() != Actor->GetRootComponent())
        { return false; }
    }
    return true;
}

bool UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsFailed(
    const UCk_SceneNodeGym_EcsRoots_UE* R)
{ return IsInGameThread() && CkSceneNodeEcsRootsPrivate::IsLiveObject(R) && R->bFailed; }

bool UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsUnproven(
    const UCk_SceneNodeGym_EcsRoots_UE* R)
{ return IsInGameThread() && CkSceneNodeEcsRootsPrivate::IsLiveObject(R) && R->bUnproven; }

int32 UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_SucceededCount(
    const UCk_SceneNodeGym_EcsRoots_UE* R)
{
    if (!IsInGameThread() || !CkSceneNodeEcsRootsPrivate::IsLiveObject(R)) { return 0; }
    int32 Count = 0;
    for (const auto& Slot : R->Slots)
    { if (Slot.bIssued && Slot.bReturned && Slot.bCallback && Slot.bSucceeded) { ++Count; } }
    return Count;
}

bool UCk_SceneNodeGym_EcsRootsRuntime_UE::TryGet_CompletedRoot(
    const UCk_SceneNodeGym_EcsRoots_UE* R, int32 Slot, FCk_Handle& OutRoot)
{
    OutRoot = {};
    if (!Get_IsConstructionComplete(R) || Slot < 0 || Slot >= 6) { return false; }
    OutRoot = R->Slots[Slot].Target;
    return true;
}
