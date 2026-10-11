// Language=angelscript

// A completed request means its input was processed, not that a pose includes it: inside the completion callback the
// action offset already reads the new input while the published sample still carries the previous id and revision. The
// next successful update publishes the new id with the next revision, at the presentation's transform.
class UCk_AutoTest_ProceduralAnimation_BodyPoseAction_PoseAcknowledgment : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(180000.0, 50000.0, 600.0);
    private FVector _Action = FVector(8.0, 0.0, 0.0);
    private float _PhaseStart = 0.0;
    private FCk_ProceduralBodyPose_ActionSample _SampleAtSubmit;
    private int32 _Completions = 0;
    private ECk_Request_OperationResult _CompletionResult = ECk_Request_OperationResult::Failed;
    private FTransform _ActionInCallback;
    private FCk_ProceduralBodyPose_ActionSample _SampleInCallback;
    private FCk_ProceduralBodyPose_ActionSample _AppliedSample;
    private FTransform _PresentationAtApplied;

    FCkProceduralAnimationGym_CrawlerHandles Get_Handles() const
    {
        return _Fixture.Crawlers[0].Handles;
    }

    float Get_Elapsed() const
    {
        return float(System::GetGameTimeInSeconds()) - _PhaseStart;
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
        Add_Step_WaitUntil("the walker and its body pose are composed and ready", n"Check_Ready", 1200);
        Add_Step_WaitUntil("the stationary walker settles for 0.5 s", n"Check_Settled");
        Add_Step("record the published sample and request an action", n"Step_Submit");
        Add_Step_WaitUntil("the request completes and a pose carrying its id is published", n"Check_Applied");
        Add_Step("verify completion preceded the pose and the pose is correlated with the presentation", n"Step_Verify");
        Add_Step_WaitUntil("the fixture is gone", n"Check_Destroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Ready(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        if (_Fixture.CompositionError.IsEmpty() == false)
        {
            FinishFailure(f"The walker could not be composed: {_Fixture.CompositionError}");
            return;
        }
        auto Ready = _Fixture.Get_AllReady();
        if (Ready)
        {
            _PhaseStart = float(System::GetGameTimeInSeconds());
        }
        auto Result = OutResult;
        Result.Set(Ready);
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(Get_Elapsed() >= 0.5);
    }

    UFUNCTION()
    private void Step_Submit(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Handles = Get_Handles();
        _SampleAtSubmit = utils_procedural_body_pose::Get_ActionSample(Handles.BodyPose);
        Assert_True(_SampleAtSubmit.Get_PoseRevision() > 0, "Precondition: the walker has published poses");
        utils_procedural_body_pose::Request_SetActionOffset(Handles.BodyPose,
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform(_Action), int64(1)),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _Completions++;
        _CompletionResult = InResult;
        _ActionInCallback = utils_procedural_body_pose::Get_ActionOffset(Get_Handles().BodyPose);
        _SampleInCallback = utils_procedural_body_pose::Get_ActionSample(Get_Handles().BodyPose);
    }

    UFUNCTION()
    private void Check_Applied(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Handles = Get_Handles();
        auto Sample = utils_procedural_body_pose::Get_ActionSample(Handles.BodyPose);
        auto Applied = _Completions > 0 && Sample.Get_SampleId() == 1;
        if (Applied)
        {
            _AppliedSample = Sample;
            _PresentationAtApplied = utils_transform::Get_EntityCurrentTransform(Handles.Presentation);
        }
        auto Result = OutResult;
        Result.Set(Applied);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1, "The request completes exactly once");
        Assert_True(_CompletionResult == ECk_Request_OperationResult::Succeeded, "The request completes Succeeded");
        Assert_True(_ActionInCallback.GetLocation().Equals(_Action, 0.001),
            "Inside the completion callback the action offset already reads the processed input");
        Assert_True(_SampleInCallback.Get_SampleId() == _SampleAtSubmit.Get_SampleId(),
            f"Inside the completion callback the published sample still carries the previous id ({_SampleInCallback.Get_SampleId()}): completion is not pose application");
        Assert_True(_SampleInCallback.Get_PoseRevision() == _SampleAtSubmit.Get_PoseRevision(),
            f"With no update between the submitting poll and the drain, the revision does not advance ({_SampleAtSubmit.Get_PoseRevision()} -> {_SampleInCallback.Get_PoseRevision()})");
        Assert_True(_AppliedSample.Get_PoseRevision() == _SampleAtSubmit.Get_PoseRevision() + 1,
            f"The first sample carrying the new id is the next successful update ({_SampleAtSubmit.Get_PoseRevision()} -> {_AppliedSample.Get_PoseRevision()})");
        auto Sampled = _AppliedSample.Get_WorldTransform();
        auto Error = (Sampled.GetLocation() - _PresentationAtApplied.GetLocation()).Size();
        auto Angle = Math::RadiansToDegrees(Sampled.GetRotation().AngularDistance(_PresentationAtApplied.GetRotation()));
        Assert_True(Error < 0.05 && Angle < 0.05,
            f"The sample carrying the new id is the presentation's transform ({Error :.4} cm, {Angle :.4} deg)");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
