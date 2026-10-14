// Language=angelscript
// Cancelling the pending child must win even though its lifetime owner is still alive.
class UCk_AutoTest_EntityScript_DestroyPendingBeforeDrain : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FCk_Handle_PendingEntityScript _Pending;
    private int32 _Completions = 0;
    private int32 _Constructions = 0;
    private ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Succeeded;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("destroy the pending child with its owner alive", n"Step_Arrange");
        Add_Step_WaitUntil("the spawn request completes", n"Check_Completed");
        Add_Step_WaitSeconds("allow end-play cancellation to drain", 0.1f);
        Add_Step("verify cancellation happened exactly once", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Arrange(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        auto Owner = InHandle;
        auto Params = UCk_EntityScript_EntityScriptGym_Spawn::Params();
        Params.InitialTransform = FTransform::Identity;
        _Pending = utils_entity_script::Request_SpawnEntity(Owner,
            UCk_EntityScript_EntityScriptGym_Spawn, FInstancedStruct::Make(Params),
            FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));
        Assert_True(ck::IsValid(_Pending), "spawn was actually enqueued");
        utils_pending_entity_script::Promise_OnConstructed(_Pending,
            FCk_Delegate_EntityScript_Constructed(this, n"OnConstructed"));
        utils_pending_entity_script::Request_DestroyEntity(_Pending);
        Assert_True(ck::IsValid(Owner), "the lifetime owner survives cancellation");
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
    }

    UFUNCTION()
    private void OnConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        ++_Constructions;
    }

    UFUNCTION()
    private void Check_Completed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        Result.Set(_Completions > 0);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_Equals_Int(_Completions, 1, "cancel completes exactly once across request drain and end-play");
        Assert_Equals_Int(_Constructions, 0, "a dying pending entity is never constructed");
        Assert_True(_Result == ECk_Request_OperationResult::Failed_Cancelled,
            "destroying the pending entity cancels while its owner is alive");
        Assert_False(ck::IsValid(_Pending), "the pending entity finishes destruction");
        FinishSuccess();
    }
}
