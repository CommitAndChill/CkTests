// Language=angelscript

//============================================================================
// CK SCENE NODE - PERF READOUT AUTOTEST: WHAT THE SCENE-NODE LAYERS COST THE SCHEDULER
//============================================================================
//
// Measurement-only harness for comparing builds that differ in the number of
// scene-node layers. The scene builds the same on every such build: 512 bare
// roots with a four-node chain each (static), and 16 roots with a ten-node
// chain each, all 16 turned every frame so every frame drains a transform
// request, runs the local settle and composes ten layers of nodes. Layers past
// the tenth exist only on a deeper build, and stay empty.
//
// What it reads is the scheduler's own wall-clock record of each frame (main
// pass, local settle and pump, summed over the world's partitions), not the
// frame time, which the test editor caps. Reading that record switches the
// per-processor clock reads on for a keep-alive window, so it is read every
// 200 frames and the frames inside each keep-alive are dropped.
//
// No timing threshold: setup failures and an empty sample are the only failure
// conditions. One log line per run; compare runs of the same scene on one
// machine.
//============================================================================

class UCk_AutoTest_SceneNode_LayerCostPerf : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 45.0f;

    private const int32 StaticChainCount = 512;
    private const int32 StaticChainLength = 4;
    private const int32 TurnedChainCount = 16;
    private const int32 TurnedChainLength = 10;
    private const float32 WarmupSeconds = 3.0f;
    private const float32 SampleSeconds = 8.0f;
    private const int64 ReadEveryFrames = 200;
    // Past the scheduler's debug-timing keep-alive (24 frames) after each read.
    private const int64 KeepAliveMarginFrames = 32;
    private const float32 TurnYawDegrees = 0.5f;

    private FCk_Handle _TestEntity;
    private TArray<FCk_Handle_Transform> _TurnedRoots;
    private TArray<float64> _SchedulerMs;
    private float32 _Elapsed = 0.0f;
    private float32 _FrameSecondsSum = 0.0f;
    private int32 _FrameCount = 0;
    private bool _Sampling = false;
    private int64 _AcceptAfterFrame = 0;
    private int64 _LastReadFrame = 0;
    private bool _TurnPositive = true;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        _TestEntity = InHandle;

        Set_CVarForTest(n"t.MaxFPS", "0");
        Set_CVarForTest(n"r.VSync", "0");

        for (int32 ChainIndex = 0; ChainIndex < StaticChainCount + TurnedChainCount; ++ChainIndex)
        {
            const auto IsTurned = ChainIndex >= StaticChainCount;
            auto Root = utils_transform::Add(utils_entity_lifetime::Request_CreateEntity(_TestEntity),
                FTransform(FRotator::ZeroRotator, FVector(0.0f, ChainIndex * 10.0f, 0.0f), FVector::OneVector),
                ECk_Replication::DoesNotReplicate);
            if (ck::Is_NOT_Valid(Root))
            {
                FinishFailure(f"Failed to add the root Transform of chain {ChainIndex}");
                return;
            }

            if (IsTurned)
            { _TurnedRoots.Add(Root); }

            auto Parent = Root;
            const auto Length = IsTurned ? TurnedChainLength : StaticChainLength;
            for (int32 LinkIndex = 0; LinkIndex < Length; ++LinkIndex)
            {
                auto Node = utils_scene_node::Create(Parent,
                    FTransform(FRotator(0.0f, 3.0f, 0.0f), FVector(50.0f, 0.0f, 0.0f), FVector::OneVector));
                if (ck::Is_NOT_Valid(Node))
                {
                    FinishFailure(f"Failed to create node {LinkIndex} of chain {ChainIndex}");
                    return;
                }
                Parent = Node.As_Transform();
            }
        }

        utils_timer::Create_Tick(_TestEntity, FCk_Delegate_Timer(this, n"OnTick"));
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished())
        { return; }

        const auto DeltaSeconds = float32(InDeltaT.Get_Seconds());
        _Elapsed += DeltaSeconds;

        if (_Elapsed <= WarmupSeconds)
        { return; }

        const auto Now = utils_time::Get_FrameCounter();
        if (!_Sampling)
        {
            _Sampling = true;
            _AcceptAfterFrame = Now;
            _LastReadFrame = Now;
        }

        const auto Yaw = _TurnPositive ? TurnYawDegrees : -TurnYawDegrees;
        _TurnPositive = !_TurnPositive;
        for (auto& Root : _TurnedRoots)
        { utils_transform::Request_AddRotationOffset(Root, FRotator(0.0f, Yaw, 0.0f), ECk_LocalWorld::World); }

        _FrameSecondsSum += DeltaSeconds;
        ++_FrameCount;

        const auto IsLastFrame = _Elapsed > WarmupSeconds + SampleSeconds;
        if (Now - _LastReadFrame >= ReadEveryFrames || IsLastFrame)
        {
            Do_ReadSchedulerTimes(Now);
        }

        if (IsLastFrame)
        { Do_Report(); }
    }

    private void Do_ReadSchedulerTimes(int64 InNow)
    {
        const auto Times = UCk_Utils_AutoTest_SceneNodeDepth_UE::Get_SchedulerFrameTimesMs(
            utils_entity_lifetime::Get_WorldForEntity(_TestEntity), _AcceptAfterFrame, InNow - 1);
        for (const auto Ms : Times)
        { _SchedulerMs.Add(Ms); }

        _LastReadFrame = InNow;
        _AcceptAfterFrame = InNow + KeepAliveMarginFrames;
    }

    private void Do_Report()
    {
        if (_SchedulerMs.Num() == 0 || _FrameCount == 0)
        {
            FinishFailure(f"No scheduler frames sampled (frames ticked {_FrameCount})");
            return;
        }

        auto Sorted = _SchedulerMs;
        Sorted.Sort();

        auto Sum = 0.0;
        for (const auto Ms : Sorted)
        { Sum += Ms; }

        const auto Count = Sorted.Num();
        const auto Avg = Sum / float64(Count);
        const auto Median = Sorted[Count / 2];
        const auto P90 = Sorted[Math::Min(Count - 1, (Count * 9) / 10)];
        const auto Max = Sorted.Last();
        const auto FrameAvgMs = (_FrameSecondsSum / float32(_FrameCount)) * 1000.0f;

        Log(f"[CkSceneNode PERF][LayerCost] maxDepth={utils_scene_node::Get_MaxDepth()} staticChains={StaticChainCount}x{StaticChainLength} turnedChains={TurnedChainCount}x{TurnedChainLength} schedulerFrames={Count} schedulerAvg={Avg :.4} ms schedulerMedian={Median :.4} ms schedulerP90={P90 :.4} ms schedulerMax={Max :.4} ms frames={_FrameCount} frameAvg={FrameAvgMs :.3} ms");
        FinishSuccess();
    }
}

class ACk_AutoTest_SceneNode_LayerCostPerf_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_SceneNode_LayerCostPerf;
    default _TimeoutSeconds = 45.0f;
}
