// Language=angelscript

//============================================================================
// CK TRANSFORM - NET AUTOMATION TEST: a declared replicated pose yields to a local parent, then converges
//============================================================================
//
// One declared mover's life on a machine without authority, in stages the server announces through the mover's
// replicated report, with an undeclared mover moving alongside:
// 1. The server moves the declared mover every frame; the client parents it to a local anchor moving on its own path:
//    every frame its pose is the offset composed with the anchor, and no ensure fires.
// 2. The server keeps moving; the client detaches mid-stream: within 10 frames the mover is on the server's latest
//    pose and keeps tracking it.
// 3. The server stops and reports the rest pose; the client re-parents, moves the anchor away, detaches 0.5 s later:
//    within 0.5 s the mover is at the rest pose (location and rotation), with no ensure.
// 4. The server moves again; the client detaches and re-adds in one call, 50 times: the mover stays on the anchor and
//    no ensure fires.
// 5. Both machines issue one gameplay Request_SetLocation on a declared mover they parented: the drain still rejects it,
//    the pose is unchanged and the process counts exactly one new ensure per world.
// Throughout, the undeclared mover is never parented and tracks the server's pose.
// Ensures are counted with utils_ensure::Get_EnsureCount(), which is process-wide: both worlds read the same number.
// One line per stage is logged on each machine ([ReplicatedPoseTest-Server] / [ReplicatedPoseTest-Client]).
//
// Surface: Ck.Transform.Net.AS_ReplicatedPose_YieldsToALocalParentThenConverges
//============================================================================

class UCk_AutoTest_Net_ReplicatedPose_YieldsToALocalParentThenConverges : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 25.0f;

    private const int32 k_DeclaredId = 4101;
    private const int32 k_UndeclaredId = 4102;
    private const int32 k_RejectedId = 4103;

    private const FVector k_DeclaredStart = FVector(0.0, 0.0, 200.0);
    private const FVector k_UndeclaredStart = FVector(0.0, 600.0, 200.0);
    private const FVector k_RejectedStart = FVector(0.0, -600.0, 200.0);
    private const float64 k_StepCm = 2.0;
    private const float64 k_StepYawDegrees = 1.0;
    // The report and the pose may arrive one update apart.
    private const float64 k_TrackToleranceCm = 2.0 * 2.0 + 0.5;
    private const float64 k_ComposeToleranceCm = 0.1;
    private const float64 k_RestToleranceCm = 1.0;
    private const float64 k_RestToleranceDegrees = 1.0;

    private const float32 k_MoveSeconds = 2.0f;
    private const float32 k_RestHoldSeconds = 3.0f;
    private const float32 k_ReMoveSeconds = 4.0f;
    private const float32 k_RejectSettleSeconds = 1.5f;
    private const float32 k_ServerHoldSeconds = 2.0f;
    private const float32 k_DetachDelaySeconds = 0.5f;
    private const float32 k_ConvergeSeconds = 0.5f;
    private const float32 k_ClientRejectSettleSeconds = 1.0f;
    private const int32 k_ReAttachCycles = 50;
    private const int32 k_DetachConvergeFrames = 10;
    private const int32 k_PollBudget = 20000;

    private const FVector k_AnchorStart = FVector(0.0, 0.0, 400.0);
    private const FVector k_AnchorStep = FVector(0.0, 1.5, 0.0);
    private const FVector k_AnchorAway = FVector(0.0, 0.0, 300.0);
    private const FVector k_RejectedTarget = FVector(5000.0, 5000.0, 5000.0);

    private FCk_Handle_Transform _Declared;
    private FCk_Handle_Transform _Undeclared;
    private FCk_Handle_Transform _Rejected;
    private FCk_Handle_Transform _Anchor;
    private FCk_Handle_SceneNode _DeclaredNode;
    private FTransform _Offset = FTransform(FVector(10.0, 0.0, 0.0));

    private int32 _Stage = 0;
    private int64 _LastFrame = -1;
    private int64 _StageStartFrame = 0;
    private float64 _StageStartSeconds = 0.0;
    private int32 _MoveIndex = 0;
    private int32 _StageEnsures = 0;
    private int32 _RejectEnsures = 0;
    private int32 _StageFrames = 0;
    private float64 _StageMaxDeviation = 0.0;
    private bool _DetachedAtRest = false;
    private float64 _DetachSeconds = 0.0;
    private FVector _PoseBeforeReject = FVector::ZeroVector;
    private ECk_Request_OperationResult _RejectResult = ECk_Request_OperationResult::Failed_NotEnqueued;
    private bool _RejectCompleted = false;

    // The undeclared mover, checked on every client frame (T8).
    private int32 _UndeclaredFrames = 0;
    private float64 _UndeclaredMaxDeviation = 0.0;

    private bool _IsServer = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { FinishFailure("subject entity not found"); return; }

        _IsServer = utils_net::Get_HasAuthority(Subject);

        if (_IsServer == false)
        {
            WaitUntil(n"Is_ClientDone", n"OnClientDone", k_PollBudget);
            return;
        }

        Spawn(Subject, k_DeclaredId, ECk_Transform_ReplicatedPosePolicy::YieldsToSceneNodeParent, k_DeclaredStart);
        Spawn(Subject, k_UndeclaredId, ECk_Transform_ReplicatedPosePolicy::AlwaysApplied, k_UndeclaredStart);
        Spawn(Subject, k_RejectedId, ECk_Transform_ReplicatedPosePolicy::YieldsToSceneNodeParent, k_RejectedStart);
        WaitUntil(n"Is_ServerDone", n"OnServerDone", k_PollBudget);
    }

    private void Spawn(FCk_Handle& InOwner, int32 InId, ECk_Transform_ReplicatedPosePolicy InPolicy, FVector InStart)
    {
        auto Params = FCk_TESTONLY_ReplicatedPose_MoverParams(InId, InPolicy, ECk_TESTONLY_ReplicatedPose_ConstructAttach::None, InStart);
        utils_entity_script::Request_SpawnEntity(InOwner, UCk_TESTONLY_ReplicatedPose_Mover_EntityScript, Params);
    }

    // The first poll of each engine frame (a wait polls more than once per frame).
    private bool Get_IsNewFrame()
    {
        const auto Frame = utils_stats::Get_FrameCount();
        if (Frame == _LastFrame)
        { return false; }

        _LastFrame = Frame;
        return true;
    }

    private float64 Get_Now()
    {
        return float64(System::GetGameTimeInSeconds());
    }

    private void Start_Stage(int32 InStage)
    {
        _Stage = InStage;
        _StageStartFrame = utils_stats::Get_FrameCount();
        _StageStartSeconds = Get_Now();
        _StageEnsures = utils_ensure::Get_EnsureCount();
        _StageFrames = 0;
        _StageMaxDeviation = 0.0;
        _DetachedAtRest = false;
        _DetachSeconds = 0.0;
    }

    private int32 Get_NewEnsures()
    {
        return utils_ensure::Get_EnsureCount() - _StageEnsures;
    }

    private void Log_Stage(const FString& InWhat)
    {
        const auto Machine = _IsServer ? "Server" : "Client";
        const auto NewEnsures = Get_NewEnsures();
        Log(f"[ReplicatedPoseTest-{Machine}] stage={_Stage} {InWhat} frames={_StageFrames} maxDev={_StageMaxDeviation :.3} newEnsures={NewEnsures} ensureCount={utils_ensure::Get_EnsureCount()}");
    }

    //------------------------------------------------------------------------
    // Server
    //------------------------------------------------------------------------

    private void Move(FCk_Handle_Transform& InMover, FVector InStart)
    {
        const auto Location = InStart + FVector(k_StepCm * _MoveIndex, 0.0, 0.0);
        const auto Rotation = FRotator(0.0, k_StepYawDegrees * _MoveIndex, 0.0);
        utils_replicated_pose_test::Request_Move(InMover, Location, Rotation);
    }

    UFUNCTION()
    private void Is_ServerDone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(false);

        if (Get_IsNewFrame() == false || IsFinished())
        { return; }

        auto Subject = Get_SubjectEntity();

        if (_Stage == 0)
        {
            _Declared = utils_replicated_pose_test::Find(Subject, k_DeclaredId);
            _Undeclared = utils_replicated_pose_test::Find(Subject, k_UndeclaredId);
            _Rejected = utils_replicated_pose_test::Find(Subject, k_RejectedId);
            if (ck::Is_NOT_Valid(_Declared) || ck::Is_NOT_Valid(_Undeclared) || ck::Is_NOT_Valid(_Rejected))
            { return; }

            Track_ForCleanup(_Declared);
            Track_ForCleanup(_Undeclared);
            Track_ForCleanup(_Rejected);
            Start_Stage(1);
            utils_replicated_pose_test::Request_SetStage(_Declared, 1);
            Log_Stage("moving");
            return;
        }

        const auto Elapsed = Get_Now() - _StageStartSeconds;
        _StageFrames += 1;

        if (_Stage == 1 || _Stage == 2 || _Stage == 4)
        {
            _MoveIndex += 1;
            Move(_Declared, k_DeclaredStart);
            Move(_Undeclared, k_UndeclaredStart);
        }
        else if (_Stage == 3)
        {
            _MoveIndex += 1;
            Move(_Undeclared, k_UndeclaredStart);
        }

        if (_Stage == 1 && Elapsed >= k_MoveSeconds)
        {
            Start_Stage(2);
            utils_replicated_pose_test::Request_SetStage(_Declared, 2);
            Log_Stage("moving");
            return;
        }

        if (_Stage == 2 && Elapsed >= k_MoveSeconds)
        {
            Start_Stage(3);
            utils_replicated_pose_test::Request_Rest(_Declared, 3);
            const auto Report = utils_replicated_pose_test::Get_Report(_Declared);
            Log_Stage(f"rest={Report.RestLocation} restRotation={Report.RestRotation}");
            return;
        }

        if (_Stage == 3 && Elapsed >= k_RestHoldSeconds)
        {
            Start_Stage(4);
            // Parented a stage ahead of its write, so the pose read before the write is already the composed one.
            Attach_ForRejection(_Rejected);
            utils_replicated_pose_test::Request_SetStage(_Declared, 4);
            Log_Stage("moving");
            return;
        }

        if (_Stage == 4 && Elapsed >= k_ReMoveSeconds)
        {
            Start_Stage(5);
            _RejectEnsures = utils_ensure::Get_EnsureCount();
            Do_RequestRejected(_Rejected, n"OnServerRejectCompleted");
            utils_replicated_pose_test::Request_SetStage(_Declared, 5);
            Log_Stage("rejected write issued");
            return;
        }

        if (_Stage == 5 && Elapsed >= k_RejectSettleSeconds)
        { Result.Set(true); }
    }

    private void Attach_ForRejection(FCk_Handle_Transform& InMover)
    {
        auto Owner = Get_SubjectEntity();
        _Anchor = utils_replicated_pose_test::Create_Anchor(Owner, k_AnchorStart);
        Track_ForCleanup(_Anchor);
        utils_scene_node::Add(InMover, _Anchor, _Offset);
    }

    // One gameplay world write on InMover, which this machine parented earlier.
    private void Do_RequestRejected(FCk_Handle_Transform& InMover, FName InCompletion)
    {
        _PoseBeforeReject = utils_transform::Get_EntityCurrentLocation(InMover);
        auto Request = FCk_Request_Transform_SetLocation(k_RejectedTarget);
        Request.Set_LocalWorld(ECk_LocalWorld::World);
        utils_transform::Request_SetLocation(InMover, Request, FCk_Delegate_Request_OnCompleted(this, InCompletion));
    }

    UFUNCTION()
    private void OnServerRejectCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _RejectResult = InResult;
        _RejectCompleted = true;
    }

    UFUNCTION()
    private void OnClientRejectCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _RejectResult = InResult;
        _RejectCompleted = true;
    }

    UFUNCTION()
    private void OnServerDone(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        Assert_RejectedWrite(_Rejected);
        WaitSeconds(k_ServerHoldSeconds, n"OnServerHeld");
    }

    private void Assert_RejectedWrite(const FCk_Handle_Transform& InMover)
    {
        const auto PoseAfter = utils_transform::Get_EntityCurrentLocation(InMover);
        const auto NewEnsures = utils_ensure::Get_EnsureCount() - _RejectEnsures;
        const auto Machine = _IsServer ? "Server" : "Client";
        Log(f"[ReplicatedPoseTest-{Machine}] stage=5 completed={_RejectCompleted} result={_RejectResult} poseBefore={_PoseBeforeReject} poseAfter={PoseAfter} newEnsures={NewEnsures} ensureCount={utils_ensure::Get_EnsureCount()}");

        Assert_True(_RejectCompleted, f"{Machine} stage 5: the gameplay write on a parented declared mover completed");
        Assert_True(_RejectResult == ECk_Request_OperationResult::Failed, f"{Machine} stage 5: the drain rejected it (result {_RejectResult})");
        Assert_True(PoseAfter.Equals(_PoseBeforeReject, k_ComposeToleranceCm), f"{Machine} stage 5: the pose is unchanged ({_PoseBeforeReject} -> {PoseAfter})");
        Assert_Equals_Int(NewEnsures, 2, f"{Machine} stage 5: exactly one rejection ensure per world");
    }

    UFUNCTION()
    private void OnServerHeld(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        FinishSuccess();
    }

    //------------------------------------------------------------------------
    // Client
    //------------------------------------------------------------------------

    private int32 Get_ServerStage()
    {
        return utils_replicated_pose_test::Get_Report(_Declared).Stage;
    }

    private float64 Get_ComposeDeviation()
    {
        const auto Expected = utils_replicated_pose_test::Get_ComposedLocation(_Anchor, _Offset);
        return (utils_transform::Get_EntityCurrentLocation(_Declared) - Expected).Size();
    }

    private void Attach_Declared()
    {
        _DeclaredNode = utils_scene_node::Add(_Declared, _Anchor, _Offset);
    }

    private void Move_Anchor(FVector InLocation)
    {
        auto Request = FCk_Request_Transform_SetLocation(InLocation);
        Request.Set_LocalWorld(ECk_LocalWorld::World);
        utils_transform::Request_SetLocation(_Anchor, Request);
    }

    // T8: the undeclared mover follows the server's pose whatever the declared one does.
    private void Check_Undeclared()
    {
        const auto Report = utils_replicated_pose_test::Get_Report(_Undeclared);
        if (Report.MoveCount < 2)
        { return; }

        _UndeclaredFrames += 1;
        const auto Deviation = (utils_transform::Get_EntityCurrentLocation(_Undeclared) - Report.LatestLocation).Size();
        if (Deviation > _UndeclaredMaxDeviation)
        { _UndeclaredMaxDeviation = Deviation; }
    }

    private void Fail_Stage(const FString& InWhat)
    {
        Log_Stage(f"FAILED: {InWhat}");
        FinishFailure(f"client stage {_Stage}: {InWhat}");
    }

    UFUNCTION()
    private void Is_ClientDone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(false);

        if (Get_IsNewFrame() == false || IsFinished())
        { return; }

        auto Subject = Get_SubjectEntity();

        if (_Stage == 0)
        {
            _Declared = utils_replicated_pose_test::Find(Subject, k_DeclaredId);
            _Undeclared = utils_replicated_pose_test::Find(Subject, k_UndeclaredId);
            if (ck::Is_NOT_Valid(_Declared) || ck::Is_NOT_Valid(_Undeclared) || Get_ServerStage() < 1)
            { return; }

            auto Owner = Get_SubjectEntity();
            _Anchor = utils_replicated_pose_test::Create_Anchor(Owner, k_AnchorStart);
            Track_ForCleanup(_Anchor);
            Start_Stage(1);
            Attach_Declared();
            Log_Stage("attached");
            return;
        }

        Check_Undeclared();
        const auto ServerStage = Get_ServerStage();
        const auto FramesInStage = utils_stats::Get_FrameCount() - _StageStartFrame;

        // T1: parented while the server streams.
        if (_Stage == 1)
        {
            if (ServerStage == 1)
            {
                if (FramesInStage >= 2)
                {
                    _StageFrames += 1;
                    _StageMaxDeviation = Math::Max(_StageMaxDeviation, Get_ComposeDeviation());
                }

                Move_Anchor(k_AnchorStart + k_AnchorStep * float64(FramesInStage));
                return;
            }

            Log_Stage("yielded");
            if (_StageFrames < 10 || _StageMaxDeviation > k_ComposeToleranceCm || Get_NewEnsures() != 0)
            { Fail_Stage("a parented declared mover left its anchor or raised an ensure while the server moved it"); return; }

            Start_Stage(2);
            utils_scene_node::Request_Detach(_DeclaredNode);
            Log_Stage("detached mid-stream");
            return;
        }

        // T3: detached mid-stream, it converges and tracks.
        if (_Stage == 2)
        {
            if (ServerStage == 2)
            {
                if (FramesInStage > k_DetachConvergeFrames)
                {
                    _StageFrames += 1;
                    const auto Latest = utils_replicated_pose_test::Get_Report(_Declared).LatestLocation;
                    _StageMaxDeviation = Math::Max(_StageMaxDeviation, (utils_transform::Get_EntityCurrentLocation(_Declared) - Latest).Size());
                }
                return;
            }

            Log_Stage("tracked");
            if (_StageFrames < 10 || _StageMaxDeviation > k_TrackToleranceCm)
            { Fail_Stage("after a detach mid-stream the mover did not take and track the server's latest pose"); return; }

            Start_Stage(3);
            Attach_Declared();
            Move_Anchor(k_AnchorStart + k_AnchorAway);
            Log_Stage("re-parented at rest");
            return;
        }

        // T2: re-parented at rest, detached 0.5 s later, it converges to the rest pose.
        if (_Stage == 3)
        {
            const auto Report = utils_replicated_pose_test::Get_Report(_Declared);
            const auto Elapsed = Get_Now() - _StageStartSeconds;

            if (_DetachedAtRest == false)
            {
                if (Elapsed < k_DetachDelaySeconds)
                { return; }

                utils_scene_node::Request_Detach(_DeclaredNode);
                _DetachedAtRest = true;
                _DetachSeconds = Get_Now();
                Log_Stage(f"detached at rest rest={Report.RestLocation} pose={utils_transform::Get_EntityCurrentLocation(_Declared)}");
                return;
            }

            const auto Pose = utils_transform::Get_EntityCurrentLocation(_Declared);
            const auto Rotation = utils_transform::Get_EntityCurrentRotation(_Declared);
            const auto AtRest = Pose.Equals(Report.RestLocation, k_RestToleranceCm) && Rotation.Equals(Report.RestRotation, k_RestToleranceDegrees);
            _StageMaxDeviation = (Pose - Report.RestLocation).Size();

            if (AtRest == false)
            {
                if (Get_Now() - _DetachSeconds > k_ConvergeSeconds)
                { Fail_Stage(f"0.5 s after a detach at rest the mover is at {Pose} {Rotation}, not the rest pose {Report.RestLocation} {Report.RestRotation}"); }
                return;
            }

            Log_Stage(f"converged at rest pose={Pose} rotation={Rotation}");
            if (Get_NewEnsures() != 0)
            { Fail_Stage("an ensure fired across the re-parent and detach at rest"); return; }

            // Wait for the server to move again before re-attaching.
            _Stage = 31;
            return;
        }

        if (_Stage == 31)
        {
            if (ServerStage < 4)
            { return; }

            Start_Stage(4);
            Attach_Declared();
            Log_Stage("attached for re-parents");
            return;
        }

        // T4: detach and re-add in one call, while the server streams.
        if (_Stage == 4)
        {
            if (ServerStage != 4)
            { Fail_Stage(f"the server left stage 4 after {_StageFrames} of {k_ReAttachCycles} re-parents"); return; }

            if (FramesInStage < 2)
            { return; }

            _StageMaxDeviation = Math::Max(_StageMaxDeviation, Get_ComposeDeviation());
            utils_scene_node::Request_Detach(_DeclaredNode);
            Attach_Declared();
            _StageFrames += 1;

            if (_StageFrames < k_ReAttachCycles)
            { return; }

            Log_Stage("re-parented");
            if (_StageMaxDeviation > k_ComposeToleranceCm || Get_NewEnsures() != 0)
            { Fail_Stage("a detach and re-add in one call moved the mover off its anchor or raised an ensure"); return; }

            // The baseline for stage 5 is taken before the server's write.
            _Stage = 41;
            _RejectEnsures = utils_ensure::Get_EnsureCount();
            return;
        }

        if (_Stage == 41)
        {
            if (ServerStage < 5)
            { return; }

            const auto RejectEnsures = _RejectEnsures;
            Start_Stage(5);
            _RejectEnsures = RejectEnsures;
            _PoseBeforeReject = utils_transform::Get_EntityCurrentLocation(_Declared);
            auto Request = FCk_Request_Transform_SetLocation(k_RejectedTarget);
            Request.Set_LocalWorld(ECk_LocalWorld::World);
            utils_transform::Request_SetLocation(_Declared, Request, FCk_Delegate_Request_OnCompleted(this, n"OnClientRejectCompleted"));
            Log_Stage("rejected write issued");
            return;
        }

        if (_Stage == 5)
        {
            if (Get_Now() - _StageStartSeconds >= k_ClientRejectSettleSeconds)
            { Result.Set(true); }
        }
    }

    UFUNCTION()
    private void OnClientDone(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        Assert_RejectedWrite(_Declared);

        Log(f"[ReplicatedPoseTest-Client] undeclared frames={_UndeclaredFrames} maxDev={_UndeclaredMaxDeviation :.3}");
        Assert_True(_UndeclaredFrames > 30, f"client: the undeclared mover was checked ({_UndeclaredFrames} frames)");
        Assert_True(_UndeclaredMaxDeviation <= k_TrackToleranceCm,
            f"client: the undeclared mover tracked the server's pose (max deviation {_UndeclaredMaxDeviation :.3} cm)");
        FinishSuccess();
    }
}
