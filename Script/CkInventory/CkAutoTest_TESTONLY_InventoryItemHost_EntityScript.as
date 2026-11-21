// Language=angelscript

//============================================================================
// CK INVENTORY - TEST-ONLY REPLICATED ITEM HOST
//============================================================================
//
// The minimal shape of a dropped world item: a REPLICATED entity script that is its own context root
// (so an item added to its holder is built under THIS entity's replication driver) and owns one
// bounded DataOnly holder inventory. Used by CkAutoTest_Net_DataOnly_TransferredItemOutlivesSourceOwner.
//
// Spawned under the actor-bridged net subject so its handle and its items resolve on the client.
//============================================================================

namespace constants_inventory_item_host_test
{
    const FName k_Tag = n"TAG_CkAutoTest_InventoryItemHost";
    const FName k_HolderName = n"Inventory.AutoTest_Net_ItemHost";
}

class UCk_AutoTest_TESTONLY_InventoryItemHost_EntityScript : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::Replicates;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow
    DoConstruct(FCk_Handle& InHandle)
    {
        // Must come first, as in the world item: children composed below inherit the context owner
        // current at their creation, and the holder's items are built under that context owner.
        InHandle.Request_OverrideToSelf();

        auto Params = utils_inventory_data_only::Make_Params_Bounded(
            utils_gameplay_tag::ResolveGameplayTag(constants_inventory_item_host_test::k_HolderName), 1,
            FCk_Delegate_Inventory_CustomCanAcceptItem_Dynamic(),
            FCk_Delegate_Inventory_CustomCanStackItems_Dynamic());
        utils_inventory_data_only::Add(InHandle, Params, ECk_Replication::Replicates);

        utils_entity_tag::Add(InHandle, constants_inventory_item_host_test::k_Tag);
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}
