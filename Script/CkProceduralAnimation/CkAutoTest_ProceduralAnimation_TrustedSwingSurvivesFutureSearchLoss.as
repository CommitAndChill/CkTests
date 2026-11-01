// Language=angelscript

struct FCkTrustedSwingSearchLossScene
{
    FCk_Handle_Transform Body;
    FCk_Handle_ProceduralGait Gait;
    FCk_Handle_ProceduralLeg Leg;
    FCk_Handle TopEntity;
    FCk_Handle_JoltBody Top;
    float OffsetX = 0.0;
    float TriggeredAt = -1.0;
    float AppliedAt = -1.0;
    int64 TriggerSequence = -1;
    float NativeMissAt = -1.0;
    int64 NativeMissSequence = -1;
    int64 DeadlineSequence = -1;
    bool QualifyingFallback = false;
    FVector SwingTarget;
    bool Landed = false;
    int32 Violations = 0;
}

class UCk_AutoTest_ProceduralAnimation_TrustedSwingSurvivesFutureSearchLoss : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(120000.0, 349000.0, 600.0);
    private TArray<FCkTrustedSwingSearchLossScene> _Scenes;
    private float _StartedAt = -1.0;

    FVector Get_Point(int32 InIndex, float InX) const
    {
        return _Origin + FVector(_Scenes[InIndex].OffsetX + InX, 100.0, 170.0);
    }

    FCk_Jolt_HitResult Get_Contact(FVector InPoint) const
    {
        return utils_jolt_query::Get_RayCast(InPoint + FVector(0.0, 0.0, 5.0),
            InPoint - FVector(0.0, 0.0, 5.0), FCk_Jolt_QueryFilter());
    }

    void DoAdd_Replacement(int32 InIndex)
    {
        auto Root = utils_entity_lifetime::Request_CreateEntity(_Fixture.SceneRoot);
        Root.Request_OverrideToSelf();
        _Fixture.Entities.Add(Root);
        utils_transform::Add(Root, FTransform(_Origin + FVector(_Scenes[InIndex].OffsetX + 52.5, 100.0, 85.0)),
            ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(FVector(2.5, 10.0, 85.0));
        auto Spec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_CollisionProfileName(n"BlockAll");
        _Scenes[InIndex].TopEntity = Root;
        _Scenes[InIndex].Top = utils_jolt_body::Add(Root, Spec);
        _Fixture.RetirementProbes.Starts.Add(Get_Point(InIndex, 52.5) + FVector(0.0, 0.0, 5.0));
        _Fixture.RetirementProbes.Ends.Add(Get_Point(InIndex, 52.5) - FVector(0.0, 0.0, 5.0));
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Query = FVector(1.0, 2.0, 3.0);
        auto SwingTarget = FVector(1.0, 2.0, 3.0);
        auto AcceptedBody = FTransform(FVector(1.0, 2.0, 3.0));
        int64 Sequence = 42;
        Assert_True(UCk_Utils_AutoTest_UE::TryGet_ProceduralAnimationLegQueryTarget(FCk_Handle(), n"Missing", Query,
            SwingTarget, AcceptedBody, Sequence) == false && Query.IsNearlyZero() && SwingTarget.IsNearlyZero()
            && AcceptedBody.Equals(FTransform::Identity) && Sequence == -1,
            "An unavailable sample rejects a default handle and resets every output");
        if (_Fixture.Create(InHandle, _Origin, ECkProceduralAnimationGym_Course::Flat, false, 1) == false)
        {
            FinishFailure("The trusted swing native fixture could not be created");
            return;
        }
        // The other leg retains its own support; the observed +Y leg can land only on its narrow replacement top.
        _Scenes.SetNum(2);
        for (int32 Index = 0; Index < 2; Index++)
        {
            _Scenes[Index].OffsetX = Index * 600.0;
            _Fixture.AddSurface(FVector(_Scenes[Index].OffsetX - 25.0, 100.0, 85.0), FRotator::ZeroRotator,
                FVector(10.0, 10.0, 85.0), FLinearColor(0.2, 0.3, 0.4, 1.0));
            _Fixture.AddSurface(FVector(_Scenes[Index].OffsetX + 50.0, -100.0, 85.0), FRotator::ZeroRotator,
                FVector(200.0, 50.0, 85.0), FLinearColor(0.2, 0.3, 0.4, 1.0));
            DoAdd_Replacement(Index);
        }
        Add_Step_WaitUntil("all owned native tops are queryable", n"Check_Ready");
        Add_Step("compose both complete public side walkers", n"Step_Compose");
        Add_Step_WaitUntil("both observed legs have ordinary native held plants", n"Check_Planted", 0, 3.0f);
        Add_Step("begin ordinary forward motion", n"Step_Start");
        Add_Step_WaitUntil("trusted swings survive query loss only while their native landing exists", n"Check_Run", 0, 5.0f);
        Add_Step("verify both qualifying production paths", n"Step_Verify");
        Add_Step("retire the walkers and native geometry", n"Step_Destroy");
        Add_Step_WaitUntil("all owned entities and collision are gone", n"Check_Destroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Ready(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Ready = _Fixture.Get_AreSurfacesReady();
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Hit = Get_Contact(Get_Point(Index, 52.5));
            Ready = Ready && ck::IsValid(_Scenes[Index].Top) && utils_jolt_body::Get_IsBodyAdded(_Scenes[Index].Top)
                && Hit.Get_HasHit() && Hit.Get_Entity() == _Scenes[Index].TopEntity;
        }
        auto Result = OutResult;
        Result.Set(Ready);
    }

    UFUNCTION()
    private void Step_Compose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Root = utils_entity_lifetime::Request_CreateEntity(_Fixture.SceneRoot);
            Root.Request_OverrideToSelf();
            _Fixture.Entities.Add(Root);
            _Scenes[Index].Body = utils_transform::Add(Root,
                FTransform(_Origin + FVector(_Scenes[Index].OffsetX, 0.0, 235.0)), ECk_Replication::DoesNotReplicate);
            auto Spec = FCk_ProceduralWalker_Spec(ck::ProceduralTest_SideRig, ck::ProceduralTest_NarrowRingGait);
            Spec.Set_Chains(TArray<FCk_ProceduralWalker_LegChain>());
            auto Walker = utils_procedural_animation::Add_Walker(_Scenes[Index].Body, Spec);
            _Scenes[Index].Gait = Walker.Get_Gait();
            for (auto Leg : Walker.Get_Legs())
            {
                if (utils_procedural_leg::Get_Placement(Leg).Get_HipLocal().Y > 0.0) { _Scenes[Index].Leg = Leg; }
            }
            Assert_True(ck::IsValid(_Scenes[Index].Gait) && Walker.Get_Legs().Num() == 2 && ck::IsValid(_Scenes[Index].Leg),
                "Each scene admits the complete public two-leg walker");
        }
    }

    UFUNCTION()
    private void Check_Planted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Ready = true;
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Foot = utils_procedural_leg::Get_Foot(_Scenes[Index].Leg);
            auto Local = Foot.Get_Position() - _Origin - FVector(_Scenes[Index].OffsetX, 0.0, 0.0);
            Ready = Ready && utils_procedural_gait::Get_Status(_Scenes[Index].Gait) == ECk_ProceduralAnimation_Status::Ready
                && Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted && Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                && Foot.Get_Foothold() == ECk_ProceduralLeg_Foothold::Held && Local.X >= -35.0 && Local.X <= -15.0;
        }
        auto Result = OutResult;
        Result.Set(Ready);
    }

    UFUNCTION()
    private void Step_Start(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _StartedAt = float(System::GetGameTimeInSeconds());
        auto Query = FVector(1.0, 2.0, 3.0);
        auto SwingTarget = FVector(1.0, 2.0, 3.0);
        auto AcceptedBody = FTransform(FVector(1.0, 2.0, 3.0));
        int64 Sequence = 42;
        Assert_True(UCk_Utils_AutoTest_UE::TryGet_ProceduralAnimationLegQueryTarget(_Scenes[0].Body, n"UnknownLeg", Query,
            SwingTarget, AcceptedBody, Sequence) == false && Query.IsNearlyZero() && SwingTarget.IsNearlyZero()
            && AcceptedBody.Equals(FTransform::Identity) && Sequence == -1,
            "An unknown leg rejects an accepted snapshot and resets every output");
    }

    UFUNCTION()
    private void Check_Run(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Now = float(System::GetGameTimeInSeconds());
        auto Grace = ck::ProceduralTest_NarrowRingGait.Get_Probe().Get_ContactGrace().Get_Seconds();
        auto StepSeconds = ck::ProceduralTest_NarrowRingGait.Get_Timing().Get_StepDuration().Get_Seconds();
        auto Done = true;
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Foot = utils_procedural_leg::Get_Foot(_Scenes[Index].Leg);
            auto Body = _Scenes[Index].Body;
            auto Query = FVector::ZeroVector;
            auto SwingTarget = FVector::ZeroVector;
            auto AcceptedBody = FTransform::Identity;
            int64 Sequence = -1;
            auto HasSample = UCk_Utils_AutoTest_UE::TryGet_ProceduralAnimationLegQueryTarget(Body,
                utils_procedural_leg::Get_Id(_Scenes[Index].Leg), Query, SwingTarget, AcceptedBody, Sequence);
            auto CurrentBody = utils_transform::Get_EntityCurrentTransform(Body);
            auto BodyMatches = HasSample && AcceptedBody.Equals(CurrentBody, 0.000001);
            if (_Scenes[Index].TriggeredAt < 0.0)
            {
                utils_transform::Request_SetTransform(Body, FTransform(_Origin + FVector(_Scenes[Index].OffsetX
                    + 27.0 * Math::Clamp(Now - _StartedAt, 0.0, 1.0), 0.0, 235.0)));
                if (Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Swinging && Foot.Get_SwingAlpha() > 0.0
                    && Foot.Get_SwingAlpha() < 0.35 && Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                    && Foot.Get_Foothold() == ECk_ProceduralLeg_Foothold::Held && BodyMatches && Sequence >= 0
                    && SwingTarget.X >= Get_Point(Index, 50.0).X && SwingTarget.X <= Get_Point(Index, 55.0).X
                    && SwingTarget.Y >= _Origin.Y + 90.0 && SwingTarget.Y <= _Origin.Y + 110.0
                    && Math::Abs(SwingTarget.Z - Get_Point(Index, 52.5).Z) < 0.000001)
                {
                    // Held names this exact foothold swing; the level top has no lift-course landing offset.
                    auto Hit = Get_Contact(SwingTarget);
                    Assert_True(Hit.Get_HasHit() && Hit.Get_Entity() == _Scenes[Index].TopEntity,
                        "The ordinary early trusted swing has its known replacement native contact");
                    if (Hit.Get_HasHit() == false || Hit.Get_Entity() != _Scenes[Index].TopEntity)
                    {
                        FinishFailure("The early foothold swing does not name the owned replacement native top");
                        return;
                    }
                    _Scenes[Index].TriggeredAt = Now;
                    _Scenes[Index].TriggerSequence = Sequence;
                    _Scenes[Index].SwingTarget = SwingTarget;
                    utils_transform::Request_SetTransform(Body, FTransform(_Origin + FVector(_Scenes[Index].OffsetX + 126.0, 0.0, 235.0)));
                    if (Index == 1) { utils_entity_lifetime::Request_DestroyEntity(_Scenes[Index].TopEntity); }
                }
                Done = false;
                continue;
            }
            auto Location = CurrentBody.GetLocation();
            auto Applied = BodyMatches && Math::Abs(Location.X - _Origin.X - _Scenes[Index].OffsetX - 126.0) < 0.000001;
            if (Applied && _Scenes[Index].AppliedAt < 0.0)
            {
                _Scenes[Index].AppliedAt = Now;
            }
            auto Fresh = Applied && _Scenes[Index].TriggerSequence >= 0 && Sequence > _Scenes[Index].TriggerSequence;
            auto Native = Get_Contact(_Scenes[Index].SwingTarget);
            if (Fresh && Index == 0)
            {
                // Closest point on the entire real top, rather than an assumed velocity lead or landing pixel.
                auto Closest = Get_Point(Index, 52.5);
                Closest.X = Math::Clamp(Query.X, Get_Point(Index, 50.0).X, Get_Point(Index, 55.0).X);
                Closest.Y = Math::Clamp(Query.Y, _Origin.Y + 90.0, _Origin.Y + 110.0);
                auto Reach = 0.0;
                for (auto Length : utils_procedural_leg::Get_ChainGeometry(_Scenes[Index].Leg).Get_SegmentLengths()) { Reach += Length; }
                auto Force = ck::ProceduralTest_NarrowRingGait.Get_Step().Get_ForceStepReachFraction() * Reach;
                if (HasSample && Reach > 0.0 && (Query - Closest).Size() > Force
                    && Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Swinging)
                {
                    auto Hip = AcceptedBody.TransformPosition(
                        utils_procedural_leg::Get_Placement(_Scenes[Index].Leg).Get_HipLocal());
                    auto Farthest = Get_Point(Index, 50.0);
                    Farthest.X = Math::Abs(Hip.X - Get_Point(Index, 50.0).X) > Math::Abs(Hip.X - Get_Point(Index, 55.0).X)
                        ? Get_Point(Index, 50.0).X : Get_Point(Index, 55.0).X;
                    Farthest.Y = Math::Abs(Hip.Y - _Origin.Y - 90.0) > Math::Abs(Hip.Y - _Origin.Y - 110.0)
                        ? _Origin.Y + 90.0 : _Origin.Y + 110.0;
                    Assert_True((Farthest - Hip).Size() <= Force,
                        "Every possible committed point on the narrow top remains inside unchanged force reach");
                    _Scenes[Index].QualifyingFallback = true;
                    if (Foot.Get_Foothold() != ECk_ProceduralLeg_Foothold::Held
                        || Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted
                        || Native.Get_HasHit() == false || Native.Get_Entity() != _Scenes[Index].TopEntity)
                    { _Scenes[Index].Violations++; }
                }
                if (Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted)
                {
                    auto Hit = Get_Contact(Foot.Get_Position());
                    _Scenes[Index].Landed = true;
                    if (Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted
                        || Foot.Get_Position().Equals(_Scenes[Index].SwingTarget, 1.0) == false
                        || Hit.Get_HasHit() == false || Hit.Get_Entity() != _Scenes[Index].TopEntity)
                    { _Scenes[Index].Violations++; }
                }
            }
            if (Index == 1 && ck::Is_NOT_Valid(_Scenes[Index].Top) && Native.Get_HasHit() == false)
            {
                if (_Scenes[Index].NativeMissAt < 0.0)
                {
                    _Scenes[Index].NativeMissAt = Now;
                    _Scenes[Index].NativeMissSequence = Sequence;
                }
                if (Fresh && _Scenes[Index].NativeMissSequence >= 0 && Sequence > _Scenes[Index].NativeMissSequence
                    && Now - _Scenes[Index].TriggeredAt > StepSeconds + Grace)
                {
                    _Scenes[Index].Landed = Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted;
                    if (Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                        || Foot.Get_Foothold() == ECk_ProceduralLeg_Foothold::Held)
                    { _Scenes[Index].Violations++; }
                }
            }
            auto PastDeadline = Now - _Scenes[Index].TriggeredAt > StepSeconds + Grace;
            if (PastDeadline && _Scenes[Index].DeadlineSequence < 0) { _Scenes[Index].DeadlineSequence = Sequence; }
            Done = Done && Fresh && _Scenes[Index].Landed && PastDeadline
                && _Scenes[Index].DeadlineSequence >= 0 && Sequence > _Scenes[Index].DeadlineSequence;
        }
        auto Result = OutResult;
        Result.Set(Done);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_Scenes[0].QualifyingFallback,
            "A fresh accepted swinging query lies beyond force reach of every point on its committed native top");
        Assert_True(_Scenes[0].Landed && _Scenes[0].Violations == 0,
            "One fresh admissible fallback observation is Held and trusted, followed by native touchdown at the recorded swing target beyond original grace");
        Assert_True(_Scenes[1].NativeMissAt >= 0.0 && _Scenes[1].NativeMissSequence >= 0
            && _Scenes[1].Landed && _Scenes[1].Violations == 0,
            "Removing the twin committed native support produces a confirmed miss and a later untrusted landing without Held retention");
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
