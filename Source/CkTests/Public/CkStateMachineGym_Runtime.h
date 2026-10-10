#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"

#include <Kismet/BlueprintFunctionLibrary.h>

#include "CkStateMachineGym_Runtime.generated.h"

// Checked context lookup for StateMachine gym callbacks. In contrast to
// FCk_Handle::Get_ContextOwner, this accepts malformed live handles without
// reading an absent ContextOwner fragment. Success proves a live same-current-
// game-world context in the callback's bounded lifetime-owner ancestry;
// failure clears OutContext, including aliased input/output arguments.
UCLASS()
class CKTESTS_API UCk_StateMachineGym_Runtime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure,
              DisplayName = "[Ck][StateMachineGym] Try Get Source Context",
              Category = "Ck|Tests|StateMachineGym")
    static bool TryGet_SourceContext(
        const FCk_Handle& InCallback, FCk_Handle& OutContext);
};
