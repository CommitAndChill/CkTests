// Language=angelscript

//============================================================================
// CkGoapGym - Cross River station entity
//
// Branching plan with cost sensitivity.
//
// Initial WS: BridgeIsOpen=true, HasCoin=true, Crossed=false.
// Plan: [UseBridge] (cost 2 - lowest).
//
// Player controls:
//   panel [B] - Bridge becomes blocked -> plan flips to UseFerry.
//   panel [F] - coin gone too -> plan flips to SwimAcross.
//   panel [2] - restore both, plan returns to UseBridge.
//============================================================================

USTRUCT()
struct FCk_GoapGym_CrossRiver_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapGym_CrossRiver_Station : UCk_EntityScript_GoapGym_SharedStation
{
    private FCk_Handle_Goap_Planner _Planner;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses;
    private TArray<FString> _KnownLabels;

    int32 Get_StationIndex() const override { return 2; }

    protected bool PrepareStation() override
    {
        if (!TryCreateWS(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.CrossRiver"))) { return false; }
        auto Goal = TArray<FCk_GoapWS_Condition_Authored>();
        Goal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.CrossRiver.Crossed"), true));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.CrossRiver"), Goal, false, _Planner))
        { return false; }
        FCk_Handle_Goap_Action Action;
        if (!TryAddAction(0, UCk_GoapGym_CrossRiver_UseBridge, Action) ||
            !TryAddAction(0, UCk_GoapGym_CrossRiver_UseFerry, Action) ||
            !TryAddAction(0, UCk_GoapGym_CrossRiver_SwimAcross, Action))
        { return false; }
        _KnownClasses.Add(UCk_GoapGym_CrossRiver_UseBridge);  _KnownLabels.Add("UseBridge");
        _KnownClasses.Add(UCk_GoapGym_CrossRiver_UseFerry);   _KnownLabels.Add("UseFerry");
        _KnownClasses.Add(UCk_GoapGym_CrossRiver_SwimAcross); _KnownLabels.Add("SwimAcross");
        return true;
    }
    private bool ReadKey(FName Name) const
    {
        return utils_goap_world_state::Get_Value(
            _WS, utils_gameplay_tag::ResolveGameplayTag(Name));
    }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const override
    {

        auto BridgeOpen = ReadKey(n"Gym.Goap.WS.CrossRiver.BridgeIsOpen");
        auto HasCoin = ReadKey(n"Gym.Goap.WS.CrossRiver.HasCoin");
        auto Crossed = ReadKey(n"Gym.Goap.WS.CrossRiver.Crossed");

        auto Status = utils_goap_planner::Get_PlanStatus(_Planner);
        auto Plan = utils_goap_planner::Get_PlanClasses(_Planner);
        auto Cost = utils_goap_planner::Get_PlanCost(_Planner);

        Frame.Detail = "World State\n"
            + f"  BridgeIsOpen   {CkGoapGym_Common::Render_Bool(BridgeOpen)}\n"
            + f"  HasCoin        {CkGoapGym_Common::Render_Bool(HasCoin)}\n"
            + f"  Crossed        {CkGoapGym_Common::Render_Bool(Crossed)}\n\n"
            + "Operators (cost)\n"
            + "  UseBridge   2   pre BridgeIsOpen\n"
            + "  UseFerry    4   pre HasCoin\n"
            + "  SwimAcross  8   pre (none)\n\n"
            + "Planner\n"
            + f"  Status         {CkGoapGym_Common::Format_PlanStatus(Status)}\n"
            + f"  Plan           {CkGoapGym_Common::Format_Plan(Plan, _KnownClasses, _KnownLabels)}\n"
            + f"  Plan cost      {Cost}\n\n"
            + "Control panel\n"
            + "  panel [B]  Block / open the bridge\n"
            + "  panel [F]  Lose the coin (one-way)\n"
            + "  panel [2]  Restore initial WS";


        Frame.Title = "STATION 3 / CROSS RIVER";
        Frame.Footer = "Branching: 3 operators, same effect, different costs.\nLowest-cost branch wins; replans on WS flips.";
        return true;
    }
}
