// Language=angelscript

//============================================================================
// CK PMG - AUTOMATION TEST: Pmg_Polyline DESTROYED MID-REQUEST LEAVES NOTHING
//============================================================================
//
// A SetPoints followed by Request_DestroyEntity in the same frame: the queued
// request is cancelled rather than drained, EndPlay destroys the mesh
// component, no ensure fires, and the next frame the handle is invalid. The
// polyline is set up and drawing before the destroy, so EndPlay has a real
// component to tear down.
//============================================================================

class UCk_AutoTest_Pmg_Polyline_DestroyedMidRequestLeavesNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private FCk_Handle_Pmg_Polyline _Polyline;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);

        auto Spec = FCk_Pmg_Polyline_Spec();
        Spec.Set_Material(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));

        _Polyline = utils_pmg_polyline::Create(Owner, Spec);
        Assert_True(ck::IsValid(_Polyline), "Create should return a valid FCk_Handle_Pmg_Polyline");
        if (IsFinished()) { return; }

        TArray<FVector> Points;
        Points.Add(FVector(0.0f, 0.0f, 100.0f));
        Points.Add(FVector(80.0f, 0.0f, 100.0f));
        utils_pmg_polyline::Request_SetPoints(_Polyline, FCk_Request_Pmg_Polyline_SetPoints(Points));

        WaitUntil(n"Check_Drawing", n"OnDrawing");
    }

    UFUNCTION()
    private void Check_Drawing(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_pmg_polyline::Get_NumDrawnPoints(_Polyline) == 2);
    }

    UFUNCTION()
    private void OnDrawing(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        TArray<FVector> Points;
        Points.Add(FVector(0.0f, 0.0f, 100.0f));
        Points.Add(FVector(0.0f, 80.0f, 100.0f));
        Points.Add(FVector(0.0f, 160.0f, 100.0f));
        utils_pmg_polyline::Request_SetPoints(_Polyline, FCk_Request_Pmg_Polyline_SetPoints(Points));

        utils_entity_lifetime::Request_DestroyEntity(FCk_Handle(_Polyline));

        WaitFrames(1, n"OnNextFrame");
    }

    UFUNCTION()
    private void OnNextFrame(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        Assert_True(ck::Is_NOT_Valid(_Polyline),
            "The frame after the destroy, the polyline handle must be invalid");
        if (IsFinished()) { return; }

        // A negative: nothing may ensure while the destruction pipeline (EndPlay, Teardown, Await,
        // Finalize: two frames after the destroy frame) runs. There is no state to wait on, so a
        // fixed span that covers the pipeline keeps any ensure inside this test.
        WaitFrames(4, n"OnDestructionSettled");
    }

    UFUNCTION()
    private void OnDestructionSettled(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        Assert_True(ck::Is_NOT_Valid(_Polyline),
            "The destroyed polyline handle must stay invalid");

        _Polyline = utils_pmg_polyline::Get_InvalidHandle();
        FinishSuccess();
    }
}
