#include "CkTests/SceneNode/CkAutoTest_SceneNodeLatePose_Utils.h"

#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"

#include "Engine/World.h"

// --------------------------------------------------------------------------------------------------------------------

bool
    UCk_Utils_AutoTest_SceneNodeLatePose_UE::
    Get_HasLateAnchorTag(
        const FCk_Handle& InHandle)
{
    return ck::IsValid(InHandle) && InHandle.Has<ck::FTag_SceneNode_LateAnchor>();
}

bool
    UCk_Utils_AutoTest_SceneNodeLatePose_UE::
    Get_HasTransformLateUpdatedTag(
        const FCk_Handle& InHandle)
{
    return ck::IsValid(InHandle) && InHandle.Has<ck::FTag_Transform_LateUpdated>();
}

bool
    UCk_Utils_AutoTest_SceneNodeLatePose_UE::
    Get_HasTransformUpdatedTag(
        const FCk_Handle& InHandle)
{
    return ck::IsValid(InHandle) && InHandle.Has<ck::FTag_Transform_Updated>();
}

auto
    UCk_Utils_AutoTest_SceneNodeLatePose_UE::
    TryGet_PreviousTransform(
        const FCk_Handle& InHandle,
        FTransform& OutPrevious)
    -> bool
{
    if (ck::Is_NOT_Valid(InHandle) || NOT InHandle.Has<ck::FFragment_Transform_Previous>())
    { return false; }

    OutPrevious = InHandle.Get<ck::FFragment_Transform_Previous>().Get_Transform();
    return true;
}

bool
    UCk_Utils_AutoTest_SceneNodeLatePose_UE::
    Get_LastFrameProcessorVisits(
        const UObject* InWorldContext,
        const FString& InProcessorName,
        int32& OutVisited,
        bool& OutSkippedEmptyView)
{
    OutVisited = 0;
    OutSkippedEmptyView = false;

#if UE_BUILD_SHIPPING
    return false;
#else
    const auto* World = ck::IsValid(InWorldContext) ? InWorldContext->GetWorld() : nullptr;
    if (ck::Is_NOT_Valid(World))
    { return false; }

    const auto* Subsystem = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    if (ck::Is_NOT_Valid(Subsystem))
    { return false; }

    for (const auto& [TickGroup, Actor] : Subsystem->Get_WorldActors())
    {
        if (NOT Actor.IsValid() || NOT Actor->Get_Scheduler().IsSet())
        { continue; }

        const auto& History = Actor->Get_Scheduler().GetValue().Get_DebugFrameHistory();
        if (History.IsEmpty())
        { continue; }

        for (const auto& Timing : History.Last().ProcessorTimings)
        {
            if (NOT Timing.ProcessorName.ToString().Contains(InProcessorName))
            { continue; }

            OutVisited = Timing.MainPassEntityCount;
            OutSkippedEmptyView = Timing.WasSkippedEmptyViewThisFrame;
            return true;
        }
    }

    return false;
#endif
}

// --------------------------------------------------------------------------------------------------------------------
