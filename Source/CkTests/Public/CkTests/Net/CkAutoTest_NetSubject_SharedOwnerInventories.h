#pragma once

#include "CkTests/Net/CkAutoTest_NetSubject.h"

#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

#include "CkInventory/Inventory/DataOnly/CkInventory_DataOnly_Fragment_Data.h"
#include "CkInventory/Inventory/Spatial/CkInventory_Spatial_Fragment_Data.h"

#include "CkAutoTest_NetSubject_SharedOwnerInventories.generated.h"

// --------------------------------------------------------------------------------------------------------------------

// Net subject whose ONE entity owns two replicated DataOnly inventories and two replicated Spatial inventories,
// the shape of a player hotbar (ten DataOnly inventories on the pawn) or a station with several bins. The
// handles are local per world, like ACk_AutoTest_NetSubject_Inventory_UE.
UCLASS()
class CKTESTS_API ACk_AutoTest_NetSubject_SharedOwnerInventories_UE : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    ACk_AutoTest_NetSubject_SharedOwnerInventories_UE();

public:
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_Inventory_DataOnly _DataOnly_A;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_Inventory_DataOnly _DataOnly_B;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_Inventory_Spatial _Spatial_A;

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Ck|AutoTest")
    FCk_Handle_Inventory_Spatial _Spatial_B;
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_SharedOwnerInventoriesEntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

// --------------------------------------------------------------------------------------------------------------------
