// Language=angelscript

/** Standard mesh marker plus UTextRenderComponent label for fixture identification. */
class ACk_SelectionGym_LabelMarker : AActor
{
    UPROPERTY(DefaultComponent, RootComponent)
    USceneComponent RootComponent;

    UPROPERTY(DefaultComponent, Attach = RootComponent)
    UStaticMeshComponent Mesh;
    default Mesh.CollisionEnabled = ECollisionEnabled::NoCollision;

    UPROPERTY(DefaultComponent, Attach = RootComponent)
    UTextRenderComponent LabelText;
    default LabelText.RelativeLocation = FVector(0.0f, 0.0f, 105.0f);
    default LabelText.SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    default LabelText.WorldSize = 32.0f;
    default LabelText.TextRenderColor = FColor::Cyan;

    void Set_Label(FString InLabel)
    {
        LabelText.SetText(FText::FromString(InLabel));
    }
}
USTRUCT()
struct FCkSelectionGym_ControlAck
{
    UPROPERTY() int32 Serial = 0;
    UPROPERTY() int64 SourceGeneration = 0;
    UPROPERTY() int64 ScenarioGeneration = 0;
    UPROPERTY() int32 Action = 0;
    // 0: none; 1: authority physical source completed; 2: rejected.
    UPROPERTY() int32 Result = 0;
    UPROPERTY() int64 CompletedScenarioGeneration = 0;
    UPROPERTY() int64 CompletedSequence = 0;
    UPROPERTY() int32 CompletedScenario = -1;
}

class ACk_SelectionGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) FCkSelectionGym_ControlAck LastControlAck;

    private bool _Admitted = false;
    private bool _Ending = false;
    private bool _LocalFailed = false;
    private bool _StationsBound = false;
    private bool _Positioned = false;
    private bool _PawnCaptured = false;
    private bool _StateCaptured = false;
    private bool _CatalogCaptured = false;
    private bool _ServerPending = false;
    private double _BootstrapStartedSeconds = 0.0;
    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int64 _PendingSourceGeneration = 0;
    private int64 _PendingScenarioGeneration = 0;
    private int32 _PendingAction = -1;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;
    private UWorld _BoundWorld;
    private ACk_SelectionGym_GameState _BoundState;
    private FCk_Handle _Station;
    private FCk_Handle _StationCohort;
    private APawn _BoundPawn;

    FCk_Handle Get_StationLifetimeOwner() override
    { return IsLocalController() ? ck::ToEntity(this) : FCk_Handle(); }
    FString Get_StationTag(FString Tag) override
    { return f"{Tag}_{GetName()}"; }

    private bool IsDirect(FCk_Handle Child, FCk_Handle Parent)
    {
        return ck::IsValid(Child) && ck::IsValid(Parent) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                Child, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }

    private ACk_SelectionGym_GameState Get_ExactState()
    {
        auto World = GetWorld();
        if (!_Admitted || _Ending || IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(World) || World != _BoundWorld) { return nullptr; }
        auto State = Cast<ACk_SelectionGym_GameState>(World.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            State.GetClass() != ACk_SelectionGym_GameState ||
            State.IsActorBeingDestroyed() ||
            (_StateCaptured && State != _BoundState)) { return nullptr; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_SelectionGym_GameMode) { return nullptr; }
        }
        if (!State.Get_HasPublishedCatalog()) { return nullptr; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return nullptr; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Selection" ||
            Entry.GameModeClass != ACk_SelectionGym_GameMode ||
            (_CatalogCaptured && (Index != _CatalogIndex ||
                Entry.DisplayName != _CatalogEntry.DisplayName ||
                Entry.GameModeClass != _CatalogEntry.GameModeClass ||
                Entry.LevelName != _CatalogEntry.LevelName ||
                Entry.Category != _CatalogEntry.Category ||
                Entry.HintCode != _CatalogEntry.HintCode))) { return nullptr; }
        return State;
    }

    private bool CaptureStateIfReady()
    {
        auto World = GetWorld();
        auto Observed = ck::IsValid(World) ? Cast<ACk_SelectionGym_GameState>(World.GetGameState()) : nullptr;
        if (ck::IsValid(Observed) && Observed.GetWorld() == World &&
            Observed.GetClass() == ACk_SelectionGym_GameState &&
            !Observed.IsActorBeingDestroyed() && !_StateCaptured)
        { _BoundState = Observed; _StateCaptured = true; }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State)) { return false; }
        if (!_StateCaptured) { _BoundState = State; _StateCaptured = true; }
        if (!_CatalogCaptured)
        {
            _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
            _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
            _CatalogCaptured = true;
        }
        return true;
    }

    private bool HasExactPawn()
    {
        auto Pawn = GetControlledPawn();
        return _PawnCaptured && ck::IsValid(Pawn) &&
            Pawn == _BoundPawn && Pawn.GetWorld() == _BoundWorld &&
            !Pawn.IsActorBeingDestroyed() &&
            Pawn.GetClass() == ACk_Gym_Base_Pawn &&
            Pawn.GetController() == this;
    }

    private bool CapturePawnIfReady()
    {
        if (_PawnCaptured) { return HasExactPawn(); }
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetWorld() != _BoundWorld ||
            Pawn.IsActorBeingDestroyed() ||
            Pawn.GetClass() != ACk_Gym_Base_Pawn ||
            Pawn.GetController() != this) { return false; }
        _BoundPawn = Pawn; _PawnCaptured = true;
        return HasExactPawn();
    }

    private bool CheckStation(bool Bind)
    {
        if (!IsLocalController() || !HasExactPawn()) { return false; }
        auto ControllerEntity = ck::ToEntity(this);
        auto WorldRoot = ck::TransientEntity();
        if (ck::Is_NOT_Valid(ControllerEntity) || ck::Is_NOT_Valid(WorldRoot) ||
            utils_entity_lifetime::Get_WorldForEntity(WorldRoot) != _BoundWorld)
        { return false; }
        auto Matches = utils_entity_tag::ForEach_Entity(
            WorldRoot, FName(Get_StationTag("Gym.Selection")));
        if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Matches[0])) { return false; }
        auto Station = Matches[0];
        auto Cohort = utils_entity_lifetime::Get_LifetimeOwner(Station);
        if (!IsDirect(Station, Cohort) || !IsDirect(Cohort, ControllerEntity) ||
            (Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter -
                FVector::ZeroVector).SizeSquared() > 1.0 ||
            (_StationsBound && (Station != _Station || Cohort != _StationCohort)))
        { return false; }
        auto Script = Cast<UCk_EntityScript_GymStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station));
        if (ck::Is_NOT_Valid(Script) ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Script) != Station)
        { return false; }
        if (Bind && !_StationsBound)
        { _Station = Station; _StationCohort = Cohort; _StationsBound = true; }
        return _StationsBound;
    }

    private void CloseLocal(FString Reason)
    {
        if (_LocalFailed) { return; }
        _LocalFailed = true;
        // The current catalog may already be gone. Close only the exact
        // previously captured GameState in its captured world.
        if (_StateCaptured && ck::IsValid(_BoundState) &&
            _BoundState.GetWorld() == _BoundWorld)
        {
            if (HasAuthority()) { _BoundState.Request_Fail(Reason); }
            else { _BoundState.Request_CloseClient(Reason); }
        }
        ck::Trace(f"Selection local presentation closed: {Reason}");
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        if (!IsLocalController()) { return Stations; }
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(FName(Get_StationTag("Gym.Selection")));
        Station.Transform = FTransform(FRotator(0.0, 180.0, 0.0), FVector::ZeroVector);
        Station.Title = FText::FromString("DEBUG OVERLAY SELECTION");
        Station.AutoSize = true;
        TArray<FText> Description;
        Description.Add(FText::FromString("Five shared ECS fixture banks exercise root labels, context roots, hierarchy, visibility, and lifecycle."));
        Description.Add(FText::FromString("Enable the Debug Overlay, then use Select / Next / Prev / Family while changing scenarios."));
        Description.Add(FText::FromString("Panel numbers replace the complete shared bank; [R] rebuilds it and [X] respawns the lifecycle target."));
        Station.Description = Description;
        Stations.Add(Station);
        return Stations;
    }

    void Request_StartGym() override
    {
        if (_Ending || _Admitted) { return; }
        _BoundWorld = GetWorld();
        if (ck::Is_NOT_Valid(_BoundWorld)) { _LocalFailed = true; return; }
        _Admitted = true;
        _BootstrapStartedSeconds = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        CaptureStateIfReady();
    }

    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    {
        if (!_Admitted || _Ending || _LocalFailed) { return; }
        if (ck::Is_NOT_Valid(_BoundWorld) || GetWorld() != _BoundWorld ||
            IsActorBeingDestroyed()) { CloseLocal("Controller world changed"); return; }
        if (!CaptureStateIfReady())
        {
            auto Current = Cast<ACk_SelectionGym_GameState>(
                _BoundWorld.GetGameState());
            if (_CatalogCaptured ||
                (_StateCaptured && (ck::Is_NOT_Valid(_BoundState) ||
                    Current != _BoundState ||
                    _BoundState.IsActorBeingDestroyed())) ||
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("Selection GameState or catalog did not converge"); }
            return; // initial catalog/replication bootstrap is asynchronous
        }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State)) { CloseLocal("Selection state changed"); return; }
        if (State.Get_HasSessionFailure())
        { CloseLocal("Shared Selection session failed"); return; }
        if (!CapturePawnIfReady())
        {
            if (_PawnCaptured ||
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("Selection pawn did not converge or changed"); }
            return;
        }
        if (HasAuthority()) { State.Request_EnsureSharedDemo(); }
        if (!IsLocalController()) { return; }
        if (!CheckStation(!_StationsBound))
        { CloseLocal("Local Selection station changed"); return; }
        FCkSelectionGym_WireSnapshot Frame;
        // Retiring and client mirror installation temporarily have no complete
        // presentation. The GameState owns their bounded terminal failure latch.
        if (!State.TryGetPresentation(Frame)) { return; }
        if (Frame.Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
            Frame.SourceGeneration <= 0 || Frame.ScenarioGeneration <= 0 ||
            Frame.Scenario < 0 || Frame.Scenario > 4)
        { CloseLocal("Selection presentation is malformed"); return; }
        if (!_Positioned)
        {
            _BoundPawn.SetActorLocation(FVector(650.0, 0.0, 320.0));
            if (!HasExactPawn()) { CloseLocal("Local pawn changed during placement"); return; }
            SetControlRotation(FRotator(-12.0, 180.0, 0.0));
            if (!HasExactPawn()) { CloseLocal("Local pawn changed during view placement"); return; }
            _Positioned = true;
        }
    }

    private bool TryReadyFrame(FCkSelectionGym_WireSnapshot&out Frame)
    {
        Frame = FCkSelectionGym_WireSnapshot();
        if (!_Admitted || _Ending || _LocalFailed || !IsLocalController() ||
            !_Positioned || !CheckStation(false) || !HasExactPawn() ||
            _RequestedSerial != LastControlAck.Serial) { return false; }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State) || State.Get_HasSessionFailure() ||
            !State.TryGetPresentation(Frame) ||
            Frame.Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
            Frame.SourceGeneration <= 0 || Frame.ScenarioGeneration <= 0 ||
            Frame.Scenario < 0 || Frame.Scenario > 4) { return false; }
        if (LastControlAck.Result != 1) { return true; }
        if (LastControlAck.CompletedScenarioGeneration <= 0 ||
            LastControlAck.CompletedSequence <= 0 ||
            LastControlAck.CompletedScenario < 0 ||
            LastControlAck.CompletedScenario > 4 ||
            Frame.SourceGeneration != LastControlAck.SourceGeneration) { return false; }
        return Frame.ScenarioGeneration >
                LastControlAck.CompletedScenarioGeneration ||
            (Frame.ScenarioGeneration ==
                LastControlAck.CompletedScenarioGeneration &&
             Frame.Sequence >= LastControlAck.CompletedSequence &&
             Frame.Scenario == LastControlAck.CompletedScenario);
    }

    UFUNCTION(BlueprintPure) bool Get_IsLocalPresentationReady()
    {
        FCkSelectionGym_WireSnapshot Frame;
        return TryReadyFrame(Frame);
    }

    UFUNCTION(Server) void Server_RequestSelectionControl(
        int32 Serial, int64 SourceGeneration, int64 ScenarioGeneration, int32 Action)
    {
        if (!HasAuthority() || !_Admitted || _Ending || _ServerPending ||
            Serial <= 0 || LastControlAck.Serial >= 2147483647 ||
            Serial != LastControlAck.Serial + 1 || Action < 0 || Action > 6)
        { return; } // malformed/replayed RPC never advances the acknowledged serial
        FCkSelectionGym_ControlAck Rejected;
        Rejected.Serial = Serial;
        Rejected.SourceGeneration = SourceGeneration;
        Rejected.ScenarioGeneration = ScenarioGeneration;
        Rejected.Action = Action;
        Rejected.Result = 2;
        auto State = Get_ExactState();
        FCkSelectionGym_WireSnapshot Frame;
        if (ck::Is_NOT_Valid(State) || !HasExactPawn() ||
            !State.TryGetPresentation(Frame) ||
            Frame.Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
            SourceGeneration <= 0 || ScenarioGeneration <= 0 ||
            SourceGeneration != Frame.SourceGeneration ||
            ScenarioGeneration != Frame.ScenarioGeneration ||
            (Action == 6 && Frame.Scenario != 4))
        { LastControlAck = Rejected; ForceNetUpdate(); return; }
        _ServerPending = true;
        _PendingSerial = Serial;
        _PendingSourceGeneration = SourceGeneration;
        _PendingScenarioGeneration = ScenarioGeneration;
        _PendingAction = Action;
        if (!State.TryStartControl(this, Serial, SourceGeneration,
                ScenarioGeneration, Action))
        {
            // A synchronous successful completion already cleared this tuple.
            if (_ServerPending && _PendingSerial == Serial)
            {
                _ServerPending = false;
                _PendingSerial = 0;
                _PendingAction = -1;
                LastControlAck = Rejected;
                ForceNetUpdate();
            }
        }
    }

    void Complete_SelectionControl(int32 Serial, int64 SourceGeneration,
        int64 ScenarioGeneration, int32 Action, bool Success,
        int64 CompletedScenarioGeneration, int64 CompletedSequence,
        int32 CompletedScenario)
    {
        auto State = Get_ExactState();
        if (!HasAuthority() || !_Admitted || _Ending || !_ServerPending ||
            Serial != _PendingSerial || SourceGeneration != _PendingSourceGeneration ||
            ScenarioGeneration != _PendingScenarioGeneration || Action != _PendingAction ||
            LastControlAck.Serial + 1 != Serial || ck::Is_NOT_Valid(State))
        { return; }
        if (Success && (!HasExactPawn() || State.Get_HasSessionFailure() ||
            CompletedScenarioGeneration <= 0 || CompletedSequence <= 0 ||
            CompletedScenario < 0 || CompletedScenario > 4 ||
            State.Snapshot.Lifecycle != ECkSelectionGym_WireLifecycle::Active ||
            State.Snapshot.SourceGeneration != SourceGeneration ||
            State.Snapshot.ScenarioGeneration != CompletedScenarioGeneration ||
            State.Snapshot.Sequence != CompletedSequence ||
            State.Snapshot.Scenario != CompletedScenario))
        {
            State.Request_Fail("Completed control lost its exact owner or frame");
            return;
        }
        _ServerPending = false;
        _PendingSerial = 0;
        _PendingAction = -1;
        FCkSelectionGym_ControlAck Completed;
        Completed.Serial = Serial;
        Completed.SourceGeneration = SourceGeneration;
        Completed.ScenarioGeneration = ScenarioGeneration;
        Completed.Action = Action;
        Completed.Result = Success ? 1 : 2;
        if (Success)
        {
            Completed.CompletedScenarioGeneration = CompletedScenarioGeneration;
            Completed.CompletedSequence = CompletedSequence;
            Completed.CompletedScenario = CompletedScenario;
        }
        LastControlAck = Completed;
        ForceNetUpdate();
    }

    private void Request_SharedControl(int32 Action)
    {
        FCkSelectionGym_WireSnapshot Frame;
        if (!TryReadyFrame(Frame) || (Action == 6 && Frame.Scenario != 4) ||
            LastControlAck.Serial >= 2147483647) { return; }
        _RequestedSerial = LastControlAck.Serial + 1;
        Server_RequestSelectionControl(_RequestedSerial,
            Frame.SourceGeneration, Frame.ScenarioGeneration, Action);
    }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        Super::EndPlay(Reason);
    }

    FString Get_ControlPanelTitle() override { return "SELECTION FIXTURES"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        FCkSelectionGym_WireSnapshot Frame;
        const bool Ready = TryReadyFrame(Frame);
        const int32 Scenario = Ready ? Frame.Scenario : -1;
        Rows.Add(CkGym_Control::Header("SCENARIO"));
        Rows.Add(CkGym_Control::Status("Active", DoGetScenarioLabel(Scenario)));
        Rows.Add(CkGym_Control::Numbered(0, "Topology and context roots", Scenario == 0, Ready));
        Rows.Add(CkGym_Control::Numbered(1, "Distance and overlap", Scenario == 1, Ready));
        Rows.Add(CkGym_Control::Numbered(2, "Mixed ECS hierarchy", Scenario == 2, Ready));
        Rows.Add(CkGym_Control::Numbered(3, "Visibility and occlusion", Scenario == 3, Ready));
        Rows.Add(CkGym_Control::Numbered(4, "Moving and respawn", Scenario == 4, Ready));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Rebuild active scenario", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Delete and respawn target", Ready && Scenario == 4));
        Rows.Add(CkGym_Control::Status("Overlay", "[,] toggle | [Shift+,] or [Shift+P] settings"));
        return Rows;
    }

    UFUNCTION() void Request_ControlActivated(int32 RowIndex) override
    {
        if (RowIndex >= 2 && RowIndex <= 6)
        { Request_SharedControl(RowIndex - 2); }
        else if (RowIndex == 7) { Request_SharedControl(5); }
        else if (RowIndex == 8) { Request_SharedControl(6); }
    }

    private FString DoGetScenarioLabel(int32 Scenario) const
    {
        if (Scenario == 0) { return "Topology"; }
        if (Scenario == 1) { return "Distance"; }
        if (Scenario == 2) { return "Hierarchy"; }
        if (Scenario == 3) { return "Visibility"; }
        if (Scenario == 4) { return "Lifecycle"; }
        return "Waiting for shared source";
    }
}