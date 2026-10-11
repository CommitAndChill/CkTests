// One authority registry bank and five rerunnable query sources.
class ACk_DialogGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkDialogGym_WireSnapshot Snapshot;
    UPROPERTY(Replicated) bool SourceFailed = false;
    private UCk_DialogGym_Bank _Bank;
    private TArray<UCk_EntityScript_DialogGym_Source> _Sources;
    private TArray<FCk_Handle> _Roots;
    private TArray<FCk_Handle_PendingEntityScript> _Pending;
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
    // 0 waiting, 1 registry construction, 2 initial preparation, 3 active,
    // 4 retirement, 5 replacement preparation.
    private int32 _Phase = 0;
    private int32 _Replacing = -1;
    private bool _RetirementRequested = false;
    private double _PendingSince = 0.0;
    private double _PublishElapsed = 0.0;
    private bool _HasConsumed = false;
    private FCkDialogGym_WireSnapshot _Consumed;
    private ACk_DialogGym_PlayerController _Requester;
    private APawn _RequesterPawn;
    private int32 _RequestSerial = 0;
    private int32 _RequestAction = -1;
    private int64 _RequestedGeneration = 0;

    UFUNCTION(BlueprintOverride)
    void BeginPlay() { SetTickGroup(ETickingGroup::TG_PostUpdateWork); }

    UCk_DialogGym_Bank Get_DialogBank()
    { return HasAuthority() && !_Failed && !_Ending ? _Bank : nullptr; }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_SourceRoot(int32 Index)
    { return HasAuthority() && _Roots.IsValidIndex(Index) ? _Roots[Index] : FCk_Handle(); }

    UFUNCTION(BlueprintPure)
    TArray<FCk_Handle_DialogLine> Get_RegisteredLines()
    {
        if (HasAuthority() && ck::IsValid(_Bank)) { return _Bank.Get_Lines(); }
        return TArray<FCk_Handle_DialogLine>();
    }

    UFUNCTION(BlueprintPure)
    bool Get_HasSessionFailure() const { return _Failed || SourceFailed; }

    UFUNCTION(BlueprintPure)
    FString Get_DiagnosticState() const
    { return f"phase={_Phase} replacing={_Replacing} roots={_Roots.Num()} captured={_Captured} failed={_Failed} reason={_FailureReason}"; }

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
                Mode.GetClass() != ACk_DialogGym_GameMode) { return false; }
        }
        else if (World.GetNetMode() != ENetMode::NM_Client) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Dialog" || Entry.GameModeClass != ACk_DialogGym_GameMode ||
            (_Captured && (Index != _Index || Entry.DisplayName != _Entry.DisplayName ||
                Entry.GameModeClass != _Entry.GameModeClass || Entry.LevelName != _Entry.LevelName ||
                Entry.Category != _Entry.Category || Entry.HintCode != _Entry.HintCode)))
        { return false; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs)) { return false; }
        auto Root = Ecs.Get_TransientEntity();
        if (!UCk_DialogGym_Runtime_UE::Get_IsLiveInWorld(Root, World) ||
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
        return UCk_DialogGym_Runtime_UE::Get_IsLiveInWorld(Root, _World) &&
            utils_entity_lifetime::Has_LifetimeOwner(Root) &&
            utils_entity_lifetime::Get_LifetimeOwner(Root) == _WorldRoot;
    }

    void Request_CloseClient(FString Reason)
    {
        if (HasAuthority() || _Failed || _Ending) { return; }
        _Failed = true; _FailureReason = Reason;
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        ck::Trace(f"Dialog client presentation closed: {Reason}");
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
        if (ck::IsValid(_Bank)) { _Bank.Request_Close(); }
        if (_Captured) { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        Snapshot.Phase = 4; Snapshot.Replacing = -1;
        if (Snapshot.Sequence < 9223372036854775806) { Snapshot.Sequence++; }
        ForceNetUpdate(); CompleteRequest(false);
        ck::Trace(f"Dialog authority bank closed: {Reason}");
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Ending || _Failed || _Phase != 0 || !TryCapture()) { return; }
        if (CkGym_Admission::Get_IsBusy(_WorldRoot)) { return; }
        auto Registry = UCk_DialogRegistry_Subsystem_UE::Get_DialogRegistry();
        if (ck::Is_NOT_Valid(Registry) || !Registry.Get_IsReady()) { return; }
        _Phase = 1; _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        _Bank = Cast<UCk_DialogGym_Bank>(NewObject(this, UCk_DialogGym_Bank));
        if (ck::Is_NOT_Valid(_Bank) || !_Bank.Create(this))
        { Request_Fail("Dialog registry bank admission failed"); return; }
        for (int32 I = 0; I < 5; ++I)
        {
            _Sources.Add(nullptr); _Roots.Add(FCk_Handle());
            _Pending.Add(FCk_Handle_PendingEntityScript()); _Generations.Add(1);
        }
    }

    private bool SpawnSource(int32 Index)
    {
        if (!_Roots.IsValidIndex(Index) || !TryCapture() ||
            !utils_entity_lifetime::Get_CanCreateEntity(_WorldRoot)) { return false; }
        TSubclassOf<UCk_EntityScript_UE> Class;
        if (Index == 0) { Class = UCk_EntityScript_DialogGym_Basics; }
        else if (Index == 1) { Class = UCk_EntityScript_DialogGym_Filters; }
        else if (Index == 2) { Class = UCk_EntityScript_DialogGym_Cooldowns; }
        else if (Index == 3) { Class = UCk_EntityScript_DialogGym_Chains; }
        else if (Index == 4) { Class = UCk_EntityScript_DialogGym_Graph; }
        else { return false; }
        auto Params = FCk_Gym_TransformSpawnParams(CkDialogGym::Get_SourceTransform(Index));
        _Pending[Index] = utils_entity_script::Request_SpawnEntity(_WorldRoot, Class, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_Pending[Index])) { return false; }
        _Roots[Index] = _Pending[Index].Get_EntityUnderConstruction();
        _Sources[Index] = nullptr;
        return HasRoot(Index);
    }

    private bool BindPrepared(int32 Index)
    {
        if (!HasRoot(Index)) { Request_Fail("Dialog source root lost"); return false; }
        auto Source = Cast<UCk_EntityScript_DialogGym_Source>(utils_entity_script::TryGet_EntityScript(_Roots[Index]));
        if (ck::Is_NOT_Valid(Source)) { return false; }
        if (!HasExpectedClass(Source, Index) ||
            (ck::IsValid(_Sources[Index]) && Source != _Sources[Index]) ||
            Source.Get_SourceRoot() != _Roots[Index] || Source.Get_IsClosed())
        { Request_Fail("Dialog source identity or preparation failed"); return false; }
        _Sources[Index] = Source;
        return Source.Get_IsPrepared();
    }

    private bool HasExpectedClass(UCk_EntityScript_DialogGym_Source Source, int32 Index) const
    {
        if (ck::Is_NOT_Valid(Source)) { return false; }
        if (Index == 0) { return Source.GetClass() == UCk_EntityScript_DialogGym_Basics; }
        if (Index == 1) { return Source.GetClass() == UCk_EntityScript_DialogGym_Filters; }
        if (Index == 2) { return Source.GetClass() == UCk_EntityScript_DialogGym_Cooldowns; }
        if (Index == 3) { return Source.GetClass() == UCk_EntityScript_DialogGym_Chains; }
        if (Index == 4) { return Source.GetClass() == UCk_EntityScript_DialogGym_Graph; }
        return false;
    }

    private bool HasRequestOwner()
    {
        if (_RequestSerial == 0) { return true; }
        return ck::IsValid(_Requester) && !_Requester.IsActorBeingDestroyed() &&
            _Requester.GetWorld() == _World && _Requester.HasAuthority() &&
            _Requester.GetClass() == ACk_DialogGym_PlayerController &&
            ck::IsValid(_RequesterPawn) && !_RequesterPawn.IsActorBeingDestroyed() &&
            _RequesterPawn.GetWorld() == _World && _Requester.GetControlledPawn() == _RequesterPawn &&
            _RequesterPawn.GetController() == _Requester;
    }

    bool TryStartControl(ACk_DialogGym_PlayerController Requester, int32 Serial,
        int64 SourceGeneration, int64 Generation, int32 Action)
    {
        if (!HasAuthority() || _Failed || _Ending || _Phase != 3 || !TryCapture() ||
            ck::Is_NOT_Valid(Requester) || Requester.IsActorBeingDestroyed() ||
            Requester.GetWorld() != _World || !Requester.HasAuthority() ||
            Requester.GetClass() != ACk_DialogGym_PlayerController || Serial <= 0 ||
            _RequestSerial != 0 || SourceGeneration != 1 || Action < 0 || Action > 5 ||
            _Sources.Num() != 5 || _Generations.Num() != 5 ||
            Generation != Snapshot.ControlGeneration || Generation >= 9223372036854775806 ||
            Snapshot.Sequence >= 9223372036854775806) { return false; }
        auto Pawn = Requester.GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetWorld() != _World || Pawn.GetController() != Requester) { return false; }
        for (int32 I = 0; I < 5; ++I)
        { if ((Action == 5 || I == Action) && _Generations[I] >= 9223372036854775806) { return false; } }
        _Requester = Requester; _RequesterPawn = Pawn; _RequestSerial = Serial;
        _RequestAction = Action; _RequestedGeneration = Generation;
        _Replacing = Action == 5 ? -1 : Action; _Retiring.Empty(); _RetirementRequested = false;
        _Phase = 4; _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        Snapshot.Phase = 3; Snapshot.Replacing = _Replacing; Snapshot.Sequence++;
        ForceNetUpdate(); return true;
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
            Requester.Complete_DialogControl(Serial, Generation, Action, Success,
                Success ? Snapshot.ControlGeneration : 0, Success ? Snapshot.Sequence : 0);
        }
    }

    private bool PublishComplete(bool CompleteControl)
    {
        if (!TryCapture() || !HasRequestOwner() || _Sources.Num() != 5 ||
            ck::Is_NOT_Valid(_Bank) || !_Bank.Get_IsHealthy() || !_Bank.Get_IsReady() ||
            Snapshot.Sequence >= 9223372036854775806 ||
            (CompleteControl && Snapshot.ControlGeneration >= 9223372036854775806))
        { Request_Fail("Dialog publication lost its bank, world or owner"); return false; }
        FCkDialogGym_WireSnapshot Frame;
        Frame.SourceGeneration = 1; Frame.ControlGeneration = Snapshot.ControlGeneration + (CompleteControl ? 1 : 0);
        Frame.Sequence = Snapshot.Sequence + 1; Frame.Phase = 2;
        for (int32 I = 0; I < 5; ++I)
        {
            if (!HasRoot(I) || ck::Is_NOT_Valid(_Sources[I]) || _Sources[I].Get_IsClosed() ||
                _Sources[I].Get_SourceRoot() != _Roots[I])
            { Request_Fail("Dialog source lost before publication"); return false; }
            FCkDialogGym_StationFrame Station;
            if (!_Sources[I].Get_IsPrepared() || !_Sources[I].Get_IsStarted() ||
                !_Sources[I].TryGet_Frame(Station))
            { Request_Fail("Dialog source stopped producing valid values"); return false; }
            Station.Index = I; Station.Generation = _Generations[I]; Frame.Stations.Add(Station);
        }
        if (!CkDialogGym::IsValidFrame(Frame)) { Request_Fail("Malformed Dialog source values"); return false; }
        Snapshot = Frame; _PublishElapsed = 0.0; ForceNetUpdate(); return true;
    }

    private bool ConsumeSnapshot()
    {
        if (_Failed || _Ending || !TryCapture() || Snapshot.Sequence == 0) { return false; }
        if (!CkDialogGym::IsValidFrame(Snapshot)) { Request_CloseClient("Malformed Dialog frame"); return false; }
        if (_HasConsumed)
        {
            if (Snapshot.Sequence < _Consumed.Sequence || Snapshot.ControlGeneration < _Consumed.ControlGeneration)
            { Request_CloseClient("Dialog frame moved backward"); return false; }
            if (Snapshot.Sequence == _Consumed.Sequence)
            {
                if (!UCk_DialogGym_Runtime_UE::Get_IsSameFrame(Snapshot, _Consumed))
                { Request_CloseClient("Dialog values changed without a new sequence"); return false; }
                return Snapshot.Phase == 2;
            }
            if (Snapshot.Phase != 4 && _Consumed.Stations.Num() == 5)
            {
                for (int32 I = 0; I < 5; ++I)
                {
                    if (Snapshot.Stations[I].Generation < _Consumed.Stations[I].Generation)
                    { Request_CloseClient("Dialog station generation moved backward"); return false; }
                }
            }
        }
        _Consumed = Snapshot; _HasConsumed = true;
        if (Snapshot.Phase == 4 || SourceFailed)
        { Request_CloseClient("Authority closed the Dialog bank"); return false; }
        return Snapshot.Phase == 2;
    }

    bool TryGetPresentation(FCkDialogGym_WireSnapshot&out Frame)
    {
        Frame = FCkDialogGym_WireSnapshot();
        if (_Failed || SourceFailed || _Ending || !TryCapture()) { return false; }
        if (HasAuthority())
        {
            if (_Phase != 3 || !CkDialogGym::IsValidFrame(Snapshot) || Snapshot.Phase != 2) { return false; }
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
        if (!TryCapture()) { Request_Fail("Captured Dialog world or catalog changed"); return; }
        if (!HasAuthority()) { ConsumeSnapshot(); return; }
        if (!HasRequestOwner()) { Request_Fail("Dialog requester changed"); return; }
        if (_Phase == 0) { return; }
        if (!Math::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0 ||
            ck::Is_NOT_Valid(_Bank) || !_Bank.Get_IsHealthy())
        { Request_Fail("Dialog bank or elapsed time invalid"); return; }
        auto Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        if (_Phase != 3 && Now - _PendingSince >= 20.0)
        { Request_Fail("Dialog admission or retirement timed out"); return; }
        if (_Phase == 1)
        {
            if (!_Bank.Get_IsReady()) { return; }
            _Phase = 2;
            for (int32 I = 0; I < 5; ++I)
            { if (!SpawnSource(I)) { Request_Fail("Dialog source spawn rejected"); return; } }
            return;
        }
        if (_Phase == 2 || _Phase == 5)
        {
            for (int32 I = 0; I < 5; ++I)
            { if ((_Phase == 2 || IsSelected(I)) && !BindPrepared(I)) { return; } }
            for (int32 I = 0; I < 5; ++I)
            {
                if ((_Phase == 2 || IsSelected(I)) && !_Sources[I].Get_IsStarted() &&
                    !_Sources[I].Request_StartPrepared())
                { Request_Fail("Dialog prepared source failed to start"); return; }
            }
            bool Replaced = _Phase == 5;
            if (PublishComplete(Replaced))
            {
                _Phase = 3; _Retiring.Empty(); _Replacing = -1; _PendingSince = Now;
                if (Replaced) { CompleteRequest(true); }
            }
            return;
        }
        if (_Phase == 3)
        {
            _PublishElapsed += DeltaSeconds;
            if (_PublishElapsed >= 0.1)
            {
                if (PublishComplete(false)) { _PendingSince = Now; }
                else if (Now - _PendingSince >= 20.0) { Request_Fail("Dialog values stopped converging"); }
            }
            return;
        }
        if (_Phase == 4)
        {
            if (!_RetirementRequested)
            {
                TArray<FCk_Handle> Receipts;
                for (int32 I = 0; I < 5; ++I)
                {
                    if (!IsSelected(I)) { continue; }
                    TArray<FCk_Handle> Tree;
                    if (!HasRoot(I) || ck::Is_NOT_Valid(_Sources[I]) || _Sources[I].Get_IsClosed() ||
                        !UCk_DialogGym_Runtime_UE::TryGet_SourceForest(_World, _Roots[I], Tree))
                    { Request_Fail("Dialog source retirement census rejected"); return; }
                    for (auto Handle : Tree)
                    {
                        if (Receipts.Contains(Handle)) { Request_Fail("Dialog source forests overlap"); return; }
                        Receipts.Add(Handle);
                    }
                }
                _Retiring = Receipts; _RetirementRequested = true;
                for (int32 I = 0; I < 5; ++I)
                { if (IsSelected(I)) { _Sources[I].Request_Block(); } }
                for (int32 I = 0; I < 5; ++I)
                { if (IsSelected(I) && HasRoot(I)) { utils_entity_lifetime::Request_DestroyEntity(_Roots[I]); } }
            }
            for (auto Handle : _Retiring)
            { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle)) { return; } }
            _Phase = 5;
            for (int32 I = 0; I < 5; ++I)
            {
                if (!IsSelected(I)) { continue; }
                _Generations[I]++;
                if (!SpawnSource(I)) { Request_Fail("Dialog replacement spawn rejected"); return; }
            }
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    { if (HasAuthority()) { Request_Fail("Dialog gym session ended"); } _Ending = true; }
}
