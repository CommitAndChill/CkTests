// Language=angelscript

//============================================================================
// CK AUTOMATION TEST - NETWORKED INVENTORY: A LATER ADD REPLICATES AFTER A TRANSFERRED ITEM'S SOURCE IS DESTROYED
//============================================================================
//
// Same setup as CkAutoTest_Net_DataOnly_TransferredItemOutlivesSourceOwner. Once the host is gone,
// the server adds a second, independent item (a Sword) to the SAME target inventory. The target's
// owner replicates its DataOnly inventories as one named snapshot, so the Sword travels in the same
// payload as the transferred Potion. The client must come to hold the Sword. The client's wait
// outlasts the replicated-fragment dispatcher's 5s NotReady drop, so a refused snapshot is not
// mistaken for a slow one.
//
// Surface: Ck.Inventory.Net.AS_DataOnly_LaterAddReplicatesAfterTransferredItemSourceDestroyed
//============================================================================

class UCk_AutoTest_Net_DataOnly_LaterAddReplicatesAfterTransferredItemSourceDestroyed : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 20.0f;
    default _NetSubjectClass = ACk_AutoTest_NetSubject_Inventory_UE;

    private const int32 kFrameBudget = 100000;
    private const float kServerLieSeconds = 2.0f;
    // The server cannot observe the client, so it waits this long after its own host is gone before the
    // second add, for the host's destroy to reach the client first. The client fails loudly if it did not.
    private const float kServerSettleAfterHostGoneSeconds = 1.0f;
    // Past the server's settle plus the dispatcher's 5s drop (ck::PendingApplyTimeoutSeconds).
    private const float kClientSecondItemWindowSeconds = 9.0f;

    private FCk_Handle_PendingEntityScript _PendingHost;
    private FCk_Handle _Host;
    private FCk_Handle_Inventory_DataOnly _Target;
    private float _ServerItemAddedTime = -1.0f;
    private float _ServerHostGoneTime = -1.0f;
    private float _ClientHostGoneTime = -1.0f;
    private int32 _ClientTargetMaxLiveItems = 0;
    private bool _ClientSecondItemSeenWhileHostExisted = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { FinishFailure("subject not found - harness misconfigured?"); return; }

        auto SubjectActor = Cast<ACk_AutoTest_NetSubject_Inventory_UE>(utils_entity_bridge::Get_Actor(Subject));
        if (SubjectActor == nullptr)
        { FinishFailure("subject actor is not an inventory net-subject"); return; }

        _Target = SubjectActor._TestInventory;
        if (ck::Is_NOT_Valid(_Target))
        { FinishFailure("the subject's entity script did not populate _TestInventory"); return; }

        if (utils_net::Get_HasAuthority(Subject))
        {
            _PendingHost = utils_entity_script::Request_SpawnEntity(
                Subject, UCk_AutoTest_TESTONLY_InventoryItemHost_EntityScript, FInstancedStruct());
            utils_pending_entity_script::Promise_OnConstructed(
                _PendingHost, FCk_Delegate_EntityScript_Constructed(this, n"OnServerHostConstructed"));
            return;
        }

        WaitUntil(n"Check_ClientHostHoldsItem", n"OnClientHostHoldsItem", kFrameBudget);
    }

    // ---- server ----

    UFUNCTION()
    private void OnServerHostConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        _PendingHost = FCk_Handle_PendingEntityScript();
        _Host = FCk_Handle(InEntityScriptHandle);
        Track_ForCleanup(_Host);

        auto Holder = Get_Holder(_Host);
        if (ck::Is_NOT_Valid(Holder))
        { FinishFailure("server: the host has no holder inventory"); return; }

        auto Request = FCk_Request_Inventory_AddItemByDefinition(inv_gym_items::Potion(), 1);
        Request.Set_Policy(ECk_Inventory_AddPolicy::ForceNewItem);
        Holder.Request_AddItemByDefinition(Request, FCk_Delegate_Inventory_OnOperationResult_AddByDefinition());
        WaitUntil(n"Check_ServerHostHasLain", n"OnServerHostHasLain", kFrameBudget);
    }

    UFUNCTION()
    private void Check_ServerHostHasLain(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        auto Holder = Get_Holder(_Host);
        if (ck::Is_NOT_Valid(Holder) || Holder.Get_NumItems() != 1)
        { Res.Set(false); return; }

        if (_ServerItemAddedTime < 0.0f)
        { _ServerItemAddedTime = System::GetGameTimeInSeconds(); }

        Res.Set(System::GetGameTimeInSeconds() - _ServerItemAddedTime >= kServerLieSeconds);
    }

    UFUNCTION()
    private void OnServerHostHasLain(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        auto Holder = Get_Holder(_Host);
        auto Items = Holder.Get_Items();
        auto Request = FCk_Request_Inventory_TransferItem_ToDataOnly(Items[0], _Target);
        Holder.Request_TransferItem_ToDataOnly(Request, FCk_Delegate_Inventory_OnOperationResult_Transfer());
        WaitUntil(n"Check_ServerHolderDrained", n"OnServerHolderDrained", kFrameBudget);
    }

    UFUNCTION()
    private void Check_ServerHolderDrained(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        auto Holder = Get_Holder(_Host);
        Res.Set(ck::IsValid(Holder) && Holder.Get_NumItems() == 0 && _Target.Get_NumItems() == 1);
    }

    UFUNCTION()
    private void OnServerHolderDrained(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        utils_entity_lifetime::Request_DestroyEntity(_Host);
        WaitUntil(n"Check_ServerHostGoneAndSettled", n"OnServerHostGoneAndSettled", kFrameBudget);
    }

    UFUNCTION()
    private void Check_ServerHostGoneAndSettled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        if (ck::IsValid(_Host))
        { Res.Set(false); return; }

        if (_ServerHostGoneTime < 0.0f)
        { _ServerHostGoneTime = System::GetGameTimeInSeconds(); }

        Res.Set(System::GetGameTimeInSeconds() - _ServerHostGoneTime >= kServerSettleAfterHostGoneSeconds);
    }

    UFUNCTION()
    private void OnServerHostGoneAndSettled(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        auto Request = FCk_Request_Inventory_AddItemByDefinition(inv_gym_items::Sword(), 1);
        Request.Set_Policy(ECk_Inventory_AddPolicy::ForceNewItem);
        _Target.Request_AddItemByDefinition(Request, FCk_Delegate_Inventory_OnOperationResult_AddByDefinition());
        WaitUntil(n"Check_ServerTargetHoldsBoth", n"OnServerTargetHoldsBoth", kFrameBudget);
    }

    UFUNCTION()
    private void Check_ServerTargetHoldsBoth(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(Holds_Definition(_Target, inv_gym_items::Potion()) && Holds_Definition(_Target, inv_gym_items::Sword()));
    }

    UFUNCTION()
    private void OnServerTargetHoldsBoth(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        FinishSuccess();
    }

    // ---- client ----

    UFUNCTION()
    private void Check_ClientHostHoldsItem(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        _Host = Find_Host();
        if (ck::Is_NOT_Valid(_Host))
        { Res.Set(false); return; }

        auto Holder = Get_Holder(_Host);
        Res.Set(ck::IsValid(Holder) && Holder.Get_NumItems() == 1);
    }

    UFUNCTION()
    private void OnClientHostHoldsItem(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        WaitUntil(n"Check_ClientHostGone", n"OnClientHostGone", kFrameBudget);
    }

    UFUNCTION()
    private void Check_ClientHostGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        const bool HostGone = ck::Is_NOT_Valid(Find_Host());
        _ClientTargetMaxLiveItems = Math::Max(_ClientTargetMaxLiveItems, _Target.Get_NumItems());
        if (HostGone == false && Holds_Definition(_Target, inv_gym_items::Sword()))
        { _ClientSecondItemSeenWhileHostExisted = true; }
        Res.Set(HostGone);
    }

    UFUNCTION()
    private void OnClientHostGone(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        if (_ClientSecondItemSeenWhileHostExisted)
        { FinishFailure(f"harness: the server's second add reached the client before the host's destroy, so the row did not exercise a later add after the source was gone - raise kServerSettleAfterHostGoneSeconds ({kServerSettleAfterHostGoneSeconds}s)"); return; }

        _ClientHostGoneTime = System::GetGameTimeInSeconds();
        WaitUntil(n"Check_ClientHoldsSecondItemOrWindowElapsed", n"OnClientSecondItemWaitEnded", kFrameBudget);
    }

    UFUNCTION()
    private void Check_ClientHoldsSecondItemOrWindowElapsed(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        _ClientTargetMaxLiveItems = Math::Max(_ClientTargetMaxLiveItems, _Target.Get_NumItems());
        Res.Set(Holds_Definition(_Target, inv_gym_items::Sword())
            || System::GetGameTimeInSeconds() - _ClientHostGoneTime >= kClientSecondItemWindowSeconds);
    }

    UFUNCTION()
    private void OnClientSecondItemWaitEnded(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        if (Holds_Definition(_Target, inv_gym_items::Sword()) == false)
        { FinishFailure(f"client: condition 'target holds the Sword the server added after the host was destroyed' did not hold within {kClientSecondItemWindowSeconds}s of the host being gone - the target holds {_Target.Get_NumItems()} live items (most ever seen: {_ClientTargetMaxLiveItems}); the owner's later DataOnly snapshot was never applied"); return; }

        FinishSuccess();
    }

    // ---- helpers ----

    private bool Holds_Definition(FCk_Handle_Inventory_DataOnly InInventory, UCk_InventoryItem_Definition InDefinition)
    {
        for (auto Item : InInventory.Get_Items())
        {
            if (ck::IsValid(Item) && utils_item::Get_Definition(Item) == InDefinition)
            { return true; }
        }
        return false;
    }

    private FCk_Handle Find_Host()
    {
        auto Subject = Get_SubjectEntity();
        for (auto Candidate : utils_entity_tag::ForEach_Entity(Subject, constants_inventory_item_host_test::k_Tag))
        {
            if (ck::IsValid(Candidate))
            { return Candidate; }
        }
        return FCk_Handle();
    }

    private FCk_Handle_Inventory_DataOnly Get_Holder(FCk_Handle InHost)
    {
        if (ck::Is_NOT_Valid(InHost))
        { return FCk_Handle_Inventory_DataOnly(); }

        auto Inventory = utils_inventory::TryGet_Inventory(InHost,
            utils_gameplay_tag::ResolveGameplayTag(constants_inventory_item_host_test::k_HolderName));
        if (ck::Is_NOT_Valid(Inventory))
        { return FCk_Handle_Inventory_DataOnly(); }

        return Inventory.As_Inventory_DataOnly();
    }
}
