// Language=angelscript

//============================================================================
// OBJECT POOLING GYM - PLAYER CONTROLLER (stress stations)
//============================================================================
//
// Two sustained-churn stations that stress the ObjectPooling subsystem and
// show its live stats:
//
//   SCRIPT CHURN - every tick spawns a wave of poolable EntityScripts and
//   destroys the wave from kWaveDepth ticks ago. Steady state keeps
//   (kWaveSize * kWaveDepth) scripts in flight with kWaveSize spawns AND
//   destroys per tick. Healthy pooling = LIVE plateaus while HITS climbs
//   forever and MISSES stays frozen at the initial ramp.
//
//   SM CHURN - a herd of entities each running a Ping<->Pong StateMachine
//   whose event-driven condition re-arms a world timer on every transition.
//   Every kSmRespawnPeriod seconds a slice of the herd is destroyed and
//   respawned, recycling the SM state/condition scripts under load.
//
// Panel: [B] burst 100 acquires, [R] restart the gym
//============================================================================

// Gym-only looping SM: Ping <-> Pong forever, both hops gated on the same
// short-delay event-driven condition the autotests use
UCLASS()
class UCk_PoolSmGym_State_Ping : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Trans = AddTransition(InHandle, UCk_PoolSmGym_State_Pong);
        AddCondition(Trans, UCk_PoolSmTest_Condition_ShortDelay);
    }
};

UCLASS()
class UCk_PoolSmGym_State_Pong : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Trans = AddTransition(InHandle, UCk_PoolSmGym_State_Ping);
        AddCondition(Trans, UCk_PoolSmTest_Condition_ShortDelay);
    }
};

// Local station projection and owner-only control delivery. Churn lives only on the GameState.
class ACk_ObjectPoolingGym_PlayerController : ACk_Gym_Base_PlayerController
{
    UPROPERTY(Replicated) int32 LastControlAttemptSerial = 0;
    UPROPERTY(Replicated) int32 LastControlSerial = 0;
    UPROPERTY(Replicated) int32 LastControlResult = 0;
    UPROPERTY(Replicated) int32 LastAcknowledgedDeliverySerial = 0;
    UPROPERTY(Replicated) FCkPoolingGym_ControlCommit LastControlCommit;
    UPROPERTY(Replicated) bool OwnerSessionFailed = false;
    UPROPERTY(Replicated) bool OwnerProjectionRetired = false;
    UPROPERTY(Replicated) int32 LastProjectionRejectedSerial = 0;
    UPROPERTY(Replicated) int32 LastRejectedAckAttemptCount = 0;
    UPROPERTY(Replicated) int32 LastRejectedAckDelivery = 0;
    UPROPERTY(Replicated) int32 LastRejectedAckRequestSerial = 0;
    UPROPERTY(Replicated) int32 LastRejectedAckReason = 0;

    private int32 _RequestedSerial = 0, _PendingSerial = 0;
    private int32 _ReservedSerial = 0, _ReservedGeneration = 0, _ReservedAction = -1;
    private int32 _NextDeliverySerial = 0, _AppliedDeliverySerial = 0;
    private ACk_Gym_Base_Pawn _SessionPawn, _CommitPawn;
    private ACk_ObjectPoolingGym_GameState _SessionState, _ReservedState, _CommitState;
    private bool _SessionBound = false, _OwnerMonitorArmed = false, _PresentationArmed = false;
    private bool _EverPresented = false;
    private float _PresentationStartedAt = 0.0f;
    UPROPERTY() private FCkPoolingGym_ControlCommit _InstalledControlCommit;
    private FCkPoolingGym_CommitAdmissionAudit _LastCommitAdmissionAudit;
    UPROPERTY() private FCkPoolingGym_Snapshot _InstalledLiveSample;
    UPROPERTY() private TArray<FCk_Handle> _LastAcceptedBurstTargets;
    UPROPERTY() private TArray<int32> _LastAcceptedBurstConstructed;
    UPROPERTY() private TArray<int32> _LastAcceptedBurstCompleted;
    UPROPERTY() int32 LastAcceptedBurstSerial = 0;
    UPROPERTY() int32 LastAcceptedBurstOriginGeneration = 0;
    private FCkGym_SharedStationCohort _LocalCohort;
    private TArray<FCk_Handle> _LocalStations;
    private ACk_ObjectPoolingGym_GameState _BoundState;
    private ACk_Gym_Base_Pawn _BoundPawn;
    private int32 _ShownSequence = 0;
    private bool _LocalStarting = false, _LocalFailed = false;
    private float _LocalStartedAt = 0.0f;

    UFUNCTION(BlueprintPure) FCkPoolingGym_ControlCommit Get_InstalledPoolingCommit() const
    { return _InstalledControlCommit; }
    UFUNCTION(BlueprintPure) FCkPoolingGym_CommitAdmissionAudit Get_LastCommitAdmissionAudit() const
    { return _LastCommitAdmissionAudit; }
    UFUNCTION(BlueprintPure) FCkPoolingGym_Snapshot Get_InstalledPoolingSample() const
    { return _InstalledLiveSample; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_LastAcceptedBurstTargets() const
    { return _LastAcceptedBurstTargets; }
    UFUNCTION(BlueprintPure) TArray<int32> Get_LastAcceptedBurstConstructed() const
    { return _LastAcceptedBurstConstructed; }
    UFUNCTION(BlueprintPure) TArray<int32> Get_LastAcceptedBurstCompleted() const
    { return _LastAcceptedBurstCompleted; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_PoolingLocalStations() const
    { return _LocalStations; }
    UFUNCTION(BlueprintPure) int32 Get_PoolingShownSequence() const
    { return _ShownSequence; }
    UFUNCTION(BlueprintPure) bool Get_IsLocalRetirementUnproven() const
    { return _LocalCohort.Get_IsRetirementUnproven(); }
    UFUNCTION(BlueprintPure) bool Get_PoolingPresentationReady()
    {
        auto State=CkPoolingShared::Get_State(GetWorld());
        if (!IsLocalController() || ck::Is_NOT_Valid(State) || State!=_BoundState ||
            OwnerProjectionRetired || _LocalFailed || _LocalStarting || _ShownSequence<=0 ||
            !State.Get_IsCompleteSnapshot(State.Snapshot) ||
            !State.Get_IsCompleteSnapshot(_InstalledLiveSample) ||
            _InstalledLiveSample.Generation!=State.Snapshot.Generation ||
            _InstalledLiveSample.Sequence!=_ShownSequence ||
            _ShownSequence>State.Snapshot.Sequence)
        { return false; }
        if (HasAuthority())
        {
            if (!_LocalStations.IsEmpty() || !State.Get_IsReady() || State.ServerStations.Num()!=2) { return false; }
            for (int32 i=0;i<2;++i)
            {
                auto H=State.ServerStations[i];
                if (!CkPoolingShared::IsLive(H) || !H.Has_Fragment(FCkGym_Station_TitleAndDescription)) { return false; }
                auto Text=H.Get_Fragment(FCkGym_Station_TitleAndDescription);
                if (Text.Title.ToString()!=_InstalledLiveSample.Titles[i] ||
                    Text.Description.ToString()!=_InstalledLiveSample.Bodies[i]) { return false; }
            }
            return true;
        }
        FString IgnoredReason;
        if (!Get_HasExactLocalCohort(State.Snapshot,IgnoredReason)) { return false; }
        for (int32 i=0;i<2;++i)
        {
            auto Text=_LocalStations[i].Get_Fragment(FCkGym_Station_TitleAndDescription);
            if (Text.Title.ToString()!=_InstalledLiveSample.Titles[i] ||
                Text.Description.ToString()!=_InstalledLiveSample.Bodies[i]) { return false; }
        }
        return true;
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_RequiredStations() override
    { return TArray<FCkGym_Station_SpawnParams_Payload>(); }
    void Request_StartGym() override
    {
        auto State=CkPoolingShared::Get_State(GetWorld());
        if (HasAuthority() && ck::IsValid(State) && State.Get_HasPublishedCatalog())
        {
            State.Request_EnsureSharedDemo();
            if (!_OwnerMonitorArmed)
            { _OwnerMonitorArmed=true; System::SetTimer(this,n"OnOwnerContext",0.1f,true); }
        }
        if (IsLocalController() && !_PresentationArmed)
        {
            _PresentationArmed=true; _PresentationStartedAt=float(System::GetGameTimeInSeconds());
            System::SetTimer(this,n"OnPresentationTick",0.1f,true);
        }
    }
    UFUNCTION() private void OnOwnerContext()
    {
        if (!HasAuthority() || OwnerSessionFailed || (_ReservedSerial==0 && LastControlSerial==0)) { return; }
        auto State=CkPoolingShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || State!=_SessionState || !State.Get_IsValidOwner(this,_SessionPawn))
        {
            OwnerSessionFailed=true;
            if (_ReservedSerial>0) { Request_RejectPoolingControl(_ReservedSerial,2); }
            ForceNetUpdate();
        }
    }

    FString Get_ControlPanelTitle() override { return "OBJECT POOLING"; }
    UFUNCTION(BlueprintPure) TArray<FCkGym_ControlRow> Get_ControlRows() override
    {
        auto Rows=TArray<FCkGym_ControlRow>();
        bool Enabled=!OwnerSessionFailed && !OwnerProjectionRetired && !_LocalFailed && _PendingSerial==0;
        Rows.Add(CkGym_Control::Action(EKeys::B,"B","Burst 100 acquires",Enabled));
        Rows.Add(CkGym_Control::Action(EKeys::R,"R","Restart the gym",Enabled));
        auto Installed=_InstalledControlCommit;
        if (Installed.DeliverySerial>0)
        {
            Rows.Add(CkGym_Control::Status("Own control delivery",
                f"Action {Installed.Action}, request {Installed.RequestSerial}, delivery {Installed.DeliverySerial}"));
            Rows.Add(CkGym_Control::Status("Installed simulation generation",
                f"{Installed.Snapshot.Generation}, sequence {Installed.Snapshot.Sequence}"));
        }
        if (OwnerSessionFailed || OwnerProjectionRetired || _LocalFailed)
        { Rows.Add(CkGym_Control::Status("Owner session","Retired",true)); }
        return Rows;
    }
    void Request_ControlActivated(int32 Row) override
    {
        if (!IsLocalController() || OwnerSessionFailed || OwnerProjectionRetired || _LocalFailed ||
            (Row!=0 && Row!=1) || _PendingSerial!=0) { return; }
        auto State=CkPoolingShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || !State.Get_IsCompleteSnapshot(State.Snapshot)) { return; }
        int32 High=Math::Max(_RequestedSerial,Math::Max(LastControlAttemptSerial,LastControlSerial));
        if (High>=2147483646) { return; }
        _RequestedSerial=High+1; _PendingSerial=_RequestedSerial;
        Server_RequestPoolingControl(_RequestedSerial,State.Snapshot.Generation,Row);
    }
    UFUNCTION(Server) void Server_RequestPoolingControl(int32 Serial,int32 Generation,int32 Action)
    {
        if (!HasAuthority()) { return; }
        if (OwnerProjectionRetired)
        {
            if (Serial>0 && Serial>LastProjectionRejectedSerial)
            { LastProjectionRejectedSerial=Serial; ForceNetUpdate(); }
            return;
        }
        if (Serial<=0 || Serial<=LastControlAttemptSerial ||
            Serial<=LastControlSerial) { return; }
        LastControlAttemptSerial=Serial; LastControlResult=0;
        auto World=GetWorld();
        auto Pawn=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        auto State=CkPoolingShared::Get_State(World);
        if (OwnerSessionFailed || ck::Is_NOT_Valid(World) || ck::Is_NOT_Valid(Pawn) ||
            Pawn.GetWorld()!=World || Pawn.GetController()!=this || ck::Is_NOT_Valid(State) ||
            State.GetWorld()!=World || World.GetGameState()!=State ||
            State.GetClass()!=ACk_ObjectPoolingGym_GameState ||
            !State.Get_IsReady() || Generation!=State.Snapshot.Generation ||
            (Action!=0 && Action!=1) || _ReservedSerial!=0 || !Get_CanAcceptOwnerCommit() ||
            (_SessionBound && (_SessionPawn!=Pawn || _SessionState!=State)))
        { LastControlResult=3; ForceNetUpdate(); return; }
        _SessionPawn=Pawn; _SessionState=State; _SessionBound=true;
        _ReservedState=State; _ReservedSerial=Serial; _ReservedGeneration=Generation; _ReservedAction=Action;
        if (!State.Request_Control(this,Pawn,Serial,Generation,Action))
        { Request_RejectPoolingControl(Serial,3); }
    }
    // Owner-only terminal report. It cannot poison an already reserved shared
    // action or alter the source simulation; only future controls are rejected.
    UFUNCTION(Server) void Server_ReportPoolingProjectionRetired(
        ACk_ObjectPoolingGym_GameState State,ACk_Gym_Base_Pawn Pawn)
    {
        if (!HasAuthority() || OwnerProjectionRetired || ck::Is_NOT_Valid(State) ||
            ck::Is_NOT_Valid(Pawn)) { return; }
        auto World=GetWorld(); auto Actual=CkPoolingShared::Get_State(World);
        auto Controlled=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(World) || ck::Is_NOT_Valid(Actual) || State!=Actual ||
            Actual.GetWorld()!=World || World.GetGameState()!=Actual ||
            Actual.GetClass()!=ACk_ObjectPoolingGym_GameState ||
            ck::Is_NOT_Valid(Controlled) || Pawn!=Controlled || Pawn.GetWorld()!=World ||
            Pawn.GetController()!=this ||
            (_SessionBound && (_SessionState!=Actual || _SessionPawn!=Pawn))) { return; }
        OwnerProjectionRetired=true; ForceNetUpdate();
    }
    bool Get_HasReservedPoolingControl(int32 Serial,int32 Generation,int32 Action,ACk_Gym_Base_Pawn Pawn) const
    {
        return HasAuthority() && !OwnerSessionFailed && Serial>0 && Generation>0 &&
            (Action==0 || Action==1) && _ReservedSerial==Serial &&
            _ReservedGeneration==Generation && _ReservedAction==Action &&
            _SessionPawn==Pawn && ck::IsValid(_ReservedState) &&
            _ReservedState==_SessionState && _ReservedState.GetWorld()==GetWorld();
    }
    int32 Get_NextDeliverySerial() const
    { return HasAuthority() && _NextDeliverySerial<2147483646 ? _NextDeliverySerial+1 : 0; }
    bool Get_CanAcceptOwnerCommit()
    {
        return HasAuthority() && !OwnerSessionFailed && _NextDeliverySerial<2147483646 &&
            (LastControlCommit.DeliverySerial==0 ||
             LastAcknowledgedDeliverySerial==LastControlCommit.DeliverySerial);
    }
    void Request_RejectPoolingControl(int32 Serial,int32 Result)
    {
        if (!HasAuthority() || Serial<=0 || Serial<=LastControlSerial || Result<2 || Result>4) { return; }
        if (_ReservedSerial==Serial)
        { _ReservedSerial=0; _ReservedGeneration=0; _ReservedAction=-1; _ReservedState=nullptr; }
        if (LastControlAttemptSerial==Serial) { LastControlResult=Result; }
        ForceNetUpdate();
    }
    bool Request_TryRecordPoolingCommit(ACk_ObjectPoolingGym_GameState State,int32 Serial)
    {
        if (!HasAuthority() || ck::Is_NOT_Valid(State) || State.GetWorld()!=GetWorld() ||
            State!=_ReservedState || State!=_SessionState || Serial<=LastControlSerial ||
            !Get_CanAcceptOwnerCommit() || !State.Get_IsValidOwner(this,_SessionPawn)) { return false; }
        FCkPoolingGym_ControlCommit Commit;
        if (!State.Request_TryGetPreparedPoolingCommit(this,Serial,Commit) ||
            Commit.RequestSerial!=Serial || Commit.OriginState!=State || Commit.Owner!=this ||
            Commit.OwnerPawn!=_SessionPawn || Commit.Action!=_ReservedAction ||
            Commit.OriginGeneration!=_ReservedGeneration ||
            Commit.Snapshot.Generation!=_ReservedGeneration+(_ReservedAction==1 ? 1 : 0) ||
            Commit.DeliverySerial!=Get_NextDeliverySerial() ||
            (_ReservedAction==0 && (Commit.BurstIssued!=100 || Commit.BurstConstructed!=100 || Commit.BurstCompleted!=100)) ||
            (_ReservedAction==1 && (Commit.BurstIssued!=0 || Commit.BurstConstructed!=0 || Commit.BurstCompleted!=0)) ||
            !State.Get_IsCompleteSnapshot(Commit.Snapshot) ||
            !Get_HasReservedPoolingControl(Serial,_ReservedGeneration,_ReservedAction,_SessionPawn)) { return false; }
        TArray<FCk_Handle> Batch;
        TArray<int32> BatchConstructed, BatchCompleted;
        if (_ReservedAction==0)
        {
            if (!State.Request_TryGetPreparedBurstEvidence(this,Serial,Batch,BatchConstructed,BatchCompleted) ||
                Batch.Num()!=100 || BatchConstructed.Num()!=100 || BatchCompleted.Num()!=100) { return false; }
            for (int32 i=0;i<Batch.Num();++i)
            { if (BatchConstructed[i]!=1 || BatchCompleted[i]!=1) { return false; }
              for (int32 j=0;j<i;++j) { if (Batch[i]==Batch[j]) { return false; } } }
        }
        if (Commit.DeliverySerial<=LastAcknowledgedDeliverySerial) { return false; }
        _NextDeliverySerial=Commit.DeliverySerial;
        LastControlCommit=Commit; LastControlSerial=Serial;
        if (_ReservedAction==0)
        {
            _LastAcceptedBurstTargets=Batch;
            _LastAcceptedBurstConstructed=BatchConstructed;
            _LastAcceptedBurstCompleted=BatchCompleted;
            LastAcceptedBurstSerial=Serial;
            LastAcceptedBurstOriginGeneration=_ReservedGeneration;
        }
        _CommitPawn=_SessionPawn; _CommitState=State;
        _ReservedSerial=0; _ReservedGeneration=0; _ReservedAction=-1; _ReservedState=nullptr;
        if (LastControlAttemptSerial==Serial) { LastControlResult=1; }
        FCkPoolingGym_CommitAdmissionAudit Audit;
        Audit.Observed=true; Audit.RequestSerial=Serial; Audit.DeliverySerial=Commit.DeliverySerial;
        Audit.AcknowledgedAtPublish=LastAcknowledgedDeliverySerial;
        Audit.CanAcceptAnother=Get_CanAcceptOwnerCommit();
        _LastCommitAdmissionAudit=Audit;
        ForceNetUpdate(); return true;
    }
    UFUNCTION(Server) void Server_AcknowledgePoolingCommit(
        ACk_ObjectPoolingGym_GameState State,ACk_Gym_Base_Pawn Pawn,int32 Delivery,int32 Serial)
    {
        if (!HasAuthority()) { return; }
        if (OwnerSessionFailed) { Request_RecordRejectedAck(Delivery,Serial,1); return; }
        if (Delivery<=LastAcknowledgedDeliverySerial) { Request_RecordRejectedAck(Delivery,Serial,2); return; }
        if (Delivery!=LastControlCommit.DeliverySerial || Serial!=LastControlCommit.RequestSerial ||
            Serial!=LastControlSerial) { Request_RecordRejectedAck(Delivery,Serial,3); return; }
        auto Actual=CkPoolingShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(Actual) || ck::Is_NOT_Valid(State) || State!=Actual ||
            State!=_CommitState || State!=_SessionState || LastControlCommit.OriginState!=State ||
            LastControlCommit.Owner!=this || Pawn!=_CommitPawn || LastControlCommit.OwnerPawn!=Pawn ||
            !State.Get_IsValidOwner(this,Pawn))
        { Request_RecordRejectedAck(Delivery,Serial,4); return; }
        LastAcknowledgedDeliverySerial=Delivery; ForceNetUpdate();
    }
    private void Request_RecordRejectedAck(int32 Delivery,int32 Serial,int32 Reason)
    {
        if (!HasAuthority() || Reason<1 || Reason>4) { return; }
        if (LastRejectedAckAttemptCount<2147483646) { ++LastRejectedAckAttemptCount; }
        LastRejectedAckDelivery=Delivery; LastRejectedAckRequestSerial=Serial;
        LastRejectedAckReason=Reason; ForceNetUpdate();
    }

    private void Request_ConsumeCommit(ACk_ObjectPoolingGym_GameState State)
    {
        auto Commit=LastControlCommit;
        if (OwnerSessionFailed || Commit.DeliverySerial<=_AppliedDeliverySerial ||
            Commit.DeliverySerial<=LastAcknowledgedDeliverySerial || Commit.RequestSerial<=0 ||
            Commit.OriginGeneration<=0 || ck::Is_NOT_Valid(State) ||
            Commit.OriginState!=State || Commit.Owner!=this) { return; }
        auto World=GetWorld(); auto Pawn=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(World) || World.GetGameState()!=State || State.GetWorld()!=World ||
            State.GetClass()!=ACk_ObjectPoolingGym_GameState || !State.Get_HasPublishedCatalog() ||
            ck::Is_NOT_Valid(Pawn) || Commit.OwnerPawn!=Pawn || Pawn.GetWorld()!=World ||
            Pawn.GetController()!=this || !State.Get_IsCompleteSnapshot(Commit.Snapshot) ||
            Commit.Snapshot.Generation!=Commit.OriginGeneration+(Commit.Action==1 ? 1 : 0)) { return; }
        _InstalledControlCommit=Commit; _AppliedDeliverySerial=Commit.DeliverySerial;
        if (_PendingSerial==Commit.RequestSerial) { _PendingSerial=0; }
        Server_AcknowledgePoolingCommit(State,Pawn,Commit.DeliverySerial,Commit.RequestSerial);
    }

    UFUNCTION() private void OnLocalCompleted(FCk_Handle H,ECk_Request_OperationResult Result)
    {
        if (!_LocalStarting || _LocalFailed || !_LocalCohort.TryRecord_Completed(H,H,Result))
        { Request_FailLocal("ObjectPooling local station completion rejected"); }
    }
    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript H)
    {
        if (!_LocalStarting || _LocalFailed || !_LocalCohort.TryRecord_Constructed(FCk_Handle(H),H))
        { Request_FailLocal("ObjectPooling local station construction rejected"); }
    }
    private void Request_StartLocal()
    {
        if (HasAuthority() || _LocalStarting || _LocalCohort.Get_Phase()!=0 || _LocalFailed) { return; }
        TArray<FCk_GymStation_SpawnParams> Recipes;
        for (int32 i=0;i<2;++i) { Recipes.Add(CkPoolingShared::Get_Params(i)); }
        _LocalStarting=true; _LocalStartedAt=float(System::GetGameTimeInSeconds());
        if (_LocalCohort.TryBegin(ck::TransientEntity(),Recipes,
            FCk_Delegate_Request_OnCompleted(this,n"OnLocalCompleted"),
            FCk_Delegate_EntityScript_Constructed(this,n"OnLocalConstructed"))!=ECkGym_SharedStationBegin::Started)
        { Request_FailLocal("ObjectPooling local station admission rejected"); }
    }
    // Cohort Ready proves construction and tags. The display fragment is
    // installed on a later station tick, so it is a separate prerequisite.
    private bool Get_HasStructuralLocalCohort(FCkPoolingGym_Snapshot Sample,FString& OutReason)
    {
        OutReason="";
        if (_LocalCohort.Get_Phase()!=2) { OutReason="phase"; return false; }
        if (_LocalCohort.Get_HasTerminalFailure()) { OutReason="terminal cohort"; return false; }
        if (_LocalStations.Num()!=2) { OutReason="local station count"; return false; }
        auto State=CkPoolingShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State)) { OutReason="state"; return false; }
        if (!State.Get_IsCompleteSnapshot(Sample)) { OutReason="source snapshot"; return false; }
        auto WorldRoot=ck::TransientEntity();
        if (!CkPoolingShared::IsLive(WorldRoot)) { OutReason="world root"; return false; }
        // A known mature shell may already be pending destruction. Let the
        // caller retire the frozen cohort before Get_Stations can fault it.
        for (int32 i=0;i<2;++i)
        { if (!CkPoolingShared::IsLive(_LocalStations[i])) { OutReason=f"station not live: {i}"; return false; } }
        auto CohortStations=_LocalCohort.Get_Stations();
        if (CohortStations.Num()!=2) { OutReason="cohort station count"; return false; }
        for (int32 i=0;i<2;++i)
        {
            auto H=_LocalStations[i];
            if (!CkPoolingShared::IsLive(H)) { OutReason=f"station not live: {i}"; return false; }
            if (CohortStations[i]!=H) { OutReason=f"cohort identity: {i}"; return false; }
            auto Tagged=utils_entity_tag::ForEach_Entity(WorldRoot,CkPoolingShared::Get_Tag(i));
            if (Tagged.Num()!=1) { OutReason=f"tag cardinality: {i}"; return false; }
            if (Tagged[0]!=H) { OutReason=f"tag identity: {i}"; return false; }
        }
        return true;
    }
    private bool Get_HasExactLocalCohort(FCkPoolingGym_Snapshot Sample,FString& OutReason)
    {
        if (!Get_HasStructuralLocalCohort(Sample,OutReason)) { return false; }
        for (int32 i=0;i<2;++i)
        {
            if (!_LocalStations[i].Has_Fragment(FCkGym_Station_TitleAndDescription))
            { OutReason=f"display fragment missing: {i}"; return false; }
        }
        return true;
    }
    private void Request_FailLocal(FString Reason)
    {
        if (_LocalFailed) { return; }
        _LocalFailed=true; _LocalStarting=false; _ShownSequence=0;
        _LocalCohort.Request_Retire();
        auto World=GetWorld(); auto Controlled=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (!HasAuthority() && IsLocalController() && ck::IsValid(World) &&
            ck::IsValid(_BoundState) && ck::IsValid(_BoundPawn) &&
            _BoundState.GetWorld()==World && World.GetGameState()==_BoundState &&
            ck::IsValid(Controlled) && Controlled==_BoundPawn &&
            Controlled.GetWorld()==World && Controlled.GetController()==this)
        { Server_ReportPoolingProjectionRetired(_BoundState,_BoundPawn); }
        ck::Error("[ObjectPoolingGym] " + Reason);
    }
    UFUNCTION() private void OnPresentationTick()
    {
        if (!IsLocalController()) { return; }
        if (_LocalFailed) { _LocalCohort.PollRetirement(); return; }
        auto State=CkPoolingShared::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State))
        {
            if (ck::IsValid(_BoundState) ||
                (!_EverPresented && float(System::GetGameTimeInSeconds())-_PresentationStartedAt>20.0f))
            { Request_FailLocal("ObjectPooling presentation GameState unavailable"); }
            return;
        }
        if (ck::IsValid(_BoundState) && _BoundState!=State)
        { Request_FailLocal("ObjectPooling GameState identity changed"); return; }
        _BoundState=State;
        auto Controlled=Cast<ACk_Gym_Base_Pawn>(GetControlledPawn());
        if (ck::Is_NOT_Valid(Controlled) || Controlled.GetWorld()!=GetWorld() ||
            Controlled.GetController()!=this)
        {
            if (ck::IsValid(_BoundPawn) ||
                (!_EverPresented && float(System::GetGameTimeInSeconds())-_PresentationStartedAt>20.0f))
            { Request_FailLocal("ObjectPooling presentation pawn unavailable"); }
            return;
        }
        if (ck::IsValid(_BoundPawn) && _BoundPawn!=Controlled)
        { Request_FailLocal("ObjectPooling presentation pawn identity changed"); return; }
        _BoundPawn=Controlled;
        Request_ConsumeCommit(State);
        if (_PendingSerial>0 && LastControlAttemptSerial==_PendingSerial && LastControlResult>=2)
        { _PendingSerial=0; }
        if (State.Snapshot.Failed)
        { Request_FailLocal("ObjectPooling shared source failed"); return; }
        if (!State.Get_IsCompleteSnapshot(State.Snapshot))
        {
            if (!_EverPresented && float(System::GetGameTimeInSeconds())-_PresentationStartedAt>20.0f)
            { Request_FailLocal("ObjectPooling initial presentation timed out"); }
            return;
        }
        if (HasAuthority())
        { if (State.Get_IsReady()) { _InstalledLiveSample=State.Snapshot; _ShownSequence=State.Snapshot.Sequence; _EverPresented=true; } return; }
        if (_LocalCohort.Get_Phase()==0) { Request_StartLocal(); return; }
        if (_LocalStarting)
        {
            if (float(System::GetGameTimeInSeconds())-_LocalStartedAt>20.0f)
            { Request_FailLocal("ObjectPooling local station readiness timed out"); return; }
            auto Poll=_LocalCohort.PollConstruction();
            if (Poll==ECkGym_SharedStationPoll::Failed)
            { Request_FailLocal("ObjectPooling local station cohort failed"); return; }
            if (Poll!=ECkGym_SharedStationPoll::Ready) { return; }
            _LocalStations=_LocalCohort.Get_Stations();
            FString RejectReason;
            if (!Get_HasStructuralLocalCohort(State.Snapshot,RejectReason))
            { Request_FailLocal("ObjectPooling local station identity rejected: "+RejectReason); return; }
            // Both station display fragments must exist before either shell
            // can receive the source sample. The existing deadline bounds this wait.
            for (int32 i=0;i<2;++i)
            { if (!_LocalStations[i].Has_Fragment(FCkGym_Station_TitleAndDescription)) { return; } }
            _LocalStarting=false;
        }
        FString RejectReason;
        if (!Get_HasExactLocalCohort(State.Snapshot,RejectReason))
        { Request_FailLocal("ObjectPooling local station cohort lost: "+RejectReason); return; }
        if (_ShownSequence>=State.Snapshot.Sequence) { return; }
        for (int32 i=0;i<2;++i)
        {
            auto& Text=_LocalStations[i].AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            Text.Title=FText::FromString(State.Snapshot.Titles[i]);
            Text.Description=FText::FromString(State.Snapshot.Bodies[i]);
        }
        _InstalledLiveSample=State.Snapshot;
        _ShownSequence=State.Snapshot.Sequence;
        _EverPresented=true;
    }
    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    { _LocalFailed=true; _LocalCohort.Request_Retire(); Super::EndPlay(Reason); }
}
