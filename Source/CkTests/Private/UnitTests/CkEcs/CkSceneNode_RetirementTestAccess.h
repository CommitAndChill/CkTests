#pragma once

#include "CkSceneNodeGym_Retirement.h"

#if WITH_DEV_AUTOMATION_TESTS

// Read-only frozen creator, actor-serial and pre-destroy stage evidence.
struct FCk_SceneNodeRetirement_TestAccess
{
    static bool TryCapture(const UCk_SceneNodeGym_Retirement_UE* Receipt,
        TArray<FCk_Handle>& OutKnown, TArray<FCk_Handle>& OutParents,
        TArray<TWeakObjectPtr<AActor>>& OutActors,
        bool& bOutQuiescenceObserved, bool& bOutDestroyIssued)
    {
        OutKnown.Reset();
        OutParents.Reset();
        OutActors.Reset();
        bOutQuiescenceObserved = false;
        bOutDestroyIssued = false;
        if (!IsInGameThread() || !IsValid(Receipt)) { return false; }
        TArray<FCk_Handle> Known = Receipt->Known;
        TArray<FCk_Handle> Parents = Receipt->ExpectedParents;
        TArray<TWeakObjectPtr<AActor>> Actors = Receipt->Actors;
        if (Known.Num() != 41 || Parents.Num() != 41 || Actors.Num() != 18)
        { return false; }
        OutKnown = MoveTemp(Known);
        OutParents = MoveTemp(Parents);
        OutActors = MoveTemp(Actors);
        bOutQuiescenceObserved = Receipt->bQuiescenceObserved;
        bOutDestroyIssued = Receipt->bDestroyIssued;
        return true;
    }

    static bool TryCaptureDestroyRequested(
        const UCk_SceneNodeGym_Retirement_UE* Receipt,
        TArray<bool>& OutRequested)
    {
        OutRequested.Reset();
        if (!IsInGameThread() || !IsValid(Receipt) ||
            Receipt->DestroyRequested.Num() != 18)
        { return false; }
        OutRequested = Receipt->DestroyRequested;
        return true;
    }
};

#endif
