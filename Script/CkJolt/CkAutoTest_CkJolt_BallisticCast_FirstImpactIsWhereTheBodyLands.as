// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: A BALLISTIC CAST'S FIRST IMPACT IS WHERE THE BODY LANDS
//============================================================================
//
// The same damped sphere and launch as the free-flight test, over a Static
// floor 300 cm below the launch:
//   1. The cast hits the floor: the hit names the floor entity, its normal is
//      up, its contact point lies on the floor's top face.
//   2. The last path point is the sphere's centre resting on that face (Z =
//      top + radius), still falling, before the max duration.
//   3. The real body's first OnJoltBodyContactAdded against the floor reports a
//      contact point within 12 cm of the predicted one (one step of travel is
//      the speculative-contact slack).
//
// Isolated origin far from every other autotest.
//============================================================================

class UCk_AutoTest_CkJolt_BallisticCast_FirstImpactIsWhereTheBodyLands : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;

    private FCk_Handle _SelfHandle;
    private FCk_Handle _FloorEntity;
    private FCk_Handle_JoltBody _Floor;
    private FCk_Handle _BodyEntity;
    private FCk_Handle_JoltBody _Body;
    private FCk_Handle_Transform _BodyTransform;

    private FVector _Origin = FVector(0.0, -140000.0, 30000.0);
    private FVector _LaunchVelocity = FVector(600.0, 0.0, 500.0);
    private float _SphereRadius = 10.0;
    private float _FloorTopZ = 0.0;

    private FCk_Jolt_HitResult _PredictedHit;

    private int _Phase = 0;   // 0 = wait for both bodies, then predict + launch; 1 = wait for the contact
    private float _Elapsed = 0.0;
    private float _ElapsedSinceLaunch = 0.0;
    private bool _HasContact = false;
    private float _ContactDistance = -1.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;

        // ---- Static floor, its top face 300 cm below the launch ---------------------------------
        _FloorTopZ = _Origin.Z - 300.0;
        _FloorEntity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        _FloorEntity.Request_OverrideToSelf();
        utils_transform::Add(_FloorEntity,
            FTransform(FRotator::ZeroRotator, FVector(_Origin.X, _Origin.Y, _FloorTopZ - 25.0)),
            ECk_Replication::DoesNotReplicate);

        auto FloorShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        FloorShape.Set_HalfExtents(FVector(2000.0, 2000.0, 25.0));
        auto FloorParams = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        FloorParams.Set_ShapeDimensions(FloorShape);
        FloorParams.Set_MotionType(ECk_MotionType::Static);
        _Floor = utils_jolt_body::Add(_FloorEntity, FloorParams);

        // ---- Damped sphere, asleep at the launch location ---------------------------------------
        _BodyEntity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        _BodyEntity.Request_OverrideToSelf();
        _BodyTransform = utils_transform::Add(_BodyEntity, FTransform(FRotator::ZeroRotator, _Origin),
            ECk_Replication::DoesNotReplicate);

        auto SphereShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Sphere);
        SphereShape.Set_Radius(_SphereRadius);
        auto SphereParams = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        SphereParams.Set_ShapeDimensions(SphereShape);
        SphereParams.Set_MotionType(ECk_MotionType::Dynamic);
        SphereParams.Set_MotionQuality(ECk_MotionQuality::LinearCast);
        SphereParams.Set_LinearDamping(0.2);
        SphereParams.Set_InitialSleepState(ECk_Jolt_SleepState::Asleep);
        _Body = utils_jolt_body::Add(_BodyEntity, SphereParams);

        utils_jolt_body::BindTo_OnJoltBodyContactAdded(_Body,
            FCk_Delegate_JoltBody_OnContact(this, n"OnContactAdded"));

        utils_timer::Create_Tick(_SelfHandle, FCk_Delegate_Timer(this, n"OnTick"));
    }

    private void DoPredictAndLaunch()
    {
        const auto Start = utils_transform::Get_EntityCurrentLocation(_BodyTransform);

        auto SphereShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Sphere);
        SphereShape.Set_Radius(_SphereRadius);

        TArray<FCk_Handle> Ignored;
        Ignored.Add(_BodyEntity);

        auto Params = FCk_Jolt_BallisticCast_Params();
        Params.Set_StartLocation(Start);
        Params.Set_StartVelocity(_LaunchVelocity);
        Params.Set_Flight(utils_jolt_body::Get_BallisticFlight(_Body));
        Params.Set_Shape(SphereShape);
        Params.Set_IgnoredEntities(Ignored);

        const auto Prediction = utils_jolt_query::Get_BallisticCast(Params);
        _PredictedHit = Prediction.Get_Hit();

        Assert_True(_PredictedHit.Get_HasHit(), "The ballistic cast hits the floor");
        Assert_True(_PredictedHit.Get_Entity() == _FloorEntity, "The hit names the floor entity");
        Assert_True(_PredictedHit.Get_Normal().Z > 0.99, f"The hit normal points up (got {_PredictedHit.Get_Normal()})");
        Assert_True(Math::Abs(_PredictedHit.Get_Position().Z - _FloorTopZ) < 1.0,
            f"The contact lies on the floor's top face at Z {_FloorTopZ} (got {_PredictedHit.Get_Position()})");

        const auto Path = Prediction.Get_Path();
        if (Path.Num() < 2)
        {
            FinishFailure(f"The path is too short ({Path.Num()} points)");
            return;
        }

        const auto Last = Path.Last();
        Assert_True(Math::Abs(Last.Get_Location().Z - (_FloorTopZ + _SphereRadius)) < 1.0,
            f"The last point is the centre resting on the floor at Z {_FloorTopZ + _SphereRadius} (got {Last.Get_Location()})");
        Assert_True(Last.Get_Velocity().Z < 0.0, f"The body is still falling at the hit (velocity {Last.Get_Velocity()})");
        Assert_True(Last.Get_Time().Get_Seconds() < Params.Get_MaxDuration().Get_Seconds(),
            f"The hit comes before the max duration (at {Last.Get_Time().Get_Seconds()} s)");

        utils_jolt_body::Request_SetLinearVelocity(_Body, FCk_Request_JoltBody_SetLinearVelocity(_LaunchVelocity));
    }

    UFUNCTION()
    private void OnContactAdded(FCk_Handle_JoltBody InHandle, FCk_JoltBody_Payload_OnContact InPayload)
    {
        if (IsFinished()) { return; }
        if (_HasContact) { return; }
        if (!(InPayload.Get_OtherEntity() == _FloorEntity)) { return; }

        _HasContact = true;

        const auto Points = InPayload.Get_ContactPoints();
        auto Best = 1.0e30;
        for (auto Point : Points)
        {
            const auto Distance = Point.Distance(_PredictedHit.Get_Position());
            if (Distance < Best)
            { Best = Distance; }
        }
        _ContactDistance = Points.Num() > 0 ? Best : -1.0;
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        if (_Phase == 0)
        {
            _Elapsed += float(InDeltaT.Get_Seconds());

            if (utils_jolt_body::Get_IsBodyAdded(_Body) && utils_jolt_body::Get_IsBodyAdded(_Floor))
            {
                DoPredictAndLaunch();
                _Phase = 1;
                _ElapsedSinceLaunch = 0.0;
                return;
            }

            if (_Elapsed > 5.0)
            { FinishFailure("The floor or the sphere body was never added to the simulation"); }

            return;
        }

        _ElapsedSinceLaunch += float(InDeltaT.Get_Seconds());

        if (_HasContact)
        {
            Assert_True(_ContactDistance >= 0.0, "The floor contact carries at least one contact point");
            Assert_True(_ContactDistance <= 12.0,
                f"The real contact lands within 12 cm of the predicted one (got {_ContactDistance} cm)");

            utils_entity_lifetime::Request_DestroyEntity(_BodyEntity);
            utils_entity_lifetime::Request_DestroyEntity(_FloorEntity);
            FinishSuccess();
            return;
        }

        if (_ElapsedSinceLaunch > 5.0)
        { FinishFailure("The sphere never touched the floor"); }
    }
}
