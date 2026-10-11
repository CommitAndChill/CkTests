// Language=angelscript

// Fixed, authority-owned GOAP station lifetime and planner receipts.
// Each derived station authors its own exact catalog and presentation.
class UCk_EntityScript_GoapGym_SharedStation : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn)
    FTransform InitialTransform = FTransform::Identity;

    protected UWorld _World;
    protected FCk_Handle _WorldRoot;
    protected FCk_Handle _Root;
    protected FCk_Handle_Goap_WorldState _WS;
    protected TArray<FCk_Handle_Goap_Planner> _Planners;
    protected TArray<int32> _PlannerParentActions;
    protected TArray<FCk_Handle_Goap_Action> _Actions;
    protected TArray<int32> _ActionParentPlanners;
    protected TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _ActionClasses;
    protected bool _Prepared = false;
    protected bool _Started = false;
    protected bool _Closed = false;
    protected int64 _ControlCount = 0;
    protected TArray<int64> _SuccessCounts;
    protected TArray<int64> _FailureCounts;
    private bool _ControlPending = false;
    private int32 _ExpectedMask = -1;
    private TArray<int64> _BeforeSuccess;
    private TArray<int64> _BeforeFailure;
    private TArray<int32> _BeforeAttempt;
    private TArray<bool> _NestedRequested;

    int32 Get_StationIndex() const { return -1; }
    protected bool PrepareStation() { return false; }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const
    { return false; }

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        _Root = InHandle;
        _World = GetWorld();
        if (ck::Is_NOT_Valid(_World) ||
            Cast<ACk_GoapGym_GameState>(_World.GetGameState()) == nullptr ||
            !_World.GetGameState().HasAuthority() ||
            !InitialTransform.IsValid() || InitialTransform.ContainsNaN())
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Ecs = Subsystem::GetWorldSubsystem(UCk_EcsWorld_Subsystem_UE);
        if (ck::Is_NOT_Valid(Ecs))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        _WorldRoot = Ecs.Get_TransientEntity();
        if (!IsDirectChild(_Root, _WorldRoot))
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        auto Transform = utils_transform::Add(
            _Root, InitialTransform, ECk_Replication::DoesNotReplicate);
        if (ck::Is_NOT_Valid(Transform) || !IsRootLive() ||
            !PrepareStation() || !Get_StructureIsHealthy())
        { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            if (utils_goap_planner::BindTo_OnPlanComplete(_Planners[I],
                    FCk_Delegate_Goap_OnPlanComplete(this, n"OnPlanComplete")) != _Planners[I] ||
                utils_goap_planner::BindTo_OnPlanFailed(_Planners[I],
                    FCk_Delegate_Goap_OnPlanFailed(this, n"OnPlanFailed")) != _Planners[I])
            { Request_Block(); return ECk_EntityScript_ConstructionFlow::Finished; }
        }
        _Prepared = true;
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    { Request_Block(); }

    protected bool IsLive(FCk_Handle Handle) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapGym_Runtime_UE::Get_IsLiveInWorld(Handle, _World);
    }
    protected bool IsDirectChild(FCk_Handle Child, FCk_Handle Parent) const
    {
        return IsLive(Child) && IsLive(Parent) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }
    protected bool IsRootLive() const
    { return IsLive(_Root) && utils_transform::Has(_Root); }
    protected bool IsWorldStateLive() const
    {
        return ck::IsValid(_World) &&
            UCk_GoapGym_Runtime_UE::Get_IsLiveWorldState(FCk_Handle(_WS), _World);
    }
    protected bool IsPlannerLive(FCk_Handle_Goap_Planner Planner) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapGym_Runtime_UE::Get_IsLivePlanner(FCk_Handle(Planner), _World);
    }
    protected bool IsActionLive(FCk_Handle_Goap_Action Action) const
    {
        return ck::IsValid(_World) &&
            UCk_GoapGym_Runtime_UE::Get_IsLiveAction(FCk_Handle(Action), _World);
    }

    // Wrappers retain the returned entity before any later operation can fail.
    protected bool TryCreateWS(FGameplayTag Tag)
    {
        if (_Closed || !IsRootLive() || !Tag.IsValid() || IsWorldStateLive())
        { return false; }
        _WS = utils_goap_world_state::Create(_Root, Tag, FCk_Goap_WorldState_Spec());
        if (!IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root))
        { return false; }
        auto Count = CkGoapGym::Get_KeyCount(Get_StationIndex());
        auto Mask = CkGoapGym::Get_InitialMask(Get_StationIndex());
        if (Count <= 0 || Count > 8 || Mask < 0 || Mask >= (1 << Count))
        { return false; }
        for (int32 I = 0; I < Count; ++I)
        {
            auto Key = CkGoapGym::Get_Key(Get_StationIndex(), I);
            if (!Key.IsValid() ||
                utils_goap_world_state::Set_Value(_WS, Key, (Mask & (1 << I)) != 0) != _WS)
            { return false; }
        }
        return true;
    }
    protected bool TryCreateTopPlanner(FGameplayTag Tag,
        TArray<FCk_GoapWS_Condition_Authored> Goal, bool AllowFailed,
        FCk_Handle_Goap_Planner&out Planner)
    {
        if (_Closed || !IsRootLive() || !IsWorldStateLive() || !Tag.IsValid())
        { return false; }
        auto Params = FCk_Goap_Planner_Spec(Tag);
        Params.Set_Goal(Goal);
        Params.Set_WorldStateSource(_WS);
        Params.Set_ReplanPolicy(ECk_Goap_ReplanPolicy::OnWorldStateDirty);
        Params.Set_InitialToggle(ECk_EnableDisable::Disable);
        Params.Set_AllowPlanFailed(AllowFailed);
        auto Result = utils_goap_planner::Create(_Root, Tag, Params);
        _Planners.Add(Result);
        _PlannerParentActions.Add(-1);
        _SuccessCounts.Add(0);
        _FailureCounts.Add(0);
        Planner = Result;
        return IsPlannerLive(Result) &&
            IsDirectChild(FCk_Handle(Result), _Root) &&
            utils_goap_planner::Get_EnableToggle(Result) == ECk_EnableDisable::Disable;
    }
    protected bool TryAddAction(int32 ParentPlanner,
        TSubclassOf<UCk_GoapAction_EntityScript> Class,
        FCk_Handle_Goap_Action&out Action)
    {
        if (_Closed || ParentPlanner < 0 || ParentPlanner >= _Planners.Num() ||
            !IsPlannerLive(_Planners[ParentPlanner]) || Class == nullptr)
        { return false; }
        auto Result = utils_goap_planner::AddAction(
            _Planners[ParentPlanner], FCk_Goap_Action_Spec(Class));
        _Actions.Add(Result);
        _ActionParentPlanners.Add(ParentPlanner);
        _ActionClasses.Add(Class);
        Action = Result;
        return IsActionLive(Result) &&
            IsDirectChild(FCk_Handle(Result), FCk_Handle(_Planners[ParentPlanner]));
    }
    protected bool TryPromoteAction(int32 ActionCode, FGameplayTag Tag,
        TArray<FCk_GoapWS_Condition_Authored> Goal, bool AllowFailed,
        FCk_Handle_Goap_Planner&out Planner)
    {
        if (_Closed || ActionCode < 0 || ActionCode >= _Actions.Num() ||
            !IsActionLive(_Actions[ActionCode]) || !Tag.IsValid())
        { return false; }
        auto Params = FCk_Goap_Planner_Spec(Tag);
        Params.Set_Goal(Goal);
        Params.Set_ReplanPolicy(ECk_Goap_ReplanPolicy::OnWorldStateDirty);
        Params.Set_InitialToggle(ECk_EnableDisable::Disable);
        Params.Set_AllowPlanFailed(AllowFailed);
        auto Result = utils_goap_planner::PromoteActionToPlanner(_Actions[ActionCode], Params);
        _Planners.Add(Result);
        _PlannerParentActions.Add(ActionCode);
        _SuccessCounts.Add(0);
        _FailureCounts.Add(0);
        Planner = Result;
        return IsPlannerLive(Result) &&
            FCk_Handle(Result) == FCk_Handle(_Actions[ActionCode]) &&
            utils_goap_planner::Get_EnableToggle(Result) == ECk_EnableDisable::Disable;
    }
    private int32 ExpectedPlannerCount() const
    {
        auto S = Get_StationIndex();
        if (S == 3) { return 3; }
        if (S == 4) { return 2; }
        if (S == 5) { return 4; }
        if (S >= 0 && S <= 6) { return 1; }
        return -1;
    }
    private int32 ExpectedActionCount() const
    {
        auto S = Get_StationIndex();
        if (S == 0) { return 2; }
        if (S == 1) { return 4; }
        if (S == 2) { return 3; }
        if (S == 3) { return 8; }
        if (S == 4) { return 6; }
        if (S == 5) { return 11; }
        if (S == 6) { return 1; }
        return -1;
    }
    protected bool Get_StructureIsHealthy() const
    {
        if (!IsRootLive() || !IsDirectChild(_Root, _WorldRoot) ||
            !IsWorldStateLive() || !IsDirectChild(FCk_Handle(_WS), _Root) ||
            _Planners.Num() != ExpectedPlannerCount() ||
            _Planners.Num() != _PlannerParentActions.Num() ||
            _Planners.Num() != _SuccessCounts.Num() ||
            _Planners.Num() != _FailureCounts.Num() ||
            _Actions.Num() != ExpectedActionCount() ||
            _Actions.Num() != _ActionParentPlanners.Num() ||
            _Actions.Num() != _ActionClasses.Num())
        { return false; }
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            if (!IsPlannerLive(_Planners[I])) { return false; }
            auto ParentCode = _PlannerParentActions[I];
            if (ParentCode == -1)
            {
                if (!IsDirectChild(FCk_Handle(_Planners[I]), _Root)) { return false; }
            }
            else if (ParentCode < 0 || ParentCode >= _Actions.Num() ||
                FCk_Handle(_Planners[I]) != FCk_Handle(_Actions[ParentCode]))
            { return false; }
        }
        for (int32 I = 0; I < _Actions.Num(); ++I)
        {
            auto Parent = _ActionParentPlanners[I];
            if (Parent < 0 || Parent >= _Planners.Num() ||
                !IsActionLive(_Actions[I]) ||
                !IsDirectChild(FCk_Handle(_Actions[I]), FCk_Handle(_Planners[Parent])) ||
                _ActionClasses[I] == nullptr)
            { return false; }
        }
        return true;
    }
    bool Get_IsPrepared() const
    {
        if (!_Prepared || _Closed || !Get_StructureIsHealthy()) { return false; }
        auto Count = CkGoapGym::Get_KeyCount(Get_StationIndex());
        if (Count <= 0 || Count > 8) { return false; }
        for (int32 I = 0; I < Count; ++I)
        {
            auto Key = CkGoapGym::Get_Key(Get_StationIndex(), I);
            if (!Key.IsValid() || !utils_goap_world_state::Has_Key(_WS, Key))
            { return false; }
        }
        for (int32 I = 0; I < _Actions.Num(); ++I)
        {
            if (!utils_goap_action::Get_IsSetupComplete(_Actions[I]))
            { return false; }
        }
        auto Toggle = _Started ? ECk_EnableDisable::Enable : ECk_EnableDisable::Disable;
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            if (utils_goap_planner::Get_EnableToggle(_Planners[I]) != Toggle)
            { return false; }
        }
        return true;
    }
    bool Get_IsStarted() const { return _Started && !_Closed; }
    bool Get_IsClosed() const { return _Closed; }
    FCk_Handle Get_SourceRoot() const { return _Root; }
    int64 Get_ControlCompletionCount() const { return _ControlCount; }

    int32 Get_WorldStateMask() const
    {
        if (!Get_IsPrepared()) { return -1; }
        auto Count = CkGoapGym::Get_KeyCount(Get_StationIndex());
        int32 Mask = 0;
        for (int32 I = 0; I < Count; ++I)
        {
            if (utils_goap_world_state::Get_Value(
                _WS, CkGoapGym::Get_Key(Get_StationIndex(), I)))
            { Mask |= (1 << I); }
        }
        return Mask;
    }
    bool Request_StartPrepared()
    {
        if (_Started) { return Get_IsPrepared(); }
        if (!Get_IsPrepared()) { Request_Block(); return false; }
        // Descendants first: a promoted Action is not native-active until selected.
        for (int32 I = _Planners.Num() - 1; I >= 0; --I)
        {
            if (utils_goap_planner::Request_SetEnableToggle(
                    _Planners[I], ECk_EnableDisable::Enable) != _Planners[I] ||
                utils_goap_planner::Get_EnableToggle(_Planners[I]) !=
                    ECk_EnableDisable::Enable)
            { Request_Block(); return false; }
        }
        _Started = true;
        return true;
    }
    void Request_Block()
    {
        if (_Closed) { return; }
        _Closed = true; _Prepared = false; _Started = false; _ControlPending = false;
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            if (IsPlannerLive(_Planners[I]))
            { utils_goap_planner::Request_SetEnableToggle(
                _Planners[I], ECk_EnableDisable::Disable); }
        }
    }

    private int32 FindPlanner(FCk_Handle_Goap_Planner Planner) const
    {
        for (int32 I = 0; I < _Planners.Num(); ++I)
        { if (Planner == _Planners[I]) { return I; } }
        return -1;
    }
    private int32 FindAction(FCk_Handle_Goap_Action Action) const
    {
        for (int32 I = 0; I < _Actions.Num(); ++I)
        { if (Action == _Actions[I]) { return I; } }
        return -1;
    }
    UFUNCTION()
    private void OnPlanComplete(
        FCk_Handle_Goap_Planner Planner, FCk_Goap_Payload_OnPlanComplete Payload)
    {
        if (!_Started || _Closed || !Get_IsPrepared()) { return; }
        auto I = FindPlanner(Planner);
        if (I < 0) { return; }
        if (_SuccessCounts[I] >= 2147483647)
        { Request_Block(); return; }
        ++_SuccessCounts[I];
    }
    UFUNCTION()
    private void OnPlanFailed(
        FCk_Handle_Goap_Planner Planner, FCk_Goap_Payload_OnPlanFailed Payload)
    {
        if (!_Started || _Closed || !Get_IsPrepared()) { return; }
        auto I = FindPlanner(Planner);
        if (I < 0) { return; }
        if (_FailureCounts[I] >= 2147483647)
        { Request_Block(); return; }
        ++_FailureCounts[I];
    }

    bool Request_Control(int32 Action)
    {
        if (_Closed || !_Started || _ControlPending ||
            CkGoapGym::Get_ActionStation(Action) != Get_StationIndex())
        { return false; }
        if (!Get_IsPrepared()) { Request_Block(); return false; }
        auto Before = Get_WorldStateMask();
        auto Expected = -1;
        if (Before < 0 ||
            !CkGoapGym::TryGet_ControlMask(
                Get_StationIndex(), Action, Before, Expected))
        { return false; }
        auto Count = CkGoapGym::Get_KeyCount(Get_StationIndex());
        if (Expected < 0 || Expected >= (1 << Count))
        { return false; }
        _ExpectedMask = Expected;
        _BeforeSuccess = _SuccessCounts;
        _BeforeFailure = _FailureCounts;
        _BeforeAttempt.Reset();
        _NestedRequested.Reset();
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            _BeforeAttempt.Add(utils_goap_planner::Get_PlanAttemptCount(_Planners[I]));
            _NestedRequested.Add(false);
        }
        _ControlPending = true;
        for (int32 I = 0; I < Count; ++I)
        {
            if (((Before ^ Expected) & (1 << I)) == 0) { continue; }
            if (utils_goap_world_state::Set_Value(
                    _WS, CkGoapGym::Get_Key(Get_StationIndex(), I),
                    (Expected & (1 << I)) != 0) != _WS)
            { Request_Block(); return false; }
        }
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            if (_PlannerParentActions[I] != -1) { continue; }
            if (utils_goap_planner::Request_Plan(_Planners[I]) != _Planners[I])
            { Request_Block(); return false; }
        }
        return true;
    }
    private bool AllowsPlanFailed(int32 PlannerIndex) const
    { return PlannerIndex == 0 && (Get_StationIndex() == 1 || Get_StationIndex() == 6); }

    // Returns false only for a terminal source loss; Ready is false while queued work settles.
    private bool CheckSelectedBranch(int32 RootPlanner, bool&out Ready)
    {
        Ready = false;
        auto Current = RootPlanner;
        for (int32 Depth = 0; Depth < 4; ++Depth)
        {
            if (Current < 0 || Current >= _Planners.Num()) { return false; }
            auto Attempt = utils_goap_planner::Get_PlanAttemptCount(_Planners[Current]);
            auto Status = utils_goap_planner::Get_PlanStatus(_Planners[Current]);
            auto FreshSuccess = _SuccessCounts[Current] > _BeforeSuccess[Current];
            auto FreshFailure = _FailureCounts[Current] > _BeforeFailure[Current];
            if (Attempt <= _BeforeAttempt[Current] || (!FreshSuccess && !FreshFailure))
            { return true; }
            if (Status == ECk_GoapPlanStatus::PlanFailed && FreshFailure &&
                AllowsPlanFailed(Current))
            { Ready = true; return true; }
            if (Status == ECk_GoapPlanStatus::PlanFailed ||
                Status == ECk_GoapPlanStatus::CostThresholdReached)
            { return false; }
            if (Status != ECk_GoapPlanStatus::PlanFound || !FreshSuccess)
            { return true; }

            auto Plan = utils_goap_planner::Get_Plan(_Planners[Current]);
            auto Chain = utils_goap_planner::Get_ActiveChain(_Planners[Current]);
            if (Plan.Num() > _Actions.Num() || Chain.Num() > 4)
            { return false; }
            if (Plan.IsEmpty())
            {
                if (!Chain.IsEmpty()) { return true; }
                Ready = true; return true;
            }
            auto ActionCode = FindAction(Plan[0]);
            if (ActionCode < 0 || _ActionParentPlanners[ActionCode] != Current)
            { return false; }
            if (Chain.IsEmpty() || Chain[0] != Plan[0])
            { return true; }
            auto Child = -1;
            for (int32 I = 0; I < _Planners.Num(); ++I)
            {
                if (_PlannerParentActions[I] == ActionCode)
                { Child = I; break; }
            }
            if (Child < 0)
            {
                if (Chain.Num() != 1) { return false; }
                Ready = true; return true;
            }
            if (!_NestedRequested[Child])
            {
                _BeforeSuccess[Child] = _SuccessCounts[Child];
                _BeforeFailure[Child] = _FailureCounts[Child];
                _BeforeAttempt[Child] =
                    utils_goap_planner::Get_PlanAttemptCount(_Planners[Child]);
                if (utils_goap_planner::Request_Plan(_Planners[Child]) != _Planners[Child])
                { return false; }
                _NestedRequested[Child] = true;
                return true;
            }
            Current = Child;
        }
        return false;
    }
    bool Request_UpdateControl()
    {
        if (!_Started || !Get_IsPrepared())
        { Request_Block(); return false; }
        if (!_ControlPending) { return true; }
        if (Get_WorldStateMask() != _ExpectedMask) { return true; }
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            if (_PlannerParentActions[I] != -1) { continue; }
            auto Ready = false;
            if (!CheckSelectedBranch(I, Ready))
            { Request_Block(); return false; }
            if (!Ready) { return true; }
        }
        if (_ControlCount >= 2147483647)
        { Request_Block(); return false; }
        ++_ControlCount;
        _ControlPending = false;
        _ExpectedMask = -1;
        return true;
    }

    bool TryGet_Frame(FCkGoapGym_StationFrame&out Out) const
    {
        if (!_Started || !Get_IsPrepared()) { return false; }
        auto Mask = Get_WorldStateMask();
        if (Mask < 0) { return false; }
        FCkGoapGym_StationFrame Frame;
        Frame.Index = Get_StationIndex();
        Frame.WorldStateMask = Mask;
        Frame.ControlCount = _ControlCount;
        for (int32 I = 0; I < _Planners.Num(); ++I)
        {
            FCkGoapGym_PlannerFrame Value;
            Value.Index = I;
            Value.ParentAction = _PlannerParentActions[I];
            Value.PlanCount = _SuccessCounts[I];
            Value.FailureCount = _FailureCounts[I];
            auto Status = utils_goap_planner::Get_PlanStatus(_Planners[I]);
            auto Cost = utils_goap_planner::Get_PlanCost(_Planners[I]);
            auto Plan = utils_goap_planner::Get_Plan(_Planners[I]);
            auto Chain = utils_goap_planner::Get_ActiveChain(_Planners[I]);
            if (!Math::IsFinite(Cost) || Cost < 0.0f ||
                Plan.Num() > _Actions.Num() || Chain.Num() > 4)
            { return false; }
            Value.PlanStatus = int32(Status);
            Value.PlanCost = Cost;
            for (auto Action : Plan)
            {
                auto Code = FindAction(Action);
                if (Code < 0 || _ActionParentPlanners[Code] != I) { return false; }
                Value.PlanActions.Add(Code);
            }
            auto ExpectedPlanner = I;
            for (auto Action : Chain)
            {
                auto Code = FindAction(Action);
                if (Code < 0 || _ActionParentPlanners[Code] != ExpectedPlanner)
                { return false; }
                Value.ActiveChain.Add(Code);
                ExpectedPlanner = -1;
                for (int32 Child = 0; Child < _Planners.Num(); ++Child)
                {
                    if (_PlannerParentActions[Child] == Code)
                    { ExpectedPlanner = Child; break; }
                }
            }
            Frame.Planners.Add(Value);
        }
        FCkGoapGym_StationFrame TextFrame;
        if (!FillStationText(TextFrame)) { return false; }
        Frame.Title = TextFrame.Title;
        Frame.Detail = TextFrame.Detail;
        Frame.Footer = TextFrame.Footer;
        Out = Frame;
        return true;
    }
}
