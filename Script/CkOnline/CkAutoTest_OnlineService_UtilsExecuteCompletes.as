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
    private bool _OwnsDirectTestProfile = false;
    private FGuid _DirectGeneration;
    private int32 _DirectCompletionBefore = 0;
    private FCk_OnlineDirect_Snapshot _BeforeRenewal;
    private FCk_OnlineDirect_ReadyLobbyBinding _BeforeRenewalBinding;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("start native fake service", n"Step_Start");
        Add_Step_WaitUntil("service has completed the AS request", n"Check_Completed");
        Add_Step("verify reflected result", n"Step_Verify");
        Add_Step("start real reflected Direct host", n"Step_StartDirect");
        Add_Step_WaitUntil("Direct host completes", n"Check_DirectCompleted");
        Add_Step("mark real Direct host ready", n"Step_ReadyDirect");
        Add_Step_WaitUntil("Direct ready completes", n"Check_DirectCompleted");
        Add_Step("renew invite through reflected action", n"Step_RenewDirect");
        Add_Step_WaitUntil("Direct renewal completes", n"Check_DirectCompleted");
        Add_Step("verify reflected invite renewal", n"Step_VerifyDirect");
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
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        if (_OwnsDirectTestProfile && ck::IsValid(Direct) && Direct.Get_IsStarted()
            && Direct.Get_Snapshot().Get_ServiceGeneration() == _DirectGeneration)
        { Direct.Request_Stop(); }
        _OwnsDirectTestProfile = false;
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
        Assert_False(Gym.Request_RenewInvite(),
            "invite renewal cannot replace or mutate a running foreign Online service");
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
    }

    UFUNCTION()
    private void Step_StartDirect(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        if (ck::Is_NOT_Valid(Direct) || Direct.Get_IsStarted())
        {
            FinishFailure("AS Direct renewal needs an unused Direct facade");
            return;
        }
        auto Spec = FCk_OnlineDirect_Spec();
        Spec.Set_Name("InviteAsHost");
        Spec.Set_Port(17904);
        if (!Direct.Request_Start(Spec))
        {
            FinishFailure("AS Direct profile could not start after owned fake cleanup");
            return;
        }
        _DirectGeneration = Direct.Get_Snapshot().Get_ServiceGeneration();
        _OwnsDirectTestProfile = true;
        _DirectCompletionBefore = Direct.Get_CompletedCount();
        if (!Direct.Request(ECk_OnlineDirect_Action::Host, "", false))
        {
            DoCleanup();
            FinishFailure("AS Direct Host request was not admitted");
        }
    }

    UFUNCTION()
    private void Check_DirectCompleted(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        auto Res = OutResult;
        Res.Set(ck::IsValid(Direct) && Direct.Get_IsStarted()
            && Direct.Get_Snapshot().Get_ServiceGeneration() == _DirectGeneration
            && Direct.Get_CompletedCount() > _DirectCompletionBefore);
    }

    UFUNCTION()
    private void Step_ReadyDirect(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        if (ck::Is_NOT_Valid(Direct) || !Direct.Get_IsStarted()
            || Direct.Get_Snapshot().Get_ServiceGeneration() != _DirectGeneration
            || Direct.Get_LastCompletion().Get_Result() != ECk_OnlineDirect_Result::Succeeded
            || Direct.Get_Snapshot().Get_State() != ECk_OnlineDirect_State::Hosting)
        {
            DoCleanup();
            FinishFailure("AS Direct Host failed; verify port 17904 is available");
            return;
        }
        Assert_True(Direct.Get_CompletedCount() == _DirectCompletionBefore + 1,
            "reflected Host has one terminal completion");
        FCk_OnlineDirect_ReadyLobbyBinding NotReady;
        Assert_False(utils_online_direct::TryCaptureReadyLobby(Direct.Get_Snapshot(), true, 1, NotReady),
            "public binding rejects the actual unready host");
        Assert_True(!NotReady.Get_Generation().IsValid() && NotReady.Get_Members().Num() == 0,
            "unready capture publishes no partial binding");
        _DirectCompletionBefore = Direct.Get_CompletedCount();
        if (!Direct.Request(ECk_OnlineDirect_Action::SetReady, "", true))
        {
            DoCleanup();
            FinishFailure("AS Direct SetReady request was not admitted");
        }
    }

    UFUNCTION()
    private void Step_RenewDirect(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        if (ck::Is_NOT_Valid(Direct) || !Direct.Get_IsStarted()
            || Direct.Get_Snapshot().Get_ServiceGeneration() != _DirectGeneration
            || Direct.Get_LastCompletion().Get_Result() != ECk_OnlineDirect_Result::Succeeded)
        {
            DoCleanup();
            FinishFailure("AS Direct SetReady failed before renewal");
            return;
        }
        _BeforeRenewal = Direct.Get_Snapshot();
        auto Members = _BeforeRenewal.Get_Members();
        if (Members.Num() != 1 || !Members[0].Get_Ready())
        {
            DoCleanup();
            FinishFailure("AS Direct host did not publish its actual ready member");
            return;
        }
        Assert_True(Direct.Get_CompletedCount() == _DirectCompletionBefore + 1,
            "reflected SetReady has one terminal completion");
        if (!utils_online_direct::TryCaptureReadyLobby(_BeforeRenewal, true, 1, _BeforeRenewalBinding))
        {
            DoCleanup();
            FinishFailure("public binding could not capture the actual single ready host");
            return;
        }
        Assert_True(utils_online_direct::MatchesReadyLobby(_BeforeRenewalBinding, _BeforeRenewal),
            "reflected binding matches its live source");
        auto Rejected = _BeforeRenewalBinding;
        Assert_False(utils_online_direct::TryCaptureReadyLobby(_BeforeRenewal, true, 2, Rejected),
            "caller-selected two-member policy rejects the actual single host");
        Assert_True(!Rejected.Get_Generation().IsValid() && !Rejected.Get_Epoch().IsValid()
            && !Rejected.Get_LocalMemberId().IsValid() && Rejected.Get_HostAddress().Len() == 0
            && Rejected.Get_Members().Num() == 0 && !Rejected.Get_Hosting(),
            "rejected reflected capture clears the entire prior binding");
        FString Canonical = "sentinel";
        FString Endpoint = f"{_BeforeRenewalBinding.Get_HostAddress()}:23456";
        Assert_True(utils_online_direct::TryParseGameplayEndpoint(Endpoint, Canonical) && Canonical == Endpoint,
            "reflected gameplay parser returns the actual canonical target");
        Assert_True(utils_online_direct::TryParseReadyLobbyEndpoint(Endpoint, _BeforeRenewalBinding, Canonical)
            && Canonical == Endpoint,
            "reflected ready binding admits a caller-selected gameplay port");
        Assert_False(utils_online_direct::TryParseReadyLobbyEndpoint("127.0.0.2:23456", _BeforeRenewalBinding, Canonical),
            "reflected ready binding rejects a different gameplay host");
        Assert_True(Canonical.Len() == 0, "rejected endpoint clears prior output");
        Assert_False(utils_online_direct::TryParseReadyLobbyEndpoint(Endpoint, Rejected, Canonical),
            "default binding rejects a gameplay endpoint");
        Assert_True(Canonical.Len() == 0 && Direct.Get_CompletedCount() == _DirectCompletionBefore + 1,
            "pure public guards neither retain partial output nor enqueue an operation");
        _DirectCompletionBefore = Direct.Get_CompletedCount();
        if (!Direct.Request(ECk_OnlineDirect_Action::RenewInvite, "", false))
        {
            DoCleanup();
            FinishFailure("AS Direct RenewInvite request was not admitted");
        }
    }

    UFUNCTION()
    private void Step_VerifyDirect(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Direct = Subsystem::GetGameInstanceSubsystem(UCk_OnlineDirect_Subsystem_UE);
        if (ck::Is_NOT_Valid(Direct) || !Direct.Get_IsStarted()
            || Direct.Get_Snapshot().Get_ServiceGeneration() != _DirectGeneration)
        {
            DoCleanup();
            FinishFailure("AS Direct ownership changed before renewal readback");
            return;
        }
        auto After = Direct.Get_Snapshot();
        Assert_True(utils_online_direct::MatchesReadyLobby(_BeforeRenewalBinding, After),
            "public ready binding survives actual invite renewal");
        Assert_True(Direct.Get_LastCompletion().Get_Result() == ECk_OnlineDirect_Result::Succeeded
            && Direct.Get_CompletedCount() == _DirectCompletionBefore + 1,
            "reflected RenewInvite succeeds with one terminal completion");
        Assert_True(After.Get_JoinDescriptor().Len() > 0
            && After.Get_JoinDescriptor() != _BeforeRenewal.Get_JoinDescriptor(),
            "reflected renewal replaces the host invite without logging its secret");
        Assert_True(After.Get_State() == ECk_OnlineDirect_State::Hosting
            && After.Get_ServiceGeneration() == _BeforeRenewal.Get_ServiceGeneration()
            && After.Get_LobbyEpoch() == _BeforeRenewal.Get_LobbyEpoch()
            && After.Get_LocalMemberId() == _BeforeRenewal.Get_LocalMemberId()
            && After.Get_HostAddress() == _BeforeRenewal.Get_HostAddress()
            && After.Get_Revision() == _BeforeRenewal.Get_Revision() + 1,
            "reflected renewal preserves the live lobby and publishes one revision");
        auto Members = After.Get_Members();
        Assert_True(Members.Num() == 1 && After.Get_Messages().Num() == 0,
            "reflected renewal preserves roster and transcript counts");
        if (Members.Num() == 1)
        {
            Assert_True(Members[0].Get_Id() == _BeforeRenewal.Get_LocalMemberId()
                && Members[0].Get_Ready(),
                "reflected renewal preserves the existing ready member");
        }
        DoCleanup();
        FinishSuccess();
    }
}
