// Language=angelscript

class UCk_AutoTest_ProceduralAnimation_DetachTransfersPartsToLegAndLegToWorld : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 15.0f;
    default _AutoStageOriginField = false;
    private FCkProceduralAnimationGym_Fixture _Fixture;
    private FVector _Origin = FVector(120000.0, 95000.0, 600.0);
    private float _PhaseStart = 0.0;
    private int32 _EnsuresBefore = 0;
    private FCk_Handle_ProceduralLeg _Leg;
    private FCk_Handle _Body;
    private int32 _Detachments = 0;
    private TArray<FCk_Handle_Transform> _ReleasedParts;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        if (_Fixture.Create(InHandle, _Origin, ECkProceduralAnimationGym_Course::Flat, false, 1) == false)
        {
            FinishFailure("The isolated single-walker floor fixture could not be created");
            return;
        }
        Add_Step_WaitUntil("the walker is composed and evaluated", n"Check_Ready", 1200);
        Add_Step("bind the detach signal", n"Step_Bind");
        Add_Step_WaitUntil("the walker walks for 1 s", n"Check_Walked");
        Add_Step("detach leg 1, handing its parts to the leg and the leg to the world", n"Step_Detach");
        Add_Step_WaitUntil("leg 1 reads Detached", n"Check_Detached");
        Add_Step("verify the ownership and destroy the fixture", n"Step_VerifyOwnershipAndDestroyFixture");
        Add_Step_WaitUntil("the body is gone", n"Check_BodyGone");
        // Two frames cover a child sweep that trails the owner's destruction by a frame.
        Add_Step_WaitFrames("the detached leg outlives the body's destruction", 2);
        Add_Step("verify the leg and its parts survived and destroy the leg", n"Step_VerifySurvivedAndDestroyLeg");
        Add_Step_WaitUntil("the leg and its parts are gone", n"Check_LegTreeGone");
        Add_Step("verify no ensure fired", n"Step_VerifyNoEnsure");
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
        _Body = _Fixture.Crawlers[0].Handles.Root;
        auto Leg = _Leg;
        utils_procedural_leg::BindTo_OnDetached(Leg, FCk_Delegate_ProceduralLeg_OnDetached(this, n"OnDetached"));
        _PhaseStart = float(System::GetGameTimeInSeconds());
    }

    UFUNCTION()
    private void Check_Walked(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update();
        auto Result = OutResult;
        Result.Set(float(System::GetGameTimeInSeconds()) - _PhaseStart >= 1.0);
    }

    UFUNCTION()
    private void Step_Detach(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _EnsuresBefore = utils_ensure::Get_EnsureCount();
        auto Request = FCk_Request_ProceduralLeg_Detach(ECk_ProceduralLeg_ReleasedPartsOwnership::TransferToLeg);
        Request.Set_LegOwnership(ECk_ProceduralLeg_DetachedLegOwnership::TransferToWorld);
        auto Leg = _Leg;
        utils_procedural_leg::Request_Detach(Leg, Request);
    }

    UFUNCTION()
    private void OnDetached(FCk_Handle_ProceduralLeg InLeg, FCk_ProceduralLeg_ReleasedParts InReleasedParts)
    {
        _Detachments++;
        _ReleasedParts = InReleasedParts.Get_Parts();
    }

    UFUNCTION()
    private void Check_Detached(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        _Fixture.Update();
        auto Result = OutResult;
        Result.Set(utils_procedural_leg::Get_Status(_Leg) == ECk_ProceduralLeg_Status::Detached);
    }

    bool Get_RecordContainsLeg() const
    {
        for (auto Leg : utils_procedural_leg::Get_Legs(_Body, ECk_ProceduralLeg_Filter::NoFilter))
        {
            if (Leg == _Leg)
            {
                return true;
            }
        }
        return false;
    }

    UFUNCTION()
    private void Step_VerifyOwnershipAndDestroyFixture(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Detachments, 1, "OnDetached fires exactly once");
        Assert_Equals_Int(_ReleasedParts.Num(), 3, "The released chain carries both segments and the foot");
        auto LegEntity = FCk_Handle(_Leg);
        for (auto Index = 0; Index < _ReleasedParts.Num(); Index++)
        {
            auto PartEntity = FCk_Handle(_ReleasedParts[Index]);
            Assert_True(utils_entity_lifetime::Get_LifetimeOwner(PartEntity) == LegEntity,
                f"Released part {Index} is owned by the detached leg");
        }
        auto LegOwner = utils_entity_lifetime::Get_LifetimeOwner(LegEntity);
        Assert_True(LegOwner != _Body, "The detached leg is no longer owned by the body");
        Assert_True(ck::IsValid(LegOwner) && utils_entity_lifetime::Get_IsTransientEntity(LegOwner),
            "The detached leg is owned by the world's transient entity");
        Assert_True(Get_RecordContainsLeg(), "The body's record keeps the detached leg");
        _Fixture.Request_Destroy();
    }

    UFUNCTION()
    private void Check_BodyGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(ck::Is_NOT_Valid(_Body));
    }

    UFUNCTION()
    private void Step_VerifySurvivedAndDestroyLeg(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(ck::IsValid(_Leg), "The world-owned detached leg outlives its body");
        for (auto Index = 0; Index < _ReleasedParts.Num(); Index++)
        {
            Assert_True(ck::IsValid(_ReleasedParts[Index]), f"Released part {Index} outlives the body with its leg");
        }
        if (ck::IsValid(_Leg))
        {
            auto LegEntity = FCk_Handle(_Leg);
            utils_entity_lifetime::Request_DestroyEntity(LegEntity);
        }
    }

    UFUNCTION()
    private void Check_LegTreeGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Gone = ck::Is_NOT_Valid(_Leg);
        for (auto Part : _ReleasedParts)
        {
            Gone = Gone && ck::Is_NOT_Valid(Part);
        }
        auto Result = OutResult;
        Result.Set(Gone);
    }

    UFUNCTION()
    private void Step_VerifyNoEnsure(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(utils_ensure::Get_EnsureCount() - _EnsuresBefore, 0,
            "Detaching, destroying the body and destroying the detached leg fire no ensure");
    }

    UFUNCTION()
    private void Check_Destroyed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Fixture.Get_IsDestroyed());
    }
}
