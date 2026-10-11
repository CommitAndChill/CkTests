#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcsExt/Transform/CkTransform_Fragment_Data.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment_Data.h"
#include "CkRotateTowards/CkRotateTowards_Fragment_Data.h"
#include "CkTween/CkTween_Fragment_Data.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment_Data.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkRotateTowardsGym_Runtime.generated.h"

class AGameStateBase;
class UStaticMeshComponent;
class UWorld;

UENUM(BlueprintType)
enum class ECkRotateTowardsGym_ActiveCohortState : uint8
{
    Rejected,
    Pending,
    Ready
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkRotateTowardsGym_MotionAdmission
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int32 Generation = 0;
    UPROPERTY(BlueprintReadWrite) FCk_Handle Owner;
    // Captured synchronously while each EntityScript spawn request still has
    // its typed request fragment. These are construction provenance, not roles.
    UPROPERTY(BlueprintReadWrite) FCk_Handle OwnerSpawnCarrier;
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> CubeSpawnCarriers;
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle_Transform> Roots;
    // Target0, Target1, Rest1, Carrier2, TargetNode2 (heterogeneous Transform/SceneNode).
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> Motion;
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle_RotateTowards> Turrets;
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle_Tween> Tweens;
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle_SceneNode> CubeNodes;
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle_UnrealComponent> CubeComponents;
    UPROPERTY(BlueprintReadWrite) TArray<int32> ComponentAdded;
    UPROPERTY(BlueprintReadWrite) bool Activated = false;
    UPROPERTY(BlueprintReadWrite) bool Closed = false;
};

// Slots 0..6 are turrets and 7..10 are tweens. Parallel arrays retain the
// original slot and identity, including a default identity for an absent slot.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkRotateTowardsGym_QuiesceResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) TArray<int32> StoppedSlots;
    UPROPERTY(BlueprintReadOnly) TArray<FCk_Handle> Stopped;
    UPROPERTY(BlueprintReadOnly) TArray<int32> AbsentSlots;
    UPROPERTY(BlueprintReadOnly) TArray<FCk_Handle> Absent;
    UPROPERTY(BlueprintReadOnly) TArray<int32> UnsafeSlots;
    UPROPERTY(BlueprintReadOnly) TArray<FCk_Handle> Unsafe;
    UPROPERTY(BlueprintReadOnly) int32 StoppedCount = 0;
    UPROPERTY(BlueprintReadOnly) int32 AbsentCount = 0;
    UPROPERTY(BlueprintReadOnly) int32 UnsafeCount = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FCk_Handle> Unexpected;
    UPROPERTY(BlueprintReadOnly) int32 UnexpectedCount = 0;
    UPROPERTY(BlueprintReadOnly) bool SchemaUnsafe = false;
    UPROPERTY(BlueprintReadOnly) bool AllKnownStopped = false;
};

UCLASS()
class CKTESTS_API UCk_RotateTowardsGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Exact production cube replica for a world, generation, and authored slot.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static FCk_Handle TryGet_ReplicaCube(UWorld* World, int32 Generation, int32 Slot);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static UStaticMeshComponent* TryGet_ReplicaCubeMesh(
        UWorld* World, int32 Generation, int32 Slot);

    // A replicated Transform may apply before the component push in that frame.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool Get_IsReplicaCubePoseConformant(
        UWorld* World, int32 Generation, int32 Slot);

    // Pure client readback. A live entity in this exact world must have no
    // Rotate or Tween fragments/tags, including partially assembled roles.
    // This grants no ownership, mutation or cleanup permission.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool Get_IsPassiveMotionEntity(const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot);

    // Pure allocated-inclusive role check for a frozen retirement ledger.
    // Accepts an allocated BeginDestroy/Teardown member, but never grants
    // ownership or permission to mutate or destroy that member.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool Get_IsPassiveAllocatedMotionEntity(const FCk_Handle& Entity,
        const FCk_Handle& WorldRoot);

    // One-shot native claim on a fresh, empty owner before any fixture child
    // is created. The private scope cannot be supplied by AngelScript.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|RotateTowards")
    static bool TryClaim_NewOwner(AGameStateBase* Source,
        const FCk_Handle& Owner, int32 Generation);

    // Startup-only context gate, callable before fixture children exist. The source
    // must store its exact generation and owner before calling any creator/bind.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool Get_IsSourceContext(AGameStateBase* Source,
        const FCk_Handle& Owner, int32 Generation);

    // True once the known script construction and feature request queues have
    // drained. Unknown descendants are left for the exact admission check to reject.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool Get_IsSourceConstructionDrained(AGameStateBase* Source);

    // Reads and compares the actual current GameState frame; copies cannot authorize activation.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool TryValidate_Cohort(AGameStateBase* Source,
        const FCkRotateTowardsGym_MotionAdmission& Candidate);

    // Pure value-state query; any invalid role wins over a pending scene pose.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static ECkRotateTowardsGym_ActiveCohortState Get_ActiveCohortState(
        AGameStateBase* Source);

    // Pure readback of the actual activated source and its currently live cohort.
    // Motion may have advanced; this is distinct from fresh startup admission.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static bool TryValidate_ActiveCohort(AGameStateBase* Source);

    // Read-only stage/slot details for the actual rejected active cohort.
    // It never grants admission or changes the validation predicate.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|RotateTowards")
    static FString Get_ActiveCohortDiagnostic(AGameStateBase* Source);

    // Commit has no callbacks, request enqueueing or fallible checks after full preflight.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|RotateTowards")
    static bool TryActivate_Cohort(AGameStateBase* Source);

    // Closes the actual source frame, then stops each independently proven
    // known producer. Destruction requires separate exact frozen-closure proof.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|RotateTowards")
    static bool TryQuiesce_KnownProducers(AGameStateBase* Source,
        FCkRotateTowardsGym_QuiesceResult& OutResult);
};
