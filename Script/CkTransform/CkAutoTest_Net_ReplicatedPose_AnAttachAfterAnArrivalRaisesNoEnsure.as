// Language=angelscript

//============================================================================
// CK TRANSFORM - NET AUTOMATION TEST: an attach right after a replicated arrival raises no ensure
//============================================================================
//
// A pose that arrives on a machine without authority is handled after that frame's transform pass. An attach made
// later in the same frame (FGroup_Replication, before the tail pump) or early in the next one (FGroup_Gameplay_Script)
// must not meet a world write from that arrival. The server moves a declared mover one step every 5 frames and reports
// each move; the client runs 50 cycles of: stamp an attach order, which one of the two processors in
// CkTransform_TESTONLY_ReplicatedPose.as executes in the first frame its copy of the report shows a newer move (odd
// cycles in FGroup_Replication, even cycles in FGroup_Gameplay_Script); hold 3 frames on the anchor; detach. No ensure
// may fire.
// Control: the same FGroup_Replication attach on an undeclared mover the server moves every frame must raise the
// parent-driven rejection ensure at least once, so the site is proven to reach the drain.
// Ensures are counted with utils_ensure::Get_EnsureCount(), which is process-wide.
//
// Surface: Ck.Transform.Net.AS_ReplicatedPose_AnAttachAfterAnArrivalRaisesNoEnsure
//============================================================================

class UCk_AutoTest_Net_ReplicatedPose_AnAttachAfterAnArrivalRaisesNoEnsure : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 25.0f;

    private const int32 k_DeclaredId = 4201;
    private const int32 k_UndeclaredId = 4202;

    private const FVector k_DeclaredStart = FVector(0.0, 0.0, 200.0);
    private const FVector k_UndeclaredStart = FVector(0.0, 600.0, 200.0);
    private const FVector k_AnchorLocation = FVector(0.0, 0.0, 500.0);
    private const float64 k_StepCm = 2.0;
    private const int32 k_DeclaredMoveEveryFrames = 5;
    private const float32 k_ServerMoveSeconds = 15.0f;
    private const float32 k_ServerHoldSeconds = 2.0f;

    private const int32 k_Cycles = 50;
    private const int32 k_HoldFrames = 3;
    private const float64 k_ComposeToleranceCm = 0.1;
    private const int32 k_PollBudget = 20000;

    private FCk_Handle_Transform _Declared;
    private FCk_Handle_Transform _Undeclared;
    private FCk_Handle_Transform _Anchor;

    private bool _IsServer = false;
    private bool _Started = false;
    private int64 _LastFrame = -1;
    private float64 _StartSeconds = 0.0;
    private int32 _ServerFrames = 0;
    private int32 _DeclaredMoves = 0;
    private int32 _UndeclaredMoves = 0;

    // Client: the cycle in progress, 1-based; cycles above k_Cycles are the control.
    private int32 _Cycle = 0;
    private bool _OrderPending = false;
    private int64 _AttachFrame = 0;
    private int32 _HeldFrames = 0;
    private float64 _MaxDeviation = 0.0;
    private int32 _EnsuresBefore = 0;
    private int32 _CycleEnsures = 0;
    private int32 _ReplicationCycles = 0;
    private int32 _GameplayScriptCycles = 0;
    private int32 _ControlEnsures = 0;
    private int32 _DeclaredEnsures = -1;

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
        WaitUntil(n"Is_ServerDone", n"OnServerDone", k_PollBudget);
    }

    private void Spawn(FCk_Handle& InOwner, int32 InId, ECk_Transform_ReplicatedPosePolicy InPolicy, FVector InStart)
    {
        auto Params = FCk_TESTONLY_ReplicatedPose_MoverParams(InId, InPolicy, ECk_TESTONLY_ReplicatedPose_ConstructAttach::None, InStart);
        utils_entity_script::Request_SpawnEntity(InOwner, UCk_TESTONLY_ReplicatedPose_Mover_EntityScript, Params);
    }

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

    //------------------------------------------------------------------------
    // Server
    //------------------------------------------------------------------------

    UFUNCTION()
    private void Is_ServerDone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(false);

        if (Get_IsNewFrame() == false || IsFinished())
        { return; }

        if (_Started == false)
        {
            auto Subject = Get_SubjectEntity();
            _Declared = utils_replicated_pose_test::Find(Subject, k_DeclaredId);
            _Undeclared = utils_replicated_pose_test::Find(Subject, k_UndeclaredId);
            if (ck::Is_NOT_Valid(_Declared) || ck::Is_NOT_Valid(_Undeclared))
            { return; }

            Track_ForCleanup(_Declared);
            Track_ForCleanup(_Undeclared);
            _Started = true;
            _StartSeconds = Get_Now();
            Log(f"[ReplicatedPoseTest-Server] stage=1 moving declared every {k_DeclaredMoveEveryFrames} frames, undeclared every frame");
            return;
        }

        _ServerFrames += 1;

        if (_ServerFrames % k_DeclaredMoveEveryFrames == 0)
        {
            _DeclaredMoves += 1;
            utils_replicated_pose_test::Request_Move(_Declared, k_DeclaredStart + FVector(k_StepCm * _DeclaredMoves, 0.0, 0.0), FRotator::ZeroRotator);
        }

        _UndeclaredMoves += 1;
        utils_replicated_pose_test::Request_Move(_Undeclared, k_UndeclaredStart + FVector(k_StepCm * _UndeclaredMoves, 0.0, 0.0), FRotator::ZeroRotator);

        Result.Set(Get_Now() - _StartSeconds >= k_ServerMoveSeconds);
    }

    UFUNCTION()
    private void OnServerDone(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        Log(f"[ReplicatedPoseTest-Server] stage=2 stopped frames={_ServerFrames} declaredMoves={_DeclaredMoves} undeclaredMoves={_UndeclaredMoves} ensureCount={utils_ensure::Get_EnsureCount()}");
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

    private bool Get_IsControlCycle()
    {
        return _Cycle > k_Cycles;
    }

    private FCk_Handle_Transform Get_CycleMover()
    {
        return Get_IsControlCycle() ? _Undeclared : _Declared;
    }

    private ECk_TESTONLY_ReplicatedPose_AttachSite Get_CycleSite()
    {
        if (Get_IsControlCycle() || _Cycle % 2 == 1)
        { return ECk_TESTONLY_ReplicatedPose_AttachSite::Replication; }

        return ECk_TESTONLY_ReplicatedPose_AttachSite::GameplayScript;
    }

    private void Stamp_Order()
    {
        auto CycleMover = Get_CycleMover();
        auto Mover = CycleMover.H();
        const auto AfterMoveCount = utils_replicated_pose_test::Get_Report(Mover).MoveCount;
        utils_replicated_pose_test::Add_AttachOrder(Mover, Get_CycleSite(), FCk_TESTONLY_ReplicatedPose_AttachOrder(_Anchor, AfterMoveCount));
        _OrderPending = true;
        _HeldFrames = 0;
        _CycleEnsures = utils_ensure::Get_EnsureCount();
    }

    UFUNCTION()
    private void Is_ClientDone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(false);

        if (Get_IsNewFrame() == false || IsFinished())
        { return; }

        if (_Cycle == 0)
        {
            auto Subject = Get_SubjectEntity();
            _Declared = utils_replicated_pose_test::Find(Subject, k_DeclaredId);
            _Undeclared = utils_replicated_pose_test::Find(Subject, k_UndeclaredId);
            if (ck::Is_NOT_Valid(_Declared) || ck::Is_NOT_Valid(_Undeclared))
            { return; }

            if (utils_replicated_pose_test::Get_Report(_Declared).MoveCount < 1 || utils_replicated_pose_test::Get_Report(_Undeclared).MoveCount < 1)
            { return; }

            auto Owner = Subject;
            _Anchor = utils_replicated_pose_test::Create_Anchor(Owner, k_AnchorLocation);
            Track_ForCleanup(_Anchor);
            _EnsuresBefore = utils_ensure::Get_EnsureCount();
            _Cycle = 1;
            Log(f"[ReplicatedPoseTest-Client] stage=1 cycles start ensureCount={_EnsuresBefore}");
            Stamp_Order();
            return;
        }

        auto Mover = Get_CycleMover();

        if (_OrderPending)
        {
            if (Mover.Has_Fragment(FCk_TESTONLY_ReplicatedPose_Attached) == false || utils_scene_node::Has(Mover) == false)
            { return; }

            _OrderPending = false;
            _AttachFrame = Mover.Get_Fragment(FCk_TESTONLY_ReplicatedPose_Attached).Frame;
            return;
        }

        // The attach frame's own composition may land in its tail pump; the hold is judged from the frame after.
        if (utils_stats::Get_FrameCount() <= _AttachFrame + 1)
        { return; }

        if (Get_IsControlCycle() == false)
        {
            const auto Deviation = (utils_transform::Get_EntityCurrentLocation(Mover) - k_AnchorLocation).Size();
            _MaxDeviation = Math::Max(_MaxDeviation, Deviation);
        }

        _HeldFrames += 1;
        if (_HeldFrames < k_HoldFrames)
        { return; }

        utils_scene_node::Request_Detach(Mover.As_SceneNode());
        const auto CycleEnsures = utils_ensure::Get_EnsureCount() - _CycleEnsures;

        if (Get_IsControlCycle())
        {
            _ControlEnsures = CycleEnsures;
            Log(f"[ReplicatedPoseTest-Client] stage=3 control (undeclared, FGroup_Replication) attachFrame={_AttachFrame} newEnsures={CycleEnsures} ensureCount={utils_ensure::Get_EnsureCount()}");
            Result.Set(true);
            return;
        }

        if (Get_CycleSite() == ECk_TESTONLY_ReplicatedPose_AttachSite::Replication)
        { _ReplicationCycles += 1; }
        else
        { _GameplayScriptCycles += 1; }

        const auto Site = Get_CycleSite();
        if (CycleEnsures != 0)
        { Log(f"[ReplicatedPoseTest-Client] stage=1 cycle={_Cycle} site={Site} attachFrame={_AttachFrame} newEnsures={CycleEnsures}"); }

        _Cycle += 1;
        if (_Cycle > k_Cycles)
        {
            _DeclaredEnsures = utils_ensure::Get_EnsureCount() - _EnsuresBefore;
            Log(f"[ReplicatedPoseTest-Client] stage=2 cycles done replication={_ReplicationCycles} gameplayScript={_GameplayScriptCycles} maxDev={_MaxDeviation :.3} newEnsures={utils_ensure::Get_EnsureCount() - _EnsuresBefore} ensureCount={utils_ensure::Get_EnsureCount()}");
        }

        Stamp_Order();
    }

    UFUNCTION()
    private void OnClientDone(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        Assert_Equals_Int(_ReplicationCycles + _GameplayScriptCycles, k_Cycles, "client: every cycle attached, held and detached");
        Assert_Equals_Int(_DeclaredEnsures, 0, "client: no ensure fired across the declared mover's 50 attach cycles");
        Assert_True(_MaxDeviation <= k_ComposeToleranceCm, f"client: the declared mover stayed on its anchor while attached (max deviation {_MaxDeviation :.3} cm)");
        Assert_True(_ControlEnsures >= 1, f"client: the same attach on an undeclared mover raised the rejection ensure ({_ControlEnsures})");
        FinishSuccess();
    }
}
