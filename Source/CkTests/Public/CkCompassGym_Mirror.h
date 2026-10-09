#pragma once

#include "CoreMinimal.h"
#include "CkCompassGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkCompassGym_Mirror.generated.h"

class AGameStateBase;
class UWorld;

// One authority-proven value projection per client World/replicated GameState.
// Local player controllers independently own Compass observers and overlays.
UCLASS()
class CKTESTS_API UCkCompassGym_Mirror : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Mirrors are client-world only. A pending transaction rejects newer
    // snapshots; the GS retries after TryGet_Installed reports completion.
    // An unchanged CatalogId must retain its entire canonical row value.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryQueue_Snapshot(UWorld* World, AGameStateBase* ExactGameState,
        const FCk_Handle& MirrorRoot,
        const FCkCompassGym_WireSnapshot& Snapshot, FString& OutFailureReason);

    // Last completely installed frame. Clears all outputs on failure.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryGet_Installed(UWorld* World, AGameStateBase* ExactGameState,
        const FCk_Handle& MirrorRoot, int64& OutGeneration,
        int64& OutSequence, TArray<FCkCompassGym_SourceRow>& OutRows,
        FString& OutFailureReason);

    // Distinguishes an ordinary pending transaction from terminal failure.
    // It reads the exact captured tuple even after world projection closure.
    // False means the tuple/status is unavailable, never healthy/idle.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static bool TryGet_Status(UWorld* World, AGameStateBase* ExactGameState,
        const FCk_Handle& MirrorRoot, bool& OutBusy, bool& OutFailed,
        FString& OutFailureReason);
};
