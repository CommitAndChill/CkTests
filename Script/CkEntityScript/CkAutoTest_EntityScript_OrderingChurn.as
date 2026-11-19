// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: THE ORDER HOLDS AFTER OTHER SCRIPTS CHURN THE POOLS
//============================================================================
//
// Twice: in one frame, spawn an unordered parent whose DoConstruct spawns an
// unordered child, and three unordered scripts; two frames later request a
// burst of six ordered scripts (default group) at one owner. Both rounds'
// records of every step must be 0..5.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_OrderingChurn : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private const int32 k_BurstSize = 6;
    private const int32 k_RoundCount = 2;
    private const int32 k_PlainScriptCount = 3;
    // Two frames: by then the churn has begun play and left its pools reshuffled, and the burst lands in a later pass
    // than the churn.
    private const int32 k_ChurnFrames = 2;

    private FCk_Handle _Recorder;
    private FCk_Handle _Owner;
    private int32 _Round = 0;
    private TArray<FString> _Records;
    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Recorder = InHandle;
        utils_autotest_entity_script_ordering::Add_Record(_Recorder);
        _Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        Request_Churn();
    }

    UFUNCTION()
    void OnOrderingEntryConstructed(FCk_Handle_EntityScript InHandle)
    {
        utils_autotest_entity_script_ordering::On_Constructed(InHandle);
    }

    private void Request_Churn()
    {
        auto Owner = _Owner;
        utils_entity_script::Request_SpawnEntity(Owner, UCk_AutoTest_EntityScriptOrdering_ChurnParent, FInstancedStruct());
        for (int32 Index = 0; Index < k_PlainScriptCount; ++Index)
        { utils_entity_script::Request_SpawnEntity(Owner, UCk_AutoTest_EntityScriptOrdering_Unordered,
            FInstancedStruct::Make(FCk_AutoTest_EntityScriptOrdering_Params())); }

        WaitFrames(k_ChurnFrames, n"OnChurned");
    }

    UFUNCTION()
    private void OnChurned(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        utils_autotest_entity_script_ordering::Request_ClearRecord(_Recorder);
        for (int32 Index = 0; Index < k_BurstSize; ++Index)
        {
            utils_autotest_entity_script_ordering::Request_Spawn(_Owner, UCk_AutoTest_EntityScriptOrdering_Ordered,
                FCk_AutoTest_EntityScriptOrdering_Params(Index, _Recorder), this);
        }

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
        Log(f"[EntityScriptOrdering] OrderingChurn round {_Round}: {Steps}");
        _Round++;

        if (_Round < k_RoundCount)
        {
            Request_Churn();
            return;
        }

        auto Expected = utils_autotest_entity_script_ordering::Format_Ascending(k_BurstSize);
        auto ExpectedSteps = f"Construct=[{Expected}] OnConstructed=[{Expected}] BeginPlay=[{Expected}]";
        for (int32 Round = 0; Round < _Records.Num(); ++Round)
        {
            Assert_Equals_String(_Records[Round], ExpectedSteps,
                f"round {Round}: every step runs the ordered scripts in request order after spawn churn");
        }
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }
}
