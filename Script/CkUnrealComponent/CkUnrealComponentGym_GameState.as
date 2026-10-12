// Language=angelscript

// One fixed source cohort for this world. Controllers own presentation only.
class ACk_UnrealComponentGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkUnrealComponentGym_Snapshot Snapshot;
    UPROPERTY() TArray<ACk_UnrealComponentGym_Driver> ServerDrivers;
    UPROPERTY() FCk_Handle SourceOwner;
    UPROPERTY() int32 ServerRoleCount = 0;
    private UWorld _SourceWorld;
    private FCk_Handle _WorldRoot;
    private FCkGym_Entry _CatalogEntry;
    private int32 _CatalogIndex = -1;
    private bool _Attempted = false;
    private bool _Creating = false;
    private bool _Mutating = false;
    private bool _BoundaryIssued = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private int32 _PreviousGeneration = 0;
    private int64 _FrameSerial = 0;
    private double _BoundaryStartedAt = 0.0;
    private double _LastPublishedAt = -1.0;

    private bool Get_HasWorldIdentity()
    {
        if (_Ending || IsActorBeingDestroyed() || !HasAuthority() ||
            !Get_HasPublishedCatalog() || GetClass() != ACk_UnrealComponentGym_GameState)
        { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != this ||
            (_Attempted && World != _SourceWorld)) { return false; }
        auto Mode = Gameplay::GetGameMode();
        if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
            Mode.GetClass() != ACk_UnrealComponentGym_GameMode) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Unreal Component" ||
            Entry.GameModeClass != ACk_UnrealComponentGym_GameMode) { return false; }
        return _CatalogIndex < 0 || (_CatalogIndex == Index &&
            _CatalogEntry.DisplayName == Entry.DisplayName &&
            _CatalogEntry.GameModeClass == Entry.GameModeClass &&
            _CatalogEntry.LevelName == Entry.LevelName &&
            _CatalogEntry.Category == Entry.Category &&
            _CatalogEntry.HintCode == Entry.HintCode);
    }

    private bool Get_HasSourceOwner()
    {
        if (ck::Is_NOT_Valid(_WorldRoot) || _WorldRoot != ck::TransientEntity() ||
            CkGym_Admission::Get_IsClosedUnproven(_WorldRoot) ||
            ck::Is_NOT_Valid(SourceOwner) ||
            !utils_entity_lifetime::Has_LifetimeOwner(SourceOwner) ||
            utils_entity_lifetime::Get_LifetimeOwner(SourceOwner) != _WorldRoot ||
            utils_entity_lifetime::Get_IsPendingDestroy(SourceOwner,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy)) { return false; }
        return true;
    }

    bool Get_IsSourceContext(ACk_UnrealComponentGym_Driver Driver, int32 Role,
        int32 Generation, FCk_Handle Owner)
    {
        if (_Failed || !Get_HasWorldIdentity() || !Get_HasSourceOwner() ||
            Role < 0 || Role >= 8 || ServerDrivers.Num() != 8 ||
            ck::Is_NOT_Valid(Driver) || Driver.IsActorBeingDestroyed() ||
            Driver.GetWorld() != _SourceWorld || !Driver.HasAuthority() ||
            ServerDrivers[Role] != Driver ||
            Owner != SourceOwner || Generation <= 0) { return false; }
        if (Generation == Snapshot.Generation) { return true; }
        return Snapshot.Lifecycle == ECkUnrealComponentGym_Lifecycle::Initializing &&
            Generation == _PreviousGeneration && _PreviousGeneration > 0;
    }

    UFUNCTION(BlueprintPure)
    bool Get_CanAnimateSource()
    {
        return !_Failed && !_Creating && !_Mutating && Get_HasWorldIdentity() &&
            Get_HasSourceOwner() &&
            Snapshot.Lifecycle == ECkUnrealComponentGym_Lifecycle::Active;
    }

    void Request_Fail(FString Reason)
    {
        if (_Failed || _Ending || !HasAuthority()) { return; }
        // Close admission before callbacks or any component/timer mutation.
        _Failed = true;
        _Creating = false;
        _Mutating = false;
        auto WorldRoot = _WorldRoot;
        if (ck::IsValid(WorldRoot) && WorldRoot == ck::TransientEntity())
        { CkGym_Admission::TryCloseWorldUnproven(WorldRoot); }
        if (Snapshot.Sequence >= 9223372036854775807) { return; }
        FCkUnrealComponentGym_Snapshot Next;
        Next.Generation = Math::Max(1, Snapshot.Generation);
        Next.Sequence = Snapshot.Sequence + 1;
        Next.Lifecycle = ECkUnrealComponentGym_Lifecycle::Failed;
        Next.FailureReason = Reason.IsEmpty() ? "UnrealComponent source failed" : Reason;
        Snapshot = Next;
        if (ck::IsValid(GetWorld()) && !IsActorBeingDestroyed()) { ForceNetUpdate(); }
        // Keep exact source roles, pending targets and components until teardown.
    }

    private bool TryPublish(FCkUnrealComponentGym_Snapshot Next)
    {
        if (_Failed || !Get_HasWorldIdentity() ||
            !CkUnrealComponentGymShared::Get_IsValidPublicationAdvance(Snapshot, Next))
        { return false; }
        Snapshot = Next;
        ForceNetUpdate();
        return !_Failed && Get_HasWorldIdentity();
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Attempted || _Failed || !Get_HasWorldIdentity())
        { return; }
        _SourceWorld = GetWorld();
        _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        _Attempted = true;
        _Creating = true;
        _BoundaryStartedAt = System::GetGameTimeInSeconds();
        _WorldRoot = ck::TransientEntity();
        if (ck::Is_NOT_Valid(_WorldRoot) ||
            CkGym_Admission::Get_IsClosedUnproven(_WorldRoot) ||
            !utils_entity_lifetime::Get_CanCreateEntity(_WorldRoot))
        { Request_Fail("Authority ECS world unavailable"); return; }
        FCkUnrealComponentGym_Snapshot Initial;
        Initial.Generation = 1;
        Initial.Sequence = 1;
        if (!TryPublish(Initial)) { Request_Fail("Initial publication rejected"); return; }
        SourceOwner = utils_entity_lifetime::Request_CreateEntity(_WorldRoot);
        if (!Get_HasSourceOwner()) { Request_Fail("Source owner rejected"); return; }
        ServerDrivers.SetNum(8);
        for (int32 Role = 0; Role < 8; ++Role)
        {
            FVector Origin;
            if (_Failed || !Get_HasWorldIdentity() || !Get_HasSourceOwner() ||
                !CkUnrealComponentGymCatalog::TryGet_Origin(Role, Origin))
            { Request_Fail("Source role context rejected"); return; }
            auto Driver = SpawnActor(ACk_UnrealComponentGym_Driver,
                Origin, FRotator(0.0, 180.0, 0.0), NAME_None, true);
            // Retain the first returned actor before any remaining setup.
            ServerDrivers[Role] = Driver;
            if (_Failed || ck::Is_NOT_Valid(Driver) || !Get_HasWorldIdentity() ||
                !Get_HasSourceOwner()) { Request_Fail("Source actor spawn rejected"); return; }
            Driver.ComponentType = ECk_UnrealComponentGym_Type(Role);
            Driver.SourceState = this;
            Driver.SourceRole = Role;
            Driver.SourceGeneration = Snapshot.Generation;
            Driver.SourceOwner = SourceOwner;
            FinishSpawningActor(Driver);
            if (_Failed || !Get_IsSourceContext(Driver, Role, Snapshot.Generation, SourceOwner))
            { Request_Fail("Source actor setup changed context"); return; }
            ServerRoleCount += 1;
        }
        _Creating = false;
    }

    private bool Get_HasCompleteRoles()
    {
        if (_Failed || _Creating || !Get_HasWorldIdentity() || !Get_HasSourceOwner() ||
            ServerDrivers.Num() != 8 || ServerRoleCount != 8) { return false; }
        for (int32 Role = 0; Role < 8; ++Role)
        {
            auto Driver = ServerDrivers[Role];
            if (ck::Is_NOT_Valid(Driver)) { return false; }
            if (!Get_IsSourceContext(Driver, Role, Driver.SourceGeneration, SourceOwner) ||
                !Driver.Get_HasValidComposition()) { return false; }
            for (int32 Other = 0; Other < Role; ++Other)
            { if (Driver == ServerDrivers[Other]) { return false; } }
        }
        return true;
    }

    private bool IssueBoundary()
    {
        if (_BoundaryIssued || !Get_HasCompleteRoles()) { return false; }
        _Mutating = true;
        // Attempt exactly once even if a callback invalidates a later role.
        _BoundaryIssued = true;
        for (int32 Role = 0; Role < 8; ++Role)
        {
            auto Driver = ServerDrivers[Role];
            if (_Failed || !Get_HasWorldIdentity() || !Get_HasSourceOwner() ||
                ck::Is_NOT_Valid(Driver))
            { Request_Fail("Complete phase reset context lost"); return false; }
            if (!Get_IsSourceContext(Driver, Role, Driver.SourceGeneration, SourceOwner) ||
                !Driver.Request_PrepareReset(Snapshot.Generation))
            { Request_Fail("Complete phase reset rejected"); return false; }
        }
        _Mutating = false;
        return !_Failed;
    }

    bool Request_ResetSharedDemo()
    {
        if (!HasAuthority() || _Failed || _Mutating || _Creating ||
            Snapshot.Lifecycle != ECkUnrealComponentGym_Lifecycle::Active ||
            Snapshot.Generation >= 2147483647 ||
            Snapshot.Sequence >= 9223372036854775806) { return false; }
        if (!Get_HasCompleteRoles()) { Request_Fail("Reset source dependency lost"); return false; }
        _PreviousGeneration = Snapshot.Generation;
        FCkUnrealComponentGym_Snapshot Initial;
        Initial.Generation = Snapshot.Generation + 1;
        Initial.Sequence = Snapshot.Sequence + 1;
        if (!TryPublish(Initial)) { Request_Fail("Reset publication rejected"); return false; }
        _BoundaryIssued = false;
        _BoundaryStartedAt = System::GetGameTimeInSeconds();
        return IssueBoundary();
    }

    private bool TryCollectFrame(FCkUnrealComponentGym_Snapshot&out Next, bool Boundary)
    {
        Next = FCkUnrealComponentGym_Snapshot();
        if (!Get_HasCompleteRoles()) { return false; }
        Next.Generation = Snapshot.Generation;
        Next.Sequence = Snapshot.Sequence + 1;
        Next.FrameSerial = _FrameSerial + 1;
        Next.Lifecycle = ECkUnrealComponentGym_Lifecycle::Active;
        for (int32 Role = 0; Role < 8; ++Role)
        {
            auto Driver = ServerDrivers[Role];
            FCkUnrealComponentGym_SourceSample Sample;
            if (!Driver.TryGet_PhysicalSample(Sample) || !Sample.bStyleValid ||
                Sample.Role != Role) { return false; }
            if (Boundary && (!Sample.bTransformQueueEmpty ||
                !Sample.ComponentWorldPose.Equals(Driver.Get_ResetPose(), 0.01)))
            { return false; }
            Next.ComponentWorldPoses.Add(Sample.ComponentWorldPose);
            Next.Bodies.Add(f"Component class: {Sample.ActualComponentClass}\n"
                + f"OnAdded fired: {Driver.OnAddedFireCount}x\n"
                + "Component ready: true\nComponent ptr valid: true\nOwningEntity valid: true\n\n"
                + "Component location:\n"
                + f"  X: {Sample.ComponentWorldPose.Location.X}\n"
                + f"  Y: {Sample.ComponentWorldPose.Location.Y}\n"
                + f"  Z: {Sample.ComponentWorldPose.Location.Z}");
            Next.Instructions.Add("Shared authority animates the ECS entity.\n"
                + "PushTransform drives the hosted component.\n"
                + "Each player views the same published component motion.\n"
                + "R restarts the same eight components.");
            if (_Failed || !Get_HasWorldIdentity()) { return false; }
        }
        return CkUnrealComponentGymShared::Get_IsValidSnapshot(Next);
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!HasAuthority() || _Ending || _Failed) { return; }
        if (!_Attempted)
        {
            Request_EnsureSharedDemo();
            return;
        }
        if (!Get_HasWorldIdentity() || !Get_HasSourceOwner())
        { Request_Fail("Authority source world changed"); return; }
        if (_Creating || _Mutating) { return; }
        if (Snapshot.Generation >= 2147483647 || Snapshot.Sequence >= 9223372036854775806 ||
            _FrameSerial >= 9223372036854775806)
        { Request_Fail("Source counters exhausted"); return; }
        if (Snapshot.Lifecycle == ECkUnrealComponentGym_Lifecycle::Initializing)
        {
            if (System::GetGameTimeInSeconds() - _BoundaryStartedAt > 20.0)
            { Request_Fail("Complete source phase readiness timed out"); return; }
            if (!_BoundaryIssued)
            {
                if (!Get_HasCompleteRoles()) { return; }
                if (!IssueBoundary()) { return; }
            }
            FCkUnrealComponentGym_Snapshot Next;
            if (!TryCollectFrame(Next, true)) { return; }
            if (!TryPublish(Next)) { Request_Fail("Source phase publication rejected"); return; }
            _FrameSerial = Next.FrameSerial;
            _PreviousGeneration = 0;
            _LastPublishedAt = System::GetGameTimeInSeconds();
            return;
        }
        if (Snapshot.Lifecycle != ECkUnrealComponentGym_Lifecycle::Active)
        { Request_Fail("Source lifecycle rejected"); return; }
        if (!Get_HasCompleteRoles()) { Request_Fail("Admitted source role lost"); return; }
        if (System::GetGameTimeInSeconds() - _LastPublishedAt < 0.1) { return; }
        FCkUnrealComponentGym_Snapshot Next;
        if (!TryCollectFrame(Next, false)) { Request_Fail("Source sample rejected"); return; }
        if (!TryPublish(Next)) { Request_Fail("Source frame publication rejected"); return; }
        _FrameSerial = Next.FrameSerial;
        _LastPublishedAt = System::GetGameTimeInSeconds();
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        _Creating = false;
        _Mutating = false;
        // World teardown owns these retained actors and ECS allocations.
    }
}
