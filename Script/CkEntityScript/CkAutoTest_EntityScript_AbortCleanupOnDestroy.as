// Language=angelscript

// Spawns a second instance of this same test class as an abort probe. That keeps
// the probe in the generated wrapper corpus only as part of this passing test.
USTRUCT()
struct FCk_AutoTest_AbortCleanupProbeParams
{
    UPROPERTY()
    bool ProbeInstance = false;

    UPROPERTY()
    bool DestroyBeforeCleanup = false;

    UPROPERTY()
    FCk_Handle ProbeEscapedOwner;

    UPROPERTY()
    int32 ProbeVSyncValue = 0;
}

class UCk_AutoTest_EntityScript_AbortCleanupOnDestroy : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 8.0f;

    UPROPERTY(ExposeOnSpawn)
    bool ProbeInstance = false;

    UPROPERTY(ExposeOnSpawn)
    bool DestroyBeforeCleanup = false;

    UPROPERTY(ExposeOnSpawn)
    FCk_Handle ProbeEscapedOwner;

    UPROPERTY(ExposeOnSpawn)
    int32 ProbeVSyncValue = 0;

    private FCk_Handle _ProbeOwner;
    private FCk_Handle _ProbeBody;
    private FCk_Handle _EscapedOwner;
    private int32 _OriginalVSync = 0;
    private int32 _OverrideVSync = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        if (ProbeInstance)
        {
            auto Owner = utils_entity_lifetime::Get_LifetimeOwner(InHandle);
            if (DestroyBeforeCleanup)
            { utils_entity_lifetime::Request_DestroyEntity(InHandle); }
            Track_ForCleanup(ProbeEscapedOwner);
            Set_CVarForTest(n"r.VSync", ProbeVSyncValue == 0 ? "0" : "1");
            if (DestroyBeforeCleanup)
            {
                utils_entity_tag::Add(Owner, n"AutoTest_AbortCleanup_LateProbeReady");
                return;
            }
            if (IsFinished()) { return; }
            utils_entity_tag::Add(Owner, n"AutoTest_AbortCleanup_ProbeReady");
            return; // The parent will destroy this running body without calling FinishFailure.
        }

        if (!UCk_Utils_AutoTest_UE::Get_CVarExists(n"r.VSync"))
        { FinishFailure("r.VSync is unavailable"); return; }

        _OriginalVSync = System::GetConsoleVariableIntValue("r.VSync");
        _OverrideVSync = _OriginalVSync == 0 ? 1 : 0;

        auto Transient = ck::TransientEntity();
        _EscapedOwner = utils_entity_lifetime::Request_CreateEntity(Transient);
        Track_ForCleanup(_EscapedOwner); // Safety net if setup fails; verification precedes our finish.
        _ProbeOwner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        if (ck::Is_NOT_Valid(_EscapedOwner) || ck::Is_NOT_Valid(_ProbeOwner))
        { FinishFailure("could not create the probe and escaped owners"); return; }

        auto Params = FCk_AutoTest_AbortCleanupProbeParams();
        Params.ProbeInstance = true;
        Params.ProbeEscapedOwner = _EscapedOwner;
        Params.ProbeVSyncValue = _OverrideVSync;
        auto Pending = utils_entity_script::Request_SpawnEntity(
            _ProbeOwner, UCk_AutoTest_EntityScript_AbortCleanupOnDestroy, Params);
        if (ck::Is_NOT_Valid(Pending))
        { FinishFailure("could not queue the abort probe"); return; }
        _ProbeBody = Pending.Get_EntityUnderConstruction();

        Add_Step_WaitUntil("probe registers cleanup obligations", n"Check_ProbeReady");
        Add_Step("destroy the running probe body", n"Step_Abort");
        Add_Step_WaitSeconds("allow escaped owner teardown", 0.1f);
        Add_Step("verify abort cleanup", n"Step_Verify");
        Add_Step("spawn a probe that destroys itself before taking obligations", n"Step_SpawnLateProbe");
        Add_Step_WaitUntil("late cleanup obligations were requested", n"Check_LateProbeReady");
        Add_Step_WaitSeconds("allow late obligation teardown", 0.1f);
        Add_Step("verify late obligation cleanup", n"Step_VerifyLateProbe");
        Run_Steps(InHandle);
    }

    // Deliberately overrides EndPlay without a Super call: the base cleanup is signal-bound.
    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    {
    }

    UFUNCTION()
    private void Check_ProbeReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_entity_tag::Has(_ProbeOwner, n"AutoTest_AbortCleanup_ProbeReady"));
    }

    UFUNCTION()
    private void Step_Abort(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(System::GetConsoleVariableIntValue("r.VSync"), _OverrideVSync,
            "probe moved the shared CVar before abort");
        utils_entity_lifetime::Request_DestroyEntity(_ProbeBody);
        Assert_Equals_Int(System::GetConsoleVariableIntValue("r.VSync"), _OriginalVSync,
            "begin-destroy synchronously restores the prior CVar value");
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Invalid(_ProbeBody, "aborted probe body is destroyed");
        Assert_Invalid(_EscapedOwner, "probe destroyed its tracked out-of-subtree owner");
        Assert_Equals_Int(System::GetConsoleVariableIntValue("r.VSync"), _OriginalVSync,
            "restored CVar remains stable after teardown");
    }

    UFUNCTION()
    private void Step_SpawnLateProbe(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _EscapedOwner = utils_entity_lifetime::Request_CreateEntity(ck::TransientEntity());
        Track_ForCleanup(_EscapedOwner);
        if (ck::Is_NOT_Valid(_EscapedOwner))
        { FinishFailure("could not create the late probe's escaped owner"); return; }

        auto Params = FCk_AutoTest_AbortCleanupProbeParams();
        Params.ProbeInstance = true;
        Params.DestroyBeforeCleanup = true;
        Params.ProbeEscapedOwner = _EscapedOwner;
        Params.ProbeVSyncValue = _OverrideVSync;
        auto Pending = utils_entity_script::Request_SpawnEntity(
            _ProbeOwner, UCk_AutoTest_EntityScript_AbortCleanupOnDestroy, Params);
        if (ck::Is_NOT_Valid(Pending))
        { FinishFailure("could not queue the late cleanup probe"); return; }
        _ProbeBody = Pending.Get_EntityUnderConstruction();
    }

    UFUNCTION()
    private void Check_LateProbeReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_entity_tag::Has(_ProbeOwner, n"AutoTest_AbortCleanup_LateProbeReady"));
    }

    UFUNCTION()
    private void Step_VerifyLateProbe(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Invalid(_ProbeBody, "late probe body is destroyed");
        Assert_Invalid(_EscapedOwner, "an obligation taken after begin-destroy is still drained");
        Assert_Equals_Int(System::GetConsoleVariableIntValue("r.VSync"), _OriginalVSync,
            "a CVar override taken after begin-destroy is immediately restored");
        FinishSuccess();
    }
}
