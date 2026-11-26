#pragma once

#include "CoreMinimal.h"

#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

#include "CkAutoTest_NetSubject_VoiceChatPawnEntityScript.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Entity script for ACk_AutoTest_NetSubject_VoiceChatPawn_UE. Construct composes a VoiceChannel child and a VoiceTalker
// (Spec transmit mode from the pawn's class default) on the bridged entity, on every machine.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_VoiceChatPawnEntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

// --------------------------------------------------------------------------------------------------------------------
