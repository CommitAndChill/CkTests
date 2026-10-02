// Language=angelscript

class UCk_AutoTest_ProceduralAnimation_GaitAddRejectsADetachedLeg : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;
    default _AutoStageOriginField = false;
    private FVector _Origin = FVector(120000.0, 97500.0, 1000.0);
    private FCk_Handle _DetachedBody;
    private FCk_Handle _IntactBody;
    private FCk_Handle_Transform _DetachedTransform;
    private FCk_Handle_Transform _IntactTransform;
    private FCk_Handle_ProceduralLeg _DetachedLeg;
    private TArray<ECk_Request_OperationResult> _DetachResults;

    FCk_Handle_Transform CreateBody(FCk_Handle InOwner, FVector InLocation)
    {
        auto Owner = InOwner;
        auto Entity = utils_entity_lifetime::Request_CreateEntity(Owner);
        Entity.Request_OverrideToSelf();
        return utils_transform::Add(Entity, FTransform(InLocation), ECk_Replication::DoesNotReplicate);
    }

    bool HasGait(FCk_Handle InBody)
    {
        return utils_procedural_gait::DoCast(InBody).IsSet();
    }

    bool CreateLegs(FCk_Handle_Transform InBody)
    {
        auto Body = InBody;
        auto Created = true;
        for (auto LegParams : ck::ProceduralGym_Rig4.Get_Legs())
        {
            Created = ck::IsValid(utils_procedural_leg::Create(Body, LegParams)) && Created;
        }
        return Created;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto DetachedBody = CreateBody(InHandle, _Origin);
        _DetachedBody = DetachedBody;
        _DetachedTransform = DetachedBody;
        Assert_True(CreateLegs(DetachedBody), "Precondition: the first body gets its four legs without a gait");

        auto IntactBody = CreateBody(InHandle, _Origin + FVector(0.0, 1500.0, 0.0));
        _IntactBody = IntactBody;
        _IntactTransform = IntactBody;
        Assert_True(CreateLegs(IntactBody), "Precondition: the control body gets its four legs without a gait");

        _DetachedLeg = utils_procedural_leg::Get_Legs(DetachedBody)[1];
        auto Leg = _DetachedLeg;
        utils_procedural_leg::Request_Detach(Leg,
            FCk_Request_ProceduralLeg_Detach(ECk_ProceduralLeg_ReleasedPartsOwnership::KeepBodyOwned),
            FCk_Delegate_Request_OnCompleted(this, n"OnDetachCompleted"));

        Add_Step_WaitUntil("the gait-less leg's detach drains", n"Check_LegDetached");
        Add_Step("a gait is rejected on the body with a detached leg and admitted on the control", n"Step_AddGaits");
        // Three frames let the leg, gait and rig processors run over the admitted and rejected compositions.
        Add_Step_WaitFrames("the world ticks over the admitted and rejected gaits", 3);
        Add_Step("retire both bodies", n"Step_Destroy");
        Add_Step_WaitUntil("both bodies are gone", n"Check_Destroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void OnDetachCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _DetachResults.Add(InResult);
    }

    UFUNCTION()
    private void Check_LegDetached(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_procedural_leg::Get_Status(_DetachedLeg) == ECk_ProceduralLeg_Status::Detached);
    }

    UFUNCTION()
    private void Step_AddGaits(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_DetachResults.Num(), 1, "The detach on a gait-less body completes once");
        if (_DetachResults.Num() > 0)
        {
            Assert_True(_DetachResults[0] == ECk_Request_OperationResult::Succeeded, "The detach on a gait-less body completes Succeeded");
        }
        Assert_Equals_Int(utils_procedural_leg::Get_Legs(_DetachedBody).Num(), 4, "The body keeps the detached leg in its record");

        auto DetachedBody = _DetachedTransform;
        auto EnsuresBefore = utils_ensure::Get_EnsureCount();
        auto RejectedGait = utils_procedural_gait::Add(DetachedBody, ck::ProceduralGym_Gait);
        Assert_True(ck::Is_NOT_Valid(RejectedGait), "Add returns an invalid gait for a body with a detached leg");
        Assert_False(HasGait(_DetachedBody), "The body with a detached leg gets no gait");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - EnsuresBefore, 1, "The rejection fires exactly one ensure");

        auto IntactBody = _IntactTransform;
        EnsuresBefore = utils_ensure::Get_EnsureCount();
        auto AdmittedGait = utils_procedural_gait::Add(IntactBody, ck::ProceduralGym_Gait);
        Assert_True(ck::IsValid(AdmittedGait) && HasGait(_IntactBody), "Control: a body whose legs are all attached admits a gait");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - EnsuresBefore, 0, "Control: no ensure fires");
    }

    UFUNCTION()
    private void Step_Destroy(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_entity_lifetime::Request_DestroyEntity(_DetachedBody);
        utils_entity_lifetime::Request_DestroyEntity(_IntactBody);
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_DetachedBody) && ck::Is_NOT_Valid(_IntactBody));
    }
}

class ACk_AutoTest_ProceduralAnimation_GaitAddRejectsADetachedLeg_Actor : ACk_AutoTestRunner
{
    default _TimeoutSeconds = 6.0f;

    UFUNCTION(BlueprintOverride)
    TSubclassOf<UCk_EntityScript_UE> Get_TestEntityScriptClass() const
    {
        auto Path = FSoftClassPath("/Script/Angelscript.Ck_AutoTest_ProceduralAnimation_GaitAddRejectsADetachedLeg");
        TSubclassOf<UCk_EntityScript_UE> ResolvedClass;
        ResolvedClass = Path.TryLoadClass();
        return ResolvedClass;
    }

    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_ExpectedLogErrors() const
    {
        auto Errors = TArray<FString>();
        Errors.Add("detach legs only after the gait exists.");
        return Errors;
    }
}
