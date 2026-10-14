// Language=angelscript

// A resident limb clip played with Auto delivery through the public surface: OnFinished fires once with the terminal,
// the stream stays readable without being consumed, and the action releases itself once the player acknowledges it.
class UCk_AutoTest_ProceduralAnimation_ActionManagedPublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Action;
    private FCk_ProceduralAction_Identity _Identity;
    private int32 _FinishedCount = 0;
    private FCk_ProceduralAction_Terminal _Terminal;
    private bool _FinishedHandleMatched = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _Root = utils_transform::Add(RootEntity, FTransform(FVector(144000.0, 71000.0, 1500.0)),
            ECk_Replication::DoesNotReplicate);
        auto PartEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        auto Part = utils_transform::Add(PartEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        auto Lengths = TArray<float32>();
        Lengths.Add(100.0f);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>();
        Layouts.Add(FCk_ProceduralLimb_Layout(n"NodArm", FTransform::Identity,
            Lengths, FTransform(FVector(100.0, 0.0, 0.0)), FVector::UpVector));
        auto Parts = TArray<FCk_Handle_Transform>();
        Parts.Add(Part);
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>();
        Bindings.Add(FCk_ProceduralLimb_Binding(n"NodArm", Parts));
        _Subject = utils_procedural_articulation::Add(_Root,
            FCk_ProceduralArticulation_Spec(FCk_ProceduralArticulation_Layout(Layouts), Bindings));
        if (ck::IsValid(_Subject) == false)
        {
            FinishFailure("Dedicated limb subject must compose");
            return;
        }
        Add_Step_WaitUntil("limb subject settles", n"Check_Settled", 600);
        Add_Step("play the resident limb clip with Auto delivery", n"Step_Play");
        Add_Step_WaitUntil("OnFinished arrives", n"Check_Finished", 600);
        Add_Step("inspect the terminal", n"Step_VerifyTerminal");
        Add_Step_WaitUntil("the action releases itself", n"Check_Released", 600);
        Add_Step("verify nothing is left to drain", n"Step_VerifyReleased");
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
        auto Owner = InHandle;
        _Player = utils_procedural_action_player::Add(_Subject, FCk_ProceduralActionPlayer_Spec());
        auto Clip = Cast<UCk_ProceduralAction_Data>(LoadObject(this,
            "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA"));
        if (ck::IsValid(_Player) == false || Clip == nullptr)
        {
            FinishFailure("Limb clip and player must exist");
            return;
        }
        auto Slots = TArray<FCk_ProceduralAction_SlotBinding>();
        Slots.Add(FCk_ProceduralAction_SlotBinding(n"Nod", n"NodArm"));
        auto Spec = FCk_ProceduralAction_PlaySpec(TSoftObjectPtr<UCk_ProceduralAction_Data>(Clip));
        Spec.Set_SlotBindings(Slots).Set_Delivery(ECk_ProceduralAction_Delivery::Auto);
        Assert_True(Spec.Get_SlotBindings().Num() == 1 && Spec.Get_Delivery() == ECk_ProceduralAction_Delivery::Auto,
            "The play spec binds its fluent accessors in AngelScript");
        _Action = utils_procedural_action::Create_FromClip(Owner, _Player, Spec);
        if (ck::IsValid(_Action) == false)
        {
            FinishFailure("Create_FromClip must return the action for a resident clip");
            return;
        }
        _Identity = _Action.Get_Identity();
        Assert_True(_Identity.Get_Value() > 0, "The action has an identity");
        Assert_True(_Action.Get_State() == ECk_ProceduralAction_State::PendingAdmission,
            "A resident clip compiles and binds inside Create_FromClip");
        utils_procedural_action::BindTo_OnFinished(_Action, FCk_Delegate_ProceduralAction_OnFinished(this, n"OnFinished"));
        Assert_True(_FinishedCount == 0, "OnFinished does not fire for an action that is still running");
        Assert_True(_Player.Read_Receipts(_Identity, 0, 8).Get_Result() == ECk_ProceduralAction_ReadResult::Pending,
            "An Auto identity stays readable");
    }

    UFUNCTION()
    private void OnFinished(FCk_Handle_ProceduralAction InAction, FCk_ProceduralAction_Terminal InTerminal)
    {
        _FinishedCount += 1;
        _Terminal = InTerminal;
        _FinishedHandleMatched = InAction.Get_Identity().Get_Value() == _Identity.Get_Value();
    }

    UFUNCTION()
    private void Check_Finished(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_FinishedCount > 0);
    }

    UFUNCTION()
    private void Step_VerifyTerminal(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_FinishedHandleMatched, "OnFinished passes the finished action");
        Assert_True(_Terminal.Get_Identity().Get_Value() == _Identity.Get_Value(),
            "The terminal names the started identity");
        Assert_True(_Terminal.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed,
            "The limb clip completes");
        Assert_True(utils_procedural_action::Get_ResultText(_Terminal.Get_Result(), _Terminal.Get_Context())
            .ToString().Contains("Completed"),
            "The terminal renders through the public result text");
    }

    UFUNCTION()
    private void Check_Released(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Retained = false;
        for (const auto& Identity : _Player.Get_RetainedReceiptIdentities())
        { Retained = Retained || Identity.Get_Value() == _Identity.Get_Value(); }
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Action) && Retained == false);
    }

    UFUNCTION()
    private void Step_VerifyReleased(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_FinishedCount == 1, "OnFinished fired exactly once");
        Assert_True(_Subject.Get_IsAnyLimbControlled() == false, "The limb is back under the subject");
    }
}
