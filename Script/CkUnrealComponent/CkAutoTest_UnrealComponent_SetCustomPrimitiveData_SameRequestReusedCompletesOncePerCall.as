// Language=angelscript
//
// CK UNREAL COMPONENT - AUTOMATION TEST: reusing one request struct does not replay a delegate
// One request variable is passed to two Request_SetCustomPrimitiveData calls in
// the same step: the first with a completion delegate, the second without one.
// The delegate belongs to the first call only, so once both requests have been
// drained it has fired exactly once, with Succeeded.
//
// Isolated Y band: 187000. The component has no collision and never bakes, so
// the band is hygiene only.

class UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_SameRequestReusedCompletesOncePerCall : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FVector _Origin = FVector(0.0, 187000.0, 0.0);

    private FCk_Handle _Owner;
    private FCk_Handle_UnrealComponent _CompHandle;

    private int32 _Completions = 0;
    private ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Failed;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("add a static mesh component", n"Step_Arrange");
        Add_Step_WaitUntil("component instantiated", n"Check_ComponentReady");
        Add_Step("pass one request to two calls, only the first with a delegate", n"Step_RequestTwice");
        Add_Step_WaitUntil("value landed and the delegate fired", n"Check_Applied");
        Add_Step("assert slot and result", n"Step_VerifyApplied");
        Add_Step_WaitSeconds("observe no second completion", 0.1f);
        Add_Step("assert the delegate fired exactly once", n"Step_VerifyOnce");
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
            Archetype, ECk_UnrealComponent_TickPolicy::DoNotTick, n"AutoTest_CpdSameRequestReused");
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
    private void Step_RequestTwice(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Request = FCk_Request_UnrealComponent_SetCustomPrimitiveData(
            FCk_CustomPrimitiveData(6, FCk_CustomPrimitiveData_Value(0.5f)));

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle, Request,
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle, Request);

        Assert_Equals_Int(_Completions, 0, "a valid request completes when drained, not on the enqueueing stack");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
        Assert_True(InOwner == _CompHandle, "the completion identifies the component handle as owner");
    }

    private float32 Get_Slot(int32 InIndex)
    {
        return utils_unreal_component::Get_CustomPrimitiveDataFloat(_CompHandle, InIndex);
    }

    UFUNCTION()
    private void Check_Applied(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_Completions > 0 && Math::IsNearlyEqual(Get_Slot(6), 0.5, 0.0001));
    }

    UFUNCTION()
    private void Step_VerifyApplied(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Float(Get_Slot(6), 0.5f, 0.0001f, "the reused request's value lands in slot 6");
        Assert_True(_Result == ECk_Request_OperationResult::Succeeded, "the first call's request completed Succeeded");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1,
            "the delegate bound by the first call fired exactly once; the second call bound none");
    }
}
