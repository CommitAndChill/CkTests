// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: TWO CUSTOM GROUPS ARE EACH IN ORDER
//============================================================================
//
// In one frame, four scripts of group X and four of group Y requested
// interleaved (X0 Y0 X1 Y1 ...), each group on its own recorder. Each
// group's record of every step must be its own request order, 0..3, and the
// stamps must show two groups: every X carries group X, every Y group Y, and
// each group's sequences are contiguous although the requests interleave (one
// shared counter would leave gaps). Nothing is asserted about the order of X
// against Y: groups never constrain each other.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_TwoCustomGroupsAreEachInOrder : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private const int32 k_PerGroup = 4;

    private FCk_Handle _RecorderX;
    private FCk_Handle _RecorderY;
    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _RecorderX = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _RecorderY = utils_entity_lifetime::Request_CreateEntity(InHandle);
        utils_autotest_entity_script_ordering::Add_Record(_RecorderX);
        utils_autotest_entity_script_ordering::Add_Record(_RecorderY);
        auto Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        for (int32 Index = 0; Index < k_PerGroup; ++Index)
        {
            utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_GroupX,
                FCk_AutoTest_EntityScriptOrdering_Params(Index, _RecorderX), this);
            utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_GroupY,
                FCk_AutoTest_EntityScriptOrdering_Params(Index, _RecorderY), this);
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
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_RecorderX).Begun.Num() >= k_PerGroup
            && utils_autotest_entity_script_ordering::Get_Record(_RecorderY).Begun.Num() >= k_PerGroup);
    }

    UFUNCTION()
    private void OnBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto StepsX = utils_autotest_entity_script_ordering::Format_Steps(_RecorderX);
        auto StepsY = utils_autotest_entity_script_ordering::Format_Steps(_RecorderY);
        Log(f"[EntityScriptOrdering] TwoCustomGroupsAreEachInOrder X: {StepsX} | Y: {StepsY}");

        auto Expected = utils_autotest_entity_script_ordering::Format_Ascending(k_PerGroup);
        auto ExpectedSteps = f"Construct=[{Expected}] OnConstructed=[{Expected}] BeginPlay=[{Expected}]";
        Assert_Equals_String(StepsX, ExpectedSteps, "group X runs every step in its own request order");
        Assert_Equals_String(StepsY, ExpectedSteps, "group Y runs every step in its own request order");

        Assert_Stamps(_RecorderX, utils_autotest_entity_script_ordering::Get_GroupX(), "X");
        Assert_Stamps(_RecorderY, utils_autotest_entity_script_ordering::Get_GroupY(), "Y");
        Assert_True(utils_autotest_entity_script_ordering::Get_GroupX() != utils_autotest_entity_script_ordering::Get_GroupY(),
            "the two groups are different tags");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }

    // Every entry of the recorder carries InGroup, and the sequences of entries 0..k_PerGroup-1 are consecutive.
    private void Assert_Stamps(FCk_Handle InRecorder, FGameplayTag InGroup, const FString& InLabel)
    {
        int64 Previous = 0;
        for (int32 Index = 0; Index < k_PerGroup; ++Index)
        {
            auto Group = FGameplayTag();
            int64 Sequence = 0;
            Assert_True(utils_autotest_entity_script_ordering::TryGet_Stamp(InRecorder, Index, Group, Sequence),
                f"{InLabel}{Index} is stamped");
            Assert_True(Group == InGroup, f"{InLabel}{Index} is stamped with group {InLabel} ({Group.ToString()})");

            if (Index > 0)
            {
                Assert_True(Sequence == Previous + 1,
                    f"group {InLabel}'s sequences are contiguous ({InLabel}{Index - 1} {Previous}, {InLabel}{Index} {Sequence})");
            }
            Previous = Sequence;
        }
    }
}
