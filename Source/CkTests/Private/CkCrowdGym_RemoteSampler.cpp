#include "CkCrowdGym_RemoteSampler.h"

#include "CkCrowd/Agent/CkCrowdAgent_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Neighbors_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkNavigation/Nav/CkNav_Fragment.h"
#include "CkQueue/Queue/CkQueue_Utils.h"
#include <Engine/World.h>

namespace ck_crowd_gym_remote_sampler
{
    auto IsLive(const FCk_Handle& Handle) -> bool
    {
        return ck::IsValid(Handle) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    // Walk an already-live owner chain without calling Get_WorldForEntity, which recurses and
    // diagnoses malformed/self-owned input before the caller can reject it.
    auto IsInWorld(const FCk_Handle& Handle, const FCk_Handle& Transient,
                   const UWorld& World) -> bool
    {
        if (!IsLive(Transient)) { return false; }
        const auto RegistryHandle = Transient.Get_RegistryView().Get_RegistryHandle();
        TSet<FCk_Handle> Seen;
        FCk_Handle Cursor = Handle;
        while (true)
        {
            if (!IsLive(Cursor) ||
                Cursor.Get_RegistryView().Get_RegistryHandle() != RegistryHandle ||
                Seen.Contains(Cursor))
            { return false; }
            Seen.Add(Cursor);
            if (Cursor.Has<TWeakObjectPtr<UWorld>>() &&
                Cursor.Get<TWeakObjectPtr<UWorld>>().Get() != &World)
            { return false; }
            if (Cursor == Transient)
            { return Cursor.Has<TWeakObjectPtr<UWorld>>(); }
            if (!Cursor.Has<ck::FFragment_LifetimeOwner>()) { return false; }
            Cursor = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Cursor);
        }
    }

    auto IsAncestorOrSelf(const FCk_Handle& Ancestor, const FCk_Handle& Leaf) -> bool
    {
        TSet<FCk_Handle> Seen;
        FCk_Handle Cursor = Leaf;
        while (IsLive(Cursor) && !Seen.Contains(Cursor))
        {
            if (Cursor == Ancestor) { return true; }
            Seen.Add(Cursor);
            if (!Cursor.Has<ck::FFragment_LifetimeOwner>()) { break; }
            Cursor = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Cursor);
        }
        return false;
    }

    auto ToRemote(ECk_Nav_PathFailReason Value, ECkCrowdRemoteNavPathFailReason& Out) -> bool
    {
        switch (Value)
        {
            case ECk_Nav_PathFailReason::None: Out = ECkCrowdRemoteNavPathFailReason::None; return true;
            case ECk_Nav_PathFailReason::NoNavSystem: Out = ECkCrowdRemoteNavPathFailReason::NoNavSystem; return true;
            case ECk_Nav_PathFailReason::NoNavData: Out = ECkCrowdRemoteNavPathFailReason::NoNavData; return true;
            case ECk_Nav_PathFailReason::NoDefaultFilter: Out = ECkCrowdRemoteNavPathFailReason::NoDefaultFilter; return true;
            case ECk_Nav_PathFailReason::StartProjectFailed: Out = ECkCrowdRemoteNavPathFailReason::StartProjectFailed; return true;
            case ECk_Nav_PathFailReason::EndProjectFailed: Out = ECkCrowdRemoteNavPathFailReason::EndProjectFailed; return true;
            case ECk_Nav_PathFailReason::FindPathError: Out = ECkCrowdRemoteNavPathFailReason::FindPathError; return true;
            case ECk_Nav_PathFailReason::FindPathNoPath: Out = ECkCrowdRemoteNavPathFailReason::FindPathNoPath; return true;
            case ECk_Nav_PathFailReason::FindPathInvalid: Out = ECkCrowdRemoteNavPathFailReason::FindPathInvalid; return true;
            case ECk_Nav_PathFailReason::EmptyPath: Out = ECkCrowdRemoteNavPathFailReason::EmptyPath; return true;
            case ECk_Nav_PathFailReason::NotAuthority: Out = ECkCrowdRemoteNavPathFailReason::NotAuthority; return true;
            case ECk_Nav_PathFailReason::BudgetExceeded: Out = ECkCrowdRemoteNavPathFailReason::BudgetExceeded; return true;
            case ECk_Nav_PathFailReason::PendingTimeout: Out = ECkCrowdRemoteNavPathFailReason::PendingTimeout; return true;
        }
        return false;
    }

    auto ToRemote(ECk_Nav_PathStatus Value, ECkCrowdRemoteNavPathStatus& Out) -> bool
    {
        switch (Value)
        {
            case ECk_Nav_PathStatus::None: Out = ECkCrowdRemoteNavPathStatus::None; return true;
            case ECk_Nav_PathStatus::Pending: Out = ECkCrowdRemoteNavPathStatus::Pending; return true;
            case ECk_Nav_PathStatus::Ready: Out = ECkCrowdRemoteNavPathStatus::Ready; return true;
            case ECk_Nav_PathStatus::Failed: Out = ECkCrowdRemoteNavPathStatus::Failed; return true;
            case ECk_Nav_PathStatus::Partial: Out = ECkCrowdRemoteNavPathStatus::Partial; return true;
        }
        return false;
    }

    auto ToRemote(ECk_PathNetwork_RouteFailReason Value, ECkCrowdRemotePathNetworkFailReason& Out) -> bool
    {
        switch (Value)
        {
            case ECk_PathNetwork_RouteFailReason::None: Out = ECkCrowdRemotePathNetworkFailReason::None; return true;
            case ECk_PathNetwork_RouteFailReason::NoNetwork: Out = ECkCrowdRemotePathNetworkFailReason::NoNetwork; return true;
            case ECk_PathNetwork_RouteFailReason::NetworkNotBuilt: Out = ECkCrowdRemotePathNetworkFailReason::NetworkNotBuilt; return true;
            case ECk_PathNetwork_RouteFailReason::NoRouteFound: Out = ECkCrowdRemotePathNetworkFailReason::NoRouteFound; return true;
            case ECk_PathNetwork_RouteFailReason::NotAuthority: Out = ECkCrowdRemotePathNetworkFailReason::NotAuthority; return true;
            case ECk_PathNetwork_RouteFailReason::BudgetExceeded: Out = ECkCrowdRemotePathNetworkFailReason::BudgetExceeded; return true;
        }
        return false;
    }

    auto ToRemote(ECk_CrowdAgent_PathProvider Value, ECkCrowdRemotePathProvider& Out) -> bool
    {
        switch (Value)
        {
            case ECk_CrowdAgent_PathProvider::None: Out = ECkCrowdRemotePathProvider::None; return true;
            case ECk_CrowdAgent_PathProvider::Navigation: Out = ECkCrowdRemotePathProvider::Navigation; return true;
            case ECk_CrowdAgent_PathProvider::PathNetwork: Out = ECkCrowdRemotePathProvider::PathNetwork; return true;
            case ECk_CrowdAgent_PathProvider::VoxelNav: Out = ECkCrowdRemotePathProvider::VoxelNav; return true;
            case ECk_CrowdAgent_PathProvider::GroundNav: Out = ECkCrowdRemotePathProvider::GroundNav; return true;
        }
        return false;
    }

    template <typename TEnum>
    auto EnumName(TEnum Value) -> FString
    {
        const UEnum* Enum = StaticEnum<TEnum>();
        return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString{};
    }

    auto TryCopyAgent(const FCkCrowdGymServerAgentRecord& Record,
                      const FCk_Handle& RequestingPawnEntity,
                      FCkCrowdRemoteAgentRow& Row) -> bool
    {
        const FCk_Handle& Agent = Record.Agent;
        Row.Id = Record.Id;
        Row.ConfigurationRevision = Record.ConfigurationRevision;
        Row.OwnerSerial = Record.OwnerSerial;
        Row.DebugName = Agent.Get_DebugName().ToString();
        Row.DisplayId = FString::Printf(TEXT("%lld:%lld"),
            Record.Id.WorldGeneration, Record.Id.AgentSerial);

        const FCk_Handle Owner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Agent);
        if (!IsLive(Owner)) { return false; }
        Row.OwnerName = Owner.Get_DebugName().ToString();

        const auto& Tunables = Agent.Get<ck::FFragment_CrowdAgent_Tunables>();
        Tunables.Get_Tags().GetGameplayTagArray(Row.Tags);
        for (const FGameplayTag& Tag : Row.Tags)
        { if (!Tag.IsValid()) { return false; } }
        Row.PrimaryTag = Row.Tags.IsEmpty() ? TEXT("—") : Row.Tags[0].ToString();
        Row.RadiusCm = Tunables.Get_Radius();
        Row.HeightCm = Tunables.Get_Height();
        Row.MaxSpeedCmPerSec = Tunables.Get_MaxSpeed();
        Row.MaxTurnRateRadPerSec = Tunables.Get_MaxTurnRate();
        Row.MaxAccelerationCmPerSecSq = Tunables.Get_MaxAcceleration();
        Row.ArrivalRadiusCm = Tunables.Get_ArrivalRadius();
        Row.SeparationRadiusCm = Tunables.Get_SeparationRadius();
        Row.SeparationWeight = Tunables.Get_SeparationWeight();

        const FCk_Handle_Transform Transform = UCk_Utils_Transform_UE::Cast(Agent);
        if (!ck::IsValid(Transform)) { return false; }
        Row.WorldTransform = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform);
        Row.Position = Row.WorldTransform.GetLocation();
        Row.BaseColor = UCk_Utils_CrowdAgent_UE::Get_DebugColor(Record.Agent);
        Row.bHasDebugOverride = UCk_Utils_CrowdAgent_UE::Get_HasDebugOverride(Record.Agent);

        if (Agent.Has<ck::FFragment_CrowdAgent_DesiredVelocity>())
        { Row.Velocity = Agent.Get<ck::FFragment_CrowdAgent_DesiredVelocity>().Get_Velocity(); }
        Row.bIsWalking = Agent.Has<ck::FTag_CrowdAgent_Walking>();
        // Native body tint uses these tags independently of status precedence.
        Row.bIsPathPending = Agent.Has<ck::FTag_CrowdAgent_PathPending>();
        Row.bIsDisabled = Agent.Has<ck::FTag_CrowdAgent_Disabled>();
        if (Agent.Has<ck::FFragment_CrowdAgent_PathFollow>())
        {
            const auto& Follow = Agent.Get<ck::FFragment_CrowdAgent_PathFollow>();
            Row.ActiveGoal = Follow.Get_ActiveGoal();
            Row.ActiveArrivalRadiusCm = Follow.Get_ActiveArrivalRadius();
            if (!ToRemote(Follow.Get_ActiveProvider(), Row.ActiveProvider)) { return false; }
        }
        if (Agent.Has<ck::FFragment_Nav_PathResult>())
        {
            const auto& Path = Agent.Get<ck::FFragment_Nav_PathResult>();
            ECkCrowdRemoteNavPathStatus ValidatedStatus;
            if (!ToRemote(Path.Get_Status(), ValidatedStatus)) { return false; }
            Row.PlannedPathPointCount = Path.Get_Waypoints().Num();
            if (Path.Get_Status() == ECk_Nav_PathStatus::Failed)
            {
                Row.Status = ECkCrowdRemoteAgentStatus::Failed;
                if (!ToRemote(Path.Get_Diagnostics().Get_LastFailReason(), Row.PathFailReason)) { return false; }
            }
        }
        if (Agent.Has<ck::FFragment_CrowdAgent_PathTrouble>())
        {
            const auto& Trouble = Agent.Get<ck::FFragment_CrowdAgent_PathTrouble>();
            Row.bHasPathTroubleEvent = Trouble.Get_HasEvent();
            Row.bHadPathNetworkFailure = Trouble.Get_HadPathNetworkFailure();
            Row.bUsedNavigationFallback = Trouble.Get_UsedNavigationFallback();
            if (!ToRemote(Trouble.Get_PathNetworkFailReason(), Row.PathNetworkFailReason) ||
                !ToRemote(Trouble.Get_NavigationStatus(), Row.TroubleNavigationStatus) ||
                !ToRemote(Trouble.Get_NavigationFailReason(), Row.TroubleNavigationFailReason))
            { return false; }
            Row.PathTroubleAgentPosition = Trouble.Get_AgentLocation();
            Row.PathTroubleGoal = Trouble.Get_GoalLocation();
            Row.PathTroubleEventTimeSeconds = Trouble.Get_EventTimeSeconds();
            if (Row.bHasPathTroubleEvent)
            {
                const FString NavStatus = EnumName(Trouble.Get_NavigationStatus());
                if (Row.bHadPathNetworkFailure)
                {
                    Row.PathTroubleSummary = FString::Printf(TEXT("SIDEWALK: %s -> UNREAL NAV: %s"),
                        *EnumName(Trouble.Get_PathNetworkFailReason()), *NavStatus);
                }
                else if (Trouble.Get_NavigationFailReason() == ECk_Nav_PathFailReason::None)
                { Row.PathTroubleSummary = FString::Printf(TEXT("UNREAL NAV: %s"), *NavStatus); }
                else
                {
                    Row.PathTroubleSummary = FString::Printf(TEXT("UNREAL NAV: %s (%s)"),
                        *NavStatus, *EnumName(Trouble.Get_NavigationFailReason()));
                }
            }
        }
        if (Agent.Has<ck::FFragment_CrowdAgent_NeighborCache>())
        { Row.NeighborCount = Agent.Get<ck::FFragment_CrowdAgent_NeighborCache>().Get_Neighbors().Num(); }
        if (Agent.Has<ck::FFragment_CrowdAgent_SeparationForce>())
        { Row.SeparationForce = Agent.Get<ck::FFragment_CrowdAgent_SeparationForce>().Get_Force(); }

        if (Row.Status != ECkCrowdRemoteAgentStatus::Failed)
        {
            if (Row.bIsDisabled) { Row.Status = ECkCrowdRemoteAgentStatus::Disabled; }
            else if (Row.bIsWalking) { Row.Status = ECkCrowdRemoteAgentStatus::Walking; }
            else if (Row.bIsPathPending) { Row.Status = ECkCrowdRemoteAgentStatus::Replanning; }
            else if (Agent.Has<ck::FTag_CrowdAgent_Idle>()) { Row.Status = ECkCrowdRemoteAgentStatus::Idle; }
        }
        if (IsLive(RequestingPawnEntity) &&
            (IsAncestorOrSelf(RequestingPawnEntity, Agent) ||
             IsAncestorOrSelf(Agent, RequestingPawnEntity)))
        { Row.Status = ECkCrowdRemoteAgentStatus::PlayerProxy; }
        return true;
    }
}

auto FCkCrowdGymRemoteSampler::TrySample(
    UWorld& World, int64 WorldGeneration, int64 SampleSequence,
    const TArray<FCkCrowdGymServerAgentRecord>& Roster,
    const FCk_Handle& RequestingPawnEntity,
    const FCkCrowdRemoteAgentId* SelectedId, int64 SelectionRequestSerial,
    TArray<FCkCrowdRemoteAgentRow>& OutRows, FCkCrowdRemoteAgentDetail& OutDetail) -> bool
{
    using namespace ck_crowd_gym_remote_sampler;
    OutRows.Reset();
    OutDetail = {};
    if (!IsInGameThread() || !::IsValid(&World) || World.GetNetMode() == NM_Client ||
        WorldGeneration <= 0 || SampleSequence <= 0 ||
        (SelectedId && (!SelectedId->IsValid() ||
            SelectedId->WorldGeneration != WorldGeneration || SelectionRequestSerial <= 0)) ||
        (!SelectedId && SelectionRequestSerial != 0))
    { return false; }
    const FCk_Handle Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(World);
    if (!IsLive(Transient) ||
        (RequestingPawnEntity != FCk_Handle{} && !IsInWorld(RequestingPawnEntity, Transient, World)))
    { return false; }
    if (Roster.IsEmpty()) { return SelectedId == nullptr; }

    TSet<FCkCrowdRemoteAgentId> Ids;
    TSet<FCk_Handle> Handles;
    TMap<FCk_Handle, FCkCrowdRemoteAgentId> ByHandle;
    int32 SelectedIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Roster.Num(); ++Index)
    {
        const auto& Entry = Roster[Index];
        const FCk_Handle& Agent = Entry.Agent;
        if (!Entry.Id.IsValid() || Entry.Id.WorldGeneration != WorldGeneration ||
            Entry.ConfigurationRevision < 0 || Entry.OwnerSerial < 0 ||
            Ids.Contains(Entry.Id) || Handles.Contains(Agent) ||
            !IsInWorld(Agent, Transient, World) ||
            !UCk_Utils_CrowdAgent_UE::Has(Agent) ||
            !UCk_Utils_Transform_UE::Has(Agent) ||
            !Agent.Has<ck::FFragment_CrowdAgent_Tunables>() ||
            !Agent.Has<ck::FFragment_CrowdAgent_PathFollow>() ||
            !Agent.Has<ck::FFragment_CrowdAgent_DesiredVelocity>() ||
            !Agent.Has<ck::FFragment_LifetimeOwner>())
        { return false; }
        Ids.Add(Entry.Id);
        Handles.Add(Agent);
        ByHandle.Add(Agent, Entry.Id);
        if (SelectedId && Entry.Id == *SelectedId) { SelectedIndex = Index; }
    }
    if (SelectedId && SelectedIndex == INDEX_NONE) { return false; }
    for (const auto& Entry : Roster)
    {
        if (Entry.OwnerSerial == 0) { continue; }
        const FCk_Handle Owner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Entry.Agent);
        const FCkCrowdRemoteAgentId* OwnerId = ByHandle.Find(Owner);
        if (OwnerId == nullptr || OwnerId->AgentSerial != Entry.OwnerSerial)
        { return false; }
    }

    TArray<FCkCrowdRemoteAgentRow> Candidate;
    Candidate.Reserve(Roster.Num());
    for (const auto& Entry : Roster)
    {
        FCkCrowdRemoteAgentRow Row;
        Row.SampleSequence = SampleSequence;
        if (!TryCopyAgent(Entry, RequestingPawnEntity, Row) ||
            !FCkCrowdRemoteDebugValidation::IsValid(Row))
        { return false; }
        Candidate.Add(MoveTemp(Row));
    }

    // Queue projection is read once from this world and joined only by the validated native
    // roster handle. A queue row never supplies an agent ID or changes roster membership.
    TSet<int32> QueuedAgentIndices;
    for (const auto& Queue : UCk_Utils_Queue_UE::Get_DebugSnapshots(Transient))
    {
        for (const auto& Member : Queue.Get_Members())
        {
            for (int32 Index = 0; Index < Roster.Num(); ++Index)
            {
                if (static_cast<int64>(Roster[Index].Agent.Get_Entity().Get_ID()) != Member.Get_MoverIdentity())
                { continue; }
                auto& Row = Candidate[Index];
                if (QueuedAgentIndices.Contains(Index)) { return false; } // ambiguous queue membership
                QueuedAgentIndices.Add(Index);
                Row.QueueDebugName = Queue.Get_QueueDebugName().ToString();
                Row.QueueCategory = Queue.Get_Category().ToString();
                Row.QueueState = EnumName(Queue.Get_State());
                Row.QueueRank = Member.Get_Rank();
            }
        }
    }

    FCkCrowdRemoteAgentDetail CandidateDetail;
    if (SelectedId)
    {
        const FCk_Handle& Agent = Roster[SelectedIndex].Agent;
        CandidateDetail.Id = *SelectedId;
        CandidateDetail.SampleSequence = SampleSequence;
        CandidateDetail.ConfigurationRevision = Roster[SelectedIndex].ConfigurationRevision;
        CandidateDetail.SelectionRequestSerial = SelectionRequestSerial;
        CandidateDetail.Sample = Candidate[SelectedIndex];
        if (Agent.Has<ck::FFragment_CrowdAgent_NeighborCache>())
        {
            for (const auto& Neighbor : Agent.Get<ck::FFragment_CrowdAgent_NeighborCache>().Get_Neighbors())
            {
                FCkCrowdRemoteNeighbor Copy;
                Copy.DistanceCm = Neighbor.Get_Distance();
                Copy.RelativeOffset = Neighbor.Get_RelativeOffset();
                if (const FCkCrowdRemoteAgentId* Resolved = ByHandle.Find(Neighbor.Get_Handle()))
                { Copy.Id = *Resolved; Copy.bIdResolved = true; }
                CandidateDetail.Neighbors.Add(MoveTemp(Copy));
            }
        }
        if (Agent.Has<ck::FFragment_Nav_PathResult>())
        { CandidateDetail.PlannedPath = Agent.Get<ck::FFragment_Nav_PathResult>().Get_Waypoints(); }
        if (!FCkCrowdRemoteDebugValidation::IsValid(CandidateDetail)) { return false; }
    }
    for (const auto& Row : Candidate)
    { if (!FCkCrowdRemoteDebugValidation::IsValid(Row)) { return false; } }
    OutRows = MoveTemp(Candidate);
    OutDetail = MoveTemp(CandidateDetail);
    return true;
}
