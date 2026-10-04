#include "CkCore/Validation/CkIsValid.h"
#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/Direct/CkOnlineDirect_Feature.h"
#include "CkOnline/Direct/CkOnlineDirect_Utils.h"

#include "CkOnline/CkOnlineDirectGym_Evidence.h"
#include "CkTest_OnlineDirectListener.h"

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_online_direct
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;
    constexpr double StepTimeoutSeconds = 8.0;

    struct FService
    {
        TSharedPtr<ck::direct::FFeature> Feature;
        TSharedPtr<ck::FOnlineServiceHost> Host;
        FCk_Handle_OnlineDirect Handle;
        FGuid Operation;
        int32 BeforeCompletion = 0;

        auto Start(const FString& InName, int32 InPort, double InInviteLifetime = 30.0,
            double InInitialTime = -1.0) -> bool
        {
            auto Spec = FCk_OnlineDirect_Spec{};
            Spec.Set_Name(InName).Set_AdvertisedAddress(TEXT("127.0.0.1"))
                .Set_Port(InPort).Set_Capacity(2).Set_InviteLifetimeSeconds(InInviteLifetime)
                .Set_OperationTimeoutSeconds(3.0);
            Feature = ck::direct::FFeature::TryCreate(Spec);
            if (!Feature) { return false; }
            Host = ck::FOnlineServiceHost::TryCreateFeature(
                StaticCastSharedRef<ck::IOnlineServiceFeature>(Feature.ToSharedRef()),
                FCk_Time{InInitialTime >= 0.0 ? InInitialTime : FPlatformTime::Seconds()});
            if (!Host) { return false; }
            Handle = UCk_Utils_OnlineDirect_UE::Cast(Host->Get_Handle());
            return UCk_Utils_OnlineDirect_UE::Has(Handle);
        }

        auto Request(ECk_OnlineDirect_Action InAction, const FString& InText = {}, bool InReady = false) -> bool
        {
            Operation = FGuid::NewGuid();
            BeforeCompletion = UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Handle);
            auto Request = FCk_Request_OnlineDirect{};
            Request.Set_OperationId(Operation).Set_Action(InAction).Set_Text(InText).Set_Ready(InReady);
            return UCk_Utils_OnlineDirect_UE::TryRequest(Handle, Request);
        }

        auto Completed() const -> bool
        { return UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Handle) > BeforeCompletion; }

        auto Snapshot() const -> FCk_OnlineDirect_Snapshot
        { return UCk_Utils_OnlineDirect_UE::Get_Snapshot(Handle); }

        auto ExpectCompletion(FAutomationTestBase& InTest, ECk_OnlineDirect_Result InResult) const -> void
        {
            InTest.TestEqual(TEXT("one terminal completion for request"),
                UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Handle), BeforeCompletion + 1);
            const auto Completion = UCk_Utils_OnlineDirect_UE::Get_LastCompletion(Handle);
            InTest.TestEqual(TEXT("completion identifies accepted request"), Completion.Get_OperationId(), Operation);
            if (Completion.Get_Result() != InResult)
            {
                const auto Current = Snapshot();
                InTest.AddError(FString::Printf(TEXT("Direct result expected=%d actual=%d detail='%s' state=%d error='%s'"),
                    static_cast<int32>(InResult), static_cast<int32>(Completion.Get_Result()),
                    *Completion.Get_Detail(), static_cast<int32>(Current.Get_State()), *Current.Get_LastError()));
            }
            InTest.TestEqual(TEXT("terminal result"), Completion.Get_Result(), InResult);
        }

        auto Tick(double InNow) -> void
        { if (Host) { Host->Tick(FCk_Time{InNow}); } }

        auto Stop() -> void
        { if (Host) { Host->Request_Shutdown(); Host.Reset(); } Feature.Reset(); }
    };

    struct FFixture
    {
        FAutomationTestBase* Test = nullptr;
        FService Server;
        FService Client;
        FService WrongKey;
        FService FullClient;
        FService SamePort;
        bool Aborted = false;
        FGuid Epoch;
        FGuid HostId;
        FGuid FirstClientId;
        FGuid RejoinedClientId;
        FString Descriptor;
        double HostedAt = 0.0;

        auto Tick() -> void
        {
            const auto Now = FPlatformTime::Seconds();
            Server.Tick(Now);
            Client.Tick(Now);
            WrongKey.Tick(Now);
            FullClient.Tick(Now);
            SamePort.Tick(Now);
        }

        auto Stop() -> void
        {
            FullClient.Stop(); WrongKey.Stop(); Client.Stop(); SamePort.Stop(); Server.Stop();
        }
    };

    using FFixtureRef = TSharedRef<FFixture>;

    class FStep final : public IAutomationLatentCommand
    {
    public:
        FStep(FFixtureRef InFixture, const TCHAR* InLabel, TFunction<bool(FFixture&)> InCondition,
            TFunction<void(FFixture&)> InAction = {})
            : Fixture(InFixture), Label(InLabel), Condition(MoveTemp(InCondition)), Action(MoveTemp(InAction)) {}

        auto Update() -> bool override
        {
            if (Fixture->Aborted) { return true; }
            if (Started < 0.0) { Started = FPlatformTime::Seconds(); }
            Fixture->Tick();
            if (Condition(*Fixture))
            {
                if (Action) { Action(*Fixture); }
                return true;
            }
            if (FPlatformTime::Seconds() - Started < StepTimeoutSeconds) { return false; }
            Fixture->Test->AddError(FString::Printf(TEXT("Direct service timed out: %s"), *Label));
            Fixture->Aborted = true;
            return true;
        }
    private:
        FFixtureRef Fixture;
        FString Label;
        TFunction<bool(FFixture&)> Condition;
        TFunction<void(FFixture&)> Action;
        double Started = -1.0;
    };

    class FStop final : public IAutomationLatentCommand
    {
    public:
        explicit FStop(FFixtureRef InFixture) : Fixture(InFixture) {}
        auto Update() -> bool override { Fixture->Stop(); return true; }
    private:
        FFixtureRef Fixture;
    };

    auto HasMember(const FCk_OnlineDirect_Snapshot& InSnapshot, FGuid InId,
        const FString& InName, bool InReady) -> bool
    {
        return InSnapshot.Get_Members().ContainsByPredicate([&](const FCk_OnlineDirect_Member& Member)
        { return Member.Get_Id() == InId && Member.Get_Name() == InName && Member.Get_Ready() == InReady; });
    }

    auto HasMessage(const FCk_OnlineDirect_Snapshot& InSnapshot, int32 InSequence,
        FGuid InSenderId, const FString& InSenderName, const FString& InText) -> bool
    {
        return InSnapshot.Get_Messages().ContainsByPredicate([&](const FCk_OnlineDirect_Message& Message)
        {
            return Message.Get_Sequence() == InSequence && Message.Get_SenderId() == InSenderId
                && Message.Get_SenderName() == InSenderName && Message.Get_Text() == InText;
        });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_OnlineDirect_Loopback,
    "Ck.Online.Direct.LoopbackLobbyReadyTextLeaveRejoin", ck_test_online_direct::Flags)

bool FCkTest_OnlineDirect_Loopback::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_direct;
    auto Fixture = MakeShared<FFixture>();
    Fixture->Test = this;
    if (!TestTrue(TEXT("host feature and ECS host start"), Fixture->Server.Start(TEXT("PrivateHostName"), 17888)) ||
        !TestTrue(TEXT("client feature and ECS host start"), Fixture->Client.Start(TEXT("PrivateClientName"), 17889)) ||
        !TestTrue(TEXT("wrong-key peer starts"), Fixture->WrongKey.Start(TEXT("WrongKey"), 17890)) ||
        !TestTrue(TEXT("full-capacity peer starts"), Fixture->FullClient.Start(TEXT("Full"), 17891)) ||
        !TestTrue(TEXT("same-port peer starts"), Fixture->SamePort.Start(TEXT("SamePort"), 17888)))
    { Fixture->Stop(); return false; }

    const auto Baseline = Fixture->Server.Snapshot();
    TestEqual(TEXT("fresh server starts Offline"), Baseline.Get_State(), ECk_OnlineDirect_State::Offline);
    TestTrue(TEXT("default request is rejected"),
        !UCk_Utils_OnlineDirect_UE::TryRequest(Fixture->Server.Handle, FCk_Request_OnlineDirect{}));
    auto BadText = FCk_Request_OnlineDirect{};
    BadText.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::SendText).Set_Text(TEXT("bad\ntext"));
    TestFalse(TEXT("control character is rejected before transport"),
        UCk_Utils_OnlineDirect_UE::TryRequest(Fixture->Server.Handle, BadText));
    TestEqual(TEXT("malformed requests have no completion"),
        UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Fixture->Server.Handle), 0);
    TestEqual(TEXT("malformed requests do not change snapshot revision"), Fixture->Server.Snapshot().Get_Revision(),
        Baseline.Get_Revision());

    TestTrue(TEXT("real host request admitted"), Fixture->Server.Request(ECk_OnlineDirect_Action::Host));
    auto Duplicate = FCk_Request_OnlineDirect{};
    Duplicate.Set_OperationId(Fixture->Server.Operation).Set_Action(ECk_OnlineDirect_Action::Host);
    TestFalse(TEXT("duplicate queued operation ID rejected"),
        UCk_Utils_OnlineDirect_UE::TryRequest(Fixture->Server.Handle, Duplicate));
    TestEqual(TEXT("host request is deferred until scheduler tick"),
        UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Fixture->Server.Handle), 0);

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("TCP host binds and publishes one-member lobby"),
        [](FFixture& F)
        { return F.Server.Completed() && F.Server.Snapshot().Get_State() == ECk_OnlineDirect_State::Hosting; },
        [this](FFixture& F)
        {
            F.Server.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            const auto Snapshot = F.Server.Snapshot();
            F.Descriptor = Snapshot.Get_JoinDescriptor();
            F.Epoch = Snapshot.Get_LobbyEpoch();
            F.HostId = Snapshot.Get_LocalMemberId();
            TestTrue(TEXT("descriptor is valid"), ck::direct::FTransport::ValidateDescriptor(F.Descriptor));
            TestTrue(TEXT("host epoch and member identity are valid"), F.Epoch.IsValid() && F.HostId.IsValid());
            TestEqual(TEXT("host has one local member"), Snapshot.Get_Members().Num(), 1);
            TestTrue(TEXT("host roster has named local member"), HasMember(Snapshot, F.HostId, TEXT("PrivateHostName"), false));
            TestTrue(TEXT("same-port host request admitted for transport result"), F.SamePort.Request(ECk_OnlineDirect_Action::Host));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("second bind to same port terminates without a lobby"),
        [](FFixture& F) { return F.SamePort.Completed(); }, [this](FFixture& F)
        {
            F.SamePort.ExpectCompletion(*this, ECk_OnlineDirect_Result::TransportError);
            TestEqual(TEXT("same-port peer remains Offline"), F.SamePort.Snapshot().Get_State(), ECk_OnlineDirect_State::Offline);
            TestEqual(TEXT("original host remains active"), F.Server.Snapshot().Get_State(), ECk_OnlineDirect_State::Hosting);
            auto Parts = TArray<FString>{};
            F.Descriptor.ParseIntoArray(Parts, TEXT("|"), false);
            if (Parts.Num() != 4) { AddError(TEXT("Host produced malformed descriptor.")); F.Aborted = true; return; }
            Parts[3] = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            const auto WrongDescriptor = FString::Join(Parts, TEXT("|"));
            TestTrue(TEXT("wrong capability is syntactically valid"), ck::direct::FTransport::ValidateDescriptor(WrongDescriptor));
            TestTrue(TEXT("wrong-capability join admitted for wire rejection"),
                F.WrongKey.Request(ECk_OnlineDirect_Action::Join, WrongDescriptor));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host rejects wrong capability on real TCP connection"),
        [](FFixture& F) { return F.WrongKey.Completed(); }, [this](FFixture& F)
        {
            F.WrongKey.ExpectCompletion(*this, ECk_OnlineDirect_Result::Unauthorized);
            TestEqual(TEXT("wrong-key peer has no member identity"), F.WrongKey.Snapshot().Get_LocalMemberId(), FGuid{});
            TestEqual(TEXT("unauthorized peer did not change host roster"), F.Server.Snapshot().Get_Members().Num(), 1);
            TestTrue(TEXT("valid client join admitted"), F.Client.Request(ECk_OnlineDirect_Action::Join, F.Descriptor));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("two real peers share epoch and named roster"),
        [](FFixture& F)
        {
            return F.Client.Completed() && F.Client.Snapshot().Get_State() == ECk_OnlineDirect_State::Joined
                && F.Server.Snapshot().Get_Members().Num() == 2 && F.Client.Snapshot().Get_Members().Num() == 2;
        }, [this](FFixture& F)
        {
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            const auto Host = F.Server.Snapshot();
            const auto Client = F.Client.Snapshot();
            F.FirstClientId = Client.Get_LocalMemberId();
            TestTrue(TEXT("admitted client has distinct valid identity"),
                F.FirstClientId.IsValid() && F.FirstClientId != F.HostId);
            TestEqual(TEXT("peer epoch matches host"), Client.Get_LobbyEpoch(), F.Epoch);
            TestTrue(TEXT("host roster names admitted peer"), HasMember(Host, F.FirstClientId, TEXT("PrivateClientName"), false));
            TestTrue(TEXT("client roster names host"), HasMember(Client, F.HostId, TEXT("PrivateHostName"), false));
            const auto JoinedLine = ck::test::direct::FormatCheckpoint(Client, false, 1);
            TestTrue(TEXT("joined checkpoint has schema and Lobby view"),
                JoinedLine.StartsWith(TEXT("[OnlineCheckpoint] schema=1 view=Lobby ordinal=1 ")));
            TestTrue(TEXT("joined checkpoint identifies actual service generation"),
                JoinedLine.Contains(FString::Printf(TEXT("generation=%s"),
                    *Client.Get_ServiceGeneration().ToString(EGuidFormats::Digits))));
            TestTrue(TEXT("joined checkpoint identifies shared epoch and local member"),
                JoinedLine.Contains(FString::Printf(TEXT("epoch=%s local=%s"),
                    *F.Epoch.ToString(EGuidFormats::Digits), *F.FirstClientId.ToString(EGuidFormats::Digits))));
            TestTrue(TEXT("joined checkpoint records process and source revision"),
                JoinedLine.Contains(FString::Printf(TEXT("pid=%u"), FPlatformProcess::GetCurrentProcessId())) &&
                JoinedLine.Contains(FString::Printf(TEXT("revision=%d"), Client.Get_Revision())));
            TestTrue(TEXT("joined checkpoint records two unready members"),
                JoinedLine.Contains(TEXT("state=Joined rosterCount=2")) &&
                JoinedLine.Contains(F.HostId.ToString(EGuidFormats::Digits) + TEXT(":0")) &&
                JoinedLine.Contains(F.FirstClientId.ToString(EGuidFormats::Digits) + TEXT(":0")));
            auto SortedIds = TArray<FString>{F.HostId.ToString(EGuidFormats::Digits),
                F.FirstClientId.ToString(EGuidFormats::Digits)};
            SortedIds.Sort();
            TestTrue(TEXT("checkpoint roster is sorted by member ID"),
                JoinedLine.Contains(FString::Printf(TEXT("roster=[%s:0,%s:0]"),
                    *SortedIds[0], *SortedIds[1])));
            TestFalse(TEXT("checkpoint omits private names and invite"),
                JoinedLine.Contains(TEXT("PrivateHostName")) || JoinedLine.Contains(TEXT("PrivateClientName")) ||
                JoinedLine.Contains(F.Descriptor));
            auto InviteParts = TArray<FString>{};
            F.Descriptor.ParseIntoArray(InviteParts, TEXT("|"), false);
            if (InviteParts.Num() == 4)
            {
                TestFalse(TEXT("checkpoint omits invite capability"), JoinedLine.Contains(InviteParts[3]));
            }
            TestTrue(TEXT("capacity contender join admitted for wire result"),
                F.FullClient.Request(ECk_OnlineDirect_Action::Join, F.Descriptor));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("capacity rejects third member"),
        [](FFixture& F) { return F.FullClient.Completed(); }, [this](FFixture& F)
        {
            F.FullClient.ExpectCompletion(*this, ECk_OnlineDirect_Result::Full);
            TestEqual(TEXT("host roster remains capacity two"), F.Server.Snapshot().Get_Members().Num(), 2);
            TestTrue(TEXT("host ready request admitted"), F.Server.Request(ECk_OnlineDirect_Action::SetReady, {}, true));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host ready propagates to client"),
        [](FFixture& F)
        { return F.Server.Completed() && HasMember(F.Client.Snapshot(), F.HostId, TEXT("PrivateHostName"), true); },
        [this](FFixture& F)
        {
            F.Server.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            TestTrue(TEXT("client ready request admitted"), F.Client.Request(ECk_OnlineDirect_Action::SetReady, {}, true));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("client ready ACK and both rosters converge"),
        [](FFixture& F)
        {
            return F.Client.Completed() && HasMember(F.Server.Snapshot(), F.FirstClientId, TEXT("PrivateClientName"), true)
                && HasMember(F.Client.Snapshot(), F.FirstClientId, TEXT("PrivateClientName"), true);
        }, [this](FFixture& F)
        {
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            const auto ReadyLine = ck::test::direct::FormatCheckpoint(F.Client.Snapshot(), false, 2);
            TestTrue(TEXT("ready checkpoint has both actual ready IDs"),
                ReadyLine.Contains(TEXT("ordinal=2")) &&
                ReadyLine.Contains(F.HostId.ToString(EGuidFormats::Digits) + TEXT(":1")) &&
                ReadyLine.Contains(F.FirstClientId.ToString(EGuidFormats::Digits) + TEXT(":1")));
            TestTrue(TEXT("host text request admitted"), F.Server.Request(ECk_OnlineDirect_Action::SendText, TEXT("hello client")));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host text arrives once with sender and sequence"),
        [](FFixture& F)
        {
            return F.Server.Completed() && F.Client.Snapshot().Get_Messages().Num() == 1
                && HasMessage(F.Client.Snapshot(), 1, F.HostId, TEXT("PrivateHostName"), TEXT("hello client"));
        }, [this](FFixture& F)
        {
            F.Server.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            TestEqual(TEXT("host transcript contains one message"), F.Server.Snapshot().Get_Messages().Num(), 1);
            TestTrue(TEXT("client text request admitted"), F.Client.Request(ECk_OnlineDirect_Action::SendText, TEXT("hello host")));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("client text ACK and matching transcript"),
        [](FFixture& F)
        {
            return F.Client.Completed() && F.Server.Snapshot().Get_Messages().Num() == 2
                && F.Client.Snapshot().Get_Messages().Num() == 2
                && HasMessage(F.Server.Snapshot(), 2, F.FirstClientId, TEXT("PrivateClientName"), TEXT("hello host"))
                && HasMessage(F.Client.Snapshot(), 2, F.FirstClientId, TEXT("PrivateClientName"), TEXT("hello host"));
        }, [this](FFixture& F)
        {
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            const auto Sample = F.Client.Snapshot();
            const auto LobbyLine = ck::test::direct::FormatCheckpoint(Sample, false, 3);
            const auto ChatLine = ck::test::direct::FormatCheckpoint(Sample, true, 3);
            TestEqual(TEXT("same sample formats stably as Lobby"),
                ck::test::direct::FormatCheckpoint(Sample, false, 3), LobbyLine);
            TestEqual(TEXT("same sample formats stably as Chat"),
                ck::test::direct::FormatCheckpoint(Sample, true, 3), ChatLine);
            TestTrue(TEXT("checkpoint views identify the same sample revision"),
                LobbyLine.Contains(TEXT("view=Lobby ordinal=3")) &&
                ChatLine.Contains(TEXT("view=Chat ordinal=3")) &&
                LobbyLine.Contains(FString::Printf(TEXT("revision=%d"), Sample.Get_Revision())) &&
                ChatLine.Contains(FString::Printf(TEXT("revision=%d"), Sample.Get_Revision())));
            const auto OrderedMessages = FString::Printf(TEXT("messages=[1:%s,2:%s]"),
                *F.HostId.ToString(EGuidFormats::Digits), *F.FirstClientId.ToString(EGuidFormats::Digits));
            TestTrue(TEXT("chat checkpoint records ordered sender IDs and retained count"),
                ChatLine.Contains(TEXT("state=Joined rosterCount=2")) &&
                ChatLine.Contains(TEXT("retained=2")) &&
                ChatLine.Contains(OrderedMessages));
            TestFalse(TEXT("checkpoint redacts names, chat text, and raw invite"),
                LobbyLine.Contains(TEXT("PrivateHostName")) || LobbyLine.Contains(TEXT("PrivateClientName")) ||
                LobbyLine.Contains(TEXT("hello client")) || LobbyLine.Contains(TEXT("hello host")) ||
                LobbyLine.Contains(F.Descriptor) || ChatLine.Contains(TEXT("PrivateHostName")) ||
                ChatLine.Contains(TEXT("PrivateClientName")) || ChatLine.Contains(TEXT("hello client")) ||
                ChatLine.Contains(TEXT("hello host")) || ChatLine.Contains(F.Descriptor));
            auto InviteParts = TArray<FString>{};
            F.Descriptor.ParseIntoArray(InviteParts, TEXT("|"), false);
            if (InviteParts.Num() == 4)
            {
                TestFalse(TEXT("Lobby and Chat checkpoints omit invite capability"),
                    LobbyLine.Contains(InviteParts[3]) || ChatLine.Contains(InviteParts[3]));
            }
            TestTrue(TEXT("client leave request admitted"), F.Client.Request(ECk_OnlineDirect_Action::Leave));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("leave closes client and removes host roster member"),
        [](FFixture& F)
        {
            return F.Client.Completed() && F.Client.Snapshot().Get_State() == ECk_OnlineDirect_State::Offline
                && F.Server.Snapshot().Get_Members().Num() == 1;
        }, [this](FFixture& F)
        {
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            TestFalse(TEXT("old peer is absent from host roster"),
                HasMember(F.Server.Snapshot(), F.FirstClientId, TEXT("PrivateClientName"), true));
            TestTrue(TEXT("same descriptor rejoin admitted"), F.Client.Request(ECk_OnlineDirect_Action::Join, F.Descriptor));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("rejoin has new identity in same lobby epoch"),
        [](FFixture& F)
        {
            return F.Client.Completed() && F.Client.Snapshot().Get_State() == ECk_OnlineDirect_State::Joined
                && F.Server.Snapshot().Get_Members().Num() == 2;
        }, [this](FFixture& F)
        {
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            F.RejoinedClientId = F.Client.Snapshot().Get_LocalMemberId();
            TestTrue(TEXT("rejoin receives fresh member identity"),
                F.RejoinedClientId.IsValid() && F.RejoinedClientId != F.FirstClientId);
            TestEqual(TEXT("rejoin remains in same host epoch"), F.Client.Snapshot().Get_LobbyEpoch(), F.Epoch);
            TestTrue(TEXT("new member appears on host"),
                HasMember(F.Server.Snapshot(), F.RejoinedClientId, TEXT("PrivateClientName"), false));
            F.Server.Stop();
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host loss fails client and closes lobby resources"),
        [](FFixture& F) { return F.Client.Snapshot().Get_State() == ECk_OnlineDirect_State::Failed; },
        [this](FFixture& F)
        {
            TestEqual(TEXT("lost host clears client roster"), F.Client.Snapshot().Get_Members().Num(), 0);
            TestEqual(TEXT("lost host clears client join descriptor"), F.Client.Snapshot().Get_JoinDescriptor(), FString{});
            TestFalse(TEXT("stopped server handle rejects new work"),
                UCk_Utils_OnlineDirect_UE::TryRequest(F.Server.Handle, FCk_Request_OnlineDirect{}
                    .Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::Host)));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStop(Fixture));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_OnlineDirect_ExpiryAndAdmission,
    "Ck.Online.Direct.InviteExpiry", ck_test_online_direct::Flags)

bool FCkTest_OnlineDirect_ExpiryAndAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_direct;
    auto Fixture = MakeShared<FFixture>();
    Fixture->Test = this;
    if (!TestTrue(TEXT("short-lived host starts"), Fixture->Server.Start(TEXT("ExpiryHost"), 17892, 0.1)) ||
        !TestTrue(TEXT("late joiner starts"), Fixture->Client.Start(TEXT("Late"), 17893)))
    { Fixture->Stop(); return false; }

    auto BadJoin = FCk_Request_OnlineDirect{};
    BadJoin.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::Join)
        .Set_Text(TEXT("127.0.0.1:17892?listen"));
    TestFalse(TEXT("malformed invite rejects before scheduling transport"),
        UCk_Utils_OnlineDirect_UE::TryRequest(Fixture->Client.Handle, BadJoin));
    TestEqual(TEXT("invalid invite leaves state Offline"), Fixture->Client.Snapshot().Get_State(),
        ECk_OnlineDirect_State::Offline);
    TestEqual(TEXT("invalid invite has no completion"),
        UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Fixture->Client.Handle), 0);

    TestTrue(TEXT("expiry host request accepted"), Fixture->Server.Request(ECk_OnlineDirect_Action::Host));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("short-lived host binds real TCP listener"),
        [](FFixture& F)
        { return F.Server.Completed() && F.Server.Snapshot().Get_State() == ECk_OnlineDirect_State::Hosting; },
        [this](FFixture& F)
        {
            F.Server.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            F.Descriptor = F.Server.Snapshot().Get_JoinDescriptor();
            F.Epoch = F.Server.Snapshot().Get_LobbyEpoch();
            F.HostedAt = FPlatformTime::Seconds();
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("invite lifetime elapses without a join"),
        [](FFixture& F)
        { return F.Server.Snapshot().Get_State() == ECk_OnlineDirect_State::Hosting &&
            FPlatformTime::Seconds() - F.HostedAt >= 0.12; },
        [this](FFixture& F)
        {
            TestTrue(TEXT("late join request admitted for wire expiry result"),
                F.Client.Request(ECk_OnlineDirect_Action::Join, F.Descriptor));
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host rejects expired capability on TCP"),
        [](FFixture& F) { return F.Client.Completed(); }, [this](FFixture& F)
        {
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::Expired);
            TestEqual(TEXT("expired join has no local member ID"), F.Client.Snapshot().Get_LocalMemberId(), FGuid{});
            TestEqual(TEXT("expired join preserves one-member host roster"), F.Server.Snapshot().Get_Members().Num(), 1);
            TestEqual(TEXT("expired invite does not rotate host epoch"), F.Server.Snapshot().Get_LobbyEpoch(), F.Epoch);
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStop(Fixture));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_OnlineDirect_QueueBound,
    "Ck.Online.Direct.QueueAdmissionBound", ck_test_online_direct::Flags)

bool FCkTest_OnlineDirect_QueueBound::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_direct;

    auto Queue = FService{};
    if (!TestTrue(TEXT("bounded queue service starts"), Queue.Start(TEXT("Queue"), 17894)))
    { Queue.Stop(); return false; }
    for (int32 Index = 0; Index < 64; ++Index)
    {
        auto Request = FCk_Request_OnlineDirect{};
        Request.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::Host);
        if (!UCk_Utils_OnlineDirect_UE::TryRequest(Queue.Handle, Request))
        { AddError(FString::Printf(TEXT("Queue rejected operation %d before capacity 64."), Index)); break; }
    }
    auto Overflow = FCk_Request_OnlineDirect{};
    Overflow.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::Host);
    TestFalse(TEXT("65th queued request is rejected"), UCk_Utils_OnlineDirect_UE::TryRequest(Queue.Handle, Overflow));
    TestEqual(TEXT("queued work has no completion before scheduler"),
        UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Queue.Handle), 0);
    TestEqual(TEXT("queue limit does not open a listener before scheduler"), Queue.Snapshot().Get_State(),
        ECk_OnlineDirect_State::Offline);
    Queue.Host.Reset();
    Queue.Feature.Reset();
    TestFalse(TEXT("final owner release invalidates queued service handle"),
        UCk_Utils_OnlineDirect_UE::Has(Queue.Handle));
    auto AfterRelease = FCk_Request_OnlineDirect{};
    AfterRelease.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::Host);
    TestFalse(TEXT("released service rejects a valid new request"),
        UCk_Utils_OnlineDirect_UE::TryRequest(Queue.Handle, AfterRelease));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_OnlineDirect_ShutdownDuringCompletion,
    "Ck.Online.Direct.CompletionShutdownAndPortReuse", ck_test_online_direct::Flags)

bool FCkTest_OnlineDirect_ShutdownDuringCompletion::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_direct;
    auto Service = FService{};
    if (!TestTrue(TEXT("real TCP host service starts"), Service.Start(TEXT("CallbackHost"), 17895)))
    { Service.Stop(); return false; }

    auto Listener = TStrongObjectPtr<UCk_Test_OnlineDirectListener_UE>{
        NewObject<UCk_Test_OnlineDirectListener_UE>()};
    bool ShutdownIssued = false;
    Listener->OnCompletion = [&]()
    {
        if (!ShutdownIssued)
        {
            ShutdownIssued = true;
            Service.Host->Request_Shutdown();
        }
    };
    auto Delegate = FCk_Delegate_OnlineDirect_Completed{};
    Delegate.BindDynamic(Listener.Get(), &UCk_Test_OnlineDirectListener_UE::OnCompleted);
    UCk_Utils_OnlineDirect_UE::BindTo_OnCompleted(Service.Handle, Delegate,
        ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);

    TestTrue(TEXT("host operation admitted"), Service.Request(ECk_OnlineDirect_Action::Host));
    const auto HostOperation = Service.Operation;
    auto Ready = FCk_Request_OnlineDirect{};
    const auto ReadyOperation = FGuid::NewGuid();
    Ready.Set_OperationId(ReadyOperation).Set_Action(ECk_OnlineDirect_Action::SetReady).Set_Ready(true);
    TestTrue(TEXT("sibling ready operation admitted before scheduler"),
        UCk_Utils_OnlineDirect_UE::TryRequest(Service.Handle, Ready));
    TestEqual(TEXT("neither completion dispatched before scheduler"), Listener->Completions.Num(), 0);

    Service.Tick(FPlatformTime::Seconds());
    TestTrue(TEXT("first completion requested shutdown from signal"), ShutdownIssued);
    TestEqual(TEXT("each owned terminal completion dispatched once"), Listener->Completions.Num(), 2);
    if (Listener->Completions.Num() == 2)
    {
        TestEqual(TEXT("first completion identifies host request"),
            Listener->Completions[0].Get_OperationId(), HostOperation);
        TestEqual(TEXT("host socket action succeeded before callback shutdown"),
            Listener->Completions[0].Get_Result(), ECk_OnlineDirect_Result::Succeeded);
        TestEqual(TEXT("second completion identifies ready request"),
            Listener->Completions[1].Get_OperationId(), ReadyOperation);
        TestEqual(TEXT("already terminal sibling remains succeeded"),
            Listener->Completions[1].Get_Result(), ECk_OnlineDirect_Result::Succeeded);
        TestTrue(TEXT("both callbacks identify original typed service"),
            Listener->Services[0] == Service.Handle && Listener->Services[1] == Service.Handle);
    }
    TestFalse(TEXT("callback shutdown invalidates typed handle"), UCk_Utils_OnlineDirect_UE::Has(Service.Handle));
    auto AfterStop = FCk_Request_OnlineDirect{};
    AfterStop.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::Host);
    TestFalse(TEXT("stale typed handle rejects valid operation"),
        UCk_Utils_OnlineDirect_UE::TryRequest(Service.Handle, AfterStop));

    auto Rebound = FService{};
    if (!TestTrue(TEXT("new service starts on released port"), Rebound.Start(TEXT("Rebound"), 17895)))
    { Service.Stop(); Rebound.Stop(); return false; }
    TestTrue(TEXT("new host request admitted"), Rebound.Request(ECk_OnlineDirect_Action::Host));
    Rebound.Tick(FPlatformTime::Seconds());
    Rebound.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
    TestEqual(TEXT("new service bound and hosts on same port"), Rebound.Snapshot().Get_State(),
        ECk_OnlineDirect_State::Hosting);
    Rebound.Stop();
    Service.Stop();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_OnlineDirect_ExtremeClock,
    "Ck.Online.Direct.ExtremeClockRejectsBeforeTransport", ck_test_online_direct::Flags)

bool FCkTest_OnlineDirect_ExtremeClock::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_direct;
    constexpr auto Port = 17896;
    const auto ExtremeTime = std::numeric_limits<double>::max();
    auto Fixture = MakeShared<FFixture>();
    Fixture->Test = this;
    if (!TestTrue(TEXT("extreme-clock feature and host start"),
            Fixture->SamePort.Start(TEXT("ExtremeHost"), Port, 30.0, ExtremeTime)))
    { Fixture->Stop(); return false; }
    TestTrue(TEXT("extreme-clock Host request admitted"), Fixture->SamePort.Request(ECk_OnlineDirect_Action::Host));
    Fixture->SamePort.Tick(ExtremeTime);
    Fixture->SamePort.ExpectCompletion(*this, ECk_OnlineDirect_Result::InvalidInput);
    const auto RejectedHost = Fixture->SamePort.Snapshot();
    TestEqual(TEXT("rejected Host remains Offline"), RejectedHost.Get_State(), ECk_OnlineDirect_State::Offline);
    TestFalse(TEXT("rejected Host publishes no epoch"), RejectedHost.Get_LobbyEpoch().IsValid());
    TestEqual(TEXT("rejected Host publishes no roster"), RejectedHost.Get_Members().Num(), 0);
    TestTrue(TEXT("rejected Host publishes no invite"), RejectedHost.Get_JoinDescriptor().IsEmpty());

    if (!TestTrue(TEXT("normal-clock feature starts on same port while rejected owner lives"),
            Fixture->Server.Start(TEXT("NormalHost"), Port)))
    { Fixture->Stop(); return false; }
    TestTrue(TEXT("normal-clock Host request admitted"), Fixture->Server.Request(ECk_OnlineDirect_Action::Host));
    Fixture->Server.Tick(FPlatformTime::Seconds());
    Fixture->Server.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
    const auto Hosted = Fixture->Server.Snapshot();
    TestEqual(TEXT("normal-clock host bound the same TCP port"), Hosted.Get_State(), ECk_OnlineDirect_State::Hosting);
    TestEqual(TEXT("bound host has one member"), Hosted.Get_Members().Num(), 1);
    TestTrue(TEXT("bound host has valid invite"), ck::direct::FTransport::ValidateDescriptor(Hosted.Get_JoinDescriptor()));

    if (!TestTrue(TEXT("extreme-clock joiner starts"),
            Fixture->WrongKey.Start(TEXT("ExtremeJoin"), 17897, 30.0, ExtremeTime)))
    { Fixture->Stop(); return false; }
    TestTrue(TEXT("extreme-clock Join request admitted for terminal result"),
        Fixture->WrongKey.Request(ECk_OnlineDirect_Action::Join, Hosted.Get_JoinDescriptor()));
    Fixture->WrongKey.Tick(ExtremeTime);
    Fixture->WrongKey.ExpectCompletion(*this, ECk_OnlineDirect_Result::InvalidInput);
    const auto RejectedJoin = Fixture->WrongKey.Snapshot();
    TestEqual(TEXT("rejected Join remains Offline"), RejectedJoin.Get_State(), ECk_OnlineDirect_State::Offline);
    TestFalse(TEXT("rejected Join publishes no epoch"), RejectedJoin.Get_LobbyEpoch().IsValid());
    TestEqual(TEXT("rejected Join publishes no roster"), RejectedJoin.Get_Members().Num(), 0);
    TestTrue(TEXT("rejected Join publishes no invite"), RejectedJoin.Get_JoinDescriptor().IsEmpty());
    Fixture->Server.Tick(FPlatformTime::Seconds());
    TestEqual(TEXT("extreme-clock Join created no host member"), Fixture->Server.Snapshot().Get_Members().Num(), 1);
    Fixture->SamePort.Stop();
    Fixture->WrongKey.Stop();

    if (!TestTrue(TEXT("normal-clock client starts"), Fixture->Client.Start(TEXT("ClockClient"), 17898)))
    { Fixture->Stop(); return false; }
    TestTrue(TEXT("normal-clock client Join admitted"),
        Fixture->Client.Request(ECk_OnlineDirect_Action::Join, Hosted.Get_JoinDescriptor()));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("normal client joins real TCP host before clock edge"),
        [](FFixture& F)
        {
            return F.Client.Completed() && F.Client.Snapshot().Get_State() == ECk_OnlineDirect_State::Joined
                && F.Server.Snapshot().Get_Members().Num() == 2 && F.Client.Snapshot().Get_Members().Num() == 2;
        }, [this, ExtremeTime](FFixture& F)
        {
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::Succeeded);
            const auto ClientId = F.Client.Snapshot().Get_LocalMemberId();
            TestTrue(TEXT("client has admitted identity"), ClientId.IsValid());
            TestTrue(TEXT("host sees admitted client not ready"),
                HasMember(F.Server.Snapshot(), ClientId, TEXT("ClockClient"), false));

            TestTrue(TEXT("ready request admitted before extreme tick"),
                F.Client.Request(ECk_OnlineDirect_Action::SetReady, {}, true));
            F.Client.Tick(ExtremeTime);
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::InvalidInput);
            TestEqual(TEXT("ready rejection leaves client Joined"), F.Client.Snapshot().Get_State(),
                ECk_OnlineDirect_State::Joined);
            TestTrue(TEXT("ready rejection leaves local member unready"),
                HasMember(F.Client.Snapshot(), ClientId, TEXT("ClockClient"), false));

            TestTrue(TEXT("text request admitted before extreme tick"),
                F.Client.Request(ECk_OnlineDirect_Action::SendText, TEXT("must not send")));
            F.Client.Tick(ExtremeTime);
            F.Client.ExpectCompletion(*this, ECk_OnlineDirect_Result::InvalidInput);
            TestEqual(TEXT("text rejection leaves client Joined"), F.Client.Snapshot().Get_State(),
                ECk_OnlineDirect_State::Joined);
            TestEqual(TEXT("text rejection leaves client transcript empty"), F.Client.Snapshot().Get_Messages().Num(), 0);

            F.Server.Tick(FPlatformTime::Seconds());
            TestTrue(TEXT("rejected ready never changed host roster"),
                HasMember(F.Server.Snapshot(), ClientId, TEXT("ClockClient"), false));
            TestEqual(TEXT("rejected text never reached host transcript"), F.Server.Snapshot().Get_Messages().Num(), 0);
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FStop(Fixture));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
