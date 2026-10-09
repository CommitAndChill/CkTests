#pragma once

#include "CkCompassGym_SourceMembership.h"
#include "CkCompass/CkCompass_Processor.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorGroups.h"

namespace ck
{
    enum class ECompassGym_SourceRequest : uint8 { None, Admit, Withdraw };
    enum class ECompassGym_PingRetirement : uint8
    { Marker, WaitMarkerGone, Timer, WaitTimerGone, Host, WaitHostGone };

    struct FFragment_CompassGym_SourceMembership
    {
        CK_GENERATED_BODY(FFragment_CompassGym_SourceMembership);

        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AActor> GameState;
        int64 Serial = 0;
        double DeadlineSeconds = 0.0;
        ECompassGym_SourceRequest Request = ECompassGym_SourceRequest::None;
        ECkCompassGym_MembershipResult Result = ECkCompassGym_MembershipResult::Unavailable;
        bool ClosedUnproven = false;
        FString Failure;
        bool BaseInitialized = false;
        bool JournalEnabled = false;
        bool RetirementMutationInProgress = false;
        bool CurrentRowsDirty = false;
        int64 CurrentRevision = 0;
        int64 HighestAcceptedCatalogId = 0;
        double PingStageDeadlineSeconds = 0.0;
        TArray<FCkCompassGym_SourceReceipt> Base;
        TArray<FCkCompassGym_SourceReceipt> ActivePings;
        TArray<FCkCompassGym_SourceReceipt> StagedPings;
        TArray<FCkCompassGym_SourceReceipt> FrozenPings;
        TArray<ECompassGym_PingRetirement> FrozenPingPhases;
        TArray<double> FrozenPingDeadlines;
        TArray<FCkCompassGym_SourceRow> CurrentRows;
        TArray<FCkCompassGym_SourceReceipt> Active;
        TArray<FCkCompassGym_SourceReceipt> Staged;
        TArray<FCkCompassGym_SourceRow> CommittedRows;
        TArray<FCkCompassGym_SourceReceipt> FrozenRetiring;
    };

    class FProcessor_CompassGym_SourceMembership : public ck_exp::TProcessor<
        FProcessor_CompassGym_SourceMembership, FCk_Handle,
        ck::TReadWrite<FFragment_CompassGym_SourceMembership>,
        CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_PostTransform;
        using RunAfter = TDepList<FProcessor_Compass_Setup, FProcessor_Compass_HandleRequests>;
        using RunBefore = TDepList<FProcessor_Compass_Update>;
        using TProcessor::TProcessor;

        static void ForEachEntity(TimeType DeltaT, HandleType SourceRoot,
            FFragment_CompassGym_SourceMembership& State);
    };
}
