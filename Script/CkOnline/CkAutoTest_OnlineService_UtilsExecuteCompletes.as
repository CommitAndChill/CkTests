// Language=angelscript

// Exercises the reflected CkOnline request and signal surface in a ticking PIE GameInstance.
class UCk_AutoTest_OnlineService_UtilsExecuteCompletes : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 8.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_OnlineService _Service;
    private FGuid _StartedGeneration;
    private bool _OwnsTestService = false;
    private int32 _SignalCount = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("start native fake service", n"Step_Start");
        Add_Step_WaitUntil("service has completed the AS request", n"Check_Completed");
        Add_Step("verify reflected result", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    {
        DoCleanup();
    }

    private void DoCleanup()
    {
        if (ck::IsValid(_Service))
        {
            _Service.UnbindFrom_OnCompleted(
                FCk_Delegate_OnlineService_Completed(this, n"OnCompleted"));
        }
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineTest_Subsystem);
        if (_OwnsTestService && ck::IsValid(Probe)
            && Probe.Get_Diagnostics().Get_Generation() == _StartedGeneration)
        { Probe.Request_Stop(); }
        _OwnsTestService = false;
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

        _StartedGeneration = Probe.Get_Diagnostics().Get_Generation();
        _OwnsTestService = true;

        _Service = Probe.Get_Service();
        if (ck::Is_NOT_Valid(_Service))
        {
            DoCleanup();
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
            DoCleanup();
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

        auto Recovery = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectIpTest_Subsystem);
        auto Probe = Subsystem::GetGameInstanceSubsystem(UCkOnlineTest_Subsystem);
        auto Gym = Subsystem::GetGameInstanceSubsystem(UCkOnlineDirectGym_Subsystem);
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        if (ck::Is_NOT_Valid(Recovery) || ck::Is_NOT_Valid(Probe) ||
            ck::Is_NOT_Valid(Gym) || ck::Is_NOT_Valid(Direct))
        {
            DoCleanup();
            FinishFailure("Online recovery test subsystem unavailable");
            return;
        }
        auto StateBefore = Recovery.Get_StateText();
        auto EndpointBefore = Recovery.Get_Endpoint();
        int32 ShutdownsBefore = Probe.Get_AdapterShutdowns();
        int32 CancelsBefore = Probe.Get_AdapterCancels();
        auto DraftBefore = Gym.Get_JoinDescriptorDraft();
        auto DirectBefore = Direct.Get_Snapshot();
        // Syntactically valid local invite only; the active fake host must reject profile start
        // before any Direct socket or Join request can be created.
        FString SyntheticInvite = "ckd:1|127.0.0.1:17901|11111111111111111111111111111111|22222222222222222222222222222222";
        Assert_False(Gym.Request_JoinInvite(SyntheticInvite),
            "valid invite cannot replace a running foreign Online service");
        Assert_False(Direct.Get_IsStarted() || Gym.Get_IsOwnedProfile(),
            "foreign-service rejection creates no Direct profile or gym ownership");
        Assert_True(Direct.Get_Snapshot().Get_Revision() == DirectBefore.Get_Revision() &&
            Gym.Get_JoinDescriptorDraft() == DraftBefore,
            "foreign-service rejection preserves Direct revision and saved invite draft");
        Assert_False(Recovery.Request_TestControlReadyLoss(),
            "ready-loss test rejects outside a bridged gameplay session");
        Assert_False(Recovery.Request_TestControlStop(),
            "control-stop test rejects outside a bridged gameplay session");
        auto After = _Service.Get_Diagnostics();
        Assert_True(After.Get_State() == ECk_OnlineService_State::Running
            && After.Get_Generation() == Diag.Get_Generation(),
            "rejected recovery actions retain the actual running service generation");
        Assert_True(After.Get_Completed() == Diag.Get_Completed()
            && After.Get_Pending() == Diag.Get_Pending() && _SignalCount == 1,
            "rejected recovery actions produce no downstream completion or pending work");
        Assert_True(Probe.Get_AdapterShutdowns() == ShutdownsBefore
            && Probe.Get_AdapterCancels() == CancelsBefore,
            "rejected recovery actions do not cancel or shut down the live adapter");
        Assert_True(Recovery.Get_StateText() == StateBefore
            && Recovery.Get_Endpoint() == EndpointBefore && !Recovery.Get_IsLobbyBridge(),
            "rejected recovery actions leave IP state and bridge unchanged");
        Assert_True(Recovery.Get_LastError().Len() > 0,
            "rejected recovery actions expose their failure to the consumer");

        DoCleanup();
        FinishSuccess();
    }
}
