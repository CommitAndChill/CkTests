// Language=angelscript
//
// CK UNREAL COMPONENT - AUTOMATION TEST: a request on a component being destroyed is not enqueued
// With the hosted component set up, the owner entity is passed to
// Request_DestroyEntity, which tags the component entity for destruction on the
// same stack. A Request_SetCustomPrimitiveData issued right after can only ever
// be cancelled, so the boundary refuses it: the completion reports
// Failed_NotEnqueued synchronously, on the calling stack, exactly once, and with
// no ensure (refusing work during teardown is legitimate, not a caller error).
//
// Isolated Y band: 186000. The component has no collision and never bakes, so
// the band is hygiene only.

class UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_RejectedWhileDestroying : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FVector _Origin = FVector(0.0, 186000.0, 0.0);

    private FCk_Handle _Owner;
    private FCk_Handle_UnrealComponent _CompHandle;

    private int32 _Completions = 0;
    private ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Succeeded;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("add a static mesh component", n"Step_Arrange");
        Add_Step_WaitUntil("component instantiated", n"Check_ComponentReady");
        Add_Step("destroy the owner, then request on the same stack", n"Step_DestroyThenRequest");
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
            Archetype, ECk_UnrealComponent_TickPolicy::DoNotTick, n"AutoTest_CpdWhileDestroying");
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
    private void Step_DestroyThenRequest(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_entity_lifetime::Request_DestroyEntity(_Owner);

        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(4, FCk_CustomPrimitiveData_Value(0.75f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));

        Assert_Equals_Int(_Completions, 1, "the request completes synchronously, on the enqueueing stack");
        Assert_True(_Result == ECk_Request_OperationResult::Failed_NotEnqueued,
            "a request on a component entity already being destroyed completes Failed_NotEnqueued");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
        Assert_True(InOwner == _CompHandle, "the completion identifies the component handle as owner");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1,
            "the completion fired exactly once (no later Failed_Cancelled from the EndPlay cancellation)");
    }
}
