#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/Steam/CkOnlineSteam_Feature.h"
#include "CkOnline/Steam/CkOnlineSteam_Session.h"
#include "CkOnline/Steam/CkOnlineSteam_Utils.h"
#include "CkTest_OnlineServiceListener.h"
#include "CkTest_OnlineSteamListener.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_online_steam
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

    struct FProvider final : ck::steam::ISessionProvider
    {
        struct FCall
        {
            ECk_OnlineSteam_Action Action = ECk_OnlineSteam_Action::Invalid;
            FGuid Ticket;
            FCk_Request_OnlineSteam Request;
        };

        ck::steam::FSessionIdentity Identity{true, true, TEXT("account-a"), TEXT("Tester"), {}};
        TArray<FCall> Calls;
        TArray<ck::steam::FSessionCompletion> Completions;
        TFunction<void(const FCall&)> OnExecute;
        bool SessionExists = false;
        FString LobbyId;
        int32 Invalidations = 0;
        mutable int32 IdentityObservations = 0;

        auto ObserveIdentity() const -> ck::steam::FSessionIdentity override
        { ++IdentityObservations; return Identity; }
        auto Execute(const FCk_Request_OnlineSteam& Request, FGuid Ticket,
            const FCk_OnlineSteam_Spec&) -> void override
        {
            Calls.Add({Request.Get_Action(), Ticket, Request});
            if (OnExecute) { OnExecute(Calls.Last()); }
        }
        auto DrainCompletions() -> TArray<ck::steam::FSessionCompletion> override
        { auto Values = MoveTemp(Completions); Completions.Reset(); return Values; }
        auto HasSession() const -> bool override { return SessionExists; }
        auto Get_LobbyId() const -> FString override { return LobbyId; }
        auto InvalidateSearch() -> void override { ++Invalidations; }

        auto Count(ECk_OnlineSteam_Action Action) const -> int32
        { return Calls.FilterByPredicate([Action](const FCall& Call) { return Call.Action == Action; }).Num(); }
        auto LastTicket(ECk_OnlineSteam_Action Action) const -> FGuid
        {
            for (int32 Index = Calls.Num() - 1; Index >= 0; --Index)
            { if (Calls[Index].Action == Action) { return Calls[Index].Ticket; } }
            return {};
        }
        auto Complete(FGuid Ticket, ECk_OnlineSteam_Action Action, ECk_OnlineSteam_Result Result,
            TArray<FCk_OnlineSteam_Lobby> Rows = {}) -> void
        { Completions.Add({Ticket, Action, Result, {}, MoveTemp(Rows)}); }
    };

    auto Lobby(const TCHAR* Id, int32 OpenSlots = 2) -> FCk_OnlineSteam_Lobby
    {
        auto Row = FCk_OnlineSteam_Lobby{};
        Row.Set_Id(Id).Set_OwnerName(TEXT("Host")).Set_Label(TEXT("Lobby"))
            .Set_OpenSlots(OpenSlots).Set_MaxMembers(4);
        return Row;
    }

    struct FFixture
    {
        TSharedRef<FProvider> Provider = MakeShared<FProvider>();
        FCk_OnlineSteam_Spec Spec;
        TSharedPtr<ck::steam::ITransport> Transport;
        TSharedPtr<ck::steam::FFeature> Feature;
        TSharedPtr<ck::FOnlineServiceHost> Host;
        FCk_Handle_OnlineSteam Handle;
        TStrongObjectPtr<UCk_Test_OnlineSteamListener_UE> Typed{NewObject<UCk_Test_OnlineSteamListener_UE>()};
        TStrongObjectPtr<UCk_Test_OnlineServiceListener_UE> Generic{NewObject<UCk_Test_OnlineServiceListener_UE>()};

        FFixture() { Spec.Set_LocalUserIndex(0).Set_MaxLobbyMembers(4).Set_MaxSearchResults(8)
            .Set_LobbyLabel(TEXT("Test Lobby")).Set_CleanupGraceSeconds(2.0); }
        ~FFixture()
        {
            Typed->OnCompletion = {};
            Generic->OnRequestCompletion = {};
            Provider->OnExecute = {};
            if (Host) { Host->Request_Shutdown(); }
            // A failed assertion must not strand the shutdown ticker with a fake callback still owed.
            if (Transport)
            {
                Provider->SessionExists = false;
                Provider->LobbyId.Reset();
                for (const auto& Call : Provider->Calls)
                { Provider->Complete(Call.Ticket, Call.Action, ECk_OnlineSteam_Result::Failed); }
                Transport->Poll(1000.0);
            }
        }
        auto Start() -> bool
        {
            Transport = ck::steam::TryCreateSessionTransport(Spec,
                StaticCastSharedRef<ck::steam::ISessionProvider>(Provider));
            if (!Transport) { return false; }
            Feature = ck::steam::FFeature::TryCreate(Transport.ToSharedRef());
            if (!Feature) { return false; }
            Host = ck::FOnlineServiceHost::TryCreateFeature(
                StaticCastSharedRef<ck::IOnlineServiceFeature>(Feature.ToSharedRef()), FCk_Time{});
            if (!Host) { return false; }
            Handle = UCk_Utils_OnlineSteam_UE::Cast(Host->Get_Handle());
            if (ck::Is_NOT_Valid(Handle)) { return false; }
            FCk_Delegate_OnlineSteam_Completed Delegate;
            Delegate.BindDynamic(Typed.Get(), &UCk_Test_OnlineSteamListener_UE::OnCompleted);
            UCk_Utils_OnlineSteam_UE::BindTo_OnCompleted(Handle, Delegate,
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
            Tick(0.0);
            return true;
        }
        auto Tick(double Now) -> void { Host->Tick(FCk_Time{Now}); }
        auto Poll(double Now) -> void { Transport->Poll(Now); }
        auto Snapshot() const -> FCk_OnlineSteam_Snapshot
        { return UCk_Utils_OnlineSteam_UE::Get_Snapshot(Handle); }
        auto Request(ECk_OnlineSteam_Action Action, FGuid& OutId, double Timeout = 15.0,
            FGuid Target = {}, int32 LobbyIndex = INDEX_NONE, FGuid SearchGeneration = {}) -> bool
        {
            OutId = FGuid::NewGuid();
            auto Request = FCk_Request_OnlineSteam{};
            Request.Set_OperationId(OutId).Set_Action(Action).Set_TimeoutSeconds(Timeout)
                .Set_TargetOperationId(Target).Set_LobbyIndex(LobbyIndex)
                .Set_SearchGeneration(SearchGeneration);
            FCk_Delegate_Request_OnCompleted Delegate;
            Delegate.BindDynamic(Generic.Get(), &UCk_Test_OnlineServiceListener_UE::OnRequestCompleted);
            Request.Set_CompletionDelegate(Delegate);
            return UCk_Utils_OnlineSteam_UE::TryRequest(Handle, Request);
        }
        auto Count(FGuid Id) const -> int32
        { return Typed->Completions.FilterByPredicate([Id](const auto& C) { return C.Get_OperationId() == Id; }).Num(); }
        auto Expect(FAutomationTestBase& Test, FGuid Id, ECk_OnlineSteam_Result Result) const -> void
        {
            Test.TestEqual(TEXT("one typed completion for operation"), Count(Id), 1);
            for (const auto& Completion : Typed->Completions)
            { if (Completion.Get_OperationId() == Id)
                { Test.TestEqual(TEXT("typed operation result"), Completion.Get_Result(), Result); } }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamAdmission,
    "Ck.Online.Steam.InvalidSpecAndRequestAdmission", ck_test_online_steam::Flags)
bool FCkOnlineSteamAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    auto BadSpec = F.Spec;
    BadSpec.Set_LocalUserIndex(INDEX_NONE);
    TestFalse(TEXT("unspecified local user has no transport"),
        ck::steam::TryCreateSessionTransport(BadSpec,
            StaticCastSharedRef<ck::steam::ISessionProvider>(F.Provider)).IsValid());
    BadSpec.Set_LocalUserIndex(1);
    TestFalse(TEXT("unsupported local user has no transport"),
        ck::steam::TryCreateSessionTransport(BadSpec,
            StaticCastSharedRef<ck::steam::ISessionProvider>(F.Provider)).IsValid());
    BadSpec = F.Spec;
    BadSpec.Set_CleanupGraceSeconds(std::numeric_limits<double>::quiet_NaN());
    TestFalse(TEXT("nonfinite cleanup grace has no transport"),
        ck::steam::TryCreateSessionTransport(BadSpec,
            StaticCastSharedRef<ck::steam::ISessionProvider>(F.Provider)).IsValid());
    TestEqual(TEXT("invalid specs never inspect provider identity"), F.Provider->IdentityObservations, 0);
    TestEqual(TEXT("invalid specs never submit provider work"), F.Provider->Calls.Num(), 0);
    if (!TestTrue(TEXT("valid transport attaches to real feature host"), F.Start())) { return false; }
    const auto Before = F.Snapshot();
    auto Invalid = FCk_Request_OnlineSteam{};
    Invalid.Set_OperationId(FGuid::NewGuid()).Set_Action(ECk_OnlineSteam_Action::JoinLobby);
    TestFalse(TEXT("join without search selection rejects at admission"),
        UCk_Utils_OnlineSteam_UE::TryRequest(F.Handle, Invalid));
    Invalid.Set_Action(ECk_OnlineSteam_Action::Cancel).Set_TargetOperationId(FGuid::NewGuid());
    TestFalse(TEXT("cancel of unknown request rejects at admission"),
        UCk_Utils_OnlineSteam_UE::TryRequest(F.Handle, Invalid));
    Invalid.Set_Action(ECk_OnlineSteam_Action::CreateLobby)
        .Set_TimeoutSeconds(std::numeric_limits<double>::quiet_NaN());
    TestFalse(TEXT("nonfinite request timeout rejects at admission"),
        UCk_Utils_OnlineSteam_UE::TryRequest(F.Handle, Invalid));
    TestEqual(TEXT("rejected requests never execute provider calls"), F.Provider->Calls.Num(), 0);
    TestEqual(TEXT("rejected requests preserve snapshot revision"), F.Snapshot().Get_Revision(), Before.Get_Revision());
    TestEqual(TEXT("rejected requests emit no typed completion"), F.Typed->Completions.Num(), 0);
    TestEqual(TEXT("rejected requests emit no generic completion"), F.Generic->RequestCompletionCount, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamIdentityAdmission,
    "Ck.Online.Steam.ProviderUnavailableAndLoggedOut", ck_test_online_steam::Flags)
bool FCkOnlineSteamIdentityAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    for (int32 Case = 0; Case < 2; ++Case)
    {
        FFixture F;
        F.Provider->Identity.Available = Case == 1;
        F.Provider->Identity.LoggedIn = false;
        F.Provider->Identity.AccountId.Reset();
        if (!TestTrue(TEXT("unavailable or logged-out fixture starts"), F.Start())) { return false; }
        FGuid Create;
        if (!TestTrue(TEXT("identity failure is accepted for typed completion"),
            F.Request(ECk_OnlineSteam_Action::CreateLobby, Create))) { return false; }
        F.Tick(1.0);
        F.Expect(*this, Create, Case == 0 ? ECk_OnlineSteam_Result::ProviderUnavailable
            : ECk_OnlineSteam_Result::NotLoggedIn);
        TestEqual(TEXT("identity failure submits no provider operation"), F.Provider->Calls.Num(), 0);
        TestEqual(TEXT("identity failure fires one generic callback"), F.Generic->RequestCompletionCount, 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamLifecycle,
    "Ck.Online.Steam.CreateFindJoinLeaveAndStaleSelection", ck_test_online_steam::Flags)
bool FCkOnlineSteamLifecycle::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("session fixture starts"), F.Start())) { return false; }
    FGuid Create;
    if (!TestTrue(TEXT("create admitted"), F.Request(ECk_OnlineSteam_Action::CreateLobby, Create))) { return false; }
    F.Tick(1.0);
    TestEqual(TEXT("create submitted once"), F.Provider->Count(ECk_OnlineSteam_Action::CreateLobby), 1);
    F.Provider->SessionExists = true;
    F.Provider->LobbyId = TEXT("owned-lobby");
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby),
        ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(1.1);
    F.Expect(*this, Create, ECk_OnlineSteam_Result::Succeeded);
    TestEqual(TEXT("owned lobby appears in snapshot"), F.Snapshot().Get_LobbyId(), FString(TEXT("owned-lobby")));
    FGuid Leave;
    if (!TestTrue(TEXT("leave admitted"), F.Request(ECk_OnlineSteam_Action::LeaveLobby, Leave))) { return false; }
    F.Tick(2.0);
    F.Provider->SessionExists = false;
    F.Provider->LobbyId.Reset();
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
        ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(2.1);
    F.Expect(*this, Leave, ECk_OnlineSteam_Result::Succeeded);
    FGuid Find;
    if (!TestTrue(TEXT("find admitted"), F.Request(ECk_OnlineSteam_Action::FindLobbies, Find))) { return false; }
    F.Tick(3.0);
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::FindLobbies),
        ECk_OnlineSteam_Action::FindLobbies, ECk_OnlineSteam_Result::Succeeded,
        {Lobby(TEXT("open-lobby")), Lobby(TEXT("full-lobby"), 0)});
    F.Tick(3.1);
    F.Expect(*this, Find, ECk_OnlineSteam_Result::Succeeded);
    const auto SearchGeneration = F.Snapshot().Get_SearchGeneration();
    TestTrue(TEXT("successful search carries a selection generation"), SearchGeneration.IsValid());
    TestEqual(TEXT("search result rows retained"), F.Snapshot().Get_SearchResults().Num(), 2);
    FGuid Join;
    const auto CallsBefore = F.Provider->Calls.Num();
    TestTrue(TEXT("full lobby request enters host for typed rejection"),
        F.Request(ECk_OnlineSteam_Action::JoinLobby, Join, 15.0, {}, 1, SearchGeneration));
    F.Tick(4.0);
    F.Expect(*this, Join, ECk_OnlineSteam_Result::Rejected);
    TestEqual(TEXT("full lobby does not reach provider"), F.Provider->Calls.Num(), CallsBefore);
    TestTrue(TEXT("stale search generation request enters host for typed rejection"),
        F.Request(ECk_OnlineSteam_Action::JoinLobby, Join, 15.0, {}, 0, FGuid::NewGuid()));
    F.Tick(4.1);
    F.Expect(*this, Join, ECk_OnlineSteam_Result::Rejected);
    TestEqual(TEXT("stale selection does not reach provider"), F.Provider->Calls.Num(), CallsBefore);
    if (!TestTrue(TEXT("fresh open selection admitted"),
        F.Request(ECk_OnlineSteam_Action::JoinLobby, Join, 15.0, {}, 0, SearchGeneration))) { return false; }
    F.Tick(5.0);
    F.Provider->SessionExists = true;
    F.Provider->LobbyId = TEXT("open-lobby");
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::JoinLobby),
        ECk_OnlineSteam_Action::JoinLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(5.1);
    F.Expect(*this, Join, ECk_OnlineSteam_Result::Succeeded);
    TestEqual(TEXT("joined lobby ID is the selected result"), F.Snapshot().Get_LobbyId(), FString(TEXT("open-lobby")));
    TestEqual(TEXT("every accepted request fires one generic callback"),
        F.Generic->RequestCompletionCount, F.Typed->Completions.Num());
    FGuid FinalLeave;
    if (!TestTrue(TEXT("joined lobby can leave"), F.Request(ECk_OnlineSteam_Action::LeaveLobby, FinalLeave))) { return false; }
    F.Tick(6.0);
    F.Provider->SessionExists = false;
    F.Provider->LobbyId.Reset();
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
        ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(6.1);
    F.Expect(*this, FinalLeave, ECk_OnlineSteam_Result::Succeeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamSearchValidation,
    "Ck.Online.Steam.MalformedFindRowsAndDeadlineEdge", ck_test_online_steam::Flags)
bool FCkOnlineSteamSearchValidation::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    for (int32 Case = 0; Case < 2; ++Case)
    {
        FFixture F;
        if (!TestTrue(TEXT("search validation fixture starts"), F.Start())) { return false; }
        FGuid Find;
        if (!TestTrue(TEXT("find admitted"),
            F.Request(ECk_OnlineSteam_Action::FindLobbies, Find, Case == 0 ? 15.0 : 1.0))) { return false; }
        F.Tick(1.0);
        const auto Ticket = F.Provider->LastTicket(ECk_OnlineSteam_Action::FindLobbies);
        if (Case == 0)
        {
            F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::FindLobbies,
                ECk_OnlineSteam_Result::Succeeded,
                {Lobby(TEXT("valid-row")), Lobby(TEXT("valid-row"))});
            F.Tick(1.1);
            F.Expect(*this, Find, ECk_OnlineSteam_Result::Failed);
        }
        else
        {
            F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::FindLobbies,
                ECk_OnlineSteam_Result::Succeeded, {Lobby(TEXT("deadline-row"))});
            F.Tick(2.0);
            F.Expect(*this, Find, ECk_OnlineSteam_Result::TimedOut);
        }
        TestEqual(TEXT("failed or expired search publishes no partial rows"),
            F.Snapshot().Get_SearchResults().Num(), 0);
        TestFalse(TEXT("failed or expired search has no selection generation"),
            F.Snapshot().Get_SearchGeneration().IsValid());
        FGuid Join;
        TestTrue(TEXT("forged selection is accepted only for typed rejection"),
            F.Request(ECk_OnlineSteam_Action::JoinLobby, Join, 15.0, {}, 0, FGuid::NewGuid()));
        F.Tick(Case == 0 ? 1.2 : 2.1);
        F.Expect(*this, Join, ECk_OnlineSteam_Result::Rejected);
        TestEqual(TEXT("malformed or expired search never submits join"),
            F.Provider->Count(ECk_OnlineSteam_Action::JoinLobby), 0);
        TestEqual(TEXT("generic and typed terminal counts match"),
            F.Generic->RequestCompletionCount, F.Typed->Completions.Num());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamLateMembership,
    "Ck.Online.Steam.CancelTimeoutAndLateMembershipCleanup", ck_test_online_steam::Flags)
bool FCkOnlineSteamLateMembership::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    for (int32 Case = 0; Case < 2; ++Case)
    {
        FFixture F;
        if (!TestTrue(TEXT("late membership fixture starts"), F.Start())) { return false; }
        FGuid Create;
        if (!TestTrue(TEXT("membership operation admitted"),
            F.Request(ECk_OnlineSteam_Action::CreateLobby, Create, Case == 0 ? 10.0 : 0.5))) { return false; }
        F.Tick(1.0);
        const auto Ticket = F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby);
        F.Provider->SessionExists = true;
        TestTrue(TEXT("provider can allocate a named session before its callback or lobby ID"),
            F.Provider->HasSession() && F.Provider->Get_LobbyId().IsEmpty());
        if (Case == 0)
        {
            FGuid Cancel;
            if (!TestTrue(TEXT("pending create can be cancelled"),
                F.Request(ECk_OnlineSteam_Action::Cancel, Cancel, 10.0, Create))) { return false; }
            F.Tick(1.1);
            F.Expect(*this, Create, ECk_OnlineSteam_Result::Cancelled);
            F.Expect(*this, Cancel, ECk_OnlineSteam_Result::Succeeded);
        }
        else
        {
            F.Tick(1.6);
            F.Expect(*this, Create, ECk_OnlineSteam_Result::TimedOut);
        }
        TestEqual(TEXT("abandonment does not leave an allocated session before create callback"),
            F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 0);
        FGuid Blocked;
        TestTrue(TEXT("new request can be admitted for typed lane rejection"),
            F.Request(ECk_OnlineSteam_Action::CreateLobby, Blocked));
        F.Tick(1.7);
        F.Expect(*this, Blocked, ECk_OnlineSteam_Result::Rejected);
        TestEqual(TEXT("one provider create remains in flight"),
            F.Provider->Count(ECk_OnlineSteam_Action::CreateLobby), 1);
        F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::FindLobbies, ECk_OnlineSteam_Result::Succeeded,
            {Lobby(TEXT("wrong-action-row"))});
        F.Tick(1.75);
        TestEqual(TEXT("same ticket with wrong action cannot settle membership lane"),
            F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 0);
        F.Provider->LobbyId = TEXT("late-owned-lobby");
        F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Succeeded);
        F.Tick(1.8);
        TestEqual(TEXT("late success starts exactly one cleanup leave"),
            F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 1);
        F.Provider->SessionExists = false;
        F.Provider->LobbyId.Reset();
        F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
            ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Succeeded);
        F.Tick(1.9);
        TestEqual(TEXT("late create retains its sole terminal completion"), F.Count(Create), 1);
        TestEqual(TEXT("every accepted request has one generic completion"),
            F.Generic->RequestCompletionCount, F.Typed->Completions.Num());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamLateJoin,
    "Ck.Online.Steam.CancelledJoinWaitsForCallbackBeforeRemoval", ck_test_online_steam::Flags)
bool FCkOnlineSteamLateJoin::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("late join fixture starts"), F.Start())) { return false; }
    FGuid Find;
    if (!TestTrue(TEXT("selection search admitted"),
        F.Request(ECk_OnlineSteam_Action::FindLobbies, Find))) { return false; }
    F.Tick(1.0);
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::FindLobbies),
        ECk_OnlineSteam_Action::FindLobbies, ECk_OnlineSteam_Result::Succeeded,
        {Lobby(TEXT("target-lobby"))});
    F.Tick(1.1);
    F.Expect(*this, Find, ECk_OnlineSteam_Result::Succeeded);
    FGuid Join;
    if (!TestTrue(TEXT("fresh selection join admitted"),
        F.Request(ECk_OnlineSteam_Action::JoinLobby, Join, 10.0, {}, 0,
            F.Snapshot().Get_SearchGeneration()))) { return false; }
    F.Tick(2.0);
    const auto Ticket = F.Provider->LastTicket(ECk_OnlineSteam_Action::JoinLobby);
    F.Provider->SessionExists = true;
    TestTrue(TEXT("join may allocate a named session before returning a lobby ID"),
        F.Provider->HasSession() && F.Provider->Get_LobbyId().IsEmpty());
    FGuid Cancel;
    if (!TestTrue(TEXT("pending join cancellation admitted"),
        F.Request(ECk_OnlineSteam_Action::Cancel, Cancel, 10.0, Join))) { return false; }
    F.Tick(2.1);
    F.Expect(*this, Join, ECk_OnlineSteam_Result::Cancelled);
    F.Expect(*this, Cancel, ECk_OnlineSteam_Result::Succeeded);
    TestEqual(TEXT("cancelled join does not leave allocated session before callback"),
        F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 0);
    F.Provider->LobbyId = TEXT("target-lobby");
    F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::JoinLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(2.2);
    TestEqual(TEXT("late joined membership causes one removal"),
        F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 1);
    F.Provider->SessionExists = false;
    F.Provider->LobbyId.Reset();
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
        ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(2.3);
    TestEqual(TEXT("confirmed join cleanup restores ready state"),
        F.Snapshot().Get_State(), ECk_OnlineSteam_State::Ready);
    TestEqual(TEXT("cancelled join remains single terminal completion"), F.Count(Join), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamSearchShutdown,
    "Ck.Online.Steam.AbandonedSearchAndCallbackShutdown", ck_test_online_steam::Flags)
bool FCkOnlineSteamSearchShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("search fixture starts"), F.Start())) { return false; }
    FGuid Find;
    if (!TestTrue(TEXT("find admitted"), F.Request(ECk_OnlineSteam_Action::FindLobbies, Find))) { return false; }
    F.Tick(1.0);
    const auto Ticket = F.Provider->LastTicket(ECk_OnlineSteam_Action::FindLobbies);
    FGuid Cancel;
    if (!TestTrue(TEXT("find cancellation admitted"),
        F.Request(ECk_OnlineSteam_Action::Cancel, Cancel, 10.0, Find))) { return false; }
    F.Tick(1.1);
    F.Expect(*this, Find, ECk_OnlineSteam_Result::Cancelled);
    F.Expect(*this, Cancel, ECk_OnlineSteam_Result::Succeeded);
    FGuid Blocked;
    TestTrue(TEXT("second search request reaches lane validation"),
        F.Request(ECk_OnlineSteam_Action::FindLobbies, Blocked));
    F.Tick(1.2);
    F.Expect(*this, Blocked, ECk_OnlineSteam_Result::Rejected);
    TestEqual(TEXT("abandoned search cannot overlap another provider search"),
        F.Provider->Count(ECk_OnlineSteam_Action::FindLobbies), 1);
    F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::FindLobbies,
        ECk_OnlineSteam_Result::Succeeded, {Lobby(TEXT("late-row"))});
    F.Tick(1.3);
    TestEqual(TEXT("abandoned search never publishes late rows"), F.Snapshot().Get_SearchResults().Num(), 0);
    TestEqual(TEXT("abandoned search still has one typed completion"), F.Count(Find), 1);
    bool ReentrantAdmission = true;
    F.Generic->OnRequestCompletion = [&F, &ReentrantAdmission]()
    {
        F.Host->Request_Shutdown();
        FGuid ReentrantId;
        ReentrantAdmission = F.Request(ECk_OnlineSteam_Action::Login, ReentrantId);
    };
    FGuid Login;
    if (!TestTrue(TEXT("fresh login admitted after search callback"),
        F.Request(ECk_OnlineSteam_Action::Login, Login))) { return false; }
    F.Tick(2.0);
    F.Expect(*this, Login, ECk_OnlineSteam_Result::Succeeded);
    TestFalse(TEXT("generic callback shutdown closes host admission"), F.Host->Get_IsAcceptingRequests());
    TestFalse(TEXT("callback reentry cannot admit a request after shutdown"), ReentrantAdmission);
    TestEqual(TEXT("generic and typed callbacks stay paired through shutdown"),
        F.Generic->RequestCompletionCount, F.Typed->Completions.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamSynchronous,
    "Ck.Online.Steam.SynchronousDuplicateAndStaleProviderCompletions", ck_test_online_steam::Flags)
bool FCkOnlineSteamSynchronous::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("synchronous fixture starts"), F.Start())) { return false; }
    F.Provider->OnExecute = [&F](const FProvider::FCall& Call)
    {
        if (Call.Action == ECk_OnlineSteam_Action::CreateLobby)
        {
            F.Provider->SessionExists = true;
            F.Provider->LobbyId = TEXT("sync-lobby");
            F.Provider->Complete(Call.Ticket, Call.Action, ECk_OnlineSteam_Result::Succeeded);
        }
    };
    FGuid Create;
    if (!TestTrue(TEXT("create with synchronous provider callback admitted"),
        F.Request(ECk_OnlineSteam_Action::CreateLobby, Create))) { return false; }
    F.Tick(1.0);
    F.Expect(*this, Create, ECk_OnlineSteam_Result::Succeeded);
    const auto CreateTicket = F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby);
    F.Provider->Complete(CreateTicket, ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Provider->Complete(FGuid::NewGuid(), ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(1.1);
    TestEqual(TEXT("duplicate and unrelated callback do not repeat typed completion"), F.Count(Create), 1);
    TestEqual(TEXT("duplicate and unrelated callback do not repeat generic completion"),
        F.Generic->RequestCompletionCount, 1);
    TestEqual(TEXT("duplicate callback does not cause compensating leave"),
        F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 0);
    F.Provider->OnExecute = {};
    FGuid Leave;
    if (!TestTrue(TEXT("synchronous session admits leave"),
        F.Request(ECk_OnlineSteam_Action::LeaveLobby, Leave))) { return false; }
    F.Tick(2.0);
    F.Provider->SessionExists = false;
    F.Provider->LobbyId.Reset();
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
        ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(2.1);
    F.Expect(*this, Leave, ECk_OnlineSteam_Result::Succeeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamUnknownCleanup,
    "Ck.Online.Steam.UnknownCleanupReservesLaneUntilLateSettlement", ck_test_online_steam::Flags)
bool FCkOnlineSteamUnknownCleanup::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("unknown cleanup fixture starts"), F.Start())) { return false; }
    FGuid Create;
    if (!TestTrue(TEXT("create admitted"), F.Request(ECk_OnlineSteam_Action::CreateLobby, Create))) { return false; }
    F.Tick(1.0);
    const auto Ticket = F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby);
    F.Provider->SessionExists = true;
    FGuid Cancel;
    if (!TestTrue(TEXT("create cancel admitted"),
        F.Request(ECk_OnlineSteam_Action::Cancel, Cancel, 10.0, Create))) { return false; }
    F.Tick(1.1);
    F.Tick(3.2);
    TestEqual(TEXT("cleanup grace expiry exposes unknown outcome"),
        F.Snapshot().Get_State(), ECk_OnlineSteam_State::CleanupUnknown);
    FGuid Blocked;
    TestTrue(TEXT("new create can be locally rejected while remote lane is unknown"),
        F.Request(ECk_OnlineSteam_Action::CreateLobby, Blocked));
    F.Tick(3.3);
    F.Expect(*this, Blocked, ECk_OnlineSteam_Result::Rejected);
    TestEqual(TEXT("unknown cleanup does not reuse provider create lane"),
        F.Provider->Count(ECk_OnlineSteam_Action::CreateLobby), 1);
    F.Provider->LobbyId = TEXT("late-lobby");
    F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(3.4);
    TestEqual(TEXT("late membership triggers one cleanup leave even after grace"),
        F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 1);
    F.Provider->SessionExists = false;
    F.Provider->LobbyId.Reset();
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
        ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(3.5);
    TestEqual(TEXT("confirmed removal restores ready state"), F.Snapshot().Get_State(), ECk_OnlineSteam_State::Ready);
    F.Expect(*this, Create, ECk_OnlineSteam_Result::Cancelled);
    F.Expect(*this, Cancel, ECk_OnlineSteam_Result::Succeeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamFailedLeave,
    "Ck.Online.Steam.FailedLeaveReservesSessionUntilProviderGone", ck_test_online_steam::Flags)
bool FCkOnlineSteamFailedLeave::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("failed leave fixture starts"), F.Start())) { return false; }
    FGuid Create;
    if (!TestTrue(TEXT("create admitted"), F.Request(ECk_OnlineSteam_Action::CreateLobby, Create))) { return false; }
    F.Tick(1.0);
    F.Provider->SessionExists = true;
    F.Provider->LobbyId = TEXT("owned-lobby");
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby),
        ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Succeeded);
    F.Tick(1.1);
    F.Expect(*this, Create, ECk_OnlineSteam_Result::Succeeded);
    FGuid Leave;
    if (!TestTrue(TEXT("leave admitted"), F.Request(ECk_OnlineSteam_Action::LeaveLobby, Leave))) { return false; }
    F.Tick(2.0);
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
        ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Failed);
    F.Tick(2.1);
    F.Expect(*this, Leave, ECk_OnlineSteam_Result::Failed);
    TestEqual(TEXT("failed removal with named session remains unknown"),
        F.Snapshot().Get_State(), ECk_OnlineSteam_State::CleanupUnknown);
    TestEqual(TEXT("failed leave makes exactly one provider removal attempt"),
        F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 1);
    FGuid Blocked;
    TestTrue(TEXT("fresh create reaches typed reserved-lane rejection"),
        F.Request(ECk_OnlineSteam_Action::CreateLobby, Blocked));
    F.Tick(2.2);
    F.Expect(*this, Blocked, ECk_OnlineSteam_Result::Rejected);
    TestEqual(TEXT("unknown cleanup never retries or starts a new provider call"), F.Provider->Calls.Num(), 2);
    F.Provider->SessionExists = false;
    F.Provider->LobbyId.Reset();
    F.Tick(2.3);
    TestEqual(TEXT("provider disappearance releases failed removal reservation"),
        F.Snapshot().Get_State(), ECk_OnlineSteam_State::Ready);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamProviderGone,
    "Ck.Online.Steam.ProviderLossCannotClaimCleanRemoval", ck_test_online_steam::Flags)
bool FCkOnlineSteamProviderGone::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("provider loss fixture starts"), F.Start())) { return false; }
    FGuid Create;
    if (!TestTrue(TEXT("create admitted"), F.Request(ECk_OnlineSteam_Action::CreateLobby, Create))) { return false; }
    F.Tick(1.0);
    const auto Ticket = F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby);
    FGuid Cancel;
    if (!TestTrue(TEXT("create cancellation admitted"),
        F.Request(ECk_OnlineSteam_Action::Cancel, Cancel, 10.0, Create))) { return false; }
    F.Tick(1.1);
    F.Expect(*this, Create, ECk_OnlineSteam_Result::Cancelled);
    F.Expect(*this, Cancel, ECk_OnlineSteam_Result::Succeeded);
    F.Provider->Identity.Available = false;
    F.Provider->Identity.LoggedIn = false;
    F.Provider->Identity.AccountId.Reset();
    F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Failed);
    F.Tick(1.2);
    TestEqual(TEXT("provider loss leaves cleanup outcome unknown despite no visible session"),
        F.Snapshot().Get_State(), ECk_OnlineSteam_State::CleanupUnknown);
    TestEqual(TEXT("provider loss never attempts removal without authority"),
        F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 0);
    TestEqual(TEXT("local cancellation remains terminal exactly once"), F.Count(Create), 1);
    F.Host->Request_Shutdown();
    F.Poll(1.3);
    TestEqual(TEXT("shutdown observer retirement preserves unknown outcome"),
        F.Transport->Get_Snapshot().Get_State(), ECk_OnlineSteam_State::CleanupUnknown);
    TestEqual(TEXT("shutdown and late callback submit no new provider work"), F.Provider->Calls.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamIdentityAndPendingShutdown,
    "Ck.Online.Steam.IdentityChangeAndShutdownRetainCleanupOwnership", ck_test_online_steam::Flags)
bool FCkOnlineSteamIdentityAndPendingShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    for (int32 Case = 0; Case < 2; ++Case)
    {
        FFixture F;
        if (!TestTrue(TEXT("identity or shutdown fixture starts"), F.Start())) { return false; }
        const auto OriginalGeneration = F.Snapshot().Get_AccountGeneration();
        FGuid Create;
        if (!TestTrue(TEXT("pending create admitted"),
            F.Request(ECk_OnlineSteam_Action::CreateLobby, Create))) { return false; }
        F.Tick(1.0);
        const auto Ticket = F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby);
        F.Provider->SessionExists = true;
        if (Case == 0)
        {
            F.Provider->Identity.AccountId = TEXT("account-b");
            F.Tick(1.1);
            F.Expect(*this, Create, ECk_OnlineSteam_Result::IdentityChanged);
            TestTrue(TEXT("identity change rotates account generation"),
                F.Snapshot().Get_AccountGeneration() != OriginalGeneration);
        }
        else
        {
            F.Host->Request_Shutdown();
            F.Expect(*this, Create, ECk_OnlineSteam_Result::Cancelled);
            TestFalse(TEXT("shutdown closes request admission"), F.Host->Get_IsAcceptingRequests());
        }
        TestEqual(TEXT("identity change or shutdown does not leave before provider result"),
            F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 0);
        F.Provider->LobbyId = TEXT("late-lobby");
        F.Provider->Complete(Ticket, ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Succeeded);
        if (Case == 0) { F.Tick(1.2); }
        else { F.Poll(1.2); }
        TestEqual(TEXT("late success owes one leave after identity change or shutdown"),
            F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 1);
        F.Provider->SessionExists = false;
        F.Provider->LobbyId.Reset();
        F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::LeaveLobby),
            ECk_OnlineSteam_Action::LeaveLobby, ECk_OnlineSteam_Result::Succeeded);
        if (Case == 0) { F.Tick(1.3); }
        else { F.Poll(1.3); }
        TestEqual(TEXT("original operation remains single completion after cleanup"), F.Count(Create), 1);
        TestEqual(TEXT("generic and typed callback counts match"),
            F.Generic->RequestCompletionCount, F.Typed->Completions.Num());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineSteamProviderReentrantShutdown,
    "Ck.Online.Steam.ProviderExecuteShutdownCompletesQueuedCancel", ck_test_online_steam::Flags)
bool FCkOnlineSteamProviderReentrantShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online_steam;
    FFixture F;
    if (!TestTrue(TEXT("provider callback shutdown fixture starts"), F.Start())) { return false; }
    F.Provider->OnExecute = [&F](const FProvider::FCall& Call)
    {
        if (Call.Action == ECk_OnlineSteam_Action::CreateLobby) { F.Host->Request_Shutdown(); }
    };
    FGuid Create;
    FGuid Cancel;
    if (!TestTrue(TEXT("create queued before provider callback"),
        F.Request(ECk_OnlineSteam_Action::CreateLobby, Create)) ||
        !TestTrue(TEXT("cancel queued against admitted create"),
            F.Request(ECk_OnlineSteam_Action::Cancel, Cancel, 10.0, Create)))
    { return false; }
    F.Tick(1.0);
    TestFalse(TEXT("provider callback closes host admission"), F.Host->Get_IsAcceptingRequests());
    TestEqual(TEXT("shutdown callback submits only the original provider operation"),
        F.Provider->Calls.Num(), 1);
    F.Expect(*this, Create, ECk_OnlineSteam_Result::Cancelled);
    TestEqual(TEXT("queued cancel receives exactly one typed terminal result"), F.Count(Cancel), 1);
    TestEqual(TEXT("queued requests each receive one generic result"),
        F.Generic->RequestCompletionCount, 2);
    F.Provider->Complete(F.Provider->LastTicket(ECk_OnlineSteam_Action::CreateLobby),
        ECk_OnlineSteam_Action::CreateLobby, ECk_OnlineSteam_Result::Failed);
    F.Poll(1.1);
    TestEqual(TEXT("shutdown cleanup needs no premature removal for failed create"),
        F.Provider->Count(ECk_OnlineSteam_Action::LeaveLobby), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
