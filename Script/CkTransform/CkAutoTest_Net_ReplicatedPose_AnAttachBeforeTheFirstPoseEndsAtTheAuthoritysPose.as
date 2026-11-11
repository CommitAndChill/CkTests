// Language=angelscript

//============================================================================
// CK TRANSFORM - NET AUTOMATION TEST: an attach before the first pose ends at the authority's pose
//============================================================================
//
// The server spawns a declared mover at rest at pose A and never moves it. A machine without authority composes its
// copy at the origin and parents it to a local anchor in DoConstruct, before the first replicated pose applies, so the
// only pose it ever receives arrives while it is parent-driven. The client finds it already parented, moves the anchor
// 2 m, detaches after 0.5 s: within 0.5 s the copy is at A, and no ensure fired. A red means the first application was
// lost while parented.
//
// Surface: Ck.Transform.Net.AS_ReplicatedPose_AnAttachBeforeTheFirstPoseEndsAtTheAuthoritysPose
//============================================================================

class UCk_AutoTest_Net_ReplicatedPose_AnAttachBeforeTheFirstPoseEndsAtTheAuthoritysPose : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 20.0f;

    private const int32 k_MoverId = 4301;
    private const FVector k_PoseA = FVector(150.0, -250.0, 75.0);
    private const FVector k_AnchorMove = FVector(200.0, 0.0, 0.0);
    private const float64 k_RestToleranceCm = 1.0;
    private const float32 k_DetachDelaySeconds = 0.5f;
    private const float32 k_ConvergeSeconds = 0.5f;
    private const float32 k_ServerHoldSeconds = 8.0f;
    private const int32 k_PollBudget = 20000;

    private FCk_Handle_Transform _Mover;
    private FCk_Handle_Transform _Anchor;
    private int32 _EnsuresBefore = 0;
    private float64 _DetachSeconds = 0.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { FinishFailure("subject entity not found"); return; }

        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        if (utils_net::Get_HasAuthority(Subject) == false)
        {
            WaitUntil(n"Is_ClientFound", n"OnClientFound", k_PollBudget);
            return;
        }

        auto Params = FCk_TESTONLY_ReplicatedPose_MoverParams(k_MoverId, ECk_Transform_ReplicatedPosePolicy::YieldsToSceneNodeParent,
            ECk_TESTONLY_ReplicatedPose_ConstructAttach::WithoutAuthority, k_PoseA);
        utils_entity_script::Request_SpawnEntity(Subject, UCk_TESTONLY_ReplicatedPose_Mover_EntityScript, Params);
        WaitUntil(n"Is_ServerSpawned", n"OnServerSpawned", k_PollBudget);
    }

    //------------------------------------------------------------------------
    // Server
    //------------------------------------------------------------------------

    UFUNCTION()
    private void Is_ServerSpawned(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Mover = utils_replicated_pose_test::Find(Get_SubjectEntity(), k_MoverId);
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Mover));
    }

    UFUNCTION()
    private void OnServerSpawned(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        Track_ForCleanup(_Mover);
        Log(f"[ReplicatedPoseTest-Server] stage=1 spawned at rest pose={utils_transform::Get_EntityCurrentLocation(_Mover)} parented={utils_scene_node::Has(_Mover)}");
        Assert_True(utils_scene_node::Has(_Mover) == false, "server: the authority's copy is not parented");
        WaitSeconds(k_ServerHoldSeconds, n"OnServerHeld");
    }

    UFUNCTION()
    private void OnServerHeld(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        FinishSuccess();
    }

    //------------------------------------------------------------------------
    // Client
    //------------------------------------------------------------------------

    UFUNCTION()
    private void Is_ClientFound(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Mover = utils_replicated_pose_test::Find(Get_SubjectEntity(), k_MoverId);
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Mover));
    }

    UFUNCTION()
    private void OnClientFound(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        _Anchor = utils_replicated_pose_test::Get_ConstructAnchor(_Mover);
        const auto Parented = utils_scene_node::Has(_Mover);
        Log(f"[ReplicatedPoseTest-Client] stage=1 found parented={Parented} pose={utils_transform::Get_EntityCurrentLocation(_Mover)} newEnsures={utils_ensure::Get_EnsureCount() - _EnsuresBefore}");

        if (Parented == false || ck::Is_NOT_Valid(_Anchor))
        { FinishFailure("client: the mover was not parented in DoConstruct"); return; }

        auto Request = FCk_Request_Transform_SetLocation(utils_transform::Get_EntityCurrentLocation(_Anchor) + k_AnchorMove);
        Request.Set_LocalWorld(ECk_LocalWorld::World);
        utils_transform::Request_SetLocation(_Anchor, Request);
        WaitSeconds(k_DetachDelaySeconds, n"OnClientAnchorMoved");
    }

    UFUNCTION()
    private void OnClientAnchorMoved(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        const auto PoseOnAnchor = utils_transform::Get_EntityCurrentLocation(_Mover);
        Log(f"[ReplicatedPoseTest-Client] stage=2 detaching pose={PoseOnAnchor} anchor={utils_transform::Get_EntityCurrentLocation(_Anchor)}");
        Assert_True(PoseOnAnchor.Equals(utils_transform::Get_EntityCurrentLocation(_Anchor), k_RestToleranceCm), "client: the parented copy followed its anchor");

        utils_scene_node::Request_Detach(_Mover.As_SceneNode());
        _DetachSeconds = float64(System::GetGameTimeInSeconds());
        WaitUntil(n"Is_ClientAtPoseA", n"OnClientAtPoseA", k_PollBudget, k_ConvergeSeconds);
    }

    UFUNCTION()
    private void Is_ClientAtPoseA(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_transform::Get_EntityCurrentLocation(_Mover).Equals(k_PoseA, k_RestToleranceCm));
    }

    UFUNCTION()
    private void OnClientAtPoseA(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        const auto NewEnsures = utils_ensure::Get_EnsureCount() - _EnsuresBefore;
        const auto Seconds = float64(System::GetGameTimeInSeconds()) - _DetachSeconds;
        Log(f"[ReplicatedPoseTest-Client] stage=3 at A pose={utils_transform::Get_EntityCurrentLocation(_Mover)} after={Seconds :.3}s newEnsures={NewEnsures} ensureCount={utils_ensure::Get_EnsureCount()}");
        Assert_Equals_Int(NewEnsures, 0, "client: no ensure fired across the construct-time attach and the detach");
        FinishSuccess();
    }
}
