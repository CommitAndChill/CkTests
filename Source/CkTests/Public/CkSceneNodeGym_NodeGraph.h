#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment_Data.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkSceneNodeGym_NodeGraph.generated.h"

class UCk_SceneNodeGym_EcsRoots_UE;
class UCk_SceneNodeGym_NodeGraphRuntime_UE;
class UCk_SceneNodeGym_Motion_UE;
class UCk_SceneNodeGym_MotionRuntime_UE;
class UCk_SceneNodeGym_ActorLedger_UE;
class UStaticMeshComponent;

// Native-only, source-owned construction record. It is not a visual snapshot,
// a producer-stop receipt, or permission to retire any actor or ECS entity.
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_NodeGraph_UE final : public UObject
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_NodeGraphRuntime_UE;
    friend class UCk_SceneNodeGym_MotionRuntime_UE;
    friend class UCk_SceneNodeGym_RetirementRuntime_UE;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FCk_SceneNodeNodeGraph_TestAccess;
#endif

    struct FSlot
    {
        FCk_Handle Raw;
        FCk_Handle Parent;
        FTransform InitialOffset = FTransform::Identity;
        int32 RootSlot = INDEX_NONE;
        int32 ParentNodeSlot = INDEX_NONE;
        bool bAllocationAttempted = false;
        bool bComposed = false;
    };

    TWeakObjectPtr<UCk_SceneNodeGym_EcsRoots_UE> Roots;
    TWeakObjectPtr<UStaticMeshComponent> Anchor;
    TArray<FSlot> Slots;
    int32 Generation = 0;
    bool bFailed = false;
    bool bUnproven = false;
    UPROPERTY(Transient) TObjectPtr<UCk_SceneNodeGym_Motion_UE> Motion;
    bool bMotionAttempted = false;
};

UCLASS()
class CKTESTS_API UCk_SceneNodeGym_NodeGraphRuntime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_MotionRuntime_UE;
public:
    // One attempt, fixed authored topology. A return value may be a retained
    // failed record; null means no record was created or allocation was opaque.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_NodeGraph_UE* TryBegin(
        UCk_SceneNodeGym_EcsRoots_UE* Roots);

    // Current eleven-node topology, anchor, feature setup and known-queue
    // proof after real propagation. TryBegin passes the frozen authored initial
    // offsets to TryAdd; this getter permits later valid offset updates. It is
    // not a first-dormant-offset, frame-pose, or whole-closure proof.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsConstructionComplete(const UCk_SceneNodeGym_NodeGraph_UE* Graph);

    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsFailed(const UCk_SceneNodeGym_NodeGraph_UE* Graph);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsUnproven(const UCk_SceneNodeGym_NodeGraph_UE* Graph);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int32 Get_ComposedCount(const UCk_SceneNodeGym_NodeGraph_UE* Graph);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool TryGet_CompletedNode(const UCk_SceneNodeGym_NodeGraph_UE* Graph,
        int32 Slot, FCk_Handle_SceneNode& OutNode);
private:
    static bool HasContext(const UCk_SceneNodeGym_NodeGraph_UE* Graph);
    static bool HasCurrentTopology(const UCk_SceneNodeGym_NodeGraph_UE* Graph,
        bool bRequireDrained);
    static bool TryGetMotionContext(const UCk_SceneNodeGym_NodeGraph_UE* Graph,
        bool bRequireDrained, UCk_SceneNodeGym_ActorLedger_UE*& OutLedger,
        FCk_Handle& OutWorldRoot, FCk_Handle (&OutRoots)[6],
        FCk_Handle (&OutNodes)[11]);
    static void Fail(UCk_SceneNodeGym_NodeGraph_UE* Graph, bool bOpaque);
};
