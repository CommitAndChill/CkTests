// Language=angelscript

// The Plane station's generation guard. A cut is requested and the station is reset on the SAME call stack, so the
// slice is certainly still queued when its source goes: the resolution arrives as FailedCancelled for an operation the
// fixture no longer waits for, is counted as stale, and the new generation shows no pair. A cut on the new generation
// then commits normally, proving the reset left a working station rather than a wedged one. The reset generation's
// root and source must also be gone (it commits nothing AND leaves nothing), and closing the fixture at the end must
// take the live generation's root with it.
class UCk_AutoTest_RuntimeMeshGym_ResetDuringPendingCutCommitsNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;
    default _AutoStageOriginField = false;

    private UCkRuntimeMeshGym_PlaneFixture _Fixture;
    private int32 _GenerationAtCut = 0;
    private FCk_Handle_Transform _ResetRoot;
    private FCk_Handle_RuntimeMesh _ResetSource;
    private FCk_Handle_Transform _LiveRoot;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Fixture = Cast<UCkRuntimeMeshGym_PlaneFixture>(NewObject(this, UCkRuntimeMeshGym_PlaneFixture));
        Assert_True(ck::IsValid(_Fixture), "the Plane fixture object is created");
        if (ck::Is_NOT_Valid(_Fixture))
        {
            FinishSuccess();
            return;
        }

        // Off-origin and rotated, at unit display scale; the fixture's root hangs off this test's entity, so the
        // test's teardown takes everything it made.
        const auto Pose = CkRuntimeMeshGym::Make_CenteredPose(FVector(400.0, -300.0, 6000.0), FRotator(15.0, 30.0, 0.0), 1.0);
        const auto Created = _Fixture.Create(InHandle, Pose, 0);
        Assert_True(Created, "the Plane fixture composes under the test entity");
        if (Created == false)
        {
            _Fixture.Request_Close();
            FinishSuccess();
            return;
        }

        Add_Step_WaitUntil("the first generation's source import becomes Ready", n"Check_SourceReady");
        Add_Step("request a cut, then reset on the same call stack", n"Step_CutThenReset");
        Add_Step_WaitUntil("the cancelled resolution reaches the fixture and is rejected", n"Check_StaleRejected");
        Add_Step_WaitUntil("the reset generation's root and source are destroyed", n"Check_ResetGenerationGone");
        Add_Step("the reset generation committed nothing", n"Step_AssertNothingCommitted");
        Add_Step_WaitUntil("the new generation's source import becomes Ready", n"Check_SourceReady");
        Add_Step("cut the new generation's source", n"Step_Cut");
        Add_Step_WaitUntil("the new generation's pair commits", n"Check_HasPair");
        Add_Step("the pair belongs to the live generation and conserves volume", n"Step_AssertPair");
        Add_Step("close the fixture", n"Step_Close");
        Add_Step_WaitUntil("closing destroys the live generation's root", n"Check_LiveRootGone");
        Run_Steps(InHandle);
    }

    // Every exit - including a failed wait - closes the fixture, so nothing it owns outlives the test.
    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    {
        if (ck::IsValid(_Fixture))
        {
            _Fixture.Request_Close();
        }
    }

    UFUNCTION()
    private void Check_SourceReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsSourceReady());
    }

    UFUNCTION()
    private void Step_CutThenReset(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _GenerationAtCut = _Fixture.Generation;
        Assert_True(_Fixture.Request_Cut(), "the cut is accepted while the source is Ready");
        Assert_True(_Fixture.HasPendingSlice, "the cut is still in flight after the request returns");
        Assert_Equals_Int(_Fixture.StaleResolutions, 0, "nothing has been rejected before the reset");

        _ResetRoot = _Fixture.Root;
        _ResetSource = _Fixture.Source;
        Assert_True(_Fixture.Request_Reset(), "the reset rebuilds the station");

        Assert_Equals_Int(_Fixture.Generation, _GenerationAtCut + 1, "the reset advances the generation");
        Assert_False(_Fixture.HasPendingSlice, "the reset forgets the in-flight cut");
        Assert_False(_Fixture.HasPair, "the new generation starts without a pair");
        Assert_True(ck::IsValid(_Fixture.Root) && _Fixture.Root != _ResetRoot, "the reset composed a new root");
    }

    UFUNCTION()
    private void Check_ResetGenerationGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_ResetRoot) && ck::Is_NOT_Valid(_ResetSource));
    }

    UFUNCTION()
    private void Check_StaleRejected(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.StaleResolutions >= 1);
    }

    UFUNCTION()
    private void Step_AssertNothingCommitted(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Fixture.StaleResolutions, 1, "exactly the one in-flight cut resolved stale");
        Assert_True(_Fixture.HasLastStaleOutcome
            && _Fixture.LastStaleOutcome == ECk_RuntimeMesh_SliceOutcome::FailedCancelled,
            "the in-flight cut resolved FailedCancelled once its source was destroyed");
        Assert_False(_Fixture.HasPair, "the stale resolution committed no pair");
        Assert_False(_Fixture.HasLastOutcome, "the stale resolution was not recorded as the new generation's outcome");
        Assert_False(ck::IsValid(_Fixture.Positive.Mesh), "no positive result exists in the new generation");
        Assert_False(ck::IsValid(_Fixture.Negative.Mesh), "no negative result exists in the new generation");
        Assert_False(ck::IsValid(_ResetRoot), "the reset generation's root is destroyed");
        Assert_False(ck::IsValid(_ResetSource), "the reset generation's source is destroyed");
    }

    UFUNCTION()
    private void Step_Cut(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_Fixture.Request_Cut(), "the new generation accepts a cut");
    }

    UFUNCTION()
    private void Check_HasPair(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.HasPair || (_Fixture.HasLastOutcome && _Fixture.HasPendingSlice == false));
    }

    UFUNCTION()
    private void Step_AssertPair(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_Fixture.HasLastOutcome && _Fixture.LastOutcome == ECk_RuntimeMesh_SliceOutcome::Succeeded,
            "the live generation's cut succeeds");
        Assert_True(_Fixture.HasPair, "the live generation commits a pair");
        Assert_Equals_Int(_Fixture.StaleResolutions, 1, "the live cut was not mistaken for a stale one");
        Assert_True(ck::IsValid(_Fixture.Positive.Mesh), "the positive result is alive");
        Assert_True(ck::IsValid(_Fixture.Negative.Mesh), "the negative result is alive");
        // The default tuned plane passes through the cube centre with a +X normal: two 500 cm3 halves.
        Assert_Equals_Float(float(_Fixture.Positive.Metrics.Get_VolumeCm3()), 500.0f, 0.01f, "the positive half is 500 cm3");
        Assert_Equals_Float(float(_Fixture.Negative.Metrics.Get_VolumeCm3()), 500.0f, 0.01f, "the negative half is 500 cm3");
        Assert_True(ck::IsValid(_Fixture.Positive.Display), "the positive result was given a display");
        Assert_True(ck::IsValid(_Fixture.Negative.Display), "the negative result was given a display");
    }

    UFUNCTION()
    private void Step_Close(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _LiveRoot = _Fixture.Root;
        _Fixture.Request_Close();
        Assert_True(_Fixture.Closed, "the fixture reports Closed");
        Assert_False(_Fixture.Request_Reset(), "a Closed fixture refuses to rebuild itself");
    }

    UFUNCTION()
    private void Check_LiveRootGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_LiveRoot));
    }
}
