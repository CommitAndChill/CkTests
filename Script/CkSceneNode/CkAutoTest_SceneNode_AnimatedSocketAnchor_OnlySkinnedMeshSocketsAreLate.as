// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: ONLY SKINNED-MESH SOCKET ANCHORS ARE RE-SAMPLED LATE
//============================================================================
//
// The late re-sample is for anchors an animated pose moves. Three anchors:
//   - hand_r of the pose driver's skinned mesh (CreateAndAttachToUnrealMesh): a
//     late anchor, and while the driver moves the bone it carries a late write
//     each frame (the positive control: it proves the reads below can see one);
//   - a static mesh component (CreateAndAttachToUnrealComponent) on an actor
//     that moves every frame: no late-anchor tag, and never a late write;
//   - the skinned mesh component itself, no socket (CreateAndAttachToUnrealComponent):
//     no late-anchor tag, and never a late write.
// A late write lives from the late pass to the next frame's transform pass, so
// a poll at the start of a frame sees the previous frame's.
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig.
//============================================================================

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_OnlySkinnedMeshSocketsAreLate : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_Polls = 30;
    private const int32 k_WarmupFrames = 5;

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_StaticMover _Mover;
    private FCk_Handle_SceneNode _SocketNode;
    private FCk_Handle_SceneNode _StaticNode;
    private FCk_Handle_SceneNode _ComponentNode;

    private int32 _PollCount = 0;
    private int32 _SocketLateWrites = 0;
    private int32 _StaticLateWrites = 0;
    private int32 _ComponentLateWrites = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver and the moving static mesh", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r", n"Check_HasBone");
        Add_Step("anchor a node to each", n"Step_Compose");
        Add_Step("start driving the pose", n"Step_StartDriving");
        Add_Step_WaitFrames("let the anchors take the motion", k_WarmupFrames);
        Add_Step_WaitUntil("poll the late writes while everything moves", n"Check_Polled");
        Add_Step("only the skinned-mesh socket anchor is late", n"Step_Assert");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));
        _Mover = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_StaticMover>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_StaticMover, ck_scene_node_late_pose::k_DriverLocation + FVector(0.0, 300.0, 0.0), FRotator::ZeroRotator));

        if (ck::Is_NOT_Valid(_Driver) || ck::Is_NOT_Valid(_Mover))
        { FinishFailure("the pose driver or the static mover did not spawn"); }
    }

    UFUNCTION()
    private void Check_HasBone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Driver.Get_HasBone());
    }

    UFUNCTION()
    private void Step_Compose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        _SocketNode = utils_scene_node::CreateAndAttachToUnrealMesh(Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone, FTransform::Identity);
        _StaticNode = utils_scene_node::CreateAndAttachToUnrealComponent(Root, _Mover.Mesh, FTransform::Identity);
        _ComponentNode = utils_scene_node::CreateAndAttachToUnrealComponent(Root, _Driver.Mesh, FTransform::Identity);

        if (ck::Is_NOT_Valid(_SocketNode) || ck::Is_NOT_Valid(_StaticNode) || ck::Is_NOT_Valid(_ComponentNode))
        {
            FinishFailure("an anchored node did not compose");
            return;
        }

        Assert_True(UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasLateAnchorTag(_SocketNode),
            "the skinned-mesh socket anchor is a late anchor");
        Assert_False(UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasLateAnchorTag(_StaticNode),
            "the static mesh component anchor is not a late anchor");
        Assert_False(UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasLateAnchorTag(_ComponentNode),
            "the skinned mesh component anchor (no socket) is not a late anchor");
    }

    UFUNCTION()
    private void Step_StartDriving(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.IsDriving = true;
    }

    UFUNCTION()
    private void Check_Polled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Mover.SetActorLocation(_Mover.GetActorLocation() + FVector(5.0, 0.0, 0.0));
        _Driver.SetActorRotation(FRotator(0.0, float(_PollCount) * 3.0, 0.0));

        if (UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasTransformLateUpdatedTag(_SocketNode))
        { _SocketLateWrites += 1; }

        if (UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasTransformLateUpdatedTag(_StaticNode))
        { _StaticLateWrites += 1; }

        if (UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasTransformLateUpdatedTag(_ComponentNode))
        { _ComponentLateWrites += 1; }

        _PollCount += 1;

        auto Result = OutResult;
        Result.Set(_PollCount >= k_Polls);
    }

    UFUNCTION()
    private void Step_Assert(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.IsDriving = false;

        Log(f"[AnimatedSocketAnchor] polls={_PollCount} lateWrites: socket={_SocketLateWrites} static={_StaticLateWrites} component={_ComponentLateWrites}");

        Assert_True(_SocketLateWrites > 0, f"the moving skinned-mesh socket anchor carried late writes ({_SocketLateWrites} of {_PollCount} polls)");
        Assert_True(_StaticLateWrites == 0, f"the moving static mesh component anchor never carried a late write ({_StaticLateWrites})");
        Assert_True(_ComponentLateWrites == 0, f"the skinned mesh component anchor (no socket) never carried a late write ({_ComponentLateWrites})");
        Assert_False(UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasLateAnchorTag(_StaticNode), "the static anchor never became late");
        Assert_False(UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasLateAnchorTag(_ComponentNode), "the component anchor never became late");

        // A detached node follows nothing, so it is no late anchor either.
        utils_scene_node::Request_Detach(_SocketNode, FCk_Delegate_Request_OnCompleted());
        Assert_False(UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_HasLateAnchorTag(_SocketNode),
            "detaching the skinned-mesh socket anchor removes its late-anchor tag");

        _Mover.DestroyActor();
        _Driver.DestroyActor();
    }
}
