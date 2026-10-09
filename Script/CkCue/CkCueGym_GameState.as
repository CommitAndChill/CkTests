// Language=angelscript
// One six-demo authority cohort; local controllers own presentation.
class ACk_CueGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkCueGym_Snapshot Snapshot;
    UPROPERTY() FCk_Handle SourceOwner;
    UPROPERTY() TArray<FCk_Handle> ServerRoles;
    private TArray<FCk_Handle_PendingEntityScript> _Pending;
    private TArray<FCk_Handle> _Carriers;
    private TArray<bool> _Constructed;
    private TArray<FString> _Bodies;
    private TArray<FString> _Instructions;
    private TArray<bool> _HasDisplay;
    private TArray<FCk_Handle> _RetirementLedger;
    private TArray<FCk_Handle> _RetiringRoots;
    private UWorld _SourceWorld;
    private FCk_Handle _WorldRoot;
    private FCkGym_Entry _CatalogEntry;
    private int32 _CatalogIndex = -1;
    private int32 _ResetRole = -1;
    private bool _Attempted = false;
    private bool _Creating = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private bool _TransientIntakeFailed = false;
    private double _BoundaryStartedAt = 0.0;
    private double _PendingSince = -1.0;
    private double _LastPublishedAt = -1.0;
    private int64 _FrameSerial = 0;
    private int64 _RequestedTransientSerial = 0;
    private int32 _ReceivedTransientEpoch = 0;
    private int64 _ReceivedTransientSerial = 0;
    private TArray<FCk_Handle> _TransientReceipts;
    private TArray<int32> _TransientEpochs;

    bool TryMakeTransientParams(UCk_CueGym_SourceDemo Demo,
        FTransform Pose, FCkCueGym_SpawnParams&out Params)
    {
        Params = FCkCueGym_SpawnParams();
        if (ck::Is_NOT_Valid(Demo) || Demo.SourceRole != 4 ||
            !Get_IsSourceContext(Demo, 4, Demo.SourceGeneration,
                Demo.RoleEpoch, ck::ToEntity(Demo)) ||
            !Pose.IsValid() || Pose.ContainsNaN()) { return false; }
        if (_RequestedTransientSerial >= 9223372036854775806)
        { Request_Fail("Transient event serial exhausted"); return false; }
        _RequestedTransientSerial += 1;
        Params.InitialTransform = Pose;
        // Event generation belongs to the transient role epoch, so unrelated
        // station resets never change the token or require a history ledger.
        Params.SourceGeneration = Snapshot.TransientEpochStartGeneration;
        Params.RoleEpoch = Snapshot.RoleEpochs[4];
        Params.EventSerial = _RequestedTransientSerial;
        return true;
    }

    ECkCueGym_EventAdmission Get_TransientAdmission(int32 Generation,
        int32 Epoch, int64 Serial)
    {
        auto World = GetWorld();
        if (_Ending || _Failed || _TransientIntakeFailed || IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(World) || World.GetGameState() != this ||
            GetClass() != ACk_CueGym_GameState ||
            Generation <= 0 || Epoch <= 0 || Serial <= 0)
        { return ECkCueGym_EventAdmission::Rejected; }
        auto Root = ck::TransientEntity();
        if (!IsLive(Root) || CkGym_Admission::Get_IsClosedUnproven(Root))
        { return ECkCueGym_EventAdmission::Rejected; }
        if (!Get_HasPublishedCatalog()) { return ECkCueGym_EventAdmission::Pending; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num() || Entries[Index].DisplayName != "Cue" ||
            Entries[Index].GameModeClass != ACk_CueGym_GameMode ||
            !CkCueGymWire::Get_IsValidSnapshot(Snapshot) ||
            Snapshot.Lifecycle == ECkCueGym_Lifecycle::Failed)
        { return ECkCueGym_EventAdmission::Rejected; }
        if (Snapshot.Generation == 0 || Snapshot.RoleEpochs.Num() != 6 ||
            Generation > Snapshot.Generation || Epoch > Snapshot.RoleEpochs[4])
        { return ECkCueGym_EventAdmission::Pending; }
        if (Epoch < Snapshot.RoleEpochs[4] ||
            Generation != Snapshot.TransientEpochStartGeneration ||
            (Snapshot.Lifecycle == ECkCueGym_Lifecycle::Retiring &&
             (Snapshot.ResetRole == -1 || Snapshot.ResetRole == 4)))
        { return ECkCueGym_EventAdmission::Rejected; }
        const int64 Issued = HasAuthority() ? _RequestedTransientSerial :
            Snapshot.TransientRequestedSerial;
        if (Serial > Issued) { return ECkCueGym_EventAdmission::Pending; }
        // Other stations may reset while this role's epoch and live relay cues
        // remain unchanged. A global generation change alone does not stale them.
        return ECkCueGym_EventAdmission::Accepted;
    }

    bool Get_HasTransientIntakeFailure() const { return _TransientIntakeFailed; }

    private bool Get_HasCueRelayOwner(FCk_Handle Entity)
    {
        auto Root = ck::TransientEntity();
        if (!IsLive(Entity) || !IsLive(Root) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Entity)) { return false; }
        auto Channel = utils_entity_lifetime::Get_LifetimeOwner(Entity);
        if (!IsLive(Channel) || !CueGym_Pmg::Get_HasDrawingContext(Channel, Root))
        { return false; }
        auto Relay = Cast<ACk_CueRelay_UE>(utils_owning_actor::TryGet_EntityOwningActor(Channel));
        return ck::IsValid(Relay) && Relay.GetWorld() == GetWorld() &&
            !Relay.IsActorBeingDestroyed() && Relay.GetClass() == ACk_CueRelay_UE &&
            utils_owning_actor::TryGet_ActorEntityHandle(Relay) == Channel;
    }

    bool TryRegisterTransient(UCk_CueGym_Cue_Transient Cue,
        int32 Generation, int32 Epoch, int64 Serial, FCk_Handle Entity)
    {
        if (ck::Is_NOT_Valid(Cue) || Cue.GetClass() != UCk_CueGym_Cue_Transient ||
            Entity != ck::ToEntity(Cue) ||
            !Get_HasCueRelayOwner(Entity) ||
            Get_TransientAdmission(Generation, Epoch, Serial) != ECkCueGym_EventAdmission::Accepted)
        { return false; }
        if (_ReceivedTransientEpoch != Epoch)
        { _ReceivedTransientEpoch = Epoch; _ReceivedTransientSerial = 0; }
        // Unreliable relay delivery can skip serials. Duplicate or reordered
        // events are rejected; no historical replay is manufactured for late join.
        if (Serial <= _ReceivedTransientSerial || _TransientReceipts.Contains(Entity))
        { return false; }
        _ReceivedTransientSerial = Serial;
        _TransientReceipts.Add(Entity);
        _TransientEpochs.Add(Epoch);
        return true;
    }

    UFUNCTION(BlueprintPure)
    int64 Get_ReceivedTransientSerial() const { return _ReceivedTransientSerial; }
    UFUNCTION(BlueprintPure)
    int32 Get_ReceivedTransientEpoch() const { return _ReceivedTransientEpoch; }
    UFUNCTION(BlueprintPure)
    TArray<FCk_Handle> Get_TransientReceipts() const { return _TransientReceipts; }

    private void MonitorTransientReceipts()
    {
        auto Root = ck::TransientEntity();
        if (_TransientReceipts.Num() != _TransientEpochs.Num())
        {
            _TransientIntakeFailed = true;
            if (IsLive(Root)) { CkGym_Admission::TryCloseWorldUnproven(Root); }
            if (HasAuthority()) { Request_Fail("Transient receipt ledger malformed"); }
        }
        for (int32 Index = _TransientReceipts.Num() - 1; Index >= 0; --Index)
        {
            auto Entity = _TransientReceipts[Index];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Entity))
            { _TransientReceipts.RemoveAt(Index); if (Index < _TransientEpochs.Num()) { _TransientEpochs.RemoveAt(Index); } continue; }
            const bool Reject = _TransientIntakeFailed || _Failed || Snapshot.Lifecycle == ECkCueGym_Lifecycle::Failed ||
                CkGym_Admission::Get_IsClosedUnproven(Root) ||
                (Snapshot.RoleEpochs.Num() == 6 && Index < _TransientEpochs.Num() && _TransientEpochs[Index] < Snapshot.RoleEpochs[4]) ||
                (Snapshot.Lifecycle == ECkCueGym_Lifecycle::Retiring &&
                 (Snapshot.ResetRole == -1 || Snapshot.ResetRole == 4));
            if (Reject && IsLive(Entity) &&
                CueGym_Pmg::Get_HasDrawingContext(Entity, Root))
            { utils_entity_lifetime::Request_DestroyEntity(Entity); }
        }
    }

    private bool IsLive(FCk_Handle Handle)
    {
        return ck::IsValid(Handle) && !utils_entity_lifetime::Get_IsPendingDestroy(
            Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    private bool IsDirect(FCk_Handle Child, FCk_Handle Parent)
    {
        return IsLive(Child) && IsLive(Parent) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }

    private bool HasWorldIdentity()
    {
        if (_Ending || IsActorBeingDestroyed() || !HasAuthority() ||
            !Get_HasPublishedCatalog() || GetClass() != ACk_CueGym_GameState)
        { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != this ||
            (_Attempted && World != _SourceWorld)) { return false; }
        auto Mode = Gameplay::GetGameMode();
        if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
            Mode.GetClass() != ACk_CueGym_GameMode) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Cue" || Entry.GameModeClass != ACk_CueGym_GameMode)
        { return false; }
        return _CatalogIndex < 0 || (_CatalogIndex == Index &&
            _CatalogEntry.DisplayName == Entry.DisplayName &&
            _CatalogEntry.GameModeClass == Entry.GameModeClass &&
            _CatalogEntry.LevelName == Entry.LevelName &&
            _CatalogEntry.Category == Entry.Category &&
            _CatalogEntry.HintCode == Entry.HintCode);
    }

    private bool HasSourceOwner()
    {
        return _WorldRoot == ck::TransientEntity() && IsLive(_WorldRoot) &&
            !CkGym_Admission::Get_IsClosedUnproven(_WorldRoot) &&
            IsDirect(SourceOwner, _WorldRoot);
    }

    bool Get_IsSourceContext(UCk_CueGym_SourceDemo Demo, int32 Role,
        int32 Generation, int32 Epoch, FCk_Handle Entity)
    {
        if (_Failed || !HasWorldIdentity() || !HasSourceOwner() ||
            Role < 0 || Role >= 6 || ServerRoles.Num() != 6 ||
            Snapshot.RoleEpochs.Num() != 6 || ck::Is_NOT_Valid(Demo) ||
            Demo.SourceStateRef.Get() != this ||
            (Demo.SourceState != nullptr && Demo.SourceState != this) || Demo.SourceRole != Role ||
            Demo.SourceGeneration != Generation || Demo.RoleEpoch != Epoch ||
            Entity != ServerRoles[Role] || Entity != ck::ToEntity(Demo) ||
            !IsDirect(Entity, SourceOwner) || Generation != Snapshot.Generation ||
            Epoch != Snapshot.RoleEpochs[Role] ||
            Demo.GetClass() != Get_RoleClass(Role)) { return false; }
        // Construction needs to issue the first real Cue requests. Retiring
        // shuts every producer before a selected role is destroyed.
        return Snapshot.Lifecycle == ECkCueGym_Lifecycle::Initializing ||
            Snapshot.Lifecycle == ECkCueGym_Lifecycle::Active;
    }

    private TSubclassOf<UCk_EntityScript_UE> Get_RoleClass(int32 Role)
    {
        if (Role == 0) { return UCk_EntityScript_CueGym_Lifetime; }
        if (Role == 1) { return UCk_EntityScript_CueGym_Concurrency; }
        if (Role == 2) { return UCk_EntityScript_CueGym_OwnerValidation; }
        if (Role == 3) { return UCk_EntityScript_CueGym_Restart; }
        if (Role == 4) { return UCk_EntityScript_CueGym_Transient; }
        if (Role == 5) { return UCk_EntityScript_CueGym_OwnerDestruction; }
        return nullptr;
    }

    void Request_Fail(FString Reason)
    {
        if (_Failed || _Ending || !HasAuthority()) { return; }
        _Failed = true;
        _Creating = false;
        if (IsLive(_WorldRoot) && _WorldRoot == ck::TransientEntity())
        { CkGym_Admission::TryCloseWorldUnproven(_WorldRoot); }
        if (Snapshot.Sequence >= 9223372036854775807) { return; }
        FCkCueGym_Snapshot Next;
        Next.Generation = Math::Max(1, Snapshot.Generation);
        Next.Sequence = Snapshot.Sequence + 1;
        Next.RoleEpochs = Snapshot.RoleEpochs;
        Next.TransientEpochStartGeneration = Math::Max(1, Snapshot.TransientEpochStartGeneration);
        Next.TransientRequestedSerial = Math::Max(_RequestedTransientSerial, Snapshot.TransientRequestedSerial);
        if (Next.RoleEpochs.Num() != 6)
        { Next.RoleEpochs.Reset(); for (int32 Role = 0; Role < 6; ++Role) { Next.RoleEpochs.Add(1); } }
        Next.Lifecycle = ECkCueGym_Lifecycle::Failed;
        Next.FailureReason = Reason.IsEmpty() ? "Cue source failed" : Reason;
        Snapshot = Next;
        if (ck::IsValid(GetWorld()) && !IsActorBeingDestroyed()) { ForceNetUpdate(); }
        // Retain partial first-return ownership until world/session teardown.
    }

    private bool TryPublish(FCkCueGym_Snapshot Next)
    {
        if (_Failed || !HasWorldIdentity() ||
            !CkCueGymWire::Get_IsValidPublicationAdvance(Snapshot, Next, _ResetRole))
        { return false; }
        Snapshot = Next;
        ForceNetUpdate();
        return !_Failed && HasWorldIdentity();
    }

    void SubmitDisplay(UCk_CueGym_SourceDemo Demo, int32 Role,
        int32 Generation, int32 Epoch, FString Title, FString Body, FString Instructions)
    {
        if (!Get_IsSourceContext(Demo, Role, Generation, Epoch, ck::ToEntity(Demo)))
        { return; }
        if (_Bodies.Num() != 6 || _Instructions.Num() != 6 || _HasDisplay.Num() != 6 ||
            Title.IsEmpty() || Instructions.IsEmpty())
        { Request_Fail("Complete source display rejected"); return; }
        _Bodies[Role] = Body;
        _Instructions[Role] = Instructions;
        _HasDisplay[Role] = true;
    }

    private void SpawnRole(int32 Role)
    {
        FVector Origin;
        if (_Failed || !HasWorldIdentity() || !HasSourceOwner() ||
            !CkCueGymCatalog::TryGet_Origin(Role, Origin))
        { Request_Fail("Source spawn context rejected"); return; }
        FCkCueGym_SourceSpawnParams Params;
        Params.SourceStateRef = this;
        Params.SourceRole = Role;
        // Producer identity follows the current cohort generation. Transient
        // event tokens separately retain their role epoch's start generation.
        Params.SourceGeneration = Snapshot.Generation;
        Params.RoleEpoch = Snapshot.RoleEpochs[Role];
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Origin);
        _Pending[Role] = utils_entity_script::Request_SpawnEntity(
            SourceOwner, Get_RoleClass(Role), FInstancedStruct::Make(Params));
        if (ck::Is_NOT_Valid(_Pending[Role]))
        { Request_Fail("Role pending allocation rejected"); return; }
        ServerRoles[Role] = _Pending[Role].Get_EntityUnderConstruction();
        if (!IsDirect(ServerRoles[Role], SourceOwner) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrier(
                ServerRoles[Role], SourceOwner, Get_RoleClass(Role), _WorldRoot, _Carriers[Role]))
        { Request_Fail("Role first-return target/carrier proof rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_Pending[Role],
            FCk_Delegate_EntityScript_Constructed(this, n"OnRoleConstructed"));
    }

    UFUNCTION()
    private void OnRoleConstructed(FCk_Handle_EntityScript Handle)
    {
        if (_Failed || !HasWorldIdentity() || !HasSourceOwner() ||
            Snapshot.Lifecycle != ECkCueGym_Lifecycle::Initializing) { return; }
        auto Entity = FCk_Handle(Handle);
        for (int32 Role = 0; Role < 6; ++Role)
        {
            if (Entity != ServerRoles[Role]) { continue; }
            auto Demo = Cast<UCk_CueGym_SourceDemo>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity));
            if (_Constructed[Role] || ck::Is_NOT_Valid(Demo) || Demo.SourceState != this ||
                !Get_IsSourceContext(Demo, Role, Demo.SourceGeneration, Demo.RoleEpoch, Entity))
            { Request_Fail("Constructed source identity rejected"); return; }
            _Constructed[Role] = true;
            return;
        }
        // A callback from an already drained earlier epoch cannot adopt a role.
    }

    void Request_EnsureSharedDemo()
    {
        if (_Attempted || _Failed || !HasWorldIdentity()) { return; }
        _SourceWorld = GetWorld();
        _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        _Attempted = true;
        _WorldRoot = ck::TransientEntity();
        if (!IsLive(_WorldRoot) || CkGym_Admission::Get_IsClosedUnproven(_WorldRoot) ||
            !utils_entity_lifetime::Get_CanCreateEntity(_WorldRoot))
        { Request_Fail("Authority ECS root unavailable"); return; }
        FCkCueGym_Snapshot Initial;
        Initial.Generation = 1;
        Initial.Sequence = 1;
        Initial.TransientEpochStartGeneration = 1;
        for (int32 Role = 0; Role < 6; ++Role) { Initial.RoleEpochs.Add(1); }
        if (!TryPublish(Initial)) { Request_Fail("Initial publication rejected"); return; }
        SourceOwner = utils_entity_lifetime::Request_CreateEntity(_WorldRoot);
        if (!HasSourceOwner()) { Request_Fail("Source owner allocation rejected"); return; }
        ServerRoles.SetNum(6); _Pending.SetNum(6); _Carriers.SetNum(6);
        _Constructed.SetNum(6); _Bodies.SetNum(6); _Instructions.SetNum(6); _HasDisplay.SetNum(6);
        _Creating = true;
        _BoundaryStartedAt = System::GetGameTimeInSeconds();
        for (int32 Role = 0; Role < 6; ++Role)
        { SpawnRole(Role); if (_Failed) { return; } }
        _Creating = false;
    }

    private bool HasCompleteRoles()
    {
        if (_Failed || _Creating || !HasWorldIdentity() || !HasSourceOwner() ||
            ServerRoles.Num() != 6 || _Constructed.Num() != 6) { return false; }
        for (int32 Role = 0; Role < 6; ++Role)
        {
            if (!_Constructed[Role] || !_HasDisplay[Role] || !IsDirect(ServerRoles[Role], SourceOwner))
            { return false; }
            auto Demo = Cast<UCk_CueGym_SourceDemo>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerRoles[Role]));
            if (ck::Is_NOT_Valid(Demo) || !Get_IsSourceContext(Demo, Role,
                Demo.SourceGeneration, Demo.RoleEpoch, ServerRoles[Role])) { return false; }
            bool Allocated = false;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrier(
                _Carriers[Role], ServerRoles[Role], SourceOwner, _WorldRoot, Allocated))
            { Request_Fail("Captured role carrier lost provenance"); return false; }
            if (Allocated) { return false; }
        }
        return true;
    }

    private bool TryCaptureRetirement(int32 ResetRole)
    {
        _RetirementLedger.Reset();
        _RetiringRoots.Reset();
        for (int32 Role = 0; Role < 6; ++Role)
        {
            if (ResetRole != -1 && ResetRole != Role) { continue; }
            if (!IsDirect(ServerRoles[Role], SourceOwner)) { return false; }
            _RetiringRoots.Add(ServerRoles[Role]);
            _RetirementLedger.Add(ServerRoles[Role]);
        }
        for (int32 Index = 0; Index < _RetirementLedger.Num(); ++Index)
        {
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                _RetirementLedger[Index], _WorldRoot, Children)) { return false; }
            for (auto Child : Children)
            {
                if (_RetirementLedger.Contains(Child)) { return false; }
                _RetirementLedger.Add(Child);
            }
        }
        // Independent owner-fragment census checks that the reverse walk did
        // not omit a current child; it does not grant provenance for new objects.
        return UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
            _RetirementLedger, _RetirementLedger, _WorldRoot);
    }

    bool Request_ResetSharedDemo(ACk_CueGym_PlayerController Requester,
        APawn Pawn, int32 ResetRole, int32 ExpectedGeneration)
    {
        if (_Failed || !HasWorldIdentity() || !HasSourceOwner() ||
            Snapshot.Lifecycle != ECkCueGym_Lifecycle::Active ||
            ExpectedGeneration != Snapshot.Generation ||
            (ResetRole != -1 && (ResetRole < 0 || ResetRole >= 6)) ||
            ck::Is_NOT_Valid(Requester) || Requester.GetWorld() != _SourceWorld ||
            Requester.GetClass() != ACk_CueGym_PlayerController ||
            !Requester.HasAuthority() || Requester.IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(Pawn) || Pawn.GetWorld() != _SourceWorld ||
            Pawn.GetController() != Requester || Requester.GetControlledPawn() != Pawn)
        { return false; }
        if (Snapshot.Generation >= 2147483647 || Snapshot.Sequence >= 9223372036854775805)
        { Request_Fail("Reset counters exhausted"); return false; }
        for (int32 Role = 0; Role < 6; ++Role)
        {
            if ((ResetRole == -1 || ResetRole == Role) && Snapshot.RoleEpochs[Role] >= 2147483647)
            { Request_Fail("Role epoch exhausted"); return false; }
        }
        if (!HasCompleteRoles() || !TryCaptureRetirement(ResetRole))
        { Request_Fail("Reset ownership closure rejected"); return false; }
        FCkCueGym_Snapshot Retiring;
        Retiring.Generation = Snapshot.Generation;
        Retiring.RoleEpochs = Snapshot.RoleEpochs;
        Retiring.Sequence = Snapshot.Sequence + 1;
        Retiring.Lifecycle = ECkCueGym_Lifecycle::Retiring;
        Retiring.ResetRole = ResetRole;
        Retiring.TransientRequestedSerial = Snapshot.TransientRequestedSerial;
        Retiring.TransientEpochStartGeneration = Snapshot.TransientEpochStartGeneration;
        _ResetRole = ResetRole;
        if (!TryPublish(Retiring)) { Request_Fail("Retirement publication rejected"); return false; }
        MonitorTransientReceipts();
        _BoundaryStartedAt = System::GetGameTimeInSeconds();
        for (auto Root : _RetiringRoots)
        {
            if (_Failed || !HasWorldIdentity() || !HasSourceOwner() || !IsDirect(Root, SourceOwner))
            { Request_Fail("Retirement root changed"); return false; }
            utils_entity_lifetime::Request_DestroyEntity(Root);
        }
        return !_Failed;
    }

    private void AdvanceRetirement()
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
            _RetirementLedger, _RetirementLedger, _WorldRoot))
        { Request_Fail("Frozen retirement membership changed"); return; }
        for (auto Entity : _RetirementLedger)
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Entity))
            {
                if (System::GetGameTimeInSeconds() - _BoundaryStartedAt > 20.0)
                { Request_Fail("Physical retirement did not complete"); }
                return;
            }
        }
        FCkCueGym_Snapshot Initial;
        Initial.Generation = Snapshot.Generation + 1;
        Initial.Sequence = Snapshot.Sequence + 1;
        Initial.RoleEpochs = Snapshot.RoleEpochs;
        const int64 NextTransientSerial = (_ResetRole == -1 || _ResetRole == 4) ? 0 : _RequestedTransientSerial;
        Initial.TransientRequestedSerial = NextTransientSerial;
        Initial.TransientEpochStartGeneration = (_ResetRole == -1 || _ResetRole == 4) ?
            Initial.Generation : Snapshot.TransientEpochStartGeneration;
        for (int32 Role = 0; Role < 6; ++Role)
        {
            if (_ResetRole == -1 || _ResetRole == Role)
            { Initial.RoleEpochs[Role] += 1; }
        }
        // While still Retiring, rebind preserved sources to the new generation.
        // Selected sources have physically retired; no old callback is adopted.
        for (int32 Role = 0; Role < 6; ++Role)
        {
            if (_ResetRole == -1 || _ResetRole == Role)
            {
                ServerRoles[Role] = FCk_Handle();
                _Pending[Role] = FCk_Handle_PendingEntityScript();
                _Carriers[Role] = FCk_Handle();
                _Constructed[Role] = false;
                _HasDisplay[Role] = false;
                _Bodies[Role] = "";
                _Instructions[Role] = "";
                continue;
            }
            if (!IsDirect(ServerRoles[Role], SourceOwner))
            { Request_Fail("Preserved reset role lost"); return; }
            auto Demo = Cast<UCk_CueGym_SourceDemo>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerRoles[Role]));
            if (ck::Is_NOT_Valid(Demo) || Demo.SourceState != this ||
                Demo.SourceRole != Role || Demo.SourceGeneration != Snapshot.Generation ||
                Demo.RoleEpoch != Snapshot.RoleEpochs[Role])
            { Request_Fail("Preserved source reset identity changed"); return; }
            Demo.SourceGeneration = Initial.Generation;
        }
        if (!TryPublish(Initial)) { Request_Fail("New source generation rejected"); return; }
        _RequestedTransientSerial = NextTransientSerial;
        _Creating = true;
        _BoundaryStartedAt = System::GetGameTimeInSeconds();
        _PendingSince = -1.0;
        for (int32 Role = 0; Role < 6; ++Role)
        {
            if (_ResetRole == -1 || _ResetRole == Role)
            { SpawnRole(Role); if (_Failed) { return; } }
        }
        _Creating = false;
        _RetirementLedger.Reset();
        _RetiringRoots.Reset();
    }

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    {
        SetTickGroup(ETickingGroup::TG_PostUpdateWork);
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!_Ending) { MonitorTransientReceipts(); }
        if (!HasAuthority() || _Ending || _Failed) { return; }
        if (!_Attempted) { Request_EnsureSharedDemo(); return; }
        if (!HasWorldIdentity() || !HasSourceOwner())
        { Request_Fail("Authority source context changed"); return; }
        if (_Creating) { return; }
        if (Snapshot.Lifecycle == ECkCueGym_Lifecycle::Retiring)
        { AdvanceRetirement(); return; }
        if (Snapshot.Sequence >= 9223372036854775806 || _FrameSerial >= 9223372036854775806)
        { Request_Fail("Source publication counters exhausted"); return; }
        if (!HasCompleteRoles())
        {
            if (Snapshot.Lifecycle == ECkCueGym_Lifecycle::Active ||
                System::GetGameTimeInSeconds() - _BoundaryStartedAt > 20.0)
            { Request_Fail("Complete source role readiness rejected"); }
            return;
        }
        TArray<FCkCueGym_SourceVisualRow> Rows;
        FString Failure;
        auto Result = UCkCueGym_SourceVisuals::TryGet_SourceVisuals(
            GetWorld(), SourceOwner, ServerRoles, Rows, Failure);
        if (_Failed) { return; }
        if (!HasWorldIdentity() || !HasSourceOwner())
        { Request_Fail("Source context changed during visual readback"); return; }
        if (Result == ECkCueGym_SourceVisualOutcome::Invalid)
        { Request_Fail(Failure); return; }
        if (Result == ECkCueGym_SourceVisualOutcome::Pending)
        {
            if (_PendingSince < 0.0) { _PendingSince = System::GetGameTimeInSeconds(); }
            if (System::GetGameTimeInSeconds() - _PendingSince > 20.0)
            { Request_Fail("Source visual settlement timed out"); }
            return;
        }
        _PendingSince = -1.0;
        if (System::GetGameTimeInSeconds() - _LastPublishedAt < 0.1) { return; }
        FCkCueGym_Snapshot Next;
        Next.Generation = Snapshot.Generation;
        Next.RoleEpochs = Snapshot.RoleEpochs;
        Next.Sequence = Snapshot.Sequence + 1;
        Next.FrameSerial = _FrameSerial + 1;
        Next.Lifecycle = ECkCueGym_Lifecycle::Active;
        Next.Bodies = _Bodies; Next.Instructions = _Instructions;
        Next.TransientRequestedSerial = _RequestedTransientSerial;
        Next.TransientEpochStartGeneration = Snapshot.TransientEpochStartGeneration;
        Next.VisualRowCount = Rows.Num();
        Next.VisualRows = Rows;
        if (!TryPublish(Next)) { Request_Fail("Complete frame publication rejected"); return; }
        _FrameSerial = Next.FrameSerial;
        _LastPublishedAt = System::GetGameTimeInSeconds();
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        _Creating = false;
    }
}
