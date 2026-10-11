// Language=angelscript

class ACk_ChainGym_PlayerController : ACk_Gym_Base_PlayerController
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        if (!IsLocalController()) { return Stations; }
        for (auto Index = 0; Index < 4; Index++)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(FName(Get_StationTag(CkChainGym::Get_Tag(Index))));
            Station.Transform = CkChainGym::Get_StationTransform(Index);
            Station.AutoSize = true;
            if (Index == 0)
            {
                Station.Title = FText::FromString("TRAIN - PATH HISTORY");
                Station.Description.Add(FText::FromString("Eight cube links at 60 cm follow a figure-eight spline tween."));
                Station.Description.Add(FText::FromString("F5/F6/F7: sample spacing 4/10/25 cm. T: pause and teleport. K: split at wagon four. R: reseed."));
            }
            else if (Index == 1)
            {
                Station.Title = FText::FromString("ROPE - DISTANCE CONSTRAINT");
                Station.Description.Add(FText::FromString("Twelve links at 60 cm keep their segment lengths behind a head on the Train's figure-eight; corners are cut, not traced."));
                Station.Description.Add(FText::FromString("P: the head chases the pawn instead. Walk the pawn along the floor and drag the rope."));
            }
            else if (Index == 2)
            {
                Station.Title = FText::FromString("HELD - WAIT FOR COVERAGE");
                Station.Description.Add(FText::FromString("Authored link poses remain still until the head records enough path. G starts the idle head."));
            }
            else
            {
                Station.Title = FText::FromString("WAGONS WITH CHILDREN");
                Station.Description.Add(FText::FromString("Yellow lamp markers are real SceneNode children 65 cm above each moving wagon. Watch for a frame of lag."));
            }
            Stations.Add(Station);
        }
        return Stations;
    }

    UPROPERTY(Replicated) FCkChainGym_ControlAck LastControlAck;

    private bool _Admitted = false;
    private bool _Ending = false;
    private bool _LocalFailed = false;
    private FString _LocalFailureReason;
    private bool _StateCaptured = false;
    private bool _CatalogCaptured = false;
    private bool _PawnCaptured = false;
    private bool _StationsBound = false;
    private bool _ServerPending = false;
    private bool _DrawHistory = false;
    private bool _DrawTargets = false;
    private double _BootstrapStartedSeconds = 0.0;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;
    private UWorld _BoundWorld;
    private ACk_ChainGym_GameState _BoundState;
    private APawn _BoundPawn;
    private TArray<FCk_Handle> _Stations;
    private TArray<FCk_Handle> _StationCohorts;
    private FCkChainGym_WireSnapshot _PresentedFrame;
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

    private ACk_ChainGym_GameState Get_ExactState()
    {
        auto World = GetWorld();
        if (!_Admitted || _Ending || IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(World) || World != _BoundWorld) { return nullptr; }
        auto State = Cast<ACk_ChainGym_GameState>(World.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            State.GetClass() != ACk_ChainGym_GameState ||
            State.IsActorBeingDestroyed() ||
            (_StateCaptured && State != _BoundState)) { return nullptr; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_ChainGym_GameMode) { return nullptr; }
        }
        if (!State.Get_HasPublishedCatalog()) { return nullptr; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return nullptr; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "Chain" ||
            Entry.GameModeClass != ACk_ChainGym_GameMode ||
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
            ? Cast<ACk_ChainGym_GameState>(World.GetGameState()) : nullptr;
        if (ck::IsValid(Observed) && Observed.GetWorld() == World &&
            Observed.GetClass() == ACk_ChainGym_GameState &&
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
                (_Stations.Num() != 4 || _StationCohorts.Num() != 4)))
        { return false; }
        TArray<FCk_Handle> Found;
        TArray<FCk_Handle> Cohorts;
        for (int32 I = 0; I < 4; ++I)
        {
            auto Tag = CkChainGym::Get_Tag(I);
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
                    CkChainGym::Get_StationTransform(I).GetLocation()
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
        ck::Trace(f"Chain local presentation closed: {Reason}");
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
            auto Current = Cast<ACk_ChainGym_GameState>(
                _BoundWorld.GetGameState());
            if (_CatalogCaptured ||
                (_StateCaptured && (ck::Is_NOT_Valid(_BoundState) ||
                    Current != _BoundState ||
                    _BoundState.IsActorBeingDestroyed())) ||
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("Chain GameState or catalog did not converge"); }
            return;
        }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State))
        { CloseLocal("Chain state changed"); return; }
        if (State.Get_HasSessionFailure())
        { CloseLocal("Shared Chain session failed"); return; }
        if (!CapturePawnIfReady())
        {
            if (_PawnCaptured)
            { CloseLocal("Chain pawn changed", false); }
            else if (UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("Chain pawn did not converge"); }
            return;
        }
        if (HasAuthority()) { State.Request_EnsureSharedDemo(); }
        if (!IsLocalController()) { return; }
        if (!CheckStations(!_StationsBound))
        { CloseLocal("Local Chain stations changed"); return; }
        FCkChainGym_WireSnapshot Frame;
        if (!State.TryGetPresentation(Frame)) { return; }
        if (!CkChainGym::IsValidFrame(Frame))
        { CloseLocal("Chain presentation is malformed"); return; }
        if (Frame.Phase == 4)
        { CloseLocal("Chain presentation ended"); return; }
        if (_ProjectedSourceGeneration > Frame.SourceGeneration ||
            (_ProjectedSourceGeneration == Frame.SourceGeneration &&
                _ProjectedSequence > Frame.Sequence))
        { CloseLocal("Chain presentation went backward"); return; }
        _PresentedFrame = Frame; // local value copy, never source handles
        _ProjectedSourceGeneration = Frame.SourceGeneration;
        _ProjectedSequence = Frame.Sequence;
        DrawFrame(_PresentedFrame);
    }

    private void DrawHistory(TArray<FVector> Points)
    {
        auto Color = FLinearColor(0.0f, 0.8f, 0.8f, 1.0f);
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            utils_debug_draw::DrawDebugPoint(Points[I], 5.0f, Color, 0.0f);
            if (I > 0)
            {
                utils_debug_draw::DrawDebugLine(
                    Points[I - 1], Points[I], Color, 0.0f, 1.5f);
            }
        }
    }

    private void DrawFrame(FCkChainGym_WireSnapshot Frame)
    {
        if ((Frame.Phase != 2 && Frame.Phase != 3) ||
            !CkChainGym::IsValidFrame(Frame)) { return; }
        for (int32 I = 0; I < 4; ++I)
        {
            auto Station = Frame.Stations[I];
            auto Head = Station.Head;
            utils_debug_draw::DrawDebugSolidBox(
                Head.GetLocation(), FVector(25.0, 20.0, 20.0),
                FLinearColor(1.0f, 0.4f, 0.05f, 1.0f), Head.Rotator());
            auto Previous = Head.GetLocation();
            for (auto Link : Station.Links)
            {
                auto Pose = Link.Pose;
                auto Color = I == 2
                    ? FLinearColor(0.6f, 0.65f, 0.7f, 1.0f)
                    : FLinearColor(0.05f, 0.8f, 0.75f, 1.0f);
                utils_debug_draw::DrawDebugSolidBox(
                    Pose.GetLocation(), FVector(22.0, 18.0, 18.0),
                    Color, Pose.Rotator());
                if (I == 1)
                {
                    utils_debug_draw::DrawDebugLine(
                        Previous, Pose.GetLocation(), Color, 0.0f, 3.0f);
                }
                if (_DrawTargets)
                {
                    if (Link.IsHeld || !Link.HasTargetPose)
                    {
                        utils_debug_draw::DrawDebugPoint(
                            Pose.GetLocation(), 12.0f,
                            FLinearColor::Gray, 0.0f);
                    }
                    else
                    {
                        auto Color = (Pose.GetLocation() -
                            Link.TargetLocation).Size() < 1.0
                            ? FLinearColor::Green
                            : FLinearColor(1.0f, 0.65f, 0.0f, 1.0f);
                        utils_debug_draw::DrawDebugArrow(
                            Pose.GetLocation(), Link.TargetLocation,
                            15.0f, Color, 0.0f, 2.0f);
                    }
                }
                Previous = Pose.GetLocation();
            }
            for (auto Lamp : Station.Lamps)
            {
                utils_debug_draw::DrawDebugSphere(
                    Lamp.GetLocation(), 10.0f, 12,
                    FLinearColor::Yellow, 0.0f, 3.0f);
            }
            if (_DrawHistory)
            {
                DrawHistory(Station.History);
                DrawHistory(Station.SplitHistory);
            }
        }
    }

    private bool TryReadyFrame(FCkChainGym_WireSnapshot&out Frame)
    {
        Frame = FCkChainGym_WireSnapshot();
        if (!_Admitted || _Ending || _LocalFailed || !IsLocalController() ||
            !CheckStations(false) || !HasExactPawn() ||
            _RequestedSerial != LastControlAck.Serial) { return false; }
        auto State = Get_ExactState();
        FCkChainGym_WireSnapshot Current;
        if (ck::Is_NOT_Valid(State) || State.Get_HasSessionFailure() ||
            !State.TryGetPresentation(Current) ||
            !CkChainGym::IsValidFrame(Current) || Current.Phase != 2 ||
            !CkChainGym::IsValidFrame(_PresentedFrame) ||
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
        FCkChainGym_WireSnapshot Frame;
        return TryReadyFrame(Frame);
    }

    UFUNCTION(BlueprintPure) FString Get_DiagnosticState() const
    {
        return f"admitted={_Admitted} localFailed={_LocalFailed} " +
            f"pawnCaptured={_PawnCaptured} stateCaptured={_StateCaptured} " +
            f"stationsBound={_StationsBound} projectedGen={_ProjectedSourceGeneration} " +
            f"projectedSeq={_ProjectedSequence} reason={_LocalFailureReason}";
    }

    UFUNCTION(BlueprintPure) bool Get_DrawHistory() const
    { return _DrawHistory; }

    UFUNCTION(BlueprintPure) bool Get_DrawTargets() const
    { return _DrawTargets; }

    UFUNCTION(Server) void Server_RequestChainControl(
        int32 Serial, int64 SourceGeneration, int64 Generation, int32 Action)
    {
        if (!HasAuthority() || !_Admitted || _Ending || _ServerPending ||
            Serial <= 0 || LastControlAck.Serial >= 2147483647 ||
            Serial != LastControlAck.Serial + 1 ||
            Action < 0 || Action > 8)
        { return; }
        FCkChainGym_ControlAck Rejected;
        Rejected.Serial = Serial;
        Rejected.Action = Action;
        Rejected.RequestedGeneration = Generation;
        Rejected.Result = 2;
        auto State = Get_ExactState();
        FCkChainGym_WireSnapshot Frame;
        if (ck::Is_NOT_Valid(State) || !HasExactPawn() ||
            !State.TryGetPresentation(Frame) ||
            !CkChainGym::IsValidFrame(Frame) ||
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

    void Complete_ChainControl(int32 Serial, int64 RequestedGeneration,
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
            !CkChainGym::IsValidFrame(State.Snapshot)))
        {
            State.Request_Fail("Completed Chain control lost its owner or frame");
            return;
        }
        _ServerPending = false;
        _PendingSerial = 0;
        _PendingAction = -1;
        FCkChainGym_ControlAck Completed;
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
        FCkChainGym_WireSnapshot Frame;
        if (Action < 0 || Action > 8 || !TryReadyFrame(Frame) ||
            LastControlAck.Serial >= 2147483647) { return; }
        _RequestedSerial = LastControlAck.Serial + 1;
        Server_RequestChainControl(_RequestedSerial,
            Frame.SourceGeneration, Frame.ControlGeneration, Action);
    }

    private void Request_Frame(int32 Index)
    {
        FCkChainGym_WireSnapshot Frame;
        if (Index < 0 || Index >= 4 || !TryReadyFrame(Frame) ||
            !HasExactPawn()) { return; }
        auto Target = Frame.Stations[Index].Origin;
        auto Eye = Target + FVector(1500.0, -1700.0, 1400.0);
        _BoundPawn.SetActorLocation(Eye);
        if (!HasExactPawn()) { CloseLocal("Chain pawn changed during placement", false); return; }
        SetControlRotation((Target - Eye).Rotation());
        if (!HasExactPawn()) { CloseLocal("Chain pawn changed during view placement", false); }
    }

    FString Get_ControlPanelTitle() override { return "CHAIN"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        FCkChainGym_WireSnapshot Frame;
        const bool Ready = TryReadyFrame(Frame);
        Rows.Add(CkGym_Control::Header("CHAIN"));
        Rows.Add(CkGym_Control::Status("State",
            _LocalFailed ? "Closed" :
                (Ready ? "Use controls to compare motion" :
                    "Waiting for shared source")));
        Rows.Add(CkGym_Control::Status("Train sample spacing",
            Ready ? f"{Frame.Stations[0].Spacing} cm" : "Waiting"));
        for (int32 I = 0; I < 4; ++I)
        {
            auto Text = Ready
                ? f"{Frame.Stations[I].LinkCount} links; {Frame.Stations[I].History.Num()} history samples"
                : "Waiting";
            Rows.Add(CkGym_Control::Status(CkChainGym::Get_Tag(I), Text));
        }
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "View Train", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "View Rope", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "View Held", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::Four, "4", "View Wagons with children", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::F5, "F5", "Train spacing 4 cm", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::F6, "F6", "Train spacing 10 cm", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::F7, "F7", "Train spacing 25 cm", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Pause and teleport Train head", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::K, "K", "Split at Train wagon four", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reseed Train history", Ready));
        Rows.Add(CkGym_Control::Action(EKeys::G, "G", "Go: start Held head", Ready));
        Rows.Add(CkGym_Control::Toggle(EKeys::P, "P", "Rope head chases the pawn",
            Ready && Frame.Stations[1].FollowingPawn, false, Ready));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Reset all stations", Ready));
        Rows.Add(CkGym_Control::Toggle(EKeys::B, "B", "History polyline",
            _DrawHistory));
        Rows.Add(CkGym_Control::Toggle(EKeys::J, "J", "Link targets",
            _DrawTargets));
        return Rows;
    }

    UFUNCTION() void Request_ControlActivated(int32 RowIndex) override
    {
        if (RowIndex >= 7 && RowIndex <= 10)
        { Request_Frame(RowIndex - 7); }
        else if (RowIndex >= 11 && RowIndex <= 19)
        { Request_SharedControl(RowIndex - 11); }
        else if (_Admitted && IsLocalController() && !_Ending &&
            !_LocalFailed && GetWorld() == _BoundWorld && HasExactPawn() &&
            RowIndex == 20) { _DrawHistory = !_DrawHistory; }
        else if (_Admitted && IsLocalController() && !_Ending &&
            !_LocalFailed && GetWorld() == _BoundWorld && HasExactPawn() &&
            RowIndex == 21) { _DrawTargets = !_DrawTargets; }
    }

    UFUNCTION(Exec) void Ck_Chain_Control(int32 RowIndex)
    { Request_ControlActivated(RowIndex); }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason InReason)
    {
        _Ending = true;
        Super::EndPlay(InReason);
    }
}
