// Language=angelscript

// An action within reach reaches its full amplitude on the first update that includes it: it is layered on the base
// offset after the springs, never eased by them, and the rig's hips follow the same composed offset.
class UCk_AutoTest_ProceduralAnimation_BodyPoseAction_ActionBypassesSpring : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(180000.0, 25000.0, 600.0);
    private FVector _Jab = FVector(24.0, 0.0, 0.0);
    private float _PhaseStart = 0.0;
    private FTransform _BaseOffset;
    private FTransform _TargetBefore;
    private int64 _RevisionAtSubmit = 0;
    private int32 _Completions = 0;
    private ECk_Request_OperationResult _CompletionResult = ECk_Request_OperationResult::Failed;
    private FCk_ProceduralBodyPose_ActionSample _FirstSample;
    private FTransform _BodyAtFirstSample;
    private FTransform _TargetAtFirstSample;
    private TArray<FVector> _RigHips;
    private TArray<FVector> _HipLocals;

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
        Add_Step("record the base offset and request a 24 cm action", n"Step_Submit");
        Add_Step_WaitUntil("the first sample carrying the action's id is published", n"Check_FirstSample");
        Add_Step("verify the first sample carries the full action on the base offset", n"Step_Verify");
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
        _TargetBefore = utils_procedural_body_pose::Get_TargetOffset(Handles.BodyPose);
        auto Sample = utils_procedural_body_pose::Get_ActionSample(Handles.BodyPose);
        _RevisionAtSubmit = Sample.Get_PoseRevision();
        Assert_True(Sample.Get_SampleId() == 0 && _RevisionAtSubmit > 0,
            f"Precondition: the walker publishes neutral samples (id {Sample.Get_SampleId()}, revision {_RevisionAtSubmit})");
        Assert_True(_BaseOffset.GetLocation().Size() < 2.0,
            f"Precondition: the settled base offset sits near the body ({_BaseOffset.GetLocation().Size() :.2} cm)");
        utils_procedural_body_pose::Request_SetActionOffset(Handles.BodyPose,
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform(_Jab), int64(1)),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _Completions++;
        _CompletionResult = InResult;
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
            _BodyAtFirstSample = utils_transform::Get_EntityCurrentTransform(Handles.Root);
            _TargetAtFirstSample = utils_procedural_body_pose::Get_TargetOffset(Handles.BodyPose);
            _RigHips.Empty();
            _HipLocals.Empty();
            for (auto Leg : Handles.Legs)
            {
                auto Hip = FVector::ZeroVector;
                if (TryGet_RigHip(Leg, Hip))
                {
                    _RigHips.Add(Hip);
                    _HipLocals.Add(utils_procedural_leg::Get_Placement(Leg).Get_HipLocal());
                }
            }
        }
        auto Result = OutResult;
        Result.Set(Landed);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Handles = Get_Handles();
        auto Composed = FTransform(_BaseOffset.GetRotation(), _BaseOffset.GetLocation() + _Jab);
        auto Expected = Composed * _BodyAtFirstSample;
        auto Sampled = _FirstSample.Get_WorldTransform();
        auto SampleError = (Sampled.GetLocation() - Expected.GetLocation()).Size();
        auto SampleAngle = Math::RadiansToDegrees(Sampled.GetRotation().AngularDistance(Expected.GetRotation()));
        auto SampledLocal = _BodyAtFirstSample.InverseTransformPosition(Sampled.GetLocation()) - _BaseOffset.GetLocation();

        auto WorstComposedHip = 0.0;
        auto WorstSampleHip = 0.0;
        for (auto Index = 0; Index < _RigHips.Num(); Index++)
        {
            WorstComposedHip = Math::Max(WorstComposedHip, (_RigHips[Index] - Expected.TransformPosition(_HipLocals[Index])).Size());
            WorstSampleHip = Math::Max(WorstSampleHip, (_RigHips[Index] - Sampled.TransformPosition(_HipLocals[Index])).Size());
        }
        ck::Trace(f"[BODY-POSE-ACTION] bypass: first sample revision {_FirstSample.Get_PoseRevision()} (submit {_RevisionAtSubmit}), action carried {SampledLocal.X :.3} cm of {_Jab.X :.1}, sample error {SampleError :.4} cm / {SampleAngle :.4} deg, rig hip vs composed {WorstComposedHip :.4} cm, rig hip vs sample {WorstSampleHip :.4} cm");

        Assert_True(SampleError < 0.5 && SampleAngle < 0.5,
            f"The first sample carrying the action's id poses the full {_Jab.X :.0} cm action on the base offset (carried {SampledLocal.X :.2} cm; error {SampleError :.3} cm, {SampleAngle :.3} deg)");
        Assert_True(_RigHips.Num() == Handles.Legs.Num() && _RigHips.Num() > 0,
            f"Every leg's rig was sampled ({_RigHips.Num()} of {Handles.Legs.Num()})");
        Assert_True(WorstComposedHip < 0.5,
            f"The rig hips follow the composed offset on the same update (worst {WorstComposedHip :.3} cm)");
        Assert_True(WorstSampleHip < 0.05,
            f"The rig hips agree with the published sample (worst {WorstSampleHip :.4} cm)");
        Assert_True(_FirstSample.Get_PoseRevision() == _RevisionAtSubmit + 1,
            f"The first sample with the action's id is the update right after the drain (revision {_FirstSample.Get_PoseRevision()}, submitted at {_RevisionAtSubmit})");
        auto TargetShift = (_TargetAtFirstSample.GetLocation() - _TargetBefore.GetLocation()).Size();
        Assert_True(TargetShift < 0.5,
            f"Get_TargetOffset keeps its environmental meaning, untouched by the action (moved {TargetShift :.3} cm)");
        Assert_Equals_Int(_Completions, 1, "The request completes exactly once");
        Assert_True(_CompletionResult == ECk_Request_OperationResult::Succeeded, "The request completes Succeeded");
        Assert_True(utils_procedural_body_pose::Get_ActionOffset(Handles.BodyPose).GetLocation().Equals(_Jab, 0.001),
            "Get_ActionOffset reads the processed action");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
