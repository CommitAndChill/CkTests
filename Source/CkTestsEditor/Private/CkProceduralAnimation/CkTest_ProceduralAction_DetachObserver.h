#pragma once

#include "CkEcs/Request/CkRequest_Completion.h"

#include "CkTest_ProceduralAction_DetachObserver.generated.h"

UCLASS()
class UCk_Test_ProceduralAction_DetachObserver_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnDetached(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult);

    FCk_Handle Subject;
    FCk_Handle Leg;
    FCk_Handle Segment;
    int32 Calls = 0;
    bool Succeeded = false;
    bool LegDetached = false;
    bool SegmentStillLive = false;
    bool HealthOperational = false;
    bool HadSettledRevision = false;
    bool ExposedStalePose = false;
};
