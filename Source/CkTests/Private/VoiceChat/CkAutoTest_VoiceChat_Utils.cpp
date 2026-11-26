#include "CkTests/VoiceChat/CkAutoTest_VoiceChat_Utils.h"

#include "CkVoiceChat/Capture/CkVoiceChat_CaptureSource.h"
#include "CkVoiceChat/Settings/CkVoiceChat_Settings.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_Utils_AutoTest_VoiceChat_UE::
    Debug_InjectToneCaptureSource(
        FCk_Handle_VoiceTalker& InTalker,
        FCk_Time InLeadInSilence,
        FCk_Time InToneDuration,
        float InAmplitude01,
        float InFrequencyHz)
    -> void
{
    CK_ENSURE_IF_NOT(ck::IsValid(InTalker),
        TEXT("Debug_InjectToneCaptureSource on an invalid VoiceTalker handle [{}]"), InTalker)
    { return; }

    const auto ToneIsAudible = InAmplitude01 > 0.0f && InAmplitude01 <= 1.0f && InFrequencyHz > 0.0f;
    CK_ENSURE_IF_NOT(ToneIsAudible,
        TEXT("Debug_InjectToneCaptureSource on VoiceTalker [{}] with amplitude [{}] and frequency [{}] - the amplitude "
             "must be in (0, 1] and the frequency above 0; use a silence lead-in for silence"),
        InTalker, InAmplitude01, InFrequencyHz)
    { return; }

    const auto ToneSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(UCk_Utils_VoiceChat_Settings_UE::Get_SampleRate());
    ToneSource->Enqueue_Silence(InLeadInSilence);
    ToneSource->Enqueue_Sine(InToneDuration, InAmplitude01, InFrequencyHz);

    UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(InTalker, ToneSource);
}

// --------------------------------------------------------------------------------------------------------------------
