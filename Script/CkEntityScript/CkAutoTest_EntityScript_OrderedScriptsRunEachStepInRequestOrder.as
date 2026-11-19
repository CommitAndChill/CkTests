// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: ORDERED SCRIPTS RUN EACH STEP IN REQUEST ORDER
//============================================================================
//
// Three bursts of six ordered (default group) scripts at one owner, indices
// 0-5, each burst requested in one continuation once the burst before it has
// begun play. Every step's record (Construct, OnConstructed, BeginPlay) must
// be 0,1,2,3,4,5 for every burst. Bursts after the first reuse the pool
// slots the earlier ones left, so the records cover several pool histories.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_OrderedScriptsRunEachStepInRequestOrder : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private const int32 k_BurstSize = 6;
    private const int32 k_BurstCount = 3;

    private FCk_Handle _Recorder;
    private FCk_Handle _Owner;
    private int32 _BurstsFired = 0;
    private TArray<FString> _Records;
    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Recorder = InHandle;
        utils_autotest_entity_script_ordering::Add_Record(_Recorder);
        _Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        Request_Burst();
    }

    UFUNCTION()
    void OnOrderingEntryConstructed(FCk_Handle_EntityScript InHandle)
    {
        utils_autotest_entity_script_ordering::On_Constructed(InHandle);
    }

    private void Request_Burst()
    {
        utils_autotest_entity_script_ordering::Request_ClearRecord(_Recorder);
        for (int32 Index = 0; Index < k_BurstSize; ++Index)
        {
            utils_autotest_entity_script_ordering::Request_Spawn(_Owner, UCk_AutoTest_EntityScriptOrdering_Ordered,
                FCk_AutoTest_EntityScriptOrdering_Params(Index, _Recorder), this);
        }
        _BurstsFired++;

        WaitUntil(n"Is_BurstBegun", n"OnBurstBegun");
    }

    UFUNCTION()
    private void Is_BurstBegun(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun.Num() >= k_BurstSize);
    }

    UFUNCTION()
    private void OnBurstBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Steps = utils_autotest_entity_script_ordering::Format_Steps(_Recorder);
        _Records.Add(Steps);
        Log(f"[EntityScriptOrdering] OrderedScriptsRunEachStepInRequestOrder burst {_BurstsFired - 1}: {Steps}");

        if (_BurstsFired < k_BurstCount)
        {
            Request_Burst();
            return;
        }

        auto Expected = utils_autotest_entity_script_ordering::Format_Ascending(k_BurstSize);
        auto ExpectedSteps = f"Construct=[{Expected}] OnConstructed=[{Expected}] BeginPlay=[{Expected}]";
        for (int32 Burst = 0; Burst < _Records.Num(); ++Burst)
        {
            Assert_Equals_String(_Records[Burst], ExpectedSteps,
                f"burst {Burst}: every step runs the scripts in request order");
        }
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }
}
