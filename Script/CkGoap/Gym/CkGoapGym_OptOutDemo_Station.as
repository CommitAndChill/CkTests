// Language=angelscript

//============================================================================
// CkGoapGym - Opt-Out Demo station
//
// EXPLICITLY demonstrates the _AllowPlanFailed=true opt-out path described in
// the CkGoap docs Sec. "Design tenets / Every Planner must always produce a
// valid plan". This is the COUNTERPART to the other CkGoap gyms: every other
// game-content Planner must comply with the always-valid-plan tenet (and the
// gyms here include explicit fallback Actions to do so). This station is the
// one place where opting out is the demo.
//
// Construction:
//   - Planner goal:  OptOutDemo.Unreachable = true
//   - One Action (CannotReach) whose effect = OptOutDemo.Unreachable=true,
//     gated by a precondition (OptOutDemo.Touch=true) that the gym never
//     flips. Even though the Action's effect technically matches the goal,
//     the precondition is registered as false and never satisfied - so the
//     planner provably never finds a viable Action chain.
//   - PlannerParams.Set_AllowPlanFailed(true). Without this flag, the
//     framework's runtime CK_ENSURE_IF_NOT on PlanFailed would fire.
//
// What the debugger should show:
//   - PlanStatus = PlanFailed every replan
//   - An OPT-OUT indicator (whatever the debugger renders for
//     _AllowPlanFailed=true) signalling this is intentional, not a bug
//   - No CK_ENSURE_IF_NOT warnings in the log
//
// Compare with MakeTea: MakeTea also opts out but ONLY when the player drops
// an ingredient. This station opts out unconditionally - the goal is
// structurally unreachable. Treat the two stations together as the complete
// teaching pair for the opt-out path.
//============================================================================

USTRUCT()
struct FCk_GoapGym_OptOutDemo_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

// The intentionally-blocked Action. Precondition Touch=true is never flipped
// by the gym, so the precondition is always false and the planner backchain
// never finds this Action selectable. The Action exists only to register
// both WS keys in the Planner's key registry - without at least one Action
// referencing each key, writes to them would be silent no-ops.
class UCk_GoapGym_OptOutDemo_CannotReach : UCk_GoapAction_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineAction()
    {
        auto _CkPerfScope = ck::ScopedStat();
        AddPrecondition(utils_gameplay_tag::ResolveGameplayTag(
            n"Gym.Goap.WS.OptOutDemo.Touch"), true);
        AddEffect(utils_gameplay_tag::ResolveGameplayTag(
            n"Gym.Goap.WS.OptOutDemo.Unreachable"), true);
        SetCost(1.0);
    }
}

class UCk_EntityScript_GoapGym_OptOutDemo_Station : UCk_EntityScript_GoapGym_SharedStation
{
    private FCk_Handle_Goap_Planner _Planner;

    int32 Get_StationIndex() const override { return 6; }

    protected bool PrepareStation() override
    {
        if (!TryCreateWS(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.OptOutDemo"))) { return false; }
        auto Goal = TArray<FCk_GoapWS_Condition_Authored>();
        Goal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.OptOutDemo.Unreachable"), true));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.OptOutDemo"), Goal, true, _Planner))
        { return false; }
        FCk_Handle_Goap_Action Action;
        if (!TryAddAction(0, UCk_GoapGym_OptOutDemo_CannotReach, Action))
        { return false; }

        return true;
    }
    private bool ReadKey(FName Name) const
    {
        return utils_goap_world_state::Get_Value(
            _WS, utils_gameplay_tag::ResolveGameplayTag(Name));
    }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const override
    {

        auto Status = utils_goap_planner::Get_PlanStatus(_Planner);
        auto Plan = utils_goap_planner::Get_PlanClasses(_Planner);

        Frame.Detail = "This station explicitly opts out of the always-valid-plan\n"
            + "tenet via _AllowPlanFailed=true. The Planner's goal is\n"
            + "unreachable (no Action's effect can satisfy it from any WS\n"
            + "state - the sole Action has a precondition the gym never\n"
            + "flips), so PlanFailed fires every replan. Watch the\n"
            + "debugger: the OPT-OUT indicator should make it clear this\n"
            + "is INTENTIONAL, not a bug.\n\n"
            + "Compare with MakeTea (Station 2): MakeTea opts out only when\n"
            + "the player drops an ingredient. This station is permanently\n"
            + "PlanFailed by design.\n\n"
            + "Planner\n"
            + f"  Status        {CkGoapGym_Common::Format_PlanStatus(Status)}\n"
            + f"  Plan length   {Plan.Num()}   (expected: 0)\n\n"
            + "See the CkGoap docs Sec. \"Design tenets / Every Planner must\n"
            + "always produce a valid plan\" for the tenet itself and the\n"
            + "rules around opt-out.";


        Frame.Title = "STATION 7 / OPT-OUT DEMO";
        Frame.Footer = "Intentional _AllowPlanFailed=true.\nDemonstrates the tenet's opt-out path.";
        return true;
    }
}
