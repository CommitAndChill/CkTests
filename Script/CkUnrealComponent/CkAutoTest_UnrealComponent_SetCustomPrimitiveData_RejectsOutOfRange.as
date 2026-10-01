// Language=angelscript
//
// CK UNREAL COMPONENT - AUTOMATION TEST: out-of-range data is rejected at the boundary
// The engine holds 36 custom primitive data floats (indices 0..35). A Float at
// index 36, a Vector4 at index 33 (it would need 33..36) and a Float at index -1
// each ensure and complete Failed_NotEnqueued synchronously, on the calling
// stack. Nothing is queued: the slots written by an earlier valid baseline keep
// their values. The baseline includes the accepted boundary: a Vector4 at index
// 32 fills exactly the last four slots, 32..35.
//
// Isolated Y band: 184000. The component has no collision and never bakes, so
// the band is hygiene only.

class UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_RejectsOutOfRange : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FVector _Origin = FVector(0.0, 184000.0, 0.0);

    private FCk_Handle _Owner;
    private FCk_Handle_UnrealComponent _CompHandle;

    private const int32 NumBaselineRequests = 3;
    private int32 _BaselineCompletions = 0;
    private int32 _BaselineSucceeded = 0;
    private int32 _RejectedCompletions = 0;
    private int32 _RejectedNotEnqueued = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("add a static mesh component", n"Step_Arrange");
        Add_Step_WaitUntil("component instantiated", n"Check_ComponentReady");
        Add_Step("write a valid baseline at 28, 30 and the Vector4 boundary 32..35", n"Step_WriteBaseline");
        Add_Step_WaitUntil("baseline landed", n"Check_BaselineApplied");
        Add_Step("issue the three out-of-range requests", n"Step_RequestOutOfRange");
        Add_Step_WaitSeconds("observe that nothing was queued", 0.1f);
        Add_Step("assert slots unchanged and no further completions", n"Step_VerifyUnchanged");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Arrange(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Cube = Cast<UStaticMesh>(LoadObject(UStaticMesh, "/Engine/BasicShapes/Cube.Cube"));
        if (!IsValid(Cube))
        {
            FinishFailure("Failed to load /Engine/BasicShapes/Cube.Cube");
            return;
        }

        _Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        utils_transform::Add(_Owner, FTransform(FRotator::ZeroRotator, _Origin), ECk_Replication::DoesNotReplicate);

        auto Archetype = NewObject(this, UStaticMeshComponent);
        Archetype.SetStaticMesh(Cube);
        Archetype.SetMobility(EComponentMobility::Movable);
        Archetype.SetCollisionEnabled(ECollisionEnabled::NoCollision);

        auto Params = utils_unreal_component::Make_Params_FromArchetype(
            Archetype, ECk_UnrealComponent_TickPolicy::DoNotTick, n"AutoTest_CpdOutOfRange");
        Params.Set_StaticWorldBakePolicy(ECk_UnrealComponent_StaticWorldBakePolicy::DoNotBake);

        _CompHandle = utils_unreal_component::Add(_Owner, Params);
    }

    UFUNCTION()
    private void Check_ComponentReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(ck::IsValid(utils_unreal_component::Get_Component(_CompHandle)));
    }

    UFUNCTION()
    private void Step_WriteBaseline(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(28, FCk_CustomPrimitiveData_Value(0.5f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnBaselineCompleted"));

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(30, FCk_CustomPrimitiveData_Value(0.25f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnBaselineCompleted"));

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(32, FCk_CustomPrimitiveData_Value(FVector4(1.0, 2.0, 3.0, 4.0)))),
            FCk_Delegate_Request_OnCompleted(this, n"OnBaselineCompleted"));
    }

    UFUNCTION()
    private void OnBaselineCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_BaselineCompletions;
        if (InResult == ECk_Request_OperationResult::Succeeded)
        { ++_BaselineSucceeded; }
    }

    private float32 Get_Slot(int32 InIndex)
    {
        return utils_unreal_component::Get_CustomPrimitiveDataFloat(_CompHandle, InIndex);
    }

    UFUNCTION()
    private void Check_BaselineApplied(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_BaselineCompletions >= NumBaselineRequests
            && Math::IsNearlyEqual(Get_Slot(28), 0.5, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(30), 0.25, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(35), 4.0, 0.0001));
    }

    UFUNCTION()
    private void Step_RequestOutOfRange(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_BaselineSucceeded, NumBaselineRequests,
            "every baseline request completed Succeeded, including the Vector4 at the boundary index 32");

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(36, FCk_CustomPrimitiveData_Value(9.0f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnRejectedCompleted"));
        Assert_Equals_Int(_RejectedCompletions, 1, "a Float at index 36 is rejected synchronously");

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(33, FCk_CustomPrimitiveData_Value(FVector4(9.0, 9.0, 9.0, 9.0)))),
            FCk_Delegate_Request_OnCompleted(this, n"OnRejectedCompleted"));
        Assert_Equals_Int(_RejectedCompletions, 2, "a Vector4 at index 33 is rejected synchronously");

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(-1, FCk_CustomPrimitiveData_Value(9.0f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnRejectedCompleted"));
        Assert_Equals_Int(_RejectedCompletions, 3, "a Float at index -1 is rejected synchronously");

        Assert_Equals_Int(_RejectedNotEnqueued, 3, "every out-of-range request completed Failed_NotEnqueued");
    }

    UFUNCTION()
    private void OnRejectedCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_RejectedCompletions;
        if (InResult == ECk_Request_OperationResult::Failed_NotEnqueued)
        { ++_RejectedNotEnqueued; }
        Assert_True(InOwner == _CompHandle, "rejection identifies the component handle as owner");
    }

    UFUNCTION()
    private void Step_VerifyUnchanged(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Float(Get_Slot(28), 0.5f, 0.0001f, "slot 28 keeps the baseline value");
        Assert_Equals_Float(Get_Slot(29), 0.0f, 0.0001f, "slot 29 is untouched");
        Assert_Equals_Float(Get_Slot(30), 0.25f, 0.0001f, "slot 30 keeps the baseline value");
        Assert_Equals_Float(Get_Slot(31), 0.0f, 0.0001f, "slot 31 is untouched");
        Assert_Equals_Float(Get_Slot(32), 1.0f, 0.0001f, "the boundary Vector4.X stays in slot 32");
        Assert_Equals_Float(Get_Slot(33), 2.0f, 0.0001f, "the boundary Vector4.Y stays in slot 33");
        Assert_Equals_Float(Get_Slot(34), 3.0f, 0.0001f, "the boundary Vector4.Z stays in slot 34");
        Assert_Equals_Float(Get_Slot(35), 4.0f, 0.0001f, "the boundary Vector4.W stays in slot 35");

        Assert_Equals_Int(_RejectedCompletions, 3, "no rejected request completed a second time");
        Assert_Equals_Int(_BaselineCompletions, NumBaselineRequests, "no baseline request completed a second time");
    }
}

class ACk_AutoTest_UnrealComponent_SetCustomPrimitiveData_RejectsOutOfRange_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_RejectsOutOfRange;
    default _TimeoutSeconds = 6.0f;

    // The boundary ensure on each out-of-range request is the behaviour under test.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_ExpectedLogErrors() const
    {
        TArray<FString> Errors;
        Errors.Add("does not fit the engine's");
        return Errors;
    }
}
