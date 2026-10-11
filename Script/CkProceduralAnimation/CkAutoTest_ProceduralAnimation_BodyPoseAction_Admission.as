// Language=angelscript

// Every malformed action request is rejected at the call: one ensure, one Failed_NotEnqueued completion, nothing queued,
// and the previously processed action input stays in place.
class UCk_AutoTest_ProceduralAnimation_BodyPoseAction_Admission : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 8.0f;
    default _AutoStageOriginField = false;
    private FVector _Origin = FVector(180000.0, 40000.0, 1000.0);
    private FVector _Retained = FVector(5.0, 0.0, 0.0);
    private FVector _Replacement = FVector(6.0, 0.0, 0.0);
    private FCk_Handle _Body;
    private FCk_Handle _FailedBody;
    private FCk_Handle_ProceduralBodyPose _BodyPose;
    private FCk_Handle_ProceduralBodyPose _FailedBodyPose;
    private TArray<ECk_Request_OperationResult> _Rejections;
    private int32 _Accepted = 0;
    private ECk_Request_OperationResult _LastAcceptedResult = ECk_Request_OperationResult::Failed;

    FCk_Handle_Transform CreateTransform(FCk_Handle InOwner, FVector InLocation)
    {
        auto Owner = InOwner;
        auto Entity = utils_entity_lifetime::Request_CreateEntity(Owner);
        Entity.Request_OverrideToSelf();
        return utils_transform::Add(Entity, FTransform(InLocation), ECk_Replication::DoesNotReplicate);
    }

    FCk_Handle_ProceduralBodyPose CreateBodyPose(FCk_Handle InOwner, FVector InLocation, FCk_Handle_Transform& OutPresentation)
    {
        auto Body = CreateTransform(InOwner, InLocation);
        FCk_Handle_ProceduralLeg First = utils_procedural_leg::Create(Body, ck::ProceduralTest_SideRig.Get_Legs()[0]);
        FCk_Handle_ProceduralLeg Second = utils_procedural_leg::Create(Body, ck::ProceduralTest_SideRig.Get_Legs()[1]);
        auto Gait = utils_procedural_gait::Add(Body, ck::ProceduralGym_Gait);
        OutPresentation = CreateTransform(Body, InLocation);
        Assert_True(ck::IsValid(First) && ck::IsValid(Second) && ck::IsValid(Gait), "Precondition: the body walks on two legs");
        return utils_procedural_body_pose::Add(Gait, FCk_ProceduralBodyPose_Spec(OutPresentation));
    }

    FCk_Request_ProceduralBodyPose_SetActionOffset MakeRequest(FTransform InOffset, int64 InSampleId)
    {
        return FCk_Request_ProceduralBodyPose_SetActionOffset(InOffset, InSampleId);
    }

    void RequestAndAssertRejected(FCk_Handle_ProceduralBodyPose InBodyPose, FCk_Request_ProceduralBodyPose_SetActionOffset InRequest,
        const FString& InCase)
    {
        auto RejectionsBefore = _Rejections.Num();
        auto EnsuresBefore = utils_ensure::Get_EnsureCount();
        auto BodyPose = InBodyPose;
        utils_procedural_body_pose::Request_SetActionOffset(BodyPose, InRequest, FCk_Delegate_Request_OnCompleted(this, n"OnRejected"));
        Assert_Equals_Int(_Rejections.Num() - RejectionsBefore, 1, f"{InCase}: the completion delegate fires exactly once, synchronously");
        if (_Rejections.Num() > RejectionsBefore)
        {
            Assert_True(_Rejections.Last() == ECk_Request_OperationResult::Failed_NotEnqueued,
                f"{InCase}: the request completes Failed_NotEnqueued");
        }
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - EnsuresBefore, 1, f"{InCase}: exactly one ensure fires");
    }

    void AssertRetained(const FString& InCase)
    {
        auto Offset = utils_procedural_body_pose::Get_ActionOffset(_BodyPose);
        Assert_True(Offset.Equals(FTransform(_Retained), 0.0001), f"{InCase}: the previously processed action input is unchanged");
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Presentation = FCk_Handle_Transform();
        _BodyPose = CreateBodyPose(InHandle, _Origin, Presentation);
        _Body = FCk_Handle(_BodyPose);
        auto FailedPresentation = FCk_Handle_Transform();
        _FailedBodyPose = CreateBodyPose(InHandle, _Origin + FVector(0.0, 400.0, 0.0), FailedPresentation);
        _FailedBody = FCk_Handle(_FailedBodyPose);
        Assert_True(ck::IsValid(_BodyPose) && ck::IsValid(_FailedBodyPose), "Precondition: both bodies carry a body pose");

        RequestAndAssertRejected(FCk_Handle_ProceduralBodyPose(), MakeRequest(FTransform(_Retained), int64(7)), "Invalid handle");

        utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(FTransform(_Retained), int64(1)),
            FCk_Delegate_Request_OnCompleted(this, n"OnAccepted"));

        FCk_Handle FailedPresentationEntity = FailedPresentation;
        utils_entity_lifetime::Request_DestroyEntity(FailedPresentationEntity);

        Add_Step_WaitUntil("the first action lands and the second body pose latches its missing presentation", n"Check_Baseline");
        Add_Step("reject every malformed request", n"Step_RejectMalformed");
        // Three frames give the request drain three passes in which a leaked request would have replaced the input.
        Add_Step_WaitFrames("the request drain runs over the rejected calls", 3);
        Add_Step("verify the input survived and a well-formed request still lands", n"Step_VerifyRetainedAndReplace");
        Add_Step_WaitUntil("the well-formed replacement lands", n"Check_Replaced");
        Add_Step("reject a request on a body pending destruction", n"Step_RejectPendingDestroy");
        Add_Step_WaitUntil("both bodies are gone", n"Check_Destroyed");
        Add_Step("verify every completion", n"Step_VerifyCounts");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void OnRejected(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _Rejections.Add(InResult);
    }

    UFUNCTION()
    private void OnAccepted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _Accepted++;
        _LastAcceptedResult = InResult;
    }

    UFUNCTION()
    private void Check_Baseline(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Landed = _Accepted == 1 && utils_procedural_body_pose::Get_ActionOffset(_BodyPose).Equals(FTransform(_Retained), 0.0001);
        auto Failed = utils_procedural_body_pose::Get_Failure(_FailedBodyPose) == ECk_ProceduralBodyPose_Failure::MissingPresentation;
        auto Result = OutResult;
        Result.Set(Landed && Failed);
    }

    UFUNCTION()
    private void Step_RejectMalformed(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_LastAcceptedResult == ECk_Request_OperationResult::Succeeded, "Precondition: the first action completed Succeeded");

        auto NaN = Math::Sqrt(-1.0);
        auto NaNTranslation = FTransform(FVector(NaN, 0.0, 0.0));
        Assert_True(NaNTranslation.ContainsNaN(), "Precondition: the translation carries a NaN");
        RequestAndAssertRejected(_BodyPose, MakeRequest(NaNTranslation, int64(2)), "NaN translation");
        AssertRetained("NaN translation");

        float64 Huge = 1.0e308;
        auto Infinite = FTransform(FVector(0.0, Huge * Huge, 0.0));
        Assert_True(Infinite.ContainsNaN(), "Precondition: the translation is infinite");
        RequestAndAssertRejected(_BodyPose, MakeRequest(Infinite, int64(3)), "Infinite translation");
        AssertRetained("Infinite translation");

        auto Scaled = FTransform(_Replacement);
        Scaled.SetScale3D(FVector(1.5, 1.5, 1.5));
        RequestAndAssertRejected(_BodyPose, MakeRequest(Scaled, int64(4)), "Scale of 1.5");
        AssertRetained("Scale of 1.5");

        auto Flattened = FTransform(_Replacement);
        Flattened.SetScale3D(FVector(1.0, 1.0, 0.0));
        RequestAndAssertRejected(_BodyPose, MakeRequest(Flattened, int64(5)), "Zero scale on one axis");
        AssertRetained("Zero scale on one axis");

        auto ZeroQuat = FTransform(_Replacement);
        ZeroQuat.SetRotation(FQuat(0.0, 0.0, 0.0, 0.0));
        RequestAndAssertRejected(_BodyPose, MakeRequest(ZeroQuat, int64(6)), "Zero quaternion");
        AssertRetained("Zero quaternion");

        auto LongQuat = FTransform(_Replacement);
        LongQuat.SetRotation(FQuat(0.0, 0.0, 0.0, 2.0));
        RequestAndAssertRejected(_BodyPose, MakeRequest(LongQuat, int64(7)), "Non-normalized quaternion");
        AssertRetained("Non-normalized quaternion");

        RequestAndAssertRejected(_BodyPose, MakeRequest(FTransform(_Replacement), int64(0)), "Sample id 0");
        AssertRetained("Sample id 0");

        RequestAndAssertRejected(_BodyPose, MakeRequest(FTransform(_Replacement), int64(-3)), "Negative sample id");
        AssertRetained("Negative sample id");

        RequestAndAssertRejected(_FailedBodyPose, MakeRequest(FTransform(_Replacement), int64(8)), "Body pose with a latched failure");
    }

    UFUNCTION()
    private void Step_VerifyRetainedAndReplace(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        AssertRetained("After three drain passes");
        Assert_Equals_Int(_Accepted, 1, "No rejected request reached the drain");

        auto EnsuresBefore = utils_ensure::Get_EnsureCount();
        auto RejectionsBefore = _Rejections.Num();
        utils_procedural_body_pose::Request_SetActionOffset(_BodyPose, MakeRequest(FTransform(_Replacement), int64(9)),
            FCk_Delegate_Request_OnCompleted(this, n"OnAccepted"));
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - EnsuresBefore, 0, "Positive control: a well-formed request fires no ensure");
        Assert_Equals_Int(_Rejections.Num() - RejectionsBefore, 0, "Positive control: a well-formed request is queued, not completed");
        Assert_Equals_Int(_Accepted, 1, "Positive control: a well-formed request does not complete synchronously");
    }

    UFUNCTION()
    private void Check_Replaced(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Accepted == 2);
    }

    UFUNCTION()
    private void Step_RejectPendingDestroy(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_LastAcceptedResult == ECk_Request_OperationResult::Succeeded, "The well-formed replacement completed Succeeded");
        Assert_True(utils_procedural_body_pose::Get_ActionOffset(_BodyPose).Equals(FTransform(_Replacement), 0.0001),
            "The well-formed replacement became the processed input");

        utils_entity_lifetime::Request_DestroyEntity(_Body);
        RequestAndAssertRejected(_BodyPose, MakeRequest(FTransform(_Retained), int64(10)), "Body pending destruction");
        utils_entity_lifetime::Request_DestroyEntity(_FailedBody);
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Body) && ck::Is_NOT_Valid(_FailedBody));
    }

    UFUNCTION()
    private void Step_VerifyCounts(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto NotEnqueued = 0;
        for (auto Result : _Rejections)
        {
            NotEnqueued += Result == ECk_Request_OperationResult::Failed_NotEnqueued ? 1 : 0;
        }
        Assert_Equals_Int(_Rejections.Num(), 11, "Each of the eleven rejected requests completed exactly once");
        Assert_Equals_Int(NotEnqueued, 11, "Each rejection completed Failed_NotEnqueued");
        Assert_Equals_Int(_Accepted, 2, "The two well-formed requests each completed exactly once");
    }
}

class ACk_AutoTest_ProceduralAnimation_BodyPoseAction_Admission_Actor : ACk_AutoTestRunner
{
    default _TimeoutSeconds = 8.0f;

    UFUNCTION(BlueprintOverride)
    TSubclassOf<UCk_EntityScript_UE> Get_TestEntityScriptClass() const
    {
        auto Path = FSoftClassPath("/Script/Angelscript.Ck_AutoTest_ProceduralAnimation_BodyPoseAction_Admission");
        TSubclassOf<UCk_EntityScript_UE> ResolvedClass;
        ResolvedClass = Path.TryLoadClass();
        return ResolvedClass;
    }

    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_ExpectedLogErrors() const
    {
        auto Errors = TArray<FString>();
        Errors.Add("Procedural body pose Request_SetActionOffset rejected body pose");
        Errors.Add("lost its presentation entity; feature is failed");
        return Errors;
    }
}
