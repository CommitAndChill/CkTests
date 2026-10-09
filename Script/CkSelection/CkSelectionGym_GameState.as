// Language=angelscript
// Authority owns one source; client worlds install passive value mirrors.
class ACk_SelectionGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;

    UPROPERTY(ReplicatedUsing=OnSnapshotUpdated)
    FCkSelectionGym_WireSnapshot Snapshot;
    UPROPERTY(Replicated) bool SourceFailed = false;

    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCkGym_Entry _Entry;
    private int32 _Index = -1;
    UPROPERTY(Transient, NotEditable, BlueprintHidden)
    private UCkSelectionGym_SourceComposition _Composition;
    private bool _Captured = false;
    private bool _Ending = false;
    private bool _Failed = false;
    private bool _ClientClosed = false;
    private bool _HasConsumed = false;
    private bool _HasMirror = false;
    private FCkSelectionGym_WireSnapshot _Consumed;
    private int32 _Phase = 0; // 0 initial, 1 staging, 2 active, 3 reset, 4 respawn
    private int32 _Scenario = 0;
    private int32 _NextScenario = 0;
    private int64 _ScenarioGeneration = 1;
    private int64 _AdmissionGeneration = 1;
    private double _PendingSince = 0.0;
    private double _PublishElapsed = 0.0;
    private double _ClientPendingSince = 0.0;
    private ACk_SelectionGym_PlayerController _Requester;
    private APawn _RequesterPawn;
    private int32 _RequestSerial = 0;
    private int32 _RequestAction = -1;
    private int64 _RequestScenarioGeneration = 0;

    UFUNCTION(BlueprintOverride)
    void BeginPlay()
    { SetTickGroup(ETickingGroup::TG_PostUpdateWork); }

    UFUNCTION()
    private void OnSnapshotUpdated() {}

    private bool IsLive(FCk_Handle Handle)
    {
        return ck::IsValid(Handle) &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    private bool TryContext(FCk_Handle&out Root, FCkGym_Entry&out Entry, int32&out Index)
    {
        Root = FCk_Handle(); Entry = FCkGym_Entry(); Index = -1;
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() ||
            GetClass() != ACk_SelectionGym_GameState || !Get_HasPublishedCatalog() ||
            ck::Is_NOT_Valid(World) || World.GetGameState() != this ||
            (_Captured && World != _World)) { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_SelectionGym_GameMode) { return false; }
        }
        else if (World.GetNetMode() != ENetMode::NM_Client) { return false; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return false; }
        Entry = Entries[Index];
        if (Entry.DisplayName != "Selection" ||
            Entry.GameModeClass != ACk_SelectionGym_GameMode) { return false; }
        if (_Captured && (Index != _Index ||
            Entry.DisplayName != _Entry.DisplayName ||
            Entry.GameModeClass != _Entry.GameModeClass ||
            Entry.LevelName != _Entry.LevelName || Entry.Category != _Entry.Category ||
            Entry.HintCode != _Entry.HintCode)) { return false; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs)) { return false; }
        Root = Ecs.Get_TransientEntity();
        if (!IsLive(Root) || utils_entity_lifetime::Get_WorldForEntity(Root) != World ||
            (_Captured && Root != _WorldRoot)) { return false; }
        bool Closed = true;
        return UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(Root, Closed) && !Closed;
    }

    private bool HasBinding()
    {
        FCk_Handle Root; FCkGym_Entry Entry; int32 Index = -1;
        return _Captured && TryContext(Root, Entry, Index);
    }

    private bool TryCapture()
    {
        if (_Captured) { return HasBinding(); }
        FCk_Handle Root; FCkGym_Entry Entry; int32 Index = -1;
        if (!TryContext(Root, Entry, Index)) { return false; }
        _World = GetWorld(); _WorldRoot = Root; _Entry = Entry; _Index = Index;
        _Captured = true;
        return true;
    }

    private void CompleteRequest(bool Success)
    {
        auto Requester = _Requester;
        int32 Serial = _RequestSerial;
        int32 Action = _RequestAction;
        int64 Generation = _RequestScenarioGeneration;
        _Requester = nullptr; _RequestSerial = 0; _RequestAction = -1;
        _RequesterPawn = nullptr;
        _RequestScenarioGeneration = 0;
        if (ck::IsValid(Requester) && Requester.GetWorld() == GetWorld())
        {
            Requester.Complete_SelectionControl(Serial, 1, Generation, Action, Success,
                Success ? Snapshot.ScenarioGeneration : 0,
                Success ? Snapshot.Sequence : 0, Success ? Snapshot.Scenario : -1);
        }
    }

    void Request_CloseClient(FString Reason)
    {
        if (HasAuthority() || _Ending || _ClientClosed) { return; }
        _Failed = true;
        if (!_Captured && !TryCapture()) { return; }
        _ClientClosed = true;
        FString Why;
        UCkSelectionGym_Mirror_UE::TryFailCaptured(_World, this, _WorldRoot, Reason, Why);
        UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot);
    }

    void Request_Fail(FString Reason)
    {
        if (!HasAuthority()) { Request_CloseClient(Reason); return; }
        if (_Failed || _Ending) { return; }
        _Failed = true; _Phase = 0; SourceFailed = true;
        auto Session = UCkSelectionGym_SourceComposer_UE::Get_Session(_Composition);
        FString Why;
        if (ck::IsValid(Session))
        {
            UCkSelectionGym_SourceSession_UE::TryFail(Session, Reason, Why);
            FCkSelectionGym_WireSnapshot Terminal;
            if (UCkSelectionGym_SourceSession_UE::TryMakeLifecycleEnvelope(
                    Session, ECkSelectionGym_WireLifecycle::Failed, Terminal, Why))
            { Snapshot = Terminal; }
        }
        if (_Captured)
        { UCkGym_AdmissionRuntime_UE::TryCloseWorldUnproven(_WorldRoot); }
        ForceNetUpdate();
        CompleteRequest(false);
    }

    private void SetInitializing()
    {
        FCkSelectionGym_WireSnapshot Initial;
        Initial.SchemaVersion = 1; Initial.SourceGeneration = 1;
        Initial.ScenarioGeneration = _ScenarioGeneration; Initial.Sequence = 0;
        Initial.Lifecycle = ECkSelectionGym_WireLifecycle::Initializing;
        Initial.Scenario = _Scenario; Initial.SourceOrigin = FVector::ZeroVector;
        Initial.DeclaredNodeCount = 0; Initial.DeclaredVisualCount = 0;
        Initial.DeclaredTimerCount = 0; Initial.DeclaredIntegerCount = 0;
        Initial.DeclaredStateMachineCount = 0;
        Snapshot = Initial;
        ForceNetUpdate();
    }

    private void ComposeOnce()
    {
        _Phase = 1;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        _PublishElapsed = 0.0;
        SetInitializing();
        FString Why;
        UCkSelectionGym_SourceComposition Created;
        bool Composed = UCkSelectionGym_SourceComposer_UE::TryCompose(
            _World, this, _WorldRoot, 1, _ScenarioGeneration, _AdmissionGeneration,
            _Scenario, FVector::ZeroVector, ACk_SelectionGym_LabelMarker,
            ACk_Gym_ObstacleWall, UCk_SelectionGym_State_Idle, Created, Why);
        _Composition = Created; // retain a partial first-return composition too
        if (!Composed) { Request_Fail(f"Source composition failed: {Why}"); }
    }

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || _Ending || _Failed || _Phase != 0) { return; }
        if (!TryCapture()) { return; }
        ComposeOnce();
    }

    private bool HasRequestOwner()
    {
        if (_RequestSerial == 0) { return true; }
        return ck::IsValid(_Requester) && !_Requester.IsActorBeingDestroyed() &&
            _Requester.GetClass() == ACk_SelectionGym_PlayerController &&
            _Requester.HasAuthority() && _Requester.GetWorld() == _World &&
            ck::IsValid(_RequesterPawn) && !_RequesterPawn.IsActorBeingDestroyed() &&
            _RequesterPawn.GetClass() == ACk_Gym_Base_Pawn &&
            _RequesterPawn.GetWorld() == _World &&
            _Requester.GetControlledPawn() == _RequesterPawn &&
            _RequesterPawn.GetController() == _Requester;
    }

    private void PublishComplete(FCkSelectionGym_WireSnapshot Frame)
    {
        if (!HasBinding() || !HasRequestOwner() ||
            Frame.Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
            Frame.SourceGeneration != 1 || Frame.ScenarioGeneration != _ScenarioGeneration ||
            Frame.Scenario != _Scenario || Frame.Sequence <= 0)
        { Request_Fail("Complete source publication lost its captured identity"); return; }
        Snapshot = Frame; _Phase = 2; _PublishElapsed = 0.0;
        ForceNetUpdate();
        CompleteRequest(true);
    }

    bool TryStartControl(ACk_SelectionGym_PlayerController Requester, int32 Serial,
        int64 SourceGeneration, int64 ScenarioGeneration, int32 Action)
    {
        if (!HasAuthority() || _Ending || _Failed || _Phase != 2 || !HasBinding() ||
            ck::Is_NOT_Valid(Requester) || Requester.IsActorBeingDestroyed() ||
            Requester.GetClass() != ACk_SelectionGym_PlayerController ||
            Requester.GetWorld() != _World || !Requester.HasAuthority() ||
            Serial <= 0 || _RequestSerial != 0 || SourceGeneration != 1 ||
            ScenarioGeneration != _ScenarioGeneration || Action < 0 || Action > 6 ||
            (Action == 6 && _Scenario != 4)) { return false; }
        auto Pawn = Requester.GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.IsActorBeingDestroyed() ||
            Pawn.GetWorld() != _World || Pawn.GetController() != Requester ||
            Pawn.GetClass() != ACk_Gym_Base_Pawn) { return false; }
        if (Action != 6 && (_ScenarioGeneration >= 9223372036854775806 ||
            _AdmissionGeneration >= 9223372036854775806)) { return false; }
        _Requester = Requester; _RequestSerial = Serial;
        _RequesterPawn = Pawn;
        _RequestScenarioGeneration = ScenarioGeneration; _RequestAction = Action;
        _NextScenario = Action < 5 ? Action : _Scenario;
        _Phase = Action == 6 ? 4 : 3;
        _PendingSince = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        FString Why;
        auto Session = UCkSelectionGym_SourceComposer_UE::Get_Session(_Composition);
        bool Started = Action == 6 ?
            UCkSelectionGym_SourceComposer_UE::TryStartRespawn(_Composition, Why) :
            UCkSelectionGym_SourceSession_UE::TryStartRetirement(Session, Why);
        if (!Started) { Request_Fail(f"Control retirement rejected: {Why}"); return true; }
        FCkSelectionGym_WireSnapshot Retiring;
        if (!UCkSelectionGym_SourceSession_UE::TryMakeLifecycleEnvelope(
                Session, ECkSelectionGym_WireLifecycle::Retiring, Retiring, Why))
        { Request_Fail(f"Control lifecycle rejected: {Why}"); return true; }
        Snapshot = Retiring;
        ForceNetUpdate();
        return true;
    }

    private void TickAuthority(float DeltaSeconds)
    {
        if (_Ending || _Failed) { return; }
        if (_Phase == 0) { Request_EnsureSharedDemo(); return; }
        if (!HasBinding()) { Request_Fail("Authority gym binding lost"); return; }
        if (!HasRequestOwner())
        { Request_Fail("Pending control lost its exact requesting owner"); return; }
        if (_Phase != 2 && UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
            _PendingSince > 15.0) { Request_Fail("Source transition did not settle"); return; }
        FString Why;
        FCkSelectionGym_WireSnapshot Next;
        if (_Phase == 1)
        {
            auto Outcome = UCkSelectionGym_SourceComposer_UE::TryPollStage(_Composition, Why);
            if (Outcome == ECkSelectionGym_SourceOutcome::Pending) { return; }
            if (Outcome == ECkSelectionGym_SourceOutcome::Invalid)
            { Request_Fail(f"Source staging rejected: {Why}"); return; }
            Outcome = UCkSelectionGym_SourceComposer_UE::TryActivateAndSample(_Composition, Next, Why);
            if (Outcome == ECkSelectionGym_SourceOutcome::Pending) { return; }
            if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
            { Request_Fail(f"Source activation rejected: {Why}"); return; }
            PublishComplete(Next);
            return;
        }
        if (_Phase == 3)
        {
            auto Session = UCkSelectionGym_SourceComposer_UE::Get_Session(_Composition);
            auto Outcome = UCkSelectionGym_SourceSession_UE::TryAdvanceRetirement(Session, Why);
            if (Outcome == ECkSelectionGym_SourceOutcome::Pending) { return; }
            if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
            { Request_Fail(f"Full retirement rejected: {Why}"); return; }
            _Composition = nullptr; _Scenario = _NextScenario;
            _ScenarioGeneration++; _AdmissionGeneration++;
            ComposeOnce();
            return;
        }
        if (_Phase == 4)
        {
            auto Outcome = UCkSelectionGym_SourceComposer_UE::TryAdvanceRespawn(_Composition, Why);
            if (Outcome == ECkSelectionGym_SourceOutcome::Pending) { return; }
            if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
            { Request_Fail(f"Respawn retirement rejected: {Why}"); return; }
            Outcome = UCkSelectionGym_SourceComposer_UE::TryFinishRespawn(_Composition, Next, Why);
            if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
            { Request_Fail(f"Respawn replacement rejected: {Why}"); return; }
            PublishComplete(Next);
            return;
        }
        if (DeltaSeconds < 0.0 || !Math::IsFinite(DeltaSeconds))
        { Request_Fail("Source tick delta is invalid"); return; }
        _PublishElapsed += DeltaSeconds;
        if (_PublishElapsed < 0.1) { return; }
        auto Outcome = _Scenario == 4 ?
            UCkSelectionGym_SourceComposer_UE::TryMoveAndSample(
                _Composition, _PublishElapsed, Next, Why) :
            UCkSelectionGym_SourceSession_UE::TrySample(
                UCkSelectionGym_SourceComposer_UE::Get_Session(_Composition), Next, Why);
        if (Outcome != ECkSelectionGym_SourceOutcome::Complete)
        { Request_Fail(f"Current source sampling rejected: {Why}"); return; }
        PublishComplete(Next);
    }

    private void TickClient()
    {
        if (_Ending || _ClientClosed) { return; }
        if (_Failed || SourceFailed)
        { Request_CloseClient("Authority or local gym session failed"); return; }
        if (!_Captured && !TryCapture()) { return; }
        if (!HasBinding()) { Request_CloseClient("Captured client gym binding lost"); return; }
        if (UCkSelectionGym_WireRuntime_UE::Get_IsDefaultFrame(Snapshot))
        {
            if (_HasConsumed) { Request_CloseClient("Received default after source binding"); }
            return;
        }
        FString Why;
        ECkSelectionGym_MirrorStatus Status;
        bool HasStatus = UCkSelectionGym_Mirror_UE::TryGetStatus(_World, this, Status, Why);
        if (_HasMirror && !HasStatus)
        { Request_CloseClient("Captured native client mirror disappeared"); return; }
        if (HasStatus) { _HasMirror = true; }
        if (HasStatus && Status == ECkSelectionGym_MirrorStatus::Failed)
        { Request_CloseClient("Native client mirror failed"); return; }
        if (!_HasConsumed || !UCkSelectionGym_WireRuntime_UE::Get_IsSameFrame(Snapshot, _Consumed))
        {
            // Settle a staged Active before nonterminal values can overtake it.
            if (Snapshot.Lifecycle == ECkSelectionGym_WireLifecycle::Failed ||
                !HasStatus || Status != ECkSelectionGym_MirrorStatus::Pending ||
                !_HasConsumed || _Consumed.Lifecycle != ECkSelectionGym_WireLifecycle::Active)
            {
                bool Accepted = Snapshot.Lifecycle == ECkSelectionGym_WireLifecycle::Active ?
                    UCkSelectionGym_Mirror_UE::TryAcceptActive(_World, this, Snapshot,
                        ACk_SelectionGym_LabelMarker, ACk_Gym_ObstacleWall, Why) :
                    UCkSelectionGym_Mirror_UE::TryAcceptEnvelope(_World, this, Snapshot, Why);
                bool NowHasStatus = UCkSelectionGym_Mirror_UE::TryGetStatus(_World, this, Status, Why);
                if (NowHasStatus) { _HasMirror = true; }
                if (!Accepted || (_HasMirror && !NowHasStatus) ||
                    (NowHasStatus && Status == ECkSelectionGym_MirrorStatus::Failed))
                { Request_CloseClient("Client snapshot intake rejected"); return; }
                _Consumed = Snapshot; _HasConsumed = true;
            }
        }
        HasStatus = UCkSelectionGym_Mirror_UE::TryGetStatus(_World, this, Status, Why);
        if (_HasMirror && !HasStatus)
        { Request_CloseClient("Accepted native client mirror disappeared"); return; }
        if (HasStatus && Status == ECkSelectionGym_MirrorStatus::Pending)
        {
            double Now = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
            if (_ClientPendingSince == 0.0) { _ClientPendingSince = Now; }
            if (Now - _ClientPendingSince > 15.0)
            { Request_CloseClient("Client mirror transition did not settle"); return; }
            UCkSelectionGym_Mirror_UE::TryAdvance(_World, this, Why);
            if (!UCkSelectionGym_Mirror_UE::TryGetStatus(_World, this, Status, Why) ||
                Status == ECkSelectionGym_MirrorStatus::Failed)
            { Request_CloseClient("Deferred client mirror failed"); }
        }
        else { _ClientPendingSince = 0.0; }
    }

    UFUNCTION(BlueprintCallable)
    bool TryGetPresentation(FCkSelectionGym_WireSnapshot&out Frame)
    {
        Frame = FCkSelectionGym_WireSnapshot();
        if (_Failed || SourceFailed || _Ending || !HasBinding() ||
            Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Active) { return false; }
        if (HasAuthority())
        {
            if (_Phase != 2) { return false; }
            Frame = Snapshot; return true;
        }
        FString Why; FCk_Handle Root; FCkSelectionGym_WireSnapshot Installed;
        if (_ClientClosed || !UCkSelectionGym_Mirror_UE::TryGetInstalled(
                _World, this, Installed, Root, Why) ||
            !UCkSelectionGym_WireRuntime_UE::Get_IsSameFrame(Snapshot, Installed)) { return false; }
        Frame = Installed;
        return true;
    }

    bool Get_HasSessionFailure() const { return _Failed || SourceFailed || _ClientClosed; }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (HasAuthority()) { TickAuthority(DeltaSeconds); }
        else { TickClient(); }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        if (HasAuthority()) { Request_Fail("Selection GameState ended"); }
        else { Request_CloseClient("Selection GameState ended"); }
        _Ending = true;
    }
}
