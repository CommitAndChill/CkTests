// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A CHAIN AS DEEP AS THE MAXIMUM FOLLOWS ITS ROOT IN ONE FRAME
//============================================================================
//
// A bare Transform root carries a chain of scene nodes exactly as deep as the
// framework allows: one node at every depth from 0 to the maximum minus one,
// so every layer processor in the chain has a node to compose. Every Create
// must succeed and the leaf must land at the composed pose.
//
// Then one processor moves the root in the gameplay group and a later one
// reads the leaf in the same frame: the move must have reached the leaf before
// the global pump, through every layer, including the deepest.
//============================================================================

struct FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult
{
    bool Completed = false;
    bool Succeeded = false;
    int64 StimulusFrame = -1;
    int64 ProbeFrame = -1;

    FCk_Handle_Transform Root;
    FCk_Handle_Transform Leaf;
    FTransform TargetRootTransform;
    FTransform ActualLeafTransform;
}

struct FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProducerMarker {}
struct FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProbeMarker {}

class UCk_TESTONLY_Processor_SceneNodeDepth_ExactFrameProducer : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Gameplay_TimeDelta";

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProducerMarker);
        Query.Require(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        for (int32 Index = 0; Index < Batch.Num(); ++Index)
        {
            auto Entity = Batch.GetHandle(Index);
            auto& Result = Entity.Get_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult);
            if (Result.StimulusFrame >= 0)
            { continue; }

            Result.StimulusFrame = utils_time::Get_FrameCounter();
            utils_transform::Request_SetTransform(Result.Root, Result.TargetRootTransform);
            Entity.Request_TryRemove(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProducerMarker);
        }
    }
}

class UCk_TESTONLY_Processor_SceneNodeDepth_ExactFrameProbe : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Gameplay_Camera";
    default _MarkedDirtyBy = FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProbeMarker;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProbeMarker);
        Query.Require(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        for (int32 Index = 0; Index < Batch.Num(); ++Index)
        {
            auto Entity = Batch.GetHandle(Index);
            auto& Result = Entity.Get_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult);
            if (Result.Completed || Result.StimulusFrame < 0)
            { continue; }

            Result.Succeeded = ck::IsValid(Result.Root) && ck::IsValid(Result.Leaf);
            if (Result.Succeeded)
            { Result.ActualLeafTransform = utils_transform::Get_EntityCurrentTransform(Result.Leaf); }

            Result.ProbeFrame = utils_time::Get_FrameCounter();
            Result.Completed = true;
            Entity.Request_TryRemove(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProbeMarker);
        }
    }
}

class UCk_AutoTest_SceneNode_Depth_ChainAtMaxDepthFollowsItsRootInOneFrame : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;

    private const FVector LinkLocalLocation = FVector(40.0f, 0.0f, 5.0f);
    private const FRotator LinkLocalRotation = FRotator(0.0f, 7.0f, 0.0f);
    private const float32 PositionToleranceCm = 1.0f;

    private FCk_Handle _TestEntity;
    private FCk_Handle_Transform _Root;
    private TArray<FCk_Handle_Transform> _Nodes;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        _TestEntity = InHandle;

        const auto MaxDepth = utils_scene_node::Get_MaxDepth();
        Assert_True(MaxDepth > 1, f"The maximum scene-node depth must allow a chain (got {MaxDepth})");
        if (IsFinished())
        { return; }

        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(_Root))
        {
            FinishFailure("Failed to add the Transform feature to the bare root");
            return;
        }

        auto Parent = _Root;
        for (int32 Depth = 0; Depth < MaxDepth; ++Depth)
        {
            auto Node = utils_scene_node::Create(Parent, Get_LinkLocal());
            if (ck::Is_NOT_Valid(Node))
            {
                FinishFailure(f"Create of the node at depth [{Depth}] must succeed: the maximum depth is [{MaxDepth}], so depths 0 to {MaxDepth - 1} are supported");
                return;
            }

            const auto NodeDepth = utils_scene_node::Get_Depth(Node);
            Assert_True(NodeDepth.IsSet() && NodeDepth.GetValue() == Depth,
                f"The node created under depth [{Depth - 1}] must sit at depth [{Depth}]");
            if (IsFinished())
            { return; }

            Parent = Node.As_Transform();
            _Nodes.Add(Parent);
        }

        WaitUntil(n"Check_LeafComposedAtRest", n"OnLeafComposedAtRest", 120);
    }

    private FTransform Get_LinkLocal() const
    {
        return FTransform(LinkLocalRotation, LinkLocalLocation, FVector::OneVector);
    }

    private FTransform Get_ExpectedLeafWorld(const FTransform& InRootWorld) const
    {
        auto World = InRootWorld;
        for (int32 Index = 0; Index < _Nodes.Num(); ++Index)
        { World = Get_LinkLocal() * World; }

        return World;
    }

    UFUNCTION()
    private void Check_LeafComposedAtRest(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_transform::Get_EntityCurrentLocation(_Nodes.Last()).Equals(
            Get_ExpectedLeafWorld(FTransform::Identity).GetLocation(), PositionToleranceCm));
    }

    UFUNCTION()
    private void OnLeafComposedAtRest(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished())
        { return; }

        auto& Result = _TestEntity.AddOrGet_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult);
        Result.Completed = false;
        Result.Succeeded = false;
        Result.StimulusFrame = -1;
        Result.ProbeFrame = -1;
        Result.Root = _Root;
        Result.Leaf = _Nodes.Last();
        Result.TargetRootTransform = FTransform(FRotator(0.0f, -35.0f, 0.0f), FVector(640.0f, -210.0f, 30.0f), FVector::OneVector);

        _TestEntity.AddOrGet_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProducerMarker);
        _TestEntity.AddOrGet_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameProbeMarker);
        WaitUntil(n"Check_ExactFrameProbe", n"OnExactFrameProbe");
    }

    UFUNCTION()
    private void Check_ExactFrameProbe(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_TestEntity.Has_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult) &&
            _TestEntity.Get_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult).Completed);
    }

    UFUNCTION()
    private void OnExactFrameProbe(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished())
        { return; }

        const auto& Result = _TestEntity.Get_Fragment(FCk_Fragment_TESTONLY_SceneNodeDepth_ExactFrameResult);
        const auto Expected = Get_ExpectedLeafWorld(Result.TargetRootTransform);

        Assert_True(Result.Succeeded, "The exact-frame probe must resolve the root and the leaf");
        Assert_True(Result.ProbeFrame == Result.StimulusFrame,
            f"The leaf must be read in the frame the root moved; stimulus [{Result.StimulusFrame}], probe [{Result.ProbeFrame}]");
        Assert_True(Result.ActualLeafTransform.GetLocation().Equals(Expected.GetLocation(), PositionToleranceCm),
            f"The leaf at depth [{_Nodes.Num() - 1}] must follow the root in the frame it moved; expected [{Expected.GetLocation()}], actual [{Result.ActualLeafTransform.GetLocation()}]");
        if (IsFinished())
        { return; }

        FinishSuccess();
    }
}
