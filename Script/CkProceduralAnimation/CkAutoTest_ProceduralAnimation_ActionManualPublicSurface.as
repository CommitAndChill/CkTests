// Language=angelscript

// Public consumer example: a body-only inspection mechanism with no gait or game-specific policy.
class UCk_AutoTest_ProceduralAnimation_ActionManualPublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_Transform _Presentation;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Action;
    private FCk_ProceduralAction_Identity _Identity;
    private FCk_ProceduralAction_Receipt _CopiedReceipt;
    private int64 _ThroughSequence = 0;
    private int32 _ConsumeCompletions = 0;
    private ECk_Request_OperationResult _ConsumeResult = ECk_Request_OperationResult::Failed_NotEnqueued;
    private int32 _RecoveryCompletions = 0;
    private ECk_Request_OperationResult _RecoveryResult = ECk_Request_OperationResult::Succeeded;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _Root = utils_transform::Add(RootEntity, FTransform(FVector(141000.0, 71000.0, 1500.0)),
            ECk_Replication::DoesNotReplicate);
        auto PresentationEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        _Presentation = utils_transform::Add(PresentationEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        auto SubjectSpec = FCk_ProceduralArticulation_Spec();
        SubjectSpec.Set_Presentation(_Presentation);
        _Subject = utils_procedural_articulation::Add(_Root, SubjectSpec);
        _Player = utils_procedural_action_player::Add(_Subject, FCk_ProceduralActionPlayer_Spec());
        if (ck::IsValid(_Subject) == false || ck::IsValid(_Player) == false)
        {
            FinishFailure("Body-only articulation and player must compose through the public API");
            return;
        }

        auto Control = FCk_ProceduralAction_ControlSpec(TArray<FName>());
        Control.Set_BodyChannel(ECk_EnableDisable::Enable);
        Control.Set_InputTimeout(FCk_Time(10.0));
        _Action = utils_procedural_action::Create_Manual(Owner, _Player, Control);
        if (ck::IsValid(_Action) == false)
        {
            FinishFailure("Manual action creation must return its pending instance");
            return;
        }
        _Identity = _Action.Get_Identity();
        Assert_True(_Action.Get_State() == ECk_ProceduralAction_State::PendingAdmission,
            "Creation allocates an instance without synchronously acquiring control");
        Assert_True(_Action.Get_LastReceipt().Get_HasAcceptedPose() == false,
            "A pending action does not fabricate an accepted pose");
        const auto PendingDiagnostic = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ActionSnapshot(_Action);
        Assert_True(PendingDiagnostic.Get_Available()
            && PendingDiagnostic.Get_State() == ECk_ProceduralAction_State::PendingAdmission
            && PendingDiagnostic.Get_Watchdogs().Num() == 1
            && PendingDiagnostic.Get_Watchdogs()[0].Get_Name() == n"Acquisition",
            "Reflected action diagnostics expose the active acquisition watchdog before admission");
        Assert_True(UCk_Utils_ProceduralAnimation_Debug_UE::Get_ActionSnapshot(FCk_Handle()).Get_Available() == false,
            "Invalid handles return an explicitly unavailable diagnostic snapshot");
        Assert_True(_Subject.Get_LimbControl(n"Missing").Get_State() == ECk_ProceduralLimbControl_State::Unavailable,
            "Unknown limbs are unavailable through the reflected observation");
        utils_procedural_articulation::Request_RecoverLimbs(_Subject,
            FCk_Request_ProceduralArticulation_RecoverLimbs(TArray<FName>()),
            FCk_Delegate_Request_OnCompleted(this, n"OnRecoveryRejected"));
        Assert_True(_RecoveryCompletions == 1 && _RecoveryResult == ECk_Request_OperationResult::Failed_NotEnqueued,
            "An empty recovery request rejects atomically through the reflected completion contract");

        Add_Step_WaitUntil("manual action acquires its settled body channel", n"Check_Ready", 600);
        Add_Step("submit an inspection offset", n"Step_Submit");
        Add_Step_WaitUntil("production publication accepts the correlated pose", n"Check_Accepted", 600);
        Add_Step("observe actual presentation and consume its receipt", n"Step_Consume");
        Add_Step_WaitUntil("receipt acknowledgement completes", n"Check_Consumed", 600);
        Add_Step("verify the diagnostic copy and end control", n"Step_End");
        Add_Step_WaitUntil("the body returns and the action finishes", n"Check_Finished", 600);
        Add_Step("verify terminal result and final presentation", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void OnRecoveryRejected(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++_RecoveryCompletions;
        _RecoveryResult = InResult;
    }

    UFUNCTION()
    private void Check_Ready(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Action) && _Action.Get_State() == ECk_ProceduralAction_State::Ready);
    }

    UFUNCTION()
    private void Step_Submit(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Batch = FCk_ProceduralAction_PoseBatch();
        Batch.Set_BodyChannel(ECk_EnableDisable::Enable);
        Batch.Set_BodyOffset(FTransform(FVector(1.0, 0.0, 0.0)));
        auto Request = FCk_Request_ProceduralAction_SubmitPoseBatch(Batch);
        Request.Set_CorrelationToken(73);
        utils_procedural_action::Request_SubmitPoseBatch(_Action, Request);
    }

    UFUNCTION()
    private void Check_Accepted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Receipt = _Action.Get_LastReceipt();
        Result.Set(Receipt.Get_HasAcceptedPose() && Receipt.Get_CorrelationToken() == 73);
    }

    UFUNCTION()
    private void Step_Consume(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _CopiedReceipt = utils_procedural_action::Get_LastReceipt(_Action);
        const auto Diagnostic = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ActionSnapshot(_Action);
        const auto SubjectDiagnostic = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(_Subject);
        Assert_True(Diagnostic.Get_Available() && Diagnostic.Get_State() == ECk_ProceduralAction_State::Ready
            && Diagnostic.Get_Identity().Get_Value() == _Identity.Get_Value()
            && Diagnostic.Get_NextSampleSequence() > _CopiedReceipt.Get_Stamp().Get_SampleSequence()
            && Diagnostic.Get_ContentRevision() == 0 && Diagnostic.Get_Watchdogs().Num() == 1
            && Diagnostic.Get_Watchdogs()[0].Get_Name() == n"Input",
            "Ready manual diagnostics expose the pinned identity and input watchdog without consuming a receipt");
        Assert_True(SubjectDiagnostic.Get_Available() && SubjectDiagnostic.Get_Controllers().Num() == 1
            && SubjectDiagnostic.Get_Controllers()[0].Get_Body()
            && SubjectDiagnostic.Get_Controllers()[0].Get_ReturnState() == ECk_ProceduralLimbControl_ReturnState::None
            && SubjectDiagnostic.Get_Controllers()[0].Get_Watchdogs().Num() == 0
            && SubjectDiagnostic.Get_Contacts().Num() == 0,
            "Body-only control diagnostics do not fabricate support or an active return deadline");
        const auto Actual = utils_transform::Get_EntityCurrentTransform(_Presentation);
        const auto Accepted = _CopiedReceipt.Get_CurrentPose().Get_BodyPresentation();
        Assert_True((Actual.GetLocation() - Accepted.GetLocation()).Size() < 0.05,
            "The accepted receipt describes the actual presentation transform");
        const auto Expected = utils_transform::Get_EntityCurrentTransform(_Root).GetLocation() + FVector(1.0, 0.0, 0.0);
        Assert_True((Accepted.GetLocation() - Expected).Size() < 0.05,
            "The accepted body offset moves the mechanism by one centimetre");
        const auto Page = _Player.Read_Receipts(_Identity, 0, 16);
        const auto Retained = utils_procedural_action_player::Get_RetainedReceiptIdentities(_Player);
        Assert_True(Retained.Num() == 1
            && Retained[0].Get_Value() == _Identity.Get_Value(),
            "Diagnostic consumers can discover the retained stream without consuming it");
        Assert_True(Page.Get_Records().Num() == 1 && Page.Get_Records()[0].Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose,
            "The designated consumer receives the accepted interval once");
        _ThroughSequence = Page.Get_LastStreamSequence();
        utils_procedural_action_player::Request_ConsumeReceipts(_Player,
            FCk_Request_ProceduralActionPlayer_ConsumeReceipts(_Identity, _ThroughSequence),
            FCk_Delegate_Request_OnCompleted(this, n"OnConsumed"));
    }

    UFUNCTION()
    private void OnConsumed(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++_ConsumeCompletions;
        _ConsumeResult = InResult;
    }

    UFUNCTION()
    private void Check_Consumed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_ConsumeCompletions > 0);
    }

    UFUNCTION()
    private void Step_End(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_ConsumeCompletions == 1 && _ConsumeResult == ECk_Request_OperationResult::Succeeded,
            "Consumption completes once through the normal request delegate");
        const auto Last = _Action.Get_LastReceipt();
        Assert_True(Last.Get_HasAcceptedPose() && Last.Get_CorrelationToken() == 73,
            "Consuming the inbox preserves the last diagnostic observation");
        _Action.Request_End(FCk_Request_ProceduralAction_End());
    }

    UFUNCTION()
    private void Check_Finished(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Action.Get_State() == ECk_ProceduralAction_State::Finished);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Result = utils_procedural_action::Get_Result(_Action);
        Assert_True(Result.Get_Outcome() == ECk_ProceduralAction_Outcome::Completed
            && Result.Get_Recovery() == ECk_ProceduralAction_Recovery::Succeeded,
            "End finishes only after a successful return");
        const auto Actual = utils_transform::Get_EntityCurrentTransform(_Presentation);
        const auto Root = utils_transform::Get_EntityCurrentTransform(_Root);
        Assert_True((Actual.GetLocation() - Root.GetLocation()).Size() < 0.05,
            "The mechanism returned to its default presentation");
        const auto Page = utils_procedural_action_player::Read_Receipts(_Player, _Identity, _ThroughSequence, 16);
        Assert_True(_Player.Get_RetainedReceiptIdentities().Num() == 1,
            "A terminal stream stays discoverable while its retention period remains active");
        Assert_True(Page.Get_Terminal() && Page.Get_Records().Num() == 2
            && Page.Get_Records()[0].Get_Kind() == ECk_ProceduralAction_RecordKind::WindowsClosed
            && Page.Get_Records()[1].Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal,
            "Closure precedes terminal in the retained consumer stream");
        Assert_True(_CopiedReceipt.Get_HasAcceptedPose() && _CopiedReceipt.Get_CorrelationToken() == 73,
            "A copied observation remains valid after recovery and acknowledgement");
        _Action.Request_Release(FCk_Request_ProceduralAction_Release());
    }
}
