// Language=angelscript

// The reusable carrier helper borrows one bound walking leg without changing gait anatomy.
class UCk_AutoTest_ProceduralAnimation_NoncombatCarrierReach : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;
    default _AutoStageOriginField = false;

    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(143000.0, 74000.0, 600.0);
    private FCk_Handle_Transform _Body;
    private FCk_Handle_Transform _Upper;
    private FCk_Handle_Transform _Lower;
    private FCk_Handle_ProceduralGait _Gait;
    private FCk_Handle_ProceduralLeg _Leg;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Action;
    private FVector _EntryEndpoint;
    private bool _OverrideCallbackSeen = false;
    private bool _OverrideRejected = false;
    private int _AcceptancePolls = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        if (_Fixture.Create(InHandle, _Origin, ECkProceduralAnimationGym_Course::Flat, false, 1) == false)
        {
            FinishFailure("Carrier floor fixture could not be created");
            return;
        }
        Add_Step_WaitUntil("carrier floor is queryable", n"Check_FloorReady", 600);
        Add_Step("compose four-leg carrier with one visible chain", n"Step_Compose");
        Add_Step_WaitUntil("all carrier feet plant and bound chain settles", n"Check_Planted", 600);
        Add_Step("borrow one walking leg through the public helper", n"Step_BeginReach");
        Add_Step_WaitUntil("carrier leg is withdrawn under support policy", n"Check_Withdrawn", 600);
        Add_Step("explicit upward bend is subject to the motion budget", n"Step_SubmitBendOverride");
        Add_Step_WaitUntil("explicit bend override rejects without changing the action", n"Check_BendOverrideRejected", 600);
        Add_Step("explicit zero bend is invalid", n"Step_SubmitZeroBendOverride");
        Add_Step_WaitUntil("zero bend rejects without changing the action", n"Check_BendOverrideRejected", 600);
        Add_Step("author a small rest-space reach", n"Step_SubmitReach");
        Add_Step_WaitUntil("borrowed carrier reach actually settles", n"Check_Accepted", 600);
        Add_Step("verify rendered reach and end", n"Step_VerifyAndEnd");
        Add_Step_WaitUntil("borrowed carrier leg replants", n"Check_Replanted", 600);
        Add_Step("verify completed return and retire fixture", n"Step_VerifyAndDestroy");
        Add_Step_WaitUntil("carrier floor and parts retire", n"Check_Destroyed", 600);
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_FloorReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_AreSurfacesReady());
    }

    UFUNCTION()
    private void Step_Compose(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Root = utils_entity_lifetime::Request_CreateEntity(_Fixture.SceneRoot);
        Root.Request_OverrideToSelf();
        _Fixture.Entities.Add(Root);
        _Body = utils_transform::Add(Root,
            FTransform(_Origin + FVector(0.0, 0.0, ck_procedural_gym::BodyClearance)),
            ECk_Replication::DoesNotReplicate);
        auto UpperEntity = utils_entity_lifetime::Request_CreateEntity(Root);
        auto LowerEntity = utils_entity_lifetime::Request_CreateEntity(Root);
        _Upper = utils_transform::Add(UpperEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        _Lower = utils_transform::Add(LowerEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        auto Parts = TArray<FCk_Handle_Transform>();
        Parts.Add(_Upper);
        Parts.Add(_Lower);
        auto Rig = FCk_ProceduralRig_Spec();
        Rig.Set_Segments(Parts);
        auto Chains = TArray<FCk_ProceduralWalker_LegChain>();
        Chains.Add(FCk_ProceduralWalker_LegChain(n"Leg0", Rig));
        const auto Walker = utils_procedural_animation::Add_Walker(_Body,
            ck::ProceduralGym_Rig4, ck::ProceduralGym_Gait, Chains);
        _Gait = Walker.Get_Gait();
        if (Walker.Get_Legs().Num() == 4)
        { _Leg = Walker.Get_Legs()[0]; }
        _Subject = utils_procedural_articulation::DoCastChecked(Root);
        Assert_True(ck::IsValid(_Gait) && ck::IsValid(_Leg) && ck::IsValid(_Subject),
            "Existing four-leg anatomy presents one bound controllable leg");
    }

    UFUNCTION()
    private void Check_Planted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_Subject) && _Subject.Get_SettledRevision() > 0
            && utils_procedural_gait::Get_PlantedCount(_Gait) == 4);
    }

    UFUNCTION()
    private void Step_BeginReach(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _EntryEndpoint = utils_procedural_leg::Get_Foot(_Leg).Get_Position();
        auto Support = FCk_ProceduralLimbControl_Support();
        Support.Set_Mode(ECk_ProceduralLimbControl_SupportMode::CountOnly);
        Support.Set_MinimumTrustedContacts(3);
        auto Return = FCk_ProceduralLimbControl_Return();
        Return.Set_Duration(FCk_Time(0.3));
        Return.Set_Timeout(FCk_Time(3.0));
        auto Locomotion = FCk_ProceduralLimbControl_Locomotion();
        Locomotion.Set_Support(Support);
        Locomotion.Set_Return(Return);
        auto Spec = FCk_ProceduralActionPlayer_Spec();
        Spec.Set_Locomotion(Locomotion);
        _Player = utils_procedural_action_player::Add(_Subject, Spec);
        _Action = ck_procedural_action_noncombat::BeginCarrierReach(InHandle, _Player, n"Leg0");
        Assert_True(ck::IsValid(_Player) && ck::IsValid(_Action),
            "Carrier helper creates one managed action with explicit three-contact support policy");
    }

    UFUNCTION()
    private void Check_Withdrawn(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Control = _Subject.Get_LimbControl(n"Leg0");
        Result.Set(ck::IsValid(_Action) && _Action.Get_State() == ECk_ProceduralAction_State::Ready
            && Control.Get_State() == ECk_ProceduralLimbControl_State::Controlled
            && Control.Get_Locomotor() && Control.Get_Participating() == false
            && utils_procedural_gait::Get_PlantedCount(_Gait) >= 3);
    }

    UFUNCTION()
    private void Step_SubmitReach(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Layout = _Subject.Get_Limb(n"Leg0").Get_Layout();
        const auto Body = utils_transform::Get_EntityCurrentTransform(_Body);
        const auto Rest = Layout.Get_RestEndpointLocal() * Layout.Get_AttachmentLocal() * Body;
        Log(f"[Noncombat carrier target] rest={Rest.GetLocation()} entry={_EntryEndpoint} restDelta={(Rest.GetLocation() - _EntryEndpoint).Size()} bend={Layout.Get_BendLocal()}");
        ck_procedural_action_noncombat::SubmitRestReach(_Action, n"Leg0",
            FTransform(FVector(0.0, 0.0, 0.005)),
            FCk_Delegate_Request_OnCompleted(this, n"OnReachSubmissionCompleted"));
    }

    UFUNCTION()
    private void Step_SubmitBendOverride(FCk_Handle InHandle, FInstancedStruct InPayload)
    { SubmitBendOverride(FVector::UpVector); }

    UFUNCTION()
    private void Step_SubmitZeroBendOverride(FCk_Handle InHandle, FInstancedStruct InPayload)
    { SubmitBendOverride(FVector::ZeroVector); }

    private void SubmitBendOverride(FVector InDirection)
    {
        _OverrideCallbackSeen = false;
        _OverrideRejected = false;
        auto Target = FCk_ProceduralAction_LimbTarget(n"Leg0", FTransform(FVector(0.0, 0.0, 0.005)));
        Assert_True(Target.Get_HasBend() == false,
            "A new manual limb target preserves its authored solver bend");
        Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        Target.Set_BendDirection(InDirection);
        Assert_True(Target.Get_HasBend(), "Set_BendDirection explicitly opts in to a bend override");
        auto Targets = TArray<FCk_ProceduralAction_LimbTarget>();
        Targets.Add(Target);
        auto Batch = FCk_ProceduralAction_PoseBatch();
        Batch.Set_Limbs(Targets);
        _Action.Request_SubmitPoseBatch(FCk_Request_ProceduralAction_SubmitPoseBatch(Batch),
            FCk_Delegate_Request_OnCompleted(this, n"OnBendOverrideCompleted"));
    }

    UFUNCTION()
    private void OnBendOverrideCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        _OverrideCallbackSeen = true;
        _OverrideRejected = InResult == ECk_Request_OperationResult::Failed;
    }

    UFUNCTION()
    private void Check_BendOverrideRejected(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_OverrideCallbackSeen);
        if (_OverrideCallbackSeen)
        {
            Assert_True(_OverrideRejected && _Action.Get_State() == ECk_ProceduralAction_State::Ready
                && _Action.Get_LastReceipt().Get_HasAcceptedPose() == false,
                "An invalid or over-budget explicit bend rejects without an accepted receipt");
        }
    }

    UFUNCTION()
    private void OnReachSubmissionCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        const auto Body = utils_transform::Get_EntityCurrentTransform(_Body);
        const auto Foot = utils_procedural_leg::Get_Foot(_Leg).Get_Position();
        Log(f"[Noncombat carrier submit] result={InResult} action={_Action.Get_State()} body={Body.GetLocation()} foot={Foot} entry={_EntryEndpoint}");
    }

    UFUNCTION()
    private void Check_Accepted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Receipt = _Action.Get_LastReceipt();
        Result.Set(Receipt.Get_HasAcceptedPose() && Receipt.Get_CurrentPose().Get_Limbs().Num() == 1);
        ++_AcceptancePolls;
        if (_AcceptancePolls % 120 == 0)
        {
            const auto Control = _Subject.Get_LimbControl(n"Leg0");
            const auto ActionResult = _Action.Get_Result();
            Log(f"[Noncombat carrier] polls={_AcceptancePolls} state={_Action.Get_State()} outcome={ActionResult.Get_Outcome()} reason={ActionResult.Get_Reason()} control={Control.Get_State()} controlReason={Control.Get_Reason()} accepted={Receipt.Get_HasAcceptedPose()} limbCount={Receipt.Get_CurrentPose().Get_Limbs().Num()} settledRevision={_Subject.Get_SettledRevision()}");
        }
    }

    UFUNCTION()
    private void Step_VerifyAndEnd(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Limbs = _Action.Get_LastReceipt().Get_CurrentPose().Get_Limbs();
        Assert_True(Limbs.Num() == 1 && Limbs[0].Get_Id() == n"Leg0"
            && Limbs[0].Get_Segments().Num() == 2,
            "Carrier receipt reports the full bound two-segment chain");
        if (Limbs.Num() == 1 && Limbs[0].Get_Segments().Num() == 2)
        {
            const auto Upper = utils_transform::Get_EntityCurrentTransform(_Upper);
            const auto Lower = utils_transform::Get_EntityCurrentTransform(_Lower);
            const auto Travel = (Limbs[0].Get_AcceptedEndpoint().GetLocation() - _EntryEndpoint).Size();
            Assert_True((Upper.GetLocation() - Limbs[0].Get_Segments()[0].GetLocation()).Size() < 0.05
                && (Lower.GetLocation() - Limbs[0].Get_Segments()[1].GetLocation()).Size() < 0.05
                && Travel > 0.3,
                "Borrowed carrier reach moves an actual settled chain from its planted endpoint");
        }
        _Action.Request_End(FCk_Request_ProceduralAction_End());
    }

    UFUNCTION()
    private void Check_Replanted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        const auto Control = _Subject.Get_LimbControl(n"Leg0");
        Result.Set(_Action.Get_State() == ECk_ProceduralAction_State::Finished
            && Control.Get_State() == ECk_ProceduralLimbControl_State::Default
            && Control.Get_Participating() && Control.Get_TrustedSupport());
    }

    UFUNCTION()
    private void Step_VerifyAndDestroy(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Result = _Action.Get_Result();
        Assert_True(Result.Get_Outcome() == ECk_ProceduralAction_Outcome::Completed
            && Result.Get_Recovery() == ECk_ProceduralAction_Recovery::Succeeded,
            "Carrier's withdrawn walking leg replants before action completion");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
