#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkSelectionGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkSelectionGym_Mirror.generated.h"

class AActor;
class AGameStateBase;
class UWorld;

UENUM(BlueprintType)
enum class ECkSelectionGym_MirrorStatus : uint8
{
    Unavailable,
    Pending,
    Installed,
    Failed,
};

/** One passive selection bank per client world and exact replicated GameState. */
UCLASS()
class CKTESTS_API UCkSelectionGym_Mirror_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** True means the complete Active value was accepted, whether installed or pending. */
    /** False means the value was rejected; a failure after mutation closes this world session. */
    UFUNCTION(BlueprintCallable, Category="CK|SelectionGym|Mirror")
    static bool TryAcceptActive(UWorld* World, AGameStateBase* ExactGameState,
        const FCkSelectionGym_WireSnapshot& Snapshot,
        TSubclassOf<AActor> MarkerClass, TSubclassOf<AActor> WallClass,
        FString& OutReason);

    /** Empty lifecycle envelope: closes old exposure on reset and absorbs Failed. */
    UFUNCTION(BlueprintCallable, Category="CK|SelectionGym|Mirror")
    static bool TryAcceptEnvelope(UWorld* World, AGameStateBase* ExactGameState,
        const FCkSelectionGym_WireSnapshot& Snapshot, FString& OutReason);

    /** Fails only the mirror captured on this exact client world/root and GameState. */
    UFUNCTION(BlueprintCallable, Category="CK|SelectionGym|Mirror")
    static bool TryFailCaptured(UWorld* World, AGameStateBase* ExactGameState,
        const FCk_Handle& CapturedWorldRoot, const FString& Reason,
        FString& OutReason);

    /** Completes deferred SceneNode setup and publishes the entire staged bank at once. */
    UFUNCTION(BlueprintCallable, Category="CK|SelectionGym|Mirror")
    static bool TryAdvance(UWorld* World, AGameStateBase* ExactGameState,
        FString& OutReason);

    /** Read-only installed value and local scenario root; outputs clear on every failure. */
    UFUNCTION(BlueprintCallable, Category="CK|SelectionGym|Mirror")
    static bool TryGetInstalled(UWorld* World, AGameStateBase* ExactGameState,
        FCkSelectionGym_WireSnapshot& OutSnapshot, FCk_Handle& OutScenarioRoot,
        FString& OutReason);

#if WITH_DEV_AUTOMATION_TESTS
    /** Read-only installed wire-key mapping after the complete physical/provider proof. */
    static bool TryGetLocalNodeForTest(UWorld* World,
        AGameStateBase* ExactGameState, int64 NodeKey, FCk_Handle& OutNode);
#endif

    /** Reports the exact session phase without treating Pending as a failed install. */
    UFUNCTION(BlueprintCallable, Category="CK|SelectionGym|Mirror")
    static bool TryGetStatus(UWorld* World, AGameStateBase* ExactGameState,
        ECkSelectionGym_MirrorStatus& OutStatus, FString& OutReason);
};
