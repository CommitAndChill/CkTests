// Language=angelscript
//
// CK UNREAL COMPONENT - AUTOMATION TEST: a pending request is cancelled by destruction
// With the hosted component set up, Request_SetCustomPrimitiveData is issued and
// the owner entity is destroyed on the same stack. The destroy cascade tags the
// component entity before the request can drain, so the request never applies:
// its completion reports Failed_Cancelled exactly once.
//
// Isolated Y band: 185000. The component has no collision and never bakes, so
// the band is hygiene only.

class UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_CancelledOnDestroy : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FVector _Origin = FVector(0.0, 185000.0, 0.0);

    private FCk_Handle _Owner;
    private FCk_Handle_UnrealComponent _CompHandle;

    private int32 _Completions = 0;
    private ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Succeeded;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("add a static mesh component", n"Step_Arrange");
        Add_Step_WaitUntil("component instantiated", n"Check_ComponentReady");
        Add_Step("request, then destroy the owner on the same stack", n"Step_RequestThenDestroy");
        Add_Step_WaitUntil("request completed", n"Check_Completed");
        Add_Step("assert Failed_Cancelled", n"Step_VerifyCancelled");
        Add_Step_WaitSeconds("observe no second completion", 0.1f);
        Add_Step("assert the completion fired exactly once", n"Step_VerifyOnce");
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
            Archetype, ECk_UnrealComponent_TickPolicy::DoNotTick, n"AutoTest_CpdCancelled");
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
    private void Step_RequestThenDestroy(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(4, FCk_CustomPrimitiveData_Value(0.75f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));

        utils_entity_lifetime::Request_DestroyEntity(_Owner);

        Assert_Equals_Int(_Completions, 0, "the request is queued, not completed on the enqueueing stack");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
    }

    UFUNCTION()
    private void Check_Completed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_Completions > 0);
    }

    UFUNCTION()
    private void Step_VerifyCancelled(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_Result == ECk_Request_OperationResult::Failed_Cancelled,
            "a request pending when its component entity is destroyed completes Failed_Cancelled");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1, "the completion fired exactly once");
    }
}
