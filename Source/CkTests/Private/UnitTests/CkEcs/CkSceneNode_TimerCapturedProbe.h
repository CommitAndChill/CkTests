#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkTimer/CkTimer_Fragment_Data.h"

#include "CkSceneNode_TimerCapturedProbe.generated.h"

// Test-owned source callback. Closing it gates production writes while a final
// Stop-driven Update remains observable.
UCLASS()
class UCk_SceneNode_TimerCapturedProbe_UE final : public UObject
{
    GENERATED_BODY()
public:
    TArray<FCk_Handle_Timer> Timers;
    TArray<int32> Updates;
    TArray<int32> ClosedUpdates;
    TArray<int32> ProducerWrites;
    TArray<int32> Stops;
    TArray<int32> ResumeCallbacks;
    TArray<int32> ResumeSucceeded;
    bool bClosed = false;

    UFUNCTION()
    void OnUpdate(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Delta);
    UFUNCTION()
    void OnStop(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Delta);
    UFUNCTION()
    void OnResumeCompleted(FCk_Handle Timer, ECk_Request_OperationResult Result);

    bool AllUpdatedAtLeast(int32 Count) const;
    bool AllResumeSucceededOnce() const;
    bool AllStoppedOnce() const;
    int32 TotalWrites() const;
    int32 TotalClosedUpdates() const;
};
