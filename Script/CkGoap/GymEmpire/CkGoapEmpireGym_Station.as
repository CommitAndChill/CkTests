// Language=angelscript

//============================================================================
// CkGoapEmpire_Gym - one authority-owned six-action planner
//
// Plan resolves to 5 steps. Player controls:
//   panel [J] / [K] / [L] / [M] / [N]
//     - flip HasFood / HasGold / HasWood / BarracksBuilt / FeudalResearched.
//   panel [R]
//     - clear everything to false.
//============================================================================

USTRUCT()
struct FCk_GoapGym_Empire_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapGym_Empire_Station : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;

    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCk_Handle _Root;
    private FCk_Handle_Goap_Planner _Planner;
    private FCk_Handle_Goap_WorldState _WS;
    private TArray<FCk_Handle_Goap_Action> _Actions;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses;
    private TArray<FString> _KnownLabels;
    private bool _Prepared = false;
    private bool _Started = false;
    private bool _Closed = false;
    private int64 _PlanCount = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow
    DoConstruct(FCk_Handle& InHandle)
    {
        _Root = InHandle;
        _World = GetWorld();
        if (ck::Is_NOT_Valid(_World) ||
            Cast<ACk_GoapEmpireGym_GameState>(_World.GetGameState()) == nullptr ||
            !_World.GetGameState().HasAuthority() ||
            !InitialTransform.IsValid() || InitialTransform.ContainsNaN())
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

        _WS = utils_goap_world_state::Create(InHandle,
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Empire"),
            FCk_Goap_WorldState_Spec());
        if (!IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        for (int32 I = 0; I < 5; ++I)
        {
            if (utils_goap_world_state::Set_Value(_WS, Get_Key(I), false) != _WS)
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }

        // U11.1: Planner goal on PlannerParams.
        auto Goal = TArray<FCk_GoapWS_Condition_Authored>();
        Goal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Empire.FeudalResearched"),
            true));

        auto ActionSetParams = FCk_Goap_Planner_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.ActionSet.Empire"));
        ActionSetParams.Set_Goal(Goal);
        ActionSetParams.Set_WorldStateSource(_WS);
        ActionSetParams.Set_ReplanPolicy(ECk_Goap_ReplanPolicy::OnWorldStateDirty);
        ActionSetParams.Set_InitialToggle(ECk_EnableDisable::Disable);
        _Planner = utils_goap_planner::Create(InHandle,
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.ActionSet.Empire"),
            ActionSetParams);
        if (!IsPlannerLive() || !IsDirectChild(FCk_Handle(_Planner), _Root) ||
            utils_goap_planner::Get_EnableToggle(_Planner) != ECk_EnableDisable::Disable)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        _KnownClasses.Add(UCk_GoapGym_Empire_GatherFood);     _KnownLabels.Add("GatherFood");
        _KnownClasses.Add(UCk_GoapGym_Empire_GatherGold);     _KnownLabels.Add("GatherGold");
        _KnownClasses.Add(UCk_GoapGym_Empire_GatherWood);     _KnownLabels.Add("GatherWood");
        _KnownClasses.Add(UCk_GoapGym_Empire_BuildBarracks);  _KnownLabels.Add("BuildBarracks");
        _KnownClasses.Add(UCk_GoapGym_Empire_ResearchFeudal); _KnownLabels.Add("ResearchFeudal");
        _KnownClasses.Add(UCk_GoapGym_Empire_WaitForOrders);  _KnownLabels.Add("WaitForOrders");
        _Actions.Add(utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_GoapGym_Empire_GatherFood)));
        _Actions.Add(utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_GoapGym_Empire_GatherGold)));
        _Actions.Add(utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_GoapGym_Empire_GatherWood)));
        _Actions.Add(utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_GoapGym_Empire_BuildBarracks)));
        _Actions.Add(utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_GoapGym_Empire_ResearchFeudal)));
        _Actions.Add(utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_GoapGym_Empire_WaitForOrders)));
        for (int32 I = 0; I < _Actions.Num(); ++I)
        {
            if (!IsActionLive(_Actions[I]) ||
                !IsDirectChild(FCk_Handle(_Actions[I]), FCk_Handle(_Planner)))
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }
        if (utils_goap_planner::BindTo_OnPlanComplete(_Planner,
            FCk_Delegate_Goap_OnPlanComplete(this, n"OnPlanComplete")) != _Planner ||
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
            UCk_GoapEmpireGym_Runtime_UE::Get_IsLiveInWorld(Handle, _World);
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
            UCk_GoapEmpireGym_Runtime_UE::Get_IsLiveWorldState(FCk_Handle(_WS), _World);
    }

    private bool IsPlannerLive() const
    {
        return ck::IsValid(_World) &&
            UCk_GoapEmpireGym_Runtime_UE::Get_IsLivePlanner(FCk_Handle(_Planner), _World);
    }

    private bool IsActionLive(FCk_Handle_Goap_Action Action) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapEmpireGym_Runtime_UE::Get_IsLiveAction(FCk_Handle(Action), _World);
    }

    private bool Get_SourceStructureIsHealthy() const
    {
        if (!IsRootLive() || !IsDirectChild(_Root, _WorldRoot) ||
            !IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root) ||
            !IsPlannerLive() || !IsDirectChild(FCk_Handle(_Planner), _Root) ||
            _Actions.Num() != 6 || _KnownClasses.Num() != 6 || _KnownLabels.Num() != 6)
        { return false; }
        for (int32 I = 0; I < 6; ++I)
        {
            if (!IsActionLive(_Actions[I]) ||
                !IsDirectChild(FCk_Handle(_Actions[I]), FCk_Handle(_Planner)))
            { return false; }
        }
        return true;
    }

    bool Get_IsPrepared() const
    {
        if (!_Prepared || _Closed || !Get_SourceStructureIsHealthy())
        { return false; }
        for (int32 I = 0; I < 5; ++I)
        {
            if (!utils_goap_world_state::Has_Key(_WS, Get_Key(I)))
            { return false; }
        }
        for (int32 I = 0; I < 6; ++I)
        {
            if (!utils_goap_action::Get_IsSetupComplete(_Actions[I]))
            { return false; }
        }
        return utils_goap_planner::Get_EnableToggle(_Planner) ==
            (_Started ? ECk_EnableDisable::Enable : ECk_EnableDisable::Disable);
    }

    bool Get_IsStarted() const { return _Started && !_Closed; }
    bool Get_IsClosed() const { return _Closed; }
    FCk_Handle Get_SourceRoot() const { return _Root; }
    int64 Get_PlanCount() const { return _PlanCount; }

    int32 Get_WorldStateMask() const
    {
        if (!Get_IsPrepared()) { return -1; }
        int32 Mask = 0;
        for (int32 I = 0; I < 5; ++I)
        {
            if (utils_goap_world_state::Get_Value(_WS, Get_Key(I)))
            { Mask |= (1 << I); }
        }
        return Mask;
    }

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
        return true;
    }

    bool Request_Control(int32 Action)
    {
        if (!_Started || !Get_IsPrepared() || _PlanCount < 1 ||
            Action < 0 || Action > 5)
        { return false; }
        auto Before = Get_WorldStateMask();
        if (Before < 0 || Before > 31)
        { Request_Block(); return false; }
        if (Action == 5)
        {
            for (int32 I = 0; I < 5; ++I)
            {
                if (utils_goap_world_state::Set_Value(_WS, Get_Key(I), false) != _WS)
                { Request_Block(); return false; }
            }
        }
        else if (utils_goap_world_state::Set_Value(_WS, Get_Key(Action),
                     (Before & (1 << Action)) == 0) != _WS)
        { Request_Block(); return false; }
        if (utils_goap_planner::Request_Plan(_Planner) != _Planner)
        { Request_Block(); return false; }
        return true;
    }

    void Request_Block()
    {
        if (_Closed) { return; }
        _Closed = true; _Prepared = false; _Started = false;
        if (IsPlannerLive())
        { utils_goap_planner::Request_SetEnableToggle(
            _Planner, ECk_EnableDisable::Disable); }
    }

    private FGameplayTag Get_Key(int32 Index) const
    {
        if (Index == 0) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Empire.HasFood"); }
        if (Index == 1) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Empire.HasGold"); }
        if (Index == 2) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Empire.HasWood"); }
        if (Index == 3) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Empire.BarracksBuilt"); }
        return utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Empire.FeudalResearched");
    }

    UFUNCTION()
    private void OnPlanComplete(
        FCk_Handle_Goap_Planner InPlanner, FCk_Goap_Payload_OnPlanComplete InPayload)
    {
        if (!_Started || _Closed || InPlanner != _Planner || !Get_IsPrepared())
        { return; }
        if (_PlanCount == 2147483647)
        { Request_Block(); return; }
        _PlanCount++;
    }

    bool TryGet_Frame(FCkGoapEmpireGym_StationFrame&out Frame) const
    {
        if (!_Started || !Get_IsPrepared()) { return false; }
        auto Mask = Get_WorldStateMask();
        if (Mask < 0 || Mask > 31) { return false; }
        auto F = (Mask & 1) != 0;
        auto G = (Mask & 2) != 0;
        auto W = (Mask & 4) != 0;
        auto B = (Mask & 8) != 0;
        auto Fr = (Mask & 16) != 0;

        auto Status = utils_goap_planner::Get_PlanStatus(_Planner);
        auto Plan = utils_goap_planner::Get_PlanClasses(_Planner);
        auto Cost = utils_goap_planner::Get_PlanCost(_Planner);
        if (Plan.Num() < 0 || Plan.Num() > 6 ||
            !Math::IsFinite(Cost) || Cost < 0.0f)
        { return false; }
        Frame = FCkGoapEmpireGym_StationFrame();
        Frame.WorldStateMask = Mask;
        Frame.PlanCount = _PlanCount;
        Frame.PlanStatus = int32(Status);
        Frame.PlanCost = Cost;
        for (int32 I = 0; I < Plan.Num(); ++I)
        {
            auto Code = -1;
            for (int32 J = 0; J < 6; ++J)
            { if (Plan[I] == _KnownClasses[J]) { Code = J; break; } }
            if (Code < 0) { return false; }
            Frame.PlanActions.Add(Code);
        }

        auto Body = "Resources / Progress\n"
            + f"  HasFood            {CkGoapGym_Common::Render_Bool(F)}\n"
            + f"  HasGold            {CkGoapGym_Common::Render_Bool(G)}\n"
            + f"  HasWood            {CkGoapGym_Common::Render_Bool(W)}\n"
            + f"  BarracksBuilt      {CkGoapGym_Common::Render_Bool(B)}\n"
            + f"  FeudalResearched   {CkGoapGym_Common::Render_Bool(Fr)}\n\n"
            + "Operators (cost)\n"
            + "  GatherFood       3   pre (none)\n"
            + "  GatherGold       4   pre (none)\n"
            + "  GatherWood       2   pre (none)\n"
            + "  BuildBarracks    5   pre Food, Gold, Wood\n"
            + "  ResearchFeudal   6   pre Barracks, Food\n\n"
            + "Planner\n"
            + f"  Status        {CkGoapGym_Common::Format_PlanStatus(Status)}\n"
            + f"  Plan length   {Plan.Num()}\n"
            + f"  Plan          {CkGoapGym_Common::Format_Plan(Plan, _KnownClasses, _KnownLabels)}\n"
            + f"  Plan cost     {Cost}\n\n"
            + "Control panel\n"
            + "  panel [J] / [K] / [L] / [M] / [N]  Food / Gold / Wood / Barracks / Feudal\n"
            + "  panel [R]                          Reset all keys to false";

        Frame.Title = "EMPIRE / FEUDAL RESEARCH";
        Frame.Detail = Body;
        Frame.Footer = "5-step plan from raw gathering to research.\nFlip any WS bit and watch the plan re-resolve.";
        return true;
    }
}
