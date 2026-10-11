// Language=angelscript

//============================================================================
// CkGoapGym - Make Tea station entity
//
// Four-step linear dependency chain. Plan resolves to:
//   [BoilWater, SteepLeaves, PourCup, Serve]
//
// Initial WS has all ingredients (HasKettle/HasWater/HasTeaLeaves/HasCup).
// Drop any ingredient via its control-panel toggle to see the plan collapse
// to PlanFailed.
//
// Player controls:
//   panel [K] / [L] / [M] / [N]
//     - flip HasKettle / HasWater / HasTeaLeaves / HasCup.
//   panel [1]
//     - restore all ingredients (true) and clear partial-progress bits.
//============================================================================

USTRUCT()
struct FCk_GoapGym_MakeTea_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapGym_MakeTea_Station : UCk_EntityScript_GoapGym_SharedStation
{
    private FCk_Handle_Goap_Planner _Planner;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses;
    private TArray<FString> _KnownLabels;

    int32 Get_StationIndex() const override { return 1; }

    protected bool PrepareStation() override
    {
        if (!TryCreateWS(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Tea"))) { return false; }
        auto Goal = TArray<FCk_GoapWS_Condition_Authored>();
        Goal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Tea.TeaServed"), true));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.Tea"), Goal, true, _Planner))
        { return false; }
        FCk_Handle_Goap_Action Action;
        if (!TryAddAction(0, UCk_GoapGym_MakeTea_BoilWater, Action) ||
            !TryAddAction(0, UCk_GoapGym_MakeTea_SteepLeaves, Action) ||
            !TryAddAction(0, UCk_GoapGym_MakeTea_PourCup, Action) ||
            !TryAddAction(0, UCk_GoapGym_MakeTea_Serve, Action))
        { return false; }
        _KnownClasses.Add(UCk_GoapGym_MakeTea_BoilWater);   _KnownLabels.Add("BoilWater");
        _KnownClasses.Add(UCk_GoapGym_MakeTea_SteepLeaves); _KnownLabels.Add("SteepLeaves");
        _KnownClasses.Add(UCk_GoapGym_MakeTea_PourCup);     _KnownLabels.Add("PourCup");
        _KnownClasses.Add(UCk_GoapGym_MakeTea_Serve);       _KnownLabels.Add("Serve");
        return true;
    }
    private bool ReadKey(FName Name) const
    {
        return utils_goap_world_state::Get_Value(
            _WS, utils_gameplay_tag::ResolveGameplayTag(Name));
    }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const override
    {

        auto HasKettle  = ReadKey(n"Gym.Goap.WS.Tea.HasKettle");
        auto HasWater   = ReadKey(n"Gym.Goap.WS.Tea.HasWater");
        auto HasLeaves  = ReadKey(n"Gym.Goap.WS.Tea.HasTeaLeaves");
        auto HasCup     = ReadKey(n"Gym.Goap.WS.Tea.HasCup");
        auto WaterBoiled = ReadKey(n"Gym.Goap.WS.Tea.WaterBoiled");
        auto TeaSteeped = ReadKey(n"Gym.Goap.WS.Tea.TeaSteeped");
        auto TeaPoured  = ReadKey(n"Gym.Goap.WS.Tea.TeaPoured");
        auto TeaServed  = ReadKey(n"Gym.Goap.WS.Tea.TeaServed");

        auto Status = utils_goap_planner::Get_PlanStatus(_Planner);
        auto Plan = utils_goap_planner::Get_PlanClasses(_Planner);

        Frame.Detail = "Ingredients\n"
            + f"  HasKettle      {CkGoapGym_Common::Render_Bool(HasKettle)}\n"
            + f"  HasWater       {CkGoapGym_Common::Render_Bool(HasWater)}\n"
            + f"  HasTeaLeaves   {CkGoapGym_Common::Render_Bool(HasLeaves)}\n"
            + f"  HasCup         {CkGoapGym_Common::Render_Bool(HasCup)}\n\n"
            + "Progress\n"
            + f"  WaterBoiled    {CkGoapGym_Common::Render_Bool(WaterBoiled)}\n"
            + f"  TeaSteeped     {CkGoapGym_Common::Render_Bool(TeaSteeped)}\n"
            + f"  TeaPoured      {CkGoapGym_Common::Render_Bool(TeaPoured)}\n"
            + f"  TeaServed      {CkGoapGym_Common::Render_Bool(TeaServed)}\n\n"
            + "Planner\n"
            + f"  Status         {CkGoapGym_Common::Format_PlanStatus(Status)}\n"
            + f"  Plan           {CkGoapGym_Common::Format_Plan(Plan, _KnownClasses, _KnownLabels)}\n\n"
            + "Control panel\n"
            + "  panel [K] / [L] / [M] / [N]  Kettle / Water / Leaves / Cup\n"
            + "  panel [1]                    Reset the tea WS";


        Frame.Title = "STATION 2 / MAKE TEA";
        Frame.Footer = "4-step dependency chain. Drop an ingredient to see PlanFailed.";
        return true;
    }
}
