// One authority cohort and reveal clock. Every client projects the same sample locally.
USTRUCT()
struct FCkStationShowcase_Snapshot
{
    UPROPERTY() int32 CatalogVersion = 0;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() int32 RevealedLines = 0;
    UPROPERTY() int32 TickCount = 0;
    UPROPERTY() FString RuntimeTitle;
    UPROPERTY() FString RuntimeBody;
    // Complete authored station configuration is retained for every owner delivery.
    UPROPERTY() TArray<FCk_GymStation_SpawnParams> Stations;
    UPROPERTY() TArray<FString> StationTitles;
    UPROPERTY() TArray<FString> StationBodies;
    UPROPERTY() TArray<FString> StationInstructions;
}

USTRUCT()
struct FCkStationShowcase_ControlCommit
{
    UPROPERTY() int32 DeliverySerial = 0;
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 OriginGeneration = 0;
    UPROPERTY() ACk_StationShowcaseGym_GameState OriginState;
    UPROPERTY() ACk_StationShowcaseGym_PlayerController Owner;
    UPROPERTY() ACk_Gym_Base_Pawn OwnerPawn;
    UPROPERTY() FCkStationShowcase_Snapshot Snapshot;
}

class ACk_StationShowcaseGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkStationShowcase_Snapshot Snapshot;
    UPROPERTY() TArray<FCk_Handle> ServerStations;
    UPROPERTY() FCk_Handle ServerTicker;

    private FCk_Handle _CohortOwner;
    private FCk_Handle _AdmissionOwner;
    private int32 _AdmissionTicket = 0;
    private FCk_Handle _ReleasedAdmissionOwner;
    private bool _AdmissionOwned = false;
    private TArray<FCk_Handle_PendingEntityScript> _PendingStations;
    private FCk_Handle_PendingEntityScript _PendingTicker;
    private TArray<int32> _StationCompleted;
    private TArray<FCk_Handle> _Retiring;
    private TArray<FCk_Handle> _KnownCohort;
    private bool _Starting = false;
    private bool _RetiringOld = false;
    private bool _Ending = false;
    private int32 _NextGeneration = 0;
    private float _StartedAt = 0.0f;
    private ACk_StationShowcaseGym_PlayerController _Requester;
    private ACk_Gym_Base_Pawn _RequesterPawn;
    private int32 _RequestSerial = 0;
    private int32 _RequestGeneration = 0;
    private FCkStationShowcase_ControlCommit _PreparedCommit;
    private int32 _CatalogIndex = -1;
    private FCkGym_Entry _CatalogEntry;

    bool Get_HasWorldIdentity()
    {
        auto World = GetWorld();
        if (_Ending || !HasAuthority() || ck::Is_NOT_Valid(World) ||
            World.GetGameState() != this || GetClass() != ACk_StationShowcaseGym_GameState ||
            !Get_HasPublishedCatalog()) { return false; }
        auto Mode = Gameplay::GetGameMode();
        TArray<ACk_StationShowcaseGym_GameMode> Modes;
        GetAllActorsOfClass(ACk_StationShowcaseGym_GameMode, Modes);
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        int32 Current = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        if (ck::Is_NOT_Valid(Mode) || Modes.Num() != 1 || Modes[0] != Mode ||
            Mode.GetWorld() != World || Mode.GetClass() != ACk_StationShowcaseGym_GameMode ||
            Current < 0 || Current >= Registry.Num()) { return false; }
        auto Entry = Registry[Current];
        if (Entry.DisplayName != "Station Showcase" ||
            Entry.GameModeClass != ACk_StationShowcaseGym_GameMode) { return false; }
        return _CatalogIndex < 0 || (Current == _CatalogIndex &&
            Entry.DisplayName == _CatalogEntry.DisplayName &&
            Entry.GameModeClass == _CatalogEntry.GameModeClass &&
            Entry.LevelName == _CatalogEntry.LevelName && Entry.Category == _CatalogEntry.Category &&
            Entry.HintCode == _CatalogEntry.HintCode);
    }

    void Request_EnsureSharedDemo()
    {
        if (!Get_HasWorldIdentity() || _Starting || _RetiringOld ||
            Snapshot.Ready || Snapshot.Failed) { return; }
        if (_CatalogIndex < 0)
        {
            _CatalogIndex = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
            _CatalogEntry = UCk_Utils_GymRegistry_UE::Get_GymRegistry()[_CatalogIndex];
        }
        Request_StartGeneration();
    }

    UFUNCTION(BlueprintPure)
    bool Get_IsReady()
    {
        auto Script = CkStationShowcase::IsLive(ServerTicker)
            ? Cast<UCk_EntityScript_GymStation_Showcase_AutoSizeTicker>(
                utils_entity_script::TryGet_EntityScript(ServerTicker)) : nullptr;
        return Get_HasWorldIdentity() && Get_IsCompleteSnapshot(Snapshot) && !_Starting &&
            !_RetiringOld && ServerStations.Num() == CkStationShowcase::SlotCount &&
            CkStationShowcase::IsLive(ServerTicker) && ck::IsValid(Script) &&
            Script.GetClass() == UCk_EntityScript_GymStation_Showcase_AutoSizeTicker &&
            CkStationShowcase::IsLive(FCk_Handle(Script.Get_Clock())) &&
            utils_timer::DoCast(FCk_Handle(Script.Get_Clock())).IsSet() &&
            utils_entity_lifetime::Has_LifetimeOwner(ServerTicker) &&
            utils_entity_lifetime::Get_LifetimeOwner(ServerTicker) == _CohortOwner &&
            Get_HasFullCohort();
    }

    UFUNCTION(BlueprintPure) FCk_Handle Get_ShowcaseClock()
    {
        if (!Get_IsReady() || !CkStationShowcase::IsLive(ServerTicker)) { return FCk_Handle(); }
        auto Script = Cast<UCk_EntityScript_GymStation_Showcase_AutoSizeTicker>(
            utils_entity_script::TryGet_EntityScript(ServerTicker));
        if (ck::Is_NOT_Valid(Script) || Script.GetClass() != UCk_EntityScript_GymStation_Showcase_AutoSizeTicker ||
            !Script.Get_HasClock()) { return FCk_Handle(); }
        return FCk_Handle(Script.Get_Clock());
    }

    FCk_Handle Get_ServerStation(int32 InSlot)
    {
        if (!Get_IsReady() || InSlot < 0 || InSlot >= ServerStations.Num())
        { return FCk_Handle(); }
        return ServerStations[InSlot];
    }

    bool Get_IsValidOwner(ACk_StationShowcaseGym_PlayerController InOwner,
        ACk_Gym_Base_Pawn InPawn)
    {
        return Get_HasCapturedOwnerContext(InOwner, InPawn) && !InOwner.OwnerSessionFailed;
    }

    bool Get_HasCapturedOwnerContext(ACk_StationShowcaseGym_PlayerController InOwner,
        ACk_Gym_Base_Pawn InPawn)
    {
        auto World = GetWorld();
        return Get_HasWorldIdentity() && ck::IsValid(InOwner) &&
            InOwner.GetClass() == ACk_StationShowcaseGym_PlayerController &&
            InOwner.HasAuthority() && InOwner.GetWorld() == World &&
            ck::IsValid(InPawn) && InPawn.GetWorld() == World &&
            InPawn.GetController() == InOwner && InOwner.GetControlledPawn() == InPawn;
    }

    // Received/retained whole recipes use strict wire equivalence; authority spawn preflight stays exact.
    UFUNCTION(BlueprintPure)
    bool Get_IsCompleteSnapshot(FCkStationShowcase_Snapshot InSample)
    {
        if (!InSample.Ready || InSample.Failed || InSample.Generation <= 0 ||
            InSample.Sequence <= 0 || InSample.CatalogVersion != CkStationShowcase::CatalogVersion ||
            InSample.Stations.Num() != CkStationShowcase::SlotCount ||
            InSample.StationTitles.Num() != CkStationShowcase::SlotCount ||
            InSample.StationBodies.Num() != CkStationShowcase::SlotCount ||
            InSample.StationInstructions.Num() != CkStationShowcase::SlotCount ||
            InSample.RevealedLines < 0 || InSample.RevealedLines > 9 ||
            InSample.TickCount < 0 ||
            InSample.RuntimeTitle != InSample.StationTitles[2] ||
            InSample.RuntimeBody != InSample.StationBodies[2]) { return false; }
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            if (!CkStationShowcase::Get_IsExactWireParams(InSample.Stations[i], i))
            { return false; }
        }
        return true;
    }

    bool Request_Rebuild(ACk_StationShowcaseGym_PlayerController InOwner,
        ACk_Gym_Base_Pawn InPawn, int32 InSerial, int32 InGeneration)
    {
        if (!Get_IsReady() || !Get_IsValidOwner(InOwner, InPawn) ||
            InSerial <= 0 || InGeneration != Snapshot.Generation ||
            ck::IsValid(_Requester) || !InOwner.Get_CanAcceptOwnerCommit() ||
            !InOwner.Get_HasReservedShowcaseControl(InSerial, InGeneration, InPawn))
        { return false; }
        _Requester = InOwner; _RequesterPawn = InPawn;
        _RequestSerial = InSerial; _RequestGeneration = InGeneration;
        Snapshot.Ready = false;
        Snapshot.Sequence++;
        ForceNetUpdate();
        Request_RetireCohort();
        _RetiringOld = true;
        _StartedAt = float(System::GetGameTimeInSeconds());
        System::SetTimer(this, n"OnMonitor", 0.1, true);
        return true;
    }

    private void Request_StartGeneration()
    {
        if (_NextGeneration >= 2147483646)
        { Request_Fail("Showcase generation exhausted"); return; }
        _NextGeneration++;
        Snapshot = FCkStationShowcase_Snapshot();
        Snapshot.CatalogVersion = CkStationShowcase::CatalogVersion;
        Snapshot.Generation = _NextGeneration;
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        { Snapshot.Stations.Add(CkStationShowcase::Get_Params(i)); }
        _Starting = true;
        _StartedAt = float(System::GetGameTimeInSeconds());
        ForceNetUpdate();
        System::SetTimer(this, n"OnMonitor", 0.1, true);
        Request_SpawnCohort();
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
        if (CkStationShowcase::IsLive(_AdmissionOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_AdmissionOwner); }
        _ReleasedAdmissionOwner = _AdmissionOwner;
        _AdmissionOwner = FCk_Handle();
        return true;
    }

    private void Request_SpawnCohort()
    {
        if (!_Starting || _Ending || Snapshot.Failed) { return; }
        auto World = ck::TransientEntity();
        if (!CkStationShowcase::IsLive(World) ||
            !utils_entity_lifetime::Get_CanCreateEntity(World))
        { Request_Fail("Showcase world owner unavailable"); return; }
        if (Snapshot.Stations.Num() != CkStationShowcase::SlotCount ||
            Snapshot.Generation <= 0 || Snapshot.Generation != _NextGeneration ||
            Snapshot.CatalogVersion != CkStationShowcase::CatalogVersion)
        { Request_Fail("Showcase provisional catalog shape invalid"); return; }
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            if (!CkStationShowcase::Get_IsExactAuthoredParams(Snapshot.Stations[i], i))
            { Request_Fail("Showcase provisional authored recipe changed"); return; }
        }
        if (CkGym_Admission::Get_IsClosedUnproven(World))
        { Request_Fail("Station admission permanently closed"); return; }
        if (CkGym_Admission::Get_IsBusy(World))
        { System::SetTimer(this, n"OnAdmissionRetry", 0.1, false); return; }
        _AdmissionTicket = CkGym_Admission::TryReserve(World);
        if (_AdmissionTicket == 0)
        { Request_Fail("Station admission reservation rejected"); return; }
        _AdmissionOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkStationShowcase::IsLive(_AdmissionOwner))
        { Request_Fail("Showcase admission allocation failed"); return; }
        if (!CkGym_Admission::TryCommitOwner(World, _AdmissionTicket, _AdmissionOwner))
        { CkGym_Admission::TryCloseUnproven(World, _AdmissionTicket, _AdmissionOwner);
          Request_Fail("Station admission owner commit rejected"); return; } _AdmissionOwned = true;
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            auto Params = Snapshot.Stations[i];
            if (!CkStationShowcase::Get_IsExactAuthoredParams(Params, i) ||
                utils_entity_tag::ForEach_Entity(World, Params.StationTags[0]).Num() != 0)
            { Request_Fail("Showcase station catalog or identity invalid"); return; }
        }
        _CohortOwner = utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkStationShowcase::IsLive(_CohortOwner))
        { Request_Fail("Showcase cohort allocation failed"); return; }
        Request_Remember(_CohortOwner);
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            auto Pending = utils_entity_script::Request_SpawnEntity(_CohortOwner,
                UCk_EntityScript_GymStation,
                FInstancedStruct::Make(Snapshot.Stations[i]),
                FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_Fail("Showcase station spawn rejected"); return; }
            _PendingStations.Add(Pending);
            _StationCompleted.Add(0);
            ServerStations.Add(FCk_Handle());
            utils_pending_entity_script::Promise_OnConstructed(Pending,
                FCk_Delegate_EntityScript_Constructed(this, n"OnStationConstructed"));
        }
    }

    UFUNCTION() private void OnAdmissionRetry()
    { if (_Starting && !_AdmissionOwned && !_Ending) { Request_SpawnCohort(); } }

    UFUNCTION() private void OnSpawnCompleted(FCk_Handle InOwner,
        ECk_Request_OperationResult InResult)
    {
        if (!_Starting || _Ending) { return; }
        for (int32 i = 0; i < _PendingStations.Num(); i++)
        {
            if (_PendingStations[i].Get_EntityUnderConstruction() != InOwner) { continue; }
            if (InResult != ECk_Request_OperationResult::Succeeded)
            { Request_Fail("Showcase station completion failed"); return; }
            _StationCompleted[i] = 1;
            return;
        }
        if (utils_pending_entity_script::Get_IsValid(_PendingTicker) &&
            _PendingTicker.Get_EntityUnderConstruction() == InOwner &&
            InResult != ECk_Request_OperationResult::Succeeded)
        { Request_Fail("Showcase ticker completion failed"); }
    }

    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript InScript)
    {
        auto Entity = FCk_Handle(InScript);
        if (!_Starting || _Ending || !CkStationShowcase::IsLive(Entity)) { return; }
        for (int32 i = 0; i < _PendingStations.Num(); i++)
        {
            if (_PendingStations[i].Get_EntityUnderConstruction() != Entity) { continue; }
            if (!utils_entity_lifetime::Has_LifetimeOwner(Entity) ||
                utils_entity_lifetime::Get_LifetimeOwner(Entity) != _CohortOwner ||
                CkStationShowcase::IsLive(ServerStations[i]))
            { Request_Fail("Showcase station construction changed owner"); return; }
            ServerStations[i] = Entity;
            Request_Remember(Entity);
            return;
        }
        Request_Fail("Unexpected Showcase station completion");
    }

    UFUNCTION() private void OnTickerConstructed(FCk_Handle_EntityScript InScript)
    {
        if (!_Starting || _Ending) { return; }
        auto Entity = FCk_Handle(InScript);
        if (!CkStationShowcase::IsLive(Entity) ||
            !utils_pending_entity_script::Get_IsValid(_PendingTicker) ||
            _PendingTicker.Get_EntityUnderConstruction() != Entity ||
            !utils_entity_lifetime::Has_LifetimeOwner(Entity) ||
            utils_entity_lifetime::Get_LifetimeOwner(Entity) != _CohortOwner)
        { Request_Fail("Showcase ticker construction invalid"); return; }
        ServerTicker = Entity;
        Request_Remember(Entity);
    }

    private bool Get_HasFullCohort()
    {
        auto WorldOwner = ck::TransientEntity();
        if (!CkStationShowcase::IsLive(WorldOwner) || !CkStationShowcase::IsLive(_CohortOwner) ||
            ServerStations.Num() != CkStationShowcase::SlotCount) { return false; }
        for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
        {
            auto Station = ServerStations[i];
            auto Tagged = utils_entity_tag::ForEach_Entity(WorldOwner,
                CkStationShowcase::Get_StationTag(i));
            if (!CkStationShowcase::IsLive(Station) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Station) ||
                utils_entity_lifetime::Get_LifetimeOwner(Station) != _CohortOwner ||
                !Station.Has_Fragment(FCkGym_Station_TitleAndDescription) ||
                Tagged.Num() != 1 || Tagged[0] != Station ||
                !CkStationShowcase::Get_IsExactStationScript(Station))
            { return false; }
        }
        return true;
    }

    UFUNCTION() private void OnMonitor()
    {
        if (_Ending || Snapshot.Failed) { return; }
        if (!Get_HasWorldIdentity())
        { Request_Fail("Showcase world or frozen catalog identity lost"); return; }
        if (_RequestSerial > 0)
        {
            bool ContextLive = Get_HasCapturedOwnerContext(_Requester, _RequesterPawn);
            bool OwnerTerminal = ck::IsValid(_Requester) && _Requester.OwnerSessionFailed;
            if (!ContextLive || OwnerTerminal)
            {
                if (ck::IsValid(_Requester))
                { _Requester.Request_DetachShowcaseControl(this, _RequesterPawn,
                    _RequestSerial, _RequestGeneration); }
                _Requester = nullptr; _RequesterPawn = nullptr;
                _RequestSerial = 0; _RequestGeneration = 0;
                _PreparedCommit = FCkStationShowcase_ControlCommit();
            }
            else if (!_Requester.Get_HasReservedShowcaseControl(_RequestSerial,
                _RequestGeneration, _RequesterPawn) || !_Requester.Get_CanAcceptOwnerCommit())
            { Request_Fail("Showcase live owner reservation corrupted"); return; }
        }
        if (_RetiringOld)
        {
            if (float(System::GetGameTimeInSeconds()) - _StartedAt > 20.0f)
            { Request_Fail("Showcase retirement timed out"); return; }
            for (auto Old : _Retiring)
            { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old)) { return; } }
            _Retiring.Empty(); _RetiringOld = false;
            Request_StartGeneration();
            return;
        }
        if (_Starting)
        {
            if (float(System::GetGameTimeInSeconds()) - _StartedAt > 20.0f)
            { Request_Fail("Showcase cohort readiness timed out"); return; }
            if (_PendingStations.Num() != CkStationShowcase::SlotCount) { return; }
            for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
            { if (_StationCompleted[i] != 1 || !CkStationShowcase::IsLive(ServerStations[i])) { return; } }
            if (!Get_HasFullCohort()) { return; }
            if (!utils_pending_entity_script::Get_IsValid(_PendingTicker))
            {
                auto TickerParams = CkStationShowcase::Get_TickerParams(Snapshot.Generation);
                _PendingTicker = utils_entity_script::Request_SpawnEntity(_CohortOwner,
                    UCk_EntityScript_GymStation_Showcase_AutoSizeTicker,
                    FInstancedStruct::Make(TickerParams),
                    FCk_Delegate_Request_OnCompleted(this, n"OnSpawnCompleted"));
                if (!utils_pending_entity_script::Get_IsValid(_PendingTicker))
                { Request_Fail("Showcase ticker spawn rejected"); return; }
                utils_pending_entity_script::Promise_OnConstructed(_PendingTicker,
                    FCk_Delegate_EntityScript_Constructed(this, n"OnTickerConstructed"));
                return;
            }
            if (!CkStationShowcase::IsLive(ServerTicker)) { return; }
            auto Script = Cast<UCk_EntityScript_GymStation_Showcase_AutoSizeTicker>(
                utils_entity_script::TryGet_EntityScript(ServerTicker));
            if (ck::Is_NOT_Valid(Script) ||
                Script.GetClass() != UCk_EntityScript_GymStation_Showcase_AutoSizeTicker ||
                !Script.Get_HasClock())
            { Request_Fail("Showcase ticker clock unavailable"); return; }
            auto Candidate = Snapshot;
            Candidate.Ready = true;
            Candidate.Sequence++;
            for (int32 i = 0; i < CkStationShowcase::SlotCount; i++)
            {
                auto Text = ServerStations[i].Get_Fragment(FCkGym_Station_TitleAndDescription);
                Candidate.StationTitles.Add(Text.Title.ToString());
                Candidate.StationBodies.Add(Text.Description.ToString());
                Candidate.StationInstructions.Add(Text.Instructions.ToString());
            }
            Candidate.RuntimeTitle = Candidate.StationTitles[2];
            Candidate.RuntimeBody = Candidate.StationBodies[2];
            if (!Get_IsCompleteSnapshot(Candidate))
            { Request_Fail("Showcase complete sample malformed"); return; }
            if (_RequestSerial > 0)
            {
                if (!Get_IsValidOwner(_Requester, _RequesterPawn) ||
                    !_Requester.Get_HasReservedShowcaseControl(_RequestSerial,
                        _RequestGeneration, _RequesterPawn))
                { Request_Fail("Showcase owner changed before delivery"); return; }
                _PreparedCommit = FCkStationShowcase_ControlCommit();
                _PreparedCommit.RequestSerial = _RequestSerial;
                _PreparedCommit.OriginGeneration = _RequestGeneration;
                _PreparedCommit.OriginState = this;
                _PreparedCommit.Owner = _Requester;
                _PreparedCommit.OwnerPawn = _RequesterPawn;
                _PreparedCommit.DeliverySerial = _Requester.Get_NextDeliverySerial();
                _PreparedCommit.Snapshot = Candidate;
                if (_PreparedCommit.DeliverySerial <= 0 ||
                    !_Requester.Request_TryRecordShowcaseCommit(this, _RequestSerial))
                { Request_Fail("Showcase retained owner commit rejected"); return; }
            }
            _Starting = false;
            Snapshot = Candidate;
            if (!Request_ReleaseAdmission(true)) { Request_Fail("Station admission publication rejected"); return; }
            ForceNetUpdate();
            _PreparedCommit = FCkStationShowcase_ControlCommit();
            _Requester = nullptr; _RequesterPawn = nullptr;
            _RequestSerial = 0; _RequestGeneration = 0;
            return;
        }
        if (Snapshot.Ready)
        {
            Request_Remember(_CohortOwner);
            for (auto Station : ServerStations) { Request_Remember(Station); }
            Request_Remember(ServerTicker);
        }
        if (Snapshot.Ready && !Get_IsReady())
        { Request_Fail("Showcase required station lost"); }
    }

    void Request_RecordReveal(FCk_Handle InTicker, int32 InGeneration, FString InTitle,
        FString InBody, int32 InLines, int32 InTicks)
    {
        if (!Get_IsReady() || InTicker != ServerTicker ||
            InGeneration != Snapshot.Generation || InTicks <= Snapshot.TickCount ||
            InLines < Snapshot.RevealedLines || InLines > 9 ||
            !CkStationShowcase::IsLive(ServerStations[2])) { return; }
        if (Snapshot.Sequence >= 2147483646)
        { Request_Fail("Showcase snapshot sequence exhausted"); return; }
        auto& Text = ServerStations[2].AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
        Text.Title = FText::FromString(InTitle);
        Text.Description = FText::FromString(InBody);
        Snapshot.RuntimeTitle = InTitle;
        Snapshot.RuntimeBody = InBody;
        Snapshot.StationTitles[2] = InTitle;
        Snapshot.StationBodies[2] = InBody;
        Snapshot.RevealedLines = InLines;
        Snapshot.TickCount = InTicks;
        Snapshot.Sequence++;
        ForceNetUpdate();
    }

    bool Request_TryGetPreparedShowcaseCommit(ACk_StationShowcaseGym_PlayerController InOwner,
        int32 InSerial, FCkStationShowcase_ControlCommit& Out)
    {
        Out = FCkStationShowcase_ControlCommit();
        if (!_Starting || !Get_IsValidOwner(InOwner, _RequesterPawn) ||
            InOwner != _Requester || InSerial != _RequestSerial ||
            _RequestGeneration <= 0 || InSerial <= 0 ||
            !InOwner.Get_HasReservedShowcaseControl(InSerial, _RequestGeneration,
                _RequesterPawn) ||
            _PreparedCommit.RequestSerial != InSerial ||
            _PreparedCommit.OriginState != this || _PreparedCommit.Owner != InOwner ||
            _PreparedCommit.OwnerPawn != _RequesterPawn ||
            _PreparedCommit.OriginGeneration != _RequestGeneration ||
            _PreparedCommit.DeliverySerial != InOwner.Get_NextDeliverySerial() ||
            _PreparedCommit.Snapshot.Generation != _RequestGeneration + 1 ||
            !Get_IsCompleteSnapshot(_PreparedCommit.Snapshot)) { return false; }
        Out = _PreparedCommit;
        return true;
    }

    private void Request_Capture(FCk_Handle InRoot)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InRoot) ||
            _Retiring.Contains(InRoot)) { return; }
        _Retiring.Add(InRoot);
        if (ck::IsValid(InRoot))
        { for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
          { Request_Capture(Child); } }
    }

    private void Request_Remember(FCk_Handle InRoot)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(InRoot)) { return; }
        if (!_KnownCohort.Contains(InRoot)) { _KnownCohort.Add(InRoot); }
        if (ck::IsValid(InRoot))
        { for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(InRoot))
          { Request_Remember(Child); } }
    }

    private void Request_RetireCohort()
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        { Request_Fail("Partial station admission cannot retire in this world"); return; }
        // Capture the entire allocated closure before the first destroy request.
        Request_Capture(_CohortOwner);
        Request_Capture(_AdmissionOwner);
        Request_Capture(_ReleasedAdmissionOwner);
        for (auto Known : _KnownCohort) { Request_Capture(Known); }
        for (auto Station : ServerStations) { Request_Capture(Station); }
        Request_Capture(ServerTicker);
        for (auto Pending : _PendingStations)
        { Request_Capture(Pending.Get_EntityUnderConstruction()); }
        Request_Capture(_PendingTicker.Get_EntityUnderConstruction());
        for (auto Pending : _PendingStations)
        { if (utils_pending_entity_script::Get_IsValid(Pending))
          { utils_pending_entity_script::Request_DestroyEntity(Pending); } }
        if (utils_pending_entity_script::Get_IsValid(_PendingTicker))
        { utils_pending_entity_script::Request_DestroyEntity(_PendingTicker); }
        if (CkStationShowcase::IsLive(_CohortOwner))
        { utils_entity_lifetime::Request_DestroyEntity(_CohortOwner); }
        _CohortOwner = FCk_Handle();
        ServerStations.Empty(); ServerTicker = FCk_Handle();
        _PendingStations.Empty(); _PendingTicker = FCk_Handle_PendingEntityScript();
        _StationCompleted.Empty();
        _KnownCohort.Empty();
        Request_ReleaseAdmission();
        _ReleasedAdmissionOwner = FCk_Handle();
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
            _Starting = false; _RetiringOld = false; Snapshot.Ready = false;
            Snapshot.Failed = true; Snapshot.FailureReason = InReason;
            ForceNetUpdate();
            return;
        }
        if (_Ending || Snapshot.Failed) { return; }
        _Starting = false; _RetiringOld = false;
        Snapshot.Ready = false; Snapshot.Failed = true;
        Snapshot.FailureReason = InReason;
        Request_RetireCohort();
        if (ck::IsValid(_Requester))
        { _Requester.Request_RejectShowcaseControl(_RequestSerial, 3); }
        _PreparedCommit = FCkStationShowcase_ControlCommit();
        _Requester = nullptr; _RequesterPawn = nullptr;
        _RequestSerial = 0; _RequestGeneration = 0;
        ForceNetUpdate();
    }

    UFUNCTION(BlueprintOverride)
    void EndPlay(EEndPlayReason InReason)
    {
        if (_AdmissionTicket != 0 || CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity()))
        {
            _Ending = true; _Starting = false; Snapshot.Ready = false;
            Request_HoldFailedAdmission();
            return;
        }
        _Ending = true;
        Request_RetireCohort();
    }
}
