// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: A CHILD OF AN ANIMATED SOCKET ANCHOR SIGNALS ONCE PER POSE CHANGE
//============================================================================
//
// A child node under a node anchored to hand_r of a skinned mesh, with a
// transform-update listener bound on the child. While the pose driver moves
// hand_r every frame, the listener must fire once per pose change: at most once
// in any frame, and as many times in all as the pose changed (give or take the
// one change in flight at either end of the window). The late re-sample writes
// the child after this frame's consumers ran; its write reaches them next frame,
// once, and the early sample of that frame (the same pose) writes nothing.
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig.
//============================================================================

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_ChildSignalFiresOncePerPoseChange : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_PoseChanges = 40;
    private const int32 k_WarmupFrames = 5;
    private const int32 k_DrainFrames = 4;

    private const FTransform k_NodeOffset = FTransform(FRotator(0.0, 15.0, 0.0), FVector(2.0, 0.0, 5.0));
    private const FTransform k_ChildOffset = FTransform(FRotator(0.0, 0.0, 30.0), FVector(10.0, 0.0, 0.0));

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe _Probe;
    private FCk_Handle_SceneNode _Node;
    private FCk_Handle_SceneNode _Child;
    private FTransform _LastPayload;
    private bool _HasLastPayload = false;
    private int32 _RepeatedPayloads = 0;
    private int32 _PoseChangesAtStart = 0;
    private int32 _PoseChangesAtStop = 0;
    private int32 _FramesAtStop = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver and the probe", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r", n"Check_HasBone");
        Add_Step("compose the node and the child; listen on the child", n"Step_Compose");
        Add_Step("start driving the pose", n"Step_StartDriving");
        Add_Step_WaitFrames("let the chain take the moving pose", k_WarmupFrames);
        Add_Step("start counting", n"Step_StartCounting");
        Add_Step_WaitUntil("the pose changes enough times", n"Check_PoseChangedEnough");
        Add_Step("stop driving", n"Step_StopDriving");
        Add_Step_WaitUntil("the last change reaches the listener", n"Check_Drained");
        Add_Step("one signal per pose change", n"Step_Assert");
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
        auto Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        _Node = utils_scene_node::CreateAndAttachToUnrealMesh(Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone, k_NodeOffset);
        if (ck::Is_NOT_Valid(_Node))
        {
            FinishFailure("CreateAndAttachToUnrealMesh returned an invalid handle");
            return;
        }

        auto NodeTransform = _Node.As_Transform();
        _Child = utils_scene_node::Create(NodeTransform, k_ChildOffset);

        auto ChildTransform = _Child.As_Transform();
        utils_transform::BindTo_OnUpdate(ChildTransform, FCk_Delegate_Transform_OnUpdate(this, n"OnChildUpdated"),
            ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
    }

    UFUNCTION()
    private void OnChildUpdated(FCk_Handle_Transform InHandle, FTransform InTransform)
    {
        if (_Probe.IsSampling == false)
        { return; }

        _Probe.PendingSignalFires += 1;

        if (_HasLastPayload && _LastPayload.Equals(InTransform))
        { _RepeatedPayloads += 1; }

        _LastPayload = InTransform;
        _HasLastPayload = true;
    }

    UFUNCTION()
    private void Step_StartDriving(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.IsDriving = true;
    }

    UFUNCTION()
    private void Step_StartCounting(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Probe.Driver = _Driver;
        _Probe.Reset_Samples();
        _Probe.IsSampling = true;
        _PoseChangesAtStart = _Driver.PoseChanges;
    }

    UFUNCTION()
    private void Check_PoseChangedEnough(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Driver.PoseChanges - _PoseChangesAtStart >= k_PoseChanges);
    }

    UFUNCTION()
    private void Step_StopDriving(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.IsDriving = false;
        _PoseChangesAtStop = _Driver.PoseChanges;
        _FramesAtStop = _Probe.Frames;
    }

    UFUNCTION()
    private void Check_Drained(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Probe.Frames - _FramesAtStop >= k_DrainFrames);
    }

    UFUNCTION()
    private void Step_Assert(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        // The probe folds a frame's fires in at the end of the frame; take the frame still open too.
        _Probe.TotalSignalFires += _Probe.PendingSignalFires;
        _Probe.MaxSignalFiresPerFrame = Math::Max(_Probe.MaxSignalFiresPerFrame, _Probe.PendingSignalFires);
        _Probe.IsSampling = false;

        const auto PoseChanges = _PoseChangesAtStop - _PoseChangesAtStart;
        const auto Fires = _Probe.TotalSignalFires;

        Log(f"[AnimatedSocketAnchor] poseChanges={PoseChanges} fires={Fires} maxFiresPerFrame={_Probe.MaxSignalFiresPerFrame} repeatedPayloads={_RepeatedPayloads}");

        Assert_True(PoseChanges >= k_PoseChanges, f"the pose changed {PoseChanges} times");
        Assert_True(_Probe.MaxSignalFiresPerFrame == 1,
            f"the child's listener fired at most once in any frame, and did fire (max {_Probe.MaxSignalFiresPerFrame})");
        Assert_True(Math::Abs(Fires - PoseChanges) <= 1,
            f"the child's listener fired once per pose change ({Fires} fires for {PoseChanges} changes)");
        Assert_True(_RepeatedPayloads == 0,
            f"no two consecutive fires carried the same transform ({_RepeatedPayloads} repeats)");

        _Probe.DestroyActor();
        _Driver.DestroyActor();
    }
}
