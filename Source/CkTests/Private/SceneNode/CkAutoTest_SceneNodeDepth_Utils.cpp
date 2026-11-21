#include "CkTests/SceneNode/CkAutoTest_SceneNodeDepth_Utils.h"

#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "Engine/World.h"

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_Utils_AutoTest_SceneNodeDepth_UE::
    Get_SchedulerFrameTimesMs(
        const UObject* InWorldContext,
        int64 InAfterFrame,
        int64 InUpToFrame)
    -> TArray<double>
{
#if UE_BUILD_SHIPPING
    return {};
#else
    const auto* World = ck::IsValid(InWorldContext) ? InWorldContext->GetWorld() : nullptr;
    if (ck::Is_NOT_Valid(World))
    { return {}; }

    const auto* Subsystem = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    if (ck::Is_NOT_Valid(Subsystem))
    { return {}; }

    auto MsByFrame = TSortedMap<uint64, double>{};
    for (const auto& [TickGroup, Actor] : Subsystem->Get_WorldActors())
    {
        if (NOT Actor.IsValid() || NOT Actor->Get_Scheduler().IsSet())
        { continue; }

        for (const auto& Snapshot : Actor->Get_Scheduler().GetValue().Get_DebugFrameHistory())
        {
            if (Snapshot.FrameNumber <= static_cast<uint64>(InAfterFrame) ||
                Snapshot.FrameNumber > static_cast<uint64>(InUpToFrame))
            { continue; }

            MsByFrame.FindOrAdd(Snapshot.FrameNumber) += Snapshot.TotalFrameTimeMs;
        }
    }

    auto Result = TArray<double>{};
    Result.Reserve(MsByFrame.Num());
    for (const auto& [Frame, Ms] : MsByFrame)
    { Result.Add(Ms); }

    return Result;
#endif
}

// --------------------------------------------------------------------------------------------------------------------
