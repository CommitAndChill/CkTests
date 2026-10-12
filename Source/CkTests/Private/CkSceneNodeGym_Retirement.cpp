#include "CkSceneNodeGym_Retirement.h"

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_EcsRoots.h"
#include "CkSceneNodeGym_FrameSampler.h"
#include "CkSceneNodeGym_Motion.h"
#include "CkSceneNodeGym_NodeGraph.h"
#include "CkSceneNodeGym_QueueRuntime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityScriptGym_Runtime.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_scene_node_gym_retirement
{
    constexpr int32 KnownCount = 41;
    constexpr int32 ActorCount = 18;
    constexpr int32 LiveCount = 24;
    constexpr FCk_Time MaxWait = FCk_Time{20.0};
    constexpr int32 RootRoles[6] = {1, 3, 5, 10, 13, 16};

    bool Live(const UObject* Object)
    {
        return ck::IsValid(Object) &&
            !Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool SameActor(const TWeakObjectPtr<AActor>& Frozen, const AActor* Current)
    {
        return Current != nullptr && Frozen.HasSameIndexAndSerialNumber(
            TWeakObjectPtr<AActor>{const_cast<AActor*>(Current)});
    }

    bool IsRootRole(int32 Role)
    {
        for (const int32 Root : RootRoles)
        { if (Role == Root) { return true; } }
        return false;
    }

    template<typename TVisitor>
    bool ForEachLoadedActor(UWorld* World, TVisitor&& Visitor)
    {
        if (!Live(World)) { return false; }
        TSet<ULevel*> Seen;
        auto VisitLevel = [&Seen, &Visitor](ULevel* Level)
        {
            if (!Live(Level)) { return false; }
            if (Seen.Contains(Level)) { return true; }
            Seen.Add(Level);
            for (const TObjectPtr<AActor>& Entry : Level->Actors)
            {
                if (AActor* const Actor = Entry.Get()) { Visitor(Actor); }
            }
            return true;
        };
        if (!VisitLevel(World->PersistentLevel)) { return false; }
        for (ULevel* const Level : World->GetLevels())
        { if (!VisitLevel(Level)) { return false; } }
        return true;
    }
}

void UCk_SceneNodeGym_Retirement_UE::Unbind()
{
    if (PostTickHandle.IsValid())
    {
        FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
        PostTickHandle.Reset();
    }
    if (WorldCleanupHandle.IsValid())
    {
        FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
        WorldCleanupHandle.Reset();
    }
    if (auto* const CurrentSource = Source.Get())
    {
        if (ck_scene_node_gym_retirement::Live(CurrentSource))
        { CurrentSource->OnEndPlay.RemoveDynamic(this,
            &UCk_SceneNodeGym_Retirement_UE::OnSourceEndPlay); }
    }
}

void UCk_SceneNodeGym_Retirement_UE::BeginDestroy()
{
    Unbind();
    Super::BeginDestroy();
}

void UCk_SceneNodeGym_Retirement_UE::OnSourceEndPlay(
    AActor* Actor, EEndPlayReason::Type Reason)
{
    if (!IsInGameThread() || Actor != Source.GetEvenIfUnreachable()) { return; }
    if (State == ECk_SceneNodeGym_RetirementState::Pending)
    { UCk_SceneNodeGym_RetirementRuntime_UE::Fail(this, TEXT("SourceEndPlay")); }
}

void UCk_SceneNodeGym_Retirement_UE::OnWorldCleanup(
    UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
    if (!IsInGameThread() || InWorld != World.GetEvenIfUnreachable()) { return; }
    if (State == ECk_SceneNodeGym_RetirementState::Pending)
    { UCk_SceneNodeGym_RetirementRuntime_UE::Fail(this, TEXT("WorldCleanup")); }
}

void UCk_SceneNodeGym_Retirement_UE::OnWorldPostActorTick(
    UWorld* InWorld, ELevelTick TickType, float DeltaSeconds)
{
    if (State != ECk_SceneNodeGym_RetirementState::Pending ||
        InWorld != World.Get())
    { return; }
    const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
    if (!FMath::IsFinite(ElapsedSeconds) || ElapsedSeconds < 0.0 ||
        FCk_Time{ElapsedSeconds} > ck_scene_node_gym_retirement::MaxWait)
    { UCk_SceneNodeGym_RetirementRuntime_UE::Fail(this, TEXT("RetirementTimeout")); return; }
    Elapsed = FCk_Time{ElapsedSeconds};
    // A non-All tick still consumes wall-clock budget but cannot advance
    // destruction or completion without the post-ECS full-frame boundary.
    if (TickType != LEVELTICK_All) { return; }
    UCk_SceneNodeGym_RetirementRuntime_UE::Observe(this, DeltaSeconds);
}

void UCk_SceneNodeGym_RetirementRuntime_UE::Fail(
    UCk_SceneNodeGym_Retirement_UE* R, FName Reason)
{
    if (!IsInGameThread() || !ck_scene_node_gym_retirement::Live(R) ||
        R->State != ECk_SceneNodeGym_RetirementState::Pending)
    { return; }
    R->State = ECk_SceneNodeGym_RetirementState::Unproven;
    R->FailureReason = Reason;
    R->Unbind();
    // Deliberately retain every creator identity, actor serial, and source claim.
}

bool UCk_SceneNodeGym_RetirementRuntime_UE::FreezeCompleted(
    UCk_SceneNodeGym_Retirement_UE* R)
{
    auto* const M = R->Motion.Get();
    if (!ck_scene_node_gym_retirement::Live(M) || !UCk_SceneNodeGym_MotionRuntime_UE::Get_IsActive(M) ||
        M->Slots.Num() != 6 || M->Nodes.Num() != 11 || M->Generation <= 0)
    { return false; }
    auto* const Graph = M->Graph.Get();
    auto* const Roots = ck_scene_node_gym_retirement::Live(Graph) ? Graph->Roots.Get() : nullptr;
    auto* const Ledger = M->Ledger.Get();
    auto* const Sampler = M->FrameSampler.Get();
    // Active Motion already proves current graph topology without requiring dormant queues.
    // All 23 known queues must drain after the six real Stop receipts, before owner destruction.
    if (!ck_scene_node_gym_retirement::Live(Graph) || !ck_scene_node_gym_retirement::Live(Roots) || !ck_scene_node_gym_retirement::Live(Ledger) || !ck_scene_node_gym_retirement::Live(Sampler) ||
        Graph->Slots.Num() != 11 || Roots->Slots.Num() != 17 ||
        Ledger->bOpaqueAllocation || Ledger->bUnknownOwnedActor ||
        Ledger->bWorldTeardown ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryProve_CurrentCohort(
            Ledger, true) ||
        !UCk_SceneNodeGym_EcsRootsRuntime_UE::Get_IsConstructionComplete(Roots))
    { return false; }
    FCkSceneNodeGym_Frame Frame;
    if (!UCk_SceneNodeGym_FrameSamplerRuntime_UE::TryCopyLatestFrame(
            Sampler, Frame) || Frame.Generation != M->Generation ||
        Frame.WorldPoses.Num() != 17 || Frame.FrameSerial <= 0 ||
        Sampler->bClosed || Sampler->bInSample || Sampler->Motion.Get() != M ||
        Sampler->Ledger.Get() != Ledger || Ledger->Generation != M->Generation ||
        Roots->Generation != M->Generation || Graph->Generation != M->Generation)
    { return false; }
    auto* const Source = Ledger->Source.Get();
    auto* const World = Ledger->SourceWorld.Get();
    if (!ck_scene_node_gym_retirement::Live(Source) || !ck_scene_node_gym_retirement::Live(World) || Source->GetWorld() != World ||
        Roots->WorldRoot != M->WorldRoot ||
        !ck::IsValid(Roots->WorldRoot) ||
        Roots->GenerationOwner == FCk_Handle{})
    { return false; }

    R->Source = Source;
    R->World = World;
    R->Ledger = Ledger;
    R->Sampler = Sampler;
    R->WorldRoot = Roots->WorldRoot;
    R->Generation = M->Generation;
    R->Known.Reserve(ck_scene_node_gym_retirement::KnownCount);
    R->ExpectedParents.Reserve(ck_scene_node_gym_retirement::KnownCount);
    auto Add = [R](const FCk_Handle& Value, const FCk_Handle& Parent)
    {
        R->Known.Add(Value);
        R->ExpectedParents.Add(Parent);
    };
    Add(Roots->GenerationOwner, R->WorldRoot);
    for (int32 I = 0; I < Roots->Slots.Num(); ++I)
    {
        const FCk_Handle Parent = I < 6 ? Roots->GenerationOwner
            : Graph->Slots[I - 6].Parent;
        Add(Roots->Slots[I].Target, Parent);
    }
    for (const auto& Slot : Roots->Slots)
    { Add(Slot.Carrier, Slot.Target); }
    for (const auto& Slot : M->Slots)
    { Add(Slot.Raw, Slot.Root); }
    if (R->Known.Num() != ck_scene_node_gym_retirement::KnownCount || R->ExpectedParents.Num() != ck_scene_node_gym_retirement::KnownCount)
    { return false; }
    const auto Registry = R->WorldRoot.Get_RegistryView().Get_RegistryHandle();
    if (ck::registry_table::Get_IsInParallelRegion(Registry)) { return false; }
    for (int32 I = 0; I < ck_scene_node_gym_retirement::KnownCount; ++I)
    {
        const FCk_Handle& Value = R->Known[I];
        const FCk_Handle& Parent = R->ExpectedParents[I];
        if (Value == FCk_Handle{} || Parent == FCk_Handle{} || Value == Parent ||
            Value.Get_RegistryView().Get_RegistryHandle() != Registry ||
            Parent.Get_RegistryView().Get_RegistryHandle() != Registry)
        { return false; }
        for (int32 J = 0; J < I; ++J)
        { if (R->Known[J] == Value) { return false; } }
    }
    for (int32 I = 0; I < 11; ++I)
    {
        const auto& Slot = Graph->Slots[I];
        if (Slot.RootSlot < 0 || Slot.RootSlot >= 6 ||
            (Slot.ParentNodeSlot != INDEX_NONE &&
             (Slot.ParentNodeSlot < 0 || Slot.ParentNodeSlot >= I)))
        { return false; }
        const FCk_Handle Expected = Slot.ParentNodeSlot == INDEX_NONE
            ? Roots->Slots[Slot.RootSlot].Target
            : Graph->Slots[Slot.ParentNodeSlot].Raw;
        if (Slot.Parent != Expected) { return false; }
    }
    for (int32 I = 0; I < 6; ++I)
    {
        if (M->Slots[I].Root != Roots->Slots[I].Target ||
            FCk_Handle(M->Slots[I].Timer) != M->Slots[I].Raw)
        { return false; }
    }
    R->Actors.Reserve(ck_scene_node_gym_retirement::ActorCount);
    R->DestroyRequested.Init(false, ck_scene_node_gym_retirement::ActorCount);
    for (int32 Role = 0; Role < ck_scene_node_gym_retirement::ActorCount; ++Role)
    {
        const auto& Slot = Ledger->Roles[Role];
        if (!Slot.bCaptured || !Slot.bFinished || Slot.bFinishing ||
            Slot.bDestroyedObserved || Slot.bRemovedObserved)
        { return false; }
        R->Actors.Add(Slot.Actor);
    }
    return HasExactAllocatedClosure(R, true);
}

bool UCk_SceneNodeGym_RetirementRuntime_UE::HasClosedContext(
    const UCk_SceneNodeGym_Retirement_UE* R)
{
    if (!IsInGameThread() || !ck_scene_node_gym_retirement::Live(R))
    { return false; }
    auto* const M = R->Motion.Get();
    auto* const Ledger = R->Ledger.Get();
    auto* const Sampler = R->Sampler.Get();
    auto* const Source = R->Source.Get();
    auto* const World = R->World.Get();
    if (!ck_scene_node_gym_retirement::Live(M) ||
        !ck_scene_node_gym_retirement::Live(Ledger) ||
        !ck_scene_node_gym_retirement::Live(Sampler) ||
        !ck_scene_node_gym_retirement::Live(Source) ||
        !ck_scene_node_gym_retirement::Live(World) ||
        R->Generation <= 0 || R->Known.Num() != ck_scene_node_gym_retirement::KnownCount ||
        R->ExpectedParents.Num() != ck_scene_node_gym_retirement::KnownCount ||
        R->Actors.Num() != ck_scene_node_gym_retirement::ActorCount ||
        R->DestroyRequested.Num() != ck_scene_node_gym_retirement::ActorCount ||
        !Ledger->bClosed || !M->bClosed || M->bFailed || M->bUnproven ||
        !Sampler->bClosed || Ledger->Generation != R->Generation ||
        Source->GetWorld() != World || World->bIsTearingDown ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasLiveSourceContext(Ledger) ||
        M->Ledger.Get() != Ledger || M->FrameSampler.Get() != Sampler ||
        !M->bRetirementAttempted || M->Retirement.Get() != R ||
        M->WorldRoot != R->WorldRoot || M->Generation != R->Generation ||
        Sampler->Motion.Get() != M || Sampler->Ledger.Get() != Ledger ||
        Sampler->Source.Get() != Source || Sampler->World.Get() != World ||
        Sampler->Generation != R->Generation ||
        Ledger->Source.Get() != Source || Ledger->SourceWorld.Get() != World ||
        Ledger->Roles.Num() != ck_scene_node_gym_retirement::ActorCount)
    { return false; }
    int32 RoleMembership[ck_scene_node_gym_retirement::ActorCount] = {};
    bool bDuplicateKnownRole = false;
    if (!ck_scene_node_gym_retirement::ForEachLoadedActor(
            World, [&](AActor* Actor)
            {
                for (int32 Role = 0; Role < ck_scene_node_gym_retirement::ActorCount; ++Role)
                {
                    if (!ck_scene_node_gym_retirement::SameActor(R->Actors[Role], Actor))
                    { continue; }
                    if (++RoleMembership[Role] > 1) { bDuplicateKnownRole = true; }
                }
            }) || bDuplicateKnownRole)
    { return false; }
    const auto Registry = R->WorldRoot.Get_RegistryView().Get_RegistryHandle();
    if (ck::registry_table::Get_IsInParallelRegion(Registry) ||
        !ck::IsValid(R->WorldRoot) ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(
            *World) != R->WorldRoot)
    { return false; }
    return true;
}

bool UCk_SceneNodeGym_RetirementRuntime_UE::HasExactAllocatedClosure(
    const UCk_SceneNodeGym_Retirement_UE* R, bool bBeforeDestroy)
{
    if (R->Known.Num() != ck_scene_node_gym_retirement::KnownCount || R->ExpectedParents.Num() != ck_scene_node_gym_retirement::KnownCount ||
        !ck::IsValid(R->WorldRoot))
    { return false; }
    const auto Registry = R->WorldRoot.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    if (ck::registry_table::Get_IsInParallelRegion(RegistryHandle)) { return false; }
    TSet<FCk_Handle> Frozen;
    TArray<FCk_Handle> Retiring;
    for (int32 I = 0; I < ck_scene_node_gym_retirement::KnownCount; ++I)
    {
        const auto H = R->Known[I];
        if (H == FCk_Handle{} || Frozen.Contains(H) ||
            H.Get_RegistryView().Get_RegistryHandle() != RegistryHandle)
        { return false; }
        Frozen.Add(H);
    }
    for (int32 I = 0; I < ck_scene_node_gym_retirement::KnownCount; ++I)
    {
        const auto H = R->Known[I];
        const bool Allocated =
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H);
        if (I >= 18 && I < 35)
        {
            if (Allocated) { return false; } // seventeen completed request carriers
            continue;
        }
        if (bBeforeDestroy && !Allocated) { return false; }
        if (!Allocated) { continue; }
        if (bBeforeDestroy && UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        { return false; }
        Retiring.Add(H);
        if (!H.Has<ck::FFragment_LifetimeOwner>() ||
            H.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() !=
                R->ExpectedParents[I])
        { return false; }
        if (H.Has<ck::FFragment_LifetimeDependents>())
        {
            for (const auto& Child : H.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities())
            {
                if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) &&
                    !Frozen.Contains(Child))
                { return false; }
            }
        }
    }
    bool UnknownChild = false;
    Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Owner)
        {
            if (!Frozen.Contains(Owner.Get_Entity())) { return; }
            const FCk_Handle Child{Entity, RegistryHandle};
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) &&
                !Frozen.Contains(Child))
            { UnknownChild = true; }
        });
    if (UnknownChild) { return false; }
    // This independent census still runs when all recorded nodes have gone;
    // the shared helper's empty/all-gone shortcut alone is not sufficient.
    if (bBeforeDestroy)
    {
        if (Retiring.Num() != ck_scene_node_gym_retirement::LiveCount ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                R->Known, Retiring, R->WorldRoot))
        { return false; }
    }
    return true;
}

bool UCk_SceneNodeGym_RetirementRuntime_UE::HasRetiredPhysicalProof(
    const UCk_SceneNodeGym_Retirement_UE* R)
{
    if (!IsInGameThread() || !ck_scene_node_gym_retirement::Live(R) ||
        !ck_scene_node_gym_retirement::Live(R->World.Get()) ||
        R->World.Get()->bIsTearingDown ||
        R->Known.Num() != ck_scene_node_gym_retirement::KnownCount ||
        R->Actors.Num() != ck_scene_node_gym_retirement::ActorCount ||
        !ck::IsValid(R->WorldRoot) ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(
            *R->World.Get()) != R->WorldRoot ||
        !HasExactAllocatedClosure(R, false))
    { return false; }
    for (const auto& H : R->Known)
    {
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H))
        { return false; }
    }
    int32 Present = 0;
    const bool LevelCensusComplete = ck_scene_node_gym_retirement::ForEachLoadedActor(
        R->World.Get(), [&](AActor* Actor)
        {
            for (const auto& Frozen : R->Actors)
            {
                if (ck_scene_node_gym_retirement::SameActor(Frozen, Actor))
                { ++Present; }
            }
        });
    return LevelCensusComplete && Present == 0 && HasNoUnknownScopedActors(R);
}

bool UCk_SceneNodeGym_RetirementRuntime_UE::HasKnownActorInWorld(
    const UCk_SceneNodeGym_Retirement_UE* R, int32 Role, AActor*& OutActor)
{
    OutActor = nullptr;
    if (Role < 0 || Role >= ck_scene_node_gym_retirement::ActorCount || R->Actors.Num() != ck_scene_node_gym_retirement::ActorCount ||
        !ck_scene_node_gym_retirement::Live(R->World.Get()))
    { return false; }
    AActor* Candidate = nullptr;
    int32 Matches = 0;
    if (!ck_scene_node_gym_retirement::ForEachLoadedActor(
            R->World.Get(), [&](AActor* Actor)
            {
                if (ck_scene_node_gym_retirement::SameActor(R->Actors[Role], Actor))
                { Candidate = Actor; ++Matches; }
            }) || Matches < 1)
    { return false; }
    OutActor = Candidate;
    return true;
}

bool UCk_SceneNodeGym_RetirementRuntime_UE::HasNoUnknownScopedActors(
    const UCk_SceneNodeGym_Retirement_UE* R)
{
    AActor* Scope = nullptr;
    const bool ScopePresent = HasKnownActorInWorld(R, 0, Scope);
    if (!ScopePresent &&
        (R->DestroyRequested.Num() != ck_scene_node_gym_retirement::ActorCount ||
         !R->DestroyRequested[0]))
    { return false; }
    bool Safe = true;
    const bool LevelsValid = ck_scene_node_gym_retirement::ForEachLoadedActor(
        R->World.Get(), [&](AActor* Candidate)
        {
            if (!Safe || ck_scene_node_gym_retirement::SameActor(R->Actors[0], Candidate))
            { return; }
            bool Known = false;
            for (int32 Role = 1; Role < ck_scene_node_gym_retirement::ActorCount; ++Role)
            { Known |= ck_scene_node_gym_retirement::SameActor(R->Actors[Role], Candidate); }
            if (Known) { return; }
            TSet<const AActor*> Visited;
            const AActor* Current = Candidate;
            while (Current != nullptr)
            {
                if (ck_scene_node_gym_retirement::SameActor(R->Actors[0], Current))
                { Safe = false; return; }
                if (!ck_scene_node_gym_retirement::Live(Current) ||
                    Current->IsActorBeingDestroyed() || Visited.Contains(Current))
                { Safe = false; return; }
                Visited.Add(Current);
                const AActor* Parent = Current->GetOwner();
                Current = Parent;
            }
        });
    return LevelsValid && Safe;
}

bool UCk_SceneNodeGym_RetirementRuntime_UE::HasSafeSweepContext(
    const UCk_SceneNodeGym_Retirement_UE* R)
{
    if (!HasClosedContext(R) ||
        R->State != ECk_SceneNodeGym_RetirementState::Pending ||
        !R->bDestroyIssued || !R->bQuiescenceObserved ||
        R->Ledger.Get()->bOpaqueAllocation ||
        R->Ledger.Get()->bUnknownOwnedActor ||
        R->Ledger.Get()->bWorldTeardown ||
        !HasExactAllocatedClosure(R, false))
    { return false; }
    for (const auto& H : R->Known)
    {
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H))
        { return false; }
    }
    if (!HasNoUnknownScopedActors(R)) { return false; }
    // WithActor teardown owns all seventeen visual destructions. The scope may only
    // retire after a complete level census proves every captured visual serial absent.
    int32 PresentVisuals = 0;
    const bool LevelCensusComplete = ck_scene_node_gym_retirement::ForEachLoadedActor(
        R->World.Get(), [&](TWeakObjectPtr<AActor> Actor)
        {
            for (int32 Role = 1; Role < ck_scene_node_gym_retirement::ActorCount; ++Role)
            {
                if (R->Actors[Role].HasSameIndexAndSerialNumber(Actor)) { ++PresentVisuals; }
            }
        });
    if (!LevelCensusComplete || PresentVisuals != 0) { return false; }
    AActor* Scope = nullptr;
    if (!HasKnownActorInWorld(R, 0, Scope))
    { return R->DestroyRequested.Num() == ck_scene_node_gym_retirement::ActorCount &&
        R->DestroyRequested[0]; }
    if (!R->DestroyRequested[0] &&
        (!ck_scene_node_gym_retirement::Live(Scope) ||
         Scope->IsActorBeingDestroyed() || Scope->GetWorld() != R->World.Get() ||
         Scope->GetClass() != ACk_SceneNodeGym_ScopeActor_UE::StaticClass() ||
         Scope->GetOwner() != R->Source.Get() || !Scope->GetIsReplicated() ||
         Scope->IsReplicatingMovement()))
    { return false; }
    return true;
}

void UCk_SceneNodeGym_RetirementRuntime_UE::Observe(
    UCk_SceneNodeGym_Retirement_UE* R, float DeltaSeconds)
{
    if (!IsInGameThread() || !ck_scene_node_gym_retirement::Live(R) ||
        R->State != ECk_SceneNodeGym_RetirementState::Pending)
    { return; }
    if (R->bInObserve)
    { Fail(R, TEXT("ReentrantRetirementObserve")); return; }
    TGuardValue<bool> ObserveGuard(R->bInObserve, true);
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
    { Fail(R, TEXT("MalformedWorldTick")); return; }
    if (!HasClosedContext(R) ||
        R->Ledger.Get()->bOpaqueAllocation ||
        R->Ledger.Get()->bUnknownOwnedActor ||
        R->Ledger.Get()->bWorldTeardown)
    { Fail(R, TEXT("ClosedContextLost")); return; }
    if (!R->bDestroyIssued)
    {
        if (!UCk_SceneNodeGym_MotionRuntime_UE::Get_IsStopped(R->Motion.Get()))
        { return; }
        if (R->Ledger.Get()->bExternalLoss)
        { Fail(R, TEXT("KnownActorLostBeforeDestroy")); return; }
        for (int32 Role = 0; Role < ck_scene_node_gym_retirement::ActorCount; ++Role)
        {
            AActor* Current = nullptr;
            AActor* Listed = nullptr;
            if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryGet_FinishedRole(
                    R->Ledger.Get(), Role, Current) ||
                !HasKnownActorInWorld(R, Role, Listed) || Current != Listed ||
                !ck_scene_node_gym_retirement::SameActor(R->Actors[Role], Current))
            { Fail(R, TEXT("ActorCohortChangedBeforeDestroy"));
              return; }
        }
        TArray<FCk_Handle> Live23;
        Live23.Reserve(23);
        for (int32 I = 1; I <= 17; ++I) { Live23.Add(R->Known[I]); }
        for (int32 I = 35; I < ck_scene_node_gym_retirement::KnownCount; ++I)
        { Live23.Add(R->Known[I]); }
        if (!UCk_SceneNodeGym_QueueRuntime_UE::Get_AreKnownQueuesDrained(
                Live23, R->WorldRoot) ||
            !HasExactAllocatedClosure(R, true) ||
            !HasNoUnknownScopedActors(R))
        { Fail(R, TEXT("PreDestroyClosureFailed")); return; }
        FCk_Handle Owner = R->Known[0];
        R->bQuiescenceObserved = true;
        R->bDestroyIssued = true; // reentrant callback cannot issue it twice
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner);
        if (R->State != ECk_SceneNodeGym_RetirementState::Pending)
        { return; }
        if (!HasClosedContext(R) || R->Ledger.Get()->bOpaqueAllocation ||
            R->Ledger.Get()->bUnknownOwnedActor ||
            R->Ledger.Get()->bWorldTeardown ||
            !HasExactAllocatedClosure(R, false) ||
            !HasNoUnknownScopedActors(R))
        { Fail(R, TEXT("PostOwnerDestroyChanged")); return; }
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                R->Known[0]) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                R->Known[0], ECk_EntityLifetime_DestructionPhase::BeginDestroy))
        { Fail(R, TEXT("OwnerDestroyNotInitiated")); }

        return;
    }
    if (!HasExactAllocatedClosure(R, false))
    { Fail(R, TEXT("RetiringClosureFailed")); return; }
    bool AnyAllocated = false;
    for (const auto& H : R->Known)
    { AnyAllocated |= UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
    if (AnyAllocated) { return; }
    if (!HasNoUnknownScopedActors(R))
    { Fail(R, TEXT("UnknownScopedActor")); return; }

    // All seventeen visual actors are tied to production WithActor entities.
    // Their owning-actor teardown processor destroys them after ECS retirement.
    // Wait for that actual cascade before destroying the identity-only scope.
    for (int32 Role = 1; Role < ck_scene_node_gym_retirement::ActorCount; ++Role)
    {
        AActor* Found = nullptr;
        if (HasKnownActorInWorld(R, Role, Found))
        { return; }
    }
    AActor* Scope = nullptr;
    if (HasKnownActorInWorld(R, 0, Scope))
    {
        if (!R->DestroyRequested[0])
        {
            if (!HasSafeSweepContext(R))
            { Fail(R, TEXT("PreScopeDestroyChanged")); return; }
            if (!ck_scene_node_gym_retirement::Live(Scope) || Scope->IsActorBeingDestroyed() ||
                Scope->GetWorld() != R->World.Get() ||
                Scope->GetOwner() != R->Source.Get() ||
                Scope->GetClass() != ACk_SceneNodeGym_ScopeActor_UE::StaticClass() ||
                !Scope->GetIsReplicated() || Scope->IsReplicatingMovement())
            { Fail(R, TEXT("ScopeIdentityChanged")); return; }
            R->DestroyRequested[0] = true;
            if (!Scope->Destroy())
            { Fail(R, TEXT("ScopeDestroyRejected")); return; }
            if (!HasSafeSweepContext(R))
            { Fail(R, TEXT("PostScopeDestroyChanged")); return; }
        }

        return;
    }
    if (!R->DestroyRequested[0])
    { Fail(R, TEXT("UnrequestedScopeLoss")); return; }
    if (!HasSafeSweepContext(R))
    { Fail(R, TEXT("FinalCohortChanged")); return; }
    R->State = ECk_SceneNodeGym_RetirementState::Complete;
    R->Unbind();

}

UCk_SceneNodeGym_Retirement_UE*
UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginCompleted(
    UCk_SceneNodeGym_Motion_UE* Motion)
{
    if (!IsInGameThread() || !ck_scene_node_gym_retirement::Live(Motion) || Motion->bRetirementAttempted ||
        Motion->Retirement != nullptr)
    { return nullptr; }
    Motion->bRetirementAttempted = true;
    auto* const R = NewObject<UCk_SceneNodeGym_Retirement_UE>(Motion);
    if (!ck_scene_node_gym_retirement::Live(R))
    {
        if (ck_scene_node_gym_retirement::Live(Motion->FrameSampler.Get()))
        { UCk_SceneNodeGym_FrameSamplerRuntime_UE::Request_Close(
            Motion->FrameSampler.Get()); }
        else { UCk_SceneNodeGym_MotionRuntime_UE::TryClose(Motion); }
        return nullptr;
    }
    Motion->Retirement = R;
    R->Motion = Motion;
    if (!FreezeCompleted(R))
    {
        Fail(R, TEXT("CompletedFreezeRejected"));
        if (ck_scene_node_gym_retirement::Live(Motion->FrameSampler.Get()))
        { UCk_SceneNodeGym_FrameSamplerRuntime_UE::Request_Close(
            Motion->FrameSampler.Get()); }
        else { UCk_SceneNodeGym_MotionRuntime_UE::TryClose(Motion); }
        return R;
    }
    auto* const World = R->World.Get();
    auto* const Source = R->Source.Get();
    R->PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(
        R, &UCk_SceneNodeGym_Retirement_UE::OnWorldPostActorTick);
    R->StartSeconds = FPlatformTime::Seconds();
    R->WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
        R, &UCk_SceneNodeGym_Retirement_UE::OnWorldCleanup);
    Source->OnEndPlay.AddUniqueDynamic(R,
        &UCk_SceneNodeGym_Retirement_UE::OnSourceEndPlay);
    if (!R->PostTickHandle.IsValid() || !R->WorldCleanupHandle.IsValid())
    { Fail(R, TEXT("RetirementObserverBindFailed")); }
    UCk_SceneNodeGym_FrameSamplerRuntime_UE::Request_Close(
        Motion->FrameSampler.Get());
    if (R->State == ECk_SceneNodeGym_RetirementState::Pending &&
        (!HasClosedContext(R) || !ck_scene_node_gym_retirement::Live(World)))
    { Fail(R, TEXT("RetirementCloseFailed")); }
    return R;
}

UCk_SceneNodeGym_ActorLedger_UE*
UCk_SceneNodeGym_RetirementRuntime_UE::TryBeginSuccessor(
    AGameStateBase* ExpectedSource,
    UCk_SceneNodeGym_Retirement_UE* R,
    const TArray<TSubclassOf<AActor>>& VisualClasses)
{
    if (!IsInGameThread() || !ck_scene_node_gym_retirement::Live(R) ||
        !ck_scene_node_gym_retirement::Live(ExpectedSource) ||
        !R->Source.HasSameIndexAndSerialNumber(
            TWeakObjectPtr<AGameStateBase>{ExpectedSource}) ||
        R->Source.Get() != ExpectedSource ||
        R->bSuccessorAttempted ||
        R->State != ECk_SceneNodeGym_RetirementState::Complete ||
        !R->FailureReason.IsNone() || !R->bQuiescenceObserved ||
        !R->bDestroyIssued || R->bInObserve || R->PostTickHandle.IsValid() ||
        R->WorldCleanupHandle.IsValid() || R->Generation <= 0 ||
        R->Generation >= MAX_int32 || !HasClosedContext(R) ||
        !HasRetiredPhysicalProof(R) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasSafeVisualClasses(VisualClasses))
    { return nullptr; }

    auto* const OldLedger = R->Ledger.Get();
    auto* const Source = R->Source.Get();
    auto* const World = R->World.Get();
    UWorld* CurrentWorld = nullptr;
    if (!ck_scene_node_gym_retirement::Live(OldLedger) ||
        !ck_scene_node_gym_retirement::Live(Source) ||
        !ck_scene_node_gym_retirement::Live(World) ||
        OldLedger->bOpaqueAllocation || OldLedger->bUnknownOwnedActor ||
        OldLedger->bWorldTeardown ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasExactCurrentClaim(OldLedger) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasSafeSourceWorld(
            Source, CurrentWorld) || CurrentWorld != World ||
        !Source->GetInstanceComponents().Contains(OldLedger))
    { return nullptr; }

    // Keep the operation inputs alive across RemoveInstanceComponent and
    // DestroyComponent, either of which may synchronously call external code.
    TStrongObjectPtr<UCk_SceneNodeGym_Retirement_UE> RetainedReceipt(R);
    TStrongObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> RetainedOldLedger(OldLedger);
    TStrongObjectPtr<AGameStateBase> RetainedSource(Source);
    TStrongObjectPtr<UWorld> RetainedWorld(World);
    TArray<TStrongObjectPtr<UClass>> RetainedClasses;
    RetainedClasses.Reserve(VisualClasses.Num());
    for (const auto& Class : VisualClasses)
    { RetainedClasses.Emplace(Class.Get()); }

    const int32 NextGeneration = R->Generation + 1;
    // This reservation has no callback. A rejection above leaves receipt and
    // claim unchanged; every failure below leaves a monotone occupied claim.
    if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryReserveSuccessorClaim(
            OldLedger, NextGeneration))
    { return nullptr; }
    R->bSuccessorAttempted = true;

    OldLedger->UnbindWorldCallbacks();
    Source->RemoveInstanceComponent(OldLedger);
    OldLedger->DestroyComponent();
    // The old component is historical after DestroyComponent. Do not read it.
    if (!UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasReservedClaim(
            Source, NextGeneration) ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasSafeSourceWorld(
            Source, CurrentWorld) || CurrentWorld != World ||
        !UCk_SceneNodeGym_ActorLedgerRuntime_UE::HasNoLedgerComponents(Source) ||
        !HasRetiredPhysicalProof(R))
    { return nullptr; }

    TArray<TSubclassOf<AActor>> FrozenClasses;
    FrozenClasses.Reserve(RetainedClasses.Num());
    for (const auto& Type : RetainedClasses)
    { FrozenClasses.Add(Type.Get()); }
    return UCk_SceneNodeGym_ActorLedgerRuntime_UE::CreateReservedLedger(
        Source, World, NextGeneration, FrozenClasses);
}

ECk_SceneNodeGym_RetirementState
UCk_SceneNodeGym_RetirementRuntime_UE::Get_State(
    const UCk_SceneNodeGym_Retirement_UE* R)
{
    return IsInGameThread() && ck_scene_node_gym_retirement::Live(R) ? R->State
        : ECk_SceneNodeGym_RetirementState::Unproven;
}

FName UCk_SceneNodeGym_RetirementRuntime_UE::Get_FailureReason(
    const UCk_SceneNodeGym_Retirement_UE* R)
{
    return IsInGameThread() && ck_scene_node_gym_retirement::Live(R) ? R->FailureReason : NAME_None;
}
