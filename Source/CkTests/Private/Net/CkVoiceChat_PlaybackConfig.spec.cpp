// P4 playback-config seams (Gate_4 items 2/6/7 machine halves), one flow:
//
//   1. Selection: bundles delivered on a channel set the recipient-side talker's
//      _PlaybackConfigChannel to THAT channel (asserted by wire idx through the debug seam).
//   2. Amplitude release: while bundles arrive the header amplitude mirrors (>0); once the
//      stream idles past the sender's stale-drop age the remote amplitude releases to ZERO -
//      a remote talker never reads as speaking forever.
//   3. EndPlay prune: after the talker's listener-mute and forwards have populated the
//      world-scoped authority maps (entries > 0 proven first), destroying the talker sweeps
//      its entries to ZERO.
//
// The render application half (attenuation/effect chain audibly applied) is [EDITOR-VERIFY].
//
// Ck.VoiceChat.Net.PlaybackSynth_TakesSoundClassVolumeAndAttachTarget reads what client B's synth for talker A was
// given (gate lanes render audio, so the synth exists), on ACk_AutoTest_NetSubject_VoicePlayback_UE subjects:
//   1. The channel's authored SoundClass, resolved on client B, is the synth's SoundClass and reached its audio
//      component.
//   2. The synth's attach parent is client B's copy of the Spec's attach component, at the Spec's socket.
//   3. Client B's listener volume for talker A, set before the first bundle, is on the synth at creation; a later
//      change reaches the synth with no new bundle.
//
// Surface in Session Frontend: Ck.VoiceChat.Net.PlaybackConfig / Ck.VoiceChat.Net.PlaybackSynth_*

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"

#include "CkVoiceChat/Codec/CkVoiceChat_Codec.h"
#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceListener/CkVoiceListener_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChat.h"
#include "CkTests/Net/CkAutoTest_NetSubject_VoicePlayback.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Components/AudioComponent.h>
#include <GameFramework/PlayerController.h>
#include <GameFramework/PlayerState.h>
#include <Sound/SoundClass.h>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_playback_config_spec
{
    const auto TalkerALocation = FVector{100.0, 0.0, 0.0};
    const auto ListenerBLocation = FVector{200.0, 0.0, 0.0};

    struct FPlaybackConfigSpecState
    {
        int32 Client0PlayerId = 0;
        uint8 ChannelIdx = 255;
        uint16 NextSeq = 1;

        // Listener B's server-side talker anchors the post-destroy map reads: talker A's own
        // handle is a tombstone after its destroy and cannot anchor (nor safely key) a lookup.
        FCk_Handle_VoiceTalker ServerAnchorB;
        FCk_Handle_VoiceListener ClientBListener;
    };

    auto Find_SubjectNear(UWorld* InWorld, const FVector& InLocation) -> ACk_AutoTest_NetSubject_VoiceChat_UE*
    {
        for (TActorIterator<ACk_AutoTest_NetSubject_VoiceChat_UE> It{InWorld}; It; ++It)
        {
            if (FVector::Dist(It->GetActorLocation(), InLocation) < 50.0)
            { return *It; }
        }

        return nullptr;
    }

    auto Find_ControllerByPlayerId(UWorld* InServer, int32 InPlayerId) -> APlayerController*
    {
        for (auto It = InServer->GetPlayerControllerIterator(); It; ++It)
        {
            if (auto* Controller = It->Get();
                Controller != nullptr && Controller->PlayerState != nullptr &&
                Controller->PlayerState->GetPlayerId() == InPlayerId)
            { return Controller; }
        }

        return nullptr;
    }

    auto Make_Bundle(uint16 InSeq, uint8 InChannelIdx) -> TArray<uint8>
    {
        auto Frame = TArray<uint8>{};
        Frame.Init(0x42, 60);

        constexpr auto AmplitudeQ8 = uint8{128};
        return ck::voice_chat::codec::Pack_Bundle(
            FCk_VoiceChat_BundleHeader{InSeq, InChannelIdx, AmplitudeQ8}, TArray<TArray<uint8>>{Frame});
    }

    auto Inject_FromTalkerA(FAutomationTestBase* InTest, UWorld* InServer,
        const TSharedRef<FPlaybackConfigSpecState, ESPMode::ThreadSafe>& InState) -> void
    {
        auto* TalkerA = Find_SubjectNear(InServer, TalkerALocation);
        auto* Client0Controller = Find_ControllerByPlayerId(InServer, InState->Client0PlayerId);

        if (TalkerA == nullptr || Client0Controller == nullptr || Client0Controller->PlayerState == nullptr)
        {
            InTest->AddError(TEXT("talker A or client 0's PlayerState missing at inject"));
            return;
        }

        UCk_Utils_VoiceTalker_UE::Debug_InjectInboundBundle(
            TalkerA->_TestTalker, Make_Bundle(InState->NextSeq++, InState->ChannelIdx),
            Client0Controller->PlayerState);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_PlaybackConfig,
    "Ck.VoiceChat.Net.PlaybackConfig",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_PlaybackConfig::RunTest(const FString& Parameters)
{
    using namespace ck_voice_playback_config_spec;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FPlaybackConfigSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool { return ck::auto_test::net::Get_AllClientsHaveLocalPlayerState(); }),
        15.0, TEXT("every client's local PlayerState replicated")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InClient) -> void
        {
            const auto* LocalController = InClient->GetFirstPlayerController();
            if (LocalController == nullptr || LocalController->PlayerState == nullptr)
            {
                AddError(TEXT("client 0 has no local PlayerState"));
                return;
            }

            State->Client0PlayerId = LocalController->PlayerState->GetPlayerId();
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            auto* Client0Controller = Find_ControllerByPlayerId(InServer, State->Client0PlayerId);
            auto* Client1Controller = static_cast<APlayerController*>(nullptr);
            for (auto It = InServer->GetPlayerControllerIterator(); It; ++It)
            {
                if (auto* Controller = It->Get();
                    Controller != nullptr && NOT Controller->IsLocalController() && Controller != Client0Controller)
                { Client1Controller = Controller; }
            }

            if (Client0Controller == nullptr || Client1Controller == nullptr)
            {
                AddError(TEXT("could not resolve both remote PlayerControllers on the server"));
                return;
            }

            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            SpawnInfo.Owner = Client0Controller;
            if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoiceChat_UE>(
                    ACk_AutoTest_NetSubject_VoiceChat_UE::StaticClass(), FTransform{TalkerALocation}, SpawnInfo) == nullptr)
            { AddError(TEXT("spawn of the talker-A subject failed")); }

            SpawnInfo.Owner = Client1Controller;
            if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoiceChat_UE>(
                    ACk_AutoTest_NetSubject_VoiceChat_UE::StaticClass(), FTransform{ListenerBLocation}, SpawnInfo) == nullptr)
            { AddError(TEXT("spawn of the listener-B subject failed")); }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(40));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            auto* TalkerA = Find_SubjectNear(InServer, TalkerALocation);
            auto* ListenerB = Find_SubjectNear(InServer, ListenerBLocation);

            if (TalkerA == nullptr || ListenerB == nullptr)
            {
                AddError(TEXT("subjects missing on the server at join time"));
                return;
            }

            State->ServerAnchorB = ListenerB->_TestTalker;

            UCk_Utils_VoiceChannel_UE::Request_Join(TalkerA->_TestChannel,
                FCk_Request_VoiceChannel_Join{TalkerA->_TestTalker}, {});
            UCk_Utils_VoiceChannel_UE::Request_Join(TalkerA->_TestChannel,
                FCk_Request_VoiceChannel_Join{ListenerB->_TestTalker}, {});

            State->ChannelIdx = UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(TalkerA->_TestChannel);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    // Act 1 - selection + amplitude mirror while the stream flows.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            if (State->ChannelIdx == 255)
            {
                AddError(TEXT("channel has no assigned idx - control plane did not settle"));
                return;
            }

            Inject_FromTalkerA(this, InServer, State);
        })));

    // The mirror is transient BY DESIGN (it releases 0.15s after the last arrival), so a fixed
    // settle races it on slow-tick hosts - poll for the >0 window instead (the exit sweep's
    // first run caught exactly that race).
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* ClientB = ck::auto_test::net::Get_ClientWorld(1);
            auto* ClientTalkerA = ClientB != nullptr ? Find_SubjectNear(ClientB, TalkerALocation) : nullptr;

            return ClientTalkerA != nullptr && ck::IsValid(ClientTalkerA->_TestTalker) &&
                UCk_Utils_VoiceTalker_UE::Get_CurrentAmplitude(ClientTalkerA->_TestTalker) > 0.0f;
        }),
        10.0,
        TEXT("amplitude mirrors the header while the stream flows")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            auto* ClientB = ck::auto_test::net::Get_ClientWorld(1);
            auto* ClientTalkerA = ClientB != nullptr ? Find_SubjectNear(ClientB, TalkerALocation) : nullptr;

            if (ClientTalkerA == nullptr || ck::Is_NOT_Valid(ClientTalkerA->_TestTalker))
            {
                AddError(TEXT("talker-A replica missing in client B's world at act 1"));
                return false;
            }

            const auto ConfigChannel = UCk_Utils_VoiceTalker_UE::Debug_Get_PlaybackConfigChannel(
                ClientTalkerA->_TestTalker);

            TestTrue(TEXT("selection: the delivering channel was selected"), ck::IsValid(ConfigChannel));

            if (ck::IsValid(ConfigChannel))
            {
                TestEqual(TEXT("selection: the selected channel carries the delivering wire idx"),
                    UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(ConfigChannel), State->ChannelIdx);
            }

            return true;
        }),
        TEXT("selection")));

    // Act 2 - the stream idles past the sender's stale-drop age: amplitude releases to zero,
    // the selection persists (config is sticky; silence does not deselect).
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* ClientB = ck::auto_test::net::Get_ClientWorld(1);
            auto* ClientTalkerA = ClientB != nullptr ? Find_SubjectNear(ClientB, TalkerALocation) : nullptr;

            return ClientTalkerA != nullptr && ck::IsValid(ClientTalkerA->_TestTalker) &&
                UCk_Utils_VoiceTalker_UE::Get_CurrentAmplitude(ClientTalkerA->_TestTalker) == 0.0f;
        }),
        10.0,
        TEXT("amplitude releases to zero after the spurt")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            auto* ClientB = ck::auto_test::net::Get_ClientWorld(1);
            auto* ClientTalkerA = ClientB != nullptr ? Find_SubjectNear(ClientB, TalkerALocation) : nullptr;

            if (ClientTalkerA == nullptr || ck::Is_NOT_Valid(ClientTalkerA->_TestTalker))
            {
                AddError(TEXT("talker-A replica missing in client B's world at act 2"));
                return false;
            }

            TestTrue(TEXT("the selection persists through silence"),
                ck::IsValid(UCk_Utils_VoiceTalker_UE::Debug_Get_PlaybackConfigChannel(
                    ClientTalkerA->_TestTalker)));

            return true;
        }),
        TEXT("sticky selection")));

    // Act 3 - prune: B's listener-mute populates the mute matrix, the earlier forwards populated
    // the serve history; both proven non-empty for talker A, then the talker's destroy sweeps
    // its entries to zero.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InClient) -> void
        {
            auto* ClientTalkerA = Find_SubjectNear(InClient, TalkerALocation);
            if (ClientTalkerA == nullptr || ck::Is_NOT_Valid(ClientTalkerA->_TestTalker))
            {
                AddError(TEXT("talker-A replica missing in client B's world at act 3 mute"));
                return;
            }

            auto TransientEntity = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(ClientTalkerA->_TestTalker);
            auto EarsEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
            State->ClientBListener = UCk_Utils_VoiceListener_UE::Add(EarsEntity);

            UCk_Utils_VoiceListener_UE::Request_MuteTalker(State->ClientBListener,
                FCk_Request_VoiceListener_MuteTalker{ClientTalkerA->_TestTalker}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            if (ck::Is_NOT_Valid(State->ServerAnchorB))
            {
                AddError(TEXT("server anchor handle was never captured"));
                return false;
            }

            TestTrue(TEXT("the authority maps track talker A before its destroy (prune precondition)"),
                UCk_Utils_VoiceTalker_UE::Debug_Get_WorldMapTalkerEntryCount(State->ServerAnchorB) > 0);
            return true;
        }),
        TEXT("maps populated pre-destroy")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            auto* TalkerA = Find_SubjectNear(InServer, TalkerALocation);
            if (TalkerA == nullptr)
            {
                AddError(TEXT("talker A missing at destroy"));
                return;
            }

            TalkerA->Destroy();
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestEqual(TEXT("the EndPlay sweep pruned every entry naming the destroyed talker"),
                UCk_Utils_VoiceTalker_UE::Debug_Get_WorldMapTalkerEntryCount(State->ServerAnchorB), 0);
            return true;
        }),
        TEXT("maps pruned post-destroy")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_playback_synth_spec
{
    using namespace ck_voice_playback_config_spec;

    constexpr auto VolumeAtCreation = 0.5f;
    constexpr auto VolumeLater = 0.25f;
    constexpr auto WaitSeconds = 15.0;

    struct FPlaybackSynthSpecState
    {
        int32 Client0PlayerId = 0;
        uint8 ChannelIdx = 255;
        uint16 NextSeq = 1;

        FCk_Handle_VoiceListener ClientBListener;
    };

    using FStateRef = TSharedRef<FPlaybackSynthSpecState, ESPMode::ThreadSafe>;

    auto Find_PlaybackSubjectNear(UWorld* InWorld, const FVector& InLocation) -> ACk_AutoTest_NetSubject_VoicePlayback_UE*
    {
        if (InWorld == nullptr)
        { return nullptr; }

        for (TActorIterator<ACk_AutoTest_NetSubject_VoicePlayback_UE> It{InWorld}; It; ++It)
        {
            if (FVector::Dist(It->GetActorLocation(), InLocation) < 50.0)
            { return *It; }
        }

        return nullptr;
    }

    // Client B's copy of a subject, with its talker and channel composed.
    auto Find_ClientBSubject(const FVector& InLocation) -> ACk_AutoTest_NetSubject_VoicePlayback_UE*
    {
        auto* Subject = Find_PlaybackSubjectNear(ck::auto_test::net::Get_ClientWorld(1), InLocation);

        if (Subject == nullptr || ck::Is_NOT_Valid(Subject->_TestTalker) || ck::Is_NOT_Valid(Subject->_TestChannel))
        { return nullptr; }

        return Subject;
    }

    auto Get_ClientBSynthForTalkerA() -> UCk_VoiceChatSynthComponent_UE*
    {
        const auto* TalkerA = Find_ClientBSubject(TalkerALocation);
        return TalkerA != nullptr ? UCk_Utils_VoiceTalker_UE::Debug_Get_PlaybackSynth(TalkerA->_TestTalker) : nullptr;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_PlaybackSynth_TakesSoundClassVolumeAndAttachTarget,
    "Ck.VoiceChat.Net.PlaybackSynth_TakesSoundClassVolumeAndAttachTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_PlaybackSynth_TakesSoundClassVolumeAndAttachTarget::RunTest(const FString& Parameters)
{
    using namespace ck_voice_playback_synth_spec;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FPlaybackSynthSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool { return ck::auto_test::net::Get_AllClientsHaveLocalPlayerState(); }),
        WaitSeconds, TEXT("every client's local PlayerState replicated")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InClient) -> void
        {
            const auto* LocalController = InClient->GetFirstPlayerController();
            if (LocalController == nullptr || LocalController->PlayerState == nullptr)
            {
                AddError(TEXT("client 0 has no local PlayerState"));
                return;
            }

            State->Client0PlayerId = LocalController->PlayerState->GetPlayerId();
        })));

    // Talker A is owned by client 0 (the stamped sender of the injected bundles), listener B's talker by client 1.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            auto* Client0Controller = Find_ControllerByPlayerId(InServer, State->Client0PlayerId);
            auto* Client1Controller = static_cast<APlayerController*>(nullptr);
            for (auto It = InServer->GetPlayerControllerIterator(); It; ++It)
            {
                if (auto* Controller = It->Get();
                    Controller != nullptr && NOT Controller->IsLocalController() && Controller != Client0Controller)
                { Client1Controller = Controller; }
            }

            if (Client0Controller == nullptr || Client1Controller == nullptr)
            {
                AddError(TEXT("could not resolve both remote PlayerControllers on the server"));
                return;
            }

            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            SpawnInfo.Owner = Client0Controller;
            if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoicePlayback_UE>(
                    ACk_AutoTest_NetSubject_VoicePlayback_UE::StaticClass(), FTransform{TalkerALocation}, SpawnInfo) == nullptr)
            { AddError(TEXT("spawn of the talker-A subject failed")); }

            SpawnInfo.Owner = Client1Controller;
            if (InServer->SpawnActor<ACk_AutoTest_NetSubject_VoicePlayback_UE>(
                    ACk_AutoTest_NetSubject_VoicePlayback_UE::StaticClass(), FTransform{ListenerBLocation}, SpawnInfo) == nullptr)
            { AddError(TEXT("spawn of the listener-B subject failed")); }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* TalkerA = Find_PlaybackSubjectNear(Server, TalkerALocation);
            const auto* ListenerB = Find_PlaybackSubjectNear(Server, ListenerBLocation);

            return TalkerA != nullptr && ListenerB != nullptr &&
                ck::IsValid(TalkerA->_TestTalker) && ck::IsValid(ListenerB->_TestTalker) &&
                ck::IsValid(TalkerA->_TestChannel) &&
                UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(TalkerA->_TestChannel) != ck::VoiceChannel_UnassignedIdx;
        }),
        WaitSeconds, TEXT("both subjects composed on the server and talker A's channel has its wire idx")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            auto* TalkerA = Find_PlaybackSubjectNear(InServer, TalkerALocation);
            auto* ListenerB = Find_PlaybackSubjectNear(InServer, ListenerBLocation);

            if (TalkerA == nullptr || ListenerB == nullptr)
            {
                AddError(TEXT("subjects missing on the server at join time"));
                return;
            }

            UCk_Utils_VoiceChannel_UE::Request_Join(TalkerA->_TestChannel,
                FCk_Request_VoiceChannel_Join{TalkerA->_TestTalker}, {});
            UCk_Utils_VoiceChannel_UE::Request_Join(TalkerA->_TestChannel,
                FCk_Request_VoiceChannel_Join{ListenerB->_TestTalker}, {});

            State->ChannelIdx = UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(TalkerA->_TestChannel);
        })));

    // The control plane has delivered both memberships to client B, and client B has resolved the channel's SoundClass:
    // the first bundle selects a channel whose config is complete, so the synth is created with it.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            const auto* TalkerA = Find_ClientBSubject(TalkerALocation);
            const auto* ListenerB = Find_ClientBSubject(ListenerBLocation);

            return TalkerA != nullptr && ListenerB != nullptr &&
                UCk_Utils_VoiceChannel_UE::Get_IsMember(TalkerA->_TestChannel, TalkerA->_TestTalker) &&
                UCk_Utils_VoiceChannel_UE::Get_IsMember(TalkerA->_TestChannel, ListenerB->_TestTalker) &&
                UCk_Utils_VoiceChannel_UE::Get_ResolvedSoundClass(TalkerA->_TestChannel) != nullptr;
        }),
        WaitSeconds, TEXT("client B holds both memberships and has resolved the channel's SoundClass")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InClient) -> void
        {
            auto* TalkerA = Find_ClientBSubject(TalkerALocation);
            if (TalkerA == nullptr)
            {
                AddError(TEXT("talker-A replica missing in client B's world at the volume request"));
                return;
            }

            auto TransientEntity = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(TalkerA->_TestTalker);
            auto EarsEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
            State->ClientBListener = UCk_Utils_VoiceListener_UE::Add(EarsEntity);

            UCk_Utils_VoiceListener_UE::Request_SetTalkerVolume(State->ClientBListener,
                FCk_Request_VoiceListener_SetTalkerVolume{TalkerA->_TestTalker, VolumeAtCreation}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            const auto* TalkerA = Find_ClientBSubject(TalkerALocation);

            return TalkerA != nullptr && ck::IsValid(State->ClientBListener) &&
                UCk_Utils_VoiceListener_UE::Get_TalkerVolume(State->ClientBListener, TalkerA->_TestTalker) == VolumeAtCreation;
        }),
        WaitSeconds, TEXT("client B's listener holds its volume for talker A")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestNull(TEXT("no synth exists for talker A before its first bundle (the volume precedes creation)"),
                Get_ClientBSynthForTalkerA());
            return true;
        }),
        TEXT("volume set before the synth exists")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            auto* TalkerA = Find_PlaybackSubjectNear(InServer, TalkerALocation);
            auto* Client0Controller = Find_ControllerByPlayerId(InServer, State->Client0PlayerId);

            if (TalkerA == nullptr || Client0Controller == nullptr || Client0Controller->PlayerState == nullptr)
            {
                AddError(TEXT("talker A or client 0's PlayerState missing at inject"));
                return;
            }

            UCk_Utils_VoiceTalker_UE::Debug_InjectInboundBundle(
                TalkerA->_TestTalker, Make_Bundle(State->NextSeq++, State->ChannelIdx), Client0Controller->PlayerState);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_ClientBSynthForTalkerA() != nullptr;
        }),
        WaitSeconds, TEXT("client B created a playback synth for talker A")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            const auto* TalkerA = Find_ClientBSubject(TalkerALocation);
            auto* Synth = Get_ClientBSynthForTalkerA();

            if (TalkerA == nullptr || Synth == nullptr)
            {
                AddError(TEXT("talker-A replica or its synth missing in client B's world"));
                return false;
            }

            const auto* ResolvedSoundClass = UCk_Utils_VoiceChannel_UE::Get_ResolvedSoundClass(TalkerA->_TestChannel);

            TestEqual(TEXT("the channel resolved its authored SoundClass on client B"),
                GetPathNameSafe(ResolvedSoundClass),
                ACk_AutoTest_NetSubject_VoicePlayback_UE::Get_ChannelSoundClassPath().ToString());
            TestTrue(TEXT("the synth plays through the channel's SoundClass"),
                Synth->SoundClass.Get() == ResolvedSoundClass);

            const auto* AudioComponent = UCk_Utils_VoiceTalker_UE::Debug_Get_PlaybackAudioComponent(TalkerA->_TestTalker);
            if (NOT TestNotNull(TEXT("the synth started an audio component"), AudioComponent))
            { return true; }

            TestTrue(TEXT("the SoundClass reached the synth's audio component"),
                AudioComponent->SoundClassOverride.Get() == ResolvedSoundClass);
            TestEqual(TEXT("the listener's volume for talker A was on the synth at creation"),
                AudioComponent->VolumeMultiplier, VolumeAtCreation);

            const auto* VoiceSocket = TalkerA->_VoiceSocket.Get();
            const auto SocketName = UCk_AutoTest_VoiceSocketComponent_UE::Get_SocketName();

            TestTrue(TEXT("the synth is attached to client B's copy of the Spec's attach component"),
                Synth->GetAttachParent() == VoiceSocket);
            TestEqual(TEXT("the synth is attached at the Spec's socket"), Synth->GetAttachSocketName().ToString(), SocketName.ToString());

            if (VoiceSocket != nullptr)
            {
                TestTrue(TEXT("the synth sits at the socket, not at the component's origin"),
                    Synth->GetComponentLocation().Equals(VoiceSocket->GetSocketLocation(SocketName), 0.1) &&
                    NOT Synth->GetComponentLocation().Equals(VoiceSocket->GetComponentLocation(), 0.1));
            }

            return true;
        }),
        TEXT("the synth was given the channel's SoundClass, the listener's volume and the Spec's attach target")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InClient) -> void
        {
            const auto* TalkerA = Find_ClientBSubject(TalkerALocation);
            if (TalkerA == nullptr || ck::Is_NOT_Valid(State->ClientBListener))
            {
                AddError(TEXT("talker-A replica or client B's listener missing at the volume change"));
                return;
            }

            UCk_Utils_VoiceListener_UE::Request_SetTalkerVolume(State->ClientBListener,
                FCk_Request_VoiceListener_SetTalkerVolume{TalkerA->_TestTalker, VolumeLater}, {});
        })));

    // No bundle follows: the change must reach the synth through the receive pass alone.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            const auto* TalkerA = Find_ClientBSubject(TalkerALocation);
            const auto* AudioComponent = TalkerA != nullptr
                ? UCk_Utils_VoiceTalker_UE::Debug_Get_PlaybackAudioComponent(TalkerA->_TestTalker)
                : nullptr;

            return AudioComponent != nullptr && AudioComponent->VolumeMultiplier == VolumeLater;
        }),
        WaitSeconds, TEXT("a later volume change reaches the synth with no new bundle")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
