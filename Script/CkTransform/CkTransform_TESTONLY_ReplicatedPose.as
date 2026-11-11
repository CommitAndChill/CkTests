// Language=angelscript

//============================================================================
// CK TRANSFORM - TEST-ONLY SUPPORT: replicated movers parented on a machine without authority
//============================================================================
//
// Fixtures for the CkAutoTest_Net_ReplicatedPose_* tests. A mover is a bare Replicates root with a replicated
// Transform, an optional ECk_Transform_ReplicatedPosePolicy declared in DoConstruct on every machine, and a replicated
// report the authority writes after each move. Anchors are plain local transforms on the machine that parents.
//
// The two attach processors execute an attach order stamped by a test body, at a chosen point of a frame, once the
// mover's replicated report shows a move newer than the one named in the order:
// - Replication:    in FGroup_Replication, after the net-apply handlers of the same frame and before the tail pump;
// - GameplayScript: in FGroup_Gameplay_Script, the main pass of the next frame.
// Both are marker-less: never pumped, they run once per frame in the main pass of their group.
//
// TESTONLY: registered globally like every script processor; each query requires a test-only fragment, so outside the
// tests they visit nothing.
//============================================================================

namespace constants_replicated_pose_test
{
    const FName k_Tag = n"TAG_CkTestReplicatedPose";
}

//--------------------------------------------------------------------------------------------------------------------------
// The mover
//--------------------------------------------------------------------------------------------------------------------------

enum ECk_TESTONLY_ReplicatedPose_ConstructAttach
{
    None,
    // A machine without authority parents the mover to a local anchor in DoConstruct, before its first pose applies.
    WithoutAuthority
}

// Written by the authority after each move, then marked dirty.
struct FCk_TESTONLY_ReplicatedPose_Report
{
    UPROPERTY()
    int32 Stage = 0;

    UPROPERTY()
    int32 MoveCount = 0;

    UPROPERTY()
    FVector LatestLocation = FVector::ZeroVector;

    UPROPERTY()
    FRotator LatestRotation = FRotator::ZeroRotator;

    UPROPERTY()
    FVector RestLocation = FVector::ZeroVector;

    UPROPERTY()
    FRotator RestRotation = FRotator::ZeroRotator;
}

// What this machine composed for the mover; never replicated.
struct FCk_TESTONLY_ReplicatedPose_Local
{
    UPROPERTY()
    int32 Id = 0;

    // Set only when the mover was parented in DoConstruct.
    UPROPERTY()
    FCk_Handle_Transform ConstructAnchor;
}

struct FCk_TESTONLY_ReplicatedPose_MoverParams
{
    UPROPERTY()
    int32 Id = 0;

    UPROPERTY()
    ECk_Transform_ReplicatedPosePolicy Policy = ECk_Transform_ReplicatedPosePolicy::AlwaysApplied;

    UPROPERTY()
    ECk_TESTONLY_ReplicatedPose_ConstructAttach ConstructAttach = ECk_TESTONLY_ReplicatedPose_ConstructAttach::None;

    UPROPERTY()
    FVector InitialLocation = FVector::ZeroVector;

    FCk_TESTONLY_ReplicatedPose_MoverParams() {}

    FCk_TESTONLY_ReplicatedPose_MoverParams(int32 InId, ECk_Transform_ReplicatedPosePolicy InPolicy, ECk_TESTONLY_ReplicatedPose_ConstructAttach InConstructAttach, FVector InInitialLocation)
    {
        Id = InId;
        Policy = InPolicy;
        ConstructAttach = InConstructAttach;
        InitialLocation = InInitialLocation;
    }
}

// The authority composes at InitialLocation; a machine without authority composes at the origin, so only a replicated
// pose can bring its copy to the authority's.
class UCk_TESTONLY_ReplicatedPose_Mover_EntityScript : UCk_GenericEntityScript_UE
{
    default _ShowInPlaceActors = false;
    default _Replication = ECk_Replication::Replicates;

    UPROPERTY(ExposeOnSpawn)
    int32 Id = 0;

    UPROPERTY(ExposeOnSpawn)
    ECk_Transform_ReplicatedPosePolicy Policy = ECk_Transform_ReplicatedPosePolicy::AlwaysApplied;

    UPROPERTY(ExposeOnSpawn)
    ECk_TESTONLY_ReplicatedPose_ConstructAttach ConstructAttach = ECk_TESTONLY_ReplicatedPose_ConstructAttach::None;

    UPROPERTY(ExposeOnSpawn)
    FVector InitialLocation = FVector::ZeroVector;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        utils_entity_tag::Add(InHandle, constants_replicated_pose_test::k_Tag);

        auto Local = FCk_TESTONLY_ReplicatedPose_Local();
        Local.Id = Id;
        InHandle.Add_Fragment(FCk_TESTONLY_ReplicatedPose_Report(), ECk_Replication::Replicates);

        const auto HasAuthority = utils_net::Get_HasAuthority(InHandle);
        const auto Start = HasAuthority ? InitialLocation : FVector::ZeroVector;
        auto Mover = utils_transform::Add(InHandle, FTransform(Start), ECk_Replication::Replicates);
        utils_transform::Request_SetReplicatedPosePolicy(Mover, Policy);

        if (ConstructAttach == ECk_TESTONLY_ReplicatedPose_ConstructAttach::WithoutAuthority && HasAuthority == false)
        {
            Local.ConstructAnchor = utils_replicated_pose_test::Create_Anchor(InHandle, FVector(0.0, 0.0, 300.0));
            utils_scene_node::Add(Mover, Local.ConstructAnchor, FTransform::Identity);
        }

        InHandle.Add_Fragment(Local);
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}

//--------------------------------------------------------------------------------------------------------------------------
// Attach orders
//--------------------------------------------------------------------------------------------------------------------------

struct FCk_TESTONLY_ReplicatedPose_AttachOrder
{
    UPROPERTY()
    FCk_Handle_Transform Anchor;

    UPROPERTY()
    FTransform Offset = FTransform::Identity;

    // Executed once the mover's report shows a MoveCount above this.
    UPROPERTY()
    int32 AfterMoveCount = 0;

    FCk_TESTONLY_ReplicatedPose_AttachOrder() {}

    FCk_TESTONLY_ReplicatedPose_AttachOrder(FCk_Handle_Transform InAnchor, int32 InAfterMoveCount)
    {
        Anchor = InAnchor;
        AfterMoveCount = InAfterMoveCount;
    }
}

struct FCk_TESTONLY_ReplicatedPose_AttachAtReplication
{
    UPROPERTY()
    FCk_TESTONLY_ReplicatedPose_AttachOrder Order;
}

struct FCk_TESTONLY_ReplicatedPose_AttachAtGameplayScript
{
    UPROPERTY()
    FCk_TESTONLY_ReplicatedPose_AttachOrder Order;
}

// Stamped by an attach processor when it executed an order: the frame and the report it saw.
struct FCk_TESTONLY_ReplicatedPose_Attached
{
    UPROPERTY()
    int64 Frame = 0;

    UPROPERTY()
    int32 MoveCount = 0;
}

enum ECk_TESTONLY_ReplicatedPose_AttachSite
{
    Replication,
    GameplayScript
}

namespace utils_replicated_pose_test
{
    FCk_Handle_Transform Create_Anchor(FCk_Handle& InOwner, FVector InLocation)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(InOwner);
        return utils_transform::Add(Entity, FTransform(InLocation), ECk_Replication::DoesNotReplicate);
    }

    // The live mover with InId on InAny's machine; invalid while it has not arrived.
    FCk_Handle_Transform Find(const FCk_Handle& InAny, int32 InId)
    {
        for (auto Candidate : utils_entity_tag::ForEach_Entity(InAny, constants_replicated_pose_test::k_Tag))
        {
            if (utils_entity_lifetime::Get_IsPendingDestroy(Candidate, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
            { continue; }

            if (Candidate.Has_Fragment(FCk_TESTONLY_ReplicatedPose_Local) == false || utils_transform::Has(Candidate) == false)
            { continue; }

            if (Candidate.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Local).Id == InId)
            { return Candidate.As_Transform(); }
        }

        return FCk_Handle_Transform();
    }

    FCk_Handle_Transform Get_ConstructAnchor(const FCk_Handle& InMover)
    {
        return InMover.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Local).ConstructAnchor;
    }

    FCk_TESTONLY_ReplicatedPose_Report Get_Report(const FCk_Handle& InMover)
    {
        return InMover.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Report);
    }

    // Authority only: moves the mover to InLocation / InRotation and reports it.
    void Request_Move(FCk_Handle_Transform& InMover, FVector InLocation, FRotator InRotation)
    {
        auto Location = FCk_Request_Transform_SetLocation(InLocation);
        Location.Set_LocalWorld(ECk_LocalWorld::World);
        utils_transform::Request_SetLocation(InMover, Location);

        auto Rotation = FCk_Request_Transform_SetRotation(InRotation);
        Rotation.Set_LocalWorld(ECk_LocalWorld::World);
        utils_transform::Request_SetRotation(InMover, Rotation);

        auto Entity = InMover.H();
        auto& Report = Entity.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Report);
        Report.MoveCount += 1;
        Report.LatestLocation = InLocation;
        Report.LatestRotation = InRotation;
        Entity.Request_MarkReplicationDirty(FCk_TESTONLY_ReplicatedPose_Report);
    }

    // Authority only.
    void Request_SetStage(FCk_Handle_Transform& InMover, int32 InStage)
    {
        auto Entity = InMover.H();
        auto& Report = Entity.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Report);
        Report.Stage = InStage;
        Entity.Request_MarkReplicationDirty(FCk_TESTONLY_ReplicatedPose_Report);
    }

    // Authority only: the mover rests where it was last moved to, and the report says so.
    void Request_Rest(FCk_Handle_Transform& InMover, int32 InStage)
    {
        auto Entity = InMover.H();
        auto& Report = Entity.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Report);
        Report.Stage = InStage;
        Report.RestLocation = Report.LatestLocation;
        Report.RestRotation = Report.LatestRotation;
        Entity.Request_MarkReplicationDirty(FCk_TESTONLY_ReplicatedPose_Report);
    }

    void Add_AttachOrder(FCk_Handle& InMover, ECk_TESTONLY_ReplicatedPose_AttachSite InSite, const FCk_TESTONLY_ReplicatedPose_AttachOrder& InOrder)
    {
        InMover.Request_TryRemove(FCk_TESTONLY_ReplicatedPose_Attached);

        if (InSite == ECk_TESTONLY_ReplicatedPose_AttachSite::Replication)
        { InMover.AddOrGet_Fragment(FCk_TESTONLY_ReplicatedPose_AttachAtReplication).Order = InOrder; }
        else
        { InMover.AddOrGet_Fragment(FCk_TESTONLY_ReplicatedPose_AttachAtGameplayScript).Order = InOrder; }
    }

    // Attaches InMover per InOrder if its report shows a newer move; true when it did.
    bool TryExecute(FCk_Handle& InMover, const FCk_TESTONLY_ReplicatedPose_AttachOrder& InOrder)
    {
        const auto MoveCount = InMover.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Report).MoveCount;
        if (MoveCount <= InOrder.AfterMoveCount)
        { return false; }

        auto Mover = InMover.As_Transform();
        auto Anchor = InOrder.Anchor;
        utils_scene_node::Add(Mover, Anchor, InOrder.Offset);

        auto& Attached = InMover.AddOrGet_Fragment(FCk_TESTONLY_ReplicatedPose_Attached);
        Attached.Frame = utils_stats::Get_FrameCount();
        Attached.MoveCount = MoveCount;
        return true;
    }

    void Collect(FCk_ScriptQueryBatch InBatch, TArray<FCk_Handle>& OutHandles)
    {
        OutHandles.Empty();
        for (int32 Index = 0; Index < InBatch.Num(); ++Index)
        { OutHandles.Add(InBatch.GetHandle(Index)); }
    }

    // Where a node composed under InAnchor with a translation-only InOffset is in the world.
    FVector Get_ComposedLocation(const FCk_Handle_Transform& InAnchor, const FTransform& InOffset)
    {
        return utils_transform::Get_EntityCurrentTransform(InAnchor).TransformPosition(InOffset.GetLocation());
    }
}

//--------------------------------------------------------------------------------------------------------------------------
// Forced-site attach processors
//--------------------------------------------------------------------------------------------------------------------------

class UCk_TESTONLY_Processor_ReplicatedPose_AttachAtReplication : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Replication";

    private TArray<FCk_Handle> _Collected;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_TESTONLY_ReplicatedPose_AttachAtReplication);
        Query.Require(FCk_TESTONLY_ReplicatedPose_Report);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        utils_replicated_pose_test::Collect(Batch, _Collected);
        for (int32 Index = 0; Index < _Collected.Num(); ++Index)
        {
            auto Mover = _Collected[Index];
            const auto Order = Mover.Get_Fragment(FCk_TESTONLY_ReplicatedPose_AttachAtReplication).Order;
            if (utils_replicated_pose_test::TryExecute(Mover, Order))
            { Mover.Request_TryRemove(FCk_TESTONLY_ReplicatedPose_AttachAtReplication); }
        }
    }
}

class UCk_TESTONLY_Processor_ReplicatedPose_AttachAtGameplayScript : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Gameplay_Script";

    private TArray<FCk_Handle> _Collected;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_TESTONLY_ReplicatedPose_AttachAtGameplayScript);
        Query.Require(FCk_TESTONLY_ReplicatedPose_Report);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        utils_replicated_pose_test::Collect(Batch, _Collected);
        for (int32 Index = 0; Index < _Collected.Num(); ++Index)
        {
            auto Mover = _Collected[Index];
            const auto Order = Mover.Get_Fragment(FCk_TESTONLY_ReplicatedPose_AttachAtGameplayScript).Order;
            if (utils_replicated_pose_test::TryExecute(Mover, Order))
            { Mover.Request_TryRemove(FCk_TESTONLY_ReplicatedPose_AttachAtGameplayScript); }
        }
    }
}
