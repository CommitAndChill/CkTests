// Language=angelscript

//============================================================================
// CK CUE - NET AUTOMATION TEST: A CLIENT'S CUES BEGIN PLAY ON THE AUTHORITY IN THE ORDER SENT
//============================================================================
//
// The client, once the subject is ready on it, fires one burst of six ordered
// Reliable ServerOnly cues at the subject, indices 0-5. Each cue records on
// its owner, so the server's subject holds the server's record. The server's
// record must be 0..5; the client records nothing (a ServerOnly cue does not
// run on the machine that sent it).
//
// Each machine logs the frame it sent or completed its record on
// ([CueOrdering-...]).
//
// Fixtures: CkAutoTest_Cue_Ordering_Common.as.
// Surface: Ck.Cue.Net.AS_Cue_ClientCuesReachTheAuthorityInOrder
//============================================================================

class UCk_AutoTest_Net_Cue_ClientCuesReachTheAuthorityInOrder : UCk_AutoTest_NetBase
{
    private const int32 k_BurstSize = 6;
    // A poll budget above any frame budget: the harness polls twice a frame.
    private const int32 k_PollBudget = 3000;
    private const float32 k_ClientWatchSeconds = 4.0f;
    private const float32 k_ServerHoldSeconds = 2.0f;

    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        if (System::IsServer())
        {
            WaitUntil(n"Is_ServerRecorded", n"OnServerRecorded", k_PollBudget);
            return;
        }

        WaitUntil(n"Is_SubjectReady", n"OnClientSubjectReady", k_PollBudget);
    }

    //------------------------------------------------------------------------
    // Server
    //------------------------------------------------------------------------

    UFUNCTION()
    private void Is_ServerRecorded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_autotest_cue_ordering::Get_Begun(Get_SubjectEntity()).Num() >= k_BurstSize);
    }

    UFUNCTION()
    private void OnServerRecorded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Order = utils_autotest_cue_ordering::Format(utils_autotest_cue_ordering::Get_Begun(Get_SubjectEntity()));
        Log(f"[CueOrdering-server] recorded {Order} at frame {utils_stats::Get_FrameCount()}");

        Assert_Equals_String(Order, utils_autotest_cue_ordering::Format_Ascending(k_BurstSize),
            "a client's cues begin play on the authority in the order the client sent them");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired on the server");

        WaitSeconds(k_ServerHoldSeconds, n"OnServerHeld");
    }

    UFUNCTION()
    private void OnServerHeld(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        FinishSuccess();
    }

    //------------------------------------------------------------------------
    // Client
    //------------------------------------------------------------------------

    UFUNCTION()
    private void Is_SubjectReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(Get_SubjectEntity()));
    }

    UFUNCTION()
    private void OnClientSubjectReady(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Subject = Get_SubjectEntity();
        for (int32 Index = 0; Index < k_BurstSize; ++Index)
        { utils_autotest_cue_ordering::Fire_Net(Subject, Index, ECk_Cue_MulticastPolicy::ServerOnly); }

        Log(f"[CueOrdering-client] sent six at frame {utils_stats::Get_FrameCount()}");

        // Nothing is expected to happen here: a wall-clock watch, then the absence is asserted.
        WaitSeconds(k_ClientWatchSeconds, n"OnClientWatched");
    }

    UFUNCTION()
    private void OnClientWatched(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Begun = utils_autotest_cue_ordering::Get_Begun(Get_SubjectEntity());
        Log(f"[CueOrdering-client] recorded '{utils_autotest_cue_ordering::Format(Begun)}'");

        Assert_Equals_Int(Begun.Num(), 0, "a ServerOnly cue does not begin play on the client that sent it");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "no ensure fired on the client");

        FinishSuccess();
    }
}
