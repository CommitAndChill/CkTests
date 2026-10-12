// Language=angelscript

class ACk_EntityLifecycleGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    UPROPERTY(Replicated) int32 LastAcknowledgedDeliverySerial = 0;
    UPROPERTY(Replicated) FCkLifecycleGym_ControlCommit LastControlCommit;
    UPROPERTY(Replicated) bool OwnerSessionFailed = false;
    UPROPERTY() bool LocalPresentationReady = false;
    UPROPERTY() bool LocalPresentationFailed = false;
    UPROPERTY() FString LocalPresentationFailureReason;
    UPROPERTY() int32 LocalPresentationSequence = 0;
    private int32 _RequestedSerial = 0;
    private int32 _PendingSerial = 0;
    private int32 _ReservedSerial = 0;
    private int32 _ReservedRow = 0;
    private int32 _ReservedGeneration = 0;
    private int32 _NextDelivery = 0;
    private int32 _AppliedDelivery = 0;
    private bool _Armed = false;
    private int32 _WaitAttempts = 0;
    private ACk_Gym_Base_Pawn _CommitPawn;
    private ACk_Gym_Base_Pawn _SessionPawn;
    private bool _OwnerMonitorArmed = false;
    private float _PresentationStart = 0.0f;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkLifecycleGym::Get_Stations();
    }
    void Request_StartGym() override
    {
        auto State = CkLifecycleGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_HasPublishedCatalog())
        {
            if (_WaitAttempts++ < 450) { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority())
        {
            State.Request_EnsureSharedDemo();
            if (!_OwnerMonitorArmed) { _OwnerMonitorArmed = true; System::SetTimer(this, n"OnOwnerContext", 0.1, true); }
        }
        if (IsLocalController() && !_Armed) { _Armed = true; _PresentationStart = float(System::GetGameTimeInSeconds()); System::SetTimer(this, n"OnPresentationTick", 0.1, true); }
    }
    UFUNCTION() private void OnStateWait() { Request_StartGym(); }
    UFUNCTION() private void OnOwnerContext()
    {
        if (!HasAuthority() || OwnerSessionFailed || (_ReservedSerial == 0 && LastControlSerial == 0)) { return; }
        auto State = CkLifecycleGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_IsValidOwner(this, _SessionPawn))
        {
            OwnerSessionFailed = true;
            if (_ReservedSerial > 0) { Request_RejectControl(_ReservedSerial, 2); }
            ForceNetUpdate();
        }
    }

    private bool ApplySnapshot(FCkLifecycleGym_Snapshot S)
    {
        auto State = CkLifecycleGym::Get_State();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() || !State.Get_IsCompleteSnapshot(S) ||
            !State.Get_IsCompleteSnapshot(State.Snapshot) ||
            !CkLifecycleGym::IsLive(State.Get_LocalVerifiedProbeRoot(State.Snapshot.Rows[3].BranchGeneration))) { return false; }
        auto Declarations = CkLifecycleGym::Get_Stations();
        TArray<FCk_Handle> Panels;
        if (Declarations.Num() != 7) { return false; }
        for (int32 i = 0; i < 7; ++i)
        {
            auto H = CkLifecycleGym::FindStation(Declarations[i].Tags[0]);
            if (!CkLifecycleGym::IsLive(H) || !H.Has_Fragment(FCkGym_Station_Anchors)) { return false; }
            if (!HasAuthority() && utils_entity_tag::ForEach_Entity(ck::TransientEntity(), CkLifecycleGym::ChildTag(i)).Num() != 0) { return false; }
            Panels.Add(H);
        }
        for (int32 i = 0; i < 7; ++i)
        {
            auto& D = Panels[i].AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            D.Title = FText::FromString(S.Rows[i].Title);
            D.Description = FText::FromString(S.Rows[i].Body);
            D.Instructions = FText::FromString(S.Rows[i].Instructions);
        }
        LocalPresentationReady = true; LocalPresentationSequence = S.Sequence; return true;
    }
    UFUNCTION() private void OnPresentationTick()
    {
        if (!IsLocalController() || LocalPresentationFailed) { return; }
        if (OwnerSessionFailed) { _PendingSerial = 0; }
        auto State = CkLifecycleGym::Get_State();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld())
        { LocalPresentationReady = false; LocalPresentationFailed = true; LocalPresentationFailureReason = "Lifecycle local world context lost"; _PendingSerial = 0; return; }
        if (State.Snapshot.Failed)
        { _PendingSerial = 0; LocalPresentationReady = false; LocalPresentationFailed = true; LocalPresentationFailureReason = State.Snapshot.FailureReason; return; }
        if (!LocalPresentationReady && float(System::GetGameTimeInSeconds()) - _PresentationStart > 45.0f)
        { LocalPresentationFailed = true; LocalPresentationFailureReason = "Actual lifecycle presentation/probe replica unavailable"; _PendingSerial = 0; return; }
        if (_PendingSerial > 0 && LastControlAttemptSerial == _PendingSerial && LastControlResult > 1) { _PendingSerial = 0; }
        auto Commit = LastControlCommit;
        // Retained commit is applied and acknowledged independently of latest epoch.
        if (!OwnerSessionFailed && Commit.DeliverySerial > _AppliedDelivery && Commit.DeliverySerial > LastAcknowledgedDeliverySerial &&
            Commit.RequestSerial > 0 && Commit.Snapshot.Generation == State.Snapshot.Generation)
        {
            if (!ApplySnapshot(Commit.Snapshot)) { return; }
            _AppliedDelivery = Commit.DeliverySerial;
            if (_PendingSerial == Commit.RequestSerial) { _PendingSerial = 0; }
            Server_AcknowledgeCommit(Commit.DeliverySerial, Commit.RequestSerial);
        }
        if (State.Snapshot.Ready && State.Snapshot.Sequence > LocalPresentationSequence)
        { ApplySnapshot(State.Snapshot); }
    }

    FString Get_ControlPanelTitle() override { return "ENTITY LIFECYCLE"; }
	TArray<FCkGym_ControlRow> Get_ControlRows() override
	{
		auto Rows = TArray<FCkGym_ControlRow>();

		Rows.Add(CkGym_Control::Header("RE-RUN A STATION"));
		Rows.Add(CkGym_Control::Numbered(0, "Handle & entity", false));
		Rows.Add(CkGym_Control::Numbered(1, "Ownership tree", false));
		Rows.Add(CkGym_Control::Numbered(2, "Destroy callbacks", false));
		Rows.Add(CkGym_Control::Numbered(3, "Actor bridge", false));
		Rows.Add(CkGym_Control::Numbered(4, "Tag system", false));
		Rows.Add(CkGym_Control::Numbered(5, "Deferred setup", false));
		Rows.Add(CkGym_Control::Numbered(6, "Script spawn cast", false));
		Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Re-run every station"));

        auto Feedback = OwnerSessionFailed ? "Owner session retired" : LocalPresentationFailed ? LocalPresentationFailureReason :
            (_PendingSerial > 0 ? "Waiting for shared rerun" :
             (LastControlResult == 1 ? "Shared rerun accepted" :
              (LastControlResult == 2 ? "Owner context rejected" :
               (LastControlResult == 3 ? "Shared cohort busy or unavailable" :
                (LastControlResult == 4 ? "Invalid control" : "Ready")))));
        Rows.Add(CkGym_Control::Status("Owner request", Feedback, OwnerSessionFailed || LastControlResult > 1 || LocalPresentationFailed));
        return Rows;
	}


    void Request_ControlActivated(int32 Row) override
    {
        if (!IsLocalController() || OwnerSessionFailed || _PendingSerial > 0 || Row < 1 || Row > 8 || _RequestedSerial >= 2147483646) { return; }
        auto State = CkLifecycleGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_IsCompleteSnapshot(State.Snapshot)) { return; }
        _RequestedSerial++; _PendingSerial = _RequestedSerial;
        Server_RequestLifecycleControl(_RequestedSerial, Row, State.Snapshot.Generation);
    }
    UFUNCTION(Server) void Server_RequestLifecycleControl(int32 Serial, int32 Row, int32 Generation)
    {
        if (!HasAuthority() || Serial <= 0 || Serial <= LastControlAttemptSerial || Serial <= LastControlSerial) { return; }
        LastControlAttemptSerial = Serial; LastControlResult = 0;
        if (OwnerSessionFailed) { LastControlResult = 2; ForceNetUpdate(); return; }
        if (Row < 1 || Row > 8 || Generation <= 0) { LastControlResult = 4; ForceNetUpdate(); return; }
        auto State = CkLifecycleGym::Get_State();
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(State) || !State.Get_IsValidOwner(this, Pawn) || State.Snapshot.Generation != Generation)
        { LastControlResult = 2; ForceNetUpdate(); return; }
        if (_ReservedSerial > 0 || !Get_CanAcceptOwnerCommit()) { LastControlResult = 3; ForceNetUpdate(); return; }
        _ReservedSerial = Serial; _ReservedRow = Row; _ReservedGeneration = Generation;
        _SessionPawn = Pawn;
        int32 Result = State.Request_Rerun(this, Serial, Row, Generation);
        if (Result != 1) { Request_RejectControl(Serial, Result); }
    }
    bool Get_IsReserved(int32 Serial, int32 Row, int32 Generation)
    { return HasAuthority() && !OwnerSessionFailed && Serial > 0 && Serial == _ReservedSerial && Row == _ReservedRow && Generation > 0 && Generation == _ReservedGeneration; }
    bool Get_CanAcceptOwnerCommit()
    { return HasAuthority() && !OwnerSessionFailed && _NextDelivery < 2147483646 && (LastControlCommit.DeliverySerial == 0 || LastAcknowledgedDeliverySerial == LastControlCommit.DeliverySerial); }
    int32 Get_NextDeliverySerial() const { return HasAuthority() && _NextDelivery < 2147483646 ? _NextDelivery + 1 : 0; }
    void Request_RejectControl(int32 Serial, int32 Result)
    {
        if (!HasAuthority() || Serial <= 0 || Result < 2 || Result > 4) { return; }
        if (Serial == _ReservedSerial) { _ReservedSerial = 0; _ReservedRow = 0; _ReservedGeneration = 0; }
        if (LastControlAttemptSerial == Serial) { LastControlResult = Result; ForceNetUpdate(); }
    }
    bool Request_RetireCapturedOwnerContext(ACk_EntityLifecycleGym_GameState State, ACk_Gym_Base_Pawn Pawn,
        int32 Serial, int32 Row, int32 Generation)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() ||
            ck::Is_NOT_Valid(GetWorld()) || GetWorld().GetGameState() != State || _SessionPawn != Pawn ||
            !State.Get_IsCapturedOwnerTransaction(this, Pawn, Serial, Row, Generation)) { return false; }
        if (!OwnerSessionFailed && State.Get_IsValidOwner(this, Pawn)) { return false; }
        if (!OwnerSessionFailed && !Get_IsReserved(Serial, Row, Generation)) { return false; }
        if (OwnerSessionFailed && _ReservedSerial != 0 &&
            (_ReservedSerial != Serial || _ReservedRow != Row || _ReservedGeneration != Generation)) { return false; }
        OwnerSessionFailed = true; Request_RejectControl(Serial, 2); ForceNetUpdate(); return true;
    }
    bool Request_TryRecordCommit(ACk_EntityLifecycleGym_GameState State, int32 Serial)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() ||
            Serial <= LastControlSerial || !Get_CanAcceptOwnerCommit()) { return false; }
        auto W = GetWorld();
        if (ck::Is_NOT_Valid(W) || W.GetGameState() != State) { return false; }
        FCkLifecycleGym_ControlCommit C;
        if (!State.Request_TryGetPreparedCommit(this, Serial, C) || !Get_IsReserved(Serial, C.Row, C.Snapshot.Generation) ||
            C.DeliverySerial != Get_NextDeliverySerial() || !State.Get_IsCompleteSnapshot(C.Snapshot)) { return false; }
        _NextDelivery = C.DeliverySerial; LastControlCommit = C; LastControlSerial = Serial;
        _CommitPawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        _ReservedSerial = 0; _ReservedRow = 0; _ReservedGeneration = 0;
        if (LastControlAttemptSerial == Serial) { LastControlResult = 1; }
        ForceNetUpdate(); return true;
    }
    UFUNCTION(Server) void Server_AcknowledgeCommit(int32 Delivery, int32 Serial)
    {
        if (!HasAuthority() || OwnerSessionFailed || Delivery <= LastAcknowledgedDeliverySerial || Delivery != LastControlCommit.DeliverySerial ||
            Serial != LastControlCommit.RequestSerial || Serial != LastControlSerial) { return; }
        auto State = CkLifecycleGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_IsValidOwner(this, _CommitPawn)) { return; }
        LastAcknowledgedDeliverySerial = Delivery; ForceNetUpdate();
    }
}
