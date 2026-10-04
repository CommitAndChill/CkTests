#pragma once

#include "CoreMinimal.h"
#include "CkOnline/CkOnline_Fragment_Data.h"
#include "CkEcs/Request/CkRequest_Completion.h"

#include <Subsystems/GameInstanceSubsystem.h>

#include "CkOnlineTest_Subsystem.generated.h"

class FCkOnlineTest_FakeAdapter;

/** Session-scoped, deliberately controllable adapter for the interactive CkOnline gym. */
UCLASS(BlueprintType)
class CKTESTS_API UCkOnlineTest_Subsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    auto Deinitialize() -> void override;

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_Start();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    void Request_Stop();

    /** Clears the test record only when the service is stopped. */
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_ResetRecord();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_SubmitImmediate();

    /** Execute inline, then stop from its generic request-completion delegate. */
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_SubmitImmediateStopFromGeneric();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    FGuid Request_NewOperationId() { return FGuid::NewGuid(); }

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_MarkOperationImmediate(FGuid InOperationId);

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_SubmitHeld();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_SubmitShortTimeout();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_ReleaseHeld();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_RepeatHeldCallback();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_ReleaseTimedAfterTimeout();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_CancelHeld();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    void Request_SetStopOnNextCompletion(bool InEnabled);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    FCk_Handle_OnlineService Get_Service() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    FCk_OnlineService_Diagnostics Get_Diagnostics() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    FString Get_GenerationText() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    FString Get_StateText() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_ObservedCompletions() const { return _ObservedCompletions; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_ObservedSuccesses() const { return _ObservedSuccesses; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_ObservedCancellations() const { return _ObservedCancellations; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_ObservedTimeouts() const { return _ObservedTimeouts; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_AdapterExecutions() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_AdapterCancels() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_AdapterShutdowns() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_StopOnNextCompletion() const { return _StopOnNextCompletion; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_StopsFromCompletion() const { return _StopsFromCompletion; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_StopsFromGenericCompletion() const { return _StopsFromGenericCompletion; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_LateReleaseAttempts() const { return _LateReleaseAttempts; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_NoCompletionAfterLateRelease() const
    { return _LateReleaseAttempts > 0 && GFrameCounter > _LateReleaseFrame + 1 &&
        _ObservedCompletions == _ObservedAtLateRelease; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_DomainLateReleaseObserved() const
    { return _StopsFromCompletion > 0 && _LateReleaseAttempts > _LateReleasesAtDomainStop &&
        Get_Diagnostics().Get_Generation() == _DomainStopGeneration; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_GenericLateReleaseObserved() const
    { return _StopsFromGenericCompletion > 0 && _LateReleaseAttempts > _LateReleasesAtGenericStop &&
        Get_Diagnostics().Get_Generation() == _GenericStopGeneration; }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_HasHeldOperation() const { return _HeldOperation.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_HasTimedOperation() const { return _TimedOperation.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_TravelVisits() const { return _TravelVisits; }

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    void Request_RecordGymVisit();

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online")
    bool Request_PinGenerationForTravel();

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_HasPinnedGeneration() const { return _PinnedGeneration.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_IsPinnedGenerationCurrent() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    bool Get_HasTickedSincePin() const;

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online")
    int32 Get_VisitsSincePin() const { return _TravelVisits - _VisitsAtPin; }

private:
    UFUNCTION()
    void OnServiceCompleted(FCk_Handle_OnlineService InService, FCk_OnlineService_Completion InCompletion);

    UFUNCTION()
    void OnRequestCompleted(FCk_Handle InService, ECk_Request_OperationResult InResult);

    auto DoSubmit(bool InImmediate, FCk_Time InTimeout, FGuid& OutOperationId,
        bool InStopFromGeneric = false) -> bool;

    TSharedPtr<FCkOnlineTest_FakeAdapter> _Adapter;
    FGuid _HeldOperation;
    FGuid _TimedOperation;
    FGuid _PinnedGeneration;
    FGuid _DomainStopGeneration;
    FGuid _GenericStopGeneration;
    TWeakObjectPtr<UWorld> _LastVisitedWorld;
    uint64 _LateReleaseFrame = 0;
    int32 _TicksAtPin = 0;
    int32 _VisitsAtPin = 0;
    int32 _ObservedCompletions = 0;
    int32 _ObservedSuccesses = 0;
    int32 _ObservedCancellations = 0;
    int32 _ObservedTimeouts = 0;
    int32 _TravelVisits = 0;
    int32 _StopsFromCompletion = 0;
    int32 _StopsFromGenericCompletion = 0;
    int32 _LateReleaseAttempts = 0;
    int32 _ObservedAtLateRelease = 0;
    int32 _LateReleasesAtDomainStop = 0;
    int32 _LateReleasesAtGenericStop = 0;
    bool _StopOnNextCompletion = false;
    bool _StopOnNextGenericCompletion = false;
};
