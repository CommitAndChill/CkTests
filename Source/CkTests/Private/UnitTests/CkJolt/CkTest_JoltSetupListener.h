#pragma once

#include "CkCore/Macros/CkMacros.h"

#include "CkJolt/Body/CkJoltBody_Fragment_Data.h"

#include "CkTest_JoltSetupListener.generated.h"

// --------------------------------------------------------------------------------------------------------------------
// Test-only receiver for the JoltBody setup-resolved and contact delegates; counts every callback it receives.

UCLASS()
class UCk_Test_JoltSetupListener_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void OnSetupResolved(
        FCk_Handle_JoltBody InBody,
        ECk_JoltBody_SetupState InState,
        ECk_JoltBody_SetupFailure InFailure)
    {
        ++_Count;
        _LastState = InState;
        _LastFailure = InFailure;

        if (_OnCallback)
        { _OnCallback(); }
    }

    UFUNCTION()
    void OnContact(
        FCk_Handle_JoltBody InBody,
        FCk_JoltBody_Payload_OnContact InPayload)
    { ++_ContactCount; }

public:
    int32 _Count = 0;
    int32 _ContactCount = 0;
    ECk_JoltBody_SetupState _LastState = ECk_JoltBody_SetupState::Pending;
    ECk_JoltBody_SetupFailure _LastFailure = ECk_JoltBody_SetupFailure::None;
    TFunction<void()> _OnCallback;
};
