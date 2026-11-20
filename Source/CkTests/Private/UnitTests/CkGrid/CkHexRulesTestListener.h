#pragma once

#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkHexRulesTestListener.generated.h"

UCLASS()
class UCk_HexRulesTestListener : public UObject
{
    GENERATED_BODY()
public:
    TFunction<void(FCk_Handle, ECk_Request_OperationResult)> Observe;

    UFUNCTION()
    void OnCompleted(FCk_Handle InOwner, ECk_Request_OperationResult InResult)
    {
        if (Observe) { Observe(InOwner, InResult); }
    }
};
