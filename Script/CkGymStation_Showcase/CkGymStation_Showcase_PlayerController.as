// Authored geometry is local presentation; the GameState owns its eight-slot source and reveal clock.
class ACk_StationShowcaseGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    UPROPERTY(Replicated) int32 LastAcknowledgedDeliverySerial = 0;
    UPROPERTY(Replicated) FCkStationShowcase_ControlCommit LastControlCommit;
    UPROPERTY(Replicated) bool OwnerSessionFailed = false;

    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _ReservedSerial = 0;
    private int32 _ReservedGeneration = 0;
    private int32 _NextDeliverySerial = 0;
    private int32 _AppliedDeliverySerial = 0;
    private ACk_Gym_Base_Pawn _SessionPawn;
    private bool _SessionBound = false;
    private ACk_Gym_Base_Pawn _CommitPawn;
    private ACk_StationShowcaseGym_GameState _SessionState;
    private ACk_StationShowcaseGym_GameState _ReservedState;
    private ACk_StationShowcaseGym_GameState _CommitState;
    UPROPERTY() private FCkStationShowcase_ControlCommit _InstalledControlCommit;

    UFUNCTION(BlueprintPure)
    FCkStationShowcase_ControlCommit Get_InstalledShowcaseCommit() const
    { return _InstalledControlCommit; }
    // Pure whole-value observations; these do not install, release, acknowledge or repair state.
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_ShowcaseLocalStations() const
    { return _LocalStations; }

    UFUNCTION(BlueprintPure) int32 Get_ShowcaseShownSequence() const
    { return _ShownSequence; }

    UFUNCTION(BlueprintPure) bool Get_ShowcasePresentationReady()
    {
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (!IsLocalController() || ck::Is_NOT_Valid(State) || State != _BoundState ||
            _LocalFailed || _LocalStarting || _ShownSequence <= 0 ||
            !State.Get_IsCompleteSnapshot(State.Snapshot) || _ShownSequence > State.Snapshot.Sequence)
        { return false; }
        if (HasAuthority()) { return _LocalStations.IsEmpty() && State.Get_IsReady(); }
        return _LocalGeneration == State.Snapshot.Generation && Get_HasExactLocalCohort(State.Snapshot);
    }

    private bool _OwnerMonitorArmed = false;
    private int32 _LocalGeneration = 0;
    private int32 _ShownSequence = 0;
    private ACk_StationShowcaseGym_GameState _BoundState;
    private bool _PresentationArmed = false;
    private bool _LocalStarting = false;
    private bool _LocalFailed = false;
    private float _LocalStartedAt = 0.0f;
    private FCk_Handle _LocalOwner;
    private TArray<FCk_Handle> _LocalStations;
    private TArray<FCk_Handle_PendingEntityScript> _LocalPending;
    private TArray<int32> _LocalCompleted;
    private TArray<int32> _LocalConstructed;
    private TArray<FCk_Handle> _LocalRetiring;
    private TArray<FCk_Handle> _KnownLocal;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    { return TArray<FCkGym_Station_SpawnParams_Payload>(); }

    void Request_StartGym() override
    {
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (HasAuthority() && ck::IsValid(State) && State.Get_HasPublishedCatalog())
        {
            State.Request_EnsureSharedDemo();
            if (!_OwnerMonitorArmed)
            { _OwnerMonitorArmed = true; System::SetTimer(this, n"OnOwnerContext", 0.1, true); }
        }
        if (IsLocalController() && !_PresentationArmed)
        {
            _PresentationArmed = true;
            System::SetTimer(this, n"OnPresentationTick", 0.1, true);
        }
    }

    UFUNCTION() private void OnOwnerContext()
    {
        if (!HasAuthority() || OwnerSessionFailed ||
            (_ReservedSerial == 0 && LastControlSerial == 0)) { return; }
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || State != _SessionState ||
            !State.Get_IsValidOwner(this, _SessionPawn))
        {
            OwnerSessionFailed = true;
            if (_ReservedSerial > 0)
            { Request_RejectShowcaseControl(_ReservedSerial, 2); }
            ForceNetUpdate();
        }
    }

    FString Get_ControlPanelTitle() override
    { return "GYM STATION SHOWCASE"; }

    UFUNCTION(BlueprintPure)
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Rebuild every station",
            !OwnerSessionFailed && _PendingSerial == 0));
        auto Installed = _InstalledControlCommit;
        if (Installed.DeliverySerial > 0)
        {
            Rows.Add(CkGym_Control::Status("Own rebuild delivery",
                f"Request {Installed.RequestSerial}, delivery {Installed.DeliverySerial}"));
            Rows.Add(CkGym_Control::Status("Installed generation",
                f"{Installed.Snapshot.Generation}, reveal {Installed.Snapshot.RevealedLines}/9, tick {Installed.Snapshot.TickCount}"));
        }
        if (OwnerSessionFailed)
        { Rows.Add(CkGym_Control::Status("Owner session", "Retired", true)); }
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || OwnerSessionFailed || InRowIndex != 0 ||
            _PendingSerial != 0) { return; }
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || !State.Snapshot.Ready || State.Snapshot.Failed) { return; }
        auto High = Math::Max(_RequestedSerial,
            Math::Max(LastControlAttemptSerial, LastControlSerial));
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        _PendingSerial = _RequestedSerial;
        Server_RequestShowcaseRebuild(_RequestedSerial, State.Snapshot.Generation);
    }

    UFUNCTION(Server)
    void Server_RequestShowcaseRebuild(int32 InSerial, int32 InGeneration)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlAttemptSerial ||
            InSerial <= LastControlSerial) { return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World))
        { LastControlResult = 3; ForceNetUpdate(); return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        auto State = CkStationShowcase::Get_State(World);
        TArray<ACk_StationShowcaseGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_StationShowcaseGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (OwnerSessionFailed || ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            ck::Is_NOT_Valid(World) || Pawn.GetWorld() != World ||
            ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            World.GetGameState() != State || State.GetClass() != ACk_StationShowcaseGym_GameState ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) || Modes[0].GetWorld() != World ||
            !State.Get_HasPublishedCatalog() || !State.Get_IsReady() ||
            Current < 0 || Current >= Registry.Num() ||
            Registry[Current].DisplayName != "Station Showcase" ||
            InGeneration != State.Snapshot.Generation || _ReservedSerial != 0 ||
            !Get_CanAcceptOwnerCommit() ||
            (_SessionBound && (_SessionPawn != Pawn || _SessionState != State)))
        { LastControlResult = 3; ForceNetUpdate(); return; }
        _SessionPawn = Pawn; _SessionState = State; _SessionBound = true;
        _ReservedState = State;
        _ReservedSerial = InSerial;
        _ReservedGeneration = InGeneration;
        if (!State.Request_Rebuild(this, Pawn, InSerial, InGeneration))
        {
            Request_RejectShowcaseControl(InSerial, 3);
        }
    }

    bool Get_HasReservedShowcaseControl(int32 InSerial, int32 InGeneration,
        ACk_Gym_Base_Pawn InPawn) const
    {
        return HasAuthority() && !OwnerSessionFailed && InSerial > 0 &&
            InGeneration > 0 && _SessionPawn == InPawn && ck::IsValid(_ReservedState) &&
            _ReservedState == _SessionState && _ReservedState.GetWorld() == GetWorld() &&
            _ReservedSerial == InSerial && _ReservedGeneration == InGeneration;
    }

    int32 Get_NextDeliverySerial() const
    { return HasAuthority() && _NextDeliverySerial < 2147483646 ? _NextDeliverySerial + 1 : 0; }

    bool Get_CanAcceptOwnerCommit()
    {
        return HasAuthority() && !OwnerSessionFailed && _NextDeliverySerial < 2147483646 &&
            (LastControlCommit.DeliverySerial == 0 ||
             LastAcknowledgedDeliverySerial == LastControlCommit.DeliverySerial);
    }

    void Request_RejectShowcaseControl(int32 InSerial, int32 InResult)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlSerial ||
            InResult < 2 || InResult > 4) { return; }
        if (_ReservedSerial == InSerial)
        { _ReservedSerial = 0; _ReservedGeneration = 0; _ReservedState = nullptr; }
        if (LastControlAttemptSerial == InSerial) { LastControlResult = InResult; }
        ForceNetUpdate();
    }

    bool Request_DetachShowcaseControl(ACk_StationShowcaseGym_GameState InState,
        ACk_Gym_Base_Pawn InPawn, int32 InSerial, int32 InGeneration)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(InState) ||
            InState.GetWorld() != GetWorld() || InState != _ReservedState ||
            InState != _SessionState || _SessionPawn != InPawn ||
            _ReservedSerial != InSerial || _ReservedGeneration != InGeneration ||
            InSerial <= 0 || InGeneration <= 0 ||
            (InState.Get_HasCapturedOwnerContext(this, InPawn) && !OwnerSessionFailed)) { return false; }
        OwnerSessionFailed = true;
        Request_RejectShowcaseControl(InSerial, 2);
        ForceNetUpdate();
        return true;
    }

    bool Request_TryRecordShowcaseCommit(ACk_StationShowcaseGym_GameState InState,
        int32 InSerial)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(InState) ||
            InState.GetWorld() != GetWorld() || InState != _ReservedState ||
            InState != _SessionState || InSerial <= LastControlSerial ||
            !Get_CanAcceptOwnerCommit()) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != InState ||
            !InState.Get_IsValidOwner(this, _SessionPawn)) { return false; }
        FCkStationShowcase_ControlCommit Commit;
        if (!InState.Request_TryGetPreparedShowcaseCommit(this, InSerial, Commit) ||
            Commit.RequestSerial != InSerial || Commit.OriginState != InState ||
            Commit.Owner != this || Commit.OwnerPawn != _SessionPawn ||
            Commit.OriginGeneration != _ReservedGeneration ||
            Commit.DeliverySerial != Get_NextDeliverySerial() ||
            !Get_HasReservedShowcaseControl(InSerial, _ReservedGeneration, _SessionPawn) ||
            !InState.Get_IsCompleteSnapshot(Commit.Snapshot)) { return false; }
        _NextDeliverySerial = Commit.DeliverySerial;
        LastControlCommit = Commit;
        LastControlSerial = InSerial;
        _CommitPawn = _SessionPawn; _CommitState = InState;
        _ReservedSerial = 0; _ReservedGeneration = 0; _ReservedState = nullptr;
        if (LastControlAttemptSerial == InSerial) { LastControlResult = 1; }
        ForceNetUpdate();
        return true;
    }

    UFUNCTION(Server) void Server_AcknowledgeShowcaseCommit(
        ACk_StationShowcaseGym_GameState InState, ACk_Gym_Base_Pawn InPawn,
        int32 InDelivery, int32 InSerial)
    {
        if (!HasAuthority() || OwnerSessionFailed ||
            InDelivery <= LastAcknowledgedDeliverySerial ||
            InDelivery != LastControlCommit.DeliverySerial ||
            InSerial != LastControlCommit.RequestSerial ||
            InSerial != LastControlSerial) { return; }
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || ck::Is_NOT_Valid(InState) ||
            InState != State || State != _CommitState || State != _SessionState ||
            LastControlCommit.OriginState != State || LastControlCommit.Owner != this ||
            InPawn != _CommitPawn || LastControlCommit.OwnerPawn != InPawn ||
            !State.Get_IsValidOwner(this, InPawn)) { return; }
        LastAcknowledgedDeliverySerial = InDelivery;
        ForceNetUpdate();
    }

    private void Request_CaptureLocal(FCk_Handle InRoot)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InRoot) ||
            _LocalRetiring.Contains(InRoot)) { return; }
        _LocalRetiring.Add(InRoot);
        if (ck::IsValid(InRoot))
        { for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
          { Request_CaptureLocal(Child); } }
    }

    private void Request_RetireLocal()
    {
        Request_CaptureLocal(_LocalOwner);
        for (auto Known : _KnownLocal) { Request_CaptureLocal(Known); }
        for (auto Station : _LocalStations) { Request_CaptureLocal(Station); }
        for (auto Pending : _LocalPending)
        { Request_CaptureLocal(Pending.Get_EntityUnderConstruction()); }
        for (auto Pending : _LocalPending)
        { if (utils_pending_entity_script::Get_IsValid(Pending))
          { utils_pending_entity_script::Request_DestroyEntity(Pending); } }
        if (CkStationShowcase::IsLive(_LocalOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_LocalOwner); }
        _LocalOwner = FCk_Handle();
        _LocalStations.Empty(); _LocalPending.Empty(); _LocalCompleted.Empty();
        _LocalConstructed.Empty();
        _KnownLocal.Empty();
        _LocalStarting = false; _ShownSequence = 0;
    }

    private bool Get_LocalRetired()
    {
        for (auto Old : _LocalRetiring)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old)) { return false; } }
        _LocalRetiring.Empty();
        return true;
    }

    private void Request_StartLocal(FCkStationShowcase_Snapshot InSample)
    {
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || State != _BoundState || State.GetWorld() != GetWorld() ||
            !Get_LocalRetired() || !State.Get_IsCompleteSnapshot(InSample)) { return; }
        auto Owner = ck::ToEntity(this);
        if (!CkStationShowcase::IsLive(Owner) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Owner))
        { Request_FailLocal("Showcase local controller entity unavailable"); return; }
        _LocalOwner = utils_entity_lifetime::Request_CreateEntity(Owner);
        if (!CkStationShowcase::IsLive(_LocalOwner))
        { Request_FailLocal("Showcase local owner allocation failed"); return; }
        Request_RememberLocal(_LocalOwner);
        _LocalGeneration = InSample.Generation;
        _LocalStarting = true;
        _LocalStartedAt = float(System::GetGameTimeInSeconds());
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            auto Params = InSample.Stations[i];
            if (!CkStationShowcase::Get_IsExactWireParams(Params, i))
            { Request_FailLocal("Showcase local catalog invalid"); return; }
            auto Pending = utils_entity_script::Request_SpawnEntity(_LocalOwner,
                UCk_EntityScript_GymStation, FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this, n"OnLocalSpawnCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_FailLocal("Showcase local station spawn rejected"); return; }
            _LocalPending.Add(Pending);
            _LocalCompleted.Add(0);
            _LocalConstructed.Add(0);
            _LocalStations.Add(FCk_Handle());
            utils_pending_entity_script::Promise_OnConstructed(Pending,
                FCk_Delegate_EntityScript_Constructed(this, n"OnLocalConstructed"));
        }
    }

    UFUNCTION() private void OnLocalSpawnCompleted(FCk_Handle InEntity,
        ECk_Request_OperationResult InResult)
    {
        if (!_LocalStarting) { return; }
        if (_LocalPending.Num() != CkStationShowcase::SlotCount ||
            _LocalStations.Num() != CkStationShowcase::SlotCount ||
            _LocalCompleted.Num() != CkStationShowcase::SlotCount ||
            _LocalConstructed.Num() != CkStationShowcase::SlotCount)
        { Request_FailLocal("Showcase local callback cardinality invalid"); return; }
        for (int32 i = 0; i < _LocalPending.Num(); i++)
        {
            if (_LocalPending[i].Get_EntityUnderConstruction() != InEntity) { continue; }
            if (i >= _LocalCompleted.Num() || _LocalCompleted[i] != 0 ||
                InResult != ECk_Request_OperationResult::Succeeded)
            { Request_FailLocal("Showcase local station completion invalid"); return; }
            _LocalCompleted[i] = 1;
            return;
        }
        Request_FailLocal("Unexpected Showcase local station completion");
    }

    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript InScript)
    {
        if (!_LocalStarting) { return; }
        if (_LocalPending.Num() != CkStationShowcase::SlotCount ||
            _LocalStations.Num() != CkStationShowcase::SlotCount ||
            _LocalCompleted.Num() != CkStationShowcase::SlotCount ||
            _LocalConstructed.Num() != CkStationShowcase::SlotCount)
        { Request_FailLocal("Showcase local callback cardinality invalid"); return; }
        auto Entity = FCk_Handle(InScript);
        for (int32 i = 0; i < _LocalPending.Num(); i++)
        {
            if (_LocalPending[i].Get_EntityUnderConstruction() != Entity) { continue; }
            if (i >= _LocalConstructed.Num() || _LocalConstructed[i] != 0 ||
                !CkStationShowcase::IsLive(Entity) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Entity) ||
                utils_entity_lifetime::Get_LifetimeOwner(Entity) != _LocalOwner ||
                CkStationShowcase::IsLive(_LocalStations[i]))
            { Request_FailLocal("Showcase local station constructed identity invalid"); return; }
            _LocalStations[i] = Entity;
            _LocalConstructed[i] = 1;
            Request_RememberLocal(Entity);
            return;
        }
        Request_FailLocal("Unexpected Showcase local station");
    }

    private void Request_FailLocal(FString InReason)
    {
        _LocalFailed = true;
        Request_RetireLocal();
        ck::Error("[StationShowcase] " + InReason);
    }

    private void Request_RememberLocal(FCk_Handle InRoot)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InRoot)) { return; }
        if (!_KnownLocal.Contains(InRoot)) { _KnownLocal.Add(InRoot); }
        if (ck::IsValid(InRoot))
        { for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
          { Request_RememberLocal(Child); } }
    }

    UFUNCTION() private void OnPresentationTick()
    {
        if (!IsLocalController()) { Request_RetireLocal(); return; }
        if (_PendingSerial > 0 && LastControlAttemptSerial == _PendingSerial &&
            LastControlResult > 1) { _PendingSerial = 0; }
        auto State = CkStationShowcase::Get_State(GetWorld());
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || ck::Is_NOT_Valid(State) ||
            State.GetWorld() != World || World.GetGameState() != State ||
            State.GetClass() != ACk_StationShowcaseGym_GameState || !State.Get_HasPublishedCatalog())
        {
            Request_RetireLocal(); _BoundState = nullptr; _LocalGeneration = 0;
            return;
        }
        if (_BoundState != State)
        {
            Request_RetireLocal();
            _BoundState = State;
            _LocalGeneration = 0;
            _LocalFailed = false;
        }
        // Owner outcome is installed independently of the latest shared geometry's generation/readiness.
        Request_ConsumeCommit(State);
        auto Sample = State.Snapshot;
        if (Sample.Failed || !Sample.Ready ||
            !State.Get_IsCompleteSnapshot(Sample))
        { Request_RetireLocal(); return; }
        if (HasAuthority())
        {
            if (!State.Get_IsReady()) { return; }
            _ShownSequence = Sample.Sequence;
            return;
        }
        if (_LocalGeneration != Sample.Generation)
        {
            Request_RetireLocal();
            _LocalGeneration = Sample.Generation;
            _LocalFailed = false;
        }
        if (_LocalFailed || !Get_LocalRetired()) { return; }
        if (!CkStationShowcase::IsLive(_LocalOwner))
        { Request_StartLocal(Sample); return; }
        if (_LocalStarting)
        {
            if (float(System::GetGameTimeInSeconds()) - _LocalStartedAt > 20.0f)
            { Request_FailLocal("Showcase local station readiness timed out"); return; }
            if (_LocalStations.Num() != CkStationShowcase::SlotCount ||
                _LocalPending.Num() != CkStationShowcase::SlotCount ||
                _LocalCompleted.Num() != CkStationShowcase::SlotCount ||
                _LocalConstructed.Num() != CkStationShowcase::SlotCount)
            { Request_FailLocal("Showcase local callback cardinality invalid"); return; }
            for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
            {
                if (_LocalCompleted[i] < 0 || _LocalCompleted[i] > 1 ||
                    _LocalConstructed[i] < 0 || _LocalConstructed[i] > 1)
                { Request_FailLocal("Showcase local callback count invalid"); return; }
                if (_LocalCompleted[i] == 0 || _LocalConstructed[i] == 0) { return; }
            }
            if (!Get_HasExactLocalIdentity(Sample))
            { Request_FailLocal("Showcase local station identity invalid"); return; }
            // GymStation publishes its display fragment on a timer after Construct.
            if (!Get_HasExactLocalCohort(Sample)) { return; }
            _LocalStarting = false;
        }
        if (!Get_HasExactLocalCohort(Sample))
        { Request_FailLocal("Showcase local station cohort lost"); return; }
        Request_RememberLocal(_LocalOwner);
        for (auto Station : _LocalStations) { Request_RememberLocal(Station); }
        if (_ShownSequence >= Sample.Sequence || Sample.TickCount == 0) { return; }
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            auto& Text = _LocalStations[i].AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            Text.Title = FText::FromString(Sample.StationTitles[i]);
            Text.Description = FText::FromString(Sample.StationBodies[i]);
            Text.Instructions = FText::FromString(Sample.StationInstructions[i]);
        }
        _ShownSequence = Sample.Sequence;
    }

    private void Request_ConsumeCommit(ACk_StationShowcaseGym_GameState InState)
    {
        auto Commit = LastControlCommit;
        if (OwnerSessionFailed || Commit.DeliverySerial <= _AppliedDeliverySerial ||
            Commit.DeliverySerial <= LastAcknowledgedDeliverySerial || Commit.RequestSerial <= 0 ||
            Commit.OriginGeneration <= 0 || Commit.OriginGeneration >= 2147483646 ||
            Commit.Snapshot.Generation != Commit.OriginGeneration + 1 ||
            ck::Is_NOT_Valid(InState) || Commit.OriginState != InState || Commit.Owner != this)
        { return; }
        auto World = GetWorld();
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != InState ||
            InState.GetWorld() != World || InState.GetClass() != ACk_StationShowcaseGym_GameState ||
            !InState.Get_HasPublishedCatalog() || ck::Is_NOT_Valid(Pawn) ||
            Commit.OwnerPawn != Pawn || Pawn.GetWorld() != World || Pawn.GetController() != this ||
            !InState.Get_IsCompleteSnapshot(Commit.Snapshot)) { return; }
        // This whole typed cache is the real per-owner HUD consumer. Shared shells stay on live GS state.
        _InstalledControlCommit = Commit;
        _AppliedDeliverySerial = Commit.DeliverySerial;
        if (_PendingSerial == Commit.RequestSerial) { _PendingSerial = 0; }
        Server_AcknowledgeShowcaseCommit(InState, Pawn, Commit.DeliverySerial, Commit.RequestSerial);
    }

    private bool Get_HasExactLocalIdentity(FCkStationShowcase_Snapshot InSample)
    {
        auto WorldOwner = ck::TransientEntity();
        if (!CkStationShowcase::IsLive(WorldOwner) || !CkStationShowcase::IsLive(_LocalOwner) ||
            _LocalGeneration != InSample.Generation ||
            InSample.CatalogVersion != CkStationShowcase::CatalogVersion ||
            _LocalStations.Num() != CkStationShowcase::SlotCount ||
            _LocalPending.Num() != CkStationShowcase::SlotCount) { return false; }
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            auto H = _LocalStations[i];
            auto Tagged = utils_entity_tag::ForEach_Entity(WorldOwner,
                CkStationShowcase::Get_StationTag(i));
            if (!CkStationShowcase::IsLive(H) ||
                _LocalPending[i].Get_EntityUnderConstruction() != H ||
                !utils_entity_lifetime::Has_LifetimeOwner(H) ||
                utils_entity_lifetime::Get_LifetimeOwner(H) != _LocalOwner ||
                Tagged.Num() != 1 || Tagged[0] != H ||
                !CkStationShowcase::Get_IsExactStationScript(H))
            { return false; }
        }
        return true;
    }

    private bool Get_HasExactLocalCohort(FCkStationShowcase_Snapshot InSample)
    {
        if (!Get_HasExactLocalIdentity(InSample)) { return false; }
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        { if (!_LocalStations[i].Has_Fragment(FCkGym_Station_TitleAndDescription))
          { return false; } }
        return true;
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        Request_RetireLocal();
        Super::EndPlay(InReason);
    }
}
