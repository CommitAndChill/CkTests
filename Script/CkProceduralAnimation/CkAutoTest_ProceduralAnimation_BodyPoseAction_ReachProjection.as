// Language=angelscript

// An action far beyond the legs' reach is projected onto what the trusted planted feet allow: the accepted offset stays
// finite, every drawn hip stays within its chain of its planted foot, and the published sample is the presentation.
class UCk_AutoTest_ProceduralAnimation_BodyPoseAction_ReachProjection : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(180000.0, 30000.0, 600.0);
    private FVector _Overreach = FVector(300.0, 0.0, 0.0);
    // The reach tolerance the posed-reach tests use.
    private float _ReachTolerance = 0.5;
    private float _PhaseStart = 0.0;
    private FTransform _BaseOffset;
    private FTransform _AcceptedOffset;
    private FCk_ProceduralBodyPose_ActionSample _FirstSample;
    private FTransform _PresentationAtFirstSample;
    private int32 _CheckedLegs = 0;
    private float _WorstReachExcess = -1000.0;
    private float _WorstSampleHip = 0.0;
    private int32 _ClearCompletions = 0;

    FCkProceduralAnimationGym_CrawlerHandles Get_Handles() const
    {
        return _Fixture.Crawlers[0].Handles;
    }

    float Get_Elapsed() const
    {
        return float(System::GetGameTimeInSeconds()) - _PhaseStart;
    }

    // Segments are centred on their transform with their length along local +X, so the hip is half a segment back.
    private bool TryGet_RigHip(FCk_Handle_ProceduralLeg InLeg, FVector& OutHip) const
    {
        auto Rig = utils_procedural_rig::DoCast(InLeg);
        if (Rig.IsSet() == false)
        {
            return false;
        }
        auto Segments = utils_procedural_rig::Get_Chain(Rig.GetValue()).Get_Segments();
        auto Lengths = utils_procedural_leg::Get_ChainGeometry(InLeg).Get_SegmentLengths();
        if (Segments.Num() == 0 || Lengths.Num() == 0)
        {
            return false;
        }
        auto Segment = utils_transform::Get_EntityCurrentTransform(Segments[0]);
        OutHip = Segment.GetLocation() - Segment.GetRotation().GetForwardVector() * (Lengths[0] * 0.5);
        return true;
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
        Add_Step_WaitUntil("the stationary walker settles for 1 s", n"Check_Settled");
        Add_Step("request a 300 cm action", n"Step_Submit");
        Add_Step_WaitUntil("the first sample carrying the action's id is published", n"Check_FirstSample");
        Add_Step("verify the accepted pose is constrained, connected and published", n"Step_Verify");
        Add_Step_WaitUntil("the identity clear lands", n"Check_Cleared");
        Add_Step("retire the fixture", n"Step_Destroy");
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
        }
        auto Result = OutResult;
        Result.Set(Ready);
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(Get_Elapsed() >= 1.0);
    }

    UFUNCTION()
    private void Step_Submit(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Handles = Get_Handles();
        _BaseOffset = utils_procedural_body_pose::Get_Offset(Handles.BodyPose);
        utils_procedural_body_pose::Request_SetActionOffset(Handles.BodyPose,
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform(_Overreach), int64(1)));
    }

    UFUNCTION()
    private void Check_FirstSample(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Handles = Get_Handles();
        auto Sample = utils_procedural_body_pose::Get_ActionSample(Handles.BodyPose);
        auto Landed = Sample.Get_SampleId() == 1;
        if (Landed)
        {
            _FirstSample = Sample;
            _AcceptedOffset = utils_procedural_body_pose::Get_Offset(Handles.BodyPose);
            _PresentationAtFirstSample = utils_transform::Get_EntityCurrentTransform(Handles.Presentation);
            auto Body = utils_transform::Get_EntityCurrentTransform(Handles.Root);
            for (auto Leg : Handles.Legs)
            {
                auto Foot = utils_procedural_leg::Get_Foot(Leg);
                if (Foot.Get_Phase() != ECk_ProceduralLeg_FootPhase::Planted ||
                    Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted)
                {
                    continue;
                }
                auto Hip = FVector::ZeroVector;
                if (TryGet_RigHip(Leg, Hip) == false)
                {
                    continue;
                }
                auto Reach = 0.0f;
                for (auto Length : utils_procedural_leg::Get_ChainGeometry(Leg).Get_SegmentLengths())
                {
                    Reach += Length;
                }
                auto HipLocal = utils_procedural_leg::Get_Placement(Leg).Get_HipLocal();
                auto Limit = Math::Max(Reach, (Foot.Get_Position() - Body.TransformPosition(HipLocal)).Size());
                _WorstReachExcess = Math::Max(_WorstReachExcess, (Foot.Get_Position() - Hip).Size() - Limit);
                _WorstSampleHip = Math::Max(_WorstSampleHip, (Hip - Sample.Get_WorldTransform().TransformPosition(HipLocal)).Size());
                _CheckedLegs++;
            }
        }
        auto Result = OutResult;
        Result.Set(Landed);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Handles = Get_Handles();
        auto Carried = _AcceptedOffset.GetLocation() - _BaseOffset.GetLocation();
        auto Sampled = _FirstSample.Get_WorldTransform();
        auto PresentationError = (Sampled.GetLocation() - _PresentationAtFirstSample.GetLocation()).Size();
        auto PresentationAngle = Math::RadiansToDegrees(Sampled.GetRotation().AngularDistance(_PresentationAtFirstSample.GetRotation()));
        ck::Trace(f"[BODY-POSE-ACTION] reach: carried {Carried.X :.3} cm of {_Overreach.X :.0}, {_CheckedLegs} trusted planted legs, worst reach excess {_WorstReachExcess :.3} cm, rig hip vs sample {_WorstSampleHip :.4} cm, sample vs presentation {PresentationError :.4} cm / {PresentationAngle :.4} deg");

        Assert_True(Carried.X > 5.0 && Carried.X < 0.5 * _Overreach.X,
            f"The {_Overreach.X :.0} cm action is reach-projected, not discarded: the accepted offset moves forward by part of it ({Carried.X :.2} cm)");
        Assert_False(_AcceptedOffset.ContainsNaN(), "The accepted offset is finite");
        Assert_True(_CheckedLegs > 0, f"Trusted planted legs bounded the projection ({_CheckedLegs})");
        Assert_True(_WorstReachExcess <= _ReachTolerance,
            f"Every drawn hip stays within its chain of its trusted planted foot (worst excess {_WorstReachExcess :.3} cm)");
        Assert_True(_WorstSampleHip < 0.05,
            f"The rig hips sit on the accepted sample (worst {_WorstSampleHip :.4} cm)");
        Assert_True(PresentationError < 0.05 && PresentationAngle < 0.05,
            f"The accepted sample is the presentation's transform ({PresentationError :.4} cm, {PresentationAngle :.4} deg)");
        Assert_True(utils_procedural_body_pose::Get_Status(Handles.BodyPose) == ECk_ProceduralAnimation_Status::Ready,
            "Projecting the action does not fail the body pose");
        utils_procedural_body_pose::Request_SetActionOffset(Handles.BodyPose,
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform::Identity, int64(2)),
            FCk_Delegate_Request_OnCompleted(this, n"OnCleared"));
    }

    UFUNCTION()
    private void OnCleared(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _ClearCompletions++;
    }

    UFUNCTION()
    private void Check_Cleared(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(_ClearCompletions > 0 && utils_procedural_body_pose::Get_ActionSample(Get_Handles().BodyPose).Get_SampleId() == 2);
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
