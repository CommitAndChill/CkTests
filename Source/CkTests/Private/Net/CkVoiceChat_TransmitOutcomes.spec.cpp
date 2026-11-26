// What a transmit reports about itself. All single-world, all on injected capture sources - no test here opens a
// real microphone, and "capture unavailable" is produced through the capture seam, not by hoping a device is missing.
//
//   CaptureUnavailable_ReportedWithoutEnsure: a source whose Start reports no device fails the transmit, is
//     readable through Get_CaptureStartResult, raises no ensure, and stays the talker's source on the next attempt
//     (an injected source is never replaced by the engine microphone). Positive control: a source that starts.
//   SilentTransmit_ReportsOnceOnlyWhenNothingWasEncoded: a transmit whose level never reaches the voice gate logs
//     exactly one line that it encoded nothing (naming the talker and its peak level against the threshold),
//     however long it runs; a self-muted talker fed the same audio stays quiet; a talker that speaks and then
//     goes silent for longer than the report window (a channel member, so its frames are sent) stays
//     quiet.
//   EncodedButUnsent_ReportsWhyAndSentStaysQuiet: a talker that encodes but is a member of no channel logs exactly
//     one line naming that stage; a member fed the same audio, whose frames are sent, logs nothing.
//   EachTransmitStartsWithTheGateClosed: a transmit stopped while the gate is open does not hand that open gate to
//     the next transmit - silence at the start of the next one encodes nothing.
//   ScriptToneHook_DrivesTheRealPipeline: the script-callable tone hook (UCk_Utils_AutoTest_VoiceChat_UE) feeds the
//     real gate -> encode -> jitter -> decode loop.
//
// Surface in Session Frontend: Ck.VoiceChat.Net.CaptureUnavailable_ReportedWithoutEnsure,
// Ck.VoiceChat.Pipeline.SilentTransmit_ReportsOnceOnlyWhenNothingWasEncoded /
// .EncodedButUnsent_ReportsWhyAndSentStaysQuiet / .EachTransmitStartsWithTheGateClosed /
// .ScriptToneHook_DrivesTheRealPipeline

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/VoiceChat/CkAutoTest_VoiceChat_Utils.h"

#include "CkVoiceChat/Capture/CkVoiceChat_CaptureSource.h"
#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "VoiceModule.h"

#include <NativeGameplayTags.h>

// --------------------------------------------------------------------------------------------------------------------

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VoiceChat_Channel_AutoTest_Outcomes, TEXT("VoiceChat.Channel.AutoTest.Outcomes"));

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_transmit_outcomes_spec
{
    constexpr auto SampleRate = 48000;
    constexpr auto BytesPerSecond = SampleRate * 2;
    constexpr auto WaitSeconds = 15.0;

    struct FOutcomesSpecState
    {
        FCk_Handle_VoiceTalker Talker;
        FCk_Handle_VoiceTalker OtherTalker;
        FCk_Handle_VoiceTalker ThirdTalker;

        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> Source;
        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> OtherSource;
        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> ThirdSource;

        int64 VoiceEnsuresBefore = 0;
        uint16 SeqBefore = 0;

        bool HadOriginalVoiceKey = false;
        bool OriginalVoiceEnabled = false;
    };

    auto EnableEngineVoice(FOutcomesSpecState& InState) -> void
    {
        InState.HadOriginalVoiceKey = GConfig->GetBool(TEXT("Voice"), TEXT("bEnabled"), InState.OriginalVoiceEnabled, GEngineIni);
        GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), true, GEngineIni);

        if (FModuleManager::Get().IsModuleLoaded(TEXT("Voice")) && NOT FVoiceModule::Get().IsVoiceEnabled())
        {
            FModuleManager::Get().UnloadModule(TEXT("Voice"));
        }
    }

    auto RestoreEngineVoice(const FOutcomesSpecState& InState) -> void
    {
        if (InState.HadOriginalVoiceKey)
        { GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), InState.OriginalVoiceEnabled, GEngineIni); }
        else
        { GConfig->RemoveKey(TEXT("Voice"), TEXT("bEnabled"), GEngineIni); }
    }

    auto Get_VoiceEnsureCount() -> int64
    {
        return ck::auto_test::entity_bridge::Get_EnsureCount_FromFiles(TEXT("CkVoiceChat"));
    }

    auto Add_Talker(UWorld* InServer, const FCk_VoiceTalker_Spec& InSpec) -> FCk_Handle_VoiceTalker
    {
        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
        auto TalkerEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);

        return UCk_Utils_VoiceTalker_UE::Add(TalkerEntity, InSpec);
    }

    // A Global2D channel on its own host entity, registered on this (authority) world.
    auto Add_RegisteredChannel(UWorld* InServer) -> FCk_Handle_VoiceChannel
    {
        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
        auto Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);

        return UCk_Utils_VoiceChannel_UE::Add(Host, FCk_VoiceChannel_Spec{TAG_VoiceChat_Channel_AutoTest_Outcomes}
            .Set_SpatializationPolicy(ECk_VoiceChat_SpatializationPolicy::Global2D));
    }

    auto Join(FCk_Handle_VoiceChannel InChannel, const FCk_Handle_VoiceTalker& InTalker) -> void
    {
        UCk_Utils_VoiceChannel_UE::Request_Join(InChannel, FCk_Request_VoiceChannel_Join{InTalker}, {});
    }

    auto Make_OpenMicSpec() -> FCk_VoiceTalker_Spec
    {
        auto Spec = FCk_VoiceTalker_Spec{};
        Spec.Set_TransmitMode(ECk_VoiceChat_TransmitMode::OpenMic);
        Spec.Set_InputGain(1.0f);
        return Spec;
    }

    // Collects the CkVoiceChat lines that report a transmit producing nothing.
    class FNoProgressWatch final : public FOutputDevice
    {
    public:
        auto Reset() -> void
        {
            FScopeLock Lock{&_Guard};
            _Lines.Reset();
        }

        auto Get_Lines() -> TArray<FString>
        {
            FScopeLock Lock{&_Guard};
            return _Lines;
        }

    protected:
        virtual void Serialize(const TCHAR* InText, ELogVerbosity::Type InVerbosity, const FName& InCategory) override
        {
            if (InCategory != FName{TEXT("CkVoiceChat")})
            { return; }

            const auto Text = FString{InText};
            if (NOT Text.Contains(TEXT("has been transmitting for")))
            { return; }

            FScopeLock Lock{&_Guard};
            _Lines.Add(Text);
        }

    private:
        FCriticalSection _Guard;
        TArray<FString> _Lines;
    };

    auto Get_NoProgressWatch() -> FNoProgressWatch&
    {
        static FNoProgressWatch Watch;
        return Watch;
    }

    auto Get_LinesNaming(const FCk_Handle_VoiceTalker& InTalker) -> TArray<FString>
    {
        const auto Identity = ck::Format_UE(TEXT("VoiceTalker [{}]"), InTalker);

        auto Lines = Get_NoProgressWatch().Get_Lines();
        Lines.RemoveAll([&](const FString& InLine) { return NOT InLine.Contains(Identity); });
        return Lines;
    }

    auto Add_Teardown(FAutomationTestBase* InTest, const TSharedRef<FOutcomesSpecState, ESPMode::ThreadSafe>& InState) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([InState]() -> bool
            {
                GLog->RemoveOutputDevice(&Get_NoProgressWatch());
                RestoreEngineVoice(*InState);
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("restore log suppression, log watch and the Voice config")));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_CaptureUnavailable_ReportedWithoutEnsure,
    "Ck.VoiceChat.Net.CaptureUnavailable_ReportedWithoutEnsure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_CaptureUnavailable_ReportedWithoutEnsure::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_outcomes_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FOutcomesSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();

            // Positive control: a source that starts.
            State->OtherTalker = Add_Talker(InServer, Make_OpenMicSpec());
            State->OtherSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->OtherSource->Enqueue_Silence(FCk_Time{2.0f});
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->OtherTalker, State->OtherSource);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->OtherTalker, {}, {});

            // The machine without a microphone, modelled at the seam.
            State->Talker = Add_Talker(InServer, Make_OpenMicSpec());
            State->Source = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->Source->Set_StartResult(ECk_VoiceChat_CaptureStartResult::Unavailable_NoCaptureDevice);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->Talker, State->Source);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->Talker, {}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->OtherTalker) && UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->OtherTalker) &&
                ck::IsValid(State->Talker) &&
                UCk_Utils_VoiceTalker_UE::Get_CaptureStartResult(State->Talker) != ECk_VoiceChat_CaptureStartResult::NotAttempted;
        }), WaitSeconds, TEXT("the control talker transmits and the unavailable one reported its start")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestTrue(TEXT("control: a source that starts reports Started"),
                UCk_Utils_VoiceTalker_UE::Get_CaptureStartResult(State->OtherTalker) == ECk_VoiceChat_CaptureStartResult::Started);

            TestTrue(TEXT("no device is reported as such"),
                UCk_Utils_VoiceTalker_UE::Get_CaptureStartResult(State->Talker) ==
                ECk_VoiceChat_CaptureStartResult::Unavailable_NoCaptureDevice);
            TestFalse(TEXT("and the talker is not transmitting"), UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->Talker));
            TestEqual(TEXT("the injected source was asked once"), State->Source->Get_StartCount(), 1);
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("capture unavailable: reported, no ensure")));

    // A second attempt reaches the SAME injected source: had StartTransmit replaced it with the engine microphone,
    // the fake would not be asked again (and a real device would have been opened).
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->Talker, {}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return State->Source->Get_StartCount() >= 2;
        }), WaitSeconds, TEXT("the retry asked the injected source again")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestFalse(TEXT("the retry also failed without transmitting"), UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->Talker));
            TestEqual(TEXT("still no CkVoiceChat ensure"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);
            return true;
        }), TEXT("the injected source wins on retry")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatPipeline_SilentTransmit_ReportsOnceOnlyWhenNothingWasEncoded,
    "Ck.VoiceChat.Pipeline.SilentTransmit_ReportsOnceOnlyWhenNothingWasEncoded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatPipeline_SilentTransmit_ReportsOnceOnlyWhenNothingWasEncoded::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_outcomes_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FOutcomesSpecState, ESPMode::ThreadSafe>();

    // RMS of a 0.02-amplitude sine at gain 1.0 is ~0.014, far under the default 0.07 gate.
    constexpr auto QuietAmplitude = 0.02f;

    // The report window is 5 s of transmit; 11.5 s of delivered audio is more than two windows.
    constexpr auto MoreThanTwoWindowsOfSamples = static_cast<uint64>(SampleRate * 11.5);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);
            GLog->AddOutputDevice(&Get_NoProgressWatch());
            Get_NoProgressWatch().Reset();

            // Never reaches the gate: reports, once.
            State->Talker = Add_Talker(InServer, Make_OpenMicSpec());
            State->Source = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->Source->Enqueue_Sine(FCk_Time{20.0f}, QuietAmplitude, 300.0f);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->Talker, State->Source);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->Talker, {}, {});

            // The same audio, self-muted: silent on purpose, never reports.
            State->OtherTalker = Add_Talker(InServer, Make_OpenMicSpec());
            State->OtherSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->OtherSource->Enqueue_Sine(FCk_Time{20.0f}, QuietAmplitude, 300.0f);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->OtherTalker, State->OtherSource);
            UCk_Utils_VoiceTalker_UE::Request_SetSelfMute(State->OtherTalker, ECk_EnableDisable::Enable, {});
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->OtherTalker, {}, {});

            // Speaks into a channel (its frames are sent), then goes quiet for more than two windows: silence
            // after speech is not a stall.
            State->ThirdTalker = Add_Talker(InServer, Make_OpenMicSpec());
            State->ThirdSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->ThirdSource->Enqueue_Sine(FCk_Time{1.0f}, 0.4f, 300.0f);
            State->ThirdSource->Enqueue_Silence(FCk_Time{20.0f});
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->ThirdTalker, State->ThirdSource);
            Join(Add_RegisteredChannel(InServer), State->ThirdTalker);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->ThirdTalker, {}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Talker) && NOT Get_LinesNaming(State->Talker).IsEmpty();
        }), WaitSeconds, TEXT("the under-the-gate transmit reported that it encoded nothing")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State, MoreThanTwoWindowsOfSamples]() -> bool
        {
            return State->Source->Get_SamplesEmitted() >= MoreThanTwoWindowsOfSamples &&
                State->ThirdSource->Get_SamplesEmitted() >= MoreThanTwoWindowsOfSamples;
        }), WaitSeconds, TEXT("every transmit has run for more than two report windows")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            const auto Lines = Get_LinesNaming(State->Talker);

            if (NOT TestEqual(TEXT("the transmit that never encoded reported exactly once, however long it ran"),
                    Lines.Num(), 1))
            { return true; }

            AddInfo(FString::Printf(TEXT("report: %s"), *Lines[0]));

            TestTrue(TEXT("the line says which stage produced nothing (the voice gate)"),
                Lines[0].Contains(TEXT("never reached the voice-gate threshold")));
            TestTrue(TEXT("the line carries the threshold it was measured against"), Lines[0].Contains(TEXT("0.07")));

            TestTrue(TEXT("all three talkers are still transmitting (their silence is not a stopped transmit)"),
                UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->Talker) &&
                UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->OtherTalker) &&
                UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->ThirdTalker));

            TestEqual(TEXT("the self-muted talker fed the same audio for as long reported nothing"),
                Get_LinesNaming(State->OtherTalker).Num(), 0);

            TestTrue(TEXT("the member that spoke did encode (the control for 'went quiet afterwards')"),
                State->ThirdTalker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq() > 0);
            TestEqual(TEXT("and its two windows of silence after speaking reported nothing"),
                Get_LinesNaming(State->ThirdTalker).Num(), 0);

            return true;
        }), TEXT("reports once, only when nothing was encoded")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatPipeline_EachTransmitStartsWithTheGateClosed,
    "Ck.VoiceChat.Pipeline.EachTransmitStartsWithTheGateClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatPipeline_EachTransmitStartsWithTheGateClosed::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_outcomes_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FOutcomesSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);

            State->Talker = Add_Talker(InServer, Make_OpenMicSpec());
            State->Source = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->Source->Enqueue_Sine(FCk_Time{10.0f}, 0.4f, 300.0f);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->Talker, State->Source);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->Talker, {}, {});
        })));

    // Positive control: the first transmit opens the gate and encodes.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Talker) && UCk_Utils_VoiceTalker_UE::Get_IsSpeaking(State->Talker) &&
                State->Talker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq() > 0;
        }), WaitSeconds, TEXT("the first transmit opened the gate and encoded frames")));

    // Stopped mid-tone: the gate is open at the stop.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            UCk_Utils_VoiceTalker_UE::Request_StopTransmit(State->Talker, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return NOT UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->Talker);
        }), WaitSeconds, TEXT("the first transmit stopped")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            State->SeqBefore = State->Talker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq();

            State->OtherSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->OtherSource->Enqueue_Silence(FCk_Time{5.0f});
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->Talker, State->OtherSource);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->Talker, {}, {});
        })));

    // Twice the gate's 0.2 s release of silence: an inherited open gate would have encoded through it by now.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return State->OtherSource->Get_SamplesEmitted() >= static_cast<uint64>(SampleRate * 0.4);
        }), WaitSeconds, TEXT("the second transmit captured 0.4 s of silence")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            RestoreEngineVoice(*State);

            TestTrue(TEXT("the second transmit is running"), UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->Talker));
            TestEqual(TEXT("silence at the start of the next transmit encoded nothing (the gate started closed)"),
                static_cast<int32>(State->Talker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq()),
                static_cast<int32>(State->SeqBefore));
            TestFalse(TEXT("and the talker is not speaking"), UCk_Utils_VoiceTalker_UE::Get_IsSpeaking(State->Talker));

            return true;
        }), TEXT("each transmit starts with the gate closed")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatPipeline_ScriptToneHook_DrivesTheRealPipeline,
    "Ck.VoiceChat.Pipeline.ScriptToneHook_DrivesTheRealPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatPipeline_ScriptToneHook_DrivesTheRealPipeline::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_outcomes_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FOutcomesSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);

            auto Spec = Make_OpenMicSpec();
            Spec.Set_Loopback(ECk_EnableDisable::Enable);
            State->Talker = Add_Talker(InServer, Spec);

            UCk_Utils_AutoTest_VoiceChat_UE::Debug_InjectToneCaptureSource(State->Talker,
                FCk_Time{0.2f}, FCk_Time{1.0f}, 0.4f, 300.0f);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->Talker, {}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Talker) &&
                UCk_Utils_VoiceTalker_UE::Debug_Get_LoopbackDecodedPcm(State->Talker).Num() >= BytesPerSecond / 2;
        }), WaitSeconds, TEXT("the tone came back out of the decoder")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestTrue(TEXT("the hook's source started"),
                UCk_Utils_VoiceTalker_UE::Get_CaptureStartResult(State->Talker) == ECk_VoiceChat_CaptureStartResult::Started);

            const auto& DecodedPcm = UCk_Utils_VoiceTalker_UE::Debug_Get_LoopbackDecodedPcm(State->Talker);
            const auto* Samples = reinterpret_cast<const int16*>(DecodedPcm.GetData());

            auto LoudSamples = 0;
            for (auto SampleIdx = 0; SampleIdx < DecodedPcm.Num() / 2; ++SampleIdx)
            {
                if (FMath::Abs(Samples[SampleIdx]) > 1000)
                { ++LoudSamples; }
            }

            TestTrue(TEXT("the decoded audio carries the tone, not silence"), LoudSamples > 1000);

            return true;
        }), TEXT("script tone hook drives the real pipeline")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatPipeline_EncodedButUnsent_ReportsWhyAndSentStaysQuiet,
    "Ck.VoiceChat.Pipeline.EncodedButUnsent_ReportsWhyAndSentStaysQuiet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatPipeline_EncodedButUnsent_ReportsWhyAndSentStaysQuiet::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_outcomes_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FOutcomesSpecState, ESPMode::ThreadSafe>();

    constexpr auto MoreThanTwoWindowsOfSamples = static_cast<uint64>(SampleRate * 11.5);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);
            GLog->AddOutputDevice(&Get_NoProgressWatch());
            Get_NoProgressWatch().Reset();

            const auto Channel = Add_RegisteredChannel(InServer);

            // Loud enough to encode, but a member of no channel: its frames have nowhere to go.
            State->Talker = Add_Talker(InServer, Make_OpenMicSpec());
            State->Source = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->Source->Enqueue_Sine(FCk_Time{20.0f}, 0.4f, 300.0f);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->Talker, State->Source);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->Talker, {}, {});

            // The same audio from a member: its frames go into the host's routing inbox.
            State->OtherTalker = Add_Talker(InServer, Make_OpenMicSpec());
            State->OtherSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->OtherSource->Enqueue_Sine(FCk_Time{20.0f}, 0.4f, 300.0f);
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(State->OtherTalker, State->OtherSource);
            Join(Channel, State->OtherTalker);
            UCk_Utils_VoiceTalker_UE::Request_StartTransmit(State->OtherTalker, {}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Talker) && NOT Get_LinesNaming(State->Talker).IsEmpty();
        }), WaitSeconds, TEXT("the encoding non-member reported that it sent nothing")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State, MoreThanTwoWindowsOfSamples]() -> bool
        {
            return State->Source->Get_SamplesEmitted() >= MoreThanTwoWindowsOfSamples &&
                State->OtherSource->Get_SamplesEmitted() >= MoreThanTwoWindowsOfSamples;
        }), WaitSeconds, TEXT("both transmits have run for more than two report windows")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            const auto Lines = Get_LinesNaming(State->Talker);

            if (NOT TestEqual(TEXT("the non-member reported exactly once, however long it ran"), Lines.Num(), 1))
            { return true; }

            AddInfo(FString::Printf(TEXT("report: %s"), *Lines[0]));

            TestTrue(TEXT("the line says frames were encoded but none was sent"), Lines[0].Contains(TEXT("but sent none")));
            TestTrue(TEXT("the line names the stage: not a CanTalk member of the registered channel(s)"),
                Lines[0].Contains(TEXT("not a CanTalk member of any of the [1] channel(s) registered here")));

            // The member encoded for more than two windows and never said "sent none", so its frames left: had any
            // stage held them, the same detector that just reported the non-member would have reported it.
            TestTrue(TEXT("control: the member encoded frames"),
                State->OtherTalker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq() > 0);
            TestEqual(TEXT("the member whose frames are sent reported nothing"), Get_LinesNaming(State->OtherTalker).Num(), 0);

            return true;
        }), TEXT("encoded but unsent names its stage; sent stays quiet")));

    Add_Teardown(this, State);

    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
