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
