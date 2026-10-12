#include "CkGameplayInventoryMenuAdapter.h"

#include "CkInventory/Inventory/CkInventory_Utils.h"
#include "CkInventory/Item/CkItem_Definition.h"
#include "CkInventory/Item/CkItem_Utils.h"
#include "CkInventory/ItemTrait/Stackable/CkItemTrait_Stackable_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /** Slate may retain a brush after the model releases its asset references. */
    struct FGameplayIconBrush final : FSlateBrush
    {
        TSharedPtr<TStrongObjectPtr<UTexture2D>> TextureRoot;
    };
}

auto UCk_GameplayInventoryMenuAdapter_UE::TryInitialize(
    const FCk_Handle_Inventory_DataOnly& InInventory, FString& OutError) -> bool
{
    OutError.Reset();
    const auto ValidHandle = ck::IsValid(InInventory);
    if (NOT ValidHandle)
    { OutError = TEXT("Gameplay inventory handle is invalid."); return false; }
    const auto IsDataOnly = UCk_Utils_Inventory_UE::Get_IsDataOnly(InInventory);
    if (NOT IsDataOnly)
    { OutError = TEXT("Gameplay inventory must be DataOnly."); return false; }
    const auto HasAuthority = UCk_Utils_Net_UE::Get_HasAuthority(InInventory);
    if (NOT HasAuthority)
    { OutError = TEXT("Gameplay inventory actions require authority."); return false; }
    _Inventory = InInventory;
    return true;
}

auto UCk_GameplayInventoryMenuAdapter_UE::Get_IsSourceValid() const -> bool
{
    if (NOT ck::IsValid(_Inventory)) { return false; }
    if (NOT UCk_Utils_Inventory_UE::Get_IsDataOnly(_Inventory)) { return false; }
    return UCk_Utils_Net_UE::Get_HasAuthority(_Inventory);
}

auto UCk_GameplayInventoryMenuAdapter_UE::TryRead(
    TArray<FCkGameplayInventoryMenuRow>& OutRows, FString& OutError) const -> bool
{
    OutError.Reset();
    if (NOT Get_IsSourceValid())
    { OutError = TEXT("Gameplay inventory source is stale or no longer authoritative."); return false; }

    auto Candidate = TArray<FCkGameplayInventoryMenuRow>{};
    auto Keys = TSet<FString>{};
    const auto Items = UCk_Utils_Inventory_UE::Get_Items(_Inventory);
    if (Items.Num() > 1024)
    { OutError = TEXT("Gameplay inventory exceeds the 1024-row UI limit."); return false; }
    Candidate.Reserve(Items.Num());
    for (const auto& Item : Items)
    {
        if (NOT ck::IsValid(Item) || NOT UCk_Utils_Inventory_UE::Get_ContainsItem(_Inventory, Item))
        { OutError = TEXT("Gameplay inventory contains a stale item."); return false; }
        const auto* Definition = UCk_Utils_Item_UE::Get_Definition(Item);
        if (NOT IsValid(Definition))
        { OutError = TEXT("Gameplay item has no valid definition."); return false; }
        const auto Quantity = UCk_Utils_ItemTrait_Stackable_UE::Get_IsStackable(Item)
            ? UCk_Utils_ItemTrait_Stackable_UE::Get_StackCount(Item) : 1;
        if (Quantity <= 0)
        { OutError = TEXT("Gameplay item has an invalid committed stack count."); return false; }
        auto Row = FCkGameplayInventoryMenuRow{};
        Row.Key = Item.ToString();
        if (Row.Key.IsEmpty() || Keys.Contains(Row.Key))
        { OutError = TEXT("Gameplay item identity is empty or duplicated."); return false; }
        Keys.Add(Row.Key);
        Row.Item = Item;
        Row.Name = Definition->Get_CoreInfo().Get_Name();
        Row.Description = Definition->Get_CoreInfo().Get_Description();
        Row.Quantity = Quantity;
        const auto Category = Definition->Get_AssetRegistryCategory().ToString();
        Row.Category = Category == TEXT("Equipment") || Category == TEXT("Supplies")
            || Category == TEXT("Materials") ? Category : TEXT("All");
        auto IconPath = Definition->Get_CoreInfo().Get_Icon();
        if (NOT IconPath.IsNull())
        {
            const auto IconKey = IconPath.ToSoftObjectPath().ToString();
            if (const auto* Cached = _IconBrushes.Find(IconKey)) { Row.Icon = *Cached; }
            else
            {
                auto* Texture = IconPath.LoadSynchronous();
                if (IsValid(Texture))
                {
                    auto Brush = MakeShared<FGameplayIconBrush>();
                    Brush->TextureRoot = MakeShared<TStrongObjectPtr<UTexture2D>>(Texture);
                    Brush->DrawAs = ESlateBrushDrawType::Image;
                    Brush->ImageSize = FVector2D{static_cast<float>(Texture->GetSizeX()),
                        static_cast<float>(Texture->GetSizeY())};
                    Brush->SetResourceObject(Texture);
                    Row.Icon = Brush;
                    _IconBrushes.Add(IconKey, MoveTemp(Brush));
                }
                else { _IconBrushes.Add(IconKey, {}); }
            }
        }
        Candidate.Add(MoveTemp(Row));
    }
    OutRows = MoveTemp(Candidate);
    return true;
}

auto UCk_GameplayInventoryMenuAdapter_UE::TryDiscard(const FString& InKey, FString& OutError) -> bool
{
    OutError.Reset();
    if (_RemovePending)
    { OutError = TEXT("An inventory action is still pending."); return false; }
    if (NOT Get_IsSourceValid())
    { OutError = TEXT("Gameplay inventory source is stale or no longer authoritative."); return false; }
    auto Rows = TArray<FCkGameplayInventoryMenuRow>{};
    if (NOT TryRead(Rows, OutError)) { return false; }
    const auto* Row = Rows.FindByPredicate([&InKey](const auto& InRow) { return InRow.Key == InKey; });
    if (Row == nullptr)
    { OutError = TEXT("Selected item is no longer in the inventory."); return false; }
    auto Request = FCk_Request_Inventory_RemoveItem{Row->Item};
    Request.Set_PostRemovePolicy(ECk_Inventory_PostRemovePolicy::DestroyItem);
    auto Completion = FCk_Delegate_Request_OnCompleted{};
    Completion.BindDynamic(this, &UCk_GameplayInventoryMenuAdapter_UE::OnRemoveCompleted);
    _RemovePending = true;
    _SubmissionRejected = false;
    FCk_Handle_Inventory Inventory = _Inventory;
    UCk_Utils_Inventory_UE::Request_RemoveItem(
        Inventory, Request, FCk_Delegate_Inventory_OnOperationResult_Remove{}, Completion);
    if (_SubmissionRejected)
    { OutError = TEXT("Inventory removal was not enqueued."); return false; }
    return true;
}

void UCk_GameplayInventoryMenuAdapter_UE::OnRemoveCompleted(
    FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
{
    (void)InRequestOwner;
    _SubmissionRejected = InResult == ECk_Request_OperationResult::Failed_NotEnqueued;
    _RemovePending = false;
}
