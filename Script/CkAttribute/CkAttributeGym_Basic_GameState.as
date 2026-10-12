// Language=angelscript

USTRUCT()
struct FCkAttributeGym_BasicSample
{
    UPROPERTY() float Health = 0.0;
    UPROPERTY() int32 Armor = 0;
    UPROPERTY() FVector Velocity = FVector::ZeroVector;
    UPROPERTY() int32 HealthChangeCount = 0;
    UPROPERTY() int32 ArmorChangeCount = 0;
    UPROPERTY() int32 VelocityChangeCount = 0;
    UPROPERTY() int32 ClampedCount = 0;
    UPROPERTY() int32 AutoStep = 0;
    UPROPERTY() bool AutoRunning = true;
}

USTRUCT()
struct FCkAttributeGym_BasicSnapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() FCkAttributeGym_BasicSample Sample;
}

class ACk_AttributeGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkAttributeGym_BasicSnapshot Snapshot;
    UPROPERTY() FCk_Handle ServerBasic;
    UPROPERTY() int32 ServerChildCount = 0;
    UPROPERTY() int32 LocalProjectionGeneration = 0;
    UPROPERTY() bool LocalProjectionReady = false;
    UPROPERTY() bool LocalProjectionFailed = false;
    UPROPERTY() FString LocalProjectionFailureReason;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _StationAdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private FCk_Handle _NewStation;
    private FCk_Handle _ReusedStation;
    private FCk_Handle_PendingEntityScript _PendingStation;
    private FCk_Handle_PendingEntityScript _PendingBasic;
    private bool _AdmissionOwned = false;
    private bool _Starting = false;
    private bool _StationReady = false;
    private bool _ServerMonitorArmed = false;
    private int32 _NextGeneration = 0;
    private float _StartSeconds = 0.0;
    private float _LastPublishSeconds = -1.0;

    private FCk_Handle_PendingEntityScript _PendingLocal;
    private FCk_Handle _LocalChild;
    private bool _LocalStarting = false;
    private bool _LocalMonitorArmed = false;
    private float _LocalWaitSeconds = 0.0;
    private float _LocalStartSeconds = -1.0;

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog() || _Starting ||
            Snapshot.Ready || Snapshot.Failed) { return; }
        _Starting = true;
        _StationReady = false;
        _StartSeconds = float(System::GetGameTimeInSeconds());
        _NextGeneration++;
        Snapshot = FCkAttributeGym_BasicSnapshot();
        Snapshot.Generation = _NextGeneration;
        ForceNetUpdate();
        System::SetTimer(this, n"OnStartupDeadline", 20.0, false);
        if (!_ServerMonitorArmed)
        {
            _ServerMonitorArmed = true;
            System::SetTimer(this, n"OnServerMonitor", 0.1, true);
        }
        Request_EnsureWorldStation();
    }

    bool Get_IsExpectedServerChild(FCk_Handle InChild, int32 InGeneration)
    {
        return HasAuthority() && _Starting && !Snapshot.Failed &&
            InGeneration == Snapshot.Generation &&
            utils_pending_entity_script::Get_IsValid(_PendingBasic) &&
            InChild == _PendingBasic.Get_EntityUnderConstruction();
    }

    void Request_FailExpectedServerChild(FCk_Handle InChild, int32 InGeneration, FString InReason)
    {
        if (Get_IsExpectedServerChild(InChild, InGeneration)) { Request_Fail(InReason); }
    }

    private void Request_Fail(FString InReason)
    {
        if (Snapshot.Failed) { return; }
        _Starting = false;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        Snapshot.FailureReason = InReason;
        Request_CloseWorldOnFailure();
        ForceNetUpdate();
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { ck::Error("[AttributeBasic] " + InReason); return; }
        Request_RetireServerChild();
        Request_ReleaseAdmission();
        if (CkAttributeBasic::IsLiveEntity(_WorldStationOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        _WorldStationOwner = FCk_Handle();
        _NewStation = FCk_Handle();
        _ReusedStation = FCk_Handle();
        _PendingStation = FCk_Handle_PendingEntityScript();
        ck::Error("[AttributeBasic] " + InReason);
    }

    private void Request_CloseWorldOnFailure()
    {
        if (_AdmissionTicket == 0) { return; }
        auto World = ck::TransientEntity();
        if (!CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _StationAdmissionOwner))
        { CkGym_Admission::TryCloseWorldUnproven(World); }
    }

    private void Request_RetireServerChild()
    {
        if (_AdmissionTicket != 0) { return; }
        ServerChildCount = 0;
        if (utils_pending_entity_script::Get_IsValid(_PendingBasic))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingBasic); }
        else if (CkAttributeBasic::IsLiveEntity(ServerBasic))
        { utils_entity_lifetime::Request_DestroyEntity(ServerBasic); }
        _PendingBasic = FCk_Handle_PendingEntityScript();
        ServerBasic = FCk_Handle();
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
                if (!CkGym_Admission::TryReleasePublished(World, _AdmissionTicket, _StationAdmissionOwner))
                { Request_CloseWorldOnFailure(); return false; }
                _AdmissionTicket = 0;
            }
            else
            { Request_CloseWorldOnFailure(); return false; }
        }
        _AdmissionOwned = false;
        if (CkAttributeBasic::IsLiveEntity(_StationAdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_StationAdmissionOwner); }
        _StationAdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_EnsureWorldStation()
    {
        if (!_Starting || _StationReady || _AdmissionOwned) { return; }
        auto World = ck::TransientEntity();
        if (!CkAttributeBasic::IsLiveEntity(World) ||
            !utils_entity_lifetime::Get_CanCreateEntity(World))
        { Request_Fail("World owner unavailable"); return; }
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
        if (!CkAttributeBasic::IsLiveEntity(_StationAdmissionOwner))
        { Request_Fail("Station admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _StationAdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _StationAdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _AdmissionOwned = true;
        auto Stations = CkAttributeBasic::Get_Stations();
        if (Stations.Num() != 1 || Stations[0].Tags.Num() != 1 ||
            Stations[0].Tags[0] != n"Gym.Attribute.BasicAttributes")
        { Request_Fail("Invalid station declaration"); return; }
        auto Matches = utils_entity_tag::ForEach_Entity(World, Stations[0].Tags[0]);
        if (Matches.Num() > 1 || (Matches.Num() == 1 &&
            (!CkAttributeBasic::IsLiveEntity(Matches[0]) ||
             !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
        { Request_Fail("Ambiguous or incomplete station"); return; }
        if (Matches.Num() == 1)
        {
            _ReusedStation = Matches[0];
            Request_CheckStationSettled();
            return;
        }
        _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkAttributeBasic::IsLiveEntity(_WorldStationOwner))
        { Request_Fail("World station hierarchy allocation failed"); return; }
        _PendingStation = CkGym_Common::Request_SpawnNewStationForOwner(
            Stations[0], _WorldStationOwner,
            FCk_Delegate_Request_OnCompleted(this, n"OnStationSpawnCompleted"));
        if (!utils_pending_entity_script::Get_IsValid(_PendingStation))
        { Request_Fail("Station spawn rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingStation,
            FCk_Delegate_EntityScript_Constructed(this, n"OnStationConstructed"));
    }

    UFUNCTION() private void OnAdmissionWait()
    {
        if (!_Starting || _AdmissionOwned || _StationReady) { return; }
        if (float(System::GetGameTimeInSeconds()) - _StartSeconds >= 20.0)
        { Request_Fail("Station admission timed out"); return; }
        Request_EnsureWorldStation();
    }

    UFUNCTION() private void OnStationSpawnCompleted(FCk_Handle InOwner,
        ECk_Request_OperationResult InResult)
    {
        if (!_Starting || !utils_pending_entity_script::Get_IsValid(_PendingStation) ||
            InOwner != _PendingStation.Get_EntityUnderConstruction()) { return; }
        if (InResult != ECk_Request_OperationResult::Succeeded)
        { Request_Fail("Station spawn failed"); }
    }

    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle) ||
            !utils_pending_entity_script::Get_IsValid(_PendingStation) ||
            FCk_Handle(InHandle) != _PendingStation.Get_EntityUnderConstruction()) { return; }
        auto Station = FCk_Handle(InHandle);
        if (!CkAttributeBasic::IsLiveEntity(Station) ||
            utils_entity_lifetime::Get_LifetimeOwner(Station) != _WorldStationOwner)
        { Request_Fail("Station completion has wrong owner"); return; }
        _NewStation = Station;
        System::SetTimer(this, n"OnStationSettle", 0.05, false);
    }

    UFUNCTION() private void OnStationSettle()
    {
        if (_Starting && _AdmissionOwned && !_StationReady)
        { Request_CheckStationSettled(); }
    }

    private void Request_CheckStationSettled()
    {
        auto Station = CkAttributeBasic::FindStation();
        if (!CkAttributeBasic::IsLiveEntity(Station) ||
            (Station != _NewStation && Station != _ReusedStation))
        { Request_Fail("Station tag did not settle uniquely"); return; }
        if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
        _PendingStation = FCk_Handle_PendingEntityScript();
        _NewStation = FCk_Handle();
        _ReusedStation = FCk_Handle();
        _StationReady = true;
        Request_SpawnServerChild(Station);
    }

    private void Request_SpawnServerChild(FCk_Handle InStation)
    {
        if (!_Starting || !CkAttributeBasic::IsLiveEntity(InStation) ||
            !InStation.Has_Fragment(FCkGym_Station_Anchors))
        { Request_Fail("Station anchors unavailable"); return; }
        auto ExistingChildren = utils_entity_tag::ForEach_Entity(
            ck::TransientEntity(), n"TAG_AttributeGym_BasicAttributes");
        if (ExistingChildren.Num() != 0)
        { Request_Fail("Simulation child identity already exists"); return; }
        auto Params = FBasicAttributesSpawnParams(
            FTransform(FRotator(0.0, 180.0, 0.0),
                InStation.Get_Fragment(FCkGym_Station_Anchors).PanelCenter),
            "BasicAttributes", Snapshot.Generation);
        auto Owner = InStation;
        _PendingBasic = utils_entity_script::Request_SpawnEntity(Owner,
            UCk_EntityScript_AttributeGym_BasicAttributes, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingBasic))
        { Request_Fail("Simulation child spawn rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingBasic,
            FCk_Delegate_EntityScript_Constructed(this, n"OnBasicConstructed"));
    }

    UFUNCTION() private void OnBasicConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || ck::Is_NOT_Valid(InHandle) ||
            !utils_pending_entity_script::Get_IsValid(_PendingBasic) ||
            FCk_Handle(InHandle) != _PendingBasic.Get_EntityUnderConstruction()) { return; }
        ServerBasic = FCk_Handle(InHandle);
    }

    UFUNCTION() private void OnStartupDeadline()
    {
        if (!_Starting || Snapshot.Ready || Snapshot.Failed) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _StartSeconds;
        if (Elapsed < 20.0)
        { System::SetTimer(this, n"OnStartupDeadline", 20.0 - Elapsed, false); return; }
        Request_Fail("Shared simulation readiness timed out");
    }

    private bool Get_IsValidServerChild()
    {
        auto Station = CkAttributeBasic::FindStation();
        if (!CkAttributeBasic::IsLiveEntity(Station) ||
            !CkAttributeBasic::IsLiveEntity(ServerBasic) ||
            utils_entity_lifetime::Get_LifetimeOwner(ServerBasic) != Station)
        { return false; }
        auto Matches = utils_entity_tag::ForEach_Entity(
            ck::TransientEntity(), n"TAG_AttributeGym_BasicAttributes");
        if (Matches.Num() != 1 || Matches[0] != ServerBasic) { return false; }
        auto Script = Cast<UCk_EntityScript_AttributeGym_BasicAttributes>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerBasic));
        return ck::IsValid(Script) && ck::ToEntity(Script) == ServerBasic &&
            Script.Generation == Snapshot.Generation &&
            Script.Get_HasValidAuthorityComposition();
    }

    bool Get_HasCompleteSnapshot()
    {
        return Snapshot.Generation > 0 && Snapshot.Sequence > 0 &&
            Snapshot.Ready && !Snapshot.Failed &&
            CkAttributeBasic::IsValidSample(Snapshot.Sample);
    }

    bool Get_HasReadyServerChild()
    {
        if (!HasAuthority() || ServerChildCount != 1 || !Get_HasCompleteSnapshot())
        { return false; }
        if (!Get_IsValidServerChild())
        { Request_Fail("Shared simulation dependency lost"); return false; }
        return true;
    }

    bool Get_IsCurrentServerChild(FCk_Handle InChild, int32 InGeneration)
    {
        if (!HasAuthority() || !Snapshot.Ready || Snapshot.Failed ||
            ServerChildCount != 1 || InGeneration != Snapshot.Generation ||
            InChild != ServerBasic) { return false; }
        if (!Get_IsValidServerChild())
        { Request_Fail("Shared simulation dependency lost"); return false; }
        return true;
    }

    UFUNCTION() private void OnServerMonitor()
    {
        if (!HasAuthority() || Snapshot.Failed) { return; }
        if (_Starting)
        {
            if (!CkAttributeBasic::IsLiveEntity(ServerBasic)) { return; }
            if (!Get_IsValidServerChild()) { return; }
            auto Script = Cast<UCk_EntityScript_AttributeGym_BasicAttributes>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerBasic));
            if (ck::Is_NOT_Valid(Script)) { Request_Fail("Simulation script missing"); return; }
            auto Sample = FCkAttributeGym_BasicSample();
            if (!Script.Request_TryBuildSample(Sample))
            { Request_Fail("Initial sample invalid"); return; }
            Snapshot.Sample = Sample;
            Snapshot.Sequence = 1;
            Snapshot.Ready = true;
            ServerChildCount = 1;
            _Starting = false;
            _LastPublishSeconds = float(System::GetGameTimeInSeconds());
            ForceNetUpdate();
            return;
        }
        if (!Snapshot.Ready || ServerChildCount != 1) { return; }
        if (!Get_IsValidServerChild())
        { Request_Fail("Shared simulation dependency lost"); return; }
        auto Script = Cast<UCk_EntityScript_AttributeGym_BasicAttributes>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerBasic));
        if (ck::Is_NOT_Valid(Script))
        { Request_Fail("Simulation script lost"); return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (Now - _LastPublishSeconds < 0.1) { return; }
        auto Sample = FCkAttributeGym_BasicSample();
        if (!Script.Request_TryBuildSample(Sample))
        { Request_Fail("Sample invalid"); return; }
        Snapshot.Sample = Sample;
        Snapshot.Sequence++;
        _LastPublishSeconds = Now;
    }

    bool Request_ApplyControl(int32 InAction, int32 InPresetIndex,
        FVector InVelocity, bool InAutoRunning)
    {
        if (!Get_HasReadyServerChild()) { return false; }
        auto Script = Cast<UCk_EntityScript_AttributeGym_BasicAttributes>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerBasic));
        if (ck::Is_NOT_Valid(Script)) { Request_Fail("Control target lost"); return false; }
        return Script.Request_ApplyControl(InAction, InPresetIndex, InVelocity, InAutoRunning);
    }

    void Request_EnsureLocalProjection()
    {
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World) ||
            World.GetNetMode() == ENetMode::NM_DedicatedServer) { return; }
        if (!_LocalMonitorArmed)
        {
            _LocalMonitorArmed = true;
            _LocalWaitSeconds = float(System::GetGameTimeInSeconds());
            System::SetTimer(this, n"OnLocalMonitor", 0.1, true);
        }
        Request_CheckLocalProjection();
    }

    bool Get_IsExpectedLocalChild(FCk_Handle InChild, int32 InGeneration)
    {
        auto World = GetWorld();
        return ck::IsValid(World) &&
            World.GetNetMode() != ENetMode::NM_DedicatedServer &&
            _LocalStarting && !LocalProjectionFailed &&
            Snapshot.Ready && Snapshot.Generation == InGeneration &&
            LocalProjectionGeneration == InGeneration &&
            utils_pending_entity_script::Get_IsValid(_PendingLocal) &&
            InChild == _PendingLocal.Get_EntityUnderConstruction();
    }

    void Request_FailExpectedLocalChild(FCk_Handle InChild, int32 InGeneration,
        FString InReason)
    {
        if (Get_IsExpectedLocalChild(InChild, InGeneration))
        { Request_FailLocalProjection(InReason); }
    }

    bool Get_HasAdmittedLocalProjection(int32 InGeneration)
    {
        if (!LocalProjectionReady || LocalProjectionFailed ||
            LocalProjectionGeneration != InGeneration ||
            Snapshot.Generation != InGeneration || !Get_HasCompleteSnapshot())
        { return false; }
        if (!Get_IsValidLocalProjection())
        { Request_FailLocalProjection("Admitted projection lost station or dependency"); return false; }
        return true;
    }

    private bool Get_IsValidLocalProjection()
    {
        auto Station = CkAttributeBasic::FindStation();
        if (!CkAttributeBasic::IsLiveEntity(Station) ||
            !CkAttributeBasic::IsLiveEntity(_LocalChild) ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalChild) != Station)
        { return false; }
        auto Matches = utils_entity_tag::ForEach_Entity(
            ck::TransientEntity(), n"TAG_AttributeGym_BasicProjection");
        if (Matches.Num() != 1 || Matches[0] != _LocalChild) { return false; }
        auto Script = Cast<UCk_EntityScript_AttributeGym_BasicProjection>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(_LocalChild));
        return ck::IsValid(Script) && ck::ToEntity(Script) == _LocalChild &&
            Script.Generation == LocalProjectionGeneration &&
            Script.Get_HasValidLocalComposition();
    }

    UFUNCTION() private void OnLocalMonitor() { Request_CheckLocalProjection(); }

    private void Request_CheckLocalProjection()
    {
        if (!_LocalMonitorArmed) { return; }
        if (Snapshot.Generation != LocalProjectionGeneration)
        {
            Request_RetireLocalProjection();
            LocalProjectionGeneration = Snapshot.Generation;
            LocalProjectionFailed = false;
            LocalProjectionFailureReason = "";
            _LocalWaitSeconds = float(System::GetGameTimeInSeconds());
            _LocalStartSeconds = -1.0;
        }
        if (LocalProjectionFailed) { return; }
        auto World = GetWorld();
        if (ck::Is_NOT_Valid(World))
        { Request_FailLocalProjection("Projection world unavailable"); return; }
        if (World.GetNetMode() == ENetMode::NM_Client &&
            utils_entity_tag::ForEach_Entity(ck::TransientEntity(),
                n"TAG_AttributeGym_BasicAttributes").Num() != 0)
        { Request_FailLocalProjection("Client contains authority simulation"); return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (Snapshot.Failed)
        { Request_FailLocalProjection("Shared simulation failed"); return; }
        if (Snapshot.Ready && !Get_HasCompleteSnapshot())
        { Request_FailLocalProjection("Shared sample malformed"); return; }
        if (LocalProjectionReady)
        {
            if (!Snapshot.Ready || !Get_IsValidLocalProjection())
            { Request_FailLocalProjection("Admitted projection lost readiness"); }
            return;
        }
        if (!Snapshot.Ready)
        {
            if (Now - _LocalWaitSeconds >= 25.0)
            { Request_FailLocalProjection("Shared simulation readiness timed out"); }
            return;
        }
        if (_LocalStartSeconds < 0.0) { _LocalStartSeconds = Now; }
        if (Now - _LocalStartSeconds >= 20.0)
        { Request_FailLocalProjection("Local projection readiness timed out"); return; }
        if (_LocalStarting)
        {
            if (CkAttributeBasic::IsLiveEntity(_LocalChild) && Get_IsValidLocalProjection())
            {
                _LocalStarting = false;
                LocalProjectionReady = true;
            }
            return;
        }
        auto Station = CkAttributeBasic::FindStation();
        if (!CkAttributeBasic::IsLiveEntity(Station)) { return; }
        auto ExistingChildren = utils_entity_tag::ForEach_Entity(
            ck::TransientEntity(), n"TAG_AttributeGym_BasicProjection");
        if (ExistingChildren.Num() != 0)
        { Request_FailLocalProjection("Projection child identity already exists"); return; }
        _LocalStarting = true;
        auto Params = FBasicProjectionSpawnParams();
        Params.Generation = LocalProjectionGeneration;
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
            Station.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        auto Owner = Station;
        _PendingLocal = utils_entity_script::Request_SpawnEntity(Owner,
            UCk_EntityScript_AttributeGym_BasicProjection, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingLocal))
        { Request_FailLocalProjection("Projection spawn rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingLocal,
            FCk_Delegate_EntityScript_Constructed(this, n"OnLocalConstructed"));
    }

    UFUNCTION() private void OnLocalConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalStarting || ck::Is_NOT_Valid(InHandle) ||
            !utils_pending_entity_script::Get_IsValid(_PendingLocal) ||
            FCk_Handle(InHandle) != _PendingLocal.Get_EntityUnderConstruction()) { return; }
        _LocalChild = FCk_Handle(InHandle);
        Request_CheckLocalProjection();
    }

    void Request_FailCurrentLocalChild(FCk_Handle InChild, int32 InGeneration, FString InReason)
    {
        if (LocalProjectionFailed || !LocalProjectionReady ||
            InGeneration != LocalProjectionGeneration ||
            InChild != _LocalChild) { return; }
        Request_FailLocalProjection(InReason);
    }

    private void Request_FailLocalProjection(FString InReason)
    {
        Request_RetireLocalProjection();
        LocalProjectionFailed = true;
        LocalProjectionFailureReason = InReason;
        ck::Error("[AttributeBasic] " + InReason);
    }

    private void Request_RetireLocalProjection()
    {
        LocalProjectionReady = false;
        _LocalStarting = false;
        if (utils_pending_entity_script::Get_IsValid(_PendingLocal))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingLocal); }
        else if (CkAttributeBasic::IsLiveEntity(_LocalChild))
        { utils_entity_lifetime::Request_DestroyEntity(_LocalChild); }
        _PendingLocal = FCk_Handle_PendingEntityScript();
        _LocalChild = FCk_Handle();
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (HasAuthority() && _AdmissionTicket != 0)
        {
            _Starting = false;
            Snapshot.Ready = false;
            Snapshot.Failed = true;
            Request_CloseWorldOnFailure();
            return;
        }
        _Starting = false;
        Snapshot.Ready = false;
        Request_RetireLocalProjection();
        if (HasAuthority())
        {
            Request_RetireServerChild();
            Request_ReleaseAdmission();
            if (CkAttributeBasic::IsLiveEntity(_WorldStationOwner))
            { utils_entity_lifetime::Request_DestroyEntity(_WorldStationOwner); }
        }
    }
}
