// Language=angelscript

// Exercises the reflected API and dynamic delegates against the real engine frame.
// Copies must share the queue, incomplete steps, and once-per-frame allowance.
class UCk_AutoTest_Pacing_SharedWork : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;
    default _AutoStageOriginField = false;

    private FCk_Pacing_SharedWork _Work;
    private FCk_Pacing_SharedWork _Alias;
    private int32 _DiscoveryCalls = 0;
    private int32 _ElevenCalls = 0;
    private int32 _TwentyTwoCalls = 0;
    private int32 _ClearedWorkCalls = 0;
    private int32 _ReportedSteps = 0;
    private bool _Clearing = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("copy the shared pacer and spend this frame's allowance", n"Step_Start");
        Add_Step_WaitUntil("discovery and incomplete work drain across frames", n"Check_Drained");
        Add_Step("clear work through the other handle copy", n"Step_Clear");
        Add_Step_WaitUntil("a fresh pump observes the shared clear", n"Check_Cleared");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Start(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_False(utils_pacing::IsValid_SharedWork(_Work), "Default shared pacer is invalid");
        _Work = utils_pacing::Create_SharedWork(2, FCk_Time(1.0));
        _Alias = _Work;
        Assert_True(utils_pacing::IsValid_SharedWork(_Alias), "Copied pacer aliases valid shared state");
        Assert_True(utils_pacing::Enqueue_SharedWork(_Alias, 11), "An alias can enqueue work");

        const auto Report = utils_pacing::Pump_SharedWork(_Work,
            FCk_Delegate_Pacing_Discover(this, n"Discover"), FCk_Delegate_Pacing_Step(this, n"Step"));
        Assert_True(Report.Pumped, "First pump runs");
        Assert_Equals_Int(Report.Steps, 1, "Execution gets one interleaved unit");
        Assert_Equals_Int(Report.DiscoverySteps, 1, "Discovery gets one interleaved unit");
        Assert_Equals_Int(_ElevenCalls, 1, "The original handle sees work queued through its alias");
        Assert_True(Report.Elapsed.Get_Seconds() >= 0.0, "Pump reports nonnegative elapsed time");
        _ReportedSteps += Report.Steps;

        const auto CallsBefore = _DiscoveryCalls + _ElevenCalls + _TwentyTwoCalls;
        const auto Repeat = utils_pacing::Pump_SharedWork(_Alias,
            FCk_Delegate_Pacing_Discover(this, n"Discover"), FCk_Delegate_Pacing_Step(this, n"Step"));
        Assert_False(Repeat.Pumped, "An alias cannot pump again in the same engine frame");
        Assert_Equals_Int(Repeat.Steps + Repeat.DiscoverySteps, 0, "Repeated pump grants no work allowance");
        Assert_Equals_Int(_DiscoveryCalls + _ElevenCalls + _TwentyTwoCalls, CallsBefore,
            "Repeated pump invokes neither dynamic delegate");
    }

    UFUNCTION()
    private bool Discover()
    {
        ++_DiscoveryCalls;
        if (_Clearing) { return false; }

        // The final discovery unit enqueues work even though no further discovery remains.
        if (_DiscoveryCalls == 1)
        {
            Assert_True(utils_pacing::Enqueue_SharedWork(_Alias, 22), "Discovery can enqueue through the alias");
        }
        return false;
    }

    UFUNCTION()
    private bool Step(int64 InWorkId)
    {
        if (InWorkId == 11)
        {
            ++_ElevenCalls;
            return _ElevenCalls >= 2;
        }
        if (InWorkId == 22)
        {
            ++_TwentyTwoCalls;
            return true;
        }

        ++_ClearedWorkCalls;
        Assert_True(false, f"Unexpected or cleared work executed: {InWorkId}");
        return true;
    }

    UFUNCTION()
    private void Check_Drained(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        const auto Report = utils_pacing::Pump_SharedWork(_Alias,
            FCk_Delegate_Pacing_Discover(this, n"Discover"), FCk_Delegate_Pacing_Step(this, n"Step"));
        Assert_True(Report.Steps + Report.DiscoverySteps <= 2, "Discovery and execution share the frame step limit");
        _ReportedSteps += Report.Steps;
        const auto Done = _ElevenCalls >= 2 && _TwentyTwoCalls >= 1;
        if (Done)
        {
            Assert_Equals_Int(_ElevenCalls, 2, "Incomplete work survives exactly until its second step");
            Assert_Equals_Int(_TwentyTwoCalls, 1, "Completed discovery work executes once");
            Assert_Equals_Int(_ReportedSteps, 3, "Reports count every real work callback");
        }
        auto Result = OutResult;
        Result.Set(Done);
    }

    UFUNCTION()
    private void Step_Clear(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Clearing = true;
        Assert_True(utils_pacing::Enqueue_SharedWork(_Alias, 99), "Alias queues the work to clear");
        utils_pacing::Clear_SharedWork(_Work);
    }

    UFUNCTION()
    private void Check_Cleared(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        const auto Report = utils_pacing::Pump_SharedWork(_Alias,
            FCk_Delegate_Pacing_Discover(this, n"Discover"), FCk_Delegate_Pacing_Step(this, n"Step"));
        if (Report.Pumped)
        {
            Assert_Equals_Int(Report.Steps, 0, "Fresh pump sees the queue cleared through the original handle");
            Assert_Equals_Int(_ClearedWorkCalls, 0, "Cleared work never reaches the step delegate");
        }
        auto Result = OutResult;
        Result.Set(Report.Pumped);
    }
}
