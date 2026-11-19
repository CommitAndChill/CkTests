// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A DESTROY BETWEEN THE EARLY AND LATE PASS RAISES NOTHING
//============================================================================
//
// The pose driver ticks in TG_PostPhysics, after the frame's early ECS pass and
// before its late re-sample, and does each teardown from there while it moves
// hand_r:
//   1. destroys the child of a late anchor (the late walk meets a node whose
//      destruction began this frame);
//   2. destroys the late anchor itself;
//   3. destroys its own actor, and so the skinned mesh a second late anchor
//      follows (the late re-sample meets a component that is gone).
// Each stage then runs k_FramesPerStage frames. Any ensure or error fails the
// test; the probe's frame count proves the frames ran.
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig.
//============================================================================

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_DestroyedBetweenPassesRaisesNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_FramesPerStage = 5;

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe _Probe;
    private FCk_Handle_Transform _Root;
    private FCk_Handle_SceneNode _Node;
    private FCk_Handle_SceneNode _Child;
    private FCk_Handle_SceneNode _SecondNode;
    private int32 _FramesMark = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver and a frame counter", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r", n"Check_HasBone");
        Add_Step("compose two late anchors and a child; drive the pose", n"Step_ComposeAndDrive");
        Add_Step_WaitUntil("the pose moves", n"Check_StageFrames");
        Add_Step("destroy the child between the passes", n"Step_DestroyChild");
        Add_Step_WaitUntil("frames run after the child's destroy", n"Check_StageFrames");
        Add_Step("destroy the late anchor between the passes", n"Step_DestroyNode");
        Add_Step_WaitUntil("frames run after the anchor's destroy", n"Check_StageFrames");
        Add_Step("destroy the mesh between the passes", n"Step_DestroyMesh");
        Add_Step_WaitUntil("frames run after the mesh's destroy", n"Check_StageFrames");
        Add_Step("the frames ran and the destroyed are gone", n"Step_Assert");
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
        _Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        _Node = utils_scene_node::CreateAndAttachToUnrealMesh(_Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone, FTransform::Identity);
        _SecondNode = utils_scene_node::CreateAndAttachToUnrealMesh(_Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone,
            FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 7.0)));
        if (ck::Is_NOT_Valid(_Node) || ck::Is_NOT_Valid(_SecondNode))
        {
            FinishFailure("CreateAndAttachToUnrealMesh returned an invalid handle");
            return;
        }

        auto NodeTransform = _Node.As_Transform();
        _Child = utils_scene_node::Create(NodeTransform, FTransform(FRotator::ZeroRotator, FVector(6.0, 0.0, 0.0)));

        _Driver.IsDriving = true;
        _FramesMark = _Probe.Frames;
    }

    UFUNCTION()
    private void Check_StageFrames(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        const auto Done = _Probe.Frames - _FramesMark >= k_FramesPerStage;
        if (Done)
        { _FramesMark = _Probe.Frames; }

        auto Result = OutResult;
        Result.Set(Done);
    }

    UFUNCTION()
    private void Step_DestroyChild(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.DestroyEntityOnNextTick = _Child;
    }

    UFUNCTION()
    private void Step_DestroyNode(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_False(ck::IsValid(_Child), "the child was destroyed");
        _Driver.DestroyEntityOnNextTick = _Node;
    }

    UFUNCTION()
    private void Step_DestroyMesh(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_False(ck::IsValid(_Node), "the late anchor was destroyed");
        _Driver.DestroySelfOnNextTick = true;
    }

    UFUNCTION()
    private void Step_Assert(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_False(ck::IsValid(_Driver), "the mesh's actor was destroyed");
        Assert_True(ck::IsValid(_SecondNode), "the node that followed the destroyed mesh is still alive");

        _Probe.DestroyActor();
    }
}
