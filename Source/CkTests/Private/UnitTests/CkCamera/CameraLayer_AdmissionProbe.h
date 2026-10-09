#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Request/CkRequest_Completion.h"
#include "CkCamera/Camera/CameraLayer/EntityScripts/CkCameraLayer_EntityScript.h"

#include "CameraLayer_AdmissionProbe.generated.h"

UCLASS()
class UCk_CameraLayer_AdmissionProbe final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 Succeeded = 0;

    UPROPERTY()
    int32 Rejected = 0;

    UFUNCTION()
    void OnCompleted(FCk_Handle InHandle, ECk_Request_OperationResult InResult)
    {
        if (InResult == ECk_Request_OperationResult::Succeeded)
        { ++Succeeded; }
        else
        { ++Rejected; }
    }
};

// A real EntityScript instance admitted by CameraLayer::Create. The fixture arms
// it only after the scheduler has constructed and naturally blended the layer.
UCLASS()
class UCk_CameraLayer_LifecycleProbe : public UCk_CameraLayer_EntityScript
{
    GENERATED_BODY()

public:
    UCk_CameraLayer_LifecycleProbe();

    enum class EMutation : uint8 { None, DisconnectSibling, RetireOwner };

    EMutation Mutation = EMutation::None;
    FCk_Handle_CameraLayer Sibling;
    FCk_Handle Owner;
    int32 ObservedTicks = 0;
    bool Armed = false;
    bool Fired = false;
    bool MutationSucceeded = false;

    virtual void Tick(FCk_Handle_CameraLayer InHandle, FCk_Time InDeltaT) override;
};

UCLASS()
class UCk_CameraLayer_LifecycleObserver : public UCk_CameraLayer_LifecycleProbe
{
    GENERATED_BODY()
};
