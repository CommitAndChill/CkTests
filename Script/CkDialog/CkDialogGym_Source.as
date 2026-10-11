// Language=angelscript

// Common authority callback host. Each station owns one root with its emitter
// and paused cadence; no callback may issue a query before cohort admission.
class UCk_EntityScript_DialogGym_Source : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;

    protected FCk_Handle _Root;
    protected FCk_Handle_Timer _Cadence;
    protected UCk_DialogGym_Bank _Bank;
    protected UWorld _World;
    protected FCk_Handle _WorldRoot;
    protected bool _Prepared = false;
    protected bool _Started = false;
    protected bool _Closed = false;

    protected bool IsRootLive() const
    {
        return ck::IsValid(_World) && !_Closed &&
            UCk_DialogGym_Runtime_UE::Get_IsLiveInWorld(_Root, _World) &&
            utils_transform::Has(_Root);
    }

    protected bool IsEmitterLive(FCk_Handle_DialogEmitter Emitter) const
    {
        return ck::IsValid(_World) &&
            UCk_DialogGym_Runtime_UE::Get_IsLiveEmitter(FCk_Handle(Emitter), _World);
    }

    protected bool IsDirectChild(FCk_Handle Child, FCk_Handle Parent) const
    {
        return ck::IsValid(_World) &&
            UCk_DialogGym_Runtime_UE::Get_IsLiveInWorld(Child, _World) &&
            UCk_DialogGym_Runtime_UE::Get_IsLiveInWorld(Parent, _World) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }

    protected bool IsTimerLive(FCk_Handle_Timer Timer) const
    {
        return ck::IsValid(_World) &&
            UCk_DialogGym_Runtime_UE::Get_IsLiveTimer(FCk_Handle(Timer), _World);
    }

    protected bool IsRunningCallback(FCk_Handle_Timer Timer) const
    {
        return _Started && !_Closed && Timer == _Cadence &&
            IsRootLive() && IsTimerLive(Timer) && Get_StationIsHealthy();
    }

    protected bool IsRunningResult(FCk_Handle_DialogEmitter Emitter) const
    {
        return _Started && !_Closed && IsRootLive() &&
            IsEmitterLive(Emitter) && Get_StationIsHealthy();
    }

    protected FCk_Handle_Timer CreateCadence(float32 Seconds, FName Callback)
    {
        if (!IsRootLive() || !Math::IsFinite(Seconds) || Seconds <= 0.0f)
        { return FCk_Handle_Timer(); }
        auto Params = FCk_Timer_Spec(FCk_Time(Seconds));
        Params.Set_StartingState(ECk_Timer_State::Paused);
        Params.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        auto Timer = utils_timer::Add(_Root, Params);
        if (!IsTimerLive(Timer)) { return FCk_Handle_Timer(); }
        Timer.BindTo_OnDone(FCk_Delegate_Timer(this, Callback));
        return Timer;
    }

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        _Root = InHandle;
        _World = GetWorld();
        if (ck::Is_NOT_Valid(_World) ||
            Cast<ACk_DialogGym_GameState>(_World.GetGameState()) == nullptr ||
            !_World.GetGameState().HasAuthority() ||
            !InitialTransform.IsValid() || InitialTransform.ContainsNaN())
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto State = Cast<ACk_DialogGym_GameState>(_World.GetGameState());
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _WorldRoot = Ecs.Get_TransientEntity();
        if (!IsDirectChild(_Root, _WorldRoot))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _Bank = State.Get_DialogBank();
        if (ck::Is_NOT_Valid(_Bank) || !_Bank.Get_IsReady() ||
            !UCk_DialogGym_Runtime_UE::Get_IsLiveInWorld(_Root, _World))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Transform = utils_transform::Add(_Root, InitialTransform,
            ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Transform) || !IsRootLive() ||
            !PrepareStation() || !Get_StationIsHealthy() ||
            !IsTimerLive(_Cadence))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _Prepared = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    { Request_Block(); }

    protected bool PrepareStation() { return false; }
    protected bool Get_StationIsHealthy() const { return false; }
    protected bool FillStationFrame(FCkDialogGym_StationFrame&out Frame) const
    { return false; }

    bool Get_IsPrepared() const
    {
        return _Prepared && !_Closed && IsRootLive() &&
            IsDirectChild(_Root, _WorldRoot) &&
            ck::IsValid(_Bank) && _Bank.Get_IsReady() &&
            IsTimerLive(_Cadence) && Get_StationIsHealthy();
    }

    bool Get_IsStarted() const { return _Started && !_Closed; }
    bool Get_IsClosed() const { return _Closed; }
    FCk_Handle Get_SourceRoot() const { return _Root; }

    bool Request_StartPrepared()
    {
        if (_Started) { return Get_IsPrepared(); }
        if (!Get_IsPrepared()) { Request_Block(); return false; }
        _Started = true;
        if (utils_timer::Request_Resume(_Cadence) != _Cadence)
        { Request_Block(); return false; }
        return true;
    }

    void Request_Block()
    {
        if (_Closed) { return; }
        _Closed = true;
        _Prepared = false;
        _Started = false;
        if (IsTimerLive(_Cadence)) { utils_timer::Request_Stop(_Cadence); }
    }

    bool TryGet_Frame(FCkDialogGym_StationFrame&out Out) const
    {
        if (!Get_IsPrepared()) { return false; }
        FCkDialogGym_StationFrame Frame;
        if (!FillStationFrame(Frame)) { return false; }
        Out = Frame;
        return true;
    }
}
