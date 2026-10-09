// Language=angelscript

class UCk_AutoTest_RuntimeMesh_SliceParity : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 20.0f;

    private FCk_Handle_RuntimeMesh _Source;
    private FCk_Handle _ResultOwner;
    private FGuid _OperationID = FGuid(617, 101, 901, 14);
    private int32 _TypedCount = 0;
    private int32 _GenericCount = 0;
    private FCk_RuntimeMesh_SliceResult _Result;
    private ECk_Request_OperationResult _GenericOutcome =
        ECk_Request_OperationResult::Failed;
    private FCk_Handle_RuntimeMeshDisplay _Display;
    private FCk_Handle_JoltBody _Body;
    private bool _JoltResolved = false;
    private ECk_JoltBody_SetupState _JoltState = ECk_JoltBody_SetupState::Pending;
    private ECk_JoltBody_SetupFailure _JoltFailure = ECk_JoltBody_SetupFailure::None;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto SourceEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        _ResultOwner = utils_entity_lifetime::Request_CreateEntity(InHandle);
        auto Spec = FCk_RuntimeMesh_Spec();
        Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
            "/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU")));
        _Source = utils_runtime_mesh::Add(SourceEntity, Spec);
        Assert_True(ck::IsValid(_Source), "RuntimeMesh Add composes on the supplied entity");

        Add_Step_WaitUntil("cooked source becomes Ready", n"Check_Ready");
        Add_Step("enqueue slice through AngelScript binding", n"Step_Request");
        Add_Step_WaitUntil("typed and generic delegates resolve", n"Check_Resolved");
        Add_Step("assert reflected result and independent owner", n"Step_AssertResult");
        Add_Step("compose Display and RuntimeConvex on sliced result", n"Step_ComposeResult");
        Add_Step_WaitUntil("Display and Jolt setup resolve", n"Check_CompositionResolved");
        Add_Step("assert composed capability states", n"Step_AssertComposition");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_Ready(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(utils_runtime_mesh::Get_SetupState(_Source)
            == ECk_RuntimeMesh_SetupState::Ready);
    }

    UFUNCTION()
    private void Step_Request(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Request = FCk_Request_RuntimeMesh_Slice();
        Request.Set_OperationID(_OperationID);
        Request.Set_ResultOwner(_ResultOwner);
        auto Plane = FCk_RuntimeMesh_PlaneLocal();
        Plane.Set_PositionCm(FVector(5.0, 0.0, 0.0));
        Plane.Set_Normal(FVector(1.0, 0.0, 0.0));
        Plane.Set_Tangent(FVector(0.0, 1.0, 0.0));
        Request.Set_Plane(Plane);
        utils_runtime_mesh::Request_Slice(_Source, Request,
            FCk_Delegate_RuntimeMesh_OnSliceResolved(this, n"OnSliceResolved"),
            FCk_Delegate_Request_OnCompleted(this, n"OnRequestCompleted"));
        Assert_Equals_Int(_TypedCount, 0, "slice resolves in deferred drain, not submission");
    }

    UFUNCTION()
    private void Check_Resolved(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_TypedCount == 1 && _GenericCount == 1);
    }

    UFUNCTION()
    private void Step_AssertResult(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(_Result.Get_OperationID() == _OperationID,
            "typed result preserves operation ID");
        Assert_True(_Result.Get_Outcome() == ECk_RuntimeMesh_SliceOutcome::Succeeded,
            "slice succeeds through AngelScript binding");
        Assert_True(_GenericOutcome == ECk_Request_OperationResult::Succeeded,
            "generic completion succeeds");
        Assert_True(ck::IsValid(_Result.Get_Positive())
            && ck::IsValid(_Result.Get_Negative()), "both result handles are valid");
        Assert_True(utils_entity_lifetime::Get_LifetimeOwner(_Result.Get_Positive())
            == _ResultOwner, "positive result has independent owner");
        Assert_Equals_Float(float(_Result.Get_PositiveMetrics().Get_VolumeCm3()),
            500.0f, 0.01f, "positive half volume is 500 cm3");
        Assert_Equals_Float(float(_Result.Get_NegativeMetrics().Get_VolumeCm3()),
            500.0f, 0.01f, "negative half volume is 500 cm3");
    }

    UFUNCTION()
    private void Step_ComposeResult(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto PositiveEntity = FCk_Handle(_Result.Get_Positive());
        auto PositiveTransform = utils_transform::Add(PositiveEntity,
            FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 100.0)),
            ECk_Replication::DoesNotReplicate);

        TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
        auto DefaultMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
        Materials.Add(DefaultMaterial);
        Materials.Add(DefaultMaterial);
        auto Visuals = FCk_RuntimeMeshDisplay_Visuals();
        Visuals.Set_Materials(Materials);
        auto DisplaySpec = FCk_RuntimeMeshDisplay_Spec();
        DisplaySpec.Set_Geometry(_Result.Get_Positive());
        DisplaySpec.Set_Visuals(Visuals);
        _Display = utils_runtime_mesh_display::Add(PositiveTransform, DisplaySpec);
        Assert_True(ck::IsValid(_Display), "Display composes on sliced result");

        auto Convex = FCk_JoltBody_RuntimeConvexSpec();
        Convex.Set_PointsCm(utils_runtime_mesh::Copy_LocalVerticesCm(_Result.Get_Positive()));
        auto BodySpec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::RuntimeConvex);
        BodySpec.Set_RuntimeConvex(Convex);
        BodySpec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
        BodySpec.Set_MassKg(1.0f);
        _Body = utils_jolt_body::Add(PositiveEntity, BodySpec);
        Assert_True(ck::IsValid(_Body), "RuntimeConvex composes on sliced result");
        Assert_True(utils_jolt_body::TryPromise_OnSetupResolved(_Body,
            FCk_Delegate_JoltBody_OnSetupResolved(this, n"OnJoltResolved")),
            "Jolt setup promise accepts sliced result");
    }

    UFUNCTION()
    private void Check_CompositionResolved(FCk_Handle InHandle, FCk_SharedBool OutResult,
        FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_JoltResolved && utils_runtime_mesh_display::Get_SetupState(_Display)
            != ECk_RuntimeMesh_SetupState::Pending);
    }

    UFUNCTION()
    private void Step_AssertComposition(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_True(utils_runtime_mesh_display::Get_SetupState(_Display)
            == ECk_RuntimeMesh_SetupState::Ready, "Display reaches Ready");
        Assert_True(utils_runtime_mesh_display::Get_SetupFailure(_Display)
            == ECk_RuntimeMeshDisplay_SetupFailure::None, "Display has no setup failure");
        Assert_True(_JoltState == ECk_JoltBody_SetupState::Ready,
            "RuntimeConvex promise reports Ready");
        Assert_True(_JoltFailure == ECk_JoltBody_SetupFailure::None,
            "RuntimeConvex promise reports no failure");
        Assert_True(utils_jolt_body::Get_SetupState(_Body)
            == ECk_JoltBody_SetupState::Ready, "RuntimeConvex getter agrees with promise");
        Assert_True(utils_jolt_body::Get_SetupDiagnostic(_Body).IsEmpty(),
            "RuntimeConvex Ready has no diagnostic");
    }

    UFUNCTION()
    private void OnJoltResolved(FCk_Handle_JoltBody InBody,
        ECk_JoltBody_SetupState InState, ECk_JoltBody_SetupFailure InFailure)
    {
        _JoltResolved = true;
        _JoltState = InState;
        _JoltFailure = InFailure;
    }

    UFUNCTION()
    private void OnSliceResolved(FCk_RuntimeMesh_SliceResult InResult)
    {
        _TypedCount += 1;
        _Result = InResult;
    }

    UFUNCTION()
    private void OnRequestCompleted(FCk_Handle InRequestOwner,
        ECk_Request_OperationResult InResult)
    {
        _GenericCount += 1;
        _GenericOutcome = InResult;
    }
}
