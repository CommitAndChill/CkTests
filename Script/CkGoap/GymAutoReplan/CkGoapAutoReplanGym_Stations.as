// Language=angelscript

//============================================================================
// CkGoapAutoReplan_Gym - three stations pinning each replan policy
//
// Each station shares the same minimal action graph (root + one or two
// operators) but configures _ReplanPolicy + _MinReplanIntervalSeconds
// differently to demonstrate when the planner triggers a replan.
//
// All three stations track:
//   - plan-completed count (incremented by OnPlanComplete)
//   - WS-change count (the station mutates Flip key on its own timer)
//
// The discrepancy between the two counts is what teaches the policy:
//   Explicit              -> plans = 1 initial; WS changes climb freely.
//   OnWorldStateDirty (0.5s throttle) -> plans coalesce ~once per 0.5s.
//   OnCostDirty           -> plans follow changes to the cheaper operator.
//============================================================================

USTRUCT()
struct FCk_GoapGym_AutoReplan_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;

    // Selector - which of the three policies this instance demonstrates.
    UPROPERTY()
    int32 Mode = 0;     // 0=Explicit, 1=OnWSDirty (throttled), 2=OnCostDirty
}

//============================================================================

class UCk_EntityScript_GoapGym_AutoReplan_Station : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;

    UPROPERTY(ExposeOnSpawn)
    int32 Mode = 0;

    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCk_Handle _Root;
    private FCk_Handle_Goap_Planner _Planner;
    private FCk_Handle_Goap_WorldState _WS;
    private FCk_Handle_Goap_Action _FlipAction;
    private FCk_Handle_Goap_Action _AltAction;
    private FCk_Handle_Timer _FlipTimer;
    private bool _Prepared = false;
    private bool _Started = false;
    private bool _Closed = false;
    private int64 _PlanCount = 0;
    private int64 _WSChangeCount = 0;
    private bool _AltCostIncreased = false;
    private float _FlipAccumulator = 0.0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow
    DoConstruct(FCk_Handle& InHandle)
    {
        _Root = InHandle;
        _World = GetWorld();
        if (ck::Is_NOT_Valid(_World) ||
            Cast<ACk_GoapAutoReplanGym_GameState>(_World.GetGameState()) == nullptr ||
            !_World.GetGameState().HasAuthority() ||
            !InitialTransform.IsValid() || InitialTransform.ContainsNaN() ||
            Mode < 0 || Mode > 2)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _WorldRoot = Ecs.Get_TransientEntity();
        if (!IsDirectChild(_Root, _WorldRoot))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto RootTransform = utils_transform::Add(
            InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(RootTransform) || !IsRootLive())
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        // WorldState and Planner are distinct direct children. The Planner remains disabled
        // until the GameState has admitted all three complete source forests.
        auto WsName = Get_WsNameTagForMode();
        _WS = utils_goap_world_state::Create(InHandle, WsName,
            FCk_Goap_WorldState_Spec());
        if (!IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        if (utils_goap_world_state::Set_Value(_WS,
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.AutoReplan.Goal"),
            false) != _WS ||
            utils_goap_world_state::Set_Value(_WS,
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.AutoReplan.Flip"),
            false) != _WS)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        // U11.1: Planner goal on PlannerParams.
        auto Goal = TArray<FCk_GoapWS_Condition_Authored>();
        Goal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.AutoReplan.Goal"),
            true));

        auto ActionSetParams = FCk_Goap_Planner_Spec(
            Get_PlannerTagForMode());
        ActionSetParams.Set_Goal(Goal);
        ActionSetParams.Set_WorldStateSource(_WS);
        ActionSetParams.Set_ReplanPolicy(Get_ReplanPolicyForMode());
        if (Mode == 1)
        { ActionSetParams.Set_MinReplanIntervalSeconds(0.5f); }
        ActionSetParams.Set_InitialToggle(ECk_EnableDisable::Disable);
        _Planner = utils_goap_planner::Create(InHandle,
            Get_PlannerTagForMode(), ActionSetParams);
        if (!IsPlannerLive() || !IsDirectChild(FCk_Handle(_Planner), _Root) ||
            utils_goap_planner::Get_EnableToggle(_Planner) != ECk_EnableDisable::Disable)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        _FlipAction = utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_GoapGym_AutoReplan_FlipOp));
        if (!IsActionLive(_FlipAction) ||
            !IsDirectChild(FCk_Handle(_FlipAction), FCk_Handle(_Planner)))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        // OnCostDirty station gets a second operator to demonstrate cost-swap.
        if (Mode == 2)
        {
            _AltAction = utils_goap_planner::AddAction(_Planner,
                FCk_Goap_Action_Spec(UCk_GoapGym_AutoReplan_AltOp));
            if (!IsActionLive(_AltAction) ||
                !IsDirectChild(FCk_Handle(_AltAction), FCk_Handle(_Planner)))
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }

        // Plan-counter bind. FireIfPayloadInFlightThisFrame default policy
        // catches the same-frame initial plan.
        if (utils_goap_planner::BindTo_OnPlanComplete(_Planner,
            FCk_Delegate_Goap_OnPlanComplete(this, n"OnPlanComplete")) != _Planner)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        // Auto-flipper timer - OnWSDirty + OnCostDirty stations flip
        // Flip key 10x per second to demonstrate (un)throttled replans.
        // Explicit station auto-flips too, to make the "no replan" visible.
        // The tick callback accumulates elapsed time and flips every 0.1s.
        _FlipTimer = utils_timer::Create_Tick(
            InHandle, FCk_Delegate_Timer(this, n"OnAutoFlip"));
        if (!IsTimerLive() || !IsDirectChild(FCk_Handle(_FlipTimer), _Root) ||
            utils_timer::Request_Pause(_FlipTimer) != _FlipTimer ||
            !Get_SourceStructureIsHealthy())
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _Prepared = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    { Request_Block(); }

    private bool IsLive(FCk_Handle Handle) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveInWorld(Handle, _World);
    }

    private bool IsDirectChild(FCk_Handle Child, FCk_Handle Parent) const
    {
        return IsLive(Child) && IsLive(Parent) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }

    private bool IsRootLive() const
    { return IsLive(_Root) && utils_transform::Has(_Root); }

    private bool IsWorldStateLive() const
    {
        return ck::IsValid(_World) &&
            UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveWorldState(
                FCk_Handle(_WS), _World);
    }

    private bool IsPlannerLive() const
    {
        return ck::IsValid(_World) &&
            UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLivePlanner(
                FCk_Handle(_Planner), _World);
    }

    private bool IsActionLive(FCk_Handle_Goap_Action Action) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveAction(
                FCk_Handle(Action), _World);
    }

    private bool IsTimerLive() const
    {
        return ck::IsValid(_World) &&
            UCk_GoapAutoReplanGym_Runtime_UE::Get_IsLiveTimer(
                FCk_Handle(_FlipTimer), _World);
    }

    private bool Get_SourceStructureIsHealthy() const
    {
        if (!IsRootLive() || !IsDirectChild(_Root, _WorldRoot) ||
            !IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root) ||
            !IsPlannerLive() || !IsDirectChild(FCk_Handle(_Planner), _Root) ||
            !IsActionLive(_FlipAction) ||
            !IsDirectChild(FCk_Handle(_FlipAction), FCk_Handle(_Planner)) ||
            !IsTimerLive() || !IsDirectChild(FCk_Handle(_FlipTimer), _Root))
        { return false; }
        if (Mode == 2 &&
            (!IsActionLive(_AltAction) ||
             !IsDirectChild(FCk_Handle(_AltAction), FCk_Handle(_Planner))))
        { return false; }
        return true;
    }

    bool Get_IsPrepared() const
    {
        if (!_Prepared || _Closed || !Get_SourceStructureIsHealthy())
        { return false; }
        if (!utils_goap_world_state::Has_Key(_WS,
                utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.AutoReplan.Goal")) ||
            !utils_goap_world_state::Has_Key(_WS,
                utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.AutoReplan.Flip")) ||
            !utils_goap_action::Get_IsSetupComplete(_FlipAction) ||
            (Mode == 2 && !utils_goap_action::Get_IsSetupComplete(_AltAction)) ||
            (Mode == 1 && utils_goap_planner::Get_MinReplanInterval(_Planner) != 0.5f))
        { return false; }
        return utils_goap_planner::Get_EnableToggle(_Planner) ==
            (_Started ? ECk_EnableDisable::Enable : ECk_EnableDisable::Disable);
    }

    bool Get_IsStarted() const { return _Started && !_Closed; }
    bool Get_IsClosed() const { return _Closed; }
    FCk_Handle Get_SourceRoot() const { return _Root; }
    int64 Get_PlanCount() const { return _PlanCount; }

    bool Request_StartPrepared()
    {
        if (_Started) { return Get_IsPrepared(); }
        if (!Get_IsPrepared())
        { Request_Block(); return false; }
        if (utils_goap_planner::Request_SetEnableToggle(
                _Planner, ECk_EnableDisable::Enable) != _Planner ||
            utils_goap_planner::Get_EnableToggle(_Planner) != ECk_EnableDisable::Enable)
        { Request_Block(); return false; }
        _Started = true;
        if (utils_timer::Request_Resume(_FlipTimer) != _FlipTimer)
        { Request_Block(); return false; }
        return true;
    }

    bool Request_ManualReplan()
    {
        if (Mode != 0 || !_Started || !Get_IsPrepared() || _PlanCount < 1)
        { return false; }
        if (utils_goap_planner::Request_Plan(_Planner) != _Planner)
        { Request_Block(); return false; }
        return true;
    }

    void Request_Block()
    {
        if (_Closed) { return; }
        _Closed = true;
        _Prepared = false;
        _Started = false;
        if (IsTimerLive())
        { utils_timer::Request_Pause(_FlipTimer); }
        if (IsPlannerLive())
        { utils_goap_planner::Request_SetEnableToggle(
            _Planner, ECk_EnableDisable::Disable); }
    }

    private FGameplayTag Get_WsNameTagForMode() const
    {
        if (Mode == 0) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Explicit"); }
        if (Mode == 1) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.OnWSDirty"); }
        return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.OnCostDirty");
    }

    private FGameplayTag Get_PlannerTagForMode() const
    {
        if (Mode == 0) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.ActionSet.Explicit"); }
        if (Mode == 1) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.ActionSet.OnWSDirty"); }
        return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.ActionSet.OnCostDirty");
    }

    private ECk_Goap_ReplanPolicy Get_ReplanPolicyForMode() const
    {
        if (Mode == 0) { return ECk_Goap_ReplanPolicy::Explicit; }
        if (Mode == 1) { return ECk_Goap_ReplanPolicy::OnWorldStateDirty; }
        return ECk_Goap_ReplanPolicy::OnCostDirty;
    }

    private FString Get_PolicyLabel() const
    {
        if (Mode == 0) { return "Explicit (manual replan only)"; }
        if (Mode == 1) { return "OnWorldStateDirty (throttled 0.5s)"; }
        return "OnCostDirty";
    }

    UFUNCTION()
    private void OnPlanComplete(FCk_Handle_Goap_Planner InPlanner, FCk_Goap_Payload_OnPlanComplete InPayload)
    {
        if (!_Started || _Closed || InPlanner != _Planner || !Get_IsPrepared())
        { return; }
        if (_PlanCount == 2147483647)
        { Request_Block(); return; }
        _PlanCount++;
    }

    UFUNCTION()
    private void OnAutoFlip(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (!_Started || _Closed || InHandle != _FlipTimer || !Get_IsPrepared())
        { return; }
        auto Seconds = InDeltaT.Get_Seconds();
        if (!Math::IsFinite(Seconds) || Seconds < 0.0 ||
            !Math::IsFinite(_FlipAccumulator) || _WSChangeCount == 2147483647)
        { Request_Block(); return; }

        // Throttle to ~10 flips/sec - every tick would dilute the throttle
        // demonstration on Mode 1.
        _FlipAccumulator = _FlipAccumulator + float(Seconds);
        if (!Math::IsFinite(_FlipAccumulator))
        { Request_Block(); return; }
        if (_FlipAccumulator < 0.1) { return; }
        _FlipAccumulator = 0.0;

        auto Tag = utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.AutoReplan.Flip");
        auto Cur = utils_goap_world_state::Get_Value(_WS, Tag);
        if (utils_goap_world_state::Set_Value(_WS, Tag, !Cur) != _WS)
        { Request_Block(); return; }
        _WSChangeCount++;

        // OnCostDirty station: bump the AltOp's cost up and down every other
        // tick so the planner has a reason to flip operators based on cost.
        if (Mode == 2)
        {
            _AltCostIncreased = !_AltCostIncreased;
            auto NewCost = _AltCostIncreased ? 0.5f : 2.0f;
            if (utils_goap_planner::Request_SetChildActionCost(_Planner,
                UCk_GoapGym_AutoReplan_AltOp, NewCost) != _Planner)
            { Request_Block(); }
        }
    }

    bool TryGet_Frame(FCkGoapAutoReplanGym_StationFrame&out Frame) const
    {
        if (!_Started || !Get_IsPrepared()) { return false; }
        auto Status = utils_goap_planner::Get_PlanStatus(_Planner);
        auto Plan = utils_goap_planner::Get_PlanClasses(_Planner);
        if (Plan.Num() < 0 || Plan.Num() > 2) { return false; }
        auto FirstAction = 0;
        if (Plan.Num() > 0)
        {
            if (Plan[0] == UCk_GoapGym_AutoReplan_FlipOp) { FirstAction = 1; }
            else if (Mode == 2 && Plan[0] == UCk_GoapGym_AutoReplan_AltOp)
            { FirstAction = 2; }
            else { return false; }
        }
        Frame = FCkGoapAutoReplanGym_StationFrame();
        Frame.Mode = Mode;
        Frame.PlanCount = _PlanCount;
        Frame.WorldStateChangeCount = _WSChangeCount;
        Frame.PlanStatus = int32(Status);
        Frame.PlanLength = Plan.Num();
        Frame.FirstAction = FirstAction;
        Frame.Flip = utils_goap_world_state::Get_Value(_WS,
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.AutoReplan.Flip"));
        Frame.AltCostLowered = Mode == 2 && _AltCostIncreased;

        auto ModeLabel = Get_PolicyLabel();
        auto Title = "";
        auto ButtonsBlock = "";
        if (Mode == 0)
        {
            Title = "STATION A / EXPLICIT";
            ButtonsBlock = "  panel [R]  Station A: replan now\n"
                + "       Manually issue Request_Plan.\n"
                + "       Plan count should jump exactly +1.";
        }
        else if (Mode == 1)
        {
            Title = "STATION B / OnWorldStateDirty (throttle 0.5s)";
            ButtonsBlock = "  (auto-flipping WS every 0.1s)\n"
                + "  Plan count should grow ~2/sec - coalesced from\n"
                + "  ~10 WS changes/sec by the 0.5s throttle window.";
        }
        else
        {
            Title = "STATION C / OnCostDirty";
            ButtonsBlock = "  (auto-bumping AltOp cost between 0.5 and 2.0)\n"
                + "  Plan flips between [FlipOp] and [AltOp] as the\n"
                + "  cheaper operator changes.";
        }

        auto Body = f"Policy   {ModeLabel}\n\n"
            + "Counters\n"
            + f"  Plan count       {_PlanCount}\n"
            + f"  WS changes       {_WSChangeCount}\n\n"
            + "Planner\n"
            + f"  Status           {CkGoapGym_Common::Format_PlanStatus(Status)}\n"
            + f"  Plan length      {Plan.Num()}\n\n"
            + "Console / behaviour\n"
            + ButtonsBlock;

        Frame.Title = Title;
        Frame.Detail = Body;
        Frame.Footer = "Replan policy showcase - see plan vs WS counts.";
        return true;
    }
}
