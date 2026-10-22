// Query-only bodies keep their query/contact identity without entering the impulse solver.
// The third lane is a solid kinematic positive control. OverlapAllDynamic covers QueryOnly + Overlap,
// not the separate Physics-enabled + Overlap combination (no test-only global profile is installed).
class UCk_AutoTest_CkJolt_QueryOnlyContactsDoNotPushDynamicBodies : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 25.0f;

    private TArray<FCk_Handle_JoltBody> _Boxes;
    private TArray<FCk_Handle_Transform> _BoxTransforms;
    private TArray<FCk_Handle_Transform> _Walls;
    private TArray<FCk_Handle> _WallEntities;
    private TArray<FVector> _Baselines;
    private TArray<int32> _Contacts;
    private bool _Watching = false;
    private float64 _MaxQueryDisplacement = 0.0;
    private float64 _MaxSolidDisplacement = 0.0;
    private float64 _Nudge = 0.05;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Contacts.SetNum(3);
        TArray<FName> Profiles;
        Profiles.Add(n"IgnoreOnlyPawn");
        Profiles.Add(n"OverlapAllDynamic");
        Profiles.Add(n"PhysicsActor");
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const auto Centre = FVector(0.0, 87000.0 + Index * 400.0, 25.2);
            auto Floor = Make_Box(InHandle, Centre - FVector(0.0, 0.0, 35.2), FVector(150.0, 150.0, 10.0),
                ECk_MotionType::Static, n"BlockAll");
            auto Box = Make_Box(InHandle, Centre, FVector(25.0, 25.0, 25.0), ECk_MotionType::Dynamic, n"PhysicsActor");
            _Boxes.Add(Box.As_JoltBody());
            _BoxTransforms.Add(Box.As_Transform());
            auto Wall = Make_Box(InHandle, Centre + FVector(100.0, 0.0, 0.0), FVector(25.0, 25.0, 25.0),
                ECk_MotionType::Kinematic, Profiles[Index]);
            _Walls.Add(Wall.As_Transform());
            _WallEntities.Add(Wall);
            utils_jolt_body::BindTo_OnJoltBodyContactAdded(_Boxes[Index],
                FCk_Delegate_JoltBody_OnContact(this, n"OnContact"));
        }

        utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
        Add_Step_WaitUntil("all dynamic boxes rest on their supported floor", n"Check_Settled", 900, 0);
        Add_Step("move each real kinematic body into its dynamic box", n"Step_Overlap");
        Add_Step_WaitSeconds("observe the awake touching pairs", 1.0f);
        Add_Step("query-only pairs never push; solid pair pushes; query identities remain", n"Step_Assert");
        Run_Steps(InHandle);
    }

    private FCk_Handle Make_Box(FCk_Handle InOwner, FVector InCentre, FVector InHalfExtents,
                               ECk_MotionType InMotion, FName InProfile)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(InOwner);
        Entity.Request_OverrideToSelf();
        utils_transform::Add(Entity, FTransform(FRotator::ZeroRotator, InCentre), ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(InHalfExtents);
        auto Spec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(InMotion);
        Spec.Set_CollisionProfileName(InProfile);
        utils_jolt_body::Add(Entity, Spec);
        return Entity;
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        bool Ready = true;
        for (auto Body : _Boxes)
        { Ready = Ready && utils_jolt_body::Get_IsBodyAdded(Body) && utils_jolt_body::Get_SleepState(Body) == ECk_Jolt_SleepState::Asleep; }
        auto Result = OutResult;
        Result.Set(Ready);
    }

    UFUNCTION()
    private void Step_Overlap(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        for (int32 Index = 0; Index < 3; ++Index)
        {
            _Baselines.Add(utils_transform::Get_EntityCurrentLocation(_BoxTransforms[Index]));
            utils_transform::Request_SetLocation(_Walls[Index], _Baselines[Index] + FVector(30.0, 0.0, 0.0));
            utils_jolt_body::Request_SetSleepState(_Boxes[Index], FCk_Request_JoltBody_SetSleepState(ECk_Jolt_SleepState::Awake));
        }
        _Watching = true;
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished() || _Watching == false) { return; }
        _Nudge = -_Nudge;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            utils_transform::Request_SetLocation(_Walls[Index], _Baselines[Index] + FVector(30.0 + _Nudge, 0.0, 0.0));
            const auto Delta = utils_transform::Get_EntityCurrentLocation(_BoxTransforms[Index]) - _Baselines[Index];
            if (Index < 2) { _MaxQueryDisplacement = Math::Max(_MaxQueryDisplacement, Delta.Size2D()); }
            else { _MaxSolidDisplacement = Math::Max(_MaxSolidDisplacement, Delta.Size2D()); }
        }
    }

    UFUNCTION()
    private void OnContact(FCk_Handle_JoltBody InBody, FCk_JoltBody_Payload_OnContact InPayload)
    {
        if (IsFinished()) { return; }
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (InBody == _Boxes[Index] && InPayload.Get_OtherEntity() == _WallEntities[Index])
            {
                _Contacts[Index] += 1;
                Assert_True(InPayload.Get_OtherIsSensor() == ECk_EnableDisable::Disable,
                    "contact response must not change the ordinary body's sensor identity");
            }
        }
    }

    UFUNCTION()
    private void Step_Assert(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_MaxQueryDisplacement < 0.25, f"QueryOnly contacts must not push dynamic boxes (max {_MaxQueryDisplacement} cm)");
        Assert_True(_MaxSolidDisplacement > 5.0, "a solid kinematic body still pushes its dynamic box");
        auto Filter = FCk_Jolt_QueryFilter();
        Filter.Set_Channel(ECollisionChannel::ECC_Visibility);
        Filter.Set_MinResponse(ECk_Jolt_PairInteraction::Overlap);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Assert_True(_Contacts[Index] > 0, f"lane {Index} must report the actual wall/body contact");
            const auto Centre = utils_transform::Get_EntityCurrentLocation(_Walls[Index]);
            const auto Hit = utils_jolt_query::Get_RayCast(Centre - FVector(0.0, 100.0, 0.0), Centre + FVector(0.0, 100.0, 0.0), Filter);
            Assert_True(Hit.Get_HasHit() && Hit.Get_Entity() == _WallEntities[Index], f"lane {Index} remains queryable with its original entity identity");
        }
    }
}
