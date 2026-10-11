// Language=angelscript

// The action API is callable from AngelScript in both generated forms: the utils_procedural_body_pose namespace and the
// mixin on a mutable body pose handle; both getter forms read the same input and sample; the delegate is optional.
class UCk_AutoTest_ProceduralAnimation_BodyPoseAction_PublicSurface : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(180000.0, 55000.0, 600.0);
    private int32 _NamespaceCompletions = 0;
    private int32 _MixinCompletions = 0;

    FCk_Handle_ProceduralBodyPose Get_BodyPose() const
    {
        return _Fixture.Crawlers[0].Handles.BodyPose;
    }

    int64 Get_SampleId() const
    {
        return utils_procedural_body_pose::Get_ActionSample(Get_BodyPose()).Get_SampleId();
    }

    void Verify_BothForms(FVector InExpectedAction, int64 InExpectedId, const FString& InCase)
    {
        auto BodyPose = Get_BodyPose();
        auto NamespaceAction = utils_procedural_body_pose::Get_ActionOffset(BodyPose);
        auto MixinAction = BodyPose.Get_ActionOffset();
        auto NamespaceSample = utils_procedural_body_pose::Get_ActionSample(BodyPose);
        auto MixinSample = BodyPose.Get_ActionSample();
        Assert_True(NamespaceAction.GetLocation().Equals(InExpectedAction, 0.001) && MixinAction.Equals(NamespaceAction, 0.0001),
            f"{InCase}: the namespace and mixin action getters both read the processed input");
        Assert_True(NamespaceSample.Get_SampleId() == InExpectedId && MixinSample.Get_SampleId() == InExpectedId,
            f"{InCase}: the namespace and mixin sample getters both read id {InExpectedId}");
        Assert_True(NamespaceSample.Get_PoseRevision() == MixinSample.Get_PoseRevision() &&
            NamespaceSample.Get_WorldTransform().Equals(MixinSample.Get_WorldTransform(), 0.0001),
            f"{InCase}: the namespace and mixin sample getters read the same sample");
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Roster = TArray<ECkProceduralAnimationGym_Species>();
        Roster.Add(ECkProceduralAnimationGym_Species::Crawler4);
        if (_Fixture.Create_WithRoster(InHandle, _Origin, ECkProceduralAnimationGym_Course::Flat, Roster, false,
            ECk_ProceduralBodyPose_ConformMode::None) == false)
        {
            FinishFailure("The isolated single-walker floor fixture could not be created");
            return;
        }
        Add_Step_WaitUntil("the walker and its body pose are composed and ready", n"Check_Ready", 1200);
        Add_Step("request through the namespace form", n"Step_RequestNamespace");
        Add_Step_WaitUntil("the namespace request lands", n"Check_NamespaceLanded");
        Add_Step("verify the getters and request through the mixin form", n"Step_VerifyNamespaceAndRequestMixin");
        Add_Step_WaitUntil("the mixin request lands", n"Check_MixinLanded");
        Add_Step("verify the getters and clear without a delegate", n"Step_VerifyMixinAndClear");
        Add_Step_WaitUntil("the delegate-less clear lands", n"Check_ClearLanded");
        Add_Step("verify the cleared getters", n"Step_VerifyCleared");
        Add_Step_WaitUntil("the fixture is gone", n"Check_Destroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Ready(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        if (_Fixture.CompositionError.IsEmpty() == false)
        {
            FinishFailure(f"The walker could not be composed: {_Fixture.CompositionError}");
            return;
        }
        auto Result = OutResult;
        Result.Set(_Fixture.Get_AllReady());
    }

    UFUNCTION()
    private void Step_RequestNamespace(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_procedural_body_pose::Request_SetActionOffset(Get_BodyPose(),
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform(FVector(5.0, 0.0, 0.0)), int64(1)),
            FCk_Delegate_Request_OnCompleted(this, n"OnNamespace"));
    }

    UFUNCTION()
    private void OnNamespace(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _NamespaceCompletions++;
        Assert_True(InResult == ECk_Request_OperationResult::Succeeded, "The namespace-form request completes Succeeded");
    }

    UFUNCTION()
    private void Check_NamespaceLanded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(_NamespaceCompletions > 0 && Get_SampleId() == 1);
    }

    UFUNCTION()
    private void Step_VerifyNamespaceAndRequestMixin(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_NamespaceCompletions, 1, "The namespace-form request completes exactly once");
        Verify_BothForms(FVector(5.0, 0.0, 0.0), 1, "After the namespace request");
        auto BodyPose = Get_BodyPose();
        BodyPose.Request_SetActionOffset(
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform(FVector(-4.0, 0.0, 2.0)), int64(2)),
            FCk_Delegate_Request_OnCompleted(this, n"OnMixin"));
    }

    UFUNCTION()
    private void OnMixin(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _MixinCompletions++;
        Assert_True(InResult == ECk_Request_OperationResult::Succeeded, "The mixin-form request completes Succeeded");
    }

    UFUNCTION()
    private void Check_MixinLanded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(_MixinCompletions > 0 && Get_SampleId() == 2);
    }

    UFUNCTION()
    private void Step_VerifyMixinAndClear(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_MixinCompletions, 1, "The mixin-form request completes exactly once");
        Verify_BothForms(FVector(-4.0, 0.0, 2.0), 2, "After the mixin request");
        utils_procedural_body_pose::Request_SetActionOffset(Get_BodyPose(),
            FCk_Request_ProceduralBodyPose_SetActionOffset(FTransform::Identity, int64(3)));
    }

    UFUNCTION()
    private void Check_ClearLanded(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update(false);
        auto Result = OutResult;
        Result.Set(Get_SampleId() == 3);
    }

    UFUNCTION()
    private void Step_VerifyCleared(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Verify_BothForms(FVector::ZeroVector, 3, "After the delegate-less clear");
        Assert_Equals_Int(_NamespaceCompletions + _MixinCompletions, 2, "The delegate-less request fired neither earlier delegate");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
