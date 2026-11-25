#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/World/CkEcsWorld.h"

#include "CkGrid/2dGridSystem/Cell/Ck2dGridCell_Fragment.h"
#include "CkGrid/2dGridSystem/Cell/Ck2dGridCell_Utils.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Fragment.h"
#include "CkGrid/2dGridSystem/Grid/Ck2dGridSystem_Utils.h"

#include "CkInventory/Inventory/CkInventory_Fragment.h"
#include "CkInventory/Inventory/Spatial/CkInventory_Spatial_Utils.h"
#include "CkInventory/Item/CkItem_Fragment.h"
#include "CkInventory/Item/CkItem_Utils.h"

namespace ck_test_inventory_spatial_free_cells
{
    auto ReferenceCount(const FCk_Handle_2dGridSystem& InGrid) -> int32
    {
        auto FreeCount = 0;
        UCk_Utils_2dGridSystem_UE::ForEach_Cell(
            InGrid, ECk_2dGridSystem_CellFilter::OnlyActiveCells,
            [&](const FCk_Handle_2dGridCell& InCell)
            {
                if (ck::Is_NOT_Valid(ck::TUtils_InventorySlot_ItemRef::Get_StoredEntity(InCell)))
                { ++FreeCount; }
            });
        return FreeCount;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_InventorySpatial_FreeCellCount_ReferenceParity,
    "Ck.Inventory.Spatial.PlacementPolicy.FreeCellCountReferenceParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_InventorySpatial_FreeCellCount_ReferenceParity::RunTest(const FString&)
{
    auto World = ck::FEcsWorld{};
    auto InventoryEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(World.Get_Registry());
    InventoryEntity.Add<ck::FTag_Inventory_Spatial>();
    InventoryEntity.Add<ck::FFragment_2dGridSystem_Params>(
        FCk_2dGridSystem_Spec{FIntPoint{3, 2}, FVector2D{1.0, 1.0}});
    InventoryEntity.Add<ck::FFragment_2dGridSystem>(FCk_Handle_SceneNode{});

    const auto Inventory = UCk_Utils_Inventory_Spatial_UE::Cast(InventoryEntity);
    const auto Grid = UCk_Utils_2dGridSystem_UE::Cast(InventoryEntity);
    if (NOT TestTrue(TEXT("spatial inventory and grid use production handle types"),
        ck::IsValid(Inventory) && ck::IsValid(Grid)))
    { return false; }

    auto& GridFragment = InventoryEntity.Get<ck::FFragment_2dGridSystem>();
    auto Cells = TArray<FCk_Handle_2dGridCell>{};
    for (auto Index = 0; Index < 6; ++Index)
    {
        auto CellEntity = GridFragment.Request_CreateCellEntity();
        CellEntity.Add<ck::FFragment_2dGridCell_Params>(FCk_2dGridCell_Spec{});
        ck::TUtils_InventorySlot_ItemRef::Clear(CellEntity);
        Cells.Add(UCk_Utils_2dGridCell_UE::Cast(CellEntity));
    }

    auto CheckParity = [&](const FString& InStage, int32 InExpected)
    {
        const auto Reference = ck_test_inventory_spatial_free_cells::ReferenceCount(Grid);
        const auto Actual = UCk_Utils_Inventory_Spatial_UE::Get_NumFreeCells(Inventory);
        TestEqual(InStage + TEXT(" reference"), Reference, InExpected);
        TestEqual(InStage + TEXT(" production parity"), Actual, Reference);
    };

    CheckParity(TEXT("empty holders"), 6);

    auto ItemEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(World.Get_Registry());
    ItemEntity.Add<ck::FFragment_InventoryItem>();
    const auto Item = UCk_Utils_Item_UE::Cast(ItemEntity);
    if (NOT TestTrue(TEXT("item handle composed"), ck::IsValid(Item)))
    { return false; }

    ck::TUtils_InventorySlot_ItemRef::AddOrReplace(Cells[0], Item);
    ck::TUtils_InventorySlot_ItemRef::AddOrReplace(Cells[1], Item);
    CheckParity(TEXT("two occupied cells"), 4);

    Cells[2].Add<ck::FTag_2dGridCell_Disabled>();
    CheckParity(TEXT("disabled empty cell"), 3);
    Cells[1].Add<ck::FTag_2dGridCell_Disabled>();
    CheckParity(TEXT("disabled occupied cell"), 3);

    ck::TUtils_InventorySlot_ItemRef::Clear(Cells[0]);
    CheckParity(TEXT("cleared holder"), 4);

    Cells[3].Add<ck::FTag_DestroyEntity_Initiate>();
    CheckParity(TEXT("Initiate cell remains included"), 4);
    Cells[4].Add<ck::FTag_DestroyEntity_EndPlay>();
    CheckParity(TEXT("EndPlay cell remains included"), 4);
    Cells[3].Add<ck::FTag_DestroyEntity_Teardown>();
    CheckParity(TEXT("Teardown cell is excluded"), 3);
    Cells[4].Add<ck::FTag_DestroyEntity_Await>();
    CheckParity(TEXT("Await cell is excluded"), 2);
    Cells[5].Add<ck::FTag_DestroyEntity_Finalize>();
    CheckParity(TEXT("Finalize cell is excluded"), 1);

    // Both implementations must treat a holder pointing to an item in destruction as free.
    Cells[0].Add<ck::FTag_2dGridCell_Disabled>();
    Cells[2].Remove<ck::FTag_2dGridCell_Disabled>();
    ck::TUtils_InventorySlot_ItemRef::AddOrReplace(Cells[2], Item);
    CheckParity(TEXT("live item ref"), 0);
    ItemEntity.Add<ck::FTag_DestroyEntity_Teardown>();
    CheckParity(TEXT("destroying item ref"), 1);

    // A missing holder follows the legacy getter's diagnostic and free-cell result.
    Cells[2].Remove<ck::FFragment_InventorySlot_ItemRef>();
    AddExpectedError(TEXT("does NOT have an EntityHolder"), EAutomationExpectedErrorFlags::Contains, -1);
    CheckParity(TEXT("missing holder"), 1);

    // Snapshot restoration can leave a typed grid fragment before its private
    // cell registry has been recomposed. The new guard must fail closed.
    auto UncomposedEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(World.Get_Registry());
    UncomposedEntity.Add<ck::FTag_Inventory_Spatial>();
    UncomposedEntity.Add<ck::FFragment_2dGridSystem_Params>(
        FCk_2dGridSystem_Spec{FIntPoint{1, 1}, FVector2D{1.0, 1.0}});
    UncomposedEntity.Add<ck::FFragment_2dGridSystem>();
    const auto UncomposedInventory = UCk_Utils_Inventory_Spatial_UE::Cast(UncomposedEntity);
    if (NOT TestTrue(TEXT("uncomposed inventory retains typed handles"), ck::IsValid(UncomposedInventory)))
    { return false; }
    AddExpectedError(TEXT("Get_NumFreeCells: grid cell registry is invalid"),
        EAutomationExpectedErrorFlags::Contains, -1);
    TestEqual(TEXT("uncomposed grid returns zero free cells"),
        UCk_Utils_Inventory_Spatial_UE::Get_NumFreeCells(UncomposedInventory), 0);

    return true;
}

#endif
