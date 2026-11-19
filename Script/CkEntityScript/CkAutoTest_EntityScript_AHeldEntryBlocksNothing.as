// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: A HELD ENTRY BLOCKS NOTHING
//============================================================================
//
// In one frame, four ordered scripts of the default group, requested in this
// order: O (index 0), A (index 1, owned by O's script entity), B (index 2)
// and C (index 3). In the pass that constructs O, A's turn comes while O is
// still constructing, so A's spawn is deferred; B and C construct in that
// same pass, in order, before O finishes construction. A constructs in a
// later pass, after O has finished.
//
// So the Construct record is 0,2,3,1, B's and C's Construct come before O's
// OnConstructed in the event list and A's after it, and no ensure fires.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_AHeldEntryBlocksNothing : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private FCk_Handle _Recorder;
    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Recorder = InHandle;
        utils_autotest_entity_script_ordering::Add_Record(_Recorder);
        auto Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        auto PendingO = utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(0, _Recorder), this);
        utils_autotest_entity_script_ordering::Request_Spawn(PendingO.Get_EntityUnderConstruction(), UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(1, _Recorder), this);
        utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(2, _Recorder), this);
        utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(3, _Recorder), this);

        WaitUntil(n"Is_AllBegun", n"OnAllBegun");
    }

    UFUNCTION()
    void OnOrderingEntryConstructed(FCk_Handle_EntityScript InHandle)
    {
        utils_autotest_entity_script_ordering::On_Constructed(InHandle);
    }

    UFUNCTION()
    private void Is_AllBegun(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun.Num() >= 4);
    }

    UFUNCTION()
    private void OnAllBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Steps = utils_autotest_entity_script_ordering::Format_Steps(_Recorder);
        auto Events = utils_autotest_entity_script_ordering::Format_Events(_Recorder);
        Log(f"[EntityScriptOrdering] AHeldEntryBlocksNothing: {Steps} events: {Events}");

        auto Record = utils_autotest_entity_script_ordering::Get_Record(_Recorder);
        Assert_Equals_String(utils_autotest_entity_script_ordering::Format(Record.Constructed), "0,2,3,1",
            f"B and C construct past the held A, which constructs later (events: {Events})");

        auto OFinished = utils_autotest_entity_script_ordering::Get_EventPosition(_Recorder, "f0");
        auto AConstructed = utils_autotest_entity_script_ordering::Get_EventPosition(_Recorder, "c1");
        auto BConstructed = utils_autotest_entity_script_ordering::Get_EventPosition(_Recorder, "c2");
        auto CConstructed = utils_autotest_entity_script_ordering::Get_EventPosition(_Recorder, "c3");

        Assert_True(OFinished >= 0 && AConstructed >= 0 && BConstructed >= 0 && CConstructed >= 0,
            f"every step of O, A, B and C was recorded (events: {Events})");
        Assert_True(BConstructed < OFinished && CConstructed < OFinished,
            f"B and C construct in the pass A was held from, while its owner O is still constructing (events: {Events})");
        Assert_True(AConstructed > OFinished,
            f"A constructs only after its owner O finished construction (events: {Events})");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }
}
