// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: A CUSTOM GROUP WITHOUT A TAG IS ORDERED IN THE DEFAULT GROUP
//============================================================================
//
// Invalid input: a class whose ordering policy is CustomGroup but names no
// _OrderingGroup. Its request ensures and is then ordered in the default group.
// In one frame, four scripts at one owner, alternating that class (indices
// 0, 2) with the valid default-group class (indices 1, 3). The ensure count
// rises, every entry is stamped with the default group (the empty tag), and
// every step's record is 0,1,2,3: the invalid class takes its turn in the
// default group's sequence beside the valid one, and nothing crashes.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_ACustomGroupWithoutATagIsOrderedInTheDefaultGroup : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private const int32 k_Count = 4;

    private FCk_Handle _Recorder;
    private int32 _EnsuresBefore = 0;
    private int32 _EnsuresAfterRequests = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Recorder = InHandle;
        utils_autotest_entity_script_ordering::Add_Record(_Recorder);
        auto Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        for (int32 Index = 0; Index < k_Count; ++Index)
        {
            auto Params = FCk_AutoTest_EntityScriptOrdering_Params(Index, _Recorder);
            if (Index % 2 == 0)
            { utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_GroupMissing, Params, this); }
            else
            { utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered, Params, this); }
        }
        _EnsuresAfterRequests = utils_ensure::Get_EnsureCount();

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
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun.Num() >= k_Count);
    }

    UFUNCTION()
    private void OnAllBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Steps = utils_autotest_entity_script_ordering::Format_Steps(_Recorder);
        auto Events = utils_autotest_entity_script_ordering::Format_Events(_Recorder);
        Log(f"[EntityScriptOrdering] ACustomGroupWithoutATagIsOrderedInTheDefaultGroup: {Steps} events: {Events}");

        Assert_True(_EnsuresAfterRequests > _EnsuresBefore,
            f"requesting a CustomGroup script that names no group ensured ({_EnsuresAfterRequests - _EnsuresBefore})");

        auto Expected = utils_autotest_entity_script_ordering::Format_Ascending(k_Count);
        Assert_Equals_String(Steps, f"Construct=[{Expected}] OnConstructed=[{Expected}] BeginPlay=[{Expected}]",
            f"both classes run every step in one default-group order (events: {Events})");

        for (int32 Index = 0; Index < k_Count; ++Index)
        {
            auto Group = FGameplayTag();
            int64 Sequence = 0;
            Assert_True(utils_autotest_entity_script_ordering::TryGet_Stamp(_Recorder, Index, Group, Sequence),
                f"entry {Index} is stamped");
            Assert_False(Group.IsValid(), f"entry {Index} is in the default group ({Group.ToString()})");
        }

        FinishSuccess();
    }
}

class ACk_AutoTest_EntityScript_ACustomGroupWithoutATagIsOrderedInTheDefaultGroup_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_EntityScript_ACustomGroupWithoutATagIsOrderedInTheDefaultGroup;
    default _TimeoutSeconds = 5.0f;

    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_ExpectedLogErrors() const
    {
        TArray<FString> Errors;
        Errors.Add("is ordered in a custom group but names no _OrderingGroup");
        return Errors;
    }
}
