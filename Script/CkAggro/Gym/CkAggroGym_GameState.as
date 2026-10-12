// Language=angelscript

namespace CkAggroGym
{
    UCk_EntityScript_UE TryGetScript(FCk_Handle InHandle)
    {
        return UCk_Utils_EntityScript_UE::TryGet_EntityScript(InHandle);
    }

    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        auto Chase = FCkGym_Station_SpawnParams_Payload();
        Chase.Tags.Add(n"Gym.Aggro.Chase");
        Chase.Title = FText::FromString("Aggro + Chase");
        Chase.Description.Add(FText::FromString("Bursts of threat rotate the active target.\nThe guard eases toward whoever it aggros."));
        Stations.Add(Chase);
        auto Perception = FCkGym_Station_SpawnParams_Payload();
        Perception.Tags.Add(n"Gym.Aggro.Perception");
        Perception.Title = FText::FromString("LoS Cone Perception");
        Perception.Description.Add(FText::FromString("An orbiter's threat holds while in the vision cone\nand decays ~5x once it slips out of sight."));
        Stations.Add(Perception);
        auto Stress = FCkGym_Station_SpawnParams_Payload();
        Stress.Tags.Add(n"Gym.Aggro.Stress");
        Stress.Title = FText::FromString("Stress");
        Stress.Description.Add(FText::FromString("24 guards x 5 targets each, all selecting.\nThe visual twin of the ScaleSmoke autotest."));
        Stations.Add(Stress);
        for (int32 i = 0; i < Stations.Num(); i++)
        {
            auto Transform = FTransform::Identity;
            Transform.SetLocation(FVector(500.0, float(i - 1) * 800.0, 0.0));
            Transform.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
            Stations[i].Transform = Transform;
        }
        return Stations;
    }

    FCk_Handle FindStation(FName InTag)
    {
        auto Matches = utils_entity_tag::ForEach_Entity(ck::TransientEntity(), InTag);
        if (Matches.Num() != 1 || ck::Is_NOT_Valid(Matches[0]) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors))
        { return FCk_Handle(); }
        return Matches[0];
    }
}

USTRUCT()
struct FCkAggroGym_Snapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FVector ChaseGuard = FVector::ZeroVector;
    UPROPERTY() TArray<FVector> ChaseDummies;
    UPROPERTY() TArray<float> ChaseThreats;
    UPROPERTY() int32 ChaseActiveIndex = -1;
    UPROPERTY() int32 ChaseTrackedCount = 0;
    UPROPERTY() FVector PerceptionGuard = FVector::ZeroVector;
    UPROPERTY() FVector PerceptionOrbiter = FVector::ZeroVector;
    UPROPERTY() bool PerceptionInCone = false;
    UPROPERTY() float PerceptionThreat = 0.0;
    UPROPERTY() TArray<FVector> StressGuards;
    UPROPERTY() int32 StressActiveMask = 0;
    UPROPERTY() int32 StressActiveCount = 0;
    UPROPERTY() int32 StressGuardCount = 0;
    UPROPERTY() int32 StressTargetsPerGuard = 0;
}

class ACk_AggroGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkAggroGym_Snapshot Snapshot;
    UPROPERTY() int32 ServerTrioCount = 0;
    UPROPERTY() FCk_Handle ServerChase;
    UPROPERTY() FCk_Handle ServerPerception;
    UPROPERTY() FCk_Handle ServerStress;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _StationAdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private TArray<FCk_Handle> _NewStations;
    private TArray<FCk_Handle> _ReusedStations;
    private TArray<FCk_Handle> _RetainedStations;
    private int32 _PendingStationCount = 0;
    private FCk_Handle_PendingEntityScript _PendingChase;
    private FCk_Handle_PendingEntityScript _PendingPerception;
    private FCk_Handle_PendingEntityScript _PendingStress;
    private bool _Starting = false;
    private bool _AdmissionOwned = false;
    private bool _StationsReady = false;
    private bool _ChaseSampleReady = false;
    private bool _PerceptionSampleReady = false;
    private bool _StressSampleReady = false;
    private FCkAggroGym_Snapshot _Staged;
    private int32 _ReadinessAttempts = 0;
    private bool _ServerMonitorArmed = false;
    private int32 _NextGeneration = 0;
    private float _LastPublishSeconds = -1.0;
    private float _GenerationStartSeconds = 0.0;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private bool _RequestedNewStations = false;
    UPROPERTY() int32 LocalProjectionGeneration = 0;
    UPROPERTY() bool LocalProjectionReady = false;
    UPROPERTY() bool LocalProjectionFailed = false;
    private bool _LocalProjectionMonitorArmed = false;
    private bool _LocalProjectionStarting = false;
    private float _LocalProjectionStartSeconds = -1.0;
    private float _LocalServerWaitStartSeconds = 0.0;
    private FCk_Handle_PendingEntityScript _LocalPendingChase;
    private FCk_Handle_PendingEntityScript _LocalPendingPerception;
    private FCk_Handle_PendingEntityScript _LocalPendingStress;
    private FCk_Handle _LocalChase;
    private FCk_Handle _LocalPerception;
    private FCk_Handle _LocalStress;

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog() || Snapshot.Ready || Snapshot.Failed || _Starting)
        { return; }
        Request_StartGeneration();
    }

    bool Request_ResetSharedDemo()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog())
        { return false; }
        if (_AdmissionTicket != 0) { return false; }
        auto WasStarting = _Starting;
        Snapshot = FCkAggroGym_Snapshot();
        Snapshot.Generation = _NextGeneration;
        ForceNetUpdate();
        Request_CancelStartupPreservingAnchors();
        Request_RetireChildren();
        Request_StartGeneration(WasStarting);
        return _Starting && !Snapshot.Failed;
    }

    private void Request_CancelStartupPreservingAnchors()
    {
        _Starting = false;
        for (auto Pending : _PendingStations)
        {
            auto Target = Pending.Get_EntityUnderConstruction();
            if (ck::IsValid(Target) && !_NewStations.Contains(Target))
            { utils_pending_entity_script::Request_DestroyEntity(Pending); }
        }
        _PendingStations.Empty();
        _PendingStationCount = 0;
        for (auto Station : _NewStations) { _RetainedStations.Add(Station); }
        _NewStations.Empty();
        _ReusedStations.Empty();
        _RequestedNewStations = false;
        if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
    }

    private void Request_StartGeneration(bool InDeferStations = false)
    {
        _Starting = true;
        _ReadinessAttempts = 0;
        _GenerationStartSeconds = float(System::GetGameTimeInSeconds());
        _NextGeneration++;
        Snapshot = FCkAggroGym_Snapshot();
        Snapshot.Generation = _NextGeneration;
        ForceNetUpdate();
        _Staged = FCkAggroGym_Snapshot();
        _Staged.Generation = Snapshot.Generation;
        _ChaseSampleReady = false;
        _PerceptionSampleReady = false;
        _StressSampleReady = false;
        _StationsReady = false;
        _LastPublishSeconds = -1.0;
        System::SetTimer(this, n"OnTrioTimeout", 20.0, false);
        if (InDeferStations)
        { System::SetTimer(this, n"OnResetSettle", 0.05, false); }
        else
        { Request_EnsureWorldStations(); }
    }

    UFUNCTION() private void OnResetSettle()
    { if (_Starting && !_AdmissionOwned && !_StationsReady) { Request_EnsureWorldStations(); } }

    // A failed partial station admission is retained until the world restarts.
    private bool Request_HoldFailedAdmission()
    {
        if (_AdmissionTicket == 0) { return false; }
        auto World = ck::TransientEntity();
        CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _StationAdmissionOwner);
        CkGym_Admission::TryCloseWorldUnproven(World);
        return true;
    }

    private void Request_Fail(FString InReason)
    {
        if (Request_HoldFailedAdmission() ||
            CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Starting = false; Snapshot.Ready = false; Snapshot.Failed = true;
            ForceNetUpdate(); ck::Error("[AggroGym] " + InReason);
            return;
        }
        _Starting = false;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        ForceNetUpdate();
        Request_RetireChildren();
        Request_ReleaseAdmission();
        if (ck::IsValid(_WorldStationOwner) && _RequestedNewStations)
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); _WorldStationOwner = FCk_Handle(); }
        _NewStations.Empty();
        _ReusedStations.Empty();
        _PendingStations.Empty();
        _RequestedNewStations = false;
        _RetainedStations.Empty();
        ck::Error("[AggroGym] " + InReason);
    }

    void Request_FailCurrentServerChild(FCk_Handle InChild, int32 InGeneration, FString InReason)
    {
        if (!HasAuthority() || Snapshot.Failed || ServerTrioCount != 3 ||
            ck::Is_NOT_Valid(InChild) || InGeneration != Snapshot.Generation ||
            (InChild != ServerChase && InChild != ServerPerception && InChild != ServerStress))
        { return; }
        Request_Fail(InReason);
    }

    private bool Request_ReleaseAdmission(bool InPublishedComplete = false)
    {
        auto World = ck::TransientEntity();
        if (_AdmissionTicket == 0 && CkGym_Admission::Get_IsClosedUnproven(World))
        { return false; }
        if (_AdmissionTicket != 0)
        {
            if (InPublishedComplete)
            {
                if (!CkGym_Admission::TryReleasePublished(World, _AdmissionTicket, _StationAdmissionOwner))
                { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _StationAdmissionOwner);
                  CkGym_Admission::TryCloseWorldUnproven(World); return false; }
                _AdmissionTicket = 0;
            }
            else
            {
                CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _StationAdmissionOwner);
                CkGym_Admission::TryCloseWorldUnproven(World);
                return false;
            }
        }
        _AdmissionOwned = false;
        if (ck::IsValid(_StationAdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_StationAdmissionOwner); }
        _StationAdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_EnsureWorldStations()
    {
        if (!_Starting || Snapshot.Failed || _AdmissionOwned || _StationsReady) { return; }
        if (_RetainedStations.Num() > 0)
        {
            auto Declarations = CkAggroGym::Get_Stations();
            int32 Visible = 0;
            for (auto Retained : _RetainedStations)
            {
                if (ck::Is_NOT_Valid(Retained)) { Request_Fail("Retained station disappeared"); return; }
                for (auto Declaration : Declarations)
                {
                    if (Retained == CkAggroGym::FindStation(Declaration.Tags[0]))
                    { Visible++; break; }
                }
            }
            if (Visible != _RetainedStations.Num())
            { System::SetTimer(this, n"OnAdmissionWait", 0.1, false); return; }
            _RetainedStations.Empty();
        }
        auto World = ck::TransientEntity();
        if (ck::Is_NOT_Valid(World) || !utils_entity_lifetime::Get_CanCreateEntity(World))
        { Request_Fail("World station owner unavailable"); return; }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { Request_Fail("Station admission permanently closed"); return; }
        if (CkGym_Admission::Get_IsBusy(World))
        {
            System::SetTimer(this, n"OnAdmissionWait", 0.1, false);
            return;
        }
        _AdmissionTicket = CkGym_Admission::TryReserve(World);
        if (_AdmissionTicket == 0)
        { Request_Fail("Station admission reservation rejected"); return; }
        _StationAdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (ck::Is_NOT_Valid(_StationAdmissionOwner))
        { Request_Fail("Station admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _StationAdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _StationAdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _AdmissionOwned = true;
        auto Stations = CkAggroGym::Get_Stations();
        if (Stations.Num() != 3)
        { Request_Fail("Invalid station declaration"); return; }
        // Inspect every declared identity before requesting any new station.
        for (auto Station : Stations)
        {
            if (Station.Tags.Num() != 1 || Station.Tags[0] == FName())
            { Request_Fail("Invalid station tag"); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, Station.Tags[0]);
            if (Matches.Num() > 1 || (Matches.Num() == 1 &&
                (ck::Is_NOT_Valid(Matches[0]) || !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
            { Request_Fail("Ambiguous or incomplete station"); return; }
            if (Matches.Num() == 1) { _ReusedStations.Add(Matches[0]); }
        }
        if (ck::Is_NOT_Valid(_WorldStationOwner))
        {
            _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
            if (ck::Is_NOT_Valid(_WorldStationOwner))
            { Request_Fail("World station hierarchy allocation failed"); return; }
        }
        _PendingStationCount = 0;
        for (auto Station : Stations)
        {
            if (ck::IsValid(CkAggroGym::FindStation(Station.Tags[0]))) { continue; }
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
        if (!_Starting) { return; }
        _ReadinessAttempts++;
        if (float(System::GetGameTimeInSeconds()) - _GenerationStartSeconds >= 20.0)
        { Request_Fail("Station admission timed out"); return; }
        Request_EnsureWorldStations();
    }

    UFUNCTION() private void OnStationSpawnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!_Starting) { return; }
        bool IsCurrent = false;
        for (auto Pending : _PendingStations)
        {
            if (Pending.Get_EntityUnderConstruction() == InOwner) { IsCurrent = true; break; }
        }
        if (IsCurrent && InResult != ECk_Request_OperationResult::Succeeded)
        { Request_Fail("Station spawn failed"); }
    }

    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting) { return; }
        // A null completion has no target identity and may belong to a retired request.
        // The per-target completion result handles a current failure; the deadline
        // covers a missing completion result without poisoning a newer generation.
        if (ck::Is_NOT_Valid(InHandle)) { return; }
        auto Station = FCk_Handle(InHandle);
        bool IsExpected = false;
        for (auto Pending : _PendingStations)
        {
            if (Pending.Get_EntityUnderConstruction() == Station) { IsExpected = true; break; }
        }
        if (!IsExpected) { return; } // completion from a retired generation
        if (utils_entity_lifetime::Get_LifetimeOwner(Station) != _WorldStationOwner ||
            _NewStations.Contains(Station) || _PendingStationCount <= 0)
        { Request_Fail("Unexpected station construction completion"); return; }
        _NewStations.Add(Station);
        _PendingStationCount--;
        if (_PendingStationCount == 0)
        { System::SetTimer(this, n"OnStationSettle", 0.05, false); }
    }

    UFUNCTION() private void OnStationSettle()
    {
        if (_Starting && _AdmissionOwned && _PendingStationCount == 0 && !_StationsReady)
        { Request_CheckStationsSettled(); }
    }

    private void Request_CheckStationsSettled()
    {
        auto Stations = CkAggroGym::Get_Stations();
        for (auto Station : Stations)
        {
            auto Handle = CkAggroGym::FindStation(Station.Tags[0]);
            if (ck::Is_NOT_Valid(Handle) ||
                (!_NewStations.Contains(Handle) && !_ReusedStations.Contains(Handle)))
            { Request_Fail("Station tags did not settle uniquely"); return; }
        }
        if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
        _NewStations.Empty();
        _ReusedStations.Empty();
        _PendingStations.Empty();
        _RequestedNewStations = false;
        _RetainedStations.Empty();
        _StationsReady = true;
        Request_SpawnTrio();
    }

    private void Request_SpawnTrio()
    {
        auto Stations = CkAggroGym::Get_Stations();
        auto Chase = CkAggroGym::FindStation(Stations[0].Tags[0]);
        auto Perception = CkAggroGym::FindStation(Stations[1].Tags[0]);
        auto Stress = CkAggroGym::FindStation(Stations[2].Tags[0]);
        if (ck::Is_NOT_Valid(Chase) || ck::Is_NOT_Valid(Perception) || ck::Is_NOT_Valid(Stress))
        { Request_Fail("Station hierarchy is incomplete"); return; }
        auto Params = FCkAggroGym_Station_SpawnParams();
        Params.Generation = Snapshot.Generation;
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Chase.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        Params.StationTitle = "AGGRO + CHASE";
        Params.StationDescription = "Three attackers ring one guard. Every 2.5s a fresh burst of threat\nlands on the next attacker, so the active target rotates and the guard\nchases whoever it is currently aggro'd on.";
        _PendingChase = utils_entity_script::Request_SpawnEntity(Chase, UCk_EntityScript_AggroGym_Chase_Station, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingChase)) { Request_Fail("Chase spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Perception.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        Params.StationTitle = "LOS CONE PERCEPTION";
        Params.StationDescription = "An attacker orbits the guard. Inside the vision cone it is perceived\nand keeps its threat; outside, threat decays ~5x faster. Perception is a\ncounted vote toggled on cone entry/exit.";
        _PendingPerception = utils_entity_script::Request_SpawnEntity(Perception, UCk_EntityScript_AggroGym_Perception_Station, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingPerception)) { Request_Fail("Perception spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Stress.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        Params.StationTitle = "STRESS";
        Params.StationDescription = "24 guards, each tracking 5 attackers, all running the pacer -> evaluate\n-> select pipeline. A random guard's active target is nudged each 0.5s.\nThe interactive twin of the ScaleSmoke autotest.";
        _PendingStress = utils_entity_script::Request_SpawnEntity(Stress, UCk_EntityScript_AggroGym_Stress_Station, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingStress)) { Request_Fail("Stress spawn rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingChase, FCk_Delegate_EntityScript_Constructed(this, n"OnChaseConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_PendingPerception, FCk_Delegate_EntityScript_Constructed(this, n"OnPerceptionConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_PendingStress, FCk_Delegate_EntityScript_Constructed(this, n"OnStressConstructed"));
    }

    UFUNCTION() private void OnChaseConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed) { return; }
        if (FCk_Handle(InHandle) != _PendingChase.Get_EntityUnderConstruction()) { return; }
        ServerChase = FCk_Handle(InHandle); Request_CheckTrio();
    }
    UFUNCTION() private void OnPerceptionConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed) { return; }
        if (FCk_Handle(InHandle) != _PendingPerception.Get_EntityUnderConstruction()) { return; }
        ServerPerception = FCk_Handle(InHandle); Request_CheckTrio();
    }
    UFUNCTION() private void OnStressConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed) { return; }
        if (FCk_Handle(InHandle) != _PendingStress.Get_EntityUnderConstruction()) { return; }
        ServerStress = FCk_Handle(InHandle); Request_CheckTrio();
    }
    UFUNCTION() private void OnTrioTimeout()
    {
        if (!_Starting || Snapshot.Ready) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _GenerationStartSeconds;
        if (Elapsed < 20.0)
        { System::SetTimer(this, n"OnTrioTimeout", 20.0 - Elapsed, false); return; }
        Request_Fail("Trio readiness timed out");
    }

    private void Request_CheckTrio()
    {
        if (ck::Is_NOT_Valid(ServerChase) || ck::Is_NOT_Valid(ServerPerception) || ck::Is_NOT_Valid(ServerStress))
        { return; }
        if (!Get_IsValidServerTrio())
        { Request_Fail("Trio construction or station ownership is invalid"); return; }
        ServerTrioCount = 3;
        if (!_ServerMonitorArmed)
        {
            _ServerMonitorArmed = true;
            System::SetTimer(this, n"OnServerTrioMonitor", 0.1, true);
        }
        // Remain pending until the first complete three-station sample is published.
    }

    private bool Get_IsValidServerTrio()
    {
        if (ck::Is_NOT_Valid(ServerChase) || ck::Is_NOT_Valid(ServerPerception) || ck::Is_NOT_Valid(ServerStress))
        { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(ServerChase) != CkAggroGym::FindStation(n"Gym.Aggro.Chase") ||
            utils_entity_lifetime::Get_LifetimeOwner(ServerPerception) != CkAggroGym::FindStation(n"Gym.Aggro.Perception") ||
            utils_entity_lifetime::Get_LifetimeOwner(ServerStress) != CkAggroGym::FindStation(n"Gym.Aggro.Stress"))
        { return false; }
        auto ChaseScript = Cast<UCk_EntityScript_AggroGym_Chase_Station>(CkAggroGym::TryGetScript(ServerChase));
        auto PerceptionScript = Cast<UCk_EntityScript_AggroGym_Perception_Station>(CkAggroGym::TryGetScript(ServerPerception));
        auto StressScript = Cast<UCk_EntityScript_AggroGym_Stress_Station>(CkAggroGym::TryGetScript(ServerStress));
        return ck::IsValid(ChaseScript) && ChaseScript.Get_HasValidAuthorityComposition() && ChaseScript.Generation == Snapshot.Generation &&
            ck::IsValid(PerceptionScript) && PerceptionScript.Get_HasValidAuthorityComposition() && PerceptionScript.Generation == Snapshot.Generation &&
            ck::IsValid(StressScript) && StressScript.Get_HasValidAuthorityComposition() && StressScript.Generation == Snapshot.Generation;
    }

    UFUNCTION() private void OnServerTrioMonitor()
    {
        if (!HasAuthority() || Snapshot.Failed || ServerTrioCount != 3) { return; }
        if (!Get_IsValidServerTrio())
        { Request_Fail("Shared trio lost a child, timer, or dependent entity"); return; }
    }

    bool Get_IsCurrentServerChild(FCk_Handle InChild, int32 InGeneration)
    {
        if (!HasAuthority() || InGeneration != Snapshot.Generation || Snapshot.Failed || ServerTrioCount != 3 ||
            (InChild != ServerChase && InChild != ServerPerception && InChild != ServerStress))
        { return false; }
        if (!Get_IsValidServerTrio())
        { Request_Fail("Shared trio lost a child or station"); return false; }
        return true;
    }

    bool IsFinitePoint(FVector InPoint)
    {
        return Math::Abs(InPoint.X) < 100000000.0 &&
            Math::Abs(InPoint.Y) < 100000000.0 && Math::Abs(InPoint.Z) < 100000000.0;
    }

    void SubmitChase(FCk_Handle InChild, int32 InGeneration, FCkAggroGym_Snapshot InSample)
    {
        if (!Get_IsCurrentServerChild(InChild, InGeneration) || InChild != ServerChase) { return; }
        if (InSample.ChaseDummies.Num() != 3 || InSample.ChaseThreats.Num() != 3 ||
            InSample.ChaseTrackedCount < 0 || InSample.ChaseTrackedCount > 3 ||
            InSample.ChaseActiveIndex < -1 || InSample.ChaseActiveIndex > 2 ||
            !IsFinitePoint(InSample.ChaseGuard))
        { Request_Fail("Malformed chase sample"); return; }
        for (int32 i = 0; i < 3; i++)
        {
            if (!IsFinitePoint(InSample.ChaseDummies[i]) ||
                !(InSample.ChaseThreats[i] >= 0.0 && InSample.ChaseThreats[i] < 1000000000.0))
            { Request_Fail("Malformed chase target sample"); return; }
        }
        _Staged.ChaseGuard = InSample.ChaseGuard;
        _Staged.ChaseDummies = InSample.ChaseDummies;
        _Staged.ChaseThreats = InSample.ChaseThreats;
        _Staged.ChaseActiveIndex = InSample.ChaseActiveIndex;
        _Staged.ChaseTrackedCount = InSample.ChaseTrackedCount;
        _ChaseSampleReady = true;
        Request_PublishIfComplete();
    }

    void SubmitPerception(FCk_Handle InChild, int32 InGeneration, FCkAggroGym_Snapshot InSample)
    {
        if (!Get_IsCurrentServerChild(InChild, InGeneration) || InChild != ServerPerception) { return; }
        if (!IsFinitePoint(InSample.PerceptionGuard) || !IsFinitePoint(InSample.PerceptionOrbiter) ||
            !(InSample.PerceptionThreat >= 0.0 && InSample.PerceptionThreat < 1000000000.0))
        { Request_Fail("Malformed perception sample"); return; }
        _Staged.PerceptionGuard = InSample.PerceptionGuard;
        _Staged.PerceptionOrbiter = InSample.PerceptionOrbiter;
        _Staged.PerceptionInCone = InSample.PerceptionInCone;
        _Staged.PerceptionThreat = InSample.PerceptionThreat;
        _PerceptionSampleReady = true;
        Request_PublishIfComplete();
    }

    void SubmitStress(FCk_Handle InChild, int32 InGeneration, FCkAggroGym_Snapshot InSample)
    {
        if (!Get_IsCurrentServerChild(InChild, InGeneration) || InChild != ServerStress) { return; }
        if (InSample.StressGuards.Num() != 24 || InSample.StressGuardCount != 24 ||
            InSample.StressTargetsPerGuard != 5 || InSample.StressActiveCount < 0 ||
            InSample.StressActiveCount > 24 || InSample.StressActiveMask < 0 ||
            InSample.StressActiveMask >= (1 << 24))
        { Request_Fail("Malformed stress sample"); return; }
        for (auto Loc : InSample.StressGuards)
        {
            if (!IsFinitePoint(Loc)) { Request_Fail("Malformed stress location"); return; }
        }
        int32 ActiveBits = 0;
        for (int32 i = 0; i < 24; i++)
        { if ((InSample.StressActiveMask & (1 << i)) != 0) { ActiveBits++; } }
        if (ActiveBits != InSample.StressActiveCount)
        { Request_Fail("Inconsistent stress active count"); return; }
        _Staged.StressGuards = InSample.StressGuards;
        _Staged.StressActiveMask = InSample.StressActiveMask;
        _Staged.StressActiveCount = InSample.StressActiveCount;
        _Staged.StressGuardCount = InSample.StressGuardCount;
        _Staged.StressTargetsPerGuard = InSample.StressTargetsPerGuard;
        _StressSampleReady = true;
        Request_PublishIfComplete();
    }

    private void Request_PublishIfComplete()
    {
        if (!_ChaseSampleReady || !_PerceptionSampleReady || !_StressSampleReady ||
            _Staged.ChaseDummies.Num() != 3 || _Staged.ChaseThreats.Num() != 3 ||
            _Staged.StressGuards.Num() != 24 || _Staged.StressGuardCount != 24 ||
            _Staged.StressTargetsPerGuard != 5) { return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (Snapshot.Ready && _LastPublishSeconds >= 0.0 && Now - _LastPublishSeconds < 0.1)
        { return; }
        _Staged.Sequence = Snapshot.Sequence + 1;
        _Staged.Ready = true;
        _Staged.Failed = false;
        Snapshot = _Staged;
        _Starting = false;
        _LastPublishSeconds = Now;
        ForceNetUpdate();
        _ChaseSampleReady = false;
        _PerceptionSampleReady = false;
        _StressSampleReady = false;
    }

    private void Request_RetireChildren()
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { Request_Fail("Partial station admission cannot retire in this world"); return; }
        ServerTrioCount = 0;
        if (utils_pending_entity_script::Get_IsValid(_PendingChase)) { utils_pending_entity_script::Request_DestroyEntity(_PendingChase); }
        else if (ck::IsValid(ServerChase)) { utils_entity_lifetime::Request_DestroyEntity(ServerChase); }
        if (utils_pending_entity_script::Get_IsValid(_PendingPerception)) { utils_pending_entity_script::Request_DestroyEntity(_PendingPerception); }
        else if (ck::IsValid(ServerPerception)) { utils_entity_lifetime::Request_DestroyEntity(ServerPerception); }
        if (utils_pending_entity_script::Get_IsValid(_PendingStress)) { utils_pending_entity_script::Request_DestroyEntity(_PendingStress); }
        else if (ck::IsValid(ServerStress)) { utils_entity_lifetime::Request_DestroyEntity(ServerStress); }
        ServerChase = FCk_Handle(); ServerPerception = FCk_Handle(); ServerStress = FCk_Handle();
        _PendingChase = FCk_Handle_PendingEntityScript();
        _PendingPerception = FCk_Handle_PendingEntityScript();
        _PendingStress = FCk_Handle_PendingEntityScript();
    }

    // Each client world admits one projection trio, independently of player-controller count.
    void Request_EnsureLocalProjection()
    {
        if (HasAuthority()) { return; }
        if (!_LocalProjectionMonitorArmed)
        {
            _LocalProjectionMonitorArmed = true;
            _LocalServerWaitStartSeconds = float(System::GetGameTimeInSeconds());
            System::SetTimer(this, n"OnLocalProjectionMonitor", 0.1, true);
        }
        Request_CheckLocalProjection();
    }

    bool Get_HasAdmittedLocalProjection(int32 InGeneration)
    {
        if (HasAuthority() || !LocalProjectionReady || LocalProjectionFailed ||
            LocalProjectionGeneration != InGeneration || Snapshot.Generation != InGeneration || !Snapshot.Ready)
        { return false; }
        if (!Get_IsValidLocalProjection())
        { Request_FailLocalProjection("Admitted projection lost a child or station"); return false; }
        return true;
    }

    UFUNCTION() private void OnLocalProjectionMonitor() { Request_CheckLocalProjection(); }

    private void Request_CheckLocalProjection()
    {
        if (HasAuthority() || !_LocalProjectionMonitorArmed) { return; }
        if (Snapshot.Generation != LocalProjectionGeneration)
        {
            Request_RetireLocalProjection();
            LocalProjectionGeneration = Snapshot.Generation;
            LocalProjectionFailed = false;
            _LocalProjectionStartSeconds = -1.0;
            _LocalServerWaitStartSeconds = float(System::GetGameTimeInSeconds());
        }
        if (LocalProjectionFailed) { return; }
        if (LocalProjectionGeneration <= 0)
        {
            if (float(System::GetGameTimeInSeconds()) - _LocalServerWaitStartSeconds >= 25.0)
            { Request_FailLocalProjection("Shared generation did not arrive"); }
            return;
        }
        if (Snapshot.Failed)
        { Request_FailLocalProjection("Shared simulation failed"); return; }
        if (LocalProjectionReady)
        {
            if (!Get_IsValidLocalProjection())
            { Request_FailLocalProjection("Admitted projection lost a child or station"); }
            return;
        }
        if (!Snapshot.Ready)
        {
            if (float(System::GetGameTimeInSeconds()) - _LocalServerWaitStartSeconds >= 25.0)
            { Request_FailLocalProjection("Shared simulation readiness timed out"); }
            return;
        }
        if (_LocalProjectionStartSeconds < 0.0)
        { _LocalProjectionStartSeconds = float(System::GetGameTimeInSeconds()); }
        if (float(System::GetGameTimeInSeconds()) - _LocalProjectionStartSeconds >= 20.0)
        { Request_FailLocalProjection("Projection readiness timed out"); return; }
        if (_LocalProjectionStarting) { return; }
        auto Stations = CkAggroGym::Get_Stations();
        auto Chase = CkAggroGym::FindStation(Stations[0].Tags[0]);
        auto Perception = CkAggroGym::FindStation(Stations[1].Tags[0]);
        auto Stress = CkAggroGym::FindStation(Stations[2].Tags[0]);
        if (ck::Is_NOT_Valid(Chase) || ck::Is_NOT_Valid(Perception) || ck::Is_NOT_Valid(Stress))
        { return; } // base controller admission may still be settling in this client world
        _LocalProjectionStarting = true;
        auto Params = FCkAggroGym_Station_SpawnParams();
        Params.Generation = LocalProjectionGeneration;
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Chase.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        Params.StationTitle = "AGGRO + CHASE";
        Params.StationDescription = "Three attackers ring one guard. Every 2.5s a fresh burst of threat\nlands on the next attacker, so the active target rotates and the guard\nchases whoever it is currently aggro'd on.";
        _LocalPendingChase = utils_entity_script::Request_SpawnEntity(Chase, UCk_EntityScript_AggroGym_Chase_Station, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_LocalPendingChase))
        { Request_FailLocalProjection("Chase projection spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Perception.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        Params.StationTitle = "LOS CONE PERCEPTION";
        Params.StationDescription = "An attacker orbits the guard. Inside the vision cone it is perceived\nand keeps its threat; outside, threat decays ~5x faster. Perception is a\ncounted vote toggled on cone entry/exit.";
        _LocalPendingPerception = utils_entity_script::Request_SpawnEntity(Perception, UCk_EntityScript_AggroGym_Perception_Station, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_LocalPendingPerception))
        { Request_FailLocalProjection("Perception projection spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Stress.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        Params.StationTitle = "STRESS";
        Params.StationDescription = "24 guards, each tracking 5 attackers, all running the pacer -> evaluate\n-> select pipeline. A random guard's active target is nudged each 0.5s.\nThe interactive twin of the ScaleSmoke autotest.";
        _LocalPendingStress = utils_entity_script::Request_SpawnEntity(Stress, UCk_EntityScript_AggroGym_Stress_Station, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_LocalPendingStress))
        { Request_FailLocalProjection("Stress projection spawn rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_LocalPendingChase, FCk_Delegate_EntityScript_Constructed(this, n"OnLocalChaseConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_LocalPendingPerception, FCk_Delegate_EntityScript_Constructed(this, n"OnLocalPerceptionConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_LocalPendingStress, FCk_Delegate_EntityScript_Constructed(this, n"OnLocalStressConstructed"));
    }

    UFUNCTION() private void OnLocalChaseConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalProjectionStarting || FCk_Handle(InHandle) != _LocalPendingChase.Get_EntityUnderConstruction()) { return; }
        _LocalChase = FCk_Handle(InHandle); Request_CheckLocalProjectionAdmission();
    }
    UFUNCTION() private void OnLocalPerceptionConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalProjectionStarting || FCk_Handle(InHandle) != _LocalPendingPerception.Get_EntityUnderConstruction()) { return; }
        _LocalPerception = FCk_Handle(InHandle); Request_CheckLocalProjectionAdmission();
    }
    UFUNCTION() private void OnLocalStressConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalProjectionStarting || FCk_Handle(InHandle) != _LocalPendingStress.Get_EntityUnderConstruction()) { return; }
        _LocalStress = FCk_Handle(InHandle); Request_CheckLocalProjectionAdmission();
    }

    private void Request_CheckLocalProjectionAdmission()
    {
        if (ck::Is_NOT_Valid(_LocalChase) || ck::Is_NOT_Valid(_LocalPerception) || ck::Is_NOT_Valid(_LocalStress))
        { return; }
        if (utils_entity_lifetime::Get_LifetimeOwner(_LocalChase) != CkAggroGym::FindStation(n"Gym.Aggro.Chase") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalPerception) != CkAggroGym::FindStation(n"Gym.Aggro.Perception") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalStress) != CkAggroGym::FindStation(n"Gym.Aggro.Stress") ||
            !Snapshot.Ready || Snapshot.Generation != LocalProjectionGeneration)
        { Request_FailLocalProjection("Projection completion has wrong station or generation"); return; }
        auto ChaseScript = Cast<UCk_EntityScript_AggroGym_Chase_Station>(CkAggroGym::TryGetScript(_LocalChase));
        auto PerceptionScript = Cast<UCk_EntityScript_AggroGym_Perception_Station>(CkAggroGym::TryGetScript(_LocalPerception));
        auto StressScript = Cast<UCk_EntityScript_AggroGym_Stress_Station>(CkAggroGym::TryGetScript(_LocalStress));
        if (ck::Is_NOT_Valid(ChaseScript) || !ChaseScript.ProjectionConstructed || ChaseScript.Generation != LocalProjectionGeneration ||
            ck::Is_NOT_Valid(PerceptionScript) || !PerceptionScript.ProjectionConstructed || PerceptionScript.Generation != LocalProjectionGeneration ||
            ck::Is_NOT_Valid(StressScript) || !StressScript.ProjectionConstructed || StressScript.Generation != LocalProjectionGeneration)
        { Request_FailLocalProjection("Projection construction rejected its client role"); return; }
        _LocalProjectionStarting = false;
        LocalProjectionReady = true;
    }

    private bool Get_IsValidLocalProjection()
    {
        if (ck::Is_NOT_Valid(_LocalChase) || ck::Is_NOT_Valid(_LocalPerception) || ck::Is_NOT_Valid(_LocalStress) ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalChase) != CkAggroGym::FindStation(n"Gym.Aggro.Chase") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalPerception) != CkAggroGym::FindStation(n"Gym.Aggro.Perception") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalStress) != CkAggroGym::FindStation(n"Gym.Aggro.Stress"))
        { return false; }
        auto ChaseScript = Cast<UCk_EntityScript_AggroGym_Chase_Station>(CkAggroGym::TryGetScript(_LocalChase));
        auto PerceptionScript = Cast<UCk_EntityScript_AggroGym_Perception_Station>(CkAggroGym::TryGetScript(_LocalPerception));
        auto StressScript = Cast<UCk_EntityScript_AggroGym_Stress_Station>(CkAggroGym::TryGetScript(_LocalStress));
        return ck::IsValid(ChaseScript) && ChaseScript.ProjectionConstructed && ChaseScript.Generation == LocalProjectionGeneration &&
            ck::IsValid(PerceptionScript) && PerceptionScript.ProjectionConstructed && PerceptionScript.Generation == LocalProjectionGeneration &&
            ck::IsValid(StressScript) && StressScript.ProjectionConstructed && StressScript.Generation == LocalProjectionGeneration;
    }

    private void Request_FailLocalProjection(FString InReason)
    {
        Request_RetireLocalProjection();
        LocalProjectionFailed = true;
        ck::Error("[AggroGym] " + InReason);
    }

    private void Request_RetireLocalProjection()
    {
        LocalProjectionReady = false;
        _LocalProjectionStarting = false;
        if (utils_pending_entity_script::Get_IsValid(_LocalPendingChase))
        { utils_pending_entity_script::Request_DestroyEntity(_LocalPendingChase); }
        else if (ck::IsValid(_LocalChase)) { utils_entity_lifetime::Request_DestroyEntity(_LocalChase); }
        if (utils_pending_entity_script::Get_IsValid(_LocalPendingPerception))
        { utils_pending_entity_script::Request_DestroyEntity(_LocalPendingPerception); }
        else if (ck::IsValid(_LocalPerception)) { utils_entity_lifetime::Request_DestroyEntity(_LocalPerception); }
        if (utils_pending_entity_script::Get_IsValid(_LocalPendingStress))
        { utils_pending_entity_script::Request_DestroyEntity(_LocalPendingStress); }
        else if (ck::IsValid(_LocalStress)) { utils_entity_lifetime::Request_DestroyEntity(_LocalStress); }
        _LocalPendingChase = FCk_Handle_PendingEntityScript();
        _LocalPendingPerception = FCk_Handle_PendingEntityScript();
        _LocalPendingStress = FCk_Handle_PendingEntityScript();
        _LocalChase = FCk_Handle(); _LocalPerception = FCk_Handle(); _LocalStress = FCk_Handle();
    }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason InReason)
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Starting = false; Snapshot.Ready = false;
            Request_HoldFailedAdmission();
            return;
        }
        _Starting = false;
        Snapshot.Ready = false;
        Request_RetireLocalProjection();
        if (HasAuthority())
        {
            Request_RetireChildren();
            Request_ReleaseAdmission();
        }
    }
}
