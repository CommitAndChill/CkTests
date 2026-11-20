// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: BODIES JOINED WITH CONNECTED-BODY COLLISION
//                            DISABLED STILL COLLIDE WITH EVERYTHING ELSE
//============================================================================
//
// Two overlapping joined pairs (ConnectedBodiesCollide = Disable) fall under
// gravity: pair P onto a static floor, pair Q onto an unrelated dynamic box
// resting on that floor. Asserts: P reports contact with the floor and comes to
// rest on it (not through it), Q reports contact with the unrelated box, and
// neither pair ever touches itself.
//
// Placed at an isolated X/Y so it never touches other autotests' bodies.
//============================================================================

class UCk_AutoTest_CkJolt_Constraint_DisabledCollision_JoinedBodiesStillHitTheWorld : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;

    private FCk_Handle _SelfHandle;
    private FCk_Handle _Floor;
    private FCk_Handle _Unrelated;
    private FCk_Handle _PA;
    private FCk_Handle _PB;
    private FCk_Handle _QA;
    private FCk_Handle _QB;

    private FVector _Origin = FVector(-50000.0, 106000.0, 0.0);
    private FVector _HalfExtents = FVector(10.0, 10.0, 10.0);
    private float64 _FloorTop = 10.0;

    private FCk_JoltConnectedBodies_Contacts _Contacts;
    private float _Elapsed = 0.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;

        _Floor = ck_jolt_connected_bodies_test::Make_Box(InHandle, _Origin, FVector(400.0, 400.0, 10.0), ECk_MotionType::Static, 1.0, n"BlockAll");

        const auto POrigin = _Origin + FVector(-100.0, 0.0, 80.0);
        _PA = Make_Watched(POrigin);
        _PB = Make_Watched(POrigin + FVector(15.0, 0.0, 0.0));
        ck_jolt_connected_bodies_test::Make_PointJoint(_PA, _PB, POrigin + FVector(7.5, 0.0, 0.0), ECk_EnableDisable::Disable);

        _Unrelated = Make_Watched(_Origin + FVector(100.0, 0.0, _FloorTop + 10.0));
        const auto QOrigin = _Origin + FVector(95.0, 0.0, 90.0);
        _QA = Make_Watched(QOrigin);
        _QB = Make_Watched(QOrigin + FVector(15.0, 0.0, 0.0));
        ck_jolt_connected_bodies_test::Make_PointJoint(_QA, _QB, QOrigin + FVector(7.5, 0.0, 0.0), ECk_EnableDisable::Disable);

        utils_timer::Create_Tick(_SelfHandle, FCk_Delegate_Timer(this, n"OnTick"));
    }

    private FCk_Handle Make_Watched(FVector InCentre)
    {
        auto Body = ck_jolt_connected_bodies_test::Make_Box(_SelfHandle, InCentre, _HalfExtents, ECk_MotionType::Dynamic, 1.0, n"PhysicsActor");
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
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        _Elapsed += float(InDeltaT.Get_Seconds());

        const auto SelfP = _Contacts.Get_Count(_PA, _PB);
        const auto SelfQ = _Contacts.Get_Count(_QA, _QB);
        if (SelfP > 0 || SelfQ > 0)
        {
            FinishFailure(f"a joined pair touched itself (P {SelfP}, Q {SelfQ}) at {_Elapsed} s");
            return;
        }

        const auto PFloor = _Contacts.Get_Count(_PA, _Floor) + _Contacts.Get_Count(_PB, _Floor);
        const auto QUnrelated = _Contacts.Get_Count(_QA, _Unrelated) + _Contacts.Get_Count(_QB, _Unrelated);
        if (_Elapsed < 3.0)
        { return; }

        const auto LowestP = Math::Min(utils_transform::Get_EntityCurrentLocation(_PA.As_Transform()).Z,
            utils_transform::Get_EntityCurrentLocation(_PB.As_Transform()).Z) - _Origin.Z;
        Log(f"[ConnectedBodies] world: P-floor {PFloor}, Q-unrelated {QUnrelated}, P lowest centre {LowestP} above the floor's centre");

        Assert_True(PFloor > 0, "a joined body still collides with the floor");
        Assert_True(LowestP > _FloorTop, f"the joined pair rests on the floor, not in it (lowest centre {LowestP}, floor top {_FloorTop})");
        Assert_True(QUnrelated > 0, "a joined body still collides with an unrelated body");
        FinishSuccess();
    }
}
