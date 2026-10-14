// Language=angelscript

// A clip assembled in script from a Reach recipe and one extra pose plays through Create_FromClip.
class UCk_AutoTest_ProceduralAnimation_ActionBuilderPublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Action;
    private FCk_ProceduralAction_Identity _Identity;
    private UCk_ProceduralAction_Data _Clip;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _Root = utils_transform::Add(RootEntity, FTransform(FVector(144000.0, 71000.0, 1500.0)),
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
        Layouts.Add(FCk_ProceduralLimb_Layout(n"Arm", FTransform::Identity,
            Lengths, FTransform(FVector(100.0, 0.0, 0.0)), FVector::UpVector));
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>();
        Bindings.Add(FCk_ProceduralLimb_Binding(n"Arm", Parts));
        _Subject = utils_procedural_articulation::Add(_Root,
            FCk_ProceduralArticulation_Spec(FCk_ProceduralArticulation_Layout(Layouts), Bindings));
        Assert_True(ck::IsValid(_Subject), "Dedicated two-segment arm composes");

        auto Slots = TArray<FName>();
        Slots.Add(n"Reach");
        auto Recipe = FCk_ProceduralAction_RecipeSpec(ECk_ProceduralAction_Recipe::Reach, Slots);
        Recipe.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        Recipe.Set_Direction(FVector::UpVector);
        Recipe.Set_AmplitudeCm(0.2f);
        Recipe.Set_Prepare(FCk_Time(0.1));
        Recipe.Set_Execute(FCk_Time(0.15));
        Recipe.Set_Recover(FCk_Time(0.1));
        auto Draft = FCk_ProceduralAction_ClipDraft();
        const auto RecipeReport = utils_procedural_action_builder::Make_FromRecipe(Recipe, Draft);
        Assert_True(RecipeReport.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "Reach recipe produces an editable draft");
        Assert_True(Draft.Get_Phases().Num() == 3, "Reach draft carries Prepare, Execute and Recover");

        auto Limbs = TArray<FCk_ProceduralAction_LimbPoseTarget>();
        Limbs.Add(FCk_ProceduralAction_LimbPoseTarget(n"Reach", FVector(0.0, 0.0, 0.1)));
        auto Pose = FCk_ProceduralAction_PoseKey(FCk_FloatRange_0to1(0.5f));
        Pose.Set_Limbs(Limbs);
        const auto PoseReport = utils_procedural_action_builder::Add_PoseKey(Draft, n"Execute", Pose);
        Assert_True(PoseReport.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "An intermediate pose key lands inside the Execute phase");

        _Clip = Cast<UCk_ProceduralAction_Data>(NewObject(this, UCk_ProceduralAction_Data));
        const auto BuildReport = utils_procedural_action_builder::Build_Into(Draft, _Clip);
        Assert_True(BuildReport.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The draft compiles and builds into a script-owned clip");
        const auto Validation = utils_procedural_action::Validate_Data(_Clip);
        Assert_True(Validation.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The built clip validates through the public surface");

        Add_Step_WaitUntil("arm subject settles", n"Check_Settled", 600);
        Add_Step("play the built clip through a named slot binding", n"Step_Play");
        Add_Step_WaitUntil("built clip reaches a terminal production receipt", n"Check_Finished", 600);
        Add_Step("inspect accepted geometry and the outcome", n"Step_Verify");
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
        auto Bindings = TArray<FCk_ProceduralAction_SlotBinding>();
        Bindings.Add(FCk_ProceduralAction_SlotBinding(n"Reach", n"Arm"));
        auto Spec = FCk_ProceduralAction_PlaySpec(TSoftObjectPtr<UCk_ProceduralAction_Data>(_Clip));
        Spec.Set_SlotBindings(Bindings);
        _Action = utils_procedural_action::Create_FromClip(Owner, _Player, Spec);
        Assert_True(ck::IsValid(_Action), "The built clip reserves an action");
        if (ck::IsValid(_Action)) { _Identity = _Action.Get_Identity(); }
    }

    UFUNCTION()
    private void Check_Finished(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Player) && _Identity.Get_Value() > 0
            && _Player.Read_Receipts(_Identity, 0, 64).Get_Terminal());
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Page = _Player.Read_Receipts(_Identity, 0, 64);
        bool AcceptedArm = false;
        for (auto Record : Page.Get_Records())
        {
            if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose) { continue; }
            const auto Limbs = Record.Get_Receipt().Get_CurrentPose().Get_Limbs();
            AcceptedArm = AcceptedArm || (Limbs.Num() == 1 && Limbs[0].Get_Id() == n"Arm");
        }
        Assert_True(AcceptedArm, "The built clip publishes settled arm geometry");
        Assert_True(_Action.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed,
            "The built clip completes through the ordinary return path");
    }
}
