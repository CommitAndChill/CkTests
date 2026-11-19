// Language=angelscript

//============================================================================
// CK CUE - NET AUTOMATION TEST: THE AUTHORITY'S CUES BEGIN PLAY IN THE ORDER SENT ON EVERY MACHINE
//============================================================================
//
// The server fires one burst of six Reliable ServerAndAllClients ordered cues at the
// subject, indices 0-5. Each cue records on its owner, so each machine's
// subject holds that machine's record. Both records must be 0..5.
//
// Fixtures: CkAutoTest_Cue_Ordering_Common.as.
// Surface: Ck.Cue.Net.AS_Cue_AuthorityCuesReachTheClientInOrder
//============================================================================

class UCk_AutoTest_Net_Cue_AuthorityCuesReachTheClientInOrder : UCk_AutoTest_NetBase
{
    private const int32 k_BurstSize = 6;
    // A poll budget above any frame budget: the harness polls twice a frame.
    private const int32 k_PollBudget = 3000;
    private const float32 k_ServerHoldSeconds = 2.0f;

    private int32 _EnsuresBefore = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _EnsuresBefore = utils_ensure::Get_EnsureCount();

        if (System::IsServer())
        {
            auto Subject = Get_SubjectEntity();
            if (ck::Is_NOT_Valid(Subject))
            { FinishFailure("subject entity not found"); return; }

            for (int32 Index = 0; Index < k_BurstSize; ++Index)
            { utils_autotest_cue_ordering::Fire_Net(Subject, Index, ECk_Cue_MulticastPolicy::ServerAndAllClients); }

            Log(f"[CueOrdering-server] sent six at frame {utils_stats::Get_FrameCount()}");
        }

        WaitUntil(n"Is_Recorded", n"OnRecorded", k_PollBudget);
    }

    UFUNCTION()
    private void Is_Recorded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_autotest_cue_ordering::Get_Begun(Get_SubjectEntity()).Num() >= k_BurstSize);
    }

    UFUNCTION()
    private void OnRecorded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Machine = System::IsServer() ? "server" : "client";
        auto Order = utils_autotest_cue_ordering::Format(utils_autotest_cue_ordering::Get_Begun(Get_SubjectEntity()));
        Log(f"[CueOrdering-{Machine}] recorded {Order} at frame {utils_stats::Get_FrameCount()}");

        Assert_Equals_String(Order, utils_autotest_cue_ordering::Format_Ascending(k_BurstSize),
            f"the authority's cues begin play on the {Machine} in the order sent");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, f"no ensure fired on the {Machine}");

        if (System::IsServer())
        {
            WaitSeconds(k_ServerHoldSeconds, n"OnServerHeld");
            return;
        }

        FinishSuccess();
    }

    UFUNCTION()
    private void OnServerHeld(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        FinishSuccess();
    }
}
