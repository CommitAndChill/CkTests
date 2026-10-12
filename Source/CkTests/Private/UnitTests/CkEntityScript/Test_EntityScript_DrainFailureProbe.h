#pragma once
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "Test_EntityScript_DrainFailureProbe.generated.h"

UCLASS()
class UCk_Test_EntityScript_DrainFailureProbe_UE final : public UCk_EntityScript_UE
{
    GENERATED_BODY()
public:
    static int32 ConstructCalls;
    virtual auto Get_EffectiveReplication() const -> ECk_Replication override;
    virtual auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
        -> ECk_EntityScript_ConstructionFlow override;
};
