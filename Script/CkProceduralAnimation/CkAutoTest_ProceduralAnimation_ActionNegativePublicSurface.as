// Language=angelscript

// A resident limb clip without its named binding is rejected inside Create_FromClip on a body-only subject: the action
// comes back Finished with a typed result, publishes only its terminal, claims nothing, and Request_Release destroys it.
class UCk_AutoTest_ProceduralAnimation_ActionNegativePublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 12.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_Transform _Presentation;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Rejected;
    private FCk_ProceduralAction_Identity _RejectedIdentity;
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
        Add_Step("reject a resident limb clip without its named binding", n"Step_RejectBinding");
        Add_Step("verify atomic rejection without pose or claim", n"Step_VerifyRejected");
        Add_Step_WaitUntil("the released action is destroyed", n"Check_Released", 600);
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
        _Rejected = utils_procedural_action::Create_FromClip(Owner, _Player,
            FCk_ProceduralAction_PlaySpec(TSoftObjectPtr<UCk_ProceduralAction_Data>(Clip)));
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
            "A missing binding rejects a resident clip before control acquisition");
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
        // Explicit delivery: nobody consumed the terminal, so the finished action stays until it is released.
        _Rejected.Request_Release(FCk_Request_ProceduralAction_Release());
    }

    UFUNCTION()
    private void Check_Released(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Rejected));
    }
}
