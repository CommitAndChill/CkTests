// Language=angelscript

// One authority-owned baked volume and two fixed-mode crowd agents.
// GameState owns the floor, wall, Recast admission and published wire frame.
class UCk_EntityScript_VoxelNavGym_WorldRoot : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::Replicates;
}

USTRUCT()
struct FVoxelNavGym_AgentSpawnParams
{
    UPROPERTY() int32 Index = -1;
    UPROPERTY() int64 PairGeneration = 0;
    UPROPERTY() FTransform InitialTransform;
}

class UCk_EntityScript_VoxelNavGym_Agent : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;
    UPROPERTY(ExposeOnSpawn) int32 Index = -1;
    UPROPERTY(ExposeOnSpawn) int64 PairGeneration = 0;
    UPROPERTY(ExposeOnSpawn) FTransform InitialTransform;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        if (Index < 0 || Index > 1 || PairGeneration <= 0)
        {
            utils_entity_lifetime::Request_DestroyEntity(InHandle);
            return ECk_EntityScript_ConstructionFlow::Finished;
        }
        utils_transform::Add(InHandle, InitialTransform, ECk_Replication::Replicates);
        utils_entity_tag::Add(InHandle, Index == 0
            ? n"VoxelNavFlyingVsGrounded.Agent.Flying"
            : n"VoxelNavFlyingVsGrounded.Agent.Grounded");
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}

class UCk_EntityScript_VoxelNavGym_FlyingVsGrounded_Source : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;

    private const FVector k_VolumeMin = FVector(-1600.0, -1600.0, -100.0);
    private const FVector k_VolumeMax = FVector(1600.0, 1600.0, 1600.0);
    private const FVector k_FlyerSpawn = FVector(-1000.0, -400.0, 1000.0);
    private const FVector k_FlyerGoal = FVector(1000.0, -400.0, 1000.0);
    private const FVector k_GroundedSpawn = FVector(-1000.0, 400.0, 0.0);
    private const FVector k_GroundedGoal = FVector(1000.0, 400.0, 0.0);
    private const float k_Radius = 42.0f;
    private const float k_Height = 192.0f;

    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCk_Handle _Root;
    private FCk_Handle _VolumeEntity;
    private FCk_Handle_VoxelNavVolume _Volume;
    private TArray<FCk_Handle> _AgentRoots;
    private TArray<FCk_Handle_PendingEntityScript> _PendingAgents;
    private TArray<FCk_Handle_CrowdAgent> _Agents;
    private TArray<FCk_Handle_VoxelNavPath> _Paths;
    private TArray<bool> _IntegratorReady;
    private TArray<FCk_Handle> _RetiringForest;
    private bool _Prepared = false;
    private bool _Built = false;
    private bool _Started = false;
    private bool _Closed = false;
    private bool _BuildRequested = false;
    private bool _GoalsAreOutbound = true;
    private bool _MovesSubmitted = false;
    // 0 none, 1 initial start, 2 pair restart, 3 reverse goals.
    private int32 _PendingOperation = 0;
    private int32 _MoveCorrelation = 0;
    private int64 _PairGeneration = 1;
    private int64 _ControlCount = 0;
    private TArray<int32> _BeforeMoveEpisodes;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        _Root = InHandle;
        _World = GetWorld();
        if (ck::Is_NOT_Valid(_World))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto State = Cast<ACk_VoxelNavGym_FlyingVsGrounded_GameState>(
            _World.GetGameState());
        if (ck::Is_NOT_Valid(State))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        if (!State.HasAuthority())
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _WorldRoot = Ecs.Get_TransientEntity();
        auto ActorRoot = utils_owning_actor::TryGet_ActorEntityHandle(State);
        if (!IsDirectChild(_Root, ActorRoot) || !IsDirectChild(ActorRoot, _WorldRoot))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        auto Params = FCk_VoxelNavVolume_Spec(FBox(k_VolumeMin, k_VolumeMax), 50.0f);
        Params.Set_AutoBuildOnSetup(ECk_EnableDisable::Disable);
        Params.Set_ClearanceUu(k_Radius);
        Params.Set_ChunkPartitioning(ECk_EnableDisable::Disable);
        _Volume = utils_voxel_nav_volume::Add(_Root, Params);
        _VolumeEntity = FCk_Handle(_Volume);
        if (!IsDirectChild(_VolumeEntity, _Root))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _VolumeEntity.Request_OverrideToSelf();
        if (!IsVolumeLive() || !CreatePair())
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _BuildRequested = true;
        if (utils_voxel_nav_volume::Request_Build(_Volume,
                FCk_Request_VoxelNavVolume_Build(),
                FCk_Delegate_Request_OnCompleted(this, n"OnBuildCompleted")) != _Volume)
        { Request_Block(); }
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle) { Request_Block(); }

    private bool IsLive(FCk_Handle Handle) const
    { return ck::IsValid(_World) && UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(Handle, _World); }

    private bool IsDirectChild(FCk_Handle Child, FCk_Handle Parent) const
    {
        return IsLive(Child) && IsLive(Parent) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }

    private bool IsVolumeLive() const
    {
        return IsDirectChild(_VolumeEntity, _Root) &&
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVolume(FCk_Handle(_Volume), _World) &&
            FCk_Handle(_Volume) == _VolumeEntity;
    }

    private bool IsAgentLive(int32 Index) const
    {
        return _AgentRoots.IsValidIndex(Index) && _Agents.IsValidIndex(Index) &&
            _Paths.IsValidIndex(Index) && IsDirectChild(_AgentRoots[Index], _Root) &&
            FCk_Handle(_Agents[Index]) == _AgentRoots[Index] &&
            FCk_Handle(_Paths[Index]) == _AgentRoots[Index] &&
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveCrowdAgent(
                FCk_Handle(_Agents[Index]), _World) &&
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVoxelPath(
                FCk_Handle(_Paths[Index]), _World) &&
            utils_transform::Has(_AgentRoots[Index]) &&
            utils_velocity::Has(_AgentRoots[Index]) &&
            utils_acceleration::Has(_AgentRoots[Index]);
    }

    private bool HasPreparedStructure() const
    {
        auto State = ck::IsValid(_World) ? _World.GetGameState() : nullptr;
        auto ActorRoot = ck::IsValid(State)
            ? utils_owning_actor::TryGet_ActorEntityHandle(State) : FCk_Handle();
        return !_Closed && IsDirectChild(_Root, ActorRoot) &&
            IsDirectChild(ActorRoot, _WorldRoot) && IsVolumeLive() &&
            _AgentRoots.Num() == 2 && _Agents.Num() == 2 &&
            _Paths.Num() == 2 && _IntegratorReady.Num() == 2 &&
            _AgentRoots[0] != _AgentRoots[1] &&
            IsAgentLive(0) && IsAgentLive(1);
    }

    private bool CreateAgent(int32 Index, FVector Spawn)
    {
        if (_Closed || Index < 0 || Index > 1 || !IsVolumeLive()) { return false; }
        FVoxelNavGym_AgentSpawnParams SpawnParams;
        SpawnParams.Index = Index;
        SpawnParams.PairGeneration = _PairGeneration;
        SpawnParams.InitialTransform = FTransform(FRotator::ZeroRotator, Spawn, FVector::OneVector);
        auto Pending = utils_entity_script::Request_SpawnEntity(_Root,
            UCk_EntityScript_VoxelNavGym_Agent, FInstancedStruct::Make(SpawnParams));
        _PendingAgents.Add(Pending);
        if (!utils_pending_entity_script::Get_IsValid(Pending))
        { return false; }
        auto Root = Pending.Get_EntityUnderConstruction();
        _AgentRoots.Add(Root);
        return IsDirectChild(Root, _Root);
    }

    bool Request_Prepare()
    {
        if (_Closed || !IsVolumeLive() || _AgentRoots.Num() != 2)
        { Request_Block(); return false; }
        if (_Agents.Num() == 2)
        { return HasPreparedStructure(); }
        for (int32 I = 0; I < 2; ++I)
        {
            if (!IsDirectChild(_AgentRoots[I], _Root))
            { Request_Block(); return false; }
            auto Script = Cast<UCk_EntityScript_VoxelNavGym_Agent>(
                utils_entity_script::TryGet_EntityScript(_AgentRoots[I]));
            if (ck::Is_NOT_Valid(Script) || !utils_transform::Has(_AgentRoots[I]))
            { return true; }
            if (Script.Index != I || Script.PairGeneration != _PairGeneration)
            { Request_Block(); return false; }
        }
        for (int32 I = 0; I < 2; ++I)
        {
            if (!ComposeAgent(I))
            { Request_Block(); return false; }
        }
        _Prepared = HasPreparedStructure();
        if (!_Prepared)
        { Request_Block(); return false; }
        return true;
    }

    private bool ComposeAgent(int32 Index)
    {
        auto Root = _AgentRoots[Index];
        auto Transform = Root.As_Transform();
        auto Mode = Index == 0 ? ECk_CrowdAgent_Mode::Flying : ECk_CrowdAgent_Mode::Grounded;
        auto Color = Index == 0 ? FLinearColor(0.15, 0.9, 1.0, 1.0)
            : FLinearColor(1.0, 0.55, 0.1, 1.0);
        auto Params = FCk_CrowdAgent_Spec(k_Radius, k_Height);
        Params.Set_AgentMode(Mode);
        auto Agent = utils_crowd_agent::Add(Transform, Params);
        _Agents.Add(Agent);
        if (!UCk_VoxelNavGym_Runtime_UE::Get_IsLiveCrowdAgent(
                FCk_Handle(Agent), _World) || FCk_Handle(Agent) != Root)
        { return false; }
        utils_velocity::Add(Root,
            FCk_Velocity_Spec(ECk_LocalWorld::World, FVector::ZeroVector),
            ECk_Replication::DoesNotReplicate);
        utils_acceleration::Add(Root,
            FCk_Acceleration_Spec(ECk_LocalWorld::World, FVector::ZeroVector),
            ECk_Replication::DoesNotReplicate);
        if (!utils_velocity::Has(Root) || !utils_acceleration::Has(Root))
        { return false; }
        auto Path = utils_voxel_nav_path::Add(Root, FCk_VoxelNavPath_Spec(k_Radius));
        _Paths.Add(Path);
        _IntegratorReady.Add(false);
        if (!UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVoxelPath(
                FCk_Handle(Path), _World) || FCk_Handle(Path) != Root)
        { return false; }
        if (utils_voxel_nav_path::Request_SetVolume(Path, _Volume) != Path)
        { return false; }
        utils_crowd_agent::Set_DebugColor(Agent, Color);
        return true;
    }

    private bool CreatePair()
    {
        if (_Closed || !_AgentRoots.IsEmpty() || !_Agents.IsEmpty() ||
            !_Paths.IsEmpty() || !_IntegratorReady.IsEmpty()) { return false; }
        return CreateAgent(0, k_FlyerSpawn) && CreateAgent(1, k_GroundedSpawn);
    }

    UFUNCTION()
    private void OnBuildCompleted(FCk_Handle Owner, ECk_Request_OperationResult Result)
    {
        if (_Closed || !_BuildRequested || Owner != FCk_Handle(_Volume)) { return; }
        if (Result != ECk_Request_OperationResult::Succeeded ||
            !IsVolumeLive() || !utils_voxel_nav_volume::Get_IsBuilt(_Volume))
        { Request_Block(); return; }
        _Built = true;
        _BuildRequested = false;
    }

    UFUNCTION()
    private void OnIntegratorStarted(FCk_Handle Owner, ECk_Request_OperationResult Result)
    {
        if (_Closed || (_PendingOperation != 1 && _PendingOperation != 2)) { return; }
        auto Index = -1;
        for (int32 I = 0; I < _AgentRoots.Num(); ++I)
        { if (Owner == _AgentRoots[I]) { Index = I; break; } }
        if (Index < 0) { return; }
        if (Result != ECk_Request_OperationResult::Succeeded ||
            !IsAgentLive(Index) || _IntegratorReady[Index])
        { Request_Block(); return; }
        _IntegratorReady[Index] = true;
    }

    FCk_Handle Get_SourceRoot() const { return _Root; }
    bool Get_IsPrepared() const { return _Prepared && HasPreparedStructure(); }
    bool Get_IsBuilt() const
    { return _Built && IsVolumeLive() && utils_voxel_nav_volume::Get_IsBuilt(_Volume); }
    bool Get_IsStarted() const { return _Started && !_Closed && Get_IsPrepared(); }
    bool Get_IsClosed() const { return _Closed; }
    int64 Get_ControlCompletionCount() const { return _ControlCount; }
    int64 Get_PairGeneration() const { return _PairGeneration; }

    bool TryGet_AgentRoots(TArray<FCk_Handle>&out Out) const
    {
        Out.Reset();
        if (!Get_IsPrepared()) { return false; }
        Out = _AgentRoots;
        return true;
    }

    private bool BeginPairMoves(int32 Operation)
    {
        if (_Closed || !_Built || _PendingOperation != 0 || !Get_IsPrepared() ||
            _MoveCorrelation >= 2147483647) { return false; }
        _PendingOperation = Operation;
        _MovesSubmitted = false;
        ++_MoveCorrelation;
        _BeforeMoveEpisodes.Reset();
        for (int32 I = 0; I < 2; ++I)
        {
            FCkVoxelNavGym_AgentFrame Frame;
            if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                    FCk_Handle(_Agents[I]), _World, I, Frame))
            { Request_Block(); return false; }
            if (Frame.MoveEpisode >= 2147483647) { Request_Block(); return false; }
            _BeforeMoveEpisodes.Add(Frame.MoveEpisode);
        }
        if (Operation == 3) { return true; }
        for (int32 I = 0; I < 2; ++I)
        {
            utils_euler_integrator::Request_Start(_AgentRoots[I],
                FCk_Delegate_Request_OnCompleted(this, n"OnIntegratorStarted"));
        }
        return true;
    }

    bool Request_StartPrepared()
    {
        if (_Started) { return Get_IsPrepared(); }
        if (!Get_IsBuilt() || !Get_IsPrepared())
        { Request_Block(); return false; }
        return BeginPairMoves(1);
    }

    private FVector GetGoal(int32 Index) const
    {
        if (Index == 0) { return _GoalsAreOutbound ? k_FlyerGoal : k_FlyerSpawn; }
        return _GoalsAreOutbound ? k_GroundedGoal : k_GroundedSpawn;
    }

    private bool SubmitMoves()
    {
        if (_Closed || _MovesSubmitted || !Get_IsPrepared() ||
            _PendingOperation == 0 || _BeforeMoveEpisodes.Num() != 2)
        { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto Move = FCk_Request_CrowdAgent_MoveTo(GetGoal(I));
            Move.Set_CorrelationId(_MoveCorrelation).Set_ForceRepath(true);
            if (utils_crowd_agent::Request_MoveTo(_Agents[I], Move) != _Agents[I])
            { Request_Block(); return false; }
        }
        _MovesSubmitted = true;
        return true;
    }

    bool Request_ReverseGoals()
    {
        if (!Get_IsStarted() || !Get_IsBuilt() || _PendingOperation != 0)
        { return false; }
        if (!BeginPairMoves(3)) { return false; }
        _GoalsAreOutbound = !_GoalsAreOutbound;
        if (!SubmitMoves()) { Request_Block(); return false; }
        return true;
    }

    bool Request_RestartPair()
    {
        if (!Get_IsStarted() || !Get_IsBuilt() || _PendingOperation != 0 ||
            _PairGeneration >= 9223372036854775806 ||
            _MoveCorrelation >= 2147483647 ||
            _RetiringForest.Num() != 0) { return false; }
        TArray<FCk_Handle> Left;
        TArray<FCk_Handle> Right;
        if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentForest(
                _Root, _AgentRoots[0], _WorldRoot, Left) ||
            !UCk_VoxelNavGym_Runtime_UE::TryGet_AgentForest(
                _Root, _AgentRoots[1], _WorldRoot, Right) ||
            Left.IsEmpty() || Right.IsEmpty() ||
            !Left.Contains(_AgentRoots[0]) || !Right.Contains(_AgentRoots[1]) ||
            Left.Num() + Right.Num() > 32)
        { Request_Block(); return false; }
        for (auto Handle : Left) { _RetiringForest.Add(Handle); }
        for (auto Handle : Right)
        {
            if (_RetiringForest.Contains(Handle))
            { Request_Block(); return false; }
            _RetiringForest.Add(Handle);
        }
        _PendingOperation = 2;
        _Started = false;
        _MovesSubmitted = false;
        for (int32 I = 0; I < 2; ++I)
        {
            if (IsAgentLive(I))
            {
                utils_crowd_agent::Request_Stop(_Agents[I]);
                utils_euler_integrator::Request_Stop(_AgentRoots[I]);
            }
            utils_entity_lifetime::Request_DestroyEntity(_AgentRoots[I]);
        }
        return true;
    }

    // Called each authority tick while initial start or a control is settling.
    // False is terminal; true may mean pending, and only ControlCount is a receipt.
    bool Request_UpdateControl()
    {
        if (_Closed || !IsVolumeLive() || !Get_IsBuilt())
        { Request_Block(); return false; }
        if (_PendingOperation == 0) { return Get_IsPrepared(); }
        if (_PendingOperation == 2 && !_RetiringForest.IsEmpty())
        {
            for (auto Handle : _RetiringForest)
            {
                if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle))
                { return true; }
            }
            _RetiringForest.Reset();
            _AgentRoots.Reset(); _Agents.Reset(); _Paths.Reset();
            _IntegratorReady.Reset(); _BeforeMoveEpisodes.Reset(); _PendingAgents.Reset();
            ++_PairGeneration;
            if (!CreatePair())
            { Request_Block(); return false; }
            ++_MoveCorrelation;
            if (_MoveCorrelation <= 0)
            { Request_Block(); return false; }
            return true;
        }
        if (_PendingOperation == 2 && _Agents.IsEmpty())
        {
            if (!Request_Prepare()) { return false; }
            if (!Get_IsPrepared()) { return true; }
        }
        if (!Get_IsPrepared())
        { Request_Block(); return false; }
        if (_PendingOperation == 2 && _BeforeMoveEpisodes.IsEmpty())
        {
            TArray<int32> Episodes;
            for (int32 I = 0; I < 2; ++I)
            {
                FCkVoxelNavGym_AgentFrame Frame;
                if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                        FCk_Handle(_Agents[I]), _World, I, Frame))
                { return true; } // newly composed Crowd fragments may settle next tick
                if (Frame.MoveEpisode >= 2147483647) { Request_Block(); return false; }
                Episodes.Add(Frame.MoveEpisode);
            }
            _BeforeMoveEpisodes = Episodes;
            for (int32 I = 0; I < 2; ++I)
            {
                utils_euler_integrator::Request_Start(_AgentRoots[I],
                    FCk_Delegate_Request_OnCompleted(this, n"OnIntegratorStarted"));
            }
            return true;
        }
        if (!_MovesSubmitted)
        {
            if (_PendingOperation == 3) { return SubmitMoves(); }
            if (!_IntegratorReady[0] || !_IntegratorReady[1]) { return true; }
            if (!SubmitMoves()) { Request_Block(); return false; }
        }
        for (int32 I = 0; I < 2; ++I)
        {
            FCkVoxelNavGym_AgentFrame Frame;
            if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                    FCk_Handle(_Agents[I]), _World, I, Frame))
            { Request_Block(); return false; }
            if (Frame.MoveCorrelation != _MoveCorrelation ||
                Frame.MoveEpisode <= _BeforeMoveEpisodes[I] ||
                !Frame.Goal.Equals(GetGoal(I), 0.1f))
            { return true; }
        }
        if (_PendingOperation == 1) { _Started = true; }
        else
        {
            if (_ControlCount >= 2147483647)
            { Request_Block(); return false; }
            ++_ControlCount;
            _Started = true;
        }
        _PendingOperation = 0;
        return true;
    }

    bool TryGet_Frame(FCkVoxelNavGym_StationFrame&out Out) const
    {
        Out = FCkVoxelNavGym_StationFrame();
        if (!Get_IsStarted() || _PendingOperation != 0 || !Get_IsBuilt())
        { return false; }
        FCkVoxelNavGym_StationFrame Frame;
        Frame.PairGeneration = _PairGeneration;
        Frame.ControlCount = _ControlCount;
        Frame.Built = true;
        Frame.GoalsAreOutbound = _GoalsAreOutbound;
        Frame.BuildProgress = utils_voxel_nav_volume::Get_BuildProgress(_Volume);
        if (!Math::IsFinite(Frame.BuildProgress) ||
            Frame.BuildProgress < 0.0f || Frame.BuildProgress > 1.0f)
        { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            FCkVoxelNavGym_AgentFrame Agent;
            if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                    FCk_Handle(_Agents[I]), _World, I, Agent) ||
                Agent.Index != I || Agent.Waypoints.Num() > 128 ||
                Agent.MoveCorrelation != _MoveCorrelation ||
                !Agent.Goal.Equals(GetGoal(I), 0.1f))
            { return false; }
        }
        Out = Frame;
        return true;
    }

    void Request_Block()
    {
        if (_Closed) { return; }
        const bool BuildPending = _BuildRequested && !_Built;
        _Closed = true; _Prepared = false; _Built = false; _Started = false;
        _PendingOperation = 0;
        for (int32 I = 0; I < _Agents.Num(); ++I)
        {
            if (IsAgentLive(I))
            {
                utils_crowd_agent::Request_Stop(_Agents[I]);
                utils_euler_integrator::Request_Stop(_AgentRoots[I]);
            }
        }
        for (auto Root : _AgentRoots)
        {
            if (IsLive(Root))
            { utils_entity_lifetime::Request_DestroyEntity(Root); }
        }
        if (BuildPending && IsVolumeLive())
        { utils_voxel_nav_volume::Request_CancelBuild(
            _Volume, FCk_Request_VoxelNavVolume_CancelBuild()); }
    }
}
