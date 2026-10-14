// Language=angelscript

// A walking walker's presentation is posed at Get_Offset * Body on every update, and its body pose stays Ready.
class UCk_AutoTest_ProceduralAnimation_BodyPosePresentationTracksOffset : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(180000.0, 20000.0, 600.0);
    private float _PhaseStart = 0.0;
    private FVector _StartLocation;
    private int32 _Polls = 0;
    private float _MaxPresentationError = 0.0;
    private float _MaxPresentationAngle = 0.0;

    FCkProceduralAnimationGym_CrawlerHandles Get_Handles() const
    {
        return _Fixture.Crawlers[0].Handles;
    }

    float Get_Elapsed() const
    {
        return float(System::GetGameTimeInSeconds()) - _PhaseStart;
    }

    float Get_AngleDegrees(FTransform InA, FTransform InB) const
    {
        return Math::RadiansToDegrees(InA.GetRotation().AngularDistance(InB.GetRotation()));
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
        Add_Step_WaitUntil("the walker walks for 2 s", n"Check_Walked");
        Add_Step("verify the presentation tracked the offset on every poll", n"Step_Verify");
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
            _StartLocation = utils_transform::Get_EntityCurrentTransform(Get_Handles().Root).GetLocation();
        }
        auto Result = OutResult;
        Result.Set(Ready);
    }

    void Sample()
    {
        auto Handles = Get_Handles();
        auto Body = utils_transform::Get_EntityCurrentTransform(Handles.Root);
        auto Presentation = utils_transform::Get_EntityCurrentTransform(Handles.Presentation);
        auto Posed = utils_procedural_body_pose::Get_Offset(Handles.BodyPose) * Body;

        _Polls++;
        _MaxPresentationError = Math::Max(_MaxPresentationError, (Presentation.GetLocation() - Posed.GetLocation()).Size());
        _MaxPresentationAngle = Math::Max(_MaxPresentationAngle, Get_AngleDegrees(Presentation, Posed));
    }

    UFUNCTION()
    private void Check_Walked(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(true);
        Sample();
        auto Result = OutResult;
        Result.Set(Get_Elapsed() >= 2.0);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Handles = Get_Handles();
        auto Travelled = (utils_transform::Get_EntityCurrentTransform(Handles.Root).GetLocation() - _StartLocation).Size();
        ck::Trace(f"[BODY-POSE-PRESENTATION] {_Polls} polls, travelled {Travelled :.1} cm, presentation error {_MaxPresentationError :.4} cm / {_MaxPresentationAngle :.4} deg");
        Assert_True(_Polls > 30, f"The walker was sampled throughout the 2 s walk ({_Polls} polls)");
        Assert_True(Travelled > 100.0, f"The walker walked while it was sampled ({Travelled :.1} cm)");
        Assert_True(_MaxPresentationError < 0.05 && _MaxPresentationAngle < 0.05,
            f"The presentation sits at Get_Offset * Body on every poll ({_MaxPresentationError :.4} cm, {_MaxPresentationAngle :.4} deg)");
        Assert_True(utils_procedural_body_pose::Get_Status(Handles.BodyPose) == ECk_ProceduralAnimation_Status::Ready,
            "The body pose stays Ready");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
