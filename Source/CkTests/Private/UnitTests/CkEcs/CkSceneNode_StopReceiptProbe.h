#pragma once

#include "CoreMinimal.h"
#include "CkTimer/CkTimer_Fragment_Data.h"

#include "CkSceneNode_StopReceiptProbe.generated.h"

// Test-owned producer callback. Closing this object is synchronous and never
// mutates a Timer; a final Stop-driven OnUpdate is still observable.
UCLASS()
class UCk_SceneNode_StopReceiptProbe_UE final : public UObject
{
    GENERATED_BODY()
public:
    TArray<FCk_Handle_Timer> Timers;
    TArray<int32> UpdateCalls;
    TArray<int32> ClosedUpdates;
    TArray<int32> ProducerWrites;
    TArray<int32> StopSignals;
    bool bClosed = false;

    UFUNCTION()
    void OnTimerUpdate(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT);

    UFUNCTION()
    void OnTimerStop(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT);

    int32 Get_MinUpdateCalls() const;
    int32 Get_TotalProducerWrites() const;
    int32 Get_MinClosedUpdates() const;
    int32 Get_TotalStopSignals() const;
    bool Get_ExactlyOneStopSignalPerTimer() const;
};
