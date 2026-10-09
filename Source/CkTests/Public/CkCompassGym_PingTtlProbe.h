#pragma once

#include "CoreMinimal.h"
#include "CkCompassGym_SourceCatalog.h"
#include "CkTimer/CkTimer_Fragment_Data.h"
#include "UObject/Object.h"
#include "CkCompassGym_PingTtlProbe.generated.h"

// Reflected receiver for the focused real StopOnDone fixture only.
UCLASS(NotBlueprintable)
class CKTESTS_API UCkCompassGym_PingTtlProbe final : public UObject
{
    GENERATED_BODY()

public:
    bool Arm(UWorld* World, AActor* ExactGameState,
        const FCk_Handle& SourceRoot,
        const TArray<FCkCompassGym_SourceReceipt>& ActiveTen);

    UFUNCTION() void OnDone(FCk_Handle_Timer InTimer,
        FCk_Chrono InChrono, FCk_Time InDeltaT);

    int32 Get_DoneCount() const { return DoneCount; }
    bool Get_Failed() const { return bFailed; }
    bool Get_FrozeBeforeDestroy() const { return bFrozeBeforeDestroy; }
    bool Get_RequestedDestroy() const { return bRequestedDestroy; }
    bool Get_ExactWorldCallback() const { return bExactWorldCallback; }
    bool Get_HostLiveBeforeDestroy() const { return bHostLiveBeforeDestroy; }
    double Get_DoneElapsedSeconds() const { return DoneElapsedSeconds; }
    FCkCompassGym_SourceReceipt Get_FrozenReceipt() const { return FrozenReceipt; }

private:
    TWeakObjectPtr<UWorld> ExactWorld;
    TWeakObjectPtr<AActor> ExactGameState;
    FCk_Handle WorldRoot;
    FCk_Handle SourceRoot;
    FCkCompassGym_SourceReceipt AdmittedReceipt;
    FCkCompassGym_SourceReceipt FrozenReceipt;
    int32 DoneCount = 0;
    double DoneElapsedSeconds = 0.0;
    bool bArmed = false;
    bool bFailed = false;
    bool bFrozeBeforeDestroy = false;
    bool bRequestedDestroy = false;
    bool bExactWorldCallback = false;
    bool bHostLiveBeforeDestroy = false;
};
