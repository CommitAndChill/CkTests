#pragma once

#include "CkInventory/Inventory/CkInventory_Fragment_Data.h"
#include "UObject/Object.h"

#include "InventorySyncReplication_TestTypes.generated.h"

UCLASS()
class UCk_InventorySyncReplication_Listener_UE : public UObject
{
    GENERATED_BODY()

public:
    struct FDelta
    {
        FCk_Handle_Inventory _Inventory;
        TArray<FCk_Handle_Item> _Added;
        TArray<FCk_Handle_Item> _Removed;
    };

    UFUNCTION()
    void
    OnItemsChanged(
        FCk_Handle_Inventory InInventory,
        const TArray<FCk_Handle_Item>& InAdded,
        const TArray<FCk_Handle_Item>& InRemoved)
    {
        _Deltas.Add(FDelta{InInventory, InAdded, InRemoved});
    }

    TArray<FDelta> _Deltas;
};
