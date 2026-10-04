#pragma once

#include "CkOnline/Direct/CkOnlineDirect_Fragment_Data.h"

#include "CkTest_OnlineDirectListener.generated.h"

UCLASS()
class UCk_Test_OnlineDirectListener_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnCompleted(FCk_Handle_OnlineDirect InService, FCk_OnlineDirect_Completion InCompletion)
    {
        Services.Add(InService);
        Completions.Add(InCompletion);
        if (OnCompletion) { OnCompletion(); }
    }

    TArray<FCk_Handle_OnlineDirect> Services;
    TArray<FCk_OnlineDirect_Completion> Completions;
    TFunction<void()> OnCompletion;
};
