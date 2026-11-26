#pragma once

#include "CoreMinimal.h"

#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

#include "CkAutoTest_NetSubject_VoiceAutoJoinEntityScript.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Entity scripts for the two auto-join subjects (CkAutoTest_NetSubject_VoiceAutoJoin.h). Construct runs on server
// and client through the entity-script replication path - the symmetric composition the control-plane handler
// requires before it applies membership on a client.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_VoiceAutoJoinChannelEntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_VoiceAutoJoinTalkerEntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

// --------------------------------------------------------------------------------------------------------------------
