// Language=angelscript
// Request_Reset zeroes the clock but leaves the footfall counter and the last side alone (consumers diff the counter
// against their own copy, exactly like the landing counter). Phase 0 puts the next dip at pi/2, so the first footfall
// after a reset is the left foot. The reset is issued right after a RIGHT footfall, so the kept side (Right) and the
// next side (Left) differ and neither can pass by a side that was simply never written.
class UCk_AutoTest_Gait_ResetKeepsFootfallCountAndSide : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;
    private FCk_GaitAutoTestFixture _F;
    private FCk_Handle _Owner;

    private int32 _CountBeforeReset = 0;
    private int32 _Completions = 0;
    private int32 _CountAtReset = -1;
    private ECk_Gait_Side _SideAtReset = ECk_Gait_Side::Left;
    private float32 _PhaseAtReset = -1.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the test character", n"Step_Spawn");
        Add_Step_WaitUntil("character entity constructed", n"Check_EntityReady");
        Add_Step("add a gait and fly at the reference speed", n"Step_Fly");
        Add_Step_WaitUntil("a right footfall has been counted", n"Check_RightFootfall");
        Add_Step("reset the gait", n"Step_Reset");
        Add_Step_WaitUntil("Reset drained", n"Check_ResetCompleted");
        Add_Step("assert the counter and side survived the reset", n"Step_VerifyKept");
        Add_Step_WaitUntil("the next footfall is counted", n"Check_NextFootfall");
        Add_Step("assert it is the left foot", n"Step_VerifyLeft");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Owner = InHandle;
        _F.Spawn(this, FVector(9000.0, 30000.0, -40000.0));
        if (ck::Is_NOT_Valid(_F.Character))
        {
            return;
        }

        utils_pending_entity_script::Promise_OnConstructed(_F.Character.PendingEntity,
            FCk_Delegate_EntityScript_Constructed(this, n"OnEntityReady"));
    }

    UFUNCTION()
    private void OnEntityReady(FCk_Handle_EntityScript InEntityScript)
    {
        if (IsFinished())
        {
            return;
        }

        _F.Ready(_Owner, InEntityScript);
    }

    UFUNCTION()
    private void Check_EntityReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(ck::IsValid(_F.Entity) && ck::IsValid(_F.Root));
    }

    UFUNCTION()
    private void Step_Fly(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _F.AddGait(FCk_Gait_Spec());
        Assert_Valid(_F.Gait, "gait added to the character's entity");
        _F.Character.Fly(FVector(420.0, 0.0, 0.0));
    }

    UFUNCTION()
    private void Check_RightFootfall(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_gait::Get_FootfallCount(_F.Gait) >= 2 && utils_gait::Get_LastFootfallSide(_F.Gait) == ECk_Gait_Side::Right);
    }

    UFUNCTION()
    private void Step_Reset(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _CountBeforeReset = utils_gait::Get_FootfallCount(_F.Gait);
        utils_gait::Request_Reset(_F.Gait, FCk_Request_Gait_Reset(), FCk_Delegate_Request_OnCompleted(this, n"OnResetCompleted"));
    }

    // Fires inside the request drain, before this frame's Update steps the clock again (see Gait_ResetZeroesClock).
    UFUNCTION()
    private void OnResetCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _CountAtReset = utils_gait::Get_FootfallCount(_F.Gait);
        _SideAtReset = utils_gait::Get_LastFootfallSide(_F.Gait);
        _PhaseAtReset = utils_gait::Get_Phase(_F.Gait);
    }

    UFUNCTION()
    private void Check_ResetCompleted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_Completions > 0);
    }

    UFUNCTION()
    private void Step_VerifyKept(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Float(_PhaseAtReset, 0.0f, 1.0e-6f, "phase is zero after Reset");
        // A footfall may land between Step_Reset and the drain; the count must never go DOWN.
        Assert_True(_CountAtReset >= _CountBeforeReset,
            f"Reset kept the footfall count ({_CountAtReset} at reset vs {_CountBeforeReset} before)");
        Assert_True(_CountAtReset > 0, "the count is not zeroed");
        if (_CountAtReset == _CountBeforeReset)
        {
            Assert_True(_SideAtReset == ECk_Gait_Side::Right, "Reset kept the last footfall side (Right)");
        }
        _CountBeforeReset = _CountAtReset;
    }

    UFUNCTION()
    private void Check_NextFootfall(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_gait::Get_FootfallCount(_F.Gait) > _CountBeforeReset);
    }

    UFUNCTION()
    private void Step_VerifyLeft(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(utils_gait::Get_FootfallCount(_F.Gait), _CountBeforeReset + 1, "one footfall after the reset");
        Assert_True(utils_gait::Get_LastFootfallSide(_F.Gait) == ECk_Gait_Side::Left,
            "from phase 0 the first dip is pi/2, the left foot");
        FinishSuccess();
    }
}

class ACk_AutoTest_Gait_ResetKeepsFootfallCountAndSide_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_Gait_ResetKeepsFootfallCountAndSide;
    default _TimeoutSeconds = 10.0f;
}
