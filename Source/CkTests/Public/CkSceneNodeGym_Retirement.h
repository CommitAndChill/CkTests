#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkCore/Time/CkTime.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_Retirement.generated.h"

class AGameStateBase;
class UCk_SceneNodeGym_Motion_UE;
class UCk_SceneNodeGym_ActorLedger_UE;
class UCk_SceneNodeGym_FrameSampler_UE;
class UWorld;

UENUM(BlueprintType)
enum class ECk_SceneNodeGym_RetirementState : uint8
{
    Pending,
    Complete,
    Unproven
};

// A single normal, fully constructed source generation. This receipt does not
// release the SourceSerial claim or permit a new generation.
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_Retirement_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_RetirementRuntime_UE;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FCk_SceneNodeRetirement_TestAccess;
#endif

    TWeakObjectPtr<UCk_SceneNodeGym_Motion_UE> Motion;
    TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
    TWeakObjectPtr<UCk_SceneNodeGym_FrameSampler_UE> Sampler;
    TWeakObjectPtr<AGameStateBase> Source;
    TWeakObjectPtr<UWorld> World;
    TArray<FCk_Handle> Known;
    TArray<FCk_Handle> ExpectedParents;
    TArray<TWeakObjectPtr<AActor>> Actors;
    TArray<bool> DestroyRequested;
    FCk_Handle WorldRoot;
    int32 Generation = 0;
    FCk_Time Elapsed = FCk_Time{0.0};
    double StartSeconds = 0.0;
    ECk_SceneNodeGym_RetirementState State = ECk_SceneNodeGym_RetirementState::Pending;
    FName FailureReason = NAME_None;
    FDelegateHandle PostTickHandle;
    FDelegateHandle WorldCleanupHandle;
    bool bDestroyIssued = false;
    bool bQuiescenceObserved = false;
    bool bInObserve = false;

    UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);
    void OnWorldPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);
    void Unbind();
    virtual void BeginDestroy() override;
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_RetirementRuntime_UE final
    : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_Retirement_UE;

public:
    // One retained attempt on Motion. A non-null receipt can be Unproven.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_Retirement_UE* TryBeginCompleted(
        UCk_SceneNodeGym_Motion_UE* Motion);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static ECk_SceneNodeGym_RetirementState Get_State(
        const UCk_SceneNodeGym_Retirement_UE* Receipt);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static FName Get_FailureReason(const UCk_SceneNodeGym_Retirement_UE* Receipt);

private:
    static void Fail(UCk_SceneNodeGym_Retirement_UE* Receipt, FName Reason);
    static bool FreezeCompleted(UCk_SceneNodeGym_Retirement_UE* Receipt);
    static bool HasClosedContext(const UCk_SceneNodeGym_Retirement_UE* Receipt);
    static bool HasExactAllocatedClosure(const UCk_SceneNodeGym_Retirement_UE* Receipt,
        bool bBeforeDestroy);
    static bool HasKnownActorInWorld(const UCk_SceneNodeGym_Retirement_UE* Receipt,
        int32 Role, AActor*& OutActor);
    static bool HasNoUnknownScopedActors(const UCk_SceneNodeGym_Retirement_UE* Receipt);
    static bool HasSafeSweepContext(const UCk_SceneNodeGym_Retirement_UE* Receipt);
    static void Observe(UCk_SceneNodeGym_Retirement_UE* Receipt, float DeltaSeconds);
};
