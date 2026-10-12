// The station and floor belong to the world, not to whichever player first visits the gym.
class ACk_CrowdGym_Separation_Floor : ACk_Gym_Floor
{
    default Replicates = true;
    default bReplicateMovement = true;

    UFUNCTION(BlueprintOverride) void ConstructionScript()
    {
        Super::ConstructionScript();
        SetActorScale3D(FVector(20.0, 20.0, 0.5));
    }
}

class ACk_CrowdGym_Separation_GameState : ACkCrowdGym_GameState_UE
{
    UPROPERTY(Replicated) bool SharedFailed = false;
    UPROPERTY(Replicated) bool SharedStarted = false;

    private FCk_Handle _WorldStationOwner;
    private FCk_Handle _AdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private FCk_Handle _Station;
    private FCk_Handle_PendingEntityScript _PendingStation;
    private ACk_CrowdGym_Separation_Floor _Floor;
    private bool _OwnsStation = false;
    private bool _OwnsAdmission = false;
    private bool _Starting = false;
    private float _StartSeconds = 0.0;

    void Request_EnsureSharedDemo()
    {
        if (!HasAuthority() || !Get_HasPublishedCatalog() ||
            _Starting || SharedStarted || SharedFailed) { return; }
        _Starting = true;
        _StartSeconds = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnStartDeadline", 20.0, false);
        Request_TryAcquireStation();
    }

    private bool IsLive(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) && !utils_entity_lifetime::Get_IsPendingDestroy(
            InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
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
        _OwnsAdmission = false;
        if (IsLive(_AdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_CloseWorldOnFailure()
    {
        auto World = ck::TransientEntity();
        if (!IsLive(World)) { return; }
        if (_AdmissionTicket != 0 &&
            CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner))
        { return; }
        // Publication released the ticket, but failure still closes this exact world.
        CkGym_Admission::TryCloseWorldUnproven(World);
    }

    private void Request_Fail(FString Reason, bool bReport = true)
    {
        if (SharedFailed) { return; }
        SharedFailed = true; SharedStarted = false; _Starting = false;
        Request_CloseWorldOnFailure();
        ForceNetUpdate();
        // Native closes known motion. Neither a failed ticket nor a released ticket grants
        // permission to destroy a partial station, floor, source root, or unknown child.
        Request_CloseFailedShared();
        if (bReport) { ck::Error("[Crowd Separation] " + Reason); }
    }

    UFUNCTION(BlueprintOverride) bool OnAuthorityTerminalFailure(bool bFailure)
    {
        if (!HasAuthority()) { return false; }
        if (bFailure && !SharedFailed)
        { Request_Fail("Native shared source terminal"); }
        else
        {
            SharedStarted = false; _Starting = false;
            Request_CloseWorldOnFailure();
        }
        auto World = ck::TransientEntity();
        return IsLive(World) && CkGym_Admission::Get_IsClosedUnproven(World);
    }

    private void Request_TryAcquireStation()
    {
        if (!_Starting || _OwnsAdmission) { return; }
        auto World = ck::TransientEntity();
        if (!IsLive(World) || !utils_entity_lifetime::Get_CanCreateEntity(World))
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
        _AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!IsLive(_AdmissionOwner))
        { Request_Fail("Station admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; }
        _OwnsAdmission = true;
        auto Matches = utils_entity_tag::ForEach_Entity(World, n"Gym.Crowd.Separation");
        if (Matches.Num() > 1 || (Matches.Num() == 1 &&
            (!IsLive(Matches[0]) || !Matches[0].Has_Fragment(FCkGym_Station_Anchors) ||
             !utils_entity_lifetime::Has_LifetimeOwner(Matches[0]) ||
             utils_entity_lifetime::Get_LifetimeOwner(Matches[0]) != World)))
        { Request_Fail("Station identity ambiguous"); return; }
        if (Matches.Num() == 1) { _Station = Matches[0]; }
        else
        {
            _WorldStationOwner = utils_entity_lifetime::Request_CreateEntity(World);
            if (!IsLive(_WorldStationOwner))
            { Request_Fail("Station owner allocation failed"); return; }
            auto Specs = CkCrowdSeparation::Get_Stations();
            if (Specs.Num() != 1 || Specs[0].Tags.Num() != 1 ||
                Specs[0].Tags[0] != n"Gym.Crowd.Separation")
            { Request_Fail("Station declaration invalid"); return; }
            _PendingStation = CkGym_Common::Request_SpawnNewStationForOwner(
                Specs[0], _WorldStationOwner,
                FCk_Delegate_Request_OnCompleted(this, n"OnStationSpawnCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(_PendingStation))
            { Request_Fail("Station spawn rejected"); return; }
            _OwnsStation = true;
            utils_pending_entity_script::Promise_OnConstructed(_PendingStation,
                FCk_Delegate_EntityScript_Constructed(this, n"OnStationConstructed"));
        }
        System::SetTimer(this, n"OnStationSettle", 0.05, false);
    }

    UFUNCTION() private void OnAdmissionWait()
    {
        if (!_Starting || _OwnsAdmission) { return; }
        if (float(System::GetGameTimeInSeconds()) - _StartSeconds >= 20.0)
        { Request_Fail("Station admission timed out"); return; }
        Request_TryAcquireStation();
    }

    UFUNCTION() private void OnStationSpawnCompleted(FCk_Handle Owner,
        ECk_Request_OperationResult Result)
    {
        if (_Starting && utils_pending_entity_script::Get_IsValid(_PendingStation) &&
            Owner == _PendingStation.Get_EntityUnderConstruction() &&
            Result != ECk_Request_OperationResult::Succeeded)
        { Request_Fail("Station spawn failed"); }
    }

    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InHandle)
    {
        if (!_Starting || !utils_pending_entity_script::Get_IsValid(_PendingStation) ||
            FCk_Handle(InHandle) != _PendingStation.Get_EntityUnderConstruction()) { return; }
        auto Child = FCk_Handle(InHandle);
        if (!IsLive(Child) || !utils_entity_lifetime::Has_LifetimeOwner(Child) ||
            utils_entity_lifetime::Get_LifetimeOwner(Child) != _WorldStationOwner)
        { Request_Fail("Station completion owner invalid"); return; }
        _Station = Child;
    }

    UFUNCTION() private void OnStationSettle()
    {
        if (!_Starting || !_OwnsAdmission) { return; }
        if (!IsLive(_Station))
        { System::SetTimer(this, n"OnStationSettle", 0.05, false); return; }
        auto World = ck::TransientEntity();
        if (!IsLive(World)) { Request_Fail("World lost during station settle"); return; }
        auto Matches = utils_entity_tag::ForEach_Entity(World, n"Gym.Crowd.Separation");
        if (Matches.Num() != 1 || Matches[0] != _Station ||
            !_Station.Has_Fragment(FCkGym_Station_Anchors) ||
            !utils_entity_lifetime::Has_LifetimeOwner(_Station) ||
            utils_entity_lifetime::Get_LifetimeOwner(_Station) !=
                (_OwnsStation ? _WorldStationOwner : World))
        { Request_Fail("Station tag or anchors invalid"); return; }
        const auto Anchors = _Station.Get_Fragment(FCkGym_Station_Anchors);
        if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
        _Floor = SpawnActor(ACk_CrowdGym_Separation_Floor, FVector::ZeroVector,
            FRotator::ZeroRotator, NAME_None, true);
        if (ck::Is_NOT_Valid(_Floor))
        { Request_Fail("Shared floor spawn failed"); return; }
        _Floor.SetActorScale3D(FVector(20.0, 20.0, 0.5));
        FinishSpawningActor(_Floor);
        if (!TryInitializeShared(_Station, FTransform(FRotator::ZeroRotator, Anchors.FootprintCenter),
            ACk_CrowdGym_Separation_PlayerController, ACk_Gym_Base_Pawn))
        { Request_Fail("Shared simulation rejected initialization"); return; }
        System::SetTimer(this, n"OnSharedSettle", 0.05, false);
    }

    UFUNCTION() private void OnSharedSettle()
    {
        if (!_Starting) { return; }
        if (Get_HasReadyServerSimulation() && Get_HasCompleteSnapshot())
        { _Starting = false; SharedStarted = true; ForceNetUpdate(); return; }
        if (float(System::GetGameTimeInSeconds()) - _StartSeconds >= 20.0)
        { Request_Fail("Shared simulation did not settle"); return; }
        System::SetTimer(this, n"OnSharedSettle", 0.05, false);
    }

    UFUNCTION() private void OnStartDeadline()
    { if (_Starting) { Request_Fail("Shared startup deadline"); } }

    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        if (HasAuthority() && _AdmissionTicket != 0)
        {
            SharedFailed = true; SharedStarted = false; _Starting = false;
            Request_CloseWorldOnFailure();
            return;
        }
        if (_Starting || SharedStarted) { Request_Fail("World ending", false); }
    }
}
