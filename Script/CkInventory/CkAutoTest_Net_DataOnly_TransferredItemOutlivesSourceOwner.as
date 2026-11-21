// Language=angelscript

//============================================================================
// CK AUTOMATION TEST - NETWORKED INVENTORY: A TRANSFERRED ITEM OUTLIVES ITS SOURCE OWNER
//============================================================================
//
// The pickup shape. A replicated host entity (UCk_AutoTest_TESTONLY_InventoryItemHost_EntityScript,
// the shape of a dropped world item) holds a Potion. The server transfers it into the subject's
// inventory, waits for the holder to drain, then destroys the host - the order the item collector's
// suction uses. On the server the transfer moved the item's lifetime to the subject's inventory, so
// the item survives. The client must agree: once the host is gone, the subject's inventory still
// holds the same live Potion.
//
// Surface: Ck.Inventory.Net.AS_DataOnly_TransferredItemOutlivesSourceOwner
//============================================================================

class UCk_AutoTest_Net_DataOnly_TransferredItemOutlivesSourceOwner : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 20.0f;
    default _NetSubjectClass = ACk_AutoTest_NetSubject_Inventory_UE;

    private const int32 kFrameBudget = 100000;
    private const float kMaxSeconds = 20.0f;
    // The client builds the host's item from replication before the transfer is issued, as with a
    // world item lying in the world. Two seconds of game time is the "it has been lying there" window.
    private const float kServerLieSeconds = 2.0f;
    // Negative window after the host is gone on the client: the cascade under test is immediate, so
    // half a second of game time is ample for it to have happened if it is going to.
    private const float kClientSurviveSeconds = 0.5f;

    private FCk_Handle_PendingEntityScript _PendingHost;
    private FCk_Handle _Host;
    private FCk_Handle_Inventory_DataOnly _Target;
    private float _ServerItemAddedTime = -1.0f;
    private float _ClientHostGoneTime = -1.0f;
    // Names the failing boundary: 0 = the transfer never reached the client's target, 1 = it arrived
    // and was lost afterwards.
    private int32 _ClientTargetMaxLiveItems = 0;
    private bool _ClientTargetHeldBeforeHostGone = false;

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
            Request_SpawnHost(Subject);
            return;
        }

        WaitUntil(n"Check_ClientHostHoldsItem", n"OnClientHostHoldsItem", kFrameBudget, kMaxSeconds);
    }

    // ---- server ----

    // The host shares the subject's replicated actor. The relay-hosted subclass moves it to another root.
    protected void Request_SpawnHost(FCk_Handle InSubject)
    {
        Request_SpawnHostUnder(InSubject);
    }

    protected void Request_SpawnHostUnder(FCk_Handle InLifetimeOwner)
    {
        _PendingHost = utils_entity_script::Request_SpawnEntity(
            InLifetimeOwner, UCk_AutoTest_TESTONLY_InventoryItemHost_EntityScript, FInstancedStruct());
        utils_pending_entity_script::Promise_OnConstructed(
            _PendingHost, FCk_Delegate_EntityScript_Constructed(this, n"OnServerHostConstructed"));
    }

    UFUNCTION()
    protected void OnServerHostConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
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
        WaitUntil(n"Check_ServerHostHasLain", n"OnServerHostHasLain", kFrameBudget, kMaxSeconds);
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

        if (Get_DestroysHostOnTransferComplete())
        {
            Holder.Request_TransferItem_ToDataOnly(Request,
                FCk_Delegate_Inventory_OnOperationResult_Transfer(this, n"OnServerTransferComplete"));
            return;
        }

        Holder.Request_TransferItem_ToDataOnly(Request, FCk_Delegate_Inventory_OnOperationResult_Transfer());
        WaitUntil(n"Check_ServerHolderDrained", n"OnServerHolderDrained", kFrameBudget, kMaxSeconds);
    }

    // The item collector destroys the world item the tick after the holder drains. The same-frame subclass
    // destroys it from the transfer's completion instead, the tightest ordering the harness can produce.
    protected bool Get_DestroysHostOnTransferComplete() const
    {
        return false;
    }

    UFUNCTION()
    private void OnServerTransferComplete(FCk_Handle_Inventory InSource,
                                          FCk_Handle_Item InItem,
                                          FCk_Handle_Inventory InTarget,
                                          int32 InCount,
                                          FCk_Handle_Item InNewItemInTarget,
                                          ECk_Inventory_OperationResult_Transfer InResult)
    {
        if (IsFinished()) { return; }

        if (InResult != ECk_Inventory_OperationResult_Transfer::Success)
        { FinishFailure(f"server: the transfer into the target did not succeed ({InResult})"); return; }

        utils_entity_lifetime::Request_DestroyEntity(_Host);
        FinishSuccess();
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
        WaitUntil(n"Check_ClientTargetReceivedAndHostGone", n"OnClientTargetReceivedAndHostGone", kFrameBudget, kMaxSeconds);
    }

    UFUNCTION()
    private void Check_ClientTargetReceivedAndHostGone(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        const bool HostGone = ck::Is_NOT_Valid(Find_Host());
        const int32 LiveItems = _Target.Get_NumItems();
        _ClientTargetMaxLiveItems = Math::Max(_ClientTargetMaxLiveItems, LiveItems);
        if (HostGone == false && LiveItems > 0)
        { _ClientTargetHeldBeforeHostGone = true; }
        if (HostGone && _ClientHostGoneTime < 0.0f)
        { _ClientHostGoneTime = System::GetGameTimeInSeconds(); }

        Res.Set(HostGone
            && _ClientHostGoneTime >= 0.0f
            && System::GetGameTimeInSeconds() - _ClientHostGoneTime >= kClientSurviveSeconds);
    }

    UFUNCTION()
    private void OnClientTargetReceivedAndHostGone(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        auto Items = _Target.Get_Items();
        if (Items.Num() != 1)
        { FinishFailure(f"client: {kClientSurviveSeconds}s after the host was destroyed, the target holds {Items.Num()} live items, expected the transferred Potion (most live items ever seen in the target: {_ClientTargetMaxLiveItems}; held it while the host still existed: {_ClientTargetHeldBeforeHostGone})"); return; }

        if (ck::Is_NOT_Valid(Items[0]) || utils_item::Get_Definition(Items[0]) != inv_gym_items::Potion())
        { FinishFailure("client: the target's only item is not a live Potion after the host was destroyed"); return; }

        FinishSuccess();
    }

    // ---- helpers ----

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
