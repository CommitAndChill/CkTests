// Language=angelscript

class ACk_MessagingGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    UPROPERTY(Replicated) FCkMessagingGym_ControlCommit LastControlCommit;
    UPROPERTY(Replicated) int32 LastAcknowledgedDeliverySerial = 0;

    private int32 _AlertPresetIndex = -1;
    private int32 _RequestedSerial = 0;
    private int32 _NextDeliverySerial = 0;
    private int32 _ServerAlertPresetIndex = -1;
    private int32 _SubmittingSerial = 0;
    private int32 _ReservedSerial = 0;
    private int32 _ReservedRow = -1;
    private int32 _ReservedPriority = 0;
    private int32 _ReservedGeneration = 0;
    private int32 _PendingSerial = 0;
    private int32 _ShownGeneration = 0;
    private int32 _ShownSequence = 0;
    private int32 _AppliedDeliverySerial = 0;
    private int32 _WaitAttempts = 0;
    private bool _PresentationArmed = false;

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    {
        if (HasAuthority()) { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
        return CkMessagingGym::Get_Stations();
    }

    void Request_StartGym() override
    {
        auto State = CkMessagingGym::Get_State();
        if (ck::Is_NOT_Valid(State))
        {
            if ((HasAuthority() || IsLocalController()) && _WaitAttempts++ < 450)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
            return;
        }
        if (HasAuthority())
        {
            if (State.Get_HasPublishedCatalog()) { State.Request_EnsureSharedDemo(); }
            else if (_WaitAttempts++ < 450)
            { System::SetTimer(this, n"OnStateWait", 0.1, false); }
        }
        if (IsLocalController() && !_PresentationArmed)
        {
            _PresentationArmed = true;
            System::SetTimer(this, n"OnPresentationTick", 0.1, true);
        }
    }

    UFUNCTION() private void OnStateWait() { Request_StartGym(); }

    private bool Request_PreflightLocalStations()
    {
        auto State = CkMessagingGym::Get_State();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld()) { return false; }
        for (int32 i = 0; i < 5; i++)
        {
            auto Tag = CkMessagingGym::Get_StationTag(i);
            auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), Tag);
            if (Matches.Num() != 1 || !CkMessagingGym::IsLive(Matches[0]) ||
                !Matches[0].Has_Fragment(FCkGym_Station_Anchors)) { return false; }
            if (!HasAuthority() &&
                utils_entity_tag::ForEach_Entity(Matches[0],
                    CkMessagingGym::Get_ChildTag(i)).Num() != 0) { return false; }
        }
        return true;
    }

    private bool Request_ApplySnapshot(FCkMessagingGym_Snapshot InSnapshot)
    {
        auto State = CkMessagingGym::Get_State();
        if (ck::Is_NOT_Valid(State) || !State.Get_IsCompleteSnapshot(InSnapshot) ||
            !Request_PreflightLocalStations()) { return false; }
        for (int32 i = 0; i < 5; i++)
        {
            auto Sample = InSnapshot.Stations[i];
            auto Display = FCkGym_Station_TitleAndDescription();
            Display.Title = FText::FromString(Sample.Title);
            Display.Description = FText::FromString(Sample.Body);
            Display.Instructions = FText::FromString(Sample.Instructions);
            Set_StationTitleAndDescription(
                CkMessagingGym::Get_StationTag(i).ToString(), Display);
        }
        _ShownGeneration = InSnapshot.Generation;
        _ShownSequence = InSnapshot.Sequence;
        return true;
    }

    UFUNCTION() private void OnPresentationTick()
    {
        if (!IsLocalController()) { return; }
        auto State = CkMessagingGym::Get_State();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld()) { return; }
        if (State.Snapshot.Failed)
        { _PendingSerial = 0; return; }
        if (_PendingSerial > 0 && LastControlAttemptSerial == _PendingSerial &&
            LastControlResult > 1)
        { _PendingSerial = 0; }
        // The complete owner commit is the source for acknowledged feedback even
        // when a later actor property has skipped the committed Snapshot sequence.
        auto Commit = LastControlCommit;
        if (Commit.DeliverySerial > _AppliedDeliverySerial &&
            Commit.DeliverySerial > LastAcknowledgedDeliverySerial &&
            Commit.RequestSerial > 0 && Commit.Snapshot.Ready &&
            Commit.Snapshot.Generation == State.Snapshot.Generation &&
            State.Get_IsCompleteSnapshot(Commit.Snapshot))
        {
            if (!Request_ApplySnapshot(Commit.Snapshot)) { return; }
            _AppliedDeliverySerial = Commit.DeliverySerial;
            if (Commit.Row == 6)
            {
                if (Commit.AlertPriority == 1) { _AlertPresetIndex = 0; }
                if (Commit.AlertPriority == 5) { _AlertPresetIndex = 1; }
                if (Commit.AlertPriority == 9) { _AlertPresetIndex = 2; }
            }
            _PendingSerial = 0;
            Server_AcknowledgeMessagingCommit(Commit.DeliverySerial, Commit.RequestSerial);
            return;
        }
        if (Commit.DeliverySerial > 0 &&
            Commit.DeliverySerial == _AppliedDeliverySerial &&
            Commit.DeliverySerial > LastAcknowledgedDeliverySerial)
        { Server_AcknowledgeMessagingCommit(Commit.DeliverySerial, Commit.RequestSerial); }
        auto Latest = State.Snapshot;
        if (Latest.Ready && !Latest.Failed &&
            (Latest.Generation != _ShownGeneration || Latest.Sequence > _ShownSequence))
        { Request_ApplySnapshot(Latest); }
    }

    FString Get_ControlPanelTitle() override { return "MESSAGING"; }

    private int32 Get_AlertPriority(int32 InIndex) const
    { return InIndex == 0 ? 1 : InIndex == 1 ? 5 : 9; }

    TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        TArray<FCkGym_ControlRow> Rows;
        Rows.Add(CkGym_Control::Header("SEND"));
        Rows.Add(CkGym_Control::Numbered(0, "Ping", false));
        Rows.Add(CkGym_Control::Numbered(1, "Fan-out ping", false));
        Rows.Add(CkGym_Control::Numbered(2, "One-shot", false));
        Rows.Add(CkGym_Control::Numbered(3, "Ping to dynamic", false));
        Rows.Add(CkGym_Control::Numbered(4, "Pong", false));
        auto Alert = CkGym_Control::Numbered(5, "Alert", false);
        Alert.Value = _AlertPresetIndex < 0 ? "(1 / 5 / 9)" :
            f"priority {Get_AlertPriority(_AlertPresetIndex)}";
        Rows.Add(Alert);
        Rows.Add(CkGym_Control::Numbered(6, "All types at once", false));
        Rows.Add(CkGym_Control::Header("BINDINGS"));
        Rows.Add(CkGym_Control::Action(EKeys::B, "B", "Flip the dynamic bind"));
        Rows.Add(CkGym_Control::Action(EKeys::R, "R", "Reset every station"));
        return Rows;
    }

    void Request_ControlActivated(int32 InRowIndex) override
    {
        if (!IsLocalController() || _PendingSerial > 0 ||
            (InRowIndex != 1 && InRowIndex != 2 && InRowIndex != 3 &&
             InRowIndex != 4 && InRowIndex != 5 && InRowIndex != 6 &&
             InRowIndex != 7 && InRowIndex != 9 && InRowIndex != 10)) { return; }
        auto High = Math::Max(_RequestedSerial,
            Math::Max(LastControlAttemptSerial, LastControlSerial));
        if (High >= 2147483647) { return; }
        _RequestedSerial = High + 1;
        _PendingSerial = _RequestedSerial;
        int32 Priority = 0;
        if (InRowIndex == 6)
        {
            auto Index = (_AlertPresetIndex + 1) % 3;
            Priority = Get_AlertPriority(Index);
        }
        Server_RequestMessagingControl(_RequestedSerial, InRowIndex, Priority);
    }

    UFUNCTION(Server)
    void Server_RequestMessagingControl(int32 InSerial, int32 InRow, int32 InPriority)
    {
        if (!HasAuthority()) { return; }
        if (InSerial <= 0 || InSerial <= LastControlAttemptSerial ||
            InSerial <= LastControlSerial) { return; }
        LastControlAttemptSerial = InSerial;
        LastControlResult = 0;
        if ((InRow != 1 && InRow != 2 && InRow != 3 && InRow != 4 &&
             InRow != 5 && InRow != 6 && InRow != 7 && InRow != 9 && InRow != 10) ||
            (InRow != 6 && InPriority != 0) ||
            (InRow == 6 && InPriority != 1 && InPriority != 5 && InPriority != 9))
        { LastControlResult = 4; return; }
        if (InRow == 6 && InPriority != Get_AlertPriority((_ServerAlertPresetIndex + 1) % 3))
        { LastControlResult = 4; return; }
        auto Pawn = Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Pawn) || Pawn.GetController() != this ||
            Pawn.GetWorld() != GetWorld())
        { LastControlResult = 2; return; }
        auto State = CkMessagingGym::Get_State();
        TArray<ACk_MessagingGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_MessagingGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != GetWorld() ||
            Modes.Num() != 1 || ck::Is_NOT_Valid(Modes[0]) ||
            Modes[0].GetWorld() != GetWorld() ||
            !State.Get_HasPublishedCatalog() || !State.Get_HasReadyServerCohort() ||
            Current < 0 || Current >= Registry.Num() ||
            Registry[Current].DisplayName != "Messaging")
        { LastControlResult = 3; return; }
        if (!Get_CanAcceptOwnerCommit() || _ReservedSerial > 0)
        { LastControlResult = 3; return; }
        _ReservedSerial = InSerial;
        _ReservedRow = InRow;
        _ReservedPriority = InPriority;
        _ReservedGeneration = State.Snapshot.Generation;
        _SubmittingSerial = InSerial;
        auto Admission = State.Request_QueueControl(this, InSerial, InRow, InPriority);
        _SubmittingSerial = 0;
        if (Admission != 1)
        { Request_ClearReservation(InSerial); LastControlResult = Admission; }
    }

    bool Get_IsSubmittingMessagingControl(int32 InSerial) const
    { return HasAuthority() && InSerial > 0 && _SubmittingSerial == InSerial; }

    bool Get_HasReservedMessagingControl(int32 InSerial, int32 InRow,
        int32 InPriority, int32 InGeneration) const
    {
        return HasAuthority() && InSerial > 0 && _ReservedSerial == InSerial &&
            _ReservedRow == InRow && _ReservedPriority == InPriority &&
            _ReservedGeneration == InGeneration && InGeneration > 0;
    }

    private void Request_ClearReservation(int32 InSerial)
    {
        if (_ReservedSerial != InSerial) { return; }
        _ReservedSerial = 0;
        _ReservedRow = -1;
        _ReservedPriority = 0;
        _ReservedGeneration = 0;
    }

    void Request_RejectQueuedMessagingControl(int32 InSerial, int32 InResult)
    {
        if (!HasAuthority() || InResult < 2 || InResult > 4 ||
            InSerial <= LastControlSerial) { return; }
        Request_ClearReservation(InSerial);
        if (LastControlAttemptSerial == InSerial)
        { LastControlResult = InResult; ForceNetUpdate(); }
    }

    int32 Get_NextDeliverySerial() const
    {
        if (!HasAuthority() || _NextDeliverySerial >= 2147483646) { return 0; }
        return _NextDeliverySerial + 1;
    }

    bool Get_CanAcceptOwnerCommit()
    {
        return HasAuthority() && _NextDeliverySerial < 2147483646 &&
            (LastControlCommit.DeliverySerial == 0 ||
             LastAcknowledgedDeliverySerial == LastControlCommit.DeliverySerial);
    }

    bool Request_TryRecordCommit(ACk_MessagingGym_GameState InState, int32 InSerial)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(InState) || InSerial <= 0 ||
            InSerial <= LastControlSerial || !Get_CanAcceptOwnerCommit() ||
            InState.GetWorld() != GetWorld()) { return false; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) || World.GetGameState() != InState ||
            InState.GetClass() != ACk_MessagingGym_GameState) { return false; }
        auto InCommit = FCkMessagingGym_ControlCommit();
        if (!InState.Request_TryGetPreparedOwnerCommit(this, InSerial, InCommit) ||
            InCommit.RequestSerial != InSerial ||
            InCommit.DeliverySerial != Get_NextDeliverySerial() ||
            !Get_HasReservedMessagingControl(InSerial, InCommit.Row,
                InCommit.AlertPriority, InCommit.Snapshot.Generation) ||
            !InState.Get_IsCompleteSnapshot(InCommit.Snapshot) ||
            (InCommit.Row != 1 && InCommit.Row != 2 && InCommit.Row != 3 &&
             InCommit.Row != 4 && InCommit.Row != 5 && InCommit.Row != 6 &&
             InCommit.Row != 7 && InCommit.Row != 9 && InCommit.Row != 10) ||
            (InCommit.Row != 6 && InCommit.AlertPriority != 0) ||
            (InCommit.Row == 6 && InCommit.AlertPriority !=
                Get_AlertPriority((_ServerAlertPresetIndex + 1) % 3))) { return false; }
        _NextDeliverySerial = InCommit.DeliverySerial;
        LastControlCommit = InCommit;
        LastControlSerial = InCommit.RequestSerial;
        if (InCommit.Row == 6) { _ServerAlertPresetIndex = (_ServerAlertPresetIndex + 1) % 3; }
        Request_ClearReservation(InSerial);
        if (LastControlAttemptSerial == InCommit.RequestSerial)
        { LastControlResult = 1; }
        ForceNetUpdate();
        return true;
    }

    UFUNCTION(Server)
    void Server_AcknowledgeMessagingCommit(int32 InDeliverySerial, int32 InRequestSerial)
    {
        if (!HasAuthority() || InDeliverySerial <= LastAcknowledgedDeliverySerial ||
            InDeliverySerial != LastControlCommit.DeliverySerial ||
            InRequestSerial != LastControlCommit.RequestSerial ||
            InRequestSerial != LastControlSerial) { return; }
        LastAcknowledgedDeliverySerial = InDeliverySerial;
        ForceNetUpdate();
    }
}
