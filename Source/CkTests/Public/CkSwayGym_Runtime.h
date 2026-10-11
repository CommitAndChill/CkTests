#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSwayGym_Runtime.generated.h"

class AGameStateBase;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;
class UCk_SwayGym_SourceRuntime_UE;

// Source creation-order inputs. Every slot is a creator return, not a census adoption.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkSwayGym_SourceRoles
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FCk_Handle OwnerRoot;          // stable GameState WithActor bridge
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> Roots;       // station 0..2
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> Heads;       // station 0..2
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> Tweens;      // 0 yaw, 1 X, 2 yaw, 2 X
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> Sways;       // 0, 1, 2 default/heavy/disabled
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> CubeNodes;   // 0 sway/rigid, 1 sway/rigid, 2 default/heavy/disabled
    UPROPERTY(BlueprintReadWrite) TArray<FCk_Handle> Components;  // same order as CubeNodes
};

// One actual post-ECS authority observation. No handles are replicated.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkSwayGym_Frame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 Generation = 0;
    UPROPERTY(BlueprintReadOnly) int64 FrameSerial = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FTransform> HeadWorldPoses;
    UPROPERTY(BlueprintReadOnly) TArray<FTransform> CubeWorldPoses;
    UPROPERTY(BlueprintReadOnly) TArray<float> SwayYawDegrees;
    UPROPERTY(BlueprintReadOnly) TArray<float> SwayXCm;
    UPROPERTY(BlueprintReadOnly) TArray<bool> SwaySettled;
};

UENUM(BlueprintType)
enum class ECk_SwayGym_RetirementState : uint8
{
    Active,
    Pending,
    Complete,
    Unproven
};

UCLASS()
class CKTESTS_API UCk_SwayGym_StopReceiver_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SwayGym_SourceRuntime_UE;

    UPROPERTY(Transient) TObjectPtr<class UCk_SwayGym_SourceReceipt_UE> Receipt;
    int32 Slot = INDEX_NONE;
    bool bReceived = false;
    UFUNCTION() void OnCompleted(FCk_Handle Owner, ECk_Request_OperationResult Result);
};

// Opaque authority receipt, retained by the actual GameState across callbacks.
UCLASS()
class CKTESTS_API UCk_SwayGym_SourceReceipt_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SwayGym_SourceRuntime_UE;
    friend class UCk_SwayGym_StopReceiver_UE;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FCk_SwayGym_Source_TestAccess;
#endif

    TWeakObjectPtr<AGameStateBase> Source;
    TWeakObjectPtr<UWorld> World;
    FCk_Handle WorldRoot;
    FCk_Handle OwnerRoot;
    FCkSwayGym_SourceRoles Roles;
    TArray<FCk_Handle> Known;
    TArray<FCk_Handle> ExpectedParents;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> SourceMeshes;
    UPROPERTY(Transient) TArray<TObjectPtr<UCk_SwayGym_StopReceiver_UE>> StopReceivers;
    FCkSwayGym_Frame Latest;
    FDelegateHandle PostTickHandle;
    FDelegateHandle WorldCleanupHandle;
    double StartedAtSeconds = 0.0;
    uint64 PublishedEngineFrame = 0;
    int32 Generation = 0;
    int32 StopCallbackCount = 0;
    ECk_SwayGym_RetirementState State = ECk_SwayGym_RetirementState::Unproven;
    FName Reason = NAME_None;
    bool bHasFrame = false;
    bool bInTick = false;
    bool bDestroyIssued = false;
    bool bStopIssued = false;
    bool bStopFailed = false;
    bool bClosed = true;
    bool bSuccessorAttempted = false;

    UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);
    void OnWorldPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);
    void Unbind();
    virtual void BeginDestroy() override;
};

UCLASS()
class CKTESTS_API UCk_SwayGym_SourceRuntime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SwayGym_SourceReceipt_UE;
    friend class UCk_SwayGym_StopReceiver_UE;

public:
    // Poll after all staged EntityScript spawn callbacks complete, before the
    // one-way source receipt admission. Does not claim or mutate the source.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Sway")
    static bool Get_IsSourceConstructionDrained(AGameStateBase* Source,
        const FCkSwayGym_SourceRoles& Roles);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Sway")
    static UCk_SwayGym_SourceReceipt_UE* TryBegin(AGameStateBase* Source,
        int32 Generation, const FCk_Handle& WorldRoot,
        const FCkSwayGym_SourceRoles& Roles);

    // The prior receipt is Complete and its exact 29 identities are absent.
    // A failed new construction is terminal; no failed-partial retry is granted.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Sway")
    static UCk_SwayGym_SourceReceipt_UE* TryBeginSuccessor(
        AGameStateBase* Source, UCk_SwayGym_SourceReceipt_UE* CompletePrevious,
        int32 Generation, const FCk_Handle& WorldRoot,
        const FCkSwayGym_SourceRoles& Roles);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Sway")
    static bool TryCopyLatestFrame(const UCk_SwayGym_SourceReceipt_UE* Receipt,
        FCkSwayGym_Frame& OutFrame);

    // One healthy-complete attempt. A rejected or partly issued Stop remains Unproven.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Sway")
    static bool TryBeginHealthyRetirement(UCk_SwayGym_SourceReceipt_UE* Receipt);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Sway")
    static ECk_SwayGym_RetirementState Get_RetirementState(
        const UCk_SwayGym_SourceReceipt_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Sway")
    static FName Get_ClosureReason(const UCk_SwayGym_SourceReceipt_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Sway")
    static TArray<FCk_Handle> Get_CapturedKnown(const UCk_SwayGym_SourceReceipt_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Sway")
    static FCkSwayGym_SourceRoles Get_CapturedRoles(
        const UCk_SwayGym_SourceReceipt_UE* Receipt);
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Sway")
    static bool Request_Close(UCk_SwayGym_SourceReceipt_UE* Receipt);

private:
    static bool HasAuthorityContext(const UCk_SwayGym_SourceReceipt_UE* Receipt);
    static bool HasExactAllocatedClosure(const UCk_SwayGym_SourceReceipt_UE* Receipt,
        bool bBeforeDestroy);
    static bool HasKnownQueuesDrained(const UCk_SwayGym_SourceReceipt_UE* Receipt);
    static void Observe(UCk_SwayGym_SourceReceipt_UE* Receipt, UWorld* World);
    static void Fail(UCk_SwayGym_SourceReceipt_UE* Receipt, FName Reason);
};
