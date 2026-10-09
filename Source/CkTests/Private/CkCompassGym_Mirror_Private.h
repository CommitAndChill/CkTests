#pragma once

#include "CkCompassGym_Mirror.h"
#include "CkCompass/CkCompass_Processor.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorGroups.h"

namespace ck
{
    enum class ECompassGym_MirrorPhase : uint8
    { Idle, Stage, Materialize, Commit, Retire, Failed };
    enum class ECompassGym_MirrorLeafPhase : uint8
    { Marker, WaitMarker, Host, WaitHost };

    struct FCompassGym_MirrorEntry
    {
        FCkCompassGym_SourceRow Row;
        FCk_Handle Host;
        FCk_Handle Marker;
        ECompassGym_MirrorLeafPhase RetirePhase =
            ECompassGym_MirrorLeafPhase::Marker;
    };

#if WITH_DEV_AUTOMATION_TESTS
    struct FCompassGym_MirrorProfileCounter
    {
        uint64 Calls = 0;
        double Seconds = 0.0;
    };

    struct FCompassGym_MirrorProfile
    {
        FCompassGym_MirrorProfileCounter CompleteCensus;
        // InstalledPhysical and CommitInstalledPhysical share one bucket.
        FCompassGym_MirrorProfileCounter InstalledPhysical;
        FCompassGym_MirrorProfileCounter Commit;
        FCompassGym_MirrorProfileCounter RetireBatch;
        FCompassGym_MirrorProfileCounter StageLoop;
    };
#endif

    struct FFragment_CompassGym_Mirror
    {
        CK_GENERATED_BODY(FFragment_CompassGym_Mirror);

        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> GameState;
        FCk_Handle WorldRoot;
        FCkCompassGym_WireSnapshot InstalledFrame;
        FCkCompassGym_WireSnapshot PendingFrame;
        TArray<FCkCompassGym_SourceRow> InstalledRows;
        TArray<FCkCompassGym_SourceRow> HistoryRows;
        TArray<FCkCompassGym_SourceRow> PendingRows;
        TArray<FCompassGym_MirrorEntry> Installed;
        TArray<FCompassGym_MirrorEntry> Staged;
        TArray<FCompassGym_MirrorEntry> Retiring;
        ECompassGym_MirrorPhase Phase = ECompassGym_MirrorPhase::Idle;
        int32 StageCursor = 0;
        double DeadlineSeconds = 0.0;
        double RetirementDeadlineSeconds = 0.0;
        bool ClosedUnproven = false;
        bool HasInstalled = false;
        bool MutationInProgress = false;
        FString Failure;
#if WITH_DEV_AUTOMATION_TESTS
        // Const proof queries record only test diagnostics. Prospective state
        // copies collect into their own discarded copy, never this live state.
        mutable FCompassGym_MirrorProfile Profile;
#endif
    };

    class FProcessor_CompassGym_Mirror : public ck_exp::TProcessor<
        FProcessor_CompassGym_Mirror, FCk_Handle,
        ck::TReadWrite<FFragment_CompassGym_Mirror>, CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_PostTransform;
        using RunAfter = TDepList<FProcessor_Compass_Setup, FProcessor_Compass_HandleRequests>;
        using RunBefore = TDepList<FProcessor_Compass_Update>;
        using TProcessor::TProcessor;

        static void ForEachEntity(TimeType DeltaT, HandleType MirrorRoot,
            FFragment_CompassGym_Mirror& State);
    };
}
