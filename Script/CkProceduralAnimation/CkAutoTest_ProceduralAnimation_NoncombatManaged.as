// Language=angelscript

// The noncombat example plays a resident body clip with Auto delivery: one OnFinished, no receipt loop, and the action
// releases itself.
class UCk_AutoTest_ProceduralAnimation_NoncombatManaged : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Action;
    private FCk_ProceduralAction_Identity _Identity;
    private int32 _CompletionCount = 0;
    private FCk_ProceduralAction_Terminal _Terminal;

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
        Add_Step("play the body clip through the noncombat example helper", n"Step_Play");
        Add_Step_WaitUntil("OnFinished arrives", n"Check_Completed", 600);
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
        _Action = ck_procedural_action_noncombat::PlayGesture(InHandle, _Player, Clip,
            TArray<FCk_ProceduralAction_SlotBinding>(),
            FCk_Delegate_ProceduralAction_OnFinished(this, n"OnFinished"));
        if (ck::Is_NOT_Valid(_Action))
        {
            FinishFailure("The example must start the body clip");
            return;
        }
        _Identity = utils_procedural_action::Get_Identity(_Action);
        Assert_True(_Identity.Get_Value() > 0, "The started action has an identity");
        Assert_True(_CompletionCount == 0, "OnFinished does not fire inside the example call for a clip that binds");
    }

    UFUNCTION()
    private void OnFinished(FCk_Handle_ProceduralAction InAction, FCk_ProceduralAction_Terminal InTerminal)
    {
        _CompletionCount += 1;
        _Terminal = InTerminal;
    }

    UFUNCTION()
    private void Check_Completed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_CompletionCount > 0);
    }

    // The player acknowledges an Auto stream itself and releases the action on its next pass; the stream is reclaimed
    // with the action, a few frames after OnFinished.
    UFUNCTION()
    private void Check_Reclaimed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        auto Retained = false;
        for (const auto& Identity : utils_procedural_action_player::Get_RetainedReceiptIdentities(_Player))
        { Retained = Retained || Identity.Get_Value() == _Identity.Get_Value(); }
        Result.Set(ck::IsValid(_Player) && Retained == false);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_CompletionCount == 1, "OnFinished fires once");
        Assert_True(_Terminal.Get_Identity().Get_Value() == _Identity.Get_Value(),
            "The terminal names the started identity");
        Assert_True(_Terminal.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed,
            "The body clip completes");
        Assert_True(ck::Is_NOT_Valid(_Action),
            "Nothing is left for the caller to drain or release: the action is gone with its stream");
    }
}
