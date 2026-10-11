// Six local shells consume the world-owned authority source; Cube behaviors run only on authority.
class ACk_TransformGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    UPROPERTY(Replicated) int32 LastAcknowledgedDeliverySerial = 0;
    UPROPERTY(Replicated) FCkTransformGym_ControlCommit LastControlCommit;
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
    private ACk_TransformGym_GameState _SessionState;
    private ACk_TransformGym_GameState _ReservedState;
    private ACk_TransformGym_GameState _CommitState;
    UPROPERTY() private FCkTransformGym_ControlCommit _InstalledControlCommit;
    // One nonreplicated source publication per owner. This is copied from the
    // authoritative GameState at publication, never from the delivery cache.
    UPROPERTY() private FCkTransformGym_ControlCommit _PublishedSourceAudit;

    UFUNCTION(BlueprintPure)
    FCkTransformGym_ControlCommit Get_InstalledTransformCommit() const
    { return _InstalledControlCommit; }
    UFUNCTION(BlueprintPure) FCkTransformGym_ControlCommit Get_PublishedTransformSourceAudit()
    {
        auto Audit = _PublishedSourceAudit;
        if (!HasAuthority() || ck::Is_NOT_Valid(Audit.OriginState) ||
            Audit.OriginState != _CommitState || Audit.Owner != this || Audit.OwnerPawn != _CommitPawn ||
            Audit.RequestSerial <= 0 || Audit.DeliverySerial <= 0 ||
            Audit.OriginGeneration <= 0 || Audit.OriginGeneration >= 2147483646 ||
            Audit.RequestSerial != LastControlCommit.RequestSerial ||
            Audit.DeliverySerial != LastControlCommit.DeliverySerial ||
            Audit.OriginGeneration != LastControlCommit.OriginGeneration ||
            Audit.Snapshot.Generation != Audit.OriginGeneration + 1 ||
            Audit.Snapshot.Sequence != LastControlCommit.Snapshot.Sequence ||
            !Audit.OriginState.Get_IsCompleteSnapshot(Audit.Snapshot))
        { return FCkTransformGym_ControlCommit(); }
        return Audit;
    }
    // Pure whole-value observations; these do not install, release, acknowledge or repair state.
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_TransformLocalStations() const
    { return _LocalStations; }

    UFUNCTION(BlueprintPure) int32 Get_TransformShownSequence() const
    { return _ShownSequence; }

    UFUNCTION(BlueprintPure) bool Get_TransformPresentationReady()
    {
        auto State = CkTransformShared::Get_State(GetWorld());
        if (!IsLocalController() || ck::Is_NOT_Valid(State) || State != _BoundState ||
            _LocalFailed || _LocalStarting || _ShownSequence <= 0 ||
            !State.Get_IsCompleteSnapshot(State.Snapshot) || !State.Get_IsCompleteSnapshot(_InstalledLiveSample) ||
            _InstalledLiveSample.Generation != State.Snapshot.Generation || _InstalledLiveSample.Sequence != _ShownSequence ||
            _ShownSequence > State.Snapshot.Sequence)
        { return false; }
        if (HasAuthority()) { return _LocalStations.IsEmpty() && State.Get_IsReady(); }
        return _LocalGeneration == State.Snapshot.Generation && Get_HasExactLocalCohort(State.Snapshot);
    }

    private bool _OwnerMonitorArmed = false;
    private int32 _LocalGeneration = 0;
    private int32 _ShownSequence = 0;
    UPROPERTY() private FCkTransformGym_Snapshot _InstalledLiveSample;
    UFUNCTION(BlueprintPure) FCkTransformGym_Snapshot Get_InstalledTransformSample() const
    { return _InstalledLiveSample; }
    private ACk_TransformGym_GameState _BoundState;
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
    private TArray<FCk_Handle> _KnownLocalParents;
    private TArray<FCk_Handle> _LocalRequired;
    private TArray<FCk_Handle> _LocalRequiredParents;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    { return TArray<FCkGym_Station_SpawnParams_Payload>(); }

    void Request_StartGym() override
    {
        auto State = CkTransformShared::Get_State(GetWorld());
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
        auto State = CkTransformShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || State != _SessionState ||
            !State.Get_IsValidOwner(this, _SessionPawn))
        {
            OwnerSessionFailed = true;
            if (_ReservedSerial > 0)
            { Request_RejectTransformControl(_ReservedSerial, 2); }
            ForceNetUpdate();
        }
    }

    FString Get_ControlPanelTitle() override
    { return "TRANSFORM"; }

    UFUNCTION(BlueprintPure)
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Re-run every station",
            !OwnerSessionFailed && _PendingSerial == 0));
        auto Installed = _InstalledControlCommit;
        if (Installed.DeliverySerial > 0)
        {
            Rows.Add(CkGym_Control::Status("Own rebuild delivery",
                f"Request {Installed.RequestSerial}, delivery {Installed.DeliverySerial}"));
            Rows.Add(CkGym_Control::Status("Installed generation",
                f"{Installed.Snapshot.Generation}, sample {Installed.Snapshot.Sequence}"));
        }
        if (LastControlAttemptSerial > LastControlSerial && LastControlResult > 1)
        { Rows.Add(CkGym_Control::Status("Own request result", f"Request {LastControlAttemptSerial}: " + (LastControlResult == 2 ? "Owner context gone" : "Rejected"), true)); }
        if (OwnerSessionFailed)
        { Rows.Add(CkGym_Control::Status("Owner session", "Retired", true)); }
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || OwnerSessionFailed || InRowIndex != 0 ||
            _PendingSerial != 0) { return; }
        auto State = CkTransformShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || !State.Snapshot.Ready || State.Snapshot.Failed) { return; }
        auto High = Math::Max(_RequestedSerial,
            Math::Max(LastControlAttemptSerial, LastControlSerial));
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        _PendingSerial = _RequestedSerial;
        Server_RequestTransformRebuild(_RequestedSerial, State.Snapshot.Generation);
    }

    UFUNCTION(Server)
    void Server_RequestTransformRebuild(int32 InSerial, int32 InGeneration)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlAttemptSerial ||
            InSerial <= LastControlSerial) { return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld))
        { LastControlResult = 3; ForceNetUpdate(); return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        auto State = CkTransformShared::Get_State(CurrentWorld);
        TArray<ACk_TransformGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_TransformGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (OwnerSessionFailed || ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            ck::Is_NOT_Valid(CurrentWorld) || Pawn.GetWorld() != CurrentWorld ||
            ck::Is_NOT_Valid(State) || State.GetWorld() != CurrentWorld ||
            CurrentWorld.GetGameState() != State || State.GetClass() != ACk_TransformGym_GameState ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) || Modes[0].GetWorld() != CurrentWorld ||
            !State.Get_HasPublishedCatalog() || !State.Get_IsReady() ||
            Current < 0 || Current >= Registry.Num() ||
            Registry[Current].DisplayName != "Transform" ||
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
            Request_RejectTransformControl(InSerial, 3);
        }
    }

    bool Get_HasReservedTransformControl(int32 InSerial, int32 InGeneration,
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

    void Request_RejectTransformControl(int32 InSerial, int32 InResult)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlSerial ||
            InResult < 2 || InResult > 4) { return; }
        if (_ReservedSerial == InSerial)
        { _ReservedSerial = 0; _ReservedGeneration = 0; _ReservedState = nullptr; }
        if (LastControlAttemptSerial == InSerial) { LastControlResult = InResult; }
        ForceNetUpdate();
    }

    bool Request_DetachTransformControl(ACk_TransformGym_GameState InState,
        ACk_Gym_Base_Pawn InPawn, int32 InSerial, int32 InGeneration)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(InState) ||
            InState.GetWorld() != GetWorld() || InState != _ReservedState ||
            InState != _SessionState || _SessionPawn != InPawn ||
            _ReservedSerial != InSerial || _ReservedGeneration != InGeneration ||
            InSerial <= 0 || InGeneration <= 0 ||
            (InState.Get_HasCapturedOwnerContext(this, InPawn) && !OwnerSessionFailed)) { return false; }
        OwnerSessionFailed = true;
        Request_RejectTransformControl(InSerial, 2);
        ForceNetUpdate();
        return true;
    }

    bool Request_TryRecordTransformCommit(ACk_TransformGym_GameState InState,
        int32 InSerial)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(InState) ||
            InState.GetWorld() != GetWorld() || InState != _ReservedState ||
            InState != _SessionState || InSerial <= LastControlSerial ||
            !Get_CanAcceptOwnerCommit()) { return false; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld) || CurrentWorld.GetGameState() != InState ||
            !InState.Get_IsValidOwner(this, _SessionPawn)) { return false; }
        FCkTransformGym_ControlCommit Commit;
        if (!InState.Request_TryGetPreparedTransformCommit(this, InSerial, Commit) ||
            Commit.RequestSerial != InSerial || Commit.OriginState != InState ||
            Commit.Owner != this || Commit.OwnerPawn != _SessionPawn ||
            Commit.OriginGeneration != _ReservedGeneration ||
            Commit.DeliverySerial != Get_NextDeliverySerial() ||
            !Get_HasReservedTransformControl(InSerial, _ReservedGeneration, _SessionPawn) ||
            !InState.Get_IsCompleteSnapshot(Commit.Snapshot)) { return false; }
        // OnMonitor assigned the actual source while Starting remains true;
        // no accepted PC field changes until both source and prepared keys agree.
        auto Published = InState.Snapshot;
        if (!InState.Get_IsCompleteSnapshot(Published) ||
            Published.Generation != Commit.Snapshot.Generation ||
            Published.Sequence != Commit.Snapshot.Sequence) { return false; }
        FCkTransformGym_ControlCommit Audit;
        Audit.DeliverySerial = Commit.DeliverySerial;
        Audit.RequestSerial = InSerial;
        Audit.OriginGeneration = _ReservedGeneration;
        Audit.OriginState = InState;
        Audit.Owner = this;
        Audit.OwnerPawn = _SessionPawn;
        Audit.Snapshot = Published;
        _NextDeliverySerial = Commit.DeliverySerial;
        _PublishedSourceAudit = Audit;
        LastControlCommit = Commit;
        LastControlSerial = InSerial;
        _CommitPawn = _SessionPawn; _CommitState = InState;
        _ReservedSerial = 0; _ReservedGeneration = 0; _ReservedState = nullptr;
        if (LastControlAttemptSerial == InSerial) { LastControlResult = 1; }
        ForceNetUpdate();
        return true;
    }

    UFUNCTION(Server) void Server_AcknowledgeTransformCommit(
        ACk_TransformGym_GameState InState, ACk_Gym_Base_Pawn InPawn,
        int32 InDelivery, int32 InSerial)
    {
        if (!HasAuthority() || OwnerSessionFailed ||
            InDelivery <= LastAcknowledgedDeliverySerial ||
            InDelivery != LastControlCommit.DeliverySerial ||
            InSerial != LastControlCommit.RequestSerial ||
            InSerial != LastControlSerial) { return; }
        auto State = CkTransformShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || ck::Is_NOT_Valid(InState) ||
            InState != State || State != _CommitState || State != _SessionState ||
            LastControlCommit.OriginState != State || LastControlCommit.Owner != this ||
            InPawn != _CommitPawn || LastControlCommit.OwnerPawn != InPawn ||
            !State.Get_IsValidOwner(this, InPawn)) { return; }
        LastAcknowledgedDeliverySerial = InDelivery;
        ForceNetUpdate();
    }

    private bool _LocalRetirementUnproven = false;
    private void Request_CaptureLocal(FCk_Handle InRoot)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InRoot)) { return; }
        if (_KnownLocal.Num() != _KnownLocalParents.Num()) { _LocalRetirementUnproven = true; return; }
        TArray<FCk_Handle> Queue; Queue.Add(InRoot); TArray<FCk_Handle> Seen;
        auto WorldRoot = ck::TransientEntity();
        for (int32 i = 0; i < Queue.Num(); ++i)
        {
            auto H = Queue[i]; if (Seen.Contains(H)) { continue; } Seen.Add(H);
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H, WorldRoot, Children))
            { _LocalRetirementUnproven = true; continue; }
            auto Parent = utils_entity_lifetime::Get_LifetimeOwner(H, ECk_PendingKill_Policy::IncludePendingKill);
            int32 Index = _KnownLocal.FindIndex(H);
            if (Index < 0)
            {
                if (Parent != _LocalOwner && Parent != ck::ToEntity(this) && !_KnownLocal.Contains(Parent))
                { _LocalRetirementUnproven = true; continue; }
                _KnownLocal.Add(H); _KnownLocalParents.Add(Parent); Index = _KnownLocal.Num()-1;
            }
            if (!_LocalRetiring.Contains(H)) { _LocalRetiring.Add(H); }
            if (Index >= _KnownLocalParents.Num()) { _LocalRetirementUnproven = true; continue; }
            // Retire captured escaped members themselves; never adopt their new owner's tree.
            if (Parent != _KnownLocalParents[Index]) { continue; }
            for (auto Child : Children) { Queue.Add(Child); }
        }
    }
    private void Request_RetireLocal()
    {
        Request_CaptureLocal(_LocalOwner);
        for (int32 i=0; i<_KnownLocal.Num(); ++i) { Request_CaptureLocal(_KnownLocal[i]); }
        for (auto Station : _LocalStations) { Request_CaptureLocal(Station); }
        for (auto Pending : _LocalPending)
        { Request_CaptureLocal(Pending.Get_EntityUnderConstruction()); }
        // Validate the entire captured cascade before ANY destruction request.
        bool SafeCascade = !_LocalRetirementUnproven && _KnownLocal.Num() == _KnownLocalParents.Num();
        for (auto Old : _LocalRetiring)
        {
            if (!_KnownLocal.Contains(Old)) { SafeCascade = false; continue; }
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Old, ck::TransientEntity(), Children))
            { SafeCascade = false; continue; }
            for (auto Child : Children) { if (!_LocalRetiring.Contains(Child)) { SafeCascade = false; } }
            if (CkTransformShared::IsLive(Old))
            { for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Old))
              { if (!_KnownLocal.Contains(Child) || !_LocalRetiring.Contains(Child)) { SafeCascade = false; } } }
        }
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(_LocalOwner) && !_LocalRetiring.Contains(_LocalOwner)) { SafeCascade = false; }
        for (auto Pending : _LocalPending)
        {
            auto Target = Pending.Get_EntityUnderConstruction();
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) && !_LocalRetiring.Contains(Target)) { SafeCascade = false; }
        }
        if (!SafeCascade) { _LocalRetirementUnproven = true; }
        // Pending-target/owner destruction is already covered by this exact ledger loop.
        if (SafeCascade && !Request_RetireLocalLeaves()) { _LocalRetirementUnproven = true; }
        _LocalOwner = FCk_Handle();
        _LocalStations.Empty(); _LocalPending.Empty(); _LocalCompleted.Empty();
        _LocalConstructed.Empty();
        // Keep the provenance journal until the captured retirement has fully drained.
        _LocalRequired.Empty(); _LocalRequiredParents.Empty();
        _LocalStarting = false; _ShownSequence = 0; _InstalledLiveSample = FCkTransformGym_Snapshot();
    }

    private bool Get_HasSafeLocalCapturedCascade()
    {
        if (_LocalRetirementUnproven || _KnownLocal.Num() != _KnownLocalParents.Num()) { return false; }
        for (auto Old : _LocalRetiring)
        {
            if (!_KnownLocal.Contains(Old)) { return false; }
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Old, ck::TransientEntity(), Children)) { return false; }
            for (auto Child : Children) { if (!_LocalRetiring.Contains(Child)) { return false; } }
            if (CkTransformShared::IsLive(Old))
            { for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Old))
              { if (!_KnownLocal.Contains(Child) || !_LocalRetiring.Contains(Child)) { return false; } } }
        }
        return true;
    }
    private bool Request_RetireLocalLeaves()
    {
        for (;;)
        {
            bool HasLive = false; bool Progress = false;
            for (auto Old : _LocalRetiring)
            {
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old) || !CkTransformShared::IsLive(Old)) { continue; }
                HasLive = true;
                if (!Get_HasSafeLocalCapturedCascade()) { return false; }
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Old, ck::TransientEntity(), Children)) { return false; }
                bool ChildrenPending = true;
                for (auto Child : Children)
                {
                    if (!_LocalRetiring.Contains(Child)) { return false; }
                    if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child) && CkTransformShared::IsLive(Child)) { ChildrenPending = false; }
                }
                for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Old))
                {
                    if (!_KnownLocal.Contains(Child) || !_LocalRetiring.Contains(Child)) { return false; }
                    if (CkTransformShared::IsLive(Child)) { ChildrenPending = false; }
                }
                if (!ChildrenPending) { continue; }
                utils_entity_lifetime::Request_DestroyEntity(Old);
                if (CkTransformShared::IsLive(Old) || !Get_HasSafeLocalCapturedCascade()) { return false; }
                Progress = true;
            }
            if (!HasLive) { return Get_HasSafeLocalCapturedCascade(); }
            if (!Progress) { return false; }
        }
        return false; // AngelScript requires a syntactic return after the open-ended loop.
    }
    private bool Get_LocalRetired()
    {
        if (_LocalRetirementUnproven) { return false; }
        if (_LocalRetiring.IsEmpty()) { return true; }
        for (auto Old : _LocalRetiring)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old)) { return false; } }
        _LocalRetiring.Empty();
        _KnownLocal.Empty(); _KnownLocalParents.Empty();
        return true;
    }

    private void Request_StartLocal(FCkTransformGym_Snapshot InSample)
    {
        auto State = CkTransformShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || State != _BoundState || State.GetWorld() != GetWorld() ||
            !Get_LocalRetired() || !State.Get_IsCompleteSnapshot(InSample)) { return; }
        auto LocalActorOwner = ck::ToEntity(this);
        if (!CkTransformShared::IsLive(LocalActorOwner) ||
            !utils_entity_lifetime::Get_CanCreateEntity(LocalActorOwner))
        { Request_FailLocal("Transform local controller entity unavailable"); return; }
        _LocalOwner = utils_entity_lifetime::Request_CreateEntity(LocalActorOwner);
        if (!CkTransformShared::IsLive(_LocalOwner))
        { Request_FailLocal("Transform local owner allocation failed"); return; }
        Request_RememberLocal(_LocalOwner);
        _LocalGeneration = InSample.Generation;
        _LocalStarting = true;
        _LocalStartedAt = float(System::GetGameTimeInSeconds());
        for (int32 i = 0; i < CkTransformShared::SlotCount; i++)
        {
            auto Params = InSample.Stations[i];
            if (!CkTransformShared::Get_IsExactWireParams(Params, i))
            { Request_FailLocal("Transform local catalog invalid"); return; }
            auto Pending = utils_entity_script::Request_SpawnEntity(_LocalOwner,
                UCk_EntityScript_GymStation, FInstancedStruct::Make(Params),
                FCk_Delegate_Request_OnCompleted(this, n"OnLocalSpawnCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_FailLocal("Transform local station spawn rejected"); return; }
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
        if (!CkTransformShared::IsLive(InEntity)) { Request_FailLocal("Transform invalid local completion"); return; }
        if (_LocalPending.Num() != CkTransformShared::SlotCount ||
            _LocalStations.Num() != CkTransformShared::SlotCount ||
            _LocalCompleted.Num() != CkTransformShared::SlotCount ||
            _LocalConstructed.Num() != CkTransformShared::SlotCount)
        { Request_FailLocal("Transform local callback cardinality invalid"); return; }
        for (int32 i = 0; i < _LocalPending.Num(); i++)
        {
            if (_LocalPending[i].Get_EntityUnderConstruction() != InEntity) { continue; }
            if (i >= _LocalCompleted.Num() || _LocalCompleted[i] != 0 ||
                InResult != ECk_Request_OperationResult::Succeeded)
            { Request_FailLocal("Transform local station completion invalid"); return; }
            TArray<FCk_Handle> Geometry, GeometryParents;
            if (!CkTransformShared::TryGet_RequiredGeometry(InEntity,Geometry,GeometryParents))
            { Request_FailLocal("Transform local geometry closure rejected"); return; }
            for (int32 j=0; j<Geometry.Num(); ++j)
            { _LocalRequired.Add(Geometry[j]); _LocalRequiredParents.Add(GeometryParents[j]); }
            Request_RememberLocal(InEntity);
            _LocalCompleted[i] = 1;
            return;
        }
        Request_FailLocal("Unexpected Transform local station completion");
    }

    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript InScript)
    {
        if (!_LocalStarting) { return; }
        if (_LocalPending.Num() != CkTransformShared::SlotCount ||
            _LocalStations.Num() != CkTransformShared::SlotCount ||
            _LocalCompleted.Num() != CkTransformShared::SlotCount ||
            _LocalConstructed.Num() != CkTransformShared::SlotCount)
        { Request_FailLocal("Transform local callback cardinality invalid"); return; }
        auto Entity = FCk_Handle(InScript);
        if (!CkTransformShared::IsLive(Entity)) { Request_FailLocal("Transform invalid local constructed callback"); return; }
        for (int32 i = 0; i < _LocalPending.Num(); i++)
        {
            if (_LocalPending[i].Get_EntityUnderConstruction() != Entity) { continue; }
            if (i >= _LocalConstructed.Num() || _LocalConstructed[i] != 0 ||
                !CkTransformShared::IsLive(Entity) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Entity) ||
                utils_entity_lifetime::Get_LifetimeOwner(Entity) != _LocalOwner ||
                CkTransformShared::IsLive(_LocalStations[i]))
            { Request_FailLocal("Transform local station constructed identity invalid"); return; }
            _LocalStations[i] = Entity;
            _LocalConstructed[i] = 1;
            Request_RememberLocal(Entity);
            return;
        }
        Request_FailLocal("Unexpected Transform local station");
    }

    private void Request_FailLocal(FString InReason)
    {
        _LocalFailed = true;
        Request_RetireLocal();
        ck::Error("[StationTransform] " + InReason);
    }

    private void Request_RememberLocal(FCk_Handle InRoot)
    {
        if (_KnownLocal.Num() != _KnownLocalParents.Num()) { _LocalRetirementUnproven = true; return; }
        TArray<FCk_Handle> Queue; Queue.Add(InRoot); TArray<FCk_Handle> Seen;
        for (int32 i=0; i<Queue.Num(); ++i)
        {
            auto H=Queue[i]; if (Seen.Contains(H)) { continue; } Seen.Add(H);
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,ck::TransientEntity(),Children))
            { _LocalRetirementUnproven=true; continue; }
            auto Parent=utils_entity_lifetime::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill);
            int32 Index=_KnownLocal.FindIndex(H);
            if (Index<0)
            {
                if (Parent!=_LocalOwner && Parent!=ck::ToEntity(this) && !_KnownLocal.Contains(Parent))
                { _LocalRetirementUnproven=true; continue; }
                _KnownLocal.Add(H); _KnownLocalParents.Add(Parent); Index=_KnownLocal.Num()-1;
            }
            if (Index>=_KnownLocalParents.Num() || Parent!=_KnownLocalParents[Index])
            { _LocalRetirementUnproven=true; continue; }
            for (auto Child:Children) { Queue.Add(Child); }
        }
    }

    UFUNCTION() private void OnPresentationTick()
    {
        if (!IsLocalController() || OwnerSessionFailed) { Request_RetireLocal(); _InstalledControlCommit = FCkTransformGym_ControlCommit(); return; }
        if (_PendingSerial > 0 && LastControlAttemptSerial == _PendingSerial &&
            LastControlResult > 1) { _PendingSerial = 0; }
        auto State = CkTransformShared::Get_State(GetWorld());
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld) || ck::Is_NOT_Valid(State) ||
            State.GetWorld() != CurrentWorld || CurrentWorld.GetGameState() != State ||
            State.GetClass() != ACk_TransformGym_GameState || !State.Get_HasPublishedCatalog())
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
            _InstalledLiveSample = Sample; _ShownSequence = Sample.Sequence;
            return;
        }
        if (_LocalGeneration != Sample.Generation)
        {
            Request_RetireLocal();
            _LocalGeneration = Sample.Generation;
            _LocalFailed = false;
        }
        if (_LocalFailed || !Get_LocalRetired()) { return; }
        if (!CkTransformShared::IsLive(_LocalOwner))
        { Request_StartLocal(Sample); return; }
        if (_LocalStarting)
        {
            if (float(System::GetGameTimeInSeconds()) - _LocalStartedAt > 20.0f)
            { Request_FailLocal("Transform local station readiness timed out"); return; }
            if (_LocalStations.Num() != CkTransformShared::SlotCount ||
                _LocalPending.Num() != CkTransformShared::SlotCount ||
                _LocalCompleted.Num() != CkTransformShared::SlotCount ||
                _LocalConstructed.Num() != CkTransformShared::SlotCount)
            { Request_FailLocal("Transform local callback cardinality invalid"); return; }
            for (int32 i = 0; i < CkTransformShared::SlotCount; i++)
            {
                if (_LocalCompleted[i] < 0 || _LocalCompleted[i] > 1 ||
                    _LocalConstructed[i] < 0 || _LocalConstructed[i] > 1)
                { Request_FailLocal("Transform local callback count invalid"); return; }
                if (_LocalCompleted[i] == 0 || _LocalConstructed[i] == 0) { return; }
            }
            if (!Get_HasExactLocalIdentity(Sample)) { return; }
            // GymStation publishes its display fragment on a timer after Construct.
            if (!Get_HasExactLocalCohort(Sample)) { return; }
            _LocalStarting = false;
        }
        if (!Get_HasExactLocalCohort(Sample))
        { Request_FailLocal("Transform local station/visual cohort lost"); return; }
        Request_RememberLocal(_LocalOwner);
        for (auto Station : _LocalStations) { Request_RememberLocal(Station); }
        // Pose comes from the locally applied production Transform fragments.
        auto Vector = Sample.Rows[5];
        auto VectorBridge = Vector.Cube.Get_SharedBridge();
        if (!CkTransformShared::IsLive(VectorBridge) || !utils_transform::Has(VectorBridge)) { return; }
        auto Origin = utils_transform::Get_EntityCurrentLocation(VectorBridge);
        auto Forward = utils_transform::Get_EntityForwardVector(VectorBridge);
        auto Right = utils_transform::Get_EntityRightVector(VectorBridge);
        auto Up = utils_transform::Get_EntityUpVector(VectorBridge);
        utils_debug_draw::DrawDebugLine(Origin, Origin + Forward * Vector.VectorLength, FLinearColor(1,0,0,1), 0.0f, 2.0f);
        utils_debug_draw::DrawDebugLine(Origin, Origin + Right * Vector.VectorLength, FLinearColor(0,1,0,1), 0.0f, 2.0f);
        utils_debug_draw::DrawDebugLine(Origin, Origin + Up * Vector.VectorLength, FLinearColor(0,0,1,1), 0.0f, 2.0f);
        utils_debug_draw::DrawDebugString(Origin + Forward * Vector.VectorLength, "FWD", FLinearColor(1,0,0,1), 0.0f);
        utils_debug_draw::DrawDebugString(Origin + Right * Vector.VectorLength, "RIGHT", FLinearColor(0,1,0,1), 0.0f);
        utils_debug_draw::DrawDebugString(Origin + Up * Vector.VectorLength, "UP", FLinearColor(0,0,1,1), 0.0f);
        if (_ShownSequence >= Sample.Sequence || Sample.Sequence <= 0) { return; }
        for (int32 i = 0; i < CkTransformShared::SlotCount; i++)
        {
            auto& Text = _LocalStations[i].AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            Text.Title = FText::FromString(Sample.StationTitles[i]);
            Text.Description = FText::FromString(Sample.StationBodies[i]);
            Text.Instructions = FText::FromString(Sample.StationInstructions[i]);
        }
        for (auto Row : Sample.Rows) { Row.Cube.TextRenderer.SetText(ck::Text(f"{Row.Behavior}")); }
        _InstalledLiveSample = Sample; _ShownSequence = Sample.Sequence;

    }

    private void Request_ConsumeCommit(ACk_TransformGym_GameState InState)
    {
        auto Commit = LastControlCommit;
        if (OwnerSessionFailed || Commit.DeliverySerial <= _AppliedDeliverySerial ||
            Commit.DeliverySerial <= LastAcknowledgedDeliverySerial || Commit.RequestSerial <= 0 ||
            Commit.OriginGeneration <= 0 || Commit.OriginGeneration >= 2147483646 ||
            Commit.Snapshot.Generation != Commit.OriginGeneration + 1 ||
            ck::Is_NOT_Valid(InState) || Commit.OriginState != InState || Commit.Owner != this)
        { return; }
        auto CurrentWorld = GetWorld();
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(CurrentWorld) || CurrentWorld.GetGameState() != InState ||
            InState.GetWorld() != CurrentWorld || InState.GetClass() != ACk_TransformGym_GameState ||
            !InState.Get_HasPublishedCatalog() || ck::Is_NOT_Valid(Pawn) ||
            Commit.OwnerPawn != Pawn || Pawn.GetWorld() != CurrentWorld || Pawn.GetController() != this ||
            !InState.Get_IsCompleteSnapshot(Commit.Snapshot)) { return; }
        // This whole typed cache is the real per-owner HUD consumer. Shared shells stay on live GS state.
        _InstalledControlCommit = Commit;
        _AppliedDeliverySerial = Commit.DeliverySerial;
        if (_PendingSerial == Commit.RequestSerial) { _PendingSerial = 0; }
        Server_AcknowledgeTransformCommit(InState, Pawn, Commit.DeliverySerial, Commit.RequestSerial);
    }

    private bool Get_HasExactLocalIdentity(FCkTransformGym_Snapshot InSample)
    {
        if (_LocalRequired.Num()==0 || _LocalRequired.Num()!=_LocalRequiredParents.Num()) { return false; }
        for (int32 i=0; i<_LocalRequired.Num(); ++i)
        {
            auto H=_LocalRequired[i]; auto Parent=_LocalRequiredParents[i];
            if (!CkTransformShared::IsLive(H) || !CkTransformShared::IsLive(Parent) ||
                !utils_entity_lifetime::Has_LifetimeOwner(H) || utils_entity_lifetime::Get_LifetimeOwner(H)!=Parent) { return false; }
        }
        auto WorldOwner = ck::TransientEntity();
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetWorld() != GetWorld() || Pawn.GetController() != this ||
            InSample.Rows.Num() != CkTransformShared::SlotCount) { return false; }
        TArray<ACk_TransformGym_Cube> Cubes; GetAllActorsOfClass(ACk_TransformGym_Cube, Cubes);
        if (Cubes.Num() != CkTransformShared::SlotCount) { return false; }
        for (int32 i = 0; i < CkTransformShared::SlotCount; ++i)
        {
            auto Cube = InSample.Rows[i].Cube;
            if (ck::Is_NOT_Valid(Cube) || !Cubes.Contains(Cube) || Cube.GetWorld() != GetWorld() ||
                Cube.GetClass() != ACk_TransformGym_Cube || Cube.HasAuthority() ||
                Cube.SharedSlot != i || Cube.SharedGeneration != InSample.Generation || Cube.Behavior != CkTransformShared::Get_Behavior(i)) { return false; }
            auto Bridge = Cube.Get_SharedBridge();
            if (!CkTransformShared::IsLive(Bridge) || utils_net::Get_EntityNetRole(Bridge) != ECk_Net_EntityNetRole::Proxy ||
                utils_net::Get_EntityNetMode(Bridge) != ECk_Net_NetModeType::Client) { return false; }
        }
        if (!CkTransformShared::IsLive(WorldOwner) || !CkTransformShared::IsLive(_LocalOwner) ||
            _LocalGeneration != InSample.Generation ||
            InSample.CatalogVersion != CkTransformShared::CatalogVersion ||
            _LocalStations.Num() != CkTransformShared::SlotCount ||
            _LocalPending.Num() != CkTransformShared::SlotCount) { return false; }
        for (int32 i = 0; i < CkTransformShared::SlotCount; i++)
        {
            auto H = _LocalStations[i];
            auto Tagged = utils_entity_tag::ForEach_Entity(WorldOwner,
                CkTransformShared::Get_StationTag(i));
            if (!CkTransformShared::IsLive(H) ||
                _LocalPending[i].Get_EntityUnderConstruction() != H ||
                !utils_entity_lifetime::Has_LifetimeOwner(H) ||
                utils_entity_lifetime::Get_LifetimeOwner(H) != _LocalOwner ||
                Tagged.Num() != 1 || Tagged[0] != H ||
                !CkTransformShared::Get_IsExactStationRecipe(H, i))
            { return false; }
        }
        return true;
    }

    private bool Get_HasExactLocalCohort(FCkTransformGym_Snapshot InSample)
    {
        if (!Get_HasExactLocalIdentity(InSample)) { return false; }
        for (int32 i = 0; i < CkTransformShared::SlotCount; i++)
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
