// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: DESTROYING A JOINT WITH CONNECTED-BODY COLLISION
//                            DISABLED LETS ITS TWO BODIES COLLIDE AGAIN
//============================================================================
//
// Two dynamic boxes overlapping by 5 cm, gravity off, joined by a point joint
// with ConnectedBodiesCollide = Disable. For 30 frames after the joint reads
// added they report no contact; then the constraint entity is destroyed, and the
// overlap must report a contact between them within 30 frames (the bodies left
// the joint's filter and were woken).
//
// Placed at an isolated X/Y so it never touches other autotests' bodies.
//============================================================================

class UCk_AutoTest_CkJolt_Constraint_DisabledCollision_DestroyingTheJointRestoresCollision : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;

    private FCk_Handle _A;
    private FCk_Handle _B;
    private FCk_Handle_JoltConstraint _Joint;

    private FVector _Origin = FVector(-50000.0, 107000.0, 300.0);
    private FVector _HalfExtents = FVector(10.0, 10.0, 10.0);

    private FCk_JoltConnectedBodies_Contacts _Contacts;
    private int32 _ContactsWhileJoined = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _A = Make_Watched(InHandle, _Origin);
        _B = Make_Watched(InHandle, _Origin + FVector(15.0, 0.0, 0.0));
        _Joint = ck_jolt_connected_bodies_test::Make_PointJoint(_A, _B, _Origin + FVector(7.5, 0.0, 0.0), ECk_EnableDisable::Disable);

        WaitUntil(n"Check_JointAdded", n"OnJointAdded");
    }

    private FCk_Handle Make_Watched(FCk_Handle InOwner, FVector InCentre)
    {
        auto Body = ck_jolt_connected_bodies_test::Make_Box(InOwner, InCentre, _HalfExtents, ECk_MotionType::Dynamic, 0.0, n"PhysicsActor");
        utils_jolt_body::BindTo_OnJoltBodyContactAdded(Body.As_JoltBody(), FCk_Delegate_JoltBody_OnContact(this, n"OnContact"));
        return Body;
    }

    UFUNCTION()
    private void OnContact(FCk_Handle_JoltBody InBody, FCk_JoltBody_Payload_OnContact InPayload)
    {
        FCk_Handle Self = InBody;
        _Contacts.Record(Self, InPayload.Get_OtherEntity());
    }

    UFUNCTION()
    private void Check_JointAdded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_jolt_constraint::Get_IsConstraintAdded(_Joint));
    }

    UFUNCTION()
    private void OnJointAdded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        // A negative: the positive half (the contact after the destroy) is what proves the bodies were watched.
        WaitFrames(30, n"OnJoinedWindowEnded");
    }

    UFUNCTION()
    private void OnJoinedWindowEnded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        _ContactsWhileJoined = _Contacts.Get_Count(_A, _B);
        Assert_Equals_Int(_ContactsWhileJoined, 0, "the joined pair never touches while the joint lives");

        FCk_Handle JointEntity = _Joint;
        utils_entity_lifetime::Request_DestroyEntity(JointEntity);

        WaitUntil(n"Check_TouchedAfterDestroy", n"OnTouchedAfterDestroy", 30);
    }

    UFUNCTION()
    private void Check_TouchedAfterDestroy(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Contacts.Get_Count(_A, _B) > _ContactsWhileJoined);
    }

    UFUNCTION()
    private void OnTouchedAfterDestroy(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        Assert_True(ck::Is_NOT_Valid(_Joint), "the destroyed joint's handle is gone");
        Log(f"[ConnectedBodies] destroy: contacts after the joint died {_Contacts.Get_Count(_A, _B)}");
        FinishSuccess();
    }
}
