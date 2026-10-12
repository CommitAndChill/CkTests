// One authority simulation and two authored stations for every observer in this PIE world.
class ACk_ObjectPoolingGym_GameState : ACkGym_GameState_UE
{
    default NetUpdateFrequency = 15.0;
    UPROPERTY(Replicated) FCkPoolingGym_Snapshot Snapshot;
    UPROPERTY() TArray<FCk_Handle> ServerStations;

    private FCkGym_SharedStationCohort _Cohort;
    private TArray<FCk_Handle> _StartupStationCarriers;
    private TArray<FCk_Handle> _StartupStationTargets;
    private FCk_Handle _StartupStationOwner;
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_StartupSpawnCarriers() const { return _StartupStationCarriers; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_StartupCarrierTargets() const { return _StartupStationTargets; }
    private FCk_Handle _TickOwner;
    private FCk_Handle_Timer _TickTimer;
    private FCk_Handle _HerdOwner;
    private TArray<FCk_Handle> _InFlight;
    private TArray<FCk_Handle> _SmHerd;
    private TArray<FCk_Handle_StateMachine> _SmMachines;
    private TArray<FCk_Handle_PendingEntityScript> _WavePending;
    private TArray<int32> _WaveGeneration;
    private TArray<int32> _WaveConstructed;
    private TArray<int32> _WaveCompleted;
    private TArray<int32> _WaveBurst;
    private TArray<int32> _BurstConstructedForTarget;
    private TArray<int32> _BurstCompletedForTarget;
    private TArray<FCk_Handle> _SteadyRetiring;
    private TArray<FCk_Handle> _SteadyRetiringParents;
    private TArray<FCk_Handle> _HerdRetiring;
    private TArray<FCk_Handle> _HerdRetiringParents;
    private TArray<FCk_Handle> _Retiring;
    private TArray<FCk_Handle> _RetiringParents;
    private TArray<FCk_Handle> _ReservedTickRetiring;
    private TArray<FCk_Handle> _ReservedTickParents;
    private TArray<FCk_Handle> _LastRestartRetired;
    private TArray<FCk_Handle> _BurstTargets;
    private int32 _BurstIssued = 0;
    UPROPERTY() int32 LastRestartOriginGeneration = 0;
    UPROPERTY() int32 LastRestartRequestSerial = 0;
    UPROPERTY() ACk_ObjectPoolingGym_PlayerController LastRestartOwner;
    private int32 _SmNextRespawnIndex = 0;
    private float _SmRespawnAccum = 0.0f;
    private float _DisplayAccum = 0.0f;
    private int32 _TotalSpawned = 0;
    private int32 _TotalDestroyed = 0;
    private int32 _Generation = 1;
    private bool _Armed = false, _Starting = false, _Ending = false, _RetiringSimulation = false;
    private bool _RetirementUnproven = false, _RetireFrozen = false;
    private float _StartedAt = 0.0f, _BusyStartedAt = 0.0f;
    private ACk_ObjectPoolingGym_PlayerController _Requester;
    private ACk_Gym_Base_Pawn _RequestPawn;
    private int32 _RequestSerial = 0, _RequestGeneration = 0, _Action = -1;
    private int32 _BurstConstructed = 0, _BurstCompleted = 0;
    private float _RestartSmBefore = 0.0f, _RestartDisplayBefore = 0.0f;
    private FCkPoolingGym_RestartCadence _LastRestartCadence;
    private FCkPoolingGym_RestartAdmissionAudit _LastRestartAdmissionAudit;
    private FString _LastRestartAdmissionReject;
    private TArray<FCk_Handle> _StationRetainedClosure;
    private TArray<FCk_Handle> _StationRetainedParents;
    private TArray<FString> _StationRetainedDescriptors;
    private FCkPoolingGym_ControlCommit _Prepared;

    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_ServerWave() const { return _InFlight; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_ServerHerd() const { return _SmHerd; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_CapturedRetirement() const { return _Retiring; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_LastRestartRetired() const { return _LastRestartRetired; }
    UFUNCTION(BlueprintPure) FCk_Handle Get_ServerTickOwner() const { return _TickOwner; }
    UFUNCTION(BlueprintPure) FCk_Handle Get_ServerHerdOwner() const { return _HerdOwner; }
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_CurrentPendingWaveTargets() const
    {
        TArray<FCk_Handle> Targets;
        for (auto Pending : _WavePending) { Targets.Add(Pending.Get_EntityUnderConstruction()); }
        return Targets;
    }
    // Whole authority simulation roots for an external allocated-closure census
    // immediately before R. Shared stations and the retained tick are excluded.
    UFUNCTION(BlueprintPure) TArray<FCk_Handle> Get_CurrentSimulationTargets()
    {
        TArray<FCk_Handle> Targets;
        if (!Get_IsReady() || _SteadyRetiring.Num()!=_SteadyRetiringParents.Num() ||
            _HerdRetiring.Num()!=_HerdRetiringParents.Num() ||
            _WavePending.Num()!=_WaveGeneration.Num() ||
            _WavePending.Num()!=_WaveConstructed.Num() ||
            _WavePending.Num()!=_WaveCompleted.Num() ||
            _WavePending.Num()!=_WaveBurst.Num()) { return Targets; }
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(_HerdOwner)) { return Targets; }
        Targets.Add(_HerdOwner);
        for (auto H : _SmHerd)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) && !Targets.Contains(H))
          { Targets.Add(H); } }
        for (auto H : _InFlight)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) && !Targets.Contains(H))
          { Targets.Add(H); } }
        for (auto Pending : _WavePending)
        { if (!utils_pending_entity_script::Get_IsValid(Pending)) { Targets.Empty(); return Targets; }
          auto H=Pending.Get_EntityUnderConstruction();
          if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) && !Targets.Contains(H))
          { Targets.Add(H); } }
        for (auto H : _SteadyRetiring)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) && !Targets.Contains(H))
          { Targets.Add(H); } }
        for (auto H : _HerdRetiring)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) && !Targets.Contains(H))
          { Targets.Add(H); } }
        return Targets;
    }
    UFUNCTION(BlueprintPure) bool Get_IsRetirementUnproven() const { return _RetirementUnproven || _Cohort.Get_IsRetirementUnproven(); }
    UFUNCTION(BlueprintPure) FCkPoolingGym_RestartCadence Get_LastRestartCadence() const { return _LastRestartCadence; }
    UFUNCTION(BlueprintPure) FCkPoolingGym_RestartAdmissionAudit Get_LastRestartAdmissionAudit() const
    { return _LastRestartAdmissionAudit; }
    UFUNCTION(BlueprintPure) FString Get_LastRestartAdmissionReject() const
    { return _LastRestartAdmissionReject; }

    bool Get_HasWorldIdentity()
    {
        auto World = GetWorld();
        auto Mode = Gameplay::GetGameMode();
        auto Registry = UCk_Utils_GymRegistry_UE::Get_GymRegistry();
        auto Index = UCk_Utils_GymRegistry_UE::Get_CurrentGymIndex();
        return !_Ending && HasAuthority() && ck::IsValid(World) && World.GetGameState() == this &&
            GetClass() == ACk_ObjectPoolingGym_GameState && Get_HasPublishedCatalog() &&
            ck::IsValid(Mode) && Mode.GetWorld() == World && Mode.GetClass() == ACk_ObjectPoolingGym_GameMode &&
            Index >= 0 && Index < Registry.Num() && Registry[Index].DisplayName == "Object Pooling Stress" &&
            Registry[Index].GameModeClass == ACk_ObjectPoolingGym_GameMode;
    }

    bool Get_HasCapturedOwnerContext(ACk_ObjectPoolingGym_PlayerController Owner, ACk_Gym_Base_Pawn Pawn)
    {
        return Get_HasWorldIdentity() && ck::IsValid(Owner) && Owner.GetClass() == ACk_ObjectPoolingGym_PlayerController &&
            Owner.HasAuthority() && Owner.GetWorld() == GetWorld() && ck::IsValid(Pawn) &&
            Pawn.GetWorld() == GetWorld() && Pawn.GetController() == Owner && Owner.GetControlledPawn() == Pawn;
    }
    bool Get_IsValidOwner(ACk_ObjectPoolingGym_PlayerController Owner, ACk_Gym_Base_Pawn Pawn)
    { return Get_HasCapturedOwnerContext(Owner, Pawn) && !Owner.OwnerSessionFailed; }

    UFUNCTION(BlueprintPure) bool Get_IsCompleteSnapshot(FCkPoolingGym_Snapshot S)
    {
        if (!S.Ready || S.Failed || !S.FailureReason.IsEmpty() || S.CatalogVersion != CkPoolingShared::CatalogVersion ||
            S.Generation <= 0 || S.Sequence <= 0 || S.Stations.Num()!=2 || S.Titles.Num()!=2 || S.Bodies.Num()!=2 ||
            S.TotalSpawned < 0 || S.TotalDestroyed < 0 || S.HerdSize != CkPoolingShared::HerdSize ||
            !CkPoolingShared::ValidSample(S.Script) || !CkPoolingShared::ValidSample(S.Ping) ||
            !CkPoolingShared::ValidSample(S.Pong) || !CkPoolingShared::ValidSample(S.Condition)) { return false; }
        for (int32 i=0;i<2;++i)
        {
            if (!CkPoolingShared::HasExactRecipe(S.Stations[i],i) ||
                S.Titles[i]!=CkPoolingShared::Get_Title(i) || S.Bodies[i].IsEmpty()) { return false; }
        }
        return true;
    }

    private bool Get_HasExactStations()
    {
        if (!Get_HasWorldIdentity() || _Cohort.Get_Phase()!=2 || _Cohort.Get_HasTerminalFailure() ||
            ServerStations.Num()!=2) { return false; }
        auto WorldRoot=ck::TransientEntity();
        if (!CkPoolingShared::IsLive(WorldRoot)) { return false; }
        // Detect a lost retained mature station before Get_Stations can
        // latch the cohort as unproven. Request_Fail owns the frozen retire.
        for (auto H : ServerStations) { if (!CkPoolingShared::IsLive(H)) { return false; } }
        auto CohortStations=_Cohort.Get_Stations();
        if (CohortStations.Num()!=2) { return false; }
        for (int32 i=0;i<2;++i)
        {
            auto H=ServerStations[i];
            auto Tagged=utils_entity_tag::ForEach_Entity(WorldRoot,CkPoolingShared::Get_Tag(i));
            if (!CkPoolingShared::IsLive(H) || CohortStations[i]!=H || Tagged.Num()!=1 || Tagged[0]!=H ||
                !H.Has_Fragment(FCkGym_Station_TitleAndDescription)) { return false; }
            auto Script=utils_entity_script::TryGet_EntityScript(H);
            if (ck::Is_NOT_Valid(Script) ||
                Script.GetClass()!=UCk_EntityScript_GymStation ||
                !CkPoolingShared::HasExactStationScript(Cast<UCk_EntityScript_GymStation>(Script),i)) { return false; }
        }
        return true;
    }
    UFUNCTION(BlueprintPure) bool Get_IsReady()
    { return Snapshot.Ready && !Snapshot.Failed && !_Starting && !_RetiringSimulation &&
        _RequestSerial==0 && Get_HasExactStations() && CkPoolingShared::IsLive(_TickOwner) &&
        CkPoolingShared::IsLive(_HerdOwner) && _SmHerd.Num()==CkPoolingShared::HerdSize && Get_HasLiveHerd(); }
    private bool Get_HasLiveHerd()
    {
        if (_SmHerd.Num()!=CkPoolingShared::HerdSize || !CkPoolingShared::IsLive(_HerdOwner)) { return false; }
        for (auto H : _SmHerd)
        { if (!CkPoolingShared::IsLive(H) || !utils_entity_lifetime::Has_LifetimeOwner(H) ||
              utils_entity_lifetime::Get_LifetimeOwner(H)!=_HerdOwner) { return false; } }
        return true;
    }

    void Request_EnsureSharedDemo()
    {
        if (!Get_HasWorldIdentity() || _Armed || Snapshot.Failed) { return; }
        _Armed=true; _Starting=true; _StartedAt=float(System::GetGameTimeInSeconds());
        System::SetTimer(this,n"OnMonitor",0.1f,true);
        TArray<FCk_GymStation_SpawnParams> Recipes;
        for (int32 i=0;i<2;++i) { Recipes.Add(CkPoolingShared::Get_Params(i)); }
        if (_Cohort.TryBegin(ck::TransientEntity(),Recipes,
            FCk_Delegate_Request_OnCompleted(this,n"OnStationCompleted"),
            FCk_Delegate_EntityScript_Constructed(this,n"OnStationConstructed")) != ECkGym_SharedStationBegin::Started)
        { Request_Fail("ObjectPooling shared station admission rejected"); return; }
        // Same-stack typed capture: the spawn processor later removes its request fragment.
        TArray<FCk_Handle> Carriers;
        TArray<FCk_Handle> Targets;
        FCk_Handle Owner;
        auto World=ck::TransientEntity();
        if (_Cohort.PendingStations.Num()!=2 || !CkPoolingShared::IsLive(World))
        { Request_Fail("ObjectPooling station request carrier capture rejected"); return; }
        for (int32 i=0;i<2;++i)
        {
            auto Pending=_Cohort.PendingStations[i];
            if (!utils_pending_entity_script::Get_IsValid(Pending))
            { Request_Fail("ObjectPooling station request carrier capture rejected"); return; }
            auto Target=Pending.Get_EntityUnderConstruction();
            if (!CkPoolingShared::IsLive(Target) ||
                !utils_entity_lifetime::Has_LifetimeOwner(Target))
            { Request_Fail("ObjectPooling station request carrier capture rejected"); return; }
            auto TargetOwner=utils_entity_lifetime::Get_LifetimeOwner(Target);
            if (!CkPoolingShared::IsLive(TargetOwner) ||
                !utils_entity_lifetime::Has_LifetimeOwner(TargetOwner) ||
                utils_entity_lifetime::Get_LifetimeOwner(TargetOwner)!=World ||
                (i>0 && TargetOwner!=Owner) || Targets.Contains(Target))
            { Request_Fail("ObjectPooling station request carrier capture rejected"); return; }
            FCk_Handle Carrier;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCapture_StationSpawnCarrier(
                    Target,TargetOwner,UCk_EntityScript_GymStation,World,Carrier) ||
                Carrier==FCk_Handle() || Carriers.Contains(Carrier))
            { Request_Fail("ObjectPooling station request carrier capture rejected"); return; }
            Owner=TargetOwner;
            Targets.Add(Target); Carriers.Add(Carrier);
        }
        _StartupStationOwner=Owner;
        _StartupStationTargets=Targets;
        _StartupStationCarriers=Carriers;
    }
    UFUNCTION() private void OnStationCompleted(FCk_Handle H, ECk_Request_OperationResult Result)
    { if (!_Ending && _Starting && !_Cohort.TryRecord_Completed(H,H,Result)) { Request_Fail("ObjectPooling shared station completion rejected"); } }
    UFUNCTION() private void OnStationConstructed(FCk_Handle_EntityScript H)
    { if (!_Ending && _Starting && !_Cohort.TryRecord_Constructed(FCk_Handle(H),H)) { Request_Fail("ObjectPooling shared station construction rejected"); } }

    private bool StartSimulation()
    {
        auto World=ck::TransientEntity();
        if (!CkPoolingShared::IsLive(World) || !utils_entity_lifetime::Get_CanCreateEntity(World) ||
            !Get_HasExactStations() || _Retiring.Num()!=0 || _RetirementUnproven) { return false; }
        if (!CkPoolingShared::IsLive(_TickOwner))
        {
            _TickOwner=utils_entity_lifetime::Request_CreateEntity(World);
            if (!CkPoolingShared::IsLive(_TickOwner)) { return false; }
            _TickTimer=utils_timer::Create_Tick(_TickOwner,FCk_Delegate_Timer(this,n"OnGymTick"));
            if (!ck::IsValid(_TickTimer)) { return false; }
        }
        _HerdOwner=utils_entity_lifetime::Request_CreateEntity(World);
        if (!CkPoolingShared::IsLive(_HerdOwner)) { return false; }
        for (int32 i=0;i<CkPoolingShared::HerdSize;++i)
        { if (!DoSpawnSmHerdMember()) { return false; } }
        return _SmHerd.Num()==CkPoolingShared::HerdSize;
    }
    private bool DoSpawnSmHerdMember()
    {
        if (!CkPoolingShared::IsLive(_HerdOwner) || !utils_entity_lifetime::Get_CanCreateEntity(_HerdOwner)) { return false; }
        auto Member=utils_entity_lifetime::Request_CreateEntity(_HerdOwner);
        if (!CkPoolingShared::IsLive(Member)) { return false; }
        Member.Set_DebugName(n"ObjectPooling.SmHerdMember");
        _SmHerd.Add(Member);
        auto Sm=UCk_Utils_StateMachine_UE::Add(Member,FCk_StateMachine_Spec(UCk_PoolSmGym_State_Ping));
        if (ck::IsValid(Sm)) { _SmMachines.Add(Sm); }
        return ck::IsValid(Sm);
    }

    private bool TrySpawnWave(int32 Count, bool Burst)
    {
        if (Count<=0 || Count>100 || !Get_HasExactStations()) { return false; }
        auto Station=ServerStations[0];
        if (!CkPoolingShared::IsLive(Station) || !utils_entity_lifetime::Get_CanCreateEntity(Station)) { return false; }
        for (int32 i=0;i<Count;++i)
        {
            auto Pending=UCk_Utils_EntityScript_UE::Request_SpawnEntity(Station,
                UCk_ObjectPoolingTest_PoolableScript,FInstancedStruct(),
                FCk_Delegate_Request_OnCompleted(this,n"OnWaveCompleted"));
            if (!utils_pending_entity_script::Get_IsValid(Pending) ||
                !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Pending.Get_EntityUnderConstruction()))
            { return false; }
            if (Burst)
            {
                auto Target=Pending.Get_EntityUnderConstruction();
                if (_BurstTargets.Contains(Target)) { return false; }
                _BurstTargets.Add(Target); _BurstConstructedForTarget.Add(0);
                _BurstCompletedForTarget.Add(0); ++_BurstIssued;
            }
            _WavePending.Add(Pending); _WaveGeneration.Add(_Generation);
            _WaveConstructed.Add(0); _WaveCompleted.Add(0); _WaveBurst.Add(Burst ? 1 : 0);
            utils_pending_entity_script::Promise_OnConstructed(Pending,
                FCk_Delegate_EntityScript_Constructed(this,n"OnWaveConstructed"));
            if (Snapshot.Failed || _RetiringSimulation) { return false; }
        }
        return true;
    }
    private int32 FindWave(FCk_Handle H)
    {
        if (_WavePending.Num()!=_WaveGeneration.Num() || _WavePending.Num()!=_WaveConstructed.Num() ||
            _WavePending.Num()!=_WaveCompleted.Num() || _WavePending.Num()!=_WaveBurst.Num()) { return -1; }
        int32 Found=-1;
        for (int32 i=0;i<_WavePending.Num();++i)
        { if (_WavePending[i].Get_EntityUnderConstruction()==H) { if (Found>=0) { return -1; } Found=i; } }
        return Found;
    }
    private void MaybeDrainWave(int32 Index)
    {
        if (Index<0 || Index>=_WavePending.Num() || _WaveConstructed[Index]!=1 || _WaveCompleted[Index]!=1) { return; }
        _WavePending.RemoveAt(Index); _WaveGeneration.RemoveAt(Index); _WaveConstructed.RemoveAt(Index);
        _WaveCompleted.RemoveAt(Index); _WaveBurst.RemoveAt(Index);
    }
    UFUNCTION() private void OnWaveConstructed(FCk_Handle_EntityScript Script)
    {
        if (_Ending || _RetiringSimulation || Snapshot.Failed) { return; }
        auto H=FCk_Handle(Script); int32 Index=FindWave(H);
        if (Index<0 && _LastRestartRetired.Contains(H) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return; }
        if (Index<0 || ServerStations.Num()!=2 || _WaveGeneration[Index]!=_Generation || _WaveConstructed[Index]!=0 ||
            !CkPoolingShared::IsLive(H) || !utils_entity_lifetime::Has_LifetimeOwner(H) ||
            utils_entity_lifetime::Get_LifetimeOwner(H)!=ServerStations[0] ||
            !utils_entity_script::Has(H)) { Request_Fail("ObjectPooling wave construction identity rejected"); return; }
        auto Body=utils_entity_script::TryGet_EntityScript(H);
        if (ck::Is_NOT_Valid(Body) || Body.GetClass()!=UCk_ObjectPoolingTest_PoolableScript)
        { Request_Fail("ObjectPooling wave script class rejected"); return; }
        _WaveConstructed[Index]=1; _InFlight.Add(H); ++_TotalSpawned;
        if (_WaveBurst[Index]==1)
        {
            int32 BatchIndex=_BurstTargets.FindIndex(H);
            if (BatchIndex<0 || BatchIndex>=_BurstConstructedForTarget.Num() ||
                _BurstConstructedForTarget[BatchIndex]!=0)
            { Request_Fail("ObjectPooling burst constructed target ledger rejected"); return; }
            _BurstConstructedForTarget[BatchIndex]=1; ++_BurstConstructed;
        }
        MaybeDrainWave(Index);
    }
    UFUNCTION() private void OnWaveCompleted(FCk_Handle H, ECk_Request_OperationResult Result)
    {
        if (_Ending || _RetiringSimulation || Snapshot.Failed) { return; }
        int32 Index=FindWave(H);
        if (Index<0 && _LastRestartRetired.Contains(H) &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return; }
        if (Index<0 || _WaveGeneration[Index]!=_Generation || _WaveCompleted[Index]!=0 ||
            Result!=ECk_Request_OperationResult::Succeeded ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H))
        { Request_Fail("ObjectPooling wave request completion rejected"); return; }
        _WaveCompleted[Index]=1;
        if (_WaveBurst[Index]==1)
        {
            int32 BatchIndex=_BurstTargets.FindIndex(H);
            if (BatchIndex<0 || BatchIndex>=_BurstCompletedForTarget.Num() ||
                _BurstCompletedForTarget[BatchIndex]!=0)
            { Request_Fail("ObjectPooling burst completed target ledger rejected"); return; }
            _BurstCompletedForTarget[BatchIndex]=1; ++_BurstCompleted;
        }
        MaybeDrainWave(Index);
    }

    private bool CaptureRoot(FCk_Handle Root, FCk_Handle Parent)
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root)) { return true; }
        if (Root==ck::TransientEntity() || !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Parent)) { return false; }
        TArray<FCk_Handle> Queue; TArray<FCk_Handle> Parents;
        Queue.Add(Root); Parents.Add(Parent);
        for (int32 i=0;i<Queue.Num();++i)
        {
            auto H=Queue[i]; auto Expected=Parents[i];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,ck::TransientEntity(),Children))
            { return false; }
            // The allocated census verifies the pending-kill-safe lifetime-owner
            // fragment before the native Get_LifetimeOwner dereference below.
            if (utils_entity_lifetime::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill)!=Expected)
            { return false; }
            int32 Known=_Retiring.FindIndex(H);
            if (Known>=0) { if (Known>=_RetiringParents.Num() || _RetiringParents[Known]!=Expected) { return false; } }
            else
            {
                if (_RetireFrozen) { return false; }
                _Retiring.Add(H); _RetiringParents.Add(Expected);
            }
            for (auto Child : Children)
            { if (!Queue.Contains(Child)) { Queue.Add(Child); Parents.Add(H); } }
        }
        return true;
    }
    // Diagnostic only: the first rejected predicate is retained without changing
    // the fixed public failure reason or the admission/retirement decisions.
    private void NoteRestartCensusReject(FString Code,int32 Index,int32 StationNow,int32 Admission)
    {
        if (!_LastRestartAdmissionReject.IsEmpty()) { return; }
        _LastRestartAdmissionReject=f"{Code} index={Index} baseline={_StationRetainedClosure.Num()} stationNow={StationNow} admission={Admission}";
    }
    private bool TryCaptureStationDescendants(TArray<FCk_Handle>& Out)
    {
        Out.Empty();
        if (ServerStations.Num()!=2)
        { NoteRestartCensusReject("station-count",-1,0,0); return false; }
        if (!Get_HasExactStations())
        { NoteRestartCensusReject("station-identity",-1,0,0); return false; }
        auto World=ck::TransientEntity();
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(World))
        { NoteRestartCensusReject("world-unallocated",-1,0,0); return false; }
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(ServerStations[0]))
        { NoteRestartCensusReject("station0-unallocated",-1,0,0); return false; }
        TArray<FCk_Handle> StationChildren;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                ServerStations[0],World,StationChildren))
        { NoteRestartCensusReject("station0-children-query",-1,0,0); return false; }
        TArray<FCk_Handle> Queue; TArray<FCk_Handle> Parents;
        for (auto Child : StationChildren)
        { Queue.Add(Child); Parents.Add(ServerStations[0]); }
        for (int32 i=0;i<Queue.Num();++i)
        {
            auto H=Queue[i]; auto Expected=Parents[i];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H))
            { NoteRestartCensusReject("station-descendant-unallocated",i,Out.Num(),0); Out.Empty(); return false; }
            if (Out.Contains(H))
            { NoteRestartCensusReject("station-descendant-duplicate",i,Out.Num(),0); Out.Empty(); return false; }
            if (utils_entity_lifetime::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill)!=Expected)
            { NoteRestartCensusReject("station-descendant-parent",i,Out.Num(),0); Out.Empty(); return false; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,World,Children))
            { NoteRestartCensusReject("station-descendant-children-query",i,Out.Num(),0); Out.Empty(); return false; }
            Out.Add(H);
            for (auto Child : Children)
            { Queue.Add(Child); Parents.Add(H); }
        }
        if (Out.IsEmpty())
        { NoteRestartCensusReject("station-descendants-empty",-1,0,0); return false; }
        return true;
    }
    private bool TryCaptureRestartAdmission(TArray<FCk_Handle>& Out)
    {
        Out.Empty();
        TArray<FCk_Handle> StationNow;
        if (!TryCaptureStationDescendants(StationNow)) { return false; }
        if (_StationRetainedClosure.IsEmpty())
        { NoteRestartCensusReject("baseline-empty",-1,StationNow.Num(),0); return false; }
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(_HerdOwner))
        { NoteRestartCensusReject("herd-owner-unallocated",-1,StationNow.Num(),0); return false; }
        for (int32 i=0;i<_StationRetainedClosure.Num();++i)
        { if (!StationNow.Contains(_StationRetainedClosure[i]))
          {
              NoteRestartCensusReject("baseline-entry-missing",i,StationNow.Num(),0);
              auto Missing=_StationRetainedClosure[i];
              bool AllocatedNow=UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Missing);
              FString Captured=i<_StationRetainedDescriptors.Num()
                  ? _StationRetainedDescriptors[i] : "captured-descriptor-unavailable";
              _LastRestartAdmissionReject+=f" captured=[{Captured}] allocatedNow={AllocatedNow}";
              // A stale allocated handle is not safe for an ordinary owner getter.
              if (AllocatedNow && ck::IsValid(Missing) &&
                  utils_entity_lifetime::Has_LifetimeOwner(Missing))
              {
                  auto CurrentParent=utils_entity_lifetime::Get_LifetimeOwner(
                      Missing,ECk_PendingKill_Policy::IncludePendingKill);
                  bool SameCaptured=i<_StationRetainedParents.Num() &&
                      CurrentParent==_StationRetainedParents[i];
                  if (ck::IsValid(CurrentParent))
                  {
                      FString CurrentParentText=utils_handle::Conv_HandleToString(CurrentParent);
                      FString CurrentParentName=utils_handle::Get_DebugName(CurrentParent).ToString();
                      _LastRestartAdmissionReject+=f" currentParent={CurrentParentText} currentParentName={CurrentParentName} sameCaptured={SameCaptured}";
                  }
                  else { _LastRestartAdmissionReject+=" currentParent-invalid"; }
              }
              else { _LastRestartAdmissionReject+=" currentParent-unavailable"; }
              return false;
          } }
        for (auto H : StationNow)
        { if (!_StationRetainedClosure.Contains(H)) { Out.Add(H); } }
        auto World=ck::TransientEntity();
        TArray<FCk_Handle> Queue; TArray<FCk_Handle> Parents;
        Queue.Add(_HerdOwner); Parents.Add(World);
        for (int32 i=0;i<Queue.Num();++i)
        {
            auto H=Queue[i]; auto Expected=Parents[i];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H))
            { NoteRestartCensusReject("herd-descendant-unallocated",i,StationNow.Num(),Out.Num()); Out.Empty(); return false; }
            if (Out.Contains(H))
            { NoteRestartCensusReject("herd-descendant-duplicate",i,StationNow.Num(),Out.Num()); Out.Empty(); return false; }
            if (utils_entity_lifetime::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill)!=Expected)
            { NoteRestartCensusReject("herd-descendant-parent",i,StationNow.Num(),Out.Num()); Out.Empty(); return false; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H,World,Children))
            { NoteRestartCensusReject("herd-descendant-children-query",i,StationNow.Num(),Out.Num()); Out.Empty(); return false; }
            Out.Add(H);
            for (auto Child : Children)
            { Queue.Add(Child); Parents.Add(H); }
        }
        if (Out.Num()<CkPoolingShared::HerdSize+1)
        { NoteRestartCensusReject("admission-too-small",-1,StationNow.Num(),Out.Num()); Out.Empty(); return false; }
        return true;
    }
    private bool Get_HasExactRestartAdmissionLedger()
    {
        if (!_LastRestartAdmissionAudit.Captured || _Action!=1 ||
            _LastRestartAdmissionAudit.RequestSerial!=_RequestSerial ||
            _LastRestartAdmissionAudit.OriginGeneration!=_RequestGeneration ||
            _LastRestartAdmissionAudit.Owner!=_Requester ||
            _LastRestartAdmissionAudit.Allocated.Num()!=_Retiring.Num()) { return false; }
        TArray<FCk_Handle> Seen;
        for (auto H : _LastRestartAdmissionAudit.Allocated)
        { if (Seen.Contains(H)) { return false; } Seen.Add(H); }
        Seen.Empty();
        for (auto H : _Retiring)
        { if (Seen.Contains(H)) { return false; } Seen.Add(H); }
        return UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
            _LastRestartAdmissionAudit.Allocated,_Retiring,ck::TransientEntity());
    }
    private bool CaptureSimulation(bool IncludeTick)
    {
        if (_Retiring.Num()!=_RetiringParents.Num() || _SteadyRetiring.Num()!=_SteadyRetiringParents.Num() ||
            _HerdRetiring.Num()!=_HerdRetiringParents.Num())
        { return false; }
        auto World=ck::TransientEntity();
        auto WaveParent=ServerStations.Num()==2 ? ServerStations[0] : FCk_Handle();
        if (!CaptureRoot(_HerdOwner,World)) { return false; }
        if (IncludeTick && !CaptureRoot(_TickOwner,World)) { return false; }
        for (auto H : _InFlight) { if (!CaptureRoot(H,WaveParent)) { return false; } }
        for (auto P : _WavePending)
        { if (!CaptureRoot(P.Get_EntityUnderConstruction(),WaveParent)) { return false; } }
        for (int32 i=0;i<_SteadyRetiring.Num();++i)
        { if (!CaptureRoot(_SteadyRetiring[i],_SteadyRetiringParents[i])) { return false; } }
        for (int32 i=0;i<_HerdRetiring.Num();++i)
        { if (!CaptureRoot(_HerdRetiring[i],_HerdRetiringParents[i])) { return false; } }
        for (auto H : _SmHerd) { if (!CaptureRoot(H,_HerdOwner)) { return false; } }
        return true;
    }
    private bool Get_HasSafeRetirementLedger()
    {
        if (_Retiring.Num()!=_RetiringParents.Num()) { return false; }
        // Prove the entire frozen read-set before dereferencing any parent.
        // This native pass validates each allocated handle and its registry.
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                _Retiring,_Retiring,ck::TransientEntity())) { return false; }
        for (int32 i=0;i<_Retiring.Num();++i)
        {
            auto H=_Retiring[i];
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { continue; }
            // The native frozen-ledger proof above validates every allocated
            // handle's complete current and stale dependent read-sets.
            if (utils_entity_lifetime::Get_LifetimeOwner(H,ECk_PendingKill_Policy::IncludePendingKill)!=_RetiringParents[i])
            { return false; }
        }
        return true;
    }
    private bool DestroyCaptured()
    {
        if (!Get_HasSafeRetirementLedger()) { return false; }
        _RetireFrozen=true;
        // Both the allocated census and native destroy read-set must be members
        // already pending before a parent can recurse into its children.
        for (;;)
        {
            bool HasLive=false, Progress=false;
            for (auto Old : _Retiring)
            {
                if (!CkPoolingShared::IsLive(Old)) { continue; }
                HasLive=true;
                // The entry proof or the immediately preceding destroy's
                // post-proof is current in this synchronous destruction call.
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Old,ck::TransientEntity(),Children))
                { return false; }
                bool ChildrenPending=true;
                for (auto Child : Children)
                { if (!_Retiring.Contains(Child)) { return false; }
                  if (CkPoolingShared::IsLive(Child)) { ChildrenPending=false; } }
                for (auto Child : utils_entity_lifetime::Get_LifetimeDependents(Old))
                { if (!_Retiring.Contains(Child)) { return false; }
                  if (CkPoolingShared::IsLive(Child)) { ChildrenPending=false; } }
                if (!ChildrenPending) { continue; }
                utils_entity_lifetime::Request_DestroyEntity(Old);
                if (CkPoolingShared::IsLive(Old) || !Get_HasSafeRetirementLedger()) { return false; }
                Progress=true;
            }
            if (!HasLive) { return true; }
            if (!Progress) { return false; }
        }
        return false; // AngelScript requires an explicit fallback after the loop.
    }
    private bool PollRetired()
    {
        if (_RetirementUnproven || !Get_HasSafeRetirementLedger())
        { _RetirementUnproven=true; return false; }
        for (auto H : _Retiring)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } }
        for (auto P : _WavePending)
        { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(P.Get_EntityUnderConstruction())) { return false; } }
        return true;
    }
    private bool ReserveTickForRestart()
    {
        if (_Retiring.Num()!=0 || _RetiringParents.Num()!=0 || _RetireFrozen ||
            _ReservedTickRetiring.Num()!=0 || _ReservedTickParents.Num()!=0) { return false; }
        if (!CaptureRoot(_TickOwner,ck::TransientEntity()) || !Get_HasSafeRetirementLedger()) { return false; }
        _ReservedTickRetiring=_Retiring; _ReservedTickParents=_RetiringParents;
        _Retiring.Empty(); _RetiringParents.Empty();
        return _ReservedTickRetiring.Num()>0;
    }
    private bool RetirePreCapturedTickAfterFailedRestart()
    {
        if (_ReservedTickRetiring.Num()==0 || _ReservedTickRetiring.Num()!=_ReservedTickParents.Num())
        { return false; }
        // The active episode must still be safe. The tick ledger was captured
        // before R's first destroy; neither episode may discover new members.
        if (_Retiring.Num()>0 && (!Get_HasSafeRetirementLedger() || !DestroyCaptured())) { return false; }
        auto Old=_Retiring; auto OldParents=_RetiringParents; bool OldFrozen=_RetireFrozen;
        _Retiring=_ReservedTickRetiring; _RetiringParents=_ReservedTickParents; _RetireFrozen=true;
        bool Retired=Get_HasSafeRetirementLedger() && DestroyCaptured();
        _Retiring=Old; _RetiringParents=OldParents; _RetireFrozen=OldFrozen;
        return Retired;
    }
    private void BeginRetireSimulation(bool IncludeTick)
    {
        _RetiringSimulation=true; Snapshot.Ready=false; ForceNetUpdate();
        if (!CaptureSimulation(IncludeTick) ||
            (_Action==1 && !Get_HasExactRestartAdmissionLedger()) || !DestroyCaptured())
        { _RetirementUnproven=true; Request_Fail("ObjectPooling allocated simulation closure unproven"); }
    }
    private void StopKnownBehavior()
    {
        // Do not refresh a frozen retirement census after a destruction callback.
        // Its new children are untrusted; stop only handles this source retained.
        if (ck::IsValid(_TickTimer) && CkPoolingShared::IsLive(FCk_Handle(_TickTimer)) &&
            utils_entity_lifetime::Has_LifetimeOwner(FCk_Handle(_TickTimer)) &&
            utils_entity_lifetime::Get_LifetimeOwner(FCk_Handle(_TickTimer))==_TickOwner)
        { utils_timer::Request_Stop(_TickTimer); }
        for (auto Sm : _SmMachines)
        { if (ck::IsValid(Sm) && CkPoolingShared::IsLive(FCk_Handle(Sm)))
          { utils_state_machine::Request_Stop(Sm); } }
    }

    private void DoChurnScriptWaves()
    {
        while (_InFlight.Num() > CkPoolingShared::WaveSize*CkPoolingShared::WaveDepth)
        {
            auto Oldest=_InFlight[0];
            _InFlight.RemoveAt(0);
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Oldest) &&
                !_SteadyRetiring.Contains(Oldest))
            { _SteadyRetiring.Add(Oldest); _SteadyRetiringParents.Add(ServerStations[0]); }
            if (!CkPoolingShared::IsLive(Oldest)) { continue; }
            _Retiring.Empty(); _RetiringParents.Empty(); _RetireFrozen=false;
            if (!CaptureRoot(Oldest,ServerStations[0]) || !DestroyCaptured())
            { Request_Fail("ObjectPooling steady wave retirement unproven"); return; }
            for (int32 i=0;i<_Retiring.Num();++i)
            {
                int32 Known=_SteadyRetiring.FindIndex(_Retiring[i]);
                if (Known>=0)
                { if (Known>=_SteadyRetiringParents.Num() || _SteadyRetiringParents[Known]!=_RetiringParents[i])
                  { Request_Fail("ObjectPooling steady retirement parent mismatch"); return; } }
                else { _SteadyRetiring.Add(_Retiring[i]); _SteadyRetiringParents.Add(_RetiringParents[i]); }
            }
            _Retiring.Empty(); _RetiringParents.Empty(); ++_TotalDestroyed;
            _RetireFrozen=false;
        }
        if (!TrySpawnWave(CkPoolingShared::WaveSize,false))
        { Request_Fail("ObjectPooling regular wave spawn rejected"); }
    }
    private void DoChurnSmHerd(float DeltaSeconds)
    {
        _SmRespawnAccum += DeltaSeconds;
        if (_SmRespawnAccum < CkPoolingShared::RespawnPeriod) { return; }
        _SmRespawnAccum=0.0f;
        for (int32 Count=0;Count<CkPoolingShared::RespawnSlice;++Count)
        {
            int32 Index=_SmNextRespawnIndex % Math::Max(_SmHerd.Num(),1);
            if (Index<0 || Index>=_SmHerd.Num()) { Request_Fail("ObjectPooling herd index invalid"); return; }
            auto Old=_SmHerd[Index];
            if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Old) &&
                !_HerdRetiring.Contains(Old))
            { _HerdRetiring.Add(Old); _HerdRetiringParents.Add(_HerdOwner); }
            _Retiring.Empty(); _RetiringParents.Empty(); _RetireFrozen=false;
            if (CkPoolingShared::IsLive(Old) && (!CaptureRoot(Old,_HerdOwner) || !DestroyCaptured()))
            { Request_Fail("ObjectPooling herd retirement unproven"); return; }
            for (int32 i=0;i<_Retiring.Num();++i)
            {
                int32 Known=_HerdRetiring.FindIndex(_Retiring[i]);
                if (Known>=0)
                { if (Known>=_HerdRetiringParents.Num() || _HerdRetiringParents[Known]!=_RetiringParents[i])
                  { Request_Fail("ObjectPooling herd retirement parent mismatch"); return; } }
                else { _HerdRetiring.Add(_Retiring[i]); _HerdRetiringParents.Add(_RetiringParents[i]); }
            }
            _Retiring.Empty(); _RetiringParents.Empty(); _RetireFrozen=false;
            auto NewMember=utils_entity_lifetime::Request_CreateEntity(_HerdOwner);
            if (!CkPoolingShared::IsLive(NewMember)) { Request_Fail("ObjectPooling herd replacement rejected"); return; }
            NewMember.Set_DebugName(n"ObjectPooling.SmHerdMember");
            _SmHerd[Index]=NewMember;
            auto Sm=UCk_Utils_StateMachine_UE::Add(NewMember,FCk_StateMachine_Spec(UCk_PoolSmGym_State_Ping));
            if (!ck::IsValid(Sm)) { Request_Fail("ObjectPooling herd state machine rejected"); return; }
            _SmMachines.Add(Sm);
            ++_SmNextRespawnIndex;
        }
    }
    UFUNCTION() private void OnGymTick(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time DeltaT)
    {
        if (_Ending || _Starting || _RetiringSimulation || Snapshot.Failed || _RequestSerial!=0 || !Get_IsReady()) { return; }
        if (_SteadyRetiring.Num()!=_SteadyRetiringParents.Num() || _HerdRetiring.Num()!=_HerdRetiringParents.Num())
        { Request_Fail("ObjectPooling steady retirement ledger invalid"); return; }
        for (int32 i=_SteadyRetiring.Num()-1;i>=0;--i)
        { if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(_SteadyRetiring[i]))
          { _SteadyRetiring.RemoveAt(i); _SteadyRetiringParents.RemoveAt(i); } }
        for (int32 i=_HerdRetiring.Num()-1;i>=0;--i)
        { if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(_HerdRetiring[i]))
          { _HerdRetiring.RemoveAt(i); _HerdRetiringParents.RemoveAt(i); } }
        float DeltaSeconds=float(DeltaT.Get_Seconds());
        DoChurnScriptWaves();
        if (Snapshot.Failed) { return; }
        DoChurnSmHerd(DeltaSeconds);
        if (Snapshot.Failed) { return; }
        _DisplayAccum += DeltaSeconds;
        if (_DisplayAccum >= CkPoolingShared::DisplayPeriod)
        { _DisplayAccum=0.0f; if (!Publish()) { Request_Fail("ObjectPooling authority stats publish rejected"); } }
    }

    private bool Publish()
    {
        if (!Get_HasExactStations() || _SmHerd.Num()!=CkPoolingShared::HerdSize ||
            Snapshot.Sequence>=2147483646 || _Generation<=0) { return false; }
        FCkPoolingGym_Snapshot S;
        S.CatalogVersion=CkPoolingShared::CatalogVersion; S.Generation=_Generation;
        S.Sequence=Snapshot.Sequence+1; S.Ready=true;
        S.TotalSpawned=_TotalSpawned; S.TotalDestroyed=_TotalDestroyed; S.HerdSize=_SmHerd.Num();
        S.Script=CkPoolingShared::Sample(utils_object::Get_ObjectPoolStats(this,UCk_ObjectPoolingTest_PoolableScript,nullptr));
        S.Ping=CkPoolingShared::Sample(utils_object::Get_ObjectPoolStats(this,UCk_PoolSmGym_State_Ping,nullptr));
        S.Pong=CkPoolingShared::Sample(utils_object::Get_ObjectPoolStats(this,UCk_PoolSmGym_State_Pong,nullptr));
        S.Condition=CkPoolingShared::Sample(utils_object::Get_ObjectPoolStats(this,UCk_PoolSmTest_Condition_ShortDelay,nullptr));
        for (int32 i=0;i<2;++i) { S.Stations.Add(CkPoolingShared::Get_Params(i)); S.Titles.Add(CkPoolingShared::Get_Title(i)); }
        S.Bodies.Add(f"Waves: {CkPoolingShared::WaveSize}/tick, depth {CkPoolingShared::WaveDepth} (~{CkPoolingShared::WaveSize*CkPoolingShared::WaveDepth} in flight)\nSpawned {_TotalSpawned}   Destroyed {_TotalDestroyed}\n\nPOOL: {CkPoolingShared::Format(S.Script)}\n\nHEALTHY = live plateaus, hits climbs, misses freezes");
        S.Bodies.Add(f"Herd {_SmHerd.Num()} SMs, {CkPoolingShared::RespawnSlice} respawned / {CkPoolingShared::RespawnPeriod}s\n\nPING STATE: {CkPoolingShared::Format(S.Ping)}\nPONG STATE: {CkPoolingShared::Format(S.Pong)}\nCONDITION:  {CkPoolingShared::Format(S.Condition)}\n\nHEALTHY = all three recycle (hits climb), no ensures in log");
        if (!Get_IsCompleteSnapshot(S)) { return false; }
        for (int32 i=0;i<2;++i)
        {
            auto& Text=ServerStations[i].AddOrGet_Fragment(FCkGym_Station_TitleAndDescription);
            Text.Title=FText::FromString(S.Titles[i]); Text.Description=FText::FromString(S.Bodies[i]);
        }
        Snapshot=S; ForceNetUpdate(); return true;
    }

    bool Request_Control(ACk_ObjectPoolingGym_PlayerController Owner, ACk_Gym_Base_Pawn Pawn,
        int32 Serial, int32 Generation, int32 Action)
    {
        if (!Get_IsReady() || !Get_IsValidOwner(Owner,Pawn) || _RequestSerial!=0 ||
            Serial<=0 || Generation!=_Generation || (Action!=0 && Action!=1) ||
            !Owner.Get_HasReservedPoolingControl(Serial,Generation,Action,Pawn) ||
            !Owner.Get_CanAcceptOwnerCommit()) { return false; }
        _Requester=Owner; _RequestPawn=Pawn; _RequestSerial=Serial; _RequestGeneration=Generation;
        _Action=Action; _BusyStartedAt=float(System::GetGameTimeInSeconds());
        _BurstTargets.Empty(); _BurstConstructedForTarget.Empty(); _BurstCompletedForTarget.Empty();
        _BurstIssued=0; _BurstConstructed=0; _BurstCompleted=0;
        if (Action==0)
        { if (!TrySpawnWave(100,true)) { Request_Fail("ObjectPooling burst request rejected"); return false; } }
        else
        {
            // Freeze a graph-derived census before the retirement ledger or destruction.
            TArray<FCk_Handle> Admission;
            _LastRestartAdmissionReject="";
            if (!TryCaptureRestartAdmission(Admission))
            { Request_Fail("ObjectPooling restart admission census rejected"); return false; }
            _RestartSmBefore=_SmRespawnAccum; _RestartDisplayBefore=_DisplayAccum;
            if (!ReserveTickForRestart()) { Request_Fail("ObjectPooling restart tick reservation rejected"); return false; }
            FCkPoolingGym_RestartAdmissionAudit Audit;
            Audit.Captured=true; Audit.RequestSerial=Serial; Audit.OriginGeneration=Generation;
            Audit.Owner=Owner; Audit.Allocated=Admission;
            _LastRestartAdmissionAudit=Audit;
            BeginRetireSimulation(false);
        }
        return !Snapshot.Failed;
    }
    bool Request_TryGetPreparedPoolingCommit(ACk_ObjectPoolingGym_PlayerController Owner,
        int32 Serial, FCkPoolingGym_ControlCommit& Out)
    {
        Out=FCkPoolingGym_ControlCommit();
        if (ck::Is_NOT_Valid(Owner) || Owner!=_Requester || Serial<=0 || Serial!=_RequestSerial ||
            _Prepared.RequestSerial!=Serial || _Prepared.Owner!=Owner ||
            !Get_IsCompleteSnapshot(_Prepared.Snapshot)) { return false; }
        Out=_Prepared; return true;
    }
    bool Request_TryGetPreparedBurstEvidence(ACk_ObjectPoolingGym_PlayerController Owner,
        int32 Serial,TArray<FCk_Handle>& Out,TArray<int32>& OutConstructed,TArray<int32>& OutCompleted)
    {
        Out.Empty(); OutConstructed.Empty(); OutCompleted.Empty();
        if (ck::Is_NOT_Valid(Owner) || Owner!=_Requester || Serial<=0 || Serial!=_RequestSerial ||
            _Prepared.RequestSerial!=Serial || _Prepared.Action!=0 ||
            _BurstIssued!=100 || _BurstConstructed!=100 || _BurstCompleted!=100 ||
            _BurstTargets.Num()!=100 || _BurstConstructedForTarget.Num()!=100 ||
            _BurstCompletedForTarget.Num()!=100) { return false; }
        for (int32 i=0;i<100;++i)
        {
            auto H=_BurstTargets[i];
            if (Out.Contains(H) || _BurstConstructedForTarget[i]!=1 || _BurstCompletedForTarget[i]!=1)
            { Out.Empty(); OutConstructed.Empty(); OutCompleted.Empty(); return false; }
            Out.Add(H); OutConstructed.Add(_BurstConstructedForTarget[i]);
            OutCompleted.Add(_BurstCompletedForTarget[i]);
        }
        return true;
    }
    private void FinishControl()
    {
        if (_RequestSerial<=0 || _Action<0 || _Action>1 || !Get_HasExactStations() ||
            !Get_HasCapturedOwnerContext(_Requester,_RequestPawn) ||
            !_Requester.Get_HasReservedPoolingControl(_RequestSerial,_RequestGeneration,_Action,_RequestPawn))
        { Request_Fail("ObjectPooling owner context lost before commit"); return; }
        if (!Publish()) { Request_Fail("ObjectPooling control outcome publish rejected"); return; }
        FCkPoolingGym_ControlCommit Commit;
        Commit.RequestSerial=_RequestSerial; Commit.OriginGeneration=_RequestGeneration; Commit.Action=_Action;
        if (_Action==0)
        {
            if (_BurstIssued!=100 || _BurstConstructed!=100 || _BurstCompleted!=100 ||
                _BurstTargets.Num()!=100 || _BurstConstructedForTarget.Num()!=100 ||
                _BurstCompletedForTarget.Num()!=100) { Request_Fail("ObjectPooling burst result incomplete"); return; }
            Commit.BurstIssued=_BurstIssued; Commit.BurstConstructed=_BurstConstructed;
            Commit.BurstCompleted=_BurstCompleted;
        }
        Commit.DeliverySerial=_Requester.Get_NextDeliverySerial(); Commit.OriginState=this;
        Commit.Owner=_Requester; Commit.OwnerPawn=_RequestPawn; Commit.Snapshot=Snapshot;
        if (Commit.DeliverySerial<=0) { Request_Fail("ObjectPooling owner delivery serial exhausted"); return; }
        _Prepared=Commit;
        if (!_Requester.Request_TryRecordPoolingCommit(this,_RequestSerial))
        { Request_Fail("ObjectPooling owner whole commit rejected"); return; }
        _Prepared=FCkPoolingGym_ControlCommit();
        _BurstTargets.Empty(); _BurstConstructedForTarget.Empty(); _BurstCompletedForTarget.Empty();
        _BurstIssued=0; _BurstConstructed=0; _BurstCompleted=0;
        _Requester=nullptr; _RequestPawn=nullptr; _RequestSerial=0; _RequestGeneration=0; _Action=-1;
    }
    UFUNCTION() private void OnMonitor()
    {
        if (_Ending || Snapshot.Failed) { return; }
        auto Now=float(System::GetGameTimeInSeconds());
        if (_Starting)
        {
            if (Now-_StartedAt>20.0f) { Request_Fail("ObjectPooling shared station readiness timed out"); return; }
            auto Poll=_Cohort.PollConstruction();
            if (Poll==ECkGym_SharedStationPoll::Failed) { Request_Fail("ObjectPooling shared station cohort failed"); return; }
            if (Poll!=ECkGym_SharedStationPoll::Ready) { return; }
            ServerStations=_Cohort.Get_Stations();
            if (ServerStations.Num()!=2 || _StartupStationTargets.Num()!=2 ||
                _StartupStationCarriers.Num()!=2 || !CkPoolingShared::IsLive(_StartupStationOwner))
            { Request_Fail("ObjectPooling station request carrier barrier rejected"); return; }
            bool CarrierPending=false;
            for (int32 i=0;i<2;++i)
            {
                if (ServerStations[i]!=_StartupStationTargets[i])
                { Request_Fail("ObjectPooling station request carrier barrier rejected"); return; }
                bool Allocated=false;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrier(
                        _StartupStationCarriers[i],_StartupStationTargets[i],
                        _StartupStationOwner,ck::TransientEntity(),Allocated))
                { Request_Fail("ObjectPooling station request carrier barrier rejected"); return; }
                if (Allocated) { CarrierPending=true; }
            }
            if (CarrierPending) { return; }
            TArray<FCk_Handle> StationRetained;
            if (!Get_HasExactStations() || !TryCaptureStationDescendants(StationRetained))
            { Request_Fail("ObjectPooling station retained closure unproven"); return; }
            // Startup only: restart admission must retain allocated pending/teardown
            // identities, but the first persistent station baseline must be live.
            for (auto H : StationRetained)
            {
                if (!CkPoolingShared::IsLive(H) || !utils_entity_lifetime::Has_LifetimeOwner(H))
                { Request_Fail("ObjectPooling station startup retained closure nonlive"); return; }
            }
            _StationRetainedClosure=StationRetained;
            // Capture diagnostic identity while these handles are still valid.
            // This ledger never gates admission or changes retirement membership.
            _StationRetainedParents.Empty(); _StationRetainedDescriptors.Empty();
            for (auto H : _StationRetainedClosure)
            {
                FCk_Handle Parent;
                FString Descriptor="captured-identity-unavailable";
                if (ck::IsValid(H) && utils_entity_lifetime::Has_LifetimeOwner(H))
                {
                    Parent=utils_entity_lifetime::Get_LifetimeOwner(H);
                    FString ParentName="parent-name-unavailable";
                    if (ck::IsValid(Parent))
                    { ParentName=utils_handle::Get_DebugName(Parent).ToString(); }
                    FString HandleText=utils_handle::Conv_HandleToString(H);
                    FString HandleName=utils_handle::Get_DebugName(H).ToString();
                    FString ParentText=utils_handle::Conv_HandleToString(Parent);
                    Descriptor=f"entity={HandleText} name={HandleName} parent={ParentText} parentName={ParentName}";
                }
                _StationRetainedParents.Add(Parent);
                _StationRetainedDescriptors.Add(Descriptor);
            }
            if (!StartSimulation() || !Publish())
            { Request_Fail("ObjectPooling initial simulation rejected"); return; }
            _Starting=false; return;
        }
        if (_RequestSerial>0)
        {
            if (Now-_BusyStartedAt>20.0f) { Request_Fail("ObjectPooling control deadline expired"); return; }
            if (!Get_HasCapturedOwnerContext(_Requester,_RequestPawn))
            { Request_Fail("ObjectPooling requesting owner context lost"); return; }
            if (_Action==0)
            {
                if (_BurstConstructed>100 || _BurstCompleted>100)
                { Request_Fail("ObjectPooling burst callback count invalid"); return; }
                if (_BurstConstructed==100 && _BurstCompleted==100) { FinishControl(); }
                return;
            }
            if (_Action==1)
            {
                if (_RetirementUnproven) { Request_Fail("ObjectPooling old simulation retirement unproven"); return; }
                if (!PollRetired()) { return; }
                _LastRestartRetired=_Retiring;
                LastRestartOriginGeneration=_RequestGeneration;
                LastRestartRequestSerial=_RequestSerial;
                LastRestartOwner=_Requester;
                _Retiring.Empty(); _RetiringParents.Empty(); _RetireFrozen=false;
                _SteadyRetiring.Empty(); _SteadyRetiringParents.Empty();
                _HerdRetiring.Empty(); _HerdRetiringParents.Empty();
                _InFlight.Empty(); _WavePending.Empty(); _WaveGeneration.Empty();
                _WaveConstructed.Empty(); _WaveCompleted.Empty(); _WaveBurst.Empty(); _SmHerd.Empty();
                _SmMachines.Empty();
                _HerdOwner=FCk_Handle(); _SmNextRespawnIndex=0; _TotalSpawned=0; _TotalDestroyed=0;
                if (_Generation>=2147483646) { Request_Fail("ObjectPooling generation exhausted"); return; }
                ++_Generation; _RetiringSimulation=false;
                if (!StartSimulation()) { Request_Fail("ObjectPooling restarted herd rejected"); return; }
                auto CadenceOwner=_Requester; int32 CadenceSerial=_RequestSerial;
                int32 CadenceOrigin=_RequestGeneration;
                float SmAfter=_SmRespawnAccum, DisplayAfter=_DisplayAccum;
                FinishControl();
                if (!Snapshot.Failed && _RequestSerial==0)
                {
                    FCkPoolingGym_RestartCadence C;
                    C.Complete=true; C.RequestSerial=CadenceSerial; C.OriginGeneration=CadenceOrigin;
                    C.ResultGeneration=_Generation; C.Owner=CadenceOwner;
                    C.SmBefore=_RestartSmBefore; C.SmAfter=SmAfter;
                    C.DisplayBefore=_RestartDisplayBefore; C.DisplayAfter=DisplayAfter;
                    _LastRestartCadence=C;
                }
                if (!Snapshot.Failed) { _ReservedTickRetiring.Empty(); _ReservedTickParents.Empty(); }
                return;
            }
            Request_Fail("ObjectPooling action invalid"); return;
        }
        if (!Get_IsReady()) { Request_Fail("ObjectPooling authority simulation lost"); }
    }

    private bool Get_IsWorldRetirementBlocked()
    {
        return _RetirementUnproven || _Cohort.Get_HasTerminalFailure() ||
            _Cohort.Get_IsRetirementUnproven() ||
            CkGym_Admission::Get_IsClosedUnproven(ck::TransientEntity());
    }
    private void Request_Fail(FString Reason)
    {
        if (Snapshot.Failed) { return; }
        Snapshot.Ready=false; Snapshot.Failed=true; Snapshot.FailureReason=Reason;
        ForceNetUpdate();
        _RetiringSimulation=true;
        StopKnownBehavior();
        // An already closed world or opaque cohort grants no simulation
        // destruction. Quiesce only the retained producers above.
        if (Get_IsWorldRetirementBlocked())
        {
            _RetirementUnproven=true;
            if (ck::IsValid(_Requester) && _RequestSerial>0)
            { _Requester.Request_RejectPoolingControl(_RequestSerial,2); }
            ck::Error("[ObjectPoolingGym] " + Reason); return;
        }
        if (_ReservedTickRetiring.Num()>0)
        {
            if ((!_RetireFrozen && !CaptureSimulation(false)) ||
                !RetirePreCapturedTickAfterFailedRestart()) { _RetirementUnproven=true; }
        }
        else if (_RetireFrozen) { _RetirementUnproven=true; }
        else if (!CaptureSimulation(true) || !DestroyCaptured()) { _RetirementUnproven=true; }
        if (!_RetirementUnproven) { _Cohort.Request_Retire(); }
        if (ck::IsValid(_Requester) && _RequestSerial>0)
        { _Requester.Request_RejectPoolingControl(_RequestSerial,2); }
        ck::Error("[ObjectPoolingGym] " + Reason);
    }
    UFUNCTION(BlueprintOverride) void EndPlay(EEndPlayReason Reason)
    {
        _Ending=true; Snapshot.Ready=false;
        StopKnownBehavior();
        if (Get_IsWorldRetirementBlocked()) { _RetirementUnproven=true; return; }
        if (_ReservedTickRetiring.Num()>0)
        {
            if ((!_RetireFrozen && !CaptureSimulation(false)) ||
                !RetirePreCapturedTickAfterFailedRestart()) { _RetirementUnproven=true; }
        }
        else if (_RetireFrozen) { _RetirementUnproven=true; }
        else if (!CaptureSimulation(true) || !DestroyCaptured()) { _RetirementUnproven=true; }
        if (!_RetirementUnproven) { _Cohort.Request_Retire(); }
    }
}
