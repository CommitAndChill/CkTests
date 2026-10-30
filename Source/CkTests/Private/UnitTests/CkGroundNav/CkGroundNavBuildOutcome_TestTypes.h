#pragma once

#include "CkGroundNav/Volume/CkGroundNavVolume_Fragment_Data.h"
#include "CkEcs/Request/CkRequest_Completion.h"

#include <UObject/GarbageCollection.h>

#include "CkGroundNavBuildOutcome_TestTypes.generated.h"

UCLASS()
class UCk_GroundNavBuildOutcomeTest_Listener_UE : public UObject
{
    GENERATED_BODY()

public:
    using FRequestList = TArray<std::variant<FCk_Request_GroundNavVolume_Build>>;

    UFUNCTION()
    void OnGeneric(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++GenericCount;
        if (Events != nullptr)
        { Events->Add(1); }
        if (GenericCount == 1)
        {
            if (RequestToReenter != nullptr)
            { RequestToReenter->TryFireCompletion(InOwner, ECk_Request_OperationResult::Failed_Cancelled); }
            if (QueueToCancel != nullptr)
            { ck::request::FireCancelledForPending(InOwner, *QueueToCancel); }
            if (CollectDuringGeneric)
            { CollectGarbage(RF_NoFlags); }
        }
    }

    UFUNCTION()
    void OnResolved(FCk_Handle_GroundNavVolume InVolume, const FCk_GroundNavVolume_BuildOutcome& InOutcome)
    {
        ++TypedCount;
        LastOutcome = InOutcome;
        LastVolume = InVolume;
        if (Events != nullptr)
        { Events->Add(2); }
    }

    FCk_Request_GroundNavVolume_Build* RequestToReenter = nullptr;
    FRequestList* QueueToCancel = nullptr;
    TArray<int32>* Events = nullptr;
    bool CollectDuringGeneric = false;
    int32 GenericCount = 0;
    int32 TypedCount = 0;
    FCk_GroundNavVolume_BuildOutcome LastOutcome;
    FCk_Handle_GroundNavVolume LastVolume;
};
