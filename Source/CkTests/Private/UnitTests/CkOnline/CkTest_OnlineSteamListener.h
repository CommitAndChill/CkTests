#pragma once

#include "CkOnline/Steam/CkOnlineSteam_Fragment_Data.h"

#include "CkTest_OnlineSteamListener.generated.h"

UCLASS()
class UCk_Test_OnlineSteamListener_UE : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION()
    void OnCompleted(FCk_Handle_OnlineSteam InService, FCk_OnlineSteam_Completion InCompletion)
    {
        Services.Add(InService);
        Completions.Add(InCompletion);
        if (OnCompletion) { OnCompletion(); }
    }

    TArray<FCk_Handle_OnlineSteam> Services;
    TArray<FCk_OnlineSteam_Completion> Completions;
    TFunction<void()> OnCompletion;
};
