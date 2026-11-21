#include "CkTests/Net/CkAutoTest_NetSubject_SharedOwnerInventories.h"

#include "CkTests/CkTests_Fragment_Data.h"

#include "CkInventory/Inventory/DataOnly/CkInventory_DataOnly_Utils.h"
#include "CkInventory/Inventory/Spatial/CkInventory_Spatial_Utils.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"

// --------------------------------------------------------------------------------------------------------------------

ACk_AutoTest_NetSubject_SharedOwnerInventories_UE::
    ACk_AutoTest_NetSubject_SharedOwnerInventories_UE()
{
    _EntityScriptClass = UCk_AutoTest_NetSubject_SharedOwnerInventoriesEntityScript_UE::StaticClass();
}

// --------------------------------------------------------------------------------------------------------------------

namespace ck::auto_test::netsubject_shared_owner_inventories
{
    constexpr auto DataOnlyCapacity = int32{5};
    const auto SpatialDimensions = FIntPoint{5, 5};

    auto
        Add_DataOnly(
            FCk_Handle& InOwner,
            const FGameplayTag& InName)
        -> FCk_Handle_Inventory_DataOnly
    {
        const auto Params = UCk_Utils_Inventory_DataOnly_UE::Make_Params_Bounded(
            InName,
            DataOnlyCapacity,
            FCk_Delegate_Inventory_CustomCanAcceptItem_Dynamic{},
            FCk_Delegate_Inventory_CustomCanStackItems_Dynamic{});

        return UCk_Utils_Inventory_DataOnly_UE::Add(InOwner, Params, ECk_Replication::Replicates);
    }

    auto
        Add_Spatial(
            FCk_Handle& InOwner,
            const FGameplayTag& InName)
        -> FCk_Handle_Inventory_Spatial
    {
        const auto Params = UCk_Utils_Inventory_Spatial_UE::Make_Params(
            InName,
            SpatialDimensions,
            FCk_Delegate_Inventory_CustomCanAcceptItem_Dynamic{},
            FCk_Delegate_Inventory_CustomCanStackItems_Dynamic{});

        return UCk_Utils_Inventory_Spatial_UE::Add(InOwner, Params, ECk_Replication::Replicates);
    }
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_AutoTest_NetSubject_SharedOwnerInventoriesEntityScript_UE::
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);

    using namespace ck::auto_test::netsubject_shared_owner_inventories;

    const auto DataOnly_A = Add_DataOnly(InHandle, TAG_Inventory_AutoTest_Net_SharedOwner_DataOnly_A.GetTag());
    const auto DataOnly_B = Add_DataOnly(InHandle, TAG_Inventory_AutoTest_Net_SharedOwner_DataOnly_B.GetTag());
    const auto Spatial_A = Add_Spatial(InHandle, TAG_Inventory_AutoTest_Net_SharedOwner_Spatial_A.GetTag());
    const auto Spatial_B = Add_Spatial(InHandle, TAG_Inventory_AutoTest_Net_SharedOwner_Spatial_B.GetTag());

    auto* OwningActor = UCk_Utils_EntityBridge_UE::Get_Actor(InHandle);
    if (auto* SubjectActor = Cast<ACk_AutoTest_NetSubject_SharedOwnerInventories_UE>(OwningActor))
    {
        SubjectActor->_DataOnly_A = DataOnly_A;
        SubjectActor->_DataOnly_B = DataOnly_B;
        SubjectActor->_Spatial_A = Spatial_A;
        SubjectActor->_Spatial_B = Spatial_B;
    }

    return Flow;
}

// --------------------------------------------------------------------------------------------------------------------
