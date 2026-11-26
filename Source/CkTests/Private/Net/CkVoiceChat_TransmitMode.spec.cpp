// The transmit-mode contract: PushToTalk and OpenMic differ in who opens the transmit and whether the voice gate
// applies, nothing else. No test here opens a real microphone: every talker that could transmit has an injected source
// before anything could open it.
//
// Open mic, two worlds (listen host + one client), on ACk_AutoTest_NetSubject_VoiceChatPawn_UE possessed by the client,
// with a host-owned ACk_AutoTest_NetSubject_VoiceChat_UE as the host's ears:
//   OpenMic_StartsOnTheLocallyControlledTalker: armed on the client by Request_SetTransmitMode, the client's copy opens
//     its transmit with no Start request, once, and the host hears it.
//   OpenMic_NeverOpensOnACopyThisMachineDoesNotControl: a talker whose Spec is OpenMic is armed on every copy; the
//     client's opens once possessed, the host's never asks its source.
//   OpenMic_ConvergesWhenControlArrivesLate: armed before possession, the talker waits; possession opens it.
//   OpenMic_ExplicitStopStandsUntilReArmed: a Stop after the open mic opened stands; setting OpenMic again re-opens.
//   OpenMic_ClosesWhenControlIsLost: an unpossess closes the open mic's transmit and re-arms the talker.
//
// Single world, bare talkers (no bridged actor, so the open mic never opens them) with an explicit Start:
//   LeavingOpenMic_ClosesTheTransmit: OpenMic -> PushToTalk and OpenMic -> Disabled close the transmit;
//     PushToTalk -> OpenMic keeps it.
//   PushToTalk_BypassesTheVoiceGate: a tone under the Spec threshold is encoded from its first frame in PushToTalk and
//     not at all in OpenMic.
//
// Surface in Session Frontend: Ck.VoiceChat.Net.OpenMic_* / Ck.VoiceChat.Pipeline.LeavingOpenMic_ClosesTheTransmit /
// Ck.VoiceChat.Pipeline.PushToTalk_BypassesTheVoiceGate

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Macros/CkMacros.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"
#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChat.h"
#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChatPawn.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkVoiceChat/Capture/CkVoiceChat_CaptureSource.h"
#include "CkVoiceChat/Settings/CkVoiceChat_Settings.h"
#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Fragment.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "VoiceModule.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_transmit_mode_spec
{
    constexpr auto SampleRate = 48000;
    constexpr auto WaitSeconds = 15.0;

    struct FTransmitModeSpecState
    {
        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> ClientSource;
        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> HostSource;

        FCk_Handle_VoiceTalker Talker;
        FCk_Handle_VoiceTalker OtherTalker;
        FCk_Handle_VoiceTalker ThirdTalker;

        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> Source;
        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> OtherSource;
        TSharedPtr<FCk_VoiceChat_CaptureSource_Fake> ThirdSource;

        int64 VoiceEnsuresBefore = 0;

        bool HadOriginalVoiceKey = false;
        bool OriginalVoiceEnabled = false;
    };

    using FStateRef = TSharedRef<FTransmitModeSpecState, ESPMode::ThreadSafe>;

    auto EnableEngineVoice(FTransmitModeSpecState& InState) -> void
    {
        InState.HadOriginalVoiceKey = GConfig->GetBool(TEXT("Voice"), TEXT("bEnabled"), InState.OriginalVoiceEnabled, GEngineIni);
        GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), true, GEngineIni);

        if (FModuleManager::Get().IsModuleLoaded(TEXT("Voice")) && NOT FVoiceModule::Get().IsVoiceEnabled())
        {
            FModuleManager::Get().UnloadModule(TEXT("Voice"));
        }
    }

    auto RestoreEngineVoice(const FTransmitModeSpecState& InState) -> void
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

    auto Make_LoudTone() -> TSharedPtr<FCk_VoiceChat_CaptureSource_Fake>
    {
        auto Source = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
        Source->Enqueue_Sine(FCk_Time{20.0f}, 0.4f, 300.0f);
        return Source;
    }

    auto Add_Teardown(FAutomationTestBase* InTest, const FStateRef& InState) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([InState]() -> bool
            {
                RestoreEngineVoice(*InState);
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("restore log suppression and the Voice config")));
    }

    // ---- Open-mic fixture: the client-possessable pawn talker and the host's ears ------------------------------------

    auto Find_Pawn(UWorld* InWorld) -> ACk_AutoTest_NetSubject_VoiceChatPawn_UE*
    {
        if (InWorld == nullptr)
        { return nullptr; }

        for (TActorIterator<ACk_AutoTest_NetSubject_VoiceChatPawn_UE> It{InWorld}; It; ++It)
        { return *It; }

        return nullptr;
    }

    auto Find_Ears(UWorld* InWorld) -> ACk_AutoTest_NetSubject_VoiceChat_UE*
    {
        if (InWorld == nullptr)
        { return nullptr; }

        for (TActorIterator<ACk_AutoTest_NetSubject_VoiceChat_UE> It{InWorld}; It; ++It)
        { return *It; }

        return nullptr;
    }

    auto Get_PawnTalker(UWorld* InWorld) -> FCk_Handle_VoiceTalker
    {
        const auto* Pawn = Find_Pawn(InWorld);
        return Pawn != nullptr ? Pawn->_TestTalker : FCk_Handle_VoiceTalker{};
    }

    auto Get_ClientTalker() -> FCk_Handle_VoiceTalker
    {
        return Get_PawnTalker(ck::auto_test::net::Get_ClientWorld(0));
    }

    auto Get_HostTalker() -> FCk_Handle_VoiceTalker
    {
        return Get_PawnTalker(ck::auto_test::net::Get_ServerWorld());
    }

    auto Get_IsClientPawnLocallyControlled() -> bool
    {
        auto* ClientPawn = Find_Pawn(ck::auto_test::net::Get_ClientWorld(0));
        return ClientPawn != nullptr &&
            UCk_Utils_Net_UE::Get_IsActorLocallyControlled_ByPlayer(ClientPawn) ==
            ECk_Utils_Net_IsLocallyControlled_Result::IsLocallyControlled;
    }

    auto Get_IsClientTalkerAMember() -> bool
    {
        auto* ClientPawn = Find_Pawn(ck::auto_test::net::Get_ClientWorld(0));
        return ClientPawn != nullptr && ck::IsValid(ClientPawn->_TestChannel) && ck::IsValid(ClientPawn->_TestTalker) &&
            UCk_Utils_VoiceChannel_UE::Get_IsMember(ClientPawn->_TestChannel, ClientPawn->_TestTalker);
    }

    auto Get_HostDecodedBytes() -> int32
    {
        auto HostTalker = Get_HostTalker();
        return ck::IsValid(HostTalker) ? UCk_Utils_VoiceTalker_UE::Debug_Get_LoopbackDecodedPcm(HostTalker).Num() : 0;
    }

    auto Get_IsOpenMicPending(const FCk_Handle_VoiceTalker& InTalker) -> bool
    {
        return InTalker.Has<ck::FTag_VoiceTalker_OpenMicPending>();
    }

    // Starts PIE (host + one client), spawns the pawn (InPawnClass) and the host-owned ears, and waits until both
    // copies of the pawn talker exist.
    auto Add_OpenMicFixture(
        FAutomationTestBase* InTest,
        const FStateRef& InState,
        UClass* InPawnClass) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool { return ck::auto_test::net::Get_AllClientsHaveLocalPlayerState(); }),
            WaitSeconds, TEXT("the client's local PlayerState replicated")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InState, InPawnClass](UWorld* InServer) -> void
            {
                EnableEngineVoice(*InState);
                InState->VoiceEnsuresBefore = Get_VoiceEnsureCount();

                auto SpawnInfo = FActorSpawnParameters{};
                SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

                if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoiceChatPawn_UE>(InPawnClass, FTransform::Identity, SpawnInfo) == nullptr)
                { InTest->AddError(TEXT("spawn of the voice-chat pawn failed")); }

                SpawnInfo.Owner = InServer->GetFirstPlayerController();
                if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoiceChat_UE>(
                        ACk_AutoTest_NetSubject_VoiceChat_UE::StaticClass(), FTransform::Identity, SpawnInfo) == nullptr)
                { InTest->AddError(TEXT("spawn of the host-owned ears subject failed")); }
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                const auto* Ears = Find_Ears(ck::auto_test::net::Get_ServerWorld());
                return ck::IsValid(Get_HostTalker()) && ck::IsValid(Get_ClientTalker()) &&
                    Ears != nullptr && ck::IsValid(Ears->_TestTalker);
            }), WaitSeconds, TEXT("the pawn talker exists on both machines and the host's ears exist")));
    }

    // On the server: the pawn's talker and the host's ears join the pawn's channel.
    auto Join_PawnChannel(FAutomationTestBase* InTest, UWorld* InServer) -> void
    {
        auto* Pawn = Find_Pawn(InServer);
        auto* Ears = Find_Ears(InServer);
        if (Pawn == nullptr || Ears == nullptr)
        {
            InTest->AddError(TEXT("pawn or ears missing on the server at join time"));
            return;
        }

        UCk_Utils_VoiceChannel_UE::Request_Join(Pawn->_TestChannel, FCk_Request_VoiceChannel_Join{Pawn->_TestTalker}, {});
        UCk_Utils_VoiceChannel_UE::Request_Join(Pawn->_TestChannel, FCk_Request_VoiceChannel_Join{Ears->_TestTalker}, {});
    }

    auto Possess_ByClient(FAutomationTestBase* InTest, UWorld* InServer) -> void
    {
        auto* Pawn = Find_Pawn(InServer);
        auto* ClientController = ck::auto_test::net::Get_RemoteClientPlayerController(InServer, 0);
        if (Pawn == nullptr || ClientController == nullptr)
        {
            InTest->AddError(TEXT("pawn or the client's PlayerController missing on the server at possession time"));
            return;
        }

        ClientController->Possess(Pawn);
    }

    // Possession and membership, then the wait until the client both controls the pawn and knows its talker is a
    // member (without membership the client's frames have nowhere to go).
    auto Add_PossessAndJoin(FAutomationTestBase* InTest) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest](UWorld* InServer) -> void
            {
                Join_PawnChannel(InTest, InServer);
                Possess_ByClient(InTest, InServer);
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                return Get_IsClientPawnLocallyControlled() && Get_IsClientTalkerAMember();
            }), WaitSeconds, TEXT("the client controls the pawn and knows its talker is a channel member")));
    }

    // On the client: a tone source on the client's copy, then the arming.
    auto Add_ArmOnClientWithTone(const FStateRef& InState) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda([InState](UWorld* InClient) -> void
            {
                auto Talker = Get_PawnTalker(InClient);

                InState->ClientSource = Make_LoudTone();
                UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(Talker, InState->ClientSource);
                UCk_Utils_VoiceTalker_UE::Request_SetTransmitMode(Talker, ECk_VoiceChat_TransmitMode::OpenMic, {});
            })));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_OpenMic_StartsOnTheLocallyControlledTalker,
    "Ck.VoiceChat.Net.OpenMic_StartsOnTheLocallyControlledTalker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_OpenMic_StartsOnTheLocallyControlledTalker::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_mode_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto State = MakeShared<FTransmitModeSpecState, ESPMode::ThreadSafe>();

    Add_OpenMicFixture(this, State, ACk_AutoTest_NetSubject_VoiceChatPawn_UE::StaticClass());
    Add_PossessAndJoin(this);
    Add_ArmOnClientWithTone(State);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool { return Get_HostDecodedBytes() > 0; }),
        WaitSeconds, TEXT("the host decoded the client's open mic")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();

            TestTrue(TEXT("the client's copy is transmitting with no Start request from anyone"),
                UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(ClientTalker));
            TestTrue(TEXT("and the open mic owns that transmit"),
                ClientTalker.Get<ck::FFragment_VoiceTalker_Capture>().Get_OpenedByOpenMic());
            TestEqual(TEXT("the open mic asked the source once"), State->ClientSource->Get_StartCount(), 1);
            TestFalse(TEXT("the arming was consumed"), Get_IsOpenMicPending(ClientTalker));

            TestTrue(TEXT("the host's copy (PushToTalk there, never armed) never attempted a capture"),
                UCk_Utils_VoiceTalker_UE::Get_CaptureStartResult(Get_HostTalker()) ==
                ECk_VoiceChat_CaptureStartResult::NotAttempted);
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("the open mic starts on the locally controlled talker")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_OpenMic_NeverOpensOnACopyThisMachineDoesNotControl,
    "Ck.VoiceChat.Net.OpenMic_NeverOpensOnACopyThisMachineDoesNotControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_OpenMic_NeverOpensOnACopyThisMachineDoesNotControl::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_mode_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto State = MakeShared<FTransmitModeSpecState, ESPMode::ThreadSafe>();

    // The Spec arms every copy at Add, so both copies get a source before anyone possesses the pawn.
    Add_OpenMicFixture(this, State, ACk_AutoTest_NetSubject_VoiceChatOpenMicPawn_UE::StaticClass());

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InClient) -> void
        {
            auto Talker = Get_PawnTalker(InClient);
            State->ClientSource = Make_LoudTone();
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(Talker, State->ClientSource);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            auto Talker = Get_PawnTalker(InServer);
            State->HostSource = Make_LoudTone();
            UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(Talker, State->HostSource);
        })));

    Add_PossessAndJoin(this);

    // Positive control: the client's copy opened once it was controlled. By then the host has run many frames with the
    // pawn possessed by the client, so its own copy has been evaluated against that possession.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();
            return ck::IsValid(ClientTalker) && UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(ClientTalker) &&
                Get_HostDecodedBytes() > 0;
        }), WaitSeconds, TEXT("the client's copy opened its open mic and the host heard it")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto HostTalker = Get_HostTalker();

            TestEqual(TEXT("control: the client's copy asked its source once"), State->ClientSource->Get_StartCount(), 1);

            TestTrue(TEXT("the host's copy never attempted a capture"),
                UCk_Utils_VoiceTalker_UE::Get_CaptureStartResult(HostTalker) == ECk_VoiceChat_CaptureStartResult::NotAttempted);
            TestEqual(TEXT("and never asked its source"), State->HostSource->Get_StartCount(), 0);
            TestFalse(TEXT("and is not transmitting"), UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(HostTalker));
            TestTrue(TEXT("it is still armed, waiting for a control this machine does not have"),
                Get_IsOpenMicPending(HostTalker));
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("the open mic never opens on a copy this machine does not control")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_OpenMic_ConvergesWhenControlArrivesLate,
    "Ck.VoiceChat.Net.OpenMic_ConvergesWhenControlArrivesLate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_OpenMic_ConvergesWhenControlArrivesLate::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_mode_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto State = MakeShared<FTransmitModeSpecState, ESPMode::ThreadSafe>();

    Add_OpenMicFixture(this, State, ACk_AutoTest_NetSubject_VoiceChatPawn_UE::StaticClass());

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void { Join_PawnChannel(this, InServer); })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool { return Get_IsClientTalkerAMember(); }),
        WaitSeconds, TEXT("the client knows its talker is a channel member")));

    Add_ArmOnClientWithTone(State);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();
            return UCk_Utils_VoiceTalker_UE::Get_TransmitMode(ClientTalker) == ECk_VoiceChat_TransmitMode::OpenMic &&
                Get_IsOpenMicPending(ClientTalker);
        }), WaitSeconds, TEXT("the client's copy is armed")));

    // A negative ("an uncontrolled talker does not open"): it is already true on arrival, so it cannot be a condition.
    // The settle gives the open mic, which runs every frame on an armed talker, frames to act wrongly in; the arming
    // observed above is the positive that proves it is in place.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();

            TestFalse(TEXT("before possession the armed talker is not controlled here"), Get_IsClientPawnLocallyControlled());
            TestFalse(TEXT("so it has not opened"), UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(ClientTalker));
            TestEqual(TEXT("and has not asked its source"), State->ClientSource->Get_StartCount(), 0);
            TestTrue(TEXT("and is still armed"), Get_IsOpenMicPending(ClientTalker));

            return true;
        }), TEXT("armed, uncontrolled: waiting")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void { Possess_ByClient(this, InServer); })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();
            return UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(ClientTalker) && Get_HostDecodedBytes() > 0;
        }), WaitSeconds, TEXT("possession arrived: the open mic opened and the host heard it")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();

            TestTrue(TEXT("the open mic owns the transmit"),
                ClientTalker.Get<ck::FFragment_VoiceTalker_Capture>().Get_OpenedByOpenMic());
            TestEqual(TEXT("one attempt for the one arming"), State->ClientSource->Get_StartCount(), 1);
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("the open mic converges when control arrives late")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_OpenMic_ExplicitStopStandsUntilReArmed,
    "Ck.VoiceChat.Net.OpenMic_ExplicitStopStandsUntilReArmed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_OpenMic_ExplicitStopStandsUntilReArmed::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_mode_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto State = MakeShared<FTransmitModeSpecState, ESPMode::ThreadSafe>();

    Add_OpenMicFixture(this, State, ACk_AutoTest_NetSubject_VoiceChatPawn_UE::StaticClass());
    Add_PossessAndJoin(this);
    Add_ArmOnClientWithTone(State);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Get_ClientTalker());
        }), WaitSeconds, TEXT("the open mic opened")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InClient) -> void
        {
            auto Talker = Get_PawnTalker(InClient);
            UCk_Utils_VoiceTalker_UE::Request_StopTransmit(Talker, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return NOT UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Get_ClientTalker());
        }), WaitSeconds, TEXT("the explicit Stop closed it")));

    // A negative ("the open mic does not reopen it"): already true on arrival, so it cannot be a condition. The talker
    // is still OpenMic and still controlled here, so a wrong open mic would reopen it in the next frame; the settle gives
    // it frames to do so, and the open + stop observed above are the positives.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();

            TestTrue(TEXT("the talker is still OpenMic"),
                UCk_Utils_VoiceTalker_UE::Get_TransmitMode(ClientTalker) == ECk_VoiceChat_TransmitMode::OpenMic);
            TestTrue(TEXT("and still controlled here"), Get_IsClientPawnLocallyControlled());
            TestFalse(TEXT("the Stop stands: not transmitting"), UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(ClientTalker));
            TestEqual(TEXT("the source was asked only by the first open"), State->ClientSource->Get_StartCount(), 1);
            TestFalse(TEXT("nothing is armed"), Get_IsOpenMicPending(ClientTalker));

            return true;
        }), TEXT("the explicit Stop stands")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InClient) -> void
        {
            auto Talker = Get_PawnTalker(InClient);
            UCk_Utils_VoiceTalker_UE::Request_SetTransmitMode(Talker, ECk_VoiceChat_TransmitMode::OpenMic, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Get_ClientTalker()) &&
                State->ClientSource->Get_StartCount() == 2;
        }), WaitSeconds, TEXT("OpenMic -> OpenMic re-armed it and the open mic reopened it")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);
            return true;
        }), TEXT("re-armed")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_OpenMic_ClosesWhenControlIsLost,
    "Ck.VoiceChat.Net.OpenMic_ClosesWhenControlIsLost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_OpenMic_ClosesWhenControlIsLost::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_mode_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto State = MakeShared<FTransmitModeSpecState, ESPMode::ThreadSafe>();

    Add_OpenMicFixture(this, State, ACk_AutoTest_NetSubject_VoiceChatPawn_UE::StaticClass());
    Add_PossessAndJoin(this);
    Add_ArmOnClientWithTone(State);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Get_ClientTalker());
        }), WaitSeconds, TEXT("the open mic opened")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* ClientController = ck::auto_test::net::Get_RemoteClientPlayerController(InServer, 0);
            if (ClientController == nullptr)
            {
                AddError(TEXT("the client's PlayerController missing on the server at unpossess time"));
                return;
            }

            ClientController->UnPossess();
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return NOT UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(Get_ClientTalker());
        }), WaitSeconds, TEXT("the unpossess closed the open mic")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto ClientTalker = Get_ClientTalker();

            TestFalse(TEXT("the client no longer controls the pawn"), Get_IsClientPawnLocallyControlled());
            TestTrue(TEXT("the talker is re-armed for the next possession"), Get_IsOpenMicPending(ClientTalker));
            TestFalse(TEXT("and no longer owned by the open mic"),
                ClientTalker.Get<ck::FFragment_VoiceTalker_Capture>().Get_OpenedByOpenMic());
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("losing control closes the open mic and re-arms it")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_transmit_mode_spec
{
    auto Add_BareTalker(UWorld* InWorld, ECk_VoiceChat_TransmitMode InMode) -> FCk_Handle_VoiceTalker
    {
        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld);
        auto TalkerEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);

        auto Spec = FCk_VoiceTalker_Spec{};
        Spec.Set_TransmitMode(InMode);
        Spec.Set_InputGain(1.0f);

        return UCk_Utils_VoiceTalker_UE::Add(TalkerEntity, Spec);
    }

    auto Start_WithSource(
        FCk_Handle_VoiceTalker& InTalker,
        const TSharedPtr<FCk_VoiceChat_CaptureSource_Fake>& InSource) -> void
    {
        UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(InTalker, InSource);
        UCk_Utils_VoiceTalker_UE::Request_StartTransmit(InTalker, {}, {});
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatPipeline_LeavingOpenMic_ClosesTheTransmit,
    "Ck.VoiceChat.Pipeline.LeavingOpenMic_ClosesTheTransmit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatPipeline_LeavingOpenMic_ClosesTheTransmit::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_mode_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto State = MakeShared<FTransmitModeSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();

            State->Talker = Add_BareTalker(InServer, ECk_VoiceChat_TransmitMode::OpenMic);
            State->Source = Make_LoudTone();
            Start_WithSource(State->Talker, State->Source);

            State->OtherTalker = Add_BareTalker(InServer, ECk_VoiceChat_TransmitMode::OpenMic);
            State->OtherSource = Make_LoudTone();
            Start_WithSource(State->OtherTalker, State->OtherSource);

            State->ThirdTalker = Add_BareTalker(InServer, ECk_VoiceChat_TransmitMode::PushToTalk);
            State->ThirdSource = Make_LoudTone();
            Start_WithSource(State->ThirdTalker, State->ThirdSource);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Talker) && UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->Talker) &&
                ck::IsValid(State->OtherTalker) && UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->OtherTalker) &&
                ck::IsValid(State->ThirdTalker) && UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->ThirdTalker);
        }), WaitSeconds, TEXT("all three talkers transmit")));

    // All three requests are drained in the same pass, so by the time the first two have closed the third has been
    // handled too.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            UCk_Utils_VoiceTalker_UE::Request_SetTransmitMode(State->Talker, ECk_VoiceChat_TransmitMode::PushToTalk, {});
            UCk_Utils_VoiceTalker_UE::Request_SetTransmitMode(State->OtherTalker, ECk_VoiceChat_TransmitMode::Disabled, {});
            UCk_Utils_VoiceTalker_UE::Request_SetTransmitMode(State->ThirdTalker, ECk_VoiceChat_TransmitMode::OpenMic, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return NOT UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->Talker) &&
                NOT UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->OtherTalker);
        }), WaitSeconds, TEXT("leaving OpenMic closed both transmits")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestTrue(TEXT("OpenMic -> PushToTalk: the mode changed"),
                UCk_Utils_VoiceTalker_UE::Get_TransmitMode(State->Talker) == ECk_VoiceChat_TransmitMode::PushToTalk);
            TestFalse(TEXT("OpenMic -> PushToTalk: not speaking"), UCk_Utils_VoiceTalker_UE::Get_IsSpeaking(State->Talker));

            TestTrue(TEXT("OpenMic -> Disabled: the mode changed"),
                UCk_Utils_VoiceTalker_UE::Get_TransmitMode(State->OtherTalker) == ECk_VoiceChat_TransmitMode::Disabled);
            TestFalse(TEXT("OpenMic -> Disabled: not speaking"), UCk_Utils_VoiceTalker_UE::Get_IsSpeaking(State->OtherTalker));

            TestTrue(TEXT("control, PushToTalk -> OpenMic: the mode changed"),
                UCk_Utils_VoiceTalker_UE::Get_TransmitMode(State->ThirdTalker) == ECk_VoiceChat_TransmitMode::OpenMic);
            TestTrue(TEXT("control, PushToTalk -> OpenMic: the caller's transmit is kept"),
                UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->ThirdTalker));
            TestFalse(TEXT("control, PushToTalk -> OpenMic while transmitting arms nothing"),
                Get_IsOpenMicPending(State->ThirdTalker));

            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("leaving OpenMic closes the transmit")));

    Add_Teardown(this, State);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatPipeline_PushToTalk_BypassesTheVoiceGate,
    "Ck.VoiceChat.Pipeline.PushToTalk_BypassesTheVoiceGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatPipeline_PushToTalk_BypassesTheVoiceGate::RunTest(const FString& Parameters)
{
    using namespace ck_voice_transmit_mode_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto State = MakeShared<FTransmitModeSpecState, ESPMode::ThreadSafe>();

    // RMS of a 0.02-amplitude sine at gain 1.0 is ~0.014, far under the Spec's default 0.07 gate.
    constexpr auto QuietAmplitude = 0.02f;

    // Half a second is 25 times the gate's 20 ms attack: a gate that could open on this tone would have.
    constexpr auto HalfASecondOfSamples = static_cast<uint64>(SampleRate / 2);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State, QuietAmplitude](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();

            State->Talker = Add_BareTalker(InServer, ECk_VoiceChat_TransmitMode::PushToTalk);
            State->Source = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->Source->Enqueue_Sine(FCk_Time{10.0f}, QuietAmplitude, 300.0f);
            Start_WithSource(State->Talker, State->Source);

            State->OtherTalker = Add_BareTalker(InServer, ECk_VoiceChat_TransmitMode::OpenMic);
            State->OtherSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            State->OtherSource->Enqueue_Sine(FCk_Time{10.0f}, QuietAmplitude, 300.0f);
            Start_WithSource(State->OtherTalker, State->OtherSource);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State, HalfASecondOfSamples]() -> bool
        {
            return ck::IsValid(State->Talker) && ck::IsValid(State->OtherTalker) &&
                State->Talker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq() > 0 &&
                State->Source->Get_SamplesEmitted() >= HalfASecondOfSamples &&
                State->OtherSource->Get_SamplesEmitted() >= HalfASecondOfSamples;
        }), WaitSeconds, TEXT("the PushToTalk talker encoded and both captured half a second of the quiet tone")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            const auto FrameSamples = static_cast<uint64>(
                UCk_Utils_VoiceChat_Settings_UE::Get_SampleRate() * UCk_Utils_VoiceChat_Settings_UE::Get_FrameSizeMs() / 1000);

            // The source emits only when the capture processor drains it, and that pass encodes every whole frame it
            // drained, so between frames the encoded count is exactly the whole frames captured.
            TestEqual(TEXT("PushToTalk: every captured frame was encoded, from the first one"),
                static_cast<uint64>(State->Talker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq()),
                State->Source->Get_SamplesEmitted() / FrameSamples);
            TestTrue(TEXT("PushToTalk: the talker is speaking while it transmits"),
                UCk_Utils_VoiceTalker_UE::Get_IsSpeaking(State->Talker));

            TestEqual(TEXT("OpenMic: the same tone under the gate encoded nothing"),
                static_cast<int32>(State->OtherTalker.Get<ck::FFragment_VoiceTalker_Capture>().Get_Seq()), 0);
            TestFalse(TEXT("OpenMic: and the talker is not speaking"), UCk_Utils_VoiceTalker_UE::Get_IsSpeaking(State->OtherTalker));
            TestTrue(TEXT("OpenMic: it is transmitting all the same"), UCk_Utils_VoiceTalker_UE::Get_IsTransmitting(State->OtherTalker));

            TestFalse(TEXT("a bare OpenMic talker (no bridged actor) is disarmed, not left waiting"),
                Get_IsOpenMicPending(State->OtherTalker));
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("push-to-talk bypasses the voice gate")));

    Add_Teardown(this, State);

    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
