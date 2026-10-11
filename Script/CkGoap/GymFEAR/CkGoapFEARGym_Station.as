// Language=angelscript

// Authority-owned F.E.A.R. source: one WorldState, two planner roles, 11 actions.
// Preparation composes the entire disabled forest; admission starts both planners.

USTRUCT()
struct FCk_GoapFEARGym_Combatant_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapFEARGym_Combatant_Station : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;

    private UWorld _World;
    private FCk_Handle _WorldRoot;
    private FCk_Handle _Root;
    private FCk_Handle_Goap_WorldState _WS;
    private FCk_Handle_Goap_Planner _Combatant;
    private FCk_Handle_Goap_Action _AttackEnemy_AsAction;
    private FCk_Handle_Goap_Planner _AttackEnemy_AsPlanner;
    private TArray<FCk_Handle_Goap_Action> _Tier1;
    private TArray<FCk_Handle_Goap_Action> _Tier2;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Combatant;
    private TArray<FString> _KnownLabels_Combatant;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Attack;
    private TArray<FString> _KnownLabels_Attack;
    private bool _Prepared = false;
    private bool _Started = false;
    private bool _Closed = false;
    private bool _ControlPending = false;
    private bool _ChildPlanRequested = false;
    private int32 _ExpectedMask = -1;
    private int64 _TopBefore = 0;
    private int32 _TopAttemptBefore = 0;
    private int64 _SubBefore = 0;
    private int32 _SubAttemptBefore = 0;
    private int64 _PlanCount = 0;
    private int64 _SubPlanCount = 0;
    private int64 _ControlCompletionCount = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        _Root = InHandle;
        _World = GetWorld();
        if (ck::Is_NOT_Valid(_World) ||
            Cast<ACk_GoapFEARGym_GameState>(_World.GetGameState()) == nullptr ||
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
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant"),
            FCk_Goap_WorldState_Spec());
        if (!IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        for (int32 I = 0; I < 8; ++I)
        {
            if (utils_goap_world_state::Set_Value(_WS, Get_Key(I), I == 3 || I == 4) != _WS)
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }

        auto Goal = TArray<FCk_GoapWS_Condition_Authored>();
        Goal.Add(FCk_GoapWS_Condition_Authored(Get_Key(6), true));
        auto PlannerParams = FCk_Goap_Planner_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.ActionSet.Combatant"));
        PlannerParams.Set_Goal(Goal);
        PlannerParams.Set_WorldStateSource(_WS);
        PlannerParams.Set_ReplanPolicy(ECk_Goap_ReplanPolicy::OnWorldStateDirty);
        PlannerParams.Set_InitialToggle(ECk_EnableDisable::Disable);
        _Combatant = utils_goap_planner::Create(InHandle,
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.ActionSet.Combatant"),
            PlannerParams);
        if (!IsPlannerLive(_Combatant) || !IsDirectChild(FCk_Handle(_Combatant), _Root) ||
            utils_goap_planner::Get_EnableToggle(_Combatant) != ECk_EnableDisable::Disable)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        _KnownClasses_Combatant.Add(UCk_GoapFEARGym_AttackEnemy);
        _KnownLabels_Combatant.Add("AttackEnemy");
        _KnownClasses_Combatant.Add(UCk_GoapFEARGym_Flank);
        _KnownLabels_Combatant.Add("Flank");
        _KnownClasses_Combatant.Add(UCk_GoapFEARGym_TakeCover);
        _KnownLabels_Combatant.Add("TakeCover");
        _KnownClasses_Combatant.Add(UCk_GoapFEARGym_Reload);
        _KnownLabels_Combatant.Add("Reload");
        _KnownClasses_Combatant.Add(UCk_GoapFEARGym_Investigate);
        _KnownLabels_Combatant.Add("Investigate");
        _KnownClasses_Combatant.Add(UCk_GoapFEARGym_Patrol);
        _KnownLabels_Combatant.Add("Patrol");
        _KnownClasses_Combatant.Add(UCk_GoapFEARGym_WaitForEnemy);
        _KnownLabels_Combatant.Add("WaitForEnemy");

        _Tier1.Add(utils_goap_planner::AddAction(_Combatant,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_AttackEnemy)));
        _Tier1.Add(utils_goap_planner::AddAction(_Combatant,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_Flank)));
        _Tier1.Add(utils_goap_planner::AddAction(_Combatant,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_TakeCover)));
        _Tier1.Add(utils_goap_planner::AddAction(_Combatant,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_Reload)));
        _Tier1.Add(utils_goap_planner::AddAction(_Combatant,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_Investigate)));
        _Tier1.Add(utils_goap_planner::AddAction(_Combatant,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_Patrol)));
        _Tier1.Add(utils_goap_planner::AddAction(_Combatant,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_WaitForEnemy)));
        if (_Tier1.Num() != 7)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        for (int32 I = 0; I < 7; ++I)
        {
            if (!IsActionLive(_Tier1[I]) ||
                !IsDirectChild(FCk_Handle(_Tier1[I]), FCk_Handle(_Combatant)))
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }
        _AttackEnemy_AsAction = _Tier1[0];

        auto AttackGoal = TArray<FCk_GoapWS_Condition_Authored>();
        AttackGoal.Add(FCk_GoapWS_Condition_Authored(Get_Key(6), true));
        auto AttackPlannerParams = FCk_Goap_Planner_Spec(
            utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.GoapFEAR.ActionSet.Combatant.AttackEnemy"));
        AttackPlannerParams.Set_Goal(AttackGoal);
        AttackPlannerParams.Set_ReplanPolicy(ECk_Goap_ReplanPolicy::OnWorldStateDirty);
        AttackPlannerParams.Set_InitialToggle(ECk_EnableDisable::Disable);
        _AttackEnemy_AsPlanner = utils_goap_planner::PromoteActionToPlanner(
            _AttackEnemy_AsAction, AttackPlannerParams);
        if (!IsPlannerLive(_AttackEnemy_AsPlanner) ||
            FCk_Handle(_AttackEnemy_AsPlanner) != FCk_Handle(_AttackEnemy_AsAction) ||
            utils_goap_planner::Get_EnableToggle(_AttackEnemy_AsPlanner) !=
                ECk_EnableDisable::Disable)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }

        _KnownClasses_Attack.Add(UCk_GoapFEARGym_AttackFromCover);
        _KnownLabels_Attack.Add("AttackFromCover");
        _KnownClasses_Attack.Add(UCk_GoapFEARGym_AttackFromFlank);
        _KnownLabels_Attack.Add("AttackFromFlank");
        _KnownClasses_Attack.Add(UCk_GoapFEARGym_AttackOpen);
        _KnownLabels_Attack.Add("AttackOpen");
        _KnownClasses_Attack.Add(UCk_GoapFEARGym_AttackEnemy_Standby);
        _KnownLabels_Attack.Add("AttackEnemy_Standby");
        _Tier2.Add(utils_goap_planner::AddAction(_AttackEnemy_AsPlanner,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_AttackFromCover)));
        _Tier2.Add(utils_goap_planner::AddAction(_AttackEnemy_AsPlanner,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_AttackFromFlank)));
        _Tier2.Add(utils_goap_planner::AddAction(_AttackEnemy_AsPlanner,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_AttackOpen)));
        _Tier2.Add(utils_goap_planner::AddAction(_AttackEnemy_AsPlanner,
            FCk_Goap_Action_Spec(UCk_GoapFEARGym_AttackEnemy_Standby)));
        if (_Tier2.Num() != 4)
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        for (int32 I = 0; I < 4; ++I)
        {
            if (!IsActionLive(_Tier2[I]) ||
                !IsDirectChild(FCk_Handle(_Tier2[I]), FCk_Handle(_AttackEnemy_AsPlanner)))
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }
        if (utils_goap_planner::BindTo_OnPlanComplete(_Combatant,
                FCk_Delegate_Goap_OnPlanComplete(this, n"OnTopPlanComplete")) != _Combatant ||
            utils_goap_planner::BindTo_OnPlanComplete(_AttackEnemy_AsPlanner,
                FCk_Delegate_Goap_OnPlanComplete(this, n"OnSubPlanComplete")) !=
                    _AttackEnemy_AsPlanner || !Get_SourceStructureIsHealthy())
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
            UCk_GoapFEARGym_Runtime_UE::Get_IsLiveInWorld(Handle, _World);
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
            UCk_GoapFEARGym_Runtime_UE::Get_IsLiveWorldState(FCk_Handle(_WS), _World);
    }
    private bool IsPlannerLive(FCk_Handle_Goap_Planner Planner) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapFEARGym_Runtime_UE::Get_IsLivePlanner(FCk_Handle(Planner), _World);
    }
    private bool IsActionLive(FCk_Handle_Goap_Action Action) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapFEARGym_Runtime_UE::Get_IsLiveAction(FCk_Handle(Action), _World);
    }
    private bool Get_SourceStructureIsHealthy() const
    {
        if (!IsRootLive() || !IsDirectChild(_Root, _WorldRoot) ||
            !IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root) ||
            !IsPlannerLive(_Combatant) ||
            !IsDirectChild(FCk_Handle(_Combatant), _Root) ||
            _Tier1.Num() != 7 || _Tier2.Num() != 4 ||
            _KnownClasses_Combatant.Num() != 7 || _KnownLabels_Combatant.Num() != 7 ||
            _KnownClasses_Attack.Num() != 4 || _KnownLabels_Attack.Num() != 4 ||
            !IsPlannerLive(_AttackEnemy_AsPlanner) ||
            FCk_Handle(_AttackEnemy_AsPlanner) != FCk_Handle(_Tier1[0]))
        { return false; }
        for (int32 I = 0; I < 7; ++I)
        {
            if (!IsActionLive(_Tier1[I]) ||
                !IsDirectChild(FCk_Handle(_Tier1[I]), FCk_Handle(_Combatant)))
            { return false; }
        }
        for (int32 I = 0; I < 4; ++I)
        {
            if (!IsActionLive(_Tier2[I]) ||
                !IsDirectChild(FCk_Handle(_Tier2[I]), FCk_Handle(_AttackEnemy_AsPlanner)))
            { return false; }
        }
        return true;
    }
    bool Get_IsPrepared() const
    {
        if (!_Prepared || _Closed || !Get_SourceStructureIsHealthy())
        { return false; }
        for (int32 I = 0; I < 8; ++I)
        {
            if (!utils_goap_world_state::Has_Key(_WS, Get_Key(I)))
            { return false; }
        }
        for (int32 I = 0; I < 7; ++I)
        {
            if (!utils_goap_action::Get_IsSetupComplete(_Tier1[I]))
            { return false; }
        }
        for (int32 I = 0; I < 4; ++I)
        {
            if (!utils_goap_action::Get_IsSetupComplete(_Tier2[I]))
            { return false; }
        }
        auto ExpectedToggle = _Started
            ? ECk_EnableDisable::Enable : ECk_EnableDisable::Disable;
        return utils_goap_planner::Get_EnableToggle(_Combatant) == ExpectedToggle &&
            utils_goap_planner::Get_EnableToggle(_AttackEnemy_AsPlanner) == ExpectedToggle;
    }
    bool Get_IsStarted() const { return _Started && !_Closed; }
    bool Get_IsClosed() const { return _Closed; }
    FCk_Handle Get_SourceRoot() const { return _Root; }
    int64 Get_PlanCount() const { return _PlanCount; }
    int64 Get_ControlCompletionCount() const { return _ControlCompletionCount; }

    private FGameplayTag Get_Key(int32 Index) const
    {
        if (Index == 0) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.EnemyVisible"); }
        if (Index == 1) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.AtCover"); }
        if (Index == 2) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.BehindEnemy"); }
        if (Index == 3) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.HasAmmo"); }
        if (Index == 4) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.HasAmmoReserve"); }
        if (Index == 5) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.HeardSound"); }
        if (Index == 6) { return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.EnemyNeutralized"); }
        return utils_gameplay_tag::ResolveGameplayTag(n"Gym.GoapFEAR.WS.Combatant.Patrolling");
    }
    int32 Get_WorldStateMask() const
    {
        if (!Get_IsPrepared()) { return -1; }
        int32 Mask = 0;
        for (int32 I = 0; I < 8; ++I)
        { if (utils_goap_world_state::Get_Value(_WS, Get_Key(I))) { Mask |= (1 << I); } }
        return Mask;
    }
    bool Request_StartPrepared()
    {
        if (_Started) { return Get_IsPrepared(); }
        if (!Get_IsPrepared())
        { Request_Block(); return false; }
        if (utils_goap_planner::Request_SetEnableToggle(
                _AttackEnemy_AsPlanner, ECk_EnableDisable::Enable) !=
                    _AttackEnemy_AsPlanner ||
            utils_goap_planner::Get_EnableToggle(_AttackEnemy_AsPlanner) !=
                    ECk_EnableDisable::Enable ||
            utils_goap_planner::Request_SetEnableToggle(
                _Combatant, ECk_EnableDisable::Enable) != _Combatant ||
            utils_goap_planner::Get_EnableToggle(_Combatant) != ECk_EnableDisable::Enable)
        { Request_Block(); return false; }
        _Started = true;
        return true;
    }
    bool Request_Control(int32 Action)
    {
        if (!_Started || _ControlPending ||
            _PlanCount < 1 || Action < 0 || Action > 7)
        { return false; }
        if (!Get_IsPrepared()) { Request_Block(); return false; }
        auto Before = Get_WorldStateMask();
        if (Before < 0 || Before > 255)
        { Request_Block(); return false; }
        auto Expected = Before;
        if (Action <= 5) { Expected ^= (1 << Action); }
        else if (Action == 6) { Expected |= 15; }
        else { Expected = 24; }
        _ExpectedMask = Expected;
        _TopBefore = _PlanCount;
        _TopAttemptBefore = utils_goap_planner::Get_PlanAttemptCount(_Combatant);
        _SubBefore = _SubPlanCount;
        _ChildPlanRequested = false;
        _ControlPending = true;
        for (int32 I = 0; I < 8; ++I)
        {
            if (((Before ^ Expected) & (1 << I)) == 0) { continue; }
            if (utils_goap_world_state::Set_Value(
                    _WS, Get_Key(I), (Expected & (1 << I)) != 0) != _WS)
            { Request_Block(); return false; }
        }
        if (utils_goap_planner::Request_Plan(_Combatant) != _Combatant)
        { Request_Block(); return false; }
        return true;
    }
    // The request completion delegate only reports dispatch; exact planner callbacks
    // and committed values are required before this increments the control receipt.
    bool Request_UpdateControl()
    {
        if (!_Started || !Get_IsPrepared())
        { Request_Block(); return false; }
        if (!_ControlPending) { return true; }
        if (Get_WorldStateMask() != _ExpectedMask) { return true; }
        auto TopStatus = utils_goap_planner::Get_PlanStatus(_Combatant);
        if (utils_goap_planner::Get_PlanAttemptCount(_Combatant) > _TopAttemptBefore &&
            (TopStatus == ECk_GoapPlanStatus::PlanFailed ||
             TopStatus == ECk_GoapPlanStatus::CostThresholdReached))
        { Request_Block(); return false; }
        if (_PlanCount <= _TopBefore ||
            utils_goap_planner::Get_PlanStatus(_Combatant) != ECk_GoapPlanStatus::PlanFound)
        { return true; }
        auto TopPlan = utils_goap_planner::Get_Plan(_Combatant);
        auto Chain = utils_goap_planner::Get_ActiveChain(_Combatant);
        if (TopPlan.Num() > 7 || Chain.Num() > 2)
        { Request_Block(); return false; }
        if (TopPlan.IsEmpty())
        {
            if (!Chain.IsEmpty()) { return true; }
        }
        else if (TopPlan[0] == _AttackEnemy_AsAction)
        {
            if (Chain.IsEmpty() || Chain[0] != _AttackEnemy_AsAction)
            { return true; }
            if (!_ChildPlanRequested)
            {
                _SubBefore = _SubPlanCount;
                _SubAttemptBefore = utils_goap_planner::Get_PlanAttemptCount(
                    _AttackEnemy_AsPlanner);
                if (utils_goap_planner::Request_Plan(_AttackEnemy_AsPlanner) !=
                    _AttackEnemy_AsPlanner)
                { Request_Block(); return false; }
                _ChildPlanRequested = true;
                return true;
            }
            auto SubStatus = utils_goap_planner::Get_PlanStatus(_AttackEnemy_AsPlanner);
            if (utils_goap_planner::Get_PlanAttemptCount(
                    _AttackEnemy_AsPlanner) > _SubAttemptBefore &&
                (SubStatus == ECk_GoapPlanStatus::PlanFailed ||
                 SubStatus == ECk_GoapPlanStatus::CostThresholdReached))
            { Request_Block(); return false; }
            if (_SubPlanCount <= _SubBefore ||
                utils_goap_planner::Get_PlanStatus(_AttackEnemy_AsPlanner) !=
                    ECk_GoapPlanStatus::PlanFound || Chain.Num() != 2)
            { return true; }
            auto KnownLeaf = false;
            for (int32 I = 0; I < 4; ++I)
            { if (Chain[1] == _Tier2[I]) { KnownLeaf = true; break; } }
            if (!KnownLeaf) { Request_Block(); return false; }
        }
        else
        {
            if (Chain.Num() != 1 || Chain[0] != TopPlan[0])
            { return true; }
            auto KnownFirst = false;
            for (int32 I = 1; I < 7; ++I)
            { if (TopPlan[0] == _Tier1[I]) { KnownFirst = true; break; } }
            if (!KnownFirst) { Request_Block(); return false; }
        }
        if (_ControlCompletionCount == 2147483647)
        { Request_Block(); return false; }
        _ControlPending = false;
        _ChildPlanRequested = false;
        _ExpectedMask = -1;
        ++_ControlCompletionCount;
        return true;
    }
    void Request_Block()
    {
        if (_Closed) { return; }
        _Closed = true;
        _Prepared = false;
        _Started = false;
        _ControlPending = false;
        if (IsPlannerLive(_Combatant))
        { utils_goap_planner::Request_SetEnableToggle(
            _Combatant, ECk_EnableDisable::Disable); }
        if (IsPlannerLive(_AttackEnemy_AsPlanner))
        { utils_goap_planner::Request_SetEnableToggle(
            _AttackEnemy_AsPlanner, ECk_EnableDisable::Disable); }
    }
    UFUNCTION()
    private void OnTopPlanComplete(
        FCk_Handle_Goap_Planner InPlanner, FCk_Goap_Payload_OnPlanComplete InPayload)
    {
        if (!_Started || _Closed || InPlanner != _Combatant || !Get_IsPrepared())
        { return; }
        if (_PlanCount == 2147483647)
        { Request_Block(); return; }
        ++_PlanCount;
    }
    UFUNCTION()
    private void OnSubPlanComplete(
        FCk_Handle_Goap_Planner InPlanner, FCk_Goap_Payload_OnPlanComplete InPayload)
    {
        if (!_Started || _Closed || InPlanner != _AttackEnemy_AsPlanner ||
            !Get_IsPrepared())
        { return; }
        if (_SubPlanCount == 2147483647)
        { Request_Block(); return; }
        ++_SubPlanCount;
    }
    bool TryGet_Frame(FCkGoapFEARGym_StationFrame&out Frame) const
    {
        if (!_Started || !Get_IsPrepared()) { return false; }
        auto Mask = Get_WorldStateMask();
        if (Mask < 0 || Mask > 255) { return false; }
        auto TopStatus = utils_goap_planner::Get_PlanStatus(_Combatant);
        auto SubStatus = utils_goap_planner::Get_PlanStatus(_AttackEnemy_AsPlanner);
        auto TopPlan = utils_goap_planner::Get_PlanClasses(_Combatant);
        auto SubPlan = utils_goap_planner::Get_PlanClasses(_AttackEnemy_AsPlanner);
        auto TopCost = utils_goap_planner::Get_PlanCost(_Combatant);
        auto SubCost = utils_goap_planner::Get_PlanCost(_AttackEnemy_AsPlanner);
        auto Chain = utils_goap_planner::Get_ActiveChain(_Combatant);
        if (TopPlan.Num() > 7 || SubPlan.Num() > 4 || Chain.Num() > 2 ||
            !Math::IsFinite(TopCost) || TopCost < 0.0f ||
            !Math::IsFinite(SubCost) || SubCost < 0.0f)
        { return false; }
        Frame = FCkGoapFEARGym_StationFrame();
        Frame.WorldStateMask = Mask;
        Frame.PlanCount = _PlanCount;
        Frame.SubPlanCount = _SubPlanCount;
        Frame.PlanStatus = int32(TopStatus);
        Frame.SubPlanStatus = int32(SubStatus);
        Frame.PlanCost = TopCost;
        Frame.SubPlanCost = SubCost;
        Frame.ControlCount = _ControlCompletionCount;
        for (int32 I = 0; I < TopPlan.Num(); ++I)
        {
            auto Code = -1;
            for (int32 J = 0; J < 7; ++J)
            { if (TopPlan[I] == _KnownClasses_Combatant[J]) { Code = J; break; } }
            if (Code < 0) { return false; }
            Frame.PlanActions.Add(Code);
        }
        for (int32 I = 0; I < SubPlan.Num(); ++I)
        {
            auto Code = -1;
            for (int32 J = 0; J < 4; ++J)
            { if (SubPlan[I] == _KnownClasses_Attack[J]) { Code = J; break; } }
            if (Code < 0) { return false; }
            Frame.SubPlanActions.Add(Code);
        }
        if (Chain.Num() >= 1)
        {
            auto Code = -1;
            for (int32 J = 0; J < 7; ++J)
            { if (Chain[0] == _Tier1[J]) { Code = J; break; } }
            if (Code < 0) { return false; }
            Frame.ActiveChain.Add(Code);
        }
        if (Chain.Num() == 2)
        {
            if (Chain[0] != _AttackEnemy_AsAction) { return false; }
            auto Code = -1;
            for (int32 J = 0; J < 4; ++J)
            { if (Chain[1] == _Tier2[J]) { Code = 7 + J; break; } }
            if (Code < 0) { return false; }
            Frame.ActiveChain.Add(Code);
        }
        auto EnemyVisible = (Mask & 1) != 0;
        auto AtCover = (Mask & 2) != 0;
        auto BehindEnemy = (Mask & 4) != 0;
        auto HasAmmo = (Mask & 8) != 0;
        auto HasAmmoReserve = (Mask & 16) != 0;
        auto HeardSound = (Mask & 32) != 0;
        auto EnemyNeutralized = (Mask & 64) != 0;
        auto Patrolling = (Mask & 128) != 0;
        auto AttackStatusStr = "(not yet active)";
        auto AttackPlanStr = "(waiting)";
        if (Chain.Num() >= 1 && Chain[0] == _AttackEnemy_AsAction)
        {
            AttackStatusStr = CkGoapGym_Common::Format_PlanStatus(SubStatus);
            AttackPlanStr = CkGoapGym_Common::Format_Plan(
                SubPlan, _KnownClasses_Attack, _KnownLabels_Attack);
        }
        Frame.Detail = "A canonical GOAP enemy AI - adapted from Jeff Orkin's F.E.A.R.\n"
            + "paper. Toggle WS keys via the Console list below to see how\n"
            + "the planner adapts. Or click WS keys directly in the\n"
            + "debugger's WORLDSTATE panel.\n\n"
            + "WorldState\n"
            + f"  EnemyVisible       {CkGoapGym_Common::Render_Bool(EnemyVisible)}     (the target)\n"
            + f"  EnemyNeutralized   {CkGoapGym_Common::Render_Bool(EnemyNeutralized)}     (goal)\n"
            + f"  HasAmmo            {CkGoapGym_Common::Render_Bool(HasAmmo)}\n"
            + f"  HasAmmoReserve     {CkGoapGym_Common::Render_Bool(HasAmmoReserve)}\n"
            + f"  AtCover            {CkGoapGym_Common::Render_Bool(AtCover)}\n"
            + f"  BehindEnemy        {CkGoapGym_Common::Render_Bool(BehindEnemy)}\n"
            + f"  HeardSound         {CkGoapGym_Common::Render_Bool(HeardSound)}\n"
            + f"  Patrolling         {CkGoapGym_Common::Render_Bool(Patrolling)}\n\n"
            + "Top-level Planner (goal: EnemyNeutralized = true)\n"
            + f"  Status   {CkGoapGym_Common::Format_PlanStatus(TopStatus)}\n"
            + f"  Plan     {CkGoapGym_Common::Format_Plan(TopPlan, _KnownClasses_Combatant, _KnownLabels_Combatant)}\n"
            + f"  Cost     {TopCost}\n\n"
            + "AttackEnemy sub-Planner (goal: EnemyNeutralized = true)\n"
            + f"  Status   {AttackStatusStr}\n"
            + f"  Plan     {AttackPlanStr}\n\n"
            + "Control panel\n"
            + "  panel [J]  EnemyVisible\n"
            + "  panel [K]  AtCover\n"
            + "  panel [L]  BehindEnemy\n"
            + "  panel [M]  HasAmmo\n"
            + "  panel [N]  HasAmmoReserve\n"
            + "  panel [O]  HeardSound\n"
            + "  panel [B]  Ideal ambush\n"
            + "  panel [R]  Reset";
        Frame.Title = "STATION 1 / F.E.A.R. COMBATANT";
        Frame.Footer = "Canonical GOAP demo - F.E.A.R.-style enemy AI.\nToggle WS keys to see the planner adapt.";
        return true;
    }
}
