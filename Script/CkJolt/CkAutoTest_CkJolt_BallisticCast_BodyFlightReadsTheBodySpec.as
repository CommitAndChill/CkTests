// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: GET_BALLISTICFLIGHT READS THE BODY'S SPEC
//============================================================================
//
// A body built with damping 0.37, gravity factor 0.5 and max linear velocity
// 1234 reports exactly those three values, on the frame it is composed and
// before it is added to the simulation (none of them is a default).
//
// Isolated origin far from every other autotest.
//============================================================================

class UCk_AutoTest_CkJolt_BallisticCast_BodyFlightReadsTheBodySpec : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private FVector _Origin = FVector(60000.0, -140000.0, 30000.0);

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto BodyEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        BodyEntity.Request_OverrideToSelf();
        utils_transform::Add(BodyEntity, FTransform(FRotator::ZeroRotator, _Origin), ECk_Replication::DoesNotReplicate);

        auto SphereShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Sphere);
        SphereShape.Set_Radius(10.0);
        auto SphereParams = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        SphereParams.Set_ShapeDimensions(SphereShape);
        SphereParams.Set_MotionType(ECk_MotionType::Dynamic);
        SphereParams.Set_GravityFactor(0.5);
        SphereParams.Set_LinearDamping(0.37);
        SphereParams.Set_MaxLinearVelocity(1234.0);
        SphereParams.Set_InitialSleepState(ECk_Jolt_SleepState::Asleep);
        auto Body = utils_jolt_body::Add(BodyEntity, SphereParams);

        Assert_False(utils_jolt_body::Get_IsBodyAdded(Body), "The body is read on the frame it is composed, before setup");

        const auto Flight = utils_jolt_body::Get_BallisticFlight(Body);
        Assert_Equals_Float(Flight.Get_LinearDamping(), 0.37, 0.00001, "Linear damping comes from the spec");
        Assert_Equals_Float(Flight.Get_GravityFactor(), 0.5, 0.00001, "Gravity factor comes from the spec");
        Assert_Equals_Float(Flight.Get_MaxLinearVelocity(), 1234.0, 0.001, "Max linear velocity comes from the spec");

        utils_entity_lifetime::Request_DestroyEntity(BodyEntity);
        FinishSuccess();
    }
}
