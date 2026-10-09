// Language=angelscript

// Drives the Runtime Convex gym's slice fixture: a reset while its slice is queued commits nothing, and a reset while
// both half bodies are pending leaves neither half (nor its body) alive. Both resets run on the call stack that
// started the pending work, which is the only way to reach those windows deterministically. A handle reads invalid
// only from the Teardown phase, which runs after FGroup_EndPlay where the JoltBody EndPlay removes the body, so an
// invalid half handle means its body has left the Jolt world.
class UCk_AutoTest_RuntimeConvexGym_StaleCompletionsAfterResetLeaveNoBodies : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 30.0f;
    default _AutoStageOriginField = false;

    private UCk_RuntimeConvexGym_SliceFixture _Fixture;
    private FCk_Handle _FirstSpecimen;
    private FCk_Handle _SecondSpecimen;
    private int32 _StaleBeforeSliceReset = 0;
    private int32 _GenerationBeforeSliceReset = 0;
    private int32 _StaleBeforeBodyReset = 0;
    private int32 _GenerationBeforeBodyReset = 0;
    private TArray<FCk_Handle> _AbandonedHalves;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Fixture = NewObject(this, UCk_RuntimeConvexGym_SliceFixture);
        _Fixture.Request_Build(InHandle, FVector(0.0, 41000.0, 20000.0));

        Add_Step_WaitUntil("the specimen's body is Ready and asleep", n"Check_SpecimenCuttable");
        Add_Step("cut, resetting on the call stack that queues the slice", n"Step_CutThenReset");
        Add_Step_WaitUntil("the cancelled slice arrives as a stale completion", n"Check_StaleSliceArrived");
        Add_Step_WaitUntil("the rebuilt specimen is cuttable", n"Check_SpecimenCuttable");
        Add_Step("assert the slice reset committed nothing", n"Step_AssertSliceResetCommittedNothing");
        Add_Step("cut with a reset right after both halves are composed", n"Step_CutThenResetAfterCompose");
        Add_Step_WaitUntil("both pending half bodies resolve as stale completions", n"Check_StaleBodiesArrived");
        Add_Step_WaitUntil("both abandoned halves are torn down, their bodies removed", n"Check_AbandonedHalvesGone");
        Add_Step_WaitUntil("the rebuilt specimen is cuttable", n"Check_SpecimenCuttable");
        Add_Step("assert the body reset left only the rebuilt specimen", n"Step_AssertBodyResetLeftNoBodies");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_SpecimenCuttable(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_CanCut(0));
    }

    UFUNCTION()
    private void Step_CutThenReset(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _FirstSpecimen = FCk_Handle(_Fixture.Get_Piece(0).Entity);
        _StaleBeforeSliceReset = _Fixture.Get_StaleCompletions();
        _GenerationBeforeSliceReset = _Fixture.Get_Generation();

        auto Cut = FCkRuntimeConvexGym_CutRequest();
        Cut.Interrupt = ECkRuntimeConvexGym_CutInterrupt::ResetAfterSliceSubmitted;
        Assert_True(_Fixture.Request_Cut(Cut), "the sleeping specimen accepts a cut");
        Assert_True(_Fixture.Get_IsCutInFlight(), "the cut is in flight while its source is isolated");
    }

    UFUNCTION()
    private void Check_StaleSliceArrived(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_StaleCompletions() > _StaleBeforeSliceReset);
    }

    UFUNCTION()
    private void Step_AssertSliceResetCommittedNothing(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Fixture.Get_StaleCompletions(), _StaleBeforeSliceReset + 1, "exactly the one queued slice completed stale");
        Assert_Equals_Int(_Fixture.Get_Generation(), _GenerationBeforeSliceReset + 1, "the reset built exactly one new generation");
        Assert_Equals_Int(_Fixture.Get_CutsCommitted(), 0, "the stale slice committed no cut");
        Assert_Equals_Int(_Fixture.Get_LastHalves().Num(), 0, "the stale slice composed no halves");
        Assert_Equals_Int(_Fixture.Get_PieceCount(), 1, "only the rebuilt specimen exists");
        Assert_False(ck::IsValid(_FirstSpecimen), "the first generation's specimen is torn down");
        Assert_True(FCk_Handle(_Fixture.Get_Piece(0).Entity) != _FirstSpecimen, "the specimen is a new entity");
        Assert_Equals_Int(_Fixture.Get_AbandonedAliveCount(), 0, "nothing abandoned is left before Teardown");
    }

    UFUNCTION()
    private void Step_CutThenResetAfterCompose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _SecondSpecimen = FCk_Handle(_Fixture.Get_Piece(0).Entity);
        _StaleBeforeBodyReset = _Fixture.Get_StaleCompletions();
        _GenerationBeforeBodyReset = _Fixture.Get_Generation();

        auto Cut = FCkRuntimeConvexGym_CutRequest();
        Cut.Interrupt = ECkRuntimeConvexGym_CutInterrupt::ResetAfterHalvesComposed;
        Assert_True(_Fixture.Request_Cut(Cut), "the rebuilt specimen accepts a cut");
    }

    UFUNCTION()
    private void Check_StaleBodiesArrived(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_StaleCompletions() >= _StaleBeforeBodyReset + 2);
        if (_AbandonedHalves.Num() == 0 && _Fixture.Get_LastHalves().Num() == 2)
        {
            _AbandonedHalves = _Fixture.Get_LastHalves();
        }
    }

    UFUNCTION()
    private void Check_AbandonedHalvesGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto AllGone = _AbandonedHalves.Num() == 2;
        for (auto Half : _AbandonedHalves)
        {
            AllGone = AllGone && ck::Is_NOT_Valid(Half);
        }
        auto Result = OutResult;
        Result.Set(AllGone && _Fixture.Get_AbandonedAliveCount() == 0);
    }

    UFUNCTION()
    private void Step_AssertBodyResetLeftNoBodies(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_AbandonedHalves.Num(), 2, "the cut composed both halves before the reset");
        Assert_Equals_Int(_Fixture.Get_StaleCompletions(), _StaleBeforeBodyReset + 2, "each pending half body completed stale exactly once");
        Assert_Equals_Int(_Fixture.Get_Generation(), _GenerationBeforeBodyReset + 1, "the reset built exactly one new generation");
        Assert_Equals_Int(_Fixture.Get_CutsCommitted(), 0, "no cut was committed");
        Assert_Equals_Int(_Fixture.Get_PartialFailures(), 0, "the stale bodies were not mistaken for a partial composition");
        Assert_False(ck::IsValid(_SecondSpecimen), "the second generation's specimen is torn down");
        Assert_Equals_Int(_Fixture.Get_PieceCount(), 1, "only the rebuilt specimen exists");
        Assert_Equals_Int(_Fixture.Get_ReadyBodyCount(), 1, "the only Ready body is the rebuilt specimen's");
        Assert_False(_Fixture.Get_IsCutInFlight(), "no cut is pending");
    }
}
