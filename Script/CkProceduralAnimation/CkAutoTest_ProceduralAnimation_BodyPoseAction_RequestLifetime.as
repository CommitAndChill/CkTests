// Language=angelscript

// Action requests are retained before the gait is set up, drain in enqueue order with the last one winning, survive a
// re-entrant enqueue from a completion callback, and complete Failed_Cancelled exactly once when the body is destroyed
// with one still queued.
class UCk_AutoTest_ProceduralAnimation_BodyPoseAction_RequestLifetime : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(180000.0, 45000.0, 600.0);
    private FCk_Handle_ProceduralBodyPose _BodyPose;
    private FCk_Handle _Body;
    private int32 _EarlyCompletions = 0;
    private ECk_Request_OperationResult _EarlyResult = ECk_Request_OperationResult::Failed;
    private TArray<int64> _FifoOrder;
    private TArray<ECk_Request_OperationResult> _FifoResults;
    private FTransform _ActionInLastFifoCallback;
    private int32 _ReentrantCompletionsAtEnqueue = -1;
    private int32 _ReentrantCompletions = 0;
    private ECk_Request_OperationResult _ReentrantResult = ECk_Request_OperationResult::Failed;
    private int64 _SampleIdAfterFifo = -1;
    private int32 _CancelledCompletions = 0;
    private ECk_Request_OperationResult _CancelledResult = ECk_Request_OperationResult::Failed;
    private bool _CancelledOwnerMatches = false;

    FTransform Get_Action() const
    {
        return utils_procedural_body_pose::Get_ActionOffset(_BodyPose);
    }

    int64 Get_SampleId() const
    {
        return utils_procedural_body_pose::Get_ActionSample(_BodyPose).Get_SampleId();
    }

    FCk_Request_ProceduralBodyPose_SetActionOffset MakeRequest(float InForward, int64 InSampleId)
    {
        return FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform(FVector(InForward, 0.0, 0.0)), InSampleId);
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Roster = TArray<ECkProceduralAnimationGym_Species>();
        Roster.Add(ECkProceduralAnimationGym_Species::Crawler4);
        if (_Fixture.Create_WithRoster(InHandle, _Origin, ECkProceduralAnimationGym_Course::Flat, Roster, false,
            ECk_ProceduralBodyPose_ConformMode::None) == false)
        {
            FinishFailure("The isolated single-walker floor fixture could not be created");
            return;
        }
        Add_Step_WaitUntil("the body pose exists and receives a request before its gait is set up", n"Check_SpawnedAndRequested", 1200);
        Add_Step_WaitUntil("the early request completes and its sample is published once the gait is ready", n"Check_EarlyLanded", 1200);
        Add_Step("verify the early request and enqueue three requests in one drain", n"Step_VerifyEarlyAndSubmitFifo");
        Add_Step_WaitUntil("the three requests drain", n"Check_FifoDrained");
        Add_Step("verify the drain order and the last-wins input", n"Step_VerifyFifo");
        Add_Step_WaitUntil("the re-entrant request lands on a later drain", n"Check_ReentrantLanded");
        Add_Step("verify the re-entrant request, then enqueue one and destroy the body on the same stack", n"Step_VerifyReentrantAndDestroy");
        Add_Step_WaitUntil("the queued request is cancelled and the body is gone", n"Check_Cancelled");
        // Three frames of later teardown pumps in which a second cancellation would have fired.
        Add_Step_WaitFrames("later frames pass after the cancellation", 3);
        Add_Step("verify the cancellation fired exactly once and retire the fixture", n"Step_VerifyCancelled");
        Add_Step_WaitUntil("the fixture is gone", n"Check_Destroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_SpawnedAndRequested(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        if (_Fixture.CompositionError.IsEmpty() == false)
        {
            FinishFailure(f"The walker could not be composed: {_Fixture.CompositionError}");
            return;
        }
        auto Spawned = _Fixture.Crawlers.Num() == 1 && ck::IsValid(_Fixture.Crawlers[0].Handles.BodyPose);
        if (Spawned)
        {
            auto Handles = _Fixture.Crawlers[0].Handles;
            _BodyPose = Handles.BodyPose;
            _Body = FCk_Handle(Handles.Root);
            if (utils_procedural_gait::Get_Status(Handles.Gait) == ECk_ProceduralAnimation_Status::Ready)
            {
                FinishFailure("The gait was already Ready when the body pose first existed; the before-setup enqueue was not exercised");
                return;
            }
            utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(6.0, int64(1)),
                FCk_Delegate_Request_OnCompleted(this, n"OnEarly"));
        }
        auto Result = OutResult;
        Result.Set(Spawned);
    }

    UFUNCTION()
    private void OnEarly(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _EarlyCompletions++;
        _EarlyResult = InResult;
    }

    UFUNCTION()
    private void Check_EarlyLanded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(_EarlyCompletions > 0 && _Fixture.Get_AllReady() && Get_SampleId() == 1);
    }

    UFUNCTION()
    private void Step_VerifyEarlyAndSubmitFifo(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_EarlyCompletions, 1, "The request enqueued before gait setup completes exactly once");
        Assert_True(_EarlyResult == ECk_Request_OperationResult::Succeeded, "The request enqueued before gait setup completes Succeeded");
        Assert_True(Get_Action().GetLocation().Equals(FVector(6.0, 0.0, 0.0), 0.001),
            "The request enqueued before gait setup is the processed input once the gait is ready");

        utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(1.0, int64(2)),
            FCk_Delegate_Request_OnCompleted(this, n"OnFifo2"));
        utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(2.0, int64(3)),
            FCk_Delegate_Request_OnCompleted(this, n"OnFifo3"));
        utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(3.0, int64(4)),
            FCk_Delegate_Request_OnCompleted(this, n"OnFifo4"));
    }

    UFUNCTION()
    private void OnFifo2(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _FifoOrder.Add(2);
        _FifoResults.Add(InResult);
    }

    UFUNCTION()
    private void OnFifo3(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _FifoOrder.Add(3);
        _FifoResults.Add(InResult);
    }

    UFUNCTION()
    private void OnFifo4(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _FifoOrder.Add(4);
        _FifoResults.Add(InResult);
        _ActionInLastFifoCallback = Get_Action();
        utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(4.0, int64(5)),
            FCk_Delegate_Request_OnCompleted(this, n"OnReentrant"));
        _ReentrantCompletionsAtEnqueue = _ReentrantCompletions;
    }

    UFUNCTION()
    private void OnReentrant(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _ReentrantCompletions++;
        _ReentrantResult = InResult;
    }

    UFUNCTION()
    private void Check_FifoDrained(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Drained = _FifoOrder.Num() == 3;
        if (Drained)
        {
            _SampleIdAfterFifo = Get_SampleId();
        }
        auto Result = OutResult;
        Result.Set(Drained);
    }

    UFUNCTION()
    private void Step_VerifyFifo(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_FifoOrder.Num() == 3 && _FifoOrder[0] == 2 && _FifoOrder[1] == 3 && _FifoOrder[2] == 4,
            "Three requests enqueued together complete in enqueue order");
        for (auto Result : _FifoResults)
        {
            Assert_True(Result == ECk_Request_OperationResult::Succeeded, "Each drained request completes Succeeded");
        }
        Assert_True(_ActionInLastFifoCallback.GetLocation().Equals(FVector(3.0, 0.0, 0.0), 0.001),
            "The last request of the drain is the processed input when its completion fires");
        Assert_True(_SampleIdAfterFifo == 4, f"The pose after the three-request drain carries the last id ({_SampleIdAfterFifo})");
        Assert_Equals_Int(_ReentrantCompletionsAtEnqueue, 0, "The request enqueued from inside a completion callback is not drained in that pass");
    }

    UFUNCTION()
    private void Check_ReentrantLanded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(_ReentrantCompletions > 0 && Get_SampleId() == 5);
    }

    UFUNCTION()
    private void Step_VerifyReentrantAndDestroy(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_ReentrantCompletions, 1, "The re-entrant request completes exactly once");
        Assert_True(_ReentrantResult == ECk_Request_OperationResult::Succeeded, "The re-entrant request completes Succeeded");
        Assert_True(Get_Action().GetLocation().Equals(FVector(4.0, 0.0, 0.0), 0.001), "The re-entrant request becomes the processed input");

        utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(7.0, int64(6)),
            FCk_Delegate_Request_OnCompleted(this, n"OnCancelled"));
        utils_entity_lifetime::Request_DestroyEntity(_Body);
    }

    UFUNCTION()
    private void OnCancelled(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _CancelledCompletions++;
        _CancelledResult = InResult;
        _CancelledOwnerMatches = InRequestOwner == _Body;
    }

    UFUNCTION()
    private void Check_Cancelled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_CancelledCompletions > 0 && ck::Is_NOT_Valid(_Body));
    }

    UFUNCTION()
    private void Step_VerifyCancelled(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_CancelledCompletions, 1, "The request queued before the destroy completes exactly once");
        Assert_True(_CancelledResult == ECk_Request_OperationResult::Failed_Cancelled,
            "The request queued before the destroy completes Failed_Cancelled");
        Assert_True(_CancelledOwnerMatches, "The cancellation reports the destroyed body pose as its owner");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
