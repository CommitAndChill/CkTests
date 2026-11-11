// Language=angelscript

class UCk_AutoTest_ProceduralAnimation_TrailingHoldDoesNotStopTheSearch : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(120000.0, 335000.0, 600.0);
    // Two gait-only side walkers, one per scene along X, on a preset whose search ring (25 cm) lies inside the keep radius
    // (45 cm, 1.5 step thresholds). Each +Y leg (hip 30 cm out, rest foot 100 cm out and 65 cm down, a 140 cm chain: target
    // reach 112 cm) has its rest foot over a 170 cm drop between narrow tops level with it; the ring reaches one top at
    // setup and the leg holds it.
    // - Behind: the held top lies 25 cm back. The body walks 27 cm forward: the hold ends behind the hip along the travel
    //   and 52 cm from the ideal, beyond the keep radius, and the ring's forward point falls on a top ahead 25 cm from the
    //   ideal.
    // - Ahead: the held top lies 25 cm forward. The body backs 32 cm (a search there finds nothing: the ring's points hang
    //   over gaps), then creeps 2 cm forward, so the gait's velocity window holds forward travel only; a top is set down
    //   between the ideal and the hold, and the body walks 5 cm forward: the hold stays ahead of the hip and 49-55 cm from
    //   the ideal, beyond the keep radius, while a search would find the new top 25 cm from the ideal, cheaper than the
    //   hold.
    // The bodies walk slowly, so the velocity lead stays short.
    private float _BodyHeight = 235.0;
    private float _TopHalfDepthY = 10.0;
    private float _BehindHoldMinX = -35.0;
    private float _BehindHoldMaxX = -15.0;
    private float _BehindAheadMinX = 35.0;
    private float _AheadHoldMinX = 15.0;
    private float _AheadHoldMaxX = 35.0;
    private float _MiddleMinX = -10.0;
    private float _MiddleMaxX = 8.0;
    private float _TopLength = 80.0;
    private float _BehindWalk = 27.0;
    private float _AheadBack = 32.0;
    private float _AheadCreep = 2.0;
    private float _AheadWalk = 5.0;
    private float _BackSeconds = 1.5;
    private float _CreepSeconds = 0.3;
    private float _WalkSeconds = 1.0;
    private float _PhaseStartTime = -1.0;
    private float _BehindSceneX = -300.0;
    private float _AheadSceneX = 300.0;
    private float _Tolerance = 1.0;
    private FCk_Handle_Transform _BehindBody;
    private FCk_Handle_Transform _AheadBody;
    private FCk_Handle_ProceduralGait _BehindGait;
    private FCk_Handle_ProceduralGait _AheadGait;
    private FCk_Handle_ProceduralLeg _BehindLeg;
    private FCk_Handle_ProceduralLeg _AheadLeg;
    private float _WatchSeconds = 1.0;
    private int32 _WatchedFrames = 0;
    private bool _BehindSearched = false;
    private bool _BehindOnTopAhead = false;
    private int32 _AheadViolations = 0;
    private FString _FirstAheadViolation;
    private FString _BehindLast;
    private float _StanceSceneX = 900.0;
    private FCk_Handle _StanceTopEntity;
    private FCk_Handle_JoltBody _StanceTop;
    private FCk_Handle _OtherStanceTopEntity;
    private FCk_Handle_JoltBody _OtherStanceTop;
    private FCk_Handle_ProceduralLeg _OtherStanceLeg;
    private FVector _OtherStancePlant;
    private FCk_Handle_ProceduralLeg _BoundaryLeg;
    private FCk_Handle_Transform _StanceBody;
    private FCk_Handle_ProceduralGait _StanceGait;
    private FCk_Handle_ProceduralLeg _StanceLeg;
    private FVector _StancePlant;
    private float _StanceRetiredAt = -1.0;
    private float _LongestStanceNoFuture = 0.0;
    private int32 _StanceNoFutureSamples = 0;
    private int32 _StanceViolations = 0;
    private FString _FirstStanceViolation;
    private float _NativeRemovalObservedAt = -1.0;
    private int64 _RemovalSolveSequence = -1;
    private float _BoundaryReach = 0.0;
    private float _BoundaryDistance = 0.0;
    private FVector _BoundaryPlant;
    private float _BoundaryObservedAt = -1.0;
    private int64 _BoundarySolveSequence = -1;
    private int32 _BoundaryViolations = 0;

    void DoAdd_StanceTop()
    {
        DoAdd_OneStanceTop(1.0);
        DoAdd_OneStanceTop(-1.0);
    }

    void DoAdd_OneStanceTop(float InSide)
    {
        auto Root = utils_entity_lifetime::Request_CreateEntity(_Fixture.SceneRoot);
        Root.Request_OverrideToSelf();
        _Fixture.Entities.Add(Root);

        auto Half = FVector((_BehindHoldMaxX - _BehindHoldMinX) * 0.5, _TopHalfDepthY, Get_TopZ() * 0.5);
        auto Center = _Origin + FVector(_StanceSceneX + (_BehindHoldMinX + _BehindHoldMaxX) * 0.5,
            InSide * ck_procedural_gym_assets::SideRestOffset, Half.Z);
        utils_transform::Add(Root, FTransform(Center), ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(Half);
        auto Spec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_CollisionProfileName(n"BlockAll");
        auto Body = utils_jolt_body::Add(Root, Spec);
        if (InSide > 0.0)
        {
            _StanceTopEntity = Root;
            _StanceTop = Body;
        }
        else
        {
            _OtherStanceTopEntity = Root;
            _OtherStanceTop = Body;
        }
        auto Probe = _Origin + FVector(_StanceSceneX - 25.0, InSide * ck_procedural_gym_assets::SideRestOffset, Get_TopZ());
        _Fixture.RetirementProbes.Starts.Add(Probe + FVector(0.0, 0.0, 5.0));
        _Fixture.RetirementProbes.Ends.Add(Probe - FVector(0.0, 0.0, 5.0));
    }

    FCk_Jolt_HitResult Get_StanceContact(FVector InPoint) const
    {
        return utils_jolt_query::Get_RayCast(InPoint + FVector(0.0, 0.0, 5.0),
            InPoint - FVector(0.0, 0.0, 5.0), FCk_Jolt_QueryFilter());
    }

    float Get_TopZ() const
    {
        return _BodyHeight - ck_procedural_gym_assets::RestDrop;
    }

    void DoAdd_Top(float InSceneX, float InMinX, float InMaxX)
    {
        auto Half = FVector((InMaxX - InMinX) * 0.5, _TopHalfDepthY, Get_TopZ() * 0.5);
        _Fixture.AddSurface(FVector(InSceneX + (InMinX + InMaxX) * 0.5, ck_procedural_gym_assets::SideRestOffset, Half.Z),
            FRotator::ZeroRotator, Half, FLinearColor(0.18, 0.25, 0.31, 1.0));
    }

    void DoAdd_Platform(float InSceneX)
    {
        // The -Y leg stands on a platform at its rest foot's height through every move.
        auto PlatformHalf = FVector(150.0, 50.0, Get_TopZ() * 0.5);
        _Fixture.AddSurface(FVector(InSceneX, -ck_procedural_gym_assets::SideRestOffset, PlatformHalf.Z), FRotator::ZeroRotator,
            PlatformHalf, FLinearColor(0.18, 0.25, 0.31, 1.0));
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Assert_True(UCk_Utils_AutoTest_UE::Get_ProceduralAnimationSolveSequence(FCk_Handle()) == -1,
            "An unavailable default-handle gait snapshot reports no accepted solve sequence");
        if (_Fixture.Create(InHandle, _Origin, ECkProceduralAnimationGym_Course::Flat, false, 1) == false)
        {
            FinishFailure("The tops fixture could not be created");
            return;
        }
        // Do not call fixture.Update: only its Jolt floor and the tops are needed, and no crawler may spawn.
        DoAdd_Top(_BehindSceneX, _BehindHoldMinX, _BehindHoldMaxX);
        DoAdd_Top(_BehindSceneX, _BehindAheadMinX, _BehindAheadMinX + _TopLength);
        DoAdd_Platform(_BehindSceneX);
        DoAdd_Top(_AheadSceneX, _AheadHoldMinX, _AheadHoldMaxX);
        DoAdd_Platform(_AheadSceneX);
        DoAdd_StanceTop();
        Add_Step_WaitUntil("the floor and the tops are queryable", n"Check_SurfacesReady");
        Add_Step("compose a gait-only side walker in each scene", n"Step_ComposeGaits");
        Add_Step_WaitUntil("both +Y legs hold their top", n"Check_Held", 0, 3.0f);
        Add_Step("start backing the second body away from its hold", n"Step_StartPhase");
        Add_Step_WaitUntil("the second body has backed 32 cm and crept 2 cm forward", n"Check_BackedAway", 0, 3.0f);
        Add_Step("set a top down between the second leg's ideal and its hold", n"Step_AddMiddleTop");
        Add_Step_WaitUntil("the new top is queryable", n"Check_SurfacesReady");
        Add_Step("start walking both bodies forward", n"Step_StartPhase");
        Add_Step_WaitUntil("the bodies walk and the +Y legs are watched for a second after", n"Check_Watched", 0, 4.0f);
        Add_Step("verify only the hold behind the hip gave way to a searched top", n"Step_Verify");
        Add_Step("place the third hip inside the physical reach tolerance", n"Step_ReachWithin");
        Add_Step_WaitUntil("the actual stance holds at Reach plus 0.0005 cm beyond target grace", n"Check_ReachWithin", 0, 1.0f);
        Add_Step("place the third hip outside the physical reach tolerance", n"Step_ReachOutside");
        Add_Step_WaitUntil("the unreachable actual stance loses trust before target grace", n"Check_ReachOutside", 0, 1.0f);
        Add_Step("return the third body to its ordinary reachable stance", n"Step_ResetReach");
        Add_Step_WaitUntil("ordinary gait reacquires the still-present top", n"Check_StanceReacquired", 0, 2.0f);
        Add_Step("remove the third walker's actual native stance contact", n"Step_RemoveStanceTop");
        Add_Step_WaitUntil("the removed contact loses trust before contact grace expires", n"Check_StanceRemoved", 0, 1.0f);
        Add_Step("restore real support at the third walker's original stance", n"Step_RestoreStanceTop");
        Add_Step_WaitUntil("ordinary gait reacquires the restored native contact", n"Check_StanceReacquired", 0, 2.0f);
        Add_Step("retire the fixture and the gaits", n"Step_Destroy");
        Add_Step_WaitUntil("entities and collision are gone", n"Check_Destroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_SurfacesReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_AreSurfacesReady() && ck::IsValid(_StanceTop) && utils_jolt_body::Get_IsBodyAdded(_StanceTop)
            && ck::IsValid(_OtherStanceTop) && utils_jolt_body::Get_IsBodyAdded(_OtherStanceTop));
    }

    FCk_Handle_Transform DoCompose_Body(float InSceneX)
    {
        auto Root = utils_entity_lifetime::Request_CreateEntity(_Fixture.SceneRoot);
        Root.Request_OverrideToSelf();
        _Fixture.Entities.Add(Root);
        return utils_transform::Add(Root, FTransform(_Origin + FVector(InSceneX, 0.0, _BodyHeight)), ECk_Replication::DoesNotReplicate);
    }

    FCk_ProceduralWalker DoCompose_Walker(FCk_Handle_Transform InBody)
    {
        auto WalkerSpec = FCk_ProceduralWalker_Spec(ck::ProceduralTest_SideRig, ck::ProceduralTest_NarrowRingGait);
        WalkerSpec.Set_Chains(TArray<FCk_ProceduralWalker_LegChain>());
        return utils_procedural_animation::Add_Walker(InBody, WalkerSpec);
    }

    FCk_Handle_ProceduralLeg Get_PlusLeg(FCk_ProceduralWalker InWalker)
    {
        for (auto Leg : InWalker.Get_Legs())
        {
            if (utils_procedural_leg::Get_Placement(Leg).Get_HipLocal().Y > 0.0)
            {
                return Leg;
            }
        }
        return FCk_Handle_ProceduralLeg();
    }

    UFUNCTION()
    private void Step_ComposeGaits(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _BehindBody = DoCompose_Body(_BehindSceneX);
        _AheadBody = DoCompose_Body(_AheadSceneX);
        auto Behind = DoCompose_Walker(_BehindBody);
        auto Ahead = DoCompose_Walker(_AheadBody);
        _BehindGait = Behind.Get_Gait();
        _AheadGait = Ahead.Get_Gait();
        _BehindLeg = Get_PlusLeg(Behind);
        _AheadLeg = Get_PlusLeg(Ahead);
        _StanceBody = DoCompose_Body(_StanceSceneX);
        auto Stance = DoCompose_Walker(_StanceBody);
        _StanceGait = Stance.Get_Gait();
        _StanceLeg = Get_PlusLeg(Stance);
        for (auto Leg : Stance.Get_Legs())
        {
            if (utils_procedural_leg::Get_Placement(Leg).Get_HipLocal().Y < 0.0)
            {
                _OtherStanceLeg = Leg;
            }
        }
        Assert_True(ck::IsValid(_StanceGait) && Stance.Get_Legs().Num() == 2
            && ck::IsValid(_StanceLeg) && ck::IsValid(_OtherStanceLeg),
            "The third scene admits both complete authored legs without borrowed limbs or persistent extra support");
        Assert_True(utils_surface_motion::DoCast(_StanceBody).IsSet() == false,
            "Precondition: the third body has no SurfaceMotion, so both missing future targets exercise inferred airborne handling");
        Assert_True(ck::IsValid(_BehindGait) && ck::IsValid(_AheadGait) && ck::IsValid(_BehindLeg) && ck::IsValid(_AheadLeg),
            "Both gait-only side walkers are admitted with a +Y leg");
    }

    bool Get_HoldsTop(FCk_Handle_ProceduralLeg InLeg, float InSceneX, float InMinX, float InMaxX) const
    {
        auto Foot = utils_procedural_leg::Get_Foot(InLeg);
        auto Local = Foot.Get_Position() - _Origin - FVector(InSceneX, 0.0, 0.0);
        return Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted && Foot.Get_Foothold() == ECk_ProceduralLeg_Foothold::Held
            && Local.X >= InMinX && Local.X <= InMaxX && Math::Abs(Local.Z - Get_TopZ()) <= _Tolerance;
    }

    UFUNCTION()
    private void Check_Held(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        auto StanceHeld = utils_procedural_gait::Get_Status(_StanceGait) == ECk_ProceduralAnimation_Status::Ready;
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Leg = Index == 0 ? _StanceLeg : _OtherStanceLeg;
            auto Top = Index == 0 ? _StanceTopEntity : _OtherStanceTopEntity;
            auto Foot = utils_procedural_leg::Get_Foot(Leg);
            auto Contact = Get_StanceContact(Foot.Get_Position());
            StanceHeld = StanceHeld && Get_HoldsTop(Leg, _StanceSceneX, _BehindHoldMinX, _BehindHoldMaxX)
                && Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                && Contact.Get_HasHit() && Contact.Get_Entity() == Top;
            if (Index == 0) { _StancePlant = Foot.Get_Position(); }
            else { _OtherStancePlant = Foot.Get_Position(); }
        }
        Result.Set(StanceHeld && utils_procedural_gait::Get_Status(_BehindGait) == ECk_ProceduralAnimation_Status::Ready
            && utils_procedural_gait::Get_Status(_AheadGait) == ECk_ProceduralAnimation_Status::Ready
            && Get_HoldsTop(_BehindLeg, _BehindSceneX, _BehindHoldMinX, _BehindHoldMaxX)
            && Get_HoldsTop(_AheadLeg, _AheadSceneX, _AheadHoldMinX, _AheadHoldMaxX));
    }

    UFUNCTION()
    private void Step_StartPhase(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _PhaseStartTime = float(System::GetGameTimeInSeconds());
    }

    float Get_PhaseShare(float InSeconds) const
    {
        return Math::Clamp((float(System::GetGameTimeInSeconds()) - _PhaseStartTime) / InSeconds, 0.0, 1.0);
    }

    void DoPlace_Body(FCk_Handle_Transform InBody, float InSceneX, float InX)
    {
        utils_transform::Request_SetTransform(InBody, FTransform(_Origin + FVector(InSceneX + InX, 0.0, _BodyHeight)));
    }

    UFUNCTION()
    private void Check_BackedAway(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _PhaseStartTime;
        auto BackShare = Math::Clamp(Elapsed / _BackSeconds, 0.0, 1.0);
        auto CreepShare = Math::Clamp((Elapsed - _BackSeconds) / _CreepSeconds, 0.0, 1.0);
        DoPlace_Body(_AheadBody, _AheadSceneX, -_AheadBack * BackShare + _AheadCreep * CreepShare);
        auto Result = OutResult;
        Result.Set(CreepShare >= 1.0);
    }

    UFUNCTION()
    private void Step_AddMiddleTop(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        DoAdd_Top(_AheadSceneX, _MiddleMinX, _MiddleMaxX);
    }

    bool Get_IsSearchSource(ECk_ProceduralLeg_Foothold InSource) const
    {
        return InSource == ECk_ProceduralLeg_Foothold::Ring || InSource == ECk_ProceduralLeg_Foothold::Inward
            || InSource == ECk_ProceduralLeg_Foothold::Outward || InSource == ECk_ProceduralLeg_Foothold::Front;
    }

    UFUNCTION()
    private void Check_Watched(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Share = Get_PhaseShare(_WalkSeconds);
        DoPlace_Body(_BehindBody, _BehindSceneX, _BehindWalk * Share);
        DoPlace_Body(_AheadBody, _AheadSceneX, -_AheadBack + _AheadCreep + _AheadWalk * Share);
        // Actual body motion creates velocity lead; the stance remains physically in reach while the future query retires it.
        auto StepSeconds = ck::ProceduralTest_NarrowRingGait.Get_Timing().Get_StepDuration().Get_Seconds();
        DoPlace_Body(_StanceBody, _StanceSceneX, 60.0 * Get_PhaseShare(StepSeconds));
        auto BothWithoutFuture = true;
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Leg = Index == 0 ? _StanceLeg : _OtherStanceLeg;
            auto Plant = Index == 0 ? _StancePlant : _OtherStancePlant;
            auto Top = Index == 0 ? _StanceTopEntity : _OtherStanceTopEntity;
            auto Foot = utils_procedural_leg::Get_Foot(Leg);
            auto WithoutFuture = Foot.Get_Foothold() == ECk_ProceduralLeg_Foothold::None;
            BothWithoutFuture = BothWithoutFuture && WithoutFuture;
            if (WithoutFuture == false) { continue; }
            auto Contact = Get_StanceContact(Foot.Get_Position());
            auto Reach = 0.0;
            auto Lengths = utils_procedural_leg::Get_ChainGeometry(Leg).Get_SegmentLengths();
            for (auto Length : Lengths) { Reach += Length; }
            auto Hip = utils_transform::Get_EntityCurrentTransform(_StanceBody).TransformPosition(
                utils_procedural_leg::Get_Placement(Leg).Get_HipLocal());
            auto HipDistance = (Foot.Get_Position() - Hip).Size();
            if (Foot.Get_Phase() != ECk_ProceduralLeg_FootPhase::Planted
                || Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted
                || Foot.Get_Position().Equals(Plant, _Tolerance) == false
                || Contact.Get_HasHit() == false || Contact.Get_Entity() != Top
                || Reach <= 0.0 || HipDistance > Reach)
            {
                _StanceViolations++;
                if (_FirstStanceViolation.IsEmpty())
                {
                    auto Id = utils_procedural_leg::Get_Id(Leg);
                    auto PhaseValue = int32(Foot.Get_Phase());
                    auto ContactValue = int32(Foot.Get_Contact());
                    _FirstStanceViolation = f"{Id}: phase {PhaseValue}, contact {ContactValue}, hip distance {HipDistance :.3}/{Reach :.3}";
                }
            }
        }
        if (BothWithoutFuture)
        {
            auto Now = float(System::GetGameTimeInSeconds());
            if (_StanceRetiredAt < 0.0) { _StanceRetiredAt = Now; }
            _StanceNoFutureSamples++;
            _LongestStanceNoFuture = Math::Max(_LongestStanceNoFuture, Now - _StanceRetiredAt);
        }
        else { _StanceRetiredAt = -1.0; }
        _WatchedFrames++;

        auto BehindFoot = utils_procedural_leg::Get_Foot(_BehindLeg);
        auto BehindLocal = BehindFoot.Get_Position() - _Origin - FVector(_BehindSceneX, 0.0, 0.0);
        _BehindSearched = _BehindSearched || Get_IsSearchSource(BehindFoot.Get_Foothold());
        _BehindOnTopAhead = BehindFoot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted && BehindLocal.X >= _BehindAheadMinX
            && BehindLocal.X <= _BehindAheadMinX + _TopLength && Math::Abs(BehindLocal.Z - Get_TopZ()) <= _Tolerance;
        auto BehindSourceValue = int32(BehindFoot.Get_Foothold());
        _BehindLast = f"foot ({BehindLocal.X :.1}, {BehindLocal.Y :.1}, {BehindLocal.Z :.1}), source {BehindSourceValue}";

        if (Get_HoldsTop(_AheadLeg, _AheadSceneX, _AheadHoldMinX, _AheadHoldMaxX) == false)
        {
            _AheadViolations++;
            if (_FirstAheadViolation.IsEmpty())
            {
                auto AheadFoot = utils_procedural_leg::Get_Foot(_AheadLeg);
                auto AheadLocal = AheadFoot.Get_Position() - _Origin - FVector(_AheadSceneX, 0.0, 0.0);
                auto AheadSourceValue = int32(AheadFoot.Get_Foothold());
                _FirstAheadViolation = f"frame {_WatchedFrames}: foot ({AheadLocal.X :.1}, {AheadLocal.Y :.1}, {AheadLocal.Z :.1}), source {AheadSourceValue}";
            }
        }

        auto Result = OutResult;
        Result.Set(float(System::GetGameTimeInSeconds()) - _PhaseStartTime >= _WalkSeconds + _WatchSeconds);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_WatchedFrames > 0, "Precondition: the +Y legs were watched");
        Assert_True(_BehindSearched && _BehindOnTopAhead,
            f"A hold behind the hip and beyond the keep radius does not stop the search: the leg takes a searched foothold and plants on the top ahead (searched {_BehindSearched}; last {_BehindLast})");
        Assert_True(_AheadViolations == 0,
            f"Control: a hold ahead of the hip keeps serving beyond the keep radius although a search would find a cheaper top ({_AheadViolations} of {_WatchedFrames} frames otherwise; first: {_FirstAheadViolation})");
        auto Grace = ck::ProceduralTest_NarrowRingGait.Get_Probe().Get_ContactGrace().Get_Seconds();
        Assert_True(_StanceNoFutureSamples > 0 && _LongestStanceNoFuture > Grace,
            "Precondition: both authored legs simultaneously have no future target for longer than contact grace");
        Assert_Equals_Int(_StanceViolations, 0,
            f"Both freshly confirmed native stances stay planted and trusted while every future target is absent beyond grace ({_FirstStanceViolation})");
    }

    UFUNCTION()
    private void Step_ReachWithin(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        DoPlace_ReachBoundary(0.0005);
    }

    UFUNCTION()
    private void Step_ReachOutside(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        DoPlace_ReachBoundary(0.002);
    }

    void DoPlace_ReachBoundary(float InExcess)
    {
        auto Transform = utils_transform::Get_EntityCurrentTransform(_StanceBody);
        auto Location = Transform.GetLocation();
        auto First = true;
        auto LimitingX = 0.0;
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Leg = Index == 0 ? _StanceLeg : _OtherStanceLeg;
            auto Reach = 0.0;
            auto Lengths = utils_procedural_leg::Get_ChainGeometry(Leg).Get_SegmentLengths();
            for (auto Length : Lengths) { Reach += Length; }
            auto Plant = utils_procedural_leg::Get_Foot(Leg).Get_Position();
            auto Hip = Transform.TransformPosition(utils_procedural_leg::Get_Placement(Leg).Get_HipLocal());
            auto HipOffset = Hip - Location;
            auto Distance = Reach + InExcess;
            auto OtherAxesSquared = Math::Square(Plant.Y - Hip.Y) + Math::Square(Plant.Z - Hip.Z);
            if (Reach <= 0.0 || Math::Square(Distance) <= OtherAxesSquared)
            {
                FinishFailure("Both actual stances must admit the authored physical reach boundary");
                return;
            }
            auto LimitX = Plant.X + Math::Sqrt(Math::Square(Distance) - OtherAxesSquared) - HipOffset.X;
            if (First || LimitX < LimitingX)
            {
                First = false;
                LimitingX = LimitX;
                _BoundaryReach = Reach;
                _BoundaryDistance = Distance;
                _BoundaryPlant = Plant;
                _BoundaryLeg = Leg;
            }
        }
        Location.X = LimitingX;
        Transform.SetLocation(Location);
        utils_transform::Request_SetTransform(_StanceBody, Transform);
        _BoundaryObservedAt = -1.0;
        _BoundarySolveSequence = -1;
        _BoundaryViolations = 0;
    }

    bool Get_IsReachBoundaryApplied() const
    {
        auto Hip = utils_transform::Get_EntityCurrentTransform(_StanceBody).TransformPosition(
            utils_procedural_leg::Get_Placement(_BoundaryLeg).Get_HipLocal());
        return Math::Abs((_BoundaryPlant - Hip).Size() - _BoundaryDistance) <= 0.000001;
    }

    UFUNCTION()
    private void Check_ReachWithin(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Now = float(System::GetGameTimeInSeconds());
        auto Applied = Get_IsReachBoundaryApplied();
        auto Sequence = UCk_Utils_AutoTest_UE::Get_ProceduralAnimationSolveSequence(_StanceBody);
        if (Applied && Sequence < 0)
        {
            FinishFailure("The applied physical reach boundary has no accepted gait solve sequence");
            return;
        }
        if (Applied && _BoundaryObservedAt < 0.0)
        {
            _BoundaryObservedAt = Now;
            _BoundarySolveSequence = Sequence;
        }
        auto Grace = ck::ProceduralTest_NarrowRingGait.Get_Probe().Get_ContactGrace().Get_Seconds();
        auto Observed = Applied && _BoundaryObservedAt >= 0.0 && Now - _BoundaryObservedAt > Grace
            && _BoundarySolveSequence >= 0 && Sequence >= 0 && Sequence > _BoundarySolveSequence;
        if (Applied)
        {
            auto BothSupported = true;
            auto BothWithoutFuture = true;
            for (int32 Index = 0; Index < 2; Index++)
            {
                auto Leg = Index == 0 ? _StanceLeg : _OtherStanceLeg;
                auto Top = Index == 0 ? _StanceTopEntity : _OtherStanceTopEntity;
                auto Foot = utils_procedural_leg::Get_Foot(Leg);
                auto Contact = Get_StanceContact(Foot.Get_Position());
                BothWithoutFuture = BothWithoutFuture && Foot.Get_Foothold() == ECk_ProceduralLeg_Foothold::None;
                BothSupported = BothSupported && Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted
                    && Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                    && Contact.Get_HasHit() && Contact.Get_Entity() == Top;
            }
            if (BothWithoutFuture == false || BothSupported == false
                || utils_procedural_leg::Get_Foot(_BoundaryLeg).Get_Position().Equals(_BoundaryPlant, 0.000001) == false)
            {
                _BoundaryViolations++;
            }
        }
        if (Observed)
        {
            Assert_Equals_Int(_BoundaryViolations, 0,
                "The limiting native plant at Reach plus 0.0005 cm and the other actual stance stay supported across grace");
        }
        auto Result = OutResult;
        Result.Set(Observed);
    }

    UFUNCTION()
    private void Check_ReachOutside(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Now = float(System::GetGameTimeInSeconds());
        auto Applied = Get_IsReachBoundaryApplied();
        auto Sequence = UCk_Utils_AutoTest_UE::Get_ProceduralAnimationSolveSequence(_StanceBody);
        if (Applied && Sequence < 0)
        {
            FinishFailure("The applied physical reach rejection has no accepted gait solve sequence");
            return;
        }
        if (Applied && _BoundaryObservedAt < 0.0)
        {
            _BoundaryObservedAt = Now;
            _BoundarySolveSequence = Sequence;
        }
        auto ObservedNextTick = Applied && _BoundaryObservedAt >= 0.0 && Now > _BoundaryObservedAt
            && _BoundarySolveSequence >= 0 && Sequence >= 0 && Sequence > _BoundarySolveSequence;
        if (ObservedNextTick)
        {
            auto Grace = ck::ProceduralTest_NarrowRingGait.Get_Probe().Get_ContactGrace().Get_Seconds();
            auto Contact = Get_StanceContact(_BoundaryPlant);
            auto Top = _BoundaryLeg == _StanceLeg ? _StanceTopEntity : _OtherStanceTopEntity;
            Assert_True(Now - _BoundaryObservedAt < Grace && Contact.Get_HasHit() && Contact.Get_Entity() == Top,
                "The physical reach rejection is observed before grace while the limiting native support still exists");
            Assert_True(utils_procedural_leg::Get_Foot(_StanceLeg).Get_Foothold() == ECk_ProceduralLeg_Foothold::None
                && utils_procedural_leg::Get_Foot(_OtherStanceLeg).Get_Foothold() == ECk_ProceduralLeg_Foothold::None,
                "Neither future target supplies the physical reach rejection");
            Assert_True(utils_procedural_leg::Get_Foot(_BoundaryLeg).Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted,
                "The limiting actual plant beyond Reach plus 0.002 cm withdraws trust instead of preserving stale support");
        }
        auto Result = OutResult;
        Result.Set(ObservedNextTick);
    }

    UFUNCTION()
    private void Step_ResetReach(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        DoPlace_Body(_StanceBody, _StanceSceneX, 0.0);
    }

    UFUNCTION()
    private void Step_RemoveStanceTop(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _NativeRemovalObservedAt = -1.0;
        _RemovalSolveSequence = -1;
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Leg = Index == 0 ? _StanceLeg : _OtherStanceLeg;
            auto Top = Index == 0 ? _StanceTopEntity : _OtherStanceTopEntity;
            auto Foot = utils_procedural_leg::Get_Foot(Leg);
            auto Contact = Get_StanceContact(Foot.Get_Position());
            Assert_True(Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                && Contact.Get_HasHit() && Contact.Get_Entity() == Top,
                "Each ordinary planted foot is trusted on its live owned native top immediately before removal");
            if (Index == 0) { _StancePlant = Foot.Get_Position(); }
            else { _OtherStancePlant = Foot.Get_Position(); }
        }
        utils_entity_lifetime::Request_DestroyEntity(_StanceTopEntity);
        utils_entity_lifetime::Request_DestroyEntity(_OtherStanceTopEntity);
    }

    UFUNCTION()
    private void Check_StanceRemoved(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Now = float(System::GetGameTimeInSeconds());
        auto Contact = Get_StanceContact(_StancePlant);
        auto OtherContact = Get_StanceContact(_OtherStancePlant);
        auto Removed = ck::Is_NOT_Valid(_StanceTop) && ck::Is_NOT_Valid(_OtherStanceTop)
            && Contact.Get_HasHit() == false && OtherContact.Get_HasHit() == false;
        auto Sequence = UCk_Utils_AutoTest_UE::Get_ProceduralAnimationSolveSequence(_StanceBody);
        if (Removed && Sequence < 0)
        {
            FinishFailure("Native support removal has no accepted gait solve sequence");
            return;
        }
        if (Removed && _NativeRemovalObservedAt < 0.0)
        {
            _NativeRemovalObservedAt = Now;
            _RemovalSolveSequence = Sequence;
        }
        auto ObservedNextTick = Removed && _NativeRemovalObservedAt >= 0.0 && Now > _NativeRemovalObservedAt
            && _RemovalSolveSequence >= 0 && Sequence >= 0 && Sequence > _RemovalSolveSequence;
        if (ObservedNextTick)
        {
            // A newer accepted solve proves the producer consumed the post-removal state; time alone does not.
            auto Grace = ck::ProceduralTest_NarrowRingGait.Get_Probe().Get_ContactGrace().Get_Seconds();
            Assert_True(Now - _NativeRemovalObservedAt < Grace,
                "Contact invalidation is observed before ordinary missing-target grace could hide stale trust");
            Assert_True(utils_procedural_leg::Get_Foot(_StanceLeg).Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted
                && utils_procedural_leg::Get_Foot(_OtherStanceLeg).Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted,
                "Removing both actual native supports clears ordinary stance trust without a lease or explicit recovery");
        }
        auto Result = OutResult;
        Result.Set(ObservedNextTick);
    }

    UFUNCTION()
    private void Step_RestoreStanceTop(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        DoPlace_Body(_StanceBody, _StanceSceneX, 0.0);
        DoAdd_StanceTop();
    }

    UFUNCTION()
    private void Check_StanceReacquired(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Reacquired = ck::IsValid(_StanceTop) && utils_jolt_body::Get_IsBodyAdded(_StanceTop)
            && ck::IsValid(_OtherStanceTop) && utils_jolt_body::Get_IsBodyAdded(_OtherStanceTop);
        for (int32 Index = 0; Index < 2; Index++)
        {
            auto Leg = Index == 0 ? _StanceLeg : _OtherStanceLeg;
            auto Top = Index == 0 ? _StanceTopEntity : _OtherStanceTopEntity;
            auto Foot = utils_procedural_leg::Get_Foot(Leg);
            auto Contact = Get_StanceContact(Foot.Get_Position());
            Reacquired = Reacquired && Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted
                && Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                && Contact.Get_HasHit() && Contact.Get_Entity() == Top;
        }
        auto Result = OutResult;
        Result.Set(Reacquired);
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
