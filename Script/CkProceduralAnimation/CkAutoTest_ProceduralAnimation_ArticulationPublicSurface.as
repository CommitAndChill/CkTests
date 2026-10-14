// Language=angelscript

// A standalone zero-limb body uses the public articulation namespace and handle mixin.
class UCk_AutoTest_ProceduralAnimation_ArticulationPublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_ProceduralArticulation _Articulation;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private int32 _RecoveryCompletions = 0;
    private ECk_Request_OperationResult _RecoveryResult = ECk_Request_OperationResult::Failed_NotEnqueued;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto Entity = utils_entity_lifetime::Request_CreateEntity(Owner);
        auto Root = utils_transform::Add(Entity, FTransform(FVector(140000.0, 70000.0, 1000.0)),
            ECk_Replication::DoesNotReplicate);
        if (ck::IsValid(Root) == false)
        {
            FinishFailure("Could not create the standalone articulation root");
            return;
        }

        auto Spec = FCk_ProceduralArticulation_Spec();
        _Articulation = utils_procedural_articulation::Add(Root, Spec);
        if (ck::IsValid(_Articulation) == false)
        {
            FinishFailure("Zero-limb articulation composition failed");
            return;
        }

        Assert_True(utils_procedural_articulation::Get_Status(_Articulation)
            == _Articulation.Get_Status(), "Namespace and mixin read the same initial status");
        Assert_True(ck::IsValid(utils_procedural_articulation::Get_Limb(_Articulation, n"Missing")) == false
            && ck::IsValid(_Articulation.Get_Limb(n"Missing")) == false,
            "Namespace and mixin both reject an unknown limb ID");

        Add_Step_WaitUntil("standalone zero-limb articulation settles", n"Check_Settled", 600);
        Add_Step("verify public articulation getter parity", n"Step_Verify");
        Add_Step_WaitUntil("unplanted recovery request drains", n"Check_RecoveryCompleted", 600);
        Add_Step("verify unplanted recovery result", n"Step_VerifyRecovery");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Settled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_procedural_articulation::Get_SettledRevision(_Articulation) > 0);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto NamespaceStatus = utils_procedural_articulation::Get_Status(_Articulation);
        auto MixinStatus = _Articulation.Get_Status();
        auto NamespaceRevision = utils_procedural_articulation::Get_SettledRevision(_Articulation);
        auto MixinRevision = _Articulation.Get_SettledRevision();
        Assert_True(NamespaceStatus == ECk_ProceduralAnimation_Status::Ready
            && MixinStatus == NamespaceStatus, "Namespace and mixin report Ready after production settlement");
        Assert_True(NamespaceRevision > 0 && MixinRevision == NamespaceRevision,
            "Namespace and mixin read the same settled revision");
        Assert_True(ck::IsValid(utils_procedural_articulation::Get_Limb(_Articulation, n"Missing")) == false
            && ck::IsValid(_Articulation.Get_Limb(n"Missing")) == false,
            "Unknown limb ID remains absent after settlement");

        auto Receipt = FCk_ProceduralAction_Receipt();
        auto Page = FCk_ProceduralAction_ReadPage();
        auto Pose = FCk_ProceduralArticulation_Pose();
        auto Stamp = Receipt.Get_Stamp();
        Assert_True(Receipt.Get_HasAcceptedPose() == false
            && Stamp.Get_SampleSequence() == 0,
            "Default receipt exposes immutable accepted-pose and stamp observations");
        Assert_True(Page.Get_Result() == ECk_ProceduralAction_ReadResult::Pending
            && Page.Get_Records().Num() == 0
            && Page.Get_Terminal() == false,
            "Default read page exposes status, records and terminal observation");
        Assert_True(Pose.Get_HasBodyPresentation() == false && Pose.Get_Limbs().Num() == 0,
            "Default copied pose exposes body and limb observations");

        Assert_True(utils_procedural_articulation::Get_IsAnyLimbControlled(_Articulation) == false
            && _Articulation.Get_IsAnyLimbControlled() == false,
            "Namespace and mixin report no controlled limb on a zero-limb body");
        Assert_True(utils_procedural_articulation::Get_IsAnyLimbReturning(_Articulation) == false
            && _Articulation.Get_IsAnyLimbReturning() == false,
            "Namespace and mixin report no returning limb on a zero-limb body");
        Assert_True(utils_procedural_articulation::Get_UnplantedLimbIds(_Articulation).Num() == 0
            && _Articulation.Get_UnplantedLimbIds().Num() == 0,
            "Namespace and mixin report no unplanted limb on a zero-limb body");

        _Player = utils_procedural_action_player::Add(_Articulation, FCk_ProceduralActionPlayer_Spec());
        if (ck::IsValid(_Player) == false)
        {
            FinishFailure("A default player spec must compose a player on the zero-limb body");
            return;
        }
        _Articulation.Request_RecoverUnplantedLimbs(FCk_Request_ProceduralArticulation_RecoverUnplantedLimbs(),
            FCk_Delegate_Request_OnCompleted(this, n"OnRecoveryCompleted"));
        Assert_True(_RecoveryCompletions == 0, "Recovery completes when the request drains, not at enqueue");
    }

    UFUNCTION()
    private void OnRecoveryCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++_RecoveryCompletions;
        _RecoveryResult = InResult;
    }

    UFUNCTION()
    private void Check_RecoveryCompleted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_RecoveryCompletions > 0);
    }

    UFUNCTION()
    private void Step_VerifyRecovery(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_RecoveryCompletions == 1, "Unplanted recovery completes exactly once");
        Assert_True(_RecoveryResult == ECk_Request_OperationResult::Succeeded,
            "An empty unplanted set recovers successfully without claims");
        Assert_True(utils_procedural_articulation::Get_IsAnyLimbReturning(_Articulation) == false,
            "An empty recovery starts no return");
    }
}
