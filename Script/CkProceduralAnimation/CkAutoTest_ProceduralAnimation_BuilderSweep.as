// Language=angelscript

// A sweep built in script (recipe, extra pose key, Build_Into) plays on a dedicated arm through the sweep example: one
// completed run, one cancelled run, and one run rejected at binding, each reported through its managed completion.
class UCk_AutoTest_ProceduralAnimation_BuilderSweep : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private UCk_ProceduralAction_Data _Sweep;
    private FCk_ProceduralAction_ManagedStart _Completed;
    private FCk_ProceduralAction_ManagedStart _Cancelled;
    private FCk_ProceduralAction_ManagedStart _Rejected;
    private TArray<FCk_ProceduralAction_ManagedCompletion> _Completions;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _Root = utils_transform::Add(RootEntity, FTransform(FVector(146000.0, 71000.0, 1500.0)),
            ECk_Replication::DoesNotReplicate);
        auto Parts = TArray<FCk_Handle_Transform>();
        for (auto Index = 0; Index < 2; ++Index)
        {
            auto PartEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
            Parts.Add(utils_transform::Add(PartEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate));
        }
        auto Lengths = TArray<float32>();
        Lengths.Add(60.0f);
        Lengths.Add(60.0f);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>();
        Layouts.Add(FCk_ProceduralLimb_Layout(n"SweepArm", FTransform::Identity,
            Lengths, FTransform(FVector(100.0, 0.0, 0.0)), FVector::UpVector));
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>();
        Bindings.Add(FCk_ProceduralLimb_Binding(n"SweepArm", Parts));
        _Subject = utils_procedural_articulation::Add(_Root,
            FCk_ProceduralArticulation_Spec(FCk_ProceduralArticulation_Layout(Layouts), Bindings));
        if (ck::IsValid(_Subject) == false)
        {
            FinishFailure("Dedicated two-segment arm must compose");
            return;
        }

        _Sweep = Cast<UCk_ProceduralAction_Data>(NewObject(this, UCk_ProceduralAction_Data));
        const auto Report = ck_procedural_action_builder::BuildSweep(n"Sweep",
            FVector(0.6, 0.0, 0.8), 0.15f, FCk_Time(0.4), _Sweep);
        Assert_True(Report.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The builder example produces a valid sweep");
        Assert_True(_Sweep.Get_Phases().Num() == 1 && _Sweep.Get_Phases()[0].Get_LimbTracks().Num() == 1
            && _Sweep.Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys().Num() == 4,
            "The sweep carries the recipe's three keys plus the added pose key");

        Add_Step_WaitUntil("arm settles", n"Check_Settled", 600);
        Add_Step("play the built sweep on the dedicated arm", n"Step_PlayCompleted");
        Add_Step_WaitUntil("the sweep completes", n"Check_FirstCompletion", 600);
        Add_Step("cancel a second sweep right after it starts", n"Step_PlayCancelled");
        Add_Step_WaitUntil("the cancelled sweep completes", n"Check_SecondCompletion", 600);
        Add_Step("bind a third sweep to a limb the subject lacks", n"Step_PlayRejected");
        Add_Step_WaitUntil("the rejected sweep completes", n"Check_ThirdCompletion", 600);
        Add_Step("inspect the three outcomes", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Subject) && _Subject.Get_SettledRevision() > 0);
    }

    UFUNCTION()
    private void Step_PlayCompleted(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Player = ck_procedural_action_sweep::AddDedicatedSweepPlayer(_Subject);
        Assert_True(ck::IsValid(_Player), "The dedicated preset composes a player");
        _Completed = ck_procedural_action_sweep::StartSweep(InHandle, _Player, _Sweep,
            FCk_ProceduralAction_SlotBinding(n"Sweep", n"SweepArm"),
            FCk_Delegate_ProceduralAction_OnManagedCompleted(this, n"OnCompleted"));
        Assert_True(_Completed.Get_Report().Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The sweep starts");
    }

    UFUNCTION()
    private void Step_PlayCancelled(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Cancelled = ck_procedural_action_sweep::StartSweep(InHandle, _Player, _Sweep,
            FCk_ProceduralAction_SlotBinding(n"Sweep", n"SweepArm"),
            FCk_Delegate_ProceduralAction_OnManagedCompleted(this, n"OnCompleted"));
        Assert_True(_Cancelled.Get_Report().Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The second sweep starts");
        ck_procedural_action_sweep::CancelSweep(_Cancelled);
    }

    UFUNCTION()
    private void Step_PlayRejected(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Rejected = ck_procedural_action_sweep::StartSweep(InHandle, _Player, _Sweep,
            FCk_ProceduralAction_SlotBinding(n"Sweep", n"MissingArm"),
            FCk_Delegate_ProceduralAction_OnManagedCompleted(this, n"OnCompleted"));
        Assert_True(_Rejected.Get_Report().Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "A binding rejection is not a start rejection: the action exists and reports through its completion");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle_ProceduralActionPlayer InPlayer, FCk_ProceduralAction_ManagedCompletion InCompletion)
    {
        _Completions.Add(InCompletion);
    }

    UFUNCTION()
    private void Check_FirstCompletion(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Completions.Num() >= 1);
    }

    UFUNCTION()
    private void Check_SecondCompletion(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Completions.Num() >= 2);
    }

    UFUNCTION()
    private void Check_ThirdCompletion(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Completions.Num() >= 3);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_Completions.Num() == 3, "Each sweep completes exactly once");
        if (_Completions.Num() != 3)
        { return; }
        Assert_True(_Completions[0].Get_Identity().Get_Value() == _Completed.Get_Identity().Get_Value()
            && _Completions[0].Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed
            && _Completions[0].Get_Delivery() == ECk_ProceduralAction_DeliveryStatus::Complete,
            "The first sweep completes and is fully delivered");
        Assert_True(_Completions[1].Get_Identity().Get_Value() == _Cancelled.Get_Identity().Get_Value()
            && _Completions[1].Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Cancelled,
            "The cancelled sweep reports Cancelled");
        const auto Rejection = ck_procedural_action_sweep::DescribeOutcome(_Completions[2]);
        Log(f"[BuilderSweep] rejected sweep: {Rejection}");
        Assert_True(_Completions[2].Get_Identity().Get_Value() == _Rejected.Get_Identity().Get_Value()
            && _Completions[2].Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Rejected
            && Rejection.Contains("Rejected"),
            "The rejected sweep reports Rejected, and the example's description says so");
        Assert_True(_Subject.Get_IsAnyLimbControlled() == false, "No limb remains controlled");
    }
}
