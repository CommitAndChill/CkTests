#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkTimer/CkTimer_Fragment_Data.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_StopReceipt.generated.h"

class UCk_SceneNodeGym_ActorLedger_UE;
class UCk_SceneNodeGym_StopRuntime_UE;

// Native-only completion sink. The source receives the receipt, never a sink.
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_StopReceiver_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_StopRuntime_UE;

    UPROPERTY(Transient) TObjectPtr<class UCk_SceneNodeGym_StopReceipt_UE> Receipt;
    int32 Slot = INDEX_NONE;

    UFUNCTION()
    void OnCompleted(FCk_Handle Owner, ECk_Request_OperationResult Result);
};

// An opaque observation of one attempted Stop batch, not lifetime permission.
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_StopReceipt_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_StopRuntime_UE;
    friend class UCk_SceneNodeGym_StopReceiver_UE;

    struct FSlot
    {
        FCk_Handle_Timer Timer;
        FCk_Handle_Timer Returned;
        bool bIssued = false;
        bool bReturned = false;
        bool bCallback = false;
        bool bSucceeded = false;
    };

    UPROPERTY(Transient) TArray<TObjectPtr<UCk_SceneNodeGym_StopReceiver_UE>> Receivers;
    TArray<FSlot> Slots;
    TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
    FCk_Handle WorldRoot;
    int32 Generation = 0;
    bool bFailed = false;
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_StopRuntime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_StopReceiver_UE;

public:
    // The supplied one-to-six timers are frozen, but this does not establish
    // that they are all source timers or authorize any entity/actor retirement.
    // The source must synchronously close all producer callbacks before this
    // call; no concurrent ECS mutation is allowed, and callers must observe
    // queue drainage at a later real post-ECS boundary, waiting for all
    // completion callbacks. A timer may have no queued requests or exactly
    // one queued Reset compatible with zero-duration ResetOnDone. The Reset's
    // origin is not proven; any other queued request shape rejects the whole
    // batch before Stop. This must start before source/ledger EndPlay.
    // Non-null means a retained attempted batch, possibly already Failed;
    // it does not mean Stop succeeded. No ownership, full closure, stable-pose,
    // or Destroy permission is granted here.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_StopReceipt_UE* TryStop_KnownTimers(
        UCk_SceneNodeGym_ActorLedger_UE* Ledger,
        const TArray<FCk_Handle_Timer>& Timers,
        const FCk_Handle& WorldRoot);

    // Completion is a current Paused/no-queue proof. A subsequent Resume
    // makes it false; it is not a historical guarantee of no reactivation.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsComplete(const UCk_SceneNodeGym_StopReceipt_UE* Receipt);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsFailed(const UCk_SceneNodeGym_StopReceipt_UE* Receipt);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int32 Get_ExpectedCount(const UCk_SceneNodeGym_StopReceipt_UE* Receipt);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int32 Get_SucceededCount(const UCk_SceneNodeGym_StopReceipt_UE* Receipt);
    // Both count getters are diagnostics only, never retirement permission.

private:
    static bool HasClosedContext(const UCk_SceneNodeGym_ActorLedger_UE* Ledger,
        const FCk_Handle& WorldRoot);
    static bool HasExactReceipt(const UCk_SceneNodeGym_StopReceipt_UE* Receipt);
};
