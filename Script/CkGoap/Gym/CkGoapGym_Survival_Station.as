// Language=angelscript

//============================================================================
// CkGoapGym - Survival Decision station entity
//
// One owner entity hosting TWO independent child Planners (via Create):
//   Planner "Hunger"   goal {Hungry=false}
//     +-- EatFood        pre {HasFood}  eff {Hungry=false}  cost 1
//     +-- Forage         pre {}         eff {HasFood=true}  cost 4
//     +-- HuddleInPlace  pre {}         eff {Hungry=false}  cost 999  (fallback)
//   Planner "Defense"  goal {SafeFromThreat=true}
//     +-- FightEnemy   pre {ThreatActive, HasWeapon}  eff {SafeFromThreat=true}  cost 1
//     +-- RunAway      pre {ThreatActive}             eff {SafeFromThreat=true}  cost 3
//     +-- RemainAlert  pre {}                          eff {SafeFromThreat=true}  cost 999  (fallback)
//
// Always-valid-plan tenet (the CkGoap docs Sec. "Design tenets"):
//   HuddleInPlace and RemainAlert are the unconditional fallbacks for the
//   two Planners. They only win when no real Action chain is viable (e.g.
//   HasFood=false on Hunger; ThreatActive=false on Defense).
//
// Both Planners share the same WS entity but plan independently. Toggling
// HasFood / HasWeapon / ThreatActive on the same WS triggers replans on the
// matching Planner's root only - the unique-to-unified-model isolation.
//
// Player controls:
//   panel [P] - flip Hungry (Hunger Planner goal).
//   panel [T] - flip HasFood.
//   panel [U] - flip ThreatActive.
//   panel [V] - flip HasWeapon.
//   panel [4] - Hungry=true, HasFood=false,
//               ThreatActive=true, HasWeapon=true.
//============================================================================

USTRUCT()
struct FCk_GoapGym_Survival_Station_SpawnParams
{
    UPROPERTY()
    FTransform InitialTransform = FTransform::Identity;
}

class UCk_EntityScript_GoapGym_Survival_Station : UCk_EntityScript_GoapGym_SharedStation
{
    private FCk_Handle_Goap_Planner _Planner_Hunger;
    private FCk_Handle_Goap_Planner _Planner_Defense;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Hunger;
    private TArray<FString> _KnownLabels_Hunger;
    private TArray<TSubclassOf<UCk_GoapAction_EntityScript>> _KnownClasses_Defense;
    private TArray<FString> _KnownLabels_Defense;

    int32 Get_StationIndex() const override { return 4; }

    protected bool PrepareStation() override
    {
        if (!TryCreateWS(utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Survival"))) { return false; }
        auto HungerGoal = TArray<FCk_GoapWS_Condition_Authored>();
        HungerGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(n"Gym.Goap.WS.Survival.Hungry"), false));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.Survival.Hunger"), HungerGoal, false, _Planner_Hunger))
        { return false; }
        FCk_Handle_Goap_Action Action;
        if (!TryAddAction(0, UCk_GoapGym_Survival_EatFood, Action) ||
            !TryAddAction(0, UCk_GoapGym_Survival_Forage, Action) ||
            !TryAddAction(0, UCk_GoapGym_Survival_HuddleInPlace, Action))
        { return false; }
        auto DefenseGoal = TArray<FCk_GoapWS_Condition_Authored>();
        DefenseGoal.Add(FCk_GoapWS_Condition_Authored(
            utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.WS.Survival.SafeFromThreat"), true));
        if (!TryCreateTopPlanner(utils_gameplay_tag::ResolveGameplayTag(
                n"Gym.Goap.ActionSet.Survival.Defense"), DefenseGoal, false, _Planner_Defense))
        { return false; }
        if (!TryAddAction(1, UCk_GoapGym_Survival_FightEnemy, Action) ||
            !TryAddAction(1, UCk_GoapGym_Survival_RunAway, Action) ||
            !TryAddAction(1, UCk_GoapGym_Survival_RemainAlert, Action))
        { return false; }
        _KnownClasses_Hunger.Add(UCk_GoapGym_Survival_EatFood);       _KnownLabels_Hunger.Add("EatFood");
        _KnownClasses_Hunger.Add(UCk_GoapGym_Survival_Forage);        _KnownLabels_Hunger.Add("Forage");
        _KnownClasses_Hunger.Add(UCk_GoapGym_Survival_HuddleInPlace); _KnownLabels_Hunger.Add("HuddleInPlace");
        _KnownClasses_Defense.Add(UCk_GoapGym_Survival_FightEnemy);  _KnownLabels_Defense.Add("FightEnemy");
        _KnownClasses_Defense.Add(UCk_GoapGym_Survival_RunAway);     _KnownLabels_Defense.Add("RunAway");
        _KnownClasses_Defense.Add(UCk_GoapGym_Survival_RemainAlert); _KnownLabels_Defense.Add("RemainAlert");
        return true;
    }
    private bool ReadKey(FName Name) const
    {
        return utils_goap_world_state::Get_Value(
            _WS, utils_gameplay_tag::ResolveGameplayTag(Name));
    }
    protected bool FillStationText(FCkGoapGym_StationFrame&out Frame) const override
    {

        auto Hungry = ReadKey(n"Gym.Goap.WS.Survival.Hungry");
        auto HasFood = ReadKey(n"Gym.Goap.WS.Survival.HasFood");
        auto Threat = ReadKey(n"Gym.Goap.WS.Survival.ThreatActive");
        auto HasWeapon = ReadKey(n"Gym.Goap.WS.Survival.HasWeapon");
        auto Safe = ReadKey(n"Gym.Goap.WS.Survival.SafeFromThreat");

        auto HStatus = utils_goap_planner::Get_PlanStatus(_Planner_Hunger);
        auto HPlan = utils_goap_planner::Get_PlanClasses(_Planner_Hunger);
        auto DStatus = utils_goap_planner::Get_PlanStatus(_Planner_Defense);
        auto DPlan = utils_goap_planner::Get_PlanClasses(_Planner_Defense);

        Frame.Detail = "World State (shared by both Planners)\n"
            + f"  Hungry          {CkGoapGym_Common::Render_Bool(Hungry)}\n"
            + f"  HasFood         {CkGoapGym_Common::Render_Bool(HasFood)}\n"
            + f"  ThreatActive    {CkGoapGym_Common::Render_Bool(Threat)}\n"
            + f"  HasWeapon       {CkGoapGym_Common::Render_Bool(HasWeapon)}\n"
            + f"  SafeFromThreat  {CkGoapGym_Common::Render_Bool(Safe)}\n\n"
            + "Hunger Planner (goal Hungry=false)\n"
            + f"  Status   {CkGoapGym_Common::Format_PlanStatus(HStatus)}\n"
            + f"  Plan     {CkGoapGym_Common::Format_Plan(HPlan, _KnownClasses_Hunger, _KnownLabels_Hunger)}\n\n"
            + "Defense Planner (goal SafeFromThreat=true)\n"
            + f"  Status   {CkGoapGym_Common::Format_PlanStatus(DStatus)}\n"
            + f"  Plan     {CkGoapGym_Common::Format_Plan(DPlan, _KnownClasses_Defense, _KnownLabels_Defense)}\n\n"
            + "Control panel\n"
            + "  panel [P] / [T]  Hungry / HasFood\n"
            + "  panel [U] / [V]  ThreatActive / HasWeapon\n"
            + "  panel [4]        Reset the survival WS";


        Frame.Title = "STATION 5 / SURVIVAL DECISION";
        Frame.Footer = "Two Planners on one entity. Independent planning + replan.";
        return true;
    }
}
