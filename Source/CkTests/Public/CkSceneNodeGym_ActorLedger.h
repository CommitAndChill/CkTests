#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_ActorLedger.generated.h"

class UCk_SceneNodeGym_StopReceipt_UE;
class UCk_SceneNodeGym_StopRuntime_UE;
class UCk_SceneNodeGym_EcsRoots_UE;
class UCk_SceneNodeGym_EcsRootsRuntime_UE;

class AGameStateBase;
class UWorld;

// Retained on the source GameState as an instance component. All role evidence
// is native-private; neither Blueprint nor AngelScript can author a receipt.
UCLASS(NotBlueprintable)
class CKTESTS_API UCk_SceneNodeGym_ActorLedger_UE final : public UActorComponent
{
    GENERATED_BODY()

    friend class UCk_SceneNodeGym_ActorLedgerRuntime_UE;
    friend class UCk_SceneNodeGym_StopRuntime_UE;
    friend class UCk_SceneNodeGym_EcsRootsRuntime_UE;
    friend class ACk_SceneNodeGym_SourceActor_UE;

private:
    struct FRole
    {
        TWeakObjectPtr<AActor> Actor;
        FTransform SpawnPose = FTransform::Identity;
        bool bCaptured = false;
        bool bFinishing = false;
        bool bFinishNotified = false;
        bool bFinished = false;
        bool bDestroyedObserved = false;
        bool bRemovedObserved = false;
    };

    TWeakObjectPtr<AGameStateBase> Source;
    TWeakObjectPtr<UWorld> SourceWorld;
    UPROPERTY(Transient) TArray<TSubclassOf<AActor>> VisualClasses;
    UPROPERTY(Transient) TObjectPtr<UCk_SceneNodeGym_StopReceipt_UE> StopReceipt;
    UPROPERTY(Transient) TObjectPtr<UCk_SceneNodeGym_EcsRoots_UE> EcsRoots;
    bool bEcsRootsAttempted = false;
    TArray<FRole> Roles;
    FDelegateHandle DestroyedHandle;
    FDelegateHandle RemovedHandle;
    FDelegateHandle SpawnedHandle;
    int32 Generation = 0;
    int32 ActiveRole = -1;
    bool bClosed = false;
    bool bOpaqueAllocation = false;
    bool bExternalLoss = false;
    bool bUnknownOwnedActor = false;
    bool bWorldTeardown = false;
    FName FirstClosureReason = NAME_None;

public:
    UCk_SceneNodeGym_ActorLedger_UE();

private:
    void OnActorDestroyed(AActor* Actor);
    void OnActorRemovedFromWorld(AActor* Actor);
    void OnActorSpawned(AActor* Actor);
    void Close(FName Reason);
    void UnbindWorldCallbacks();
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginDestroy() override;
};

// A future authored parent/child cube derives from this base. The native claim
// is set before FinishSpawning, so BeginPlay can read it; scripts cannot write it.
UCLASS(Blueprintable)
class CKTESTS_API ACk_SceneNodeGym_SourceActor_UE : public AActor
{
    GENERATED_BODY()

    friend class UCk_SceneNodeGym_ActorLedgerRuntime_UE;

private:
    TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
    int32 Role = -1;
    int32 Generation = 0;

public:
    ACk_SceneNodeGym_SourceActor_UE();

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    UCk_SceneNodeGym_ActorLedger_UE* Get_ActorLedger() const;

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    int32 Get_ActorRole() const { return IsInGameThread() ? Role : -1; }

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    int32 Get_ActorGeneration() const { return IsInGameThread() ? Generation : 0; }
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_ActorLedgerRuntime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

    friend class UCk_SceneNodeGym_ActorLedger_UE;
    friend class UCk_SceneNodeGym_StopRuntime_UE;
    friend class UCk_SceneNodeGym_EcsRootsRuntime_UE;

public:
    // VisualClasses fixes roles 1..17; role 0 is the plain nonvisual AActor scope.
    // The source retains the returned component even if the caller drops it.
    // A private weak Source serial claim prevents a second begin even if that
    // component is removed; this slice has no re-claim or release operation.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_ActorLedger_UE* TryBeginActorGeneration(
        AGameStateBase* Source, int32 Generation,
        const TArray<TSubclassOf<AActor>>& VisualClasses);

    // One atomic native operation: capture a raw AActor identity before level
    // insertion, configure the native claim/nonreplication, then FinishSpawning.
    // No unfinished actor escapes this call. False clears OutActor, retaining
    // every captured weak serial inside the closed ledger.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool TrySpawnConfiguredRole(
        UCk_SceneNodeGym_ActorLedger_UE* Ledger, int32 Role,
        const FTransform& SpawnPose, AActor*& OutActor);

    // Cheap source and captured-role admission projection; no world-wide census.
    // A false result does not mutate the ledger. Source must Request_Close.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsOpen(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int32 Get_Generation(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);

    // Count only; raw unfinished actors are never exposed to caller code.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int32 Get_CapturedCount(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);

    // First native closure site; NAME_None means no closure was latched.
    // This is diagnostic evidence, never cleanup or retirement permission.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static FName Get_ClosureReason(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static AGameStateBase* TryGet_Source(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);

    // Completed roles only, including still-live exact recorded roles after a
    // closed failure. This neither reopens admission nor permits destruction.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool TryGet_FinishedRole(const UCk_SceneNodeGym_ActorLedger_UE* Ledger,
        int32 Role, AActor*& OutActor);

    // Point-in-time 18-actor identity/owner census at a post-ECS game-thread
    // boundary outside actor callbacks or reentry. It does not prove visual
    // components, ECS children, geometry, producer stop, retirement safety,
    // or historical absence. No adoption, mutation, or destruction permission.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool TryProve_CurrentCohort(
        const UCk_SceneNodeGym_ActorLedger_UE* Ledger, bool bRequireComplete);

    // Explicit producer gate closure. No actor destruction or cleanup.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool Request_Close(UCk_SceneNodeGym_ActorLedger_UE* Ledger);

private:
    static bool HasLiveSourceContext(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);
    static bool HasCurrentSource(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);
    static bool HasExactCapturedRoles(const UCk_SceneNodeGym_ActorLedger_UE* Ledger,
        bool bRequireComplete);
    static bool IsSafePose(const FTransform& Pose);
    static bool IsKnownIdentity(const UCk_SceneNodeGym_ActorLedger_UE* Ledger,
        const AActor* Actor, int32& OutRole);
};
