#pragma once

#include "CoreMinimal.h"
#include "CkCompassGym_SourceCatalog.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkCompassGym_Wire.generated.h"

UENUM(BlueprintType)
enum class ECkCompassGym_WireLifecycle : uint8
{
    Initializing,
    Active,
    Failed
};

// Values only: no source receipts, ECS handles, actors, or scene objects.
// SourceMarker.Lines remains NotReplicated; it is reconstructed locally.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkCompassGym_WireSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int32 SchemaVersion = 1;
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    UPROPERTY(BlueprintReadWrite) ECkCompassGym_WireLifecycle Lifecycle =
        ECkCompassGym_WireLifecycle::Initializing;
    UPROPERTY(BlueprintReadWrite) int32 DeclaredRowCount = 0;
    UPROPERTY(BlueprintReadWrite) TArray<FCkCompassGym_SourceRow> Rows;
};

UCLASS()
class CKTESTS_API UCkCompassGym_Wire : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Validates the complete envelope and each value row. Distinct output is
    // cleared on failure; aliasing Snapshot.Rows is rejected without writes.
    // Successful marker rows contain exactly the local 96-line sphere recipe.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryValidateAndExpand(const FCkCompassGym_WireSnapshot& Snapshot,
        TArray<FCkCompassGym_SourceRow>& OutExpandedRows);

    // Both complete snapshots must validate. Local line-array presence does
    // not affect equality: each is first checked against the same recipe.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Compass")
    static bool Get_IsSameFrame(const FCkCompassGym_WireSnapshot& A,
        const FCkCompassGym_WireSnapshot& B);

    // Initial default -> positive epoch, or lexicographically newer (G, Q).
    // Within a generation lifecycle cannot move backwards or revive Failed.
    // Same identity is not an advance; use Get_IsSameFrame for a duplicate.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Compass")
    static bool Get_IsAdvance(const FCkCompassGym_WireSnapshot& Previous,
        const FCkCompassGym_WireSnapshot& Next);
};
