// Language=angelscript

//============================================================================
// CK GOAP - AUTOMATION TEST ACTION: ROOT (InvalidGoal test)
//============================================================================
//
// Supplies the unconditional Ready=true action for the accepted goal. The test
// rejects a mixed goal containing UnknownKey and verifies this action remains
// selected by an explicit replan before accepting an intentionally empty goal.
//============================================================================

class UCk_AutoTestAction_Goap_ActionSet_Root_InvalidGoal : UCk_GoapAction_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineAction()
    {
        auto _CkPerfScope = ck::ScopedStat();
        AddEffect(utils_gameplay_tag::ResolveGameplayTag(
            n"AutoTest.Goap.ActionSet.WS.Ready"), true);
        SetCost(1.0);
    }
}
