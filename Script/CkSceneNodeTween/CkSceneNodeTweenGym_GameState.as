// Language=angelscript

namespace CkSceneNodeTweenGym
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        auto Simple = FCkGym_Station_SpawnParams_Payload();
        Simple.Tags.Add(n"Gym.SceneNodeTween.Simple");
        Simple.Title = FText::FromString("TWEEN + SCENE NODE (SIMPLE)");
        Simple.AutoSize = true;
        Simple.Description.Add(FText::FromString("Root tween + 1 scene-node child."));
        Simple.Description.Add(FText::FromString("Child tracks the tweened root via"));
        Simple.Description.Add(FText::FromString("single-hop transform propagation."));
        Simple.Description.Add(FText::FromString(""));
        Simple.Description.Add(FText::FromString("Purple sphere: co-located with the"));
        Simple.Description.Add(FText::FromString("cyan child box (leaf IS the child)."));
        Stations.Add(Simple);
        auto Chain = FCkGym_Station_SpawnParams_Payload();
        Chain.Tags.Add(n"Gym.SceneNodeTween.Chain");
        Chain.Title = FText::FromString("TWEEN + SCENE NODE (CHAIN)");
        Chain.AutoSize = true;
        Chain.Description.Add(FText::FromString("Root tween + 3-level chain"));
        Chain.Description.Add(FText::FromString("(root -> A yaw45 -> B roll30)."));
        Chain.Description.Add(FText::FromString("Exercises multi-layer rotation"));
        Chain.Description.Add(FText::FromString("composition under a tweened root."));
        Chain.Description.Add(FText::FromString(""));
        Chain.Description.Add(FText::FromString("Purple sphere: co-located with the"));
        Chain.Description.Add(FText::FromString("purple B box (leaf IS node B)."));
        Stations.Add(Chain);
        auto Deep = FCkGym_Station_SpawnParams_Payload();
        Deep.Tags.Add(n"Gym.SceneNodeTween.Deep");
        Deep.Title = FText::FromString("TWEEN + SCENE NODE (DEEP)");
        Deep.AutoSize = true;
        Deep.Description.Add(FText::FromString("Root tween + 5-level chain."));
        Deep.Description.Add(FText::FromString("Stress-tests propagation depth —"));
        Deep.Description.Add(FText::FromString("every layer must observe the"));
        Deep.Description.Add(FText::FromString("previous layer's update each frame."));
        Deep.Description.Add(FText::FromString(""));
        Deep.Description.Add(FText::FromString("Purple sphere: OFFSET from the last"));
        Deep.Description.Add(FText::FromString("box. It sits at the end of the 5th"));
        Deep.Description.Add(FText::FromString("local offset (its own link), so it"));
        Deep.Description.Add(FText::FromString("is past link 4 by the 5th local."));
        Stations.Add(Deep);
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
            utils_entity_lifetime::Get_IsPendingDestroy(
                Matches[0], ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !Matches[0].Has_Fragment(FCkGym_Station_Anchors))
        { return FCk_Handle(); }
        return Matches[0];
    }

    bool IsLiveEntity(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) && !utils_entity_lifetime::Get_IsPendingDestroy(
            InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    ACk_SceneNodeTweenGym_GameState Get_State()
    {
        TArray<ACk_SceneNodeTweenGym_GameState> States;
        GetAllActorsOfClass(ACk_SceneNodeTweenGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }

    bool IsFinitePoint(FVector InPoint)
    {
        return Math::Abs(InPoint.X) < 100000000.0 &&
            Math::Abs(InPoint.Y) < 100000000.0 && Math::Abs(InPoint.Z) < 100000000.0;
    }
}

USTRUCT()
struct FCkSceneNodeTweenGym_StationSample
{
    UPROPERTY() FVector RootPosition = FVector::ZeroVector;
    UPROPERTY() TArray<FVector> ExpectedLinks;
    UPROPERTY() TArray<FVector> ActualLinks;
    UPROPERTY() float Drift = 0.0;
}

USTRUCT()
struct FCkSceneNodeTweenGym_Snapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() bool AutoRunning = true;
    UPROPERTY() TArray<FCkSceneNodeTweenGym_StationSample> Stations;
}

class ACk_SceneNodeTweenGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkSceneNodeTweenGym_Snapshot Snapshot;
    UPROPERTY() FCk_Handle ServerSimple;
    UPROPERTY() FCk_Handle ServerChain;
    UPROPERTY() FCk_Handle ServerDeep;
    UPROPERTY() int32 ServerTrioCount = 0;
    UPROPERTY() int32 LocalProjectionGeneration = 0;
    UPROPERTY() bool LocalProjectionReady = false;
    UPROPERTY() bool LocalProjectionFailed = false;
    UPROPERTY() FString LocalProjectionFailureReason;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _StationAdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private TArray<FCk_Handle> _NewStations;
    private TArray<FCk_Handle> _ReusedStations;
    private TArray<FCk_Handle> _RetainedStations;
    private int32 _PendingStationCount = 0;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private bool _RequestedNewStations = false;
    private bool _Starting = false;
    private bool _AdmissionOwned = false;
    private bool _StationsReady = false;
    private int32 _ReadinessAttempts = 0;
    private bool _ServerMonitorArmed = false;
    private int32 _NextGeneration = 0;
    private float _GenerationStartSeconds = 0.0;
    private float _LastPublishSeconds = -1.0;
    private FCkSceneNodeTweenGym_Snapshot _Staged;
    private int32 _SampleMask = 0;
    private FCk_Handle_PendingEntityScript _PendingSimple;
    private FCk_Handle_PendingEntityScript _PendingChain;
    private FCk_Handle_PendingEntityScript _PendingDeep;
    private TArray<FCk_Handle> _RetiringEntities;

    private bool _LocalProjectionMonitorArmed = false;
    private bool _LocalProjectionStarting = false;
    private float _LocalProjectionStartSeconds = -1.0;
    private float _LocalServerWaitStartSeconds = 0.0;
    private FCk_Handle_PendingEntityScript _LocalPendingSimple;
    private FCk_Handle_PendingEntityScript _LocalPendingChain;
    private FCk_Handle_PendingEntityScript _LocalPendingDeep;
    private FCk_Handle _LocalSimple;
    private FCk_Handle _LocalChain;
    private FCk_Handle _LocalDeep;
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
        auto AutoWasRunning = Snapshot.AutoRunning;
        Snapshot = FCkSceneNodeTweenGym_Snapshot();
        Snapshot.AutoRunning = AutoWasRunning;
        Snapshot.Generation = _NextGeneration;
        ForceNetUpdate();
        Request_CancelStartupPreservingAnchors();
        Request_CaptureRetiringSubtree(ServerSimple);
        Request_CaptureRetiringSubtree(ServerChain);
        Request_CaptureRetiringSubtree(ServerDeep);
        if (utils_pending_entity_script::Get_IsValid(_PendingSimple))
        { Request_CaptureRetiringSubtree(_PendingSimple.Get_EntityUnderConstruction()); }
        if (utils_pending_entity_script::Get_IsValid(_PendingChain))
        { Request_CaptureRetiringSubtree(_PendingChain.Get_EntityUnderConstruction()); }
        if (utils_pending_entity_script::Get_IsValid(_PendingDeep))
        { Request_CaptureRetiringSubtree(_PendingDeep.Get_EntityUnderConstruction()); }
        Request_RetireChildren();
        Request_StartGeneration(true);
        return _Starting && !Snapshot.Failed;
    }

    private void Request_CaptureRetiringSubtree(FCk_Handle InRoot)
    {
        if (ck::Is_NOT_Valid(InRoot) || _RetiringEntities.Contains(InRoot)) { return; }
        _RetiringEntities.Add(InRoot);
        for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
        { Request_CaptureRetiringSubtree(Child); }
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
        auto AutoWasRunning = Snapshot.AutoRunning;
        Snapshot = FCkSceneNodeTweenGym_Snapshot();
        Snapshot.AutoRunning = AutoWasRunning;
        Snapshot.Generation = _NextGeneration;
        ForceNetUpdate();
        _Staged = FCkSceneNodeTweenGym_Snapshot();
        _Staged.Generation = Snapshot.Generation;
        _Staged.AutoRunning = Snapshot.AutoRunning;
        _SampleMask = 0;
        _StationsReady = false;
        _LastPublishSeconds = -1.0;
        System::SetTimer(this, n"OnTrioTimeout", 20.0, false);
        if (InDeferStations)
        { System::SetTimer(this, n"OnResetSettle", 0.05, false); }
        else
        { Request_EnsureWorldStations(); }
    }

    UFUNCTION() private void OnResetSettle()
    {
        if (!_Starting || _AdmissionOwned || _StationsReady) { return; }
        for (auto Retiring : _RetiringEntities)
        {
            if (ck::IsValid(Retiring))
            {
                if (float(System::GetGameTimeInSeconds()) - _GenerationStartSeconds >= 20.0)
                { Request_Fail("Old trio retirement timed out"); return; }
                System::SetTimer(this, n"OnResetSettle", 0.05, false);
                return;
            }
        }
        _RetiringEntities.Empty();
        Request_EnsureWorldStations();
    }

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
            Snapshot.FailureReason = InReason; ForceNetUpdate();
            ck::Error("[SceneNodeTweenGym] " + InReason);
            return;
        }
        _Starting = false;
        Snapshot.Ready = false;
        Snapshot.Failed = true;
        Snapshot.FailureReason = InReason;
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
        _RetiringEntities.Empty();
        ck::Error("[SceneNodeTweenGym] " + InReason);
    }

    void Request_FailCurrentServerChild(FCk_Handle InChild, int32 InGeneration, FString InReason)
    {
        if (!HasAuthority() || Snapshot.Failed || ServerTrioCount != 3 ||
            ck::Is_NOT_Valid(InChild) || InGeneration != Snapshot.Generation ||
            (InChild != ServerSimple && InChild != ServerChain && InChild != ServerDeep))
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
            auto Declarations = CkSceneNodeTweenGym::Get_Stations();
            int32 Visible = 0;
            for (auto Retained : _RetainedStations)
            {
                if (!CkSceneNodeTweenGym::IsLiveEntity(Retained))
                { Request_Fail("Retained station disappeared"); return; }
                for (auto Declaration : Declarations)
                {
                    if (Retained == CkSceneNodeTweenGym::FindStation(Declaration.Tags[0]))
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
        auto Stations = CkSceneNodeTweenGym::Get_Stations();
        if (Stations.Num() != 3)
        { Request_Fail("Invalid station declaration"); return; }
        // Inspect every declared identity before requesting any new station.
        for (auto Station : Stations)
        {
            if (Station.Tags.Num() != 1 || Station.Tags[0] == FName())
            { Request_Fail("Invalid station tag"); return; }
            auto Matches = utils_entity_tag::ForEach_Entity(World, Station.Tags[0]);
            if (Matches.Num() > 1 || (Matches.Num() == 1 &&
                (!CkSceneNodeTweenGym::IsLiveEntity(Matches[0]) ||
                 !Matches[0].Has_Fragment(FCkGym_Station_Anchors))))
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
            if (ck::IsValid(CkSceneNodeTweenGym::FindStation(Station.Tags[0]))) { continue; }
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
        auto Stations = CkSceneNodeTweenGym::Get_Stations();
        for (auto Station : Stations)
        {
            auto Handle = CkSceneNodeTweenGym::FindStation(Station.Tags[0]);
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
        auto Stations = CkSceneNodeTweenGym::Get_Stations();
        if (Stations.Num() != 3) { Request_Fail("Station declarations changed"); return; }
        auto Simple = CkSceneNodeTweenGym::FindStation(Stations[0].Tags[0]);
        auto Chain = CkSceneNodeTweenGym::FindStation(Stations[1].Tags[0]);
        auto Deep = CkSceneNodeTweenGym::FindStation(Stations[2].Tags[0]);
        if (ck::Is_NOT_Valid(Simple) || ck::Is_NOT_Valid(Chain) || ck::Is_NOT_Valid(Deep))
        { Request_Fail("Station hierarchy is incomplete"); return; }
        auto Params = FCkSceneNodeTweenGym_Station_SpawnParams();
        Params.Generation = Snapshot.Generation;
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
            Simple.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        _PendingSimple = utils_entity_script::Request_SpawnEntity(Simple,
            UCk_EntityScript_SceneNodeTweenGym_SimpleStation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingSimple))
        { Request_Fail("Simple spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
            Chain.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        _PendingChain = utils_entity_script::Request_SpawnEntity(Chain,
            UCk_EntityScript_SceneNodeTweenGym_ChainStation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingChain))
        { Request_Fail("Chain spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0),
            Deep.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        _PendingDeep = utils_entity_script::Request_SpawnEntity(Deep,
            UCk_EntityScript_SceneNodeTweenGym_DeepStation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_PendingDeep))
        { Request_Fail("Deep spawn rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_PendingSimple,
            FCk_Delegate_EntityScript_Constructed(this, n"OnSimpleConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_PendingChain,
            FCk_Delegate_EntityScript_Constructed(this, n"OnChainConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_PendingDeep,
            FCk_Delegate_EntityScript_Constructed(this, n"OnDeepConstructed"));
    }

    UFUNCTION() private void OnSimpleConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed ||
            FCk_Handle(InHandle) != _PendingSimple.Get_EntityUnderConstruction()) { return; }
        ServerSimple = FCk_Handle(InHandle); Request_CheckTrio();
    }
    UFUNCTION() private void OnChainConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed ||
            FCk_Handle(InHandle) != _PendingChain.Get_EntityUnderConstruction()) { return; }
        ServerChain = FCk_Handle(InHandle); Request_CheckTrio();
    }
    UFUNCTION() private void OnDeepConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || Snapshot.Failed ||
            FCk_Handle(InHandle) != _PendingDeep.Get_EntityUnderConstruction()) { return; }
        ServerDeep = FCk_Handle(InHandle); Request_CheckTrio();
    }
    UFUNCTION() private void OnTrioTimeout()
    {
        if (!_Starting || Snapshot.Ready) { return; }
        auto Elapsed = float(System::GetGameTimeInSeconds()) - _GenerationStartSeconds;
        if (Elapsed < 20.0)
        { System::SetTimer(this, n"OnTrioTimeout", 20.0 - Elapsed, false); return; }
        Request_Fail("Trio readiness timed out");
    }

    bool Get_IsCurrentGeneration(int32 InGeneration)
    { return HasAuthority() && _Starting && !Snapshot.Failed && InGeneration == Snapshot.Generation; }

    private void Request_CheckTrio()
    {
        if (ck::Is_NOT_Valid(ServerSimple) || ck::Is_NOT_Valid(ServerChain) ||
            ck::Is_NOT_Valid(ServerDeep)) { return; }
        auto Simple = Cast<UCk_EntityScript_SceneNodeTweenGym_SimpleStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerSimple));
        auto Chain = Cast<UCk_EntityScript_SceneNodeTweenGym_ChainStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChain));
        auto Deep = Cast<UCk_EntityScript_SceneNodeTweenGym_DeepStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerDeep));
        if (ck::Is_NOT_Valid(Simple) || ck::Is_NOT_Valid(Chain) || ck::Is_NOT_Valid(Deep) ||
            !Simple.Get_HasPreparedAuthorityComposition() ||
            !Chain.Get_HasPreparedAuthorityComposition() ||
            !Deep.Get_HasPreparedAuthorityComposition())
        { Request_Fail("Trio construction is incomplete"); return; }
        if (!Simple.Request_ActivateAuthority() || !Chain.Request_ActivateAuthority() ||
            !Deep.Request_ActivateAuthority())
        { Request_Fail("Trio activation failed"); return; }
        ServerTrioCount = 3;
        if (!_ServerMonitorArmed)
        {
            _ServerMonitorArmed = true;
            System::SetTimer(this, n"OnServerTrioMonitor", 0.1, true);
        }
    }

    private bool Get_IsValidServerTrio()
    {
        if (!CkSceneNodeTweenGym::IsLiveEntity(ServerSimple) ||
            !CkSceneNodeTweenGym::IsLiveEntity(ServerChain) ||
            !CkSceneNodeTweenGym::IsLiveEntity(ServerDeep)) { return false; }
        if (utils_entity_lifetime::Get_LifetimeOwner(ServerSimple) !=
                CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Simple") ||
            utils_entity_lifetime::Get_LifetimeOwner(ServerChain) !=
                CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Chain") ||
            utils_entity_lifetime::Get_LifetimeOwner(ServerDeep) !=
                CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Deep")) { return false; }
        auto Simple = Cast<UCk_EntityScript_SceneNodeTweenGym_SimpleStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerSimple));
        auto Chain = Cast<UCk_EntityScript_SceneNodeTweenGym_ChainStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChain));
        auto Deep = Cast<UCk_EntityScript_SceneNodeTweenGym_DeepStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerDeep));
        return ck::IsValid(Simple) && Simple.Generation == Snapshot.Generation &&
            Simple.Get_HasValidAuthorityComposition() &&
            ck::IsValid(Chain) && Chain.Generation == Snapshot.Generation &&
            Chain.Get_HasValidAuthorityComposition() &&
            ck::IsValid(Deep) && Deep.Generation == Snapshot.Generation &&
            Deep.Get_HasValidAuthorityComposition();
    }

    bool Get_HasReadyServerTrio()
    {
        return HasAuthority() && Snapshot.Ready && !Snapshot.Failed &&
            ServerTrioCount == 3 && Get_HasCompleteSnapshot() && Get_IsValidServerTrio();
    }

    UFUNCTION() private void OnServerTrioMonitor()
    {
        if (!HasAuthority() || Snapshot.Failed || ServerTrioCount != 3) { return; }
        if (!Get_IsValidServerTrio())
        { Request_Fail("Shared trio lost a child or dependent entity"); }
    }

    bool Get_IsCurrentServerChild(FCk_Handle InChild, int32 InGeneration)
    {
        if (!HasAuthority() || Snapshot.Failed || ServerTrioCount != 3 ||
            InGeneration != Snapshot.Generation ||
            (InChild != ServerSimple && InChild != ServerChain && InChild != ServerDeep))
        { return false; }
        if (!Get_IsValidServerTrio())
        { Request_Fail("Shared trio lost a child or dependent entity"); return false; }
        return true;
    }

    bool Request_SetSharedAuto(bool InRunning)
    {
        if (!HasAuthority() || !Snapshot.Ready || Snapshot.Failed || !Get_IsValidServerTrio())
        { return false; }
        auto Simple = Cast<UCk_EntityScript_SceneNodeTweenGym_SimpleStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerSimple));
        auto Chain = Cast<UCk_EntityScript_SceneNodeTweenGym_ChainStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerChain));
        auto Deep = Cast<UCk_EntityScript_SceneNodeTweenGym_DeepStation>(
            UCk_Utils_EntityScript_UE::TryGet_EntityScript(ServerDeep));
        if (ck::Is_NOT_Valid(Simple) || ck::Is_NOT_Valid(Chain) || ck::Is_NOT_Valid(Deep))
        { Request_Fail("Auto control lost a station"); return false; }
        Simple.Request_SetAuto(InRunning);
        Chain.Request_SetAuto(InRunning);
        Deep.Request_SetAuto(InRunning);
        Snapshot.AutoRunning = InRunning;
        Snapshot.Sequence++;
        _Staged.AutoRunning = InRunning;
        _Staged.Sequence = Snapshot.Sequence;
        ForceNetUpdate();
        return true;
    }

    void SubmitStationSample(FCk_Handle InChild, int32 InGeneration, int32 InIndex,
                             FCkSceneNodeTweenGym_StationSample InSample)
    {
        if (!Get_IsCurrentServerChild(InChild, InGeneration)) { return; }
        if (InIndex < 0 || InIndex > 2 ||
            (InIndex == 0 && InChild != ServerSimple) ||
            (InIndex == 1 && InChild != ServerChain) ||
            (InIndex == 2 && InChild != ServerDeep))
        { Request_Fail("Wrong sample sender"); return; }
        auto Needed = InIndex == 0 ? 1 : (InIndex == 1 ? 2 : 5);
        if (InSample.ExpectedLinks.Num() != Needed || InSample.ActualLinks.Num() != Needed ||
            !CkSceneNodeTweenGym::IsFinitePoint(InSample.RootPosition) ||
            !(InSample.Drift >= 0.0 && InSample.Drift < 100000000.0))
        { Request_Fail("Malformed station sample"); return; }
        for (int32 i = 0; i < Needed; i++)
        {
            if (!CkSceneNodeTweenGym::IsFinitePoint(InSample.ExpectedLinks[i]) ||
                !CkSceneNodeTweenGym::IsFinitePoint(InSample.ActualLinks[i]))
            { Request_Fail("Malformed station link sample"); return; }
        }
        if (_Staged.Stations.Num() != 3)
        {
            _Staged.Stations.Empty();
            _Staged.Stations.Add(FCkSceneNodeTweenGym_StationSample());
            _Staged.Stations.Add(FCkSceneNodeTweenGym_StationSample());
            _Staged.Stations.Add(FCkSceneNodeTweenGym_StationSample());
        }
        _Staged.Stations[InIndex] = InSample;
        _SampleMask |= (1 << InIndex);
        Request_PublishIfComplete();
    }

    private void Request_PublishIfComplete()
    {
        if (_SampleMask != 7 || _Staged.Stations.Num() != 3) { return; }
        auto Now = float(System::GetGameTimeInSeconds());
        if (Snapshot.Ready && _LastPublishSeconds >= 0.0 && Now - _LastPublishSeconds < 0.1)
        { return; }
        _Staged.Sequence = Snapshot.Sequence + 1;
        _Staged.Ready = true;
        _Staged.Failed = false;
        Snapshot = _Staged;
        _Starting = false;
        _LastPublishSeconds = Now;
        _SampleMask = 0;
        ForceNetUpdate();
    }

    private void Request_RetireChildren()
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { Request_Fail("Partial station admission cannot retire in this world"); return; }
        ServerTrioCount = 0;
        if (utils_pending_entity_script::Get_IsValid(_PendingSimple))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingSimple); }
        else if (ck::IsValid(ServerSimple)) { utils_entity_lifetime::Request_DestroyEntity(ServerSimple); }
        if (utils_pending_entity_script::Get_IsValid(_PendingChain))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingChain); }
        else if (ck::IsValid(ServerChain)) { utils_entity_lifetime::Request_DestroyEntity(ServerChain); }
        if (utils_pending_entity_script::Get_IsValid(_PendingDeep))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingDeep); }
        else if (ck::IsValid(ServerDeep)) { utils_entity_lifetime::Request_DestroyEntity(ServerDeep); }
        ServerSimple = FCk_Handle(); ServerChain = FCk_Handle(); ServerDeep = FCk_Handle();
        _PendingSimple = FCk_Handle_PendingEntityScript();
        _PendingChain = FCk_Handle_PendingEntityScript();
        _PendingDeep = FCk_Handle_PendingEntityScript();
    }

    bool Get_HasCompleteSnapshot()
    {
        if (Snapshot.Generation <= 0 || Snapshot.Sequence <= 0 ||
            !Snapshot.Ready || Snapshot.Failed ||
            Snapshot.Stations.Num() != 3) { return false; }
        for (int32 s = 0; s < 3; s++)
        {
            auto Needed = s == 0 ? 1 : (s == 1 ? 2 : 5);
            auto Sample = Snapshot.Stations[s];
            if (Sample.ExpectedLinks.Num() != Needed || Sample.ActualLinks.Num() != Needed ||
                !CkSceneNodeTweenGym::IsFinitePoint(Sample.RootPosition) ||
                !(Sample.Drift >= 0.0 && Sample.Drift < 100000000.0))
            { return false; }
            for (int32 i = 0; i < Needed; i++)
            {
                if (!CkSceneNodeTweenGym::IsFinitePoint(Sample.ExpectedLinks[i]) ||
                    !CkSceneNodeTweenGym::IsFinitePoint(Sample.ActualLinks[i]))
                { return false; }
            }
        }
        return true;
    }

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
            LocalProjectionGeneration != InGeneration || Snapshot.Generation != InGeneration ||
            !Get_HasCompleteSnapshot())
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
            LocalProjectionFailureReason = "";
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
        if (LocalProjectionReady && !Snapshot.Ready)
        { Request_RetireLocalProjection(); return; }
        if (Snapshot.Ready && !Get_HasCompleteSnapshot())
        { Request_FailLocalProjection("Shared snapshot is malformed"); return; }
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
        auto Stations = CkSceneNodeTweenGym::Get_Stations();
        auto Simple = CkSceneNodeTweenGym::FindStation(Stations[0].Tags[0]);
        auto Chain = CkSceneNodeTweenGym::FindStation(Stations[1].Tags[0]);
        auto Deep = CkSceneNodeTweenGym::FindStation(Stations[2].Tags[0]);
        if (ck::Is_NOT_Valid(Simple) || ck::Is_NOT_Valid(Chain) || ck::Is_NOT_Valid(Deep))
        { return; } // base controller admission may still be settling in this client world
        _LocalProjectionStarting = true;
        auto Params = FCkSceneNodeTweenGym_Station_SpawnParams();
        Params.Generation = LocalProjectionGeneration;
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Simple.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        _LocalPendingSimple = utils_entity_script::Request_SpawnEntity(Simple, UCk_EntityScript_SceneNodeTweenGym_SimpleStation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_LocalPendingSimple))
        { Request_FailLocalProjection("Simple projection spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Chain.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        _LocalPendingChain = utils_entity_script::Request_SpawnEntity(Chain, UCk_EntityScript_SceneNodeTweenGym_ChainStation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_LocalPendingChain))
        { Request_FailLocalProjection("Chain projection spawn rejected"); return; }
        Params.InitialTransform = FTransform(FRotator(0.0, 180.0, 0.0), Deep.Get_Fragment(FCkGym_Station_Anchors).PanelCenter);
        _LocalPendingDeep = utils_entity_script::Request_SpawnEntity(Deep, UCk_EntityScript_SceneNodeTweenGym_DeepStation, FInstancedStruct::Make(Params));
        if (!utils_pending_entity_script::Get_IsValid(_LocalPendingDeep))
        { Request_FailLocalProjection("Deep projection spawn rejected"); return; }
        utils_pending_entity_script::Promise_OnConstructed(_LocalPendingSimple, FCk_Delegate_EntityScript_Constructed(this, n"OnLocalSimpleConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_LocalPendingChain, FCk_Delegate_EntityScript_Constructed(this, n"OnLocalChainConstructed"));
        utils_pending_entity_script::Promise_OnConstructed(_LocalPendingDeep, FCk_Delegate_EntityScript_Constructed(this, n"OnLocalDeepConstructed"));
    }

    UFUNCTION() private void OnLocalSimpleConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalProjectionStarting || FCk_Handle(InHandle) != _LocalPendingSimple.Get_EntityUnderConstruction()) { return; }
        _LocalSimple = FCk_Handle(InHandle); Request_CheckLocalProjectionAdmission();
    }
    UFUNCTION() private void OnLocalChainConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalProjectionStarting || FCk_Handle(InHandle) != _LocalPendingChain.Get_EntityUnderConstruction()) { return; }
        _LocalChain = FCk_Handle(InHandle); Request_CheckLocalProjectionAdmission();
    }
    UFUNCTION() private void OnLocalDeepConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_LocalProjectionStarting || FCk_Handle(InHandle) != _LocalPendingDeep.Get_EntityUnderConstruction()) { return; }
        _LocalDeep = FCk_Handle(InHandle); Request_CheckLocalProjectionAdmission();
    }

    private void Request_CheckLocalProjectionAdmission()
    {
        if (!CkSceneNodeTweenGym::IsLiveEntity(_LocalSimple) ||
            !CkSceneNodeTweenGym::IsLiveEntity(_LocalChain) ||
            !CkSceneNodeTweenGym::IsLiveEntity(_LocalDeep))
        { return; }
        if (utils_entity_lifetime::Get_LifetimeOwner(_LocalSimple) != CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Simple") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalChain) != CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Chain") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalDeep) != CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Deep") ||
            !Snapshot.Ready || Snapshot.Generation != LocalProjectionGeneration)
        { Request_FailLocalProjection("Projection completion has wrong station or generation"); return; }
        auto SimpleScript = Cast<UCk_EntityScript_SceneNodeTweenGym_SimpleStation>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(_LocalSimple));
        auto ChainScript = Cast<UCk_EntityScript_SceneNodeTweenGym_ChainStation>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(_LocalChain));
        auto DeepScript = Cast<UCk_EntityScript_SceneNodeTweenGym_DeepStation>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(_LocalDeep));
        if (ck::Is_NOT_Valid(SimpleScript) || !SimpleScript.Get_HasValidProjectionComposition() ||
            SimpleScript.Generation != LocalProjectionGeneration ||
            ck::Is_NOT_Valid(ChainScript) || !ChainScript.Get_HasValidProjectionComposition() ||
            ChainScript.Generation != LocalProjectionGeneration ||
            ck::Is_NOT_Valid(DeepScript) || !DeepScript.Get_HasValidProjectionComposition() ||
            DeepScript.Generation != LocalProjectionGeneration)
        { Request_FailLocalProjection("Projection construction rejected its client role"); return; }
        _LocalProjectionStarting = false;
        LocalProjectionReady = true;
    }

    private bool Get_IsValidLocalProjection()
    {
        if (!CkSceneNodeTweenGym::IsLiveEntity(_LocalSimple) ||
            !CkSceneNodeTweenGym::IsLiveEntity(_LocalChain) ||
            !CkSceneNodeTweenGym::IsLiveEntity(_LocalDeep) ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalSimple) != CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Simple") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalChain) != CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Chain") ||
            utils_entity_lifetime::Get_LifetimeOwner(_LocalDeep) != CkSceneNodeTweenGym::FindStation(n"Gym.SceneNodeTween.Deep"))
        { return false; }
        auto SimpleScript = Cast<UCk_EntityScript_SceneNodeTweenGym_SimpleStation>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(_LocalSimple));
        auto ChainScript = Cast<UCk_EntityScript_SceneNodeTweenGym_ChainStation>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(_LocalChain));
        auto DeepScript = Cast<UCk_EntityScript_SceneNodeTweenGym_DeepStation>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(_LocalDeep));
        return ck::IsValid(SimpleScript) && SimpleScript.Get_HasValidProjectionComposition() &&
            SimpleScript.Generation == LocalProjectionGeneration &&
            ck::IsValid(ChainScript) && ChainScript.Get_HasValidProjectionComposition() &&
            ChainScript.Generation == LocalProjectionGeneration &&
            ck::IsValid(DeepScript) && DeepScript.Get_HasValidProjectionComposition() &&
            DeepScript.Generation == LocalProjectionGeneration;
    }

    private void Request_FailLocalProjection(FString InReason)
    {
        Request_RetireLocalProjection();
        LocalProjectionFailed = true;
        LocalProjectionFailureReason = InReason;
        ck::Error("[SceneNodeTweenGym] " + InReason);
    }

    void Request_FailCurrentLocalChild(FCk_Handle InChild, int32 InGeneration, FString InReason)
    {
        if (HasAuthority() || LocalProjectionFailed || !LocalProjectionReady ||
            InGeneration != LocalProjectionGeneration ||
            (InChild != _LocalSimple && InChild != _LocalChain && InChild != _LocalDeep))
        { return; }
        Request_FailLocalProjection(InReason);
    }

    private void Request_RetireLocalProjection()
    {
        LocalProjectionReady = false;
        _LocalProjectionStarting = false;
        if (utils_pending_entity_script::Get_IsValid(_LocalPendingSimple))
        { utils_pending_entity_script::Request_DestroyEntity(_LocalPendingSimple); }
        else if (ck::IsValid(_LocalSimple)) { utils_entity_lifetime::Request_DestroyEntity(_LocalSimple); }
        if (utils_pending_entity_script::Get_IsValid(_LocalPendingChain))
        { utils_pending_entity_script::Request_DestroyEntity(_LocalPendingChain); }
        else if (ck::IsValid(_LocalChain)) { utils_entity_lifetime::Request_DestroyEntity(_LocalChain); }
        if (utils_pending_entity_script::Get_IsValid(_LocalPendingDeep))
        { utils_pending_entity_script::Request_DestroyEntity(_LocalPendingDeep); }
        else if (ck::IsValid(_LocalDeep)) { utils_entity_lifetime::Request_DestroyEntity(_LocalDeep); }
        _LocalPendingSimple = FCk_Handle_PendingEntityScript();
        _LocalPendingChain = FCk_Handle_PendingEntityScript();
        _LocalPendingDeep = FCk_Handle_PendingEntityScript();
        _LocalSimple = FCk_Handle(); _LocalChain = FCk_Handle(); _LocalDeep = FCk_Handle();
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
