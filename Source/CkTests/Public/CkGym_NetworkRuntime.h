#pragma once

#include "CoreMinimal.h"

#include "CkGym_Registry.h"
#include "CkCore/Engine/CkPlayerController.h"

#include <GameFramework/GameStateBase.h>
#include <Containers/Ticker.h>

#include "CkGym_NetworkRuntime.generated.h"

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGym_CatalogSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TArray<FCkGym_Entry> Entries;

    // Stable load paths let a client complete class resolution after the replicated object refs arrive.
    UPROPERTY(BlueprintReadOnly)
    TArray<FString> GameModeClassPaths;

    UPROPERTY(BlueprintReadOnly)
    int32 CurrentGymIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly)
    bool SuppressHUDDuringStartup = false;

    UPROPERTY(BlueprintReadOnly)
    bool Published = false;
};

// One authoritative publication per complete RegisterAll/startup decision. Replication transfers
// entries and menu state as one property, and the client validates before replacing its local view.
UCLASS(BlueprintType, Blueprintable)
class CKTESTS_API ACkGym_GameState_UE : public AGameStateBase
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Ck|Gym")
    bool PublishGymCatalog();

    UFUNCTION(BlueprintPure, Category = "Ck|Gym")
    bool Get_HasPublishedCatalog() const;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void OnRep_Catalog();

    UPROPERTY(ReplicatedUsing = OnRep_Catalog)
    FCkGym_CatalogSnapshot _Catalog;

    bool _CatalogAccepted = false;
    double _CatalogRetryDeadline = 0.0;
    double _NextCatalogRetryTime = 0.0;
    FTSTicker::FDelegateHandle _CatalogRetryHandle;

    auto TryApplyCatalog() -> bool;
    auto PollCatalogRetry(float InDeltaSeconds) -> bool;
    auto StopCatalogRetry() -> void;

};

// The owning client sends only a stable name. The authoritative registry supplies map and class.
UCLASS(BlueprintType, Blueprintable)
class CKTESTS_API ACkGym_PlayerController_UE : public ACk_PlayerController_UE
{
    GENERATED_BODY()

public:
    ACkGym_PlayerController_UE();

    UFUNCTION(Server, Reliable)
    void Server_RequestTravelToGym(const FString& InRegisteredName, int32 InRequestId);

    UFUNCTION(Client, Reliable)
    void Client_ReportTravelResult(int32 InRequestId, ECkGym_TravelResult InResult, const FString& InDetail);

    UFUNCTION(BlueprintCallable, Category = "Ck|Gym")
    bool TryQueueTravelRequest(const FString& InRegisteredName);

    UFUNCTION(BlueprintPure, Category = "Ck|Gym")
    ECkGym_TravelResult Get_LastTravelResult() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Gym")
    FString Get_LastTravelDetail() const;

private:
    auto GetLocalRegistry() const -> UCkGym_Registry_Subsystem*;
};
