// Language=angelscript
//
// CK RUNTIME MESH DISPLAY - AUTOMATION TEST: requests made while Pending apply once setup is Ready
// The display is added and, on the same stack, a Float at 6 and a Vector4 at 32 are
// requested while its setup is still Pending. Both are held, not dropped and not
// failed: once setup reaches Ready the Float reads back in slot 6, the Vector4 in
// slots 32..35, and each completion reports Succeeded exactly once. A further
// request on the Ready display then completes Succeeded and lands.
//
// Isolated Y band: 188000. The display has no collision, so the band is hygiene only.

class UCk_AutoTest_RuntimeMeshDisplay_SetCustomPrimitiveData_AppliesOnceReady : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;
    default _AutoStageOriginField = false;

    private FVector _Origin = FVector(0.0, 188000.0, 0.0);

    private FCk_Handle_RuntimeMesh _Geometry;
    private FCk_Handle _Owner;
    private FCk_Handle_RuntimeMeshDisplay _Display;

    private int32 _FloatCompletions = 0;
    private ECk_Request_OperationResult _FloatResult = ECk_Request_OperationResult::Failed;
    private int32 _Vector4Completions = 0;
    private ECk_Request_OperationResult _Vector4Result = ECk_Request_OperationResult::Failed;
    private int32 _ReadyCompletions = 0;
    private ECk_Request_OperationResult _ReadyResult = ECk_Request_OperationResult::Failed;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto GeometryEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Spec = FCk_RuntimeMesh_Spec();
        Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
            "/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU")));
        _Geometry = utils_runtime_mesh::Add(GeometryEntity, Spec);

        Add_Step_WaitUntil("the cooked geometry becomes Ready", n"Check_GeometryReady");
        Add_Step("add the display and request a Float at 6 and a Vector4 at 32 in the same step", n"Step_AddAndRequest");
        Add_Step_WaitUntil("setup reached Ready and both held requests completed", n"Check_HeldApplied");
        Add_Step("assert the held values and results", n"Step_VerifyHeldApplied");
        Add_Step("request a Float at 10 on the Ready display", n"Step_RequestOnReady");
        Add_Step_WaitUntil("the Ready request landed and completed", n"Check_ReadyApplied");
        Add_Step("assert the Ready request's value and result", n"Step_VerifyReadyApplied");
        Add_Step_WaitSeconds("observe no second completion", 0.1f);
        Add_Step("assert each completion fired exactly once", n"Step_VerifyOnce");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_GeometryReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_runtime_mesh::Get_SetupState(_Geometry) == ECk_RuntimeMesh_SetupState::Ready);
    }

    private void Request_Value(FCk_CustomPrimitiveData InData, FName InCompletionFunction)
    {
        utils_runtime_mesh_display::Request_SetCustomPrimitiveData(_Display,
            FCk_Request_RuntimeMeshDisplay_SetCustomPrimitiveData(InData),
            FCk_Delegate_Request_OnCompleted(this, InCompletionFunction));
    }

    UFUNCTION()
    private void Step_AddAndRequest(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto DisplayOwner = utils_transform::Add(_Owner, FTransform(FRotator::ZeroRotator, _Origin),
            ECk_Replication::DoesNotReplicate);
        _Display = utils_runtime_mesh_display::Add(DisplayOwner, CkRuntimeMeshGym::Make_DisplaySpec(_Geometry));
        Assert_True(ck::IsValid(_Display), "the display composes on the owner");

        Assert_True(utils_runtime_mesh_display::Get_SetupState(_Display) == ECk_RuntimeMesh_SetupState::Pending,
            "the display is still Pending when the requests are made");

        Request_Value(FCk_CustomPrimitiveData(6, FCk_CustomPrimitiveData_Value(0.75f)), n"OnFloatCompleted");
        Request_Value(FCk_CustomPrimitiveData(32, FCk_CustomPrimitiveData_Value(FVector4(1.0, 2.0, 3.0, 4.0))),
            n"OnVector4Completed");

        Assert_Equals_Int(_FloatCompletions + _Vector4Completions, 0,
            "a request held for setup does not complete on the enqueueing stack");
    }

    UFUNCTION()
    private void OnFloatCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_FloatCompletions;
        _FloatResult = InResult;
        Assert_True(InOwner == _Display, "Float completion identifies the display handle as owner");
        Assert_True(utils_runtime_mesh_display::Get_SetupState(_Display) == ECk_RuntimeMesh_SetupState::Ready,
            "the held Float completes only once setup is Ready");
    }

    UFUNCTION()
    private void OnVector4Completed(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Vector4Completions;
        _Vector4Result = InResult;
        Assert_True(InOwner == _Display, "Vector4 completion identifies the display handle as owner");
        Assert_True(utils_runtime_mesh_display::Get_SetupState(_Display) == ECk_RuntimeMesh_SetupState::Ready,
            "the held Vector4 completes only once setup is Ready");
    }

    UFUNCTION()
    private void OnReadyCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_ReadyCompletions;
        _ReadyResult = InResult;
        Assert_True(InOwner == _Display, "completion identifies the display handle as owner");
    }

    private float32 Get_Slot(int32 InIndex)
    {
        return utils_runtime_mesh_display::Get_CustomPrimitiveDataFloat(_Display, InIndex);
    }

    UFUNCTION()
    private void Check_HeldApplied(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_FloatCompletions > 0 && _Vector4Completions > 0
            && utils_runtime_mesh_display::Get_SetupState(_Display) == ECk_RuntimeMesh_SetupState::Ready
            && Math::IsNearlyEqual(Get_Slot(6), 0.75, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(35), 4.0, 0.0001));
    }

    UFUNCTION()
    private void Step_VerifyHeldApplied(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Float(Get_Slot(6), 0.75f, 0.0001f, "the held Float landed in slot 6");
        Assert_Equals_Float(Get_Slot(7), 0.0f, 0.0001f, "the Float request wrote only slot 6");

        Assert_Equals_Float(Get_Slot(31), 0.0f, 0.0001f, "the Vector4 request wrote nothing below slot 32");
        Assert_Equals_Float(Get_Slot(32), 1.0f, 0.0001f, "Vector4.X lands in slot 32");
        Assert_Equals_Float(Get_Slot(33), 2.0f, 0.0001f, "Vector4.Y lands in slot 33");
        Assert_Equals_Float(Get_Slot(34), 3.0f, 0.0001f, "Vector4.Z lands in slot 34");
        Assert_Equals_Float(Get_Slot(35), 4.0f, 0.0001f, "Vector4.W lands in slot 35");

        Assert_True(_FloatResult == ECk_Request_OperationResult::Succeeded, "the held Float completed Succeeded");
        Assert_True(_Vector4Result == ECk_Request_OperationResult::Succeeded, "the held Vector4 completed Succeeded");
    }

    UFUNCTION()
    private void Step_RequestOnReady(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Request_Value(FCk_CustomPrimitiveData(10, FCk_CustomPrimitiveData_Value(0.5f)), n"OnReadyCompleted");

        Assert_Equals_Int(_ReadyCompletions, 0, "a valid request completes when drained, not on the enqueueing stack");
    }

    UFUNCTION()
    private void Check_ReadyApplied(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_ReadyCompletions > 0 && Math::IsNearlyEqual(Get_Slot(10), 0.5, 0.0001));
    }

    UFUNCTION()
    private void Step_VerifyReadyApplied(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Float(Get_Slot(10), 0.5f, 0.0001f, "the Ready request's Float landed in slot 10");
        Assert_Equals_Float(Get_Slot(6), 0.75f, 0.0001f, "the earlier Float in slot 6 is untouched");
        Assert_True(_ReadyResult == ECk_Request_OperationResult::Succeeded, "the Ready request completed Succeeded");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_FloatCompletions, 1, "the held Float's completion fired exactly once");
        Assert_Equals_Int(_Vector4Completions, 1, "the held Vector4's completion fired exactly once");
        Assert_Equals_Int(_ReadyCompletions, 1, "the Ready request's completion fired exactly once");
    }
}
