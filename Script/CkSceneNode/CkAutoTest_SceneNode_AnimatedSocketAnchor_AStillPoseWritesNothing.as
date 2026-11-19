// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A STILL POSE WRITES NOTHING
//============================================================================
//
// A node anchored to hand_r of a skinned mesh, and a child under it. While the
// pose driver moves the bone, the node carries late writes (the positive
// control). Once the pose stops, after the last write has been handed on, no
// frame writes either node: no late write and no transform-updated marker,
// for k_StillFrames frames. A re-sample that wrote an unchanged pose, or an
// early sample that disagreed with the late one, would show here.
//
// A late write lives from the late pass to the next frame's transform pass, and
// the transform-updated marker until the next frame's cleanup, so a poll at the
// start of a frame sees what the previous frame wrote.
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig.
//============================================================================

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_AStillPoseWritesNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_DrivenFrames = 20;
    private const int32 k_GraceFrames = 3;
    private const int32 k_StillFrames = 15;

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe _Probe;
    private FCk_Handle_SceneNode _Node;
    private FCk_Handle_SceneNode _Child;

    private int32 _FramesMark = 0;
    private int32 _DrivenLateWrites = 0;
    private int32 _StillPolls = 0;
    private int32 _StillLateWrites = 0;
    private int32 _StillUpdates = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver and a frame counter", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r", n"Check_HasBone");
        Add_Step("compose the node and its child; drive the pose", n"Step_ComposeAndDrive");
        Add_Step_WaitUntil("the moving pose writes the node late", n"Check_Driven");
        Add_Step("stop the pose", n"Step_Stop");
        Add_Step_WaitUntil("the last write is handed on", n"Check_Grace");
        Add_Step_WaitUntil("the still pose writes nothing", n"Check_Still");
        Add_Step("no late write and no update while the pose is still", n"Step_Assert");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));
        _Probe = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));

        if (ck::Is_NOT_Valid(_Driver) || ck::Is_NOT_Valid(_Probe))
        { FinishFailure("the pose driver or the probe did not spawn"); }
    }

    UFUNCTION()
    private void Check_HasBone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Driver.Get_HasBone());
    }

    UFUNCTION()
    private void Step_ComposeAndDrive(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        _Node = utils_scene_node::CreateAndAttachToUnrealMesh(Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone,
            FTransform(FRotator(0.0, 20.0, 0.0), FVector(3.0, 0.0, 0.0)));
        if (ck::Is_NOT_Valid(_Node))
        {
            FinishFailure("CreateAndAttachToUnrealMesh returned an invalid handle");
            return;
        }

        auto NodeTransform = _Node.As_Transform();
        _Child = utils_scene_node::Create(NodeTransform, FTransform(FRotator::ZeroRotator, FVector(0.0, 9.0, 0.0)));

        _Driver.IsDriving = true;
        _FramesMark = _Probe.Frames;
    }

    UFUNCTION()
    private void Check_Driven(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        if (UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasTransformLateUpdatedTag(_Node))
        { _DrivenLateWrites += 1; }

        auto Result = OutResult;
        Result.Set(_Probe.Frames - _FramesMark >= k_DrivenFrames);
    }

    UFUNCTION()
    private void Step_Stop(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.IsDriving = false;
        _FramesMark = _Probe.Frames;
    }

    UFUNCTION()
    private void Check_Grace(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Probe.Frames - _FramesMark >= k_GraceFrames);
        if (_Probe.Frames - _FramesMark >= k_GraceFrames)
        { _FramesMark = _Probe.Frames; }
    }

    UFUNCTION()
    private void Check_Still(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _StillPolls += 1;
        CountWrites(_Node);
        CountWrites(_Child);

        auto Result = OutResult;
        Result.Set(_Probe.Frames - _FramesMark >= k_StillFrames);
    }

    private void CountWrites(FCk_Handle_SceneNode InNode)
    {
        if (UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasTransformLateUpdatedTag(InNode))
        { _StillLateWrites += 1; }

        if (UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasTransformUpdatedTag(InNode))
        { _StillUpdates += 1; }
    }

    UFUNCTION()
    private void Step_Assert(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Log(f"[AnimatedSocketAnchor] drivenLateWrites={_DrivenLateWrites} stillPolls={_StillPolls} stillLateWrites={_StillLateWrites} stillUpdates={_StillUpdates}");

        Assert_True(_DrivenLateWrites > 0, f"the moving pose wrote the node late ({_DrivenLateWrites} polls)");
        Assert_True(_StillPolls > 0, "the still frames were polled");
        Assert_True(_StillLateWrites == 0, f"no late write while the pose was still ({_StillLateWrites})");
        Assert_True(_StillUpdates == 0, f"no transform update while the pose was still ({_StillUpdates})");

        _Probe.DestroyActor();
        _Driver.DestroyActor();
    }
}
