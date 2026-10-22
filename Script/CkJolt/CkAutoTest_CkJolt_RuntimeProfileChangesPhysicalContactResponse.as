// A cached supporting contact must stop supporting when its live profile becomes QueryOnly,
// without adding a persisted-event subscriber. The isolated gate checks the logged global counter delta.
// The reverse transition repositions the same box; it proves restored support, not a continuous cached manifold.
class UCk_AutoTest_CkJolt_RuntimeProfileChangesPhysicalContactResponse : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 30.0f;

    private const FVector k_Centre = FVector(0.0, 89000.0, 0.0);
    private FCk_Handle_JoltBody _Box;
    private FCk_Handle_Transform _BoxTransform;
    private FCk_Handle_JoltBody _Platform;
    private FCk_Handle_Transform _PlatformTransform;
    private FCk_Handle _PlatformEntity;
    private bool _ProfileApplied = false;
    private bool _ContactWithPlatform = false;
    private int64 _PersistedStart = 0;
    private FName _RequestedProfile;
    private float64 _Nudge = 0.01;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Make_Box(InHandle, k_Centre - FVector(0.0, 0.0, 110.0), FVector(150.0, 150.0, 10.0), ECk_MotionType::Static);
        _PlatformEntity = Make_Box(InHandle, k_Centre, FVector(75.0, 75.0, 25.0), ECk_MotionType::Kinematic);
        _Platform = _PlatformEntity.As_JoltBody();
        _PlatformTransform = _PlatformEntity.As_Transform();
        auto Box = Make_Box(InHandle, k_Centre + FVector(0.0, 0.0, 50.2), FVector(25.0, 25.0, 25.0), ECk_MotionType::Dynamic);
        _Box = Box.As_JoltBody();
        _BoxTransform = Box.As_Transform();
        utils_jolt_body::BindTo_OnJoltBodyContactAdded(_Box, FCk_Delegate_JoltBody_OnContact(this, n"OnContact"));
        utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnNudge"));

        Add_Step_WaitUntil("solid platform supports the resting box", n"Check_Supported", 900, 0);
        Add_Step("change the touching platform to QueryOnly", n"Step_QueryOnly");
        Add_Step_WaitUntil("profile request completed", n"Check_ProfileApplied");
        Add_Step("wake the dynamic body through the public request", n"Step_Wake");
        Add_Step_WaitSeconds("the cached pair is stepped without persisted-event interest", 1.0f);
        Add_Step("QueryOnly no longer supports; restore the same solid platform and box", n"Step_AssertFallAndRestore");
        Add_Step_WaitUntil("solid profile completed", n"Check_ProfileApplied");
        Add_Step_WaitUntil("the same box rests on the restored platform", n"Check_Supported", 900, 0);
        Add_Step("change the touching platform to QueryOnly overlap", n"Step_Overlap");
        Add_Step_WaitUntil("overlap profile completed", n"Check_ProfileApplied");
        Add_Step("apply the identical public awake policy", n"Step_Wake");
        Add_Step_WaitSeconds("observe the cached overlap pair", 1.0f);
        Add_Step("overlap no longer supports; restore again", n"Step_AssertFallAndRestore");
        Add_Step_WaitUntil("final solid profile completed", n"Check_ProfileApplied");
        Add_Step_WaitUntil("solid support restored without replacing either body", n"Check_Supported", 900, 0);
        Run_Steps(InHandle);
    }

    private FCk_Handle Make_Box(FCk_Handle InOwner, FVector InCentre, FVector InHalfExtents, ECk_MotionType InMotion)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(InOwner);
        Entity.Request_OverrideToSelf();
        utils_transform::Add(Entity, FTransform(FRotator::ZeroRotator, InCentre), ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(InHalfExtents);
        auto Spec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(InMotion);
        Spec.Set_CollisionProfileName(n"PhysicsActor");
        utils_jolt_body::Add(Entity, Spec);
        return Entity;
    }

    UFUNCTION()
    private void Check_Supported(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        const auto Location = utils_transform::Get_EntityCurrentLocation(_BoxTransform);
        auto Result = OutResult;
        Result.Set(utils_jolt_body::Get_IsBodyAdded(_Box) && utils_jolt_body::Get_IsBodyAdded(_Platform)
            && _ContactWithPlatform && Location.Z > 49.0 && Location.Z < 51.0
            && utils_jolt_body::Get_LinearVelocity(_Box).Size() < 5.0);
    }

    UFUNCTION()
    private void OnContact(FCk_Handle_JoltBody InBody, FCk_JoltBody_Payload_OnContact InPayload)
    {
        if (InPayload.Get_OtherEntity() == _PlatformEntity) { _ContactWithPlatform = true; }
    }

    private void Request_Profile(FName InProfile)
    {
        _ProfileApplied = false;
        _RequestedProfile = InProfile;
        utils_jolt_body::Request_SetCollisionProfile(_Platform, FCk_Request_JoltBody_SetCollisionProfile(InProfile),
            FCk_Delegate_Request_OnCompleted(this, n"OnProfileApplied"));
    }

    UFUNCTION()
    private void OnProfileApplied(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        Assert_True(InResult == ECk_Request_OperationResult::Succeeded, "the live collision-profile request must succeed");
        _ProfileApplied = InResult == ECk_Request_OperationResult::Succeeded;
    }

    UFUNCTION()
    private void Check_ProfileApplied(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    { auto Result = OutResult; Result.Set(_ProfileApplied); }

    UFUNCTION()
    private void Step_QueryOnly(FCk_Handle InHandle, FInstancedStruct InPayload)
    { Request_Profile(n"IgnoreOnlyPawn"); }

    UFUNCTION()
    private void Step_Overlap(FCk_Handle InHandle, FInstancedStruct InPayload)
    { Request_Profile(n"OverlapAllDynamic"); }

    UFUNCTION()
    private void Step_Wake(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _PersistedStart = utils_jolt::Get_Debug_NumPersistedContactEventsTotal(_Box);
        utils_jolt_body::Request_SetSleepState(_Box, FCk_Request_JoltBody_SetSleepState(ECk_Jolt_SleepState::Awake));
    }

    UFUNCTION()
    private void OnNudge(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        _Nudge = -_Nudge;
        utils_transform::Request_SetLocation(_PlatformTransform, k_Centre + FVector(_Nudge, 0.0, 0.0));
    }

    UFUNCTION()
    private void Step_AssertFallAndRestore(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Location = utils_transform::Get_EntityCurrentLocation(_BoxTransform);
        const auto PersistedDelta = utils_jolt::Get_Debug_NumPersistedContactEventsTotal(_Box) - _PersistedStart;
        Log(f"[JoltContactResponse] profile={_RequestedProfile} Z={Location.Z} expectedZ<-40 persisted start={_PersistedStart} end={utils_jolt::Get_Debug_NumPersistedContactEventsTotal(_Box)} delta={PersistedDelta}");
        Assert_True(Location.Z < -40.0, f"{_RequestedProfile} cached contact must not support the dynamic box (Z={Location.Z})");
        auto Filter = FCk_Jolt_QueryFilter();
        Filter.Set_Channel(ECollisionChannel::ECC_Visibility);
        Filter.Set_MinResponse(ECk_Jolt_PairInteraction::Overlap);
        const auto Hit = utils_jolt_query::Get_RayCast(k_Centre + FVector(60.0, -100.0, 0.0), k_Centre + FVector(60.0, 100.0, 0.0), Filter);
        Assert_True(Hit.Get_HasHit() && Hit.Get_Entity() == _PlatformEntity, "the nonphysical platform retains its query identity");
        Assert_True(utils_jolt_body::Get_MotionType(_Platform) == ECk_MotionType::Kinematic, "profile changes must not change motion type");
        Request_Profile(n"PhysicsActor");
        _ContactWithPlatform = false;
        // A new drop onto the same body proves restored support; the prior forward transition kept its cached contact.
        auto Reset = FCk_Request_JoltBody_Teleport(k_Centre + FVector(0.0, 0.0, 50.2), FRotator::ZeroRotator);
        Reset.Set_VelocityPolicy(ECk_Jolt_TeleportVelocityPolicy::ResetVelocity);
        utils_jolt_body::Request_Teleport(_Box, Reset);
        utils_jolt_body::Request_SetSleepState(_Box, FCk_Request_JoltBody_SetSleepState(ECk_Jolt_SleepState::Awake));
    }
}
