// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: A JOINT WITH CONNECTED-BODY COLLISION DISABLED
//                            KEEPS ITS TWO BODIES FROM EVER TOUCHING
//============================================================================
//
// Two pairs of overlapping dynamic boxes (5 cm of overlap at the pivot), each
// pair pinned by a point joint at the middle of the overlap, gravity off, and
// set spinning by one impulse at the far end of the second box.
//   - ConnectedBodiesCollide = Disable: zero contacts between the two boxes over
//     60 frames from the frame the joint reads added, and the joint gap (the
//     pivot as carried by each box) never above 0.1 cm.
//   - The default (Enable), same setup: the overlap reports contacts.
//
// Placed at an isolated X/Y so it never touches other autotests' bodies.
//============================================================================

class UCk_AutoTest_CkJolt_Constraint_DisabledCollision_JoinedPairNeverTouches : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;

    private FCk_Handle _SelfHandle;
    private FCk_Handle _DisabledA;
    private FCk_Handle _DisabledB;
    private FCk_Handle _EnabledA;
    private FCk_Handle _EnabledB;
    private FCk_Handle_JoltConstraint _DisabledJoint;
    private FCk_Handle_JoltConstraint _EnabledJoint;

    private FVector _DisabledOrigin = FVector(-50000.0, 104000.0, 300.0);
    private FVector _EnabledOrigin = FVector(-50000.0, 104400.0, 300.0);
    private FVector _HalfExtents = FVector(10.0, 10.0, 10.0);

    private FCk_JoltConnectedBodies_Contacts _Contacts;
    private FVector _LocalPivotA;
    private FVector _LocalPivotB;
    private int32 _FramesWatched = 0;
    private float64 _WidestGap = 0.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;
        _DisabledA = Make_Pair(InHandle, _DisabledOrigin, ECk_EnableDisable::Disable, _DisabledB, _DisabledJoint);
        _EnabledA = Make_Pair(InHandle, _EnabledOrigin, ECk_EnableDisable::Enable, _EnabledB, _EnabledJoint);

        WaitUntil(n"Check_JointsAdded", n"OnJointsAdded");
    }

    private FCk_Handle Make_Pair(FCk_Handle InOwner, FVector InOrigin, ECk_EnableDisable InCollide,
                                 FCk_Handle& OutB, FCk_Handle_JoltConstraint& OutJoint)
    {
        auto A = ck_jolt_connected_bodies_test::Make_Box(InOwner, InOrigin, _HalfExtents, ECk_MotionType::Dynamic, 0.0, n"PhysicsActor");
        OutB = ck_jolt_connected_bodies_test::Make_Box(InOwner, InOrigin + FVector(15.0, 0.0, 0.0), _HalfExtents,
            ECk_MotionType::Dynamic, 0.0, n"PhysicsActor");
        OutJoint = ck_jolt_connected_bodies_test::Make_PointJoint(A, OutB, InOrigin + FVector(7.5, 0.0, 0.0), InCollide);

        utils_jolt_body::BindTo_OnJoltBodyContactAdded(A.As_JoltBody(), FCk_Delegate_JoltBody_OnContact(this, n"OnContact"));
        utils_jolt_body::BindTo_OnJoltBodyContactAdded(OutB.As_JoltBody(), FCk_Delegate_JoltBody_OnContact(this, n"OnContact"));
        return A;
    }

    UFUNCTION()
    private void OnContact(FCk_Handle_JoltBody InBody, FCk_JoltBody_Payload_OnContact InPayload)
    {
        FCk_Handle Self = InBody;
        _Contacts.Record(Self, InPayload.Get_OtherEntity());
    }

    UFUNCTION()
    private void Check_JointsAdded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_jolt_constraint::Get_IsConstraintAdded(_DisabledJoint) && utils_jolt_constraint::Get_IsConstraintAdded(_EnabledJoint));
    }

    UFUNCTION()
    private void OnJointsAdded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        const auto Pivot = _DisabledOrigin + FVector(7.5, 0.0, 0.0);
        _LocalPivotA = ck_jolt_connected_bodies_test::Get_LocalPivot(_DisabledA, Pivot);
        _LocalPivotB = ck_jolt_connected_bodies_test::Get_LocalPivot(_DisabledB, Pivot);

        Spin(_DisabledB);
        Spin(_EnabledB);

        utils_timer::Create_Tick(_SelfHandle, FCk_Delegate_Timer(this, n"OnTick"));
    }

    // One impulse at the box's far end: the pair spins about its pivot.
    private void Spin(FCk_Handle InBody)
    {
        const auto FarEnd = utils_transform::Get_EntityCurrentLocation(InBody.As_Transform()) + FVector(10.0, 0.0, 0.0);
        utils_jolt_body::Request_AddImpulseAtLocation(InBody.As_JoltBody(),
            FCk_Request_JoltBody_AddImpulseAtLocation(FVector(0.0, 50.0, 0.0), FarEnd));
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        _WidestGap = Math::Max(_WidestGap,
            ck_jolt_connected_bodies_test::Get_JointGap(_DisabledA, _LocalPivotA, _DisabledB, _LocalPivotB));

        _FramesWatched++;
        if (_FramesWatched < 60)
        { return; }

        const auto DisabledContacts = _Contacts.Get_Count(_DisabledA, _DisabledB);
        const auto EnabledContacts = _Contacts.Get_Count(_EnabledA, _EnabledB);
        Log(f"[ConnectedBodies] disabled contacts {DisabledContacts}, enabled contacts {EnabledContacts}, widest gap {_WidestGap} cm over {_FramesWatched} frames");

        Assert_Equals_Int(DisabledContacts, 0, "the two bodies of a joint with ConnectedBodiesCollide = Disable never touch");
        Assert_True(_WidestGap <= 0.1, f"the disabled pair's joint gap stays at most 0.1 cm (widest {_WidestGap})");
        Assert_True(EnabledContacts > 0, "the same overlapping pair under the default (Enable) reports contacts");
        FinishSuccess();
    }
}
