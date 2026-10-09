#include "CkEntityScriptGym_Runtime.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"

auto
    UCk_EntityScriptGym_Spawn_Runtime_UE::
    TryCapture_StationSpawnCarrier(
        const FCk_Handle& InTarget, const FCk_Handle& InOwner,
        TSubclassOf<UCk_EntityScript_UE> InExpectedClass,
        const FCk_Handle& InWorld, FCk_Handle& OutCarrier)
    -> bool
{
    return TryCapture_StationSpawnCarrierWithParent(
        InTarget, InOwner, InExpectedClass, InWorld, InWorld, OutCarrier);
}

auto
    UCk_EntityScriptGym_Spawn_Runtime_UE::
    TryCapture_StationSpawnCarrierWithParent(
        const FCk_Handle& InTarget, const FCk_Handle& InOwner,
        TSubclassOf<UCk_EntityScript_UE> InExpectedClass,
        const FCk_Handle& InWorld, const FCk_Handle& InOwnerParent,
        FCk_Handle& OutCarrier)
    -> bool
{
    const auto Target = InTarget;
    const auto Owner = InOwner;
    const auto World = InWorld;
    const auto OwnerParent = InOwnerParent;
    OutCarrier = {};
    if (!ck::IsValid(Target) || !ck::IsValid(Owner) || !ck::IsValid(World) ||
        !ck::IsValid(OwnerParent) ||
        !IsValid(InExpectedClass.Get()) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Target,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Owner,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(World,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(OwnerParent,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        Target.Get_RegistryView().Get_RegistryHandle() != World.Get_RegistryView().Get_RegistryHandle() ||
        Owner.Get_RegistryView().Get_RegistryHandle() != World.Get_RegistryView().Get_RegistryHandle() ||
        OwnerParent.Get_RegistryView().Get_RegistryHandle() != World.Get_RegistryView().Get_RegistryHandle() ||
        World != FCk_Handle{World.Get_RegistryView().Get_TransientEntity(),
            World.Get_RegistryView().Get_RegistryHandle()})
    { return false; }
    if (OwnerParent != World &&
        (!OwnerParent.Has<ck::FFragment_LifetimeOwner>() ||
         OwnerParent.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != World))
    { return false; }
    if (!Target.Has<ck::FFragment_LifetimeOwner>() ||
        Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Owner ||
        !Owner.Has<ck::FFragment_LifetimeOwner>() ||
        Owner.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != OwnerParent)
    { return false; }
    TArray<FCk_Handle> Children;
    if (!TryGet_AllocatedDirectLifetimeDependents(Target, World, Children) ||
        Children.Num() != 1) { return false; }
    const auto Carrier = Children[0];
    if (!ck::IsValid(Carrier) ||
        !Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
    { return false; }
    // Require the raw dependent set; it is stronger than the destroy read-set,
    // which filters deallocated entries but not transferred current owners.
    // A filtered current-owner census alone is insufficient here.
    if (!Target.Has<ck::FFragment_LifetimeDependents>()) { return false; }
    const auto& RawChildren = Target.Get<ck::FFragment_LifetimeDependents>().Get_Entities();
    if (RawChildren.Num() != 1 || RawChildren[0] != Carrier ||
        (Carrier.Has<ck::FFragment_LifetimeDependents>() &&
         !Carrier.Get<ck::FFragment_LifetimeDependents>().Get_Entities().IsEmpty()))
    { return false; }
    const auto& Request = Carrier.Get<ck::FFragment_EntityScript_RequestSpawnEntity>();
    const auto* Archetype = Request.Get_EntityScriptClassArchetype().Get();
    if (Request.Get_NewEntity() != Target || Request.Get_Owner() != Owner ||
        Request.Get_ContextOwner() != Owner || !IsValid(Archetype) ||
        Archetype->GetClass() != InExpectedClass.Get()) { return false; }
    // Peek only: GetDefaultObject(false) must not construct a wrong-class CDO
    // while this pure validation query is rejecting malformed input.
    const auto* ExpectedCDO = Cast<UCk_EntityScript_UE>(
        InExpectedClass.Get()->GetDefaultObject(false));
    if (!IsValid(ExpectedCDO) || Archetype != ExpectedCDO) { return false; }
    TArray<FCk_Handle> Grandchildren;
    if (!TryGet_AllocatedDirectLifetimeDependents(Carrier, World, Grandchildren) ||
        !Grandchildren.IsEmpty()) { return false; }
    OutCarrier = Carrier;
    return true;
}

auto
    UCk_EntityScriptGym_Spawn_Runtime_UE::
    TryCheck_CapturedStationCarrier(
        const FCk_Handle& InCarrier, const FCk_Handle& InTarget,
        const FCk_Handle& InOwner, const FCk_Handle& InWorld,
        bool& OutAllocated)
    -> bool
{
    return TryCheck_CapturedStationCarrierWithParent(
        InCarrier, InTarget, InOwner, InWorld, InWorld, OutAllocated);
}

auto
    UCk_EntityScriptGym_Spawn_Runtime_UE::
    TryCheck_CapturedStationCarrierWithParent(
        const FCk_Handle& InCarrier, const FCk_Handle& InTarget,
        const FCk_Handle& InOwner, const FCk_Handle& InWorld,
        const FCk_Handle& InOwnerParent, bool& OutAllocated)
    -> bool
{
    const auto Carrier = InCarrier;
    const auto Target = InTarget;
    const auto Owner = InOwner;
    const auto World = InWorld;
    const auto OwnerParent = InOwnerParent;
    OutAllocated = false;
    if (!ck::IsValid(Target) || !ck::IsValid(Owner) || !ck::IsValid(World) ||
        !ck::IsValid(OwnerParent) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Target,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Owner,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(World,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(OwnerParent,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
        Carrier == FCk_Handle{} ||
        Carrier.Get_RegistryView().Get_RegistryHandle() != Target.Get_RegistryView().Get_RegistryHandle() ||
        Target.Get_RegistryView().Get_RegistryHandle() != World.Get_RegistryView().Get_RegistryHandle() ||
        Owner.Get_RegistryView().Get_RegistryHandle() != World.Get_RegistryView().Get_RegistryHandle() ||
        OwnerParent.Get_RegistryView().Get_RegistryHandle() != World.Get_RegistryView().Get_RegistryHandle() ||
        World != FCk_Handle{World.Get_RegistryView().Get_TransientEntity(),
            World.Get_RegistryView().Get_RegistryHandle()})
    { return false; }
    if (OwnerParent != World &&
        (!OwnerParent.Has<ck::FFragment_LifetimeOwner>() ||
         OwnerParent.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != World))
    { return false; }
    if (!Target.Has<ck::FFragment_LifetimeOwner>() ||
        Target.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Owner ||
        !Owner.Has<ck::FFragment_LifetimeOwner>() ||
        Owner.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != OwnerParent)
    { return false; }
    if (!Get_IsEntityAllocated(Carrier)) { return true; }
    if (!Carrier.Has<ck::FFragment_LifetimeOwner>() ||
        Carrier.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != Target)
    { return false; }
    TArray<FCk_Handle> Children;
    TArray<FCk_Handle> Grandchildren;
    if (!TryGet_AllocatedDirectLifetimeDependents(Target, World, Children) ||
        !Children.Contains(Carrier) ||
        !TryGet_AllocatedDirectLifetimeDependents(Carrier, World, Grandchildren) ||
        !Grandchildren.IsEmpty() ||
        (Carrier.Has<ck::FFragment_LifetimeDependents>() &&
         !Carrier.Get<ck::FFragment_LifetimeDependents,
             ck::IsValid_Policy_IncludePendingKill>().Get_Entities().IsEmpty()))
    { return false; }
    OutAllocated = true;
    return true;
}

auto
    UCk_EntityScriptGym_Spawn_Runtime_UE::
    Get_IsEntityAllocated(
        const FCk_Handle& InHandle)
    -> bool
{
    return ck::IsValid(InHandle, ck::IsValid_Policy_IncludePendingKill{});
}

namespace ck_entity_script_gym_runtime
{
    // Called only after IncludePendingKill allocation validation. Bare allocations are not leaves.
    auto HasLifetimeRepresentation(const FCk_Handle& InHandle) -> bool
    {
        const auto Registry = InHandle.Get_RegistryView();
        const auto Transient = FCk_Handle{Registry.Get_TransientEntity(), Registry.Get_RegistryHandle()};
        if (InHandle == Transient) { return true; }
        if (!InHandle.Has<ck::FFragment_LifetimeOwner>()) { return false; }
        const auto Owner = InHandle.Get<ck::FFragment_LifetimeOwner, ck::IsValid_Policy_IncludePendingKill>().Get_Entity();
        if (!ck::IsValid(Owner, ck::IsValid_Policy_IncludePendingKill{})) { return false; }
        return Owner != InHandle && Owner.Get_RegistryView().Get_RegistryHandle() == Registry.Get_RegistryHandle();
    }
}

auto
    UCk_EntityScriptGym_Spawn_Runtime_UE::
    TryGet_AllocatedDirectLifetimeDependents(
        const FCk_Handle& InRoot,
        const FCk_Handle& InContext,
        TArray<FCk_Handle>& OutHandles)
    -> bool
{
    // Copy before clearing: caller input handles may be elements of its output array.
    const auto Root = InRoot;
    const auto Context = InContext;
    OutHandles.Reset();
    if (!ck::IsValid(Root, ck::IsValid_Policy_IncludePendingKill{})) { return false; }
    if (!ck::IsValid(Context, ck::IsValid_Policy_IncludePendingKill{})) { return false; }
    const auto Registry = Root.Get_RegistryView().Get_RegistryHandle();
    if (Context.Get_RegistryView().Get_RegistryHandle() != Registry) { return false; }
    if (!ck_entity_script_gym_runtime::HasLifetimeRepresentation(Root) ||
        !ck_entity_script_gym_runtime::HasLifetimeRepresentation(Context)) { return false; }
    if (!Root.Has<ck::FFragment_LifetimeDependents>()) { return true; }
    const auto& Dependents = Root.Get<ck::FFragment_LifetimeDependents, ck::IsValid_Policy_IncludePendingKill>().Get_Entities();
    TArray<FCk_Handle> Candidate;
    TSet<FCk_Handle> Seen;
    Seen.Reserve(Dependents.Num());
    for (const auto& Child : Dependents)
    {
        if (!ck::IsValid(Child, ck::IsValid_Policy_IncludePendingKill{})) { continue; }
        if (Child == Root || Child.Get_RegistryView().Get_RegistryHandle() != Registry) { continue; }
        if (!Child.Has<ck::FFragment_LifetimeOwner>()) { continue; }
        const auto Owner = Child.Get<ck::FFragment_LifetimeOwner, ck::IsValid_Policy_IncludePendingKill>().Get_Entity();
        if (Owner != Root) { continue; }
        if (!Seen.Contains(Child))
        {
            Seen.Add(Child);
            Candidate.Add(Child);
        }
    }
    OutHandles = MoveTemp(Candidate);
    return true;
}

auto
    UCk_EntityScriptGym_Spawn_Runtime_UE::
    TryProve_FrozenRetirementLedger(
        const TArray<FCk_Handle>& InKnownAllocated,
        const TArray<FCk_Handle>& InRetiring,
        const FCk_Handle& InContext)
    -> bool
{
    // Membership, not provenance, is the question here. The caller freezes both
    // arrays before destruction and retains their separate provenance ledger.
    TSet<FCk_Handle> KnownSet;
    TSet<FCk_Handle> RetiringSet;
    KnownSet.Reserve(InKnownAllocated.Num());
    RetiringSet.Reserve(InRetiring.Num());
    for (const auto& Known : InKnownAllocated) { KnownSet.Add(Known); }
    for (const auto& Old : InRetiring) { RetiringSet.Add(Old); }

    for (const auto& Known : InKnownAllocated)
    {
        if (Get_IsEntityAllocated(Known) && !RetiringSet.Contains(Known)) { return false; }
    }
    bool HasAllocatedRetiringMember = false;
    for (const auto& Old : InRetiring)
    {
        if (!KnownSet.Contains(Old)) { return false; }
        HasAllocatedRetiringMember |= Get_IsEntityAllocated(Old);
    }
    // Preserve the established empty/all-deallocated history contract: it requires
    // membership consistency but no world context that may already have retired.
    if (!HasAllocatedRetiringMember) { return true; }

    if (!Get_IsEntityAllocated(InContext) ||
        !ck_entity_script_gym_runtime::HasLifetimeRepresentation(InContext)) { return false; }
    const auto Registry = InContext.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();

    // Current lifetime-owner membership is independent of the reverse child arrays.
    // Const iteration includes all destruction phases and does not create fragment storage.
    bool CompleteOwnerCensus = true;
    Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Owner)
        {
            const auto Parent = Owner.Get_Entity();
            if (!RetiringSet.Contains(Parent) || !Get_IsEntityAllocated(Parent)) { return; }
            const auto Child = FCk_Handle{Entity, RegistryHandle};
            if (!KnownSet.Contains(Child) || !RetiringSet.Contains(Child))
            { CompleteOwnerCensus = false; }
        });
    if (!CompleteOwnerCensus) { return false; }

    for (const auto& Old : InRetiring)
    {
        if (!KnownSet.Contains(Old)) { return false; }
        if (!Get_IsEntityAllocated(Old)) { continue; }
        if (Old.Get_RegistryView().Get_RegistryHandle() != RegistryHandle) { return false; }

        // This existing query first validates both represented handles and a
        // matching registry. It includes pending-kill direct current-owner children.
        TArray<FCk_Handle> Children;
        if (!TryGet_AllocatedDirectLifetimeDependents(Old, InContext, Children)) { return false; }
        for (const auto& Child : Children)
        {
            if (!KnownSet.Contains(Child) || !RetiringSet.Contains(Child)) { return false; }
        }

        // Pending destruction does not make the stored allocated read-set safe to skip.
        // Read the raw fragment only after allocation/context validation, without an
        // ordinary getter that filters phases or relies on a live-only precondition.
        if (Old.Has<ck::FFragment_LifetimeDependents>())
        {
            for (const auto& Child : Old.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities())
            {
                if (!Get_IsEntityAllocated(Child)) { continue; }
                if (Child.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
                    !KnownSet.Contains(Child) || !RetiringSet.Contains(Child)) { return false; }
            }
        }
    }
    return true;
}
