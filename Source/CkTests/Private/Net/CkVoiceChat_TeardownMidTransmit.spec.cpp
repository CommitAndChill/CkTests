// P3 teardown gate (amendment S4 / spec §7.8): destroying a talker's whole subject - actor,
// bridged entity, channel child, relay-addressed streams - MID-TRANSMIT must tear down cleanly:
// voice was flowing (the client had decoded audio), the destroy lands, the replica disappears on
// the client, and both worlds keep ticking to a clean EndPIE. Voice is disposable; nothing may
// stash, hang, or storm when its subject dies mid-stream.
//
// Surface in Session Frontend: Ck.VoiceChat.Net.TeardownMidTransmit

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"

#include "CkVoiceChat/Capture/CkVoiceChat_CaptureSource.h"
#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChat.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "VoiceModule.h"

#include <GameFramework/PlayerController.h>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_teardown_spec
{
    constexpr auto SampleRate = 48000;

    struct FTeardownSpecState
    {
        bool ClientHadDecodedAudioBeforeDestroy = false;

        bool HadOriginalVoiceKey = false;
        bool OriginalVoiceEnabled = false;
    };

    auto EnableEngineVoice(FTeardownSpecState& InState) -> void
    {
        InState.HadOriginalVoiceKey = GConfig->GetBool(TEXT("Voice"), TEXT("bEnabled"), InState.OriginalVoiceEnabled, GEngineIni);
        GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), true, GEngineIni);

        if (FModuleManager::Get().IsModuleLoaded(TEXT("Voice")) && NOT FVoiceModule::Get().IsVoiceEnabled())
        {
            FModuleManager::Get().UnloadModule(TEXT("Voice"));
        }
    }

    auto RestoreEngineVoice(const FTeardownSpecState& InState) -> void
    {
        if (InState.HadOriginalVoiceKey)
        { GConfig->SetBool(TEXT("Voice"), TEXT("bEnabled"), InState.OriginalVoiceEnabled, GEngineIni); }
        else
        { GConfig->RemoveKey(TEXT("Voice"), TEXT("bEnabled"), GEngineIni); }
    }

    auto Find_Subject(UWorld* InWorld, bool InSpeaker) -> ACk_AutoTest_NetSubject_VoiceChat_UE*
    {
        for (TActorIterator<ACk_AutoTest_NetSubject_VoiceChat_UE> It{InWorld}; It; ++It)
        {
            const auto IsSpeaker = ck::auto_test::net::Get_NumClientWorlds() > 1
                ? It->GetActorLocation().X < 500.0 : It->GetOwner() == nullptr;
            if (IsSpeaker == InSpeaker)
            { return *It; }
        }

        return nullptr;
    }

    auto Find_RemotePlayerController(UWorld* InServer) -> APlayerController*
    { return ck::auto_test::net::Get_RemoteClientPlayerController(InServer, 0); }
}

// --------------------------------------------------------------------------------------------------------------------

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkVoiceChatNet_TeardownMidTransmit,
    "Ck.VoiceChat.Net.TeardownMidTransmit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_TeardownMidTransmit::RunTest(const FString& Parameters)
{
    using namespace ck_voice_teardown_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto DedicatedServer = Parameters == TEXT("ClientServer");
    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FTeardownSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(DedicatedServer ? 3 : 2, MapPath, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(DedicatedServer ? 3 : 2, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([DedicatedServer]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            return ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0) != nullptr &&
                (NOT DedicatedServer ||
                    ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1) != nullptr);
        }), 45.0, TEXT("listener and speaker server controllers ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State, DedicatedServer](UWorld* InServer) -> void
        {
            EnableEngineVoice(*State);

            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            if (DedicatedServer)
            {
                SpawnInfo.Owner = ck::auto_test::net::Get_RemoteClientPlayerController(InServer, 1);
                if (SpawnInfo.Owner == nullptr)
                { AddError(TEXT("speaker client 1 has no server controller")); return; }
            }
            if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoiceChat_UE>(
                    ACk_AutoTest_NetSubject_VoiceChat_UE::StaticClass(), FTransform::Identity, SpawnInfo) == nullptr)
            { AddError(TEXT("spawn of the speaker subject failed")); }

            auto* RemoteController = Find_RemotePlayerController(InServer);
            if (RemoteController == nullptr)
            {
                AddError(TEXT("no remote PlayerController on the server"));
                return;
            }

            SpawnInfo.Owner = RemoteController;
            if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoiceChat_UE>(
                    ACk_AutoTest_NetSubject_VoiceChat_UE::StaticClass(),
                    DedicatedServer ? FTransform{FVector{1000.0, 0.0, 0.0}} : FTransform::Identity, SpawnInfo) == nullptr)
            { AddError(TEXT("spawn of the listener subject failed")); }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(40));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, DedicatedServer](UWorld* InServer) -> void
        {
            auto* Speaker = Find_Subject(InServer, true);
            auto* Listener = Find_Subject(InServer, false);
            if (Speaker == nullptr || Listener == nullptr)
            {
                AddError(TEXT("subjects missing on the server at join time"));
                return;
            }

            UCk_Utils_VoiceChannel_UE::Request_Join(Speaker->_TestChannel,
                FCk_Request_VoiceChannel_Join{Speaker->_TestTalker}, {});
            UCk_Utils_VoiceChannel_UE::Request_Join(Speaker->_TestChannel,
                FCk_Request_VoiceChannel_Join{Listener->_TestTalker}, {});

            // A LONG tape - the destroy below lands while frames are still flowing.
            const auto FakeSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
            FakeSource->Enqueue_Silence(FCk_Time{0.2f});
            FakeSource->Enqueue_Sine(FCk_Time{5.0f}, 0.4f, 300.0f);

            if (NOT DedicatedServer)
            {
                UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(Speaker->_TestTalker, FakeSource);
                UCk_Utils_VoiceTalker_UE::Request_StartTransmit(Speaker->_TestTalker, {}, {});
            }
        })));

    if (DedicatedServer)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                auto* World = ck::auto_test::net::Get_ClientWorld(1);
                auto* Speaker = World != nullptr ? Find_Subject(World, true) : nullptr;
                return Speaker != nullptr && ck::IsValid(Speaker->_TestChannel) &&
                    ck::IsValid(Speaker->_TestTalker) &&
                    UCk_Utils_VoiceChannel_UE::Get_IsMember(Speaker->_TestChannel, Speaker->_TestTalker);
            }), 10.0, TEXT("speaker channel membership replicated to capture client")));
    }

    if (DedicatedServer)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
            FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InClient) -> void
            {
                auto* Speaker = Find_Subject(InClient, true);
                auto* Owner = Speaker != nullptr ? Cast<APlayerController>(Speaker->GetOwner()) : nullptr;
                if (Speaker == nullptr || ck::Is_NOT_Valid(Speaker->_TestTalker) ||
                    Owner == nullptr || NOT Owner->IsLocalController())
                { AddError(TEXT("client 1 speaker replica/talker/local owner missing")); return; }
                const auto FakeSource = MakeShared<FCk_VoiceChat_CaptureSource_Fake>(SampleRate);
                FakeSource->Enqueue_Silence(FCk_Time{0.2f});
                FakeSource->Enqueue_Sine(FCk_Time{5.0f}, 0.4f, 300.0f);
                UCk_Utils_VoiceTalker_UE::Debug_InjectCaptureSource(Speaker->_TestTalker, FakeSource);
                UCk_Utils_VoiceTalker_UE::Request_StartTransmit(Speaker->_TestTalker, {}, {});
            })));
    }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(80));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* ClientSpeaker = Client != nullptr ? Find_Subject(Client, true) : nullptr;
            if (ClientSpeaker == nullptr || ck::Is_NOT_Valid(ClientSpeaker->_TestTalker))
            {
                AddError(TEXT("client-side speaker replica missing pre-destroy"));
                return false;
            }

            State->ClientHadDecodedAudioBeforeDestroy =
                UCk_Utils_VoiceTalker_UE::Debug_Get_LoopbackDecodedPcm(ClientSpeaker->_TestTalker).Num() > 0;

            TestTrue(TEXT("voice was flowing before the destroy (the teardown is genuinely mid-transmit)"),
                State->ClientHadDecodedAudioBeforeDestroy);

            return true;
        }),
        TEXT("mid-transmit baseline")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* Speaker = Find_Subject(InServer, true);
            if (Speaker == nullptr)
            {
                AddError(TEXT("speaker subject missing at destroy time"));
                return;
            }

            // The realistic teardown shape (a player leaves): destroy the whole subject actor
            // mid-stream - its bridged entity, talker, channel child, and memberships cascade.
            Speaker->Destroy();
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(80));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            RestoreEngineVoice(*State);

            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            if (Server == nullptr || Client == nullptr)
            {
                AddError(TEXT("worlds unavailable post-destroy"));
                return false;
            }

            TestTrue(TEXT("the speaker subject is gone on the server"), Find_Subject(Server, true) == nullptr);
            TestTrue(TEXT("the speaker replica is gone on the client (teardown replicated cleanly)"),
                Find_Subject(Client, true) == nullptr);

            // The listener side survived the mid-stream teardown of everything it was hearing.
            auto* ClientListener = Find_Subject(Client, false);
            TestTrue(TEXT("the listener subject is alive and well on the client"),
                ClientListener != nullptr && ck::IsValid(ClientListener->_TestTalker));

            return true;
        }),
        TEXT("clean mid-transmit teardown")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
        {
            FAutomationTestBase::bSuppressLogErrors = false;
            FAutomationTestBase::bSuppressLogWarnings = false;
            return true;
        }),
        TEXT("restore log suppression statics")));

    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
