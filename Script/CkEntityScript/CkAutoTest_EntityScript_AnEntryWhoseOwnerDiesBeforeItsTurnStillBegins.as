// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: AN ENTRY WHOSE OWNER DIES BEFORE ITS
// TURN STILL BEGINS
//============================================================================
//
// One frame, three ordered scripts of the default group: index 0 at owner A,
// index 1 at owner B, index 2 at owner A. Script 0's DoBeginPlay requests
// owner B's destruction, before script 1's turn. That only starts B's
// destruction, which does not stop a script beginning play (an unordered
// script begins too), so script 1 begins in its turn: the BeginPlay record
// is 0,1,2. The group goes on: a fourth script (index 3, owner A) requested
// the next frame begins play. All three of the first three end play.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_AnEntryWhoseOwnerDiesBeforeItsTurnStillBegins : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    // Frames to keep watching after the three began, so any extra BeginPlay would be seen.
    private const int32 k_WatchFrames = 3;

    private FCk_Handle _Recorder;
    private FCk_Handle _OwnerA;
    private FString _FirstBegun;
    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Recorder = InHandle;
        utils_autotest_entity_script_ordering::Add_Record(_Recorder);
        _OwnerA = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto OwnerB = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        auto First = FCk_AutoTest_EntityScriptOrdering_Params(0, _Recorder);
        First.DestroyOnBeginPlay = OwnerB;

        utils_autotest_entity_script_ordering::Request_Spawn(_OwnerA, UCk_AutoTest_EntityScriptOrdering_Ordered, First, this);
        utils_autotest_entity_script_ordering::Request_Spawn(OwnerB, UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(1, _Recorder), this);
        utils_autotest_entity_script_ordering::Request_Spawn(_OwnerA, UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(2, _Recorder), this);

        WaitUntil(n"Is_ThreeBegun", n"OnThreeBegun");
    }

    UFUNCTION()
    void OnOrderingEntryConstructed(FCk_Handle_EntityScript InHandle)
    {
        utils_autotest_entity_script_ordering::On_Constructed(InHandle);
    }

    UFUNCTION()
    private void Is_ThreeBegun(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun.Num() >= 3);
    }

    // A frame settle before the next step, so the record holds every BeginPlay of the first three.
    UFUNCTION()
    private void OnThreeBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        WaitFrames(k_WatchFrames, n"OnWatched");
    }

    UFUNCTION()
    private void OnWatched(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        _FirstBegun = utils_autotest_entity_script_ordering::Format(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun);
        Log(f"[EntityScriptOrdering] AnEntryWhoseOwnerDiesBeforeItsTurnStillBegins first three: {utils_autotest_entity_script_ordering::Format_Events(_Recorder)}");

        utils_autotest_entity_script_ordering::Request_Spawn(_OwnerA, UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(3, _Recorder), this);

        WaitUntil(n"Is_FourthBegun", n"OnFourthBegun");
    }

    UFUNCTION()
    private void Is_FourthBegun(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun.Contains(3));
    }

    UFUNCTION()
    private void OnFourthBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto OwnerA = _OwnerA;
        utils_entity_lifetime::Request_DestroyEntity(OwnerA);

        WaitUntil(n"Is_FirstFourEnded", n"OnFirstFourEnded");
    }

    UFUNCTION()
    private void Is_FirstFourEnded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Ended = utils_autotest_entity_script_ordering::Get_Record(_Recorder).Ended;
        auto Result = OutResult;
        Result.Set(Ended.Contains(0) && Ended.Contains(1) && Ended.Contains(2) && Ended.Contains(3));
    }

    UFUNCTION()
    private void OnFirstFourEnded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Record = utils_autotest_entity_script_ordering::Get_Record(_Recorder);
        auto Events = utils_autotest_entity_script_ordering::Format_Events(_Recorder);
        Log(f"[EntityScriptOrdering] AnEntryWhoseOwnerDiesBeforeItsTurnStillBegins: {Events}");

        auto EndedOfFirstThree = 0;
        for (auto Index : Record.Ended)
        {
            if (Index <= 2)
            { EndedOfFirstThree++; }
        }

        Assert_Equals_String(_FirstBegun, "0,1,2", f"script 1, whose owner script 0 began destroying, begins in its turn (events: {Events})");
        Assert_True(Record.Begun.Contains(3), "the group goes on: a fourth script requested the next frame begins play");
        Assert_Equals_Int(EndedOfFirstThree, 3, f"all three of the first three end play (events: {Events})");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }
}
