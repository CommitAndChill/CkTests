// Language=angelscript
//
// CK RUNTIME MESH DISPLAY - AUTOMATION TEST: a request on a display being destroyed is not enqueued
// With the display Ready, its entity is passed to Request_DestroyEntity, which tags
// it for destruction on the same stack. A Request_SetCustomPrimitiveData issued
// right after can only ever be cancelled, so the boundary refuses it: the
// completion reports Failed_NotEnqueued synchronously, on the calling stack,
// exactly once, and with no ensure (refusing work during teardown is legitimate,
// not a caller error).
//
// Isolated Y band: 191000. The display has no collision, so the band is hygiene only.

class UCk_AutoTest_RuntimeMeshDisplay_SetCustomPrimitiveData_RejectedWhileDestroying : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;
    default _AutoStageOriginField = false;

    private FVector _Origin = FVector(0.0, 191000.0, 0.0);

    private FCk_Handle_RuntimeMesh _Geometry;
    private FCk_Handle _Owner;
    private FCk_Handle_RuntimeMeshDisplay _Display;

    private int32 _Completions = 0;
    private ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Succeeded;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto GeometryEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Spec = FCk_RuntimeMesh_Spec();
        Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
            "/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU")));
        _Geometry = utils_runtime_mesh::Add(GeometryEntity, Spec);

        Add_Step_WaitUntil("the cooked geometry becomes Ready", n"Check_GeometryReady");
        Add_Step("add the display", n"Step_AddDisplay");
        Add_Step_WaitUntil("the display's setup reaches Ready", n"Check_DisplayReady");
        Add_Step("destroy the display's entity, then request on the same stack", n"Step_DestroyThenRequest");
        Add_Step_WaitSeconds("observe no second completion", 0.1f);
        Add_Step("assert the completion fired exactly once", n"Step_VerifyOnce");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_GeometryReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_runtime_mesh::Get_SetupState(_Geometry) == ECk_RuntimeMesh_SetupState::Ready);
    }

    UFUNCTION()
    private void Step_AddDisplay(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto DisplayOwner = utils_transform::Add(_Owner, FTransform(FRotator::ZeroRotator, _Origin),
            ECk_Replication::DoesNotReplicate);
        _Display = utils_runtime_mesh_display::Add(DisplayOwner, CkRuntimeMeshGym::Make_DisplaySpec(_Geometry));
        Assert_True(ck::IsValid(_Display), "the display composes on the owner");
    }

    UFUNCTION()
    private void Check_DisplayReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_runtime_mesh_display::Get_SetupState(_Display) == ECk_RuntimeMesh_SetupState::Ready);
    }

    UFUNCTION()
    private void Step_DestroyThenRequest(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        utils_entity_lifetime::Request_DestroyEntity(_Owner);

        utils_runtime_mesh_display::Request_SetCustomPrimitiveData(_Display,
            FCk_Request_RuntimeMeshDisplay_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(4, FCk_CustomPrimitiveData_Value(0.75f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));

        Assert_Equals_Int(_Completions, 1, "the request completes synchronously, on the enqueueing stack");
        Assert_True(_Result == ECk_Request_OperationResult::Failed_NotEnqueued,
            "a request on a display already being destroyed completes Failed_NotEnqueued");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
        Assert_True(InOwner == _Display, "the completion identifies the display handle as owner");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1,
            "the completion fired exactly once (no later Failed_Cancelled from the EndPlay cancellation)");
    }
}
