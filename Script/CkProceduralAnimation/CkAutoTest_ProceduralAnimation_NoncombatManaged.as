// Language=angelscript

// The noncombat example plays a resident body clip with managed delivery: one completion, no receipt loop.
class UCk_AutoTest_ProceduralAnimation_NoncombatManaged : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_ProceduralAction_ManagedStart _Start;
    private int32 _CompletionCount = 0;
    private FCk_ProceduralAction_ManagedCompletion _Completion;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _Root = utils_transform::Add(RootEntity, FTransform(FVector(145000.0, 71000.0, 1500.0)),
            ECk_Replication::DoesNotReplicate);
        auto PresentationEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        auto Presentation = utils_transform::Add(PresentationEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        auto Spec = FCk_ProceduralArticulation_Spec();
        Spec.Set_Presentation(Presentation);
        _Subject = utils_procedural_articulation::Add(_Root, Spec);
        if (ck::IsValid(_Subject) == false)
        {
            FinishFailure("Body-only subject must compose");
            return;
        }
        Add_Step_WaitUntil("body subject settles", n"Check_Settled", 600);
        Add_Step("play the body clip through the managed example helper", n"Step_Play");
        Add_Step_WaitUntil("managed completion arrives", n"Check_Completed", 600);
        Add_Step_WaitUntil("the framework releases the action and reclaims its stream", n"Check_Reclaimed", 600);
        Add_Step("inspect the completion", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Subject) && _Subject.Get_SettledRevision() > 0);
    }

    UFUNCTION()
    private void Step_Play(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Player = utils_procedural_action_player::Add(_Subject, FCk_ProceduralActionPlayer_Spec());
        auto Clip = Cast<UCk_ProceduralAction_Data>(LoadObject(this,
            "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA"));
        if (ck::IsValid(_Player) == false || Clip == nullptr)
        {
            FinishFailure("Body clip and player must exist");
            return;
        }
        _Start = ck_procedural_action_noncombat::PlayManagedGesture(InHandle, _Player, Clip,
            TArray<FCk_ProceduralAction_SlotBinding>(),
            FCk_Delegate_ProceduralAction_OnManagedCompleted(this, n"OnManagedCompleted"));
        Assert_True(_Start.Get_Report().Get_Status() == ECk_ProceduralAction_ReportStatus::Valid
            && ck::IsValid(_Start.Get_Action()) && _Start.Get_Identity().Get_Value() > 0,
            "The example starts a managed body clip");
        Assert_True(_CompletionCount == 0, "No completion fires inside the example call");
        Assert_True(utils_procedural_action_player::Get_IsManagedDelivery(_Player, _Start.Get_Identity()),
            "The example selects managed delivery");
    }

    UFUNCTION()
    private void OnManagedCompleted(FCk_Handle_ProceduralActionPlayer InPlayer,
        FCk_ProceduralAction_ManagedCompletion InCompletion)
    {
        _CompletionCount += 1;
        _Completion = InCompletion;
    }

    UFUNCTION()
    private void Check_Completed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_CompletionCount > 0);
    }

    // The seal on a managed identity lives as long as its receipt stream; the stream is reclaimed once the framework
    // has released the action and acknowledged its terminal, a few frames after the completion.
    UFUNCTION()
    private void Check_Reclaimed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        auto Retained = false;
        for (const auto& Identity : utils_procedural_action_player::Get_RetainedReceiptIdentities(_Player))
        { Retained = Retained || Identity.Get_Value() == _Start.Get_Identity().Get_Value(); }
        Result.Set(ck::IsValid(_Player) && Retained == false);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_CompletionCount == 1, "The managed completion fires once");
        Assert_True(_Completion.Get_Identity().Get_Value() == _Start.Get_Identity().Get_Value(),
            "The completion names the started identity");
        Assert_True(_Completion.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed,
            "The body clip completes");
        Assert_True(_Completion.Get_Delivery() == ECk_ProceduralAction_DeliveryStatus::Complete,
            "Every record was delivered and acknowledged by the framework");
        Assert_True(ck::Is_NOT_Valid(_Start.Get_Action())
            && utils_procedural_action_player::Get_IsManagedDelivery(_Player, _Start.Get_Identity()) == false,
            "Nothing is left for the caller to drain or release: the action is gone and the seal lifted with its stream");
    }
}
