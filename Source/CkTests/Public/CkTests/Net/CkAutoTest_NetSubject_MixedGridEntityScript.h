#pragma once

#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkAutoTest_NetSubject_MixedGridEntityScript.generated.h"

UCLASS()
class CKTESTS_API UCk_AutoTest_NetSubject_MixedGridEntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

protected:
    auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
        -> ECk_EntityScript_ConstructionFlow override;
    auto Get_IsSnapshotRespawnable() const -> bool override { return true; }
};

UCLASS()
class CKTESTS_API UCk_AutoTest_NetSubject_MixedGridObjectEntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

protected:
    auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
        -> ECk_EntityScript_ConstructionFlow override;
    auto Get_IsSnapshotRespawnable() const -> bool override { return true; }
};
