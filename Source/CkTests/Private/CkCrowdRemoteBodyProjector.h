#pragma once

#include "CkCrowd/Debug/CkCrowd_RemoteDebugContract.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkPmg/CkPmg_Utils_BasicShapes.h"

#include <UObject/Object.h>
#include <UObject/WeakObjectPtr.h>

#include "CkCrowdRemoteBodyProjector.generated.h"

class APlayerController;
class UProceduralMeshComponent;
class UWorld;

// One client-world presentation lease. Its caller must retain this UObject with UPROPERTY and
// call RequestRetire on EndPlay; dynamic request delegates hold only a weak UObject target.
UCLASS()
class CKTESTS_API UCkCrowdRemoteBodyProjector : public UObject
{
    GENERATED_BODY()

public:
    auto GetWorld() const -> UWorld* override;

    auto TryBind(UWorld* InWorld, APlayerController* InLocalController) -> bool;
    auto TryApply(int64 InGeneration, int64 InSequence,
        const TArray<FCkCrowdRemoteAgentRow>& InRows) -> bool;
    auto TickProjection() -> void;
    auto RequestRetire() -> void;
    auto IsReadyFor(int64 InGeneration, int64 InSequence) const -> bool;
    auto HasFailed() const -> bool { return _Failed; }
    auto IsRetired() -> bool;

private:
    struct FBody
    {
        FCk_Handle Entity;
        FCk_Handle_Transform Transform;
        FCk_Handle_Pmg_DebugShape Capsule;
        FCk_Handle_Pmg_DebugShape Cone;
        TWeakObjectPtr<UProceduralMeshComponent> CapsuleMesh;
        TWeakObjectPtr<UProceduralMeshComponent> ConeMesh;
        bool bMeshesCaptured = false;
        FCkCrowdRemoteAgentRow Row;
    };
    struct FRetiringTree
    {
        FCk_Handle Root;
        TMap<FCkCrowdRemoteAgentId, FBody> Bodies;
    };

    auto IsClientWorldReady() const -> bool;
    auto HasStageContext(const FCk_Handle& InFrozenRoot,
        const FCkCrowdRemoteAgentId& InId, const FBody& InExpected) const -> bool;
    auto HasQueueBody(const FBody& InExpected, bool bStage) const -> bool;
    auto IsLive(const FCk_Handle& InHandle) const -> bool;
    auto IsComplete(const FCk_Handle& InRoot,
        const TMap<FCkCrowdRemoteAgentId, FBody>& InBodies) const -> bool;
    auto HasExactAllocatedChildren(const FCk_Handle& InParent,
        const TSet<FCk_Handle>& InExpected) const -> bool;
    auto HasNoNewAllocatedChildren(const FCk_Handle& InParent,
        const TSet<FCk_Handle>& InExpected) const -> bool;
    auto HasNoNewAllocatedChildren(const FRetiringTree& InTree) const -> bool;
    auto HideCaptured(const FCk_Handle& InRoot,
        const TMap<FCkCrowdRemoteAgentId, FBody>& InBodies) -> void;
    auto TryStage(const TArray<FCkCrowdRemoteAgentRow>& InRows) -> bool;
    auto TryQueueUpdates(const TArray<FCkCrowdRemoteAgentRow>& InRows) -> bool;
    auto QueueTransform(FBody& InBody, const FTransform& InTransform) -> bool;
    auto QueueColor(FBody& InBody, const FLinearColor& InColor) -> bool;
    auto QueueVisibility(FBody& InBody, bool InVisible) -> bool;
    auto TryExpectOwner(const FCk_Handle& InOwner) -> bool;
    auto QueueReveal() -> bool;
    auto FinishIfComplete() -> void;
    auto RequestRetireRoot(FCk_Handle& InRoot,
        const TMap<FCkCrowdRemoteAgentId, FBody>& InBodies) -> bool;
    auto FailAndRetire() -> void;
    static auto ResolveTint(const FCkCrowdRemoteAgentRow& InRow) -> FLinearColor;

    UFUNCTION()
    void OnVisualRequestCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult);

    TWeakObjectPtr<UWorld> _World;
    TWeakObjectPtr<APlayerController> _LocalController;
    FCk_Handle _Root;
    FCk_Handle _StageRoot;
    TArray<FCk_Handle> _Retiring;
    TArray<FRetiringTree> _RetiringTrees;
    TMap<FCkCrowdRemoteAgentId, FBody> _Bodies;
    TMap<FCkCrowdRemoteAgentId, FBody> _StageBodies;
    TArray<FCkCrowdRemoteAgentRow> _CandidateRows;
    TMap<FCk_Handle, int32> _PendingOwners;
    int64 _Generation = 0;
    int64 _Sequence = 0;
    int64 _CandidateGeneration = 0;
    int64 _CandidateSequence = 0;
    int32 _Outstanding = 0;
    bool _Submitting = false;
    bool _Ready = false;
    bool _Staging = false;
    bool _Revealing = false;
    bool _Failed = false;
    bool _RetireRequested = false;
    bool _AppliedVisible = false;
    bool _CandidateVisible = false;
};
