// Language=angelscript

// A single public-surface test runs two independent negative actions on one body-only subject.
class UCk_AutoTest_ProceduralAnimation_ActionNegativePublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_Transform _Presentation;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Rejected;
    private FCk_Handle_ProceduralAction _TimedOut;
    private FCk_ProceduralAction_Identity _RejectedIdentity;
    private FCk_ProceduralAction_Identity _TimedOutIdentity;
    private FVector _InitialPresentation;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _Root = utils_transform::Add(RootEntity,
            FTransform(FVector(144000.0, 71000.0, 1500.0)),
            ECk_Replication::DoesNotReplicate);
        auto PresentationEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        _Presentation = utils_transform::Add(PresentationEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        auto SubjectSpec = FCk_ProceduralArticulation_Spec();
        SubjectSpec.Set_Presentation(_Presentation);
        _Subject = utils_procedural_articulation::Add(_Root, SubjectSpec);
        _Player = utils_procedural_action_player::Add(_Subject,
            FCk_ProceduralActionPlayer_Spec());
        if (ck::IsValid(_Root) == false || ck::IsValid(_Presentation) == false
            || ck::IsValid(_Subject) == false || ck::IsValid(_Player) == false)
        {
            FinishFailure("Negative public-surface subject and player must compose");
            return;
        }

        Add_Step_WaitUntil("body-only subject settles", n"Check_Settled", 600);
        Add_Step("reject a loaded limb clip without its named binding", n"Step_RejectBinding");
        Add_Step("verify atomic rejection without pose or claim", n"Step_VerifyRejected");
        Add_Step("create a short-lived manual body action", n"Step_CreateManual");
        Add_Step_WaitUntil("manual body action acquires", n"Check_ManualReady", 600);
        Add_Step_WaitUntil("manual action times out and returns", n"Check_ManualFinished", 600);
        Add_Step("verify failed timeout result, release, and terminal", n"Step_VerifyTimeout");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Subject.Get_SettledRevision() > 0);
    }

    UFUNCTION()
    private void Step_RejectBinding(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _InitialPresentation = utils_transform::Get_EntityCurrentTransform(_Presentation).GetLocation();
        auto Clip = Cast<UCk_ProceduralAction_Data>(LoadObject(this,
            "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA"));
        if (Clip == nullptr)
        {
            FinishFailure("Saved LimbNod clip must exist for binding rejection");
            return;
        }
        const auto Validation = utils_procedural_action::Validate_Data(Clip);
        Assert_True(Validation.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The negative case uses valid content so failure identifies missing binding");

        auto Owner = InHandle;
        // LimbNod requires slot Nod, but this body-only subject has no limb and no slot binding.
        _Rejected = utils_procedural_action::Create_FromLoadedClip(Owner, _Player,
            FCk_ProceduralAction_LoadedPlaySpec(Clip));
        if (ck::IsValid(_Rejected) == false)
        {
            FinishFailure("Binding rejection must retain a typed terminal action instance");
            return;
        }
        _RejectedIdentity = _Rejected.Get_Identity();
    }

    UFUNCTION()
    private void Step_VerifyRejected(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto ActionResult = _Rejected.Get_Result();
        Assert_True(_Rejected.Get_State() == ECk_ProceduralAction_State::Finished
            && ActionResult.Get_Outcome() == ECk_ProceduralAction_Outcome::Rejected
            && ActionResult.Get_Recovery() == ECk_ProceduralAction_Recovery::NotRequired
            && ActionResult.Get_Reason() == n"ClipBindingsRejected",
            "Missing loaded-clip binding rejects before control acquisition");
        const auto Page = _Player.Read_Receipts(_RejectedIdentity, 0, 16);
        bool HasAcceptedPose = false;
        for (auto Record : Page.Get_Records())
        {
            HasAcceptedPose = HasAcceptedPose
                || Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose;
        }
        const auto Control = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(_Subject);
        Assert_True(Page.Get_Terminal() && HasAcceptedPose == false
            && _Rejected.Get_LastReceipt().Get_HasAcceptedPose() == false
            && Control.Get_Available() && Control.Get_Controllers().Num() == 0,
            "Invalid binding publishes only terminal history and acquires no claim");
        Assert_True((utils_transform::Get_EntityCurrentTransform(_Presentation).GetLocation()
            - _InitialPresentation).Size() < 0.05,
            "Invalid binding cannot move the actual presentation");
    }

    UFUNCTION()
    private void Step_CreateManual(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Control = FCk_ProceduralAction_ControlSpec(TArray<FName>());
        Control.Set_BodyChannel(ECk_EnableDisable::Enable);
        Control.Set_InputTimeout(FCk_Time(2.0));
        auto Owner = InHandle;
        _TimedOut = utils_procedural_action::Create_Manual(Owner, _Player, Control);
        if (ck::IsValid(_TimedOut) == false)
        {
            FinishFailure("Manual timeout test must reserve an action instance");
            return;
        }
        _TimedOutIdentity = _TimedOut.Get_Identity();
    }

    UFUNCTION()
    private void Check_ManualReady(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_TimedOut)
            && _TimedOut.Get_State() == ECk_ProceduralAction_State::Ready);
    }

    UFUNCTION()
    private void Check_ManualFinished(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        if (ck::IsValid(_TimedOut) == false
            || _TimedOut.Get_State() != ECk_ProceduralAction_State::Finished)
        {
            Result.Set(false);
            return;
        }
        const auto Page = _Player.Read_Receipts(_TimedOutIdentity, 0, 16);
        Result.Set(Page.Get_Terminal());
    }

    UFUNCTION()
    private void Step_VerifyTimeout(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto ActionResult = _TimedOut.Get_Result();
        Assert_True(ActionResult.Get_Outcome() == ECk_ProceduralAction_Outcome::InputTimeout
            && ActionResult.Get_Recovery() == ECk_ProceduralAction_Recovery::Succeeded
            && ActionResult.Get_Reason() == n"InputTimeout",
            "No further submissions produce a typed InputTimeout after finite recovery");
        const auto Page = _Player.Read_Receipts(_TimedOutIdentity, 0, 16);
        Assert_True(Page.Get_Terminal() && Page.Get_Records().Num() == 2
            && Page.Get_Records()[0].Get_Kind() == ECk_ProceduralAction_RecordKind::WindowsClosed
            && Page.Get_Records()[1].Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal,
            "Input timeout closes windows before publishing its terminal record");
        const auto Control = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(_Subject);
        Assert_True(Control.Get_Available() && Control.Get_Controllers().Num() == 0,
            "Timed-out action has returned and released its body claim");
        Assert_True((utils_transform::Get_EntityCurrentTransform(_Presentation).GetLocation()
            - _InitialPresentation).Size() < 0.05,
            "No submitted pose caused actual presentation movement");
        _Rejected.Request_Release(FCk_Request_ProceduralAction_Release());
        _TimedOut.Request_Release(FCk_Request_ProceduralAction_Release());
    }
}
