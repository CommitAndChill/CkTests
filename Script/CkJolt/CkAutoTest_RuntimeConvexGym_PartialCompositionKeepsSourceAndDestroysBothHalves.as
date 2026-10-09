// Language=angelscript

// Drives the Runtime Convex gym's slice fixture through a cut whose negative half is destroyed before its body is
// admitted (RuntimeConvex setup resolves Cancelled, which is not an ensure). The positive half reaches Ready, yet
// the cut is rejected as a whole: both halves are destroyed, and the source, isolated as a Kinematic body while the
// cut was in flight, becomes Dynamic again once they are gone.
class UCk_AutoTest_RuntimeConvexGym_PartialCompositionKeepsSourceAndDestroysBothHalves : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 30.0f;
    default _AutoStageOriginField = false;

    private UCk_RuntimeConvexGym_SliceFixture _Fixture;
    private FCk_Handle _Source;
    private FCk_Handle_JoltBody _SourceBody;
    private bool _SawSourceIsolated = false;
    private TArray<FCk_Handle> _Halves;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Fixture = NewObject(this, UCk_RuntimeConvexGym_SliceFixture);
        _Fixture.Request_Build(InHandle, FVector(0.0, 43000.0, 20000.0));

        Add_Step_WaitUntil("the specimen's body is Ready and asleep", n"Check_SpecimenCuttable");
        Add_Step("cut, destroying the negative half before its body is admitted", n"Step_CutWithCancelledHalf");
        Add_Step_WaitUntil("the cut resolves as a partial composition", n"Check_PartialCompositionResolved");
        Add_Step("assert which half failed and that the source was kept", n"Step_AssertResolution");
        Add_Step_WaitUntil("both halves are torn down", n"Check_HalvesGone");
        Add_Step_WaitUntil("the source is a Dynamic body again", n"Check_SourceRestored");
        Add_Step("assert the source is the only piece and is still Ready", n"Step_AssertSourceKept");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_SpecimenCuttable(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_CanCut(0));
    }

    UFUNCTION()
    private void Step_CutWithCancelledHalf(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Source = FCk_Handle(_Fixture.Get_Piece(0).Entity);
        _SourceBody = _Fixture.Get_Piece(0).Body;
        auto Cut = FCkRuntimeConvexGym_CutRequest();
        Cut.Interrupt = ECkRuntimeConvexGym_CutInterrupt::DestroyNegativeHalfBeforeAdmission;
        Assert_True(_Fixture.Request_Cut(Cut), "the sleeping specimen accepts a cut");
    }

    UFUNCTION()
    private void Check_PartialCompositionResolved(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        if (utils_jolt_body::Get_MotionType(_SourceBody) == ECk_MotionType::Kinematic)
        {
            _SawSourceIsolated = true;
        }
        auto Result = OutResult;
        Result.Set(_Fixture.Get_PartialFailures() == 1);
    }

    UFUNCTION()
    private void Step_AssertResolution(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Halves = _Fixture.Get_LastHalves();
        const auto Positive = _Fixture.Get_LastPositiveResolution();
        const auto Negative = _Fixture.Get_LastNegativeResolution();
        Assert_Equals_Int(_Halves.Num(), 2, "the cut composed both halves before one was destroyed");
        Assert_True(Positive.State == ECk_JoltBody_SetupState::Ready, "the untouched positive half's body was admitted");
        Assert_True(Negative.State == ECk_JoltBody_SetupState::Failed, "the destroyed negative half's body failed");
        Assert_True(Negative.Failure == ECk_JoltBody_SetupFailure::Cancelled, "pre-admission destruction resolves Cancelled");
        Assert_Equals_Int(_Fixture.Get_CutsCommitted(), 0, "a partial composition commits nothing");
        Assert_False(_Fixture.Get_IsCutInFlight(), "the cut is finished");
        Assert_Equals_Int(_Fixture.Get_StaleCompletions(), 0, "no completion was mistaken for a stale one");
        Assert_True(_SawSourceIsolated, "the source was a Kinematic body while the cut was in flight");
    }

    UFUNCTION()
    private void Check_SourceRestored(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_jolt_body::Get_MotionType(_SourceBody) == ECk_MotionType::Dynamic);
    }

    UFUNCTION()
    private void Check_HalvesGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto AllGone = _Halves.Num() == 2;
        for (auto Half : _Halves)
        {
            AllGone = AllGone && ck::Is_NOT_Valid(Half);
        }
        auto Result = OutResult;
        Result.Set(AllGone);
    }

    UFUNCTION()
    private void Step_AssertSourceKept(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(CkRuntimeConvexGym::Get_IsAlive(_Source), "the source survives the failed cut");
        Assert_Equals_Int(_Fixture.Get_PieceCount(), 1, "the source is the only piece");
        Assert_True(FCk_Handle(_Fixture.Get_Piece(0).Entity) == _Source, "the remaining piece is the original source");
        Assert_Equals_Int(_Fixture.Get_ReadyBodyCount(), 1, "the source's body is still Ready");
    }
}
