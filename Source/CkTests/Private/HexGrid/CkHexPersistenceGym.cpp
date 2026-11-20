#include "CkTests/HexGrid/CkHexPersistenceGym.h"

#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "CkEcs/EntityScript/CkEntityScript_SpawnRecipe.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/Registry/CkRegistry.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include <Engine/GameInstance.h>
#include <Engine/World.h>

namespace ck::tests::hex_persistence
{
    const FName GymSlot = TEXT("CkTests_HexPersistenceGym");

    auto FindRecipe(UWorld* InWorld, UClass* InClass) -> FCk_Handle
    {
        auto* Ecs = IsValid(InWorld) ? InWorld->GetSubsystem<UCk_EcsWorld_Subsystem_UE>() : nullptr;
        if (!Ecs) { return {}; }
        auto& Registry = Ecs->Get_Registry();
        auto* Raw = ck::registry_table::TryResolve(Registry.Get_RegistryHandle());
        if (!Raw) { return {}; }
        FCk_Handle Found;
        for (const auto Entity : Raw->view<ck::FFragment_SpawnRecipe>())
        {
            auto Handle = ck::MakeHandle(FCk_Entity{Entity}, Registry);
            if (!ck::IsValid(Handle) || Handle.Get<ck::FFragment_SpawnRecipe>().Get_ScriptClass().Get() != InClass)
            { continue; }
            // Ambiguous fixtures must not silently pick whichever registry row happens to appear first.
            if (ck::IsValid(Found)) { return {}; }
            Found = Handle;
        }
        return Found;
    }
}

UCk_HexPersistenceGymBoard::UCk_HexPersistenceGymBoard()
{
    _Replication = ECk_Replication::DoesNotReplicate;
    _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity;
}

UCk_HexPersistenceGymObject::UCk_HexPersistenceGymObject()
{
    _Replication = ECk_Replication::DoesNotReplicate;
    _InstancingPolicy = ECk_EntityScript_InstancingPolicy::InstancedPerEntity;
}

void UCk_HexPersistenceGymSession::Request_Start(const FTransform& InPivot)
{
    auto* Snapshot = GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>();
    auto* World = GetWorld();
    if (!World || !Snapshot || Snapshot->Get_IsLoadInProgress() || _SpawnWorld.Get() == World
        || ck::IsValid(Get_Board()))
    { return; }
    auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, ck::tests::hex_persistence::BoardAssetPath);
    auto* ObjectAsset = LoadObject<UCk_HexGridObject_AuthoringSpec>(nullptr,
        TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_RulesObject.DA_HexGrid_RulesObject"));
    if (!Asset || !ObjectAsset)
    { _Report = TEXT("Missing saved authoring fixture; run Persistence.AuthoringAssets in the editor"); return; }
    auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!ck::IsValid(Owner)) { return; }
    _SpawnWorld = World;
    FCk_HexGridInitialization Fresh;
    Fresh.Set_Kind(ECk_HexGridInitialization::Fresh);
    FCk_HexGrid_SpawnParams BoardParams{Asset, InPivot};
    BoardParams.Set_Initialization(Fresh);
    FCk_HexGridObject_SpawnParams ObjectParams{TSoftObjectPtr<UCk_HexGridObject_AuthoringSpec>{ObjectAsset}};
    ObjectParams.Set_Initialization(Fresh);
    UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner, UCk_HexPersistenceGymBoard::StaticClass(), FInstancedStruct::Make(BoardParams), {});
    UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner, UCk_HexPersistenceGymObject::StaticClass(), FInstancedStruct::Make(ObjectParams), {});
}

FCk_Handle_HexGrid UCk_HexPersistenceGymSession::Get_Board() const
{
    const auto Found = ck::tests::hex_persistence::FindRecipe(GetWorld(), UCk_HexPersistenceGymBoard::StaticClass());
    return UCk_Utils_HexGrid_UE::Has(Found) ? UCk_Utils_HexGrid_UE::Cast(Found) : FCk_Handle_HexGrid{};
}

FString UCk_HexPersistenceGymSession::Get_Status() const
{
    const auto Board = Get_Board();
    if (!ck::IsValid(Board)) { return _Busy ? TEXT("AwaitingRestore") : TEXT("Preparing"); }
    const auto State = UCk_Utils_HexGrid_UE::Get_Readiness(Board).Get_State();
    const auto Placements = UCk_Utils_HexGridOccupancy_UE::Get_Placements(Board);
    return FString::Printf(TEXT("%s | cells %d | placements %d"),
        *StaticEnum<ECk_HexGridReadiness>()->GetNameStringByValue(static_cast<int64>(State)),
        UCk_Utils_HexGrid_UE::Get_Cells(Board).Num(), Placements.Get_Succeeded() ? Placements.Get_Placements().Num() : 0);
}

void UCk_HexPersistenceGymSession::Request_Place()
{
    auto Board = Get_Board();
    if (_Busy || !UCk_Utils_HexGrid_UE::Get_IsReady(Board)) { return; }
    const auto Found = ck::tests::hex_persistence::FindRecipe(GetWorld(), UCk_HexPersistenceGymObject::StaticClass());
    if (!UCk_Utils_HexGridObject_UE::Has(Found)) { return; }
    FCk_HexGridPlacement_Spec Spec;
    Spec.Set_Object(UCk_Utils_HexGridObject_UE::Cast(Found));
    FCk_Request_HexGridPlacement_Place Request{Spec};
    FCk_Delegate_HexGridPlacement_OnResult Callback;
    Callback.BindDynamic(this, &ThisClass::OnPlaced);
    Request.Set_OnResult(Callback);
    _Busy = true;
    UCk_Utils_HexGridPlacement_UE::Request_Place(Board, Request, {});
}

void UCk_HexPersistenceGymSession::OnPlaced(const FCk_HexGridPlacement_Result& InResult)
{
    _Busy = false;
    _Report = InResult.Get_Succeeded() ? TEXT("Piece placed") : TEXT("Placement rejected");
}

void UCk_HexPersistenceGymSession::Request_Save()
{
    if (_Busy || !UCk_Utils_HexGrid_UE::Get_IsReady(Get_Board())) { return; }
    auto* Snapshot = GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>();
    if (!Snapshot) { return; }
    FCk_Delegate_OnSaveComplete Done;
    Done.BindDynamic(this, &ThisClass::OnSaved);
    _Busy = true;
    _Report = TEXT("Saving");
    Snapshot->Request_Save(ck::tests::hex_persistence::GymSlot, Done);
}

void UCk_HexPersistenceGymSession::OnSaved(ECk_SnapshotResult InResult)
{
    _Busy = false;
    _HasSave = InResult == ECk_SnapshotResult::Success;
    _Report = _HasSave ? TEXT("Saved") : TEXT("Save failed");
}

void UCk_HexPersistenceGymSession::Request_Load()
{
    if (_Busy || !_HasSave) { return; }
    auto* Snapshot = GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>();
    if (!Snapshot) { return; }
    _BeforeLoad = Get_Board();
    _PhysicallyReacquired = false;
    _Busy = true;
    _Report = TEXT("Loading saved board");
    FCk_Delegate_OnLoadComplete Done;
    Done.BindDynamic(this, &ThisClass::OnLoaded);
    Snapshot->Request_Load(ck::tests::hex_persistence::GymSlot, Done);
}

void UCk_HexPersistenceGymSession::OnLoaded(FCk_Snapshot_LoadReport InReport)
{
    _Busy = false;
    const auto Board = Get_Board();
    _PhysicallyReacquired = !ck::IsValid(_BeforeLoad) && ck::IsValid(Board) && Board != _BeforeLoad;
    _Report = FString::Printf(TEXT("%s | applied %d | rejected %d | fresh board %s"),
        *StaticEnum<ECk_SnapshotResult>()->GetNameStringByValue(static_cast<int64>(InReport.Get_Result())),
        InReport.Get_PayloadsApplied(), InReport.Get_PayloadsRejected(), _PhysicallyReacquired ? TEXT("yes") : TEXT("no"));
}
