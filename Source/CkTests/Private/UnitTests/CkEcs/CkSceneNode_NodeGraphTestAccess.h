#pragma once

#include "CkSceneNodeGym_NodeGraph.h"

#if WITH_DEV_AUTOMATION_TESTS

// Read-only evidence for the raw returns. Production exposes only completed nodes.
struct FCk_SceneNodeNodeGraph_TestAccess
{
    struct FSlot
    {
        FCk_Handle Raw;
        FCk_Handle Parent;
        FTransform InitialOffset;
        int32 RootSlot = INDEX_NONE;
        int32 ParentNodeSlot = INDEX_NONE;
        bool bAllocationAttempted = false;
        bool bComposed = false;
    };

    static bool TryCapture(const UCk_SceneNodeGym_NodeGraph_UE* Graph,
        int32& OutGeneration, TArray<FSlot>& OutSlots)
    {
        OutGeneration = 0;
        OutSlots.Reset();
        if (!IsInGameThread() || !IsValid(Graph) || Graph->Slots.Num() != 11)
        { return false; }
        TArray<FSlot> Candidate;
        Candidate.Reserve(11);
        for (const auto& Slot : Graph->Slots)
        {
            Candidate.Add(FSlot{Slot.Raw, Slot.Parent, Slot.InitialOffset,
                Slot.RootSlot, Slot.ParentNodeSlot,
                Slot.bAllocationAttempted, Slot.bComposed});
        }
        OutGeneration = Graph->Generation;
        OutSlots = MoveTemp(Candidate);
        return true;
    }
};

#endif
