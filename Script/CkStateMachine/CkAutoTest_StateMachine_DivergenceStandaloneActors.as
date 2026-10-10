// Language=angelscript

// These two bodies support both standalone and replicated harnesses. Keep their
// existing placed actor names explicitly: the generated wrapper writer excludes
// replicated bodies, while the functional runner can execute their standalone path.

class ACk_AutoTest_StateMachine_DivergenceFirstBranch_Actor : ACk_AutoTestRunner
{
    default _TimeoutSeconds = 7.0f;
    UFUNCTION(BlueprintOverride)
    TSubclassOf<UCk_EntityScript_UE> Get_TestEntityScriptClass() const
    {
        auto Path = FSoftClassPath("/Script/Angelscript.Ck_AutoTest_StateMachine_DivergenceFirstBranch");
        TSubclassOf<UCk_EntityScript_UE> ResolvedClass;
        ResolvedClass = Path.TryLoadClass();
        return ResolvedClass;
    }
}

class ACk_AutoTest_StateMachine_DivergenceFirstBranchTimed_Actor : ACk_AutoTestRunner
{
    default _TimeoutSeconds = 7.0f;
    UFUNCTION(BlueprintOverride)
    TSubclassOf<UCk_EntityScript_UE> Get_TestEntityScriptClass() const
    {
        auto Path = FSoftClassPath("/Script/Angelscript.Ck_AutoTest_StateMachine_DivergenceFirstBranchTimed");
        TSubclassOf<UCk_EntityScript_UE> ResolvedClass;
        ResolvedClass = Path.TryLoadClass();
        return ResolvedClass;
    }
}
