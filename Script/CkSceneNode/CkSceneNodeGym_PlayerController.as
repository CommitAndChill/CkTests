// Each local player owns six station shells. The GameState owns the one
// SceneNode simulation and, on clients, its single passive visual cohort.
USTRUCT()
struct FCkSceneNodeGym_ControlAck
{
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 Generation = 0;
    // 1 admitted, 2 requester context lost, 3 rejected. No completion claim.
    UPROPERTY() int32 Result = 0;
}

class ACk_SceneNodeGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) FCkSceneNodeGym_ControlAck LastControlAck;

    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _ReservedSerial = 0;
    private int32 _ReservedGeneration = 0;
    private APawn _ReservedPawn;
    private ACk_SceneNodeGym_GameState _ReservedState;
    private APawn _SessionPawn;
    private ACk_SceneNodeGym_GameState _SessionState;
    private bool _SessionBound = false;
    private bool _OwnerSessionLost = false;
    private bool _ControllerAdmitted = false;
    private bool _Ending = false;
    private bool _PresentationBound = false;
    private bool _PresentationLost = false;
    private APawn _PresentationPawn;
    private ACk_SceneNodeGym_GameState _BoundState;
    private bool _LocalCreating = false;
    private bool _LocalStarting = false;
    private bool _LocalRetiring = false;
    private bool _LocalFailed = false;
    private bool _LocalRetirementUnproven = false;
    private bool _IdleTextStaged = false;
    private FString _DeferredLocalFailure;
    UPROPERTY() private FString _LocalFailureReason;
    private float _LocalStartedAt = 0.0f;
    private int32 _LocalGeneration = 0;
    private int64 _ShownSequence = 0;
    private FCk_Handle _LocalWorldRoot;
    private FCk_Handle _LocalParent;
    UPROPERTY() private FCkSceneNodeGym_StationCohort _StationCohort;
    UPROPERTY() private FCkSceneNodeGym_Snapshot _PendingInstallSample;
    UPROPERTY() private FCkSceneNodeGym_Snapshot _InstalledSample;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    { return TArray<FCkGym_Station_SpawnParams_Payload>(); }

    private bool Get_HasCurrentState(ACk_SceneNodeGym_GameState State) const
    {
        auto CurrentWorld = GetWorld();
        return !_Ending && !IsActorBeingDestroyed() && ck::IsValid(CurrentWorld) &&
            ck::IsValid(State) && State.GetClass() == ACk_SceneNodeGym_GameState &&
            State.GetWorld() == CurrentWorld && CurrentWorld.GetGameState() == State &&
            State.Get_HasPublishedCatalog();
    }

    void Request_StartGym() override
    {
        // Base PC invokes this only after its real controller ECS admission.
        _ControllerAdmitted = true;
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld)) { return; }
        auto State = Cast<ACk_SceneNodeGym_GameState>(CurrentWorld.GetGameState());
        if (HasAuthority() && Get_HasCurrentState(State))
        { State.Request_EnsureSharedDemo(); }
    }

    FString Get_ControlPanelTitle() override { return "SCENE NODE"; }

    // The requester owns only its input/ack session. A successfully admitted
    // shared retirement continues even if this owner later disappears.
    private void MonitorOwnerContext()
    {
        if (!_ControllerAdmitted || !HasAuthority() || !_SessionBound ||
            _OwnerSessionLost) { return; }
        auto CurrentWorld = GetWorld();
        bool Exact = !_Ending && ck::IsValid(CurrentWorld) &&
            Get_HasCurrentState(_SessionState) && ck::IsValid(_SessionPawn);
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
            FCkSceneNodeGym_ControlAck Ack;
            Ack.RequestSerial = _ReservedSerial;
            Ack.Generation = _ReservedGeneration;
            Ack.Result = 2;
            LastControlAck = Ack;
        }
        _ReservedSerial = 0; _ReservedGeneration = 0;
        _ReservedPawn = nullptr; _ReservedState = nullptr;
        if (!_Ending && ck::IsValid(this)) { ForceNetUpdate(); }
    }

    private bool Get_CanRequestControl() const
    {
        if (!_ControllerAdmitted || !IsLocalController() || _Ending ||
            _OwnerSessionLost || LastControlAck.Result == 2 ||
            _PresentationLost || _LocalFailed || _LocalStarting ||
            _LocalRetiring || _LocalRetirementUnproven ||
            !_PresentationBound || _PendingSerial != 0) { return false; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld)) { return false; }
        auto State = Cast<ACk_SceneNodeGym_GameState>(CurrentWorld.GetGameState());
        auto Pawn = GetControlledPawn();
        if (!Get_HasLocalContext(State, Pawn) ||
            !_StationCohort.Get_HasExactReadyCohort())
        { return false; }
        FCkSceneNodeGym_Snapshot Sample;
        return State.TryGet_PresentationSample(Sample) &&
            CkSceneNodeGymShared::Get_IsValidSnapshot(Sample) &&
            Sample.Lifecycle == ECkSceneNodeGym_Lifecycle::Active;
    }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Re-run every station",
            Get_CanRequestControl()));
        if (LastControlAck.RequestSerial > 0 && LastControlAck.Result != 0)
        {
            auto Status = LastControlAck.Result == 1 ? "Shared rebuild admitted" :
                LastControlAck.Result == 2 ? "Owner context lost" : "Rebuild rejected";
            Rows.Add(CkGym_Control::Status("Own request", Status,
                LastControlAck.Result != 1));
        }
        return Rows;
    }

    // Reflect the same live row list the HUD uses for native production tests.
    UFUNCTION(BlueprintPure)
    TArray<FCkGym_ControlRow> Get_SceneNodeControlRows()
    { return Get_ControlRows(); }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (InRowIndex != 0 || !Get_CanRequestControl()) { return; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld)) { return; }
        auto State = Cast<ACk_SceneNodeGym_GameState>(CurrentWorld.GetGameState());
        if (!Get_HasCurrentState(State)) { return; }
        FCkSceneNodeGym_Snapshot Sample;
        if (!State.TryGet_PresentationSample(Sample) ||
            !CkSceneNodeGymShared::Get_IsValidSnapshot(Sample) ||
            Sample.Lifecycle != ECkSceneNodeGym_Lifecycle::Active) { return; }
        auto High = Math::Max(_RequestedSerial,
            LastControlAck.RequestSerial);
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        _PendingSerial = _RequestedSerial;
        Server_RequestSceneNodeRebuild(_RequestedSerial, Sample.Generation);
    }

    UFUNCTION(BlueprintPure)
    bool Get_HasReservedSceneNodeControl(int32 Serial, int32 Generation,
        APawn Pawn) const
    {
        if (!_ControllerAdmitted || !HasAuthority() || _Ending ||
            _OwnerSessionLost ||
            Serial <= 0 || Generation <= 0 || _ReservedSerial != Serial ||
            _ReservedGeneration != Generation || _ReservedPawn != Pawn ||
            !Get_HasCurrentState(_ReservedState) ||
            _ReservedState != _SessionState || _SessionPawn != Pawn ||
            ck::Is_NOT_Valid(Pawn) || Pawn.GetWorld() != GetWorld() ||
            Pawn.GetController() != this || GetControlledPawn() != Pawn)
        { return false; }
        return true;
    }

    UFUNCTION(Server)
    void Server_RequestSceneNodeRebuild(int32 Serial, int32 Generation)
    {
        MonitorOwnerContext();
        if (!_ControllerAdmitted || !HasAuthority() || _Ending || _ReservedSerial != 0 ||
            _OwnerSessionLost || Serial <= 0 ||
            Serial <= LastControlAck.RequestSerial) { return; }
        FCkSceneNodeGym_ControlAck Ack;
        Ack.RequestSerial = Serial; Ack.Generation = Generation;
        LastControlAck = Ack;
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld))
        { Ack.Result = 3; LastControlAck = Ack; ForceNetUpdate(); return; }
        auto State = Cast<ACk_SceneNodeGym_GameState>(CurrentWorld.GetGameState());
        auto Pawn = GetControlledPawn();
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (!Get_HasCurrentState(State) ||
            ck::Is_NOT_Valid(Pawn) || Pawn.GetWorld() != CurrentWorld ||
            Pawn.GetController() != this || GetControlledPawn() != Pawn ||
            Index < 0 || Index >= Registry.Num() ||
            Registry[Index].DisplayName != "Scene Node" ||
            Registry[Index].GameModeClass != ACk_SceneNodeGym_GameMode ||
            Generation <= 0 || State.Snapshot.Generation != Generation ||
            State.Snapshot.Lifecycle != ECkSceneNodeGym_Lifecycle::Active ||
            (_SessionBound && (_SessionPawn != Pawn || _SessionState != State)))
        { Ack.Result = 3; LastControlAck = Ack; ForceNetUpdate(); return; }
        _SessionBound = true;
        _SessionPawn = Pawn;
        _SessionState = State;
        _ReservedSerial = Serial;
        _ReservedGeneration = Generation;
        _ReservedPawn = Pawn;
        _ReservedState = State;
        bool Accepted = State.Request_Rebuild(this, Pawn, Serial, Generation);
        MonitorOwnerContext();
        bool OwnerStillExact = !_Ending && !_OwnerSessionLost &&
            Get_HasCurrentState(State) &&
            _ReservedSerial == Serial && _ReservedGeneration == Generation &&
            _ReservedPawn == Pawn && _ReservedState == State &&
            _SessionPawn == Pawn && _SessionState == State &&
            ck::IsValid(Pawn) && Pawn.GetWorld() == CurrentWorld &&
            Pawn.GetController() == this && GetControlledPawn() == Pawn;
        // The reservation is valid only for the synchronous admission call.
        _ReservedSerial = 0; _ReservedGeneration = 0;
        _ReservedPawn = nullptr; _ReservedState = nullptr;
        Ack.Result = OwnerStillExact ? (Accepted ? 1 : 3) : 2;
        if (!_Ending && ck::IsValid(this))
        { LastControlAck = Ack; ForceNetUpdate(); }
    }

    private bool Get_HasLocalContext(ACk_SceneNodeGym_GameState State,
        APawn Pawn) const
    {
        if (!IsLocalController() || _Ending || _PresentationLost ||
            _OwnerSessionLost ||
            LastControlAck.Result == 2 ||
            !Get_HasCurrentState(State) || State != _BoundState ||
            ck::Is_NOT_Valid(Pawn) || Pawn != _PresentationPawn ||
            Pawn.GetWorld() != GetWorld() || Pawn.GetController() != this ||
            GetControlledPawn() != Pawn || _LocalWorldRoot != ck::TransientEntity() ||
            ck::Is_NOT_Valid(_LocalWorldRoot) || ck::Is_NOT_Valid(_LocalParent) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                _LocalParent, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            _LocalParent != utils_owning_actor::TryGet_ActorEntityHandle(this) ||
            _StationCohort.Get_CohortOwner() == FCk_Handle())
        { return false; }
        return true;
    }

    private void Request_RetireLocal()
    {
        _ShownSequence = 0;
        _InstalledSample = FCkSceneNodeGym_Snapshot();
        _PendingInstallSample = FCkSceneNodeGym_Snapshot();
        if (_LocalCreating)
        {
            if (_DeferredLocalFailure.IsEmpty())
            { _DeferredLocalFailure = "SceneNode local context retired during creation"; }
            return;
        }
        _LocalStarting = false;
        if (_StationCohort.Get_Phase() == 0) { return; }
        _LocalRetiring = true;
        if (!_StationCohort.Request_CloseKnownPresentation())
        { _LocalRetirementUnproven = true; }
        _StationCohort.Request_Retire();
        if (_StationCohort.Get_IsRetirementUnproven())
        { _LocalRetirementUnproven = true; }
    }

    private bool Get_LocalRetired()
    {
        if (_LocalRetirementUnproven) { return false; }
        if (!_LocalRetiring) { return true; }
        if (!_StationCohort.PollRetirement())
        {
            if (_StationCohort.Get_IsRetirementUnproven())
            { _LocalRetirementUnproven = true; }
            return false;
        }
        if (_StationCohort.Get_IsRetirementUnproven())
        { _LocalRetirementUnproven = true; return false; }
        _LocalWorldRoot = FCk_Handle(); _LocalParent = FCk_Handle();
        _LocalGeneration = 0; _LocalRetiring = false;
        _DeferredLocalFailure = "";
        return true;
    }

    private void Request_FailLocal(FString Reason)
    {
        if (_LocalCreating)
        {
            if (_DeferredLocalFailure.IsEmpty()) { _DeferredLocalFailure = Reason; }
            return;
        }
        if (_LocalFailureReason.IsEmpty()) { _LocalFailureReason = Reason; }
        _LocalFailed = true;
        Request_RetireLocal();
        ck::Error("[SceneNodeShared] " + Reason);
    }

    private void Request_StartLocal(ACk_SceneNodeGym_GameState State,
        FCkSceneNodeGym_Snapshot Sample)
    {
        if (!Get_HasCurrentState(State) || !IsLocalController() ||
            _LocalCreating || _LocalStarting || _LocalFailed || !Get_LocalRetired() ||
            !CkSceneNodeGymShared::Get_IsValidSnapshot(Sample) ||
            Sample.Lifecycle != ECkSceneNodeGym_Lifecycle::Active) { return; }
        auto Parent = utils_owning_actor::TryGet_ActorEntityHandle(this);
        auto WorldRoot = ck::TransientEntity();
        if (ck::Is_NOT_Valid(Parent) || ck::Is_NOT_Valid(WorldRoot) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                Parent, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Parent))
        { Request_FailLocal("SceneNode local controller entity unavailable"); return; }
        TArray<FCk_GymStation_SpawnParams> Recipes;
        if (!_StationCohort.TryBuildLocalRecipes(Parent, Recipes) || Recipes.Num() != 6)
        { Request_FailLocal("SceneNode local station catalog rejected"); return; }
        _LocalParent = Parent; _LocalWorldRoot = WorldRoot;
        _LocalGeneration = Sample.Generation;
        _LocalStarting = true;
        _LocalStartedAt = float(System::GetGameTimeInSeconds());
        _LocalCreating = true;
        auto Begin = _StationCohort.TryBeginLocal(WorldRoot, Parent, Recipes,
            FCk_Delegate_Request_OnCompleted(this, n"OnLocalSpawnCompleted"),
            FCk_Delegate_EntityScript_Constructed(this, n"OnLocalConstructed"));
        _LocalCreating = false;
        if (!_DeferredLocalFailure.IsEmpty())
        { Request_FailLocal(_DeferredLocalFailure); return; }
        if (Begin != ECkGym_SharedStationBegin::Started)
        { Request_FailLocal("SceneNode local station admission rejected: " +
            _StationCohort.Get_LastAdmissionRejection()); }
    }

    UFUNCTION() private void OnLocalSpawnCompleted(FCk_Handle H,
        ECk_Request_OperationResult Result)
    {
        if (_Ending || _LocalFailed || !_LocalStarting) { return; }
        if (!_StationCohort.TryRecord_Completed(H, H, Result))
        { Request_FailLocal("SceneNode local station completion rejected"); }
    }

    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript H)
    {
        if (_Ending || _LocalFailed || !_LocalStarting) { return; }
        if (!_StationCohort.TryRecord_Constructed(FCk_Handle(H), H))
        { Request_FailLocal("SceneNode local station construction rejected"); }
    }

    private FString Get_ViewerRole() const
    {
        auto CurrentWorld = GetWorld();
        if (!IsLocalController() || ck::Is_NOT_Valid(CurrentWorld)) { return ""; }
        auto Mode = CurrentWorld.GetNetMode();
        if (Mode == ENetMode::NM_Client) { return "CLIENT"; }
        if (Mode == ENetMode::NM_ListenServer) { return "CLIENT+HOST"; }
        if (Mode == ENetMode::NM_Standalone) { return "HOST"; }
        return "";
    }

    private bool TryGet_ExpectedText(FCkSceneNodeGym_Snapshot Sample,
        int32 Slot, FString&out Title, FString&out Body,
        FString&out Instructions) const
    {
        Title = ""; Body = ""; Instructions = "";
        if (!CkSceneNodeGymShared::Get_IsValidSnapshot(Sample) ||
            Sample.Lifecycle != ECkSceneNodeGym_Lifecycle::Active ||
            Slot < 0 || Slot >= 6) { return false; }
        auto Role = Get_ViewerRole();
        if (Role.IsEmpty()) { return false; }
        Title = Sample.BaseTitles[Slot] + " (" + Role + ")";
        Body = Sample.Bodies[Slot]; Instructions = Sample.Instructions[Slot];
        return true;
    }

    private bool Get_HasExactLocalText(FCkSceneNodeGym_Snapshot Sample) const
    {
        if (!_StationCohort.Get_HasExactReadyCohort() ||
            _StationCohort.Stations.Num() != 6) { return false; }
        for (int32 i = 0; i < 6; ++i)
        {
            auto Station = _StationCohort.Stations[i];
            if (ck::Is_NOT_Valid(Station) ||
                utils_entity_lifetime::Get_IsPendingDestroy(
                    Station, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !Station.Has_Fragment(FCkGym_Station_TitleAndDescription) ||
                !utils_entity_script::Has(Station)) { return false; }
            auto Script = Cast<UCk_EntityScript_GymStation>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station));
            if (ck::Is_NOT_Valid(Script) ||
                UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(Script) != Station)
            { return false; }
            FCkGym_StationVisibleGeometry Geometry;
            if (!Script.TryGet_VisibleGeometry(Geometry) ||
                Geometry.Pieces.Num() != 10 || Geometry.Anchors.Num() != 7 ||
                Geometry.Pieces[6].Role != ECkGym_StationVisibleRole::TitleText ||
                Geometry.Pieces[7].Role != ECkGym_StationVisibleRole::DescriptionText ||
                Geometry.Pieces[8].Role != ECkGym_StationVisibleRole::FloorDescriptionText)
            { return false; }
            FString Title, Body, Instructions;
            if (!TryGet_ExpectedText(Sample, i, Title, Body, Instructions))
            { return false; }
            auto& Fragment = Station.Get_Fragment(FCkGym_Station_TitleAndDescription);
            if (Fragment.Title.ToString() != Title ||
                Fragment.Description.ToString() != Body ||
                Fragment.Instructions.ToString() != Instructions ||
                !Geometry.Pieces[6].Visible || Geometry.Pieces[6].HiddenInGame ||
                !Geometry.Pieces[7].Visible || Geometry.Pieces[7].HiddenInGame ||
                Geometry.Pieces[6].Text != Title || Geometry.Pieces[7].Text != Body)
            { return false; }
            if (Instructions.IsEmpty())
            { if (Geometry.Pieces[8].Visible) { return false; } }
            else if (!Geometry.Pieces[8].Visible ||
                Geometry.Pieces[8].HiddenInGame ||
                Geometry.Pieces[8].Text != Instructions) { return false; }
        }
        return true;
    }

    private bool TryStageLocalText(FCkSceneNodeGym_Snapshot Sample)
    {
        if (!_StationCohort.Get_HasExactReadyCohort() ||
            _StationCohort.Stations.Num() != 6) { return false; }
        // Validate all six known fragment slots before the first write.
        TArray<FString> Titles, Bodies, InstructionsBySlot;
        for (int32 i = 0; i < 6; ++i)
        {
            auto Station = _StationCohort.Stations[i];
            FString Title, Body, Instructions;
            if (!TryGet_ExpectedText(Sample, i, Title, Body, Instructions) ||
                ck::Is_NOT_Valid(Station) ||
                utils_entity_lifetime::Get_IsPendingDestroy(
                    Station, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !Station.Has_Fragment(FCkGym_Station_TitleAndDescription))
            { return false; }
            Titles.Add(Title); Bodies.Add(Body); InstructionsBySlot.Add(Instructions);
        }
        for (int32 i = 0; i < 6; ++i)
        {
            auto& Fragment = _StationCohort.Stations[i].Get_Fragment(
                FCkGym_Station_TitleAndDescription);
            Fragment.Title = FText::FromString(Titles[i]);
            Fragment.Description = FText::FromString(Bodies[i]);
            Fragment.Instructions = FText::FromString(InstructionsBySlot[i]);
        }
        _PendingInstallSample = Sample;
        _IdleTextStaged = false;
        return true;
    }

    // Preserve the six local shells through shared retirement. Restore their
    // authored label and an inactive body so no prior source row stays visible.
    private bool TryStageIdleText()
    {
        if (!_StationCohort.Get_HasExactReadyCohort() ||
            _StationCohort.Stations.Num() != 6) { return false; }
        TArray<FText> Titles;
        for (int32 i = 0; i < 6; ++i)
        {
            auto Station = _StationCohort.Stations[i];
            FCkGym_Station_SpawnParams_Payload Recipe;
            if (!CkSceneNodeGymCatalog::TryGet_LaidOutStation(i, Recipe) ||
                Recipe.Title.IsEmpty() || ck::Is_NOT_Valid(Station) ||
                utils_entity_lifetime::Get_IsPendingDestroy(
                    Station, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
                !Station.Has_Fragment(FCkGym_Station_TitleAndDescription))
            { return false; }
            Titles.Add(Recipe.Title);
        }
        for (int32 i = 0; i < 6; ++i)
        {
            auto& Fragment = _StationCohort.Stations[i].Get_Fragment(
                FCkGym_Station_TitleAndDescription);
            Fragment.Title = Titles[i];
            Fragment.Description = FText::FromString("Waiting for shared simulation");
            Fragment.Instructions = FText::FromString("");
        }
        _IdleTextStaged = true;
        return true;
    }

    UFUNCTION(BlueprintPure)
    TArray<FCk_Handle> Get_CapturedSceneNodeStations() const
    { return _StationCohort.Stations; }

    UFUNCTION(BlueprintPure)
    FCk_Handle Get_CapturedSceneNodeCohortOwner() const
    { return _StationCohort.Get_CohortOwner(); }

    UFUNCTION(BlueprintPure)
    FCkSceneNodeGym_Snapshot Get_InstalledSceneNodeSample() const
    { return _InstalledSample; }

    UFUNCTION(BlueprintPure)
    FCkSceneNodeGym_ControlAck Get_SceneNodeControlAck() const
    { return LastControlAck; }

    UFUNCTION(BlueprintPure)
    bool Get_SceneNodePresentationReady() const
    {
        auto CurrentWorld = GetWorld();
        if (!IsLocalController() || _Ending || _PresentationLost ||
            _LocalFailed || _LocalStarting || _LocalRetiring ||
            _LocalRetirementUnproven || !_PresentationBound ||
            ck::Is_NOT_Valid(CurrentWorld) || _ShownSequence <= 0)
        { return false; }
        auto State = Cast<ACk_SceneNodeGym_GameState>(CurrentWorld.GetGameState());
        if (!Get_HasLocalContext(State, _PresentationPawn)) { return false; }
        FCkSceneNodeGym_Snapshot Current;
        if (!State.TryGet_PresentationSample(Current) ||
            !CkSceneNodeGymShared::Get_IsValidSnapshot(Current) ||
            Current.Lifecycle != ECkSceneNodeGym_Lifecycle::Active ||
            Current.Sequence != _ShownSequence ||
            Current.Generation != _LocalGeneration ||
            !CkSceneNodeGymShared::Get_IsExactSnapshotValue(Current, _InstalledSample) ||
            !Get_HasExactLocalText(_InstalledSample)) { return false; }
        return true;
    }

    UFUNCTION(BlueprintOverride)
    void Tick(float DeltaSeconds)
    {
        if (!_ControllerAdmitted) { return; }
        MonitorOwnerContext();
        if (IsLocalController()) { OnPresentationTick(); }
    }

    private void OnPresentationTick()
    {
        if (_LocalRetiring) { Get_LocalRetired(); }
        if (_OwnerSessionLost || LastControlAck.Result == 2)
        { _PresentationLost = true; _LocalFailed = true; Request_RetireLocal(); return; }
        if (_Ending || !IsLocalController() || _PresentationLost)
        { Request_RetireLocal(); return; }
        auto CurrentWorld = GetWorld();
        if (ck::Is_NOT_Valid(CurrentWorld)) { Request_RetireLocal(); return; }
        auto Pawn = GetControlledPawn();
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetWorld() != CurrentWorld ||
            Pawn.GetController() != this ||
            (_PresentationBound && _PresentationPawn != Pawn))
        {
            if (_PresentationBound) { _PresentationLost = true; _LocalFailed = true; }
            Request_RetireLocal(); return;
        }
        if (!_PresentationBound)
        { _PresentationBound = true; _PresentationPawn = Pawn; }
        if (_PendingSerial > 0 && LastControlAck.RequestSerial == _PendingSerial &&
            LastControlAck.Result > 0) { _PendingSerial = 0; }
        auto State = Cast<ACk_SceneNodeGym_GameState>(CurrentWorld.GetGameState());
        if (!Get_HasCurrentState(State))
        {
            if (_BoundState != nullptr)
            { _PresentationLost = true; _LocalFailed = true; Request_RetireLocal(); }
            return;
        }
        if (_BoundState != nullptr && _BoundState != State)
        {
            _PresentationLost = true; _LocalFailed = true;
            Request_RetireLocal(); return;
        }
        if (_BoundState == nullptr)
        {
            _BoundState = State;
            // Observe the shared receipt after its post-ECS installation tick.
            AddTickPrerequisiteActor(_BoundState);
        }
        auto Wire = State.Snapshot;
        if (!CkSceneNodeGymShared::Get_IsValidSnapshot(Wire))
        { Request_FailLocal("SceneNode received snapshot rejected"); return; }
        FCkSceneNodeGym_Snapshot Sample;
        bool HasSample = State.TryGet_PresentationSample(Sample);
        if (HasSample && !CkSceneNodeGymShared::Get_IsValidSnapshot(Sample))
        { Request_FailLocal("SceneNode received snapshot rejected"); return; }
        if (!Get_LocalRetired() || _LocalFailed) { return; }
        if (_StationCohort.Get_Phase() == 0)
        {
            if (HasSample && Sample.Lifecycle == ECkSceneNodeGym_Lifecycle::Active)
            { Request_StartLocal(State, Sample); }
            return;
        }
        if (!Get_HasLocalContext(State, Pawn))
        { Request_FailLocal("SceneNode local owner context changed"); return; }
        if (_LocalStarting)
        {
            if (float(System::GetGameTimeInSeconds()) - _LocalStartedAt > 20.0f)
            { Request_FailLocal("SceneNode local station readiness timed out"); return; }
            auto Poll = _StationCohort.PollConstruction();
            if (Poll == ECkGym_SharedStationPoll::Failed)
            { Request_FailLocal("SceneNode local station proof rejected"); return; }
            if (Poll != ECkGym_SharedStationPoll::Ready) { return; }
            _LocalStarting = false;
        }
        if (!_StationCohort.Get_HasExactReadyCohort())
        { Request_FailLocal("SceneNode local station cohort lost"); return; }
        if (Wire.Lifecycle != ECkSceneNodeGym_Lifecycle::Active)
        {
            _ShownSequence = 0;
            _InstalledSample = FCkSceneNodeGym_Snapshot();
            _PendingInstallSample = FCkSceneNodeGym_Snapshot();
            if (!_IdleTextStaged && !TryStageIdleText())
            { Request_FailLocal("SceneNode inactive display clear rejected"); }
            return;
        }
        // A newer source sample does not replace text still being applied by
        // the station's timer. First observe the exact pending value in all
        // eighteen actual text roles, then install or stage the newer value.
        if (_PendingInstallSample.Sequence > 0)
        {
            if (!Get_HasExactLocalText(_PendingInstallSample)) { return; }
            _InstalledSample = _PendingInstallSample;
            _ShownSequence = _PendingInstallSample.Sequence;
            _LocalGeneration = _PendingInstallSample.Generation;
            _PendingInstallSample = FCkSceneNodeGym_Snapshot();
            if (!HasSample || Sample.Sequence == _ShownSequence) { return; }
        }
        if (!HasSample) { return; } // Valid active wire, passive visual still settling.
        if (Sample.Sequence <= _ShownSequence) { return; }
        if (!TryStageLocalText(Sample))
        {
            Request_FailLocal("SceneNode local display preflight rejected");
        }
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason Reason)
    {
        _Ending = true;
        if (ck::IsValid(_BoundState) && !_BoundState.IsActorBeingDestroyed())
        {
            auto CurrentWorld = GetWorld();
            if (ck::IsValid(CurrentWorld) && _BoundState.GetWorld() == CurrentWorld)
            { RemoveTickPrerequisiteActor(_BoundState); }
        }
        Request_RetireLocal();
        Super::EndPlay(Reason);
    }

    UFUNCTION(Exec)
    void Ck_SceneNode_Control(int32 Index)
    { Request_ControlActivated(Index); }
}
