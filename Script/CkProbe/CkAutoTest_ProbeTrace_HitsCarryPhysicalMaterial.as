// Language=angelscript

//============================================================================
// CK PROBE TRACE - AUTOMATION TEST: HITS CARRY THE SURFACE'S PHYS MAT
//============================================================================
//
// Along one line, three surfaces, each with its own transient phys mat:
//   probe (SurfaceInfo Direct) -> baked cube wall (PhysMaterialOverride) -> Static JoltBody block
//   (SurfaceSource PhysicalMaterial).
//
// Under the Reported policy both a line trace and a sphere shape trace return all three,
// and each result's _PhysicalMaterial is that surface's own: the Probe path, the
// JoltStaticActor per-body path and the JoltBody spec path.
//============================================================================

namespace ck_probetrace_physmat_test
{
    asset Asset_ProbeTracePhysMat_Tags of UCk_GameplayTags
    {
        GameplayTags.Add(n"CkTests.ProbeTrace.PhysMat.Target");
    }
}

class UCk_AutoTest_ProbeTrace_HitsCarryPhysicalMaterial : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;

    private FCk_Handle _SelfHandle;
    private AStaticMeshActor _Wall;

    private UPhysicalMaterial _ProbeMaterial;
    private UPhysicalMaterial _WallMaterial;
    private UPhysicalMaterial _BlockMaterial;

    // Y-band 63000.
    private float _Band = 63000.0;
    private float _TraceZ = 300.0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;

        _ProbeMaterial = Cast<UPhysicalMaterial>(NewObject(GetTransientPackage(), UPhysicalMaterial));
        _WallMaterial = Cast<UPhysicalMaterial>(NewObject(GetTransientPackage(), UPhysicalMaterial));
        _BlockMaterial = Cast<UPhysicalMaterial>(NewObject(GetTransientPackage(), UPhysicalMaterial));

        auto Cube = Cast<UStaticMesh>(LoadObject(UStaticMesh, "/Engine/BasicShapes/Cube.Cube"));
        if (!IsValid(Cube))
        {
            FinishFailure("Failed to load /Engine/BasicShapes/Cube.Cube");
            return;
        }

        _Wall = Cast<AStaticMeshActor>(SpawnActor(AStaticMeshActor, FVector(750.0, _Band, _TraceZ)));
        _Wall.StaticMeshComponent.SetMobility(EComponentMobility::Movable);
        _Wall.StaticMeshComponent.SetStaticMesh(Cube);
        _Wall.StaticMeshComponent.SetCollisionProfileName(n"BlockAll");
        _Wall.StaticMeshComponent.SetPhysMaterialOverride(_WallMaterial);

        Do_MakeProbe(300.0);
        Do_MakeBlock(1200.0);

        WaitUntil(n"Check_ProbeTraceable", n"OnSettled");
    }

    private void Do_MakeProbe(float InX)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        Entity.Request_OverrideToSelf();

        auto ProbeTransform = utils_transform::Add(Entity,
            FTransform(FRotator::ZeroRotator, FVector(InX, _Band, _TraceZ)), ECk_Replication::DoesNotReplicate);

        auto SurfaceInfo = FCk_Probe_SurfaceInfo();
        SurfaceInfo.Set_PhysicalMaterialSource(ECk_PhysicalMaterialSource::Direct);
        SurfaceInfo.Set_PhysicalMaterial(_ProbeMaterial);

        auto ProbeParams = FCk_Probe_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"CkTests.ProbeTrace.PhysMat.Target"));
        ProbeParams.Set_MotionType(ECk_MotionType::Static);
        ProbeParams.Set_SurfaceInfo(SurfaceInfo);
        utils_probe::Add_Box(ProbeTransform, FVector(50.0, 50.0, 50.0), ProbeParams, FCk_Probe_DebugInfo());
    }

    private void Do_MakeBlock(float InX)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(_SelfHandle);
        Entity.Request_OverrideToSelf();
        utils_transform::Add(Entity, FTransform(FRotator::ZeroRotator, FVector(InX, _Band, _TraceZ)),
            ECk_Replication::DoesNotReplicate);

        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(FVector(50.0, 50.0, 50.0));
        auto Params = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Params.Set_ShapeDimensions(Shape);
        Params.Set_MotionType(ECk_MotionType::Static);
        Params.Set_CollisionProfileName(n"BlockAll");
        Params.Set_SurfaceSource(ECk_JoltBody_SurfaceSource::PhysicalMaterial);
        Params.Set_PhysicalMaterial(_BlockMaterial);
        utils_jolt_body::Add(Entity, Params);
    }

    private FGameplayTagContainer Make_Filter() const
    {
        auto Filter = FGameplayTagContainer();
        Filter.AddTag(utils_gameplay_tag::ResolveGameplayTag(n"CkTests.ProbeTrace.PhysMat.Target"));
        return Filter;
    }

    private FCk_Probe_RayCast_Settings Make_LineSettings() const
    {
        auto Settings = FCk_Probe_RayCast_Settings(
            FVector(0.0, _Band, _TraceZ), FVector(1500.0, _Band, _TraceZ), Make_Filter());
        Settings.Set_WorldHitPolicy(ECk_ProbeTrace_WorldHitPolicy::Reported);
        Settings.Set_OverlapNotifyPolicy(ECk_ProbeResponse_Policy::Silent);
        return Settings;
    }

    UFUNCTION()
    private void Check_ProbeTraceable(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        // The probe body and the JoltBody block are both added a frame or more after Add.
        auto Res = OutResult;
        Res.Set(utils_probe_trace::Request_MultiLineTrace(_SelfHandle, Make_LineSettings()).Num() == 2);
    }

    UFUNCTION()
    private void OnSettled(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        Assert_Equals_Int(utils_jolt_static_world::Request_BakeActor(_Wall), 1, "Wall cube bakes one body");

        auto LineHits = utils_probe_trace::Request_MultiLineTrace(_SelfHandle, Make_LineSettings());
        Assert_Equals_Int(LineHits.Num(), 3, "the line trace reports the probe, the wall and the block");
        if (LineHits.Num() == 3)
        {
            Assert_True(LineHits[0].Get_HitKind() == ECk_ProbeTrace_HitKind::Probe, "line [0] is the probe");
            Assert_True(LineHits[0].Get_PhysicalMaterial().Get() == _ProbeMaterial, "line [0] carries the probe's SurfaceInfo phys mat");
            Assert_True(LineHits[1].Get_HitKind() == ECk_ProbeTrace_HitKind::World, "line [1] is the baked wall");
            Assert_True(LineHits[1].Get_PhysicalMaterial().Get() == _WallMaterial, "line [1] carries the wall's PhysMaterialOverride");
            Assert_True(LineHits[2].Get_HitKind() == ECk_ProbeTrace_HitKind::World, "line [2] is the JoltBody block");
            Assert_True(LineHits[2].Get_PhysicalMaterial().Get() == _BlockMaterial, "line [2] carries the block's spec phys mat");
        }

        auto ShapeSettings = FCk_ShapeCast_Settings(
            FVector(0.0, _Band, _TraceZ), FVector(1500.0, _Band, _TraceZ),
            utils_shapes::Make_Sphere(FCk_ShapeSphere_Dimensions(10.0)), Make_Filter());
        ShapeSettings.Set_WorldHitPolicy(ECk_ProbeTrace_WorldHitPolicy::Reported);
        ShapeSettings.Set_OverlapNotifyPolicy(ECk_ProbeResponse_Policy::Silent);

        auto ShapeHits = utils_probe_trace::Request_MultiShapeTrace(_SelfHandle, ShapeSettings);
        Assert_Equals_Int(ShapeHits.Num(), 3, "the shape trace reports the probe, the wall and the block");
        if (ShapeHits.Num() == 3)
        {
            Assert_True(ShapeHits[0].Get_PhysicalMaterial().Get() == _ProbeMaterial, "shape [0] carries the probe's SurfaceInfo phys mat");
            Assert_True(ShapeHits[1].Get_PhysicalMaterial().Get() == _WallMaterial, "shape [1] carries the wall's PhysMaterialOverride");
            Assert_True(ShapeHits[2].Get_PhysicalMaterial().Get() == _BlockMaterial, "shape [2] carries the block's spec phys mat");
        }

        Do_Cleanup();
        FinishSuccess();
    }

    private void Do_Cleanup()
    {
        if (IsValid(_Wall))
        {
            utils_jolt_static_world::Request_RemoveActor(_Wall);
            _Wall.DestroyActor();
        }
    }
}
