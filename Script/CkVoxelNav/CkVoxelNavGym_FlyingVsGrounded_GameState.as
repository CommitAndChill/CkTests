// Language=angelscript

class ACk_VoxelNavGym_SharedFloor : ACk_Gym_Floor
{
    default bReplicates = false;
    default bReplicateMovement = true;
    UFUNCTION(BlueprintOverride) void ConstructionScript()
    { Super::ConstructionScript(); SetActorScale3D(FVector(30.0, 30.0, 0.5)); }
}

class ACk_VoxelNavGym_SharedWall : AStaticMeshActor
{
    default bReplicates = false;
    default bReplicateMovement = true;
    UFUNCTION(BlueprintOverride) void ConstructionScript()
    {
        StaticMeshComponent.SetMobility(EComponentMobility::Movable);
        auto Cube = Cast<UStaticMesh>(LoadObject(this, "/Engine/BasicShapes/Cube.Cube"));
        if (ck::IsValid(Cube)) { StaticMeshComponent.SetStaticMesh(Cube); }
        auto Material = Cast<UMaterialInterface>(
            LoadObject(this, "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (ck::IsValid(Material)) { StaticMeshComponent.SetMaterial(0, Material); }
        StaticMeshComponent.SetCollisionProfileName(n"BlockAll");
        SetActorScale3D(FVector(2.0, 9.0, 12.0));
    }
}

class ACk_VoxelNavGym_FlyingVsGrounded_GameState : ACkGym_GameState_UE
{
    default bReplicateUsingRegisteredSubObjectList = true;
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkVoxelNavGym_WireSnapshot Snapshot;
    UPROPERTY(Replicated) bool SourceFailed = false;
    UPROPERTY(Replicated) TArray<AActor> FixtureActors;

    private TArray<AActor> _Fixtures;
    private TArray<AActor> _AcceptedFixtures;
    private bool _FixturesAccepted = false;
    private FCk_Handle_PendingEntityScript _Pending;
    private FCk_Handle_PendingEntityScript _PendingRoot;
    private FCk_Handle _ReplicationRoot;
    private FCk_Handle _SourceRoot;
    private UCk_EntityScript_VoxelNavGym_FlyingVsGrounded_Source _Source;
    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCkGym_Entry _Entry;
    private int32 _Index = -1;
    private bool _Captured = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private bool _ProviderSelected = false;
    private bool _StartRequested = false;
    private bool _HasConsumed = false;
    private FString _FailureReason;
    private FString _NavigationReason;
    private ECk_NavSurface_Provider _ProviderBefore = ECk_NavSurface_Provider::Recast;
    // 0 waiting, 1 Recast/fixtures, 2 source start, 3 active, 4 control settling.
    private int32 _Phase = 0;
    private double _PendingSince = 0.0;
    private double _PublishElapsed = 0.0;
    private FCkVoxelNavGym_WireSnapshot _Consumed;
    private ACk_VoxelNavGym_FlyingVsGrounded_PlayerController _Requester;
    private APawn _RequesterPawn;
    private int32 _RequestSerial = 0;
    private int32 _RequestAction = -1;
    private int64 _RequestedGeneration = 0;
    private int64 _BeforeControlCount = 0;
    private int64 _BeforePairGeneration = 0;
    private bool _BeforeGoalsOutbound = true;

    UFUNCTION(BlueprintOverride)
    void BeginPlay() { SetTickGroup(ETickingGroup::TG_PostUpdateWork); }

    UFUNCTION(BlueprintPure) FCk_Handle Get_SourceRoot(int32 Index)
    { return HasAuthority() && Index == 0 ? _SourceRoot : FCk_Handle(); }

    UFUNCTION(BlueprintPure) bool Get_HasSessionFailure() const
    { return _Failed || SourceFailed; }

    UFUNCTION(BlueprintPure) FString Get_DiagnosticState() const
    {
        return f"phase={_Phase} captured={_Captured} failed={_Failed} " +
            f"source={ck::IsValid(_SourceRoot)} fixtures={FixtureActors.Num()} " +
            f"nav={_NavigationReason} reason={_FailureReason}";
    }

    private bool TryCapture()
    {
        auto CurrentWorld = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(CurrentWorld) ||
            CurrentWorld.GetGameState() != this || !Get_HasPublishedCatalog() ||
            (_Captured && CurrentWorld != _World)) { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != CurrentWorld ||
                Mode.GetClass() != ACk_VoxelNavGym_FlyingVsGrounded_GameMode)
            { return false; }
        }
        else if (CurrentWorld.GetNetMode() != ENetMode::NM_Client) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "VoxelNav Flying Vs Grounded" ||
            Entry.GameModeClass != ACk_VoxelNavGym_FlyingVsGrounded_GameMode ||
            (_Captured && (Index != _Index ||
                Entry.DisplayName != _Entry.DisplayName ||
                Entry.GameModeClass != _Entry.GameModeClass ||
                Entry.LevelName != _Entry.LevelName ||
                Entry.Category != _Entry.Category ||
                Entry.HintCode != _Entry.HintCode ||
                Entry.HasMultiplayerComponent != _Entry.HasMultiplayerComponent)))
        { return false; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs)) { return false; }
        auto Root = Ecs.Get_TransientEntity();
        if (!UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(Root, CurrentWorld) ||
            (_Captured && Root != _WorldRoot)) { return false; }
        bool Closed = true;
        if (!UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(Root, Closed) || Closed)
        { return false; }
        if (!_Captured)
        { _World = CurrentWorld; _WorldRoot = Root; _Entry = Entry; _Index = Index; _Captured = true; }
        return true;
    }

    private bool HasSourceRoot()
    {
        return _Captured &&
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(_SourceRoot, _World) &&
            utils_entity_lifetime::Has_LifetimeOwner(_SourceRoot) &&
            utils_entity_lifetime::Get_LifetimeOwner(_SourceRoot) == _ReplicationRoot &&
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(_ReplicationRoot, _World) &&
            utils_entity_lifetime::Has_LifetimeOwner(_ReplicationRoot) &&
            utils_entity_lifetime::Get_LifetimeOwner(_ReplicationRoot) == _WorldRoot;
    }

    private bool HasExactSource()
    {
        return HasSourceRoot() && ck::IsValid(_Source) &&
            _Source.GetClass() == UCk_EntityScript_VoxelNavGym_FlyingVsGrounded_Source &&
            _Source.Get_SourceRoot() == _SourceRoot && !_Source.Get_IsClosed() &&
            utils_entity_script::TryGet_EntityScript(_SourceRoot) == _Source;
    }

    private void RestoreProvider()
    {
        if (!_ProviderSelected) { return; }
        if (HasAuthority() && _Captured &&
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(_WorldRoot, _World) &&
            utils_nav_surface::Get_Provider() == ECk_NavSurface_Provider::Recast)
        { utils_nav_surface::Request_SetProvider(_ProviderBefore); }
        _ProviderSelected = false;
    }

    void Request_CloseClient(FString Reason)
    {
        if (HasAuthority() || _Failed || _Ending) { return; }
        _Failed = true; _FailureReason = Reason;
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        ck::Trace(f"VoxelNav client presentation closed: {Reason}");
    }

    UFUNCTION() void Request_Fail(FString Reason)
    {
        if (!HasAuthority()) { Request_CloseClient(Reason); return; }
        if (_Failed || _Ending) { return; }
        _Failed = true; SourceFailed = true; _FailureReason = Reason; _Phase = 0;
        if (ck::IsValid(_Source)) { _Source.Request_Block(); }
        if (HasSourceRoot())
        {
            if (utils_pending_entity_script::Get_IsValid(_Pending))
            { utils_pending_entity_script::Request_DestroyEntity(_Pending); }
            else { utils_entity_lifetime::Request_DestroyEntity(_SourceRoot); }
        }
        for (auto Actor : _Fixtures)
        {
            if (ck::IsValid(Actor) && !Actor.IsActorBeingDestroyed() &&
                Actor.GetWorld() == _World && Actor.GetOwner() == this)
            { Actor.DestroyActor(); }
        }
        FixtureActors.Empty();
        RestoreProvider();
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        Snapshot.Phase = 4; Snapshot.Replacing = -1;
        if (Snapshot.Sequence < 9223372036854775806) { Snapshot.Sequence++; }
        ForceNetUpdate();
        CompleteRequest(false);
        ck::Trace(f"VoxelNav authority bank closed: {Reason}");
    }

    private FVector FixtureLocation(int32 Index) const
    { return Index == 0 ? FVector::ZeroVector : FVector(0.0, -400.0, 600.0); }

    private bool IsExactFixture(AActor Actor, int32 Index, bool RequireReplicated)
    {
        if (Index < 0 || Index > 1 || ck::Is_NOT_Valid(Actor) ||
            Actor.IsActorBeingDestroyed() || Actor.GetWorld() != GetWorld() ||
            Actor.GetOwner() != this ||
            (Index == 0 && Actor.GetClass() != ACk_VoxelNavGym_SharedFloor) ||
            (Index == 1 && Actor.GetClass() != ACk_VoxelNavGym_SharedWall) ||
            !UCk_CameraGym_LocalAdmission_UE::Get_IsReplicatingMovement(Actor) ||
            (RequireReplicated &&
                !UCk_CameraGym_LocalAdmission_UE::Get_HasReplicationDelivery(Actor)))
        { return false; }
        auto Pose = Actor.GetActorTransform();
        if (!Pose.IsValid() || Pose.ContainsNaN()) { return false; }
        auto Scale = Index == 0
            ? FVector(30.0, 30.0, 0.5) : FVector(2.0, 9.0, 12.0);
        if ((Pose.GetLocation() - FixtureLocation(Index)).SizeSquared() > 1.0 ||
            (Pose.GetScale3D() - Scale).SizeSquared() > 0.0001 ||
            !Pose.GetRotation().Equals(FQuat::Identity, 0.001))
        { return false; }
        if (Index == 1)
        {
            auto Wall = Cast<ACk_VoxelNavGym_SharedWall>(Actor);
            if (ck::Is_NOT_Valid(Wall) || ck::Is_NOT_Valid(Wall.StaticMeshComponent))
            { return false; }
        }
        return true;
    }

    private bool SpawnFixtures()
    {
        if (!HasAuthority() || !_Fixtures.IsEmpty() || !TryCapture()) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            TSubclassOf<AActor> FixtureClass = ACk_VoxelNavGym_SharedFloor;
            if (I == 1) { FixtureClass = ACk_VoxelNavGym_SharedWall; }
            auto Actor = SpawnActor(FixtureClass, FixtureLocation(I),
                FRotator::ZeroRotator, NAME_None, true);
            if (ck::Is_NOT_Valid(Actor)) { return false; }
            _Fixtures.Add(Actor);
            Actor.SetOwner(this);
            FinishSpawningActor(Actor);
            if (!IsExactFixture(Actor, I, false)) { return false; }
        }
        if (utils_jolt_static_world::Request_BakeActor(_Fixtures[1]) <= 0)
        { return false; }
        for (auto Actor : _Fixtures) { Actor.SetReplicates(true); }
        for (int32 I = 0; I < 2; ++I)
        { if (!IsExactFixture(_Fixtures[I], I, true)) { return false; } }
        FixtureActors = _Fixtures;
        ForceNetUpdate();
        utils_nav::Request_NavigationRebuild_ForTesting(_WorldRoot);
        return true;
    }

    UFUNCTION(BlueprintPure) bool Get_AreFixturesReady()
    {
        if (_Failed || SourceFailed || _Ending || !TryCapture()) { return false; }
        if (FixtureActors.Num() != 2)
        {
            if (_FixturesAccepted) { Request_Fail("VoxelNav fixture bank changed"); }
            return false;
        }
        for (int32 I = 0; I < 2; ++I)
        {
            if (!IsExactFixture(FixtureActors[I], I, true) ||
                (_FixturesAccepted && FixtureActors[I] != _AcceptedFixtures[I]))
            {
                if (_FixturesAccepted) { Request_Fail("VoxelNav fixture identity changed"); }
                return false;
            }
        }
        if (!_FixturesAccepted)
        { _AcceptedFixtures = FixtureActors; _FixturesAccepted = true; }
        return true;
    }

    private bool HasNavigationSurface()
    {
        if (!HasAuthority() || !Get_AreFixturesReady()) { return false; }
        auto Health = utils_nav_surface::Get_ProviderHealth();
        if (Health != ECk_NavSurface_ProviderHealth::Ready)
        { _NavigationReason = f"provider={Health}"; return false; }
        auto Query = FCk_NavSurface_ProjectionQuery(FVector(-1000.0, 400.0, 0.0));
        Query.Set_SearchHalfExtents(FVector(200.0, 200.0, 250.0));
        auto Result = utils_nav_surface::Try_ProjectPoint(Query);
        if (Result.Get_Status() != ECk_NavSurface_QueryStatus::Success ||
            Result.Get_Location().ContainsNaN() ||
            Math::Abs(Result.Get_Location().Z) > 30.0)
        {
            _NavigationReason = f"start status={Result.Get_Status()} location={Result.Get_Location()}";
            return false;
        }
        _NavigationReason = "ready";
        return true;
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Ending || _Failed || _Phase != 0 ||
            !TryCapture() || CkGym_Admission::Get_IsBusy(_WorldRoot)) { return; }
        _Phase = 1;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        _ProviderBefore = utils_nav_surface::Get_Provider();
        utils_nav_surface::Request_SetProvider(ECk_NavSurface_Provider::Recast);
        _ProviderSelected = true;
        if (utils_nav_surface::Get_Provider() != ECk_NavSurface_Provider::Recast ||
            !SpawnFixtures())
        { Request_Fail("VoxelNav Recast/fixture admission failed"); }
    }

    private bool SpawnSource()
    {
        if (!HasAuthority() || !TryCapture() || !Get_AreFixturesReady() ||
            !utils_entity_lifetime::Get_CanCreateEntity(_WorldRoot))
        { return false; }
        _PendingRoot = utils_entity_script_with_actor::Request_SpawnEntityScript_OnActor(
            this, UCk_EntityScript_VoxelNavGym_WorldRoot);
        if (!utils_pending_entity_script::Get_IsValid(_PendingRoot)) { return false; }
        _ReplicationRoot = _PendingRoot.Get_EntityUnderConstruction();
        utils_pending_entity_script::Promise_OnConstructed(_PendingRoot,
            FCk_Delegate_EntityScript_Constructed(this, n"OnReplicationRootConstructed"));
        return UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(_ReplicationRoot, _World);
    }

    UFUNCTION()
    private void OnReplicationRootConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (_Failed || _Ending) { return; }
        if (!HasAuthority() || FCk_Handle(InHandle) != _ReplicationRoot ||
            utils_owning_actor::TryGet_ActorEntityHandle(this) != _ReplicationRoot ||
            !UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(_ReplicationRoot, _World))
        { Request_Fail("VoxelNav replicated root construction failed"); return; }
        _Pending = utils_entity_script::Request_SpawnEntity(_ReplicationRoot,
            UCk_EntityScript_VoxelNavGym_FlyingVsGrounded_Source, FInstancedStruct());
        if (!utils_pending_entity_script::Get_IsValid(_Pending))
        { Request_Fail("VoxelNav replicated source rejected"); return; }
        _SourceRoot = _Pending.Get_EntityUnderConstruction();
        if (!HasSourceRoot())
        { Request_Fail("VoxelNav source ownership failed"); }
    }

    private bool BindSource()
    {
        if (ck::Is_NOT_Valid(_SourceRoot) &&
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(_ReplicationRoot, _World))
        { return false; }
        if (!HasSourceRoot())
        { Request_Fail("VoxelNav source root lost"); return false; }
        auto Source = Cast<UCk_EntityScript_VoxelNavGym_FlyingVsGrounded_Source>(
            utils_entity_script::TryGet_EntityScript(_SourceRoot));
        if (ck::Is_NOT_Valid(Source)) { return false; }
        if (Source.GetClass() != UCk_EntityScript_VoxelNavGym_FlyingVsGrounded_Source ||
            (ck::IsValid(_Source) && Source != _Source) ||
            Source.Get_SourceRoot() != _SourceRoot || Source.Get_IsClosed())
        { Request_Fail("VoxelNav source identity or preparation failed"); return false; }
        _Source = Source;
        if (!Source.Request_Prepare())
        { Request_Fail("VoxelNav agent preparation failed"); return false; }
        return Source.Get_IsPrepared();
    }

    private bool HasRequestOwner()
    {
        if (_RequestSerial == 0) { return true; }
        return ck::IsValid(_Requester) && !_Requester.IsActorBeingDestroyed() &&
            _Requester.GetWorld() == _World && _Requester.HasAuthority() &&
            _Requester.GetClass() ==
                ACk_VoxelNavGym_FlyingVsGrounded_PlayerController &&
            ck::IsValid(_RequesterPawn) && !_RequesterPawn.IsActorBeingDestroyed() &&
            _RequesterPawn.GetWorld() == _World &&
            _Requester.GetControlledPawn() == _RequesterPawn &&
            _RequesterPawn.GetController() == _Requester;
    }

    bool Request_Control(
        ACk_VoxelNavGym_FlyingVsGrounded_PlayerController Requester,
        int32 Serial, int64 SourceGeneration, int64 Generation, int32 Action)
    {
        if (!HasAuthority() || _Failed || _Ending || _Phase != 3 ||
            !TryCapture() || !Get_AreFixturesReady() || !HasExactSource() ||
            !_Source.Get_IsPrepared() || !_Source.Get_IsStarted() ||
            !CkVoxelNavGym::IsValidFrame(Snapshot) || Snapshot.Phase != 2 ||
            ck::Is_NOT_Valid(Requester) || Requester.IsActorBeingDestroyed() ||
            Requester.GetWorld() != _World || !Requester.HasAuthority() ||
            Requester.GetClass() !=
                ACk_VoxelNavGym_FlyingVsGrounded_PlayerController ||
            Serial <= 0 || _RequestSerial != 0 || SourceGeneration != 1 ||
            Action < 0 || Action > 1 || Generation <= 0 ||
            Generation != Snapshot.ControlGeneration ||
            Generation >= 9223372036854775806 ||
            Snapshot.Sequence >= 9223372036854775806)
        { return false; }
        auto Pawn = Requester.GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetWorld() != _World || Pawn.GetController() != Requester)
        { return false; }
        auto Count = _Source.Get_ControlCompletionCount();
        auto PairGeneration = _Source.Get_PairGeneration();
        if (Count < 0 || Count >= 2147483647 || PairGeneration <= 0 ||
            PairGeneration >= 9223372036854775806 ||
            Snapshot.Stations[0].ControlCount != Count ||
            Snapshot.Stations[0].PairGeneration != PairGeneration)
        { return false; }
        _Requester = Requester; _RequesterPawn = Pawn; _RequestSerial = Serial;
        _RequestAction = Action; _RequestedGeneration = Generation;
        _BeforeControlCount = Count; _BeforePairGeneration = PairGeneration;
        _BeforeGoalsOutbound = Snapshot.Stations[0].GoalsAreOutbound;
        _Phase = 4;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        Snapshot.Phase = 1; Snapshot.Replacing = 0; Snapshot.Sequence++;
        ForceNetUpdate();
        auto Accepted = Action == 0
            ? _Source.Request_RestartPair() : _Source.Request_ReverseGoals();
        if (!Accepted)
        {
            if (_Source.Get_IsClosed())
            { Request_Fail("VoxelNav control partially failed"); }
            else
            {
                _Phase = 3;
                Snapshot.Phase = 2; Snapshot.Replacing = -1; Snapshot.Sequence++;
                ForceNetUpdate();
                CompleteRequest(false);
            }
            return false;
        }
        return true;
    }

    private void CompleteRequest(bool Success)
    {
        auto Requester = _Requester;
        int32 Serial = _RequestSerial; int32 Action = _RequestAction;
        int64 Generation = _RequestedGeneration;
        _Requester = nullptr; _RequesterPawn = nullptr; _RequestSerial = 0;
        _RequestAction = -1; _RequestedGeneration = 0;
        if (Serial > 0 && ck::IsValid(Requester) && Requester.GetWorld() == _World)
        {
            Requester.Complete_VoxelControl(Serial, Generation, Action, Success,
                Success ? Snapshot.ControlGeneration : 0,
                Success ? Snapshot.Sequence : 0);
        }
    }

    private bool PublishComplete(bool CompleteControl)
    {
        if (!TryCapture() || !Get_AreFixturesReady() ||
            !HasRequestOwner() || !HasExactSource() ||
            !_Source.Get_IsPrepared() || !_Source.Get_IsBuilt() ||
            !_Source.Get_IsStarted() ||
            Snapshot.Sequence >= 9223372036854775806 ||
            (CompleteControl && Snapshot.ControlGeneration >= 9223372036854775806))
        { Request_Fail("VoxelNav publication lost its bank, world or owner"); return false; }
        FCkVoxelNavGym_StationFrame Station;
        if (!_Source.TryGet_Frame(Station))
        { Request_Fail("VoxelNav source stopped producing valid values"); return false; }
        if (CompleteControl)
        {
            if (Station.ControlCount != _BeforeControlCount + 1 ||
                Station.PairGeneration !=
                    _BeforePairGeneration + (_RequestAction == 0 ? 1 : 0) ||
                Station.GoalsAreOutbound !=
                    (_RequestAction == 1 ? !_BeforeGoalsOutbound : _BeforeGoalsOutbound))
            { Request_Fail("VoxelNav control source receipt disagreed"); return false; }
        }
        else if (_Phase == 2 && (Station.ControlCount != 0 ||
            Station.PairGeneration != 1 || !Station.GoalsAreOutbound))
        { Request_Fail("VoxelNav initial source receipt disagreed"); return false; }
        else if (!CompleteControl && _Phase == 3 &&
            (Snapshot.Stations.Num() != 1 ||
                Station.ControlCount != Snapshot.Stations[0].ControlCount ||
                Station.PairGeneration != Snapshot.Stations[0].PairGeneration ||
                Station.GoalsAreOutbound != Snapshot.Stations[0].GoalsAreOutbound))
        { Request_Fail("VoxelNav source changed without an owned control"); return false; }
        FCkVoxelNavGym_WireSnapshot Frame;
        Frame.SourceGeneration = 1;
        Frame.ControlGeneration = Snapshot.ControlGeneration +
            (CompleteControl ? 1 : 0);
        Frame.Sequence = Snapshot.Sequence + 1;
        Frame.Phase = 2; Frame.Replacing = -1;
        Frame.Stations.Add(Station);
        if (!CkVoxelNavGym::IsValidFrame(Frame))
        { Request_Fail("Malformed VoxelNav source values"); return false; }
        Snapshot = Frame; _PublishElapsed = 0.0; ForceNetUpdate();
        return true;
    }

    private bool ConsumeSnapshot()
    {
        if (_Failed || _Ending || !TryCapture() || Snapshot.Sequence == 0)
        { return false; }
        if (!CkVoxelNavGym::IsValidFrame(Snapshot))
        { Request_CloseClient("Malformed VoxelNav frame"); return false; }
        if (_HasConsumed)
        {
            if (Snapshot.SourceGeneration != _Consumed.SourceGeneration ||
                Snapshot.Sequence < _Consumed.Sequence ||
                Snapshot.ControlGeneration < _Consumed.ControlGeneration)
            { Request_CloseClient("VoxelNav frame moved backward"); return false; }
            if (Snapshot.Sequence == _Consumed.Sequence)
            {
                if (!UCk_VoxelNavGym_Runtime_UE::Get_IsSameFrame(
                        Snapshot, _Consumed))
                { Request_CloseClient("VoxelNav values changed without a new sequence"); return false; }
                return Snapshot.Phase == 1 || Snapshot.Phase == 2;
            }
            if (Snapshot.Phase != 4 && _Consumed.Phase != 4 &&
                Snapshot.Stations[0].ControlCount <
                    _Consumed.Stations[0].ControlCount)
            { Request_CloseClient("VoxelNav control count moved backward"); return false; }
        }
        _Consumed = Snapshot; _HasConsumed = true;
        if (Snapshot.Phase == 4 || SourceFailed)
        { Request_CloseClient("Authority closed the VoxelNav bank"); return false; }
        return Snapshot.Phase == 1 || Snapshot.Phase == 2;
    }

    bool Get_HasConsumableFrame()
    {
        if (_Failed || SourceFailed || _Ending) { return false; }
        if (!TryCapture()) { return false; }
        if (!Get_AreFixturesReady()) { return false; }
        if (HasAuthority())
        { return CkVoxelNavGym::IsValidFrame(Snapshot) &&
            (Snapshot.Phase == 1 || Snapshot.Phase == 2); }
        return ConsumeSnapshot();
    }

    FCkVoxelNavGym_WireSnapshot Get_ConsumedFrame()
    {
        if (!Get_HasConsumableFrame())
        { return FCkVoxelNavGym_WireSnapshot(); }
        return HasAuthority() ? Snapshot : _Consumed;
    }

    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    {
        if (_Failed || _Ending) { return; }
        if (!_Captured)
        { if (!HasAuthority()) { ConsumeSnapshot(); } return; }
        if (!TryCapture())
        { Request_Fail("Captured VoxelNav world or catalog changed"); return; }
        if (!HasAuthority())
        { ConsumeSnapshot(); return; }
        if (!HasRequestOwner())
        { Request_Fail("VoxelNav requester changed"); return; }
        if (_Phase == 0) { return; }
        if (!_ProviderSelected ||
            utils_nav_surface::Get_Provider() != ECk_NavSurface_Provider::Recast ||
            !Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0 ||
            !Get_AreFixturesReady())
        { Request_Fail("VoxelNav provider, fixture or elapsed time invalid"); return; }
        auto Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (_Phase != 3 && Now - _PendingSince >= 30.0)
        { Request_Fail("VoxelNav admission or control timed out"); return; }
        if (_Phase == 1)
        {
            if (!HasNavigationSurface()) { return; }
            if (!SpawnSource())
            { Request_Fail("VoxelNav source spawn rejected"); return; }
            _Phase = 2; _PendingSince = Now;
            return;
        }
        if (_Phase == 2)
        {
            if (!BindSource()) { return; }
            if (!_Source.Get_IsBuilt()) { return; }
            if (!_StartRequested)
            {
                if (!_Source.Request_StartPrepared())
                { Request_Fail("VoxelNav source start rejected"); return; }
                _StartRequested = true;
            }
            if (!_Source.Request_UpdateControl())
            { Request_Fail("VoxelNav initial move failed"); return; }
            if (!_Source.Get_IsStarted()) { return; }
            if (PublishComplete(false))
            { _Phase = 3; _PendingSince = Now; }
            return;
        }
        if (_Phase == 3)
        {
            if (!HasExactSource() || !_Source.Get_IsPrepared() ||
                !_Source.Get_IsStarted())
            { Request_Fail("VoxelNav active source lost"); return; }
            _PublishElapsed += DeltaSeconds;
            if (_PublishElapsed >= 0.1)
            {
                if (PublishComplete(false)) { _PendingSince = Now; }
            }
            return;
        }
        if (_Phase == 4)
        {
            if (!HasExactSource() || !_Source.Request_UpdateControl())
            { Request_Fail("VoxelNav control source failed"); return; }
            if (_Source.Get_ControlCompletionCount() ==
                _BeforeControlCount + 1)
            {
                if (PublishComplete(true))
                {
                    _Phase = 3; _PendingSince = Now;
                    CompleteRequest(true);
                }
            }
            else if (_Source.Get_ControlCompletionCount() != _BeforeControlCount)
            { Request_Fail("VoxelNav control count diverged"); }
        }
    }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        if (HasAuthority()) { Request_Fail("VoxelNav gym session ended"); }
        RestoreProvider();
        _Ending = true;
    }
}
