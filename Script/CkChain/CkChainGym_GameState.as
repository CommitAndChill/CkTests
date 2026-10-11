// Four authority fixtures; clients consume complete value frames only.
class ACk_ChainGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkChainGym_WireSnapshot Snapshot;
    UPROPERTY(Replicated) bool SourceFailed = false;
    private TArray<FCkChainGym_Fixture> _Fixtures;
    private TArray<int64> _Generations;
    private TArray<FCk_Handle> _Retiring;
    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCkGym_Entry _Entry;
    private int32 _Index = -1;
    private bool _Captured = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private FString _FailureReason;
    private int32 _WaitingStation = -1;
    // 0 waiting, 1 prepared, 2 starting, 3 active, 4 retiring, 5 replacing, 6 command drain.
    private int32 _Phase = 0;
    private int32 _Replacing = -1;
    private bool _RetirementRequested = false;
    private bool _ReplacementStarted = false;
    private int32 _DrainTicks = 0;
    private float32 _Spacing = 10.0f;
    private double _PendingSince = 0.0;
    private double _PublishElapsed = 0.0;
    private bool _HasConsumed = false;
    private FCkChainGym_WireSnapshot _Consumed;
    private ACk_ChainGym_PlayerController _Requester;
    private APawn _RequesterPawn;
    private int32 _RequestSerial = 0;
    private int32 _RequestAction = -1;
    private int64 _RequestedGeneration = 0;
    private ACk_ChainGym_PlayerController _FollowOwner;
    private APawn _FollowPawn;

    UFUNCTION(BlueprintOverride)
    void BeginPlay() { SetTickGroup(ETickingGroup::TG_PostUpdateWork); }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_SourceRoot(int32 Index)
    { return HasAuthority() && Index >= 0 && Index < _Fixtures.Num() ? FCk_Handle(_Fixtures[Index].Root) : FCk_Handle(); }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_SourceHead(int32 Index)
    { return HasAuthority() && Index >= 0 && Index < _Fixtures.Num() ? FCk_Handle(_Fixtures[Index].Head) : FCk_Handle(); }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_SourceChain(int32 Index)
    { return HasAuthority() && Index >= 0 && Index < _Fixtures.Num() ? FCk_Handle(_Fixtures[Index].Chain) : FCk_Handle(); }

    UFUNCTION(BlueprintPure)
    bool Get_HasSessionFailure() const { return _Failed || SourceFailed; }

    UFUNCTION(BlueprintPure)
    FString Get_DiagnosticState() const
    {
        return f"phase={_Phase} waitingStation={_WaitingStation} fixtures={_Fixtures.Num()} captured={_Captured} failed={_Failed} reason={_FailureReason}";
    }

    private bool TryCapture()
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(World) ||
            World.GetGameState() != this || !Get_HasPublishedCatalog() ||
            (_Captured && World != _World)) { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_ChainGym_GameMode) { return false; }
        }
        else if (World.GetNetMode() != ENetMode::NM_Client) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Chain" || Entry.GameModeClass != ACk_ChainGym_GameMode ||
            (_Captured && (Index != _Index || Entry.DisplayName != _Entry.DisplayName ||
                Entry.GameModeClass != _Entry.GameModeClass || Entry.LevelName != _Entry.LevelName ||
                Entry.Category != _Entry.Category || Entry.HintCode != _Entry.HintCode)))
        { return false; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs)) { return false; }
        auto Root = Ecs.Get_TransientEntity();
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root) ||
            ck::Is_NOT_Valid(Root) ||
            utils_entity_lifetime::Get_IsPendingDestroy(Root, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            utils_entity_lifetime::Get_WorldForEntity(Root) != World ||
            (_Captured && Root != _WorldRoot)) { return false; }
        bool Closed = true;
        if (!UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(Root, Closed) || Closed)
        { return false; }
        if (!_Captured)
        { _World = World; _WorldRoot = Root; _Entry = Entry; _Index = Index; _Captured = true; }
        return true;
    }

    void Request_CloseClient(FString Reason)
    {
        if (HasAuthority() || _Failed || _Ending) { return; }
        _Failed = true;
        _FailureReason = Reason;
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        ck::Trace(f"Chain client presentation closed: {Reason}");
    }

    UFUNCTION()
    void Request_Fail(FString Reason)
    {
        if (!HasAuthority()) { Request_CloseClient(Reason); return; }
        if (_Failed || _Ending) { return; }
        _FailureReason = Reason;
        _Failed = true; SourceFailed = true; _Phase = 0;
        // Block all motion before any retirement can invoke a callback.
        for (int32 I = 0; I < _Fixtures.Num(); ++I) { _Fixtures[I].Request_Block(); }
        for (int32 I = 0; I < _Fixtures.Num(); ++I) { _Fixtures[I].Request_Destroy(); }
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        Snapshot.Phase = 4; Snapshot.Replacing = -1;
        if (Snapshot.Sequence < 9223372036854775806) { Snapshot.Sequence++; }
        ForceNetUpdate();
        CompleteRequest(false);
        _FollowOwner = nullptr; _FollowPawn = nullptr;
        ck::Trace(f"Chain authority bank closed: {Reason}");
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Ending || _Failed || _Phase != 0 || !TryCapture()) { return; }
        if (CkGym_Admission::Get_IsBusy(_WorldRoot)) { return; }
        _Phase = 1; _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        for (int32 I = 0; I < 4; ++I)
        { _Fixtures.Add(FCkChainGym_Fixture()); _Generations.Add(1); }
        for (int32 I = 0; I < 4; ++I)
        {
            if (!_Fixtures[I].Create(_WorldRoot, CkChainGym::Get_Origin(I), I, _Spacing))
            { Request_Fail(f"Chain station {I} composition failed"); return; }
        }
    }

    private bool HasRequestOwner()
    {
        if (_RequestSerial == 0) { return true; }
        return ck::IsValid(_Requester) && !_Requester.IsActorBeingDestroyed() &&
            _Requester.GetWorld() == _World && _Requester.HasAuthority() &&
            _Requester.GetClass() == ACk_ChainGym_PlayerController &&
            ck::IsValid(_RequesterPawn) && !_RequesterPawn.IsActorBeingDestroyed() &&
            _RequesterPawn.GetWorld() == _World && _Requester.GetControlledPawn() == _RequesterPawn &&
            _RequesterPawn.GetController() == _Requester;
    }

    private bool HasFollowOwner()
    {
        return ck::IsValid(_FollowOwner) && !_FollowOwner.IsActorBeingDestroyed() &&
            _FollowOwner.GetWorld() == _World && _FollowOwner.HasAuthority() &&
            ck::IsValid(_FollowPawn) && !_FollowPawn.IsActorBeingDestroyed() &&
            _FollowPawn.GetWorld() == _World && _FollowOwner.GetControlledPawn() == _FollowPawn &&
            _FollowPawn.GetController() == _FollowOwner;
    }

    bool TryStartControl(ACk_ChainGym_PlayerController Requester, int32 Serial,
        int64 SourceGeneration, int64 Generation, int32 Action)
    {
        if (!HasAuthority() || _Failed || _Ending || _Phase != 3 || !TryCapture() ||
            ck::Is_NOT_Valid(Requester) || Requester.IsActorBeingDestroyed() ||
            Requester.GetWorld() != _World || !Requester.HasAuthority() ||
            Requester.GetClass() != ACk_ChainGym_PlayerController || Serial <= 0 ||
            _RequestSerial != 0 || SourceGeneration != 1 || Action < 0 || Action > 8 ||
            _Fixtures.Num() != 4 || _Generations.Num() != 4 ||
            Generation != Snapshot.ControlGeneration || Generation >= 9223372036854775806 ||
            Snapshot.Sequence >= 9223372036854775806) { return false; }
        auto Pawn = Requester.GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetWorld() != _World || Pawn.GetController() != Requester) { return false; }
        FCk_Handle_Transform PawnTransform;
        if (Action == 7 && !_Fixtures[1].FollowingPawn)
        {
            auto Entity = ck::ToEntity(Pawn);
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Entity) ||
                ck::Is_NOT_Valid(Entity) || !utils_transform::Has(Entity) ||
                utils_owning_actor::TryGet_EntityOwningActor(Entity) != Pawn ||
                utils_entity_lifetime::Get_WorldForEntity(Entity) != _World) { return false; }
            PawnTransform = Entity.As_Transform();
        }
        if (Action <= 2 || Action == 8)
        {
            for (int32 I = 0; I < 4; ++I)
            {
                if ((Action == 8 || I == 0) && _Generations[I] >= 9223372036854775806)
                { return false; }
            }
        }
        _Requester = Requester; _RequesterPawn = Pawn; _RequestSerial = Serial;
        _RequestAction = Action; _RequestedGeneration = Generation;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (Action <= 2 || Action == 8)
        {
            if (Action <= 2) { _Spacing = Action == 0 ? 4.0f : (Action == 1 ? 10.0f : 25.0f); }
            _Replacing = Action == 8 ? -1 : 0;
            _RetirementRequested = false; _ReplacementStarted = false; _Retiring.Empty();
            _Phase = 4; Snapshot.Phase = 3; Snapshot.Replacing = _Replacing;
            Snapshot.Sequence++; ForceNetUpdate();
            return true;
        }
        bool Applied = false;
        if (Action == 3) { Applied = _Fixtures[0].Request_Teleport(); }
        else if (Action == 4) { Applied = _Fixtures[0].Request_SplitAtFour(); }
        else if (Action == 5) { Applied = _Fixtures[0].Request_ReseedHistory(); }
        else if (Action == 6) { Applied = _Fixtures[2].Request_StartTravel(); }
        else if (Action == 7)
        {
            if (_Fixtures[1].FollowingPawn)
            {
                Applied = _Fixtures[1].Request_StopPawnFollow();
                _FollowOwner = nullptr; _FollowPawn = nullptr;
            }
            else
            {
                Applied = _Fixtures[1].Request_TogglePawnFollow(PawnTransform);
                if (Applied) { _FollowOwner = Requester; _FollowPawn = Pawn; }
            }
        }
        if (!Applied) { Request_Fail("Admitted Chain control failed"); return true; }
        _Phase = 6; _DrainTicks = 0;
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
            Requester.Complete_ChainControl(Serial, Generation, Action, Success,
                Success ? Snapshot.ControlGeneration : 0, Success ? Snapshot.Sequence : 0);
        }
    }

    private bool PublishComplete(bool CompleteControl)
    {
        if (!TryCapture() || !HasRequestOwner() || _Fixtures.Num() != 4 || _Generations.Num() != 4 ||
            Snapshot.Sequence >= 9223372036854775806 ||
            (CompleteControl && Snapshot.ControlGeneration >= 9223372036854775806))
        { Request_Fail("Chain publication lost its world or owner"); return false; }
        FCkChainGym_WireSnapshot Frame;
        Frame.SourceGeneration = 1; Frame.ControlGeneration = Snapshot.ControlGeneration + (CompleteControl ? 1 : 0);
        Frame.Sequence = Snapshot.Sequence + 1; Frame.Phase = 2;
        for (int32 I = 0; I < 4; ++I)
        {
            FCkChainGym_StationFrame Station;
            if (!_Fixtures[I].TryGet_Frame(Station)) { _WaitingStation = I; return false; }
            Station.Index = I; Station.Generation = _Generations[I]; Frame.Stations.Add(Station);
        }
        _WaitingStation = -1;
        if (!CkChainGym::IsValidFrame(Frame)) { Request_Fail("Malformed Chain source values"); return false; }
        Snapshot = Frame; _PublishElapsed = 0.0; ForceNetUpdate();
        return true;
    }

    private bool ConsumeSnapshot()
    {
        if (_Failed || _Ending || !TryCapture() || Snapshot.Sequence == 0) { return false; }
        if (!CkChainGym::IsValidFrame(Snapshot)) { Request_CloseClient("Malformed Chain frame"); return false; }
        if (_HasConsumed)
        {
            if (Snapshot.Sequence < _Consumed.Sequence || Snapshot.ControlGeneration < _Consumed.ControlGeneration)
            { Request_CloseClient("Chain frame moved backward"); return false; }
            if (Snapshot.Sequence == _Consumed.Sequence)
            {
                if (!UCk_ChainGym_Runtime_UE::Get_IsSameFrame(Snapshot, _Consumed))
                { Request_CloseClient("Chain values changed without a new sequence"); return false; }
                return Snapshot.Phase == 2;
            }
            if (Snapshot.Phase != 4 && _Consumed.Stations.Num() == 4)
            {
                for (int32 I = 0; I < 4; ++I)
                {
                    if (Snapshot.Stations[I].Generation < _Consumed.Stations[I].Generation)
                    { Request_CloseClient("Chain station generation moved backward"); return false; }
                }
            }
        }
        _Consumed = Snapshot; _HasConsumed = true;
        if (Snapshot.Phase == 4 || SourceFailed)
        { Request_CloseClient("Authority closed the Chain bank"); return false; }
        return Snapshot.Phase == 2;
    }

    bool TryGetPresentation(FCkChainGym_WireSnapshot&out Frame)
    {
        Frame = FCkChainGym_WireSnapshot();
        if (_Failed || SourceFailed || _Ending || !TryCapture()) { return false; }
        if (HasAuthority())
        {
            if (_Phase != 3 || !CkChainGym::IsValidFrame(Snapshot) || Snapshot.Phase != 2) { return false; }
            Frame = Snapshot; return true;
        }
        if (!ConsumeSnapshot()) { return false; }
        Frame = _Consumed; return true;
    }

    private bool IsSelected(int32 Index) const { return _Replacing == -1 || _Replacing == Index; }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (_Failed || _Ending) { return; }
        if (!_Captured) { if (!HasAuthority()) { ConsumeSnapshot(); } return; }
        if (!TryCapture()) { Request_Fail("Captured Chain world or catalog changed"); return; }
        if (!HasAuthority()) { ConsumeSnapshot(); return; }
        if (!HasRequestOwner()) { Request_Fail("Chain requester changed"); return; }
        if (_Phase == 0) { return; }
        if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
        { Request_Fail("Invalid Chain elapsed time"); return; }
        auto Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (_Phase != 3 && Now - _PendingSince >= 20.0)
        { Request_Fail("Chain admission or retirement timed out"); return; }
        if (_Phase == 1)
        {
            for (int32 I = 0; I < 4; ++I)
            { if (!_Fixtures[I].Get_IsPrepared()) { _WaitingStation = I; return; } }
            _WaitingStation = -1;
            _Phase = 2;
            for (int32 I = 0; I < 4; ++I)
            {
                if (!_Fixtures[I].Request_StartPrepared())
                { Request_Fail("Prepared Chain station failed to start"); return; }
            }
        }
        if (_Phase == 2)
        { if (PublishComplete(false)) { _Phase = 3; _PendingSince = Now; } return; }
        if (_Phase == 3)
        {
            if (_Fixtures[1].FollowingPawn && !HasFollowOwner())
            {
                if (!_Fixtures[1].Request_StopPawnFollow())
                { Request_Fail("Departed Rope owner could not be released"); return; }
                _FollowOwner = nullptr; _FollowPawn = nullptr;
                _Phase = 6; _DrainTicks = 0; _PendingSince = Now;
                return;
            }
            _PublishElapsed += DeltaSeconds;
            if (_PublishElapsed >= 0.1)
            {
                if (PublishComplete(false)) { _PendingSince = Now; }
                else if (Now - _PendingSince >= 20.0) { Request_Fail("Chain values stopped converging"); }
            }
            return;
        }
        if (_Phase == 4)
        {
            if (!_RetirementRequested)
            {
                TArray<FCk_Handle> Receipts;
                for (int32 I = 0; I < 4; ++I)
                {
                    if (!IsSelected(I)) { continue; }
                    TArray<FCk_Handle> Tree;
                    if (!_Fixtures[I].TryGet_RetirementReceipts(Tree)) { _WaitingStation = I; return; }
                    for (auto Handle : Tree)
                    {
                        if (Receipts.Contains(Handle)) { Request_Fail("Chain retirement trees overlap"); return; }
                        Receipts.Add(Handle);
                    }
                }
                _WaitingStation = -1;
                _Retiring = Receipts; _RetirementRequested = true;
                for (int32 I = 0; I < 4; ++I)
                {
                    if (IsSelected(I) && !_Fixtures[I].Request_Destroy())
                    { Request_Fail("Chain retirement rejected its captured tree"); return; }
                }
                if (_Replacing == -1) { _FollowOwner = nullptr; _FollowPawn = nullptr; }
            }
            for (auto Handle : _Retiring)
            { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle)) { return; } }
            _Phase = 5;
            for (int32 I = 0; I < 4; ++I)
            {
                if (!IsSelected(I)) { continue; }
                _Fixtures[I] = FCkChainGym_Fixture(); _Generations[I]++;
                if (!_Fixtures[I].Create(_WorldRoot, CkChainGym::Get_Origin(I), I, _Spacing))
                { Request_Fail("Chain replacement composition failed"); return; }
            }
            return;
        }
        if (_Phase == 5)
        {
            if (!_ReplacementStarted)
            {
                for (int32 I = 0; I < 4; ++I)
                { if (IsSelected(I) && !_Fixtures[I].Get_IsPrepared()) { _WaitingStation = I; return; } }
                _WaitingStation = -1;
                _ReplacementStarted = true;
                for (int32 I = 0; I < 4; ++I)
                {
                    if (IsSelected(I) && !_Fixtures[I].Request_StartPrepared())
                    { Request_Fail("Chain replacement failed to start"); return; }
                }
            }
            if (PublishComplete(true))
            { _Phase = 3; _Retiring.Empty(); _Replacing = -1; _PendingSince = Now; CompleteRequest(true); }
            return;
        }
        if (_Phase == 6)
        {
            // Requests drain through Transform Derived; never acknowledge pre-command values.
            if (_DrainTicks == 0) { _DrainTicks = 1; return; }
            if (PublishComplete(true))
            { _Phase = 3; _PendingSince = Now; CompleteRequest(true); }
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    { if (HasAuthority()) { Request_Fail("Chain gym session ended"); } _Ending = true; }
}
