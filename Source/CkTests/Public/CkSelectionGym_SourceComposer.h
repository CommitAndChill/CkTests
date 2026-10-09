#pragma once

#include "CoreMinimal.h"
#include "CkSelectionGym_SourceSession.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSelectionGym_SourceComposer.generated.h"

class UCk_SmState_EntityScript;

// One authority creator attempt. The GameState retains this object through
// physical retirement; failure never retries composition in the same world.
UCLASS(BlueprintType)
class CKTESTS_API UCkSelectionGym_SourceComposition final : public UObject
{
    GENERATED_BODY()

private:
    friend class UCkSelectionGym_SourceComposer_UE;
    UPROPERTY(Transient) TObjectPtr<UCkSelectionGym_SourceSession> Session;
    UPROPERTY(Transient) TWeakObjectPtr<UWorld> World;
    UPROPERTY(Transient) TWeakObjectPtr<AGameStateBase> GameState;
    UPROPERTY(Transient) FCk_Handle WorldRoot;
    UPROPERTY(Transient) TArray<FCk_Handle> BareRoots;
    UPROPERTY(Transient) TArray<FCk_Handle> AuthoredNodes;
    UPROPERTY(Transient) TArray<FCk_Handle> AuthoredParents;
    UPROPERTY(Transient) TArray<FCkSelectionGym_SourceVisualReceipt> Visuals;
    UPROPERTY(Transient) TSubclassOf<AActor> MarkerClass;
    UPROPERTY(Transient) TSubclassOf<AActor> WallClass;
    UPROPERTY(Transient) FCk_Handle MovingNode;
    UPROPERTY(Transient) FCk_Handle RespawnNode;
    int64 MovingVisualKey = 0;
    int64 RespawnVisualKey = 0;
    int64 NextVisualKey = 1;
    int32 Scenario = -1;
    FVector Origin = FVector::ZeroVector;
    double MovingElapsed = 0.0;
    bool Failed = false;
    bool Staged = false;
    bool Activated = false;
    bool RespawnPending = false;
    bool RespawnReady = false;
    bool RespawnAttempted = false;
};

UCLASS()
class CKTESTS_API UCkSelectionGym_SourceComposer_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

private:
    static bool Fail(UCkSelectionGym_SourceComposition* Composition,
        const FString& Reason, FString& OutReason);
    static bool ProveKnown(const UCkSelectionGym_SourceComposition* Composition);

public:
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryCompose(UWorld* World, AGameStateBase* ExactGameState,
        const FCk_Handle& WorldRoot, int64 SourceGeneration,
        int64 ScenarioGeneration, int64 AdmissionGeneration, int32 Scenario,
        FVector Origin, TSubclassOf<AActor> MarkerClass,
        TSubclassOf<AActor> WallClass,
        TSubclassOf<UCk_SmState_EntityScript> IdleStateClass,
        UCkSelectionGym_SourceComposition*& OutComposition,
        FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryPollStage(
        UCkSelectionGym_SourceComposition* Composition, FString& OutReason);

    // Call in the same authority action as visual activation. Only a complete
    // physical readback may be published by the GameState.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryActivateAndSample(
        UCkSelectionGym_SourceComposition* Composition,
        FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryMoveAndSample(
        UCkSelectionGym_SourceComposition* Composition, double DeltaSeconds,
        FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryStartRespawn(UCkSelectionGym_SourceComposition* Composition,
        FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryAdvanceRespawn(
        UCkSelectionGym_SourceComposition* Composition, FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryFinishRespawn(
        UCkSelectionGym_SourceComposition* Composition,
        FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Selection")
    static UCkSelectionGym_SourceSession* Get_Session(
        const UCkSelectionGym_SourceComposition* Composition);

#if WITH_DEV_AUTOMATION_TESTS
    // Narrow native fixture hook: the exact first-retained source root.
    // It does not grant mutation or reconstruct a missing creator receipt.
    static bool TryGet_PrimaryBareRootForTest(
        const UCkSelectionGym_SourceComposition* Composition,
        FCk_Handle& OutRoot);
#endif
};
