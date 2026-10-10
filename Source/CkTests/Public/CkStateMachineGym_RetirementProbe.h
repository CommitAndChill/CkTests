#pragma once

#include "CoreMinimal.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment_Data.h"
#include "CkEcs/Handle/CkHandle.h"
#include "UObject/Object.h"
#include "CkStateMachineGym_RetirementProbe.generated.h"

class AActor;
class UWorld;

// Receives the real first-leaf BeginDestroy signal during source retirement.
UCLASS(NotBlueprintable)
class CKTESTS_API UCkStateMachineGym_RetirementProbe final : public UObject
{
    GENERATED_BODY()
public:
    bool Arm(UWorld* World, AActor* Actor, const FCk_Handle& Primary,
        const FCk_Handle& FirstLeaf);

    UFUNCTION() void OnFirstLeafBeginDestroy(FCk_Handle InLeaf);

    int32 Get_CallbackCount() const { return CallbackCount; }
    bool Get_Failed() const { return bFailed; }
    FCk_Handle Get_UnknownChild() const { return UnknownChild; }

private:
    TWeakObjectPtr<UWorld> ExactWorld;
    TWeakObjectPtr<AActor> ExactActor;
    FCk_Handle Primary;
    FCk_Handle FirstLeaf;
    FCk_Handle UnknownChild;
    FCk_Delegate_OnBeginDestroy BeginDelegate;
    int32 CallbackCount = 0;
    bool bFailed = false;
};
