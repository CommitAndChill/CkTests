// Language=angelscript

//============================================================================
// CK PMG - AUTOMATION TEST: A REQUEST FROM THE CAMERA GROUP IS DRAWN THE SAME FRAME
//============================================================================
//
// A consumer that computes its points from final poses issues its SetPoints
// from FGroup_Gameplay_Camera. The polyline drains in FGroup_PostTransform,
// after that group, so the new path must be drawn in the frame it was issued.
//
// A FGroup_Gameplay_Camera processor issues a SetPoints with K points and
// stamps utils_time::Get_FrameCounter(); a FGroup_Overlap processor (the
// post-physics group, after the whole pre-physics graph) records, in the same
// frame, the frame counter and Get_NumDrawnPoints. Three consecutive frames,
// three different K: each must be observed in its own issue frame with its own
// K. A drain that ran before the camera group would observe the PREVIOUS K.
//============================================================================

struct FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe
{
    FCk_Handle_Pmg_Polyline Polyline;

    TArray<int32> IssuedCounts;
    TArray<int64> IssuedFrames;
    TArray<int32> ObservedCounts;
    TArray<int64> ObservedFrames;
}

namespace ck_autotest_pmg_polyline_same_frame
{
    const int32 NumSteps = 3;

    int32 Get_PointCountForStep(int32 InStep)
    {
        if (InStep == 0)
        { return 3; }

        if (InStep == 1)
        { return 7; }

        return 12;
    }
}

class UCk_TESTONLY_Processor_PmgPolylineSameFrameProducer : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Gameplay_Camera";

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        for (int32 Index = 0; Index < Batch.Num(); ++Index)
        {
            auto Entity = Batch.GetHandle(Index);
            auto& Probe = Entity.Get_Fragment(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe);

            if (Probe.IssuedCounts.Num() >= ck_autotest_pmg_polyline_same_frame::NumSteps)
            { continue; }

            // One step at a time: the next SetPoints goes out only once the previous one was observed.
            if (Probe.ObservedCounts.Num() != Probe.IssuedCounts.Num())
            { continue; }

            const auto PointCount = ck_autotest_pmg_polyline_same_frame::Get_PointCountForStep(Probe.IssuedCounts.Num());

            TArray<FVector> Points;
            for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
            {
                Points.Add(FVector(float32(PointIndex) * 25.0f, 0.0f, 100.0f));
            }

            utils_pmg_polyline::Request_SetPoints(Probe.Polyline, FCk_Request_Pmg_Polyline_SetPoints(Points));

            Probe.IssuedCounts.Add(PointCount);
            Probe.IssuedFrames.Add(utils_time::Get_FrameCounter());
        }
    }
}

class UCk_TESTONLY_Processor_PmgPolylineSameFrameObserver : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Overlap";

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        for (int32 Index = 0; Index < Batch.Num(); ++Index)
        {
            auto Entity = Batch.GetHandle(Index);
            auto& Probe = Entity.Get_Fragment(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe);

            if (Probe.ObservedCounts.Num() >= Probe.IssuedCounts.Num())
            { continue; }

            Probe.ObservedCounts.Add(utils_pmg_polyline::Get_NumDrawnPoints(Probe.Polyline));
            Probe.ObservedFrames.Add(utils_time::Get_FrameCounter());
        }
    }
}

class UCk_AutoTest_Pmg_Polyline_ARequestFromTheCameraGroupIsDrawnTheSameFrame : UCk_AutoTest_Base
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

        // Priming path: once it is drawn, Setup has run, so the measured requests cannot be held back by it.
        TArray<FVector> Points;
        Points.Add(FVector(0.0f, 0.0f, 100.0f));
        Points.Add(FVector(50.0f, 0.0f, 100.0f));
        utils_pmg_polyline::Request_SetPoints(_Polyline, FCk_Request_Pmg_Polyline_SetPoints(Points));

        WaitUntil(n"Check_Primed", n"OnPrimed");
    }

    UFUNCTION()
    private void Check_Primed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_pmg_polyline::Get_NumDrawnPoints(_Polyline) == 2);
    }

    UFUNCTION()
    private void OnPrimed(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        auto PolylineEntity = FCk_Handle(_Polyline);
        auto& Probe = PolylineEntity.AddOrGet_Fragment(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe);
        Probe.Polyline = _Polyline;

        WaitUntil(n"Check_AllStepsObserved", n"OnAllStepsObserved");
    }

    UFUNCTION()
    private void Check_AllStepsObserved(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        auto PolylineEntity = FCk_Handle(_Polyline);
        if (PolylineEntity.Has_Fragment(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe) == false)
        { Res.Set(false); return; }

        const auto& Probe = PolylineEntity.Get_Fragment(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe);
        Res.Set(Probe.ObservedCounts.Num() >= ck_autotest_pmg_polyline_same_frame::NumSteps);
    }

    UFUNCTION()
    private void OnAllStepsObserved(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        auto PolylineEntity = FCk_Handle(_Polyline);
        const auto& Probe = PolylineEntity.Get_Fragment(FCk_Fragment_TESTONLY_PmgPolylineSameFrameProbe);

        Assert_True(Probe.IssuedCounts.Num() == ck_autotest_pmg_polyline_same_frame::NumSteps
                 && Probe.IssuedFrames.Num() == ck_autotest_pmg_polyline_same_frame::NumSteps
                 && Probe.ObservedFrames.Num() == ck_autotest_pmg_polyline_same_frame::NumSteps,
            "The camera-group producer should have issued exactly three SetPoints");
        if (IsFinished()) { return; }

        for (int32 Step = 0; Step < ck_autotest_pmg_polyline_same_frame::NumSteps; ++Step)
        {
            Assert_True(Probe.ObservedFrames[Step] == Probe.IssuedFrames[Step],
                f"Step {Step}: the observation must be taken in the issue frame; issued [{Probe.IssuedFrames[Step]}], observed [{Probe.ObservedFrames[Step]}]");
            Assert_True(Probe.ObservedCounts[Step] == Probe.IssuedCounts[Step],
                f"Step {Step}: a SetPoints issued from FGroup_Gameplay_Camera must be drawn the same frame; issued K [{Probe.IssuedCounts[Step]}], drawn [{Probe.ObservedCounts[Step]}]");

            if (Step > 0)
            {
                Assert_True(Probe.IssuedFrames[Step] == Probe.IssuedFrames[Step - 1] + 1,
                    f"Step {Step}: the steps must run on consecutive frames; previous [{Probe.IssuedFrames[Step - 1]}], this [{Probe.IssuedFrames[Step]}]");
                Assert_True(Probe.IssuedCounts[Step] != Probe.IssuedCounts[Step - 1],
                    f"Step {Step}: consecutive K must differ, or a one-frame lag could pass unseen");
            }
        }

        FinishSuccess();
    }
}
