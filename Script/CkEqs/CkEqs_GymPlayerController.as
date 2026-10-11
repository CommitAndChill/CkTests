// Language=angelscript

class ACk_EqsGym_PlayerController : ACk_Gym_Base_PlayerController
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        if (!IsLocalController()) { return Stations; }

        Stations.Add(MakeStationPayload(n"Gym.Eqs.SimpleGrid", "SimpleGrid + Distance",
            "5x5 SimpleGrid (~25 candidates).\nDistance test scores closer-to-querier higher.\nSingleBest run mode."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.Donut", "Donut + Dot",
            "3 rings x 8 points = 24 candidates.\nDot test favours forward-facing candidates.\nSingleBest run mode."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.Cone", "Cone Generator",
            "Forward-cone candidate spread (no tests).\nAllMatchingSorted run mode.\nDemonstrates pure generator output."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.Immediate", "Immediate Sync",
            "Same SimpleGrid + Distance config\nbut via Request_RunQuery_Immediate.\nSync result, no broadcast."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.RandomBest", "RandomBest5Pct",
            "SimpleGrid + Distance test.\nRandomBest5Pct picks one of the\ntop 5% candidates each restart."));

        // v1.1 stations.
        Stations.Add(MakeStationPayload(n"Gym.Eqs.NavProjection", "NavProjection",
            "5x5 SimpleGrid with _ProjectOntoNav=true.\nCandidates snap to floor's navmesh\nvia UNavigationSystemV1::ProjectPointToNavigation."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.OnCircle", "OnCircle Generator",
            "8 evenly-spaced points on a 300uu ring.\nDistance test scores candidates.\nAllMatchingSorted run mode."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.VolumeCheck", "VolumeCheck Filter",
            "5x5 SimpleGrid filtered by an AABB at the\nstation origin (HalfExtent 150,150,50).\nInner 3x3 = 9 candidates survive."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.Random", "Random Tiebreaker",
            "Distance + Random tests on a SimpleGrid.\nRandom (Weight=0.1) tiebreaks equal-score\ncandidates each restart."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.Trace", "Trace (LOS)",
            "5x5 SimpleGrid + LineTrace LOS test.\nA wall blocks part of the grid; only\ncandidates with clear LOS survive."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.EntitiesWithTag", "EntitiesWithTag",
            "Generator returns 5 spawned target entities\non a 250uu ring (tagged Gym.Eqs.Target).\nDistance scoring picks the closest."));

        Stations.Add(MakeStationPayload(n"Gym.Eqs.Overlap", "Overlap Test",
            "5x5 SimpleGrid + Overlap test.\nThree probe markers near the grid score\ncandidates by neighbour count."));

        for (int32 I = 0; I < 12; ++I)
        {
            Stations[I].Transform = CkEqsGym::Get_StationTransform(I);
        }
        return Stations;
    }

    private FCkGym_Station_SpawnParams_Payload MakeStationPayload(FName InTag, FString InTitle, FString InDesc)
    {
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(FName(Get_StationTag(InTag.ToString())));
        Station.Title = FText::FromString(InTitle);
        auto Desc = TArray<FText>();
        Desc.Add(FText::FromString(InDesc));
        Station.Description = Desc;
        return Station;
    }

    UPROPERTY(Replicated) FCkEqsGym_ControlAck LastControlAck;

    private bool _Admitted = false;
    private bool _Ending = false;
    private bool _LocalFailed = false;
    private FString _LocalFailureReason;
    private bool _StateCaptured = false;
    private bool _CatalogCaptured = false;
    private bool _PawnCaptured = false;
    private bool _StationsBound = false;
    private bool _ServerPending = false;
    private double _BootstrapStartedSeconds = 0.0;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;
    private UWorld _BoundWorld;
    private ACk_EqsGym_GameState _BoundState;
    private APawn _BoundPawn;
    private TArray<FCk_Handle> _Stations;
    private TArray<FCk_Handle> _StationCohorts;
    private FCkEqsGym_WireSnapshot _PresentedFrame;
    private bool _FixturesSeen = false;
    private int64 _ProjectedSourceGeneration = 0;
    private int64 _ProjectedSequence = 0;
    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _PendingAction = -1;
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

    private ACk_EqsGym_GameState Get_ExactState()
    {
        auto World = GetWorld();
        if (!_Admitted || _Ending || IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(World) || World != _BoundWorld) { return nullptr; }
        auto State = Cast<ACk_EqsGym_GameState>(World.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            State.GetClass() != ACk_EqsGym_GameState ||
            State.IsActorBeingDestroyed() ||
            (_StateCaptured && State != _BoundState)) { return nullptr; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_EqsGym_GameMode) { return nullptr; }
        }
        if (!State.Get_HasPublishedCatalog()) { return nullptr; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return nullptr; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "EQS" ||
            Entry.GameModeClass != ACk_EqsGym_GameMode ||
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
            ? Cast<ACk_EqsGym_GameState>(World.GetGameState()) : nullptr;
        if (ck::IsValid(Observed) && Observed.GetWorld() == World &&
            Observed.GetClass() == ACk_EqsGym_GameState &&
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
        if (ck::Is_NOT_Valid(ControllerEntity) || ck::Is_NOT_Valid(WorldRoot) ||
            utils_entity_lifetime::Get_WorldForEntity(WorldRoot) != _BoundWorld ||
            (_StationsBound &&
                (_Stations.Num() != 12 || _StationCohorts.Num() != 12)))
        { return false; }
        TArray<FCk_Handle> Found;
        TArray<FCk_Handle> Cohorts;
        for (int32 I = 0; I < 12; ++I)
        {
            auto Tag = CkEqsGym::Get_BaseTag(I);
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
                    CkEqsGym::Get_StationTransform(I).GetLocation()
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

    private void CloseLocal(FString Reason, bool FailShared = true)
    {
        if (_LocalFailed) { return; }
        _LocalFailed = true;
        _LocalFailureReason = Reason;
        if (FailShared && _StateCaptured && ck::IsValid(_BoundState) &&
            _BoundState.GetWorld() == _BoundWorld)
        {
            if (HasAuthority()) { _BoundState.Request_Fail(Reason); }
            else { _BoundState.Request_CloseClient(Reason); }
        }
        ck::Trace(f"Eqs local presentation closed: {Reason}");
    }

    void Request_StartGym() override
    {
        if (_Ending || _Admitted) { return; }
        _BoundWorld = GetWorld();
        if (ck::Is_NOT_Valid(_BoundWorld))
        { _LocalFailed = true; _LocalFailureReason = "Controller world is unavailable"; return; }
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
            auto Current = Cast<ACk_EqsGym_GameState>(
                _BoundWorld.GetGameState());
            if (_CatalogCaptured ||
                (_StateCaptured && (ck::Is_NOT_Valid(_BoundState) ||
                    Current != _BoundState ||
                    _BoundState.IsActorBeingDestroyed())) ||
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("Eqs GameState or catalog did not converge"); }
            return;
        }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State))
        { CloseLocal("Eqs state changed"); return; }
        if (State.Get_HasSessionFailure())
        { CloseLocal("Shared Eqs session failed"); return; }
        if (!CapturePawnIfReady())
        {
            if (_PawnCaptured)
            { CloseLocal("Eqs pawn changed", false); }
            else if (UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("Eqs pawn did not converge"); }
            return;
        }
        if (HasAuthority()) { State.Request_EnsureSharedDemo(); }
        if (!IsLocalController()) { return; }
        if (!CheckStations(!_StationsBound))
        { CloseLocal("Local Eqs stations changed"); return; }
        FCkEqsGym_WireSnapshot Frame;
        if (!State.TryGetPresentation(Frame)) { return; }
        if (!CkEqsGym::IsValidFrame(Frame))
        { CloseLocal("Eqs presentation is malformed"); return; }
        if (Frame.Phase == 4)
        { CloseLocal("Eqs presentation ended"); return; }
        if (_ProjectedSourceGeneration > Frame.SourceGeneration ||
            (_ProjectedSourceGeneration == Frame.SourceGeneration &&
                _ProjectedSequence > Frame.Sequence))
        { CloseLocal("Eqs presentation went backward"); return; }
        if (!State.Get_AreFixturesReady())
        {
            if (_FixturesSeen) { CloseLocal("EQS fixtures changed"); }
            return;
        }
        _FixturesSeen = true;
        if (!ProjectFrame(Frame))
        { CloseLocal("Eqs presentation failed"); return; }
        _PresentedFrame = Frame; // local value copy, never source handles
        _ProjectedSourceGeneration = Frame.SourceGeneration;
        _ProjectedSequence = Frame.Sequence;
    }

    private bool TryReadyFrame(FCkEqsGym_WireSnapshot&out Frame)
    {
        Frame = FCkEqsGym_WireSnapshot();
        if (!_Admitted || _Ending || _LocalFailed || !IsLocalController() ||
            !CheckStations(false) || !HasExactPawn() ||
            _RequestedSerial != LastControlAck.Serial) { return false; }
        auto State = Get_ExactState();
        FCkEqsGym_WireSnapshot Current;
        if (ck::Is_NOT_Valid(State) || State.Get_HasSessionFailure() ||
            !_FixturesSeen || !State.Get_AreFixturesReady() ||
            !State.TryGetPresentation(Current) ||
            !CkEqsGym::IsValidFrame(Current) || Current.Phase != 2 ||
            !CkEqsGym::IsValidFrame(_PresentedFrame) ||
            _PresentedFrame.Phase != 2 ||
            _ProjectedSourceGeneration != _PresentedFrame.SourceGeneration ||
            _ProjectedSequence != _PresentedFrame.Sequence ||
            _ProjectedSequence <= 0 ||
            _PresentedFrame.SourceGeneration != Current.SourceGeneration ||
            _PresentedFrame.ControlGeneration != Current.ControlGeneration ||
            _PresentedFrame.Sequence > Current.Sequence) { return false; }
        Frame = _PresentedFrame;
        if (LastControlAck.Result != 1) { return true; }
        return LastControlAck.RequestedGeneration > 0 &&
            LastControlAck.CompletedGeneration >
                LastControlAck.RequestedGeneration &&
            LastControlAck.CompletedSequence > 0 &&
            Frame.ControlGeneration >= LastControlAck.CompletedGeneration &&
            Frame.Sequence >= LastControlAck.CompletedSequence;
    }

    UFUNCTION(BlueprintPure) bool Get_IsLocalPresentationReady()
    {
        FCkEqsGym_WireSnapshot Frame;
        return TryReadyFrame(Frame);
    }

    UFUNCTION(BlueprintPure) FString Get_DiagnosticState() const
    {
        return f"admitted={_Admitted} localFailed={_LocalFailed} fixturesSeen={_FixturesSeen} " +
            f"pawnCaptured={_PawnCaptured} stateCaptured={_StateCaptured} " +
            f"stationsBound={_StationsBound} projectedGen={_ProjectedSourceGeneration} " +
            f"projectedSeq={_ProjectedSequence} reason={_LocalFailureReason}";
    }

    UFUNCTION(Server) void Server_RequestEqsControl(
        int32 Serial, int64 SourceGeneration, int64 Generation, int32 Action)
    {
        if (!HasAuthority() || !_Admitted || _Ending || _ServerPending ||
            Serial <= 0 || LastControlAck.Serial >= 2147483647 ||
            Serial != LastControlAck.Serial + 1 ||
            Action != 0)
        { return; }
        FCkEqsGym_ControlAck Rejected;
        Rejected.Serial = Serial;
        Rejected.Action = Action;
        Rejected.RequestedGeneration = Generation;
        Rejected.Result = 2;
        auto State = Get_ExactState();
        FCkEqsGym_WireSnapshot Frame;
        if (ck::Is_NOT_Valid(State) || !HasExactPawn() ||
            !State.TryGetPresentation(Frame) ||
            !CkEqsGym::IsValidFrame(Frame) ||
            Frame.Phase != 2 ||
            SourceGeneration <= 0 || Generation <= 0 ||
            SourceGeneration != Frame.SourceGeneration ||
            Generation != Frame.ControlGeneration)
        { LastControlAck = Rejected; ForceNetUpdate(); return; }
        _ServerPending = true;
        _PendingSerial = Serial;
        _PendingAction = Action;
        _PendingSourceGeneration = SourceGeneration;
        _PendingGeneration = Generation;
        if (!State.TryStartControl(this, Serial, SourceGeneration,
                Generation, Action))
        {
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

    void Complete_EqsControl(int32 Serial, int64 RequestedGeneration,
        int32 Action, bool Success, int64 CompletedGeneration,
        int64 CompletedSequence)
    {
        auto State = Get_ExactState();
        if (!HasAuthority() || !_Admitted || _Ending || !_ServerPending ||
            Serial != _PendingSerial || Action != _PendingAction ||
            RequestedGeneration != _PendingGeneration ||
            LastControlAck.Serial + 1 != Serial ||
            ck::Is_NOT_Valid(State)) { return; }
        if (Success && (!HasExactPawn() || State.Get_HasSessionFailure() ||
            CompletedGeneration <= RequestedGeneration ||
            CompletedSequence <= 0 ||
            State.Snapshot.Phase != 2 ||
            State.Snapshot.SourceGeneration != _PendingSourceGeneration ||
            State.Snapshot.ControlGeneration != CompletedGeneration ||
            State.Snapshot.Sequence != CompletedSequence ||
            !CkEqsGym::IsValidFrame(State.Snapshot)))
        {
            State.Request_Fail("Completed Eqs control lost its owner or frame");
            return;
        }
        _ServerPending = false;
        _PendingSerial = 0;
        _PendingAction = -1;
        FCkEqsGym_ControlAck Completed;
        Completed.Serial = Serial;
        Completed.Action = Action;
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

    private void Request_SharedControl(int32 Action)
    {
        FCkEqsGym_WireSnapshot Frame;
        if (Action != 0 || !TryReadyFrame(Frame) ||
            LastControlAck.Serial >= 2147483647) { return; }
        _RequestedSerial = LastControlAck.Serial + 1;
        Server_RequestEqsControl(_RequestedSerial,
            Frame.SourceGeneration, Frame.ControlGeneration, Action);
    }

    UFUNCTION(BlueprintPure) FCkEqsGym_WireSnapshot Get_PresentedFrame() const
    { return _PresentedFrame; }

    private bool ProjectFrame(FCkEqsGym_WireSnapshot Frame)
    {
        if (!CkEqsGym::IsValidFrame(Frame) || Frame.Phase != 2 ||
            !CheckStations(false)) { return false; }
        // Validate all targets before publishing a single copied frame.
        for (int32 I = 0; I < 12; ++I)
        {
            if (ck::Is_NOT_Valid(_Stations[I]) ||
                (_Stations[I].Get_Fragment(FCkGym_Station_Anchors).FootprintCenter -
                    CkEqsGym::Get_StationTransform(I).GetLocation()).SizeSquared() > 1.0)
            { return false; }
        }
        for (int32 I = 0; I < 12; ++I)
        {
            auto& Text = _Stations[I].AddOrGet_Fragment(
                FCkGym_Station_TitleAndDescription);
            Text.Title = FText::FromString(Frame.Stations[I].Title);
            Text.Description = FText::FromString(Frame.Stations[I].Detail);
            Text.Instructions = FText::FromString(Frame.Stations[I].Footer);
        }
        return true;
    }

    FString Get_ControlPanelTitle() override { return "ENTITY QUERY"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Re-run every query"));
        return Rows;
    }

    UFUNCTION() void Request_ControlActivated(int32 RowIndex) override
    { if (RowIndex == 0) { Request_SharedControl(0); } }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason InReason)
    {
        _Ending = true;
        Super::EndPlay(InReason);
    }
}
