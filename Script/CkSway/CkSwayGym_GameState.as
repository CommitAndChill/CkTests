// Language=angelscript

// GameState owns the replicated station trees. Only authority simulates Sway;
// Head/Cube production Transforms drive each world's component presentation.
UCLASS()
class ACk_SwayGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkSwayGym_Snapshot Snapshot;
    UPROPERTY() private UCk_SwayGym_SourceReceipt_UE _Source;
    UPROPERTY() private UCk_SwayGym_SourceReceipt_UE _PreviousComplete;
    private FCk_Handle _OwnerRoot;
    private FCk_Handle_PendingEntityScript _PendingRoot;
    private bool _RootRequested = false;
    private bool _RootReady = false;
    private double _RootStartedAt = 0.0;
    private TArray<FCkSwayGym_Fixture> _Fixtures;
    private bool _Armed = false;
    private bool _GenerationCreating = false;
    private bool _SourceReceiptAttempted = false;
    private bool _ClientBound = false;
    private bool _Retiring = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private FString _LocalFailureReason;
    private double _CreateStartedAt = 0.0;
    private double _ClientWaitStartedAt = 0.0;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;

    private bool Request_EnsureOwnerRoot()
    {
        if (_Ending || _Failed || !Get_HasWorldIdentity()) { return false; }
        if (!_RootRequested)
        {
            _RootRequested = true;
            _RootStartedAt = System::GetGameTimeInSeconds();
            if (HasAuthority())
            {
                _PendingRoot = utils_entity_script_with_actor::Request_SpawnEntityScript_OnActor(
                    this, UCk_EntityScript_SwayGym_WorldRoot);
                if (!utils_pending_entity_script::Get_IsValid(_PendingRoot))
                { Request_Fail("Sway owner bridge rejected"); return false; }
                _OwnerRoot = _PendingRoot.Get_EntityUnderConstruction();
                utils_pending_entity_script::Promise_OnConstructed(_PendingRoot,
                    FCk_Delegate_EntityScript_Constructed(this, n"OnOwnerRootConstructed"));
            }
        }
        if (!HasAuthority() && !_RootReady)
        {
            // WithActor spawning is authority-only. The replicated driver
            // constructs the associated client bridge through production metadata.
            auto Candidate = utils_owning_actor::TryGet_ActorEntityHandle(this);
            if (ck::IsValid(Candidate) && utils_entity_script::Has(Candidate) &&
                utils_net::Has(Candidate) && utils_entity_replication_driver::Has(Candidate))
            {
                auto Script = Cast<UCk_EntityScript_SwayGym_WorldRoot>(
                    utils_entity_script::TryGet_EntityScript(Candidate));
                if (ck::IsValid(Script))
                { _OwnerRoot = Candidate; _RootReady = true; }
            }
        }
        if (!_RootReady)
        {
            if (System::GetGameTimeInSeconds() - _RootStartedAt > 20.0)
            { Request_Fail("Sway owner bridge construction timed out"); }
            return false;
        }
        if (ck::Is_NOT_Valid(_OwnerRoot) ||
            utils_owning_actor::TryGet_ActorEntityHandle(this) != _OwnerRoot ||
            !utils_net::Has(_OwnerRoot) ||
            utils_net::Get_Replication(_OwnerRoot) != ECk_Replication::Replicates ||
            (HasAuthority() && !utils_entity_replication_driver::Has(_OwnerRoot)))
        { Request_Fail("Sway owner bridge lost"); return false; }
        return true;
    }

    UFUNCTION()
    private void OnOwnerRootConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (_Ending || _Failed) { return; }
        if (FCk_Handle(InHandle) != _OwnerRoot || ck::Is_NOT_Valid(_OwnerRoot) ||
            utils_owning_actor::TryGet_ActorEntityHandle(this) != _OwnerRoot)
        { Request_Fail("Sway owner bridge construction failed"); return; }
        _RootReady = true;
    }

    private FCk_Handle Get_ReplicatedNode(ECkSwayGym_NodeRole Role,
        int32 Station, int32 Index)
    {
        if (_Ending || _Failed || Snapshot.Generation <= 0 ||
            Snapshot.Lifecycle == ECkSwayGym_Lifecycle::Failed || !_RootReady ||
            ck::Is_NOT_Valid(_OwnerRoot) || !Get_HasWorldIdentity()) { return FCk_Handle(); }
        auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(),
            FName(f"SwayGym_G{Snapshot.Generation}_Role{int32(Role)}_Station{Station}_Index{Index}"));
        if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]) ||
            !utils_transform::Has(Matches[0]) || !utils_entity_script::Has(Matches[0]) ||
            utils_entity_lifetime::Get_WorldForEntity(Matches[0]) != GetWorld() ||
            utils_owning_actor::TryGet_EntityOwningActor_Recursive(Matches[0]) != this)
        { return FCk_Handle(); }
        auto Script = Cast<UCk_EntityScript_SwayGym_Node>(utils_entity_script::TryGet_EntityScript(Matches[0]));
        if (ck::Is_NOT_Valid(Script) || !Script.Get_IsReady() ||
            Script.Generation != Snapshot.Generation || Script.Role != Role ||
            Script.Station != Station || Script.Index != Index) { return FCk_Handle(); }
        return Matches[0];
    }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_ReplicatedHead(int32 Station)
    {
        if (Station < 0 || Station >= 3) { return FCk_Handle(); }
        return Get_ReplicatedNode(ECkSwayGym_NodeRole::Head, Station, 0);
    }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_ReplicatedCube(int32 Slot)
    {
        if (Slot < 0 || Slot >= 7) { return FCk_Handle(); }
        const int32 Station = Slot < 2 ? 0 : Slot < 4 ? 1 : 2;
        const int32 Index = Slot < 2 ? Slot : Slot < 4 ? Slot - 2 : Slot - 4;
        return Get_ReplicatedNode(ECkSwayGym_NodeRole::Cube, Station, Index);
    }

    private bool Get_HasWorldIdentity()
    {
        if (_Ending || IsActorBeingDestroyed() || !Get_HasPublishedCatalog() ||
            GetClass() != ACk_SwayGym_GameState) { return false; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld) || CurrentWorld.GetGameState() != this) { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != CurrentWorld ||
                Mode.GetClass() != ACk_SwayGym_GameMode) { return false; }
        }
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Registry.Num()) { return false; }
        auto Entry = Registry[Index];
        if (Entry.DisplayName != "Sway" || Entry.GameModeClass != ACk_SwayGym_GameMode)
        { return false; }
        return _CatalogIndex < 0 || (_CatalogIndex == Index &&
            _CatalogEntry.DisplayName == Entry.DisplayName &&
            _CatalogEntry.GameModeClass == Entry.GameModeClass &&
            _CatalogEntry.LevelName == Entry.LevelName &&
            _CatalogEntry.Category == Entry.Category &&
            _CatalogEntry.HintCode == Entry.HintCode);
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Armed || _Failed || !Get_HasWorldIdentity())
        { return; }
        _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        _Armed = true;
    }

    private void Request_Fail(FString Reason)
    {
        if (_Failed || _Ending) { return; }
        _Failed = true;
        _LocalFailureReason = Reason.IsEmpty() ? "Sway source failed" : Reason;
        _GenerationCreating = false;
        _Retiring = false;
        if (ck::IsValid(_Source))
        { UCk_SwayGym_SourceRuntime_UE::Request_Close(_Source); }
        for (int32 I = 0; I < _Fixtures.Num(); ++I)
        { _Fixtures[I].Request_Destroy(); }
        // Retain rejected identities while retiring the entire partial cohort.
        // This world never starts a second allocation after a failed attempt.
        if (Snapshot.Generation <= 0 || Snapshot.Sequence <= 0 ||
            Snapshot.Sequence >= 9223372036854775807) { return; }
        FCkSwayGym_Snapshot Next;
        Next.Generation = Snapshot.Generation;
        Next.Sequence = Snapshot.Sequence + 1;
        Next.Lifecycle = ECkSwayGym_Lifecycle::Failed;
        Next.FailureReason = Reason.IsEmpty() ? "Sway source failed" : Reason;
        if (CkSwayGymShared::Get_IsValidPublicationAdvance(Snapshot, Next))
        {
            Snapshot = Next;
            if (ck::IsValid(GetWorld()) && !IsActorBeingDestroyed())
            { ForceNetUpdate(); }
        }
    }

    private bool TryPublish(FCkSwayGym_Snapshot Next)
    {
        if (!HasAuthority() || !Get_HasWorldIdentity() ||
            !CkSwayGymShared::Get_IsValidPublicationAdvance(Snapshot, Next))
        { return false; }
        Snapshot = Next;
        ForceNetUpdate();
        return true;
    }

    private void StartGeneration(int32 Generation)
    {
        if (!HasAuthority() || _Failed || !_Armed || _GenerationCreating ||
            _SourceReceiptAttempted || Snapshot.Generation != Generation ||
            Snapshot.Lifecycle != ECkSwayGym_Lifecycle::Initializing ||
            !Get_HasWorldIdentity()) { Request_Fail("Sway generation context rejected"); return; }
        _GenerationCreating = true;
        _CreateStartedAt = System::GetGameTimeInSeconds();
        _Fixtures.Reset();
        auto WorldRoot = ck::TransientEntity();
        if (ck::Is_NOT_Valid(WorldRoot))
        { Request_Fail("Sway authority ECS world unavailable"); return; }
        for (int32 Station = 0; Station < CkSwayGymCatalog::StationCount; ++Station)
        {
            FVector Origin;
            if (!CkSwayGymCatalog::TryGet_Origin(Station, Origin))
            { Request_Fail("Sway station origin rejected"); return; }
            _Fixtures.Add(FCkSwayGym_Fixture());
            if (!_Fixtures[Station].Create(_OwnerRoot, this, Origin, Station, Generation))
            { Request_Fail("Sway fixed source composition failed"); return; }
            if (!Get_HasWorldIdentity() || _Failed)
            { Request_Fail("Sway source callback changed context"); return; }
        }
    }

    private bool Get_AreSourceMeshesReady()
    {
        if (_Fixtures.Num() != 3) { return false; }
        int32 Count = 0;
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld)) { return false; }
        for (auto Fixture : _Fixtures)
        {
            if (!Fixture.Get_IsReady()) { return false; }
            for (auto Component : Fixture.Components)
            {
                if (ck::Is_NOT_Valid(Component) ||
                    !utils_unreal_component::Has(FCk_Handle(Component)))
                { return false; }
                auto Mesh = Cast<UStaticMeshComponent>(
                    utils_unreal_component::Get_Component(Component));
                if (ck::Is_NOT_Valid(Mesh) || Mesh.GetWorld() != CurrentWorld ||
                    ck::Is_NOT_Valid(Mesh.StaticMesh))
                { return false; }
                ++Count;
            }
        }
        return Count == 7;
    }

    private FCkSwayGym_SourceRoles Get_FrozenRoles()
    {
        FCkSwayGym_SourceRoles Roles;
        Roles.OwnerRoot = _OwnerRoot;
        if (_Fixtures.Num() != 3) { return Roles; }
        for (auto Fixture : _Fixtures)
        {
            Roles.Roots.Add(FCk_Handle(Fixture.Root));
            Roles.Heads.Add(FCk_Handle(Fixture.Head));
            for (auto Tween : Fixture.Tweens) { Roles.Tweens.Add(FCk_Handle(Tween)); }
            for (auto Sway : Fixture.Sways) { Roles.Sways.Add(FCk_Handle(Sway)); }
            for (auto Node : Fixture.CubeNodes) { Roles.CubeNodes.Add(FCk_Handle(Node)); }
            for (auto Component : Fixture.Components)
            { Roles.Components.Add(FCk_Handle(Component)); }
        }
        return Roles;
    }

    private void FinishSourceAdmission()
    {
        if (!_GenerationCreating || _SourceReceiptAttempted || _Failed ||
            !Get_HasWorldIdentity() || !Get_AreSourceMeshesReady()) { return; }
        auto Roles = Get_FrozenRoles();
        if (!UCk_SwayGym_SourceRuntime_UE::Get_IsSourceConstructionDrained(this, Roles))
        { return; }
        _SourceReceiptAttempted = true;
        auto WorldRoot = ck::TransientEntity();
        if (Snapshot.Generation == 1)
        {
            _Source = UCk_SwayGym_SourceRuntime_UE::TryBegin(
                this, 1, WorldRoot, Roles);
        }
        else
        {
            _Source = UCk_SwayGym_SourceRuntime_UE::TryBeginSuccessor(
                this, _PreviousComplete, Snapshot.Generation, WorldRoot, Roles);
        }
        if (ck::Is_NOT_Valid(_Source) ||
            UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(_Source) !=
                ECk_SwayGym_RetirementState::Active || !Get_HasWorldIdentity())
        { Request_Fail("Sway native fixed source rejected"); return; }
        for (int32 I = 0; I < _Fixtures.Num(); ++I)
        {
            if (!_Fixtures[I].Request_Start())
            { Request_Fail("Sway admitted motion could not start"); return; }
        }
        _GenerationCreating = false;
    }

    // The caller is only the currently controlled authority PC. Accepted means
    // retirement admitted, not that successor publication has completed.
    bool Request_Rebuild(APlayerController InOwner, APawn Pawn,
        int32 Serial, int32 ExpectedGeneration)
    {
        if (!HasAuthority() || _Failed || _Retiring || _GenerationCreating ||
            !Get_HasWorldIdentity() || Snapshot.Lifecycle != ECkSwayGym_Lifecycle::Active ||
            ExpectedGeneration != Snapshot.Generation || Serial <= 0 ||
            Snapshot.Generation >= 2147483647 ||
            // Reserve Retiring, successor Initializing/Active, and terminal Failed.
            Snapshot.Sequence >= 9223372036854775804 ||
            ck::Is_NOT_Valid(InOwner) || ck::Is_NOT_Valid(Pawn)) { return false; }
        auto PC = Cast<ACk_SwayGym_PlayerController>(InOwner);
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(PC) || PC.GetWorld() != CurrentWorld ||
            Pawn.GetWorld() != CurrentWorld || Pawn.GetController() != PC ||
            PC.GetControlledPawn() != Pawn ||
            !PC.Get_HasReservedSwayControl(Serial, ExpectedGeneration, Pawn) ||
            ck::Is_NOT_Valid(_Source) ||
            UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(_Source) !=
                ECk_SwayGym_RetirementState::Active) { return false; }
        FCkSwayGym_Snapshot Next;
        Next.Generation = Snapshot.Generation;
        Next.Sequence = Snapshot.Sequence + 1;
        Next.Lifecycle = ECkSwayGym_Lifecycle::Retiring;
        if (!TryPublish(Next)) { return false; }
        _Retiring = true;
        if (!UCk_SwayGym_SourceRuntime_UE::TryBeginHealthyRetirement(_Source) ||
            _Failed || !Get_HasWorldIdentity() ||
            Snapshot.Generation != ExpectedGeneration ||
            Snapshot.Lifecycle != ECkSwayGym_Lifecycle::Retiring)
        { Request_Fail("Sway healthy retirement rejected"); return false; }
        return true;
    }

    private void TickAuthority(float DeltaSeconds)
    {
        if (!_Armed || _Failed) { return; }
        if (!Get_HasWorldIdentity())
        { Request_Fail("Sway authority context lost"); return; }
        if (Snapshot.Generation == 0)
        {
            FCkSwayGym_Snapshot Initial;
            Initial.Generation = 1; Initial.Sequence = 1;
            Initial.Lifecycle = ECkSwayGym_Lifecycle::Initializing;
            if (!TryPublish(Initial)) { Request_Fail("Sway initial publication rejected"); return; }
            StartGeneration(1);
            return;
        }
        if (_GenerationCreating)
        {
            if (System::GetGameTimeInSeconds() - _CreateStartedAt > 20.0)
            { Request_Fail("Sway source setup timed out"); return; }
            for (int32 I = 0; I < _Fixtures.Num(); ++I)
            {
                if (!_Fixtures[I].Request_Advance())
                { Request_Fail("Sway staged source construction failed"); return; }
            }
            FinishSourceAdmission();
            return;
        }
        if (ck::Is_NOT_Valid(_Source))
        { Request_Fail("Sway source receipt missing"); return; }
        auto State = UCk_SwayGym_SourceRuntime_UE::Get_RetirementState(_Source);
        if (State == ECk_SwayGym_RetirementState::Unproven)
        { Request_Fail("Sway source dependency lost"); return; }
        if (_Retiring)
        {
            if (State == ECk_SwayGym_RetirementState::Pending) { return; }
            if (State != ECk_SwayGym_RetirementState::Complete ||
                Snapshot.Generation >= 2147483647 ||
                Snapshot.Sequence >= 9223372036854775807)
            { Request_Fail("Sway physical retirement rejected"); return; }
            _PreviousComplete = _Source;
            _Source = nullptr;
            _Retiring = false;
            _SourceReceiptAttempted = false;
            FCkSwayGym_Snapshot Initial;
            Initial.Generation = Snapshot.Generation + 1;
            Initial.Sequence = Snapshot.Sequence + 1;
            Initial.Lifecycle = ECkSwayGym_Lifecycle::Initializing;
            if (!TryPublish(Initial))
            { Request_Fail("Sway successor publication rejected"); return; }
            StartGeneration(Initial.Generation);
            return;
        }
        if (State != ECk_SwayGym_RetirementState::Active ||
            // Keep one sequence value for the terminal Failed publication.
            Snapshot.Sequence >= 9223372036854775806)
        { Request_Fail("Sway active source rejected"); return; }
        FCkSwayGym_Frame Frame;
        if (!UCk_SwayGym_SourceRuntime_UE::TryCopyLatestFrame(_Source, Frame))
        { return; }
        if (Snapshot.Lifecycle == ECkSwayGym_Lifecycle::Active &&
            Frame.FrameSerial <= Snapshot.FrameSerial) { return; }
        FCkSwayGym_Snapshot Next;
        Next.Generation = Snapshot.Generation;
        Next.Sequence = Snapshot.Sequence + 1;
        Next.Lifecycle = ECkSwayGym_Lifecycle::Active;
        Next.FrameSerial = Frame.FrameSerial;
        Next.SwayYawDegrees = Frame.SwayYawDegrees;
        Next.SwayXCm = Frame.SwayXCm;
        Next.SwaySettled = Frame.SwaySettled;
        if (!TryPublish(Next))
        { Request_Fail("Sway frame publication rejected"); }
    }

    private void TickClient()
    {
        if (_Failed || _Ending || HasAuthority()) { return; }
        if (!Get_HasWorldIdentity())
        { if (_ClientBound) { _Failed = true; } return; }
        _ClientBound = true;
        if (!CkSwayGymShared::Get_IsValidSnapshot(Snapshot))
        { _Failed = true; return; }
        if (Snapshot.Generation == 0 && Snapshot.Sequence == 0)
        {
            if (_ClientWaitStartedAt <= 0.0)
            { _ClientWaitStartedAt = System::GetGameTimeInSeconds(); }
            if (System::GetGameTimeInSeconds() - _ClientWaitStartedAt > 20.0)
            { _Failed = true; }
        }
        if (Snapshot.Lifecycle == ECkSwayGym_Lifecycle::Failed) { _Failed = true; }
    }
    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (_Ending || !Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
        { return; }
        if (!Request_EnsureOwnerRoot()) { return; }
        if (HasAuthority()) { TickAuthority(DeltaSeconds); }
        else { TickClient(); }
    }

    UFUNCTION(BlueprintPure)
    bool TryGet_PresentationSample(FCkSwayGym_Snapshot&out Out)
    {
        Out = FCkSwayGym_Snapshot();
        if (_Ending || _Failed || !Get_HasWorldIdentity() ||
            !CkSwayGymShared::Get_IsValidSnapshot(Snapshot) ||
            Snapshot.Lifecycle != ECkSwayGym_Lifecycle::Active) { return false; }
        for (int32 I = 0; I < CkSwayGymShared::HeadCount; ++I)
        { if (ck::Is_NOT_Valid(Get_ReplicatedHead(I))) { return false; } }
        for (int32 I = 0; I < CkSwayGymShared::CubeCount; ++I)
        { if (ck::Is_NOT_Valid(Get_ReplicatedCube(I))) { return false; } }
        Out = Snapshot;
        return true;
    }

    UFUNCTION(BlueprintPure)
    UCk_SwayGym_SourceReceipt_UE Get_SourceReceipt() const { return _Source; }

    UFUNCTION(BlueprintPure)
    FString Get_CurrentFailureReason() const
    {
        if (Snapshot.Lifecycle == ECkSwayGym_Lifecycle::Failed &&
            CkSwayGymShared::Get_IsValidSnapshot(Snapshot))
        { return Snapshot.FailureReason; }
        return _Failed ? (_LocalFailureReason.IsEmpty() ?
            "Sway local source or replication failed" : _LocalFailureReason) : "";
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        if (ck::IsValid(_Source))
        { UCk_SwayGym_SourceRuntime_UE::Request_Close(_Source); }
        for (int32 I = 0; I < _Fixtures.Num(); ++I)
        { _Fixtures[I].Request_Destroy(); }
    }
}
