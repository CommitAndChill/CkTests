#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment_Data.h"
#include "CkTimer/CkTimer_Fragment_Data.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_Motion.generated.h"

class UCk_SceneNodeGym_ActorLedger_UE;
class UCk_SceneNodeGym_NodeGraph_UE;
class UCk_SceneNodeGym_StopReceipt_UE;
class UCk_SceneNodeGym_MotionRuntime_UE;
class UCk_SceneNodeGym_FrameSampler_UE;
class UCk_SceneNodeGym_FrameSamplerRuntime_UE;

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_MotionReceiver_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_MotionRuntime_UE;

    UPROPERTY(Transient) TObjectPtr<class UCk_SceneNodeGym_Motion_UE> Receipt;
    int32 Slot = INDEX_NONE;

    UFUNCTION() void OnUpdate(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Delta);
    UFUNCTION() void OnResume(FCk_Handle Owner, ECk_Request_OperationResult Result);
};

// Source-owned attempted producer batch. It grants no visual publication,
// complete-closure, or entity/actor retirement permission.
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_Motion_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_MotionRuntime_UE;
    friend class UCk_SceneNodeGym_MotionReceiver_UE;
    friend class UCk_SceneNodeGym_FrameSamplerRuntime_UE;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FCk_SceneNodeMotion_TestAccess;
#endif

    struct FSlot
    {
        FCk_Handle Root;
        FCk_Handle Raw;
        FCk_Handle_Timer Timer;
        FCk_Handle_Timer ResumeReturn;
        float Elapsed = 0.0f;
        int32 IssuedMotionUpdateCount = 0;
        bool bAllocationAttempted = false;
        bool bBound = false;
        bool bResumeIssued = false;
        bool bResumeReturned = false;
        bool bResumeCallback = false;
        bool bResumeSucceeded = false;
    };

    TWeakObjectPtr<UCk_SceneNodeGym_NodeGraph_UE> Graph;
    TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
    FCk_Handle WorldRoot;
    int32 Generation = 0;
    TArray<FCk_Handle> Nodes;
    TArray<FSlot> Slots;
    UPROPERTY(Transient) TArray<TObjectPtr<UCk_SceneNodeGym_MotionReceiver_UE>> Receivers;
    TArray<FCk_Delegate_Timer> UpdateDelegates;
    TArray<FCk_Delegate_Request_OnCompleted> ResumeDelegates;
    UPROPERTY(Transient) TObjectPtr<UCk_SceneNodeGym_StopReceipt_UE> StopReceipt;
    UPROPERTY(Transient) TObjectPtr<UCk_SceneNodeGym_FrameSampler_UE> FrameSampler;
    bool bFrameSamplerAttempted = false;
    bool bActivationAttempted = false;
    bool bEverActive = false;
    bool bClosed = false;
    bool bFailed = false;
    bool bUnproven = false; // opaque construction/identity failure, never cleared
    bool bStopUnproven = false; // current Stop admission cannot be proved yet
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_MotionRuntime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_MotionReceiver_UE;
public:
    // One retained attempt. A non-null result may already be Failed/Unproven.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_Motion_UE* TryBeginDormant(UCk_SceneNodeGym_NodeGraph_UE* Graph);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsDormantReady(const UCk_SceneNodeGym_Motion_UE* Motion);

    // True means all six Resume requests were issued, not that they completed.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool TryActivateOnce(UCk_SceneNodeGym_Motion_UE* Motion);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsActive(const UCk_SceneNodeGym_Motion_UE* Motion);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsFailed(const UCk_SceneNodeGym_Motion_UE* Motion);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    // Construction uncertainty is permanent; Stop-admission uncertainty
    // remains until an actual Stop receipt is retained.
    static bool Get_IsUnproven(const UCk_SceneNodeGym_Motion_UE* Motion);

    // Closes producer callbacks synchronously. A true result only proves that
    // gate closure was attempted. If no Stop receipt was retained, retrying
    // after queued Resume/setup requests drain is permitted; no allocation
    // or Resume is retried. Use Get_IsStopped for actual six-stop proof.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool TryClose(UCk_SceneNodeGym_Motion_UE* Motion);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsStopped(const UCk_SceneNodeGym_Motion_UE* Motion);
private:
    static bool HasContext(const UCk_SceneNodeGym_Motion_UE* Motion,
        bool bRequireDrained);
    static bool HasExactTimer(const UCk_SceneNodeGym_Motion_UE* Motion, int32 Slot,
        bool bRequirePaused);
    static void Fail(UCk_SceneNodeGym_Motion_UE* Motion, bool bOpaque);
    static void HandleUpdate(UCk_SceneNodeGym_Motion_UE* Motion, int32 Slot,
        FCk_Handle_Timer Timer, FCk_Time Delta);
};
