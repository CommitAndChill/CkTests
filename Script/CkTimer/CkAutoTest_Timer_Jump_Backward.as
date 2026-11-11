// Language=angelscript

//============================================================================
// CK TIMER - AUTOMATION TEST: JUMP BACKWARD
//============================================================================
//
// A relative backward jump larger than the elapsed time clamps a CountUp timer
// to zero. Seed a paused timer through Request_Jump, then inspect the negative
// jump in its completion callback, before any normal elapsed-time advance.
//============================================================================

class UCk_AutoTest_Timer_Jump_Backward : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 4.0f;

    private FCk_Handle_Timer _Timer;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto LocalHandle = InHandle;

        auto Params = FCk_Timer_Spec(FCk_Time(10.0f));
        Params.Set_StartingState(ECk_Timer_State::Paused);
        Params.Set_Behavior(ECk_Timer_Behavior::PauseOnDone);
        _Timer = utils_timer::Add(LocalHandle, Params);

        auto SeedJump = FCk_Request_Timer_Jump(FCk_Time(1.0f));
        SeedJump.Set_JumpMode(ECk_RelativeAbsolute::Relative);
        utils_timer::Request_Jump(_Timer, SeedJump,
            FCk_Delegate_Request_OnCompleted(this, n"OnSeedCompleted"));
    }

    UFUNCTION()
    private void OnSeedCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        if (IsFinished()) { return; }
        if (InResult != ECk_Request_OperationResult::Succeeded || InRequestOwner != _Timer)
        {
            FinishFailure(f"Seed jump must succeed on the timer owner (got {InResult})");
            return;
        }

        auto SeedChrono = utils_timer::Get_CurrentTimerValue(_Timer);
        FCk_Time SeedGoal;
        FCk_Time SeedElapsed;
        FCk_Time SeedRemaining;
        SeedChrono.Break_Chrono(SeedGoal, SeedElapsed, SeedRemaining);
        if (SeedElapsed.Get_Seconds() != 1.0)
        {
            FinishFailure(f"Seed jump must establish exactly one second (got {SeedElapsed.Get_Seconds()})");
            return;
        }

        auto BackwardJump = FCk_Request_Timer_Jump(FCk_Time(-5.0f));
        BackwardJump.Set_JumpMode(ECk_RelativeAbsolute::Relative);
        utils_timer::Request_Jump(_Timer, BackwardJump,
            FCk_Delegate_Request_OnCompleted(this, n"OnBackwardJumpCompleted"));
    }

    UFUNCTION()
    private void OnBackwardJumpCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        if (IsFinished()) { return; }
        if (InResult != ECk_Request_OperationResult::Succeeded || InRequestOwner != _Timer)
        {
            FinishFailure(f"Backward jump must succeed on the timer owner (got {InResult})");
            return;
        }

        auto NowChrono = utils_timer::Get_CurrentTimerValue(_Timer);
        FCk_Time NowGoal;
        FCk_Time NowElapsed;
        FCk_Time NowRemaining;
        NowChrono.Break_Chrono(NowGoal, NowElapsed, NowRemaining);

        Assert_True(NowElapsed.Get_Seconds() == 0.0,
            f"Relative -5s jump from 1s must clamp elapsed to zero (got {NowElapsed.Get_Seconds()}s)");
        Assert_True(NowGoal.Get_Seconds() == 10.0, "Backward jump preserves the ten-second goal");
        Assert_True(_Timer.Get_CurrentState() == ECk_Timer_State::Paused,
            "Backward jump preserves the paused timer state");

        FinishSuccess();
    }
}
