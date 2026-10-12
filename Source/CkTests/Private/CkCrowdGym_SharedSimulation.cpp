#include "CkCrowdGym_SharedSimulation.h"

#include "CkCrowdGym_RemoteSampler.h"
#include "CkCrowdGym_NetworkRuntime.h"
#include "CkCrowd/Agent/CkCrowdAgent_DebugColor_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Neighbors_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkPhysics/Acceleration/CkAcceleration_Utils.h"
#include "CkPhysics/EulerIntegrator/CkEulerIntegrator_Fragment.h"
#include "CkPhysics/EulerIntegrator/CkEulerIntegrator_Utils.h"
#include "CkPhysics/Velocity/CkVelocity_Utils.h"
#include "CkSpatialQuery/Probe/CkProbe_Utils.h"

#include <Engine/World.h>
#include <GameFramework/GameStateBase.h>

namespace ck_crowd_gym_shared_simulation
{
    constexpr double PendingDeadlineSeconds = 20.0;
    int64 NextWorldGeneration = 1;

    auto IsLive(const FCk_Handle& Handle) -> bool
    {
        return ck::IsValid(Handle) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto IsFullyRetired(const FCk_Handle& Handle) -> bool
    { return ck::Is_NOT_Valid(Handle, ck::IsValid_Policy_IncludePendingKill{}); }

    auto HasDirectOwner(const FCk_Handle& Child, const FCk_Handle& Owner) -> bool
    {
        return IsLive(Child) && IsLive(Owner) &&
            Child.Get_RegistryView().Get_RegistryHandle() ==
                Owner.Get_RegistryView().Get_RegistryHandle() &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner;
    }

    auto HasExactDirectChildren(const FCk_Handle& Owner,
                                const TSet<FCk_Handle>& Expected) -> bool
    {
        if (!IsLive(Owner)) { return false; }
        TSet<FCk_Handle> Seen;
        if (!Owner.Has<ck::FFragment_LifetimeDependents>())
        { return Expected.IsEmpty(); }
        // The public utility excludes pending destruction. Publication must also reject a child
        // still present in the registry's retirement phases, so inspect this owner's own fragment.
        for (const auto& Child : Owner.Get<ck::FFragment_LifetimeDependents>().Get_Entities())
        {
            if (IsFullyRetired(Child)) { continue; }
            if (!IsLive(Child) || !Expected.Contains(Child) ||
                !HasDirectOwner(Child, Owner) || Seen.Contains(Child))
            { return false; }
            Seen.Add(Child);
        }
        return Seen.Num() == Expected.Num();
    }
}

UCkCrowdGym_SharedSimulation::UCkCrowdGym_SharedSimulation()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

auto UCkCrowdGym_SharedSimulation::HasAuthorityWorld() const -> bool
{
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    const auto* State = Cast<AGameStateBase>(GetOwner());
    const auto* World = GetWorld();
    if (!IsValid(State) || State->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        State->IsActorBeingDestroyed() || !IsValid(World) ||
        World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        World->bIsTearingDown)
    { return false; }
    return State->GetWorld() == World && World->GetGameState() == State &&
        State->HasAuthority() && World->GetNetMode() != NM_Client &&
        (WorldGeneration == 0 || OwningWorld.Get() == World);
}

auto UCkCrowdGym_SharedSimulation::HasLiveRoot() const -> bool
{
    if (!HasAuthorityWorld() || !ck_crowd_gym_shared_simulation::IsLive(WorldRoot))
    { return false; }
    auto* World = GetWorld();
    const auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    return ck_crowd_gym_shared_simulation::HasDirectOwner(WorldRoot, Transient);
}

auto UCkCrowdGym_SharedSimulation::TryInitialize(const FTransform& StationOrigin) -> bool
{
    if (WorldGeneration != 0 || bFailed || !HasAuthorityWorld() || !StationOrigin.IsValid() ||
        ck_crowd_gym_shared_simulation::NextWorldGeneration <= 0 ||
        ck_crowd_gym_shared_simulation::NextWorldGeneration == TNumericLimits<int64>::Max())
    { return false; }

    auto* World = GetWorld();
    const auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!ck_crowd_gym_shared_simulation::IsLive(Transient) ||
        !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Transient))
    { return false; }

    WorldRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Transient);
    if (!ck_crowd_gym_shared_simulation::HasDirectOwner(WorldRoot, Transient))
    {
        FailAndRetire(TEXT("Cannot create exact Crowd world root"));
        return false;
    }
    OwningWorld = World;
    Origin = StationOrigin.GetLocation() + FVector(0.0, 0.0, 100.0);
    WorldGeneration = ck_crowd_gym_shared_simulation::NextWorldGeneration++;
    return true;
}

auto UCkCrowdGym_SharedSimulation::BuildScenario(int32 Scenario,
                                                  TArray<FSpawnSpec>& OutSpecs) const -> bool
{
    OutSpecs.Reset();
    if (Scenario < 0 || Scenario > 3 || Origin.ContainsNaN()) { return false; }
    const auto North = Origin + FVector(0.0, 600.0, 0.0);
    const auto South = Origin + FVector(0.0, -600.0, 0.0);
    const auto East = Origin + FVector(600.0, 0.0, 0.0);
    const auto West = Origin + FVector(-600.0, 0.0, 0.0);
    if (Scenario == 0 || Scenario == 2)
    {
        OutSpecs.Add(FSpawnSpec{North, South, FLinearColor(0.42, 0.85, 1.0, 0.6)});
        OutSpecs.Add(FSpawnSpec{South, North, FLinearColor(1.0, 0.42, 0.85, 0.6)});
    }
    if (Scenario == 1 || Scenario == 2)
    {
        OutSpecs.Add(FSpawnSpec{East, West, FLinearColor(0.85, 1.0, 0.42, 0.6)});
        OutSpecs.Add(FSpawnSpec{West, East, FLinearColor(1.0, 0.85, 0.42, 0.6)});
    }
    if (Scenario == 3)
    {
        constexpr int32 Count = 5;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const double Angle = (2.0 * PI * Index) / Count;
            const auto Offset = FVector(600.0 * FMath::Cos(Angle),
                                        600.0 * FMath::Sin(Angle), 0.0);
            const double Hue = (360.0 / Count) * Index;
            OutSpecs.Add(FSpawnSpec{Origin + Offset, Origin,
                FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue * 255.0 / 360.0), 200, 220)});
        }
    }
    for (const auto& Spec : OutSpecs)
    {
        if (Spec.Spawn.ContainsNaN() || Spec.Target.ContainsNaN() ||
            !FMath::IsFinite(Spec.Color.R) || !FMath::IsFinite(Spec.Color.G) ||
            !FMath::IsFinite(Spec.Color.B) || !FMath::IsFinite(Spec.Color.A) ||
            Spec.Spawn.Equals(Spec.Target))
        { OutSpecs.Reset(); return false; }
    }
    return !OutSpecs.IsEmpty();
}

auto UCkCrowdGym_SharedSimulation::HasExactAgent(const FAgent& Agent) const -> bool
{
    const FCk_Handle Raw{Agent.Handle};
    return Agent.Raw == Raw && Agent.Id.WorldGeneration == WorldGeneration &&
        Agent.Id.AgentSerial > 0 &&
        Agent.Revision > 0 && ck_crowd_gym_shared_simulation::HasDirectOwner(Raw, Agent.BatchRoot) &&
        UCk_Utils_Transform_UE::Has(Raw) && UCk_Utils_CrowdAgent_UE::Has(Raw) &&
        Raw.Has<ck::FFragment_CrowdAgent_Tunables>() &&
        Raw.Has<ck::FFragment_CrowdAgent_PathFollow>() &&
        Raw.Has<ck::FFragment_CrowdAgent_DesiredVelocity>() &&
        UCk_Utils_Velocity_UE::Has(Raw) && UCk_Utils_Acceleration_UE::Has(Raw) &&
        Raw.Has<ck::FFragment_CrowdAgent_DebugColor>() &&
        Raw.Has<ck::FFragment_EulerIntegrator>() &&
        Raw.Has<ck::FTag_EulerIntegrator_NeedsUpdate>() &&
        UCk_Utils_CrowdAgent_UE::Get_DebugColor(Agent.Handle).Equals(Agent.Color);
}

auto UCkCrowdGym_SharedSimulation::HasExactRoster() const -> bool
{
    if (!HasLiveRoot()) { return false; }
    TSet<FCk_Handle> ExpectedRoots;
    TSet<FCkCrowdRemoteAgentId> SeenIds;
    for (const auto& BatchRoot : CommittedBatchRoots)
    {
        if (!ck_crowd_gym_shared_simulation::HasDirectOwner(BatchRoot, WorldRoot) ||
            ExpectedRoots.Contains(BatchRoot))
        { return false; }
        ExpectedRoots.Add(BatchRoot);
        TSet<FCk_Handle> ExpectedAgents;
        for (const auto& Agent : Admitted)
        {
            if (Agent.BatchRoot != BatchRoot) { continue; }
            const FCk_Handle Raw{Agent.Handle};
            if (!HasExactAgent(Agent) || !Agent.bMoveCompleted ||
                ExpectedAgents.Contains(Raw) || SeenIds.Contains(Agent.Id))
            { return false; }
            ExpectedAgents.Add(Raw);
            SeenIds.Add(Agent.Id);
        }
        if (ExpectedAgents.IsEmpty() ||
            !ck_crowd_gym_shared_simulation::HasExactDirectChildren(BatchRoot, ExpectedAgents))
        { return false; }
    }
    if (Pending == EPending::Append)
    {
        if (!ck_crowd_gym_shared_simulation::HasDirectOwner(PendingBatchRoot, WorldRoot) ||
            ExpectedRoots.Contains(PendingBatchRoot))
        { return false; }
        ExpectedRoots.Add(PendingBatchRoot);
    }
    return SeenIds.Num() == Admitted.Num() &&
        ck_crowd_gym_shared_simulation::HasExactDirectChildren(WorldRoot, ExpectedRoots);
}

auto UCkCrowdGym_SharedSimulation::HasExactProbeRoster() const -> bool
{
    // Setup creates exactly one retained probe child per admitted Crowd agent.
    // Pending append is intentionally excluded: its asynchronous setup may not yet run.
    for (const auto& Agent : Admitted)
    {
        if (!ck_crowd_gym_shared_simulation::IsLive(Agent.Raw) ||
            !Agent.Raw.Has<ck::FTag_CrowdAgent_HasProbe>() ||
            !Agent.Raw.Has<ck::FFragment_CrowdAgent_ProbeRef>())
        { return false; }
        const auto Probe = Agent.Raw.Get<ck::FFragment_CrowdAgent_ProbeRef>()
            .Get_ProbeChild();
        const FCk_Handle ProbeRaw{Probe};
        if (!ck_crowd_gym_shared_simulation::HasDirectOwner(ProbeRaw, Agent.Raw) ||
            !UCk_Utils_Probe_UE::Has(ProbeRaw))
        { return false; }
        TSet<FCk_Handle> ExpectedProbe;
        ExpectedProbe.Add(ProbeRaw);
        if (!ck_crowd_gym_shared_simulation::HasExactDirectChildren(
                Agent.Raw, ExpectedProbe) ||
            !ck_crowd_gym_shared_simulation::HasExactDirectChildren(
                ProbeRaw, TSet<FCk_Handle>{}))
        { return false; }
    }
    return true;
}

auto UCkCrowdGym_SharedSimulation::TryCreateAgent(const FSpawnSpec& Spec, int64 Serial,
                                                   FAgent& OutAgent) -> bool
{
    OutAgent = {};
    OutAgent.BatchRoot = PendingBatchRoot;
    OutAgent.Id.WorldGeneration = WorldGeneration;
    OutAgent.Id.AgentSerial = Serial;
    if (!ck_crowd_gym_shared_simulation::IsLive(PendingBatchRoot) ||
        !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(PendingBatchRoot))
    { return false; }
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(PendingBatchRoot);
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    // Retain the exact first return on a surviving component before owner/feature checks.
    OutAgent.Raw = Entity;
    if (!HasAuthorityWorld())
    { return false; }
    if (bFailed || !ck_crowd_gym_shared_simulation::HasDirectOwner(Entity, PendingBatchRoot))
    { return false; }
    const auto Direction = (Spec.Target - Spec.Spawn).GetSafeNormal();
    const auto Transform = FTransform(Direction.Rotation(), Spec.Spawn, FVector::OneVector);
    auto TransformHandle = UCk_Utils_Transform_UE::Add(Entity, Transform,
                                                       ECk_Replication::DoesNotReplicate);
    if (!HasAuthorityWorld())
    { return false; }
    if (bFailed || !ck_crowd_gym_shared_simulation::HasDirectOwner(Entity, PendingBatchRoot) ||
        !ck::IsValid(TransformHandle) || !UCk_Utils_Transform_UE::Has(Entity))
    { return false; }
    auto Crowd = UCk_Utils_CrowdAgent_UE::Add(
        TransformHandle, FCk_CrowdAgent_Spec(42.0f, 192.0f));
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    OutAgent.Handle = Crowd; // Retain the typed return before the next composition step.
    if (!HasAuthorityWorld())
    { return false; }
    if (bFailed || !ck_crowd_gym_shared_simulation::HasDirectOwner(Entity, PendingBatchRoot) ||
        !ck::IsValid(Crowd) || !UCk_Utils_CrowdAgent_UE::Has(Entity))
    { return false; }
    const auto Velocity = UCk_Utils_Velocity_UE::Add(Entity,
        FCk_Velocity_Spec(ECk_LocalWorld::World, FVector::ZeroVector),
        ECk_Replication::DoesNotReplicate);
    if (!HasAuthorityWorld())
    { return false; }
    if (bFailed || !ck_crowd_gym_shared_simulation::HasDirectOwner(Entity, PendingBatchRoot) ||
        !ck::IsValid(Velocity) || !UCk_Utils_Velocity_UE::Has(Entity))
    { return false; }
    const auto Acceleration = UCk_Utils_Acceleration_UE::Add(Entity,
        FCk_Acceleration_Spec(ECk_LocalWorld::World, FVector::ZeroVector),
        ECk_Replication::DoesNotReplicate);
    if (!HasAuthorityWorld())
    { return false; }
    if (bFailed || !ck_crowd_gym_shared_simulation::HasDirectOwner(Entity, PendingBatchRoot) ||
        !ck::IsValid(Acceleration) || !UCk_Utils_Acceleration_UE::Has(Entity))
    { return false; }
    UCk_Utils_EulerIntegrator_UE::Request_Start(Entity, {});
    if (!HasAuthorityWorld())
    { return false; }
    // An Add observer may have closed the source before Request_Start returned, then
    // the remainder of Request_Start may have rearmed its tag. Stop that exact raw
    // identity again after the mutation returns.
    if (bFailed) { return false; }
    if (!ck_crowd_gym_shared_simulation::HasDirectOwner(Entity, PendingBatchRoot) ||
        !Entity.Has<ck::FFragment_EulerIntegrator>() ||
        !Entity.Has<ck::FTag_EulerIntegrator_NeedsUpdate>())
    { return false; }
    Crowd = UCk_Utils_CrowdAgent_UE::Set_DebugColor(Crowd, Spec.Color);
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    OutAgent.Handle = Crowd;
    if (!HasAuthorityWorld())
    { return false; }
    if (bFailed || !ck_crowd_gym_shared_simulation::HasDirectOwner(Entity, PendingBatchRoot) ||
        !ck::IsValid(Crowd)) { return false; }
    OutAgent.Color = Spec.Color;
    OutAgent.Revision = 1;
    return HasExactAgent(OutAgent);
}

auto UCkCrowdGym_SharedSimulation::BeginAppendScenario(int32 Scenario,
                                                        int64& OutBatchSerial) -> bool
{
    OutBatchSerial = 0;
    if (!Get_IsReady() || !HasExactRoster()) { return false; }
    TArray<FSpawnSpec> Specs;
    if (!BuildScenario(Scenario, Specs) || NextBatchSerial == TNumericLimits<int64>::Max() ||
        NextAgentSerial > TNumericLimits<int64>::Max() - Specs.Num())
    { return false; }
    if (!UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(WorldRoot)) { return false; }
    const auto NewBatchRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldRoot);
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    PendingBatchRoot = NewBatchRoot;
    if (!HasAuthorityWorld() || bFailed)
    { return false; }
    if (!ck_crowd_gym_shared_simulation::HasDirectOwner(PendingBatchRoot, WorldRoot))
    { FailAndRetire(TEXT("Cannot create complete scenario batch root")); return false; }

    Pending = EPending::Append;
    bMoveCallbackFailed = false;
    PendingBatchSerial = NextBatchSerial++;
    PendingSinceSeconds = GetWorld()->GetTimeSeconds();
    // Fixed slots precede every allocation; rejected first returns stay retained.
    PendingAgents.SetNum(Specs.Num());
    for (int32 Index = 0; Index < Specs.Num(); ++Index)
    {
        if (!TryCreateAgent(Specs[Index], NextAgentSerial++, PendingAgents[Index]))
        {
            if (!HasAuthorityWorld()) { return false; }
            if (bFailed)
            {
                if (auto* State = Cast<ACkCrowdGym_GameState_UE>(GetOwner()))
                { State->Request_CloseFailedShared(); }
            }
            else { FailAndRetire(TEXT("Cannot compose complete scenario agent")); }
            return false;
        }
    }
    // Register every pending identity before issuing any potentially synchronous callback.
    FCk_Delegate_Request_OnCompleted Completion;
    Completion.BindDynamic(this, &UCkCrowdGym_SharedSimulation::OnMoveCompleted);
    for (int32 Index = 0; Index < PendingAgents.Num(); ++Index)
    {
        auto& Agent = PendingAgents[Index];
        const FSpawnSpec& Spec = Specs[Index];
        UCk_Utils_CrowdAgent_UE::Request_MoveTo(
            Agent.Handle, FCk_Request_CrowdAgent_MoveTo(Spec.Target), Completion);
        if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        if (bFailed || bMoveCallbackFailed)
        {
            if (bFailed)
            {
                // The initiating MoveTo may append after an observer already queued Stop.
                // This bounded trailing Stop is a real production request, not a receipt.
                Agent.bStopIssued = false;
                Agent.bStopSucceeded = false;
                if (auto* State = Cast<ACkCrowdGym_GameState_UE>(GetOwner()))
                { State->Request_CloseFailedShared(); }
            }
            else { FailAndRetire(TEXT("Scenario MoveTo request rejected")); }
            return false;
        }
    }
    OutBatchSerial = PendingBatchSerial;
    return true;
}

auto UCkCrowdGym_SharedSimulation::BeginClear(int64& OutBatchSerial) -> bool
{
    OutBatchSerial = 0;
    if (!Get_IsReady() || !HasExactRoster() ||
        NextBatchSerial == TNumericLimits<int64>::Max())
    { return false; }
    Pending = EPending::Clear;
    PendingBatchSerial = NextBatchSerial++;
    PendingSinceSeconds = GetWorld()->GetTimeSeconds();
    RetiringBatchRoots = CommittedBatchRoots;
    for (const auto& BatchRoot : RetiringBatchRoots)
    {
        auto RequestHandle = BatchRoot;
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(RequestHandle);
    }
    OutBatchSerial = PendingBatchSerial;
    return true;
}

void UCkCrowdGym_SharedSimulation::OnMoveCompleted(
    FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
{
    if (Pending != EPending::Append || bFailed) { return; }
    for (auto& Agent : PendingAgents)
    {
        if (FCk_Handle{Agent.Handle} != InRequestOwner) { continue; }
        if (Agent.bMoveCompleted || InResult != ECk_Request_OperationResult::Succeeded)
        { bMoveCallbackFailed = true; return; }
        Agent.bMoveCompleted = true;
        return;
    }
    bMoveCallbackFailed = true;
}

void UCkCrowdGym_SharedSimulation::AdvancePending()
{
    if (bFailed || WorldGeneration == 0) { return; }
    if (bMoveCallbackFailed)
    { FailAndRetire(TEXT("Scenario MoveTo completion failed or duplicated")); return; }
    if (!HasLiveRoot())
    { FailAndRetire(TEXT("Crowd world root lost")); return; }
    if (Pending != EPending::None &&
        GetWorld()->GetTimeSeconds() - PendingSinceSeconds >
            ck_crowd_gym_shared_simulation::PendingDeadlineSeconds)
    { FailAndRetire(TEXT("Crowd scenario operation timed out")); return; }

    if (Pending == EPending::Append)
    {
        if (!HasExactRoster())
        { FailAndRetire(TEXT("Admitted Crowd roster changed during append")); return; }
        TSet<FCk_Handle> Expected;
        for (const auto& Agent : PendingAgents)
        {
            if (!HasExactAgent(Agent))
            { FailAndRetire(TEXT("Pending Crowd agent lost required composition")); return; }
            Expected.Add(FCk_Handle{Agent.Handle});
        }
        if (Expected.Num() != PendingAgents.Num() ||
            !ck_crowd_gym_shared_simulation::HasExactDirectChildren(PendingBatchRoot, Expected))
        { FailAndRetire(TEXT("Pending Crowd batch has incomplete membership")); return; }
        for (const auto& Agent : PendingAgents)
        { if (!Agent.bMoveCompleted) { return; } }
        CommittedBatchRoots.Add(PendingBatchRoot);
        Admitted.Append(PendingAgents);
        PendingAgents.Reset();
        PendingBatchRoot = {};
        Pending = EPending::None;
        if (!HasExactRoster())
        { FailAndRetire(TEXT("Complete Crowd roster admission failed")); return; }
        LastCompletedBatchSerial = PendingBatchSerial;
        PendingBatchSerial = 0;
    }
    else if (Pending == EPending::Clear)
    {
        for (const auto& Root : RetiringBatchRoots)
        { if (!ck_crowd_gym_shared_simulation::IsFullyRetired(Root)) { return; } }
        if (!ck_crowd_gym_shared_simulation::HasExactDirectChildren(WorldRoot, {}))
        { FailAndRetire(TEXT("Clear left an unexpected Crowd child")); return; }
        RetiringBatchRoots.Reset();
        CommittedBatchRoots.Reset();
        Admitted.Reset();
        Pending = EPending::None;
        if (!HasExactRoster())
        { FailAndRetire(TEXT("Empty Crowd roster retirement failed")); return; }
        LastCompletedBatchSerial = PendingBatchSerial;
        PendingBatchSerial = 0;
    }
    else if (!HasExactRoster())
    { FailAndRetire(TEXT("Admitted Crowd roster lost exact composition")); }
}

auto UCkCrowdGym_SharedSimulation::Get_IsReady() const -> bool
{ return WorldGeneration > 0 && !bFailed && Pending == EPending::None && HasExactRoster(); }

auto UCkCrowdGym_SharedSimulation::Get_IsBusy() const -> bool
{ return !bFailed && Pending != EPending::None; }

auto UCkCrowdGym_SharedSimulation::TrySampleRosterAtSequence(
    int64 InSequence, const FCk_Handle& RequestingPawnEntity,
    const FCkCrowdRemoteAgentId* SelectedId, int64 SelectionRequestSerial,
    TArray<FCkCrowdRemoteAgentRow>& OutRows,
    FCkCrowdRemoteAgentDetail& OutDetail) const -> bool
{
    OutRows.Reset();
    OutDetail = {};
    if (InSequence <= 0 || InSequence == TNumericLimits<int64>::Max() ||
        !Get_IsReady() || !HasExactRoster())
    { return false; }
    TArray<FCkCrowdGymServerAgentRecord> Roster;
    Roster.Reserve(Admitted.Num());
    for (const auto& Agent : Admitted)
    // These agents are owned by batch roots, not by another roster agent.
    { Roster.Add(FCkCrowdGymServerAgentRecord{Agent.Id, Agent.Handle, Agent.Revision, 0}); }
    TArray<FCkCrowdRemoteAgentRow> CandidateRows;
    FCkCrowdRemoteAgentDetail CandidateDetail;
    if (!FCkCrowdGymRemoteSampler::TrySample(*GetWorld(), WorldGeneration,
        InSequence, Roster, RequestingPawnEntity, SelectedId,
        SelectionRequestSerial, CandidateRows, CandidateDetail))
    { return false; }
    OutRows = MoveTemp(CandidateRows);
    OutDetail = MoveTemp(CandidateDetail);
    return true;
}

auto UCkCrowdGym_SharedSimulation::TryResolveAgent(
    const FCkCrowdRemoteAgentId& Id, int64 ExpectedRevision,
    FCk_Handle_CrowdAgent& OutAgent) const -> bool
{
    OutAgent = {};
    if (!Get_IsReady() || !Id.IsValid() || Id.WorldGeneration != WorldGeneration ||
        ExpectedRevision <= 0 || ExpectedRevision >= TNumericLimits<int64>::Max() - 1 ||
        !HasExactRoster())
    { return false; }
    for (const auto& Agent : Admitted)
    {
        if (Agent.Id == Id && Agent.Revision == ExpectedRevision && HasExactAgent(Agent))
        { OutAgent = Agent.Handle; return true; }
    }
    return false;
}

auto UCkCrowdGym_SharedSimulation::TryAdvanceConfigurationRevision(
    const FCkCrowdRemoteAgentId& Id, int64 ExpectedRevision,
    int64& OutNewRevision) -> bool
{
    OutNewRevision = 0;
    if (!Get_IsReady() || !Id.IsValid() || Id.WorldGeneration != WorldGeneration ||
        ExpectedRevision <= 0 || ExpectedRevision >= TNumericLimits<int64>::Max() - 1 ||
        !HasExactRoster())
    { return false; }
    for (auto& Agent : Admitted)
    {
        if (!(Agent.Id == Id) || Agent.Revision != ExpectedRevision || !HasExactAgent(Agent))
        { continue; }
        OutNewRevision = ++Agent.Revision;
        return true;
    }
    return false;
}

void UCkCrowdGym_SharedSimulation::OnKnownStopCompleted(
    FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
{
    if (!bFailed) { return; }
    auto Observe = [&](TArray<FAgent>& Slots) -> bool
    {
        for (auto& Slot : Slots)
        {
            if (Slot.Raw != InRequestOwner) { continue; }
            if (!Slot.bStopIssued) { return true; }
            if (Slot.bStopCallbackSeen)
            { Slot.bStopSucceeded = false; return true; }
            Slot.bStopCallbackSeen = true;
            Slot.bStopSucceeded = InResult == ECk_Request_OperationResult::Succeeded;
            return true;
        }
        return false;
    };
    if (!Observe(Admitted)) { Observe(PendingAgents); }
}

void UCkCrowdGym_SharedSimulation::StopKnownMotion()
{
    if (bStoppingKnownMotion) { return; }
    bStoppingKnownMotion = true;
    // Fragment removal may notify external observers. Never iterate a retained array
    // across that operation; freeze only the exact raw/parent identity pairs first.
    TArray<TPair<FCk_Handle, FCk_Handle>> Known;
    Known.Reserve(Admitted.Num() + PendingAgents.Num());
    for (const auto& Agent : Admitted) { Known.Emplace(Agent.Raw, Agent.BatchRoot); }
    for (const auto& Agent : PendingAgents) { Known.Emplace(Agent.Raw, Agent.BatchRoot); }
    for (const auto& Entry : Known)
    {
        if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            !IsInGameThread()) { return; }
        const auto& Raw = Entry.Key;
        const auto& BatchRoot = Entry.Value;
        if (!ck_crowd_gym_shared_simulation::IsLive(Raw) ||
            !ck::IsValid(BatchRoot, ck::IsValid_Policy_IncludePendingKill{}) ||
            Raw.Get_RegistryView().Get_RegistryHandle() !=
                BatchRoot.Get_RegistryView().Get_RegistryHandle() ||
            !Raw.Has<ck::FFragment_LifetimeOwner>() ||
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Raw) != BatchRoot)
        { continue; }
        if (Raw.Has<ck::FFragment_EulerIntegrator>() ||
            Raw.Has<ck::FTag_EulerIntegrator_NeedsUpdate>())
        {
            auto Mutable = Raw;
            UCk_Utils_EulerIntegrator_UE::Request_Stop(Mutable, {});
        }
        if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return; }
        // Crowd steering is a distinct producer. Its real Stop completion is retained;
        // a queued request is not itself a quiescence proof.
        if (!ck_crowd_gym_shared_simulation::HasDirectOwner(Raw, BatchRoot) ||
            !HasLiveRoot() || !UCk_Utils_CrowdAgent_UE::Has(Raw) ||
            !UCk_Utils_Net_UE::Get_HasAuthority(Raw)) { continue; }
        FAgent* Slot = nullptr;
        for (auto& Candidate : Admitted)
        { if (Candidate.Raw == Raw) { Slot = &Candidate; break; } }
        if (!Slot)
        {
            for (auto& Candidate : PendingAgents)
            { if (Candidate.Raw == Raw) { Slot = &Candidate; break; } }
        }
        if (!Slot || Slot->bStopIssued || FCk_Handle{Slot->Handle} != Raw) { continue; }
        Slot->bStopIssued = true;
        auto Crowd = Slot->Handle;
        FCk_Delegate_Request_OnCompleted Completion;
        Completion.BindDynamic(this, &UCkCrowdGym_SharedSimulation::OnKnownStopCompleted);
        UCk_Utils_CrowdAgent_UE::Request_Stop(Crowd, Completion);
        if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return; }
    }
    // A callback may have destroyed this component. Only the surviving instance clears
    // its reentry latch; terminal invalidation never needs to rearm a rejected object.
    if (IsValid(this) && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { bStoppingKnownMotion = false; }
}

void UCkCrowdGym_SharedSimulation::Request_CloseUnproven()
{
    if (!bFailed) { FailAndRetire(TEXT("Shared source closed without a retirement proof")); }
    else { StopKnownMotion(); }
}

void UCkCrowdGym_SharedSimulation::FailAndRetire(const TCHAR* Reason)
{
    if (bFailed) { return; }
    bFailed = true;
    FailureReason = Reason;
    Pending = EPending::None;
    // Common admission must close before any fragment or request mutation can notify observers.
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return; }
    // The owner is notified on the same stack; it closes Common admission before another
    // callback can allocate. Retain the root and every known creator return on failure.
    auto* State = Cast<ACkCrowdGym_GameState_UE>(GetOwner());
    auto* World = GetWorld();
    if (IsValid(State) && !State->IsActorBeingDestroyed() &&
        !State->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        IsValid(World) && !World->bIsTearingDown &&
        State->GetWorld() == World && World->GetGameState() == State &&
        State->HasAuthority())
    { State->Request_CloseFailedShared(); }
}

auto UCkCrowdGym_SharedSimulation::HasExactStructuralRoster() const -> bool
{
    if (!HasLiveRoot() || Pending != EPending::None) { return false; }
    TSet<FCk_Handle> ExpectedRoots;
    TSet<FCk_Handle> SeenAgents;
    for (const auto& Root : CommittedBatchRoots)
    {
        if (!ck_crowd_gym_shared_simulation::HasDirectOwner(Root, WorldRoot) ||
            ExpectedRoots.Contains(Root)) { return false; }
        ExpectedRoots.Add(Root);
        TSet<FCk_Handle> ExpectedAgents;
        for (const auto& Agent : Admitted)
        {
            if (Agent.BatchRoot != Root) { continue; }
            if (!ck_crowd_gym_shared_simulation::HasDirectOwner(Agent.Raw, Root) ||
                FCk_Handle{Agent.Handle} != Agent.Raw ||
                SeenAgents.Contains(Agent.Raw)) { return false; }
            ExpectedAgents.Add(Agent.Raw);
            SeenAgents.Add(Agent.Raw);
        }
        if (ExpectedAgents.IsEmpty() ||
            !ck_crowd_gym_shared_simulation::HasExactDirectChildren(Root, ExpectedAgents))
        { return false; }
    }
    return SeenAgents.Num() == Admitted.Num() &&
        ck_crowd_gym_shared_simulation::HasExactDirectChildren(WorldRoot, ExpectedRoots) &&
        HasExactProbeRoster();
}

bool UCkCrowdGym_SharedSimulation::Request_RetireHealthyRoot()
{
    // Healthy component EndPlay and explicit authority retirement share this bounded path.
    // Initiate is not a generic owner admission fence; detect any currently allocated
    // callback-created descendant after the one root request, then retain Unproven.
    if (bFailed || Pending != EPending::None || !HasExactRoster() ||
        !HasExactProbeRoster())
    {
        bHealthyRetirementUnproven = true;
        Request_CloseUnproven();
        return false;
    }
    TSet<FCk_Handle> Frozen;
    Frozen.Add(WorldRoot);
    for (const auto& Root : CommittedBatchRoots) { Frozen.Add(Root); }
    for (const auto& Agent : Admitted)
    {
        Frozen.Add(Agent.Raw);
        // Get_IsReady/HasExactProbeRoster proved the exact typed reference before read.
        const auto Probe = Agent.Raw.Get<ck::FFragment_CrowdAgent_ProbeRef>()
            .Get_ProbeChild();
        Frozen.Add(FCk_Handle{Probe});
    }
    const auto Registry = WorldRoot.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    auto* World = GetWorld();
    // Healthy retirement preserves the established single-cascade contract.
    // An Initiate owner can still do final-frame work; no queued Stop receipt or
    // pre-cascade producer quiescence is claimed by this synchronous method.
    bFailed = true;
    Pending = EPending::None;
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    { return false; }
    if (!IsValid(World) || World->bIsTearingDown || GetWorld() != World ||
        !ck::IsValid(Registry) || !HasExactStructuralRoster())
    { bHealthyRetirementUnproven = true; return false; }
    // The settled probe may not be replaced between admission and destruction.
    for (const auto& Agent : Admitted)
    {
        const auto Probe = Agent.Raw.Get<ck::FFragment_CrowdAgent_ProbeRef>()
            .Get_ProbeChild();
        if (!Frozen.Contains(FCk_Handle{Probe}))
        { bHealthyRetirementUnproven = true; return false; }
    }
    auto MutableRoot = WorldRoot;
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableRoot);
    if (!IsValid(this) || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !IsValid(World) || World->bIsTearingDown || GetWorld() != World ||
        !ck::IsValid(Registry)) { return false; }
    bool bUnknown = false;
    Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Owner)
        {
            if (!Frozen.Contains(Owner.Get_Entity())) { return; }
            const FCk_Handle Child{Entity, RegistryHandle};
            if (!Frozen.Contains(Child)) { bUnknown = true; }
        });
    if (bUnknown) { bHealthyRetirementUnproven = true; }
    return !bUnknown;
}

void UCkCrowdGym_SharedSimulation::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvancePending();
}

void UCkCrowdGym_SharedSimulation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (!bFailed) { Request_RetireHealthyRoot(); }
    bFailed = true;
    Pending = EPending::None;
    // Failure never initiates a new root cascade; world teardown owns its release.
    Super::EndPlay(EndPlayReason);
}
