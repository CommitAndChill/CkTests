#include "CkTests/UI/CkGameplayInventoryProof.h"

#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkInventory/Inventory/Coordinator/CkInventory_OperationCoordinator_Fragment.h"
#include "CkInventory/Inventory/DataOnly/CkInventory_DataOnly_Utils.h"
#include "CkInventory/Inventory/CkInventory_Utils.h"
#include "CkInventory/Item/CkItem_Definition.h"
#include "CkInventory/Item/CkItem_Utils.h"
#include "CkInventory/ItemTrait/Stackable/CkItemTrait_Stackable.h"
#include "CkInventory/ItemTrait/Stackable/CkItemTrait_Stackable_Utils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"

namespace ck::gameplay_inventory_proof
{
    struct FCatalogEntry
    {
        const TCHAR* Key;
        const TCHAR* Asset;
        int32 Maximum;
        int32 Seed;
    };

    static const FCatalogEntry Catalog[] = {
        {TEXT("potion"), TEXT("/Script/AngelscriptAssets.ItemDef_InvGym_Potion"), 10, 6},
        {TEXT("arrow"), TEXT("/Script/AngelscriptAssets.ItemDef_InvGym_Arrow"), 99, 24},
        {TEXT("sword"), TEXT("/Script/AngelscriptAssets.ItemDef_InvGym_Sword"), 1, 1}};

    auto IsSlotValid(const FString& InSlot) -> bool
    {
        if (InSlot.IsEmpty() || InSlot.Len() > 64)
        { return false; }
        for (const auto Character : InSlot)
        {
            if (NOT ((Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z')
                || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-'))
            { return false; }
        }
        return true;
    }

    auto ValidateEntries(const TArray<FCk_GameplayInventoryProof_SavedEntry>& InEntries, FString& OutError) -> bool
    {
        if (InEntries.Num() != UE_ARRAY_COUNT(Catalog))
        { OutError = TEXT("Save must contain exactly the three catalog keys, including zero quantities."); return false; }
        auto Keys = TSet<FString>{};
        for (const auto& Entry : InEntries)
        {
            const FCatalogEntry* Definition = nullptr;
            for (const auto& Candidate : Catalog)
            { if (Entry.CatalogKey == Candidate.Key) { Definition = &Candidate; break; } }
            if (Definition == nullptr || Keys.Contains(Entry.CatalogKey))
            { OutError = TEXT("Save contains an unknown or duplicate catalog key."); return false; }
            if (Entry.Quantity < 0 || Entry.Quantity > Definition->Maximum)
            { OutError = TEXT("Save quantity is outside this catalog's supported range."); return false; }
            Keys.Add(Entry.CatalogKey);
        }
        return true;
    }
}

auto
    UCk_GameplayInventoryProof_UE::
    TryCreate(UObject* InWorldContextObject, FString& OutError)
    -> UCk_GameplayInventoryProof_UE*
{
    auto Entries = TArray<FCk_GameplayInventoryProof_SavedEntry>{};
    for (const auto& Entry : ck::gameplay_inventory_proof::Catalog)
    {
        auto Saved = FCk_GameplayInventoryProof_SavedEntry{};
        Saved.CatalogKey = Entry.Key;
        Saved.Quantity = Entry.Seed;
        Entries.Add(MoveTemp(Saved));
    }
    return TryCreateWithEntries(InWorldContextObject, Entries, OutError);
}

auto
    UCk_GameplayInventoryProof_UE::
    TryLoad(
    UObject* InWorldContextObject, const FString& InSlot, FString& OutError)
    -> UCk_GameplayInventoryProof_UE*
{
    OutError.Reset();
    if (NOT IsInGameThread() || NOT ck::gameplay_inventory_proof::IsSlotValid(InSlot))
    { OutError = TEXT("Load requires the game thread and a simple nonempty save slot name."); return nullptr; }
    if (ck::Is_NOT_Valid(InWorldContextObject))
    { OutError = TEXT("Load requires a valid world context."); return nullptr; }
    auto* Save = Cast<UCk_GameplayInventoryProof_SaveGame_UE>(UGameplayStatics::LoadGameFromSlot(InSlot, 0));
    if (ck::Is_NOT_Valid(Save) || Save->SchemaVersion != 1)
    { OutError = TEXT("Save is missing, unreadable, or has an unsupported schema."); return nullptr; }
    if (NOT ck::gameplay_inventory_proof::ValidateEntries(Save->Entries, OutError))
    { return nullptr; }
    return TryCreateWithEntries(InWorldContextObject, Save->Entries, OutError);
}

auto
    UCk_GameplayInventoryProof_UE::
    TryCreateWithEntries(
    UObject* InContext, const TArray<FCk_GameplayInventoryProof_SavedEntry>& InEntries, FString& OutError)
    -> UCk_GameplayInventoryProof_UE*
{
    OutError.Reset();
    if (NOT IsInGameThread() || ck::Is_NOT_Valid(InContext))
    { OutError = TEXT("Inventory creation requires the game thread and a valid context."); return nullptr; }
    auto* World = InContext->GetWorld();
    if (ck::Is_NOT_Valid(World) || NOT World->IsGameWorld() || World->bIsTearingDown)
    { OutError = TEXT("Inventory proof requires a live game world."); return nullptr; }
    const auto IsLocalAuthorityWorld = World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_ListenServer;
    if (NOT IsLocalAuthorityWorld)
    { OutError = TEXT("Inventory proof requires a standalone world or local listen host."); return nullptr; }
    auto* Ecs = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    if (ck::Is_NOT_Valid(Ecs) || Ecs->Get_LoadHold() != ECk_EcsWorld_LoadHold::None)
    { OutError = TEXT("Inventory ECS world is unavailable or held for load/teardown."); return nullptr; }
    const auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (ck::Is_NOT_Valid(Transient) || NOT UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Transient))
    { OutError = TEXT("Inventory world cannot admit a new lifetime owner."); return nullptr; }
    if (NOT ck::gameplay_inventory_proof::ValidateEntries(InEntries, OutError))
    { return nullptr; }

    // Resolve and validate the complete immutable catalog before creating any ECS entity.
    auto Definitions = TArray<TObjectPtr<UCk_InventoryItem_Definition>>{};
    for (const auto& Entry : ck::gameplay_inventory_proof::Catalog)
    {
        auto* Definition = LoadObject<UCk_InventoryItem_Definition>(nullptr, Entry.Asset);
        if (ck::Is_NOT_Valid(Definition))
        { OutError = FString::Printf(TEXT("Catalog asset is unavailable: %s"), Entry.Asset); return nullptr; }
        const auto* Stack = Definition->Get_ItemTrait<UCk_ItemTrait_Stackable>();
        const auto CatalogMatches = Entry.Maximum == 1 ? Stack == nullptr
            : ck::IsValid(Stack) && Stack->Get_InitialCount() == 1 && Stack->Get_HasMaxStackSize()
                && Stack->Get_MaxStackSize() == Entry.Maximum;
        if (NOT CatalogMatches)
        { OutError = TEXT("Catalog stack traits do not match the versioned save contract."); return nullptr; }
        Definitions.Add(Definition);
    }
    auto* Result = NewObject<UCk_GameplayInventoryProof_UE>(World);
    Result->_World = World;
    Result->_Definitions = MoveTemp(Definitions);
    Result->_ExpectedEntries = InEntries;
    Result->_Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    if (ck::Is_NOT_Valid(Result->_Owner))
    { OutError = TEXT("Inventory lifetime owner creation failed."); Result->Retire(OutError); return nullptr; }
    // Definition-built items inherit network metadata even when replication is disabled.
    UCk_Utils_Net_UE::Add(Result->_Owner, FCk_Net_ConnectionSettings{
        ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::ClientAndHost,
        ECk_Net_EntityNetRole::Authority});
    if (NOT UCk_Utils_Net_UE::Get_HasAuthority(Result->_Owner))
    { OutError = TEXT("Inventory lifetime owner has no authority."); Result->Retire(OutError); return nullptr; }
    const auto Params = UCk_Utils_Inventory_DataOnly_UE::Make_Params_Bounded(FGameplayTag{}, 3, {}, {});
    Result->_Inventory = UCk_Utils_Inventory_DataOnly_UE::Add(
        Result->_Owner, Params, ECk_Replication::DoesNotReplicate, World);
    if (NOT Result->Get_IsLiveAuthority())
    { OutError = TEXT("DataOnly inventory composition failed."); Result->Retire(OutError); return nullptr; }
    Result->_HydrationDeadline = FPlatformTime::Seconds() + 5.0;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(ck::gameplay_inventory_proof::Catalog); ++Index)
    {
        const auto* Entry = InEntries.FindByPredicate([Index](const auto& Candidate)
        { return Candidate.CatalogKey == ck::gameplay_inventory_proof::Catalog[Index].Key; });
        if (Entry->Quantity == 0)
        { continue; }
        auto Completion = FCk_Delegate_Request_OnCompleted{};
        Completion.BindDynamic(Result, &UCk_GameplayInventoryProof_UE::OnHydrationCompleted);
        ++Result->_PendingAdds;
        FCk_Handle_Inventory Inventory = Result->_Inventory;
        UCk_Utils_Inventory_UE::Request_AddItemByDefinition(Inventory,
            FCk_Request_Inventory_AddItemByDefinition{Result->_Definitions[Index].Get(), Entry->Quantity}, {}, Completion);
        if (Result->_Released)
        { OutError = Result->_LastError; return nullptr; }
    }
    return Result;
}

auto
    UCk_GameplayInventoryProof_UE::
    GetWorld() const
    -> UWorld*
{ return _World.Get(); }

auto
    UCk_GameplayInventoryProof_UE::
    Get_IsLiveAuthority() const
    -> bool
{
    if (NOT IsInGameThread() || _Released)
    { return false; }
    const auto* World = GetWorld();
    if (ck::Is_NOT_Valid(World) || World->bIsTearingDown)
    { return false; }
    const auto IsLocalAuthorityWorld = World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_ListenServer;
    if (NOT IsLocalAuthorityWorld)
    { return false; }
    const auto* Ecs = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    if (ck::Is_NOT_Valid(Ecs) || Ecs->Get_LoadHold() != ECk_EcsWorld_LoadHold::None)
    { return false; }
    if (ck::Is_NOT_Valid(_Owner) || ck::Is_NOT_Valid(_Inventory))
    { return false; }
    if (NOT UCk_Utils_Inventory_DataOnly_UE::Has(_Inventory))
    { return false; }
    return UCk_Utils_Net_UE::Get_HasAuthority(_Owner) && UCk_Utils_Net_UE::Get_HasAuthority(_Inventory);
}

auto
    UCk_GameplayInventoryProof_UE::
    Get_IsReady() const
    -> bool
{ return _Ready && Get_IsLiveAuthority(); }

auto
    UCk_GameplayInventoryProof_UE::
    Get_Inventory() const
    -> FCk_Handle_Inventory_DataOnly
{ return Get_IsReady() ? _Inventory : FCk_Handle_Inventory_DataOnly{}; }

auto
    UCk_GameplayInventoryProof_UE::
    Get_HasPendingWork() const
    -> bool
{
    if (NOT Get_IsLiveAuthority() || _PendingAdds != 0)
    { return true; }
    if (_Inventory.Has<ck::FFragment_Inventory_DataOnly_Requests>()
        && NOT _Inventory.Get<ck::FFragment_Inventory_DataOnly_Requests>().Get_Requests().IsEmpty()) { return true; }
    const auto Coordinator = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*GetWorld());
    if (ck::Is_NOT_Valid(Coordinator))
    { return true; }
    const auto ContainsOurs = [this](const auto& Submissions)
    {
        for (const auto& Submission : Submissions)
        { if (Submission.Source == _Inventory || Submission.Participants.Contains(_Inventory)) { return true; } }
        return false;
    };
    if (Coordinator.Has<ck::FFragment_Inventory_OperationCoordinator_Requests>()
        && ContainsOurs(Coordinator.Get<ck::FFragment_Inventory_OperationCoordinator_Requests>().Get_Requests())) { return true; }
    if (Coordinator.Has<ck::FFragment_Inventory_OperationCoordinator>()
        && ContainsOurs(Coordinator.Get<ck::FFragment_Inventory_OperationCoordinator>().Get_Pending())) { return true; }

    // Read-only census of our lifetime subtree also catches deferred stack-attribute writes.
    auto Work = TArray<FCk_Handle>{_Owner};
    auto Seen = TSet<FCk_Handle>{};
    for (int32 Index = 0; Index < Work.Num(); ++Index)
    {
        const auto Entity = Work[Index];
        if (ck::Is_NOT_Valid(Entity) || Work.Num() > 128)
        { return true; }
        if (Seen.Contains(Entity))
        { continue; }
        Seen.Add(Entity);
        if (Entity.Has_Any<FCk_Fragment_IntegerAttribute_PendingModifier, FCk_Fragment_IntegerAttribute_OverrideModifier,
            FCk_Fragment_IntegerAttribute_RemovePendingModifier, ck::FFragment_IntegerAttribute::FTag_RecomputeFinalValue,
            ck::FFragment_IntegerAttributeModifier::FTag_ComputeResult>()) { return true; }
        Work.Append(UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Entity));
    }
    return false;
}

auto
    UCk_GameplayInventoryProof_UE::
    TryReadCommitted(
    TArray<FCk_GameplayInventoryProof_SavedEntry>& OutEntries, FString& OutError) const
    -> bool
{
    if (NOT Get_IsLiveAuthority())
    { OutError = TEXT("Inventory owner is no longer live and authoritative."); return false; }
    auto Candidate = TArray<FCk_GameplayInventoryProof_SavedEntry>{};
    for (const auto& Entry : ck::gameplay_inventory_proof::Catalog)
    {
        auto Saved = FCk_GameplayInventoryProof_SavedEntry{};
        Saved.CatalogKey = Entry.Key;
        Candidate.Add(MoveTemp(Saved));
    }
    auto Seen = TSet<int32>{};
    const auto Items = UCk_Utils_Inventory_UE::Get_Items(_Inventory);
    if (Items.Num() > 3)
    { OutError = TEXT("Inventory exceeds the catalog entry limit."); return false; }
    for (const auto& Item : Items)
    {
        if (ck::Is_NOT_Valid(Item) || NOT UCk_Utils_Inventory_UE::Get_ContainsItem(_Inventory, Item))
        { OutError = TEXT("Inventory contains a stale item."); return false; }
        const auto* Definition = UCk_Utils_Item_UE::Get_Definition(Item);
        const auto Index = _Definitions.IndexOfByPredicate([Definition](const auto& Entry) { return Entry.Get() == Definition; });
        if (ck::Is_NOT_Valid(Definition) || Index == INDEX_NONE || Seen.Contains(Index))
        { OutError = TEXT("Inventory contains an unknown definition or duplicate catalog entry."); return false; }
        const auto Quantity = UCk_Utils_ItemTrait_Stackable_UE::Get_IsStackable(Item)
            ? UCk_Utils_ItemTrait_Stackable_UE::Get_StackCount(Item) : 1;
        if (Quantity <= 0 || Quantity > ck::gameplay_inventory_proof::Catalog[Index].Maximum)
        { OutError = TEXT("Inventory has an invalid committed quantity."); return false; }
        Candidate[Index].Quantity = Quantity;
        Seen.Add(Index);
    }
    OutEntries = MoveTemp(Candidate);
    return true;
}

auto
    UCk_GameplayInventoryProof_UE::
    Request_Save(const FString& InSlot, FString& OutError)
    -> bool
{
    OutError.Reset();
    if (NOT Get_IsReady() || Get_HasPendingWork())
    { OutError = TEXT("Inventory is not ready or still has deferred work."); return false; }
    if (NOT ck::gameplay_inventory_proof::IsSlotValid(InSlot))
    { OutError = TEXT("Save requires a simple nonempty slot name of at most 64 characters."); return false; }
    auto Entries = TArray<FCk_GameplayInventoryProof_SavedEntry>{};
    if (NOT TryReadCommitted(Entries, OutError))
    { return false; }
    auto* Save = NewObject<UCk_GameplayInventoryProof_SaveGame_UE>();
    Save->Entries = MoveTemp(Entries);
    if (NOT UGameplayStatics::SaveGameToSlot(Save, InSlot, 0))
    { OutError = TEXT("SaveGameToSlot failed; no save success is reported."); return false; }
    return true;
}

auto
    UCk_GameplayInventoryProof_UE::
    OnHydrationCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    -> void
{
    if (_Released)
    { return; }
    if (InRequestOwner != _Inventory || _PendingAdds <= 0 || InResult != ECk_Request_OperationResult::Succeeded)
    { Retire(TEXT("A required inventory hydration request failed or was cancelled.")); return; }
    --_PendingAdds;
}

auto
    UCk_GameplayInventoryProof_UE::
    Tick(float DeltaTime)
    -> void
{
    (void)DeltaTime;
    if (NOT Get_IsLiveAuthority())
    { Retire(TEXT("Inventory world or authority ended during hydration.")); return; }
    if (FPlatformTime::Seconds() > _HydrationDeadline)
    { Retire(TEXT("Inventory hydration did not commit before its deadline.")); return; }
    if (_PendingAdds != 0 || Get_HasPendingWork())
    { return; }
    auto Actual = TArray<FCk_GameplayInventoryProof_SavedEntry>{};
    auto Error = FString{};
    if (NOT TryReadCommitted(Actual, Error))
    { Retire(Error); return; }
    for (const auto& Expected : _ExpectedEntries)
    {
        const auto* Entry = Actual.FindByPredicate([&Expected](const auto& Candidate)
        { return Candidate.CatalogKey == Expected.CatalogKey; });
        if (Entry == nullptr || Entry->Quantity != Expected.Quantity)
        { Retire(TEXT("Hydration committed quantities differ from the complete accepted payload.")); return; }
    }
    _Ready = true;
    _ExpectedEntries.Reset();
}

auto
    UCk_GameplayInventoryProof_UE::
    IsTickable() const
    -> bool
{ return NOT IsTemplate() && NOT _Released && NOT _Ready; }

auto
    UCk_GameplayInventoryProof_UE::
    GetStatId() const
    -> TStatId
{ RETURN_QUICK_DECLARE_CYCLE_STAT(UCk_GameplayInventoryProof_UE, STATGROUP_Tickables); }

auto
    UCk_GameplayInventoryProof_UE::
    Retire(const FString& InError)
    -> void
{ _LastError = InError; Release(); }

auto
    UCk_GameplayInventoryProof_UE::
    Release()
    -> void
{
    if (_Released)
    { return; }
    _Released = true;
    _Ready = false;
    auto Owner = _Owner;
    _Owner = {};
    _Inventory = {};
    _PendingAdds = 0;
    _ExpectedEntries.Reset();
    if (ck::IsValid(Owner))
    { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner); }
}

auto
    UCk_GameplayInventoryProof_UE::
    BeginDestroy()
    -> void
{ Release(); Super::BeginDestroy(); }
