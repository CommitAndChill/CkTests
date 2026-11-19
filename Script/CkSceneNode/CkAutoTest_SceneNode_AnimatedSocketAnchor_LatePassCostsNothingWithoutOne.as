// Language=angelscript

//============================================================================
// CK SCENE NODE - AUTOMATION TEST: THE LATE-POSE PASS COSTS NOTHING WITHOUT A LATE ANCHOR
//============================================================================
//
// The late-pose pass (FGroup_Transform_LatePose, its own TG_PostUpdateWork
// partition) must be free in a world with no late anchor: the scheduler's
// empty-view skip bypasses each of its processors and none visits an entity.
// With one late anchor its re-sample visits exactly that one node.
//
// The world is shared with every other autotest, so the first wait is for a
// frame in which the pass was skipped whole: a node another test destroyed
// keeps its tags until it is finalized, which keeps the view non-empty. The
// scheduler re-derives its empty-view verdict only when an include type of the
// view is added or removed somewhere, and destroying an entity is not such a
// mutation, so each poll adds one bare Transform entity: that re-derives the
// verdict, and it holds no late anchor. (A stale verdict is only ever the safe
// one: it can keep a pass running over an empty view, never skip a non-empty one.)
// The second half counts the late re-sample's visits against the frame before
// the node exists, so it holds even while another row's late anchor is still
// being finalized.
//
// See CkAutoTest_SceneNode_AnimatedSocketAnchor_Common.as for the rig.
//============================================================================

class UCk_AutoTest_SceneNode_AnimatedSocketAnchor_LatePassCostsNothingWithoutOne : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private const FString k_LateFollow = "FProcessor_SceneNode_LateFollowUnrealAnchor";
    private const FString k_SyncToActorLate = "FProcessor_Transform_SyncToActor_Late";
    private const FString k_PushTransformLate = "FProcessor_UnrealComponent_PushTransform_Late";

    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_PoseDriver _Driver;
    private ACk_AutoTest_SceneNode_AnimatedSocketAnchor_Probe _Probe;
    private FCk_Handle_SceneNode _Node;
    private int32 _FramesAtCompose = 0;
    private int32 _VisitsBeforeCompose = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the pose driver and a frame counter", n"Step_Spawn");
        Add_Step_WaitUntil("the driver's mesh has hand_r", n"Check_HasBone");
        Add_Step_WaitUntil("a frame with no late anchor anywhere skips the late-pose pass", n"Check_LatePassSkipped");
        Add_Step("every late processor was skipped, none visited an entity", n"Step_AssertSkipped");
        Add_Step("anchor one node to hand_r", n"Step_Compose");
        Add_Step_WaitUntil("a frame has passed since", n"Check_FramePassed");
        Add_Step("the late re-sample visited that one node", n"Step_AssertOneVisit");
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
    private void Check_LatePassSkipped(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto ScratchEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        utils_transform::Add(ScratchEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        auto Result = OutResult;
        Result.Set(Get_WasSkipped(k_LateFollow) && Get_WasSkipped(k_SyncToActorLate) && Get_WasSkipped(k_PushTransformLate));
    }

    private bool Get_WasSkipped(const FString& InProcessor)
    {
        auto Visited = 0;
        auto Skipped = false;
        const auto Found = UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_LastFrameProcessorVisits(_Driver, InProcessor, Visited, Skipped);
        return Found && Skipped && Visited == 0;
    }

    UFUNCTION()
    private void Step_AssertSkipped(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        AssertSkipped(k_LateFollow);
        AssertSkipped(k_SyncToActorLate);
        AssertSkipped(k_PushTransformLate);
    }

    private void AssertSkipped(const FString& InProcessor)
    {
        auto Visited = 0;
        auto Skipped = false;
        const auto Found = UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_LastFrameProcessorVisits(_Driver, InProcessor, Visited, Skipped);

        Assert_True(Found, f"[{InProcessor}] is registered in a scheduler partition");
        Assert_True(Skipped, f"[{InProcessor}] was skipped by the empty-view check");
        Assert_True(Visited == 0, f"[{InProcessor}] visited no entity ({Visited})");
    }

    UFUNCTION()
    private void Step_Compose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Skipped = false;
        UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_LastFrameProcessorVisits(_Driver, k_LateFollow, _VisitsBeforeCompose, Skipped);

        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Root = utils_transform::Add(RootEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        _Node = utils_scene_node::CreateAndAttachToUnrealMesh(Root, _Driver.Mesh, ck_scene_node_late_pose::k_Bone, FTransform::Identity);
        if (ck::Is_NOT_Valid(_Node))
        { FinishFailure("CreateAndAttachToUnrealMesh returned an invalid handle"); }

        _FramesAtCompose = _Probe.Frames;
    }

    UFUNCTION()
    private void Check_FramePassed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Probe.Frames - _FramesAtCompose >= 2);
    }

    UFUNCTION()
    private void Step_AssertOneVisit(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Visited = 0;
        auto Skipped = false;
        const auto Found = UCk_Utils_AutoTest_SceneNodeLatePose_UE::Get_LastFrameProcessorVisits(_Driver, k_LateFollow, Visited, Skipped);

        Log(f"[AnimatedSocketAnchor] one late anchor: lateFollow visited={Visited} skipped={Skipped}");

        Assert_True(Found, "the late re-sample is registered");
        Assert_False(Skipped, "with a late anchor the late re-sample ran");
        Assert_True(Visited == _VisitsBeforeCompose + 1,
            f"it visited the one new late anchor ({Visited}, {_VisitsBeforeCompose} before it)");

        _Probe.DestroyActor();
        _Driver.DestroyActor();
    }
}
