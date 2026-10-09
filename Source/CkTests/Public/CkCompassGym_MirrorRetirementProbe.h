#pragma once

#include "CoreMinimal.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment_Data.h"
#include "CkEcs/Handle/CkHandle.h"
#include "UObject/Object.h"
#include "CkCompassGym_MirrorRetirementProbe.generated.h"

class UWorld;

// A reflected receiver for the real local Marker BeginDestroy signal.
UCLASS(NotBlueprintable)
class CKTESTS_API UCkCompassGym_MirrorRetirementProbe final : public UObject
{
    GENERATED_BODY()
public:
    bool Arm(UWorld* World, const FCk_Handle& MirrorRoot,
        const FCk_Handle& Host,
        const FCk_Handle& Marker);

    UFUNCTION() void OnMarkerBeginDestroy(FCk_Handle InMarker);

    int32 Get_CallbackCount() const { return CallbackCount; }
    bool Get_Failed() const { return bFailed; }
    FCk_Handle Get_UnknownChild() const { return UnknownChild; }

private:
    TWeakObjectPtr<UWorld> ExactWorld;
    FCk_Handle MirrorRoot;
    FCk_Handle Host;
    FCk_Handle Marker;
    FCk_Handle UnknownChild;
    FCk_Delegate_OnBeginDestroy BeginDelegate;
    int32 CallbackCount = 0;
    bool bFailed = false;
};
