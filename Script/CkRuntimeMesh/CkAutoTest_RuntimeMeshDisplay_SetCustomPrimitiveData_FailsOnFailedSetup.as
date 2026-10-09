// Language=angelscript
//
// CK RUNTIME MESH DISPLAY - AUTOMATION TEST: a display whose setup failed fails the request
// The display's Spec names a material soft path that does not exist: the Spec is
// valid, so Add accepts it, and setup then fails with MaterialLoadFailed. A
// Request_SetCustomPrimitiveData made after that is accepted at the boundary (the
// handle is live and the data fits), then the drain ensures and completes Failed
// exactly once. Nothing reads back from a display that never reached Ready.
//
// Isolated Y band: 192000. The display has no collision, so the band is hygiene only.

class UCk_AutoTest_RuntimeMeshDisplay_SetCustomPrimitiveData_FailsOnFailedSetup : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;
    default _AutoStageOriginField = false;

    private FVector _Origin = FVector(0.0, 192000.0, 0.0);

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
        Add_Step("add a display whose material does not exist", n"Step_AddDisplay");
        Add_Step_WaitUntil("the display's setup resolves", n"Check_SetupResolved");
        Add_Step("assert MaterialLoadFailed, then request a Float at 6", n"Step_VerifyFailedThenRequest");
        Add_Step_WaitUntil("request completed", n"Check_Completed");
        Add_Step("assert Failed and no read-back", n"Step_VerifyRejected");
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
        TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
        auto MissingMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            "/CkTests/CkRuntimeMesh/Render/M_DoesNotExist_RuntimeMeshDisplayCpd.M_DoesNotExist_RuntimeMeshDisplayCpd"));
        Materials.Add(MissingMaterial);
        Materials.Add(MissingMaterial);

        auto Visuals = FCk_RuntimeMeshDisplay_Visuals();
        Visuals.Set_Materials(Materials);

        auto Spec = FCk_RuntimeMeshDisplay_Spec();
        Spec.Set_Geometry(_Geometry);
        Spec.Set_Visuals(Visuals);

        _Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto DisplayOwner = utils_transform::Add(_Owner, FTransform(FRotator::ZeroRotator, _Origin),
            ECk_Replication::DoesNotReplicate);
        _Display = utils_runtime_mesh_display::Add(DisplayOwner, Spec);
        Assert_True(ck::IsValid(_Display), "a Spec naming a missing material is still valid, so Add accepts it");
    }

    UFUNCTION()
    private void Check_SetupResolved(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(utils_runtime_mesh_display::Get_SetupState(_Display) != ECk_RuntimeMesh_SetupState::Pending);
    }

    UFUNCTION()
    private void Step_VerifyFailedThenRequest(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(utils_runtime_mesh_display::Get_SetupState(_Display) == ECk_RuntimeMesh_SetupState::Failed,
            "setup fails when a material cannot load");
        Assert_True(utils_runtime_mesh_display::Get_SetupFailure(_Display)
            == ECk_RuntimeMeshDisplay_SetupFailure::MaterialLoadFailed, "the setup failure is MaterialLoadFailed");

        utils_runtime_mesh_display::Request_SetCustomPrimitiveData(_Display,
            FCk_Request_RuntimeMeshDisplay_SetCustomPrimitiveData(
                FCk_CustomPrimitiveData(6, FCk_CustomPrimitiveData_Value(0.75f))),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));

        Assert_Equals_Int(_Completions, 0, "the request is accepted at the boundary and completes when drained");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
        Assert_True(InOwner == _Display, "completion identifies the display handle as owner");
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
        Assert_True(_Result == ECk_Request_OperationResult::Failed, "a display whose setup failed completes Failed");
        Assert_Equals_Float(utils_runtime_mesh_display::Get_CustomPrimitiveDataFloat(_Display, 6), 0.0f, 0.0001f,
            "nothing reads back from a display that never reached Ready");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1, "the completion fired exactly once");
    }
}

class ACk_AutoTest_RuntimeMeshDisplay_SetCustomPrimitiveData_FailsOnFailedSetup_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_RuntimeMeshDisplay_SetCustomPrimitiveData_FailsOnFailedSetup;
    default _TimeoutSeconds = 10.0f;

    // The missing material's load diagnostics, setup's ensure on the failed load, and the drain's ensure on a display
    // that never reached Ready are the behaviour under test.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_ExpectedLogErrors() const
    {
        TArray<FString> Errors;
        Errors.Add("M_DoesNotExist_RuntimeMeshDisplayCpd");
        Errors.Add("failed to load its materials");
        Errors.Add("its setup did not reach Ready");
        return Errors;
    }
}
