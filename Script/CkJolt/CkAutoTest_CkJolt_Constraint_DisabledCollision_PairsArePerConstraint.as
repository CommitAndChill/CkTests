// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: CONNECTED-BODY COLLISION IS DISABLED PER
//                            CONSTRAINT, NOT PER CHAIN
//============================================================================
//
// A chain 0-1-2 of dynamic boxes, gravity off, each neighbour overlapping the
// middle box by 1 cm at its pivot, both point joints with
// ConnectedBodiesCollide = Disable. One impulse at each end box's outer end, the
// same way, folds both ends about their pivots until they meet over the middle
// box. Asserts: 0-1 and 1-2 never touch, and 0 and 2 (joined to nothing directly)
// collide with each other.
//
// Placed at an isolated X/Y so it never touches other autotests' bodies.
//============================================================================

class UCk_AutoTest_CkJolt_Constraint_DisabledCollision_PairsArePerConstraint : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;

    private FCk_Handle _SelfHandle;
    private FCk_Handle _Body0;
    private FCk_Handle _Body1;
    private FCk_Handle _Body2;
    private FCk_Handle_JoltConstraint _Joint01;
    private FCk_Handle_JoltConstraint _Joint12;

    private FVector _Origin = FVector(-50000.0, 105000.0, 300.0);
    private FVector _HalfExtents = FVector(10.0, 10.0, 10.0);

    private FCk_JoltConnectedBodies_Contacts _Contacts;
    private float _Elapsed = 0.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;

        _Body0 = Make_Watched(_Origin - FVector(19.0, 0.0, 0.0));
        _Body1 = Make_Watched(_Origin);
        _Body2 = Make_Watched(_Origin + FVector(19.0, 0.0, 0.0));

        _Joint01 = ck_jolt_connected_bodies_test::Make_PointJoint(_Body0, _Body1, _Origin - FVector(9.5, 0.0, 0.0), ECk_EnableDisable::Disable);
        _Joint12 = ck_jolt_connected_bodies_test::Make_PointJoint(_Body1, _Body2, _Origin + FVector(9.5, 0.0, 0.0), ECk_EnableDisable::Disable);

        WaitUntil(n"Check_JointsAdded", n"OnJointsAdded");
    }

    private FCk_Handle Make_Watched(FVector InCentre)
    {
        auto Body = ck_jolt_connected_bodies_test::Make_Box(_SelfHandle, InCentre, _HalfExtents, ECk_MotionType::Dynamic, 0.0, n"PhysicsActor");
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
    private void Check_JointsAdded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_jolt_constraint::Get_IsConstraintAdded(_Joint01) && utils_jolt_constraint::Get_IsConstraintAdded(_Joint12));
    }

    UFUNCTION()
    private void OnJointsAdded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        utils_jolt_body::Request_AddImpulseAtLocation(_Body0.As_JoltBody(),
            FCk_Request_JoltBody_AddImpulseAtLocation(FVector(0.0, 100.0, 0.0), _Origin - FVector(29.0, 0.0, 0.0)));
        utils_jolt_body::Request_AddImpulseAtLocation(_Body2.As_JoltBody(),
            FCk_Request_JoltBody_AddImpulseAtLocation(FVector(0.0, 100.0, 0.0), _Origin + FVector(29.0, 0.0, 0.0)));

        utils_timer::Create_Tick(_SelfHandle, FCk_Delegate_Timer(this, n"OnTick"));
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        _Elapsed += float(InDeltaT.Get_Seconds());

        const auto Contacts01 = _Contacts.Get_Count(_Body0, _Body1);
        const auto Contacts12 = _Contacts.Get_Count(_Body1, _Body2);
        const auto Contacts02 = _Contacts.Get_Count(_Body0, _Body2);

        if (Contacts01 > 0 || Contacts12 > 0)
        {
            FinishFailure(f"a joined pair touched (0-1 {Contacts01}, 1-2 {Contacts12}) at {_Elapsed} s");
            return;
        }

        // At least a second of folding: the joined pairs overlap the whole time and must still read no contact.
        if (Contacts02 > 0 && _Elapsed > 1.0)
        {
            Log(f"[ConnectedBodies] chain: 0-2 contacts {Contacts02}, 0-1 {Contacts01}, 1-2 {Contacts12} at {_Elapsed} s");
            FinishSuccess();
            return;
        }

        if (_Elapsed > 8.0)
        { FinishFailure(f"the chain's ends never met (0-2 contacts {Contacts02}) in {_Elapsed} s"); }
    }
}
