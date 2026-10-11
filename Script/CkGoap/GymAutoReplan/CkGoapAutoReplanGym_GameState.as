// Language=angelscript

// One world-owned bank compares three planner policies; controls replan A in place.
class ACk_GoapAutoReplanGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkGoapAutoReplanGym_WireSnapshot Snapshot;
    UPROPERTY(Replicated) bool SourceFailed = false;
    private TArray<UCk_EntityScript_GoapGym_AutoReplan_Station> _Sources;
    private TArray<FCk_Handle> _Roots;
    private TArray<FCk_Handle_PendingEntityScript> _Pending;
    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCkGym_Entry _Entry;
    private int32 _Index = -1;
    private bool _Captured = false;
    private bool _Failed = false;
    private bool _Ending = false;
    private FString _FailureReason;
    // 0 waiting, 1 preparing, 3 active; failure remains terminal.
    private int32 _Phase = 0;
    private double _PendingSince = 0.0;
    private double _PublishElapsed = 0.0;
    private bool _HasConsumed = false;
    private FCkGoapAutoReplanGym_WireSnapshot _Consumed;
    private ACk_GoapAutoReplanGym_PlayerController _Requester;
    private APawn _RequesterPawn;
    private int32 _RequestSerial = 0;
    private int32 _RequestAction = -1;
    private int64 _RequestedGeneration = 0;
    private int64 _PlanCountBefore = 0;
    private double _RequestSince = 0.0;

    UFUNCTION(BlueprintOverride)
    void BeginPlay() { SetTickGroup(ETickingGroup::TG_PostUpdateWork); }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_SourceRoot(int32 Index)
    { return HasAuthority() && _Roots.IsValidIndex(Index) ? _Roots[Index] : FCk_Handle(); }

    UFUNCTION(BlueprintPure)
    bool Get_HasSessionFailure() const { return _Failed || SourceFailed; }

    UFUNCTION(BlueprintPure)
    FString Get_DiagnosticState() const
    { return f"phase={_Phase} roots={_Roots.Num()} captured={_Captured} failed={_Failed} reason={_FailureReason}"; }

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
                Mode.GetClass() != ACk_GoapAutoReplanGym_GameMode) { return false; }
        }
        else if (World.GetNetMode() != ENetMode::NM_Client) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Goap AutoReplan" || Entry.GameModeClass != ACk_GoapAutoReplanGym_GameMode ||
            (_Captured && (Index != _Index || Entry.DisplayName != _Entry.DisplayName ||
                Entry.GameModeClass != _Entry.GameModeClass || Entry.LevelName != _Entry.LevelName ||
                Entry.Category != _Entry.Category || Entry.HintCode != _Entry.HintCode)))
        { return false; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs)) { return false; }
        auto Root = Ecs.Get_TransientEntity();
        if (!UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveInWorld(Root, World) ||
            (_Captured && Root != _WorldRoot)) { return false; }
        bool Closed = true;
        if (!UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(Root, Closed) || Closed)
        { return false; }
        if (!_Captured)
        { _World = World; _WorldRoot = Root; _Entry = Entry; _Index = Index; _Captured = true; }
        return true;
    }

    private bool HasRoot(int32 Index)
    {
        if (!_Roots.IsValidIndex(Index)) { return false; }
        auto Root = _Roots[Index];
        return UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveInWorld(Root, _World) &&
            utils_entity_lifetime::Has_LifetimeOwner(Root) &&
            utils_entity_lifetime::Get_LifetimeOwner(Root) == _WorldRoot;
    }

    void Request_CloseClient(FString Reason)
    {
        if (HasAuthority() || _Failed || _Ending) { return; }
        _Failed = true; _FailureReason = Reason;
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        ck::Trace(f"Goap AutoReplan client presentation closed: {Reason}");
    }

    UFUNCTION()
    void Request_Fail(FString Reason)
    {
        if (!HasAuthority()) { Request_CloseClient(Reason); return; }
        if (_Failed || _Ending) { return; }
        _Failed = true; SourceFailed = true; _FailureReason = Reason; _Phase = 0;
        for (auto Source : _Sources)
        { if (ck::IsValid(Source)) { Source.Request_Block(); } }
        for (int32 I = 0; I < _Roots.Num(); ++I)
        {
            if (HasRoot(I))
            {
                if (_Pending.IsValidIndex(I) && utils_pending_entity_script::Get_IsValid(_Pending[I]))
                { utils_pending_entity_script::Request_DestroyEntity(_Pending[I]); }
                else { utils_entity_lifetime::Request_DestroyEntity(_Roots[I]); }
            }
        }
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        Snapshot.Phase = 4; Snapshot.Replacing = -1;
        if (Snapshot.Sequence < 9223372036854775806) { Snapshot.Sequence++; }
        ForceNetUpdate(); CompleteRequest(false);
        ck::Trace(f"Goap AutoReplan authority bank closed: {Reason}");
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Ending || _Failed || _Phase != 0 || !TryCapture()) { return; }
        if (CkGym_Admission::Get_IsBusy(_WorldRoot)) { return; }
        _Phase = 1; _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        for (int32 I = 0; I < 3; ++I)
        {
            _Sources.Add(nullptr); _Roots.Add(FCk_Handle());
            _Pending.Add(FCk_Handle_PendingEntityScript());
        }
        for (int32 I = 0; I < 3; ++I)
        { if (!SpawnSource(I)) { Request_Fail("Goap AutoReplan source spawn rejected"); return; } }
    }

    private bool SpawnSource(int32 Index)
    {
        if (!_Roots.IsValidIndex(Index) || !TryCapture() ||
            !utils_entity_lifetime::Get_CanCreateEntity(_WorldRoot)) { return false; }
        auto Params = FCk_GoapGym_AutoReplan_Station_SpawnParams();
        Params.InitialTransform = CkGoapAutoReplanGym::Get_SourceTransform(Index);
        Params.Mode = Index;
        _Pending[Index] = utils_entity_script::Request_SpawnEntity(_WorldRoot,
            UCk_EntityScript_GoapGym_AutoReplan_Station, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_Pending[Index])) { return false; }
        _Roots[Index] = _Pending[Index].Get_EntityUnderConstruction();
        _Sources[Index] = nullptr;
        return HasRoot(Index);
    }

    private bool BindPrepared(int32 Index)
    {
        if (!HasRoot(Index)) { Request_Fail("Goap AutoReplan source root lost"); return false; }
        auto Source = Cast<UCk_EntityScript_GoapGym_AutoReplan_Station>(utils_entity_script::TryGet_EntityScript(_Roots[Index]));
        if (ck::Is_NOT_Valid(Source)) { return false; }
        if (Source.GetClass() != UCk_EntityScript_GoapGym_AutoReplan_Station ||
            (ck::IsValid(_Sources[Index]) && Source != _Sources[Index]) ||
            Source.Get_SourceRoot() != _Roots[Index] || Source.Get_IsClosed())
        { Request_Fail(f"Goap AutoReplan source {Index} identity or preparation failed: closed={Source.Get_IsClosed()}"); return false; }
        _Sources[Index] = Source;
        return Source.Get_IsPrepared();
    }

    private bool HasRequestOwner()
    {
        if (_RequestSerial == 0) { return true; }
        return ck::IsValid(_Requester) && !_Requester.IsActorBeingDestroyed() &&
            _Requester.GetWorld() == _World && _Requester.HasAuthority() &&
            _Requester.GetClass() == ACk_GoapAutoReplanGym_PlayerController &&
            ck::IsValid(_RequesterPawn) && !_RequesterPawn.IsActorBeingDestroyed() &&
            _RequesterPawn.GetWorld() == _World && _Requester.GetControlledPawn() == _RequesterPawn &&
            _RequesterPawn.GetController() == _Requester;
    }

    bool TryStartControl(ACk_GoapAutoReplanGym_PlayerController Requester, int32 Serial,
        int64 SourceGeneration, int64 Generation, int32 Action)
    {
        if (!HasAuthority() || _Failed || _Ending || _Phase != 3 || !TryCapture() ||
            ck::Is_NOT_Valid(Requester) || Requester.IsActorBeingDestroyed() ||
            Requester.GetWorld() != _World || !Requester.HasAuthority() ||
            Requester.GetClass() != ACk_GoapAutoReplanGym_PlayerController || Serial <= 0 ||
            _RequestSerial != 0 || SourceGeneration != 1 || Action != 0 ||
            _Sources.Num() != 3 || _Roots.Num() != 3 ||
            Generation != Snapshot.ControlGeneration || Generation >= 9223372036854775806 ||
            Snapshot.Sequence >= 9223372036854775806) { return false; }
        auto Pawn = Requester.GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetWorld() != _World || Pawn.GetController() != Requester) { return false; }
        for (int32 I = 0; I < 3; ++I)
        {
            if (!HasRoot(I) || ck::Is_NOT_Valid(_Sources[I]) ||
                !_Sources[I].Get_IsPrepared() || !_Sources[I].Get_IsStarted() ||
                _Sources[I].Get_SourceRoot() != _Roots[I]) { return false; }
        }
        auto PlanCount = _Sources[0].Get_PlanCount();
        if (PlanCount <= 0 || PlanCount >= 9223372036854775806) { return false; }
        _Requester = Requester; _RequesterPawn = Pawn; _RequestSerial = Serial;
        _RequestAction = Action; _RequestedGeneration = Generation; _PlanCountBefore = PlanCount;
        _RequestSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (!_Sources[0].Request_ManualReplan())
        { CompleteRequest(false); return false; }
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
            Requester.Complete_GoapAutoReplanControl(Serial, Generation, Action, Success,
                Success ? Snapshot.ControlGeneration : 0, Success ? Snapshot.Sequence : 0);
        }
    }

    private bool PublishComplete(bool CompleteControl)
    {
        if (!TryCapture() || !HasRequestOwner() || _Sources.Num() != 3 ||
            Snapshot.Sequence >= 9223372036854775806 ||
            (CompleteControl && Snapshot.ControlGeneration >= 9223372036854775806))
        { Request_Fail("Goap AutoReplan publication lost its bank, world or owner"); return false; }
        FCkGoapAutoReplanGym_WireSnapshot Frame;
        Frame.SourceGeneration = 1; Frame.ControlGeneration = Snapshot.ControlGeneration + (CompleteControl ? 1 : 0);
        Frame.Sequence = Snapshot.Sequence + 1; Frame.Phase = 2;
        for (int32 I = 0; I < 3; ++I)
        {
            if (!HasRoot(I) || ck::Is_NOT_Valid(_Sources[I]) || _Sources[I].Get_IsClosed() ||
                _Sources[I].Get_SourceRoot() != _Roots[I])
            { Request_Fail("Goap AutoReplan source lost before publication"); return false; }
            FCkGoapAutoReplanGym_StationFrame Station;
            if (!_Sources[I].Get_IsPrepared() || !_Sources[I].Get_IsStarted() ||
                !_Sources[I].TryGet_Frame(Station))
            { Request_Fail("Goap AutoReplan source stopped producing valid values"); return false; }
            Station.Index = I; Station.Generation = 1; Frame.Stations.Add(Station);
        }
        if (!CkGoapAutoReplanGym::IsValidFrame(Frame)) { Request_Fail("Malformed Goap AutoReplan source values"); return false; }
        Snapshot = Frame; _PublishElapsed = 0.0; ForceNetUpdate(); return true;
    }

    private bool ConsumeSnapshot()
    {
        if (_Failed || _Ending || !TryCapture() || Snapshot.Sequence == 0) { return false; }
        if (!CkGoapAutoReplanGym::IsValidFrame(Snapshot)) { Request_CloseClient("Malformed Goap AutoReplan frame"); return false; }
        if (_HasConsumed)
        {
            if (Snapshot.Sequence < _Consumed.Sequence || Snapshot.ControlGeneration < _Consumed.ControlGeneration)
            { Request_CloseClient("Goap AutoReplan frame moved backward"); return false; }
            if (Snapshot.Sequence == _Consumed.Sequence)
            {
                if (!UCk_GoapAutoReplanGym_Runtime_UE::Get_IsSameFrame(Snapshot, _Consumed))
                { Request_CloseClient("Goap AutoReplan values changed without a new sequence"); return false; }
                return Snapshot.Phase == 2;
            }
            if (Snapshot.Phase != 4 && _Consumed.Stations.Num() == 3)
            {
                for (int32 I = 0; I < 3; ++I)
                {
                    if (Snapshot.Stations[I].Generation != _Consumed.Stations[I].Generation ||
                        Snapshot.Stations[I].PlanCount < _Consumed.Stations[I].PlanCount ||
                        Snapshot.Stations[I].WorldStateChangeCount < _Consumed.Stations[I].WorldStateChangeCount)
                    { Request_CloseClient("Goap AutoReplan station generation moved backward"); return false; }
                }
            }
        }
        _Consumed = Snapshot; _HasConsumed = true;
        if (Snapshot.Phase == 4 || SourceFailed)
        { Request_CloseClient("Authority closed the Goap AutoReplan bank"); return false; }
        return Snapshot.Phase == 2;
    }

    bool TryGetPresentation(FCkGoapAutoReplanGym_WireSnapshot&out Frame)
    {
        Frame = FCkGoapAutoReplanGym_WireSnapshot();
        if (_Failed || SourceFailed || _Ending || !TryCapture()) { return false; }
        if (HasAuthority())
        {
            if (_Phase != 3 || !CkGoapAutoReplanGym::IsValidFrame(Snapshot) || Snapshot.Phase != 2) { return false; }
            Frame = Snapshot; return true;
        }
        if (!ConsumeSnapshot()) { return false; }
        Frame = _Consumed; return true;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (_Failed || _Ending) { return; }
        if (!_Captured) { if (!HasAuthority()) { ConsumeSnapshot(); } return; }
        if (!TryCapture()) { Request_Fail("Captured Goap AutoReplan world or catalog changed"); return; }
        if (!HasAuthority()) { ConsumeSnapshot(); return; }
        if (!HasRequestOwner()) { Request_Fail("Goap AutoReplan requester changed"); return; }
        if (_Phase == 0) { return; }
        if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
        { Request_Fail("Goap AutoReplan elapsed time invalid"); return; }
        auto Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (_Phase == 1)
        {
            if (Now - _PendingSince >= 30.0)
            { Request_Fail("Goap AutoReplan preparation timed out"); return; }
            for (int32 I = 0; I < 3; ++I)
            { if (!BindPrepared(I)) { return; } }
            for (int32 I = 0; I < 3; ++I)
            {
                if (!_Sources[I].Get_IsStarted() && !_Sources[I].Request_StartPrepared())
                { Request_Fail("Goap AutoReplan prepared source failed to start"); return; }
            }
            if (PublishComplete(false)) { _Phase = 3; }
            return;
        }
        if (_RequestSerial > 0)
        {
            if (!HasRoot(0) || ck::Is_NOT_Valid(_Sources[0]) || _Sources[0].Get_IsClosed())
            { Request_Fail("Goap AutoReplan controlled source lost"); return; }
            auto Count = _Sources[0].Get_PlanCount();
            if (Count < _PlanCountBefore || Count > _PlanCountBefore + 1)
            { Request_Fail("Goap AutoReplan manual completion count changed unexpectedly"); return; }
            if (Count == _PlanCountBefore + 1)
            {
                if (PublishComplete(true)) { CompleteRequest(true); }
                return;
            }
            if (Now - _RequestSince >= 15.0)
            { Request_Fail("Goap AutoReplan manual request timed out"); return; }
        }
        _PublishElapsed += DeltaSeconds;
        if (_PublishElapsed >= 0.1) { PublishComplete(false); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    { if (HasAuthority()) { Request_Fail("Goap AutoReplan gym session ended"); } _Ending = true; }
}
