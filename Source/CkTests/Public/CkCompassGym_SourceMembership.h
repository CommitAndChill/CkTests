#pragma once

#include "CoreMinimal.h"
#include "CkCompassGym_SourceCatalog.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkCompassGym_SourceMembership.generated.h"

class AActor;
class UWorld;

UENUM(BlueprintType)
enum class ECkCompassGym_MembershipResult : uint8
{
    Unavailable, Pending, Committed, Rejected
};

// Authority-only queue. Results remain readable until a later accepted serial.
UCLASS()
class CKTESTS_API UCkCompassGym_SourceMembership : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // One-shot authority queue for a complete authored but unexposed Base9.
    // The ordered barrier proves all nine before exposing any POI tag.
    // Poll TryGet_CurrentRows for the first complete publication. A retained
    // CatalogId names immutable canonical row values; a later change to a
    // surviving row closes source admission instead of silently updating it.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryRequest_BaseAdmission(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        const TArray<FCkCompassGym_SourceReceipt>& BaseReceipts9,
        int64 RequestSerial,
        FString& OutFailureReason);

    // Bind the immutable nine-receipt source base after its whole physical
    // proof. Repeating the exact same base is idempotent.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryInitialize_BaseCohort(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        const TArray<FCkCompassGym_SourceReceipt>& BaseReceipts9,
        FString& OutFailureReason);

    // Retain a newly authored, unexposed Ping first-return receipt. Its
    // CatalogId must exceed every accepted base, Stress, or Ping ID, even
    // after an older Ping is physically gone. The ordered processor alone
    // publishes it after physical proof. Initialize the base first; bind
    // OnDone before the timer starts counting, then stage its exact receipt.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryStage_Ping(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        const FCkCompassGym_SourceReceipt& StagedPingReceipt,
        FString& OutFailureReason);

    // Call from the authored timer's real OnDone delivery. The API checks
    // current Done state and exact retained Timer identity, but cannot prove
    // signal-call provenance by itself.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryRetire_Ping(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        const FCk_Handle& ExactDoneTimer,
        FString& OutFailureReason);

    // Last complete active source projection published by the ordered
    // barrier; independent of the historical stress request result serial.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryGet_CurrentRows(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        int64& OutRevision,
        TArray<FCkCompassGym_SourceRow>& OutActiveRows,
        FString& OutFailureReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryRequest_StressAdmission(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        const TArray<FCkCompassGym_SourceReceipt>& ActiveReceipts,
        const TArray<FCkCompassGym_SourceReceipt>& StagedStressReceipts,
        int64 RequestSerial, FString& OutFailureReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryRequest_StressWithdrawal(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        const TArray<FCkCompassGym_SourceReceipt>& ActiveReceipts509,
        int64 RequestSerial, FString& OutFailureReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static ECkCompassGym_MembershipResult Get_Result(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        int64 RequestSerial,
        TArray<FCkCompassGym_SourceRow>& OutActiveRows,
        TArray<FCkCompassGym_SourceReceipt>& OutFrozenRetiringStressReceipts,
        FString& OutFailureReason);
};
