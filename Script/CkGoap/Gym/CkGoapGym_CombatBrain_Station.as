// Language=angelscript

//============================================================================
// CkGoapGym - Combat Brain station entity  (3-tier canonical demo)
//
// Builds the canonical multi-tier example from spec Sec.2.2 - one tier deeper
// than the Patrol station - with two sibling composites promoted at tier 2
// so the user can watch sibling branch selection happen at depth.
//
// PR-B.1b Stage 5: the implicit-root model is gone - Engage and Win are
// direct children of the Alive Planner.
//
// Construction sequence:
//   1. utils_goap_planner::Add(InHandle, AlivePlannerParams)
//        -> top-level Alive Planner (goal: EnemyDead=true)
//   2. utils_goap_planner::AddAction(_Alive, EngageActionParams)
//        -> Engage as Tier-1 Action under Alive
//   3. utils_goap_planner::AddAction(_Alive, WinParams)
//        -> Win as Tier-1 atomic finisher under Alive
//   4. utils_goap_planner::PromoteActionToPlanner(_Engage, EngagePlannerParams)
//        -> Engage gains the Planner role (sub-goal EnemyAttacked=true)
//   5. utils_goap_planner::AddAction(_Engage_AsPlanner, LightAttacksParams)
//      utils_goap_planner::AddAction(_Engage_AsPlanner, HeavyAttacksParams)
//        -> Tier-2 composites under Engage
//   6. Promote each composite to a Planner (sub-goal EnemyHit=true).
//   7. utils_goap_planner::AddAction for Tier-3 leaves under each.
//
// Entity hierarchy after construction:
//   Owner entity
//     Alive_Planner            [Planner only]            goal: EnemyDead
//       Engage                 [Action + Planner]        eff: EnemyAttacked
//                                                        sub-goal: EnemyAttacked
//         LightAttacks         [Action + Planner]        eff: EnemyAttacked
//                                                        sub-goal: EnemyHit
//           Light1/Light2/Light3 [Action only]           eff: EnemyHit
//         HeavyAttacks         [Action + Planner]        eff: EnemyAttacked
//                                                        sub-goal: EnemyHit
//           Heavy1/Heavy2      [Action only]             eff: EnemyHit
//         Engage_HoldFire      [Action only]             eff: EnemyAttacked   (cost 999, fallback)
//       Win                    [Action only]             eff: EnemyDead
//       Standby                [Action only]             eff: EnemyDead       (cost 999, fallback)
//
// Always-valid-plan tenet (the CkGoap docs Sec. "Design tenets"):
//   Standby is the top Alive fallback; Engage_HoldFire is the Engage sub-Planner
//   fallback. LightAttacks and HeavyAttacks sub-Planners already comply via
//   their precondition-less Light1..3 / Heavy1..2 leaves.
//============================================================================

USTRUCT()
struct FCk_GoapGym_CombatBrain_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapGym_CombatBrain_Station : UCk_EntityScript_GoapGym_SharedStation
{
    private FCk_Handle_Goap_Planner _AlivePlanner;
    private FCk_Handle_Goap_Action  _Engage_AsAction;
    private FCk_Handle_Goap_Planner _Engage_AsPlanner;
    private FCk_Handle_Goap_Action  _Win;
    private FCk_Handle_Goap_Action  _LightAttacks_AsAction;
    private FCk_Handle_Goap_Planner _LightAttacks_AsPlanner;
    private FCk_Handle_Goap_Action  _HeavyAttacks_AsAction;
    private FCk_Handle_Goap_Planner _HeavyAttacks_AsPlanner;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Alive;
    private TArray<FString> _KnownLabels_Alive;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Engage;
    private TArray<FString> _KnownLabels_Engage;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Light;
    private TArray<FString> _KnownLabels_Light;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Heavy;
    private TArray<FString> _KnownLabels_Heavy;

    int32 Get_StationIndex() const override { return 5; }

    protected bool PrepareStation() override
    {
        if (!TryCreateWS(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.WS.CombatBrain"))) { return false; }
        auto AliveGoal = TArray<FCk_GoapWS_Condition_Authored>();
        AliveGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.CombatBrain.EnemyDead"), true));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.CombatBrain"), AliveGoal, false, _AlivePlanner))
        { return false; }
        if (!TryAddAction(0, UCk_GoapGym_CombatBrain_Engage, _Engage_AsAction) ||
            !TryAddAction(0, UCk_GoapGym_CombatBrain_Win, _Win))
        { return false; }
        FCk_Handle_Goap_Action Action;
        if (!TryAddAction(0, UCk_GoapGym_CombatBrain_Standby, Action))
        { return false; }
        auto EngageGoal = TArray<FCk_GoapWS_Condition_Authored>();
        EngageGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.WS.CombatBrain.EnemyAttacked"), true));
        if (!TryPromoteAction(0, utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.CombatBrain.Engage"), EngageGoal, false,
                _Engage_AsPlanner))
        { return false; }
        if (!TryAddAction(1, UCk_GoapGym_CombatBrain_LightAttacks,
                _LightAttacks_AsAction) ||
            !TryAddAction(1, UCk_GoapGym_CombatBrain_HeavyAttacks,
                _HeavyAttacks_AsAction) ||
            !TryAddAction(1, UCk_GoapGym_CombatBrain_Engage_HoldFire, Action))
        { return false; }
        auto LightGoal = TArray<FCk_GoapWS_Condition_Authored>();
        LightGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.CombatBrain.EnemyHit"), true));
        if (!TryPromoteAction(3, utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.CombatBrain.LightAttacks"), LightGoal, false,
                _LightAttacks_AsPlanner))
        { return false; }
        auto HeavyGoal = TArray<FCk_GoapWS_Condition_Authored>();
        HeavyGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.CombatBrain.EnemyHit"), true));
        if (!TryPromoteAction(4, utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.CombatBrain.HeavyAttacks"), HeavyGoal, false,
                _HeavyAttacks_AsPlanner))
        { return false; }
        if (!TryAddAction(2, UCk_GoapGym_CombatBrain_Light1, Action) ||
            !TryAddAction(2, UCk_GoapGym_CombatBrain_Light2, Action) ||
            !TryAddAction(2, UCk_GoapGym_CombatBrain_Light3, Action) ||
            !TryAddAction(3, UCk_GoapGym_CombatBrain_Heavy1, Action) ||
            !TryAddAction(3, UCk_GoapGym_CombatBrain_Heavy2, Action))
        { return false; }
        _KnownClasses_Alive.Add(UCk_GoapGym_CombatBrain_Engage);
        _KnownLabels_Alive.Add("Engage");
        _KnownClasses_Alive.Add(UCk_GoapGym_CombatBrain_Win);
        _KnownLabels_Alive.Add("Win");
        _KnownClasses_Alive.Add(UCk_GoapGym_CombatBrain_Standby);
        _KnownLabels_Alive.Add("Standby");
        _KnownClasses_Engage.Add(UCk_GoapGym_CombatBrain_LightAttacks);
        _KnownLabels_Engage.Add("LightAttacks");
        _KnownClasses_Engage.Add(UCk_GoapGym_CombatBrain_HeavyAttacks);
        _KnownLabels_Engage.Add("HeavyAttacks");
        _KnownClasses_Engage.Add(UCk_GoapGym_CombatBrain_Engage_HoldFire);
        _KnownLabels_Engage.Add("HoldFire");
        _KnownClasses_Light.Add(UCk_GoapGym_CombatBrain_Light1);
        _KnownLabels_Light.Add("Light1");
        _KnownClasses_Light.Add(UCk_GoapGym_CombatBrain_Light2);
        _KnownLabels_Light.Add("Light2");
        _KnownClasses_Light.Add(UCk_GoapGym_CombatBrain_Light3);
        _KnownLabels_Light.Add("Light3");
        _KnownClasses_Heavy.Add(UCk_GoapGym_CombatBrain_Heavy1);
        _KnownLabels_Heavy.Add("Heavy1");
        _KnownClasses_Heavy.Add(UCk_GoapGym_CombatBrain_Heavy2);
        _KnownLabels_Heavy.Add("Heavy2");
        return true;
    }
    private bool ReadKey(FName Name) const
    {
        return utils_goap_world_state::Get_Value(
            _WS, utils_gameplay_tag::ResolveGameplayTag(Name));
    }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const override
    {

        auto EnemyVisible    = ReadKey(n"Gym.Goap.WS.CombatBrain.EnemyVisible");
        auto WeaponEquipped  = ReadKey(n"Gym.Goap.WS.CombatBrain.WeaponEquipped");
        auto StaminaHigh     = ReadKey(n"Gym.Goap.WS.CombatBrain.StaminaHigh");
        auto EnemyHit        = ReadKey(n"Gym.Goap.WS.CombatBrain.EnemyHit");
        auto EnemyAttacked   = ReadKey(n"Gym.Goap.WS.CombatBrain.EnemyAttacked");
        auto EnemyDead       = ReadKey(n"Gym.Goap.WS.CombatBrain.EnemyDead");

        auto Chain = utils_goap_planner::Get_ActiveChain(_AlivePlanner);
        auto ChainLen = Chain.Num();

        // Top-level Alive.
        auto AliveStatus = utils_goap_planner::Get_PlanStatus(_AlivePlanner);
        auto AlivePlan   = utils_goap_planner::Get_PlanClasses(_AlivePlanner);

        // Tier-1 Engage (only meaningful once activation reaches it).
        auto EngageStatusStr = "(not yet active)";
        auto EngagePlanStr   = "(waiting)";
        if (ChainLen >= 1 && ck::IsValid(_Engage_AsAction))
        {
            EngageStatusStr = CkGoapGym_Common::Format_PlanStatus(
                utils_goap_action::Get_PlanStatus(_Engage_AsAction));
            EngagePlanStr = CkGoapGym_Common::Format_Plan(
                utils_goap_action::Get_Plan(_Engage_AsAction),
                _KnownClasses_Engage, _KnownLabels_Engage);
        }

        // Tier-2 LightAttacks.
        auto LightStatusStr = "(not yet active)";
        auto LightPlanStr   = "(waiting)";
        if (ChainLen >= 2 && ck::IsValid(_LightAttacks_AsAction))
        {
            LightStatusStr = CkGoapGym_Common::Format_PlanStatus(
                utils_goap_action::Get_PlanStatus(_LightAttacks_AsAction));
            LightPlanStr = CkGoapGym_Common::Format_Plan(
                utils_goap_action::Get_Plan(_LightAttacks_AsAction),
                _KnownClasses_Light, _KnownLabels_Light);
        }

        // Tier-2 HeavyAttacks.
        auto HeavyStatusStr = "(not yet active)";
        auto HeavyPlanStr   = "(waiting)";
        if (ChainLen >= 2 && ck::IsValid(_HeavyAttacks_AsAction))
        {
            HeavyStatusStr = CkGoapGym_Common::Format_PlanStatus(
                utils_goap_action::Get_PlanStatus(_HeavyAttacks_AsAction));
            HeavyPlanStr = CkGoapGym_Common::Format_Plan(
                utils_goap_action::Get_Plan(_HeavyAttacks_AsAction),
                _KnownClasses_Heavy, _KnownLabels_Heavy);
        }

        Frame.Detail = "World State\n"
            + f"  EnemyVisible    {CkGoapGym_Common::Render_Bool(EnemyVisible)}\n"
            + f"  WeaponEquipped  {CkGoapGym_Common::Render_Bool(WeaponEquipped)}\n"
            + f"  StaminaHigh     {CkGoapGym_Common::Render_Bool(StaminaHigh)}\n"
            + f"  EnemyHit        {CkGoapGym_Common::Render_Bool(EnemyHit)}\n"
            + f"  EnemyAttacked   {CkGoapGym_Common::Render_Bool(EnemyAttacked)}\n"
            + f"  EnemyDead       {CkGoapGym_Common::Render_Bool(EnemyDead)}\n\n"
            + "Top Alive Planner (goal: EnemyDead=true)\n"
            + f"  Status          {CkGoapGym_Common::Format_PlanStatus(AliveStatus)}\n"
            + f"  Plan            {CkGoapGym_Common::Format_Plan(AlivePlan, _KnownClasses_Alive, _KnownLabels_Alive)}\n"
            + f"  Chain length    {ChainLen}\n\n"
            + "Tier-1 Engage Planner (goal: EnemyAttacked=true)\n"
            + f"  Status          {EngageStatusStr}\n"
            + f"  Plan            {EngagePlanStr}\n\n"
            + "Tier-2 LightAttacks Planner (goal: EnemyHit=true)\n"
            + f"  Status          {LightStatusStr}\n"
            + f"  Plan            {LightPlanStr}\n\n"
            + "Tier-2 HeavyAttacks Planner (goal: EnemyHit=true)\n"
            + f"  Status          {HeavyStatusStr}\n"
            + f"  Plan            {HeavyPlanStr}\n\n"
            + "Control panel\n"
            + "  panel [X] / [Y] / [Z]  EnemyVisible / WeaponEquipped / StaminaHigh\n"
            + "  panel [5] / [6]        Drive to EnemyDead / Reset";


        Frame.Title = "STATION 6 / COMBAT BRAIN";
        Frame.Footer = "Canonical 3-tier Planner (spec 2.2).\nAlive -> Engage -> Light/Heavy -> atomic leaves.";
        return true;
    }
}
