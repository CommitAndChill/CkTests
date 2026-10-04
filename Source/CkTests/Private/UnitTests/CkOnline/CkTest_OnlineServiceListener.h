#pragma once

#include "CkOnline/CkOnline_Fragment_Data.h"
#include "CkEcs/Request/CkRequest_Completion.h"

#include "CkTest_OnlineServiceListener.generated.h"

// A dynamic delegate requires a reflected receiver. Retain every observation so a
// duplicate, reordered, or cross-context delivery remains visible to the tests.
UCLASS()
class UCk_Test_OnlineServiceListener_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnCompleted(FCk_Handle_OnlineService InService, FCk_OnlineService_Completion InCompletion)
    {
        Services.Add(InService);
        Completions.Add(InCompletion);
        if (OnCompletion)
        { OnCompletion(); }
    }

    UFUNCTION()
    void OnRequestCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        ++RequestCompletionCount;
        LastRequestResult = InResult;
        RequestResults.Add(InResult);
        if (OnRequestCompletion)
        { OnRequestCompletion(); }
    }

    TArray<FCk_Handle_OnlineService> Services;
    TArray<FCk_OnlineService_Completion> Completions;
    TFunction<void()> OnCompletion;
    TFunction<void()> OnRequestCompletion;
    int32 RequestCompletionCount = 0;
    ECk_Request_OperationResult LastRequestResult = ECk_Request_OperationResult::Failed;
    TArray<ECk_Request_OperationResult> RequestResults;
};
