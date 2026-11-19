// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: AN ACTOR UNDER A LATE ANCHOR FOLLOWS THIS FRAME'S POSE, AND CAN DIE BETWEEN PASSES
//============================================================================
//
// An actor-backed entity (a WithActor entity script: its Transform is anchored
// on the actor's root component) scene-node-parented under a node on hand_r is
// parent-driven (FTag_Transform_ExternallyDriven), so the late re-sample
// composes it and FProcessor_Transform_SyncToActor_Late pushes the pose to the
// actor. While the pose driver moves hand_r every frame:
//   1. at the probe (after everything else in the frame) the actor's root
//      component sits where its offsets put it on THIS frame's hand_r, within
//      0.01 cm and 0.01 degrees, for k_Frames frames;
//   2. the pose driver then destroys the actor from its TG_PostPhysics tick,
//      between the frame's early pass and its late one: the late pass meets an
//      entity whose destruction only began and whose root component is gone.
//      No ensure may fire, and the frames go on.
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig and
// CkAutoTest_SceneNode_ActorAttachedToActor.as for the actor-backed helper.
//============================================================================

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_AnActorUnderALateAnchorFollowsAndCanDie : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_Frames = 30;
    private const int32 k_WarmupFrames = 5;
    private const int32 k_FramesAfterDestroy = 5;
    private const float64 k_LocationToleranceCm = 0.01;
    private const float64 k_RotationToleranceDeg = 0.01;

    private const FTransform k_NodeOffset = FTransform(FRotator(10.0, 30.0, 0.0), FVector(4.0, -2.0, 6.0));
    private const FTransform k_ActorOffset = FTransform(FRotator(0.0, -45.0, 20.0), FVector(12.0, 0.0, 3.0));

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe _Probe;
    private ACk_AutoTest_SceneNode_AttachedActor_Helper _Actor;
    private FCk_Handle _ActorEntity;
    private FCk_Handle_SceneNode _Node;
    private int32 _EnsuresBefore = 0;
    private int32 _FramesMark = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver, the probe and the actor", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r and the actor's entity is ready", n"Check_Ready");
        Add_Step("parent the actor's entity under a node on hand_r; drive the pose", n"Step_Compose");
        Add_Step_WaitFrames("let the chain take the moving pose", k_WarmupFrames);
        Add_Step("start sampling", n"Step_StartSampling");
        Add_Step_WaitUntil("sample the frames", n"Check_Sampled");
        Add_Step("the actor matched this frame's pose; destroy it between the passes", n"Step_AssertAndDestroy");
        Add_Step_WaitUntil("frames run after the actor's destroy", n"Check_FramesAfterDestroy");
        Add_Step("nothing ensured and the actor is gone", n"Step_AssertDestroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));
        _Probe = Cast<ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe>(SpawnActor(
            ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe, ck_scene_node_late_pose::k_DriverLocation, FRotator::ZeroRotator));
        _Actor = Cast<ACk_AutoTest_SceneNode_AttachedActor_Helper>(SpawnActor(
            ACk_AutoTest_SceneNode_AttachedActor_Helper, FVector::ZeroVector, FRotator::ZeroRotator));

        if (ck::Is_NOT_Valid(_Driver) || ck::Is_NOT_Valid(_Probe) || ck::Is_NOT_Valid(_Actor))
        {
            FinishFailure("the pose driver, the probe or the actor did not spawn");
            return;
        }

        _Actor.EntityBridge.Promise_OnEntityReady(FCk_Delegate_EntityBridge_OnEntityReady(this, n"OnActorEntityReady"));
    }

    UFUNCTION()
    private void OnActorEntityReady(AActor InActor, FCk_Handle InEntity)
    {
        _ActorEntity = InEntity;
        // The helper anchors its entity outside the runner's teardown subtree.
        Track_ForCleanup(_ActorEntity);
    }

    UFUNCTION()
    private void Check_Ready(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Driver.Get_HasBone() && ck::IsValid(_ActorEntity));
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

        auto ActorTransform = _ActorEntity.As_Transform();
        if (ck::Is_NOT_Valid(ActorTransform))
        {
            FinishFailure("the actor's entity has no Transform");
            return;
        }

        auto NodeTransform = _Node.As_Transform();
        utils_scene_node::Add(ActorTransform, NodeTransform, k_ActorOffset);

        _Driver.IsDriving = true;
    }

    UFUNCTION()
    private void Step_StartSampling(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto ActorCheck = FCk_AutoTest_SceneNode_LatePose_Check();
        ActorCheck.Name = "actor-backed entity";
        ActorCheck.Node = _ActorEntity.As_Transform();
        ActorCheck.FromSocket = k_ActorOffset * k_NodeOffset;
        ActorCheck.Component = _Actor.GetRootComponent();

        _Probe.Driver = _Driver;
        _Probe.Checks.Add(ActorCheck);
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
    private void Step_AssertAndDestroy(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Probe.IsSampling = false;
        _Probe.Checks.Empty();

        Log(f"[AnimatedSocketAnchor] actor under a late anchor: samples={_Probe.Samples} maxLocationError={_Probe.MaxLocationErrorCm :.4}cm maxRotationError={_Probe.MaxRotationErrorDeg :.4}deg worst={_Probe.WorstCheck} socketTravel={_Probe.MaxSocketTravelCm :.2}cm");

        Assert_True(_Probe.MaxSocketTravelCm >= 10.0,
            f"the pose moved hand_r at least 10 cm a frame (max {_Probe.MaxSocketTravelCm :.2} cm)");
        Assert_True(_Probe.MaxLocationErrorCm <= k_LocationToleranceCm,
            f"the actor sat on this frame's hand_r within {k_LocationToleranceCm} cm (max {_Probe.MaxLocationErrorCm :.4} cm, {_Probe.WorstCheck})");
        Assert_True(_Probe.MaxRotationErrorDeg <= k_RotationToleranceDeg,
            f"the actor faced this frame's hand_r within {k_RotationToleranceDeg} deg (max {_Probe.MaxRotationErrorDeg :.4} deg, {_Probe.WorstCheck})");

        _EnsuresBefore = utils_ensure::Get_EnsureCount();
        _FramesMark = _Probe.Frames;
        _Driver.DestroyActorOnNextTick = _Actor;
    }

    UFUNCTION()
    private void Check_FramesAfterDestroy(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Probe.Frames - _FramesMark >= k_FramesAfterDestroy);
    }

    UFUNCTION()
    private void Step_AssertDestroyed(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Driver.IsDriving = false;

        Assert_False(ck::IsValid(_Actor), "the actor was destroyed between the passes");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0,
            "no ensure fired when the late pass met the destroyed actor's entity");

        _Probe.DestroyActor();
        _Driver.DestroyActor();
    }
}
