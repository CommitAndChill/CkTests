// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: A BALLISTIC CAST PREDICTS THE BODY'S FREE FLIGHT
//============================================================================
//
// Get_BallisticCast replays the simulation's own free-flight step, so a real
// Dynamic body launched with the same velocity and flight must stay on the
// predicted polyline:
//   1. A damped (0.2) LinearCast sphere spawned ASLEEP high above everything,
//      so it sits exactly at its spawn location until the launch.
//   2. Once the body is added: predict from its current location with its own
//      flight (Get_BallisticFlight), one step per segment, the body ignored;
//      then launch it with Request_SetLinearVelocity on the same frame.
//   3. Every location sampled over the next 1.2 s of tick delta lies within
//      1 cm of the predicted polyline (at least 20 samples).
//   4. The path carries strictly increasing times ending one step from 1.5 s,
//      and its velocities change along it; an undamped prediction ends more
//      than 20 cm from the damped one.
//   5. A 150-step / 50-segment flight that hits nothing has 51 points and no hit.
//
// Isolated origin far from every other autotest; no floor below it.
//============================================================================

class UCk_AutoTest_CkJolt_BallisticCast_FreeFlightFollowsThePredictedPath : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;

    private FCk_Handle _SelfHandle;
    private FCk_Handle _BodyEntity;
    private FCk_Handle_JoltBody _Body;
    private FCk_Handle_Transform _BodyTransform;

    private FVector _Origin = FVector(-20000.0, -140000.0, 30000.0);
    private FVector _LaunchVelocity = FVector(600.0, 0.0, 500.0);

    private TArray<FCk_Jolt_BallisticCast_Point> _Predicted;

    private int _Phase = 0;   // 0 = wait for the body, then predict + launch; 1 = sample the flight
    private float _Elapsed = 0.0;
    private float _ElapsedSinceLaunch = 0.0;
    private int _NumSamples = 0;
    private float _MaxError = 0.0;
    private float _MaxErrorAtTime = 0.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;

        _BodyEntity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        _BodyEntity.Request_OverrideToSelf();
        _BodyTransform = utils_transform::Add(_BodyEntity, FTransform(FRotator::ZeroRotator, _Origin),
            ECk_Replication::DoesNotReplicate);

        auto SphereShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Sphere);
        SphereShape.Set_Radius(10.0);
        auto SphereParams = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        SphereParams.Set_ShapeDimensions(SphereShape);
        SphereParams.Set_MotionType(ECk_MotionType::Dynamic);
        SphereParams.Set_MotionQuality(ECk_MotionQuality::LinearCast);
        SphereParams.Set_LinearDamping(0.2);
        // Asleep: a sleeping body does not fall, so the flight starts exactly where it was spawned.
        SphereParams.Set_InitialSleepState(ECk_Jolt_SleepState::Asleep);
        _Body = utils_jolt_body::Add(_BodyEntity, SphereParams);

        utils_timer::Create_Tick(_SelfHandle, FCk_Delegate_Timer(this, n"OnTick"));
    }

    private FCk_Jolt_BallisticCast_Params Make_Params(FVector InStart, FCk_Jolt_BallisticFlight InFlight)
    {
        auto SphereShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Sphere);
        SphereShape.Set_Radius(10.0);

        TArray<FCk_Handle> Ignored;
        Ignored.Add(_BodyEntity);

        auto Params = FCk_Jolt_BallisticCast_Params();
        Params.Set_StartLocation(InStart);
        Params.Set_StartVelocity(_LaunchVelocity);
        Params.Set_Flight(InFlight);
        Params.Set_Shape(SphereShape);
        Params.Set_StepsPerSegment(1);
        Params.Set_MaxDuration(FCk_Time(1.5));
        Params.Set_IgnoredEntities(Ignored);
        return Params;
    }

    private float Get_DistanceToPredictedPath(FVector InPoint)
    {
        auto Best = 1.0e30;
        for (int32 Index = 0; Index + 1 < _Predicted.Num(); ++Index)
        {
            const auto A = _Predicted[Index].Get_Location();
            const auto B = _Predicted[Index + 1].Get_Location();
            const auto AB = B - A;
            const auto LengthSquared = AB.SizeSquared();
            auto Alpha = 0.0;
            if (LengthSquared > 0.0)
            { Alpha = Math::Clamp((InPoint - A).DotProduct(AB) / LengthSquared, 0.0, 1.0); }

            const auto Distance = InPoint.Distance(A + AB * Alpha);
            if (Distance < Best)
            { Best = Distance; }
        }

        return Best;
    }

    private void DoPredictAndLaunch()
    {
        const auto Start = utils_transform::Get_EntityCurrentLocation(_BodyTransform);
        const auto Flight = utils_jolt_body::Get_BallisticFlight(_Body);

        const auto Prediction = utils_jolt_query::Get_BallisticCast(Make_Params(Start, Flight));
        _Predicted = Prediction.Get_Path();

        Assert_False(Prediction.Get_Hit().Get_HasHit(), "Nothing lies on the free flight, so the cast must not hit");
        Assert_True(_Predicted.Num() > 20, f"The prediction carries one point per step (got {_Predicted.Num()})");
        if (_Predicted.Num() < 3)
        {
            FinishFailure("The prediction is too short to check");
            return;
        }

        Assert_True(_Predicted[0].Get_Location().Distance(Start) < 0.001, "The path starts at the launch location");

        auto TimesIncrease = true;
        for (int32 Index = 1; Index < _Predicted.Num(); ++Index)
        {
            if (_Predicted[Index].Get_Time().Get_Seconds() <= _Predicted[Index - 1].Get_Time().Get_Seconds())
            { TimesIncrease = false; }
        }
        Assert_True(TimesIncrease, "Path times are strictly increasing");

        const auto Dt = _Predicted[1].Get_Time().Get_Seconds();
        const auto LastTime = _Predicted.Last().Get_Time().Get_Seconds();
        Assert_True(Math::Abs(LastTime - 1.5) <= Dt + 0.0001,
            f"The last point is within one step ({Dt} s) of the 1.5 s duration (got {LastTime} s)");

        Assert_True(_Predicted.Last().Get_Velocity().Distance(_Predicted[0].Get_Velocity()) > 100.0,
            f"The path carries its velocities: start {_Predicted[0].Get_Velocity()}, end {_Predicted.Last().Get_Velocity()}");

        // Damping must be honoured: the same launch with none ends far from the damped prediction.
        auto Undamped = Flight;
        Undamped.Set_LinearDamping(0.0);
        const auto UndampedPath = utils_jolt_query::Get_BallisticCast(Make_Params(Start, Undamped)).Get_Path();
        Assert_True(UndampedPath.Num() > 0 &&
            UndampedPath.Last().Get_Location().Distance(_Predicted.Last().Get_Location()) > 20.0,
            "An undamped prediction ends more than 20 cm from the damped one");

        // A 150-step flight in 50 segments that hits nothing (also the cost sample for this query).
        auto Long = Make_Params(Start, Flight);
        Long.Set_StepsPerSegment(3);
        Long.Set_MaxDuration(FCk_Time(150.5 * Dt));
        const auto LongCast = utils_jolt_query::Get_BallisticCast(Long);
        Assert_False(LongCast.Get_Hit().Get_HasHit(), "The long free flight hits nothing");
        Assert_Equals_Int(LongCast.Get_Path().Num(), 51, "150 steps in segments of 3 give 50 segments and 51 points");

        utils_jolt_body::Request_SetLinearVelocity(_Body, FCk_Request_JoltBody_SetLinearVelocity(_LaunchVelocity));
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        if (_Phase == 0)
        {
            _Elapsed += float(InDeltaT.Get_Seconds());

            if (utils_jolt_body::Get_IsBodyAdded(_Body))
            {
                DoPredictAndLaunch();
                _Phase = 1;
                _ElapsedSinceLaunch = 0.0;
                return;
            }

            if (_Elapsed > 5.0)
            { FinishFailure("The sphere body was never added to the simulation"); }

            return;
        }

        _ElapsedSinceLaunch += float(InDeltaT.Get_Seconds());

        const auto Location = utils_transform::Get_EntityCurrentLocation(_BodyTransform);
        const auto Error = Get_DistanceToPredictedPath(Location);
        ++_NumSamples;
        if (Error > _MaxError)
        {
            _MaxError = Error;
            _MaxErrorAtTime = _ElapsedSinceLaunch;
        }

        if (_ElapsedSinceLaunch < 1.2)
        { return; }

        Assert_True(_NumSamples >= 20, f"At least 20 samples over the flight (got {_NumSamples})");
        Assert_True(_MaxError <= 1.0,
            f"Every sampled location lies within 1 cm of the prediction (max {_MaxError} cm at {_MaxErrorAtTime} s)");
        Assert_True(Location.Distance(_Origin) > 300.0, f"The body actually flew (now at {Location})");

        utils_entity_lifetime::Request_DestroyEntity(_BodyEntity);
        FinishSuccess();
    }
}
