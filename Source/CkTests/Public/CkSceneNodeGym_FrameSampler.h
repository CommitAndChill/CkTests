#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_FrameSampler.generated.h"

class AGameStateBase;
class UCk_SceneNodeGym_ActorLedger_UE;
class UCk_SceneNodeGym_Motion_UE;
class UWorld;

// One actual authority-world observation. WorldPoses contains roles 1..17 in
// order. It is a value sample, not a wire message or retirement permission.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkSceneNodeGym_Frame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 Generation = 0;
    UPROPERTY(BlueprintReadOnly) int64 FrameSerial = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FTransform> WorldPoses;
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_FrameSampler_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_FrameSamplerRuntime_UE;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FCk_SceneNodeFrameSampler_TestAccess;
#endif

    TWeakObjectPtr<UCk_SceneNodeGym_Motion_UE> Motion;
    TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
    TWeakObjectPtr<AGameStateBase> Source;
    TWeakObjectPtr<UWorld> World;
    int32 Generation = 0;
    FCkSceneNodeGym_Frame Latest;
    uint64 PublishedEngineFrame = 0;
    FDelegateHandle PostTickHandle;
    FDelegateHandle WorldCleanupHandle;
    bool bClosed = true;
    bool bInSample = false;
    bool bHasFrame = false;

    UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded,
        bool bCleanupResources);
    void OnWorldPostActorTick(UWorld* InWorld, ELevelTick TickType,
        float DeltaSeconds);
    void CloseAndUnbind();
    virtual void BeginDestroy() override;
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_FrameSamplerRuntime_UE final
    : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_FrameSampler_UE;
public:
    // One source-owned attempt; a non-null receipt may already be closed.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_FrameSampler_UE* TryBegin(
        UCk_SceneNodeGym_Motion_UE* Motion);

    // Clears OutFrame first. At most one GFrameCounter old; no stale fallback.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool TryCopyLatestFrame(
        const UCk_SceneNodeGym_FrameSampler_UE* Sampler,
        FCkSceneNodeGym_Frame& OutFrame);

    // Closes/unbinds synchronously, then attempts Motion producer Stop.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool Request_Close(UCk_SceneNodeGym_FrameSampler_UE* Sampler);
private:
    static bool HasCurrentContext(
        const UCk_SceneNodeGym_FrameSampler_UE* Sampler);
    static void Sample(UCk_SceneNodeGym_FrameSampler_UE* Sampler,
        UWorld* InWorld);
    static void Fail(UCk_SceneNodeGym_FrameSampler_UE* Sampler);
};
