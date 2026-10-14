// Language=angelscript

// A walking leg borrowed through the borrowing preset sweeps and replants. A second sweep loses its floor mid-motion:
// the failed return is diagnosed through the result text and the unplanted query, and the leg is recovered explicitly
// once the floor is back.
class UCk_AutoTest_ProceduralAnimation_BorrowingSweep : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 40.0f;
    default _AutoStageOriginField = false;

    private FVector _Origin = FVector(147000.0, 74000.0, 600.0);
    private FCk_Handle _FloorEntity;
    private FCk_Handle_JoltBody _Floor;
    private FCk_Handle_Transform _Body;
    private FCk_Handle_ProceduralGait _Gait;
    private TArray<FCk_Handle_ProceduralLeg> _Legs;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private UCk_ProceduralAction_Data _ShortSweep;
    private UCk_ProceduralAction_Data _LongSweep;
    private FCk_ProceduralAction_ManagedStart _Start;
    private TArray<FCk_ProceduralAction_ManagedCompletion> _Completions;
    private int32 _RecoveryCompletions = 0;
    private ECk_Request_OperationResult _RecoveryResult = ECk_Request_OperationResult::Failed;
    private int32 _RecoveryPolls = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        AddFloor(InHandle);
        _ShortSweep = Cast<UCk_ProceduralAction_Data>(NewObject(this, UCk_ProceduralAction_Data));
        _LongSweep = Cast<UCk_ProceduralAction_Data>(NewObject(this, UCk_ProceduralAction_Data));
        const auto ShortReport = ck_procedural_action_builder::BuildSweep(n"Sweep", FVector(0.6, 0.0, 0.8), 0.08f,
            FCk_Time(0.6), _ShortSweep);
        const auto LongReport = ck_procedural_action_builder::BuildSweep(n"Sweep", FVector(0.6, 0.0, 0.8), 0.08f,
            FCk_Time(3.0), _LongSweep);
        Assert_True(ShortReport.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid
            && LongReport.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "Both walking-leg sweeps build");

        Add_Step_WaitUntil("floor is queryable", n"Check_FloorReady", 600);
        Add_Step("compose a four-leg walker with one bound leg", n"Step_Compose");
        Add_Step_WaitUntil("all four feet plant", n"Check_Planted", 600);
        Add_Step("borrow the bound leg for a short sweep", n"Step_SweepAndReturn");
        Add_Step_WaitUntil("the short sweep completes", n"Check_FirstCompletion", 900);
        Add_Step("verify the borrowed leg replanted", n"Step_VerifyReturned");
        Add_Step_WaitUntil("the leg is back in the gait", n"Check_Replanted", 600);
        Add_Step("start a long sweep", n"Step_SweepLong");
        Add_Step_WaitUntil("the long sweep is playing", n"Check_Playing", 600);
        Add_Step("remove the floor under the walker", n"Step_RemoveFloor");
        Add_Step_WaitUntil("the floor is gone", n"Check_FloorGone", 600);
        Add_Step_WaitUntil("the long sweep completes", n"Check_SecondCompletion", 900);
        Add_Step("diagnose the failed return and restore the floor", n"Step_DiagnoseAndRestore");
        Add_Step_WaitUntil("the restored floor is queryable", n"Check_FloorReady", 600);
        Add_Step_WaitUntil("the other three feet plant on the restored floor", n"Check_SupportRestored", 900);
        Add_Step("recover the unplanted leg", n"Step_Recover");
        Add_Step_WaitUntil("the leg is recovered", n"Check_Recovered", 900);
        Add_Step("verify the recovery", n"Step_VerifyRecovered");
        Run_Steps(InHandle);
    }

    private void AddFloor(FCk_Handle InOwner)
    {
        auto Owner = InOwner;
        _FloorEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _FloorEntity.Request_OverrideToSelf();
        utils_transform::Add(_FloorEntity, FTransform(_Origin - FVector(0.0, 0.0, 25.0)),
            ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(FVector(600.0, 600.0, 25.0));
        auto Params = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Params.Set_ShapeDimensions(Shape);
        Params.Set_MotionType(ECk_MotionType::Static);
        Params.Set_CollisionProfileName(n"BlockAll");
        _Floor = utils_jolt_body::Add(_FloorEntity, Params);
    }

    UFUNCTION()
    private void Check_FloorReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Floor) && utils_jolt_body::Get_IsBodyAdded(_Floor));
    }

    UFUNCTION()
    private void Step_Compose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Owner = InHandle;
        auto Root = utils_entity_lifetime::Request_CreateEntity(Owner);
        Root.Request_OverrideToSelf();
        _Body = utils_transform::Add(Root, FTransform(_Origin + FVector(0.0, 0.0, ck_procedural_gym::BodyClearance)),
            ECk_Replication::DoesNotReplicate);
        auto Parts = TArray<FCk_Handle_Transform>();
        for (auto Index = 0; Index < 2; ++Index)
        {
            auto PartEntity = utils_entity_lifetime::Request_CreateEntity(Root);
            Parts.Add(utils_transform::Add(PartEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate));
        }
        auto Rig = FCk_ProceduralRig_Spec();
        Rig.Set_Segments(Parts);
        auto Chains = TArray<FCk_ProceduralWalker_LegChain>();
        Chains.Add(FCk_ProceduralWalker_LegChain(n"Leg0", Rig));
        const auto Walker = utils_procedural_animation::Add_Walker(_Body,
            ck::ProceduralGym_Rig4, ck::ProceduralGym_Gait, Chains);
        _Gait = Walker.Get_Gait();
        _Legs = Walker.Get_Legs();
        _Subject = utils_procedural_articulation::DoCastChecked(Root);
        Assert_True(ck::IsValid(_Gait) && ck::IsValid(_Subject), "The walker presents one bound walking leg");
    }

    UFUNCTION()
    private void Check_Planted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Subject) && _Subject.Get_SettledRevision() > 0
            && utils_procedural_gait::Get_PlantedCount(_Gait) == 4);
    }

    UFUNCTION()
    private void Step_SweepAndReturn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Return = FCk_ProceduralLimbControl_Return();
        Return.Set_Duration(FCk_Time(0.3));
        Return.Set_Timeout(FCk_Time(1.5));
        Return.Set_MaxProbeAttempts(2);
        _Player = ck_procedural_action_sweep::AddBorrowingSweepPlayer(_Subject, 3, Return);
        Assert_True(ck::IsValid(_Player), "The borrowing preset composes a player on the walker");
        _Start = ck_procedural_action_sweep::StartSweep(InHandle, _Player, _ShortSweep,
            FCk_ProceduralAction_SlotBinding(n"Sweep", n"Leg0"),
            FCk_Delegate_ProceduralAction_OnManagedCompleted(this, n"OnCompleted"));
        Assert_True(_Start.Get_Report().Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The short sweep starts on the walking leg");
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
    private void Step_VerifyReturned(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Completion = _Completions[0];
        Log(f"[BorrowingSweep] short sweep: {ck_procedural_action_sweep::DescribeOutcome(Completion)}");
        Assert_True(Completion.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed
            && Completion.Get_Result().Get_Recovery() == ECk_ProceduralAction_Recovery::Succeeded,
            "The borrowed leg sweeps and replants before the action completes");
        Assert_True(_Subject.Get_UnplantedLimbIds().Num() == 0, "Nothing is left unplanted");
    }

    UFUNCTION()
    private void Check_Replanted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Control = _Subject.Get_LimbControl(n"Leg0");
        Result.Set(Control.Get_State() == ECk_ProceduralLimbControl_State::Default && Control.Get_TrustedSupport()
            && utils_procedural_gait::Get_PlantedCount(_Gait) == 4);
    }

    UFUNCTION()
    private void Step_SweepLong(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Start = ck_procedural_action_sweep::StartSweep(InHandle, _Player, _LongSweep,
            FCk_ProceduralAction_SlotBinding(n"Sweep", n"Leg0"),
            FCk_Delegate_ProceduralAction_OnManagedCompleted(this, n"OnCompleted"));
        Assert_True(_Start.Get_Report().Get_Status() == ECk_ProceduralAction_ReportStatus::Valid,
            "The long sweep starts on the walking leg");
    }

    UFUNCTION()
    private void Check_Playing(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Start.Get_Action())
            && utils_procedural_action::Get_State(_Start.Get_Action()) == ECk_ProceduralAction_State::Playing);
    }

    UFUNCTION()
    private void Step_RemoveFloor(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_entity_lifetime::Request_DestroyEntity(_FloorEntity);
    }

    UFUNCTION()
    private void Check_FloorGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Floor));
    }

    UFUNCTION()
    private void Check_SecondCompletion(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Completions.Num() >= 2);
    }

    UFUNCTION()
    private void Step_DiagnoseAndRestore(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Completion = _Completions[1];
        const auto Description = ck_procedural_action_sweep::DescribeOutcome(Completion);
        Log(f"[BorrowingSweep] long sweep without floor: {Description}");
        Assert_True(Completion.Get_Result().Get_Recovery() == ECk_ProceduralAction_Recovery::Failed
            && Description.Contains("Recovery: Failed"),
            "The failed return is reported through the result text");
        Assert_True(ck_procedural_action_sweep::Get_IsWaitingForRecovery(_Subject, n"Leg0"),
            "The leg that could not replant is reported unplanted");
        AddFloor(InHandle);
    }

    UFUNCTION()
    private void Check_SupportRestored(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        auto Planted = 0;
        for (auto Leg : _Legs)
        {
            if (utils_procedural_leg::Get_Id(Leg) == n"Leg0")
            { continue; }
            const auto Foot = utils_procedural_leg::Get_Foot(Leg);
            if (Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted
                && Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted)
            { ++Planted; }
        }
        Result.Set(_Legs.Num() == 4 && Planted == 3);
    }

    UFUNCTION()
    private void Step_Recover(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        ck_procedural_action_sweep::RecoverUnplanted(_Subject,
            FCk_Delegate_Request_OnCompleted(this, n"OnRecoveryCompleted"));
    }

    UFUNCTION()
    private void OnRecoveryCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        _RecoveryCompletions += 1;
        _RecoveryResult = InResult;
        auto Unplanted = FString();
        for (auto Id : _Subject.Get_UnplantedLimbIds())
        { Unplanted += Id.ToString() + " "; }
        const auto Control = _Subject.Get_LimbControl(n"Leg0");
        Log(f"[BorrowingSweep] RecoverUnplantedLimbs completed {InResult}; unplanted now [{Unplanted}] control={Control.Get_State()} reason={Control.Get_Reason()}");
    }

    UFUNCTION()
    private void Check_Recovered(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Control = _Subject.Get_LimbControl(n"Leg0");
        Result.Set(_RecoveryCompletions > 0 && _Subject.Get_UnplantedLimbIds().Num() == 0
            && Control.Get_State() == ECk_ProceduralLimbControl_State::Default && Control.Get_TrustedSupport());
        ++_RecoveryPolls;
        if (_RecoveryPolls % 120 == 0)
        {
            Log(f"[BorrowingSweep] recovering: polls={_RecoveryPolls} completions={_RecoveryCompletions} result={_RecoveryResult} control={Control.Get_State()} reason={Control.Get_Reason()} trusted={Control.Get_TrustedSupport()} contacts={utils_procedural_gait::Get_TrustedContactCount(_Gait)}");
        }
    }

    UFUNCTION()
    private void Step_VerifyRecovered(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Log(f"[BorrowingSweep] recovery completions={_RecoveryCompletions} result={_RecoveryResult}");
        Assert_True(_RecoveryCompletions == 1 && _RecoveryResult == ECk_Request_OperationResult::Succeeded,
            "The unplanted recovery request completes once and succeeds");
        Assert_True(ck_procedural_action_sweep::Get_IsWaitingForRecovery(_Subject, n"Leg0") == false,
            "The recovered leg is back under the gait");
    }
}
