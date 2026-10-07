#pragma once

#include "CkSceneNodeGym_Motion.h"

#if WITH_DEV_AUTOMATION_TESTS

// Read-only creator and completion evidence. This never changes a receipt.
struct FCk_SceneNodeMotion_TestAccess
{
    struct FSlot
    {
        FCk_Handle Root;
        FCk_Handle Raw;
        FCk_Handle_Timer Timer;
        FCk_Handle_Timer ResumeReturn;
        int32 IssuedMotionUpdateCount = 0;
        bool bAllocationAttempted = false;
        bool bBound = false;
        bool bResumeIssued = false;
        bool bResumeReturned = false;
        bool bResumeCallback = false;
        bool bResumeSucceeded = false;
    };

    static bool TryCapture(const UCk_SceneNodeGym_Motion_UE* Motion,
        int32& OutGeneration, bool& bOutClosed, bool& bOutEverActive,
        TArray<FSlot>& OutSlots)
    {
        OutGeneration = 0;
        bOutClosed = false;
        bOutEverActive = false;
        OutSlots.Reset();
        if (!IsInGameThread() || !IsValid(Motion) || Motion->Slots.Num() != 6)
        { return false; }
        TArray<FSlot> Candidate;
        Candidate.Reserve(6);
        for (const auto& Slot : Motion->Slots)
        {
            Candidate.Add(FSlot{Slot.Root, Slot.Raw, Slot.Timer,
                Slot.ResumeReturn, Slot.IssuedMotionUpdateCount,
                Slot.bAllocationAttempted, Slot.bBound, Slot.bResumeIssued,
                Slot.bResumeReturned, Slot.bResumeCallback,
                Slot.bResumeSucceeded});
        }
        OutGeneration = Motion->Generation;
        bOutClosed = Motion->bClosed;
        bOutEverActive = Motion->bEverActive;
        OutSlots = MoveTemp(Candidate);
        return true;
    }

    static bool TryGet_StopState(const UCk_SceneNodeGym_Motion_UE* Motion,
        bool& bOutClosed, bool& bOutOpaqueConstruction,
        bool& bOutStopUnproven)
    {
        bOutClosed = false;
        bOutOpaqueConstruction = false;
        bOutStopUnproven = false;
        if (!IsInGameThread() || !IsValid(Motion)) { return false; }
        bOutClosed = Motion->bClosed;
        bOutOpaqueConstruction = Motion->bUnproven;
        bOutStopUnproven = Motion->bStopUnproven;
        return true;
    }
};

#endif
