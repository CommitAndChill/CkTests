#pragma once

#include "CoreMinimal.h"
#include "CkMessaging/CkMessaging_Fragment_Data.h"

#include "Test_MessagingBindingProbe.generated.h"

UCLASS()
class UCk_Test_MessagingBindingProbe : public UObject
{
    GENERATED_BODY()

public:
    int32 Received = 0;

    UFUNCTION()
    void OnMessage(FCk_Handle InOwner, FGameplayTag InMessageName, FInstancedStruct InPayload) { ++Received; }

    UFUNCTION()
    void OnOther(FCk_Handle InOwner, FGameplayTag InMessageName, FInstancedStruct InPayload) { ++Received; }
};
