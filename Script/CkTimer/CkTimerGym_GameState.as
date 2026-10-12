// Language=angelscript

namespace CkTimerGym
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
		auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

		// Station 1: Basics
	    {
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Timer.Basics");
			Station.Title = FText::FromString("TIMER BASICS");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Demonstrates timer creation and chrono querying."));
			Description.Add(FText::FromString("Shows state, elapsed/remaining, progress bar, and IsDone flag."));
			Station.Description = Description;
			Stations.Add(Station);
	    }

		// Station 2: Behaviors
	    {
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Timer.Behaviors");
			Station.Title = FText::FromString("TIMER BEHAVIORS");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Compares ResetOnDone, PauseOnDone, and StopOnDone behaviors."));
			Description.Add(FText::FromString("Three timers run side-by-side showing different completion behavior."));
			Station.Description = Description;
			Stations.Add(Station);
	    }

		// Station 3: Signals
	    {
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Timer.Signals");
			Station.Title = FText::FromString("TIMER SIGNALS");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests all 8 timer signals: OnUpdate, OnDone, OnPause, OnResume,"));
			Description.Add(FText::FromString("OnReset, OnStop, OnJump, OnDepleted."));
			Station.Description = Description;
			Stations.Add(Station);
	    }

		// Station 4: Control
	    {
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Timer.Control");
			Station.Title = FText::FromString("TIMER RUNTIME CONTROL");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests Pause, Resume, Stop, Reset, Complete, Jump, and ReverseDirection."));
			Description.Add(FText::FromString("Watch the progress bar respond to each control action."));
			Station.Description = Description;
			Stations.Add(Station);
	    }

		// Station 5: Countdown
	    {
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.Timer.Countdown");
			Station.Title = FText::FromString("TIMER COUNTDOWN");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests CountDown direction with different behaviors."));
			Description.Add(FText::FromString("Includes a consume demo that subtracts time until OnDepleted fires."));
			Station.Description = Description;
			Stations.Add(Station);
	    }

		for (int32 i = 0; i < Stations.Num(); i++)
        {
            auto Transform = FTransform::Identity;
            Transform.SetLocation(FVector(500.0, float(i - 2) * 800.0, 0.0));
            Transform.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
            Stations[i].Transform = Transform;
        }
        return Stations;
    }

    ACk_TimerGym_GameState Get_State()
    {
        TArray<ACk_TimerGym_GameState> States;
        GetAllActorsOfClass(ACk_TimerGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }
    bool IsLiveEntity(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) && !utils_entity_lifetime::Get_IsPendingDestroy(
            InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }
    FCk_Handle FindStation(FName InTag)
    {
        auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), InTag);
        if (Matches.Num() != 1 || !IsLiveEntity(Matches[0]) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors)) { return FCk_Handle(); }
        return Matches[0];
    }
    int32 Get_DemoTimerCount(int32 InIndex)
    { return InIndex == 0 ? 2 : (InIndex == 1 ? 4 : (InIndex == 4 ? 5 : 2)); }
    int32 Get_CounterCount(int32 InIndex)
    { return InIndex == 0 ? 1 : (InIndex == 1 ? 3 : (InIndex == 2 ? 8 : (InIndex == 3 ? 1 : 4))); }
}

USTRUCT()
struct FCkTimerGym_StationSpawnParams
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 SlotEpoch = 0;
    UPROPERTY() bool StageOnly = false;
}

USTRUCT()
struct FCkTimerGym_StationSample
{
    UPROPERTY() int32 SlotEpoch = 0;
    UPROPERTY() TArray<float> ElapsedSeconds;
    UPROPERTY() TArray<float> GoalSeconds;
    UPROPERTY() TArray<int32> TimerStates;
    UPROPERTY() TArray<int32> Counters;
    UPROPERTY() FString Body;
}

USTRUCT()
struct FCkTimerGym_Snapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() TArray<FCkTimerGym_StationSample> Stations;
}

namespace CkTimerGym
{
    bool AppendTimer(FCk_Handle InOwner, FCk_Handle_Timer InTimer,
                     FCkTimerGym_StationSample& OutSample)
    {
        auto Handle = FCk_Handle(InTimer);
        if (!IsLiveEntity(Handle) || !utils_timer::DoCast(Handle).IsSet() ||
            utils_entity_lifetime::Get_LifetimeOwner(Handle) != InOwner) { return false; }
        auto Chrono = InTimer.Get_CurrentTimerValue();
        auto Goal = FCk_Time(); auto Elapsed = FCk_Time(); auto Remaining = FCk_Time();
        Chrono.Break_Chrono(Goal, Elapsed, Remaining);
        auto GoalSeconds = float(Goal.Get_Milliseconds()) * 0.001;
        auto ElapsedSeconds = float(Elapsed.Get_Milliseconds()) * 0.001;
        if (!(GoalSeconds >= 0.0 && GoalSeconds < 1000000.0) ||
            !(ElapsedSeconds >= 0.0 && ElapsedSeconds < 1000000.0)) { return false; }
        auto State = InTimer.Get_CurrentState();
        if (State != ECk_Timer_State::Running && State != ECk_Timer_State::Paused) { return false; }
        OutSample.GoalSeconds.Add(GoalSeconds);
        OutSample.ElapsedSeconds.Add(ElapsedSeconds);
        OutSample.TimerStates.Add(int32(State));
        return true;
    }
    bool ValidateSample(FCkTimerGym_StationSample InSample, int32 InIndex, int32 InEpoch)
    {
        if (InIndex < 0 || InIndex > 4 || InSample.SlotEpoch != InEpoch || InEpoch <= 0 ||
            InSample.Body.Len() > 4096 ||
            InSample.ElapsedSeconds.Num() != Get_DemoTimerCount(InIndex) ||
            InSample.GoalSeconds.Num() != Get_DemoTimerCount(InIndex) ||
            InSample.TimerStates.Num() != Get_DemoTimerCount(InIndex) ||
            InSample.Counters.Num() != Get_CounterCount(InIndex)) { return false; }
        for (int32 i = 0; i < InSample.ElapsedSeconds.Num(); i++)
        {
            if (!(InSample.ElapsedSeconds[i] >= 0.0 && InSample.ElapsedSeconds[i] < 1000000.0) ||
                !(InSample.GoalSeconds[i] >= 0.0 && InSample.GoalSeconds[i] < 1000000.0) ||
                (InSample.TimerStates[i] != int32(ECk_Timer_State::Running) &&
                 InSample.TimerStates[i] != int32(ECk_Timer_State::Paused))) { return false; }
        }
        for (auto Counter : InSample.Counters) { if (Counter < 0) { return false; } }
        return true;
    }
}

class ACk_TimerGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkTimerGym_Snapshot Snapshot;
    UPROPERTY() TArray<FCk_Handle> ServerScripts;
    UPROPERTY() int32 ServerQuintetCount = 0;
    UPROPERTY() int32 LocalProjectionGeneration = 0;
    UPROPERTY() bool LocalProjectionReady = false;
    UPROPERTY() bool LocalProjectionFailed = false;
    UPROPERTY() FString LocalProjectionFailureReason;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _AdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private bool _AdmissionOwned = false;
    private bool _Starting = false;
    private bool _StationsReady = false;
    private bool _ActivationPending = false;
    private int32 _PrimeMask = 0;
    private bool _ServerMonitorArmed = false;
    private bool _RequestedNewStations = false;
    private int32 _NextGeneration = 0;
    private float _GenerationStartSeconds = 0.0;
    private float _LastPublishSeconds = -1.0;
    private TArray<FCk_Handle> _NewStations;
    private TArray<FCk_Handle> _ReusedStations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private int32 _PendingStationCount = 0;
    private TArray<FCk_Handle> _Server;
    private TArray<FCk_Handle_PendingEntityScript> _Pending;
    private TArray<int32> _Epochs;
    private TArray<FCk_Handle> _Retiring;
    private FCkTimerGym_Snapshot _Staged;
    private int32 _SampleMask = 0;
    private int32 _CandidateIndex = -1;
    private int32 _CandidateEpoch = 0;
    private FCk_Handle _Candidate;
    private FCk_Handle_PendingEntityScript _CandidatePending;
    private bool _CandidateWithdrawn = false;
    private bool _CandidateCommitted = false;
    private float _CandidateStartSeconds = 0.0;
    private ACk_TimerGym_PlayerController _ControlRequester;
    private int32 _ControlSerial = 0;
    private int32 _ControlGeneration = 0;
    private int32 _ControlIndex = -1;
    private int32 _ControlEpoch = 0;

    private bool _LocalMonitorArmed = false;
    private bool _LocalStarting = false;
    private float _LocalWaitStart = 0.0;
    private float _LocalProjectionStart = -1.0;
    private TArray<FCk_Handle> _Local;
    private TArray<FCk_Handle_PendingEntityScript> _LocalPending;
    private TArray<int32> _LocalEpochs;
    private TArray<FCk_Handle> _LocalRetiring;

    private void Request_SizeArrays()
    {
        if (_Server.Num() == 5) { return; }
        _Server.Empty(); _Pending.Empty(); _Epochs.Empty();
        for (int32 i = 0; i < 5; i++)
        {
            _Server.Add(FCk_Handle());
            _Pending.Add(FCk_Handle_PendingEntityScript());
            _Epochs.Add(1);
        }
    }
    private void Request_SyncReflectedHandles()
    {
        ServerScripts = _Server;
    }
    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog() || Snapshot.Ready ||
            Snapshot.Failed || _Starting || _CandidateIndex >= 0) { return; }
        Request_StartGeneration(false);
    }
    private void Request_StartGeneration(bool InWaitForRetirement)
    {
        Request_SizeArrays();
        _Starting = true; _StationsReady = false; _ActivationPending = false; _PrimeMask = 0;
        _GenerationStartSeconds = float(System::GetGameTimeInSeconds());
        _NextGeneration++;
        Snapshot = FCkTimerGym_Snapshot(); Snapshot.Generation = _NextGeneration;
        _Staged = FCkTimerGym_Snapshot(); _Staged.Generation = _NextGeneration;
        _SampleMask = 0; _LastPublishSeconds = -1.0;
        for (int32 i = 0; i < 5; i++) { _Epochs[i] = 1; }
        ForceNetUpdate();
        System::SetTimer(this, n"OnStartupDeadline", 20.0, false);
        if (InWaitForRetirement) { System::SetTimer(this, n"OnRetirementWait", 0.05, false); }
        else { Request_EnsureWorldStations(); }
    }
    private void Request_Capture(FCk_Handle InRoot)
    {
        if (!CkTimerGym::IsLiveEntity(InRoot) || _Retiring.Contains(InRoot)) { return; }
        _Retiring.Add(InRoot);
        for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
        { Request_Capture(Child); }
    }
    private bool Get_HasRetiringEntities()
    {
        for (auto Handle : _Retiring) { if (ck::IsValid(Handle)) { return true; } }
        return false;
    }
    private void Request_RetireAllChildren()
    {
        if (_AdmissionTicket != 0) { return; }
        ServerQuintetCount = 0;
        Request_SizeArrays();
        for (int32 i = 0; i < 5; i++)
        {
            if (utils_pending_entity_script::Get_IsValid(_Pending[i]))
            { utils_pending_entity_script::Request_DestroyEntity(_Pending[i]); }
            else if (CkTimerGym::IsLiveEntity(_Server[i]))
            { utils_entity_lifetime::Request_DestroyEntity(_Server[i]); }
            _Server[i] = FCk_Handle(); _Pending[i] = FCk_Handle_PendingEntityScript();
        }
        Request_SyncReflectedHandles();
        if (utils_pending_entity_script::Get_IsValid(_CandidatePending))
        { utils_pending_entity_script::Request_DestroyEntity(_CandidatePending); }
        else if (CkTimerGym::IsLiveEntity(_Candidate))
        { utils_entity_lifetime::Request_DestroyEntity(_Candidate); }
        _Candidate = FCk_Handle(); _CandidatePending = FCk_Handle_PendingEntityScript();
        _CandidateIndex = -1; _CandidateWithdrawn = false; _CandidateCommitted = false;
    }
    private bool Request_ReleaseAdmission(bool InPublishedComplete = false)
    {
        if (_AdmissionTicket == 0 && CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { return false; }
        auto World = ck::TransientEntity();
        if (_AdmissionTicket != 0)
        {
            if (InPublishedComplete)
            {
                if (!CkGym_Admission::TryReleasePublished(World, _AdmissionTicket, _AdmissionOwner))
                { Request_CloseWorldOnFailure(); return false; }
                _AdmissionTicket = 0;
            }
            else
            { Request_CloseWorldOnFailure(); return false; }
        }
        _AdmissionOwned = false;
        if (CkTimerGym::IsLiveEntity(_AdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }
    private void Request_Fail(FString InReason)
    {
        if (Snapshot.Failed) { return; }
        _Starting = false; _ActivationPending = false; _PrimeMask = 0;
        Snapshot.Ready = false; Snapshot.Failed = true; Snapshot.FailureReason = InReason;
        Request_CloseWorldOnFailure();
        ForceNetUpdate();
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { ck::Error("[TimerGym] " + InReason); return; }
        Request_RetireAllChildren(); Request_ReleaseAdmission();
        for (auto Pending : _PendingStations)
        { if (utils_pending_entity_script::Get_IsValid(Pending))
          { utils_pending_entity_script::Request_DestroyEntity(Pending); } }
        if (_RequestedNewStations && CkTimerGym::IsLiveEntity(_WorldStationOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); _WorldStationOwner = FCk_Handle(); }
        _RequestedNewStations = false;
        _NewStations.Empty(); _ReusedStations.Empty(); _PendingStations.Empty();
        _Retiring.Empty(); _SampleMask = 0;
        Request_CompleteControl(3);
        ck::Error("[TimerGym] " + InReason);
    }
    private void Request_CloseWorldOnFailure()
    {
        if (_AdmissionTicket == 0) { return; }
        auto World = ck::TransientEntity();
        if (!CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseWorldUnproven(World); }
    }
    private void Request_CompleteControl(int32 InResult)
    {
        if (ck::IsValid(_ControlRequester))
        { _ControlRequester.Request_CompleteTimerControl(_ControlSerial, InResult); }
        _ControlRequester = nullptr; _ControlSerial = 0;
        _ControlGeneration = 0; _ControlIndex = -1; _ControlEpoch = 0;
    }
    UFUNCTION() private void OnStartupDeadline()
    {
        if (!_Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _GenerationStartSeconds;
        if (Elapsed < 20.0) { System::SetTimer(this, n"OnStartupDeadline", 20.0 - Elapsed, false); return; }
        Request_Fail("Shared quintet readiness timed out");
    }
    private void Request_EnsureWorldStations()
    {
        if (!_Starting || Snapshot.Failed || _AdmissionOwned || _StationsReady) { return; }
        auto World = ck::TransientEntity();
        if (!CkTimerGym::IsLiveEntity(World) || !utils_entity_lifetime::Get_CanCreateEntity(World))
        { Request_Fail("World station owner unavailable"); return; }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { Request_Fail("Station admission permanently closed"); return; }
        if (CkGym_Admission::Get_IsBusy(World))
        { System::SetTimer(this, n"OnAdmissionWait", 0.1, false); return; }
        _AdmissionTicket = CkGym_Admission::TryReserve(World);
        if (_AdmissionTicket == 0)
        { Request_Fail("Station admission reservation rejected"); return; }
        _AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkTimerGym::IsLiveEntity(_AdmissionOwner))
        { Request_Fail("Station admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; } _AdmissionOwned = true;
        auto Stations = CkTimerGym::Get_Stations();
        if (Stations.Num() != 5) { Request_Fail("Station declarations changed"); return; }
        for (auto Station : Stations)
        {
            if (Station.Tags.Num() != 1 || Station.Tags[0] == FName())
            { Request_Fail("Invalid station tag"); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, Station.Tags[0]);
            if (Matches.Num() > 1 || (Matches.Num() == 1 &&
                (!CkTimerGym::IsLiveEntity(Matches[0]) ||
                 !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
            { Request_Fail("Ambiguous station anchor"); return; }
            if (Matches.Num() == 1) { _ReusedStations.Add(Matches[0]); }
        }
        if (!CkTimerGym::IsLiveEntity(_WorldStationOwner))
        {
            _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
            if (!CkTimerGym::IsLiveEntity(_WorldStationOwner))
            { Request_Fail("Station hierarchy allocation failed"); return; }
        }
        _PendingStationCount = 0;
        for (auto Station : Stations)
        {
            if (CkTimerGym::IsLiveEntity(CkTimerGym::FindStation(Station.Tags[0]))) { continue; }
            _PendingStationCount++;
            _RequestedNewStations = true;
            auto Pending = CkGym_Common::Request_SpawnNewStationForOwner(Station, _WorldStationOwner,
                FCk_Delegate_Request_OnCompleted(this, n"OnStationSpawnCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_Fail("Station spawn rejected"); return; }
            _PendingStations.Add(Pending);
            utils_pending_entity_script::Promise_OnConstructed(Pending,
                FCk_Delegate_EntityScript_Constructed(this, n"OnStationConstructed"));
        }
        if (_PendingStationCount == 0) { Request_CheckStationsSettled(); }
    }
    UFUNCTION() private void OnAdmissionWait()
    {
        if (!_Starting || Snapshot.Failed) { return; }
        if (float(System::GetGameTimeInSeconds()) - _GenerationStartSeconds >= 20.0)
        { Request_Fail("Station admission timed out"); return; }
        Request_EnsureWorldStations();
    }
    UFUNCTION() private void OnStationSpawnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!_Starting || Snapshot.Failed) { return; }
        for (auto Pending : _PendingStations)
        {
            if (Pending.Get_EntityUnderConstruction() == InOwner &&
                InResult != ECk_Request_OperationResult::Succeeded)
            { Request_Fail("Station spawn failed"); return; }
        }
    }
    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed || ck::Is_NOT_Valid(InHandle)) { return; }
        auto Station = FCk_Handle(InHandle);
        bool Current = false;
        for (auto Pending : _PendingStations)
        { if (Pending.Get_EntityUnderConstruction() == Station) { Current = true; break; } }
        if (!Current) { return; }
        if (!CkTimerGym::IsLiveEntity(Station) ||
            utils_entity_lifetime::Get_LifetimeOwner(Station) != _WorldStationOwner ||
            _NewStations.Contains(Station) || _PendingStationCount <= 0)
        { Request_Fail("Station completion malformed"); return; }
        _NewStations.Add(Station); _PendingStationCount--;
        if (_PendingStationCount == 0)
        { System::SetTimer(this, n"OnStationSettle", 0.05, false); }
    }
    UFUNCTION() private void OnStationSettle()
    { if (_Starting && _AdmissionOwned && _PendingStationCount == 0) { Request_CheckStationsSettled(); } }
    private void Request_CheckStationsSettled()
    {
        auto Stations = CkTimerGym::Get_Stations();
        for (auto Station : Stations)
        {
            auto Handle = CkTimerGym::FindStation(Station.Tags[0]);
            if (!CkTimerGym::IsLiveEntity(Handle) ||
                (!_NewStations.Contains(Handle) && !_ReusedStations.Contains(Handle)))
            { Request_Fail("Station tags did not settle uniquely"); return; }
        }
        if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; } _RequestedNewStations = false;
        _NewStations.Empty(); _ReusedStations.Empty();
        _PendingStations.Empty(); _StationsReady = true;
        Request_SpawnAll();
    }
    private FCk_Handle_PendingEntityScript Request_SpawnChild(int32 InIndex, int32 InEpoch, bool InStage)
    {
        auto Stations = CkTimerGym::Get_Stations();
        if (InIndex < 0 || InIndex >= 5 || Stations.Num() != 5) { return FCk_Handle_PendingEntityScript(); }
        auto Anchor = CkTimerGym::FindStation(Stations[InIndex].Tags[0]);
        if (!CkTimerGym::IsLiveEntity(Anchor)) { return FCk_Handle_PendingEntityScript(); }
        auto Params = FCkTimerGym_StationSpawnParams();
        Params.Generation = Snapshot.Generation; Params.SlotEpoch = InEpoch; Params.StageOnly = InStage;
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
            Anchor.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        if (InIndex == 0) { return utils_entity_script::Request_SpawnEntity(Anchor, UCk_EntityScript_TimerGym_Basics, FInstancedStruct::Make(Params)); }
        if (InIndex == 1) { return utils_entity_script::Request_SpawnEntity(Anchor, UCk_EntityScript_TimerGym_Behaviors, FInstancedStruct::Make(Params)); }
        if (InIndex == 2) { return utils_entity_script::Request_SpawnEntity(Anchor, UCk_EntityScript_TimerGym_Signals, FInstancedStruct::Make(Params)); }
        if (InIndex == 3) { return utils_entity_script::Request_SpawnEntity(Anchor, UCk_EntityScript_TimerGym_Control, FInstancedStruct::Make(Params)); }
        return utils_entity_script::Request_SpawnEntity(Anchor, UCk_EntityScript_TimerGym_Countdown, FInstancedStruct::Make(Params));
    }
    private void Request_SpawnAll()
    {
        if (!_Starting || Snapshot.Failed) { return; }
        for (int32 i = 0; i < 5; i++)
        {
            _Pending[i] = Request_SpawnChild(i, _Epochs[i], false);
            if (!utils_pending_entity_script::Get_IsValid(_Pending[i]))
            { Request_Fail("Station child spawn rejected"); return; }
            utils_pending_entity_script::Promise_OnConstructed(_Pending[i],
                FCk_Delegate_EntityScript_Constructed(this, n"OnChildConstructed"));
        }
    }
    UFUNCTION() private void OnChildConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed || ck::Is_NOT_Valid(InHandle)) { return; }
        auto Handle = FCk_Handle(InHandle);
        for (int32 i = 0; i < 5; i++)
        {
            if (_Pending[i].Get_EntityUnderConstruction() != Handle) { continue; }
            if (!CkTimerGym::IsLiveEntity(Handle) || CkTimerGym::IsLiveEntity(_Server[i]))
            { Request_Fail("Child completion malformed"); return; }
            _Server[i] = Handle; Request_SyncReflectedHandles();
            Request_CheckAllConstructed(); return;
        }
    }
    private void Request_CheckAllConstructed()
    {
        for (auto Child : _Server) { if (!CkTimerGym::IsLiveEntity(Child)) { return; } }
        for (int32 i = 0; i < 5; i++)
        {
            if (!Get_HasPreparedChild(i, _Server[i]))
            { Request_Fail("Prepared timer composition incomplete"); return; }
        }
        _ActivationPending = true;
        _PrimeMask = 0;
        for (int32 i = 0; i < 5; i++)
        { if (!Request_PrimeChild(i, _Server[i])) { Request_Fail("Timer priming rejected"); return; } }
    }
    void Request_FailCurrentPrimingChild(FCk_Handle InChild, int32 InGeneration,
                                         int32 InEpoch, int32 InIndex)
    {
        if (!HasAuthority() || Snapshot.Failed || InGeneration != Snapshot.Generation ||
            InIndex < 0 || InIndex > 4 || _Server.Num() != 5 || _Epochs.Num() != 5)
        { return; }
        if ((_ActivationPending && _Starting && _Server[InIndex] == InChild &&
             _Epochs[InIndex] == InEpoch) ||
            (_CandidateWithdrawn && _CandidateIndex == InIndex &&
             _Candidate == InChild && _CandidateEpoch == InEpoch))
        { Request_Fail("Timer priming request failed"); }
    }
    void Request_OnChildPrimed(FCk_Handle InChild, int32 InGeneration,
                               int32 InEpoch, int32 InIndex)
    {
        if (!HasAuthority() || Snapshot.Failed || InGeneration != Snapshot.Generation ||
            InIndex < 0 || InIndex > 4) { return; }
        if (_CandidateWithdrawn)
        {
            if (_CandidateIndex != InIndex || _CandidateEpoch != InEpoch ||
                _Candidate != InChild) { return; }
            auto Stations = CkTimerGym::Get_Stations();
            if (Stations.Num() != 5 || _Server.Num() != 5 || _Epochs.Num() != 5 ||
                _Server[InIndex] != InChild || _Epochs[InIndex] != InEpoch ||
                !Get_HasValidRetainedSiblings() || !Get_HasPrimedChild(InIndex, InChild))
            { Request_Fail("Candidate or retained timer composition lost"); return; }
            auto Anchor = CkTimerGym::FindStation(Stations[InIndex].Tags[0]);
            if (!CkTimerGym::IsLiveEntity(Anchor) ||
                utils_entity_lifetime::Get_LifetimeOwner(InChild) != Anchor ||
                !Request_ActivateChild(InIndex, InChild))
            { Request_Fail("Candidate activation failed"); return; }
            _Candidate = FCk_Handle(); _CandidatePending = FCk_Handle_PendingEntityScript();
            _CandidateWithdrawn = false; _CandidateCommitted = true;
            return;
        }
        if (!_ActivationPending || !_Starting || _Server.Num() != 5 ||
            _Epochs.Num() != 5 || _Server[InIndex] != InChild ||
            _Epochs[InIndex] != InEpoch || !Get_HasPrimedChild(InIndex, InChild)) { return; }
        _PrimeMask |= (1 << InIndex);
        if (_PrimeMask != 31) { return; }
        for (int32 i = 0; i < 5; i++)
        {
            if (!Get_HasPrimedChild(i, _Server[i]) || !Request_ActivateChild(i, _Server[i]))
            { Request_Fail("Timer activation rejected"); return; }
        }
        _ActivationPending = false; ServerQuintetCount = 5;
        if (!_ServerMonitorArmed)
        { _ServerMonitorArmed = true; System::SetTimer(this, n"OnServerMonitor", 0.1, true); }
    }
    bool Get_IsCurrentConstruction(int32 InGeneration, int32 InEpoch, int32 InIndex, bool InStage)
    {
        if (!HasAuthority() || Snapshot.Failed || InGeneration != Snapshot.Generation ||
            InIndex < 0 || InIndex > 4 || _Epochs.Num() != 5) { return false; }
        if (InStage) { return _CandidateIndex == InIndex && _CandidateEpoch == InEpoch && !_CandidateWithdrawn; }
        return _Starting && _Epochs[InIndex] == InEpoch;
    }
    bool Get_IsCurrentServerChild(FCk_Handle InChild, int32 InGeneration, int32 InEpoch, int32 InIndex)
    {
        return HasAuthority() && !Snapshot.Failed && ServerQuintetCount == 5 &&
            InGeneration == Snapshot.Generation && InIndex >= 0 && InIndex < 5 &&
            _Epochs.Num() == 5 && _Server.Num() == 5 &&
            _Epochs[InIndex] == InEpoch && _Server[InIndex] == InChild;
    }
    void Request_FailCurrentServerChild(FCk_Handle InChild, int32 InGeneration,
                                        int32 InEpoch, int32 InIndex, FString InReason)
    {
        if (!Get_IsCurrentServerChild(InChild, InGeneration, InEpoch, InIndex)) { return; }
        Request_Fail(InReason);
    }
    private bool Get_IsValidServerQuintet()
    {
        if (ServerQuintetCount != 5 || _Server.Num() != 5 || _Epochs.Num() != 5) { return false; }
        auto Stations = CkTimerGym::Get_Stations();
        if (Stations.Num() != 5) { return false; }
        for (int32 i = 0; i < 5; i++)
        {
            auto Anchor = CkTimerGym::FindStation(Stations[i].Tags[0]);
            if (!CkTimerGym::IsLiveEntity(Anchor) || !CkTimerGym::IsLiveEntity(_Server[i]) ||
                utils_entity_lifetime::Get_LifetimeOwner(_Server[i]) != Anchor ||
                !Get_HasValidChild(i, _Server[i])) { return false; }
        }
        return true;
    }
    private bool Get_HasValidRetainedSiblings()
    {
        if (_CandidateIndex < 0 || _CandidateIndex > 4 || ServerQuintetCount != 5 ||
            _Server.Num() != 5 || _Epochs.Num() != 5) { return false; }
        auto Stations = CkTimerGym::Get_Stations();
        if (Stations.Num() != 5) { return false; }
        for (int32 i = 0; i < 5; i++)
        {
            if (i == _CandidateIndex) { continue; }
            auto Anchor = CkTimerGym::FindStation(Stations[i].Tags[0]);
            if (!CkTimerGym::IsLiveEntity(Anchor) || !CkTimerGym::IsLiveEntity(_Server[i]) ||
                utils_entity_lifetime::Get_LifetimeOwner(_Server[i]) != Anchor ||
                !Get_HasValidChild(i, _Server[i])) { return false; }
        }
        return true;
    }
    bool Get_HasReadyServerQuintet()
    { return HasAuthority() && Snapshot.Ready && !Snapshot.Failed && Get_HasCompleteSnapshot() && Get_IsValidServerQuintet(); }
    UFUNCTION() private void OnServerMonitor()
    {
        if (!HasAuthority() || Snapshot.Failed || ServerQuintetCount != 5) { return; }
        if (_CandidateWithdrawn)
        {
            if (!Get_HasValidRetainedSiblings())
            { Request_Fail("Unselected timer station lost a dependency"); }
            return;
        }
        if (!Get_IsValidServerQuintet()) { Request_Fail("Shared quintet lost a child or timer"); }
    }
    private bool Get_HasCompleteSnapshot()
    {
        if (Snapshot.Generation <= 0 || Snapshot.Sequence <= 0 || !Snapshot.Ready ||
            Snapshot.Failed || Snapshot.Stations.Num() != 5) { return false; }
        if (HasAuthority() && _Epochs.Num() != 5) { return false; }
        for (int32 i = 0; i < 5; i++)
        {
            if (!CkTimerGym::ValidateSample(Snapshot.Stations[i], i,
                Snapshot.Stations[i].SlotEpoch) ||
                (HasAuthority() && Snapshot.Stations[i].SlotEpoch != _Epochs[i])) { return false; }
        }
        return true;
    }
    void SubmitStationSample(FCk_Handle InChild, int32 InGeneration, int32 InEpoch,
                             int32 InIndex, FCkTimerGym_StationSample InSample)
    {
        if (!Get_IsCurrentServerChild(InChild, InGeneration, InEpoch, InIndex)) { return; }
        if (_CandidateWithdrawn || ServerQuintetCount != 5) { return; }
        if (!Get_IsValidServerQuintet()) { Request_Fail("Shared timer composition lost"); return; }
        if (!CkTimerGym::ValidateSample(InSample, InIndex, InEpoch))
        { Request_Fail("Malformed station sample"); return; }
        if (_Staged.Stations.Num() != 5)
        {
            _Staged.Stations.Empty();
            for (int32 i = 0; i < 5; i++) { _Staged.Stations.Add(FCkTimerGym_StationSample()); }
        }
        _Staged.Stations[InIndex] = InSample;
        _SampleMask |= (1 << InIndex);
        if (_SampleMask != 31) { return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (Snapshot.Ready && _LastPublishSeconds >= 0.0 && Now - _LastPublishSeconds < 0.1)
        { return; }
        _Staged.Sequence = Snapshot.Sequence + 1;
        _Staged.Ready = true; _Staged.Failed = false;
        Snapshot = _Staged; _Starting = false; _LastPublishSeconds = Now;
        _SampleMask = 0; ForceNetUpdate();
        if (_CandidateCommitted && _CandidateIndex >= 0 &&
            Snapshot.Stations[_CandidateIndex].SlotEpoch == _CandidateEpoch)
        {
            _Candidate = FCk_Handle(); _CandidateIndex = -1;
            _CandidateCommitted = false;
        }
        if (_ControlSerial > 0 && _ControlGeneration == Snapshot.Generation &&
            (_ControlIndex < 0 ||
             Snapshot.Stations[_ControlIndex].SlotEpoch == _ControlEpoch))
        { Request_CompleteControl(1); }
    }
    bool Request_RerunStation(int32 InIndex, ACk_TimerGym_PlayerController InRequester,
                              int32 InSerial)
    {
        if (!Get_HasReadyServerQuintet() || InIndex < 0 || InIndex > 4 ||
            _CandidateIndex >= 0 || _ControlSerial > 0 || ck::Is_NOT_Valid(InRequester) || InSerial <= 0)
        { return false; }
        _CandidateIndex = InIndex; _CandidateEpoch = _Epochs[InIndex] + 1;
        _CandidateWithdrawn = false; _CandidateCommitted = false;
        _CandidateStartSeconds = float(System::GetGameTimeInSeconds());
        _CandidatePending = Request_SpawnChild(InIndex, _CandidateEpoch, true);
        if (!utils_pending_entity_script::Get_IsValid(_CandidatePending))
        { _CandidateIndex = -1; return false; }
        _ControlRequester = InRequester; _ControlSerial = InSerial;
        _ControlGeneration = Snapshot.Generation; _ControlIndex = InIndex;
        _ControlEpoch = _CandidateEpoch;
        utils_pending_entity_script::Promise_OnConstructed(_CandidatePending,
            FCk_Delegate_EntityScript_Constructed(this, n"OnCandidateConstructed"));
        System::SetTimer(this, n"OnCandidateDeadline", 20.0, false);
        return true;
    }
    UFUNCTION() private void OnCandidateDeadline()
    {
        if (_CandidateIndex < 0) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _CandidateStartSeconds;
        if (Elapsed < 20.0)
        { System::SetTimer(this, n"OnCandidateDeadline", 20.0 - Elapsed, false); return; }
        if (_CandidateCommitted)
        { Request_Fail("Selected station publication timed out"); return; }
        if (!_CandidateWithdrawn)
        {
            if (utils_pending_entity_script::Get_IsValid(_CandidatePending))
            { utils_pending_entity_script::Request_DestroyEntity(_CandidatePending); }
            _CandidateIndex = -1; _CandidatePending = FCk_Handle_PendingEntityScript();
            _Candidate = FCk_Handle(); Request_CompleteControl(4); return;
        }
        Request_Fail("Selected station replacement timed out");
    }
    UFUNCTION() private void OnCandidateConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (_CandidateIndex < 0 || _CandidateWithdrawn || ck::Is_NOT_Valid(InHandle) ||
            !utils_pending_entity_script::Get_IsValid(_CandidatePending) ||
            FCk_Handle(InHandle) != _CandidatePending.Get_EntityUnderConstruction()) { return; }
        _Candidate = FCk_Handle(InHandle);
        if (!Get_IsValidServerQuintet())
        { Request_Fail("Original timer quintet lost before replacement"); return; }
        auto Stations = CkTimerGym::Get_Stations();
        auto Anchor = CkTimerGym::FindStation(Stations[_CandidateIndex].Tags[0]);
        if (!CkTimerGym::IsLiveEntity(Anchor) || !CkTimerGym::IsLiveEntity(_Candidate) ||
            utils_entity_lifetime::Get_LifetimeOwner(_Candidate) != Anchor ||
            !Get_HasPreparedChild(_CandidateIndex, _Candidate))
        {
            utils_pending_entity_script::Request_DestroyEntity(_CandidatePending);
            _Candidate = FCk_Handle(); _CandidatePending = FCk_Handle_PendingEntityScript();
            _CandidateIndex = -1; Request_CompleteControl(4); return;
        }
        Snapshot.Ready = false; _CandidateWithdrawn = true; _SampleMask = 0;
        ForceNetUpdate();
        _Epochs[_CandidateIndex] = _CandidateEpoch;
        Request_Capture(_Server[_CandidateIndex]);
        auto Old = _Server[_CandidateIndex];
        utils_entity_lifetime::Request_DestroyEntity(Old);
        System::SetTimer(this, n"OnRetirementWait", 0.05, false);
    }
    UFUNCTION() private void OnRetirementWait()
    {
        if (Snapshot.Failed || (!_Starting && !_CandidateWithdrawn)) { return; }
        if (Get_HasRetiringEntities())
        {
            if (float(System::GetGameTimeInSeconds()) -
                (_CandidateWithdrawn ? _CandidateStartSeconds : _GenerationStartSeconds) >= 20.0)
            { Request_Fail("Old station subtree retirement timed out"); return; }
            System::SetTimer(this, n"OnRetirementWait", 0.05, false); return;
        }
        _Retiring.Empty();
        if (_CandidateWithdrawn)
        {
            auto Index = _CandidateIndex;
            _Server[Index] = _Candidate; _Pending[Index] = _CandidatePending;
            Request_SyncReflectedHandles();
            if (!Request_PrimeChild(Index, _Candidate))
            { Request_Fail("Candidate timer priming rejected"); return; }
            return;
        }
        Request_EnsureWorldStations();
    }
    bool Request_ResetSharedDemo(ACk_TimerGym_PlayerController InRequester, int32 InSerial)
    {
        if (_AdmissionTicket != 0) { return false; }
        if (!Get_HasReadyServerQuintet() || _CandidateIndex >= 0 || _ControlSerial > 0 ||
            ck::Is_NOT_Valid(InRequester) || InSerial <= 0) { return false; }
        Snapshot.Ready = false; ForceNetUpdate();
        for (auto Child : _Server) { Request_Capture(Child); }
        Request_RetireAllChildren();
        Request_StartGeneration(true);
        if (_Starting && !Snapshot.Failed)
        {
            _ControlRequester = InRequester; _ControlSerial = InSerial;
            _ControlGeneration = Snapshot.Generation; _ControlIndex = -1;
        }
        return _Starting && !Snapshot.Failed;
    }

    void Request_EnsureLocalProjection()
    {
        if (HasAuthority()) { return; }
        if (!_LocalMonitorArmed)
        {
            _LocalMonitorArmed = true;
            _LocalWaitStart = float(System::GetGameTimeInSeconds());
            System::SetTimer(this, n"OnLocalMonitor", 0.1, true);
        }
        Request_CheckLocalProjection();
    }
    UFUNCTION() private void OnLocalMonitor() { Request_CheckLocalProjection(); }
    private void Request_CaptureLocal(FCk_Handle InRoot)
    {
        if (!CkTimerGym::IsLiveEntity(InRoot) || _LocalRetiring.Contains(InRoot)) { return; }
        _LocalRetiring.Add(InRoot);
        for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
        { Request_CaptureLocal(Child); }
    }
    private void Request_RetireLocalProjection()
    {
        LocalProjectionReady = false; _LocalStarting = false;
        for (auto Pending : _LocalPending)
        {
            if (utils_pending_entity_script::Get_IsValid(Pending))
            {
                Request_CaptureLocal(Pending.Get_EntityUnderConstruction());
                utils_pending_entity_script::Request_DestroyEntity(Pending);
            }
        }
        for (auto Child : _Local)
        {
            Request_CaptureLocal(Child);
            if (CkTimerGym::IsLiveEntity(Child)) { utils_entity_lifetime::Request_DestroyEntity(Child); }
        }
        _LocalPending.Empty(); _Local.Empty(); _LocalEpochs.Empty();
    }
    private void Request_FailLocalProjection(FString InReason)
    {
        Request_RetireLocalProjection(); LocalProjectionFailed = true;
        LocalProjectionFailureReason = InReason;
        ck::Error("[TimerGym] " + InReason);
    }
    private void Request_CheckLocalProjection()
    {
        if (HasAuthority() || !_LocalMonitorArmed) { return; }
        if (Snapshot.Generation != LocalProjectionGeneration)
        {
            Request_RetireLocalProjection(); LocalProjectionGeneration = Snapshot.Generation;
            LocalProjectionFailed = false; LocalProjectionFailureReason = "";
            _LocalWaitStart = float(System::GetGameTimeInSeconds());
            _LocalProjectionStart = -1.0;
        }
        if (LocalProjectionFailed) { return; }
        if (Snapshot.Failed)
        { Request_FailLocalProjection("Shared simulation failed"); return; }
        if ((LocalProjectionReady || _LocalStarting) && !Snapshot.Ready)
        {
            Request_RetireLocalProjection();
            _LocalWaitStart = float(System::GetGameTimeInSeconds());
            _LocalProjectionStart = -1.0;
            return;
        }
        if (Snapshot.Ready && !Get_HasCompleteSnapshot())
        { Request_FailLocalProjection("Malformed shared snapshot"); return; }
        if ((LocalProjectionReady || _LocalStarting) && _LocalEpochs.Num() == 5)
        {
            bool SameEpochs = true;
            for (int32 i = 0; i < 5; i++)
            { if (_LocalEpochs[i] != Snapshot.Stations[i].SlotEpoch) { SameEpochs = false; break; } }
            if (!SameEpochs)
            {
                Request_RetireLocalProjection();
                _LocalProjectionStart = float(System::GetGameTimeInSeconds());
            }
        }
        if (LocalProjectionReady)
        {
            if (!Get_IsValidLocalProjection())
            { Request_FailLocalProjection("Local projection lost a child or anchor"); }
            return;
        }
        if (!Snapshot.Ready)
        {
            if (float(System::GetGameTimeInSeconds()) - _LocalWaitStart >= 25.0)
            { Request_FailLocalProjection("Shared readiness timed out"); }
            return;
        }
        if (_LocalProjectionStart < 0.0)
        { _LocalProjectionStart = float(System::GetGameTimeInSeconds()); }
        if (float(System::GetGameTimeInSeconds()) - _LocalProjectionStart >= 20.0)
        { Request_FailLocalProjection("Projection readiness timed out"); return; }
        if (_LocalStarting) { return; }
        for (auto Old : _LocalRetiring)
        { if (ck::IsValid(Old)) { return; } }
        _LocalRetiring.Empty();
        auto Stations = CkTimerGym::Get_Stations();
        for (auto Station : Stations)
        { if (!CkTimerGym::IsLiveEntity(CkTimerGym::FindStation(Station.Tags[0]))) { return; } }
        _LocalStarting = true;
        _LocalEpochs.Empty();
        for (int32 i = 0; i < 5; i++)
        { _LocalEpochs.Add(Snapshot.Stations[i].SlotEpoch); }
        for (int32 i = 0; i < 5; i++)
        {
            auto Anchor = CkTimerGym::FindStation(Stations[i].Tags[0]);
            auto Params = FCkTimerGym_StationSpawnParams();
            Params.Generation = LocalProjectionGeneration;
            Params.SlotEpoch = Snapshot.Stations[i].SlotEpoch;
            Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
                Anchor.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
            _LocalPending.Add(Request_SpawnLocalChild(i, Anchor, Params));
            _Local.Add(FCk_Handle());
            if (!utils_pending_entity_script::Get_IsValid(_LocalPending[i]))
            { Request_FailLocalProjection("Projection spawn rejected"); return; }
            utils_pending_entity_script::Promise_OnConstructed(_LocalPending[i],
                FCk_Delegate_EntityScript_Constructed(this, n"OnLocalConstructed"));
        }
    }
    private FCk_Handle_PendingEntityScript Request_SpawnLocalChild(int32 InIndex, FCk_Handle InAnchor,
                                                                   FCkTimerGym_StationSpawnParams InParams)
    {
        if (InIndex == 0) { return utils_entity_script::Request_SpawnEntity(InAnchor, UCk_EntityScript_TimerGym_Basics, FInstancedStruct::Make(InParams)); }
        if (InIndex == 1) { return utils_entity_script::Request_SpawnEntity(InAnchor, UCk_EntityScript_TimerGym_Behaviors, FInstancedStruct::Make(InParams)); }
        if (InIndex == 2) { return utils_entity_script::Request_SpawnEntity(InAnchor, UCk_EntityScript_TimerGym_Signals, FInstancedStruct::Make(InParams)); }
        if (InIndex == 3) { return utils_entity_script::Request_SpawnEntity(InAnchor, UCk_EntityScript_TimerGym_Control, FInstancedStruct::Make(InParams)); }
        return utils_entity_script::Request_SpawnEntity(InAnchor, UCk_EntityScript_TimerGym_Countdown, FInstancedStruct::Make(InParams));
    }
    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalStarting || ck::Is_NOT_Valid(InHandle)) { return; }
        auto Child = FCk_Handle(InHandle);
        for (int32 i = 0; i < _LocalPending.Num(); i++)
        {
            if (_LocalPending[i].Get_EntityUnderConstruction() != Child) { continue; }
            if (!CkTimerGym::IsLiveEntity(Child) || CkTimerGym::IsLiveEntity(_Local[i]))
            { Request_FailLocalProjection("Projection completion malformed"); return; }
            _Local[i] = Child; Request_CheckLocalAdmission(); return;
        }
    }
    private void Request_CheckLocalAdmission()
    {
        if (_Local.Num() != 5) { return; }
        if (!Snapshot.Ready)
        {
            Request_RetireLocalProjection();
            _LocalWaitStart = float(System::GetGameTimeInSeconds());
            _LocalProjectionStart = -1.0;
            return;
        }
        if (Snapshot.Ready && Snapshot.Stations.Num() == 5 && _LocalEpochs.Num() == 5)
        {
            for (int32 i = 0; i < 5; i++)
            {
                if (_LocalEpochs[i] != Snapshot.Stations[i].SlotEpoch)
                {
                    Request_RetireLocalProjection();
                    _LocalProjectionStart = float(System::GetGameTimeInSeconds());
                    return;
                }
            }
        }
        for (auto Child : _Local) { if (!CkTimerGym::IsLiveEntity(Child)) { return; } }
        if (!Snapshot.Ready || !Get_IsValidLocalProjection())
        { Request_FailLocalProjection("Projection composition rejected"); return; }
        _LocalStarting = false; LocalProjectionReady = true;
        _LocalEpochs.Empty();
        for (int32 i = 0; i < 5; i++)
        { _LocalEpochs.Add(Snapshot.Stations[i].SlotEpoch); }
    }
    private bool Get_IsValidLocalProjection()
    {
        if (_Local.Num() != 5 || !Get_HasCompleteSnapshot()) { return false; }
        auto Stations = CkTimerGym::Get_Stations();
        for (int32 i = 0; i < 5; i++)
        {
            if (!CkTimerGym::IsLiveEntity(_Local[i]) ||
                utils_entity_lifetime::Get_LifetimeOwner(_Local[i]) != CkTimerGym::FindStation(Stations[i].Tags[0]) ||
                !Get_HasValidProjectionChild(i, _Local[i])) { return false; }
        }
        return true;
    }
    bool Get_HasAdmittedLocalProjection(FCk_Handle InChild, int32 InGeneration,
                                         int32 InEpoch, int32 InIndex)
    {
        if (HasAuthority() || !LocalProjectionReady || LocalProjectionFailed ||
            InGeneration != LocalProjectionGeneration || Snapshot.Generation != InGeneration ||
            InIndex < 0 || InIndex > 4 || _Local.Num() != 5 ||
            !Get_HasCompleteSnapshot() || _Local[InIndex] != InChild ||
            Snapshot.Stations[InIndex].SlotEpoch != InEpoch || !Get_IsValidLocalProjection())
        { return false; }
        return true;
    }
    void Request_DrawProjection(FCk_Handle InChild, int32 InGeneration, int32 InEpoch,
                                int32 InIndex, FString InTitle, FString InInstructions)
    {
        if (!Get_HasAdmittedLocalProjection(InChild, InGeneration, InEpoch, InIndex)) { return; }
        if (GetWorld().GetNetMode() == ENetMode::NM_DedicatedServer) { return; }
        CkGym_Common::Update_StationDisplay(InChild,
            InTitle + " (" + CkGym_Common::Get_NetworkRoleTitle(InChild) + ")",
            Snapshot.Stations[InIndex].Body, InInstructions);
    }
    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason InReason)
    {
        if (HasAuthority() && _AdmissionTicket != 0)
        {
            _Starting = false;
            _ActivationPending = false;
            Snapshot.Ready = false;
            Snapshot.Failed = true;
            Request_CloseWorldOnFailure();
            return;
        }
        _Starting = false; Snapshot.Ready = false; Request_RetireLocalProjection();
        if (HasAuthority())
        {
            Request_RetireAllChildren(); Request_ReleaseAdmission();
            if (CkTimerGym::IsLiveEntity(_WorldStationOwner))
            { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        }
    }
    private bool Get_HasPreparedChild(int32 InIndex, FCk_Handle InChild)
    {
        if (!CkTimerGym::IsLiveEntity(InChild) || InIndex < 0 || InIndex > 4 || _Epochs.Num() != 5) { return false; }
        if (InIndex == 0)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Basics>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPreparedAuthorityComposition();
        }
        if (InIndex == 1)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Behaviors>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPreparedAuthorityComposition();
        }
        if (InIndex == 2)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Signals>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPreparedAuthorityComposition();
        }
        if (InIndex == 3)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Control>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPreparedAuthorityComposition();
        }
        if (InIndex == 4)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Countdown>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPreparedAuthorityComposition();
        }
        return false;
    }
    private bool Request_PrimeChild(int32 InIndex, FCk_Handle InChild)
    {
        if (!CkTimerGym::IsLiveEntity(InChild) || InIndex < 0 || InIndex > 4 || _Epochs.Num() != 5) { return false; }
        if (InIndex == 0)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Basics>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_PrimeAuthority();
        }
        if (InIndex == 1)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Behaviors>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_PrimeAuthority();
        }
        if (InIndex == 2)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Signals>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_PrimeAuthority();
        }
        if (InIndex == 3)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Control>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_PrimeAuthority();
        }
        if (InIndex == 4)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Countdown>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_PrimeAuthority();
        }
        return false;
    }
    private bool Get_HasPrimedChild(int32 InIndex, FCk_Handle InChild)
    {
        if (!CkTimerGym::IsLiveEntity(InChild) || InIndex < 0 || InIndex > 4 || _Epochs.Num() != 5) { return false; }
        if (InIndex == 0)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Basics>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPrimedAuthorityComposition();
        }
        if (InIndex == 1)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Behaviors>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPrimedAuthorityComposition();
        }
        if (InIndex == 2)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Signals>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPrimedAuthorityComposition();
        }
        if (InIndex == 3)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Control>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPrimedAuthorityComposition();
        }
        if (InIndex == 4)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Countdown>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasPrimedAuthorityComposition();
        }
        return false;
    }
    private bool Request_ActivateChild(int32 InIndex, FCk_Handle InChild)
    {
        if (!CkTimerGym::IsLiveEntity(InChild) || InIndex < 0 || InIndex > 4 || _Epochs.Num() != 5) { return false; }
        if (InIndex == 0)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Basics>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_ActivateAuthority();
        }
        if (InIndex == 1)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Behaviors>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_ActivateAuthority();
        }
        if (InIndex == 2)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Signals>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_ActivateAuthority();
        }
        if (InIndex == 3)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Control>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_ActivateAuthority();
        }
        if (InIndex == 4)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Countdown>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Request_ActivateAuthority();
        }
        return false;
    }
    private bool Get_HasValidChild(int32 InIndex, FCk_Handle InChild)
    {
        if (!CkTimerGym::IsLiveEntity(InChild) || InIndex < 0 || InIndex > 4 || _Epochs.Num() != 5) { return false; }
        if (InIndex == 0)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Basics>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 1)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Behaviors>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 2)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Signals>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 3)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Control>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasValidAuthorityComposition();
        }
        if (InIndex == 4)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Countdown>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == ((_CandidateIndex == InIndex && _Candidate == InChild) ? _CandidateEpoch : _Epochs[InIndex]) && Script.StationIndex == InIndex && Script.Get_HasValidAuthorityComposition();
        }
        return false;
    }
    private bool Get_HasValidProjectionChild(int32 InIndex, FCk_Handle InChild)
    {
        if (!CkTimerGym::IsLiveEntity(InChild) || InIndex < 0 || InIndex > 4 || Snapshot.Stations.Num() != 5) { return false; }
        if (InIndex == 0)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Basics>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == Snapshot.Stations[InIndex].SlotEpoch && Script.StationIndex == InIndex && Script.Get_HasValidProjectionComposition();
        }
        if (InIndex == 1)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Behaviors>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == Snapshot.Stations[InIndex].SlotEpoch && Script.StationIndex == InIndex && Script.Get_HasValidProjectionComposition();
        }
        if (InIndex == 2)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Signals>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == Snapshot.Stations[InIndex].SlotEpoch && Script.StationIndex == InIndex && Script.Get_HasValidProjectionComposition();
        }
        if (InIndex == 3)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Control>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == Snapshot.Stations[InIndex].SlotEpoch && Script.StationIndex == InIndex && Script.Get_HasValidProjectionComposition();
        }
        if (InIndex == 4)
        {
            auto Script = Cast<UCk_EntityScript_TimerGym_Countdown>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(InChild));
            return ck::IsValid(Script) && Script.Generation == Snapshot.Generation && Script.SlotEpoch == Snapshot.Stations[InIndex].SlotEpoch && Script.StationIndex == InIndex && Script.Get_HasValidProjectionComposition();
        }
        return false;
    }
}
