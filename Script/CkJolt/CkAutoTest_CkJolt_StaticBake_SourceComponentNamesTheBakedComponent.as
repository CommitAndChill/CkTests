// Language=angelscript

//============================================================================
// CK JOLT - AUTOMATION TEST: STATIC BAKE, A HIT NAMES ITS SOURCE COMPONENT
//============================================================================
//
// A static-world hit resolves to a JoltStaticActor attribution entity. For a
// component bake, Get_SourceActor answers only the host - and one host can own
// many baked components - so Get_SourceComponent is what tells two components
// of the same actor apart. Contract under test:
//   1. Two components of ONE actor, each baked with Request_BakeComponent: a
//      ray over each names that component, and both share the host actor.
//   2. An actor bake (Request_BakeActor): Get_SourceComponent is null and
//      Get_SourceActor names the actor.
//============================================================================

class UCk_AutoTest_CkJolt_StaticBake_SourceComponentNamesTheBakedComponent : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 8.0f;

    private FVector _Origin = FVector(0.0, 67000.0, 300.0);

    private AActor _Host;
    private UStaticMeshComponent _Left;
    private UStaticMeshComponent _Right;
    private AStaticMeshActor _WholeActor;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Cube = Cast<UStaticMesh>(LoadObject(UStaticMesh, "/Engine/BasicShapes/Cube.Cube"));
        if (!IsValid(Cube))
        {
            FinishFailure("Failed to load /Engine/BasicShapes/Cube.Cube");
            return;
        }

        _Host = SpawnActor(AActor, _Origin);
        _Left = Make_Cube(Cube, _Origin);
        _Right = Make_Cube(Cube, _Origin + FVector(300.0, 0.0, 0.0));

        _WholeActor = Cast<AStaticMeshActor>(SpawnActor(AStaticMeshActor, _Origin + FVector(600.0, 0.0, 0.0)));
        _WholeActor.StaticMeshComponent.SetMobility(EComponentMobility::Movable);
        _WholeActor.StaticMeshComponent.SetStaticMesh(Cube);
        _WholeActor.StaticMeshComponent.SetCollisionProfileName(n"BlockAll");

        WaitOneFrame(n"OnBakeAndQuery");
    }

    private UStaticMeshComponent Make_Cube(UStaticMesh InCube, FVector InLocation)
    {
        auto Component = UStaticMeshComponent::Create(_Host);
        Component.SetWorldLocation(InLocation);
        Component.SetStaticMesh(InCube);
        Component.SetCollisionProfileName(n"BlockAll");
        return Component;
    }

    private FCk_Handle_JoltStaticActor Do_RayDownAt(FVector InCenter, FString InLabel)
    {
        auto Hit = utils_jolt_static_world::Get_RayCastStaticWorld(
            InCenter + FVector(0.0, 0.0, 500.0), InCenter - FVector(0.0, 0.0, 500.0));
        Assert_True(Hit.Get_HasHit(), f"down-ray over the {InLabel} hits");
        auto HitEntity = Hit.Get_Entity();
        const auto MaybeStaticActor = utils_jolt_static_actor::DoCast(HitEntity);
        Assert_True(MaybeStaticActor.IsSet(), f"the {InLabel} hit resolves to a JoltStaticActor attribution entity");
        return MaybeStaticActor.IsSet() ? MaybeStaticActor.GetValue() : utils_jolt_static_actor::Get_InvalidHandle();
    }

    UFUNCTION()
    private void OnBakeAndQuery(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (IsFinished()) { return; }

        // 1. Two components on one host: each hit names its own component, both name the host.
        Assert_True(utils_jolt_static_world::Request_BakeComponent(_Left) > 0, "left cube bakes");
        Assert_True(utils_jolt_static_world::Request_BakeComponent(_Right) > 0, "right cube bakes");

        auto LeftHit = Do_RayDownAt(_Origin, "left cube");
        auto RightHit = Do_RayDownAt(_Origin + FVector(300.0, 0.0, 0.0), "right cube");
        Assert_True(utils_jolt_static_actor::Get_SourceComponent(LeftHit) == _Left,
            "the left hit names the left component");
        Assert_True(utils_jolt_static_actor::Get_SourceComponent(RightHit) == _Right,
            "the right hit names the right component, not its sibling");
        Assert_True(utils_jolt_static_actor::Get_SourceActor(LeftHit) == _Host
            && utils_jolt_static_actor::Get_SourceActor(RightHit) == _Host,
            "both component hits share the host actor - which is why the component is needed");

        // 2. Actor bake: no source component, the actor is the source.
        Assert_Equals_Int(utils_jolt_static_world::Request_BakeActor(_WholeActor), 1, "the whole actor bakes one body");
        auto ActorHit = Do_RayDownAt(_Origin + FVector(600.0, 0.0, 0.0), "actor-baked cube");
        Assert_True(utils_jolt_static_actor::Get_SourceComponent(ActorHit) == nullptr,
            "an actor bake has no source component");
        Assert_True(utils_jolt_static_actor::Get_SourceActor(ActorHit) == _WholeActor,
            "an actor bake names the actor");

        // Shared-session hygiene: free the bodies and the actors.
        utils_jolt_static_world::Request_RemoveComponent(_Left);
        utils_jolt_static_world::Request_RemoveComponent(_Right);
        utils_jolt_static_world::Request_RemoveActor(_WholeActor);
        _Host.DestroyActor();
        _WholeActor.DestroyActor();

        FinishSuccess();
    }
}
