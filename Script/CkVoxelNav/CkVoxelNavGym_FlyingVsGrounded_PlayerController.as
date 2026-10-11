class ACk_VoxelNavGym_FlyingVsGrounded_PlayerController : ACk_Gym_Base_PlayerController
{
    // Original world coordinates remain fixed; only the authority owns the floor, wall, volume and agents.
    private const FVector k_VolumeMin = FVector(-1600.0, -1600.0, -100.0);
    private const FVector k_VolumeMax = FVector(1600.0, 1600.0, 1600.0);
    private const FLinearColor k_FlyerColor = FLinearColor(0.15, 0.9, 1.0, 1.0);
    private const FLinearColor k_GroundedColor = FLinearColor(1.0, 0.55, 0.1, 1.0);
    private const FLinearColor k_VolumeColor = FLinearColor(0.35, 0.35, 0.45, 1.0);
    private const FVector k_PlayerViewLocation = FVector(2400.0, 0.0, 1000.0);
    private const FRotator k_PlayerViewRotation = FRotator(-15.0, 180.0, 0.0);

    UPROPERTY(Replicated) FCkVoxelNavGym_ControlAck LastControlAck;

    private bool _Admitted = false;
    private bool _Ending = false;
    private bool _LocalFailed = false;
    private FString _LocalFailureReason;
    private bool _StateCaptured = false;
    private bool _CatalogCaptured = false;
    private bool _PawnCaptured = false;
    private bool _StationBound = false;
    private bool _ServerPending = false;
    private bool _ViewpointPlaced = false;
    private double _BootstrapStartedSeconds = 0.0;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;
    private UWorld _BoundWorld;
    private ACk_VoxelNavGym_FlyingVsGrounded_GameState _BoundState;
    private APawn _BoundPawn;
    private FCk_Handle _Station;
    private FCk_Handle _StationCohort;
    private FCkVoxelNavGym_WireSnapshot _PresentedFrame;
    private TArray<FCk_Handle> _PresentedAgents;
    private TArray<FTransform> _PresentedTransforms;
    private int64 _ProjectedSourceGeneration = 0;
    private int64 _ProjectedSequence = 0;
    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _PendingAction = -1;
    private int64 _PendingSourceGeneration = 0;
    private int64 _PendingGeneration = 0;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();
        if (!IsLocalController()) { return Stations; }
        auto Station = FCkGym_Station_SpawnParams_Payload();
        Station.Tags.Add(FName(Get_StationTag(CkVoxelNavGym::Get_BaseTag(0))));
        Station.AutoSize = true;
        Station.Transform = CkVoxelNavGym::Get_StationTransform(0);
        Station.Title = FText::FromString(CkVoxelNavGym::Get_Title(0));
        auto Description = TArray<FText>();
        Description.Add(FText::FromString(CkVoxelNavGym::Get_Description(0)));
        Station.Description = Description;
        Stations.Add(Station);
        return Stations;
    }

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

    private ACk_VoxelNavGym_FlyingVsGrounded_GameState Get_ExactState()
    {
        auto CurrentWorld = GetWorld();
        if (!_Admitted || _Ending || IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(CurrentWorld) || CurrentWorld != _BoundWorld) { return nullptr; }
        auto State = Cast<ACk_VoxelNavGym_FlyingVsGrounded_GameState>(CurrentWorld.GetGameState());
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != CurrentWorld ||
            State.GetClass() != ACk_VoxelNavGym_FlyingVsGrounded_GameState ||
            State.IsActorBeingDestroyed() ||
            (_StateCaptured && State != _BoundState)) { return nullptr; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != CurrentWorld ||
                Mode.GetClass() != ACk_VoxelNavGym_FlyingVsGrounded_GameMode) { return nullptr; }
        }
        if (!State.Get_HasPublishedCatalog()) { return nullptr; }
        auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (Index < 0 || Index >= Entries.Num()) { return nullptr; }
        auto Entry = Entries[Index];
        if (Entry.DisplayName != "VoxelNav Flying Vs Grounded" ||
            Entry.GameModeClass != ACk_VoxelNavGym_FlyingVsGrounded_GameMode ||
            (_CatalogCaptured && (Index != _CatalogIndex ||
                Entry.DisplayName != _CatalogEntry.DisplayName ||
                Entry.GameModeClass != _CatalogEntry.GameModeClass ||
                Entry.LevelName != _CatalogEntry.LevelName ||
                Entry.Category != _CatalogEntry.Category ||
                Entry.HintCode != _CatalogEntry.HintCode ||
                Entry.HasMultiplayerComponent != _CatalogEntry.HasMultiplayerComponent)))
        { return nullptr; }
        return State;
    }

    private bool CaptureStateIfReady()
    {
        auto CurrentWorld = GetWorld();
        auto Observed = ck::IsValid(CurrentWorld)
            ? Cast<ACk_VoxelNavGym_FlyingVsGrounded_GameState>(CurrentWorld.GetGameState()) : nullptr;
        if (ck::IsValid(Observed) && Observed.GetWorld() == CurrentWorld &&
            Observed.GetClass() == ACk_VoxelNavGym_FlyingVsGrounded_GameState &&
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

    private bool CheckStation(bool Bind)
    {
        if (!IsLocalController() || !HasExactPawn()) { return false; }
        auto ControllerEntity = ck::ToEntity(this);
        auto WorldRoot = ck::TransientEntity();
        if (ck::Is_NOT_Valid(ControllerEntity) || ck::Is_NOT_Valid(WorldRoot) ||
            utils_entity_lifetime::Get_WorldForEntity(WorldRoot) != _BoundWorld)
        { return false; }
        auto Tag = CkVoxelNavGym::Get_BaseTag(0);
        if (Tag.IsEmpty()) { return false; }
        auto Matches = utils_entity_tag::ForEach_Entity(
            WorldRoot, FName(Get_StationTag(Tag)));
        if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors) ||
            !utils_entity_lifetime::Has_LifetimeOwner(Matches[0]))
        { return false; }
        auto Station = Matches[0];
        auto Cohort = utils_entity_lifetime::Get_LifetimeOwner(Station);
        if (!IsDirect(Station, Cohort) || !IsDirect(Cohort, ControllerEntity) ||
            (Station.Get_Fragment(FCkGym_Station_Anchors).FootprintCenter -
                CkVoxelNavGym::Get_StationTransform(0).GetLocation()
            ).SizeSquared() > 1.0 ||
            (_StationBound && (Station != _Station || Cohort != _StationCohort)))
        { return false; }
        auto Script = Cast<UCk_EntityScript_GymStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station));
        if (ck::Is_NOT_Valid(Script) ||
            UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Script)
                != Station)
        { return false; }
        if (Bind && !_StationBound)
        {
            _Station = Station;
            _StationCohort = Cohort;
            _StationBound = true;
        }
        return _StationBound;
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
        ck::Trace(f"VoxelNav local presentation closed: {Reason}");
    }

    void Request_StartGym() override
    {
        if (_Ending || _Admitted) { return; }
        _BoundWorld = GetWorld();
        if (ck::Is_NOT_Valid(_BoundWorld))
        { _LocalFailed = true; _LocalFailureReason = "Controller world is unavailable"; return; }
        _Admitted = true;
        _BootstrapStartedSeconds = UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds();
        CaptureStateIfReady();
    }

    private void PlaceLocalView()
    {
        if (_ViewpointPlaced || !IsLocalController() || !HasExactPawn()) { return; }
        _BoundPawn.SetActorLocation(k_PlayerViewLocation);
        SetControlRotation(k_PlayerViewRotation);
        _ViewpointPlaced = true;
    }

    UFUNCTION(BlueprintOverride) void Tick(float DeltaSeconds)
    {
        if (!_Admitted || _Ending || _LocalFailed) { return; }
        if (ck::Is_NOT_Valid(_BoundWorld) || GetWorld() != _BoundWorld ||
            IsActorBeingDestroyed())
        { CloseLocal("Controller world changed"); return; }
        if (!CaptureStateIfReady())
        {
            auto Current = Cast<ACk_VoxelNavGym_FlyingVsGrounded_GameState>(
                _BoundWorld.GetGameState());
            if (_CatalogCaptured ||
                (_StateCaptured && (ck::Is_NOT_Valid(_BoundState) ||
                    Current != _BoundState || _BoundState.IsActorBeingDestroyed())) ||
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("VoxelNav GameState or catalog did not converge"); }
            return;
        }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State))
        { CloseLocal("VoxelNav state changed"); return; }
        if (State.Get_HasSessionFailure())
        { CloseLocal("Shared VoxelNav session failed"); return; }
        if (!CapturePawnIfReady())
        {
            if (_PawnCaptured)
            { CloseLocal("VoxelNav pawn changed", false); }
            else if (UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                _BootstrapStartedSeconds >= 20.0)
            { CloseLocal("VoxelNav pawn did not converge"); }
            return;
        }
        if (HasAuthority()) { State.Request_EnsureSharedDemo(); }
        if (!IsLocalController()) { return; }
        PlaceLocalView();
        if (!CheckStation(!_StationBound))
        { CloseLocal("Local VoxelNav station changed"); return; }
        if (!State.Get_HasConsumableFrame())
        {
            if (_ProjectedSequence == 0 &&
                UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds() -
                    _BootstrapStartedSeconds >= 35.0)
            { CloseLocal("VoxelNav presentation did not converge"); }
            return;
        }
        auto Frame = State.Get_ConsumedFrame();
        if (!CkVoxelNavGym::IsValidFrame(Frame))
        { CloseLocal("VoxelNav presentation is malformed"); return; }
        if (Frame.Phase == 4)
        { CloseLocal("VoxelNav presentation ended"); return; }
        if (_ProjectedSourceGeneration > Frame.SourceGeneration ||
            (_ProjectedSourceGeneration == Frame.SourceGeneration &&
                _ProjectedSequence > Frame.Sequence))
        { CloseLocal("VoxelNav presentation went backward"); return; }
        _PresentedFrame = Frame;
        _ProjectedSourceGeneration = Frame.SourceGeneration;
        _ProjectedSequence = Frame.Sequence;
        DrawFrame(Frame);
    }

    private void DrawFrame(FCkVoxelNavGym_WireSnapshot Frame)
    {
        if (!CkVoxelNavGym::IsValidFrame(Frame) || !CheckStation(false))
        { return; }
        const float DrawSeconds = 0.12f;
        auto Center = (k_VolumeMin + k_VolumeMax) * 0.5;
        auto Extent = (k_VolumeMax - k_VolumeMin) * 0.5;
        utils_debug_draw::DrawDebugBox(Center, Extent, k_VolumeColor,
            FRotator::ZeroRotator, DrawSeconds, 1.0f);
        auto Station = Frame.Stations[0];
        if (!Station.Built)
        {
            utils_debug_draw::DrawDebugString(FVector(0.0, 0.0, 500.0),
                f"Baking voxel volume... {Station.BuildProgress}",
                FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), DrawSeconds);
        }
        _PresentedAgents.Reset();
        _PresentedTransforms.Reset();
        for (int32 I = 0; I < 2; ++I)
        {
            auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), I == 0
                ? n"VoxelNavFlyingVsGrounded.Agent.Flying"
                : n"VoxelNavFlyingVsGrounded.Agent.Grounded");
            if (Matches.Num() != 1 ||
                !UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(Matches[0], _BoundWorld) ||
                !utils_transform::Has(Matches[0]))
            { _PresentedAgents.Reset(); _PresentedTransforms.Reset(); return; }
            auto Handle = Matches[0];
            auto Script = Cast<UCk_EntityScript_VoxelNavGym_Agent>(
                utils_entity_script::TryGet_EntityScript(Handle));
            if (ck::Is_NOT_Valid(Script) || Script.Index != I ||
                Script.PairGeneration != Station.PairGeneration ||
                !utils_entity_replication_driver::Has(Handle) ||
                !utils_entity_replication_driver::Get_IsReplicationComplete(Handle))
            { _PresentedAgents.Reset(); _PresentedTransforms.Reset(); return; }
            auto Pose = utils_transform::Get_EntityCurrentTransform(Handle.As_Transform());
            _PresentedAgents.Add(Handle);
            _PresentedTransforms.Add(Pose);
        }
        for (int32 I = 0; I < 2; ++I)
        {
            auto Pose = _PresentedTransforms[I];
            auto Color = I == 0 ? k_FlyerColor : k_GroundedColor;
            auto Location = Pose.GetLocation();
            utils_debug_draw::DrawDebugCapsule(Location, 96.0f, 42.0f,
                Pose.GetRotation().Rotator(), Color, DrawSeconds, 2.0f);
            auto Forward = Pose.GetRotation().GetForwardVector();
            utils_debug_draw::DrawDebugArrow(Location,
                Location + Forward * 150.0f, 24.0f, Color, DrawSeconds, 2.0f);
            auto Label = I == 0 ? "FLYING (replicated Transform)" : "GROUNDED (replicated Transform)";
            utils_debug_draw::DrawDebugString(
                Location + FVector(0.0, 0.0, 120.0), Label, Color, DrawSeconds);
        }
    }

    private bool TryReadyFrame(FCkVoxelNavGym_WireSnapshot&out Frame)
    {
        Frame = FCkVoxelNavGym_WireSnapshot();
        if (!_Admitted || _Ending || _LocalFailed || !IsLocalController() ||
            !CheckStation(false) || !HasExactPawn() ||
            _RequestedSerial != LastControlAck.Serial ||
            _PresentedAgents.Num() != 2 || _PresentedTransforms.Num() != 2) { return false; }
        for (auto Handle : _PresentedAgents)
        {
            if (!UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(Handle, _BoundWorld))
            { return false; }
        }
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State) || State.Get_HasSessionFailure() ||
            !State.Get_HasConsumableFrame()) { return false; }
        auto Current = State.Get_ConsumedFrame();
        if (!CkVoxelNavGym::IsValidFrame(Current) || Current.Phase != 2 ||
            !CkVoxelNavGym::IsValidFrame(_PresentedFrame) ||
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
            LastControlAck.CompletedGeneration > LastControlAck.RequestedGeneration &&
            LastControlAck.CompletedSequence > 0 &&
            Frame.ControlGeneration >= LastControlAck.CompletedGeneration &&
            Frame.Sequence >= LastControlAck.CompletedSequence;
    }

    UFUNCTION(BlueprintPure) bool Get_IsLocalPresentationReady()
    {
        FCkVoxelNavGym_WireSnapshot Frame;
        return TryReadyFrame(Frame);
    }

    UFUNCTION(BlueprintPure) FCkVoxelNavGym_WireSnapshot Get_PresentedFrame() const
    { return _PresentedFrame; }

    UFUNCTION(BlueprintPure) FCk_Handle Get_PresentedAgent(int32 Index) const
    {
        return !_LocalFailed && !_Ending && _PresentedAgents.IsValidIndex(Index)
            ? _PresentedAgents[Index] : FCk_Handle();
    }

    UFUNCTION(BlueprintPure) FTransform Get_PresentedAgentTransform(int32 Index) const
    {
        return !_LocalFailed && !_Ending && _PresentedTransforms.IsValidIndex(Index)
            ? _PresentedTransforms[Index] : FTransform::Identity;
    }

    UFUNCTION(BlueprintPure) FString Get_DiagnosticState() const
    {
        return f"admitted={_Admitted} localFailed={_LocalFailed} " +
            f"pawnCaptured={_PawnCaptured} stateCaptured={_StateCaptured} " +
            f"stationBound={_StationBound} projectedGen={_ProjectedSourceGeneration} " +
            f"projectedSeq={_ProjectedSequence} reason={_LocalFailureReason}";
    }

    UFUNCTION(Server) void Server_RequestVoxelControl(
        int32 Serial, int64 SourceGeneration, int64 Generation, int32 Action)
    {
        if (!HasAuthority() || !_Admitted || _Ending || _ServerPending ||
            Serial <= 0 || LastControlAck.Serial >= 2147483647 ||
            Serial != LastControlAck.Serial + 1 || Action < 0 || Action > 1)
        { return; }
        FCkVoxelNavGym_ControlAck Rejected;
        Rejected.Serial = Serial;
        Rejected.Action = Action;
        Rejected.RequestedGeneration = Generation;
        Rejected.Result = 2;
        auto State = Get_ExactState();
        if (ck::Is_NOT_Valid(State) || !HasExactPawn() ||
            !State.Get_HasConsumableFrame())
        { LastControlAck = Rejected; ForceNetUpdate(); return; }
        auto Frame = State.Get_ConsumedFrame();
        if (!CkVoxelNavGym::IsValidFrame(Frame) || Frame.Phase != 2 ||
            SourceGeneration <= 0 || Generation <= 0 ||
            SourceGeneration != Frame.SourceGeneration ||
            Generation != Frame.ControlGeneration)
        { LastControlAck = Rejected; ForceNetUpdate(); return; }
        _ServerPending = true;
        _PendingSerial = Serial;
        _PendingAction = Action;
        _PendingSourceGeneration = SourceGeneration;
        _PendingGeneration = Generation;
        if (!State.Request_Control(this, Serial, SourceGeneration, Generation, Action))
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

    void Complete_VoxelControl(int32 Serial, int64 RequestedGeneration,
        int32 Action, bool Success, int64 CompletedGeneration,
        int64 CompletedSequence)
    {
        auto State = Get_ExactState();
        if (!HasAuthority() || !_Admitted || _Ending || !_ServerPending ||
            Serial != _PendingSerial || Action != _PendingAction ||
            RequestedGeneration != _PendingGeneration ||
            LastControlAck.Serial + 1 != Serial ||
            ck::Is_NOT_Valid(State)) { return; }
        if (Success)
        {
            if (!HasExactPawn() || State.Get_HasSessionFailure() ||
                !State.Get_HasConsumableFrame())
            { State.Request_Fail("Completed VoxelNav control lost its owner or frame"); return; }
            auto Frame = State.Get_ConsumedFrame();
            if (CompletedGeneration <= RequestedGeneration ||
                CompletedSequence <= 0 || Frame.Phase != 2 ||
                Frame.SourceGeneration != _PendingSourceGeneration ||
                Frame.ControlGeneration != CompletedGeneration ||
                Frame.Sequence != CompletedSequence ||
                !CkVoxelNavGym::IsValidFrame(Frame))
            { State.Request_Fail("Completed VoxelNav control lost its frame"); return; }
        }
        _ServerPending = false;
        _PendingSerial = 0;
        _PendingAction = -1;
        FCkVoxelNavGym_ControlAck Completed;
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
        FCkVoxelNavGym_WireSnapshot Frame;
        if (Action < 0 || Action > 1 || !TryReadyFrame(Frame) ||
            LastControlAck.Serial >= 2147483647) { return; }
        _RequestedSerial = LastControlAck.Serial + 1;
        Server_RequestVoxelControl(_RequestedSerial,
            Frame.SourceGeneration, Frame.ControlGeneration, Action);
    }

    FString Get_ControlPanelTitle() override
    { return "VOXELNAV: FLYING vs GROUNDED"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows = TArray<FCkGym_ControlRow>();
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Restart both agents"));
        Rows.Add(CkGym_Control::Action(EKeys::T, "T", "Send them back (return trip)"));
        return Rows;
    }

    UFUNCTION() void Request_ControlActivated(int32 RowIndex) override
    {
        if (RowIndex == 0) { Request_SharedControl(0); }
        else if (RowIndex == 1) { Request_SharedControl(1); }
    }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason InReason)
    {
        _Ending = true;
        Super::EndPlay(InReason);
    }
}
