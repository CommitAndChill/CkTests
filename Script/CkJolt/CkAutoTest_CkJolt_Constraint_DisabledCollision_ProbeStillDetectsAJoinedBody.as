// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: A PROBE STILL DETECTS A BODY JOINED WITH
//                            CONNECTED-BODY COLLISION DISABLED
//============================================================================
//
// A probe body carries CkSpatialQuery's own collision group filter; a joined body
// carries the joint's. Jolt asks only the first non-null filter of a pair, so the
// joint's filter must hand a probe pair to the probe's filter and never silence
// it. Two overlapping dynamic boxes on a profile the probe layer pairs with
// (BlockAllDynamic), gravity off, joined with ConnectedBodiesCollide = Disable; a
// Notify detector probe is moved onto the first box once the joint reads added.
// Asserts: the joined box reports the contact with the detector probe, and the two
// boxes never touch each other.
//
// Placed at an isolated X/Y so it never touches other autotests' bodies.
//============================================================================

namespace ck_jolt_connected_bodies_probe_test
{
    asset Asset_ConnectedBodiesProbeTest_Tags of UCk_GameplayTags
    {
        GameplayTags.Add(n"CkTests.Jolt.ConnectedBodies.Detector");
    }
}

class UCk_AutoTest_CkJolt_Constraint_DisabledCollision_ProbeStillDetectsAJoinedBody : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;

    private FCk_Handle _A;
    private FCk_Handle _B;
    private FCk_Handle _DetectorEntity;
    private FCk_Handle_Transform _DetectorTransform;
    private FCk_Handle_JoltConstraint _Joint;

    private FVector _Origin = FVector(-50000.0, 108000.0, 300.0);
    private FVector _HalfExtents = FVector(10.0, 10.0, 10.0);

    private FCk_JoltConnectedBodies_Contacts _Contacts;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _A = Make_Watched(InHandle, _Origin);
        _B = Make_Watched(InHandle, _Origin + FVector(15.0, 0.0, 0.0));
        _Joint = ck_jolt_connected_bodies_test::Make_PointJoint(_A, _B, _Origin + FVector(7.5, 0.0, 0.0), ECk_EnableDisable::Disable);

        _DetectorEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _DetectorEntity.Request_OverrideToSelf();
        _DetectorTransform = utils_transform::Add(_DetectorEntity,
            FTransform(FRotator::ZeroRotator, _Origin + FVector(0.0, 0.0, 400.0)), ECk_Replication::DoesNotReplicate);

        auto DetectorParams = FCk_Probe_Spec(utils_gameplay_tag::ResolveGameplayTag(n"CkTests.Jolt.ConnectedBodies.Detector"));
        DetectorParams.Set_MotionType(ECk_MotionType::Kinematic);
        DetectorParams.Set_ResponsePolicy(ECk_ProbeResponse_Policy::Notify);
        utils_probe::Add_Sphere(_DetectorTransform, 15.0, DetectorParams, FCk_Probe_DebugInfo());

        WaitUntil(n"Check_JointAdded", n"OnJointAdded");
    }

    private FCk_Handle Make_Watched(FCk_Handle InOwner, FVector InCentre)
    {
        auto Body = ck_jolt_connected_bodies_test::Make_Box(InOwner, InCentre, _HalfExtents, ECk_MotionType::Dynamic, 0.0, n"BlockAllDynamic");
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

        utils_transform::Request_SetLocation(_DetectorTransform, _Origin - FVector(5.0, 0.0, 0.0));
        WaitUntil(n"Check_DetectorTouched", n"OnDetectorTouched", 120);
    }

    UFUNCTION()
    private void Check_DetectorTouched(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Contacts.Get_Count(_A, _DetectorEntity) > 0);
    }

    UFUNCTION()
    private void OnDetectorTouched(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        Log(f"[ConnectedBodies] probe: A-detector {_Contacts.Get_Count(_A, _DetectorEntity)}, A-B {_Contacts.Get_Count(_A, _B)}");
        Assert_Equals_Int(_Contacts.Get_Count(_A, _B), 0, "the joined pair never touches while the probe detects one of them");
        FinishSuccess();
    }
}
