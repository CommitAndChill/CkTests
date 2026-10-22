// Language=angelscript

// Reject the entire mixed goal request, retain the accepted Ready=true goal,
// and require its precise missing-key diagnostic. Then accept an intentionally empty goal.
class UCk_AutoTest_Goap_Planner_InvalidGoal : UCk_AutoTest_Base
{
    private FCk_Handle_Goap_Planner _Planner;
    private FCk_Handle_Goap_Action _RootAction;
    private int32 _PlanCompleteCount = 0;
    private int32 _RejectedCompletionCount = 0;
    private int32 _AttemptsBeforeReject = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Local = InHandle;
        utils_transform::Add(Local, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        auto Ready = utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.Goap.ActionSet.WS.Ready");
        auto WSParams = FCk_Goap_WorldState_Spec();
        auto Keys = TArray<FGameplayTag>();
        Keys.Add(Ready);
        WSParams.Set_PreRegisteredKeys(Keys);
        auto WS = utils_goap_world_state::Create(Local,
            utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.Goap.ActionSet.WS"), WSParams);
        utils_goap_world_state::Set_Value(WS, Ready, false);

        auto InitialGoal = TArray<FCk_GoapWS_Condition_Authored>();
        InitialGoal.Add(FCk_GoapWS_Condition_Authored(Ready, true));
        auto Params = FCk_Goap_Planner_Spec(
            utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.Goap.ActionSet.Set"));
        Params.Set_WorldStateSource(WS);
        Params.Set_Goal(InitialGoal);
        Params.Set_ReplanPolicy(ECk_Goap_ReplanPolicy::Explicit);
        _Planner = utils_goap_planner::Add(Local, Params);
        Assert_Valid(_Planner, "Planner is composed");
        _RootAction = utils_goap_planner::AddAction(_Planner,
            FCk_Goap_Action_Spec(UCk_AutoTestAction_Goap_ActionSet_Root_InvalidGoal));
        Assert_Valid(_RootAction, "Ready action is composed");
        utils_goap_planner::BindTo_OnPlanComplete(_Planner,
            FCk_Delegate_Goap_OnPlanComplete(this, n"OnPlan"));
    }

    UFUNCTION()
    private void OnPlan(
        FCk_Handle_Goap_Planner InPlanner,
        FCk_Goap_Payload_OnPlanComplete InPayload)
    {
        if (IsFinished()) { return; }
        _PlanCompleteCount += 1;
        Assert_True(utils_goap_planner::Get_PlanStatus(_Planner) == ECk_GoapPlanStatus::PlanFound,
            "An accepted goal reaches PlanFound");
        auto Plan = utils_goap_planner::Get_Plan(_Planner);

        if (_PlanCompleteCount <= 2)
        {
            Assert_Equals_Int(Plan.Num(), 1, "The accepted Ready=true goal still requires its action");
            if (Plan.Num() != 1) { FinishFailure("Ready goal must retain its complete one-action plan"); return; }
            Assert_True(Plan[0] == _RootAction, "The retained goal still selects the Ready action");
        }

        if (_PlanCompleteCount == 1)
        {
            _AttemptsBeforeReject = utils_goap_planner::Get_PlanAttemptCount(_Planner);
            auto RejectedGoal = TArray<FCk_GoapWS_Condition_Authored>();
            RejectedGoal.Add(FCk_GoapWS_Condition_Authored(
                utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.Goap.ActionSet.WS.Ready"), false));
            RejectedGoal.Add(FCk_GoapWS_Condition_Authored(
                utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.Goap.ActionSet.WS.UnknownKey"), true));
            utils_goap_planner::Request_SetGoal(_Planner, RejectedGoal,
                FCk_Delegate_Request_OnCompleted(this, n"OnRejectedGoalCompleted"));
            return;
        }

        if (_PlanCompleteCount == 2)
        {
            auto EmptyGoal = TArray<FCk_GoapWS_Condition_Authored>();
            utils_goap_planner::Request_SetGoal(_Planner, EmptyGoal,
                FCk_Delegate_Request_OnCompleted(this, n"OnEmptyGoalCompleted"));
            return;
        }

        Assert_Equals_Int(_PlanCompleteCount, 3, "Only the initial, explicit retained-goal and empty-goal plans complete");
        Assert_Equals_Int(Plan.Num(), 0, "An intentionally empty goal succeeds with an empty plan");
        Assert_Equals_Int(_RejectedCompletionCount, 1, "Rejected request completes exactly once");
        FinishSuccess();
    }

    UFUNCTION()
    private void OnRejectedGoalCompleted(
        FCk_Handle InRequestOwner,
        ECk_Request_OperationResult InResult)
    {
        if (IsFinished()) { return; }
        _RejectedCompletionCount += 1;
        Assert_True(InRequestOwner == _Planner, "Rejection reports the request owner");
        Assert_True(InResult == ECk_Request_OperationResult::Failed, "An unregistered required key rejects the whole request");
        Assert_Equals_Int(utils_goap_planner::Get_PlanAttemptCount(_Planner), _AttemptsBeforeReject,
            "Rejection does not schedule a search for its valid prefix");
        Assert_Equals_Int(_PlanCompleteCount, 1, "Rejected goal emits no PlanComplete callback");
        utils_goap_planner::Request_Plan(_Planner);
    }

    UFUNCTION()
    private void OnEmptyGoalCompleted(
        FCk_Handle InRequestOwner,
        ECk_Request_OperationResult InResult)
    {
        Assert_True(InResult == ECk_Request_OperationResult::Succeeded,
            "An intentionally empty goal is admitted independently of rejected unknown keys");
    }
}

class ACk_AutoTest_Goap_Planner_InvalidGoal_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_Goap_Planner_InvalidGoal;

    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        TArray<FString> Out;
        Out.Add("[Server] Registry.Find(Cond.Get_Key()) != goap::InvalidGoapKey\nCannot set a GOAP goal with an unresolved required key [AutoTest.Goap.ActionSet.WS.UnknownKey] on [");
        return Out;
    }
}
