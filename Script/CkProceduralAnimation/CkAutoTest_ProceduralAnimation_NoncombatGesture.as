// Language=angelscript

// Exercises the reusable noncombat example against two independently bound dedicated limbs.
class UCk_AutoTest_ProceduralAnimation_NoncombatGesture : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_Transform _LeftPart;
    private FCk_Handle_Transform _RightPart;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Action;
    private FCk_Handle_ProceduralAction _Inspection;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _Root = utils_transform::Add(RootEntity, FTransform(FVector(142000.0, 72000.0, 1500.0)),
            ECk_Replication::DoesNotReplicate);
        auto LeftEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        auto RightEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        _LeftPart = utils_transform::Add(LeftEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        _RightPart = utils_transform::Add(RightEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);

        auto Lengths = TArray<float32>();
        Lengths.Add(100.0f);
        auto Left = FCk_ProceduralLimb_Layout(n"Left", FTransform(FVector(0.0, 40.0, 0.0)),
            Lengths, FTransform(FVector(100.0, 0.0, 0.0)), FVector::UpVector);
        auto Right = FCk_ProceduralLimb_Layout(n"Right", FTransform(FVector(0.0, -40.0, 0.0)),
            Lengths, FTransform(FVector(100.0, 0.0, 0.0)), FVector::UpVector);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>();
        Layouts.Add(Left);
        Layouts.Add(Right);
        auto LeftParts = TArray<FCk_Handle_Transform>();
        LeftParts.Add(_LeftPart);
        auto RightParts = TArray<FCk_Handle_Transform>();
        RightParts.Add(_RightPart);
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>();
        Bindings.Add(FCk_ProceduralLimb_Binding(n"Left", LeftParts));
        Bindings.Add(FCk_ProceduralLimb_Binding(n"Right", RightParts));
        _Subject = utils_procedural_articulation::Add(_Root,
            FCk_ProceduralArticulation_Spec(FCk_ProceduralArticulation_Layout(Layouts), Bindings));
        if (ck::IsValid(_Subject) == false)
        {
            FinishFailure("Standalone paired-limb subject did not compose");
            return;
        }
        Add_Step_WaitUntil("paired dedicated limbs settle", n"Check_Settled", 600);
        Add_Step("start one coordinated gesture", n"Step_Begin");
        Add_Step_WaitUntil("gesture claims both limbs", n"Check_Ready", 600);
        Add_Step("submit a paired rest-space offset", n"Step_Submit");
        Add_Step_WaitUntil("paired pose actually settles", n"Check_Accepted", 600);
        Add_Step("verify both rendered parts then end", n"Step_VerifyAndEnd");
        Add_Step_WaitUntil("both limbs return", n"Check_Finished", 600);
        Add_Step("verify terminal success", n"Step_VerifyFinished");
        Add_Step("start a later single-arm inspection reach", n"Step_BeginInspection");
        Add_Step_WaitUntil("inspection arm claims one channel", n"Check_InspectionReady", 600);
        Add_Step("submit inspection reach", n"Step_SubmitInspection");
        Add_Step_WaitUntil("inspection pose actually settles", n"Check_InspectionAccepted", 600);
        Add_Step("verify inspection arm then end", n"Step_VerifyInspectionAndEnd");
        Add_Step_WaitUntil("inspection arm returns", n"Check_InspectionFinished", 600);
        Add_Step("verify inspection terminal success", n"Step_VerifyInspectionFinished");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Subject.Get_SettledRevision() > 0 && _Subject.Get_Limbs().Num() == 2);
    }

    UFUNCTION()
    private void Step_Begin(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Player = utils_procedural_action_player::Add(_Subject, FCk_ProceduralActionPlayer_Spec());
        _Action = ck_procedural_action_noncombat::BeginCoordinatedGesture(
            InHandle, _Player, n"Left", n"Right");
        Assert_True(ck::IsValid(_Player) && ck::IsValid(_Action),
            "The noncombat example creates one scoped action for both limbs");
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
        const auto Angle = 0.01;
        const auto Along = Math::Cos(Angle) - 1.0;
        const auto Across = Math::Sin(Angle);
        ck_procedural_action_noncombat::SubmitCoordinatedGesture(_Action,
            n"Left", FTransform(FQuat(FVector::UpVector, Angle), FVector(Along, Across, 0.0)),
            n"Right", FTransform(FQuat(FVector::UpVector, -Angle), FVector(Along, -Across, 0.0)));
    }

    UFUNCTION()
    private void Check_Accepted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Receipt = _Action.Get_LastReceipt();
        Result.Set(Receipt.Get_HasAcceptedPose() && Receipt.Get_CurrentPose().Get_Limbs().Num() == 2);
    }

    UFUNCTION()
    private void Step_VerifyAndEnd(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Pose = _Action.Get_LastReceipt().Get_CurrentPose();
        const auto Limbs = Pose.Get_Limbs();
        Assert_True(Limbs.Num() == 2 && Limbs[0].Get_Id() == n"Left"
            && Limbs[1].Get_Id() == n"Right",
            "One accepted sample contains both authored limb channels");
        Assert_True(Limbs.Num() == 2 && Limbs[0].Get_Segments().Num() == 1
            && Limbs[1].Get_Segments().Num() == 1,
            "Both accepted limbs include one bound presentation part");
        if (Limbs.Num() == 2 && Limbs[0].Get_Segments().Num() == 1
            && Limbs[1].Get_Segments().Num() == 1)
        {
            const auto LeftActual = utils_transform::Get_EntityCurrentTransform(_LeftPart);
            const auto RightActual = utils_transform::Get_EntityCurrentTransform(_RightPart);
            Assert_True((LeftActual.GetLocation() - Limbs[0].Get_Segments()[0].GetLocation()).Size() < 0.05
                && (RightActual.GetLocation() - Limbs[1].Get_Segments()[0].GetLocation()).Size() < 0.05,
                "The accepted paired pose equals both settled presentation parts");
            const auto Root = utils_transform::Get_EntityCurrentLocation(_Root);
            const auto LeftRest = Root + FVector(100.0, 40.0, 0.0);
            const auto RightRest = Root + FVector(100.0, -40.0, 0.0);
            const auto LeftTravel = (Limbs[0].Get_AcceptedEndpoint().GetLocation() - LeftRest).Size();
            const auto RightTravel = (Limbs[1].Get_AcceptedEndpoint().GetLocation() - RightRest).Size();
            Assert_True(LeftTravel > 0.5 && LeftTravel < 1.5
                && RightTravel > 0.5 && RightTravel < 1.5,
                "Both accepted endpoints visibly leave rest by a bounded one-centimetre gesture");
        }
        _Action.Request_End(FCk_Request_ProceduralAction_End());
    }

    UFUNCTION()
    private void Check_Finished(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Action.Get_State() == ECk_ProceduralAction_State::Finished);
    }

    UFUNCTION()
    private void Step_VerifyFinished(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Result = _Action.Get_Result();
        Assert_True(Result.Get_Outcome() == ECk_ProceduralAction_Outcome::Completed
            && Result.Get_Recovery() == ECk_ProceduralAction_Recovery::Succeeded,
            "The paired gesture returns both dedicated limbs through one lifecycle");
    }

    UFUNCTION()
    private void Step_BeginInspection(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Inspection = ck_procedural_action_noncombat::BeginInspectionReach(InHandle, _Player, n"Left");
        Assert_True(ck::IsValid(_Inspection), "Inspection uses the same standalone subject after paired return");
    }

    UFUNCTION()
    private void Check_InspectionReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Inspection)
            && _Inspection.Get_State() == ECk_ProceduralAction_State::Ready);
    }

    UFUNCTION()
    private void Step_SubmitInspection(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Angle = 0.01;
        ck_procedural_action_noncombat::SubmitRestReach(_Inspection, n"Left",
            FTransform(FQuat(FVector::UpVector, Angle),
                FVector(Math::Cos(Angle) - 1.0, Math::Sin(Angle), 0.0)));
    }

    UFUNCTION()
    private void Check_InspectionAccepted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Receipt = _Inspection.Get_LastReceipt();
        Result.Set(Receipt.Get_HasAcceptedPose() && Receipt.Get_CurrentPose().Get_Limbs().Num() == 1);
    }

    UFUNCTION()
    private void Step_VerifyInspectionAndEnd(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Limbs = _Inspection.Get_LastReceipt().Get_CurrentPose().Get_Limbs();
        Assert_True(Limbs.Num() == 1 && Limbs[0].Get_Id() == n"Left",
            "Inspection receipt contains only its claimed arm");
        if (Limbs.Num() == 1)
        {
            const auto Actual = utils_transform::Get_EntityCurrentTransform(_LeftPart);
            const auto Root = utils_transform::Get_EntityCurrentLocation(_Root);
            const auto Travel = (Limbs[0].Get_AcceptedEndpoint().GetLocation()
                - Root - FVector(100.0, 40.0, 0.0)).Size();
            Assert_True(Limbs[0].Get_Segments().Num() == 1
                && (Actual.GetLocation() - Limbs[0].Get_Segments()[0].GetLocation()).Size() < 0.05
                && Travel > 0.5 && Travel < 1.5,
                "Single-arm inspection has one actual settled part and a one-centimetre reach");
        }
        _Inspection.Request_End(FCk_Request_ProceduralAction_End());
    }

    UFUNCTION()
    private void Check_InspectionFinished(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Inspection.Get_State() == ECk_ProceduralAction_State::Finished);
    }

    UFUNCTION()
    private void Step_VerifyInspectionFinished(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Result = _Inspection.Get_Result();
        Assert_True(Result.Get_Outcome() == ECk_ProceduralAction_Outcome::Completed
            && Result.Get_Recovery() == ECk_ProceduralAction_Recovery::Succeeded,
            "Inspection arm returns without affecting the other dedicated limb");
    }
}
