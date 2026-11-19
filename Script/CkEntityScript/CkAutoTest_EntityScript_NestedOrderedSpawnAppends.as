// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: A NESTED ORDERED SPAWN APPENDS
//============================================================================
//
// In one frame, two ordered scripts of the default group: P (index 0), whose
// DoConstruct requests an ordered child K (index 2, owned by P), then Q
// (index 1), whose DoConstruct requests an ordered child L (index 3, owned by
// Q). K takes the next sequence of the group when P's Construct requests it,
// so it sits after Q; L is requested after K, so it sits after K. The two
// children reach the spawn step in one later pass, where only their sequences
// order them: every step's record is 0,1,2,3, and K and L begin play in the
// same frame as Q.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_NestedOrderedSpawnAppends : UCk_AutoTest_Base
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

        auto Parent = FCk_AutoTest_EntityScriptOrdering_Params(0, _Recorder);
        Parent.ChildIndex = 2;

        auto Second = FCk_AutoTest_EntityScriptOrdering_Params(1, _Recorder);
        Second.ChildIndex = 3;

        utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered, Parent, this);
        utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered, Second, this);

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
        auto Record = utils_autotest_entity_script_ordering::Get_Record(_Recorder);
        auto Steps = utils_autotest_entity_script_ordering::Format_Steps(_Recorder);
        auto Events = utils_autotest_entity_script_ordering::Format_Events(_Recorder);

        FString Frames = "";
        for (int32 Index = 0; Index < Record.BegunFrames.Num(); ++Index)
        { Frames += f"{Record.Begun[Index]}@{Record.BegunFrames[Index]} "; }
        Log(f"[EntityScriptOrdering] NestedOrderedSpawnAppends: {Steps} events: {Events} begun frames: {Frames}");

        Assert_Equals_String(utils_autotest_entity_script_ordering::Format(Record.Constructed), "0,1,2,3",
            f"P, then Q, then P's nested child K, then Q's nested child L construct (events: {Events})");
        Assert_Equals_String(utils_autotest_entity_script_ordering::Format(Record.Finished), "0,1,2,3",
            f"K finishes before L, both after Q (events: {Events})");
        Assert_Equals_String(utils_autotest_entity_script_ordering::Format(Record.Begun), "0,1,2,3",
            f"K begins play before L, both after Q (events: {Events})");

        // The nested request took the group's next sequence at request time, after Q's.
        auto Group = FGameplayTag();
        int64 QSequence = 0;
        int64 KSequence = 0;
        int64 LSequence = 0;
        Assert_True(utils_autotest_entity_script_ordering::TryGet_Stamp(_Recorder, 1, Group, QSequence), "Q is stamped");
        Assert_True(utils_autotest_entity_script_ordering::TryGet_Stamp(_Recorder, 2, Group, KSequence), "K is stamped");
        Assert_True(utils_autotest_entity_script_ordering::TryGet_Stamp(_Recorder, 3, Group, LSequence), "L is stamped");
        Assert_True(KSequence > QSequence, f"K's sequence follows Q's (Q {QSequence}, K {KSequence})");
        Assert_True(LSequence > KSequence, f"L's sequence follows K's (K {KSequence}, L {LSequence})");

        auto QFrame = Record.BegunFrames[Record.Begun.FindIndex(1)];
        auto KFrame = Record.BegunFrames[Record.Begun.FindIndex(2)];
        auto LFrame = Record.BegunFrames[Record.Begun.FindIndex(3)];
        Assert_True(KFrame == QFrame, f"K begins play in the same frame as Q (Q {QFrame}, K {KFrame})");
        Assert_True(LFrame == QFrame, f"L begins play in the same frame as Q (Q {QFrame}, L {LFrame})");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }
}
