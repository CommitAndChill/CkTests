#pragma once

#include "CoreMinimal.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "Test_EntityScript_PendingRendezvous_RetireProbe.generated.h"

// Retains the real replicated actor/WithActor bridge while avoiding the default
// subject's unrelated Float/Byte/Integer/Vector child allocation recipes.
UCLASS()
class ACk_Test_PendingRendezvous_MinimalSubject_UE final : public ACk_AutoTest_NetSubject
{
    GENERATED_BODY()

public:
    ACk_Test_PendingRendezvous_MinimalSubject_UE();
};

// A one-shot test receiver. It records exact identities before requesting retirement,
// and deliberately never reads either rejected handle after the destroy requests.
UCLASS()
class UCk_Test_PendingRendezvous_RetireProbe_UE final : public UObject
{
    GENERATED_BODY()

public:
    FCk_Handle ExpectedOwner;
    FCk_Handle Pending;
    FCk_Handle ExpectedConstructed;
    FCk_Handle Constructed;
    int32 CallbackCount = 0;
    bool bExactAtCallback = false;
    bool bRequestedBoth = false;

    UFUNCTION()
    void OnConstructed(FCk_Handle_EntityScript InConstructed);
};
