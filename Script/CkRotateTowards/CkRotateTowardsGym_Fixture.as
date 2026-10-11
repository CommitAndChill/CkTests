// Language=angelscript

struct FCkRotateTowardsGym_Fixture
{
    FCk_Handle_Transform Root;
    FCk_Handle_Transform Target;
    FCk_Handle_Transform RestPoint;
    TArray<FCk_Handle_RotateTowards> Turrets;
    TArray<FCk_Handle_Tween> Tweens;
    FVector Origin;
    int32 Station = 0;

    // Staged shared-source provenance. The legacy Create path below remains unchanged.
    FCk_Handle Owner;
    FCk_Handle_Transform Carrier;
    FCk_Handle_SceneNode TargetNode;
    TArray<FCk_Handle_Transform> TurretRoots;
    TArray<FCk_Handle_SceneNode> CubeNodes;
    TArray<FCk_Handle> CubeTargets;
    TArray<FCk_Handle_PendingEntityScript> CubePending;
    TArray<FCk_Handle> CubeSpawnCarriers;
    TArray<FCk_Handle_Transform> CubeAnchors;
    TArray<FTransform> CubeOffsets;
    TArray<FCk_Handle_UnrealComponent> CubeComponents;
    int32 Generation = 0;
    bool StaticStarted = false;
    bool StaticReady = false;
    bool TweenQueueStarted = false;
    bool StagingFailed = false;

    // Station 0 = Tracker (rate-limited vs instant), 1 = Clamped (+-45 deg yaw about a rest line), 2 = Modes (free /
    // yaw-locked / pitch-locked / disabled). InMeshListener receives the cube mesh components as they are added
    // (FCk_Delegate_UnrealComponent_OnAdded needs a UObject).
    bool Create(FCk_Handle InOwner, UObject InMeshListener, FVector InOrigin, int32 InStation)
    {
        Origin = InOrigin;
        Station = InStation;
        Turrets.Reset();
        Tweens.Reset();

        Root = utils_transform::Create(InOwner, FTransform(Origin), ECk_Replication::DoesNotReplicate);

        auto Composed = true;
        if (InStation == 0)
        {
            Create_LateralTarget(InMeshListener);
            Composed = Add_Turret(InMeshListener, FVector(0.0, -75.0, 0.0), Make_Spec(ECk_RotateTowards_Mode::RateLimited, 60.0f))
                && Add_Turret(InMeshListener, FVector(0.0, 75.0, 0.0), Make_Spec(ECk_RotateTowards_Mode::Instant, 60.0f));
        }
        else if (InStation == 1)
        {
            // The target tweens along the chord between the points at +70 and -70 deg of yaw on a 400 cm circle: it stays
            // in front of the turret (137 cm ahead at mid-sweep) and spends the ends of each sweep outside the +-45 deg range.
            const auto Start = Origin + FRotator(0.0, 70.0, 0.0).Vector() * 400.0;
            const auto End = Origin + FRotator(0.0, -70.0, 0.0).Vector() * 400.0;
            Target = utils_transform::Create(Root, FTransform(Start), ECk_Replication::DoesNotReplicate);
            Tweens.Add(utils_tween::Create_TweenEntityLocation(Target, End, 4.0f,
                ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo, -1));
            Add_Cube(InMeshListener, Target, FVector::ZeroVector, FVector(0.25, 0.25, 0.25));

            RestPoint = utils_transform::Create(Root, FTransform(Origin + FVector(400.0, 0.0, 0.0)), ECk_Replication::DoesNotReplicate);
            Add_Cube(InMeshListener, RestPoint, FVector::ZeroVector, FVector(0.1, 0.1, 0.1));

            auto Clamp = FCk_RotateTowards_RangeClamp(RestPoint);
            Clamp.Set_Yaw(FCk_RotateTowards_AxisRange(ECk_EnableDisable::Enable, FCk_FloatRange(-45.0, 45.0)));
            auto Spec = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            Spec.Set_RangeClamp(Clamp);
            Composed = Add_Turret(InMeshListener, FVector::ZeroVector, Spec);
        }
        else
        {
            // Two tweens on two entities: a carrier sweeps Y, the target (a scene node under it) sweeps Z in its offset.
            auto LegacyCarrier = utils_transform::Create(Root, FTransform(Origin + FVector(400.0, -300.0, 0.0)), ECk_Replication::DoesNotReplicate);
            Tweens.Add(utils_tween::Create_TweenEntityLocation(LegacyCarrier, Origin + FVector(400.0, 300.0, 0.0), 3.0f,
                ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo, -1));
            auto LegacyTargetNode = utils_scene_node::Create(LegacyCarrier, FTransform(FVector(0.0, 0.0, -150.0)));
            Tweens.Add(utils_tween::Create_TweenSceneNodeOffsetLocation(LegacyTargetNode, FVector(0.0, 0.0, 150.0), 2.0f,
                ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo, -1));
            Target = LegacyTargetNode.As_Transform();
            Add_Cube(InMeshListener, Target, FVector::ZeroVector, FVector(0.25, 0.25, 0.25));

            auto YawLocked = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            auto YawLockedTunables = YawLocked.Get_Tunables();
            YawLockedTunables.Set_Yaw(FCk_RotateTowards_Axis(ECk_RotateTowards_AxisMode::Locked, 90.0f));
            YawLocked.Set_Tunables(YawLockedTunables);

            auto PitchLocked = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            auto PitchLockedTunables = PitchLocked.Get_Tunables();
            PitchLockedTunables.Set_Pitch(FCk_RotateTowards_Axis(ECk_RotateTowards_AxisMode::Locked, 90.0f));
            PitchLocked.Set_Tunables(PitchLockedTunables);

            auto Disabled = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            Disabled.Set_StartingState(ECk_EnableDisable::Disable);

            Composed = Add_Turret(InMeshListener, FVector(0.0, -225.0, 0.0), Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f))
                && Add_Turret(InMeshListener, FVector(0.0, -75.0, 0.0), YawLocked)
                && Add_Turret(InMeshListener, FVector(0.0, 75.0, 0.0), PitchLocked)
                && Add_Turret(InMeshListener, FVector(0.0, 225.0, 0.0), Disabled);
        }

        if (Composed == false)
        {
            Request_Destroy();
            return false;
        }
        return true;
    }

    // Staged source path: caller retains this fixture, even when a creator fails.
    // No destroy or reset occurs here; source owns quiescence and exact retirement.
    private bool IsLive(FCk_Handle InHandle) const
    {
        return ck::IsValid(InHandle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    private bool Get_IsPristineStaged() const
    {
        return !StaticStarted && !StaticReady && !TweenQueueStarted && !StagingFailed &&
            Generation == 0 && Owner == FCk_Handle() &&
            Root == FCk_Handle_Transform() && Target == FCk_Handle_Transform() &&
            RestPoint == FCk_Handle_Transform() && Carrier == FCk_Handle_Transform() &&
            TargetNode == FCk_Handle_SceneNode() && TurretRoots.Num() == 0 &&
            Turrets.Num() == 0 && Tweens.Num() == 0 && CubeNodes.Num() == 0 &&
            CubeComponents.Num() == 0 && Origin == FVector::ZeroVector && Station == 0;
    }

    // Coupled native context reads MotionAdmission.Owner/Generation from the
    // actual GameState; source then rejects terminal/non-staging phases.
    private bool Get_HasStagingSource(UObject InListener, FCk_Handle InOwner,
        int32 InGeneration) const
    {
        if (!IsLive(InOwner) || InGeneration <= 0 || ck::Is_NOT_Valid(InListener))
        { return false; }
        auto Source = Cast<ACk_RotateTowardsGym_GameState>(InListener);
        if (ck::Is_NOT_Valid(Source) ||
            Source.GetClass() != ACk_RotateTowardsGym_GameState ||
            Source.IsActorBeingDestroyed() || !Source.HasAuthority())
        { return false; }
        auto World = Source.GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != Source ||
            !UCk_RotateTowardsGym_Runtime_UE::Get_IsSourceContext(
                Source, InOwner, InGeneration) ||
            !Source.Get_HasWorldIdentity() ||
            !Source.Get_CanStageMotion(InGeneration))
        { return false; }
        auto WorldRoot = ck::TransientEntity();
        if (!IsLive(WorldRoot)) { return false; }
        TArray<FCk_Handle> Direct;
        return UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
            InOwner, WorldRoot, Direct);
    }

    private bool Fail_Staging()
    {
        StagingFailed = true;
        StaticReady = false;
        return false;
    }

    private bool Add_Cube_Static(UObject InMeshListener, FCk_Handle_Transform InAttachTo,
        FVector InOffset, FVector InScale)
    {
        if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
            !IsLive(FCk_Handle(InAttachTo)) || !utils_transform::Has(InAttachTo) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Owner)) { return Fail_Staging(); }
        FCkRotateTowardsGym_CubeParams Params;
        Params.Generation = Generation; Params.Station = Station;
        Params.Slot = (Station == 0 ? 0 : Station == 1 ? 3 : 6) + CubeTargets.Num();
        auto Offset = FTransform(FRotator::ZeroRotator, InOffset, InScale);
        Params.InitialTransform = Offset * utils_transform::Get_EntityCurrentTransform(InAttachTo);
        auto Pending = utils_entity_script::Request_SpawnEntity(Owner,
            UCk_EntityScript_RotateTowardsGym_Cube, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(Pending)) { return Fail_Staging(); }
        auto TargetHandle = Pending.Get_EntityUnderConstruction();
        CubeTargets.Add(TargetHandle); CubePending.Add(Pending);
        FCk_Handle CubeCarrier;
        auto OwnerParent = utils_entity_lifetime::Get_LifetimeOwner(Owner);
        bool Captured = UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrierWithParent(
            TargetHandle, Owner, UCk_EntityScript_RotateTowardsGym_Cube,
            ck::TransientEntity(), OwnerParent, CubeCarrier);
        CubeSpawnCarriers.Add(CubeCarrier);
        CubeAnchors.Add(InAttachTo); CubeOffsets.Add(Offset);
        CubeNodes.Add(FCk_Handle_SceneNode()); CubeComponents.Add(FCk_Handle_UnrealComponent());
        if (!Captured || !utils_pending_entity_script::Get_IsValid(Pending) || !IsLive(TargetHandle))
        { return Fail_Staging(); }
        return true;
    }

    // Read only creator-returned scripts, including after a later staging failure.
    // Component children can finish before the source's next admission poll.
    bool Capture_CubeComponents()
    {
        bool Valid = true;
        for (int32 i = 0; i < CubeTargets.Num(); ++i)
        {
            auto H = CubeTargets[i];
            if (!IsLive(H) || !utils_entity_script::Has(H)) { continue; }
            auto Script = Cast<UCk_EntityScript_RotateTowardsGym_Cube>(utils_entity_script::TryGet_EntityScript(H));
            if (ck::Is_NOT_Valid(Script) || Script.Generation != Generation || Script.Station != Station ||
                Script.Slot != (Station == 0 ? 0 : Station == 1 ? 3 : 6) + i)
            { Valid = false; continue; }
            auto Component = Script.Get_Component();
            if (CubeComponents[i] == FCk_Handle_UnrealComponent()) { CubeComponents[i] = Component; }
            else if (CubeComponents[i] != Component) { Valid = false; }
        }
        return Valid;
    }

    // Deferred construction retains all targets before polling or feature admission.
    bool Poll_Cubes(UObject InMeshListener)
    {
        if (StagingFailed || !Get_HasStagingSource(InMeshListener, Owner, Generation)) { return false; }
        bool Ready = true;
        for (int32 i = 0; i < CubeTargets.Num(); ++i)
        {
            auto H = CubeTargets[i];
            if (!IsLive(H)) { StagingFailed = true; return false; }
            if (!utils_entity_script::Has(H) || !utils_transform::Has(H)) { Ready = false; continue; }
            auto Script = Cast<UCk_EntityScript_RotateTowardsGym_Cube>(utils_entity_script::TryGet_EntityScript(H));
            if (ck::Is_NOT_Valid(Script) || Script.Generation != Generation || Script.Station != Station ||
                Script.Slot != (Station == 0 ? 0 : Station == 1 ? 3 : 6) + i)
            { StagingFailed = true; return false; }
            auto Component = Script.Get_Component();
            if (CubeComponents[i] == FCk_Handle_UnrealComponent()) { CubeComponents[i] = Component; }
            else if (CubeComponents[i] != Component) { StagingFailed = true; return false; }
            if (!Script.Get_IsReady()) { Ready = false; continue; }
            if (CubeNodes[i] == FCk_Handle_SceneNode())
            {
                auto Transform = utils_transform::DoCast(H);
                if (!Transform.IsSet()) { StagingFailed = true; return false; }
                auto T = H.As_Transform();
                CubeNodes[i] = utils_scene_node::Add(T, CubeAnchors[i], CubeOffsets[i]);
                if (ck::Is_NOT_Valid(CubeNodes[i]) || FCk_Handle(CubeNodes[i]) != H)
                { StagingFailed = true; return false; }
            }
        }
        return Ready;
    }

    private bool Add_Turret_Static(UObject InMeshListener, FVector InLocalOffset,
        FCk_RotateTowards_Spec InSpec)
    {
        if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
            !IsLive(FCk_Handle(Root)) || !utils_transform::Has(Root) ||
            !IsLive(FCk_Handle(Target)) || !utils_transform::Has(Target) ||
            (Station == 1 && (!IsLive(FCk_Handle(RestPoint)) ||
                !utils_transform::Has(RestPoint))) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Root))
        { return Fail_Staging(); }
        auto Turret = utils_transform::Create(Root, FTransform(Origin + InLocalOffset),
            ECk_Replication::DoesNotReplicate);
        TurretRoots.Add(Turret); // Keep the transform even if feature admission fails.
        if (!IsLive(FCk_Handle(Turret)) || !utils_transform::Has(Turret) ||
            !Get_HasStagingSource(InMeshListener, Owner, Generation))
        { return Fail_Staging(); }
        auto DisabledSpec = InSpec;
        DisabledSpec.Set_StartingState(ECk_EnableDisable::Disable);
        auto RotateTowards = utils_rotate_towards::Add(Turret, DisabledSpec);
        if (!IsLive(FCk_Handle(RotateTowards)) ||
            !utils_rotate_towards::DoCast(FCk_Handle(RotateTowards)).IsSet())
        { return Fail_Staging(); }
        Turrets.Add(RotateTowards);
        return Add_Cube_Static(InMeshListener, Turret, FVector(40.0, 0.0, 0.0),
            FVector(0.9, 0.3, 0.3));
    }

    // Called by the authoritative source only. Three slots produce 3/3/5 cube pairs.
    bool Create_Static(FCk_Handle InOwner, UObject InMeshListener, FVector InOrigin,
        int32 InStation, int32 InGeneration)
    {
        if (!Get_IsPristineStaged() || InStation < 0 || InStation > 2 ||
            !Math::IsFinite(InOrigin.X) || !Math::IsFinite(InOrigin.Y) ||
            !Math::IsFinite(InOrigin.Z) ||
            !Get_HasStagingSource(InMeshListener, InOwner, InGeneration) ||
            !utils_entity_lifetime::Get_CanCreateEntity(InOwner)) { return false; }
        StaticStarted = true;
        Owner = InOwner;
        Origin = InOrigin;
        Station = InStation;
        Generation = InGeneration;
        Root = utils_transform::Create(Owner, FTransform(Origin),
            ECk_Replication::DoesNotReplicate);
        if (!IsLive(FCk_Handle(Root)) || !utils_transform::Has(Root))
        { return Fail_Staging(); }

        if (Station == 0)
        {
            if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
                !IsLive(FCk_Handle(Root)) || !utils_transform::Has(Root) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Root))
            { return Fail_Staging(); }
            Target = utils_transform::Create(Root,
                FTransform(Origin + FVector(400.0, -300.0, 0.0)),
                ECk_Replication::DoesNotReplicate);
            if (!IsLive(FCk_Handle(Target)) || !utils_transform::Has(Target) ||
                !Add_Cube_Static(InMeshListener, Target, FVector::ZeroVector,
                    FVector(0.25, 0.25, 0.25)) ||
                !Add_Turret_Static(InMeshListener, FVector(0.0, -75.0, 0.0),
                    Make_Spec(ECk_RotateTowards_Mode::RateLimited, 60.0f)) ||
                !Add_Turret_Static(InMeshListener, FVector(0.0, 75.0, 0.0),
                    Make_Spec(ECk_RotateTowards_Mode::Instant, 60.0f)))
            { return Fail_Staging(); }
        }
        else if (Station == 1)
        {
            if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
                !IsLive(FCk_Handle(Root)) || !utils_transform::Has(Root) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Root))
            { return Fail_Staging(); }
            const auto Start = Origin + FRotator(0.0, 70.0, 0.0).Vector() * 400.0;
            Target = utils_transform::Create(Root, FTransform(Start),
                ECk_Replication::DoesNotReplicate);
            if (!IsLive(FCk_Handle(Target)) || !utils_transform::Has(Target) ||
                !Add_Cube_Static(InMeshListener, Target, FVector::ZeroVector,
                    FVector(0.25, 0.25, 0.25))) { return Fail_Staging(); }
            if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
                !IsLive(FCk_Handle(Root)) || !utils_transform::Has(Root) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Root))
            { return Fail_Staging(); }
            RestPoint = utils_transform::Create(Root,
                FTransform(Origin + FVector(400.0, 0.0, 0.0)),
                ECk_Replication::DoesNotReplicate);
            if (!IsLive(FCk_Handle(RestPoint)) || !utils_transform::Has(RestPoint) ||
                !Add_Cube_Static(InMeshListener, RestPoint, FVector::ZeroVector,
                    FVector(0.1, 0.1, 0.1))) { return Fail_Staging(); }
            if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
                !IsLive(FCk_Handle(RestPoint)) || !utils_transform::Has(RestPoint) ||
                !IsLive(FCk_Handle(Target)) || !utils_transform::Has(Target))
            { return Fail_Staging(); }
            auto Clamp = FCk_RotateTowards_RangeClamp(RestPoint);
            Clamp.Set_Yaw(FCk_RotateTowards_AxisRange(ECk_EnableDisable::Enable,
                FCk_FloatRange(-45.0, 45.0)));
            auto Spec = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            Spec.Set_RangeClamp(Clamp);
            if (!Add_Turret_Static(InMeshListener, FVector::ZeroVector, Spec))
            { return Fail_Staging(); }
        }
        else
        {
            if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
                !IsLive(FCk_Handle(Root)) || !utils_transform::Has(Root) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Root))
            { return Fail_Staging(); }
            Carrier = utils_transform::Create(Root,
                FTransform(Origin + FVector(400.0, -300.0, 0.0)),
                ECk_Replication::DoesNotReplicate);
            if (!IsLive(FCk_Handle(Carrier)) || !utils_transform::Has(Carrier))
            { return Fail_Staging(); }
            if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Carrier))
            { return Fail_Staging(); }
            TargetNode = utils_scene_node::Create(Carrier,
                FTransform(FVector(0.0, 0.0, -150.0)));
            if (!IsLive(FCk_Handle(TargetNode)) ||
                !utils_scene_node::Has(FCk_Handle(TargetNode)) ||
                !utils_transform::Has(FCk_Handle(TargetNode)))
            { return Fail_Staging(); }
            Target = TargetNode.As_Transform();
            if (!IsLive(FCk_Handle(Target)) || !utils_transform::Has(Target) ||
                !Add_Cube_Static(InMeshListener, Target, FVector::ZeroVector,
                    FVector(0.25, 0.25, 0.25))) { return Fail_Staging(); }

            auto YawLocked = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            auto YawTunables = YawLocked.Get_Tunables();
            YawTunables.Set_Yaw(FCk_RotateTowards_Axis(ECk_RotateTowards_AxisMode::Locked, 90.0f));
            YawLocked.Set_Tunables(YawTunables);
            auto PitchLocked = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            auto PitchTunables = PitchLocked.Get_Tunables();
            PitchTunables.Set_Pitch(FCk_RotateTowards_Axis(ECk_RotateTowards_AxisMode::Locked, 90.0f));
            PitchLocked.Set_Tunables(PitchTunables);
            auto Disabled = Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f);
            Disabled.Set_StartingState(ECk_EnableDisable::Disable);
            if (!Add_Turret_Static(InMeshListener, FVector(0.0, -225.0, 0.0),
                    Make_Spec(ECk_RotateTowards_Mode::RateLimited, 90.0f)) ||
                !Add_Turret_Static(InMeshListener, FVector(0.0, -75.0, 0.0), YawLocked) ||
                !Add_Turret_Static(InMeshListener, FVector(0.0, 75.0, 0.0), PitchLocked) ||
                !Add_Turret_Static(InMeshListener, FVector(0.0, 225.0, 0.0), Disabled))
            { return Fail_Staging(); }
        }

        const auto ExpectedTurrets = Station == 0 ? 2 : (Station == 1 ? 1 : 4);
        const auto ExpectedCubes = Station == 2 ? 5 : 3;
        if (!Get_HasStagingSource(InMeshListener, Owner, Generation) ||
            TurretRoots.Num() != ExpectedTurrets || Turrets.Num() != ExpectedTurrets ||
            CubeNodes.Num() != ExpectedCubes || CubeComponents.Num() != ExpectedCubes)
        { return Fail_Staging(); }
        for (int32 i = 0; i < TurretRoots.Num(); ++i)
        {
            if (!IsLive(FCk_Handle(TurretRoots[i])) ||
                !utils_transform::Has(TurretRoots[i]) ||
                !IsLive(FCk_Handle(Turrets[i])) ||
                !utils_rotate_towards::DoCast(FCk_Handle(Turrets[i])).IsSet())
            { return Fail_Staging(); }
            for (int32 j = 0; j < i; ++j)
            {
                if (TurretRoots[i] == TurretRoots[j] || Turrets[i] == Turrets[j])
                { return Fail_Staging(); }
            }
        }
        StaticReady = true; // Structural admission only; no motion/Ready publication.
        return true;
    }

    // Caller must pass its exact successful OnAdded identities after all 11 callbacks.
    // Call all three fixtures consecutively from one non-latent GameState Actor Tick.
    bool Create_PausedTweens(UObject InListener,
        TArray<FCk_Handle_UnrealComponent> InConfirmedComponents)
    {
        if (!StaticReady || StagingFailed || TweenQueueStarted) { return false; }
        if (Station < 0 || Station > 2 || Generation <= 0 ||
            !Get_HasStagingSource(InListener, Owner, Generation) ||
            !IsLive(FCk_Handle(Root)) || !utils_transform::Has(Root) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Owner) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Root) ||
            InConfirmedComponents.Num() != CubeComponents.Num() ||
            CubeNodes.Num() != CubeComponents.Num() ||
            TurretRoots.Num() != Turrets.Num() ||
            Tweens.Num() != 0)
        { return Fail_Staging(); }
        for (int32 i = 0; i < CubeComponents.Num(); ++i)
        {
            if (!IsLive(FCk_Handle(CubeNodes[i])) ||
                !utils_scene_node::Has(FCk_Handle(CubeNodes[i])) ||
                !IsLive(FCk_Handle(CubeComponents[i])) ||
                !utils_unreal_component::Has(FCk_Handle(CubeComponents[i])) ||
                InConfirmedComponents[i] != CubeComponents[i])
            { return Fail_Staging(); }
        }
        for (int32 i = 0; i < TurretRoots.Num(); ++i)
        {
            if (!IsLive(FCk_Handle(TurretRoots[i])) ||
                !utils_transform::Has(TurretRoots[i]) ||
                !IsLive(FCk_Handle(Turrets[i])) ||
                !utils_rotate_towards::DoCast(FCk_Handle(Turrets[i])).IsSet())
            { return Fail_Staging(); }
        }
        if (!Get_HasStagingSource(InListener, Owner, Generation))
        { return Fail_Staging(); }
        auto PauseDelegate = FCk_Delegate_Request_OnCompleted(
            InListener, n"OnRotateTowardsGymTweenPaused");
        TweenQueueStarted = true;
        if (Station == 0 || Station == 1)
        {
            if (!Get_HasStagingSource(InListener, Owner, Generation) ||
                !IsLive(FCk_Handle(Target)) || !utils_transform::Has(Target) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Target))
            { return Fail_Staging(); }
            FVector End = Origin + FVector(400.0, 300.0, 0.0);
            float32 Duration = 3.0f;
            ECk_TweenEasing Easing = ECk_TweenEasing::InOutSine;
            if (Station == 1)
            {
                End = Origin + FRotator(0.0, -70.0, 0.0).Vector() * 400.0;
                Duration = 4.0f;
            }
            auto Tween = utils_tween::Create_TweenEntityLocation(Target, End, Duration,
                Easing, ECk_TweenLoopType::Yoyo, -1);
            Tweens.Add(Tween);
            if (!IsLive(FCk_Handle(Tween)) ||
                !utils_tween::Has(FCk_Handle(Tween)))
            { return Fail_Staging(); }
            auto Queued = utils_tween::Pause(Tween, PauseDelegate);
            if (!IsLive(FCk_Handle(Queued)) || Queued != Tween)
            { return Fail_Staging(); }
            if (!Get_HasStagingSource(InListener, Owner, Generation))
            { return Fail_Staging(); }
        }
        else
        {
            if (!Get_HasStagingSource(InListener, Owner, Generation) ||
                !IsLive(FCk_Handle(Carrier)) ||
                !IsLive(FCk_Handle(TargetNode)) ||
                !utils_transform::Has(Carrier) ||
                !utils_scene_node::Has(FCk_Handle(TargetNode)) ||
                !utils_transform::Has(FCk_Handle(TargetNode)) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Carrier) ||
                !utils_entity_lifetime::Get_CanCreateEntity(FCk_Handle(TargetNode)))
            { return Fail_Staging(); }
            auto CarrierTween = utils_tween::Create_TweenEntityLocation(Carrier,
                Origin + FVector(400.0, 300.0, 0.0), 3.0f,
                ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo, -1);
            Tweens.Add(CarrierTween);
            if (!IsLive(FCk_Handle(CarrierTween)) ||
                !utils_tween::Has(FCk_Handle(CarrierTween)))
            { return Fail_Staging(); }
            auto CarrierQueued = utils_tween::Pause(CarrierTween, PauseDelegate);
            if (!IsLive(FCk_Handle(CarrierQueued)) || CarrierQueued != CarrierTween)
            { return Fail_Staging(); }
            if (!Get_HasStagingSource(InListener, Owner, Generation) ||
                !IsLive(FCk_Handle(TargetNode)) ||
                !utils_scene_node::Has(FCk_Handle(TargetNode)) ||
                !utils_transform::Has(FCk_Handle(TargetNode)) ||
                !utils_entity_lifetime::Get_CanCreateEntity(FCk_Handle(TargetNode)))
            { return Fail_Staging(); }
            auto OffsetTween = utils_tween::Create_TweenSceneNodeOffsetLocation(TargetNode,
                FVector(0.0, 0.0, 150.0), 2.0f,
                ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo, -1);
            Tweens.Add(OffsetTween);
            if (!IsLive(FCk_Handle(OffsetTween)) ||
                !utils_tween::Has(FCk_Handle(OffsetTween)))
            { return Fail_Staging(); }
            auto OffsetQueued = utils_tween::Pause(OffsetTween, PauseDelegate);
            if (!IsLive(FCk_Handle(OffsetQueued)) || OffsetQueued != OffsetTween)
            { return Fail_Staging(); }
            if (!Get_HasStagingSource(InListener, Owner, Generation))
            { return Fail_Staging(); }
        }
        return true; // Pause receipts and actual paused states remain source obligations.
    }

    // The target sweeps +-300 cm laterally, 400 cm ahead of the turrets.
    void Create_LateralTarget(UObject InMeshListener)
    {
        Target = utils_transform::Create(Root, FTransform(Origin + FVector(400.0, -300.0, 0.0)), ECk_Replication::DoesNotReplicate);
        Tweens.Add(utils_tween::Create_TweenEntityLocation(Target, Origin + FVector(400.0, 300.0, 0.0), 3.0f,
            ECk_TweenEasing::InOutSine, ECk_TweenLoopType::Yoyo, -1));
        Add_Cube(InMeshListener, Target, FVector::ZeroVector, FVector(0.25, 0.25, 0.25));
    }

    FCk_RotateTowards_Spec Make_Spec(ECk_RotateTowards_Mode InMode, float32 InTurnRateDegPerSec) const
    {
        const auto Axis = FCk_RotateTowards_Axis(ECk_RotateTowards_AxisMode::Free, InTurnRateDegPerSec);
        auto Tunables = FCk_RotateTowards_Tunables(InMode);
        Tunables.Set_Pitch(Axis);
        Tunables.Set_Yaw(Axis);
        Tunables.Set_Roll(Axis);
        Tunables.Set_ReachedToleranceDeg(1.0f);

        auto Spec = FCk_RotateTowards_Spec(Target);
        Spec.Set_Tunables(Tunables);
        return Spec;
    }

    bool Add_Turret(UObject InMeshListener, FVector InLocalOffset, FCk_RotateTowards_Spec InSpec)
    {
        auto Turret = utils_transform::Create(Root, FTransform(Origin + InLocalOffset), ECk_Replication::DoesNotReplicate);
        auto RotateTowards = utils_rotate_towards::Add(Turret, InSpec);
        if (ck::Is_NOT_Valid(RotateTowards))
        {
            return false;
        }
        Turrets.Add(RotateTowards);
        // Elongated along +X and pushed forward of the pivot, so the cube's long axis reads as the facing.
        Add_Cube(InMeshListener, Turret, FVector(40.0, 0.0, 0.0), FVector(0.9, 0.3, 0.3));
        return true;
    }

    void Add_Cube(UObject InMeshListener, FCk_Handle_Transform InAttachTo, FVector InOffset, FVector InScale)
    {
        auto Node = utils_scene_node::Create(InAttachTo, FTransform(FRotator::ZeroRotator, InOffset, InScale));
        const auto ComponentParams = utils_unreal_component::Make_Params(UStaticMeshComponent,
            ECk_UnrealComponent_TickPolicy::DoNotTick, n"RotateTowardsGym_Cube");
        auto ComponentHandle = utils_unreal_component::Add(FCk_Handle(Node), ComponentParams);
        utils_unreal_component::BindTo_OnAdded(ComponentHandle,
            FCk_Delegate_UnrealComponent_OnAdded(InMeshListener, n"OnRotateTowardsGymCubeAdded"));
    }

    void Request_Destroy()
    {
        if (ck::IsValid(Root))
        {
            utils_entity_lifetime::Request_DestroyEntity(Root);
        }
    }
}
