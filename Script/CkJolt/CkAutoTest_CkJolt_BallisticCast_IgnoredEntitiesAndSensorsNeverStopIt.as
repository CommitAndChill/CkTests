// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: IGNORED ENTITIES AND SENSORS NEVER STOP A BALLISTIC CAST
//============================================================================
//
// A horizontal launch at a Static wall 50 cm away, with a Static floor far
// below and beyond it:
//   (a) plain: the cast stops at the wall.
//   (b) the wall in _IgnoredEntities: the cast passes it (and lands on the floor).
//   (d) started inside the wall: a blocked launch, reported as a hit whose last
//       path point is still at time 0.
//   (c) the wall replaced by a probe (a sensor) of the same size: an ordinary
//       shape cast with the same filter sees the probe, the ballistic cast does not.
//
// Isolated origin far from every other autotest.
//============================================================================

class UCk_AutoTest_CkJolt_BallisticCast_IgnoredEntitiesAndSensorsNeverStopIt : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;

    private FCk_Handle _SelfHandle;
    private FCk_Handle _WallEntity;
    private FCk_Handle_JoltBody _Wall;
    private FCk_Handle _FloorEntity;
    private FCk_Handle_JoltBody _Floor;
    private FCk_Handle _ProbeEntity;

    private FVector _Origin = FVector(40000.0, -140000.0, 30000.0);
    private FVector _WallHalfExtents = FVector(25.0, 500.0, 500.0);
    private float _SphereRadius = 10.0;

    private int _Phase = 0;   // 0 = wait for wall + floor; 1 = wait for the probe
    private float _Elapsed = 0.0;

    private FVector Get_WallCenter() const
    { return _Origin + FVector(50.0 + _WallHalfExtents.X, 0.0, 0.0); }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;

        _WallEntity = Make_StaticBox(Get_WallCenter(), _WallHalfExtents);
        _Wall = _LastStaticBody;

        _FloorEntity = Make_StaticBox(_Origin + FVector(1500.0, 0.0, -2000.0), FVector(3000.0, 3000.0, 25.0));
        _Floor = _LastStaticBody;

        utils_timer::Create_Tick(_SelfHandle, FCk_Delegate_Timer(this, n"OnTick"));
    }

    private FCk_Handle_JoltBody _LastStaticBody;

    private FCk_Handle Make_StaticBox(FVector InCenter, FVector InHalfExtents)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        Entity.Request_OverrideToSelf();
        utils_transform::Add(Entity, FTransform(FRotator::ZeroRotator, InCenter), ECk_Replication::DoesNotReplicate);

        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(InHalfExtents);
        auto Params = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Params.Set_ShapeDimensions(Shape);
        Params.Set_MotionType(ECk_MotionType::Static);
        _LastStaticBody = utils_jolt_body::Add(Entity, Params);
        return Entity;
    }

    // WorldDynamic at Overlap: the only channel a probe's layer answers, and one the PhysicsActor walls block, so the
    // probe in (c) is a body this filter would otherwise stop at.
    private FCk_Jolt_QueryFilter Make_Filter()
    {
        auto Filter = FCk_Jolt_QueryFilter();
        Filter.Set_Channel(ECollisionChannel::ECC_WorldDynamic);
        Filter.Set_MinResponse(ECk_Jolt_PairInteraction::Overlap);
        return Filter;
    }

    private FCk_Jolt_BallisticCast_Params Make_Params(FVector InStart)
    {
        auto SphereShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Sphere);
        SphereShape.Set_Radius(_SphereRadius);

        auto Params = FCk_Jolt_BallisticCast_Params();
        Params.Set_StartLocation(InStart);
        Params.Set_StartVelocity(FVector(600.0, 0.0, 0.0));
        Params.Set_Shape(SphereShape);
        Params.Set_Filter(Make_Filter());
        Params.Set_MaxDuration(FCk_Time(5.0));
        return Params;
    }

    private void DoCheckWall()
    {
        // (a) plain
        const auto Plain = utils_jolt_query::Get_BallisticCast(Make_Params(_Origin));
        Assert_True(Plain.Get_Hit().Get_HasHit(), "(a) the cast hits something");
        Assert_True(Plain.Get_Hit().Get_Entity() == _WallEntity, "(a) the cast stops at the wall");

        // (b) the wall ignored
        auto IgnoringWall = Make_Params(_Origin);
        TArray<FCk_Handle> Ignored;
        Ignored.Add(_WallEntity);
        IgnoringWall.Set_IgnoredEntities(Ignored);
        const auto PastWall = utils_jolt_query::Get_BallisticCast(IgnoringWall);
        Assert_True(PastWall.Get_Hit().Get_HasHit(), "(b) the cast still lands somewhere");
        Assert_False(PastWall.Get_Hit().Get_Entity() == _WallEntity, "(b) an ignored wall never stops the cast");
        Assert_True(PastWall.Get_Hit().Get_Entity() == _FloorEntity, "(b) the cast passes the wall and lands on the floor");

        // (d) a launch from inside the wall is blocked at once
        const auto InsideStart = Get_WallCenter() - FVector(_WallHalfExtents.X - 5.0, 0.0, 0.0);
        const auto Blocked = utils_jolt_query::Get_BallisticCast(Make_Params(InsideStart));
        Assert_True(Blocked.Get_Hit().Get_HasHit(), "(d) a launch from inside the wall reports a hit");
        Assert_True(Blocked.Get_Hit().Get_Entity() == _WallEntity, "(d) the blocking hit is the wall");
        const auto BlockedPath = Blocked.Get_Path();
        Assert_Equals_Int(BlockedPath.Num(), 2, "(d) a blocked launch is two points");
        if (BlockedPath.Num() > 0)
        {
            Assert_True(BlockedPath.Last().Get_Time().Get_Seconds() == 0.0,
                f"(d) the blocked launch's last point is at time 0 (got {BlockedPath.Last().Get_Time().Get_Seconds()} s)");
            Assert_True(BlockedPath.Last().Get_Location().Distance(InsideStart) < 0.001,
                "(d) the blocked launch's last point is where it started");
        }

        // (c) swap the wall for a probe of the same size
        utils_entity_lifetime::Request_DestroyEntity(_WallEntity);

        _ProbeEntity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        _ProbeEntity.Request_OverrideToSelf();
        auto ProbeTransform = utils_transform::Add(_ProbeEntity, FTransform(FRotator::ZeroRotator, Get_WallCenter()),
            ECk_Replication::DoesNotReplicate);

        auto ProbeParams = FCk_Probe_Spec(utils_gameplay_tag::ResolveGameplayTag(n"CkTests.Probe.LinearCast.Wall"));
        ProbeParams.Set_MotionType(ECk_MotionType::Static);
        ProbeParams.Set_ResponsePolicy(ECk_ProbeResponse_Policy::Silent);
        utils_probe::Add_Box(ProbeTransform, _WallHalfExtents, ProbeParams, FCk_Probe_DebugInfo());
    }

    // The control: a shape cast does not skip sensors, so once it reports the probe, the probe is in the world and
    // answers this filter.
    private bool Get_ShapeCastSeesProbe()
    {
        auto SphereShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Sphere);
        SphereShape.Set_Radius(_SphereRadius);

        const auto Hit = utils_jolt_query::Get_ShapeCast(
            _Origin, _Origin + FVector(200.0, 0.0, 0.0), FRotator::ZeroRotator, SphereShape, Make_Filter());
        return Hit.Get_HasHit() && Hit.Get_Entity() == _ProbeEntity;
    }

    private void DoCheckProbe()
    {
        const auto ThroughProbe = utils_jolt_query::Get_BallisticCast(Make_Params(_Origin));
        Assert_True(ThroughProbe.Get_Hit().Get_HasHit(), "(c) the cast still lands somewhere");
        Assert_False(ThroughProbe.Get_Hit().Get_Entity() == _ProbeEntity, "(c) a sensor never stops the cast");
        Assert_True(ThroughProbe.Get_Hit().Get_Entity() == _FloorEntity, "(c) the cast passes the probe and lands on the floor");
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        _Elapsed += float(InDeltaT.Get_Seconds());

        if (_Phase == 0)
        {
            if (utils_jolt_body::Get_IsBodyAdded(_Wall) && utils_jolt_body::Get_IsBodyAdded(_Floor))
            {
                DoCheckWall();
                _Phase = 1;
                _Elapsed = 0.0;
                return;
            }

            if (_Elapsed > 5.0)
            { FinishFailure("The wall or the floor body was never added to the simulation"); }

            return;
        }

        // The destroyed wall's body leaves the world with its entity; only then is the probe alone in its place.
        if (!ck::IsValid(_WallEntity) && Get_ShapeCastSeesProbe())
        {
            DoCheckProbe();

            utils_entity_lifetime::Request_DestroyEntity(_ProbeEntity);
            utils_entity_lifetime::Request_DestroyEntity(_FloorEntity);
            FinishSuccess();
            return;
        }

        if (_Elapsed > 4.0)
        { FinishFailure("Control failed: the wall never left, or an ordinary shape cast never saw the probe that replaced it"); }
    }
}
