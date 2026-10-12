// Language=angelscript

// Two separate staged Game processes share only this test-owned SaveGame slot.
// Change the suffix before recooking for another run; never overwrite an existing slot.
namespace ck_inventory_package_gauntlet
{
    const FString Slot = "CkUiPackage_20261011_a12a46_bf28";

    bool HasQuantities(FCk_Handle_Inventory_DataOnly InInventory,
        int32 InPotion, int32 InArrow, int32 InSword)
    {
        if (ck::Is_NOT_Valid(InInventory)) { return false; }
        int32 Potion = 0;
        int32 Arrow = 0;
        int32 Sword = 0;
        int32 ItemCount = 0;
        for (auto Item : utils_inventory::Get_Items(InInventory))
        {
            if (ck::Is_NOT_Valid(Item) || !utils_inventory::Get_ContainsItem(InInventory, Item))
            { return false; }
            auto Definition = utils_item::Get_Definition(Item);
            if (ck::Is_NOT_Valid(Definition)) { return false; }
            const int32 Quantity = utils_item_trait_stackable::Get_IsStackable(Item)
                ? utils_item_trait_stackable::Get_StackCount(Item) : 1;
            if (Quantity <= 0) { return false; }
            if (Definition == inv_gym_items::Potion()) { Potion += Quantity; }
            else if (Definition == inv_gym_items::Arrow()) { Arrow += Quantity; }
            else if (Definition == inv_gym_items::Sword()) { Sword += Quantity; }
            else { return false; }
            ++ItemCount;
        }
        const int32 ExpectedItems = (InPotion > 0 ? 1 : 0)
            + (InArrow > 0 ? 1 : 0) + (InSword > 0 ? 1 : 0);
        return ItemCount == ExpectedItems && Potion == InPotion && Arrow == InArrow
            && Sword == InSword && utils_inventory::Get_TotalUnits(InInventory)
                == InPotion + InArrow + InSword;
    }

    FCk_Handle_Item FindPotion(FCk_Handle_Inventory_DataOnly InInventory)
    {
        for (auto Item : utils_inventory::Get_Items(InInventory))
        {
            if (ck::IsValid(Item) && utils_item::Get_Definition(Item) == inv_gym_items::Potion())
            { return Item; }
        }
        return utils_item::Get_InvalidHandle();
    }

    bool HasLocalAuthority(APlayerController InPlayer)
    {
        if (ck::Is_NOT_Valid(InPlayer) || !InPlayer.IsLocalController()
            || ck::Is_NOT_Valid(InPlayer.GetWorld())) { return false; }
        const auto Mode = InPlayer.GetWorld().GetNetMode();
        return Mode == ENetMode::NM_Standalone || Mode == ENetMode::NM_ListenServer;
    }

    bool MenuAdmitted(APlayerController InPlayer, UCk_GameplayInventoryProof_UE InOwner)
    {
        if (Gameplay::IsGamePaused()) { return false; }
        auto Menu = UCk_PauseMenuProof_UE::OpenWithGameplayInventory(
            InPlayer, InOwner.Get_Inventory());
        return ck::IsValid(Menu) && Menu.IsActivated() && Menu.Get_UsesGameplayInventory()
            && Menu.Get_Screen() == ECk_UiAuthoringProof_Screen::Inventory
            && Menu.Get_InventorySummary() == "2 entries  /  2 shown  /  All"
            && Menu.Get_SourceError().IsEmpty() && !Gameplay::IsGamePaused();
    }
}

class UCk_GauntletAsTest_InventorySave : UCk_GauntletAsTest_Base
{
    default _TimeoutSeconds = 90.0f;

    UPROPERTY()
    private UCk_GameplayInventoryProof_UE _Owner;

    private FCk_Handle_Item _Potion;
    private bool _Ended = false;
    private bool _RemoveCompleted = false;
    private int32 _Stage = 0;
    private int32 _PostRemoveTicks = 0;

    private void Fail(const FString& InReason)
    {
        if (_Ended) { return; }
        _Ended = true;
        Controller.Request_MarkHeartbeat("[CkInvPackage] save FAIL: " + InReason);
        Controller.Request_EndTest(1);
    }

    UFUNCTION(BlueprintOverride)
    void OnAsInit()
    {
        auto Player = Controller.Get_FirstPlayerController();
        if (!ck_inventory_package_gauntlet::HasLocalAuthority(Player))
        { Fail("local authority player/world unavailable"); return; }
        if (Gameplay::DoesSaveGameExist(ck_inventory_package_gauntlet::Slot, 0))
        { Fail("test-owned slot already exists; refusing overwrite"); return; }
        FString Error;
        _Owner = UCk_GameplayInventoryProof_UE::TryCreate(Error);
        if (ck::Is_NOT_Valid(_Owner))
        { Fail("inventory creation rejected: " + Error); return; }
        Controller.Request_MarkHeartbeat("[CkInvPackage] save: awaiting seeded inventory");
    }

    UFUNCTION(BlueprintOverride)
    void OnAsTick(float DeltaTime)
    {
        if (_Ended) { return; }
        if (Controller.Get_ElapsedTimeSeconds() > 75.0)
        { Fail("phase deadline exceeded"); return; }
        if (ck::Is_NOT_Valid(_Owner)) { Fail("inventory owner vanished"); return; }
        if (!_Owner.Get_LastError().IsEmpty())
        { Fail("inventory owner failed: " + _Owner.Get_LastError()); return; }
        if (_Stage == 0)
        {
            if (!_Owner.Get_IsReady()) { return; }
            auto Inventory = _Owner.Get_Inventory();
            if (!ck_inventory_package_gauntlet::HasQuantities(Inventory, 6, 24, 1))
            { Fail("seed quantities differ from Potion 6, Arrow 24, Sword 1"); return; }
            _Potion = ck_inventory_package_gauntlet::FindPotion(Inventory);
            if (ck::Is_NOT_Valid(_Potion)) { Fail("Potion item is absent"); return; }
            auto Remove = FCk_Request_Inventory_RemoveItem(_Potion);
            Remove.Set_PostRemovePolicy(ECk_Inventory_PostRemovePolicy::DestroyItem);
            FCk_Handle_Inventory BaseInventory = Inventory;
            _Stage = 1;
            BaseInventory.Request_RemoveItem(Remove,
                FCk_Delegate_Inventory_OnOperationResult_Remove(this, n"OnRemoveResult"));
            Controller.Request_MarkHeartbeat("[CkInvPackage] save: Potion removal submitted");
            return;
        }
        if (_Stage == 1)
        {
            if (!_RemoveCompleted) { return; }
            if (!ck_inventory_package_gauntlet::HasQuantities(_Owner.Get_Inventory(), 0, 24, 1))
            { Fail("completed remove did not commit 25 units"); return; }
            _Stage = 2;
            return;
        }
        if (_Stage == 2)
        {
            // The native PIE gate advances five world ticks after completion so
            // the deferred item-lifetime cleanup can finish before saving.
            auto Player = Controller.Get_FirstPlayerController();
            if (!ck_inventory_package_gauntlet::HasLocalAuthority(Player)
                || Gameplay::IsGamePaused() || Player.GetWorld().GetDeltaSeconds() <= 0.0f)
            { Fail("world stopped before deferred cleanup finished"); return; }
            ++_PostRemoveTicks;
            if (_PostRemoveTicks < 5) { return; }
            if (ck::IsValid(_Potion))
            { Fail("removed Potion entity survived deferred cleanup"); return; }
            if (!ck_inventory_package_gauntlet::HasQuantities(_Owner.Get_Inventory(), 0, 24, 1))
            { Fail("committed inventory changed during cleanup"); return; }
            if (Gameplay::DoesSaveGameExist(ck_inventory_package_gauntlet::Slot, 0))
            { Fail("test-owned slot appeared before save; refusing overwrite"); return; }
            FString Error;
            _Stage = 3;
            if (!_Owner.Request_Save(ck_inventory_package_gauntlet::Slot, Error))
            { Fail("save rejected: " + Error); return; }
            if (!Gameplay::DoesSaveGameExist(ck_inventory_package_gauntlet::Slot, 0))
            { Fail("save reported success but slot is absent"); return; }
            if (!ck_inventory_package_gauntlet::MenuAdmitted(
                Controller.Get_FirstPlayerController(), _Owner))
            { Fail("authored gameplay menu admission failed or paused the world"); return; }
            Controller.Request_MarkHeartbeat("[CkInvPackage] save PASS: 0 Potion, 24 Arrow, 1 Sword; authored menu admitted");
            _Ended = true;
            Controller.Request_EndTest(0);
        }
    }

    UFUNCTION()
    private void OnRemoveResult(FCk_Handle_Inventory InInventory, FCk_Handle_Item InItem,
        ECk_Inventory_OperationResult_Remove InResult)
    {
        if (_Ended) { return; }
        if (_Stage != 1 || InItem != _Potion || InResult != ECk_Inventory_OperationResult_Remove::Success)
        { Fail("Potion removal completion failed or mismatched"); return; }
        _RemoveCompleted = true;
        Controller.Request_MarkHeartbeat("[CkInvPackage] save: Potion removal completed");
    }
}

class UCk_GauntletAsTest_InventoryLoad : UCk_GauntletAsTest_Base
{
    default _TimeoutSeconds = 90.0f;

    UPROPERTY()
    private UCk_GameplayInventoryProof_UE _Owner;

    private bool _Ended = false;

    private void Fail(const FString& InReason)
    {
        if (_Ended) { return; }
        _Ended = true;
        Controller.Request_MarkHeartbeat("[CkInvPackage] load FAIL: " + InReason);
        Controller.Request_EndTest(1);
    }

    UFUNCTION(BlueprintOverride)
    void OnAsInit()
    {
        auto Player = Controller.Get_FirstPlayerController();
        if (!ck_inventory_package_gauntlet::HasLocalAuthority(Player))
        { Fail("local authority player/world unavailable"); return; }
        if (!Gameplay::DoesSaveGameExist(ck_inventory_package_gauntlet::Slot, 0))
        { Fail("saved slot is absent; save phase did not finish"); return; }
        FString Error;
        _Owner = UCk_GameplayInventoryProof_UE::TryLoad(ck_inventory_package_gauntlet::Slot, Error);
        if (ck::Is_NOT_Valid(_Owner))
        { Fail("fresh-process load rejected: " + Error); return; }
        Controller.Request_MarkHeartbeat("[CkInvPackage] load: awaiting committed hydration");
    }

    UFUNCTION(BlueprintOverride)
    void OnAsTick(float DeltaTime)
    {
        if (_Ended) { return; }
        if (Controller.Get_ElapsedTimeSeconds() > 75.0)
        { Fail("phase deadline exceeded"); return; }
        if (ck::Is_NOT_Valid(_Owner)) { Fail("loaded owner vanished"); return; }
        if (!_Owner.Get_LastError().IsEmpty())
        { Fail("loaded owner failed: " + _Owner.Get_LastError()); return; }
        if (!_Owner.Get_IsReady()) { return; }
        if (!ck_inventory_package_gauntlet::HasQuantities(_Owner.Get_Inventory(), 0, 24, 1))
        { Fail("loaded quantities differ from 0 Potion, 24 Arrow, 1 Sword"); return; }
        if (!ck_inventory_package_gauntlet::MenuAdmitted(
            Controller.Get_FirstPlayerController(), _Owner))
        { Fail("authored gameplay menu admission failed or paused the world"); return; }
        Controller.Request_MarkHeartbeat("[CkInvPackage] load PASS: fresh process restored 25 units; authored menu admitted");
        _Ended = true;
        Controller.Request_EndTest(0);
    }
}
