class ACk_SmTest_GymPlayerController : ACk_Gym_Base_PlayerController
{
    // ========================================================================
    // STATION DEFINITION
    // ========================================================================

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        if (!IsLocalController()) { return Stations; }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(0))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(0);
            Station.Title = FText::FromString("AUTO CYCLE");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("State machine cycles: Idle -> Patrol -> Alert."));
            Description.Add(FText::FromString("Tests SM creation, auto-start, transitions, and signal binding."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(1))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(1);
            Station.Title = FText::FromString("PAUSE / RESUME");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("State machine pauses after 6 transitions, resumes after 3 seconds."));
            Description.Add(FText::FromString("Tests SM pause and resume functionality."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(2))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(2);
            Station.Title = FText::FromString("COMPLEX GRAPH");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("6-state SM: Idle, Patrol, Chase, Attack, Search, Flee."));
            Description.Add(FText::FromString("Tests branching, bidirectional, multi-condition transitions, polled + event conditions, tasks."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(3))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(3);
            Station.Title = FText::FromString("HIERARCHICAL (SUB-SM)");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Parent SM: Approach -> Engage -> Retreat."));
            Description.Add(FText::FromString("Engage spawns a child SM: WindUp -> Strike -> Recover (cycles)."));
            Description.Add(FText::FromString("Tests UCk_SmTask_SubStateMachine, context propagation, viewer drill-down."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(4))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(4);
            Station.Title = FText::FromString("GRAPH-WALK REGRESSION");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Guards CkFoundation PR #643: graph-walk temp entities must not fire task DoEnterTask bodies."));
            Description.Add(FText::FromString("Constructs two 5-state linear SMs (top-level + sub-SM) gated by polled-false conditions."));
            Description.Add(FText::FromString("PASS = only initial states incremented counters, both SMs still Running."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(5))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(5);
            Station.Title = FText::FromString("DIVERGENCE FIRST-BRANCH");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Sub-SM with a divergence-point state (two outgoing transitions). Asserts that the first-added transition's target state-task chain is built exactly once when that branch is chosen."));
            Description.Add(FText::FromString("Vacuous-transition variant: linear hops have no conditions, so transitions Pass on first evaluation. Exercises the fast-path race window between transition-firing and source-state teardown."));
            Description.Add(FText::FromString("Two passes (Pass A: AddLeft-first + ChooseLeft; Pass B: AddRight-first + ChooseRight) confirm doubling tracks add-order, not state class or direction."));
            Description.Add(FText::FromString("PASS = every per-state task counter == 1. FAIL = first-added-and-chosen branch counter == 2."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(6))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(6);
            Station.Title = FText::FromString("DIVERGENCE FIRST-BRANCH (TIMED)");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Same divergence-point assertion as DIVERGENCE FIRST-BRANCH, but every linear transition is gated by a 0.05s event-driven timer condition. Exercises the slow-path race: condition Pass arrives asynchronously while the source state is in PendingExit."));
            Description.Add(FText::FromString("Together with the vacuous variant, covers both immediate-Pass and timer-Pass divergence handling during state teardown."));
            Description.Add(FText::FromString("Toggle the framework fix off and rebuild to confirm this station FAILs with the bug present (chosen-and-first-added counter reads 2)."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(7))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(7);
            Station.Title = FText::FromString("RACING EVENT-DRIVEN");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("State with two racing event-driven transitions: ToDestA (slow timer, declared FIRST) and ToDestB (fast timer, declared SECOND)."));
            Description.Add(FText::FromString("Guards FProcessor_SmState_Evaluate's first-Undetermined-Break behavior. With the bug, only the slow timer ever starts ticking; SM lands on DestA at the slower delay."));
            Description.Add(FText::FromString("PASS = SM lands on DestB (the second-declared, faster transition wins)."));
            Description.Add(FText::FromString("FAIL = SM lands on DestA (state evaluator Break'd on ToDestA's Undetermined and never inspected ToDestB)."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkStateMachineGym::Get_Tag(8))));
            Station.Transform = CkStateMachineGym::Get_StationTransform(8);
            Station.Title = FText::FromString("EVENT-DRIVEN MULTI-CONDITION");
            auto Description = TArray<FText>();
            Description.Add(FText::FromString("Single transition Idle->Finish gated by TWO event-driven timer conditions: FastEvent (resolves at 0.1s) and SlowEvent (resolves at 0.4s)."));
            Description.Add(FText::FromString("Guards the framework contract that event-driven cond Pass results are preserved across transition Reset cycles. Between 0.1s and 0.4s the transition is in Fail (one Pass, one Fail) and state.Evaluate cycles through Reset many times; FastEvent's Pass MUST survive those cycles."));
            Description.Add(FText::FromString("PASS = Counter_Finish == 1 (transition fired after SlowEvent resolved). FAIL = Counter_Finish == 0 (FastEvent's Pass was lost across Reset)."));
            Station.Description = Description;
            Stations.Add(Station);
        }

        return Stations;
    }

    UPROPERTY(Replicated) FCkStateMachineGym_ControlAck LastControlAck;

    private bool _Admitted = false;
    private bool _Ending = false;
    private bool _LocalFailed = false;
    private bool _StateCaptured = false;
    private bool _CatalogCaptured = false;
    private bool _PawnCaptured = false;
    private bool _StationsBound = false;
    private bool _ServerPending = false;
    private double _BootstrapStartedSeconds = 0.0;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;
    private UWorld _BoundWorld;
    private ACk_SmTest_GymGameState _BoundState;
    private APawn _BoundPawn;
    private TArray<FCk_Handle> _Stations;
    private TArray<FCk_Handle> _StationCohorts;
    private int64 _ProjectedSourceGeneration = 0;
    private int64 _ProjectedSequence = 0;
    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _PendingStationIndex = -1;
    private int64 _PendingSourceGeneration = 0;
    private int64 _PendingGeneration = 0;

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

    private ACk_SmTest_GymGameState Get_ExactState()
    {
        auto World = GetWorld();
        if (!_Admitted || _Ending || IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(World) || World != _BoundWorld) { return nullptr; }
        auto State = Cast<ACk_SmTest_GymGameState>(World.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            State.GetClass() != ACk_SmTest_GymGameState ||
            State.IsActorBeingDestroyed() ||
            (_StateCaptured && State != _BoundState)) { return nullptr; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_SmTest_GymGameMode) { return nullptr; }
        }
        if (!State.Get_HasPublishedCatalog()) { return nullptr; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return nullptr; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "State Machine" ||
            Entry.GameModeClass != ACk_SmTest_GymGameMode ||
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
        auto Observed = ck::IsValid(World)
            ? Cast<ACk_SmTest_GymGameState>(World.GetGameState()) : nullptr;
        if (ck::IsValid(Observed) && Observed.GetWorld() == World &&
            Observed.GetClass() == ACk_SmTest_GymGameState &&
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
        _BoundPawn = Pawn;
        _PawnCaptured = true;
        return HasExactPawn();
    }

    private bool CheckStations(bool Bind)
    {
        if (!IsLocalController() || !HasExactPawn()) { return false; }
        auto ControllerEntity = ck::ToEntity(this);
        auto WorldRoot = ck::TransientEntity();
        if (ck::Is_NOT_Valid(ControllerEntity) ||
            ck::Is_NOT_Valid(WorldRoot) ||
            utils_entity_lifetime::Get_WorldForEntity(WorldRoot) != _BoundWorld ||
            (_StationsBound && (_Stations.Num() != 9 ||
                _StationCohorts.Num() != 9))) { return false; }

        TArray<FCk_Handle> Found;
        TArray<FCk_Handle> Cohorts;
        for (int32 I = 0; I < 9; ++I)
        {
            auto Tag = CkStateMachineGym::Get_Tag(I);
            if (Tag.IsEmpty()) { return false; }
            auto Matches = utils_entity_tag::ForEach_Entity(
                WorldRoot, FName(Get_StationTag(Tag)));
            if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]) ||
                !Matches[0].Has_Fragment(FCkGym_Station_Anchors) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Matches[0]))
            { return false; }
            auto Station = Matches[0];
            auto Cohort = utils_entity_lifetime::Get_LifetimeOwner(Station);
            if (!IsDirect(Station, Cohort) ||
                !IsDirect(Cohort, ControllerEntity) ||
                (Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter -
                    CkStateMachineGym::Get_StationTransform(I).GetLocation()
                ).SizeSquared() > 1.0 ||
                (_StationsBound &&
                    (Station != _Stations[I] || Cohort != _StationCohorts[I])))
            { return false; }
            auto Script = Cast<UCk_EntityScript_GymStation>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station));
            if (ck::Is_NOT_Valid(Script) ||
                UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Script)
                    != Station || Found.Contains(Station))
            { return false; }
            Found.Add(Station);
            Cohorts.Add(Cohort);
        }
        if (Bind && !_StationsBound)
        {
            _Stations = Found;
            _StationCohorts = Cohorts;
            _StationsBound = true;
        }
        return _StationsBound;
    }

    private void CloseLocal(FString Reason)
    {
        if (_LocalFailed) { return; }
        _LocalFailed = true;
        if (_StateCaptured && ck::IsValid(_BoundState) &&
            _BoundState.GetWorld() == _BoundWorld)
        {
            if (HasAuthority()) { _BoundState.Request_Fail(Reason); }
            else { _BoundState.Request_CloseClient(Reason); }
        }
        ck::Trace(f"State Machine local presentation closed: {Reason}");
    }

    void Request_StartGym() override
    {
        if (_Ending || _Admitted) { return; }
        _BoundWorld = GetWorld();
        if (ck::Is_NOT_Valid(_BoundWorld)) { _LocalFailed = true; return; }
        _Admitted = true;
        _BootstrapStartedSeconds =
            UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        CaptureStateIfReady();
    }

    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    {
        if (!_Admitted || _Ending || _LocalFailed) { return; }
        if (ck::Is_NOT_Valid(_BoundWorld) || GetWorld() != _BoundWorld ||
            IsActorBeingDestroyed())
        { CloseLocal("Controller world changed"); return; }
        if (!CaptureStateIfReady())
        {
            auto Current = Cast<ACk_SmTest_GymGameState>(
                _BoundWorld.GetGameState());
            if (_CatalogCaptured ||
                (_StateCaptured && (ck::Is_NOT_Valid(_BoundState) ||
                    Current != _BoundState ||
                    _BoundState.IsActorBeingDestroyed())) ||
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("State Machine GameState or catalog did not converge"); }
            return;
        }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State))
        { CloseLocal("State Machine state changed"); return; }
        if (State.Get_HasSessionFailure())
        { CloseLocal("Shared State Machine session failed"); return; }
        if (!CapturePawnIfReady())
        {
            if (_PawnCaptured ||
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("State Machine pawn did not converge or changed"); }
            return;
        }
        if (HasAuthority()) { State.Request_EnsureSharedDemo(); }
        if (!IsLocalController()) { return; }
        if (!CheckStations(!_StationsBound))
        { CloseLocal("Local State Machine stations changed"); return; }
        FCkStateMachineGym_WireSnapshot Frame;
        if (!State.TryGetPresentation(Frame)) { return; }
        if (!ValidateFrame(Frame))
        { CloseLocal("State Machine presentation is malformed"); return; }
        if (_ProjectedSourceGeneration == Frame.SourceGeneration &&
            _ProjectedSequence == Frame.Sequence) { return; }
        if (_ProjectedSourceGeneration > Frame.SourceGeneration ||
            (_ProjectedSourceGeneration == Frame.SourceGeneration &&
                _ProjectedSequence > Frame.Sequence))
        { CloseLocal("State Machine presentation went backward"); return; }
        // Every owner and every frame is checked before the first local write.
        // Reruns update text on the same nine controller-owned station entities.
        for (int32 I = 0; I < 9; ++I)
        {
            auto Title = FText::FromString(Frame.Stations[I].Title);
            auto Detail = FText::FromString(Frame.Stations[I].Detail);
            auto& Text = _Stations[I].AddOrGet_Fragment(
                FCkGym_Station_TitleAndDescription);
            Text.Title = Title;
            Text.Description = Detail;
            auto& Colored = _Stations[I].AddOrGet_Fragment(
                FCkGym_Station_ColoredBody);
            Colored.Title = Title;
            Colored.Lines = TArray<FCkGym_ColoredLine>();
            Colored.Lines.Add(FCkGym_ColoredLine(
                Frame.Stations[I].Detail, Frame.Stations[I].Color));
            Colored.Active = true;
        }
        _ProjectedSourceGeneration = Frame.SourceGeneration;
        _ProjectedSequence = Frame.Sequence;
    }

    private bool ValidateFrame(FCkStateMachineGym_WireSnapshot Frame)
    {
        if (Frame.Phase != 2 || Frame.Replacing != -1 ||
            Frame.SourceGeneration <= 0 || Frame.Sequence <= 0 ||
            Frame.Stations.Num() != 9) { return false; }
        for (int32 I = 0; I < 9; ++I)
        {
            if (Frame.Stations[I].Index != I ||
                Frame.Stations[I].Generation <= 0) { return false; }
        }
        return true;
    }

    private bool TryReadyFrame(FCkStateMachineGym_WireSnapshot&out Frame)
    {
        Frame = FCkStateMachineGym_WireSnapshot();
        if (!_Admitted || _Ending || _LocalFailed || !IsLocalController() ||
            !CheckStations(false) || !HasExactPawn() ||
            _RequestedSerial != LastControlAck.Serial) { return false; }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State) || State.Get_HasSessionFailure() ||
            !State.TryGetPresentation(Frame) || !ValidateFrame(Frame) ||
            _ProjectedSourceGeneration != Frame.SourceGeneration ||
            _ProjectedSequence != Frame.Sequence) { return false; }
        if (LastControlAck.Result != 1) { return true; }
        int32 Index = LastControlAck.StationIndex;
        return Index >= 0 && Index < 9 &&
            LastControlAck.RequestedGeneration > 0 &&
            LastControlAck.CompletedGeneration > 0 &&
            LastControlAck.CompletedSequence > 0 &&
            Frame.Stations[Index].Generation >=
                LastControlAck.CompletedGeneration &&
            Frame.Sequence >= LastControlAck.CompletedSequence;
    }

    UFUNCTION(BlueprintPure) bool Get_IsLocalPresentationReady()
    {
        FCkStateMachineGym_WireSnapshot Frame;
        return TryReadyFrame(Frame);
    }

    UFUNCTION(Server) void Server_RequestStateMachineControl(
        int32 Serial, int64 SourceGeneration, int32 StationIndex,
        int64 Generation)
    {
        if (!HasAuthority() || !_Admitted || _Ending || _ServerPending ||
            Serial <= 0 || LastControlAck.Serial >= 2147483647 ||
            Serial != LastControlAck.Serial + 1 ||
            StationIndex < 0 || StationIndex >= 9)
        { return; } // malformed or replayed request leaves the ack inert
        FCkStateMachineGym_ControlAck Rejected;
        Rejected.Serial = Serial;
        Rejected.StationIndex = StationIndex;
        Rejected.RequestedGeneration = Generation;
        Rejected.Result = 2;
        auto State = Get_ExactState();
        FCkStateMachineGym_WireSnapshot Frame;
        if (ck::Is_NOT_Valid(State) || !HasExactPawn() ||
            !State.TryGetPresentation(Frame) || !ValidateFrame(Frame) ||
            SourceGeneration <= 0 || Generation <= 0 ||
            SourceGeneration != Frame.SourceGeneration ||
            Generation != Frame.Stations[StationIndex].Generation)
        { LastControlAck = Rejected; ForceNetUpdate(); return; }
        _ServerPending = true;
        _PendingSerial = Serial;
        _PendingStationIndex = StationIndex;
        _PendingSourceGeneration = SourceGeneration;
        _PendingGeneration = Generation;
        if (!State.TryStartControl(this, Serial, SourceGeneration,
                StationIndex, Generation))
        {
            if (_ServerPending && _PendingSerial == Serial)
            {
                _ServerPending = false;
                _PendingSerial = 0;
                _PendingStationIndex = -1;
                LastControlAck = Rejected;
                ForceNetUpdate();
            }
        }
    }

    void Complete_StateMachineControl(int32 Serial, int32 StationIndex,
        int64 RequestedGeneration, bool Success, int64 CompletedGeneration,
        int64 CompletedSequence)
    {
        auto State = Get_ExactState();
        if (!HasAuthority() || !_Admitted || _Ending || !_ServerPending ||
            Serial != _PendingSerial ||
            StationIndex != _PendingStationIndex ||
            RequestedGeneration != _PendingGeneration ||
            LastControlAck.Serial + 1 != Serial ||
            ck::Is_NOT_Valid(State)) { return; }
        if (Success && (!HasExactPawn() || State.Get_HasSessionFailure() ||
            StationIndex < 0 || StationIndex >= 9 ||
            CompletedGeneration <= RequestedGeneration ||
            CompletedSequence <= 0 ||
            State.Snapshot.Phase != 2 ||
            State.Snapshot.SourceGeneration != _PendingSourceGeneration ||
            State.Snapshot.Sequence != CompletedSequence ||
            State.Snapshot.Stations.Num() != 9 ||
            State.Snapshot.Stations[StationIndex].Generation !=
                CompletedGeneration))
        {
            State.Request_Fail("Completed control lost its exact owner or frame");
            return;
        }
        _ServerPending = false;
        _PendingSerial = 0;
        _PendingStationIndex = -1;
        FCkStateMachineGym_ControlAck Completed;
        Completed.Serial = Serial;
        Completed.StationIndex = StationIndex;
        Completed.RequestedGeneration = RequestedGeneration;
        Completed.Result = Success ? 1 : 2;
        if (Success)
        {
            Completed.CompletedGeneration = CompletedGeneration;
            Completed.CompletedSequence = CompletedSequence;
        }
        LastControlAck = Completed;
        ForceNetUpdate();
    }

    private void Request_SharedControl(int32 StationIndex)
    {
        FCkStateMachineGym_WireSnapshot Frame;
        if (StationIndex < 0 || StationIndex >= 9 ||
            !TryReadyFrame(Frame) ||
            LastControlAck.Serial >= 2147483647) { return; }
        _RequestedSerial = LastControlAck.Serial + 1;
        Server_RequestStateMachineControl(_RequestedSerial,
            Frame.SourceGeneration, StationIndex,
            Frame.Stations[StationIndex].Generation);
    }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        Super::EndPlay(Reason);
    }

    FString Get_ControlPanelTitle() override { return "STATE MACHINE"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        FCkStateMachineGym_WireSnapshot Frame;
        const bool Ready = TryReadyFrame(Frame);
        Rows.Add(CkGym_Control::Header("RE-RUN A STATION"));
        Rows.Add(CkGym_Control::Numbered(0, "Auto cycle", false, Ready));
        Rows.Add(CkGym_Control::Numbered(1, "Pause / resume", false, Ready));
        Rows.Add(CkGym_Control::Numbered(2, "Complex", false, Ready));
        Rows.Add(CkGym_Control::Numbered(3, "Hierarchical", false, Ready));
        Rows.Add(CkGym_Control::Numbered(4, "GraphWalk regression", false, Ready));
        Rows.Add(CkGym_Control::Numbered(5, "Divergence: first branch", false, Ready));
        Rows.Add(CkGym_Control::Numbered(6, "Divergence: timed", false, Ready));
        Rows.Add(CkGym_Control::Numbered(7, "Racing: event-driven", false, Ready));
        Rows.Add(CkGym_Control::Numbered(8, "Event-driven multi-cond", false, Ready));
        return Rows;
    }

    UFUNCTION() void Request_ControlActivated(int32 RowIndex) override
    {
        if (RowIndex >= 1 && RowIndex <= 9)
        { Request_SharedControl(RowIndex - 1); }
    }
}
