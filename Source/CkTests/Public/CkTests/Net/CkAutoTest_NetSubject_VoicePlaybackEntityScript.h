#pragma once

#include "CoreMinimal.h"

#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"

#include "CkAutoTest_NetSubject_VoicePlaybackEntityScript.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Entity script for ACk_AutoTest_NetSubject_VoicePlayback_UE. Construct composes, on every machine, a Global2D
// VoiceChannel child that authors a SoundClass and a VoiceTalker that attaches its playback to the actor's
// `_VoiceSocket` at its socket. Each machine's Spec names that machine's own copy of the component.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType)
class CKTESTS_API UCk_AutoTest_NetSubject_VoicePlaybackEntityScript_UE : public UCk_EntityScript_WithActor_UE
{
    GENERATED_BODY()

public:
    virtual auto
    Construct(
        FCk_Handle& InHandle,
        const FInstancedStruct& InSpawnParams) -> ECk_EntityScript_ConstructionFlow override;
};

// --------------------------------------------------------------------------------------------------------------------
