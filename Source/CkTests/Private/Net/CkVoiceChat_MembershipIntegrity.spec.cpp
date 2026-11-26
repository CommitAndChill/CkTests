// Membership integrity: a talker that is destroyed stops being a channel member - on the authority promptly (its
// teardown leaves its channels) and on a client through the replicated control plane - and a member whose talker
// exit never ran (an abrupt destroy) is swept by the channel itself before routing can meet it. Routing for the
// talkers that remain then raises no ensure.
//
//   DestroyedTalkerLeavesItsChannels (2 worlds): two subjects join one channel; the second subject is destroyed;
//     the channel holds only the first talker on the server AND on the client.
//   DestroyedTalkerNeverReachesRouting (1 world): after a member talker is destroyed, the remaining member's
//     bundle routes with no ensure anywhere. Every ensure site hit in the routing window is printed, which is how
//     the stale-member question ("does routing ensure on a stale member?") is answered on an unfixed build.
//   StaleMemberIsSwept (1 world): a member whose talker exit cannot run (forged below) is removed by the channel's
//     own sweep once its entity is gone.
//
// Surface in Session Frontend: Ck.VoiceChat.Net.DestroyedTalkerLeavesItsChannels /
// .DestroyedTalkerNeverReachesRouting / .StaleMemberIsSwept

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"

#include "CkCore/Ensure/CkEnsure_Tracker.h"
#include "CkCore/Ensure/CkEnsure_Utils.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkVoiceChat/Codec/CkVoiceChat_Codec.h"
#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"
#include "CkTests/Net/CkAutoTest_NetSubject_VoiceChat.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <GameFramework/PlayerController.h>
#include <NativeGameplayTags.h>

// --------------------------------------------------------------------------------------------------------------------

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VoiceChat_Channel_AutoTest_Integrity, TEXT("VoiceChat.Channel.AutoTest.Integrity"));

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_membership_integrity_spec
{
    constexpr auto WaitSeconds = 15.0;

    struct FIntegritySpecState
    {
        int64 VoiceEnsuresBefore = 0;

        FCk_Handle Host;
        FCk_Handle_VoiceChannel Channel;
        FCk_Handle_VoiceTalker Speaker;
        FCk_Handle_VoiceTalker Leaver;
        FCk_Handle LeaverEntity;

        int32 EnsuresBeforeRouting = 0;
        TMap<FString, uint64> EnsureSitesBeforeRouting;
    };

    auto Get_VoiceEnsureCount() -> int64
    {
        return ck::auto_test::entity_bridge::Get_EnsureCount_FromFiles(TEXT("CkVoiceChat"));
    }

    auto Get_EnsureSites() -> TMap<FString, uint64>
    {
        auto Sites = TMap<FString, uint64>{};

        for (const auto& Occurrence : ck::ensure::Get_EnsureOccurrenceTracker().GetSnapshot())
        {
            Sites.FindOrAdd(FString::Printf(TEXT("%s:%d (%s)"), *Occurrence.Signature.File.ToString(),
                Occurrence.Signature.Line, *Occurrence.Signature.Expression)) += Occurrence.Count;
        }

        return Sites;
    }

    // Subjects told apart by owner: the speaker is server-owned (no owner on any world), the leaver is owned by the
    // remote client's PlayerController.
    auto Find_Subject(UWorld* InWorld, bool InServerOwned) -> ACk_AutoTest_NetSubject_VoiceChat_UE*
    {
        if (InWorld == nullptr)
        { return nullptr; }

        for (TActorIterator<ACk_AutoTest_NetSubject_VoiceChat_UE> It{InWorld}; It; ++It)
        {
            const auto IsServerOwned = It->GetOwner() == nullptr;
            if (IsServerOwned == InServerOwned)
            { return *It; }
        }

        return nullptr;
    }

    auto Find_RemotePlayerController(UWorld* InServer) -> APlayerController*
    {
        for (auto It = InServer->GetPlayerControllerIterator(); It; ++It)
        {
            if (auto* Controller = It->Get();
                Controller != nullptr && NOT Controller->IsLocalController())
            { return Controller; }
        }

        return nullptr;
    }

    // Members of the speaker subject's channel on InWorld, as that world holds them (on a client: applied from the
    // control plane). A member key whose talker no longer exists still counts here.
    auto Get_SpeakerChannelMemberCount(UWorld* InWorld) -> int32
    {
        const auto* Speaker = Find_Subject(InWorld, true);

        if (Speaker == nullptr || ck::Is_NOT_Valid(Speaker->_TestChannel))
        { return INDEX_NONE; }

        return Speaker->_TestChannel.Get<ck::FFragment_VoiceChannel>().Get_Members().Num();
    }

    auto Get_SpeakerIsMemberOfOwnChannel(UWorld* InWorld) -> bool
    {
        const auto* Speaker = Find_Subject(InWorld, true);

        if (Speaker == nullptr || ck::Is_NOT_Valid(Speaker->_TestChannel) || ck::Is_NOT_Valid(Speaker->_TestTalker))
        { return false; }

        return UCk_Utils_VoiceChannel_UE::Get_IsMember(Speaker->_TestChannel, Speaker->_TestTalker);
    }

    // The server never decodes, so frame content is irrelevant to Route - only the header must be honest.
    auto Make_Bundle(uint8 InChannelIdx) -> TArray<uint8>
    {
        auto Frame = TArray<uint8>{};
        Frame.Init(0x42, 60);

        constexpr auto Seq = uint16{1};
        constexpr auto AmplitudeQ8 = uint8{128};
        return ck::voice_chat::codec::Pack_Bundle(
            FCk_VoiceChat_BundleHeader{Seq, InChannelIdx, AmplitudeQ8}, TArray<TArray<uint8>>{Frame});
    }

    // One server-only world: a host entity carrying a Global2D channel, and the speaker talker on its own entity.
    auto Compose_LocalChannelAndSpeaker(UWorld* InServer, FIntegritySpecState& InState) -> void
    {
        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);

        InState.Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
        InState.Channel = UCk_Utils_VoiceChannel_UE::Add(InState.Host,
            FCk_VoiceChannel_Spec{TAG_VoiceChat_Channel_AutoTest_Integrity}
                .Set_SpatializationPolicy(ECk_VoiceChat_SpatializationPolicy::Global2D));

        auto SpeakerEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
        InState.Speaker = UCk_Utils_VoiceTalker_UE::Add(SpeakerEntity, FCk_VoiceTalker_Spec{});
    }

    auto Add_RestoreLogSuppression(FAutomationTestBase* InTest) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
            {
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("restore log suppression statics")));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_DestroyedTalkerLeavesItsChannels,
    "Ck.VoiceChat.Net.DestroyedTalkerLeavesItsChannels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_DestroyedTalkerLeavesItsChannels::RunTest(const FString& Parameters)
{
    using namespace ck_voice_membership_integrity_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FIntegritySpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();

            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

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
                    ACk_AutoTest_NetSubject_VoiceChat_UE::StaticClass(), FTransform::Identity, SpawnInfo) == nullptr)
            { AddError(TEXT("spawn of the leaver subject failed")); }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            const auto* Speaker = Find_Subject(ck::auto_test::net::Get_ServerWorld(), true);
            const auto* Leaver = Find_Subject(ck::auto_test::net::Get_ServerWorld(), false);

            return Speaker != nullptr && Leaver != nullptr &&
                ck::IsValid(Speaker->_TestChannel) && ck::IsValid(Speaker->_TestTalker) && ck::IsValid(Leaver->_TestTalker) &&
                UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(Speaker->_TestChannel) != ck::VoiceChannel_UnassignedIdx;
        }), WaitSeconds, TEXT("both subjects composed on the server and the channel registered")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* Speaker = Find_Subject(InServer, true);
            auto* Leaver = Find_Subject(InServer, false);
            if (Speaker == nullptr || Leaver == nullptr)
            {
                AddError(TEXT("subjects missing on the server at join time"));
                return;
            }

            UCk_Utils_VoiceChannel_UE::Request_Join(Speaker->_TestChannel,
                FCk_Request_VoiceChannel_Join{Speaker->_TestTalker}, {});
            UCk_Utils_VoiceChannel_UE::Request_Join(Speaker->_TestChannel,
                FCk_Request_VoiceChannel_Join{Leaver->_TestTalker}, {});
        })));

    // Positive control: before the destroy the control plane carries BOTH members to the client.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_SpeakerChannelMemberCount(ck::auto_test::net::Get_ServerWorld()) == 2 &&
                Get_SpeakerChannelMemberCount(ck::auto_test::net::Get_ClientWorld(0)) == 2;
        }), WaitSeconds, TEXT("both talkers are members on the server and on the client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* Leaver = Find_Subject(InServer, false);
            if (Leaver == nullptr)
            {
                AddError(TEXT("leaver subject missing at destroy time"));
                return;
            }

            // The realistic shape: the player's whole subject goes away (actor, bridged entity, talker).
            Leaver->Destroy();
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_SpeakerChannelMemberCount(ck::auto_test::net::Get_ServerWorld()) == 1 &&
                Get_SpeakerIsMemberOfOwnChannel(ck::auto_test::net::Get_ServerWorld());
        }), WaitSeconds, TEXT("the server's channel holds only the surviving talker")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_SpeakerChannelMemberCount(ck::auto_test::net::Get_ClientWorld(0)) == 1 &&
                Get_SpeakerIsMemberOfOwnChannel(ck::auto_test::net::Get_ClientWorld(0));
        }), WaitSeconds, TEXT("the client's channel replica holds only the surviving talker (control plane)")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);
            return true;
        }), TEXT("destroyed talker left its channels")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    Add_RestoreLogSuppression(this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_DestroyedTalkerNeverReachesRouting,
    "Ck.VoiceChat.Net.DestroyedTalkerNeverReachesRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_DestroyedTalkerNeverReachesRouting::RunTest(const FString& Parameters)
{
    using namespace ck_voice_membership_integrity_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FIntegritySpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            Compose_LocalChannelAndSpeaker(InServer, *State);

            auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
            State->LeaverEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
            State->Leaver = UCk_Utils_VoiceTalker_UE::Add(State->LeaverEntity, FCk_VoiceTalker_Spec{});

            UCk_Utils_VoiceChannel_UE::Request_Join(State->Channel, FCk_Request_VoiceChannel_Join{State->Speaker}, {});
            UCk_Utils_VoiceChannel_UE::Request_Join(State->Channel, FCk_Request_VoiceChannel_Join{State->Leaver}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Channel) &&
                UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(State->Channel) != ck::VoiceChannel_UnassignedIdx &&
                UCk_Utils_VoiceChannel_UE::Get_IsMember(State->Channel, State->Speaker) &&
                UCk_Utils_VoiceChannel_UE::Get_IsMember(State->Channel, State->Leaver);
        }), WaitSeconds, TEXT("both talkers are members of the registered channel")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->LeaverEntity);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::Is_NOT_Valid(State->LeaverEntity, ck::IsValid_Policy_IncludePendingKill{});
        }), WaitSeconds, TEXT("the leaver's entity is fully destroyed")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            State->EnsuresBeforeRouting = UCk_Utils_Ensure_UE::Get_EnsureCount();
            State->EnsureSitesBeforeRouting = Get_EnsureSites();

            UCk_Utils_VoiceTalker_UE::Debug_InjectInboundBundle(State->Speaker,
                Make_Bundle(UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(State->Channel)), nullptr);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            for (const auto& [Site, Count] : Get_EnsureSites())
            {
                const auto* CountBefore = State->EnsureSitesBeforeRouting.Find(Site);
                const auto Delta = Count - (CountBefore != nullptr ? *CountBefore : uint64{0});

                if (Delta > 0)
                { AddInfo(FString::Printf(TEXT("ensure site hit while routing past the destroyed member: %s x%llu"), *Site, Delta)); }
            }

            TestEqual(TEXT("routing the surviving talker raised no ensure anywhere"),
                UCk_Utils_Ensure_UE::Get_EnsureCount(), State->EnsuresBeforeRouting);

            TestEqual(TEXT("the destroyed talker is no longer a member key"),
                State->Channel.Get<ck::FFragment_VoiceChannel>().Get_Members().Num(), 1);

            TestEqual(TEXT("no CkVoiceChat ensure fired since the destroy"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("routing past a destroyed member")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    Add_RestoreLogSuppression(this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_StaleMemberIsSwept,
    "Ck.VoiceChat.Net.StaleMemberIsSwept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_StaleMemberIsSwept::RunTest(const FString& Parameters)
{
    using namespace ck_voice_membership_integrity_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FIntegritySpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            Compose_LocalChannelAndSpeaker(InServer, *State);

            // Forges the one member shape the prompt path cannot clean: a key whose entity carries no talker
            // feature, so no talker exit will ever run for it - standing in for the paths that destroy an entity
            // without its exits (an abrupt destroy, a restore). Deliberately unchecked; CastChecked would refuse it.
            auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
            State->LeaverEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
            State->Leaver = ck::StaticCast<FCk_Handle_VoiceTalker>(State->LeaverEntity);

            UCk_Utils_VoiceChannel_UE::Request_Join(State->Channel, FCk_Request_VoiceChannel_Join{State->Speaker}, {});
            UCk_Utils_VoiceChannel_UE::Request_Join(State->Channel, FCk_Request_VoiceChannel_Join{State->Leaver}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Channel) &&
                State->Channel.Get<ck::FFragment_VoiceChannel>().Get_Members().Num() == 2;
        }), WaitSeconds, TEXT("the speaker and the forged member are both member keys")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->LeaverEntity);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::Is_NOT_Valid(State->LeaverEntity, ck::IsValid_Policy_IncludePendingKill{});
        }), WaitSeconds, TEXT("the forged member's entity is fully destroyed")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Channel) &&
                State->Channel.Get<ck::FFragment_VoiceChannel>().Get_Members().Num() == 1;
        }), WaitSeconds, TEXT("the channel swept the stale member")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            State->EnsuresBeforeRouting = UCk_Utils_Ensure_UE::Get_EnsureCount();

            UCk_Utils_VoiceTalker_UE::Debug_InjectInboundBundle(State->Speaker,
                Make_Bundle(UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(State->Channel)), nullptr);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestTrue(TEXT("the speaker is still a member"),
                UCk_Utils_VoiceChannel_UE::Get_IsMember(State->Channel, State->Speaker));

            TestEqual(TEXT("routing after the sweep raised no ensure"),
                UCk_Utils_Ensure_UE::Get_EnsureCount(), State->EnsuresBeforeRouting);

            return true;
        }), TEXT("stale member swept")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    Add_RestoreLogSuppression(this);

    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
