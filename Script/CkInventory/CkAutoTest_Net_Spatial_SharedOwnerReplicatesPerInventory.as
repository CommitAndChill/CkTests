// Language=angelscript

//============================================================================
// CK AUTOMATION TEST - NETWORKED INVENTORY: TWO Spatial INVENTORIES ON ONE OWNER
//============================================================================
//
// One replicated owner holds Spatial (grid) inventories A and B, the Spatial twin of the DataOnly
// shared-owner row. The server puts a Shield (1x1) in A, then a Sword (3x1) in B; both carry the grid Dimensions a
// spatial placement needs.
// The client must see exactly that split: A holds the Shield and only the Shield, B holds the
// Sword and only the Sword. An item must never appear in a sibling it was not added to.
//
// Surface: Ck.Inventory.Net.AS_Spatial_SharedOwnerReplicatesPerInventory
//============================================================================

class UCk_AutoTest_Net_Spatial_SharedOwnerReplicatesPerInventory : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 20.0f;
    default _NetSubjectClass = ACk_AutoTest_NetSubject_SharedOwnerInventories_UE;

    private const int32 kFrameBudget = 100000;
    private const float kMaxSeconds = 20.0f;

    private FCk_Handle_Inventory_Spatial _A;
    private FCk_Handle_Inventory_Spatial _B;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { FinishFailure("subject not found - harness misconfigured?"); return; }

        auto SubjectActor = Cast<ACk_AutoTest_NetSubject_SharedOwnerInventories_UE>(
            utils_entity_bridge::Get_Actor(Subject));
        if (SubjectActor == nullptr)
        { FinishFailure("subject actor is not the shared-owner inventories subject"); return; }

        _A = SubjectActor._Spatial_A;
        _B = SubjectActor._Spatial_B;
        if (ck::Is_NOT_Valid(_A) || ck::Is_NOT_Valid(_B))
        { FinishFailure("the subject's entity script did not populate both Spatial inventories"); return; }

        if (utils_net::Get_HasAuthority(Subject))
        {
            auto Request = FCk_Request_Inventory_AddItemByDefinition(inv_gym_items::Shield(), 1);
            Request.Set_Policy(ECk_Inventory_AddPolicy::ForceNewItem);
            _A.Request_AddItemByDefinition(Request, FCk_Delegate_Inventory_OnOperationResult_AddByDefinition());
            WaitUntil(n"Check_ServerAHoldsShield", n"OnServerAHoldsShield", kFrameBudget, kMaxSeconds);
            return;
        }

        WaitUntil(n"Check_ClientAHoldsAnItem", n"OnClientAHoldsAnItem", kFrameBudget, kMaxSeconds);
    }

    // ---- server ----

    UFUNCTION()
    private void Check_ServerAHoldsShield(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_A.Get_NumItems() == 1);
    }

    UFUNCTION()
    private void OnServerAHoldsShield(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        auto Request = FCk_Request_Inventory_AddItemByDefinition(inv_gym_items::Sword(), 1);
        Request.Set_Policy(ECk_Inventory_AddPolicy::ForceNewItem);
        _B.Request_AddItemByDefinition(Request, FCk_Delegate_Inventory_OnOperationResult_AddByDefinition());
        WaitUntil(n"Check_ServerBHoldsSword", n"OnServerBHoldsSword", kFrameBudget, kMaxSeconds);
    }

    UFUNCTION()
    private void Check_ServerBHoldsSword(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_B.Get_NumItems() == 1);
    }

    UFUNCTION()
    private void OnServerBHoldsSword(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        Assert_True(Holds_Only(_A, inv_gym_items::Shield()), f"server: A should hold only the Shield - {Describe()}");
        FinishSuccess();
    }

    // ---- client ----

    UFUNCTION()
    private void Check_ClientAHoldsAnItem(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_A.Get_NumItems() > 0);
    }

    UFUNCTION()
    private void OnClientAHoldsAnItem(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        if (Holds_Only(_A, inv_gym_items::Shield()) == false)
        { FinishFailure(f"client: A's first replicated contents are not exactly the Shield - {Describe()}"); return; }

        if (Holds_Any(_B, inv_gym_items::Shield()))
        { FinishFailure(f"client: the Shield added to A also appeared in sibling B - {Describe()}"); return; }

        WaitUntil(n"Check_ClientBHoldsAnItem", n"OnClientBHoldsAnItem", kFrameBudget, kMaxSeconds);
    }

    UFUNCTION()
    private void Check_ClientBHoldsAnItem(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_B.Get_NumItems() > 0);
    }

    UFUNCTION()
    private void OnClientBHoldsAnItem(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        if (Holds_Only(_B, inv_gym_items::Sword()) == false)
        { FinishFailure(f"client: B's replicated contents are not exactly the Sword - {Describe()}"); return; }

        if (Holds_Only(_A, inv_gym_items::Shield()) == false)
        { FinishFailure(f"client: B's update overwrote sibling A - {Describe()}"); return; }

        FinishSuccess();
    }

    // ---- helpers ----

    private bool Holds_Only(FCk_Handle_Inventory_Spatial InInventory, UCk_InventoryItem_Definition InDefinition)
    {
        auto Items = InInventory.Get_Items();
        if (Items.Num() != 1)
        { return false; }
        return ck::IsValid(Items[0]) && utils_item::Get_Definition(Items[0]) == InDefinition;
    }

    private bool Holds_Any(FCk_Handle_Inventory_Spatial InInventory, UCk_InventoryItem_Definition InDefinition)
    {
        for (auto Item : InInventory.Get_Items())
        {
            if (ck::IsValid(Item) && utils_item::Get_Definition(Item) == InDefinition)
            { return true; }
        }
        return false;
    }

    private FString Describe_Inventory(FCk_Handle_Inventory_Spatial InInventory)
    {
        auto Text = "[";
        for (auto Item : InInventory.Get_Items())
        {
            if (ck::Is_NOT_Valid(Item))
            { Text += " <invalid>"; continue; }
            auto Definition = utils_item::Get_Definition(Item);
            FString DefinitionName = "<null def>";
            if (Definition != nullptr)
            { DefinitionName = Definition.GetName().ToString(); }
            Text += " " + DefinitionName;
        }
        return Text + " ]";
    }

    private FString Describe()
    {
        return f"A={Describe_Inventory(_A)} B={Describe_Inventory(_B)}";
    }
}
