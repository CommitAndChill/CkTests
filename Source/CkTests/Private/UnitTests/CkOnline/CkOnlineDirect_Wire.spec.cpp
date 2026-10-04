#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/Direct/CkOnlineDirect_Feature.h"
#include "CkOnline/Direct/CkOnlineDirect_Utils.h"

#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Misc/AutomationTest.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_online_direct_wire
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

        auto Start(const TCHAR* Name, int32 Port) -> bool
        {
            auto Spec = FCk_OnlineDirect_Spec{};
            Spec.Set_Name(Name).Set_AdvertisedAddress(TEXT("127.0.0.1"))
                .Set_Port(Port).Set_Capacity(2).Set_InviteLifetimeSeconds(30.0)
                .Set_OperationTimeoutSeconds(3.0);
            Feature = ck::direct::FFeature::TryCreate(Spec);
            if (!Feature) return false;
            Host = ck::FOnlineServiceHost::TryCreateFeature(
                StaticCastSharedRef<ck::IOnlineServiceFeature>(Feature.ToSharedRef()),
                FCk_Time{FPlatformTime::Seconds()});
            if (!Host) return false;
            Handle = UCk_Utils_OnlineDirect_UE::Cast(Host->Get_Handle());
            return UCk_Utils_OnlineDirect_UE::Has(Handle);
        }
        auto Request(ECk_OnlineDirect_Action Action, const FString& Text = {}, bool Ready = false) -> bool
        {
            Operation = FGuid::NewGuid();
            BeforeCompletion = UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Handle);
            auto Request = FCk_Request_OnlineDirect{};
            Request.Set_OperationId(Operation).Set_Action(Action).Set_Text(Text).Set_Ready(Ready);
            return UCk_Utils_OnlineDirect_UE::TryRequest(Handle, Request);
        }
        auto Completed() const -> bool
        { return UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Handle) > BeforeCompletion; }
        auto Snapshot() const -> FCk_OnlineDirect_Snapshot
        { return UCk_Utils_OnlineDirect_UE::Get_Snapshot(Handle); }
        auto Tick(double Now) -> void { if (Host) Host->Tick(FCk_Time{Now}); }
        auto Stop() -> void { if (Host) { Host->Request_Shutdown(); Host.Reset(); } Feature.Reset(); }
    };

    struct FFixture
    {
        FAutomationTestBase* Test = nullptr;
        FService Server;
        FService Client;
        bool Aborted = false;
        double AttackAt = 0;
        int32 BaselineRevision = 0;
        int32 ServerCompletions = 0;
        int32 ClientCompletions = 0;
        FGuid ClientId;
        FString Descriptor;

        auto Tick() -> void
        {
            const double Now = FPlatformTime::Seconds();
            Server.Tick(Now); Client.Tick(Now);
        }
        auto Stop() -> void { Client.Stop(); Server.Stop(); }
        auto AssertUnchanged() -> void
        {
            const auto Host = Server.Snapshot();
            const auto Guest = Client.Snapshot();
            Test->TestEqual(TEXT("malformed peer leaves host online"), Host.Get_State(), ECk_OnlineDirect_State::Hosting);
            Test->TestEqual(TEXT("malformed peer does not enter host roster"), Host.Get_Members().Num(), 2);
            Test->TestEqual(TEXT("malformed peer does not alter host revision"), Host.Get_Revision(), BaselineRevision);
            Test->TestEqual(TEXT("malformed peer does not produce host completion"),
                UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Server.Handle), ServerCompletions);
            Test->TestEqual(TEXT("malformed peer leaves healthy client joined"), Guest.Get_State(), ECk_OnlineDirect_State::Joined);
            Test->TestEqual(TEXT("malformed peer does not produce client completion"),
                UCk_Utils_OnlineDirect_UE::Get_CompletedCount(Client.Handle), ClientCompletions);
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
            if (Fixture->Aborted) return true;
            if (Started < 0.0) Started = FPlatformTime::Seconds();
            Fixture->Tick();
            if (Condition(*Fixture))
            {
                if (Action) Action(*Fixture);
                return true;
            }
            if (FPlatformTime::Seconds() - Started < StepTimeoutSeconds) return false;
            Fixture->Test->AddError(FString::Printf(TEXT("Direct wire test timed out: %s"), *Label));
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

    auto SendRaw(const TArray<uint8>& Bytes) -> bool
    {
        auto* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!Subsystem) return false;
        FSocket* Socket = Subsystem->CreateSocket(NAME_Stream, TEXT("CkOnlineDirectMalformedPeer"), false);
        if (!Socket) return false;
        bool ValidIp = false;
        auto Address = Subsystem->CreateInternetAddr();
        Address->SetIp(TEXT("127.0.0.1"), ValidIp);
        Address->SetPort(17890);
        bool SentAll = ValidIp && Socket->Connect(*Address);
        int32 Offset = 0;
        while (SentAll && Offset < Bytes.Num())
        {
            int32 Sent = 0;
            SentAll = Socket->Send(Bytes.GetData() + Offset, Bytes.Num() - Offset, Sent) && Sent > 0;
            Offset += Sent;
        }
        Socket->Close();
        Subsystem->DestroySocket(Socket);
        return SentAll && Offset == Bytes.Num();
    }
    auto Frame(const TArray<uint8>& Body) -> TArray<uint8>
    {
        TArray<uint8> Result;
        const uint32 Length = Body.Num();
        Result.Add((Length >> 24) & 255); Result.Add((Length >> 16) & 255);
        Result.Add((Length >> 8) & 255); Result.Add(Length & 255);
        Result.Append(Body);
        return Result;
    }
    auto AfterAttack(FFixture& F) -> bool
    { return FPlatformTime::Seconds() - F.AttackAt >= 0.1; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_OnlineDirect_MalformedWireIsolation,
    "Ck.Online.Direct.MalformedWireIsolation", ck_test_online_direct_wire::Flags)

bool FCkTest_OnlineDirect_MalformedWireIsolation::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_direct_wire;
    auto Fixture = MakeShared<FFixture>();
    Fixture->Test = this;
    if (!TestTrue(TEXT("production host feature starts"), Fixture->Server.Start(TEXT("Host"), 17890)) ||
        !TestTrue(TEXT("production client feature starts"), Fixture->Client.Start(TEXT("Client"), 17895)))
    { Fixture->Stop(); return false; }

    TestTrue(TEXT("host request admitted"), Fixture->Server.Request(ECk_OnlineDirect_Action::Host));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host binds"), [](FFixture& F)
    { return F.Server.Completed() && F.Server.Snapshot().Get_State() == ECk_OnlineDirect_State::Hosting; },
    [this](FFixture& F)
    {
        TestEqual(TEXT("host succeeds"), UCk_Utils_OnlineDirect_UE::Get_LastCompletion(F.Server.Handle).Get_Result(),
            ECk_OnlineDirect_Result::Succeeded);
        F.Descriptor = F.Server.Snapshot().Get_JoinDescriptor();
        TestTrue(TEXT("healthy join admitted"), F.Client.Request(ECk_OnlineDirect_Action::Join, F.Descriptor));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("healthy client joins"), [](FFixture& F)
    { return F.Client.Completed() && F.Client.Snapshot().Get_State() == ECk_OnlineDirect_State::Joined &&
        F.Server.Snapshot().Get_Members().Num() == 2; }, [this](FFixture& F)
    {
        TestEqual(TEXT("healthy join succeeds"), UCk_Utils_OnlineDirect_UE::Get_LastCompletion(F.Client.Handle).Get_Result(),
            ECk_OnlineDirect_Result::Succeeded);
        F.ClientId = F.Client.Snapshot().Get_LocalMemberId();
        TestTrue(TEXT("healthy client has member identity"), F.ClientId.IsValid());
        F.BaselineRevision = F.Server.Snapshot().Get_Revision();
        F.ServerCompletions = UCk_Utils_OnlineDirect_UE::Get_CompletedCount(F.Server.Handle);
        F.ClientCompletions = UCk_Utils_OnlineDirect_UE::Get_CompletedCount(F.Client.Handle);
        TestTrue(TEXT("oversized frame header sent"), SendRaw({0, 0, 16, 1}));
        F.AttackAt = FPlatformTime::Seconds();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("oversized frame isolated"), AfterAttack,
    [this](FFixture& F)
    {
        F.AssertUnchanged();
        TestTrue(TEXT("invalid UTF-8 frame sent"), SendRaw(Frame({0xC0, 0xAF})));
        F.AttackAt = FPlatformTime::Seconds();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("invalid UTF-8 isolated"), AfterAttack,
    [this](FFixture& F)
    {
        F.AssertUnchanged();
        TestTrue(TEXT("malformed JSON frame sent"), SendRaw(Frame({static_cast<uint8>('{')})));
        F.AttackAt = FPlatformTime::Seconds();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("malformed JSON isolated"), AfterAttack,
    [this](FFixture& F)
    {
        F.AssertUnchanged();
        const ANSICHAR* WrongVersion = "{\"t\":\"join\",\"v\":2}";
        TArray<uint8> Body;
        for (const ANSICHAR* C = WrongVersion; *C; ++C) Body.Add(static_cast<uint8>(*C));
        TestTrue(TEXT("wrong-version frame sent"), SendRaw(Frame(Body)));
        F.AttackAt = FPlatformTime::Seconds();
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("wrong version isolated"), AfterAttack,
    [this](FFixture& F)
    {
        F.AssertUnchanged();
        TestTrue(TEXT("healthy client ready request admitted"),
            F.Client.Request(ECk_OnlineDirect_Action::SetReady, {}, true));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("healthy ready succeeds after attacks"), [](FFixture& F)
    {
        return F.Client.Completed() && F.Server.Snapshot().Get_Members().ContainsByPredicate(
            [&](const FCk_OnlineDirect_Member& Member)
            { return Member.Get_Id() == F.ClientId && Member.Get_Ready(); });
    }, [this](FFixture& F)
    {
        TestEqual(TEXT("healthy ready ACK succeeds"), UCk_Utils_OnlineDirect_UE::Get_LastCompletion(F.Client.Handle).Get_Result(),
            ECk_OnlineDirect_Result::Succeeded);
        TestTrue(TEXT("healthy text admitted after attacks"),
            F.Client.Request(ECk_OnlineDirect_Action::SendText, TEXT("still healthy")));
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("healthy text succeeds after attacks"), [](FFixture& F)
    {
        return F.Client.Completed() && F.Server.Snapshot().Get_Messages().ContainsByPredicate(
            [&](const FCk_OnlineDirect_Message& Message)
            { return Message.Get_SenderId() == F.ClientId && Message.Get_Text() == TEXT("still healthy"); });
    }, [this](FFixture& F)
    {
        TestEqual(TEXT("healthy text ACK succeeds"), UCk_Utils_OnlineDirect_UE::Get_LastCompletion(F.Client.Handle).Get_Result(),
            ECk_OnlineDirect_Result::Succeeded);
        TestEqual(TEXT("malformed peers never entered host roster"), F.Server.Snapshot().Get_Members().Num(), 2);
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FStop(Fixture));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
