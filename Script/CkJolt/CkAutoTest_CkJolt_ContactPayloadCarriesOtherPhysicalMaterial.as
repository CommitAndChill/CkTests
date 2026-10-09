// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: CONTACT PAYLOAD CARRIES THE OTHER BODY'S PHYS MAT
//============================================================================
//
// Two drops side by side, each onto a floor with its own transient phys mat:
//
//   1. A Static JoltBody floor with SurfaceSource PhysicalMaterial -> the box's
//      ContactAdded payload names that phys mat (the dynamic-body path).
//   2. A baked static-world floor (an AStaticMeshActor whose component carries
//      a PhysMaterialOverride, baked with Request_BakeActor) -> the payload names
//      the override (the JoltStaticActor per-body path).
//
// Distinct phys mats per floor, so neither can pass by reading the other's.
// Placed at Y 54000 so it never touches other autotests' physics bodies.
//============================================================================

class UCk_AutoTest_CkJolt_ContactPayloadCarriesOtherPhysicalMaterial : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private FCk_Handle _SelfHandle;
    private UPhysicalMaterial _JoltBodyFloorMaterial;
    private UPhysicalMaterial _BakedFloorMaterial;
    private AStaticMeshActor _BakedFloor;

    private FVector _JoltBodyFloorCenter = FVector(0.0, 54000.0, 0.0);
    private FVector _BakedFloorCenter = FVector(2000.0, 54000.0, 0.0);

    private UPhysicalMaterial _JoltBodyBoxSaw;
    private UPhysicalMaterial _BakedBoxSaw;
    private bool _JoltBodyBoxContacted = false;
    private bool _BakedBoxContacted = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;

        _JoltBodyFloorMaterial = Cast<UPhysicalMaterial>(NewObject(GetTransientPackage(), UPhysicalMaterial));
        _BakedFloorMaterial = Cast<UPhysicalMaterial>(NewObject(GetTransientPackage(), UPhysicalMaterial));

        // ---- 1. Static JoltBody floor, surface from its phys mat --------------------------------
        auto FloorEntity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        FloorEntity.Request_OverrideToSelf();
        utils_transform::Add(FloorEntity, FTransform(FRotator::ZeroRotator, _JoltBodyFloorCenter),
            ECk_Replication::DoesNotReplicate);

        auto FloorShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        FloorShape.Set_HalfExtents(FVector(500.0, 500.0, 25.0));
        auto FloorParams = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        FloorParams.Set_ShapeDimensions(FloorShape);
        FloorParams.Set_MotionType(ECk_MotionType::Static);
        FloorParams.Set_SurfaceSource(ECk_JoltBody_SurfaceSource::PhysicalMaterial);
        FloorParams.Set_PhysicalMaterial(_JoltBodyFloorMaterial);
        utils_jolt_body::Add(FloorEntity, FloorParams);

        // ---- 2. Baked static-world floor, surface from the component's override ----------------
        auto Cube = Cast<UStaticMesh>(LoadObject(UStaticMesh, "/Engine/BasicShapes/Cube.Cube"));
        if (!IsValid(Cube))
        {
            FinishFailure("Failed to load /Engine/BasicShapes/Cube.Cube");
            return;
        }

        _BakedFloor = Cast<AStaticMeshActor>(SpawnActor(AStaticMeshActor, _BakedFloorCenter));
        _BakedFloor.StaticMeshComponent.SetMobility(EComponentMobility::Movable);
        _BakedFloor.StaticMeshComponent.SetStaticMesh(Cube);
        _BakedFloor.StaticMeshComponent.SetCollisionProfileName(n"BlockAll");
        _BakedFloor.StaticMeshComponent.SetPhysMaterialOverride(_BakedFloorMaterial);
        _BakedFloor.SetActorScale3D(FVector(10.0, 10.0, 0.5));
        Assert_Equals_Int(utils_jolt_static_world::Request_BakeActor(_BakedFloor), 1, "the baked floor bakes one body");

        // ---- Dynamic boxes, one above each floor ------------------------------------------------
        auto JoltBodyBox = Do_DropBox(_JoltBodyFloorCenter + FVector(0.0, 0.0, 250.0));
        utils_jolt_body::BindTo_OnJoltBodyContactAdded(JoltBodyBox,
            FCk_Delegate_JoltBody_OnContact(this, n"OnJoltBodyBoxContact"));

        auto BakedBox = Do_DropBox(_BakedFloorCenter + FVector(0.0, 0.0, 250.0));
        utils_jolt_body::BindTo_OnJoltBodyContactAdded(BakedBox,
            FCk_Delegate_JoltBody_OnContact(this, n"OnBakedBoxContact"));

        WaitUntil(n"Check_BothContacted", n"OnBothContacted");
    }

    private FCk_Handle_JoltBody Do_DropBox(FVector InStart)
    {
        auto BoxEntity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        BoxEntity.Request_OverrideToSelf();
        utils_transform::Add(BoxEntity, FTransform(FRotator::ZeroRotator, InStart), ECk_Replication::DoesNotReplicate);

        auto BoxShape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        BoxShape.Set_HalfExtents(FVector(50.0, 50.0, 50.0));
        auto BoxParams = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        BoxParams.Set_ShapeDimensions(BoxShape);
        BoxParams.Set_MotionType(ECk_MotionType::Dynamic);
        return utils_jolt_body::Add(BoxEntity, BoxParams);
    }

    // The FIRST contact of each box is the floor under it: nothing else is within reach of the drop.
    UFUNCTION()
    private void OnJoltBodyBoxContact(FCk_Handle_JoltBody InHandle, FCk_JoltBody_Payload_OnContact InPayload)
    {
        if (IsFinished() || _JoltBodyBoxContacted) { return; }

        _JoltBodyBoxContacted = true;
        _JoltBodyBoxSaw = InPayload.Get_OtherPhysicalMaterial().Get();
    }

    UFUNCTION()
    private void OnBakedBoxContact(FCk_Handle_JoltBody InHandle, FCk_JoltBody_Payload_OnContact InPayload)
    {
        if (IsFinished() || _BakedBoxContacted) { return; }

        _BakedBoxContacted = true;
        _BakedBoxSaw = InPayload.Get_OtherPhysicalMaterial().Get();
    }

    UFUNCTION()
    private void Check_BothContacted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_JoltBodyBoxContacted && _BakedBoxContacted);
    }

    UFUNCTION()
    private void OnBothContacted(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        Assert_True(_JoltBodyBoxSaw == _JoltBodyFloorMaterial,
            "a box landing on a JoltBody floor (SurfaceSource PhysicalMaterial) reads that floor's phys mat");
        Assert_True(_BakedBoxSaw == _BakedFloorMaterial,
            "a box landing on a baked static floor reads the component's PhysMaterialOverride");

        Do_Cleanup();
        FinishSuccess();
    }

    private void Do_Cleanup()
    {
        if (IsValid(_BakedFloor))
        {
            utils_jolt_static_world::Request_RemoveActor(_BakedFloor);
            _BakedFloor.DestroyActor();
        }
    }
}
