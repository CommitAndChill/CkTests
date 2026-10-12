#pragma once

#include "CkCrowd/Agent/CkCrowdAgent_Fragment_Data.h"
#include "CkCrowd/Debug/CkCrowd_RemoteDebugContract.h"

class UWorld;

// Server-only roster entry. The caller owns ID/epoch allocation and agent lifetime.
// Handles never enter the returned wire DTOs.
struct FCkCrowdGymServerAgentRecord
{
    FCkCrowdRemoteAgentId Id;
    FCk_Handle_CrowdAgent Agent;
    int64 ConfigurationRevision = 0;
    int64 OwnerSerial = 0;
};

class FCkCrowdGymRemoteSampler
{
public:
    // SelectedId may be null; if set, SelectionRequestSerial must be positive.
    // All outputs are cleared on failure. Must be called on the game thread.
    static auto TrySample(
        UWorld& World,
        int64 WorldGeneration,
        int64 SampleSequence,
        const TArray<FCkCrowdGymServerAgentRecord>& Roster,
        const FCk_Handle& RequestingPawnEntity,
        const FCkCrowdRemoteAgentId* SelectedId,
        int64 SelectionRequestSerial,
        TArray<FCkCrowdRemoteAgentRow>& OutRows,
        FCkCrowdRemoteAgentDetail& OutDetail) -> bool;
};
