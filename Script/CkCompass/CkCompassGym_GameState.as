// Language=angelscript
// One exact authority world owns the source. Each client world has one
// mirror; each controller owns its own Compass and overlay. No ECS handle
// crosses the wire.
class ACk_CompassGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;

    UPROPERTY(ReplicatedUsing=OnSnapshotUpdated)
    FCkCompassGym_WireSnapshot Snapshot;

    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCk_Handle _SourceRoot;
    private FCk_Handle _ClientMirrorRoot;
    private TArray<FCkCompassGym_SourceReceipt> _Base;
    private TArray<FCkCompassGym_SourceReceipt> _Stress;
    private TArray<FCkCompassGym_SourceReceipt> _FrozenStress;
    private TArray<FCkCompassGym_SourceReceipt> _Pings;
    private TArray<bool> _PingDone;
    private FCkGym_Entry _CatalogEntry;
    private int32 _CatalogIndex = -1;
    private int64 _NextCatalogId = 1;
    private int64 _NextRequestSerial = 1;
    private int64 _PendingSerial = 0;
    private int32 _PendingKind = 0; // 1 base, 2 stress admission, 3 withdrawal
    private double _PendingSince = 0.0;
    private double _CurrentRowsPendingSince = 0.0;
    private bool _Attempted = false;
    private bool _Active = false;
    private bool _StressActive = false;
    private bool _ClientAttempted = false;
    private bool _ClientQueued = false;
    private bool _ClientClosed = false;
    private bool _Failed = false;
    private bool _Ending = false;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        SetTickGroup(ETickingGroup::TG_PostUpdateWork);
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
    }

    UFUNCTION()
    private void OnSnapshotUpdated()
    {
        // Replication only delivers values. Client Tick validates the latest
        // complete envelope before any ECS creation or mirror mutation.
    }

    private bool TryClientBootstrap(FCk_Handle&out Root,
        FCkGym_Entry&out Entry, int32&out Index)
    {
        Root = FCk_Handle();
        Entry = FCkGym_Entry();
        Index = -1;
        if (_Ending || HasAuthority() || IsActorBeingDestroyed() ||
            GetClass() != ACk_CompassGym_GameState ||
            !Get_HasPublishedCatalog()) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) ||
            World.GetNetMode() != ENetMode::NM_Client ||
            World.GetGameState() != this ||
            (_ClientAttempted && World != _World)) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        Entry = Entries[Index];
        if (Entry.DisplayName != "Compass" ||
            Entry.GameModeClass != ACk_CompassGym_GameMode)
        { return false; }
        auto EcsWorldSubsystem =
            Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(EcsWorldSubsystem)) { return false; }
        Root = EcsWorldSubsystem.Get_TransientEntity();
        if (!IsLive(Root) ||
            utils_entity_lifetime::Get_WorldForEntity(Root) != World)
        { return false; }
        bool AdmissionClosed = true;
        return UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(
                Root, AdmissionClosed) && !AdmissionClosed &&
            !utils_compass::Get_IsWorldProjectionClosed(Root);
    }

    private bool HasClientBinding()
    {
        if (!_ClientAttempted || _ClientClosed || _Ending ||
            !IsLive(_ClientMirrorRoot)) { return false; }
        FCk_Handle Root;
        FCkGym_Entry Entry;
        int32 Index = -1;
        if (!TryClientBootstrap(Root, Entry, Index) ||
            Root != _WorldRoot || GetWorld() != _World ||
            Index != _CatalogIndex ||
            Entry.DisplayName != _CatalogEntry.DisplayName ||
            Entry.GameModeClass != _CatalogEntry.GameModeClass ||
            Entry.LevelName != _CatalogEntry.LevelName ||
            Entry.Category != _CatalogEntry.Category ||
            Entry.HintCode != _CatalogEntry.HintCode ||
            !IsDirect(_ClientMirrorRoot, _WorldRoot))
        { return false; }
        return true;
    }

    private void CloseCapturedClient()
    {
        if (_ClientClosed || _Ending || HasAuthority()) { return; }
        _ClientClosed = true; // terminal; never discover or adopt another root
        if (!IsLive(_WorldRoot)) { return; }
        // Both native functions validate the captured exact world root and
        // weak World independently of this actor's current GS identity.
        UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot);
        utils_compass::TryCloseWorldProjection(_WorldRoot);
    }

    private void TryCloseClientFailure()
    {
        if (_ClientClosed || _Ending || HasAuthority()) { return; }
        _Failed = true; // latch failure even when ECS/bootstrap is still pending
        if (_ClientAttempted) { CloseCapturedClient(); return; }
        // Before the first capture, a Failed/malformed value can arrive before
        // ECS/bootstrap. Retry only this exact current client-world preflight.
        FCk_Handle Root;
        FCkGym_Entry Entry;
        int32 Index = -1;
        if (!TryClientBootstrap(Root, Entry, Index)) { return; }
        _World = GetWorld();
        _WorldRoot = Root;
        _CatalogIndex = Index;
        _CatalogEntry = Entry;
        _ClientAttempted = true;
        CloseCapturedClient();
    }

    private void TickClientMirror()
    {
        if (_Ending || _ClientClosed || HasAuthority()) { return; }
        if (_Failed || Snapshot.Lifecycle == ECkCompassGym_WireLifecycle::Failed)
        { TryCloseClientFailure(); return; }
        TArray<FCkCompassGym_SourceRow> Expanded;
        if (!UCkCompassGym_Wire::TryValidateAndExpand(Snapshot, Expanded))
        { TryCloseClientFailure(); return; }
        if (Snapshot.SourceGeneration == 0 && Snapshot.Sequence == 0)
        {
            if (_ClientAttempted) { CloseCapturedClient(); }
            return; // initial replicated default can precede ECS/bootstrap
        }
        if (!_ClientAttempted)
        {
            FCk_Handle Root;
            FCkGym_Entry Entry;
            int32 Index = -1;
            if (!TryClientBootstrap(Root, Entry, Index) ||
                !utils_entity_lifetime::Get_CanCreateEntity(Root))
            { return; }
            _World = GetWorld();
            _WorldRoot = Root;
            _CatalogIndex = Index;
            _CatalogEntry = Entry;
            _ClientAttempted = true; // no rebind after first allocation attempt
            _ClientMirrorRoot =
                utils_entity_lifetime::Request_CreateEntity(_WorldRoot);
            if (!HasClientBinding())
            { CloseCapturedClient(); return; }
        }
        if (!HasClientBinding())
        { CloseCapturedClient(); return; }
        FString Why;
        if (!_ClientQueued)
        {
            if (!UCkCompassGym_Mirror::TryQueue_Snapshot(
                    _World, this, _ClientMirrorRoot, Snapshot, Why))
            { CloseCapturedClient(); return; }
            _ClientQueued = true;
            return;
        }
        bool Busy = false;
        bool Failed = false;
        if (!UCkCompassGym_Mirror::TryGet_Status(
                _World, this, _ClientMirrorRoot, Busy, Failed, Why) || Failed)
        { CloseCapturedClient(); return; }
        if (Busy) { return; } // retain latest replicated Snapshot until Idle
        int64 Generation = 0;
        int64 Sequence = 0;
        TArray<FCkCompassGym_SourceRow> InstalledRows;
        if (!UCkCompassGym_Mirror::TryGet_Installed(
                _World, this, _ClientMirrorRoot, Generation, Sequence,
                InstalledRows, Why))
        { CloseCapturedClient(); return; }
        FCkCompassGym_WireSnapshot Installed = Snapshot;
        Installed.SourceGeneration = Generation;
        Installed.Sequence = Sequence;
        Installed.Rows = InstalledRows;
        Installed.DeclaredRowCount = InstalledRows.Num();
        if (UCkCompassGym_Wire::Get_IsSameFrame(Snapshot, Installed))
        { return; }
        if (!UCkCompassGym_Mirror::TryQueue_Snapshot(
                _World, this, _ClientMirrorRoot, Snapshot, Why))
        { CloseCapturedClient(); }
    }

    private bool IsLive(const FCk_Handle& Handle)
    {
        return ck::IsValid(Handle) &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    private bool IsDirect(const FCk_Handle& Child, const FCk_Handle& Owner)
    {
        return IsLive(Child) && IsLive(Owner) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Owner;
    }

    private bool HasContext()
    {
        return !_Ending && !_Failed && HasAuthority() && _World != nullptr &&
            HasPublishedCompassIdentity() && GetWorld() == _World &&
            IsLive(_WorldRoot) &&
            ck::TransientEntity() == _WorldRoot &&
            utils_entity_lifetime::Get_WorldForEntity(_WorldRoot) == _World &&
            !CkGym_Admission::Get_IsClosedUnproven(_WorldRoot) &&
            !utils_compass::Get_IsWorldProjectionClosed(_WorldRoot) &&
            IsDirect(_SourceRoot, _WorldRoot);
    }

    private bool HasPublishedCompassIdentity()
    {
        if (_Ending || IsActorBeingDestroyed() || !HasAuthority() ||
            GetClass() != ACk_CompassGym_GameState ||
            !Get_HasPublishedCatalog()) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != this ||
            (_Attempted && World != _World)) { return false; }
        auto Mode = Gameplay::GetGameMode();
        if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
            Mode.GetClass() != ACk_CompassGym_GameMode) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Compass" ||
            Entry.GameModeClass != ACk_CompassGym_GameMode) { return false; }
        return !_Attempted || (_CatalogIndex == Index &&
            _CatalogEntry.DisplayName == Entry.DisplayName &&
            _CatalogEntry.GameModeClass == Entry.GameModeClass &&
            _CatalogEntry.LevelName == Entry.LevelName &&
            _CatalogEntry.Category == Entry.Category &&
            _CatalogEntry.HintCode == Entry.HintCode);
    }

    private bool CanRequester(ACk_CompassGym_PlayerController Requester, APawn Pawn)
    {
        // A Ping can stage while a stress request is pending.
        return _Active && HasContext() &&
            ck::IsValid(Requester) && Requester.GetClass() == ACk_CompassGym_PlayerController &&
            Requester.GetWorld() == _World && Requester.HasAuthority() &&
            !Requester.IsActorBeingDestroyed() && ck::IsValid(Pawn) &&
            Pawn.GetWorld() == _World && Pawn.GetClass() == ACk_CompassGym_Pawn && !Pawn.IsActorBeingDestroyed() &&
            Pawn.GetController() == Requester && Requester.GetControlledPawn() == Pawn;
    }

    void Request_Fail(FString Reason)
    {
        if (_Failed || _Ending || !HasAuthority()) { return; }
        _Failed = true;
        _Active = false;
        if (IsLive(_WorldRoot))
        {
            CkGym_Admission::TryCloseWorldUnproven(_WorldRoot);
            utils_compass::TryCloseWorldProjection(_WorldRoot);
        }
        FCkCompassGym_WireSnapshot Failed;
        if (Snapshot.Sequence >= 9223372036854775806)
        {
            ck::Error("Compass source closed; wire sequence exhausted");
            return;
        }
        Failed.SourceGeneration = 1;
        Failed.Sequence = Snapshot.Sequence > 0 ? Snapshot.Sequence + 1 : 1;
        Failed.Lifecycle = ECkCompassGym_WireLifecycle::Failed;
        Failed.DeclaredRowCount = 0;
        if (UCkCompassGym_Wire::Get_IsAdvance(Snapshot, Failed))
        {
            Snapshot = Failed;
            ForceNetUpdate();
        }
        ck::Error(f"Compass source closed: {Reason}");
        // Retain partial source descendants and all first-return receipts.
        // A failed world is inert until normal world teardown.
    }

    private FGameplayTag Category(FName Name)
    {
        return utils_gameplay_tag::ResolveGameplayTag(Name);
    }

    private bool ComposePoi(FCkCompassGym_SourceReceipt&inout Receipt,
        FVector Location, int32 Priority, ECk_Poi_OffscreenPolicy Offscreen,
        float Radius, FLinearColor Color, bool WithTimer)
    {
        if (!HasContext() || !utils_entity_lifetime::Get_CanCreateEntity(_SourceRoot))
        { return false; }
        Receipt.Host = utils_entity_lifetime::Request_CreateEntity(_SourceRoot);
        if (!IsDirect(Receipt.Host, _SourceRoot)) { return false; }
        auto Transform = utils_transform::Add(
            Receipt.Host, FTransform(FRotator::ZeroRotator, Location),
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Transform)) { return false; }
        auto Tagged = utils_entity_tag::Add_UsingGameplayTag(
            Receipt.Host, Receipt.ExpectedCategory);
        if (ck::Is_NOT_Valid(Tagged)) { return false; }
        auto Display = FCk_PoiDisplayDefinition_Spec(Category(n"Poi.Consumer.Compass"));
        Display.Set_Priority(Priority);
        Display.Set_OffscreenPolicy(Offscreen);
        auto DisplayHandle = utils_poi_display_definition::Add(Receipt.Host, Display);
        if (ck::Is_NOT_Valid(DisplayHandle)) { return false; }
        Receipt.Marker = utils_pmg_basic_shapes::Create_Sphere(
            Receipt.Host,
            FTransform(FRotator::ZeroRotator, Location + FVector(0.0, 0.0, 60.0)),
            Radius, 12, 12, ECk_Plane_Axis::XY, Color, true, 2.0, -1.0);
        if (!IsDirect(Receipt.Marker, Receipt.Host)) { return false; }
        if (WithTimer)
        {
            auto Spec = FCk_Timer_Spec(FCk_Time(5.0f));
            Spec.Set_Behavior(ECk_Timer_Behavior::StopOnDone);
            Spec.Set_StartingState(ECk_Timer_State::Running);
            Spec.Set_CountDirection(ECk_Timer_CountDirection::CountUp);
            auto ExactTimer = utils_timer::Add(Receipt.Host, Spec);
            Receipt.Timer = ExactTimer;
            if (!IsDirect(Receipt.Timer, Receipt.Host)) { return false; }
            auto Done = FCk_Delegate_Timer(this, n"OnPingDone");
            if (!Done.IsBound()) { return false; }
            ExactTimer.BindTo_OnDone(Done,
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight);
        }
        return HasContext();
    }

    private bool AddBase(ECkCompassGym_SourceKind Kind, FVector Location,
        FName CategoryName, int32 Priority, ECk_Poi_OffscreenPolicy Offscreen,
        float Radius, FLinearColor Color)
    {
        FCkCompassGym_SourceReceipt Blank;
        _Base.Add(Blank);
        auto Index = _Base.Num() - 1;
        _Base[Index].CatalogId = _NextCatalogId++;
        _Base[Index].Kind = Kind;
        _Base[Index].ExpectedCategory = Category(CategoryName);
        return ComposePoi(_Base[Index], Location, Priority, Offscreen,
            Radius, Color, false);
    }

    void Request_EnsureSharedSource()
    {
        if (_Attempted || _Failed || _Ending || !HasPublishedCompassIdentity())
        { return; }
        _World = GetWorld();
        _WorldRoot = ck::TransientEntity();
        if (_World == nullptr || !IsLive(_WorldRoot) ||
            utils_entity_lifetime::Get_WorldForEntity(_WorldRoot) != _World ||
            CkGym_Admission::Get_IsClosedUnproven(_WorldRoot) ||
            utils_compass::Get_IsWorldProjectionClosed(_WorldRoot) ||
            !utils_entity_lifetime::Get_CanCreateEntity(_WorldRoot))
        { return; } // normal ECS/bootstrap delay, no admission attempted
        _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        _Attempted = true;
        _SourceRoot = utils_entity_lifetime::Request_CreateEntity(_WorldRoot);
        if (!HasContext()) { Request_Fail("Source root unavailable"); return; }
        const FVector MainAnchor(500.0, -400.0, 0.0);
        const FVector Center = MainAnchor + FVector(-800.0, 0.0, 100.0);
        if (!AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(600.0,0.0,0.0),
                n"Poi.Category.Quest", 5, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(1.0,0.78,0.25)) ||
            !AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(424.3,424.3,0.0),
                n"Poi.Category.Shop", 3, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(0.95,0.5,0.2)) ||
            !AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(0.0,600.0,0.0),
                n"Poi.Category.Danger", 4, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(0.9,0.25,0.25)) ||
            !AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(-424.3,424.3,0.0),
                n"Poi.Category.Info", 1, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(0.35,0.6,0.95)) ||
            !AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(-600.0,0.0,0.0),
                n"Poi.Category.Quest", 2, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(1.0,0.78,0.25)) ||
            !AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(-424.3,-424.3,0.0),
                n"Poi.Category.Shop", 1, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(0.95,0.5,0.2)) ||
            !AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(0.0,-600.0,0.0),
                n"Poi.Category.Danger", 3, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(0.9,0.25,0.25)) ||
            !AddBase(ECkCompassGym_SourceKind::Ring, Center + FVector(424.3,-424.3,0.0),
                n"Poi.Category.Info", 2, ECk_Poi_OffscreenPolicy::Hide, 40.0,
                FLinearColor(0.35,0.6,0.95)) ||
            !AddBase(ECkCompassGym_SourceKind::Waypoint,
                Center + FVector(-2500.0,0.0,0.0), n"Poi.Category.Waypoint",
                10, ECk_Poi_OffscreenPolicy::ClampToEdge, 55.0,
                FLinearColor(0.7,0.35,0.95)))
        { Request_Fail("Base composition incomplete"); return; }
        FString Why;
        if (!UCkCompassGym_SourceMembership::TryRequest_BaseAdmission(
                _World, this, _SourceRoot, _Base, 1, Why))
        { Request_Fail(f"Base admission rejected: {Why}"); return; }
        _PendingSerial = 1;
        _NextRequestSerial = 2;
        _PendingKind = 1;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
    }

    void Request_EnsureSharedDemo()
    {
        Request_EnsureSharedSource();
    }

    bool Get_IsPresentationReady()
    {
        if (_Failed || _Ending || Snapshot.Lifecycle != ECkCompassGym_WireLifecycle::Active)
        { return false; }
        if (HasAuthority())
        {
            if (!HasContext()) { return false; }
            int64 Revision = 0;
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            if (!UCkCompassGym_SourceMembership::TryGet_CurrentRows(
                    _World, this, _SourceRoot, Revision, Rows, Why) ||
                Revision != Snapshot.Sequence) { return false; }
            FCkCompassGym_WireSnapshot Current = Snapshot;
            Current.Rows = Rows;
            Current.DeclaredRowCount = Rows.Num();
            return UCkCompassGym_Wire::Get_IsSameFrame(Snapshot, Current);
        }
        if (!HasClientBinding() || !_ClientQueued) { return false; }
        bool Busy = false;
        bool Failed = false;
        FString Why;
        if (!UCkCompassGym_Mirror::TryGet_Status(
                _World, this, _ClientMirrorRoot, Busy, Failed, Why) ||
            Busy || Failed)
        { return false; }
        int64 Generation = 0;
        int64 Sequence = 0;
        TArray<FCkCompassGym_SourceRow> InstalledRows;
        if (!UCkCompassGym_Mirror::TryGet_Installed(
                _World, this, _ClientMirrorRoot, Generation, Sequence,
                InstalledRows, Why) ||
            Generation != Snapshot.SourceGeneration ||
            Sequence != Snapshot.Sequence)
        { return false; }
        FCkCompassGym_WireSnapshot Installed = Snapshot;
        Installed.SourceGeneration = Generation;
        Installed.Sequence = Sequence;
        Installed.Rows = InstalledRows;
        Installed.DeclaredRowCount = InstalledRows.Num();
        return UCkCompassGym_Wire::Get_IsSameFrame(Snapshot, Installed);
    }

    private bool PollRequest()
    {
        if (_PendingKind == 0) { return true; }
        TArray<FCkCompassGym_SourceRow> ResultRows;
        TArray<FCkCompassGym_SourceReceipt> Frozen;
        FString Why;
        auto Result = UCkCompassGym_SourceMembership::Get_Result(
            _World, this, _SourceRoot, _PendingSerial, ResultRows, Frozen, Why);
        if (Result == ECkCompassGym_MembershipResult::Unavailable)
        { Request_Fail(f"Accepted membership result unavailable: {Why}"); return false; }
        if (Result == ECkCompassGym_MembershipResult::Pending)
        {
            if (UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                _PendingSince >= 30.0)
            { Request_Fail("Membership request did not settle"); }
            return false;
        }
        if (Result != ECkCompassGym_MembershipResult::Committed)
        { Request_Fail(f"Membership rejected: {Why}"); return false; }
        if ((_PendingKind == 1 && (ResultRows.Num() != 9 || Frozen.Num() != 0)) ||
            (_PendingKind == 2 && (ResultRows.Num() != 509 || Frozen.Num() != 0)) ||
            (_PendingKind == 3 && (ResultRows.Num() != 9 || Frozen.Num() != 500)))
        { Request_Fail("Committed membership count mismatch"); return false; }
        if (_PendingKind == 1) { _Active = true; }
        if (_PendingKind == 2) { _StressActive = true; }
        if (_PendingKind == 3)
        {
            _StressActive = false;
            _FrozenStress = Frozen;
        }
        _PendingKind = 0;
        _PendingSerial = 0;
        return true;
    }

    private void PublishCurrent()
    {
        int64 Revision = 0;
        TArray<FCkCompassGym_SourceRow> Rows;
        FString Why;
        if (!UCkCompassGym_SourceMembership::TryGet_CurrentRows(
                _World, this, _SourceRoot, Revision, Rows, Why))
        {
            if (!HasContext()) { Request_Fail("Authority source context lost"); return; }
            double Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
            if (_CurrentRowsPendingSince == 0.0) { _CurrentRowsPendingSince = Now; }
            if (Now - _CurrentRowsPendingSince >= 20.0)
            { Request_Fail(f"Current source did not materialize: {Why}"); }
            return;
        }
        _CurrentRowsPendingSince = 0.0;
        if (Revision >= 9223372036854775805)
        { Request_Fail("Source revision exhausted"); return; }
        if (Revision <= 0 || Rows.Num() < 9 ||
            (Snapshot.SourceGeneration == 1 && Revision < Snapshot.Sequence))
        { Request_Fail("Source revision or row count regressed"); return; }
        if (Snapshot.SourceGeneration == 1 && Revision == Snapshot.Sequence)
        { return; }
        FCkCompassGym_WireSnapshot Next;
        Next.SourceGeneration = 1;
        Next.Sequence = Revision;
        Next.Lifecycle = ECkCompassGym_WireLifecycle::Active;
        Next.DeclaredRowCount = Rows.Num();
        Next.Rows = Rows;
        // Native journal already checks retained-ID row immutability before
        // it releases a current revision.
        TArray<FCkCompassGym_SourceRow> Expanded;
        if (!UCkCompassGym_Wire::TryValidateAndExpand(Next, Expanded) ||
            !UCkCompassGym_Wire::Get_IsAdvance(Snapshot, Next))
        { Request_Fail("Source publication failed wire validation"); return; }
        Snapshot = Next;
        ForceNetUpdate();
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!HasAuthority())
        {
            TickClientMirror();
            return;
        }
        if (!HasAuthority() || _Ending || _Failed) { return; }
        if (!_Attempted) { Request_EnsureSharedDemo(); return; }
        if (!HasContext()) { Request_Fail("Authority source context lost"); return; }
        if (!PollRequest() || _Failed || !_Active) { return; }
        PublishCurrent();
    }

    bool TryRequestPing(ACk_CompassGym_PlayerController Requester, APawn Pawn)
    {
        if (!CanRequester(Requester, Pawn) || _NextCatalogId >= 9223372036854775805)
        { return false; }
        const FVector Ahead = Pawn.GetActorLocation() +
            Pawn.GetActorForwardVector() * 1500.0;
        if (Ahead.ContainsNaN() || !Math::IsFinite(Ahead.X) ||
            !Math::IsFinite(Ahead.Y) || !Math::IsFinite(Ahead.Z))
        { return false; }
        FCkCompassGym_SourceReceipt Blank;
        _Pings.Add(Blank);
        _PingDone.Add(false);
        int32 Index = _Pings.Num() - 1;
        _Pings[Index].CatalogId = _NextCatalogId++;
        _Pings[Index].Kind = ECkCompassGym_SourceKind::Ping;
        _Pings[Index].ExpectedCategory = Category(n"Poi.Category.Ping");
        if (!ComposePoi(_Pings[Index], Ahead, 20,
                ECk_Poi_OffscreenPolicy::ClampToEdge, 45.0,
                FLinearColor(0.3,0.9,0.95), true))
        { Request_Fail("Ping composition incomplete"); return false; }
        FString Why;
        if (!UCkCompassGym_SourceMembership::TryStage_Ping(
                _World, this, _SourceRoot, _Pings[Index], Why))
        { Request_Fail(f"Ping staging rejected: {Why}"); return false; }
        return true;
    }

    UFUNCTION()
    private void OnPingDone(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time DeltaT)
    {
        if (_Failed || _Ending || !HasAuthority()) { return; }
        if (!HasContext()) { Request_Fail("Ping callback lost source context"); return; }
        for (int32 Index = 0; Index < _Pings.Num(); ++Index)
        {
            if (_Pings[Index].Timer != FCk_Handle(Timer)) { continue; }
            if (_PingDone[Index]) { Request_Fail("Repeated Ping Done callback"); return; }
            _PingDone[Index] = true;
            FString Why;
            if (!UCkCompassGym_SourceMembership::TryRetire_Ping(
                    _World, this, _SourceRoot, _Pings[Index].Timer, Why))
            { Request_Fail(f"Ping retirement rejected: {Why}"); }
            return;
        }
        Request_Fail("Unknown Ping Done callback");
    }

    bool TryRequestStressAdmission(ACk_CompassGym_PlayerController Requester, APawn Pawn)
    {
        if (!CanRequester(Requester, Pawn) || _PendingKind != 0 || _StressActive ||
            _NextRequestSerial >= 9223372036854775805 ||
            _NextCatalogId >= 9223372036854775300)
        { return false; }
        for (auto Retired : _FrozenStress)
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Retired.Host))
            { return false; }
        }
        _FrozenStress.Empty();
        _Stress.Empty();
        const FVector StressAnchor(500.0, 400.0, 0.0);
        const FVector Center = StressAnchor + FVector(-800.0, 0.0, 100.0);
        for (int32 Row = 0; Row < 25; ++Row)
        {
            for (int32 Col = 0; Col < 20; ++Col)
            {
                FCkCompassGym_SourceReceipt Blank;
                _Stress.Add(Blank);
                int32 Index = _Stress.Num() - 1;
                _Stress[Index].CatalogId = _NextCatalogId++;
                _Stress[Index].Kind = ECkCompassGym_SourceKind::Stress;
                _Stress[Index].ExpectedCategory = Category(n"Poi.Category.Stress");
                _Stress[Index].Host = utils_entity_lifetime::Request_CreateEntity(_SourceRoot);
                if (!IsDirect(_Stress[Index].Host, _SourceRoot))
                { Request_Fail("Stress host allocation failed"); return false; }
                FVector Location = Center +
                    FVector(float(Row - 12) * 200.0, float(Col - 10) * 200.0, 0.0);
                auto Transform = utils_transform::Add(_Stress[Index].Host,
                    FTransform(FRotator::ZeroRotator, Location),
                    ECk_Replication::DoesNotReplicate);
                if (ck::Is_NOT_Valid(Transform))
                { Request_Fail("Stress transform failed"); return false; }
                auto Tagged = utils_entity_tag::Add_UsingGameplayTag(
                    _Stress[Index].Host, _Stress[Index].ExpectedCategory);
                if (ck::Is_NOT_Valid(Tagged))
                { Request_Fail("Stress category failed"); return false; }
            }
        }
        FString Why;
        int64 Serial = _NextRequestSerial;
        if (!UCkCompassGym_SourceMembership::TryRequest_StressAdmission(
                _World, this, _SourceRoot, _Base, _Stress, Serial, Why))
        { Request_Fail(f"Stress admission rejected: {Why}"); return false; }
        _NextRequestSerial++;
        _PendingSerial = Serial;
        _PendingKind = 2;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        return true;
    }

    bool TryRequestStressWithdrawal(ACk_CompassGym_PlayerController Requester, APawn Pawn)
    {
        if (!CanRequester(Requester, Pawn) || _PendingKind != 0 || !_StressActive ||
            _Stress.Num() != 500 || _Base.Num() != 9 ||
            _NextRequestSerial >= 9223372036854775805)
        { return false; }
        TArray<FCkCompassGym_SourceReceipt> Active;
        for (auto BaseReceipt : _Base) { Active.Add(BaseReceipt); }
        for (auto StressReceipt : _Stress) { Active.Add(StressReceipt); }
        FString Why;
        int64 Serial = _NextRequestSerial;
        if (!UCkCompassGym_SourceMembership::TryRequest_StressWithdrawal(
                _World, this, _SourceRoot, Active, Serial, Why))
        { Request_Fail(f"Stress withdrawal rejected: {Why}"); return false; }
        _NextRequestSerial++;
        _PendingSerial = Serial;
        _PendingKind = 3;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        return true;
    }
}
