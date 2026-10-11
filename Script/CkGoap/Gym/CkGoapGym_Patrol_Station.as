// Language=angelscript

//============================================================================
// CkGoapGym - Patrol Route station entity  (multi-tier demo)
//
// Multi-tier Planner example demonstrating spec Sec.2.2.
//
// PR-B.1b Stage 5: the implicit-root model is gone. AddAction registers
// direct candidates on the Planner; the backchain orders them by precondition
// chain.
//
// Construction pattern:
//   1. utils_goap_planner::Add                -> top-level Planner
//                                                (Set_WorldStateSource + Set_Goal
//                                                 on PlannerParams)
//   2. utils_goap_planner::AddAction          -> direct child candidates of the
//                                                Planner (GoToWaypoint / Observe /
//                                                MarkDone)
//   3. utils_goap_planner::PromoteActionToPlanner(GoToWaypoint, subParams)
//        -> GoToWaypoint is now ALSO a Planner with goal AtWaypoint=true
//   4. utils_goap_planner::AddAction(GoToWaypoint_AsPlanner, RunParams)
//      utils_goap_planner::AddAction(GoToWaypoint_AsPlanner, WalkParams)
//        -> Tier-2 leaves: direct tree children of the promoted GoToWaypoint
//   5. Repeat for Observe composite (goal AreaScanned=true)
//
// Entity hierarchy after construction:
//   Owner entity
//     Patrol_Planner              [Planner only]     goal: AreaPatrolled=true
//       GoToWaypoint              [Action+Planner]   eff: AtWaypoint=true
//         Run                     [Action]           eff: AtWaypoint=true (cost 1)
//         Walk                    [Action]           eff: AtWaypoint=true (cost 2)
//       Observe                   [Action+Planner]   eff: AreaScanned=true
//         LookAround              [Action]           eff: AreaScanned=true (cost 1)
//         WaitAtPost              [Action]           eff: AreaScanned=true (cost 3)
//       MarkDone                  [Action]           eff: AreaPatrolled=true
//       StandWatch                [Action]           eff: AreaPatrolled=true (cost 999, fallback)
//
// Always-valid-plan tenet (the CkGoap docs Sec. "Design tenets"):
//   StandWatch is the top-Planner fallback (no preconditions, effect = goal).
//   Sub-Planners GoToWaypoint (Run/Walk leaves) and Observe (LookAround/
//   WaitAtPost leaves) already comply via their precondition-less children.
//   Default reset -> MarkDone gated by AtWaypoint+AreaScanned; planner still
//   produces a valid plan because StandWatch (cost 999) is always selectable.
//
// Player controls:
//   panel [G] - set AtWaypoint=true
//   panel [I] - set AreaScanned=true
//   panel [O] - set AreaPatrolled=true
//   panel [3] - all back to false
//============================================================================

USTRUCT()
struct FCk_GoapGym_Patrol_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapGym_Patrol_Station : UCk_EntityScript_GoapGym_SharedStation
{
    private FCk_Handle_Goap_Planner _TopPlanner;
    private FCk_Handle_Goap_Action  _GoToWaypoint_AsAction;
    private FCk_Handle_Goap_Planner _GoToWaypoint_AsPlanner;
    private FCk_Handle_Goap_Action  _Observe_AsAction;
    private FCk_Handle_Goap_Planner _Observe_AsPlanner;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Top;
    private TArray<FString> _KnownLabels_Top;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_GoTo;
    private TArray<FString> _KnownLabels_GoTo;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Observe;
    private TArray<FString> _KnownLabels_Observe;

    int32 Get_StationIndex() const override { return 3; }

    protected bool PrepareStation() override
    {
        if (!TryCreateWS(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Patrol"))) { return false; }
        auto TopGoal = TArray<FCk_GoapWS_Condition_Authored>();
        TopGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Patrol.AreaPatrolled"), true));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.Patrol"), TopGoal, false, _TopPlanner))
        { return false; }
        if (!TryAddAction(0, UCk_GoapGym_Patrol_GoToWaypoint, _GoToWaypoint_AsAction))
        { return false; }
        auto GoToGoal = TArray<FCk_GoapWS_Condition_Authored>();
        GoToGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Patrol.AtWaypoint"), true));
        if (!TryPromoteAction(0, utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.Patrol.GoToWaypoint"), GoToGoal, false,
                _GoToWaypoint_AsPlanner))
        { return false; }
        FCk_Handle_Goap_Action Action;
        if (!TryAddAction(1, UCk_GoapGym_Patrol_Run, Action) ||
            !TryAddAction(1, UCk_GoapGym_Patrol_Walk, Action) ||
            !TryAddAction(0, UCk_GoapGym_Patrol_Observe, _Observe_AsAction))
        { return false; }
        auto ObserveGoal = TArray<FCk_GoapWS_Condition_Authored>();
        ObserveGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Patrol.AreaScanned"), true));
        if (!TryPromoteAction(3, utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.Patrol.Observe"), ObserveGoal, false,
                _Observe_AsPlanner))
        { return false; }
        if (!TryAddAction(2, UCk_GoapGym_Patrol_LookAround, Action) ||
            !TryAddAction(2, UCk_GoapGym_Patrol_WaitAtPost, Action) ||
            !TryAddAction(0, UCk_GoapGym_Patrol_MarkDone, Action) ||
            !TryAddAction(0, UCk_GoapGym_Patrol_StandWatch, Action))
        { return false; }
        _KnownClasses_Top.Add(UCk_GoapGym_Patrol_GoToWaypoint); _KnownLabels_Top.Add("GoToWaypoint");
        _KnownClasses_Top.Add(UCk_GoapGym_Patrol_Observe);      _KnownLabels_Top.Add("Observe");
        _KnownClasses_Top.Add(UCk_GoapGym_Patrol_MarkDone);     _KnownLabels_Top.Add("MarkDone");
        _KnownClasses_Top.Add(UCk_GoapGym_Patrol_StandWatch);   _KnownLabels_Top.Add("StandWatch");
        _KnownClasses_GoTo.Add(UCk_GoapGym_Patrol_Run);  _KnownLabels_GoTo.Add("Run");
        _KnownClasses_GoTo.Add(UCk_GoapGym_Patrol_Walk); _KnownLabels_GoTo.Add("Walk");
        _KnownClasses_Observe.Add(UCk_GoapGym_Patrol_LookAround); _KnownLabels_Observe.Add("LookAround");
        _KnownClasses_Observe.Add(UCk_GoapGym_Patrol_WaitAtPost); _KnownLabels_Observe.Add("WaitAtPost");
        return true;
    }
    private bool ReadKey(FName Name) const
    {
        return utils_goap_world_state::Get_Value(
            _WS, utils_gameplay_tag::ResolveGameplayTag(Name));
    }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const override
    {

        auto AtWaypoint   = ReadKey(n"Gym.Goap.WS.Patrol.AtWaypoint");
        auto AreaScanned  = ReadKey(n"Gym.Goap.WS.Patrol.AreaScanned");
        auto AreaPatrolled = ReadKey(n"Gym.Goap.WS.Patrol.AreaPatrolled");

        auto Chain = utils_goap_planner::Get_ActiveChain(_TopPlanner);
        auto ChainLen = Chain.Num();

        // Top-level tier display.
        auto TopStatus = utils_goap_planner::Get_PlanStatus(_TopPlanner);
        auto TopPlan = utils_goap_planner::Get_PlanClasses(_TopPlanner);

        // GoToWaypoint sub-planner - only meaningful once it's the active step.
        auto GoToStatus = "(not yet active)";
        auto GotoPlan = "(waiting)";
        if (ChainLen >= 1 && ck::IsValid(_GoToWaypoint_AsAction))
        {
            GoToStatus = CkGoapGym_Common::Format_PlanStatus(
                utils_goap_action::Get_PlanStatus(_GoToWaypoint_AsAction));
            GotoPlan = CkGoapGym_Common::Format_Plan(
                utils_goap_action::Get_Plan(_GoToWaypoint_AsAction),
                _KnownClasses_GoTo, _KnownLabels_GoTo);
        }

        // Observe sub-planner.
        auto ObserveStatus = "(not yet active)";
        auto ObservePlan = "(waiting)";
        if (ChainLen >= 1 && ck::IsValid(_Observe_AsAction))
        {
            ObserveStatus = CkGoapGym_Common::Format_PlanStatus(
                utils_goap_action::Get_PlanStatus(_Observe_AsAction));
            ObservePlan = CkGoapGym_Common::Format_Plan(
                utils_goap_action::Get_Plan(_Observe_AsAction),
                _KnownClasses_Observe, _KnownLabels_Observe);
        }

        Frame.Detail = "World State\n"
            + f"  AtWaypoint      {CkGoapGym_Common::Render_Bool(AtWaypoint)}\n"
            + f"  AreaScanned     {CkGoapGym_Common::Render_Bool(AreaScanned)}\n"
            + f"  AreaPatrolled   {CkGoapGym_Common::Render_Bool(AreaPatrolled)}\n\n"
            + "Top Planner (goal: AreaPatrolled=true)\n"
            + f"  Status          {CkGoapGym_Common::Format_PlanStatus(TopStatus)}\n"
            + f"  Plan            {CkGoapGym_Common::Format_Plan(TopPlan, _KnownClasses_Top, _KnownLabels_Top)}\n"
            + f"  Chain length    {ChainLen}\n\n"
            + "Tier-1a GoToWaypoint Planner (goal: AtWaypoint=true)\n"
            + f"  Status          {GoToStatus}\n"
            + f"  Plan            {GotoPlan}\n\n"
            + "Tier-1b Observe Planner (goal: AreaScanned=true)\n"
            + f"  Status          {ObserveStatus}\n"
            + f"  Plan            {ObservePlan}\n\n"
            + "Control panel\n"
            + "  panel [G] / [I]  AtWaypoint / AreaScanned\n"
            + "  panel [O] / [3]  AreaPatrolled / Reset";


        Frame.Title = "STATION 4 / PATROL ROUTE";
        Frame.Footer = "Multi-tier Planner. 2-deep chain:\n[GoToWaypoint/Observe/MarkDone, Run/LookAround].";
        return true;
    }
}
