// Language=angelscript

USTRUCT()
struct FCkCueGym_ControlAck
{
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 Generation = 0;
    // 1 = admitted by GameState; 2 = requester lost; 3 = rejected. Not completion.
    UPROPERTY() int32 Result = 0;
}

// The shared source belongs to GameState. This controller owns only its local
// Common stations, control panel, and (on a client) one passive visual actor.
class ACk_CueGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) FCkCueGym_ControlAck LastControlAck;
    UPROPERTY() private ACk_CueGym_Presenter_UE _Presenter;

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
    private ACk_CueGym_GameState _OwnerState;
    private APawn _LocalPawn;
    private ACk_CueGym_GameState _LocalState;
    private UWorld _LocalWorld;
    private FCk_Handle _LocalWorldRoot;
    private FCkCueGym_Snapshot _Installed;
    private FCkCueGym_Snapshot _Seen;
    private TArray<FCk_Handle> _FrozenStations;
    private TArray<FVector> _FrozenAnchors;
    private FCk_Handle _StationCohortOwner;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        if (!IsLocalController() || ck::Is_NOT_Valid(ck::ToEntity(this)))
        { return Stations; }
        for (int32 I = 0; I < CkCueGymCatalog::RoleCount; ++I)
        {
            FCkGym_Station_SpawnParams_Payload Recipe;
            if (!CkCueGymCatalog::TryGet_Station(I, Recipe))
            { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
            Recipe.Tags.Reset();
            Recipe.Tags.Add(FName(Get_StationTag(CkCueGymCatalog::Get_Tag(I))));
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

    private bool Get_HasState(ACk_CueGym_GameState State)
    {
        auto World = GetWorld();
        if (_Ending || IsActorBeingDestroyed() || ck::Is_NOT_Valid(World) ||
            ck::Is_NOT_Valid(State) || State.GetWorld() != World ||
            World.GetGameState() != State ||
            State.GetClass() != ACk_CueGym_GameState ||
            State.Get_HasTransientIntakeFailure() || !State.Get_HasPublishedCatalog()) { return false; }
        if (HasAuthority())
        {
            auto Mode = Gameplay::GetGameMode();
            if (ck::Is_NOT_Valid(Mode) || Mode.GetWorld() != World ||
                Mode.GetClass() != ACk_CueGym_GameMode)
            { return false; }
        }
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        return Index >= 0 && Index < Registry.Num() &&
            Registry[Index].DisplayName == "Cue" &&
            Registry[Index].GameModeClass == ACk_CueGym_GameMode;
    }

    void Request_StartGym() override
    {
        // Base invokes this only after actual controller entity/station admission.
        _ControllerAdmitted = true;
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World)) { return; }
        auto State = Cast<ACk_CueGym_GameState>(World.GetGameState());
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
            FCkCueGym_ControlAck Ack = LastControlAck;
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
        for (int32 I = 0; I < CkCueGymCatalog::RoleCount; ++I)
        {
            auto Matches = utils_entity_tag::ForEach_Entity(WorldRoot,
                FName(Get_StationTag(CkCueGymCatalog::Get_Tag(I))));
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
            if (!CkCueGymCatalog::TryGet_Origin(I, Expected) ||
                (Anchor - Expected).SizeSquared() > 1.0)
            { return false; }
            for (auto Existing : Stations)
            { if (Existing == Station) { return false; } }
            if (_StationsFrozen &&
                (_FrozenStations.Num() != CkCueGymCatalog::RoleCount ||
                 _FrozenAnchors.Num() != CkCueGymCatalog::RoleCount ||
                 _FrozenStations[I] != Station ||
                 (_FrozenAnchors[I] - Anchor).SizeSquared() > 1.0 ||
                 _StationCohortOwner != Owner))
            { return false; }
            Stations.Add(Station);
            Anchors.Add(Anchor);
        }
        return Stations.Num() == CkCueGymCatalog::RoleCount;
    }

    private void Request_CloseLocalFailure(FString Reason)
    {
        if (_LocalLost) { return; }
        _LocalLost = true;
        _Installed = FCkCueGym_Snapshot();
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
        ck::Error("[Cue Gym] Local presentation closed: " + Reason);
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
        auto Candidate = SpawnActor(ACk_CueGym_Presenter_UE,
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

    private bool Get_IsSameFrame(FCkCueGym_Snapshot A, FCkCueGym_Snapshot B)
    {
        if (A.CatalogVersion != B.CatalogVersion || A.Generation != B.Generation ||
            A.Sequence != B.Sequence || A.FrameSerial != B.FrameSerial ||
            A.VisualRowCount != B.VisualRowCount ||
            A.ResetRole != B.ResetRole || A.TransientRequestedSerial != B.TransientRequestedSerial ||
            A.TransientEpochStartGeneration != B.TransientEpochStartGeneration ||
            A.Lifecycle != B.Lifecycle || A.FailureReason != B.FailureReason ||
            A.RoleEpochs.Num() != B.RoleEpochs.Num() ||
            A.Bodies.Num() != B.Bodies.Num() || A.Instructions.Num() != B.Instructions.Num() ||
            A.VisualRows.Num() != B.VisualRows.Num()) { return false; }
        for (int32 I = 0; I < A.RoleEpochs.Num(); ++I)
        { if (A.RoleEpochs[I] != B.RoleEpochs[I]) { return false; } }
        for (int32 I = 0; I < A.Bodies.Num(); ++I)
        { if (A.Bodies[I] != B.Bodies[I] || A.Instructions[I] != B.Instructions[I]) { return false; } }
        for (int32 I = 0; I < A.VisualRows.Num(); ++I)
        {
            auto X = A.VisualRows[I]; auto Y = B.VisualRows[I];
            if (X.Key != Y.Key || X.Role != Y.Role || X.Kind != Y.Kind ||
                X.OutlineRecipeVersion != Y.OutlineRecipeVersion ||
                !X.Pose.Equals(Y.Pose, 0.0) || X.Color != Y.Color ||
                X.Radius != Y.Radius || X.Extent != Y.Extent ||
                X.Segments != Y.Segments || X.Rings != Y.Rings || X.Axis != Y.Axis ||
                X.DrawLines != Y.DrawLines || X.RenderMode != Y.RenderMode ||
                X.VisibleByRenderMode != Y.VisibleByRenderMode ||
                X.EnableCollision != Y.EnableCollision || X.LineThickness != Y.LineThickness ||
                X.Duration != Y.Duration || X.Lines.Num() != Y.Lines.Num()) { return false; }
            for (int32 J = 0; J < X.Lines.Num(); ++J)
            {
                auto P = X.Lines[J]; auto Q = Y.Lines[J];
                if (P.Start != Q.Start || P.End != Q.End || P.Color != Q.Color ||
                    P.Thickness != Q.Thickness) { return false; }
            }
        }
        return true;
    }

    private bool Get_HasPresentationReady()
    {
        if (!Get_HasLocalIdentity() || !_StationsFrozen ||
            !CkCueGymWire::Get_IsValidSnapshot(_Installed) ||
            _Installed.Lifecycle != ECkCueGym_Lifecycle::Active)
        { return false; }
        auto Wire = _LocalState.Snapshot;
        return CkCueGymWire::Get_IsValidSnapshot(Wire) &&
            Wire.Lifecycle == ECkCueGym_Lifecycle::Active &&
            Wire.Generation == _Installed.Generation &&
            Wire.Sequence == _Installed.Sequence && Get_IsSameFrame(Wire, _Installed) &&
            (HasAuthority() || (ck::IsValid(_Presenter) &&
                !_Presenter.Get_IsSuspended() && _Presenter.Get_InstalledSequence() == _Installed.Sequence &&
                _Presenter.Get_InstalledGeneration() == _Installed.Generation));
    }

    FString Get_ControlPanelTitle() override { return "CUE"; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        const bool Ready = Get_HasPresentationReady() && LastControlAck.Result != 2;
        Rows.Add(CkGym_Control::Header("RE-FIRE A SHARED STATION"));
        for (int32 Role = 0; Role < 6; ++Role)
        { Rows.Add(CkGym_Control::Numbered(Role, CkCueGymCatalog::Get_Title(Role), false, Ready)); }
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Re-fire every shared station", Ready));
        return Rows;
    }

    void Request_ControlActivated(int32 RowIndex) override
    {
        if (RowIndex < 1 || RowIndex > 7 || LastControlAck.Result == 2 ||
            !Get_HasPresentationReady()) { return; }
        auto High = Math::Max(_RequestedSerial, LastControlAck.RequestSerial);
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        Server_RequestCueReset(_RequestedSerial, _Installed.Generation,
            RowIndex == 7 ? -1 : RowIndex - 1);
    }

    UFUNCTION(BlueprintPure)
    FCkCueGym_ControlAck Get_CueControlAck() const
    { return LastControlAck; }

    UFUNCTION(BlueprintPure)
    FCkCueGym_Snapshot Get_InstalledCueSample() const
    { return _Installed; }

    UFUNCTION(BlueprintPure)
    TArray<FCk_Handle> Get_CapturedCueStations() const
    { return _FrozenStations; }

    UFUNCTION(BlueprintPure)
    TArray<FVector> Get_CapturedCueAnchors() const
    { return _FrozenAnchors; }

    UFUNCTION(Server)
    void Server_RequestCueReset(int32 Serial, int32 Generation, int32 Role)
    {
        // A malformed/replayed serial does not call the source or change ACK.
        if (Serial <= 0 || Serial <= LastControlAck.RequestSerial)
        { return; }
        MonitorOwnerContext();
        if (!_ControllerAdmitted || !HasAuthority() || _Ending || _OwnerLost)
        { return; }
        FCkCueGym_ControlAck Ack;
        Ack.RequestSerial = Serial; Ack.Generation = Generation; Ack.Result = 3;
        auto World = GetWorld();
        ACk_CueGym_GameState State;
        if (ck::IsValid(World))
        { State = Cast<ACk_CueGym_GameState>(World.GetGameState()); }
        auto Pawn = GetControlledPawn();
        bool Exact = Get_HasState(State) && Get_HasExactPawn(Pawn) &&
            Generation > 0 && (Role == -1 || (Role >= 0 && Role < 6)) &&
            CkCueGymWire::Get_IsValidSnapshot(State.Snapshot) &&
            State.Snapshot.Lifecycle == ECkCueGym_Lifecycle::Active &&
            State.Snapshot.Generation == Generation &&
            (!_OwnerBound || (_OwnerPawn == Pawn && _OwnerState == State));
        if (Exact)
        {
            _OwnerBound = true; _OwnerPawn = Pawn; _OwnerState = State;
            const bool Admitted = State.Request_ResetSharedDemo(this, Pawn, Role, Generation);
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
        auto State = Cast<ACk_CueGym_GameState>(World.GetGameState());
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
        if (!CkCueGymWire::Get_IsValidSnapshot(Wire) ||
            Wire.Lifecycle == ECkCueGym_Lifecycle::Failed ||
            Wire.Sequence < _Seen.Sequence || Wire.Generation < _Seen.Generation ||
            (Wire.Sequence == _Seen.Sequence && !Get_IsSameFrame(Wire, _Seen)) ||
            (Wire.Sequence > _Seen.Sequence &&
             !CkCueGymWire::Get_IsValidClientAdvance(_Seen, Wire)))
        { Request_CloseLocalFailure("invalid, failed, or regressed source wire"); return; }
        const bool IsNewPublication = Wire.Sequence > _Seen.Sequence;
        _Seen = Wire;
        if (ck::IsValid(_Presenter) && !_Presenter.TryCheckHealth())
        { Request_CloseLocalFailure("passive visual cohort changed"); return; }
        if (Wire.Lifecycle != ECkCueGym_Lifecycle::Active)
        {
            // A reset has an intentional hidden interval. Keep the exact
            // presenter cohort and wait for a newer physically settled frame.
            if (IsNewPublication && ck::IsValid(_Presenter) && !_Presenter.Request_Suspend())
            { Request_CloseLocalFailure("passive visual suspend failed"); }
            return;
        }
        if (Wire.Sequence <= _Installed.Sequence) { return; }
        // Complete visual/body preflight above precedes all
        // presenter or display mutations. Global sequence permits coalescing.
        if (!TryEnsurePresenter())
        { Request_CloseLocalFailure("passive visual creation failed"); return; }
        if (!HasAuthority() && !_Presenter.TryInstall(
            World, Wire.Generation, Wire.Sequence, Wire.VisualRows))
        { Request_CloseLocalFailure("passive visual install failed"); return; }
        if (!Get_HasLocalIdentity() || !TryCollectStations(Stations, Anchors, Owner))
        { Request_CloseLocalFailure("local station changed during install"); return; }
        TArray<FText> Titles;
        TArray<FText> Bodies;
        TArray<FText> Instructions;
        for (int32 I = 0; I < CkCueGymCatalog::RoleCount; ++I)
        {
            Titles.Add(FText::FromString(CkCueGymCatalog::Get_Title(I) + " (" +
                CkGym_Common::Get_NetworkRoleTitle(_FrozenStations[I]) + ")"));
            Bodies.Add(FText::FromString(Wire.Bodies[I]));
            Instructions.Add(FText::FromString(Wire.Instructions[I]));
        }
        if (!Get_HasLocalIdentity() || !TryCollectStations(Stations, Anchors, Owner) ||
            !CkCueGymWire::Get_IsValidSnapshot(State.Snapshot) ||
            !Get_IsSameFrame(Wire, State.Snapshot))
        { Request_CloseLocalFailure("station/source changed before display commit"); return; }
        // No fallible operation or callback is interleaved with this commit.
        for (int32 I = 0; I < CkCueGymCatalog::RoleCount; ++I)
        {
            auto& Text = _FrozenStations[I].Get_Fragment(FCkGym_Station_TitleAndDescription);
            Text.Title = Titles[I];
            Text.Description = Bodies[I];
            Text.Instructions = Instructions[I];
        }
        _Installed = Wire;
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        if (ck::IsValid(_Presenter)) { _Presenter.Request_CloseFailure(); }
        _Installed = FCkCueGym_Snapshot();
        Super::EndPlay(Reason);
    }
}
