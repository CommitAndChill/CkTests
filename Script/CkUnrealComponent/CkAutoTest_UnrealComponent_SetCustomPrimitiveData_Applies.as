// Language=angelscript
//
// CK UNREAL COMPONENT - AUTOMATION TEST: Request_SetCustomPrimitiveData applies
// Once the hosted UStaticMeshComponent exists, every value type lands where it
// was asked to: a Float at 4, a Vector4 at 8..11, a Vector2D at 14..15, a Vector
// at 18..20 and a LinearColor at 24..27. Two writes to slot 30 issued in the same
// step apply in enqueue order, so the later one wins. A neighbour of each write
// stays untouched, and every request's completion delegate reports Succeeded
// exactly once.
//
// Isolated Y band: 181000. The component has no collision and never bakes, so
// the band is hygiene only.

class UCk_AutoTest_UnrealComponent_SetCustomPrimitiveData_Applies : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FVector _Origin = FVector(0.0, 181000.0, 0.0);

    private FCk_Handle _Owner;
    private FCk_Handle_UnrealComponent _CompHandle;

    private int32 _FloatCompletions = 0;
    private ECk_Request_OperationResult _FloatResult = ECk_Request_OperationResult::Failed;
    private int32 _Vector4Completions = 0;
    private ECk_Request_OperationResult _Vector4Result = ECk_Request_OperationResult::Failed;

    // Vector2D, Vector, LinearColor and the two same-slot writes.
    private const int32 NumOtherRequests = 5;
    private int32 _OtherCompletions = 0;
    private int32 _OtherSucceeded = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("add a static mesh component", n"Step_Arrange");
        Add_Step_WaitUntil("component instantiated", n"Check_ComponentReady");
        Add_Step("request every value type, and two writes to slot 30", n"Step_Request");
        Add_Step_WaitUntil("every value landed and every request completed", n"Check_Applied");
        Add_Step("assert slots and results", n"Step_VerifyApplied");
        Add_Step_WaitSeconds("observe no second completion", 0.1f);
        Add_Step("assert each completion fired exactly once", n"Step_VerifyOnce");
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
            Archetype, ECk_UnrealComponent_TickPolicy::DoNotTick, n"AutoTest_CpdApplies");
        Params.Set_StaticWorldBakePolicy(ECk_UnrealComponent_StaticWorldBakePolicy::DoNotBake);

        _CompHandle = utils_unreal_component::Add(_Owner, Params);
    }

    UFUNCTION()
    private void Check_ComponentReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(ck::IsValid(utils_unreal_component::Get_Component(_CompHandle)));
    }

    private void Request_Value(FCk_CustomPrimitiveData InData, FName InCompletionFunction)
    {
        utils_unreal_component::Request_SetCustomPrimitiveData(_CompHandle,
            FCk_Request_UnrealComponent_SetCustomPrimitiveData(InData),
            FCk_Delegate_Request_OnCompleted(this, InCompletionFunction));
    }

    UFUNCTION()
    private void Step_Request(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Request_Value(FCk_CustomPrimitiveData(4, FCk_CustomPrimitiveData_Value(0.75f)), n"OnFloatCompleted");
        Request_Value(FCk_CustomPrimitiveData(8, FCk_CustomPrimitiveData_Value(FVector4(1.0, 2.0, 3.0, 4.0))),
            n"OnVector4Completed");
        Request_Value(FCk_CustomPrimitiveData(14, FCk_CustomPrimitiveData_Value(FVector2D(5.0, 6.0))),
            n"OnOtherCompleted");
        Request_Value(FCk_CustomPrimitiveData(18, FCk_CustomPrimitiveData_Value(FVector(7.0, 8.0, 9.0))),
            n"OnOtherCompleted");
        Request_Value(FCk_CustomPrimitiveData(24, FCk_CustomPrimitiveData_Value(FLinearColor(0.1f, 0.2f, 0.3f, 0.4f))),
            n"OnOtherCompleted");

        Request_Value(FCk_CustomPrimitiveData(30, FCk_CustomPrimitiveData_Value(1.5f)), n"OnOtherCompleted");
        Request_Value(FCk_CustomPrimitiveData(30, FCk_CustomPrimitiveData_Value(2.5f)), n"OnOtherCompleted");

        Assert_Equals_Int(_FloatCompletions + _Vector4Completions + _OtherCompletions, 0,
            "a valid request completes when drained, not on the enqueueing stack");
    }

    UFUNCTION()
    private void OnFloatCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_FloatCompletions;
        _FloatResult = InResult;
        Assert_True(InOwner == _CompHandle, "Float completion identifies the component handle as owner");
    }

    UFUNCTION()
    private void OnVector4Completed(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Vector4Completions;
        _Vector4Result = InResult;
        Assert_True(InOwner == _CompHandle, "Vector4 completion identifies the component handle as owner");
    }

    UFUNCTION()
    private void OnOtherCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_OtherCompletions;
        if (InResult == ECk_Request_OperationResult::Succeeded)
        { ++_OtherSucceeded; }
        Assert_True(InOwner == _CompHandle, "completion identifies the component handle as owner");
    }

    private float32 Get_Slot(int32 InIndex)
    {
        return utils_unreal_component::Get_CustomPrimitiveDataFloat(_CompHandle, InIndex);
    }

    UFUNCTION()
    private void Check_Applied(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_FloatCompletions > 0 && _Vector4Completions > 0 && _OtherCompletions >= NumOtherRequests
            && Math::IsNearlyEqual(Get_Slot(4), 0.75, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(11), 4.0, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(15), 6.0, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(20), 9.0, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(27), 0.4, 0.0001)
            && Math::IsNearlyEqual(Get_Slot(30), 2.5, 0.0001));
    }

    UFUNCTION()
    private void Step_VerifyApplied(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Float(Get_Slot(4), 0.75f, 0.0001f, "Float lands in slot 4");
        Assert_Equals_Float(Get_Slot(5), 0.0f, 0.0001f, "the Float request wrote only slot 4");

        Assert_Equals_Float(Get_Slot(8), 1.0f, 0.0001f, "Vector4.X lands in slot 8");
        Assert_Equals_Float(Get_Slot(9), 2.0f, 0.0001f, "Vector4.Y lands in slot 9");
        Assert_Equals_Float(Get_Slot(10), 3.0f, 0.0001f, "Vector4.Z lands in slot 10");
        Assert_Equals_Float(Get_Slot(11), 4.0f, 0.0001f, "Vector4.W lands in slot 11");
        Assert_Equals_Float(Get_Slot(12), 0.0f, 0.0001f, "the Vector4 request wrote only slots 8..11");

        Assert_Equals_Float(Get_Slot(14), 5.0f, 0.0001f, "Vector2D.X lands in slot 14");
        Assert_Equals_Float(Get_Slot(15), 6.0f, 0.0001f, "Vector2D.Y lands in slot 15");
        Assert_Equals_Float(Get_Slot(16), 0.0f, 0.0001f, "the Vector2D request wrote only slots 14..15");

        Assert_Equals_Float(Get_Slot(18), 7.0f, 0.0001f, "Vector.X lands in slot 18");
        Assert_Equals_Float(Get_Slot(19), 8.0f, 0.0001f, "Vector.Y lands in slot 19");
        Assert_Equals_Float(Get_Slot(20), 9.0f, 0.0001f, "Vector.Z lands in slot 20");
        Assert_Equals_Float(Get_Slot(21), 0.0f, 0.0001f, "the Vector request wrote only slots 18..20");

        Assert_Equals_Float(Get_Slot(24), 0.1f, 0.0001f, "LinearColor.R lands in slot 24");
        Assert_Equals_Float(Get_Slot(25), 0.2f, 0.0001f, "LinearColor.G lands in slot 25");
        Assert_Equals_Float(Get_Slot(26), 0.3f, 0.0001f, "LinearColor.B lands in slot 26");
        Assert_Equals_Float(Get_Slot(27), 0.4f, 0.0001f, "LinearColor.A lands in slot 27");
        Assert_Equals_Float(Get_Slot(28), 0.0f, 0.0001f, "the LinearColor request wrote only slots 24..27");

        Assert_Equals_Float(Get_Slot(30), 2.5f, 0.0001f, "of two writes to slot 30 in one step, the later one wins");
        Assert_Equals_Float(Get_Slot(31), 0.0f, 0.0001f, "the slot 30 writes touched only slot 30");

        Assert_True(_FloatResult == ECk_Request_OperationResult::Succeeded, "Float request completed Succeeded");
        Assert_True(_Vector4Result == ECk_Request_OperationResult::Succeeded, "Vector4 request completed Succeeded");
        Assert_Equals_Int(_OtherSucceeded, NumOtherRequests,
            "the Vector2D, Vector, LinearColor and both slot 30 requests completed Succeeded");
    }

    UFUNCTION()
    private void Step_VerifyOnce(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_FloatCompletions, 1, "Float completion fired exactly once");
        Assert_Equals_Int(_Vector4Completions, 1, "Vector4 completion fired exactly once");
        Assert_Equals_Int(_OtherCompletions, NumOtherRequests,
            "each of the other requests' completions fired exactly once");
    }
}
