// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: ORDER HOLDS ACROSS OWNERS AND CLASSES
//============================================================================
//
// One burst of eight ordered scripts in the default group: owners A and B
// alternating, the two ordered classes alternating every second script, one
// recorder. Every step's record must be 0..7.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_OrderingAcrossOwnersAndClasses : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private const int32 k_BurstSize = 8;

    private FCk_Handle _Recorder;
    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Recorder = InHandle;
        utils_autotest_entity_script_ordering::Add_Record(_Recorder);
        auto OwnerA = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto OwnerB = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        for (int32 Index = 0; Index < k_BurstSize; ++Index)
        {
            auto Owner = Index % 2 == 0 ? OwnerA : OwnerB;
            auto Params = FCk_AutoTest_EntityScriptOrdering_Params(Index, _Recorder);

            if (Index % 4 < 2)
            { utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered, Params, this); }
            else
            { utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_OrderedOther, Params, this); }
        }

        WaitUntil(n"Is_Begun", n"OnBegun");
    }

    UFUNCTION()
    void OnOrderingEntryConstructed(FCk_Handle_EntityScript InHandle)
    {
        utils_autotest_entity_script_ordering::On_Constructed(InHandle);
    }

    UFUNCTION()
    private void Is_Begun(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun.Num() >= k_BurstSize);
    }

    UFUNCTION()
    private void OnBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Steps = utils_autotest_entity_script_ordering::Format_Steps(_Recorder);
        Log(f"[EntityScriptOrdering] OrderingAcrossOwnersAndClasses: {Steps}");

        auto Expected = utils_autotest_entity_script_ordering::Format_Ascending(k_BurstSize);
        Assert_Equals_String(Steps, f"Construct=[{Expected}] OnConstructed=[{Expected}] BeginPlay=[{Expected}]",
            "every step runs the scripts in request order across owners and classes");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }
}
