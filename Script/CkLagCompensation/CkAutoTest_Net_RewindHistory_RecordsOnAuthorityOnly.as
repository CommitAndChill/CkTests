// Language=angelscript

//============================================================================
// CK REWIND HISTORY - NET AUTOMATION TEST: RECORDS ON AUTHORITY ONLY
//============================================================================
//
// Multi-world PIE (server + client). Both worlds add RewindHistory to their
// replica of the same replicated subject:
//   - The SERVER subject must accumulate frames (it rewinds shots).
//   - The CLIENT subject must record NOTHING - clients never rewind, they get
//     rewound against. Recording there would be wasted memory/CPU at best and
//     a cheat surface at worst.
//============================================================================

class UCk_AutoTest_Net_RewindHistory_RecordsOnAuthorityOnly : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 8.0f;

    private FCk_Handle _SelfHandle;
    private FCk_Handle _Subject;
    private FCk_Handle_RewindHistory _History;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        _SelfHandle = InHandle;
        if (ck::Is_NOT_Valid(_SelfHandle))
        { FinishFailure("local test entity unavailable"); return; }
        _Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(_Subject))
        { FinishFailure("replicated subject not found"); return; }
        if (!utils_transform::Has(_Subject))
        { FinishFailure("replicated subject has no Transform"); return; }

        // The test body is locally authoritative even in a client world. The
        // subject carries the real replicated authority being tested.
        bool IsClientWorld = utils_net::Get_EntityNetMode(_SelfHandle) == ECk_Net_NetModeType::Client;
        bool SubjectAuthority = utils_net::Get_HasAuthority(_Subject);
        if (IsClientWorld == SubjectAuthority)
        { FinishFailure("replicated subject authority disagrees with PIE world role"); return; }

        auto HitShapes = TArray<FCk_LagComp_HitShape>();
        HitShapes.Add(FCk_LagComp_HitShape(
            utils_gameplay_tag::ResolveGameplayTag(n"CkTests.LagComp.Body"),
            UCk_Utils_Shapes_UE::Make_Sphere(FCk_ShapeSphere_Dimensions(50.0))));

        _History = UCk_Utils_RewindHistory_UE::Add(_Subject, FCk_RewindHistory_Spec(HitShapes));
        if (ck::Is_NOT_Valid(_History))
        { FinishFailure("RewindHistory Add failed on replicated subject"); return; }
        if (FCk_Handle(_History) != _Subject)
        { FinishFailure("RewindHistory attached to the wrong entity"); return; }

        ScheduleCheck(0.5);
    }

    private void ScheduleCheck(float InDelaySeconds)
    {
        auto Params = FCk_Timer_Spec(FCk_Time(InDelaySeconds));
        Params.Set_StartingState(ECk_Timer_State::Running)
              .Set_Behavior(ECk_Timer_Behavior::StopOnDone);
        auto Timer = utils_timer::Add(_SelfHandle, Params);
        if (ck::Is_NOT_Valid(Timer))
        { FinishFailure("frame-check timer admission failed"); return; }
        Timer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnCheck"));
    }

    UFUNCTION()
    private void OnCheck(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        if (ck::Is_NOT_Valid(_SelfHandle))
        { FinishFailure("local test entity lost before frame check"); return; }
        if (ck::Is_NOT_Valid(_Subject))
        { FinishFailure("replicated subject lost before frame check"); return; }
        if (ck::Is_NOT_Valid(_History))
        { FinishFailure("RewindHistory lost before frame check"); return; }
        if (FCk_Handle(_History) != _Subject)
        { FinishFailure("RewindHistory subject changed before frame check"); return; }

        bool IsClientWorld = utils_net::Get_EntityNetMode(_SelfHandle) == ECk_Net_NetModeType::Client;
        bool SubjectAuthority = utils_net::Get_HasAuthority(_Subject);
        if (IsClientWorld == SubjectAuthority)
        { FinishFailure("replicated subject authority changed or disagrees with PIE world role"); return; }

        auto FrameCount = _History.Get_RecordedFrameCount();
        if (SubjectAuthority)
        {
            Assert_True(FrameCount >= 2,
                f"Server subject should have recorded several frames (got {FrameCount})");
        }
        else
        {
            Assert_Equals_Int(FrameCount, 0,
                "Client subject must not record hitbox history");
        }

        FinishSuccess();
    }
}
