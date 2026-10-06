#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment_Data.h"
#include "CkEcsExt/Transform/CkTransform_Fragment_Data.h"
#include "CkTimer/CkTimer_Fragment_Data.h"

#include "CkSceneNode_SourceQueuesProbe.generated.h"

class USceneComponent;

// Test-owned producer. Close is a synchronous source gate, not a timer mutation.
UCLASS()
class UCk_SceneNode_SourceQueuesProbe_UE final : public UObject
{
    GENERATED_BODY()
public:
    FCk_Handle_Transform Root;
    FCk_Handle_SceneNode Node;
    TWeakObjectPtr<USceneComponent> SourceComponent;
    bool bClosed = false;
    int32 UpdateCalls = 0;
    int32 QueuedUpdates = 0;
    int32 ClosedUpdateCalls = 0;
    int32 StopSignalCalls = 0;
    int32 StopReceiptCalls = 0;
    FCk_Handle StopReceiptOwner;
    ECk_Request_OperationResult StopReceipt = ECk_Request_OperationResult::Failed;

    UFUNCTION()
    void OnTimerUpdate(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT);

    UFUNCTION()
    void OnTimerStop(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT);

    UFUNCTION()
    void OnStopCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult);
};
