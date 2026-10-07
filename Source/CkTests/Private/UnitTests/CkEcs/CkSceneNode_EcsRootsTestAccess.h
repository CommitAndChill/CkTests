#pragma once

#include "CkSceneNodeGym_EcsRoots.h"

#if WITH_DEV_AUTOMATION_TESTS

// Read-only test evidence for the thirteen raw allocator returns. The runtime
// deliberately exposes no public API that could use these identities to retire
// a partially constructed graph.
struct FCk_SceneNodeEcsRoots_TestAccess
{
    struct FSlot
    {
        FCk_Handle Target;
        FCk_Handle Carrier;
        FCk_Handle_PendingEntityScript Pending;
        bool bAllocationAttempted = false;
        bool bIssued = false;
        bool bReturned = false;
        bool bCallback = false;
        bool bSucceeded = false;
    };

    static bool TryCapture(const UCk_SceneNodeGym_EcsRoots_UE* Receipt,
        FCk_Handle& OutWorldRoot, FCk_Handle& OutGenerationOwner,
        TArray<FSlot>& OutSlots)
    {
        OutWorldRoot = {};
        OutGenerationOwner = {};
        OutSlots.Reset();
        if (!IsInGameThread() || !IsValid(Receipt) || Receipt->Slots.Num() != 6)
        { return false; }
        TArray<FSlot> Candidate;
        Candidate.Reserve(6);
        for (const auto& Entry : Receipt->Slots)
        {
            Candidate.Add(FSlot{Entry.Target, Entry.Carrier, Entry.Pending,
                Entry.bAllocationAttempted, Entry.bIssued, Entry.bReturned,
                Entry.bCallback, Entry.bSucceeded});
        }
        OutWorldRoot = Receipt->WorldRoot;
        OutGenerationOwner = Receipt->GenerationOwner;
        OutSlots = MoveTemp(Candidate);
        return true;
    }
};

#endif
