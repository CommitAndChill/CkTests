#pragma once

#include "CoreMinimal.h"
#include "CkCompassGym_SourceCatalog.h"
#include "CkTimer/CkTimer_Fragment_Data.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment_Data.h"
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

    // Bind to the real timer before journal staging; accepts a composed but
    // not yet POI-exposed Ping and leaves the legacy Arm path unchanged.
    bool ArmJournal(UWorld* World, AActor* ExactGameState,
        const FCk_Handle& SourceRoot,
        const FCkCompassGym_SourceReceipt& StagedPing);
    bool ArmJournalReentrantMarker();

    UFUNCTION() void OnMarkerBeginDestroy(FCk_Handle InHandle);

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
    bool Get_JournalRetired() const { return bJournalRetired; }
    const FString& Get_JournalFailure() const { return JournalFailure; }
    int32 Get_ReentrantCallbackCount() const { return ReentrantCallbackCount; }
    FCk_Handle Get_ReentrantUnknownChild() const { return ReentrantUnknownChild; }

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
    bool bJournalMode = false;
    bool bJournalRetired = false;
    FString JournalFailure;
    FCk_Delegate_OnBeginDestroy MarkerBeginDelegate;
    FCk_Handle ReentrantUnknownChild;
    int32 ReentrantCallbackCount = 0;
    bool bReentrantArmed = false;
};
