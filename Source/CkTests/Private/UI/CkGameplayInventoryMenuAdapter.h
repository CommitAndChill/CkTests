#pragma once

#include "CkInventory/Inventory/CkInventory_Fragment_Data.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "Styling/SlateBrush.h"

#include "UObject/Object.h"

#include "CkGameplayInventoryMenuAdapter.generated.h"

/** A committed inventory entry. Keys are only valid for the current ECS world. */
struct FCkGameplayInventoryMenuRow
{
    FString Key;
    FCk_Handle_Item Item;
    FText Name;
    FText Description;
    FString Category;
    int32 Quantity = 0;
    TSharedPtr<FSlateBrush> Icon;
};

/** Owns the deferred request callback; the callback never touches the UI collection. */
UCLASS()
class UCk_GameplayInventoryMenuAdapter_UE final : public UObject
{
    GENERATED_BODY()

public:
    auto TryInitialize(const FCk_Handle_Inventory_DataOnly& InInventory, FString& OutError) -> bool;
    auto Get_IsSourceValid() const -> bool;
    auto Get_IsRequestPending() const -> bool { return _RemovePending; }
    auto TryRead(TArray<FCkGameplayInventoryMenuRow>& OutRows, FString& OutError) const -> bool;
    auto TryDiscard(const FString& InKey, FString& OutError) -> bool;

private:
    UFUNCTION()
    void OnRemoveCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult);

    UPROPERTY()
    FCk_Handle_Inventory_DataOnly _Inventory;

    bool _RemovePending = false;
    bool _SubmissionRejected = false;
    mutable TMap<FString, TSharedPtr<FSlateBrush>> _IconBrushes;
};
