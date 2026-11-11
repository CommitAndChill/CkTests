// Language=angelscript

//============================================================================
// CK PMG - AUTOMATION TEST: Pmg_Polyline SETPOINTS DRAWS AND CLEARS
//============================================================================
//
// Three points draw three points over the path's length; an empty SetPoints
// clears the drawing; of two SetPoints enqueued in the same frame only the
// second is drawn.
//============================================================================

class UCk_AutoTest_Pmg_Polyline_SetPoints_DrawsAndClears : UCk_AutoTest_Base
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
        Spec.Set_Radius(2.0f);

        _Polyline = utils_pmg_polyline::Create(Owner, Spec);
        Assert_True(ck::IsValid(_Polyline), "Create should return a valid FCk_Handle_Pmg_Polyline");
        if (IsFinished()) { return; }

        TArray<FVector> Points;
        Points.Add(FVector(0.0f, 0.0f, 100.0f));
        Points.Add(FVector(300.0f, 0.0f, 100.0f));
        Points.Add(FVector(300.0f, 400.0f, 100.0f));
        utils_pmg_polyline::Request_SetPoints(_Polyline, FCk_Request_Pmg_Polyline_SetPoints(Points));

        WaitUntil(n"Check_ThreePointsDrawn", n"OnThreePointsDrawn");
    }

    UFUNCTION()
    private void Check_ThreePointsDrawn(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_pmg_polyline::Get_NumDrawnPoints(_Polyline) == 3);
    }

    UFUNCTION()
    private void OnThreePointsDrawn(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        const auto DrawnLength = utils_pmg_polyline::Get_DrawnLength(_Polyline);
        Assert_True(Math::Abs(DrawnLength - 700.0f) <= 0.1f,
            f"The drawn length should be the polyline's length (300 + 400 = 700 cm), got [{DrawnLength}]");
        Assert_True(Math::Abs(utils_pmg_polyline::Get_Radius(_Polyline) - 2.0f) <= 0.001f,
            "The radius should be the Spec's");
        if (IsFinished()) { return; }

        TArray<FVector> NoPoints;
        utils_pmg_polyline::Request_SetPoints(_Polyline, FCk_Request_Pmg_Polyline_SetPoints(NoPoints));

        WaitUntil(n"Check_Cleared", n"OnCleared");
    }

    UFUNCTION()
    private void Check_Cleared(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_pmg_polyline::Get_NumDrawnPoints(_Polyline) == 0);
    }

    UFUNCTION()
    private void OnCleared(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        Assert_True(utils_pmg_polyline::Get_DrawnLength(_Polyline) == 0.0f,
            "An empty SetPoints should leave no drawn length");
        if (IsFinished()) { return; }

        TArray<FVector> FirstPoints;
        FirstPoints.Add(FVector(0.0f, 0.0f, 50.0f));
        FirstPoints.Add(FVector(100.0f, 0.0f, 50.0f));

        TArray<FVector> SecondPoints;
        SecondPoints.Add(FVector(0.0f, 0.0f, 50.0f));
        SecondPoints.Add(FVector(0.0f, 100.0f, 50.0f));
        SecondPoints.Add(FVector(0.0f, 200.0f, 50.0f));
        SecondPoints.Add(FVector(0.0f, 300.0f, 50.0f));

        utils_pmg_polyline::Request_SetPoints(_Polyline, FCk_Request_Pmg_Polyline_SetPoints(FirstPoints));
        utils_pmg_polyline::Request_SetPoints(_Polyline, FCk_Request_Pmg_Polyline_SetPoints(SecondPoints));

        WaitUntil(n"Check_SomethingDrawn", n"OnSomethingDrawn");
    }

    UFUNCTION()
    private void Check_SomethingDrawn(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_pmg_polyline::Get_NumDrawnPoints(_Polyline) != 0);
    }

    UFUNCTION()
    private void OnSomethingDrawn(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        const auto NumDrawn = utils_pmg_polyline::Get_NumDrawnPoints(_Polyline);
        const auto DrawnLength = utils_pmg_polyline::Get_DrawnLength(_Polyline);
        Assert_True(NumDrawn == 4,
            f"Of two SetPoints in one frame the second (4 points) should be drawn, got [{NumDrawn}] points");
        Assert_True(Math::Abs(DrawnLength - 300.0f) <= 0.1f,
            f"The drawn length should be the second path's (300 cm), got [{DrawnLength}]");

        FinishSuccess();
    }
}
