#pragma once

#include "CoreMinimal.h"

#include "CkCore/Time/CkTime.h"

#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Fragment_Data.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkAutoTest_VoiceChat_Utils.generated.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Test-only voice seams for script tests. CkVoiceChat's capture seam (UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource)
// takes a C++ capture source; this exposes the scripted tone source to AngelScript and Blueprint, so a net test can
// drive the real capture -> voice gate -> encode -> send -> route path without opening a microphone.
//
// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable)
class CKTESTS_API UCk_Utils_AutoTest_VoiceChat_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Replaces InTalker's microphone with a scripted source on this machine: InLeadInSilence of silence, then
    // InToneDuration of a sine at InFrequencyHz with peak InAmplitude01 (0..1 of full scale), then no audio at all.
    // Call it on the machine that owns the talker, BEFORE Request_StartTransmit; a transmitting talker refuses it.
    // An amplitude of 0.4 sits well above the default voice gate; a quiet value below the gate models a player the
    // gate does not open for.
    UFUNCTION(BlueprintCallable,
              Category = "Ck|Utils|AutoTest|VoiceChat",
              DisplayName = "[Ck][AutoTest] Inject Tone Capture Source",
              meta = (DevelopmentOnly))
    static void
    Debug_InjectToneCaptureSource(
        UPARAM(ref) FCk_Handle_VoiceTalker& InTalker,
        FCk_Time InLeadInSilence,
        FCk_Time InToneDuration,
        float InAmplitude01 = 0.4f,
        float InFrequencyHz = 300.0f);
};

// --------------------------------------------------------------------------------------------------------------------
