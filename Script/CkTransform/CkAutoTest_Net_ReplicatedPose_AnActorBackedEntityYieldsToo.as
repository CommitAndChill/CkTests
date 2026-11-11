// Language=angelscript

//============================================================================
// CK TRANSFORM - NET AUTOMATION TEST: an actor-backed entity yields to a local parent too
//============================================================================
//
// The subject is actor-backed: its Transform is anchored on the actor's root component, so a scene-node parent makes it
// parent-driven through FTag_Transform_ExternallyDriven rather than through a bare node. Both worlds declare the policy
// on it. The server moves it every frame for 1 s, then rests and reports the rest pose; the client parents it to a
// local anchor: while the server moves it, its pose is the offset composed with the anchor; 0.5 s after the rest is
// reported the client detaches and within 0.5 s the subject is at the rest pose. No ensure may fire.
//
// Surface: Ck.Transform.Net.AS_ReplicatedPose_AnActorBackedEntityYieldsToo
//============================================================================

class UCk_AutoTest_Net_ReplicatedPose_AnActorBackedEntityYieldsToo : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 20.0f;

    private const float64 k_StepCm = 2.0;
    private const float32 k_MoveSeconds = 1.0f;
    private const float32 k_ServerHoldSeconds = 4.0f;
    private const float32 k_DetachDelaySeconds = 0.5f;
    private const float32 k_ConvergeSeconds = 0.5f;
    private const float64 k_ComposeToleranceCm = 0.1;
    private const float64 k_RestToleranceCm = 1.0;
    private const FVector k_AnchorAbove = FVector(0.0, 0.0, 200.0);
    private const FVector k_AnchorStep = FVector(0.0, 1.5, 0.0);
    private const int32 k_PollBudget = 20000;

    private FCk_Handle_Transform _Subject;
    private FCk_Handle_Transform _Anchor;
    private FCk_Handle_SceneNode _Node;
    private FTransform _Offset = FTransform(FVector(10.0, 0.0, 0.0));
    private FVector _Start = FVector::ZeroVector;
    private FVector _AnchorStart = FVector::ZeroVector;

    private bool _IsServer = false;
    private int64 _LastFrame = -1;
    private int64 _AttachFrame = 0;
    private float64 _StartSeconds = 0.0;
    private int32 _Moves = 0;
    private int32 _ComposeFrames = 0;
    private float64 _MaxDeviation = 0.0;
    private int32 _EnsuresBefore = 0;
    private float64 _DetachSeconds = 0.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { FinishFailure("subject entity not found"); return; }

        if (utils_transform::Has(Subject) == false)
        { FinishFailure("subject has no Transform"); return; }

        _IsServer = utils_net::Get_HasAuthority(Subject);
        _Subject = Subject.As_Transform();
        utils_transform::Request_SetReplicatedPosePolicy(_Subject, ECk_Transform_ReplicatedPosePolicy::YieldsToSceneNodeParent);

        if (Subject.Has_Fragment(FCk_TESTONLY_ReplicatedPose_Report) == false)
        { Subject.Add_Fragment(FCk_TESTONLY_ReplicatedPose_Report(), ECk_Replication::Replicates); }

        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        if (_IsServer == false)
        {
            WaitUntil(n"Is_ClientMoving", n"OnClientMoving", k_PollBudget);
            return;
        }

        _Start = utils_transform::Get_EntityCurrentLocation(_Subject);
        _StartSeconds = float64(System::GetGameTimeInSeconds());
        utils_replicated_pose_test::Request_SetStage(_Subject, 1);
        Log(f"[ReplicatedPoseTest-Server] stage=1 moving start={_Start}");
        WaitUntil(n"Is_ServerMoved", n"OnServerMoved", k_PollBudget);
    }

    private bool Get_IsNewFrame()
    {
        const auto Frame = utils_stats::Get_FrameCount();
        if (Frame == _LastFrame)
        { return false; }

        _LastFrame = Frame;
        return true;
    }

    //------------------------------------------------------------------------
    // Server
    //------------------------------------------------------------------------

    UFUNCTION()
    private void Is_ServerMoved(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(false);

        if (Get_IsNewFrame() == false)
        { return; }

        if (float64(System::GetGameTimeInSeconds()) - _StartSeconds >= k_MoveSeconds)
        {
            Result.Set(true);
            return;
        }

        _Moves += 1;
        utils_replicated_pose_test::Request_Move(_Subject, _Start + FVector(k_StepCm * _Moves, 0.0, 0.0), FRotator::ZeroRotator);
    }

    UFUNCTION()
    private void OnServerMoved(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        utils_replicated_pose_test::Request_Rest(_Subject, 2);
        const auto Report = utils_replicated_pose_test::Get_Report(_Subject);
        Log(f"[ReplicatedPoseTest-Server] stage=2 rest={Report.RestLocation} moves={_Moves}");
        WaitSeconds(k_ServerHoldSeconds, n"OnServerHeld");
    }

    UFUNCTION()
    private void OnServerHeld(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        utils_transform::Request_SetReplicatedPosePolicy(_Subject, ECk_Transform_ReplicatedPosePolicy::AlwaysApplied);
        FinishSuccess();
    }

    //------------------------------------------------------------------------
    // Client
    //------------------------------------------------------------------------

    UFUNCTION()
    private void Is_ClientMoving(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_replicated_pose_test::Get_Report(_Subject).Stage >= 1);
    }

    UFUNCTION()
    private void OnClientMoving(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Owner = Get_SubjectEntity();
        _AnchorStart = utils_transform::Get_EntityCurrentLocation(_Subject) + k_AnchorAbove;
        _Anchor = utils_replicated_pose_test::Create_Anchor(Owner, _AnchorStart);
        Track_ForCleanup(_Anchor);
        _Node = utils_scene_node::Add(_Subject, _Anchor, _Offset);
        _AttachFrame = utils_stats::Get_FrameCount();
        _LastFrame = _AttachFrame;
        Log(f"[ReplicatedPoseTest-Client] stage=1 attached anchor={_AnchorStart}");
        WaitUntil(n"Is_ClientRestReported", n"OnClientRestReported", k_PollBudget);
    }

    UFUNCTION()
    private void Is_ClientRestReported(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(false);

        if (Get_IsNewFrame() == false)
        { return; }

        if (utils_replicated_pose_test::Get_Report(_Subject).Stage >= 2)
        {
            Result.Set(true);
            return;
        }

        const auto FramesAttached = utils_stats::Get_FrameCount() - _AttachFrame;
        if (FramesAttached >= 2)
        {
            const auto Expected = utils_replicated_pose_test::Get_ComposedLocation(_Anchor, _Offset);
            _MaxDeviation = Math::Max(_MaxDeviation, (utils_transform::Get_EntityCurrentLocation(_Subject) - Expected).Size());
            _ComposeFrames += 1;
        }

        auto Request = FCk_Request_Transform_SetLocation(_AnchorStart + k_AnchorStep * float64(FramesAttached));
        Request.Set_LocalWorld(ECk_LocalWorld::World);
        utils_transform::Request_SetLocation(_Anchor, Request);
    }

    UFUNCTION()
    private void OnClientRestReported(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        Log(f"[ReplicatedPoseTest-Client] stage=2 rest reported composeFrames={_ComposeFrames} maxDev={_MaxDeviation :.3} newEnsures={utils_ensure::Get_EnsureCount() - _EnsuresBefore}");
        Assert_True(_ComposeFrames >= 10, f"client: the parented subject was checked while the server moved it ({_ComposeFrames} frames)");
        Assert_True(_MaxDeviation <= k_ComposeToleranceCm, f"client: the parented subject stayed on its anchor (max deviation {_MaxDeviation :.3} cm)");
        WaitSeconds(k_DetachDelaySeconds, n"OnClientDetachDue");
    }

    UFUNCTION()
    private void OnClientDetachDue(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        utils_scene_node::Request_Detach(_Node);
        _DetachSeconds = float64(System::GetGameTimeInSeconds());
        WaitUntil(n"Is_ClientAtRest", n"OnClientAtRest", k_PollBudget, k_ConvergeSeconds);
    }

    UFUNCTION()
    private void Is_ClientAtRest(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Rest = utils_replicated_pose_test::Get_Report(_Subject).RestLocation;
        Result.Set(utils_transform::Get_EntityCurrentLocation(_Subject).Equals(Rest, k_RestToleranceCm));
    }

    UFUNCTION()
    private void OnClientAtRest(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        const auto NewEnsures = utils_ensure::Get_EnsureCount() - _EnsuresBefore;
        const auto Seconds = float64(System::GetGameTimeInSeconds()) - _DetachSeconds;
        Log(f"[ReplicatedPoseTest-Client] stage=3 at rest pose={utils_transform::Get_EntityCurrentLocation(_Subject)} after={Seconds :.3}s newEnsures={NewEnsures} ensureCount={utils_ensure::Get_EnsureCount()}");
        Assert_Equals_Int(NewEnsures, 0, "client: no ensure fired while the actor-backed subject was parented and detached");
        utils_transform::Request_SetReplicatedPosePolicy(_Subject, ECk_Transform_ReplicatedPosePolicy::AlwaysApplied);
        FinishSuccess();
    }
}
