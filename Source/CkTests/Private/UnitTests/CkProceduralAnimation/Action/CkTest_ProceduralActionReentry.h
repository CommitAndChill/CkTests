#pragma once

#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkCore/Validation/CkIsValid.h"

#include "CkTest_ProceduralActionReentry.generated.h"

UCLASS()
class UCk_Test_ProceduralActionReentry_UE : public UObject
{
    GENERATED_BODY()

public:
    int32 StateChangeCount = 0;
    int32 FinishedCount = 0;
    uint64 FinishedFrame = 0;
    FCk_ProceduralAction_Terminal LastTerminal;
    int32 InvalidCompletionCount = 0;
    ECk_Request_OperationResult InvalidResult = ECk_Request_OperationResult::Succeeded;
    int32 ConsumeCompletionCount = 0;
    ECk_Request_OperationResult ConsumeResult = ECk_Request_OperationResult::Failed;

    UFUNCTION()
    void OnStateChanged(FCk_Handle_ProceduralAction InAction, ECk_ProceduralAction_State InState)
    {
        ++StateChangeCount;
    }

    UFUNCTION()
    void OnFinished(FCk_Handle_ProceduralAction InAction, FCk_ProceduralAction_Terminal InTerminal)
    {
        ++FinishedCount;
        FinishedFrame = GFrameCounter;
        LastTerminal = InTerminal;
    }

    UFUNCTION()
    void OnInvalidCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++InvalidCompletionCount;
        InvalidResult = InResult;
    }

    UFUNCTION()
    void OnConsumeCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++ConsumeCompletionCount;
        ConsumeResult = InResult;
    }
};
