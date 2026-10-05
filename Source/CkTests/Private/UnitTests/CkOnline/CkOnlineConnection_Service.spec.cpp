#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/Connection/CkOnlineConnection_Feature.h"
#include "CkOnline/Connection/CkOnlineConnection_Utils.h"
#include "CkOnline/Direct/CkOnlineDirect_Feature.h"
#include "CkOnline/Direct/CkOnlineDirect_Utils.h"
#include "CkTest_OnlineConnectionListener.h"
#include "CkTest_OnlineServiceListener.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_online_connection
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

    auto SameKey(const FCk_OnlineConnection_Operation& A, const FCk_OnlineConnection_Operation& B) -> bool
    { return A.Get_Id() == B.Get_Id() && A.Get_Generation() == B.Get_Generation(); }

    auto ValidKey(const FCk_OnlineConnection_Operation& Key) -> bool
    { return Key.Get_Id().IsValid() && Key.Get_Generation().IsValid(); }

    // Controlled adapter observations prove feature protocol only, never Unreal travel or driver readiness.
    struct FTransport final : ck::connection::ITransport
    {
        mutable int32 CanEnterCalls = 0;
        mutable int32 IdleCalls = 0;
        int32 EntryCalls = 0;
        int32 ObserveCalls = 0;
        int32 ReturnCalls = 0;
        int32 ShutdownCalls = 0;
        bool Idle = true;
        bool CanEnterResult = true;
        ck::connection::EReturnAdmission ReturnAdmission = ck::connection::EReturnAdmission::Started;
        ck::connection::FObservation Observation;
        mutable TFunction<void()> OnCanEnter;
        TFunction<void()> OnObserve;
        TFunction<void()> OnBeginEntry;

        auto CanEnter(const ck::connection::FEntryIntent&) const -> bool override
        { ++CanEnterCalls; if (OnCanEnter) { OnCanEnter(); } return CanEnterResult; }
        auto IsIdle() const -> bool override { ++IdleCalls; return Idle; }
        auto BeginEntry(const ck::connection::FEntryIntent&) -> bool override
        { ++EntryCalls; Idle = false; if (OnBeginEntry) { OnBeginEntry(); } return true; }
        auto Observe() -> ck::connection::FObservation override
        { ++ObserveCalls; if (OnObserve) { OnObserve(); } return Observation; }
        auto BeginReturn(const ck::connection::FEntryIntent&) -> ck::connection::EReturnAdmission override
        { ++ReturnCalls; return ReturnAdmission; }
        auto Shutdown() -> void override { ++ShutdownCalls; }
        auto Calls() const -> int32
        { return CanEnterCalls + IdleCalls + EntryCalls + ObserveCalls + ReturnCalls + ShutdownCalls; }
    };

    struct FFixture
    {
        TSharedRef<FTransport> Transport = MakeShared<FTransport>();
        TSharedPtr<ck::direct::FFeature> DirectFeature;
        TSharedPtr<ck::FOnlineServiceHost> DirectHost;
        FCk_Handle_OnlineDirect DirectHandle;
        TSharedPtr<ck::connection::FFeature> Feature;
        TSharedPtr<ck::FOnlineServiceHost> Host;
        FCk_Handle_OnlineConnection Handle;
        FCk_OnlineConnection_Spec Spec;
        TStrongObjectPtr<UCk_Test_OnlineConnectionListener_UE> Listener{NewObject<UCk_Test_OnlineConnectionListener_UE>()};
        TStrongObjectPtr<UCk_Test_OnlineServiceListener_UE> GenericListener{NewObject<UCk_Test_OnlineServiceListener_UE>()};

        ~FFixture()
        {
            GenericListener->OnRequestCompletion = {};
            Transport->OnCanEnter = {};
            Transport->OnObserve = {};
            Transport->OnBeginEntry = {};
            if (Host) { Host->Request_Shutdown(); }
            if (DirectHost) { DirectHost->Request_Shutdown(); }
        }

        auto Start(int32 Port) -> bool
        {
            auto DirectSpec = FCk_OnlineDirect_Spec{};
            DirectSpec.Set_Name(TEXT("ConnectionProtocolHost")).Set_AdvertisedAddress(TEXT("127.0.0.1"))
                .Set_Port(Port).Set_Capacity(2).Set_InviteLifetimeSeconds(30.0).Set_OperationTimeoutSeconds(3.0);
            DirectFeature = ck::direct::FFeature::TryCreate(DirectSpec);
            if (!DirectFeature) { return false; }
            DirectHost = ck::FOnlineServiceHost::TryCreateFeature(
                StaticCastSharedRef<ck::IOnlineServiceFeature>(DirectFeature.ToSharedRef()), FCk_Time{});
            if (!DirectHost) { return false; }
            DirectHandle = UCk_Utils_OnlineDirect_UE::Cast(DirectHost->Get_Handle());
            auto Request = FCk_Request_OnlineDirect{};
            Request.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::Host);
            if (!UCk_Utils_OnlineDirect_UE::TryRequest(DirectHandle, Request)) { return false; }
            DirectHost->Tick(FCk_Time{});
            if (UCk_Utils_OnlineDirect_UE::Get_CompletedCount(DirectHandle) != 1 ||
                UCk_Utils_OnlineDirect_UE::Get_LastCompletion(DirectHandle).Get_Result() != ECk_OnlineDirect_Result::Succeeded)
            { return false; }
            Request = FCk_Request_OnlineDirect{};
            Request.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineDirect_Action::SetReady).Set_Ready(true);
            if (!UCk_Utils_OnlineDirect_UE::TryRequest(DirectHandle, Request)) { return false; }
            DirectHost->Tick(FCk_Time{});
            FCk_OnlineDirect_ReadyLobbyBinding Binding;
            if (!FCk_OnlineDirect_ReadyLobbyBinding::TryCapture(
                UCk_Utils_OnlineDirect_UE::Get_Snapshot(DirectHandle), true, 1, Binding))
            { return false; }
            const TSoftObjectPtr<UWorld> Map{FSoftObjectPath{TEXT("/Engine/Maps/Entry.Entry")}};
            Spec.Set_DirectService(DirectHandle).Set_Binding(Binding).Set_ExpectedMembers(1)
                .Set_GameplayMap(Map).Set_GameplayMode(AGameModeBase::StaticClass()).Set_ListenPort(7789)
                .Set_ReturnMap(Map).Set_ReturnMode(AGameModeBase::StaticClass())
                .Set_EntryTimeoutSeconds(5.0).Set_ReturnTimeoutSeconds(2.0);
            Feature = ck::connection::FFeature::TryCreate(StaticCastSharedRef<ck::connection::ITransport>(Transport));
            if (!Feature) { return false; }
            Host = ck::FOnlineServiceHost::TryCreateFeature(
                StaticCastSharedRef<ck::IOnlineServiceFeature>(Feature.ToSharedRef()), FCk_Time{});
            if (!Host) { return false; }
            Handle = UCk_Utils_OnlineConnection_UE::Cast(Host->Get_Handle());
            if (ck::Is_NOT_Valid(Handle)) { return false; }
            FCk_Delegate_OnlineConnection_Completed Delegate;
            Delegate.BindDynamic(Listener.Get(), &UCk_Test_OnlineConnectionListener_UE::OnCompleted);
            UCk_Utils_OnlineConnection_UE::BindTo_OnCompleted(Handle, Delegate,
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
            return true;
        }

        auto Request(ECk_OnlineConnection_Action Action, const FCk_OnlineConnection_Operation& Target,
            FCk_OnlineConnection_Operation& Out) -> bool
        {
            FCk_Request_OnlineConnection Request;
            Request.Set_Action(Action).Set_Spec(Spec).Set_Target(Target);
            FCk_Delegate_Request_OnCompleted Delegate;
            Delegate.BindDynamic(GenericListener.Get(), &UCk_Test_OnlineServiceListener_UE::OnRequestCompleted);
            Request.Set_CompletionDelegate(Delegate);
            return UCk_Utils_OnlineConnection_UE::TryRequest(Handle, Request, Out);
        }
        auto Tick(double Now) -> void { Host->Tick(FCk_Time{Now}); }
        auto Snapshot() const -> FCk_OnlineConnection_Snapshot
        { return UCk_Utils_OnlineConnection_UE::Get_Snapshot(Handle); }
        auto Completed() const -> int32 { return UCk_Utils_OnlineConnection_UE::Get_CompletedCount(Handle); }
        auto Count(const FCk_OnlineConnection_Operation& Key) const -> int32
        { return Listener->Completions.FilterByPredicate([&](const auto& C) { return SameKey(C.Get_Operation(), Key); }).Num(); }
        auto ExpectResult(FAutomationTestBase& Test, const FCk_OnlineConnection_Operation& Key,
            ECk_OnlineConnection_Result Result) const -> void
        {
            Test.TestEqual(TEXT("operation has exactly one typed terminal completion"), Count(Key), 1);
            for (const auto& Completion : Listener->Completions)
            { if (SameKey(Completion.Get_Operation(), Key)) { Test.TestEqual(TEXT("operation terminal result"), Completion.Get_Result(), Result); } }
        }
        auto ExpectRejected(FAutomationTestBase& Test, ECk_OnlineConnection_Action Action,
            const FCk_OnlineConnection_Operation& Target) -> void
        {
            const auto Before = Snapshot();
            const auto CountBefore = Completed();
            const auto CallsBefore = Transport->Calls();
            auto Out = Target;
            Out.Set_Id(FGuid::NewGuid()).Set_Generation(FGuid::NewGuid());
            Test.TestFalse(TEXT("stale or inadmissible protocol request rejects synchronously"), Request(Action, Target, Out));
            Test.TestFalse(TEXT("rejection clears operation output"), Out.Get_Id().IsValid() || Out.Get_Generation().IsValid());
            Test.TestEqual(TEXT("rejection makes no transport adapter call"), Transport->Calls(), CallsBefore);
            Test.TestEqual(TEXT("rejection preserves state"), Snapshot().Get_State(), Before.Get_State());
            Test.TestEqual(TEXT("rejection preserves snapshot revision"), Snapshot().Get_Revision(), Before.Get_Revision());
            Test.TestEqual(TEXT("rejection preserves endpoint"), Snapshot().Get_Endpoint(), Before.Get_Endpoint());
            Test.TestEqual(TEXT("rejection preserves hosting intent"), Snapshot().Get_Hosting(), Before.Get_Hosting());
            Test.TestEqual(TEXT("rejection preserves error readback"), Snapshot().Get_LastError(), Before.Get_LastError());
            Test.TestTrue(TEXT("rejection preserves operation and retained session"),
                SameKey(Snapshot().Get_Operation(), Before.Get_Operation()) && SameKey(Snapshot().Get_Session(), Before.Get_Session()));
            Test.TestEqual(TEXT("rejection dispatches no completion"), Completed(), CountBefore);
        }
        auto EnterActive(FAutomationTestBase& Test, FCk_OnlineConnection_Operation& Entry) -> bool
        {
            if (!Test.TestTrue(TEXT("real one-member ready control binding admits unit-protocol entry"), Request(ECk_OnlineConnection_Action::Host, {}, Entry)))
            { return false; }
            Tick(0.0);
            Transport->Observation.TransportEstablished = true;
            Tick(1.0);
            FCk_OnlineConnection_Operation Confirm;
            if (!Test.TestTrue(TEXT("matching explicit readiness is admitted"), Request(ECk_OnlineConnection_Action::ConfirmGameReady, Entry, Confirm)))
            { return false; }
            Tick(2.0);
            Test.TestEqual(TEXT("confirmed entry is Active"), Snapshot().Get_State(), ECk_OnlineConnection_State::Active);
            ExpectResult(Test, Entry, ECk_OnlineConnection_Result::Succeeded);
            ExpectResult(Test, Confirm, ECk_OnlineConnection_Result::Succeeded);
            return Snapshot().Get_State() == ECk_OnlineConnection_State::Active;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineConnectionAdmission, "Ck.Online.Connection.ProtocolAdmissionAndReadiness", ck_test_online_connection::Flags)
bool FCkOnlineConnectionAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_connection;
    FFixture F;
    if (!TestTrue(TEXT("protocol fixture binds a real TCP host and ready member"), F.Start(17911))) { return false; }
    FCk_Request_OnlineConnection Bad;
    FCk_OnlineConnection_Operation Out;
    FCk_Handle_OnlineConnection InvalidHandle;
    Out.Set_Id(FGuid::NewGuid()).Set_Generation(FGuid::NewGuid());
    TestFalse(TEXT("default connection handle rejects total input"),
        UCk_Utils_OnlineConnection_UE::TryRequest(InvalidHandle, Bad, Out));
    TestFalse(TEXT("default handle clears operation output"), Out.Get_Id().IsValid() || Out.Get_Generation().IsValid());
    Out.Set_Id(FGuid::NewGuid()).Set_Generation(FGuid::NewGuid());
    const auto InitialCalls = F.Transport->Calls();
    TestFalse(TEXT("default request rejects"), UCk_Utils_OnlineConnection_UE::TryRequest(F.Handle, Bad, Out));
    TestFalse(TEXT("default request clears output"), ValidKey(Out) || Out.Get_Id().IsValid() || Out.Get_Generation().IsValid());
    TestEqual(TEXT("default request invokes no adapter"), F.Transport->Calls(), InitialCalls);
    for (int32 Case = 0; Case < 8; ++Case)
    {
        auto Spec = F.Spec;
        switch (Case)
        {
            case 0: Spec.Set_Binding(FCk_OnlineDirect_ReadyLobbyBinding{}); break;
            case 1: Spec.Set_ExpectedMembers(0); break;
            case 2: Spec.Set_ListenPort(0); break;
            case 3: Spec.Set_GameplayMap(TSoftObjectPtr<UWorld>{}); break;
            case 4: Spec.Set_ReturnMode(TSubclassOf<AGameModeBase>{}); break;
            case 5: Spec.Set_EntryTimeoutSeconds(std::numeric_limits<double>::quiet_NaN()); break;
            case 6: Spec.Set_ReturnTimeoutSeconds(0.0); break;
        }
        Bad.Set_Action(Case == 7 ? static_cast<ECk_OnlineConnection_Action>(255) : ECk_OnlineConnection_Action::Host).Set_Spec(Spec);
        Out.Set_Id(FGuid::NewGuid()).Set_Generation(FGuid::NewGuid());
        const auto Calls = F.Transport->Calls();
        const auto Before = F.Snapshot();
        TestFalse(TEXT("malformed entry rejects before adapter access"), UCk_Utils_OnlineConnection_UE::TryRequest(F.Handle, Bad, Out));
        TestFalse(TEXT("malformed entry resets output"), Out.Get_Id().IsValid() || Out.Get_Generation().IsValid());
        TestEqual(TEXT("malformed entry invokes no adapter"), F.Transport->Calls(), Calls);
        TestEqual(TEXT("malformed entry preserves snapshot revision"), F.Snapshot().Get_Revision(), Before.Get_Revision());
        TestEqual(TEXT("malformed entry emits no completion"), F.Completed(), 0);
    }
    FCk_OnlineConnection_Operation Entry;
    if (!TestTrue(TEXT("valid entry is queued"), F.Request(ECk_OnlineConnection_Action::Host, {}, Entry))) { return false; }
    TestTrue(TEXT("generated entry key has ID and owning connection generation"),
        ValidKey(Entry) && Entry.Get_Generation() == F.Host->Get_Diagnostics().Get_Generation());
    TestEqual(TEXT("admission defers travel submission"), F.Transport->EntryCalls, 0);
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::Host, {});
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::ConfirmGameReady, Entry);
    F.Tick(0.0);
    TestEqual(TEXT("owning tick submits entry once"), F.Transport->EntryCalls, 1);
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::ConfirmGameReady, Entry);
    F.Transport->Observation.TransportEstablished = true;
    F.Tick(1.0);
    TestEqual(TEXT("transport observation awaits explicit game readiness"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::AwaitingGameReady);
    TestEqual(TEXT("transport establishment is not terminal entry success"), F.Completed(), 0);
    auto Stale = Entry;
    Stale.Set_Generation(FGuid::NewGuid());
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::ConfirmGameReady, Stale);
    Stale = Entry;
    Stale.Set_Id(FGuid::NewGuid());
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::ConfirmGameReady, Stale);
    FCk_OnlineConnection_Operation Confirm;
    if (!TestTrue(TEXT("exact key readiness is accepted"), F.Request(ECk_OnlineConnection_Action::ConfirmGameReady, Entry, Confirm))) { return false; }
    TestTrue(TEXT("confirm has a separate key in the same connection generation"), ValidKey(Confirm) && !SameKey(Entry, Confirm) && Entry.Get_Generation() == Confirm.Get_Generation());
    F.Tick(2.0);
    F.ExpectResult(*this, Entry, ECk_OnlineConnection_Result::Succeeded);
    F.ExpectResult(*this, Confirm, ECk_OnlineConnection_Result::Succeeded);
    TestEqual(TEXT("entry and explicit confirm each complete once"), F.Completed(), 2);
    TestTrue(TEXT("successful entry retains immutable session identity"), SameKey(F.Snapshot().Get_Session(), Entry));
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::ConfirmGameReady, Entry);
    Stale = Entry;
    Stale.Set_Generation(FGuid::NewGuid());
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::Return, Stale);
    Stale = Entry;
    Stale.Set_Id(FGuid::NewGuid());
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::Return, Stale);
    FCk_OnlineConnection_Operation Return;
    if (!TestTrue(TEXT("retained session admits a separate return transaction"), F.Request(ECk_OnlineConnection_Action::Return, Entry, Return))) { return false; }
    TestTrue(TEXT("return key differs from completed entry"), ValidKey(Return) && !SameKey(Return, Entry));
    F.Tick(3.0);
    TestEqual(TEXT("return submits once"), F.Transport->ReturnCalls, 1);
    F.Transport->Observation = {};
    F.Transport->Observation.ReturnEstablished = true;
    F.Transport->Idle = true;
    F.Tick(4.0);
    F.ExpectResult(*this, Return, ECk_OnlineConnection_Result::Succeeded);
    TestEqual(TEXT("successful return reaches Idle"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::Idle);
    TestEqual(TEXT("successful return does not complete entry again"), F.Count(Entry), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineConnectionDeadline, "Ck.Online.Connection.ProtocolDeadlineAndBoundedReturn", ck_test_online_connection::Flags)
bool FCkOnlineConnectionDeadline::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_connection;
    FFixture F;
    if (!TestTrue(TEXT("deadline protocol fixture has real ready control"), F.Start(17912))) { return false; }
    FCk_OnlineConnection_Operation Entry;
    if (!F.Request(ECk_OnlineConnection_Action::Host, {}, Entry)) { AddError(TEXT("Entry was not admitted.")); return false; }
    F.Tick(0.0);
    F.Transport->Observation.TransportEstablished = true;
    F.Tick(4.0);
    TestEqual(TEXT("late transport observation still awaits readiness"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::AwaitingGameReady);
    TestEqual(TEXT("awaiting readiness is nonterminal"), F.Completed(), 0);
    F.Transport->ReturnAdmission = ck::connection::EReturnAdmission::Busy;
    F.Tick(5.1);
    F.ExpectResult(*this, Entry, ECk_OnlineConnection_Result::TimedOut);
    const auto Recovery = F.Snapshot().Get_Operation();
    TestTrue(TEXT("entry timeout creates a separate bounded recovery key"), ValidKey(Recovery) && !SameKey(Recovery, Entry));
    F.Tick(5.2);
    TestEqual(TEXT("busy return waits without changing transaction"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::AwaitingReturn);
    TestTrue(TEXT("busy return retains recovery key"), SameKey(F.Snapshot().Get_Operation(), Recovery));
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::Host, {});
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::Cancel, Entry);
    F.Tick(7.2);
    const auto CallsAfterTimeout = F.Transport->ReturnCalls;
    TestEqual(TEXT("expired return reaches Failed"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::Failed);
    TestEqual(TEXT("entry and recovery each have one terminal completion"), F.Completed(), 2);
    F.ExpectResult(*this, Recovery, ECk_OnlineConnection_Result::ReturnFailed);
    F.Tick(8.0);
    TestEqual(TEXT("failed return is never retried"), F.Transport->ReturnCalls, CallsAfterTimeout);
    TestEqual(TEXT("failed return emits no duplicate completion"), F.Completed(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineConnectionRejectedReturn, "Ck.Online.Connection.ProtocolRejectedReturnIsTerminal", ck_test_online_connection::Flags)
bool FCkOnlineConnectionRejectedReturn::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_connection;
    FFixture F;
    if (!TestTrue(TEXT("rejected-return fixture has real ready control"), F.Start(17918))) { return false; }
    FCk_OnlineConnection_Operation Entry;
    if (!F.EnterActive(*this, Entry)) { return false; }
    F.Transport->ReturnAdmission = ck::connection::EReturnAdmission::Rejected;
    FCk_OnlineConnection_Operation Return;
    if (!TestTrue(TEXT("active session admits one return operation"),
        F.Request(ECk_OnlineConnection_Action::Return, Entry, Return))) { return false; }
    F.Tick(3.0);
    F.ExpectResult(*this, Return, ECk_OnlineConnection_Result::ReturnFailed);
    F.ExpectResult(*this, Entry, ECk_OnlineConnection_Result::Succeeded);
    TestEqual(TEXT("rejected return reaches terminal Failed state"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::Failed);
    TestEqual(TEXT("rejected return is attempted once"), F.Transport->ReturnCalls, 1);
    TestEqual(TEXT("entry remains completed exactly once"), F.Count(Entry), 1);
    const auto CallsAfterRejection = F.Transport->Calls();
    const auto CompletionsAfterRejection = F.Completed();
    F.Tick(4.0);
    TestEqual(TEXT("terminal rejected return is never retried"), F.Transport->Calls(), CallsAfterRejection);
    TestEqual(TEXT("terminal rejected return emits no duplicate completion"), F.Completed(), CompletionsAfterRejection);
    TestEqual(TEXT("entry and return each own one terminal completion"), F.Completed(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineConnectionRecovery, "Ck.Online.Connection.ProtocolEstablishedLossAndControlStop", ck_test_online_connection::Flags)
bool FCkOnlineConnectionRecovery::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_connection;
    for (int32 Case = 0; Case < 2; ++Case)
    {
        FFixture F;
        if (!TestTrue(TEXT("recovery protocol fixture has real ready control"), F.Start(17913 + Case))) { return false; }
        FCk_OnlineConnection_Operation Entry;
        if (!F.EnterActive(*this, Entry)) { return false; }
        if (Case == 0) { F.Transport->Observation.TransportLost = true; }
        else { F.DirectHost->Request_Shutdown(); }
        F.Tick(3.0);
        const auto Recovery = F.Snapshot().Get_Operation();
        TestTrue(TEXT("established loss or control stop creates fresh recovery transaction"), ValidKey(Recovery) && !SameKey(Recovery, Entry));
        TestEqual(TEXT("recovery exposes a typed loss cause"), F.Snapshot().Get_RecoveryCause(),
            Case == 0 ? ECk_OnlineConnection_Result::TransportError : ECk_OnlineConnection_Result::ControlLost);
        TestEqual(TEXT("recovery does not repeat completed entry"), F.Count(Entry), 1);
        TestEqual(TEXT("loss first creates AwaitingReturn without immediate travel submission"),
            F.Snapshot().Get_State(), ECk_OnlineConnection_State::AwaitingReturn);
        F.ExpectRejected(*this, ECk_OnlineConnection_Action::Host, {});
        F.Transport->Observation = {};
        F.Tick(3.1);
        TestEqual(TEXT("next owning tick submits configured recovery"), F.Transport->ReturnCalls, 1);
        F.Transport->Observation.ReturnEstablished = true;
        F.Transport->Idle = true;
        F.Tick(4.0);
        F.ExpectResult(*this, Recovery, ECk_OnlineConnection_Result::Succeeded);
        const auto Completion = UCk_Utils_OnlineConnection_UE::Get_LastCompletion(F.Handle);
        TestTrue(TEXT("automatic return completion correlates with original session"), SameKey(Completion.Get_Target(), Entry));
        TestEqual(TEXT("successful return retains its typed recovery cause"), Completion.Get_RecoveryCause(),
            Case == 0 ? ECk_OnlineConnection_Result::TransportError : ECk_OnlineConnection_Result::ControlLost);
        TestEqual(TEXT("recovery returns to Idle"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::Idle);
        TestEqual(TEXT("recovery leaves original entry completion exactly once"), F.Count(Entry), 1);
        TestEqual(TEXT("entry confirm and recovery are distinct terminal operations"), F.Completed(), 3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineConnectionCancel, "Ck.Online.Connection.ProtocolCancelWaitsForReturn", ck_test_online_connection::Flags)
bool FCkOnlineConnectionCancel::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_connection;
    FFixture F;
    if (!TestTrue(TEXT("cancel protocol fixture has real ready control"), F.Start(17915))) { return false; }
    FCk_OnlineConnection_Operation Entry;
    if (!F.Request(ECk_OnlineConnection_Action::Host, {}, Entry)) { AddError(TEXT("Entry was not admitted.")); return false; }
    F.Tick(0.0);
    FCk_OnlineConnection_Operation Cancel;
    if (!TestTrue(TEXT("exact pending entry accepts cancel"), F.Request(ECk_OnlineConnection_Action::Cancel, Entry, Cancel))) { return false; }
    F.Tick(0.1);
    F.ExpectResult(*this, Entry, ECk_OnlineConnection_Result::Cancelled);
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::Host, {});
    F.ExpectRejected(*this, ECk_OnlineConnection_Action::Cancel, Entry);
    F.Transport->Observation.ReturnEstablished = true;
    F.Transport->Idle = true;
    F.Tick(0.2);
    TestEqual(TEXT("cancelled entry waits for actual adapter return observation"), F.Snapshot().Get_State(), ECk_OnlineConnection_State::Idle);
    TestEqual(TEXT("cancelled entry completes only once"), F.Count(Entry), 1);
    F.ExpectResult(*this, Cancel, ECk_OnlineConnection_Result::Succeeded);
    TestEqual(TEXT("explicit cancel is itself the return transaction"), F.Completed(), 2);
    FCk_OnlineConnection_Operation Reentry;
    TestTrue(TEXT("settled return permits fresh entry"), F.Request(ECk_OnlineConnection_Action::Host, {}, Reentry));
    TestTrue(TEXT("reentry has fresh operation identity"), ValidKey(Reentry) && !SameKey(Reentry, Entry));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineConnectionCallbackShutdown, "Ck.Online.Connection.ProtocolGenericCallbackShutdown", ck_test_online_connection::Flags)
bool FCkOnlineConnectionCallbackShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_connection;
    FFixture F;
    if (!TestTrue(TEXT("callback protocol fixture has real ready control"), F.Start(17916))) { return false; }
    FCk_OnlineConnection_Operation Entry;
    if (!F.Request(ECk_OnlineConnection_Action::Host, {}, Entry)) { AddError(TEXT("Entry was not admitted.")); return false; }
    F.Tick(0.0);
    F.Transport->Observation.TransportEstablished = true;
    F.Tick(1.0);
    F.GenericListener->OnRequestCompletion = [&F]() { F.Host->Request_Shutdown(); };
    FCk_OnlineConnection_Operation Confirm;
    if (!F.Request(ECk_OnlineConnection_Action::ConfirmGameReady, Entry, Confirm)) { AddError(TEXT("Confirm was not admitted.")); return false; }
    F.Tick(2.0);
    TestFalse(TEXT("generic completion callback safely stops request admission"), F.Host->Get_IsAcceptingRequests());
    TestEqual(TEXT("callback shutdown releases adapter exactly once"), F.Transport->ShutdownCalls, 1);
    TestEqual(TEXT("both accepted requests receive generic completion despite reentrant shutdown"),
        F.GenericListener->RequestCompletionCount, 2);
    F.ExpectResult(*this, Entry, ECk_OnlineConnection_Result::Succeeded);
    F.ExpectResult(*this, Confirm, ECk_OnlineConnection_Result::Succeeded);
    const auto Count = F.GenericListener->RequestCompletionCount;
    const auto AdapterCalls = F.Transport->Calls();
    F.Tick(3.0);
    TestEqual(TEXT("stopped host does not repeat generic callbacks"), F.GenericListener->RequestCompletionCount, Count);
    TestEqual(TEXT("stopped host submits no adapter work"), F.Transport->Calls(), AdapterCalls);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineConnectionAdapterShutdown, "Ck.Online.Connection.ProtocolAdapterCallbackShutdown", ck_test_online_connection::Flags)
bool FCkOnlineConnectionAdapterShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_connection;
    // Admission, execution preflight, pending observation, entry submission, then local queued requests.
    for (int32 Case = 0; Case < 5; ++Case)
    {
        FFixture F;
        if (!TestTrue(TEXT("adapter reentrancy fixture has real ready control"), F.Start(17917 + Case))) { return false; }
        const auto Stop = [&F]() { F.Host->Request_Shutdown(); };
        FCk_OnlineConnection_Operation Entry;
        FCk_OnlineConnection_Operation Confirm;
        FCk_OnlineConnection_Operation Cancel;
        if (Case == 0)
        {
            F.Transport->OnCanEnter = Stop;
            Entry.Set_Id(FGuid::NewGuid()).Set_Generation(FGuid::NewGuid());
            TestFalse(TEXT("shutdown inside admission preflight rejects the request"),
                F.Request(ECk_OnlineConnection_Action::Host, {}, Entry));
            TestFalse(TEXT("reentrant admission rejection clears output key"),
                Entry.Get_Id().IsValid() || Entry.Get_Generation().IsValid());
            TestEqual(TEXT("rejected reentrant admission submits no entry"), F.Transport->EntryCalls, 0);
            TestEqual(TEXT("rejected reentrant admission emits no typed completion"), F.Listener->Completions.Num(), 0);
            TestEqual(TEXT("rejected reentrant admission emits no generic completion"), F.GenericListener->RequestCompletionCount, 0);
        }
        else
        {
            if (!TestTrue(TEXT("reentrancy entry is admitted before adapter shutdown"),
                F.Request(ECk_OnlineConnection_Action::Host, {}, Entry))) { return false; }
            if (Case == 1) { F.Transport->OnCanEnter = Stop; F.Transport->CanEnterResult = false; }
            if (Case == 3) { F.Transport->OnBeginEntry = Stop; }
            F.Tick(0.0);
            if (Case == 2 || Case == 4)
            {
                F.Transport->Observation.TransportEstablished = true;
                if (Case == 4)
                {
                    F.Tick(1.0);
                    if (!TestTrue(TEXT("readiness request enters local queue before callback shutdown"),
                        F.Request(ECk_OnlineConnection_Action::ConfirmGameReady, Entry, Confirm)) ||
                        !TestTrue(TEXT("second command enters the same local queue"),
                            F.Request(ECk_OnlineConnection_Action::Cancel, Entry, Cancel)))
                    { return false; }
                }
                F.Transport->OnObserve = Stop;
                F.Tick(Case == 4 ? 2.0 : 1.0);
            }
            F.ExpectResult(*this, Entry, ECk_OnlineConnection_Result::Cancelled);
            if (Case == 4)
            {
                F.ExpectResult(*this, Confirm, ECk_OnlineConnection_Result::Cancelled);
                F.ExpectResult(*this, Cancel, ECk_OnlineConnection_Result::Cancelled);
            }
            TestEqual(TEXT("every accepted request receives exactly one generic cancelled completion"),
                F.GenericListener->RequestCompletionCount, Case == 4 ? 3 : 1);
            for (const auto Result : F.GenericListener->RequestResults)
            { TestEqual(TEXT("adapter shutdown never reports generic success"), Result, ECk_Request_OperationResult::Failed_Cancelled); }
            TestEqual(TEXT("execution preflight shutdown prevents entry submission"), F.Transport->EntryCalls, Case == 1 ? 0 : 1);
        }
        TestFalse(TEXT("adapter callback shutdown closes request admission"), F.Host->Get_IsAcceptingRequests());
        TestEqual(TEXT("adapter callback shuts transport down once"), F.Transport->ShutdownCalls, 1);
        TestEqual(TEXT("adapter callback shutdown issues no recovery travel"), F.Transport->ReturnCalls, 0);
        const auto CallsAfterStop = F.Transport->Calls();
        const auto TypedAfterStop = F.Listener->Completions.Num();
        const auto GenericAfterStop = F.GenericListener->RequestCompletionCount;
        F.Tick(3.0);
        TestEqual(TEXT("stopped owner invokes no subsequent adapter work"), F.Transport->Calls(), CallsAfterStop);
        TestEqual(TEXT("stopped owner repeats no typed completion"), F.Listener->Completions.Num(), TypedAfterStop);
        TestEqual(TEXT("stopped owner repeats no generic completion"), F.GenericListener->RequestCompletionCount, GenericAfterStop);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
