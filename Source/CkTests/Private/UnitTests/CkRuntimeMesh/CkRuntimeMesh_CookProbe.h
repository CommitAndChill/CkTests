#pragma once

#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkJolt/Body/CkJoltBody_Fragment_Data.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Fragment_Data.h"

#include "UObject/Object.h"

#include "CkRuntimeMesh_CookProbe.generated.h"

// --------------------------------------------------------------------------------------------------------------------
// Receiver for the packaged probe's slice, request-completion and Jolt setup delegates; it only records what arrived.
// Bodies are inline so the reflected thunks link in every configuration, including those without the probe.

UCLASS(Transient)
class UCk_Test_RuntimeMeshCookProbeListener_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnSliceResolved(
        FCk_RuntimeMesh_SliceResult InResult)
    { SliceResults.Add(InResult); }

    UFUNCTION()
    void OnRequestCompleted(
        FCk_Handle InRequestOwner,
        ECk_Request_OperationResult InResult)
    { Completions.Add(InResult); }

    UFUNCTION()
    void OnJoltSetupResolved(
        FCk_Handle_JoltBody InBody,
        ECk_JoltBody_SetupState InState,
        ECk_JoltBody_SetupFailure InFailure)
    {
        ++JoltResolutions;
        JoltState = InState;
        JoltFailure = InFailure;
    }

public:
    TArray<FCk_RuntimeMesh_SliceResult> SliceResults;
    TArray<ECk_Request_OperationResult> Completions;
    int32 JoltResolutions = 0;
    ECk_JoltBody_SetupState JoltState = ECk_JoltBody_SetupState::Pending;
    ECk_JoltBody_SetupFailure JoltFailure = ECk_JoltBody_SetupFailure::None;
};

// --------------------------------------------------------------------------------------------------------------------

#if WITH_DEV_AUTOMATION_TESTS

namespace ck::tests::runtimemesh
{
    auto StartCookProbeIfRequested() -> void;
    auto StopCookProbe() -> void;
}

#endif
