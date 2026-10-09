// Language=angelscript

// Drives the Runtime Convex gym's slice fixture through a committed cut with no separation kick. Both halves are
// admitted at the source's pose while the source still exists, so if the source still collided with them for that
// step the position solver would push them apart. With a 0 cm/s kick their centroids must stay exactly as far
// apart along the cut normal as the cut made them.
class UCk_AutoTest_RuntimeConvexGym_CommitWithoutImpulseDoesNotPushHalvesApart : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 30.0f;
    default _AutoStageOriginField = false;

    private UCk_RuntimeConvexGym_SliceFixture _Fixture;
    private FCk_Handle _Source;
    private FTransform _SourcePose;
    private float _ExpectedSeparationCm = 0.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Fixture = NewObject(this, UCk_RuntimeConvexGym_SliceFixture);
        _Fixture.Request_Build(InHandle, FVector(0.0, 47000.0, 20000.0));

        Add_Step_WaitUntil("the specimen's body is Ready and asleep", n"Check_SpecimenCuttable");
        Add_Step("cut the specimen with no separation kick", n"Step_CutWithoutKick");
        Add_Step_WaitUntil("the cut commits", n"Check_CutCommitted");
        Add_Step_WaitUntil("the source is gone", n"Check_SourceGone");
        Add_Step("assert both halves are Ready and record the separation the cut made", n"Step_RecordExpectedSeparation");
        Add_Step_WaitUntil("both halves come to rest asleep", n"Check_HalvesAsleep");
        Add_Step("assert the halves were not pushed apart", n"Step_AssertSeparationUnchanged");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_SpecimenCuttable(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_CanCut(0));
    }

    UFUNCTION()
    private void Step_CutWithoutKick(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Source = FCk_Handle(_Fixture.Get_Piece(0).Entity);
        _SourcePose = utils_transform::Get_EntityCurrentTransform(_Fixture.Get_Piece(0).Entity);
        auto Cut = FCkRuntimeConvexGym_CutRequest();
        Cut.SeparationSpeedCmS = 0.0;
        Assert_True(_Fixture.Request_Cut(Cut), "the sleeping specimen accepts a cut");
    }

    UFUNCTION()
    private void Check_CutCommitted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_CutsCommitted() == 1);
    }

    UFUNCTION()
    private void Check_SourceGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Source));
    }

    UFUNCTION()
    private void Step_RecordExpectedSeparation(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Fixture.Get_PieceCount(), 2, "the cut left exactly two pieces");
        Assert_Equals_Int(_Fixture.Get_ReadyBodyCount(), 2, "both halves' bodies are Ready");

        // Both halves keep the source's local frame and were posed at the source's transform, so the separation the
        // cut itself made is the source pose applied to the two local centroids.
        const auto Positive = _Fixture.Get_Piece(0);
        const auto Negative = _Fixture.Get_Piece(1);
        const auto PositiveCentroid = _SourcePose.TransformPosition(utils_runtime_mesh::Get_Metrics(Positive.Geometry).Get_CentroidCm());
        const auto NegativeCentroid = _SourcePose.TransformPosition(utils_runtime_mesh::Get_Metrics(Negative.Geometry).Get_CentroidCm());
        _ExpectedSeparationCm = (PositiveCentroid - NegativeCentroid).DotProduct(_Fixture.Get_LastCutWorldNormal());
        Assert_True(_ExpectedSeparationCm > 1.0, "the positive half's centroid lies on the +normal side of the negative half's");
    }

    UFUNCTION()
    private void Check_HalvesAsleep(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_PieceCount() == 2 && _Fixture.Get_IsSettled(0) && _Fixture.Get_IsSettled(1));
    }

    UFUNCTION()
    private void Step_AssertSeparationUnchanged(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Positive = _Fixture.Get_Piece(0);
        const auto Negative = _Fixture.Get_Piece(1);
        const auto PositiveCentroid = utils_transform::Get_EntityCurrentTransform(Positive.Entity)
            .TransformPosition(utils_runtime_mesh::Get_Metrics(Positive.Geometry).Get_CentroidCm());
        const auto NegativeCentroid = utils_transform::Get_EntityCurrentTransform(Negative.Entity)
            .TransformPosition(utils_runtime_mesh::Get_Metrics(Negative.Geometry).Get_CentroidCm());
        const auto Separation = (PositiveCentroid - NegativeCentroid).DotProduct(_Fixture.Get_LastCutWorldNormal());
        Assert_Equals_Float(Separation, _ExpectedSeparationCm, 0.1,
            f"with no kick the halves stay {_ExpectedSeparationCm :.3} cm apart along the cut normal (measured {Separation :.3} cm)");
    }
}
