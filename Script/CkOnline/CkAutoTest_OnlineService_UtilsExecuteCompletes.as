// Language=angelscript

// Exercises the reflected CkOnline request and signal surface in a ticking PIE GameInstance.
class UCk_AutoTest_OnlineService_UtilsExecuteCompletes : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 8.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_OnlineService _Service;
    private int32 _SignalCount = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("start native fake service", n"Step_Start");
        Add_Step_WaitUntil("service has completed the AS request", n"Check_Completed");
        Add_Step("verify reflected result", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Start(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineTest_Subsystem);
        if (ck::Is_NOT_Valid(Probe))
        {
            FinishFailure("CkOnline test GameInstance subsystem unavailable");
            return;
        }

        Probe.Request_Stop();
        Probe.Request_ResetRecord();
        if (!Probe.Request_Start())
        {
            FinishFailure("CkOnline fake service failed to start");
            return;
        }

        _Service = Probe.Get_Service();
        if (ck::Is_NOT_Valid(_Service))
        {
            FinishFailure("CkOnline did not return a service handle");
            return;
        }

        _Service.BindTo_OnCompleted(
            FCk_Delegate_OnlineService_Completed(this, n"OnCompleted"),
            ECk_Signal_BindingPolicy::FireIfPayloadInFlightThisFrame,
            ECk_Signal_PostFireBehavior::DoNothing);

        auto OperationId = Probe.Request_NewOperationId();
        if (!Probe.Request_MarkOperationImmediate(OperationId))
        {
            FinishFailure("CkOnline fake did not accept inline operation");
            return;
        }

        auto Request = FCk_Request_OnlineService_Execute(OperationId);
        Request.Set_Timeout(FCk_Time(2.0));
        _Service.Request_Execute(Request,
            FCk_Delegate_Request_OnCompleted());
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle_OnlineService InService,
        FCk_OnlineService_Completion InCompletion)
    {
        if (InCompletion.Get_Result() == ECk_OnlineService_Result::Succeeded)
        { _SignalCount++; }
    }

    UFUNCTION()
    private void Check_Completed(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_SignalCount == 1);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Diag = _Service.Get_Diagnostics();
        Assert_True(Diag.Get_State() == ECk_OnlineService_State::Running,
            "reflected service remains running after inline completion");
        Assert_True(Diag.Get_Completed() == 1 && Diag.Get_Pending() == 0,
            "exactly one completed request is visible through the handle diagnostics");
        Assert_True(_SignalCount == 1,
            "AS completion signal fired exactly once");

        _Service.UnbindFrom_OnCompleted(
            FCk_Delegate_OnlineService_Completed(this, n"OnCompleted"));
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineTest_Subsystem);
        Probe.Request_Stop();
        FinishSuccess();
    }
}
