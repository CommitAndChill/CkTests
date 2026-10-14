#pragma once

#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"

#include "CkTest_ProceduralActionReentry.generated.h"

UCLASS()
class UCk_Test_ProceduralActionReentry_UE : public UObject
{
    GENERATED_BODY()

public:
    FCk_Handle Scope;
    FCk_Handle_ProceduralActionPlayer Player;
    FCk_Handle_ProceduralAction Created;
    FCk_Handle DestroyScope;
    FCk_Handle DestroyAtFinishedScope;
    int32 FinishedStateDestroyCount = 0;
    int32 DestroyingCompletionCount = 0;
    ECk_Request_OperationResult DestroyingResult = ECk_Request_OperationResult::Succeeded;
    int32 CompletionCount = 0;
    ECk_Request_OperationResult LastResult = ECk_Request_OperationResult::Failed;
    int32 InvalidCompletionCount = 0;
    ECk_Request_OperationResult InvalidResult = ECk_Request_OperationResult::Succeeded;
    int32 StateChangeCount = 0;
    int32 FinishedCount = 0;
    FCk_ProceduralAction_Terminal LastTerminal;
    int32 TuningCompletionCount = 0;
    ECk_Request_OperationResult TuningResult = ECk_Request_OperationResult::Failed;

    UFUNCTION()
    void OnTuningCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++TuningCompletionCount;
        TuningResult = InResult;
    }

    UFUNCTION()
    void OnStateChanged(FCk_Handle_ProceduralAction InAction, ECk_ProceduralAction_State InState)
    {
        ++StateChangeCount;
        if (InState == ECk_ProceduralAction_State::Finished
            && ck::IsValid(DestroyAtFinishedScope))
        {
            ++FinishedStateDestroyCount;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(DestroyAtFinishedScope);
        }
    }

    UFUNCTION()
    void OnFinished(FCk_Handle_ProceduralAction InAction, FCk_ProceduralAction_Terminal InTerminal)
    {
        ++FinishedCount;
        LastTerminal = InTerminal;
    }

    UFUNCTION()
    void OnInvalidCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++InvalidCompletionCount;
        InvalidResult = InResult;
    }

    UFUNCTION()
    void OnRequestCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++CompletionCount;
        LastResult = InResult;
        if (CompletionCount == 1)
        {
            Created = UCk_Utils_ProceduralAction_UE::Create_Manual(
                Scope, Player, FCk_ProceduralAction_ControlSpec{TArray<FName>{FName{TEXT("ManualArm")}}});
        }
    }

    UFUNCTION()
    void OnDestroyingSubmitCompleted(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        ++DestroyingCompletionCount;
        DestroyingResult = InResult;
        if (DestroyingCompletionCount == 1)
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(DestroyScope); }
    }
};
