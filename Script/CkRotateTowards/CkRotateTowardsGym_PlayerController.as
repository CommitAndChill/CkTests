// Three local station shells and eleven component projections consume one authority RotateTowards kernel.
class ACk_RotateTowardsGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    UPROPERTY(Replicated) int32 LastAcknowledgedDeliverySerial = 0;
    UPROPERTY(Replicated) FCkRotateTowardsGym_ControlCommit LastControlCommit;
    UPROPERTY(Replicated) bool OwnerSessionFailed = false;

    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _ReservedSerial = 0;
    private int32 _ReservedGeneration = 0;
    private int32 _NextDeliverySerial = 0;
    private int32 _AppliedDeliverySerial = 0;
    private ACk_Gym_Base_Pawn _SessionPawn;
    private bool _SessionBound = false;
    private ACk_Gym_Base_Pawn _CommitPawn;
    private ACk_RotateTowardsGym_GameState _SessionState;
    private ACk_RotateTowardsGym_GameState _ReservedState;
    private ACk_RotateTowardsGym_GameState _CommitState;
    UPROPERTY() private FCkRotateTowardsGym_ControlCommit _InstalledControlCommit;

    UFUNCTION(BlueprintPure)
    FCkRotateTowardsGym_ControlCommit Get_InstalledRotateTowardsCommit() const
    { return _InstalledControlCommit; }
    // Pure whole-value observations; these do not install, release, acknowledge or repair state.
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_RotateTowardsLocalStations() const
    { return _LocalStations; }

    UFUNCTION(BlueprintPure) int32 Get_RotateTowardsShownSequence() const
    { return _ShownSequence; }

    UFUNCTION(BlueprintPure) bool Get_RotateTowardsPresentationReady()
    {
        auto State = CkRotateTowardsShared::Get_State(GetWorld());
        if (!IsLocalController() || ck::Is_NOT_Valid(State) || State != _BoundState ||
            _Ending || _PresentationLost || OwnerSessionFailed ||
            _LocalFailed || _LocalStarting || _ShownSequence <= 0 ||
            !State.Get_IsCompleteSnapshot(State.Snapshot) || !State.Get_IsCompleteSnapshot(_InstalledLiveSample) ||
            _InstalledLiveSample.Generation != State.Snapshot.Generation || _InstalledLiveSample.Sequence != _ShownSequence ||
            _ShownSequence != State.Snapshot.Sequence ||
            FInstancedStruct::Make(_InstalledLiveSample)!=FInstancedStruct::Make(State.Snapshot))
        { return false; }
        auto Pawn=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (!_PresentationBound || ck::Is_NOT_Valid(Pawn) || Pawn!=_PresentationPawn ||
            Pawn.GetWorld()!=GetWorld() || Pawn.GetController()!=this) { return false; }
        if (HasAuthority()) { return _LocalStations.IsEmpty() && State.Get_IsReady(); }
        return _LocalGeneration == _InstalledLiveSample.Generation &&
            Get_HasExactLocalCohort(_InstalledLiveSample) && Get_HasExactInstalledVisuals(_InstalledLiveSample);
    }

    private bool _DrawTargets = false;
    private ACk_Gym_Base_Pawn _PresentationPawn;
    private bool _PresentationBound=false, _PresentationLost=false;
    private bool _Ending = false;
    private bool _OwnerMonitorArmed = false;
    private int32 _LocalGeneration = 0;
    private int32 _ShownSequence = 0;
    UPROPERTY() private FCkRotateTowardsGym_Snapshot _InstalledLiveSample;
    UFUNCTION(BlueprintPure) FCkRotateTowardsGym_Snapshot Get_InstalledRotateTowardsSample() const
    { return _InstalledLiveSample; }
    private ACk_RotateTowardsGym_GameState _BoundState;
    private bool _PresentationArmed = false;
    private bool _LocalStarting = false;
    private bool _LocalFailed = false;
    private float _LocalStartedAt = 0.0f;
    UPROPERTY() private FCkRotateTowardsGym_StationCohort _StationCohort;
    UPROPERTY() private FCkRotateTowardsGym_VisualCohort _VisualCohort;
    private FCk_Handle _LocalOwner, _LocalWorldRoot, _LocalParent;
    private TArray<FCk_Handle> _LocalStations;
    private bool _LocalCreating=false, _LocalRetiring=false;
    private bool _LocalRetirementUnproven=false;
    private FString _DeferredLocalFailure;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    { return TArray<FCkGym_Station_SpawnParams_Payload>(); }

    void Request_StartGym() override
    {
        auto State = CkRotateTowardsShared::Get_State(GetWorld());
        if (HasAuthority() && ck::IsValid(State) && State.Get_HasPublishedCatalog())
        {
            State.Request_EnsureSharedDemo();
            if (!_OwnerMonitorArmed)
            { _OwnerMonitorArmed = true; System::SetTimer(this, n"OnOwnerContext", 0.1, true); }
        }
        if (IsLocalController() && !_PresentationArmed)
        {
            _PresentationArmed = true;
            System::SetTimer(this, n"OnPresentationTick", 0.1, true);
        }
    }

    UFUNCTION() private void OnOwnerContext()
    {
        if (!HasAuthority() || OwnerSessionFailed ||
            (_ReservedSerial == 0 && LastControlSerial == 0)) { return; }
        auto State = CkRotateTowardsShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || State != _SessionState ||
            !State.Get_IsValidOwner(this, _SessionPawn))
        {
            OwnerSessionFailed = true;
            if (_ReservedSerial > 0)
            { Request_RejectRotateTowardsControl(_ReservedSerial, 2); }
            ForceNetUpdate();
        }
    }

    FString Get_ControlPanelTitle() override
    { return "ROTATE TOWARDS"; }

    UFUNCTION(BlueprintPure)
    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Action(EKeys::X,"X","Reset all stations",!OwnerSessionFailed && _PendingSerial==0));
        Rows.Add(CkGym_Control::Header("ROTATE TOWARDS"));
        Rows.Add(CkGym_Control::Status("State",_LocalFailed || OwnerSessionFailed ? "Owner presentation retired" : "Watch each turret against its target",_LocalStarting));
        auto S=_InstalledLiveSample;
        if (S.Ready && !S.Failed && S.Turrets.Num()==7)
        {
            for (int32 i=0;i<3;++i)
            {
                int32 TurretIndex = i == 0 ? 0 : i == 1 ? 2 : 3;
                auto R=S.Turrets[TurretIndex]; FTransform Pose; FVector Target, Rest;
                if (!TryGet_TurretPresentation(S.Generation, TurretIndex, Pose, Target, Rest)) { continue; }
                auto Yaw=Pose.Rotator().Yaw;
                Rows.Add(CkGym_Control::Status(f"{CkRotateTowardsShared::Get_StationTag(i)}",f"yaw {Yaw :.1} deg, rem {R.Remaining.Yaw :.1}, "+(R.AtTarget?"at target":"turning")));
            }
        }
        Rows.Add(CkGym_Control::Action(EKeys::One,"1","View Tracker"));
        Rows.Add(CkGym_Control::Action(EKeys::Two,"2","View Clamped"));
        Rows.Add(CkGym_Control::Action(EKeys::Three,"3","View Modes"));
        Rows.Add(CkGym_Control::Toggle(EKeys::O,"O","Draw targets (this player)",_DrawTargets));
        if (_InstalledControlCommit.DeliverySerial>0)
        { Rows.Add(CkGym_Control::Status("Own rebuild delivery",f"Request {_InstalledControlCommit.RequestSerial}, generation {_InstalledControlCommit.Snapshot.Generation}")); }
        if (LastControlAttemptSerial>LastControlSerial && LastControlResult>1)
        { Rows.Add(CkGym_Control::Status("Own request result",f"Request {LastControlAttemptSerial}: "+(LastControlResult==2?"Owner context gone":"Rejected"),true)); }
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || OwnerSessionFailed || _Ending) { return; }
        auto Rows=Get_ControlRows(); if (!Rows.IsValidIndex(InRowIndex)) { return; }
        auto Key=Rows[InRowIndex].Key;
        if (Key==EKeys::O) { _DrawTargets=!_DrawTargets; return; }
        if (Key==EKeys::One) { Request_Frame(0); return; }
        if (Key==EKeys::Two) { Request_Frame(1); return; }
        if (Key==EKeys::Three) { Request_Frame(2); return; }
        if (Key!=EKeys::X || _PendingSerial!=0) { return; }
        auto State = CkRotateTowardsShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || !State.Snapshot.Ready || State.Snapshot.Failed) { return; }
        auto High = Math::Max(_RequestedSerial,
            Math::Max(LastControlAttemptSerial, LastControlSerial));
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        _PendingSerial = _RequestedSerial;
        Server_RequestRotateTowardsRebuild(_RequestedSerial, State.Snapshot.Generation);
    }

    UFUNCTION(Server)
    void Server_RequestRotateTowardsRebuild(int32 InSerial, int32 InGeneration)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlAttemptSerial ||
            InSerial <= LastControlSerial) { return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        auto LocalWorld = GetWorld();
        if (ck::Is_NOT_Valid(LocalWorld))
        { LastControlResult = 3; ForceNetUpdate(); return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        auto State = CkRotateTowardsShared::Get_State(LocalWorld);
        TArray<ACk_RotateTowardsGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_RotateTowardsGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (OwnerSessionFailed || ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            ck::Is_NOT_Valid(LocalWorld) || Pawn.GetWorld() != LocalWorld ||
            ck::Is_NOT_Valid(State) || State.GetWorld() != LocalWorld ||
            LocalWorld.GetGameState() != State || State.GetClass() != ACk_RotateTowardsGym_GameState ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) || Modes[0].GetWorld() != LocalWorld ||
            !State.Get_HasPublishedCatalog() || !State.Get_IsReady() ||
            Current < 0 || Current >= Registry.Num() ||
            Registry[Current].DisplayName != "Rotate Towards" ||
            InGeneration != State.Snapshot.Generation || _ReservedSerial != 0 ||
            !Get_CanAcceptOwnerCommit() ||
            (_SessionBound && (_SessionPawn != Pawn || _SessionState != State)))
        { LastControlResult = 3; ForceNetUpdate(); return; }
        _SessionPawn = Pawn; _SessionState = State; _SessionBound = true;
        _ReservedState = State;
        _ReservedSerial = InSerial;
        _ReservedGeneration = InGeneration;
        if (!State.Request_Rebuild(this, Pawn, InSerial, InGeneration))
        {
            Request_RejectRotateTowardsControl(InSerial, 3);
        }
    }

    bool Get_HasReservedRotateTowardsControl(int32 InSerial, int32 InGeneration,
        ACk_Gym_Base_Pawn InPawn) const
    {
        return HasAuthority() && !OwnerSessionFailed && InSerial > 0 &&
            InGeneration > 0 && _SessionPawn == InPawn && ck::IsValid(_ReservedState) &&
            _ReservedState == _SessionState && _ReservedState.GetWorld() == GetWorld() &&
            _ReservedSerial == InSerial && _ReservedGeneration == InGeneration;
    }

    int32 Get_NextDeliverySerial() const
    { return HasAuthority() && _NextDeliverySerial < 2147483646 ? _NextDeliverySerial + 1 : 0; }

    bool Get_CanAcceptOwnerCommit()
    {
        return HasAuthority() && !OwnerSessionFailed && _NextDeliverySerial < 2147483646 &&
            (LastControlCommit.DeliverySerial == 0 ||
             LastAcknowledgedDeliverySerial == LastControlCommit.DeliverySerial);
    }

    void Request_RejectRotateTowardsControl(int32 InSerial, int32 InResult)
    {
        if (!HasAuthority() || InSerial <= 0 || InSerial <= LastControlSerial ||
            InResult < 2 || InResult > 4) { return; }
        if (_ReservedSerial == InSerial)
        { _ReservedSerial = 0; _ReservedGeneration = 0; _ReservedState = nullptr; }
        if (LastControlAttemptSerial == InSerial) { LastControlResult = InResult; }
        ForceNetUpdate();
    }

    bool Request_DetachRotateTowardsControl(ACk_RotateTowardsGym_GameState InState,
        ACk_Gym_Base_Pawn InPawn, int32 InSerial, int32 InGeneration)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(InState) ||
            InState.GetWorld() != GetWorld() || InState != _ReservedState ||
            InState != _SessionState || _SessionPawn != InPawn ||
            _ReservedSerial != InSerial || _ReservedGeneration != InGeneration ||
            InSerial <= 0 || InGeneration <= 0 ||
            (InState.Get_HasCapturedOwnerContext(this, InPawn) && !OwnerSessionFailed)) { return false; }
        OwnerSessionFailed = true;
        Request_RejectRotateTowardsControl(InSerial, 2);
        ForceNetUpdate();
        return true;
    }

    bool Request_TryRecordRotateTowardsCommit(ACk_RotateTowardsGym_GameState InState,
        int32 InSerial)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(InState) ||
            InState.GetWorld() != GetWorld() || InState != _ReservedState ||
            InState != _SessionState || InSerial <= LastControlSerial ||
            !Get_CanAcceptOwnerCommit()) { return false; }
        auto LocalWorld = GetWorld();
        if (ck::Is_NOT_Valid(LocalWorld) || LocalWorld.GetGameState() != InState ||
            !InState.Get_IsValidOwner(this, _SessionPawn)) { return false; }
        FCkRotateTowardsGym_ControlCommit Commit;
        if (!InState.Request_TryGetPreparedRotateTowardsCommit(this, InSerial, Commit) ||
            Commit.RequestSerial != InSerial || Commit.OriginState != InState ||
            Commit.Owner != this || Commit.OwnerPawn != _SessionPawn ||
            Commit.OriginGeneration != _ReservedGeneration ||
            Commit.DeliverySerial != Get_NextDeliverySerial() ||
            !Get_HasReservedRotateTowardsControl(InSerial, _ReservedGeneration, _SessionPawn) ||
            !InState.Get_IsCompleteSnapshot(Commit.Snapshot)) { return false; }
        _NextDeliverySerial = Commit.DeliverySerial;
        LastControlCommit = Commit;
        LastControlSerial = InSerial;
        _CommitPawn = _SessionPawn; _CommitState = InState;
        _ReservedSerial = 0; _ReservedGeneration = 0; _ReservedState = nullptr;
        if (LastControlAttemptSerial == InSerial) { LastControlResult = 1; }
        ForceNetUpdate();
        return true;
    }

    UFUNCTION(Server) void Server_AcknowledgeRotateTowardsCommit(
        ACk_RotateTowardsGym_GameState InState, ACk_Gym_Base_Pawn InPawn,
        int32 InDelivery, int32 InSerial)
    {
        if (!HasAuthority() || OwnerSessionFailed ||
            InDelivery <= LastAcknowledgedDeliverySerial ||
            InDelivery != LastControlCommit.DeliverySerial ||
            InSerial != LastControlCommit.RequestSerial ||
            InSerial != LastControlSerial) { return; }
        auto State = CkRotateTowardsShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || ck::Is_NOT_Valid(InState) ||
            InState != State || State != _CommitState || State != _SessionState ||
            LastControlCommit.OriginState != State || LastControlCommit.Owner != this ||
            InPawn != _CommitPawn || LastControlCommit.OwnerPawn != InPawn ||
            !State.Get_IsValidOwner(this, InPawn)) { return; }
        LastAcknowledgedDeliverySerial = InDelivery;
        ForceNetUpdate();
    }

    private void Request_RetireLocal()
    {
        if (_LocalCreating)
        { if (_DeferredLocalFailure.IsEmpty()) { _DeferredLocalFailure="RotateTowards local context retired during creation"; } return; }
        _LocalStarting=false; _ShownSequence=0;
        _InstalledLiveSample=FCkRotateTowardsGym_Snapshot();
        if (_StationCohort.Get_Phase()==0 && _VisualCohort.Get_Phase()==0) { return; }
        _LocalRetiring=true;
        if (!_StationCohort.Request_CloseKnownPresentation()) { _LocalRetirementUnproven=true; }
        // Each cohort has its own disjoint owner and frozen creator ledger.
        _VisualCohort.Request_Retire(); _StationCohort.Request_Retire();
        if (_VisualCohort.Get_IsRetirementUnproven() || _StationCohort.Get_IsRetirementUnproven())
        { _LocalRetirementUnproven=true; }
    }
    private bool Get_LocalRetired()
    {
        if (_LocalRetirementUnproven) { return false; }
        if (!_LocalRetiring) { return true; }
        bool VisualGone=_VisualCohort.PollRetirement();
        bool StationsGone=_StationCohort.PollRetirement();
        if (_VisualCohort.Get_IsRetirementUnproven() || _StationCohort.Get_IsRetirementUnproven())
        { _LocalRetirementUnproven=true; return false; }
        if (!VisualGone || !StationsGone) { return false; }
        // Each cohort clears its creator records only after its own all-gone proof.
        _LocalStations.Empty(); _LocalOwner=FCk_Handle();
        _LocalWorldRoot=FCk_Handle(); _LocalParent=FCk_Handle();
        _DeferredLocalFailure="";
        _LocalRetiring=false; return true;
    }
    private void Request_StartLocal(FCkRotateTowardsGym_Snapshot InSample)
    {
        auto State=CkRotateTowardsShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || State!=_BoundState || !IsLocalController() || HasAuthority() ||
            _LocalCreating || _LocalStarting || !Get_LocalRetired() ||
            !State.Get_IsCompleteSnapshot(InSample)) { return; }
        auto WorldRoot=ck::TransientEntity(); auto Parent=utils_owning_actor::TryGet_ActorEntityHandle(this);
        if (!CkRotateTowardsShared::IsLive(WorldRoot) || !CkRotateTowardsShared::IsLive(Parent) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Parent))
        { Request_FailLocal("RotateTowards local controller context unavailable"); return; }
        TArray<FCk_GymStation_SpawnParams> Recipes;
        for (int32 i=0;i<3;++i)
        {
            auto P=InSample.PassiveStations[i];
            // Completeness above validates the received source frame. Local geometry
            // is proved only from the actual constructed station readback below.
            if (P.AutoSize || !CkRotateTowardsShared::Get_HasFiniteRecipe(P,i))
            { Request_FailLocal("RotateTowards passive catalog rejected"); return; }
            Recipes.Add(P);
        }
        _LocalWorldRoot=WorldRoot; _LocalParent=Parent; _LocalGeneration=InSample.Generation;
        _LocalStarting=true; _LocalStartedAt=float(System::GetGameTimeInSeconds());
        _LocalCreating=true;
        auto Begin=_StationCohort.TryBeginLocal(WorldRoot,Parent,Recipes,
            FCk_Delegate_Request_OnCompleted(this,n"OnLocalSpawnCompleted"),
            FCk_Delegate_EntityScript_Constructed(this,n"OnLocalConstructed"));
        _LocalOwner=_StationCohort.Get_CohortOwner(); _LocalStations=_StationCohort.Stations;
        _LocalCreating=false;
        if (!_DeferredLocalFailure.IsEmpty()) { Request_FailLocal(_DeferredLocalFailure); return; }
        if (Begin!=ECkGym_SharedStationBegin::Started)
        { Request_FailLocal(f"RotateTowards passive station admission rejected: {_StationCohort.Get_LastAdmissionRejection()}"); }
    }
    UFUNCTION() private void OnLocalSpawnCompleted(FCk_Handle H, ECk_Request_OperationResult Result)
    {
        if (_Ending || _LocalFailed || !_LocalStarting) { return; }
        if (!_StationCohort.TryRecord_Completed(H,H,Result))
        { Request_FailLocal("RotateTowards passive station completion rejected"); }
    }
    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript H)
    {
        if (_Ending || _LocalFailed || !_LocalStarting) { return; }
        if (!_StationCohort.TryRecord_Constructed(FCk_Handle(H),H))
        { Request_FailLocal("RotateTowards passive station construction rejected"); }
    }
    private void Request_FailLocal(FString Reason)
    {
        if (_LocalCreating)
        { if (_DeferredLocalFailure.IsEmpty()) { _DeferredLocalFailure=Reason; } return; }
        _LocalFailed=true; Request_RetireLocal();
        ck::Error("[RotateTowardsShared] "+Reason);
    }

    UFUNCTION() private void OnPresentationTick()
    {
        if (_Ending || _PresentationLost || !IsLocalController() || OwnerSessionFailed) { Request_RetireLocal(); _InstalledControlCommit = FCkRotateTowardsGym_ControlCommit(); return; }
        auto ActualPawn=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(ActualPawn) || ActualPawn.GetWorld()!=GetWorld() || ActualPawn.GetController()!=this ||
            (_PresentationBound && _PresentationPawn!=ActualPawn))
        { if (!_PresentationBound) { return; } _PresentationLost=true; Request_RetireLocal(); _InstalledControlCommit=FCkRotateTowardsGym_ControlCommit(); _LocalFailed=true; return; }
        if (!_PresentationBound) { _PresentationPawn=ActualPawn; _PresentationBound=true; }
        if (_PendingSerial > 0 && LastControlAttemptSerial == _PendingSerial &&
            LastControlResult > 1) { _PendingSerial = 0; }
        auto State = CkRotateTowardsShared::Get_State(GetWorld());
        auto LocalWorld = GetWorld();
        if (ck::Is_NOT_Valid(LocalWorld) || ck::Is_NOT_Valid(State) ||
            State.GetWorld() != LocalWorld || LocalWorld.GetGameState() != State ||
            State.GetClass() != ACk_RotateTowardsGym_GameState || !State.Get_HasPublishedCatalog())
        {
            Request_RetireLocal(); _BoundState = nullptr; _LocalGeneration = 0;
            return;
        }
        if (_BoundState != State)
        {
            Request_RetireLocal();
            _BoundState = State;
            _LocalGeneration = 0;
            _LocalFailed = false;
        }
        // Owner outcome is installed independently of the latest shared geometry's generation/readiness.
        Request_ConsumeCommit(State);
        auto Sample = State.Snapshot;
        if (Sample.Failed || !Sample.Ready ||
            !State.Get_IsCompleteSnapshot(Sample))
        { Request_RetireLocal(); return; }
        if (HasAuthority())
        {
            if (!State.Get_IsReady()) { return; }
            _InstalledLiveSample = Sample; _ShownSequence = Sample.Sequence;
            if (_FramedGeneration!=Sample.Generation) { _FramedGeneration=Sample.Generation; Request_Frame(0); }
            return;
        }
        if (_LocalGeneration != Sample.Generation)
        {
            Request_RetireLocal();
            _LocalGeneration = Sample.Generation;
            _LocalFailed = false;
        }
        if (_LocalFailed || !Get_LocalRetired()) { return; }
        if (!CkRotateTowardsShared::IsLive(_LocalOwner))
        { Request_StartLocal(Sample); return; }
        if (_LocalStarting)
        {
            if (float(System::GetGameTimeInSeconds()) - _LocalStartedAt > 20.0f)
            { Request_FailLocal("RotateTowards local station readiness timed out"); return; }
            auto Poll=_StationCohort.PollConstruction();
            _LocalStations=_StationCohort.Stations;
            if (Poll==ECkGym_SharedStationPoll::Failed)
            { Request_FailLocal("RotateTowards passive station proof rejected"); return; }
            if (Poll!=ECkGym_SharedStationPoll::Ready) { return; }
            if (!Get_HasExactLocalIdentity(Sample)) { return; }
            // Geometry callbacks and the deferred display fragment both precede local Ready.
            if (!Get_HasExactLocalCohort(Sample)) { return; }
            if (_VisualCohort.Get_Phase()==0) { if (!Request_CreateVisuals(Sample)) { Request_FailLocal("RotateTowards projection allocation rejected"); } return; }
            if (!Get_HasExactVisuals()) { return; }
            _LocalStarting = false;
        }
        if (!Get_HasExactLocalCohort(Sample) || !Get_HasExactVisuals())
        { Request_FailLocal("RotateTowards local station/visual cohort lost"); return; }
        if (_ShownSequence >= Sample.Sequence || Sample.Sequence <= 0) { return; }
        bool Installed=_VisualCohort.TryInstall(Sample);
        bool LocalStillExact=Installed && Get_HasExactLocalCohort(Sample);
        bool VisualsStillExact=LocalStillExact && Get_HasExactInstalledVisuals(Sample);
        if (!VisualsStillExact)
        {
            FString Detail=!Installed ? _VisualCohort.Get_LastInstallFailure() :
                !LocalStillExact ? "post-install-local-cohort" : "post-install-visual-readback";
            Request_FailLocal("RotateTowards whole projection installation rejected: "+Detail);
            return;
        }
        // Station text stays at its original authored value. Each owner's HUD reads this sample.
        _InstalledLiveSample = Sample; _ShownSequence = Sample.Sequence;
        if (_FramedGeneration!=Sample.Generation) { _FramedGeneration=Sample.Generation; Request_Frame(0); }
    }

    private void Request_ConsumeCommit(ACk_RotateTowardsGym_GameState InState)
    {
        auto Commit = LastControlCommit;
        if (OwnerSessionFailed || Commit.DeliverySerial <= _AppliedDeliverySerial ||
            Commit.DeliverySerial <= LastAcknowledgedDeliverySerial || Commit.RequestSerial <= 0 ||
            Commit.OriginGeneration <= 0 || Commit.OriginGeneration >= 2147483646 ||
            Commit.Snapshot.Generation != Commit.OriginGeneration + 1 ||
            ck::Is_NOT_Valid(InState) || Commit.OriginState != InState || Commit.Owner != this)
        { return; }
        auto LocalWorld = GetWorld();
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(LocalWorld) || LocalWorld.GetGameState() != InState ||
            InState.GetWorld() != LocalWorld || InState.GetClass() != ACk_RotateTowardsGym_GameState ||
            !InState.Get_HasPublishedCatalog() || ck::Is_NOT_Valid(Pawn) ||
            Commit.OwnerPawn != Pawn || Pawn.GetWorld() != LocalWorld || Pawn.GetController() != this ||
            !InState.Get_IsCompleteSnapshot(Commit.Snapshot)) { return; }
        // This whole typed cache is the real per-owner HUD consumer. Shared shells stay on live GS state.
        _InstalledControlCommit = Commit;
        _AppliedDeliverySerial = Commit.DeliverySerial;
        if (_PendingSerial == Commit.RequestSerial) { _PendingSerial = 0; }
        Server_AcknowledgeRotateTowardsCommit(InState, Pawn, Commit.DeliverySerial, Commit.RequestSerial);
    }

    private bool Get_HasExactLocalIdentity(FCkRotateTowardsGym_Snapshot InSample)
    {
        auto Pawn=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (!IsLocalController() || HasAuthority() || _LocalRetiring || _LocalRetirementUnproven ||
            ck::Is_NOT_Valid(Pawn) || Pawn!=_PresentationPawn || Pawn.GetWorld()!=GetWorld() ||
            Pawn.GetController()!=this || _LocalWorldRoot!=ck::TransientEntity() ||
            _LocalParent!=utils_owning_actor::TryGet_ActorEntityHandle(this) || !CkRotateTowardsShared::IsLive(_LocalParent) ||
            _LocalGeneration!=InSample.Generation || InSample.Visuals.Num()!=11 ||
            InSample.Turrets.Num()!=7 || InSample.CatalogVersion!=CkRotateTowardsShared::CatalogVersion ||
            _LocalOwner!=_StationCohort.Get_CohortOwner() || _LocalStations.Num()!=3 ||
            !_StationCohort.Get_HasExactReadyCohort()) { return false; }
        for (int32 i=0;i<3;++i)
        {
            auto H=_LocalStations[i];
            if (H!=_StationCohort.Stations[i] || !CkRotateTowardsShared::IsLive(H) ||
                !utils_entity_lifetime::Has_LifetimeOwner(H) ||
                utils_entity_lifetime::Get_LifetimeOwner(H)!=_LocalOwner ||
                !CkRotateTowardsShared::Get_IsExactStationScript(H)) { return false; }
        }
        return true;
    }

    private bool Get_HasExactLocalCohort(FCkRotateTowardsGym_Snapshot InSample)
    {
        if (!Get_HasExactLocalIdentity(InSample) || InSample.PassiveStations.Num()!=3 ||
            InSample.StationGeometry.Num()!=3) { return false; }
        for (int32 i=0;i<3;++i)
        {
            FCkGym_StationVisibleGeometry LocalGeometry;
            if (!_LocalStations[i].Has_Fragment(FCkGym_Station_TitleAndDescription) ||
                !CkRotateTowardsShared::TryCapture_PassiveStationPresentation(
                    _LocalStations[i],_LocalOwner,i,InSample,LocalGeometry))
            { return false; }
        }
        return true;
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        _Ending=true; _DrawTargets=false; Request_RetireLocal();
        Super::EndPlay(InReason);
    }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle_UnrealComponent> Get_LocalVisualComponents() const
    { return _VisualCohort.Components; }
    private int32 _FramedGeneration=0;
    UFUNCTION(BlueprintPure) bool Get_DrawTargetsForPlayer() const { return _DrawTargets; }
    private bool Request_CreateVisuals(FCkRotateTowardsGym_Snapshot Sample)
    {
        if (_LocalCreating || _LocalFailed || _LocalRetiring ||
            !Get_HasExactLocalCohort(Sample)) { return false; }
        _LocalCreating=true;
        bool Created=_VisualCohort.TryBegin(GetWorld(),_LocalParent,Sample,
            FCk_Delegate_Request_OnCompleted(this,n"OnLocalVisualPushDisabled"),
            FCk_Delegate_UnrealComponent_OnAdded(this,n"OnRotateTowardsGymCubeAdded"));
        _LocalCreating=false;
        if (!_DeferredLocalFailure.IsEmpty()) { Request_FailLocal(_DeferredLocalFailure); return false; }
        return Created;
    }
    UFUNCTION() private void OnLocalVisualPushDisabled(FCk_Handle H,ECk_Request_OperationResult Result)
    {
        if (_Ending || _LocalFailed || !_LocalStarting) { return; }
        if (!_VisualCohort.TryRecord_PushDisabled(H,Result))
        { Request_FailLocal("RotateTowards passive transform-push receipt rejected"); }
    }
    UFUNCTION() private void OnRotateTowardsGymCubeAdded(FCk_Handle_UnrealComponent H)
    {
        if (_Ending || _LocalFailed || !_LocalStarting) { return; }
        if (!_VisualCohort.TryRecord_Added(H))
        { Request_FailLocal("RotateTowards passive mesh receipt rejected"); }
    }
    private bool Get_HasExactVisuals() { return _VisualCohort.Get_IsReady(); }
    void Request_Frame(int32 Index)
    {
        auto Pawn=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn()); auto S=_InstalledLiveSample;
        if (!IsLocalController() || ck::Is_NOT_Valid(Pawn) || Pawn.GetWorld()!=GetWorld() || Pawn.GetController()!=this ||
            _LocalFailed || !S.Ready || S.Failed || Index<0 || Index>=S.Origins.Num()) { return; }
        auto Target=S.Origins[Index]+FVector(200,0,0); auto Eye=Target+FVector(-700,-900,650);
        Pawn.SetActorLocation(Eye); SetControlRotation((Target-Eye).Rotation());
    }
    // Derive gameplay pose from the replicated visible nodes and authored offsets.
    bool TryGet_TurretPresentation(int32 Generation, int32 TurretIndex,
        FTransform&out Pose, FVector&out Target, FVector&out Rest)
    {
        Pose=FTransform(); Target=FVector::ZeroVector; Rest=FVector::ZeroVector;
        if (TurretIndex < 0 || TurretIndex >= 7 || Generation <= 0) { return false; }
        int32 Slot = TurretIndex < 2 ? TurretIndex + 1 : TurretIndex == 2 ? 5 : TurretIndex + 4;
        int32 Station = TurretIndex < 2 ? 0 : TurretIndex == 2 ? 1 : 2;
        auto Cube = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(GetWorld(),Generation,Slot);
        auto TargetCube = UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(GetWorld(),Generation,Station*3);
        if (ck::Is_NOT_Valid(Cube) || ck::Is_NOT_Valid(TargetCube)) { return false; }
        auto CubePose=utils_transform::Get_EntityCurrentTransform(Cube.As_Transform());
        auto Rotation=CubePose.Rotator();
        Pose=FTransform(Rotation,CubePose.GetTranslation()-Rotation.Vector()*40.0,FVector(1,1,1));
        Target=utils_transform::Get_EntityCurrentLocation(TargetCube.As_Transform());
        if (Station==1)
        {
            auto RestCube=UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(GetWorld(),Generation,4);
            if (ck::Is_NOT_Valid(RestCube)) { return false; }
            Rest=utils_transform::Get_EntityCurrentLocation(RestCube.As_Transform());
        }
        return true;
    }

    // HUD-only read: no installation, acknowledgement, request or world draw side effect.
    bool TryGet_TargetOverlaySample(FCkRotateTowardsGym_Snapshot&out Out)
    {
        Out=FCkRotateTowardsGym_Snapshot();
        if (!_DrawTargets || _Ending || _PresentationLost || !_PresentationBound ||
            !IsLocalController() || !Get_RotateTowardsPresentationReady()) { return false; }
        auto Pawn=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn!=_PresentationPawn || Pawn.GetWorld()!=GetWorld() ||
            Pawn.GetController()!=this || _InstalledLiveSample.Sequence!=_ShownSequence)
        { return false; }
        Out=_InstalledLiveSample; return true;
    }
    private bool Get_HasExactInstalledVisuals(FCkRotateTowardsGym_Snapshot Sample)
    {
        return _VisualCohort.Get_HasExactInstalledVisuals(Sample);
    }
    UFUNCTION(Exec) void Ck_RotateTowards_Control(int32 Index) { Request_ControlActivated(Index); }
}
