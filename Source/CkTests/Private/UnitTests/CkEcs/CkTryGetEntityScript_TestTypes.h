#pragma once

#include "CkEcs/EntityScript/CkEntityScript.h"

#include "CkTryGetEntityScript_TestTypes.generated.h"

// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable, HideDropdown)
class UCk_TryGetEntityScriptTest_Instanced_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable, HideDropdown)
class UCk_TryGetEntityScriptTest_NotInstanced_UE : public UCk_EntityScript_UE
{
    GENERATED_BODY()

public:
    UCk_TryGetEntityScriptTest_NotInstanced_UE()
    {
        _InstancingPolicy = ECk_EntityScript_InstancingPolicy::NotInstanced;
    }
};
