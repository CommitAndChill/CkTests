#pragma once

#include "CkSceneNodeGym_FrameSampler.h"

#if WITH_DEV_AUTOMATION_TESTS

// Read-only publication state for actual world-post-tick test observations.
struct FCk_SceneNodeFrameSampler_TestAccess
{
    static bool TryCapture(const UCk_SceneNodeGym_FrameSampler_UE* Sampler,
        uint64& OutPublishedEngineFrame, int64& OutSerial,
        bool& bOutSampling, bool& bOutClosed)
    {
        OutPublishedEngineFrame = 0;
        OutSerial = 0;
        bOutSampling = false;
        bOutClosed = true;
        if (!IsInGameThread() || !IsValid(Sampler)) { return false; }
        OutPublishedEngineFrame = Sampler->PublishedEngineFrame;
        OutSerial = Sampler->Latest.FrameSerial;
        bOutSampling = Sampler->bInSample;
        bOutClosed = Sampler->bClosed;
        return true;
    }
};

#endif
