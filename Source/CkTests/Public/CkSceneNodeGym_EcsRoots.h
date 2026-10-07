#pragma once

#include "CoreMinimal.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_EcsRoots.generated.h"

class UCk_SceneNodeGym_ActorLedger_UE;
class UCk_SceneNodeGym_EcsRootsRuntime_UE;
class UCk_SceneNodeGym_NodeGraph_UE;
class UCk_SceneNodeGym_NodeGraphRuntime_UE;

// This is the only SceneNode source script class admitted by this recorder.
// Unlike generic WithActor, its CDO is nonreplicated before spawn injection.
UCLASS(Blueprintable, meta=(CkSpawnParams="FCk_EntityScript_WithActor_SpawnParams"))
class CKTESTS_API UCk_SceneNodeGym_WithActorScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()
public:
    ECk_Replication Get_EffectiveReplication() const override final
    { return ECk_Replication::DoesNotReplicate; }
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_EcsRootReceiver_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_EcsRootsRuntime_UE;
    UPROPERTY(Transient) TObjectPtr<class UCk_SceneNodeGym_EcsRoots_UE> Receipt;
    int32 Slot = INDEX_NONE;
    UFUNCTION() void OnCompleted(FCk_Handle Target, ECk_Request_OperationResult Result);
};

// Opaque construction observation only. It cannot authorize publication,
// whole-graph proof, actor/entity retirement, or retry of a partial begin.
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_EcsRoots_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_EcsRootsRuntime_UE;
    friend class UCk_SceneNodeGym_NodeGraphRuntime_UE;
    friend class UCk_SceneNodeGym_RetirementRuntime_UE;
    friend class UCk_SceneNodeGym_EcsRootReceiver_UE;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FCk_SceneNodeEcsRoots_TestAccess;
#endif

    struct FSlot
    {
        FCk_Handle Target;
        FCk_Handle Carrier;
        FCk_Handle_PendingEntityScript Pending;
        bool bAllocationAttempted = false;
        bool bIssued = false;
        bool bReturned = false;
        bool bCallback = false;
        bool bSucceeded = false;
    };

    TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
    TWeakObjectPtr<UCk_SceneNodeGym_WithActorScript_UE> ClassCDO;
    FCk_Handle WorldRoot;
    FCk_Handle GenerationOwner;
    TArray<FSlot> Slots;
    UPROPERTY(Transient) TArray<TObjectPtr<UCk_SceneNodeGym_EcsRootReceiver_UE>> Receivers;
    int32 Generation = 0;
    bool bFailed = false;
    bool bUnproven = false;
    UPROPERTY(Transient) TObjectPtr<UCk_SceneNodeGym_NodeGraph_UE> NodeGraph;
    bool bNodeGraphAttempted = false;
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_EcsRootsRuntime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_EcsRootReceiver_UE;
    friend class UCk_SceneNodeGym_NodeGraphRuntime_UE;
public:
    // Caller supplies a pre-materialized native source-only subclass. This
    // claims one attempt before any ECS allocation; a failed attempt is final.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_EcsRoots_UE* TryBegin(
        UCk_SceneNodeGym_ActorLedger_UE* Ledger,
        TSubclassOf<UCk_SceneNodeGym_WithActorScript_UE> ScriptClass);

    // Six request-drain receipts and current root/actor binding. Not a
    // construction-finished, geometry, complete-closure, or readiness proof.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsConstructionComplete(const UCk_SceneNodeGym_EcsRoots_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsFailed(const UCk_SceneNodeGym_EcsRoots_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsUnproven(const UCk_SceneNodeGym_EcsRoots_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int32 Get_SucceededCount(const UCk_SceneNodeGym_EcsRoots_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool TryGet_CompletedRoot(const UCk_SceneNodeGym_EcsRoots_UE* Receipt,
        int32 Slot, FCk_Handle& OutRoot);
private:
    static bool HasContext(const UCk_SceneNodeGym_ActorLedger_UE* Ledger);
    static bool HasSafeWorldRoot(const UCk_SceneNodeGym_ActorLedger_UE* Ledger,
        const FCk_Handle& WorldRoot);
    static bool HasExactReceipt(const UCk_SceneNodeGym_EcsRoots_UE* Receipt);
    static void Fail(UCk_SceneNodeGym_EcsRoots_UE* Receipt, bool bOpaque);
};
