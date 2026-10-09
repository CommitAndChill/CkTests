#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkSelectionGym_Wire.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSelectionGym_SourceSession.generated.h"

class AActor;
class AGameStateBase;
class UWorld;
class UStaticMeshComponent;
class UTextRenderComponent;
class UCk_SmState_EntityScript;

UENUM(BlueprintType)
enum class ECkSelectionGym_SourceOutcome : uint8
{
    Pending,
    Complete,
    Invalid
};

// Exact first-return visual identity, never inferred from a matching class alone.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_SourceVisualReceipt
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int64 VisualKey = 0;
    UPROPERTY(BlueprintReadWrite) ECkSelectionGym_VisualKind Kind =
        ECkSelectionGym_VisualKind::Invalid;
    UPROPERTY(BlueprintReadWrite) FCk_Handle Node;
    UPROPERTY(Transient) TWeakObjectPtr<AActor> Actor;
    UPROPERTY(Transient) TWeakObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(Transient) TWeakObjectPtr<UTextRenderComponent> Text;
};

// GameState retains this object strongly. A session never discovers an omitted
// creator receipt by reconstructing a plausible source from wire values.
UCLASS(BlueprintType)
class CKTESTS_API UCkSelectionGym_SourceSession final : public UObject
{
    GENERATED_BODY()

private:
    friend class UCkSelectionGym_SourceSession_UE;

    UPROPERTY(Transient) TWeakObjectPtr<UWorld> World;
    UPROPERTY(Transient) TWeakObjectPtr<AGameStateBase> GameState;
    UPROPERTY(Transient) FCk_Handle WorldRoot;
    UPROPERTY(Transient) TArray<FCk_Handle> BareRoots;
    UPROPERTY(Transient) TArray<FCk_Handle> AuthoredNodes;
    UPROPERTY(Transient) FCk_Handle FrameworkIdleState;
    UPROPERTY(Transient) TArray<FCk_Handle> FrozenRetirement;
    UPROPERTY(Transient) TArray<FCkSelectionGym_SourceVisualReceipt> Visuals;
    UPROPERTY(Transient) TSubclassOf<AActor> MarkerClass;
    UPROPERTY(Transient) TSubclassOf<AActor> WallClass;
    UPROPERTY(Transient) TSubclassOf<UCk_SmState_EntityScript> IdleStateClass;
    int64 SourceGeneration = 0;
    int64 ScenarioGeneration = 0;
    int64 AdmissionGeneration = 0;
    int64 NextSequence = 1;
    int32 Scenario = -1;
    FVector Origin = FVector::ZeroVector;
    bool Staged = false;
    bool EverComplete = false;
    bool Failed = false;
    UPROPERTY(Transient) FString TerminalReason;
    bool Retiring = false;
    bool RespawnMode = false;
    bool Replacing = false;
    FCk_Handle OldRespawnNode;
    int64 OldRespawnVisualKey = 0;
};

UCLASS()
class CKTESTS_API UCkSelectionGym_SourceSession_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

private:
    static bool Current(const UCkSelectionGym_SourceSession* Session);
    static bool ValidVisual(const UCkSelectionGym_SourceSession* Session,
        const FCkSelectionGym_SourceVisualReceipt& Visual);
    static bool ProveFrozenRetirement(const UCkSelectionGym_SourceSession* Session);
    static bool ExactOwnedVisualCensus(const UCkSelectionGym_SourceSession* Session);
    static ECkSelectionGym_SourceOutcome CheckExactNodes(
        const UCkSelectionGym_SourceSession* Session,
        const TSet<FCk_Handle>& Authored,
        const TSet<FCk_Handle>& Whole, bool AllowInitialPending,
        FCk_Handle& OutIdleState, FString& OutReason);

public:
    // Bare roots are created and registered in this single action, before any
    // selectable Transform or label is added. SourceGeneration is world-scoped;
    // AdmissionGeneration advances for each replacement scenario.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryBegin(UWorld* World, AGameStateBase* ExactGameState,
        const FCk_Handle& WorldRoot, int64 SourceGeneration,
        int64 ScenarioGeneration, int64 AdmissionGeneration, int32 Scenario,
        FVector Origin, const TArray<FCk_Handle>& BareRoots,
        TSubclassOf<AActor> MarkerClass, TSubclassOf<AActor> WallClass,
        TSubclassOf<UCk_SmState_EntityScript> IdleStateClass,
        UCkSelectionGym_SourceSession*& OutSession, FString& OutReason);

    // Call immediately after each successful actor spawn, before further
    // composition. This retains partial creator provenance for TryFail.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryRetainVisual(UCkSelectionGym_SourceSession* Session,
        const FCkSelectionGym_SourceVisualReceipt& Visual,
        FString& OutReason);

    // One shot after all authored node receipts exist. A runtime-created SM
    // state/request child is found by the separate complete-bank census.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryStage(UCkSelectionGym_SourceSession* Session,
        const TArray<FCk_Handle>& AuthoredNodes,
        FString& OutReason);

    // Read-only pre-stage gate. Pending permits one proven native request
    // carrier, or one already-retiring unadmitted leaf beneath the proven
    // current Idle state after native request metadata was consumed. The
    // latter has no inferable historical request provenance: it is never
    // adopted, sampled, published or destroyed by this session. Stage waits
    // for physical deallocation and then requires the exact settled census.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryCheckStageReadiness(
        UCkSelectionGym_SourceSession* Session,
        const TArray<FCk_Handle>& AuthoredNodes, FString& OutReason);

    // Read the real current actor/components after the whole-bank final action
    // has enabled the exact retained visual. This never derives active style
    // from spawn parameters.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryReadVisual(UCkSelectionGym_SourceSession* Session,
        const FCkSelectionGym_SourceVisualReceipt& Visual,
        FCkSelectionGym_VisualRow& OutRow, FString& OutReason);

    // Complete is an actual current-source read. Pending is limited to the
    // exact initial all-hidden visual stage; all outputs clear otherwise.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TrySample(
        UCkSelectionGym_SourceSession* Session,
        FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason);

    // Only non-Active lifecycle envelopes. Consumes the same session-owned
    // monotone Sequence as TrySample and carries no ECS or visual rows.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryMakeLifecycleEnvelope(
        UCkSelectionGym_SourceSession* Session,
        ECkSelectionGym_WireLifecycle Lifecycle,
        FCkSelectionGym_WireSnapshot& OutSnapshot, FString& OutReason);

    // Ordered, one-leaf-at-a-time authority retirement. The caller stops
    // authored producers before Start, then polls Advance until Complete.
    // Invalid is terminal; no foreign or newly discovered child is adopted.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryStartRetirement(UCkSelectionGym_SourceSession* Session,
        FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryAdvanceRetirement(
        UCkSelectionGym_SourceSession* Session, FString& OutReason);

    // Scenario 4 X: retire only the old respawn leaf/marker while preserving
    // the exact main root and moving candidate. Complete permits one new
    // creator attempt; TryInstallRespawnReplacement then proves the new bank.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryStartRespawnRetirement(UCkSelectionGym_SourceSession* Session,
        const FCk_Handle& OldNode, int64 OldVisualKey, FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static ECkSelectionGym_SourceOutcome TryAdvanceRespawnRetirement(
        UCkSelectionGym_SourceSession* Session, FString& OutReason);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryInstallRespawnReplacement(UCkSelectionGym_SourceSession* Session,
        const FCk_Handle& NewNode,
        const FCkSelectionGym_SourceVisualReceipt& NewVisual,
        FString& OutReason);

    // Terminal closure; only exact retained, still-owned visual actors are
    // hidden/disabled. Uncertain objects remain for world teardown.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Selection")
    static bool TryFail(UCkSelectionGym_SourceSession* Session,
        const FString& Reason, FString& OutReason);
};
