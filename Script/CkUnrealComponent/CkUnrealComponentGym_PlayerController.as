// Language=angelscript

USTRUCT()
struct FCkUnrealComponentGym_ControlAck
{
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 Generation = 0;
    // 1 = admitted by GameState; 2 = requester lost; 3 = rejected. Not completion.
    UPROPERTY() int32 Result = 0;
}

// The shared source belongs to GameState. This controller owns only its local
// Common stations, control panel, and (on a client) one passive visual actor.
class ACk_UnrealComponentGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) FCkUnrealComponentGym_ControlAck LastControlAck;
    UPROPERTY() private ACk_UnrealComponentGym_Presenter_UE _Presenter;

    private bool _ControllerAdmitted = false;
    private bool _Ending = false;
    private bool _OwnerBound = false;
    private bool _OwnerLost = false;
    private bool _LocalBound = false;
    private bool _LocalLost = false;
    private bool _StationsFrozen = false;
    private bool _PresenterAttempted = false;
    private int32 _RequestedSerial = 0;
    private APawn _OwnerPawn;
    private ACk_UnrealComponentGym_GameState _OwnerState;
    private APawn _LocalPawn;
    private ACk_UnrealComponentGym_GameState _LocalState;
    private UWorld _LocalWorld;
    private FCk_Handle _LocalWorldRoot;
    private FCkUnrealComponentGym_Snapshot _Installed;
    private TArray<FCk_Handle> _FrozenStations;
    private TArray<FVector> _FrozenAnchors;
    private FCk_Handle _StationCohortOwner;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        if (!IsLocalController() || ck::Is_NOT_Valid(ck::ToEntity(this)))
        { return Stations; }
        for (int32 I = 0; I < CkUnrealComponentGymCatalog::RoleCount; ++I)
        {
            FCkGym_Station_SpawnParams_Payload Recipe;
            if (!CkUnrealComponentGymCatalog::TryGet_Station(I, Recipe))
            { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
            Recipe.Tags.Reset();
            Recipe.Tags.Add(FName(Get_StationTag(CkUnrealComponentGymCatalog::Get_Tag(I))));
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

    private bool Get_HasState(ACk_UnrealComponentGym_GameState State)
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(World) ||
            ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            World.GetGameState() != State ||
            State.GetClass() != ACk_UnrealComponentGym_GameState ||
            !State.Get_HasPublishedCatalog()) { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_UnrealComponentGym_GameMode)
            { return false; }
        }
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        return Index >= 0 && Index < Registry.Num() &&
            Registry[Index].DisplayName == "Unreal Component" &&
            Registry[Index].GameModeClass == ACk_UnrealComponentGym_GameMode;
    }

    void Request_StartGym() override
    {
        // Base invokes this only after actual controller entity/station admission.
        _ControllerAdmitted = true;
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World)) { return; }
        auto State = Cast<ACk_UnrealComponentGym_GameState>(World.GetGameState());
        if (HasAuthority() && Get_HasState(State))
        { State.Request_EnsureSharedDemo(); }
    }

    private bool Get_HasExactPawn(APawn Pawn)
    {
        auto World = GetWorld();
        return ck::IsValid(World) && ck::IsValid(Pawn) &&
            Pawn.GetWorld() == World && Pawn.GetController() == this &&
            GetControlledPawn() == Pawn;
    }

    private void MonitorOwnerContext()
    {
        if (!_ControllerAdmitted || !HasAuthority() || !_OwnerBound || _OwnerLost)
        { return; }
        if (Get_HasState(_OwnerState) && Get_HasExactPawn(_OwnerPawn))
        { return; }
        _OwnerLost = true;
        // A lost requester cannot authorize another reset this session.
        if (LastControlAck.RequestSerial > 0)
        {
            FCkUnrealComponentGym_ControlAck Ack = LastControlAck;
            Ack.Result = 2;
            LastControlAck = Ack;
            if (!_Ending && ck::IsValid(this)) { ForceNetUpdate(); }
        }
    }

    private bool Get_HasLocalIdentity()
    {
        return _ControllerAdmitted && IsLocalController() && !_Ending &&
            !_LocalLost && _LocalBound && !_OwnerLost &&
            Get_HasState(_LocalState) && Get_HasExactPawn(_LocalPawn);
    }

    // Every check uses exact tagged identities and the common admission owner.
    // After first success, any replacement is terminal instead of silently
    // binding a newly allocated shell to the existing presentation.
    private bool TryCollectStations(TArray<FCk_Handle>&out Stations,
        TArray<FVector>&out Anchors, FCk_Handle&out CohortOwner)
    {
        Stations.Reset(); Anchors.Reset(); CohortOwner = FCk_Handle();
        if (!Get_HasLocalIdentity()) { return false; }
        auto Parent = ck::ToEntity(this);
        auto WorldRoot = ck::TransientEntity();
        if (ck::Is_NOT_Valid(Parent) || ck::Is_NOT_Valid(WorldRoot))
        { return false; }
        for (int32 I = 0; I < CkUnrealComponentGymCatalog::RoleCount; ++I)
        {
            auto Matches = utils_entity_tag::ForEach_Entity(WorldRoot,
                FName(Get_StationTag(CkUnrealComponentGymCatalog::Get_Tag(I))));
            if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]))
            { return false; }
            auto Station = Matches[0];
            if (!Station.Has_Fragment(FCkGym_Station_Anchors) ||
                !Station.Has_Fragment(FCkGym_Station_TitleAndDescription) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Station))
            { return false; }
            auto Owner = utils_entity_lifetime::Get_LifetimeOwner(Station);
            if (ck::Is_NOT_Valid(Owner) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Owner) ||
                utils_entity_lifetime::Get_LifetimeOwner(Owner) != Parent ||
                (I > 0 && Owner != CohortOwner))
            { return false; }
            if (I == 0) { CohortOwner = Owner; }
            auto Anchor = Station.Get_Fragment(FCkGym_Station_Anchors).PanelCenter;
            FVector Expected;
            if (!CkUnrealComponentGymCatalog::TryGet_Origin(I, Expected) ||
                (Anchor - Expected).SizeSquared() > 1.0)
            { return false; }
            for (auto Existing : Stations)
            { if (Existing == Station) { return false; } }
            if (_StationsFrozen &&
                (_FrozenStations.Num() != CkUnrealComponentGymCatalog::RoleCount ||
                 _FrozenAnchors.Num() != CkUnrealComponentGymCatalog::RoleCount ||
                 _FrozenStations[I] != Station ||
                 (_FrozenAnchors[I] - Anchor).SizeSquared() > 1.0 ||
                 _StationCohortOwner != Owner))
            { return false; }
            Stations.Add(Station);
            Anchors.Add(Anchor);
        }
        return Stations.Num() == CkUnrealComponentGymCatalog::RoleCount;
    }

    private void Request_CloseLocalFailure(FString Reason)
    {
        if (_LocalLost) { return; }
        _LocalLost = true;
        _Installed = FCkUnrealComponentGym_Snapshot();
        // Close admission before a presenter callback can run, and only for
        // the exact ECS world captured with this controller's local session.
        if (ck::IsValid(_LocalWorld) && ck::IsValid(_LocalWorldRoot) &&
            utils_entity_lifetime::Get_WorldForEntity(_LocalWorldRoot) == _LocalWorld)
        { CkGym_Admission::TryCloseWorldUnproven(_LocalWorldRoot); }
        if (HasAuthority() && ck::IsValid(_LocalState) &&
            _LocalState.GetWorld() == _LocalWorld &&
            !_LocalState.IsActorBeingDestroyed())
        { _LocalState.Request_Fail("Local authority presentation lost: " + Reason); }
        if (ck::IsValid(_Presenter)) { _Presenter.Request_CloseFailure(); }
        ck::Error("[UnrealComponent Gym] Local presentation closed: " + Reason);
        // No replacement presenter or station cohort is created this session.
    }

    private bool TryEnsurePresenter()
    {
        if (HasAuthority()) { return true; } // Listen host reuses source visuals.
        if (_PresenterAttempted)
        { return ck::IsValid(_Presenter) && !_Presenter.Get_IsFailed(); }
        _PresenterAttempted = true;
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World)) { return false; }
        auto Candidate = SpawnActor(ACk_UnrealComponentGym_Presenter_UE,
            FVector::ZeroVector, FRotator(0.0, 0.0, 0.0), NAME_None, true);
        if (ck::Is_NOT_Valid(Candidate)) { return false; }
        _Presenter = Candidate; // Retain first returned actor even on failure.
        Candidate.SetOwner(this); // Native BeginPlay captures this exact local PC.
        TArray<FCk_Handle> Stations;
        TArray<FVector> Anchors;
        FCk_Handle Owner;
        if (ck::Is_NOT_Valid(Candidate) || !Get_HasLocalIdentity() ||
            !TryCollectStations(Stations, Anchors, Owner) ||
            Candidate.GetWorld() != World || Candidate.GetOwner() != this)
        { if (ck::IsValid(Candidate)) { Candidate.Request_CloseFailure(); } return false; }
        FinishSpawningActor(Candidate);
        if (ck::Is_NOT_Valid(Candidate) || !Get_HasLocalIdentity() ||
            !TryCollectStations(Stations, Anchors, Owner) ||
            Candidate.GetWorld() != World || Candidate.GetOwner() != this ||
            !Candidate.TryCheckHealth())
        { if (ck::IsValid(Candidate)) { Candidate.Request_CloseFailure(); } return false; }
        return true;
    }

    private bool Get_IsSameFrame(FCkUnrealComponentGym_Snapshot A,
        FCkUnrealComponentGym_Snapshot B)
    {
        if (A.CatalogVersion != B.CatalogVersion ||
            A.Generation != B.Generation || A.Sequence != B.Sequence ||
            A.FrameSerial != B.FrameSerial || A.Lifecycle != B.Lifecycle ||
            A.FailureReason != B.FailureReason ||
            A.ComponentWorldPoses.Num() != 8 || B.ComponentWorldPoses.Num() != 8 ||
            A.Bodies.Num() != 8 || B.Bodies.Num() != 8 ||
            A.Instructions.Num() != 8 || B.Instructions.Num() != 8)
        { return false; }
        for (int32 I = 0; I < 8; ++I)
        {
            if (!A.ComponentWorldPoses[I].Equals(B.ComponentWorldPoses[I], 0.0) ||
                A.Bodies[I] != B.Bodies[I] ||
                A.Instructions[I] != B.Instructions[I])
            { return false; }
        }
        return true;
    }

    private bool Get_HasPresentationReady()
    {
        if (!Get_HasLocalIdentity() || !_StationsFrozen ||
            !CkUnrealComponentGymShared::Get_IsValidSnapshot(_Installed) ||
            _Installed.Lifecycle != ECkUnrealComponentGym_Lifecycle::Active)
        { return false; }
        auto Wire = _LocalState.Snapshot;
        return CkUnrealComponentGymShared::Get_IsValidSnapshot(Wire) &&
            Wire.Lifecycle == ECkUnrealComponentGym_Lifecycle::Active &&
            Wire.Generation == _Installed.Generation &&
            Wire.Sequence == _Installed.Sequence &&
            (HasAuthority() || (ck::IsValid(_Presenter) &&
                _Presenter.Get_InstalledSequence() == _Installed.Sequence &&
                _Presenter.Get_InstalledGeneration() == _Installed.Generation));
    }

    FString Get_ControlPanelTitle() override { return "UNREAL COMPONENT"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset shared components",
            Get_HasPresentationReady() && LastControlAck.Result != 2));
        return Rows;
    }

    void Request_ControlActivated(int32 RowIndex) override
    {
        if (RowIndex != 0 || LastControlAck.Result == 2 ||
            !Get_HasPresentationReady()) { return; }
        auto High = Math::Max(_RequestedSerial, LastControlAck.RequestSerial);
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        Server_RequestUnrealComponentReset(_RequestedSerial, _Installed.Generation);
    }

    UFUNCTION(BlueprintPure)
    FCkUnrealComponentGym_ControlAck Get_UnrealComponentControlAck() const
    { return LastControlAck; }

    UFUNCTION(BlueprintPure)
    FCkUnrealComponentGym_Snapshot Get_InstalledUnrealComponentSample() const
    { return _Installed; }

    UFUNCTION(BlueprintPure)
    TArray<FCk_Handle> Get_CapturedUnrealComponentStations() const
    { return _FrozenStations; }

    UFUNCTION(BlueprintPure)
    TArray<FVector> Get_CapturedUnrealComponentAnchors() const
    { return _FrozenAnchors; }

    UFUNCTION(Server)
    void Server_RequestUnrealComponentReset(int32 Serial, int32 Generation)
    {
        // A malformed/replayed serial does not call the source or change ACK.
        if (Serial <= 0 || Serial <= LastControlAck.RequestSerial)
        { return; }
        MonitorOwnerContext();
        if (!_ControllerAdmitted || !HasAuthority() || _Ending || _OwnerLost)
        { return; }
        FCkUnrealComponentGym_ControlAck Ack;
        Ack.RequestSerial = Serial; Ack.Generation = Generation; Ack.Result = 3;
        auto World = GetWorld();
        ACk_UnrealComponentGym_GameState State;
        if (ck::IsValid(World))
        { State = Cast<ACk_UnrealComponentGym_GameState>(World.GetGameState()); }
        auto Pawn = GetControlledPawn();
        bool Exact = Get_HasState(State) && Get_HasExactPawn(Pawn) &&
            Generation > 0 &&
            CkUnrealComponentGymShared::Get_IsValidSnapshot(State.Snapshot) &&
            State.Snapshot.Lifecycle == ECkUnrealComponentGym_Lifecycle::Active &&
            State.Snapshot.Generation == Generation &&
            (!_OwnerBound || (_OwnerPawn == Pawn && _OwnerState == State));
        if (Exact)
        {
            _OwnerBound = true; _OwnerPawn = Pawn; _OwnerState = State;
            const bool Admitted = State.Request_ResetSharedDemo();
            MonitorOwnerContext();
            Ack.Result = !_OwnerLost && Get_HasState(State) &&
                Get_HasExactPawn(Pawn) ? (Admitted ? 1 : 3) : 2;
        }
        if (!_Ending && ck::IsValid(this))
        { LastControlAck = Ack; ForceNetUpdate(); }
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        MonitorOwnerContext();
        if (!_ControllerAdmitted || _Ending || !IsLocalController() || _LocalLost)
        { return; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World))
        { if (_LocalBound) { Request_CloseLocalFailure("world lost"); } return; }
        auto State = Cast<ACk_UnrealComponentGym_GameState>(World.GetGameState());
        auto Pawn = GetControlledPawn();
        if (!_LocalBound)
        {
            if (!Get_HasState(State) || !Get_HasExactPawn(Pawn)) { return; }
            _LocalBound = true; _LocalState = State; _LocalPawn = Pawn;
            _LocalWorld = World; _LocalWorldRoot = ck::TransientEntity();
            if (ck::Is_NOT_Valid(_LocalWorldRoot) ||
                utils_entity_lifetime::Get_WorldForEntity(_LocalWorldRoot) != World)
            { Request_CloseLocalFailure("local ECS world unavailable"); return; }
        }
        if (!Get_HasLocalIdentity())
        { Request_CloseLocalFailure("bound PC, pawn, state, or gym changed"); return; }
        TArray<FCk_Handle> Stations;
        TArray<FVector> Anchors;
        FCk_Handle Owner;
        if (!TryCollectStations(Stations, Anchors, Owner))
        {
            Request_CloseLocalFailure("admitted station cohort missing or changed");
            return;
        }
        if (!_StationsFrozen)
        {
            _FrozenStations = Stations;
            _FrozenAnchors = Anchors;
            _StationCohortOwner = Owner;
            _StationsFrozen = true;
        }
        auto Wire = State.Snapshot;
        if (!CkUnrealComponentGymShared::Get_IsValidSnapshot(Wire) ||
            Wire.Lifecycle == ECkUnrealComponentGym_Lifecycle::Failed ||
            (_Installed.Sequence > 0 &&
                (Wire.Sequence < _Installed.Sequence ||
                 Wire.Generation < _Installed.Generation ||
                 (Wire.Sequence == _Installed.Sequence &&
                  !Get_IsSameFrame(Wire, _Installed)))))
        { Request_CloseLocalFailure("invalid, failed, or regressed source wire"); return; }
        if (ck::IsValid(_Presenter) && !_Presenter.TryCheckHealth())
        { Request_CloseLocalFailure("passive visual cohort changed"); return; }
        if (Wire.Lifecycle != ECkUnrealComponentGym_Lifecycle::Active)
        {
            // A reset has an intentional hidden interval. Keep the exact
            // presenter cohort and wait for a newer physically settled frame.
            if (ck::IsValid(_Presenter) && !_Presenter.Request_Suspend())
            { Request_CloseLocalFailure("passive visual suspend failed"); }
            return;
        }
        if (Wire.Sequence <= _Installed.Sequence) { return; }
        // Complete finite eight-pose and body preflight above precedes all
        // presenter or display mutations. Global sequence permits coalescing.
        if (!TryEnsurePresenter())
        { Request_CloseLocalFailure("passive visual creation failed"); return; }
        if (!HasAuthority() && !_Presenter.TryInstall(
            Wire.Generation, Wire.Sequence, Wire.ComponentWorldPoses))
        { Request_CloseLocalFailure("passive visual install failed"); return; }
        if (!Get_HasLocalIdentity() || !TryCollectStations(Stations, Anchors, Owner))
        { Request_CloseLocalFailure("local station changed during install"); return; }
        for (int32 I = 0; I < CkUnrealComponentGymCatalog::RoleCount; ++I)
        {
            auto Title = CkUnrealComponentGymCatalog::Get_Title(I) +
                " (" + CkGym_Common::Get_NetworkRoleTitle(_FrozenStations[I]) + ")";
            auto& Text = _FrozenStations[I].Get_Fragment(FCkGym_Station_TitleAndDescription);
            Text.Title = FText::FromString(Title);
            Text.Description = FText::FromString(Wire.Bodies[I]);
            Text.Instructions = FText::FromString(Wire.Instructions[I]);
            if (!Get_HasLocalIdentity() ||
                !TryCollectStations(Stations, Anchors, Owner) ||
                !CkUnrealComponentGymShared::Get_IsValidSnapshot(State.Snapshot) ||
                !Get_IsSameFrame(Wire, State.Snapshot))
            { Request_CloseLocalFailure("station changed during display update"); return; }
        }
        _Installed = Wire;
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        if (ck::IsValid(_Presenter)) { _Presenter.Request_CloseFailure(); }
        _Installed = FCkUnrealComponentGym_Snapshot();
        Super::EndPlay(Reason);
    }
}
