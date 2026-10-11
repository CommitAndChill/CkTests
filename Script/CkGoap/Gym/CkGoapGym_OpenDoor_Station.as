// Language=angelscript

//============================================================================
// CkGoapGym - Open Door station entity
//
// Visual + interactive wrapper around the OpenDoor Planner. The station
// entity carries its own Goap root, Planner, and WS. A Tick timer refreshes
// the alcove panel with the current Door.IsOpen value, plan status, and the
// resolved plan.
//
// Player commands (exec'd on ACk_GoapGym_PlayerController, routed by station
// tag):
//   Goap.OpenDoor.Toggle  - flips Door.IsOpen and lets OnWorldStateDirty
//                           replan from the new state.
//
// Always-valid-plan tenet (the CkGoap docs Sec. "Design tenets"):
//   Catalog includes WaitAtDoor (no preconditions, effect=goal, cost 999) as
//   the unconditional fallback. OpenDoor wins whenever the door is closed
//   (cost 1 < 999); WaitAtDoor only ever appears if OpenDoor becomes
//   ineligible, which guarantees the Planner never returns PlanFailed.
//============================================================================

USTRUCT()
struct FCk_GoapGym_OpenDoor_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapGym_OpenDoor_Station : UCk_EntityScript_GoapGym_SharedStation
{
    private FCk_Handle_Goap_Planner _Planner;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses;
    private TArray<FString> _KnownLabels;

    int32 Get_StationIndex() const override { return 0; }

    protected bool PrepareStation() override
    {
        if (!TryCreateWS(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Door"))) { return false; }
        auto Goal = TArray<FCk_GoapWS_Condition_Authored>();
        Goal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Door.IsOpen"), true));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.Door"), Goal, false, _Planner))
        { return false; }
        FCk_Handle_Goap_Action Action;
        if (!TryAddAction(0, UCk_GoapGym_OpenDoor_Operator, Action) ||
            !TryAddAction(0, UCk_GoapGym_OpenDoor_WaitAtDoor, Action))
        { return false; }
        _KnownClasses.Add(UCk_GoapGym_OpenDoor_Operator);
        _KnownLabels.Add("OpenDoor");
        _KnownClasses.Add(UCk_GoapGym_OpenDoor_WaitAtDoor);
        _KnownLabels.Add("WaitAtDoor");
        return true;
    }
    private bool ReadKey(FName Name) const
    {
        return utils_goap_world_state::Get_Value(
            _WS, utils_gameplay_tag::ResolveGameplayTag(Name));
    }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const override
    {

        auto IsOpen = utils_goap_world_state::Get_Value(_WS,
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Door.IsOpen"));
        auto Status = utils_goap_planner::Get_PlanStatus(_Planner);
        auto Plan = utils_goap_planner::Get_PlanClasses(_Planner);

        auto WSLine = f"  Door.IsOpen      {CkGoapGym_Common::Render_Bool(IsOpen)}";
        auto StatusLine = f"  Status           {CkGoapGym_Common::Format_PlanStatus(Status)}";
        auto PlanLine = f"  Plan             {CkGoapGym_Common::Format_Plan(Plan, _KnownClasses, _KnownLabels)}";
        auto ChainLine = f"  Chain.Num()      {utils_goap_planner::Get_ActiveChain(_Planner).Num()}";

        Frame.Detail = "World State\n" + WSLine
            + "\n\nPlanner\n" + StatusLine
            + "\n" + PlanLine
            + "\n" + ChainLine
            + "\n\nControl panel\n  panel [J]  Flip Door.IsOpen";


        Frame.Title = "STATION 1 / OPEN DOOR";
        Frame.Footer = "Single atomic operator. Root goal Door.IsOpen=true.\nOpenDoor is the only operator.";
        return true;
    }
}
