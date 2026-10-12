// Language=angelscript

// A cancelled pending target must never enter Construct or BeginPlay, even when its
// lifetime owner remains live and the spawn request is drained in the same frame.
class UCk_EntityScript_CancelPendingTarget_Probe : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto Owner = utils_entity_lifetime::Get_LifetimeOwner(InHandle);
        utils_entity_tag::Add(Owner, n"AutoTest_CancelPendingTarget_Constructed");
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Owner = utils_entity_lifetime::Get_LifetimeOwner(InHandle);
        utils_entity_tag::Add(Owner, n"AutoTest_CancelPendingTarget_BeganPlay");
    }
}

class UCk_AutoTest_EntityScript_CancelPendingTargetBeforeDrain : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 6.0f;

    private FCk_Handle _Owner;
    private FCk_Handle _Target;
    private int32 _Completions = 0;
    private int32 _ConstructedCallbacks = 0;
    private ECk_Request_OperationResult _Result = ECk_Request_OperationResult::Failed;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("queue spawn and destroy pending target before drain", n"Step_Arrange");
        Add_Step_WaitUntil("cancelled spawn completion arrives", n"Check_Completed");
        Add_Step_WaitSeconds("allow another ECS drain to catch late construction", 0.1f);
        Add_Step("verify cancellation has no constructed body", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Arrange(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Owner = InHandle;
        auto Owner = _Owner;
        auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
            Owner, UCk_EntityScript_CancelPendingTarget_Probe,
            FInstancedStruct(), FCk_Delegate_Request_OnCompleted(this, n"OnCompleted"));
        if (!ck::IsValid(Pending))
        { FinishFailure("spawn request was rejected before target-cancellation setup"); return; }

        _Target = Pending.Get_EntityUnderConstruction();
        utils_pending_entity_script::Promise_OnConstructed(
            Pending, FCk_Delegate_EntityScript_Constructed(this, n"OnConstructed"));
        utils_entity_lifetime::Request_DestroyEntity(_Target);
    }

    UFUNCTION()
    private void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++_Completions;
        _Result = InResult;
    }

    UFUNCTION()
    private void OnConstructed(FCk_Handle_EntityScript InHandle)
    {
        ++_ConstructedCallbacks;
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
        Assert_Equals_Int(_Completions, 1, "cancel completion fires exactly once");
        Assert_True(_Result == ECk_Request_OperationResult::Failed_Cancelled,
            "pending-target cancellation reports Failed_Cancelled");
        Assert_Equals_Int(_ConstructedCallbacks, 0, "OnConstructed never fires");
        Assert_False(utils_entity_tag::Has(_Owner, n"AutoTest_CancelPendingTarget_Constructed"),
            "Construct never mutates the surviving owner");
        Assert_False(utils_entity_tag::Has(_Owner, n"AutoTest_CancelPendingTarget_BeganPlay"),
            "BeginPlay never mutates the surviving owner");
        Assert_Invalid(_Target, "cancelled pending target has no live entity after cleanup");
        FinishSuccess();
    }
}
