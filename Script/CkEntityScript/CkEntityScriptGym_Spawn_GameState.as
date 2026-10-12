// Language=angelscript

USTRUCT()
struct FCkEntityScriptGym_SpawnSample
{
    UPROPERTY() FName TestName = n"";
    UPROPERTY() int32 TestInt = 0;
    UPROPERTY() float32 TestFloat = 0.0f;
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() bool NamePass = false;
    UPROPERTY() bool IntPass = false;
    UPROPERTY() bool FloatPass = false;
}

USTRUCT()
struct FCkEntityScriptGym_SpawnSnapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() int32 PlainEpoch = 0;
    UPROPERTY() int32 ReplicatedEpoch = 0;
    UPROPERTY() FCkEntityScriptGym_SpawnSample Plain;
    UPROPERTY() FCkEntityScriptGym_SpawnSample Replicated;
}

class ACk_EntityScriptGym_Spawn_GameState : ACkGym_GameState_UE
{
    default bReplicateUsingRegisteredSubObjectList = true;
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkEntityScriptGym_SpawnSnapshot Snapshot;
    UPROPERTY() FCk_Handle ServerPlain;
    UPROPERTY() FCk_Handle ServerReplicated;
    UPROPERTY() FCk_Handle ServerReplicatedRoot;
    UPROPERTY() int32 ServerCohortCount = 0;
    UPROPERTY() int32 LocalPresentationGeneration = 0;
    UPROPERTY() bool LocalPresentationReady = false;
    UPROPERTY() bool LocalPresentationFailed = false;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _AdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private bool _AdmissionOwned = false;
    private bool _Ending = false;
    private bool _Starting = false;
    private bool _StationsReady = false;
    private int32 _PendingStationCount = 0;
    private TArray<FCk_Handle> _NewStations;
    private TArray<FCk_Handle> _ReusedStations;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private FCk_Handle_PendingEntityScript _PendingRoot;
    private FCk_Handle_PendingEntityScript _PendingPlain;
    private FCk_Handle_PendingEntityScript _PendingReplicated;
    private TArray<FCk_Handle> _Retiring;
    private int32 _RerunBranch = -1;
    private ACk_EntityScriptGym_Spawn_PlayerController _PendingControlOwner;
    private int32 _PendingControlSerial = 0;
    private int32 _NextGeneration = 0;
    private float _StartSeconds = 0.0;
    private bool _MonitorArmed = false;
    private bool _LocalMonitorArmed = false;
    private bool _LocalStarting = false;
    private float _LocalWaitSeconds = 0.0;
    private FCk_Handle_PendingEntityScript _PendingLocalPlain;
    private FCk_Handle_PendingEntityScript _PendingLocalRep;
    private FCk_Handle _LocalPlain;
    private FCk_Handle _LocalRep;
    private TArray<FCk_Handle> _LocalRetiring;

    void Request_EnsureSharedDemo()
    {
        if (_Ending || !HasAuthority() || !Get_HasPublishedCatalog() || _Starting || Snapshot.Ready || Snapshot.Failed)
        { return; }
        _NextGeneration++;
        Snapshot = FCkEntityScriptGym_SpawnSnapshot();
        Snapshot.Generation = _NextGeneration;
        Snapshot.PlainEpoch = 1;
        Snapshot.ReplicatedEpoch = 1;
        _Starting = true;
        _StartSeconds = float(System::GetGameTimeInSeconds());
        ForceNetUpdate();
        if (!_MonitorArmed)
        {
            _MonitorArmed = true;
            System::SetTimer(this, n"OnMonitor", 0.1, true);
        }
        Request_EnsureWorldStations();
    }

    bool Request_RerunBranch(int32 InBranch,
        ACk_EntityScriptGym_Spawn_PlayerController InOwner, int32 InSerial)
    {
        if (_Ending || !Get_HasReadyServerCohort() || InBranch < 0 || InBranch > 1 || _Starting ||
            ck::Is_NOT_Valid(InOwner) || InOwner.GetWorld() != GetWorld() ||
            InSerial <= 0 || InSerial <= InOwner.LastControlSerial)
        { return false; }
        auto Old = InBranch == 0 ? ServerPlain : ServerReplicated;
        if (!CkEntityScriptGym_Spawn::IsLive(Old)) { Request_Fail("Selected branch was lost"); return false; }
        Snapshot.Ready = false;
        ForceNetUpdate();
        _Starting = true;
        _RerunBranch = InBranch;
        _PendingControlOwner = InOwner;
        _PendingControlSerial = InSerial;
        _StartSeconds = float(System::GetGameTimeInSeconds());
        Request_CaptureRetiring(Old);
        utils_entity_lifetime::Request_DestroyEntity(Old);
        if (InBranch == 0) { ServerPlain = FCk_Handle(); }
        else { ServerReplicated = FCk_Handle(); }
        ServerCohortCount = 1;
        return true;
    }

    private void Request_CaptureRetiring(FCk_Handle InRoot)
    {
        if (ck::Is_NOT_Valid(InRoot) || _Retiring.Contains(InRoot)) { return; }
        _Retiring.Add(InRoot);
        for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
        { Request_CaptureRetiring(Child); }
    }

    private bool Get_RetiringComplete()
    {
        for (auto Handle : _Retiring)
        {
            // Native total query remains true through BeginDestroy, Teardown and
            // Finalize, including after default ck::IsValid becomes false.
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle))
            { return false; }
        }
        return true;
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
                if (!CkGym_Admission::TryReleasePublished(World, _AdmissionTicket, _AdmissionOwner))
                { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
                  CkGym_Admission::TryCloseWorldUnproven(World); return false; }
                _AdmissionTicket = 0;
            }
            else
            {
                CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
                CkGym_Admission::TryCloseWorldUnproven(World);
                return false;
            }
        }
        _AdmissionOwned = false;
        if (ck::IsValid(_AdmissionOwner)) { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_EnsureWorldStations()
    {
        if (!_Starting || Snapshot.Failed || _StationsReady || _AdmissionOwned) { return; }
        auto World = ck::TransientEntity();
        if (!CkEntityScriptGym_Spawn::IsLive(World) || !utils_entity_lifetime::Get_CanCreateEntity(World))
        { Request_Fail("World owner unavailable"); return; }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { Request_Fail("Station admission permanently closed"); return; }
        if (CkGym_Admission::Get_IsBusy(World)) { return; }
        _AdmissionTicket = CkGym_Admission::TryReserve(World);
        if (_AdmissionTicket == 0)
        { Request_Fail("Station admission reservation rejected"); return; }
        _AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (ck::Is_NOT_Valid(_AdmissionOwner)) { Request_Fail("Station admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _AdmissionOwned = true;
        auto Stations = CkEntityScriptGym_Spawn::Get_Stations();
        if (Stations.Num() != 2) { Request_Fail("Station declarations changed"); return; }
        for (auto Station : Stations)
        {
            if (Station.Tags.Num() != 1 || Station.Tags[0] == FName())
            { Request_Fail("Malformed station declaration"); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, Station.Tags[0]);
            if (Matches.Num() > 1 || (Matches.Num() == 1 &&
                (!CkEntityScriptGym_Spawn::IsLive(Matches[0]) ||
                 !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
            { Request_Fail("Ambiguous or incomplete world station"); return; }
            if (Matches.Num() == 1) { _ReusedStations.Add(Matches[0]); }
        }
        if (ck::Is_NOT_Valid(_WorldStationOwner))
        {
            _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
            if (ck::Is_NOT_Valid(_WorldStationOwner))
            { Request_Fail("World station owner allocation failed"); return; }
        }
        for (auto Station : Stations)
        {
            if (CkEntityScriptGym_Spawn::IsLive(CkEntityScriptGym_Spawn::FindStation(Station.Tags[0])))
            { continue; }
            _PendingStationCount++;
            auto Pending = CkGym_Common::Request_SpawnNewStationForOwner(Station, _WorldStationOwner,
                FCk_Delegate_Request_OnCompleted(this, n"OnStationSpawnCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_Fail("Station spawn rejected"); return; }
            _PendingStations.Add(Pending);
            utils_pending_entity_script::Promise_OnConstructed(Pending,
                FCk_Delegate_EntityScript_Constructed(this, n"OnStationConstructed"));
        }
        if (_PendingStationCount == 0) { Request_CheckStations(); }
    }

    UFUNCTION() private void OnStationSpawnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (!_Starting) { return; }
        bool IsCurrent = false;
        for (auto Pending : _PendingStations)
        { if (Pending.Get_EntityUnderConstruction() == InOwner) { IsCurrent = true; break; } }
        if (IsCurrent && InResult != ECk_Request_OperationResult::Succeeded)
        { Request_Fail("Station spawn failed"); }
    }

    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle)) { return; }
        auto Handle = FCk_Handle(InHandle);
        bool IsCurrent = false;
        for (auto Pending : _PendingStations)
        { if (Pending.Get_EntityUnderConstruction() == Handle) { IsCurrent = true; break; } }
        if (!IsCurrent) { return; }
        if (!CkEntityScriptGym_Spawn::IsDirectChild(Handle, _WorldStationOwner) ||
            _NewStations.Contains(Handle) || _PendingStationCount <= 0)
        { Request_Fail("Invalid station construction"); return; }
        _NewStations.Add(Handle);
        _PendingStationCount--;
        if (_PendingStationCount == 0)
        { System::SetTimer(this, n"OnStationSettle", 0.05, false); }
    }

    UFUNCTION() private void OnStationSettle() { if (_Starting) { Request_CheckStations(); } }

    private void Request_CheckStations()
    {
        if (!_Starting || _PendingStationCount != 0) { return; }
        auto Stations = CkEntityScriptGym_Spawn::Get_Stations();
        for (auto Station : Stations)
        {
            auto Handle = CkEntityScriptGym_Spawn::FindStation(Station.Tags[0]);
            if (ck::Is_NOT_Valid(Handle) ||
                (!_NewStations.Contains(Handle) && !_ReusedStations.Contains(Handle)))
            { Request_Fail("Station tag settlement failed"); return; }
        }
        if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
        _PendingStations.Empty(); _NewStations.Empty(); _ReusedStations.Empty();
        _StationsReady = true;
        Request_SpawnRootAndPlain();
    }

    private void Request_SpawnRootAndPlain()
    {
        if (!CkEntityScriptGym_Spawn::IsLive(CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn")) ||
            !CkEntityScriptGym_Spawn::IsLive(CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.SpawnReplicated")))
        { Request_Fail("Station missing before cohort spawn"); return; }
        _PendingRoot = utils_entity_script_with_actor::Request_SpawnEntityScript_OnActor(
            this, UCk_EntityScript_EntityScriptGym_WorldRoot);
        if (!utils_pending_entity_script::Get_IsValid(_PendingRoot))
        { Request_Fail("Replicated world root rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingRoot,
            FCk_Delegate_EntityScript_Constructed(this, n"OnRootConstructed"));
        Request_SpawnBranch(0);
    }

    private void Request_SpawnBranch(int32 InBranch)
    {
        auto Station = CkEntityScriptGym_Spawn::FindStation(InBranch == 0 ?
            n"Gym.EntityScript.Spawn" : n"Gym.EntityScript.SpawnReplicated");
        auto Owner = InBranch == 0 ? Station : ServerReplicatedRoot;
        if (!CkEntityScriptGym_Spawn::IsLive(Station) ||
            !CkEntityScriptGym_Spawn::IsLive(Owner) ||
            !Station.Has_Fragment(FCkGym_Station_Anchors))
        { Request_Fail("Branch owner or anchor missing"); return; }
        auto Params = FEntityScriptGym_SpawnParams();
        Params.InitialTransform = CkEntityScriptGym_Spawn::GetPanelTransform(Station);
        Params.TestName = n"GymTestEntity";
        Params.TestInt = 42;
        Params.TestFloat = 3.14f;
        if (InBranch == 0)
        {
            _PendingPlain = utils_entity_script::Request_SpawnEntity(Owner,
                UCk_EntityScript_EntityScriptGym_Spawn, FInstancedStruct::Make(Params));
            if (!utils_pending_entity_script::Get_IsValid(_PendingPlain))
            { Request_Fail("Plain branch spawn rejected"); return; }
            utils_pending_entity_script::Promise_OnConstructed(_PendingPlain,
                FCk_Delegate_EntityScript_Constructed(this, n"OnPlainConstructed"));
        }
        else
        {
            _PendingReplicated = utils_entity_script::Request_SpawnEntity(Owner,
                UCk_EntityScript_EntityScriptGym_SpawnReplicated, FInstancedStruct::Make(Params));
            if (!utils_pending_entity_script::Get_IsValid(_PendingReplicated))
            { Request_Fail("Replicated branch spawn rejected"); return; }
            utils_pending_entity_script::Promise_OnConstructed(_PendingReplicated,
                FCk_Delegate_EntityScript_Constructed(this, n"OnReplicatedConstructed"));
        }
    }

    UFUNCTION() private void OnRootConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle) ||
            FCk_Handle(InHandle) != _PendingRoot.Get_EntityUnderConstruction()) { return; }
        ServerReplicatedRoot = FCk_Handle(InHandle);
        if (!CkEntityScriptGym_Spawn::IsLive(ServerReplicatedRoot) ||
            utils_owning_actor::TryGet_ActorEntityHandle(this) != ServerReplicatedRoot ||
            !utils_entity_replication_driver::Has(ServerReplicatedRoot))
        { Request_Fail("Actor-linked replication root incomplete"); return; }
        Request_SpawnBranch(1);
    }

    UFUNCTION() private void OnPlainConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle) ||
            FCk_Handle(InHandle) != _PendingPlain.Get_EntityUnderConstruction()) { return; }
        ServerPlain = FCk_Handle(InHandle);
        Request_CheckCohort();
    }

    UFUNCTION() private void OnReplicatedConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle) ||
            FCk_Handle(InHandle) != _PendingReplicated.Get_EntityUnderConstruction()) { return; }
        ServerReplicated = FCk_Handle(InHandle);
        Request_CheckCohort();
    }

    private bool Request_TrySample(FCk_Handle InHandle, FCk_Handle InOwner,
        bool InReplicated, FCkEntityScriptGym_SpawnSample& Out)
    {
        if (!CkEntityScriptGym_Spawn::IsDirectChild(InHandle, InOwner) ||
            !utils_entity_script::Has(InHandle) ||
            !InHandle.Has_Fragment(FEntityScriptGym_SpawnParams) ||
            !utils_transform::Has(InHandle) ||
            utils_net::Get_EntityReplication(InHandle) != (InReplicated ?
                ECk_Replication::Replicates : ECk_Replication::DoesNotReplicate))
        { return false; }
        auto Script = utils_entity_script::TryGet_EntityScript(InHandle);
        if (InReplicated && ck::Is_NOT_Valid(Cast<UCk_EntityScript_EntityScriptGym_SpawnReplicated>(Script)))
        { return false; }
        if (!InReplicated && ck::Is_NOT_Valid(Cast<UCk_EntityScript_EntityScriptGym_Spawn>(Script)))
        { return false; }
        if (InReplicated && !utils_entity_replication_driver::Has(InHandle)) { return false; }
        auto Params = InHandle.Get_Fragment(FEntityScriptGym_SpawnParams);
        if (!(Params.TestFloat > -1000000.0f && Params.TestFloat < 1000000.0f) ||
            Math::Abs(Params.InitialTransform.Location.X) >= 100000000.0 ||
            Math::Abs(Params.InitialTransform.Location.Y) >= 100000000.0 ||
            Math::Abs(Params.InitialTransform.Location.Z) >= 100000000.0)
        { return false; }
        Out.TestName = Params.TestName;
        Out.TestInt = Params.TestInt;
        Out.TestFloat = Params.TestFloat;
        Out.InitialTransform = Params.InitialTransform;
        Out.NamePass = Params.TestName == n"GymTestEntity";
        Out.IntPass = Params.TestInt == 42;
        Out.FloatPass = Math::Abs(Params.TestFloat - 3.14f) < 0.001f;
        return true;
    }

    private bool Get_IsValidServerCohort()
    {
        if (!CkEntityScriptGym_Spawn::IsLive(ServerReplicatedRoot) ||
            !CkEntityScriptGym_Spawn::IsLive(ServerPlain) ||
            !CkEntityScriptGym_Spawn::IsLive(ServerReplicated) ||
            utils_owning_actor::TryGet_ActorEntityHandle(this) != ServerReplicatedRoot ||
            !utils_entity_replication_driver::Has(ServerReplicatedRoot) ||
            utils_net::Get_EntityReplication(ServerReplicatedRoot) != ECk_Replication::Replicates ||
            ck::Is_NOT_Valid(Cast<UCk_EntityScript_EntityScriptGym_WorldRoot>(
                utils_entity_script::TryGet_EntityScript(ServerReplicatedRoot))))
        { return false; }
        auto PlainStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn");
        auto RepStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.SpawnReplicated");
        if (!CkEntityScriptGym_Spawn::IsLive(PlainStation) ||
            !CkEntityScriptGym_Spawn::IsLive(RepStation)) { return false; }
        auto PlainMatches = utils_entity_tag::ForEach_Entity(PlainStation,
            n"TAG_EntityScriptGym_Spawn");
        auto RepMatches = utils_entity_tag::ForEach_Entity(ServerReplicatedRoot,
            n"TAG_EntityScriptGym_SpawnReplicated");
        if (PlainMatches.Num() != 1 || PlainMatches[0] != ServerPlain ||
            RepMatches.Num() != 1 || RepMatches[0] != ServerReplicated)
        { return false; }
        FCkEntityScriptGym_SpawnSample Plain;
        FCkEntityScriptGym_SpawnSample Rep;
        return Request_TrySample(ServerPlain, PlainStation, false, Plain) &&
            Request_TrySample(ServerReplicated, ServerReplicatedRoot, true, Rep) &&
            Plain.NamePass && Plain.IntPass && Plain.FloatPass &&
            Rep.NamePass && Rep.IntPass && Rep.FloatPass &&
            Plain.InitialTransform.GetLocation() == CkEntityScriptGym_Spawn::GetPanelTransform(PlainStation).GetLocation() &&
            Rep.InitialTransform.GetLocation() == CkEntityScriptGym_Spawn::GetPanelTransform(RepStation).GetLocation();
    }

    bool Get_HasReadyServerCohort()
    {
        return !_Ending && HasAuthority() && Snapshot.Ready && !Snapshot.Failed &&
            ServerCohortCount == 2 && Get_HasCompleteSnapshot() && Get_IsValidServerCohort();
    }

    private void Request_CheckCohort()
    {
        if (!_Starting || ck::Is_NOT_Valid(ServerPlain) || ck::Is_NOT_Valid(ServerReplicated)) { return; }
        // EntityTag::Add drains after construction. An absent tag is pending
        // settlement; a duplicate or wrong settled identity is malformed.
        auto PlainStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn");
        if (!CkEntityScriptGym_Spawn::IsLive(PlainStation))
        { Request_Fail("Plain station disappeared"); return; }
        auto PlainMatches = utils_entity_tag::ForEach_Entity(PlainStation,
            n"TAG_EntityScriptGym_Spawn");
        auto RepMatches = utils_entity_tag::ForEach_Entity(ServerReplicatedRoot,
            n"TAG_EntityScriptGym_SpawnReplicated");
        if (PlainMatches.Num() > 1 || RepMatches.Num() > 1 ||
            (PlainMatches.Num() == 1 && PlainMatches[0] != ServerPlain) ||
            (RepMatches.Num() == 1 && RepMatches[0] != ServerReplicated))
        { Request_Fail("Cohort tags settled ambiguously"); return; }
        if (PlainMatches.Num() == 0 || RepMatches.Num() == 0) { return; }
        if (!Get_IsValidServerCohort()) { Request_Fail("Cohort construction incomplete"); return; }
        FCkEntityScriptGym_SpawnSample Plain;
        FCkEntityScriptGym_SpawnSample Rep;
        if (!Request_TrySample(ServerPlain,
                CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn"), false, Plain) ||
            !Request_TrySample(ServerReplicated, ServerReplicatedRoot, true, Rep))
        { Request_Fail("Cohort sample invalid"); return; }
        Snapshot.Plain = Plain;
        Snapshot.Replicated = Rep;
        Snapshot.Sequence++;
        Snapshot.Ready = true;
        ServerCohortCount = 2;
        _Starting = false;
        _RerunBranch = -1;
        ForceNetUpdate();
        if (ck::IsValid(_PendingControlOwner) &&
            _PendingControlOwner.GetWorld() == GetWorld() && _PendingControlSerial > 0)
        { _PendingControlOwner.Request_OnSharedControlCommitted(_PendingControlSerial, true); }
        _PendingControlOwner = nullptr;
        _PendingControlSerial = 0;
    }

    bool Get_HasCompleteSnapshot()
    {
        if (!Snapshot.Ready || Snapshot.Failed || Snapshot.Generation <= 0 ||
            Snapshot.Sequence <= 0 || Snapshot.PlainEpoch <= 0 || Snapshot.ReplicatedEpoch <= 0)
        { return false; }
        auto Plain = Snapshot.Plain;
        auto Rep = Snapshot.Replicated;
        return Plain.NamePass && Plain.IntPass && Plain.FloatPass &&
            Rep.NamePass && Rep.IntPass && Rep.FloatPass &&
            Plain.TestName == n"GymTestEntity" && Rep.TestName == n"GymTestEntity" &&
            Plain.TestInt == 42 && Rep.TestInt == 42 &&
            Math::Abs(Plain.TestFloat - 3.14f) < 0.001f &&
            Math::Abs(Rep.TestFloat - 3.14f) < 0.001f &&
            Math::Abs(Plain.InitialTransform.GetLocation().X) < 100000000.0 &&
            Math::Abs(Plain.InitialTransform.GetLocation().Y) < 100000000.0 &&
            Math::Abs(Plain.InitialTransform.GetLocation().Z) < 100000000.0 &&
            Math::Abs(Rep.InitialTransform.GetLocation().X) < 100000000.0 &&
            Math::Abs(Rep.InitialTransform.GetLocation().Y) < 100000000.0 &&
            Math::Abs(Rep.InitialTransform.GetLocation().Z) < 100000000.0;
    }

    FCk_Handle Get_LocalVerifiedReplica()
    {
        if (!Get_HasCompleteSnapshot()) { return FCk_Handle(); }
        auto Root = utils_owning_actor::TryGet_ActorEntityHandle(this);
        if (!CkEntityScriptGym_Spawn::IsLive(Root) ||
            !utils_entity_replication_driver::Has(Root)) { return FCk_Handle(); }
        auto Matches = utils_entity_tag::ForEach_Entity(Root, n"TAG_EntityScriptGym_SpawnReplicated");
        if (Matches.Num() != 1) { return FCk_Handle(); }
        FCkEntityScriptGym_SpawnSample Local;
        if (!Request_TrySample(Matches[0], Root, true, Local) ||
            Local.TestName != Snapshot.Replicated.TestName ||
            Local.TestInt != Snapshot.Replicated.TestInt ||
            Math::Abs(Local.TestFloat - Snapshot.Replicated.TestFloat) >= 0.001f ||
            Local.InitialTransform.GetLocation() != Snapshot.Replicated.InitialTransform.GetLocation())
        { return FCk_Handle(); }
        return Matches[0];
    }

    void Request_EnsureLocalPresentation()
    {
        if (_Ending || _LocalMonitorArmed) { return; }
        _LocalMonitorArmed = true;
        _LocalWaitSeconds = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnLocalMonitor", 0.1, true);
        OnLocalMonitor();
    }

    private void Request_RetireLocalPresentation()
    {
        LocalPresentationReady = false;
        _LocalStarting = false;
        if (CkEntityScriptGym_Spawn::IsLive(_LocalPlain) && !_LocalRetiring.Contains(_LocalPlain))
        { _LocalRetiring.Add(_LocalPlain); }
        if (CkEntityScriptGym_Spawn::IsLive(_LocalRep) && !_LocalRetiring.Contains(_LocalRep))
        { _LocalRetiring.Add(_LocalRep); }
        if (utils_pending_entity_script::Get_IsValid(_PendingLocalPlain))
        {
            auto Target = _PendingLocalPlain.Get_EntityUnderConstruction();
            if (ck::IsValid(Target) && !_LocalRetiring.Contains(Target)) { _LocalRetiring.Add(Target); }
        }
        if (utils_pending_entity_script::Get_IsValid(_PendingLocalRep))
        {
            auto Target = _PendingLocalRep.Get_EntityUnderConstruction();
            if (ck::IsValid(Target) && !_LocalRetiring.Contains(Target)) { _LocalRetiring.Add(Target); }
        }
        if (utils_pending_entity_script::Get_IsValid(_PendingLocalPlain))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingLocalPlain); }
        else if (CkEntityScriptGym_Spawn::IsLive(_LocalPlain))
        { utils_entity_lifetime::Request_DestroyEntity(_LocalPlain); }
        if (utils_pending_entity_script::Get_IsValid(_PendingLocalRep))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingLocalRep); }
        else if (CkEntityScriptGym_Spawn::IsLive(_LocalRep))
        { utils_entity_lifetime::Request_DestroyEntity(_LocalRep); }
        _PendingLocalPlain = FCk_Handle_PendingEntityScript();
        _PendingLocalRep = FCk_Handle_PendingEntityScript();
        _LocalPlain = FCk_Handle(); _LocalRep = FCk_Handle();
    }

    private void Request_FailLocalPresentation(FString InReason)
    {
        if (_Ending) { return; }
        Request_RetireLocalPresentation();
        LocalPresentationFailed = true;
        ck::Error("[EntityScriptGym] Local presentation: " + InReason);
    }

    private bool Get_IsValidLocalPresentation()
    {
        auto PlainStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn");
        auto RepStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.SpawnReplicated");
        if (!CkEntityScriptGym_Spawn::IsDirectChild(_LocalPlain, PlainStation) ||
            !CkEntityScriptGym_Spawn::IsDirectChild(_LocalRep, RepStation))
        { return false; }
        auto PlainMatches = utils_entity_tag::ForEach_Entity(PlainStation,
            n"TAG_EntityScriptGym_PresentationPlain");
        auto RepMatches = utils_entity_tag::ForEach_Entity(RepStation,
            n"TAG_EntityScriptGym_PresentationReplicated");
        if (PlainMatches.Num() != 1 || PlainMatches[0] != _LocalPlain ||
            RepMatches.Num() != 1 || RepMatches[0] != _LocalRep)
        { return false; }
        auto Plain = Cast<UCk_EntityScript_EntityScriptGym_Presentation>(
            utils_entity_script::TryGet_EntityScript(_LocalPlain));
        auto Rep = Cast<UCk_EntityScript_EntityScriptGym_Presentation>(
            utils_entity_script::TryGet_EntityScript(_LocalRep));
        return ck::IsValid(Plain) && ck::IsValid(Rep) &&
            Plain.StationIndex == 0 && Rep.StationIndex == 1 &&
            Plain.Generation == LocalPresentationGeneration &&
            Rep.Generation == LocalPresentationGeneration;
    }

    bool Get_HasAdmittedLocalPresentation()
    {
        if (_Ending || !LocalPresentationReady || LocalPresentationFailed ||
            LocalPresentationGeneration != Snapshot.Generation ||
            !Get_HasCompleteSnapshot()) { return false; }
        if (!Get_IsValidLocalPresentation())
        { Request_FailLocalPresentation("Admitted local child lost"); return false; }
        return CkEntityScriptGym_Spawn::IsLive(Get_LocalVerifiedReplica());
    }

    UFUNCTION() private void OnLocalMonitor()
    {
        if (_Ending || !_LocalMonitorArmed) { return; }
        if (Snapshot.Generation != LocalPresentationGeneration)
        {
            Request_RetireLocalPresentation();
            LocalPresentationGeneration = Snapshot.Generation;
            LocalPresentationFailed = false;
            _LocalWaitSeconds = float(System::GetGameTimeInSeconds());
        }
        if (LocalPresentationFailed) { return; }
        if (Snapshot.Failed)
        { Request_FailLocalPresentation("Shared cohort failed"); return; }
        if (LocalPresentationReady)
        {
            if (!Get_IsValidLocalPresentation())
            { Request_FailLocalPresentation("Local pair lost"); }
            return;
        }
        if (float(System::GetGameTimeInSeconds()) - _LocalWaitSeconds >= 25.0)
        { Request_FailLocalPresentation("Local presentation timed out"); return; }
        for (auto Retiring : _LocalRetiring)
        {
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Retiring))
            { return; }
        }
        _LocalRetiring.Empty();
        if (!Get_HasCompleteSnapshot() ||
            !CkEntityScriptGym_Spawn::IsLive(Get_LocalVerifiedReplica()))
        { return; }
        if (_LocalStarting)
        { Request_CheckLocalPair(); return; }
        auto PlainStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn");
        auto RepStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.SpawnReplicated");
        if (!CkEntityScriptGym_Spawn::IsLive(PlainStation) ||
            !CkEntityScriptGym_Spawn::IsLive(RepStation)) { return; }
        _LocalStarting = true;
        auto Params = FCkEntityScriptGym_PresentationParams();
        Params.Generation = LocalPresentationGeneration;
        Params.StationIndex = 0;
        _PendingLocalPlain = utils_entity_script::Request_SpawnEntity(PlainStation,
            UCk_EntityScript_EntityScriptGym_Presentation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingLocalPlain))
        { Request_FailLocalPresentation("Plain panel child rejected"); return; }
        Params.StationIndex = 1;
        _PendingLocalRep = utils_entity_script::Request_SpawnEntity(RepStation,
            UCk_EntityScript_EntityScriptGym_Presentation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingLocalRep))
        { Request_FailLocalPresentation("Replica panel child rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingLocalPlain,
            FCk_Delegate_EntityScript_Constructed(this, n"OnLocalPlainConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_PendingLocalRep,
            FCk_Delegate_EntityScript_Constructed(this, n"OnLocalRepConstructed"));
    }

    UFUNCTION() private void OnLocalPlainConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalStarting || ck::Is_NOT_Valid(InHandle) ||
            FCk_Handle(InHandle) != _PendingLocalPlain.Get_EntityUnderConstruction()) { return; }
        _LocalPlain = FCk_Handle(InHandle);
        Request_CheckLocalPair();
    }
    UFUNCTION() private void OnLocalRepConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalStarting || ck::Is_NOT_Valid(InHandle) ||
            FCk_Handle(InHandle) != _PendingLocalRep.Get_EntityUnderConstruction()) { return; }
        _LocalRep = FCk_Handle(InHandle);
        Request_CheckLocalPair();
    }
    private void Request_CheckLocalPair()
    {
        if (!_LocalStarting || ck::Is_NOT_Valid(_LocalPlain) || ck::Is_NOT_Valid(_LocalRep))
        { return; }
        auto PlainStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn");
        auto RepStation = CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.SpawnReplicated");
        if (!CkEntityScriptGym_Spawn::IsLive(PlainStation) ||
            !CkEntityScriptGym_Spawn::IsLive(RepStation))
        { Request_FailLocalPresentation("Station lost during panel admission"); return; }
        auto PlainMatches = utils_entity_tag::ForEach_Entity(PlainStation,
            n"TAG_EntityScriptGym_PresentationPlain");
        auto RepMatches = utils_entity_tag::ForEach_Entity(RepStation,
            n"TAG_EntityScriptGym_PresentationReplicated");
        if (PlainMatches.Num() > 1 || RepMatches.Num() > 1 ||
            (PlainMatches.Num() == 1 && PlainMatches[0] != _LocalPlain) ||
            (RepMatches.Num() == 1 && RepMatches[0] != _LocalRep))
        { Request_FailLocalPresentation("Panel tags settled ambiguously"); return; }
        if (PlainMatches.Num() == 0 || RepMatches.Num() == 0) { return; }
        if (!Get_IsValidLocalPresentation())
        { Request_FailLocalPresentation("Local pair composition invalid"); return; }
        _LocalStarting = false;
        LocalPresentationReady = true;
    }

    UFUNCTION() private void OnMonitor()
    {
        if (_Ending || !_MonitorArmed || !HasAuthority() || Snapshot.Failed) { return; }
        if (_Starting)
        {
            if (float(System::GetGameTimeInSeconds()) - _StartSeconds >= 20.0)
            { Request_Fail("Admission or retirement timed out"); return; }
            if (_RerunBranch >= 0)
            {
                auto Retained = _RerunBranch == 0 ? ServerReplicated : ServerPlain;
                auto RetainedOwner = _RerunBranch == 0 ? ServerReplicatedRoot :
                    CkEntityScriptGym_Spawn::FindStation(n"Gym.EntityScript.Spawn");
                FCkEntityScriptGym_SpawnSample RetainedSample;
                if (!Request_TrySample(Retained, RetainedOwner,
                    _RerunBranch == 0, RetainedSample))
                { Request_Fail("Retained branch lost during rerun"); return; }
                if (!Get_RetiringComplete()) { return; }
                _Retiring.Empty();
                auto Branch = _RerunBranch;
                _RerunBranch = -2;
                if (Branch == 0) { Snapshot.PlainEpoch++; }
                else { Snapshot.ReplicatedEpoch++; }
                Request_SpawnBranch(Branch);
                return;
            }
            if (!_StationsReady && !_AdmissionOwned) { Request_EnsureWorldStations(); }
            if (_StationsReady && ck::IsValid(ServerPlain) && ck::IsValid(ServerReplicated))
            { Request_CheckCohort(); }
            return;
        }
        if (Snapshot.Ready && !Get_IsValidServerCohort())
        { Request_Fail("Shared cohort lost a prerequisite"); }
    }

    // A failed partial station admission is retained until the world restarts.
    private bool Request_HoldFailedAdmission()
    {
        if (_AdmissionTicket == 0) { return false; }
        auto World = ck::TransientEntity();
        CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
        CkGym_Admission::TryCloseWorldUnproven(World);
        return true;
    }

    private void Request_Fail(FString InReason)
    {
        if (Request_HoldFailedAdmission() ||
            CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Starting = false; Snapshot.Ready = false; Snapshot.Failed = true;
            Snapshot.FailureReason = InReason; ServerCohortCount = 0;
            ForceNetUpdate();
            return;
        }
        if (_Ending) { return; }
        if (ck::IsValid(_PendingControlOwner) &&
            _PendingControlOwner.GetWorld() == GetWorld() && _PendingControlSerial > 0)
        { _PendingControlOwner.Request_OnSharedControlCommitted(_PendingControlSerial, false); }
        _PendingControlOwner = nullptr;
        _PendingControlSerial = 0;
        _Starting = false;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        Snapshot.FailureReason = InReason;
        ServerCohortCount = 0;
        ForceNetUpdate();
        if (utils_pending_entity_script::Get_IsValid(_PendingPlain))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingPlain); }
        else if (CkEntityScriptGym_Spawn::IsLive(ServerPlain))
        { utils_entity_lifetime::Request_DestroyEntity(ServerPlain); }
        if (utils_pending_entity_script::Get_IsValid(_PendingReplicated))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingReplicated); }
        else if (CkEntityScriptGym_Spawn::IsLive(ServerReplicated))
        { utils_entity_lifetime::Request_DestroyEntity(ServerReplicated); }
        if (utils_pending_entity_script::Get_IsValid(_PendingRoot))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingRoot); }
        else if (CkEntityScriptGym_Spawn::IsLive(ServerReplicatedRoot))
        { utils_entity_lifetime::Request_DestroyEntity(ServerReplicatedRoot); }
        for (auto Pending : _PendingStations)
        {
            if (utils_pending_entity_script::Get_IsValid(Pending) &&
                !_NewStations.Contains(Pending.Get_EntityUnderConstruction()))
            { utils_pending_entity_script::Request_DestroyEntity(Pending); }
        }
        for (auto Station : _NewStations)
        { if (CkEntityScriptGym_Spawn::IsLive(Station))
          { utils_entity_lifetime::Request_DestroyEntity(Station); } }
        Request_ReleaseAdmission();
        _PendingStations.Empty(); _NewStations.Empty(); _ReusedStations.Empty();
        if (CkEntityScriptGym_Spawn::IsLive(_WorldStationOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        _WorldStationOwner = FCk_Handle();
        ServerPlain = FCk_Handle(); ServerReplicated = FCk_Handle();
        ServerReplicatedRoot = FCk_Handle();
        ck::Error("[EntityScriptGym] " + InReason);
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Ending = true; _Starting = false; _MonitorArmed = false; _LocalMonitorArmed = false; Snapshot.Ready = false;
            Request_HoldFailedAdmission();
            return;
        }
        _Ending = true;
        // The actor timer manager clears this actor's timers at EndPlay. Disarm
        // callbacks first so a queued invocation cannot restart admission.
        _Starting = false;
        _MonitorArmed = false;
        _LocalMonitorArmed = false;
        Snapshot.Ready = false;
        _PendingControlOwner = nullptr;
        _PendingControlSerial = 0;
        Request_RetireLocalPresentation();
        _LocalRetiring.Empty();
        if (HasAuthority())
        {
            if (utils_pending_entity_script::Get_IsValid(_PendingPlain))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingPlain); }
            else if (CkEntityScriptGym_Spawn::IsLive(ServerPlain))
            { utils_entity_lifetime::Request_DestroyEntity(ServerPlain); }
            if (utils_pending_entity_script::Get_IsValid(_PendingReplicated))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingReplicated); }
            else if (CkEntityScriptGym_Spawn::IsLive(ServerReplicated))
            { utils_entity_lifetime::Request_DestroyEntity(ServerReplicated); }
            if (utils_pending_entity_script::Get_IsValid(_PendingRoot))
            { utils_pending_entity_script::Request_DestroyEntity(_PendingRoot); }
            else if (CkEntityScriptGym_Spawn::IsLive(ServerReplicatedRoot))
            { utils_entity_lifetime::Request_DestroyEntity(ServerReplicatedRoot); }
            for (auto Pending : _PendingStations)
            {
                if (utils_pending_entity_script::Get_IsValid(Pending) &&
                    !_NewStations.Contains(Pending.Get_EntityUnderConstruction()))
                { utils_pending_entity_script::Request_DestroyEntity(Pending); }
            }
            Request_ReleaseAdmission();
            if (CkEntityScriptGym_Spawn::IsLive(_WorldStationOwner))
            { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        }
        _PendingStations.Empty(); _NewStations.Empty(); _ReusedStations.Empty();
        _PendingPlain = FCk_Handle_PendingEntityScript();
        _PendingReplicated = FCk_Handle_PendingEntityScript();
        _PendingRoot = FCk_Handle_PendingEntityScript();
        ServerPlain = FCk_Handle(); ServerReplicated = FCk_Handle();
        ServerReplicatedRoot = FCk_Handle(); _WorldStationOwner = FCk_Handle();
        ServerCohortCount = 0;
        // The native parent EndPlay invokes this Blueprint event after its own cleanup.
    }
}
