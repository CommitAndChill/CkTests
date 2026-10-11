// Language=angelscript

USTRUCT()
struct FCkSwayGym_ControlAck
{
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 Generation = 0;
    // 1 admitted, 2 requester context lost, 3 rejected; not completion.
    UPROPERTY() int32 Result = 0;
}

// Each local PC owns its three Common station shells and camera/HUD only.
// The replicated production entity trees belong to GS.
class ACk_SwayGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) FCkSwayGym_ControlAck LastControlAck;

    private bool _ControllerAdmitted = false;
    private bool _Ending = false;
    private bool _OwnerSessionBound = false;
    private bool _OwnerSessionLost = false;
    private bool _LocalBound = false;
    private bool _LocalTickPrerequisiteBound = false;
    private bool _LocalLost = false;
    private bool _StationsReadyOnce = false;
    private bool _DrawLocalOffsets = false;
    private int32 _RequestedSerial = 0;
    private int32 _ReservedSerial = 0;
    private int32 _ReservedGeneration = 0;
    private APawn _ReservedPawn;
    private ACk_SwayGym_GameState _ReservedState;
    private APawn _SessionPawn;
    private ACk_SwayGym_GameState _SessionState;
    private APawn _LocalPawn;
    private ACk_SwayGym_GameState _LocalState;
    private FCkSwayGym_Snapshot _Installed;
    private TArray<FCk_Handle> _FrozenStations;
    private FCk_Handle _StationCohortOwner;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        if (!IsLocalController() || ck::Is_NOT_Valid(ck::ToEntity(this)))
        { return Stations; }
        for (int32 I = 0; I < CkSwayGymCatalog::StationCount; ++I)
        {
            FCkGym_Station_SpawnParams_Payload Recipe;
            if (!CkSwayGymCatalog::TryGet_Station(I, Recipe))
            { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
            Recipe.Tags.Reset();
            Recipe.Tags.Add(FName(Get_StationTag(CkSwayGymCatalog::Get_Tag(I))));
            Stations.Add(Recipe);
        }
        return Stations;
    }

    FCk_Handle Get_StationLifetimeOwner() override
    {
        if (!IsLocalController()) { return FCk_Handle(); }
        return ck::ToEntity(this);
    }

    FString Get_StationTag(FString Tag) override
    { return f"{Tag}_{GetName()}"; }

    private bool Get_HasState(ACk_SwayGym_GameState State) const
    {
        auto CurrentWorld = GetWorld();
        return !_Ending && !IsActorBeingDestroyed() && ck::IsValid(CurrentWorld) &&
            ck::IsValid(State) && State.GetWorld() == CurrentWorld &&
            State.GetClass() == ACk_SwayGym_GameState &&
            CurrentWorld.GetGameState() == State && State.Get_HasPublishedCatalog();
    }

    void Request_StartGym() override
    {
        // Base PC calls this after its actual controller ECS admission.
        _ControllerAdmitted = true;
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld)) { return; }
        auto State = Cast<ACk_SwayGym_GameState>(CurrentWorld.GetGameState());
        if (HasAuthority() && Get_HasState(State))
        { State.Request_EnsureSharedDemo(); }
    }

    FString Get_ControlPanelTitle() override { return "SWAY"; }

    private void MonitorOwnerContext()
    {
        if (!_ControllerAdmitted || !HasAuthority() || !_OwnerSessionBound ||
            _OwnerSessionLost) { return; }
        auto CurrentWorld = GetWorld();
        bool Exact = ck::IsValid(CurrentWorld) && Get_HasState(_SessionState) &&
            ck::IsValid(_SessionPawn);
        if (Exact)
        {
            Exact = _SessionPawn.GetWorld() == CurrentWorld &&
                _SessionPawn.GetController() == this &&
                GetControlledPawn() == _SessionPawn;
        }
        if (Exact) { return; }
        _OwnerSessionLost = true;
        if (_ReservedSerial > 0)
        {
            FCkSwayGym_ControlAck Ack;
            Ack.RequestSerial = _ReservedSerial;
            Ack.Generation = _ReservedGeneration;
            Ack.Result = 2;
            LastControlAck = Ack;
        }
        _ReservedSerial = 0; _ReservedGeneration = 0;
        _ReservedPawn = nullptr; _ReservedState = nullptr;
        if (!_Ending && ck::IsValid(this)) { ForceNetUpdate(); }
    }

    private bool Get_HasLocalIdentity()
    {
        if (!_ControllerAdmitted || !IsLocalController() || _Ending ||
            _LocalLost || !_LocalBound || _OwnerSessionLost) { return false; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld) || !Get_HasState(_LocalState) ||
            ck::Is_NOT_Valid(_LocalPawn) || _LocalPawn.GetWorld() != CurrentWorld ||
            _LocalPawn.GetController() != this ||
            GetControlledPawn() != _LocalPawn) { return false; }
        return true;
    }

    private bool Get_HasStations()
    {
        if (!Get_HasLocalIdentity()) { return false; }
        auto Parent = ck::ToEntity(this);
        auto WorldRoot = ck::TransientEntity();
        FCk_Handle CommonOwner;
        if (ck::Is_NOT_Valid(Parent) || ck::Is_NOT_Valid(WorldRoot))
        { return false; }
        for (int32 I = 0; I < CkSwayGymCatalog::StationCount; ++I)
        {
            auto Matches = utils_entity_tag::ForEach_Entity(WorldRoot,
                FName(Get_StationTag(CkSwayGymCatalog::Get_Tag(I))));
            if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]))
            { return false; }
            auto H = Matches[0];
            if (!H.Has_Fragment(FCkGym_Station_Anchors) ||
                !H.Has_Fragment(FCkGym_Station_TitleAndDescription) ||
                !utils_entity_lifetime::Has_LifetimeOwner(H))
            { return false; }
            auto CohortOwner = utils_entity_lifetime::Get_LifetimeOwner(H);
            if (ck::Is_NOT_Valid(CohortOwner) ||
                !utils_entity_lifetime::Has_LifetimeOwner(CohortOwner) ||
                utils_entity_lifetime::Get_LifetimeOwner(CohortOwner) != Parent ||
                (I > 0 && CohortOwner != CommonOwner) ||
                (_StationsReadyOnce && CohortOwner != _StationCohortOwner) ||
                (_StationsReadyOnce &&
                    (_FrozenStations.Num() != 3 || _FrozenStations[I] != H)))
            { return false; }
            if (I == 0) { CommonOwner = CohortOwner; }
        }
        return true;
    }

    UFUNCTION(BlueprintPure)
    bool Get_SwayPresentationReady()
    {
        if (!Get_HasStations() || !_StationsReadyOnce ||
            !CkSwayGymShared::Get_IsValidSnapshot(_Installed) ||
            _Installed.Lifecycle != ECkSwayGym_Lifecycle::Active)
        { return false; }
        FCkSwayGym_Snapshot Current;
        return _LocalState.TryGet_PresentationSample(Current) &&
            CkSwayGymShared::Get_IsValidSnapshot(Current) &&
            Current.Lifecycle == ECkSwayGym_Lifecycle::Active &&
            Current.Generation == _Installed.Generation &&
            Current.Sequence == _Installed.Sequence;
    }

    UFUNCTION(BlueprintPure)
    FCkSwayGym_Snapshot Get_InstalledSwaySample() const
    { return _Installed; }

    UFUNCTION(BlueprintPure)
    TArray<FCk_Handle> Get_CapturedSwayStations() const
    { return _FrozenStations; }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_CapturedSwayStationOwner() const
    { return _StationCohortOwner; }

    UFUNCTION(BlueprintPure)
    FCkSwayGym_ControlAck Get_SwayControlAck() const
    { return LastControlAck; }

    private bool Get_CanControl()
    {
        return Get_SwayPresentationReady() && _ReservedSerial == 0 &&
            LastControlAck.Result != 2;
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Header("SWAY"));
        if (Get_SwayPresentationReady())
        {
            for (int32 I = 0; I < 3; ++I)
            {
                auto Yaw = _Installed.SwayYawDegrees[I];
                auto X = _Installed.SwayXCm[I];
                auto Settled = _Installed.SwaySettled[I] ? "settled" : "moving";
                Rows.Add(CkGym_Control::Status(CkSwayGymCatalog::Get_Tag(I),
                    f"yaw {Yaw :.2} deg, x {X :.2} cm, {Settled}"));
            }
            if (_DrawLocalOffsets)
            {
                for (int32 I = 0; I < 5; ++I)
                {
                    FString SpecLabel = I == 0 ? "Turntable default" :
                        I == 1 ? "Shuttle default" :
                        I == 2 ? "Compare default" :
                        I == 3 ? "Compare heavy" : "Compare disabled";
                    auto Yaw = _Installed.SwayYawDegrees[I];
                    auto X = _Installed.SwayXCm[I];
                    auto Settled = _Installed.SwaySettled[I] ? "settled" : "moving";
                    Rows.Add(CkGym_Control::Status(SpecLabel,
                        f"yaw {Yaw :.2} deg, x {X :.2} cm, {Settled}", I == 4));
                }
            }
        }
        else
        {
            FString Failure = "";
            auto CurrentWorld = GetWorld();
            if (ck::IsValid(CurrentWorld))
            {
                auto State = Cast<ACk_SwayGym_GameState>(CurrentWorld.GetGameState());
                if (Get_HasState(State))
                { Failure = State.Get_CurrentFailureReason(); }
            }
            Rows.Add(CkGym_Control::Status("State",
                Failure.IsEmpty() ? "Waiting for shared Sway source" : Failure,
                !Failure.IsEmpty()));
        }
        Rows.Add(CkGym_Control::Action(EKeys::One, "1", "View Turntable", Get_CanControl()));
        Rows.Add(CkGym_Control::Action(EKeys::Two, "2", "View Shuttle", Get_CanControl()));
        Rows.Add(CkGym_Control::Action(EKeys::Three, "3", "View Compare", Get_CanControl()));
        Rows.Add(CkGym_Control::Action(EKeys::X, "X", "Reset shared Sway", Get_CanControl()));
        Rows.Add(CkGym_Control::Toggle(EKeys::O, "O", "Draw local sway offsets",
            _DrawLocalOffsets, false, Get_CanControl()));
        return Rows;
    }

    UFUNCTION(BlueprintPure)
    TArray<FCkGym_ControlRow> Get_SwayControlRows()
    { return Get_ControlRows(); }

    private void Request_Frame(int32 Index)
    {
        if (!Get_CanControl() || Index < 0 || Index >= 3) { return; }
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn)) { return; }
        auto Head = _LocalState.Get_ReplicatedHead(Index);
        if (ck::Is_NOT_Valid(Head) || !utils_transform::Has(Head)) { return; }
        const auto Target = utils_transform::Get_EntityCurrentTransform(Head).GetLocation();
        const auto Eye = Target + FVector(-700.0, -900.0, 450.0);
        Pawn.SetActorLocation(Eye);
        SetControlRotation((Target - Eye).Rotation());
    }

    void Request_ControlActivated(int32 RowIndex) override
    {
        auto Rows = Get_ControlRows();
        if (!Rows.IsValidIndex(RowIndex) || !Get_CanControl()) { return; }
        auto Key = Rows[RowIndex].Key;
        if (Key == EKeys::One) { Request_Frame(0); return; }
        if (Key == EKeys::Two) { Request_Frame(1); return; }
        if (Key == EKeys::Three) { Request_Frame(2); return; }
        if (Key == EKeys::O)
        { _DrawLocalOffsets = !_DrawLocalOffsets; return; }
        if (Key != EKeys::X) { return; }
        auto High = Math::Max(_RequestedSerial, LastControlAck.RequestSerial);
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        Server_RequestSwayRebuild(_RequestedSerial, _Installed.Generation);
    }

    UFUNCTION(BlueprintPure)
    bool Get_HasReservedSwayControl(int32 Serial, int32 Generation,
        APawn Pawn) const
    {
        return _ControllerAdmitted && HasAuthority() && !_Ending &&
            !_OwnerSessionLost && Serial > 0 && Generation > 0 &&
            _ReservedSerial == Serial && _ReservedGeneration == Generation &&
            _ReservedPawn == Pawn && _ReservedState == _SessionState &&
            _SessionPawn == Pawn && Get_HasState(_ReservedState) &&
            ck::IsValid(Pawn) && Pawn.GetWorld() == GetWorld() &&
            Pawn.GetController() == this && GetControlledPawn() == Pawn;
    }

    UFUNCTION(Server)
    void Server_RequestSwayRebuild(int32 Serial, int32 Generation)
    {
        MonitorOwnerContext();
        if (!_ControllerAdmitted || !HasAuthority() || _Ending ||
            _OwnerSessionLost || _ReservedSerial != 0 || Serial <= 0 ||
            Serial <= LastControlAck.RequestSerial) { return; }
        FCkSwayGym_ControlAck Ack;
        Ack.RequestSerial = Serial; Ack.Generation = Generation;
        LastControlAck = Ack;
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld))
        { Ack.Result = 3; LastControlAck = Ack; ForceNetUpdate(); return; }
        auto State = Cast<ACk_SwayGym_GameState>(CurrentWorld.GetGameState());
        auto Pawn = GetControlledPawn();
        if (!Get_HasState(State) || ck::Is_NOT_Valid(Pawn) ||
            Pawn.GetWorld() != CurrentWorld || Pawn.GetController() != this ||
            Generation <= 0 || State.Snapshot.Generation != Generation ||
            State.Snapshot.Lifecycle != ECkSwayGym_Lifecycle::Active ||
            (_OwnerSessionBound && (_SessionPawn != Pawn || _SessionState != State)))
        { Ack.Result = 3; LastControlAck = Ack; ForceNetUpdate(); return; }
        _OwnerSessionBound = true; _SessionPawn = Pawn; _SessionState = State;
        _ReservedSerial = Serial; _ReservedGeneration = Generation;
        _ReservedPawn = Pawn; _ReservedState = State;
        bool Accepted = State.Request_Rebuild(this, Pawn, Serial, Generation);
        MonitorOwnerContext();
        bool Exact = !_Ending && !_OwnerSessionLost &&
            Get_HasReservedSwayControl(Serial, Generation, Pawn);
        _ReservedSerial = 0; _ReservedGeneration = 0;
        _ReservedPawn = nullptr; _ReservedState = nullptr;
        Ack.Result = Exact ? (Accepted ? 1 : 3) : 2;
        if (!_Ending && ck::IsValid(this))
        { LastControlAck = Ack; ForceNetUpdate(); }
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        MonitorOwnerContext();
        if (!_ControllerAdmitted || _Ending || !IsLocalController()) { return; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld))
        { if (_LocalBound) { _LocalLost = true; } return; }
        auto State = Cast<ACk_SwayGym_GameState>(CurrentWorld.GetGameState());
        auto Pawn = GetControlledPawn();
        if (!_LocalBound)
        {
            if (!Get_HasState(State) || ck::Is_NOT_Valid(Pawn) ||
                Pawn.GetWorld() != CurrentWorld || Pawn.GetController() != this)
            { return; }
            _LocalBound = true; _LocalState = State; _LocalPawn = Pawn;
            AddTickPrerequisiteActor(_LocalState);
            _LocalTickPrerequisiteBound = true;
        }
        if (_LocalLost || !Get_HasLocalIdentity())
        { _LocalLost = true; _Installed = FCkSwayGym_Snapshot(); return; }
        if (!Get_HasStations())
        {
            if (_StationsReadyOnce)
            { _LocalLost = true; _Installed = FCkSwayGym_Snapshot(); }
            return;
        }
        if (!_StationsReadyOnce)
        {
            for (int32 I = 0; I < CkSwayGymCatalog::StationCount; ++I)
            {
                auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(),
                    FName(Get_StationTag(CkSwayGymCatalog::Get_Tag(I))));
                if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]))
                { _LocalLost = true; return; }
                _FrozenStations.Add(Matches[0]);
            }
            _StationCohortOwner =
                utils_entity_lifetime::Get_LifetimeOwner(_FrozenStations[0]);
            _StationsReadyOnce = true;
        }
        FCkSwayGym_Snapshot Sample;
        if (!State.TryGet_PresentationSample(Sample))
        {
            if (CkSwayGymShared::Get_IsValidSnapshot(State.Snapshot) &&
                State.Snapshot.Lifecycle != ECkSwayGym_Lifecycle::Active)
            { _Installed = FCkSwayGym_Snapshot(); }
            return; // Active telemetry may arrive before the replicated visual entities.
        }
        if (Sample.Sequence > _Installed.Sequence)
        { _Installed = Sample; }
        if (Get_SwayPresentationReady())
        {
            for (int32 I = 0; I < 3; ++I)
            {
                auto Head = _LocalState.Get_ReplicatedHead(I);
                if (ck::Is_NOT_Valid(Head) || !utils_transform::Has(Head)) { continue; }
                const auto Pose = utils_transform::Get_EntityCurrentTransform(Head);
                utils_debug_draw::DrawDebugSolidBox(
                    Pose.GetLocation(),
                    FVector(25.0, 20.0, 20.0),
                    FLinearColor(1.0f, 0.4f, 0.05f, 1.0f),
                    Pose.Rotator());
            }
        }
    }

    UFUNCTION(Exec)
    void Ck_Sway_Control(int32 Index)
    { Request_ControlActivated(Index); }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        if (_LocalTickPrerequisiteBound && ck::IsValid(_LocalState) &&
            !_LocalState.IsActorBeingDestroyed())
        {
            auto CurrentWorld = GetWorld();
            if (ck::IsValid(CurrentWorld) && _LocalState.GetWorld() == CurrentWorld)
            { RemoveTickPrerequisiteActor(_LocalState); }
        }
        _LocalTickPrerequisiteBound = false;
        _Installed = FCkSwayGym_Snapshot();
        Super::EndPlay(Reason);
    }
}
