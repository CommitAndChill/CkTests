// Language=angelscript

// Losing support during an action keeps the environmental sag and still layers the action on it; identity with a new id
// clears the action on its first sample, leaves no residue, and the walker walks on.
class UCk_AutoTest_ProceduralAnimation_BodyPoseAction_ClearAndSupportLoss : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 25.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(180000.0, 35000.0, 600.0);
    private FVector _Action = FVector(10.0, 0.0, 0.0);
    private float _PhaseStart = 0.0;
    private FCk_Handle_ProceduralLeg _DisabledLeg;
    private FCk_Handle_ProceduralLeg _DetachedLeg;
    private FTransform _OffsetBeforeClear;
    private FTransform _OffsetAtClear;
    private FVector _StartLocation;
    private int64 _SettleRevision = -1;
    private FVector _SettleOffset;
    private int32 _QuietUpdates = 0;

    FCkProceduralAnimationGym_CrawlerHandles Get_Handles() const
    {
        return _Fixture.Crawlers[0].Handles;
    }

    float Get_Elapsed() const
    {
        return float(System::GetGameTimeInSeconds()) - _PhaseStart;
    }

    void Restart_Phase()
    {
        _PhaseStart = float(System::GetGameTimeInSeconds());
        _SettleRevision = -1;
        _QuietUpdates = 0;
    }

    // The base spring has settled once two consecutive updates moved the accepted offset by less than 0.05 cm, at least
    // 1 s after the support change. Polls run twice per frame, so only polls that see a new pose revision are compared.
    bool Get_IsBaseSettled()
    {
        auto Sample = utils_procedural_body_pose::Get_ActionSample(Get_Handles().BodyPose);
        if (Sample.Get_PoseRevision() != _SettleRevision)
        {
            auto Offset = Get_Offset().GetLocation();
            if (_SettleRevision >= 0)
            {
                _QuietUpdates = (Offset - _SettleOffset).Size() < 0.05 ? _QuietUpdates + 1 : 0;
            }
            _SettleRevision = Sample.Get_PoseRevision();
            _SettleOffset = Offset;
        }
        return Get_Elapsed() >= 1.0 && _QuietUpdates >= 2;
    }

    FTransform Get_Offset() const
    {
        return utils_procedural_body_pose::Get_Offset(Get_Handles().BodyPose);
    }

    FTransform Get_Target() const
    {
        return utils_procedural_body_pose::Get_TargetOffset(Get_Handles().BodyPose);
    }

    // How far the accepted offset is from the environmental target with the action layered on it.
    float Get_CompositionError(FVector InAction) const
    {
        return (Get_Offset().GetLocation() - (Get_Target().GetLocation() + InAction)).Size();
    }

    float Get_CompositionAngle() const
    {
        return Math::RadiansToDegrees(Get_Offset().GetRotation().AngularDistance(Get_Target().GetRotation()));
    }

    float Get_SupportDrop(int32 InUnsupported) const
    {
        return ck_procedural_gym::BodyCollapseDrop * float(InUnsupported) / float(_Fixture.Crawlers[0].Layout.LegCount);
    }

    int64 Get_SampleId() const
    {
        return utils_procedural_body_pose::Get_ActionSample(Get_Handles().BodyPose).Get_SampleId();
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
        Add_Step("request a 10 cm action", n"Step_SubmitAction");
        Add_Step_WaitUntil("the action's sample is published", n"Check_ActionLanded");
        Add_Step("disable a leg during the action", n"Step_DisableLeg");
        Add_Step_WaitUntil("the base spring settles under the action or 4 s pass", n"Check_DisabledSettled", 0, 5.0f);
        Add_Step("verify the one-leg sag with the action layered on it, then detach a second leg", n"Step_VerifyDisabledAndDetach");
        Add_Step_WaitUntil("the base spring settles under the deeper sag or 4 s pass", n"Check_DetachedSettled", 0, 5.0f);
        Add_Step("verify the two-leg sag with the action layered on it, then clear the action", n"Step_VerifyDetachedAndClear");
        Add_Step_WaitUntil("the clear's sample is published", n"Check_ClearLanded");
        Add_Step("verify the clear removed the action on its first sample", n"Step_VerifyClearSample");
        Add_Step_WaitUntil("the cleared pose holds for 0.5 s", n"Check_HalfSecond");
        Add_Step("verify no residue and re-enable the disabled leg", n"Step_VerifyNoResidue");
        Add_Step_WaitUntil("the walker walks for 1.5 s", n"Check_Walked");
        Add_Step("verify the neutral gait resumed", n"Step_VerifyWalking");
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
            Restart_Phase();
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
    private void Step_SubmitAction(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_procedural_body_pose::Request_SetActionOffset(Get_Handles().BodyPose,
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform(_Action), int64(1)));
    }

    UFUNCTION()
    private void Check_ActionLanded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(Get_SampleId() == 1);
    }

    UFUNCTION()
    private void Step_DisableLeg(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Legs = Get_Handles().Legs;
        Assert_True(Legs.Num() == 4, f"Precondition: the walker has four legs ({Legs.Num()})");
        _DisabledLeg = Legs[0];
        _DetachedLeg = Legs[1];
        auto Leg = _DisabledLeg;
        utils_procedural_leg::Request_EnableDisable(Leg, FCk_Request_ProceduralLeg_EnableDisable(ECk_EnableDisable::Disable));
        Restart_Phase();
    }

    UFUNCTION()
    private void Check_DisabledSettled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Settled = Get_IsBaseSettled() && Get_Target().GetLocation().Z < -0.5 * Get_SupportDrop(1);
        auto Result = OutResult;
        Result.Set(Settled || Get_Elapsed() >= 4.0);
    }

    void Verify_Sag(int32 InUnsupported, const FString& InCase)
    {
        auto Drop = Get_SupportDrop(InUnsupported);
        auto Offset = Get_Offset();
        auto Target = Get_Target();
        auto Error = Get_CompositionError(_Action);
        auto Angle = Get_CompositionAngle();
        ck::Trace(f"[BODY-POSE-ACTION] {InCase}: offset ({Offset.GetLocation().X :.3}, {Offset.GetLocation().Y :.3}, {Offset.GetLocation().Z :.3}), target Z {Target.GetLocation().Z :.3} of -{Drop :.2}, composition error {Error :.3} cm / {Angle :.3} deg");
        Assert_True(Error < 1.0 && Angle < 1.0,
            f"{InCase}: the action still composes onto the sagging base offset (error {Error :.3} cm, {Angle :.3} deg)");
        Assert_True(Math::Abs(Target.GetLocation().Z + Drop) < 0.5,
            f"{InCase}: the environmental target sags by the unsupported share ({Target.GetLocation().Z :.2} of -{Drop :.2} cm)");
        Assert_True(Offset.GetLocation().Z <= -0.9 * Drop,
            f"{InCase}: the drawn body still sags under the action ({Offset.GetLocation().Z :.2} of -{Drop :.2} cm)");
        Assert_True(Get_SampleId() == 1, f"{InCase}: the published sample still carries the action's id");
    }

    UFUNCTION()
    private void Step_VerifyDisabledAndDetach(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Verify_Sag(1, "One leg disabled");
        auto Leg = _DetachedLeg;
        utils_procedural_leg::Request_Detach(Leg, FCk_Request_ProceduralLeg_Detach(ECk_ProceduralLeg_ReleasedPartsOwnership::KeepBodyOwned));
        Restart_Phase();
    }

    UFUNCTION()
    private void Check_DetachedSettled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Settled = Get_IsBaseSettled() && utils_procedural_leg::Get_Status(_DetachedLeg) == ECk_ProceduralLeg_Status::Detached &&
            Get_Target().GetLocation().Z < -0.75 * Get_SupportDrop(2);
        auto Result = OutResult;
        Result.Set(Settled || Get_Elapsed() >= 4.0);
    }

    UFUNCTION()
    private void Step_VerifyDetachedAndClear(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(utils_procedural_leg::Get_Status(_DetachedLeg) == ECk_ProceduralLeg_Status::Detached, "The second leg is detached");
        Verify_Sag(2, "One leg disabled and one detached");
        _OffsetBeforeClear = Get_Offset();
        utils_procedural_body_pose::Request_SetActionOffset(Get_Handles().BodyPose,
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform::Identity, int64(2)));
    }

    UFUNCTION()
    private void Check_ClearLanded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Landed = Get_SampleId() == 2;
        if (Landed)
        {
            _OffsetAtClear = Get_Offset();
            Restart_Phase();
        }
        auto Result = OutResult;
        Result.Set(Landed);
    }

    UFUNCTION()
    private void Step_VerifyClearSample(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Removed = _OffsetBeforeClear.GetLocation() - _OffsetAtClear.GetLocation();
        ck::Trace(f"[BODY-POSE-ACTION] clear: removed ({Removed.X :.3}, {Removed.Y :.3}, {Removed.Z :.3}) on the first cleared sample");
        Assert_True(Removed.Equals(_Action, 0.5),
            f"Identity with a new id removes the whole action on its first sample (removed {Removed.X :.2} of {_Action.X :.1} cm)");
        Assert_True(utils_procedural_body_pose::Get_ActionOffset(Get_Handles().BodyPose).Equals(FTransform::Identity, 0.0001),
            "Get_ActionOffset reads identity after the clear");
        Assert_True(_OffsetAtClear.GetLocation().Z <= -0.9 * Get_SupportDrop(2),
            f"Clearing the action keeps the support sag ({_OffsetAtClear.GetLocation().Z :.2} cm)");
    }

    UFUNCTION()
    private void Check_HalfSecond(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(Get_Elapsed() >= 0.5);
    }

    UFUNCTION()
    private void Step_VerifyNoResidue(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Error = Get_CompositionError(FVector::ZeroVector);
        Assert_True(Error < 1.0 && Get_CompositionAngle() < 1.0,
            f"After the clear the accepted offset is the environmental base with no action residue ({Error :.3} cm)");
        auto Leg = _DisabledLeg;
        utils_procedural_leg::Request_EnableDisable(Leg, FCk_Request_ProceduralLeg_EnableDisable(ECk_EnableDisable::Enable));
        _StartLocation = utils_transform::Get_EntityCurrentTransform(Get_Handles().Root).GetLocation();
        Restart_Phase();
    }

    UFUNCTION()
    private void Check_Walked(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(true);
        auto Result = OutResult;
        Result.Set(Get_Elapsed() >= 1.5);
    }

    UFUNCTION()
    private void Step_VerifyWalking(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Handles = Get_Handles();
        auto Travelled = (utils_transform::Get_EntityCurrentTransform(Handles.Root).GetLocation() - _StartLocation).Size();
        auto Presentation = utils_transform::Get_EntityCurrentTransform(Handles.Presentation);
        auto Sampled = utils_procedural_body_pose::Get_ActionSample(Handles.BodyPose).Get_WorldTransform();
        Assert_True(Travelled > 30.0, f"The three-legged walker walks on after the clear ({Travelled :.1} cm)");
        Assert_True(Get_SampleId() == 2, "Walking keeps publishing the clear's id");
        Assert_True((Sampled.GetLocation() - Presentation.GetLocation()).Size() < 0.05,
            "The published sample stays the presentation's transform while walking");
        Assert_True(utils_procedural_body_pose::Get_Status(Handles.BodyPose) == ECk_ProceduralAnimation_Status::Ready,
            "The body pose stays Ready through support loss and the clear");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
