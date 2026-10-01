// Language=angelscript
//
// CK UNREAL COMPONENT - AUTOMATION TEST: a non-primitive host rejects the request
// The hosted component is a plain USceneComponent. Request_SetCustomPrimitiveData
// is accepted at the boundary (the class is only known once the component
// exists), then the drain ensures and completes Failed exactly once. Nothing is
// written, nothing crashes, and the component survives.
//
// Isolated Y band: 183000.

class UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_RejectsNonPrimitive : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FVector _Origin = FVector(0.0, 183000.0, 0.0);

    private FCk_Handle _Owner;
    private FCk_Handle_UnrealComponent _CompHandle;

    private int32 _Completions = 0;
    private ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Succeeded;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("add a scene component", n"Step_Arrange");
        Add_Step_WaitUntil("component instantiated", n"Check_ComponentReady");
        Add_Step("request Float at 4 on the scene component", n"Step_Request");
        Add_Step_WaitUntil("request completed", n"Check_Completed");
        Add_Step("assert Failed and no write", n"Step_VerifyRejected");
        Add_Step_WaitSeconds("observe no second completion", 0.1f);
        Add_Step("assert the completion fired exactly once", n"Step_VerifyOnce");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Arrange(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        utils_transform::Add(_Owner, FTransform(FRotator::ZeroRotator, _Origin), ECk_Replication::DoesNotReplicate);

        const auto Params = utils_unreal_component::Make_Params(
            USceneComponent, ECk_UnrealComponent_TickPolicy::DoNotTick, n"AutoTest_CpdNonPrimitive");
        _CompHandle = utils_unreal_component::Add(_Owner, Params);
    }

    UFUNCTION()
    private void Check_ComponentReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(ck::IsValid(utils_unreal_component::Get_Component(_CompHandle)));
    }

    UFUNCTION()
    private void Step_Request(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(4, FCk_CustomPrimitiveData_Value(0.75f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));

        Assert_Equals_Int(_Completions, 0, "the request is accepted at the boundary and completes when drained");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
        Assert_True(InOwner == _CompHandle, "completion identifies the component handle as owner");
    }

    UFUNCTION()
    private void Check_Completed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_Completions > 0);
    }

    UFUNCTION()
    private void Step_VerifyRejected(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_Result == ECk_Request_OperationResult::Failed, "a non-primitive host completes Failed");

        auto Component = utils_unreal_component::Get_Component(_CompHandle);
        Assert_True(ck::IsValid(Component), "the scene component survives the rejected request");
        Assert_False(IsValid(Cast<UPrimitiveComponent>(Component)), "the hosted component is not a primitive");
        Assert_Equals_Float(utils_unreal_component::Get_CustomPrimitiveDataFloat(_CompHandle, 4), 0.0f, 0.0001f,
            "nothing reads back from a non-primitive host");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1, "the completion fired exactly once");
    }
}

class ACk_AutoTest_UnrealComponent_SetCustomPrimitiveData_RejectsNonPrimitive_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_RejectsNonPrimitive;
    default _TimeoutSeconds = 6.0f;

    // The drain's ensure on the non-primitive host is the behaviour under test.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_ExpectedLogErrors() const
    {
        TArray<FString> Errors;
        Errors.Add("is not a live PRIMITIVE component");
        return Errors;
    }
}
