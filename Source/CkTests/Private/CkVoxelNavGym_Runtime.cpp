#include "CkVoxelNavGym_Runtime.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkVoxelNav/Volume/CkVoxelNavVolume_Utils.h"
#include "CkVoxelNav/Path/CkVoxelNavPath_Utils.h"
#include "Engine/World.h"

namespace ck_voxel_nav_gym
{
    bool IsFinitePoint(const FVector& Point)
    {
        return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) &&
            FMath::IsFinite(Point.Z) && Point.GetAbsMax() <= 100000.0;
    }

    bool IsValidAgent(const FCkVoxelNavGym_AgentFrame& Agent, int32 Index)
    {
        if (Index < 0 || Index > 1 || Agent.Index != Index ||
            !Agent.Transform.IsValid() || Agent.Transform.ContainsNaN() ||
            !Agent.Transform.GetRotation().IsNormalized() ||
            !Agent.Transform.GetScale3D().Equals(FVector::OneVector, 0.001) ||
            !IsFinitePoint(Agent.Transform.GetLocation()) || !IsFinitePoint(Agent.Goal) ||
            !FMath::IsFinite(Agent.TargetPitch) || FMath::Abs(Agent.TargetPitch) > 180.0f ||
            Agent.MovementState < 0 || Agent.MovementState > static_cast<int32>(ECk_CrowdAgent_MovementState::Walking) ||
            Agent.MoveEpisode < 0 || Agent.MoveCorrelation < 0 || Agent.Waypoints.Num() > 128)
        { return false; }
        for (const auto& Point : Agent.Waypoints)
        { if (!IsFinitePoint(Point)) { return false; } }
        return true;
    }
}

bool UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(const FCk_Handle& Handle, UWorld* World)
{
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
        World->bIsTearingDown || World->IsBeingCleanedUp() ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle) || !ck::IsValid(Handle))
    { return false; }
    if (Handle.Has_Any<ck::FTag_DestroyEntity_Initiate, ck::FTag_DestroyEntity_EndPlay,
        ck::FTag_DestroyEntity_Teardown, ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>())
    { return false; }
    const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(WorldRoot) ||
        !ck::IsValid(WorldRoot) || !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    return WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get() == World &&
        WorldRoot.Get_RegistryView().Get_RegistryHandle() == Handle.Get_RegistryView().Get_RegistryHandle();
}


bool UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVolume(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) &&
        Handle.Has_All<ck::FFragment_VoxelNavVolume_Params,
            ck::FFragment_VoxelNavVolume_BuildState,
            ck::FFragment_VoxelNavVolume_BuiltOctree>() &&
        !Handle.Has<ck::FFragment_VoxelNavVolume_Chunks>();
}

bool UCk_VoxelNavGym_Runtime_UE::Get_IsLiveCrowdAgent(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) &&
        Handle.Has_All<ck::FFragment_CrowdAgent_Tunables,
            ck::FFragment_CrowdAgent_PathFollow, ck::FFragment_CrowdAgent_FaceAngle>();
}

bool UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVoxelPath(const FCk_Handle& Handle, UWorld* World)
{
    return Get_IsLiveInWorld(Handle, World) &&
        Handle.Has_All<ck::FFragment_VoxelNavPath_Params,
            ck::FFragment_VoxelNavPath_Result>();
}

bool UCk_VoxelNavGym_Runtime_UE::TryGet_SourceForest(
    const FCk_Handle& Root, const FCk_Handle& WorldRoot, TArray<FCk_Handle>& OutForest)
{
    const auto RetainedRoot = Root;
    const auto RetainedWorld = WorldRoot;
    OutForest.Reset();
    if (!IsInGameThread() ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(RetainedWorld) ||
        !RetainedWorld.Has<TWeakObjectPtr<UWorld>>()) { return false; }
    UWorld* World = RetainedWorld.Get<TWeakObjectPtr<UWorld>>().Get();
    if (!Get_IsLiveInWorld(RetainedWorld, World) ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) != RetainedWorld)
    { return false; }
    if (!Get_IsLiveInWorld(RetainedRoot, World) ||
        !RetainedRoot.Has<ck::FFragment_LifetimeOwner>())
    { return false; }
    const auto ActorRoot = RetainedRoot.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
    TArray<FCk_Handle> Complete;
    FString Reason;
    if (!ck_selection_gym_census::TryCensusAllocatedBank(World, ActorRoot, {}, Complete, Reason) ||
        !Complete.Contains(RetainedRoot))
    { return false; }
    for (const auto& Handle : Complete)
    {
        auto Ancestor = Handle;
        TSet<FCk_Handle> Seen;
        while (Ancestor != ActorRoot)
        {
            if (!Complete.Contains(Ancestor) || Seen.Contains(Ancestor) ||
                !Ancestor.Has<ck::FFragment_LifetimeOwner>())
            { OutForest.Reset(); return false; }
            if (Ancestor == RetainedRoot)
            { OutForest.Add(Handle); break; }
            Seen.Add(Ancestor);
            Ancestor = Ancestor.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
        }
    }
    return OutForest.Contains(RetainedRoot);
}

bool UCk_VoxelNavGym_Runtime_UE::TryGet_AgentForest(const FCk_Handle& SourceRoot,
    const FCk_Handle& AgentRoot, const FCk_Handle& WorldRoot, TArray<FCk_Handle>& OutForest)
{
    const auto Source = SourceRoot, Agent = AgentRoot, Context = WorldRoot;
    OutForest.Reset();
    TArray<FCk_Handle> Complete;
    if (!TryGet_SourceForest(Source, Context, Complete) || !Complete.Contains(Agent) ||
        Agent == Source || !Agent.Has<ck::FFragment_LifetimeOwner>() ||
        Agent.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Source ||
        !UCk_Utils_CrowdAgent_UE::Has(Agent)) { return false; }
    TArray<FCk_Handle> Candidate;
    for (const auto& Handle : Complete)
    {
        auto Ancestor = Handle;
        TSet<FCk_Handle> Seen;
        while (Ancestor != Source)
        {
            if (!Complete.Contains(Ancestor) || Seen.Contains(Ancestor) ||
                !Ancestor.Has<ck::FFragment_LifetimeOwner>()) { return false; }
            if (Ancestor == Agent) { Candidate.Add(Handle); break; }
            Seen.Add(Ancestor);
            Ancestor = Ancestor.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
        }
    }
    if (!Candidate.Contains(Agent)) { return false; }
    OutForest = MoveTemp(Candidate);
    return true;
}

bool UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
    const FCk_Handle& Handle, UWorld* World, int32 Index, FCkVoxelNavGym_AgentFrame& OutFrame)
{
    OutFrame = {};
    if (Index < 0 || Index > 1 || !Get_IsLiveCrowdAgent(Handle, World) ||
        !Get_IsLiveVoxelPath(Handle, World) || !UCk_Utils_Transform_UE::Has(Handle))
    { return false; }
    auto Mutable = Handle;
    const auto Agent = UCk_Utils_CrowdAgent_UE::CastChecked(Mutable);
    const auto Path = UCk_Utils_VoxelNavPath_UE::CastChecked(Mutable);
    const auto Transform = UCk_Utils_Transform_UE::CastChecked(Mutable);
    FCkVoxelNavGym_AgentFrame Candidate;
    Candidate.Index = Index;
    Candidate.Transform = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform);
    Candidate.Goal = UCk_Utils_CrowdAgent_UE::Get_ActiveGoal(Agent);
    Candidate.TargetPitch = UCk_Utils_CrowdAgent_UE::Get_TargetPitchDegrees(Agent);
    Candidate.MovementState = static_cast<int32>(UCk_Utils_CrowdAgent_UE::Get_MovementState(Agent));
    Candidate.MoveEpisode = UCk_Utils_CrowdAgent_UE::Get_ActiveMoveEpisode(Agent);
    Candidate.MoveCorrelation = UCk_Utils_CrowdAgent_UE::Get_ActiveMoveCorrelationId(Agent);
    Candidate.GoalReached = UCk_Utils_CrowdAgent_UE::Get_HasReachedActiveGoal(Agent);
    Candidate.Waypoints = UCk_Utils_VoxelNavPath_UE::Get_Waypoints(Path);
    if (!ck_voxel_nav_gym::IsValidAgent(Candidate, Index)) { return false; }
    OutFrame = MoveTemp(Candidate);
    return true;
}

bool UCk_VoxelNavGym_Runtime_UE::Get_IsValidFrame(const FCkVoxelNavGym_WireSnapshot& Frame)
{
    if (Frame.SourceGeneration != 1 || Frame.Sequence <= 0 || Frame.ControlGeneration <= 0 ||
        (Frame.Phase != 1 && Frame.Phase != 2 && Frame.Phase != 4) ||
        (Frame.Phase == 1 ? Frame.Replacing != 0 : Frame.Replacing != -1)) { return false; }
    if (Frame.Phase == 4) { return Frame.Stations.Num() <= 1; }
    if (Frame.Stations.Num() != 1) { return false; }
    const auto& Station = Frame.Stations[0];
    if (Station.PairGeneration <= 0 || Station.ControlCount < 0 || !Station.Built ||
        !FMath::IsFinite(Station.BuildProgress) || Station.BuildProgress < 0.0f ||
        Station.BuildProgress > 1.0f) { return false; }
    return true;
}

bool UCk_VoxelNavGym_Runtime_UE::Get_IsSameFrame(
    const FCkVoxelNavGym_WireSnapshot& Left, const FCkVoxelNavGym_WireSnapshot& Right)
{ return FCkVoxelNavGym_WireSnapshot::StaticStruct()->CompareScriptStruct(&Left, &Right, PPF_None); }
