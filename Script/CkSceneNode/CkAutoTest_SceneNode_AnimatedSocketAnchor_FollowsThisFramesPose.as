// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: AN ANIMATED SOCKET ANCHOR FOLLOWS THIS FRAME'S POSE
//============================================================================
//
// A node made with CreateAndAttachToUnrealMesh on hand_r of a skinned mesh, a
// child node under it, and a grandchild node that hosts an Unreal component.
// The pose driver moves hand_r every frame in TG_PostPhysics; the probe, after
// everything else in the frame, requires each node's ECS transform (and the
// hosted component's world transform) to sit where its offsets put it on hand_r
// as the mesh reads THAT frame - within 0.01 cm and 0.01 degrees, every frame,
// for k_Frames frames. A node that sampled the socket before the pose moved
// trails by the bone's travel per frame (about 15 cm here).
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig.
//============================================================================

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_FollowsThisFramesPose : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_Frames = 40;
    private const int32 k_WarmupFrames = 5;
    private const float64 k_LocationToleranceCm = 0.01;
    private const float64 k_RotationToleranceDeg = 0.01;

    private const FTransform k_NodeOffset = FTransform(FRotator(10.0, 30.0, 0.0), FVector(4.0, -2.0, 6.0));
    private const FTransform k_ChildOffset = FTransform(FRotator(0.0, -45.0, 20.0), FVector(12.0, 0.0, 3.0));
    private const FTransform k_GrandchildOffset = FTransform(FRotator(5.0, 0.0, -15.0), FVector(0.0, 8.0, -4.0));

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe _Probe;
    private FCk_Handle_Transform _Root;
    private FCk_Handle_SceneNode _Node;
    private FCk_Handle_SceneNode _Child;
    private FCk_Handle_SceneNode _Grandchild;
    private FCk_Handle_UnrealComponent _Component;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver and the probe", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r", n"Check_HasBone");
        Add_Step("compose the node, its child and the component host", n"Step_Compose");
        Add_Step_WaitUntil("the hosted component is set up", n"Check_ComponentReady");
        Add_Step("start driving the pose", n"Step_StartDriving");
        Add_Step_WaitFrames("let the chain take the moving pose", k_WarmupFrames);
        Add_Step("start sampling", n"Step_StartSampling");
        Add_Step_WaitUntil("sample the frames", n"Check_Sampled");
        Add_Step("every node matched this frame's pose", n"Step_Assert");
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
    private void Step_Compose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        _Node = utils_scene_node::CreateAndAttachToUnrealMesh(_Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone, k_NodeOffset);
        if (ck::Is_NOT_Valid(_Node))
        {
            FinishFailure("CreateAndAttachToUnrealMesh returned an invalid handle");
            return;
        }

        auto NodeTransform = _Node.As_Transform();
        _Child = utils_scene_node::Create(NodeTransform, k_ChildOffset);
        auto ChildTransform = _Child.As_Transform();
        _Grandchild = utils_scene_node::Create(ChildTransform, k_GrandchildOffset);

        const auto ComponentParams = utils_unreal_component::Make_Params(USceneComponent,
            ECk_UnrealComponent_TickPolicy::DoNotTick, n"AnimatedSocketAnchor_Component");
        _Component = utils_unreal_component::Add(FCk_Handle(_Grandchild), ComponentParams);
    }

    UFUNCTION()
    private void Check_ComponentReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(Cast<USceneComponent>(utils_unreal_component::Get_Component(_Component))));
    }

    UFUNCTION()
    private void Step_StartDriving(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.IsDriving = true;
    }

    UFUNCTION()
    private void Step_StartSampling(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto NodeCheck = FCk_AutoTest_SceneNode_LatePose_Check();
        NodeCheck.Name = "node";
        NodeCheck.Node = _Node.As_Transform();
        NodeCheck.FromSocket = k_NodeOffset;

        auto ChildCheck = FCk_AutoTest_SceneNode_LatePose_Check();
        ChildCheck.Name = "child";
        ChildCheck.Node = _Child.As_Transform();
        ChildCheck.FromSocket = k_ChildOffset * k_NodeOffset;

        auto GrandchildCheck = FCk_AutoTest_SceneNode_LatePose_Check();
        GrandchildCheck.Name = "grandchild";
        GrandchildCheck.Node = _Grandchild.As_Transform();
        GrandchildCheck.FromSocket = k_GrandchildOffset * k_ChildOffset * k_NodeOffset;
        GrandchildCheck.Component = Cast<USceneComponent>(utils_unreal_component::Get_Component(_Component));

        _Probe.Driver = _Driver;
        _Probe.Checks.Add(NodeCheck);
        _Probe.Checks.Add(ChildCheck);
        _Probe.Checks.Add(GrandchildCheck);
        _Probe.Reset_Samples();
        _Probe.IsSampling = true;
    }

    UFUNCTION()
    private void Check_Sampled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Probe.Samples >= k_Frames);
    }

    UFUNCTION()
    private void Step_Assert(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Probe.IsSampling = false;
        _Driver.IsDriving = false;

        Log(f"[AnimatedSocketAnchor] samples={_Probe.Samples} maxLocationError={_Probe.MaxLocationErrorCm :.4}cm maxRotationError={_Probe.MaxRotationErrorDeg :.4}deg worst={_Probe.WorstCheck} socketTravel={_Probe.MaxSocketTravelCm :.2}cm");

        Assert_True(_Probe.MaxSocketTravelCm >= 10.0,
            f"the pose moved hand_r at least 10 cm a frame (max {_Probe.MaxSocketTravelCm :.2} cm)");
        Assert_True(_Probe.MaxLocationErrorCm <= k_LocationToleranceCm,
            f"every node sat on this frame's hand_r within {k_LocationToleranceCm} cm (max {_Probe.MaxLocationErrorCm :.4} cm, {_Probe.WorstCheck})");
        Assert_True(_Probe.MaxRotationErrorDeg <= k_RotationToleranceDeg,
            f"every node faced this frame's hand_r within {k_RotationToleranceDeg} deg (max {_Probe.MaxRotationErrorDeg :.4} deg, {_Probe.WorstCheck})");

        _Probe.DestroyActor();
        _Driver.DestroyActor();
    }
}
