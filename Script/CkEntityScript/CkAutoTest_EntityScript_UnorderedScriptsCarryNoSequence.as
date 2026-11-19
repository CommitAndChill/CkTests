// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: UNORDERED SCRIPTS CARRY NO SEQUENCE
//============================================================================
//
// In one frame, six unordered scripts (the default policy) and, on its own
// recorder, one ordered control script. Every unordered script constructs
// and begins play in the frame it was requested, and none carries an
// ordering sequence at its DoConstruct; the control does, which proves the
// read sees one when it is there.
//
// The sequence is read through the test-only helper
// UCk_Utils_AutoTest_EntityScriptOrdering_UE::Get_HasOrderingSequence
// (CkTests C++), called from each fixture's DoConstruct.
//
// Fixtures: CkAutoTest_EntityScript_Ordering_Common.as.
//============================================================================

class UCk_AutoTest_EntityScript_UnorderedScriptsCarryNoSequence : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 5.0f;

    private const int32 k_Count = 6;

    private FCk_Handle _Recorder;
    private FCk_Handle _ControlRecorder;
    private int64 _RequestFrame = 0;
    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _Recorder = InHandle;
        utils_autotest_entity_script_ordering::Add_Record(_Recorder);
        _ControlRecorder = utils_entity_lifetime::Request_CreateEntity(InHandle);
        utils_autotest_entity_script_ordering::Add_Record(_ControlRecorder);
        auto Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        _RequestFrame = utils_stats::Get_FrameCount();
        for (int32 Index = 0; Index < k_Count; ++Index)
        {
            utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Unordered,
                FCk_AutoTest_EntityScriptOrdering_Params(Index, _Recorder), this);
        }
        utils_autotest_entity_script_ordering::Request_Spawn(Owner, UCk_AutoTest_EntityScriptOrdering_Ordered,
            FCk_AutoTest_EntityScriptOrdering_Params(0, _ControlRecorder), this);

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
        Result.Set(utils_autotest_entity_script_ordering::Get_Record(_Recorder).Begun.Num() >= k_Count
            && utils_autotest_entity_script_ordering::Get_Record(_ControlRecorder).Begun.Num() >= 1);
    }

    UFUNCTION()
    private void OnAllBegun(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Record = utils_autotest_entity_script_ordering::Get_Record(_Recorder);
        auto Control = utils_autotest_entity_script_ordering::Get_Record(_ControlRecorder);
        Log(f"[EntityScriptOrdering] UnorderedScriptsCarryNoSequence: {utils_autotest_entity_script_ordering::Format_Steps(_Recorder)} "
            + f"sequenced=[{utils_autotest_entity_script_ordering::Format(Record.Sequenced)}] requested at {_RequestFrame}");

        Assert_Equals_Int(Record.Constructed.Num(), k_Count, "every unordered script constructs");
        Assert_Equals_Int(Record.Finished.Num(), k_Count, "every unordered script finishes construction");
        Assert_Equals_Int(Record.Begun.Num(), k_Count, "every unordered script begins play");

        for (int32 Index = 0; Index < Record.BegunFrames.Num(); ++Index)
        {
            Assert_True(Record.BegunFrames[Index] == _RequestFrame,
                f"unordered script {Record.Begun[Index]} begins play in the frame it was requested ({_RequestFrame}, began {Record.BegunFrames[Index]})");
        }

        Assert_Equals_Int(Record.Sequenced.Num(), 0, "no unordered script carries an ordering sequence");
        Assert_Equals_Int(Control.Sequenced.Num(), 1, "the ordered control script carries an ordering sequence");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired");

        FinishSuccess();
    }
}
