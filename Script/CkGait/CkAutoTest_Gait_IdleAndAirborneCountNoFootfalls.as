// Language=angelscript
// The clock never stops: at rest it idles at MinCadenceScale (0.35 x 1.6 strides/s, a dip every ~0.9 s), and airborne it
// keeps that idle cadence. Neither is a step. Both windows are negatives (the count must stay put), so each one also
// proves the clock crossed at least two dips meanwhile, and the test ends by flying to show the counter is alive.
class UCk_AutoTest_Gait_IdleAndAirborneCountNoFootfalls : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;
    private FCk_GaitAutoTestFixture _F;
    private FCk_Handle _Owner;

    private float32 _LastPhase = 0.0f;
    private float32 _UnwrappedAdvance = 0.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the test character", n"Step_Spawn");
        Add_Step_WaitUntil("character entity constructed", n"Check_EntityReady");
        Add_Step("add a gait to the resting character", n"Step_AddGait");
        Add_Step_WaitUntil("the idle clock crosses two dips", n"Check_TwoDipsCrossed", 0, 5.0f);
        Add_Step("assert no footfall at rest, then fall with ground-plane velocity", n"Step_VerifyIdleThenFall");
        Add_Step_WaitUntil("gait samples airborne", n"Check_Airborne");
        Add_Step("restart the dip window", n"Step_RestartWindow");
        Add_Step_WaitUntil("the airborne clock crosses two dips", n"Check_TwoDipsCrossed", 0, 5.0f);
        Add_Step("assert no footfall airborne, then fly at the reference speed", n"Step_VerifyAirborneThenFly");
        Add_Step_WaitUntil("a footfall is counted once moving", n"Check_Counted");
        Add_Step("finish", n"Step_Finish");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Owner = InHandle;
        _F.Spawn(this, FVector(8000.0, 30000.0, -40000.0));
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
    private void Step_AddGait(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _F.AddGait(FCk_Gait_Spec());
        Assert_Valid(_F.Gait, "gait added to the character's entity");
        _LastPhase = utils_gait::Get_Phase(_F.Gait);
        utils_timer::Create_Tick(_Owner, FCk_Delegate_Timer(this, n"OnTick"));
    }

    // Sums the wrap-aware phase advance; at 60 fps one frame turns the idle clock by ~0.06 rad, so a negative delta is a wrap.
    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished() || ck::Is_NOT_Valid(_F.Gait))
        {
            return;
        }

        const auto Phase = utils_gait::Get_Phase(_F.Gait);
        auto Delta = Phase - _LastPhase;
        if (Delta < 0.0f)
        {
            Delta += 6.2831853f;
        }

        _UnwrappedAdvance += Delta;
        _LastPhase = Phase;
    }

    // Two dips are pi apart; 3 pi of advance crosses at least two wherever the window starts. At the idle cadence that is
    // ~2.7 s, past the default 2 s wait budget, hence the 5 s windows above.
    UFUNCTION()
    private void Check_TwoDipsCrossed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_UnwrappedAdvance >= 3.0f * 3.1415927f);
    }

    UFUNCTION()
    private void Step_VerifyIdleThenFall(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(utils_gait::Get_FootfallCount(_F.Gait), 0, "the idle clock crossed dips but counted no footfall");

        // Falling with a ground-plane component: only the airborne gate stands between this and a footfall.
        _F.Character.CharacterMovement.SetMovementMode(EMovementMode::MOVE_Falling);
        _F.Character.CharacterMovement.Velocity = FVector(420.0, 0.0, -100.0);
    }

    UFUNCTION()
    private void Check_Airborne(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_gait::Get_LastMotion(_F.Gait).Get_Footing() == ECk_Gait_Footing::Airborne);
    }

    UFUNCTION()
    private void Step_RestartWindow(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _UnwrappedAdvance = 0.0f;
    }

    UFUNCTION()
    private void Step_VerifyAirborneThenFly(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(utils_gait::Get_FootfallCount(_F.Gait), 0, "the airborne clock crossed dips but counted no footfall");
        _F.Character.Fly(FVector(420.0, 0.0, 0.0));
    }

    UFUNCTION()
    private void Check_Counted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_gait::Get_FootfallCount(_F.Gait) > 0);
    }

    UFUNCTION()
    private void Step_Finish(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        FinishSuccess();
    }
}

class ACk_AutoTest_Gait_IdleAndAirborneCountNoFootfalls_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_Gait_IdleAndAirborneCountNoFootfalls;
    default _TimeoutSeconds = 12.0f;
}
