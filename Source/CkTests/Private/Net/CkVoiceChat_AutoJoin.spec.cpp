// Auto-join: a talker whose Spec lists a channel name becomes a member of every channel of that name that opts in
// (_AutoJoinTalkers), on the authority, whichever of the two was composed first - and a client learns the
// membership only through the replicated control plane, never by joining itself (a client-side join attempt would
// trip the Utils authority ensure, so every scenario asserts the ensure count did not move). An explicit Leave
// after an auto-join stands: the reconcile, proven to have run again by a second channel being joined, does not
// undo it.
//
// Surface in Session Frontend: Ck.VoiceChat.Net.AutoJoin_ChannelComposedFirst / .AutoJoin_TalkerComposedFirst /
// .AutoJoin_ExplicitLeaveStands

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"

#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"
#include "CkTests/Net/CkAutoTest_NetSubject_VoiceAutoJoin.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_auto_join_spec
{
    constexpr auto WaitSeconds = 15.0;

    struct FAutoJoinSpecState
    {
        int64 VoiceEnsuresBefore = 0;

        TWeakObjectPtr<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE> FirstChannelSubject;
        TWeakObjectPtr<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE> SecondChannelSubject;
    };

    // Ensures raised from CkVoiceChat's own sources; a client-side join attempt would raise one at the Utils authority
    // check. Counted per file rather than globally so an unrelated system's ensure during PIE cannot decide the test.
    auto Get_VoiceEnsureCount() -> int64
    {
        return ck::auto_test::entity_bridge::Get_EnsureCount_FromFiles(TEXT("CkVoiceChat"));
    }

    template <typename T_Subject>
    auto Find_First(UWorld* InWorld) -> T_Subject*
    {
        if (InWorld == nullptr)
        { return nullptr; }

        for (TActorIterator<T_Subject> It{InWorld}; It; ++It)
        { return *It; }

        return nullptr;
    }

    template <typename T_Subject>
    auto Spawn_Subject(FAutomationTestBase* InTest, UWorld* InServer) -> T_Subject*
    {
        auto SpawnInfo = FActorSpawnParameters{};
        SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        auto* Subject = InServer->SpawnActor<T_Subject>(T_Subject::StaticClass(), FTransform::Identity, SpawnInfo);

        if (Subject == nullptr)
        { InTest->AddError(TEXT("auto-join subject spawn failed")); }

        return Subject;
    }

    auto Get_ChannelIsRegistered(const ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE* InSubject) -> bool
    {
        return InSubject != nullptr && ck::IsValid(InSubject->_TestChannel) &&
            UCk_Utils_VoiceChannel_UE::Get_ChannelIdx(InSubject->_TestChannel) != ck::VoiceChannel_UnassignedIdx;
    }

    // The talker is a member of the channel on InWorld - each world reads its OWN copies (on a client: the replica
    // channel and replica talker, membership applied from the control plane).
    auto Get_TalkerIsMember(UWorld* InWorld) -> bool
    {
        const auto* ChannelSubject = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(InWorld);
        const auto* TalkerSubject = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(InWorld);

        if (NOT Get_ChannelIsRegistered(ChannelSubject) ||
            TalkerSubject == nullptr || ck::Is_NOT_Valid(TalkerSubject->_TestTalker))
        { return false; }

        return UCk_Utils_VoiceChannel_UE::Get_IsMember(ChannelSubject->_TestChannel, TalkerSubject->_TestTalker);
    }

    auto Get_IsMemberOf(
        const TWeakObjectPtr<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>& InChannelSubject,
        UWorld* InWorld) -> bool
    {
        const auto* TalkerSubject = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(InWorld);

        if (NOT Get_ChannelIsRegistered(InChannelSubject.Get()) ||
            TalkerSubject == nullptr || ck::Is_NOT_Valid(TalkerSubject->_TestTalker))
        { return false; }

        return UCk_Utils_VoiceChannel_UE::Get_IsMember(InChannelSubject->_TestChannel, TalkerSubject->_TestTalker);
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
    FCkVoiceChatNet_AutoJoin_ChannelComposedFirst,
    "Ck.VoiceChat.Net.AutoJoin_ChannelComposedFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_AutoJoin_ChannelComposedFirst::RunTest(const FString& Parameters)
{
    using namespace ck_voice_auto_join_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FAutoJoinSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();
            Spawn_Subject<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(this, InServer);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_ChannelIsRegistered(Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(
                    ck::auto_test::net::Get_ServerWorld())) &&
                Get_ChannelIsRegistered(Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(
                    ck::auto_test::net::Get_ClientWorld(0)));
        }), WaitSeconds, TEXT("the channel is registered on the server and its index reached the client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            Spawn_Subject<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(this, InServer);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_TalkerIsMember(ck::auto_test::net::Get_ServerWorld());
        }), WaitSeconds, TEXT("the later talker auto-joined the earlier channel on the server")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_TalkerIsMember(ck::auto_test::net::Get_ClientWorld(0));
        }), WaitSeconds, TEXT("the client learned the membership through the control plane")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            const auto* ChannelSubject = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(
                ck::auto_test::net::Get_ServerWorld());

            if (NOT TestTrue(TEXT("server channel subject alive"), Get_ChannelIsRegistered(ChannelSubject)))
            { return true; }

            TestEqual(TEXT("exactly one member on the server - joined once"),
                UCk_Utils_VoiceChannel_UE::Get_Members(ChannelSubject->_TestChannel).Num(), 1);

            TestEqual(TEXT("no CkVoiceChat ensure fired (a client-side join attempt would ensure at the authority check)"),
                Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("channel-first auto-join assertions")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    Add_RestoreLogSuppression(this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_AutoJoin_TalkerComposedFirst,
    "Ck.VoiceChat.Net.AutoJoin_TalkerComposedFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_AutoJoin_TalkerComposedFirst::RunTest(const FString& Parameters)
{
    using namespace ck_voice_auto_join_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FAutoJoinSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();
            Spawn_Subject<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(this, InServer);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            const auto* Server = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(ck::auto_test::net::Get_ServerWorld());
            const auto* Client = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(ck::auto_test::net::Get_ClientWorld(0));

            return Server != nullptr && ck::IsValid(Server->_TestTalker) &&
                Client != nullptr && ck::IsValid(Client->_TestTalker);
        }), WaitSeconds, TEXT("the talker is composed on both worlds before any channel exists")));

    // The talker sits with no channel for a while: the reconcile has had many passes with nothing to join.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            const auto* TalkerSubject = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(
                ck::auto_test::net::Get_ServerWorld());

            if (NOT TestTrue(TEXT("server talker subject alive"),
                    TalkerSubject != nullptr && ck::IsValid(TalkerSubject->_TestTalker)))
            { return true; }

            TestTrue(TEXT("the talker carries the auto-join record (its Spec listed a channel)"),
                TalkerSubject->_TestTalker.Has<ck::FFragment_VoiceTalker_AutoJoin>());
            TestTrue(TEXT("and has joined nothing yet"),
                TalkerSubject->_TestTalker.Get<ck::FFragment_VoiceTalker_AutoJoin>().Get_JoinedChannels().IsEmpty());

            return true;
        }), TEXT("talker-first: nothing to join yet")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            Spawn_Subject<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(this, InServer);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_TalkerIsMember(ck::auto_test::net::Get_ServerWorld());
        }), WaitSeconds, TEXT("the earlier talker auto-joined the later channel on the server")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_TalkerIsMember(ck::auto_test::net::Get_ClientWorld(0));
        }), WaitSeconds, TEXT("the client learned the membership through the control plane")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestEqual(TEXT("no CkVoiceChat ensure fired (a client-side join attempt would ensure at the authority check)"),
                Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("talker-first auto-join assertions")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    Add_RestoreLogSuppression(this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_AutoJoin_ExplicitLeaveStands,
    "Ck.VoiceChat.Net.AutoJoin_ExplicitLeaveStands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_AutoJoin_ExplicitLeaveStands::RunTest(const FString& Parameters)
{
    using namespace ck_voice_auto_join_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FAutoJoinSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            State->VoiceEnsuresBefore = Get_VoiceEnsureCount();
            State->FirstChannelSubject = Spawn_Subject<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(this, InServer);
            Spawn_Subject<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(this, InServer);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return Get_IsMemberOf(State->FirstChannelSubject, ck::auto_test::net::Get_ServerWorld());
        }), WaitSeconds, TEXT("the talker auto-joined the first channel")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            auto* TalkerSubject = Find_First<ACk_AutoTest_NetSubject_VoiceAutoJoinTalker_UE>(InServer);

            if (NOT State->FirstChannelSubject.IsValid() || TalkerSubject == nullptr)
            { return; }

            UCk_Utils_VoiceChannel_UE::Request_Leave(State->FirstChannelSubject->_TestChannel,
                FCk_Request_VoiceChannel_Leave{TalkerSubject->_TestTalker}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return Get_ChannelIsRegistered(State->FirstChannelSubject.Get()) &&
                NOT Get_IsMemberOf(State->FirstChannelSubject, ck::auto_test::net::Get_ServerWorld());
        }), WaitSeconds, TEXT("the explicit Leave applied")));

    // A second channel of the same name re-runs the reconcile: joining it is the proof that the reconcile ran
    // after the Leave, so staying out of the first channel is a decision, not an absence of passes.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InServer) -> void
        {
            State->SecondChannelSubject = Spawn_Subject<ACk_AutoTest_NetSubject_VoiceAutoJoinChannel_UE>(this, InServer);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return Get_IsMemberOf(State->SecondChannelSubject, ck::auto_test::net::Get_ServerWorld());
        }), WaitSeconds, TEXT("the reconcile ran again: the talker auto-joined the second channel")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestFalse(TEXT("the explicit Leave from the first channel was NOT undone by the later reconcile"),
                Get_IsMemberOf(State->FirstChannelSubject, ck::auto_test::net::Get_ServerWorld()));

            TestEqual(TEXT("no CkVoiceChat ensure fired"), Get_VoiceEnsureCount(), State->VoiceEnsuresBefore);

            return true;
        }), TEXT("explicit leave stands")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    Add_RestoreLogSuppression(this);

    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
