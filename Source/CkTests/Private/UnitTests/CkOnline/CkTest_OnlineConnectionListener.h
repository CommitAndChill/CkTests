#pragma once

#include "CkOnline/Connection/CkOnlineConnection_Fragment_Data.h"

#include "CkTest_OnlineConnectionListener.generated.h"

UCLASS()
class UCk_Test_OnlineConnectionListener_UE : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION()
    void OnCompleted(FCk_Handle_OnlineConnection InService, FCk_OnlineConnection_Completion InCompletion)
    {
        Services.Add(InService);
        Completions.Add(InCompletion);
    }

    TArray<FCk_Handle_OnlineConnection> Services;
    TArray<FCk_OnlineConnection_Completion> Completions;
};
