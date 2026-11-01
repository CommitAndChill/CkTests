// Language=angelscript

// The touchdown acceptance path must not mark a ray hit trusted beyond either hip's chain reach.
class UCk_AutoTest_ProceduralAnimation_BeamTouchdownStaysInReach : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 80.0f;
    default _AutoStageOriginField = false;

    private FCkProceduralAnimationGym_Fixture _Fixture;
    private TArray<bool> _WasSwinging;
    private int32 _TrustedTouchdowns = 0;
    private float _MaxSimulationExcess = 0.0;
    private float _MaxPosedExcess = 0.0;
    private FString _WorstSimulation = "none";
    private FString _WorstPosed = "none";
    private float _LastTraceAt = -1.0;
    private int32 _PreparingSamples = 0;
    private int32 _ActiveSamples = 0;
    private int32 _AdaptiveTrustedPlantSamples = 0;
    private float _MaxAdaptiveSimulationExcess = 0.0;
    private float _MaxAdaptivePosedExcess = 0.0;
    private bool _PreviousLoweredActive = false;
    private float _PreviousLoweredX = 0.0;
    private bool _SawLoweredActiveWalking = false;
    private int64 _LastAdaptiveSequence = -1;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        int64 MissingSequence = 42;
        bool MissingActive = true;
        bool MissingPreparing = true;
        Assert_True(UCk_Utils_AutoTest_UE::TryGet_ProceduralAnimationAdaptiveHeightSample(FCk_Handle(),
            MissingSequence, MissingActive, MissingPreparing) == false && MissingSequence == -1 &&
            MissingActive == false && MissingPreparing == false,
            "An unavailable accepted adaptive-height pair rejects and resets every output");
        auto Roster = TArray<ECkProceduralAnimationGym_Species>();
        Roster.Add(ECkProceduralAnimationGym_Species::Beast);
        if (_Fixture.Create_WithRoster(InHandle, FVector(120000.0, 345000.0, 600.0),
            ECkProceduralAnimationGym_Course::Beam, Roster, false, InAdaptiveHeight: true) == false)
        {
            FinishFailure("The isolated real-Jolt Beast beam could not be created");
            return;
        }
        Add_Step_WaitUntil("the Beast crosses the beam and returns", n"Check_Traversal", 0, 60.0f);
        Add_Step("verify trusted touchdown reach", n"Step_Check");
        Add_Step("retire the fixture", n"Step_Destroy");
        Add_Step_WaitUntil("fixture lifetime subtree is gone", n"Check_Destroyed", 0, 10.0f);
        Run_Steps(InHandle);
    }

    void SampleAdaptiveMotion()
    {
        if (_Fixture.Get_AllReady() == false || _Fixture.Crawlers.Num() != 1)
        {
            _PreviousLoweredActive = false;
            return;
        }
        auto Crawler = _Fixture.Crawlers[0];
        // The AS check may precede physics: observe one matched accepted gait/motion pair, once per solve.
        int64 Sequence = -1;
        bool Active = false;
        bool Preparing = false;
        if (UCk_Utils_AutoTest_UE::TryGet_ProceduralAnimationAdaptiveHeightSample(Crawler.Handles.Root,
            Sequence, Active, Preparing) == false)
        {
            _PreviousLoweredActive = false;
            return;
        }
        if (Sequence <= _LastAdaptiveSequence)
        {
            return;
        }
        _LastAdaptiveSequence = Sequence;
        if (Preparing)
        {
            _PreparingSamples++;
        }
        if (Active)
        {
            _ActiveSamples++;
        }
        auto Body = utils_transform::Get_EntityCurrentTransform(Crawler.Handles.Root);
        auto Posed = utils_transform::Get_EntityCurrentTransform(Crawler.Handles.Presentation);
        if (Preparing || Active)
        {
            for (auto Leg : Crawler.Handles.Legs)
            {
                if (ck::Is_NOT_Valid(Leg) || utils_procedural_leg::Get_EnableDisable(Leg) != ECk_EnableDisable::Enable)
                {
                    continue;
                }
                auto Foot = utils_procedural_leg::Get_Foot(Leg);
                if (Foot.Get_Phase() != ECk_ProceduralLeg_FootPhase::Planted ||
                    Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted)
                {
                    continue;
                }
                _AdaptiveTrustedPlantSamples++;
                auto Reach = 0.0;
                for (auto Length : utils_procedural_leg::Get_ChainGeometry(Leg).Get_SegmentLengths())
                {
                    Reach += Length;
                }
                auto Hip = utils_procedural_leg::Get_Placement(Leg).Get_HipLocal();
                _MaxAdaptiveSimulationExcess = Math::Max(_MaxAdaptiveSimulationExcess,
                    (Foot.Get_Position() - Body.TransformPosition(Hip)).Size() - Reach);
                _MaxAdaptivePosedExcess = Math::Max(_MaxAdaptivePosedExcess,
                    (Foot.Get_Position() - Posed.TransformPosition(Hip)).Size() - Reach);
            }
        }
        auto Local = Body.GetLocation() - Crawler.Layout.Origin;
        auto Profile = ck_procedural_gym::Get_SpeciesProfile(Crawler.Layout.Species);
        auto LoweredActive = Active && Preparing == false &&
            Math::Abs(Local.X) < ck_procedural_gym::BeamHalfLength - Profile.BodyHalfExtents.X &&
            Local.Z > ck_procedural_gym::BeamHeight + Profile.BodyHalfExtents.Z &&
            Local.Z < ck_procedural_gym::BeamHeight + Profile.Clearance - 1.0;
        if (LoweredActive && _PreviousLoweredActive && Math::Abs(Local.X - _PreviousLoweredX) > 0.1)
        {
            _SawLoweredActiveWalking = true;
        }
        _PreviousLoweredActive = LoweredActive;
        _PreviousLoweredX = Local.X;
    }

    void SampleTouchdowns()
    {
        if (_Fixture.Get_AllReady() == false || _Fixture.Crawlers.Num() != 1)
        {
            return;
        }
        auto Crawler = _Fixture.Crawlers[0];
        auto Body = utils_transform::Get_EntityCurrentTransform(Crawler.Handles.Root);
        auto Posed = utils_procedural_body_pose::Get_Offset(Crawler.Handles.BodyPose) * Body;
        if (_WasSwinging.Num() != Crawler.Handles.Legs.Num())
        {
            _WasSwinging.SetNum(Crawler.Handles.Legs.Num());
        }
        for (auto Index = 0; Index < Crawler.Handles.Legs.Num(); Index++)
        {
            auto Leg = Crawler.Handles.Legs[Index];
            if (ck::Is_NOT_Valid(Leg) || utils_procedural_leg::Get_EnableDisable(Leg) != ECk_EnableDisable::Enable)
            {
                continue;
            }
            auto Foot = utils_procedural_leg::Get_Foot(Leg);
            auto Swinging = Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Swinging;
            if (_WasSwinging[Index] && Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted &&
                Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted)
            {
                _TrustedTouchdowns++;
                auto Reach = 0.0;
                auto Lengths = utils_procedural_leg::Get_ChainGeometry(Leg).Get_SegmentLengths();
                for (auto Length : Lengths)
                {
                    Reach += Length;
                }
                auto HipLocal = utils_procedural_leg::Get_Placement(Leg).Get_HipLocal();
                auto SimulationExcess = (Foot.Get_Position() - Body.TransformPosition(HipLocal)).Size() - Reach;
                auto PosedExcess = (Foot.Get_Position() - Posed.TransformPosition(HipLocal)).Size() - Reach;
                auto LocalX = Body.GetLocation().X - Crawler.Layout.Origin.X;
                if (SimulationExcess > _MaxSimulationExcess)
                {
                    _MaxSimulationExcess = SimulationExcess;
                    _WorstSimulation = f"{utils_procedural_leg::Get_Id(Leg)} at x {LocalX :.1}";
                }
                if (PosedExcess > _MaxPosedExcess)
                {
                    _MaxPosedExcess = PosedExcess;
                    _WorstPosed = f"{utils_procedural_leg::Get_Id(Leg)} at x {LocalX :.1}";
                }
            }
            _WasSwinging[Index] = Swinging;
        }
    }

    UFUNCTION()
    private void Check_Traversal(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update();
        if (_Fixture.CompositionError.IsEmpty() == false)
        {
            FinishFailure(_Fixture.CompositionError);
            return;
        }
        SampleAdaptiveMotion();
        SampleTouchdowns();
        auto Now = float(System::GetGameTimeInSeconds());
        if (_LastTraceAt < 0.0 || Now - _LastTraceAt >= 5.0)
        {
            _LastTraceAt = Now;
            auto Stage = _Fixture.Crawlers.Num() == 1 ? _Fixture.Crawlers[0].Progress.RouteStage : -1;
            ck::Trace(f"[BeamTouchdown] stage {Stage}, trusted touchdowns {_TrustedTouchdowns}, max sim excess {_MaxSimulationExcess :.2}, max posed excess {_MaxPosedExcess :.2}",
                n"BeamTouchdown", 0.0f);
            if (_Fixture.Crawlers.Num() > 0 && ck::IsValid(_Fixture.Crawlers[0].Handles.Root))
            {
                auto Observed = _Fixture.Crawlers[0];
                auto Body = utils_transform::Get_EntityCurrentTransform(Observed.Handles.Root);
                auto Local = Body.GetLocation() - Observed.Layout.Origin;
                auto Rotation = Body.GetRotation().Rotator();
                auto MotionStatus = utils_surface_motion::Get_Status(Observed.Handles.Motion);
                auto MotionFailure = utils_surface_motion::Get_Failure(Observed.Handles.Motion);
                auto PoseStatus = utils_procedural_body_pose::Get_Status(Observed.Handles.BodyPose);
                auto PoseFailure = utils_procedural_body_pose::Get_Failure(Observed.Handles.BodyPose);
                auto Active = utils_surface_motion::Get_AdaptiveHeightActive(Observed.Handles.Motion);
                auto Preparing = utils_surface_motion::Get_AdaptiveHeightPreparing(Observed.Handles.Motion);
                auto Trials = utils_surface_motion::Get_HeightPlanningTrials(Observed.Handles.Motion);
                auto Rays = utils_surface_motion::Get_HeightPlanningRays(Observed.Handles.Motion);
                auto Envelope = utils_surface_motion::Get_HeightEnvelopeQueries(Observed.Handles.Motion);
                ck::Trace(f"[BeamState] bodyLocal ({Local.X :.2},{Local.Y :.2},{Local.Z :.2}) yaw {Rotation.Yaw :.2} pitch {Rotation.Pitch :.2} motion {MotionStatus}/{MotionFailure} pose {PoseStatus}/{PoseFailure} active {Active} preparing {Preparing} trials {Trials} rays {Rays} envelope {Envelope} acceptedSequence {_LastAdaptiveSequence}",
                    n"BeamState", 0.0f);
            }
        }
        auto Complete = _Fixture.Crawlers.Num() == 1 && _Fixture.Crawlers[0].Get_HasCompletedCourse() &&
            _Fixture.Get_HasObservedWalking();
        auto Result = OutResult;
        Result.Set(Complete);
    }

    UFUNCTION()
    private void Step_Check(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Crawler = _Fixture.Crawlers[0];
        Assert_True(Crawler.Progress.Traversals > 0 && Crawler.Evidence.InvalidOutput == false,
            "The Beast completed the real beam route with finite ready output");
        Assert_True(_TrustedTouchdowns >= Crawler.Layout.LegCount,
            f"Sampled at least a leg-count of swing-to-trusted-plant transitions ({_TrustedTouchdowns})");
        Assert_Equals_Int(Crawler.Get_ReplantedCount(), Crawler.Layout.LegCount,
            "Every Beast leg replanted on trusted contact");
        Assert_True(_MaxSimulationExcess <= 0.5,
            f"A trusted touchdown stayed within simulation-hip chain reach (worst {_MaxSimulationExcess :.2} cm at {_WorstSimulation})");
        Assert_True(_MaxPosedExcess <= 0.5,
            f"A trusted touchdown stayed within posed-hip chain reach (worst {_MaxPosedExcess :.2} cm at {_WorstPosed})");
        Assert_True(_PreparingSamples > 0 || _ActiveSamples > 0,
            f"Fresh accepted-frame samples exercised adaptive height (preparing {_PreparingSamples}, active {_ActiveSamples})");
        Assert_True(_AdaptiveTrustedPlantSamples > 0,
            f"Observed trusted planted feet while adaptive height prepared or ran ({_AdaptiveTrustedPlantSamples})");
        Assert_True(_MaxAdaptiveSimulationExcess <= 0.5,
            f"Adaptive preparation and motion retained all observed trusted simulation-hip reaches ({_MaxAdaptiveSimulationExcess :.2} cm)");
        Assert_True(_MaxAdaptivePosedExcess <= 0.5,
            f"Adaptive preparation and motion retained all observed trusted posed-hip reaches ({_MaxAdaptivePosedExcess :.2} cm)");
        Assert_True(_SawLoweredActiveWalking,
            "The Beast made actual horizontal progress with fresh active adaptive height below its preferred beam clearance");
    }

    UFUNCTION()
    private void Step_Destroy(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
