// Language=angelscript

class UCk_AutoTest_ProceduralAnimation_DetachKeepsLegInRecordWithDetachedStatus : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(120000.0, 82500.0, 600.0);
    private float _PhaseStart = 0.0;
    private int32 _EnsuresBefore = 0;
    private FCk_Handle_ProceduralLeg _Leg;
    private FName _LegId;
    private int32 _Detachments = 0;
    private int32 _LegSetChanges = 0;
    private int32 _EnabledCount = -1;
    private int32 _TotalCount = -1;
    private int32 _Plants = 0;
    private int32 _Lifts = 0;
    private int32 _PlantsAtRepeat = 0;
    private int32 _LiftsAtRepeat = 0;
    private TArray<ECk_Request_OperationResult> _DetachResults;
    private TArray<ECk_Request_OperationResult> _EnableDisableResults;
    private FVector _BodyAtRepeat;

    float Get_Elapsed() const
    {
        return float(System::GetGameTimeInSeconds()) - _PhaseStart;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        if (_Fixture.Create(InHandle, _Origin, ECkProceduralAnimationGym_Course::Flat, false, 1) == false)
        {
            FinishFailure("The isolated single-walker floor fixture could not be created");
            return;
        }
        Add_Step_WaitUntil("the walker is composed and evaluated", n"Check_Ready", 1200);
        Add_Step("bind the leg and gait signals", n"Step_Bind");
        Add_Step_WaitUntil("the walker walks for 1 s", n"Check_Walked");
        Add_Step("detach leg 1", n"Step_Detach");
        Add_Step_WaitUntil("leg 1 reads Detached", n"Check_Detached");
        Add_Step("verify the record keeps the detached leg, then request it again", n"Step_VerifyAndRepeat");
        Add_Step_WaitUntil("the survivors walk for 1 s", n"Check_WalkedAfterRepeat");
        Add_Step("verify the detached leg stayed silent", n"Step_VerifySilence");
        Add_Step_WaitUntil("the fixture is gone", n"Check_Destroyed");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Ready(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update();
        if (_Fixture.CompositionError.IsEmpty() == false)
        {
            FinishFailure(f"The walker could not be composed: {_Fixture.CompositionError}");
            return;
        }
        auto Result = OutResult;
        Result.Set(_Fixture.Get_AllReady());
    }

    UFUNCTION()
    private void Step_Bind(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Leg = _Fixture.Crawlers[0].Handles.Legs[1];
        _LegId = utils_procedural_leg::Get_Id(_Leg);
        auto Leg = _Leg;
        utils_procedural_leg::BindTo_OnDetached(Leg, FCk_Delegate_ProceduralLeg_OnDetached(this, n"OnDetached"));
        utils_procedural_leg::BindTo_OnPlanted(Leg, FCk_Delegate_ProceduralLeg_OnPlanted(this, n"OnPlanted"));
        utils_procedural_leg::BindTo_OnLifted(Leg, FCk_Delegate_ProceduralLeg_OnLifted(this, n"OnLifted"));
        auto Gait = _Fixture.Crawlers[0].Handles.Gait;
        utils_procedural_gait::BindTo_OnLegSetChanged(Gait, FCk_Delegate_ProceduralGait_OnLegSetChanged(this, n"OnLegSetChanged"));
        _PhaseStart = float(System::GetGameTimeInSeconds());
    }

    UFUNCTION()
    private void Check_Walked(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update();
        auto Result = OutResult;
        Result.Set(Get_Elapsed() >= 1.0);
    }

    UFUNCTION()
    private void Step_Detach(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _EnsuresBefore = utils_ensure::Get_EnsureCount();
        auto Leg = _Leg;
        utils_procedural_leg::Request_Detach(Leg,
            FCk_Request_ProceduralLeg_Detach(ECk_ProceduralLeg_ReleasedPartsOwnership::KeepBodyOwned),
            FCk_Delegate_Request_OnCompleted(this, n"OnDetachCompleted"));
    }

    UFUNCTION()
    private void OnDetached(FCk_Handle_ProceduralLeg InLeg, FCk_ProceduralLeg_ReleasedParts InReleasedParts)
    {
        _Detachments++;
    }

    UFUNCTION()
    private void OnPlanted(FCk_Handle_ProceduralLeg InLeg, FCk_ProceduralLeg_Footfall InFootfall)
    {
        _Plants++;
    }

    UFUNCTION()
    private void OnLifted(FCk_Handle_ProceduralLeg InLeg, FCk_ProceduralLeg_Footfall InFootfall)
    {
        _Lifts++;
    }

    UFUNCTION()
    private void OnLegSetChanged(FCk_Handle_ProceduralGait InGait, int32 InEnabledCount, int32 InTotalCount)
    {
        _LegSetChanges++;
        _EnabledCount = InEnabledCount;
        _TotalCount = InTotalCount;
    }

    UFUNCTION()
    private void OnDetachCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _DetachResults.Add(InResult);
    }

    UFUNCTION()
    private void OnEnableDisableCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        _EnableDisableResults.Add(InResult);
    }

    UFUNCTION()
    private void Check_Detached(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update();
        auto Result = OutResult;
        Result.Set(utils_procedural_leg::Get_Status(_Leg) == ECk_ProceduralLeg_Status::Detached);
    }

    bool Get_ContainsLeg(const TArray<FCk_Handle_ProceduralLeg>& InLegs) const
    {
        for (auto Leg : InLegs)
        {
            if (Leg == _Leg)
            {
                return true;
            }
        }
        return false;
    }

    UFUNCTION()
    private void Step_VerifyAndRepeat(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Crawler = _Fixture.Crawlers[0];
        auto Root = Crawler.Handles.Root;
        Assert_True(ck::IsValid(_Leg), "The detached leg entity is still alive");
        Assert_False(utils_procedural_leg::Get_IsAttached(_Leg), "The detached leg is not attached");
        Assert_True(utils_procedural_leg::Get_EnableDisable(_Leg) == ECk_EnableDisable::Disable, "The detached leg reads Disable");
        Assert_True(utils_procedural_leg::Get_Id(_Leg) == _LegId, "The detached leg keeps its Id");
        Assert_Equals_Int(utils_procedural_leg::Get_Legs(Root).Num(), 4, "The record keeps all four legs");
        Assert_Equals_Int(utils_procedural_leg::Get_Legs(Root, ECk_ProceduralLeg_Filter::OnlyAttached).Num(), 3, "Three legs are attached");
        auto DetachedLegs = utils_procedural_leg::Get_Legs(Root, ECk_ProceduralLeg_Filter::OnlyDetached);
        Assert_Equals_Int(DetachedLegs.Num(), 1, "One leg is detached");
        Assert_True(Get_ContainsLeg(DetachedLegs), "The detached filter lists the detached leg");
        Assert_Equals_Int(utils_procedural_leg::Get_Legs(Root, ECk_ProceduralLeg_Filter::OnlyEnabled).Num(), 3, "Three legs are enabled");
        Assert_Equals_Int(utils_procedural_leg::Get_Legs(Root, ECk_ProceduralLeg_Filter::OnlyDisabled).Num(), 0, "No leg is disabled");
        Assert_True(utils_procedural_leg::TryGet_Leg(Root, _LegId) == _Leg, "The Id still resolves to the detached leg");
        Assert_False(utils_procedural_rig::DoCast(_Leg).IsSet(), "The detached leg has no rig");
        Assert_Equals_Int(utils_procedural_gait::Get_EnabledLegCount(Crawler.Handles.Gait), 3, "The gait counts three enabled legs");
        Assert_True(utils_procedural_gait::Get_PlantedCount(Crawler.Handles.Gait) <= 3, "At most the three survivors count as planted");
        Assert_Equals_Int(_LegSetChanges, 1, "OnLegSetChanged fires exactly once");
        Assert_Equals_Int(_EnabledCount, 3, "The leg-set change reports three enabled legs");
        Assert_Equals_Int(_TotalCount, 4, "The leg-set change reports four legs");
        Assert_True(utils_procedural_gait::Get_Status(Crawler.Handles.Gait) == ECk_ProceduralAnimation_Status::Ready,
            "The gait keeps evaluating on the survivors");
        Assert_Equals_Int(_Detachments, 1, "OnDetached fires exactly once");
        Assert_Equals_Int(_DetachResults.Num(), 1, "The detach request completes exactly once");
        if (_DetachResults.Num() > 0)
        {
            Assert_True(_DetachResults[0] == ECk_Request_OperationResult::Succeeded, "The detach request completes Succeeded");
        }

        auto Leg = _Leg;
        utils_procedural_leg::Request_Detach(Leg,
            FCk_Request_ProceduralLeg_Detach(ECk_ProceduralLeg_ReleasedPartsOwnership::KeepBodyOwned),
            FCk_Delegate_Request_OnCompleted(this, n"OnDetachCompleted"));
        utils_procedural_leg::Request_EnableDisable(Leg, FCk_Request_ProceduralLeg_EnableDisable(ECk_EnableDisable::Enable),
            FCk_Delegate_Request_OnCompleted(this, n"OnEnableDisableCompleted"));
        Assert_Equals_Int(_DetachResults.Num(), 2, "A second detach completes synchronously");
        if (_DetachResults.Num() > 1)
        {
            Assert_True(_DetachResults[1] == ECk_Request_OperationResult::Failed_NotEnqueued,
                "A second detach on a detached leg completes Failed_NotEnqueued");
        }
        Assert_Equals_Int(_EnableDisableResults.Num(), 1, "An enable request completes synchronously");
        if (_EnableDisableResults.Num() > 0)
        {
            Assert_True(_EnableDisableResults[0] == ECk_Request_OperationResult::Failed_NotEnqueued,
                "An enable request on a detached leg completes Failed_NotEnqueued");
        }
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "Detaching and the rejected requests fire no ensure");

        _PlantsAtRepeat = _Plants;
        _LiftsAtRepeat = _Lifts;
        _BodyAtRepeat = utils_transform::Get_EntityCurrentLocation(Root);
        _PhaseStart = float(System::GetGameTimeInSeconds());
    }

    UFUNCTION()
    private void Check_WalkedAfterRepeat(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update();
        auto Result = OutResult;
        Result.Set(Get_Elapsed() >= 1.0);
    }

    UFUNCTION()
    private void Step_VerifySilence(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Root = _Fixture.Crawlers[0].Handles.Root;
        Assert_Equals_Int(_Plants - _PlantsAtRepeat, 0, "The detached leg fires no plant");
        Assert_Equals_Int(_Lifts - _LiftsAtRepeat, 0, "The detached leg fires no lift");
        Assert_Equals_Int(_Detachments, 1, "OnDetached still fired only once");
        Assert_True(utils_procedural_leg::Get_Status(_Leg) == ECk_ProceduralLeg_Status::Detached, "The leg still reads Detached");
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0, "No ensure fired after the detach");
        auto Travel = (utils_transform::Get_EntityCurrentLocation(Root) - _BodyAtRepeat).Size();
        Assert_True(Travel > 50.0, f"The survivors keep walking ({Travel :.1} cm)");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
