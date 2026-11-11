#pragma once

#include "CkInventory/Inventory/DataOnly/CkInventory_DataOnly_Fragment_Data.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "GameFramework/SaveGame.h"
#include "Tickable.h"

#include "CkGameplayInventoryProof.generated.h"

class UCk_InventoryItem_Definition;

/** Stable catalog identity and committed units; never contains an ECS handle. */
USTRUCT(BlueprintType)
struct CKTESTS_API FCk_GameplayInventoryProof_SavedEntry
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, BlueprintReadWrite)
    FString CatalogKey;

    UPROPERTY(SaveGame, BlueprintReadWrite)
    int32 Quantity = 0;
};

UCLASS()
class CKTESTS_API UCk_GameplayInventoryProof_SaveGame_UE final : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY(SaveGame)
    int32 SchemaVersion = 1;

    UPROPERTY(SaveGame)
    TArray<FCk_GameplayInventoryProof_SavedEntry> Entries;
};

/** Local-authority demo owner (standalone or listen host), without client replication.
 *  The caller must retain this UObject and explicitly release it. */
UCLASS(BlueprintType)
class CKTESTS_API UCk_GameplayInventoryProof_UE final : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Gameplay Inventory", meta = (WorldContext = "InWorldContextObject"))
    static UCk_GameplayInventoryProof_UE*
    TryCreate(UObject* InWorldContextObject, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Gameplay Inventory", meta = (WorldContext = "InWorldContextObject"))
    static UCk_GameplayInventoryProof_UE*
    TryLoad(UObject* InWorldContextObject, const FString& InSlot, FString& OutError);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gameplay Inventory")
    FCk_Handle_Inventory_DataOnly
    Get_Inventory() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gameplay Inventory")
    bool
    Get_IsReady() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gameplay Inventory")
    FString
    Get_LastError() const { return _LastError; }

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Gameplay Inventory")
    bool
    Request_Save(const FString& InSlot, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Gameplay Inventory")
    void
    Release();

    auto GetWorld() const -> UWorld* override;
    auto BeginDestroy() -> void override;
    auto Tick(float DeltaTime) -> void override;
    auto IsTickable() const -> bool override;
    auto GetTickableGameObjectWorld() const -> UWorld* override { return GetWorld(); }
    auto GetStatId() const -> TStatId override;

private:
    static auto TryCreateWithEntries(UObject* InContext,
        const TArray<FCk_GameplayInventoryProof_SavedEntry>& InEntries, FString& OutError) -> UCk_GameplayInventoryProof_UE*;
    auto Get_IsLiveAuthority() const -> bool;
    auto Get_HasPendingWork() const -> bool;
    auto TryReadCommitted(TArray<FCk_GameplayInventoryProof_SavedEntry>& OutEntries, FString& OutError) const -> bool;
    auto Retire(const FString& InError) -> void;

    UFUNCTION()
    void
    OnHydrationCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult);

    UPROPERTY(Transient)
    FCk_Handle _Owner;

    UPROPERTY(Transient)
    FCk_Handle_Inventory_DataOnly _Inventory;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UCk_InventoryItem_Definition>> _Definitions;

    UPROPERTY(Transient)
    TArray<FCk_GameplayInventoryProof_SavedEntry> _ExpectedEntries;

    TWeakObjectPtr<UWorld> _World;
    FString _LastError;
    int32 _PendingAdds = 0;
    double _HydrationDeadline = 0.0;
    bool _Ready = false;
    bool _Released = false;
};
