// Language=angelscript

// A resident limb clip played with managed delivery: records arrive through a listener and the framework drains them.
class UCk_AutoTest_ProceduralAnimation_ActionManagedPublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_ProceduralAction_ManagedStart _Start;
    private int32 _RecordCount = 0;
    private bool _LastRecordWasTerminal = false;
    private int32 _CompletionCount = 0;
    private FCk_ProceduralAction_ManagedCompletion _Completion;

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
        Add_Step("play the resident limb clip with managed delivery", n"Step_Play");
        Add_Step_WaitUntil("managed completion arrives", n"Check_Completed", 600);
        Add_Step("inspect the delivered records and the completion", n"Step_Verify");
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
        auto Loaded = FCk_ProceduralAction_LoadedPlaySpec(Clip);
        Loaded.Set_SlotBindings(Slots);
        _Start = utils_procedural_action::Play_Loaded(Owner, _Player, Loaded,
            FCk_Delegate_ProceduralAction_OnManagedCompleted(this, n"OnManagedCompleted"));
        Assert_True(ck::IsValid(_Start.Get_Action()) && _Start.Get_Identity().Get_Value() > 0,
            "Managed play returns its action and identity");
        Assert_True(_CompletionCount == 0, "No completion fires inside Play_Loaded");
        Assert_True(utils_procedural_action_player::Get_IsManagedDelivery(_Player, _Start.Get_Identity()),
            "The started identity uses managed delivery");
        Assert_True(_Player.Read_Receipts(_Start.Get_Identity(), 0, 8).Get_Result()
            == ECk_ProceduralAction_ReadResult::Pending,
            "A managed identity stays readable without being consumed");
        auto Player = _Player;
        utils_procedural_action_player::BindTo_OnManagedRecords(Player, _Start.Get_Identity(),
            FCk_Delegate_ProceduralAction_OnManagedRecords(this, n"OnManagedRecords"));
    }

    UFUNCTION()
    private void OnManagedRecords(FCk_Handle_ProceduralActionPlayer InPlayer, FCk_ProceduralAction_ManagedPage InPage)
    {
        for (auto Record : InPage.Get_Records())
        {
            _RecordCount += 1;
            _LastRecordWasTerminal = Record.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal;
        }
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

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_RecordCount >= 3 && _LastRecordWasTerminal,
            "The records listener receives the stream through its terminal");
        Assert_True(_CompletionCount == 1, "Managed completion fires once");
        Assert_True(_Completion.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed,
            "The producer outcome is Completed");
        Assert_True(_Completion.Get_Delivery() == ECk_ProceduralAction_DeliveryStatus::Complete,
            "Every record was delivered");
    }
}
