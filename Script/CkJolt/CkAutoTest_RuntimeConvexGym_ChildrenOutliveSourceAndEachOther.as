// Language=angelscript

// Drives the Runtime Convex gym's slice fixture through a committed cut: the source is destroyed once both halves'
// bodies are Ready and both halves survive it, owned by the fixture's pieces owner rather than by the source. Then
// one half is destroyed and the other keeps its entity, display and body.
class UCk_AutoTest_RuntimeConvexGym_ChildrenOutliveSourceAndEachOther : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 30.0f;
    default _AutoStageOriginField = false;

    private UCk_RuntimeConvexGym_SliceFixture _Fixture;
    private FCk_Handle _Source;
    private FCkRuntimeConvexGym_Piece _Destroyed;
    private FCkRuntimeConvexGym_Piece _Survivor;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Fixture = NewObject(this, UCk_RuntimeConvexGym_SliceFixture);
        _Fixture.Request_Build(InHandle, FVector(0.0, 45000.0, 20000.0));

        Add_Step_WaitUntil("the specimen's body is Ready and asleep", n"Check_SpecimenCuttable");
        Add_Step("cut the specimen", n"Step_Cut");
        Add_Step_WaitUntil("the cut commits", n"Check_CutCommitted");
        Add_Step_WaitUntil("the source is destroyed and both half displays are Ready", n"Check_SourceGoneAndDisplaysReady");
        Add_Step("assert both halves outlive the source and destroy one", n"Step_AssertHalvesAndDestroyOne");
        Add_Step_WaitUntil("the destroyed half is gone", n"Check_DestroyedHalfGone");
        Add_Step("assert the other half is intact", n"Step_AssertSurvivor");
        Add_Step_WaitSeconds("give a late cascade time to reach the survivor", 0.3f);
        Add_Step("assert the survivor is still intact", n"Step_AssertSurvivor");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_SpecimenCuttable(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_CanCut(0));
    }

    UFUNCTION()
    private void Step_Cut(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Source = FCk_Handle(_Fixture.Get_Piece(0).Entity);
        auto Cut = FCkRuntimeConvexGym_CutRequest();
        Cut.SeparationSpeedCmS = 30.0;
        Assert_True(_Fixture.Request_Cut(Cut), "the sleeping specimen accepts a cut");
    }

    UFUNCTION()
    private void Check_CutCommitted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_CutsCommitted() == 1);
    }

    UFUNCTION()
    private void Check_SourceGoneAndDisplaysReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto DisplaysReady = _Fixture.Get_PieceCount() == 2;
        for (auto Index = 0; Index < _Fixture.Get_PieceCount(); Index++)
        {
            const auto Display = _Fixture.Get_Piece(Index).Display;
            DisplaysReady = DisplaysReady && ck::IsValid(Display)
                && utils_runtime_mesh_display::Get_SetupState(Display) == ECk_RuntimeMesh_SetupState::Ready;
        }
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Source) && DisplaysReady);
    }

    UFUNCTION()
    private void Step_AssertHalvesAndDestroyOne(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Fixture.Get_PieceCount(), 2, "the cut left exactly two pieces");
        Assert_Equals_Int(_Fixture.Get_ReadyBodyCount(), 2, "both halves' bodies are Ready after the source is gone");
        for (auto Index = 0; Index < 2; Index++)
        {
            const auto Piece = _Fixture.Get_Piece(Index);
            Assert_True(ck::IsValid(Piece.Entity), "a half outlives the source");
            Assert_True(utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(Piece.Entity)) == _Fixture.Get_PiecesOwner(),
                "a half is owned by the fixture's pieces owner, not by the source");
            Assert_True(Piece.MassKg > 0.0, "a half carries a positive explicit mass");
        }
        const auto TotalMass = _Fixture.Get_Piece(0).MassKg + _Fixture.Get_Piece(1).MassKg;
        Assert_Equals_Float(TotalMass, 1.0, 0.001, "the halves split the 1 kg source mass between them");

        _Destroyed = _Fixture.Get_Piece(0);
        _Survivor = _Fixture.Get_Piece(1);
        Assert_True(_Fixture.Request_DestroyPiece(0), "a piece can be destroyed when no cut is in flight");
    }

    UFUNCTION()
    private void Check_DestroyedHalfGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Destroyed.Entity));
    }

    UFUNCTION()
    private void Step_AssertSurvivor(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(CkRuntimeConvexGym::Get_IsAlive(_Survivor.Entity), "the other half lives");
        Assert_True(utils_jolt_body::Get_SetupState(_Survivor.Body) == ECk_JoltBody_SetupState::Ready, "the other half keeps its Ready body");
        Assert_True(utils_runtime_mesh_display::Get_SetupState(_Survivor.Display) == ECk_RuntimeMesh_SetupState::Ready,
            "the other half keeps its Ready display");
        Assert_Equals_Int(_Fixture.Get_PieceCount(), 1, "the fixture holds only the survivor");
        Assert_True(_Fixture.Get_Piece(0).Entity == _Survivor.Entity, "the remaining piece is the survivor");
    }
}
