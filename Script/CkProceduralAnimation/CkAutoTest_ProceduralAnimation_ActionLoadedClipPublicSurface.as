// Language=angelscript

// A loaded plugin clip drives a noncombat body-only inspection mechanism.
class UCk_AutoTest_ProceduralAnimation_ActionLoadedClipPublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;

    private FCk_Handle_Transform _Root;
    private FCk_Handle_Transform _Presentation;
    private FCk_Handle_ProceduralArticulation _Subject;
    private FCk_Handle_ProceduralActionPlayer _Player;
    private FCk_Handle_ProceduralAction _Action;
    private FCk_ProceduralAction_Identity _Identity;
    private FCk_Handle_Transform _LimbRoot;
    private FCk_Handle_Transform _LimbPart;
    private FCk_Handle_ProceduralArticulation _LimbSubject;
    private FCk_Handle_ProceduralActionPlayer _LimbPlayer;
    private FCk_Handle_ProceduralAction _LimbAction;
    private FCk_ProceduralAction_Identity _LimbIdentity;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _Root = utils_transform::Add(RootEntity, FTransform(FVector(142000.0, 71000.0, 1500.0)),
            ECk_Replication::DoesNotReplicate);
        auto PresentationEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        _Presentation = utils_transform::Add(PresentationEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        auto SubjectSpec = FCk_ProceduralArticulation_Spec();
        SubjectSpec.Set_Presentation(_Presentation);
        _Subject = utils_procedural_articulation::Add(_Root, SubjectSpec);
        _Player = utils_procedural_action_player::Add(_Subject, FCk_ProceduralActionPlayer_Spec());
        auto Clip = Cast<UCk_ProceduralAction_Data>(LoadObject(this,
            "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA"));
        if (ck::IsValid(_Subject) == false || ck::IsValid(_Player) == false || Clip == nullptr)
        {
            FinishFailure("Loaded clip fixture and body-only subject must exist");
            return;
        }
        const auto Validation = utils_procedural_action::Validate_Data(Clip);
        Assert_True(Validation.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid
            && Validation.Get_Advisory() == false,
            "Structured public validation reports the resident clip as valid");
        const auto Soft = TSoftObjectPtr<UCk_ProceduralAction_Data>(FSoftObjectPath(
            "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA"));
        auto SoftSpec = FCk_ProceduralAction_PlaySpec(Soft);
        auto EmptySlots = TArray<FCk_ProceduralAction_SlotBinding>();
        auto EmptyAnchors = TArray<FCk_ProceduralAction_AnchorBinding>();
        auto Tuning = FCk_ProceduralAction_Tuning();
        auto Playback = Tuning.Get_Playback();
        Playback.Set_PlayRate(1.25f);
        Tuning.Set_Playback(Playback);
        SoftSpec.Set_SlotBindings(EmptySlots).Set_AnchorBindings(EmptyAnchors);
        SoftSpec.Set_Tuning(Tuning);
        Assert_True(SoftSpec.Get_SlotBindings().Num() == 0 && SoftSpec.Get_AnchorBindings().Num() == 0
            && SoftSpec.Get_TuningMode() == ECk_ProceduralAction_TuningMode::Override
            && SoftSpec.Get_Tuning().Get_Playback().Get_PlayRate() == 1.25f,
            "Soft play spec binds fluent accessor calls and selects explicit tuning override");
        SoftSpec.Set_TuningMode(ECk_ProceduralAction_TuningMode::PlayerDefault);
        Assert_True(SoftSpec.Get_TuningMode() == ECk_ProceduralAction_TuningMode::PlayerDefault
            && SoftSpec.Get_Tuning().Get_Playback().Get_PlayRate() == 1.0f,
            "Explicit PlayerDefault clears an inactive override");
        const auto Admission = utils_procedural_action_player::Get_AdmissionReport(_Player,
            SoftSpec);
        Assert_True(Admission.Get_Status() == ECk_ProceduralAction_ReportStatus::Valid
            && Admission.Get_Advisory(),
            "Resident clip admission is advisory and does not claim resources");
        auto LoadedSpec = FCk_ProceduralAction_LoadedPlaySpec(Clip);
        LoadedSpec.Set_SlotBindings(EmptySlots).Set_AnchorBindings(EmptyAnchors);
        LoadedSpec.Set_Tuning(Tuning);
        Assert_True(LoadedSpec.Get_SlotBindings().Num() == 0 && LoadedSpec.Get_AnchorBindings().Num() == 0
            && LoadedSpec.Get_TuningMode() == ECk_ProceduralAction_TuningMode::Override
            && LoadedSpec.Get_Tuning().Get_Playback().Get_PlayRate() == 1.25f,
            "Loaded play spec binds fluent accessors on its concrete AngelScript type");
        LoadedSpec.Set_TuningMode(ECk_ProceduralAction_TuningMode::PlayerDefault);
        _Action = utils_procedural_action::Create_FromLoadedClip(Owner, _Player, LoadedSpec);
        if (ck::IsValid(_Action) == false)
        {
            FinishFailure("Loaded clip must reserve a public action instance");
            return;
        }
        _Identity = _Action.Get_Identity();
        Assert_True(_Action.Get_State() == ECk_ProceduralAction_State::PendingAdmission,
            "Loaded clip compiles and binds before the first service tick");
        Add_Step_WaitUntil("loaded body clip reaches a terminal production receipt", n"Check_Finished", 600);
        Add_Step("inspect accepted geometry and terminal result", n"Step_Verify");
        Add_Step("compose an independently bound limb subject", n"Step_ComposeLimb");
        Add_Step_WaitUntil("dedicated limb subject settles", n"Check_LimbSettled", 600);
        Add_Step("play the resident limb clip through a named binding", n"Step_PlayLimb");
        Add_Step_WaitUntil("loaded limb clip reaches terminal", n"Check_LimbFinished", 600);
        Add_Step("inspect the bound limb receipt", n"Step_VerifyLimb");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Finished(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Player.Read_Receipts(_Identity, 0, 64).Get_Terminal());
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Page = utils_procedural_action_player::Read_Receipts(_Player, _Identity, 0, 64);
        bool AcceptedBody = false;
        for (auto Record : Page.Get_Records())
        {
            if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose
                && Record.Get_Receipt().Get_HasAcceptedPose()
                && Record.Get_Receipt().Get_CurrentPose().Get_HasBodyPresentation())
            { AcceptedBody = true; }
        }
        Assert_True(AcceptedBody, "Loaded clip publishes settled noncombat body geometry");
        Assert_True(_Action.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed,
            "Loaded clip completes through the ordinary return path");
    }

    UFUNCTION()
    private void Step_ComposeLimb(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Owner = InHandle;
        auto RootEntity = utils_entity_lifetime::Request_CreateEntity(Owner);
        _LimbRoot = utils_transform::Add(RootEntity,
            FTransform(FVector(143000.0, 71000.0, 1500.0)), ECk_Replication::DoesNotReplicate);
        auto PartEntity = utils_entity_lifetime::Request_CreateEntity(RootEntity);
        _LimbPart = utils_transform::Add(PartEntity, FTransform::Identity,
            ECk_Replication::DoesNotReplicate);
        auto Lengths = TArray<float32>();
        Lengths.Add(100.0f);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>();
        Layouts.Add(FCk_ProceduralLimb_Layout(n"NodArm", FTransform::Identity,
            Lengths, FTransform(FVector(100.0, 0.0, 0.0)), FVector::UpVector));
        auto Parts = TArray<FCk_Handle_Transform>();
        Parts.Add(_LimbPart);
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>();
        Bindings.Add(FCk_ProceduralLimb_Binding(n"NodArm", Parts));
        _LimbSubject = utils_procedural_articulation::Add(_LimbRoot,
            FCk_ProceduralArticulation_Spec(FCk_ProceduralArticulation_Layout(Layouts), Bindings));
        Assert_True(ck::IsValid(_LimbSubject), "Dedicated limb topology composes for loaded content");
    }

    UFUNCTION()
    private void Check_LimbSettled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_LimbSubject) && _LimbSubject.Get_SettledRevision() > 0);
    }

    UFUNCTION()
    private void Step_PlayLimb(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Owner = InHandle;
        _LimbPlayer = utils_procedural_action_player::Add(_LimbSubject,
            FCk_ProceduralActionPlayer_Spec());
        auto Clip = Cast<UCk_ProceduralAction_Data>(LoadObject(this,
            "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA"));
        if (ck::IsValid(_LimbPlayer) == false || Clip == nullptr)
        {
            FinishFailure("Loaded limb clip and player must exist");
            return;
        }
        auto Slots = TArray<FCk_ProceduralAction_SlotBinding>();
        Slots.Add(FCk_ProceduralAction_SlotBinding(n"Nod", n"NodArm"));
        auto Loaded = FCk_ProceduralAction_LoadedPlaySpec(Clip);
        Loaded.Set_SlotBindings(Slots);
        Assert_True(Loaded.Get_SlotBindings().Num() == 1,
            "Concrete loaded play spec exposes the authored limb binding in AngelScript");
        _LimbAction = utils_procedural_action::Create_FromLoadedClip(Owner, _LimbPlayer, Loaded);
        Assert_True(ck::IsValid(_LimbAction), "Resident limb clip reserves an action");
        if (ck::IsValid(_LimbAction)) { _LimbIdentity = _LimbAction.Get_Identity(); }
    }

    UFUNCTION()
    private void Check_LimbFinished(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::IsValid(_LimbPlayer) && _LimbIdentity.Get_Value() > 0
            && _LimbPlayer.Read_Receipts(_LimbIdentity, 0, 64).Get_Terminal());
    }

    UFUNCTION()
    private void Step_VerifyLimb(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        const auto Page = _LimbPlayer.Read_Receipts(_LimbIdentity, 0, 64);
        bool AcceptedBoundLimb = false;
        for (auto Record : Page.Get_Records())
        {
            if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose) { continue; }
            const auto Limbs = Record.Get_Receipt().Get_CurrentPose().Get_Limbs();
            AcceptedBoundLimb = AcceptedBoundLimb || (Limbs.Num() == 1
                && Limbs[0].Get_Id() == n"NodArm" && Limbs[0].Get_Segments().Num() == 1);
        }
        Assert_True(AcceptedBoundLimb,
            "Loaded LimbNod produces settled geometry for its named AS slot binding");
        Assert_True(_LimbAction.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed,
            "Loaded limb clip reaches normal completion and return");
    }
}
