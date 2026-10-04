#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/CkOnline_Utils.h"

#include "CkTest_OnlineServiceListener.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_online
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

    class FAdapter final : public ck::IOnlineServiceAdapter
    {
    public:
        auto Execute(FGuid InOperationId, FCompletion InCompletion) -> void override
        {
            ++ExecuteCount;
            auto Callback = MakeShared<FCompletion>(MoveTemp(InCompletion));
            Callbacks.Add(InOperationId, Callback);
            if (CompleteInline)
            { (*Callback)(ECk_OnlineService_Result::Succeeded); }
        }

        auto Cancel(FGuid InOperationId) -> void override
        {
            CancelledIds.Add(InOperationId);
        }

        auto Shutdown() -> void override
        {
            ++ShutdownCount;
        }

        auto Fire(FGuid InOperationId, ECk_OnlineService_Result InResult) -> bool
        {
            const auto* Callback = Callbacks.Find(InOperationId);
            if (Callback == nullptr)
            { return false; }
            (**Callback)(InResult);
            return true;
        }

        TMap<FGuid, TSharedPtr<FCompletion>> Callbacks;
        TArray<FGuid> CancelledIds;
        int32 ExecuteCount = 0;
        int32 ShutdownCount = 0;
        bool CompleteInline = false;
    };

    struct FFixture
    {
        TSharedRef<FAdapter> Adapter = MakeShared<FAdapter>();
        TSharedPtr<ck::FOnlineServiceHost> Host;
        FCk_Handle_OnlineService Handle;
        TStrongObjectPtr<UCk_Test_OnlineServiceListener_UE> Listener{
            NewObject<UCk_Test_OnlineServiceListener_UE>()};

        auto Start(double InInitialSeconds = 0.0) -> bool
        {
            Host = ck::FOnlineServiceHost::TryCreate(Adapter, FCk_Time{InInitialSeconds});
            if (NOT Host.IsValid())
            { return false; }
            Handle = Host->Get_Handle();
            return ck::IsValid(Handle);
        }

        auto Bind() -> void
        {
            auto Delegate = FCk_Delegate_OnlineService_Completed{};
            Delegate.BindDynamic(Listener.Get(), &UCk_Test_OnlineServiceListener_UE::OnCompleted);
            UCk_Utils_OnlineService_UE::BindTo_OnCompleted(Handle, Delegate,
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
                ECk_Signal_PostFireBehavior::DoNothing);
        }

        auto Generic() const -> FCk_Delegate_Request_OnCompleted
        {
            auto Delegate = FCk_Delegate_Request_OnCompleted{};
            Delegate.BindDynamic(Listener.Get(), &UCk_Test_OnlineServiceListener_UE::OnRequestCompleted);
            return Delegate;
        }

        auto Execute(FGuid InOperationId, double InTimeoutSeconds = 5.0) -> void
        {
            UCk_Utils_OnlineService_UE::Request_Execute(Handle,
                FCk_Request_OnlineService_Execute{InOperationId}.Set_Timeout(FCk_Time{InTimeoutSeconds}), Generic());
        }

        auto Cancel(FGuid InOperationId) -> void
        {
            UCk_Utils_OnlineService_UE::Request_Cancel(Handle,
                FCk_Request_OnlineService_Cancel{InOperationId}, Generic());
        }

        auto Tick(double InSeconds) -> void
        {
            Host->Tick(FCk_Time{InSeconds});
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_InlineAndDelayed,
    "Ck.Online.Host.InlineAndDelayedCompletion", ck_test_online::TestFlags)

bool FCkTest_Online_InlineAndDelayed::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started with a valid handle"), Fixture.Start()))
    { return false; }
    Fixture.Bind();

    const auto InlineId = FGuid::NewGuid();
    Fixture.Adapter->CompleteInline = true;
    Fixture.Execute(InlineId);
    TestEqual(TEXT("request remains queued until scheduler tick"),
        UCk_Utils_OnlineService_UE::Get_Diagnostics(Fixture.Handle).Get_Queued(), 1);
    TestEqual(TEXT("adapter has not run during enqueue"), Fixture.Adapter->ExecuteCount, 0);
    Fixture.Tick(0.0);
    TestEqual(TEXT("inline callback was delivered to adapter"), Fixture.Adapter->ExecuteCount, 1);
    TestEqual(TEXT("inline completion fires once after scheduler and dispatch"), Fixture.Listener->Completions.Num(), 1);
    if (Fixture.Listener->Completions.Num() == 1)
    {
        TestEqual(TEXT("inline completion identifies operation"),
            Fixture.Listener->Completions[0].Get_OperationId(), InlineId);
        TestEqual(TEXT("inline result succeeds"), Fixture.Listener->Completions[0].Get_Result(),
            ECk_OnlineService_Result::Succeeded);
        TestTrue(TEXT("signal identifies its service"), Fixture.Listener->Services[0] == Fixture.Handle);
    }
    TestEqual(TEXT("generic completion fires once"), Fixture.Listener->RequestCompletionCount, 1);
    TestEqual(TEXT("generic result succeeds"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Succeeded);

    const auto DelayedId = FGuid::NewGuid();
    Fixture.Adapter->CompleteInline = false;
    Fixture.Execute(DelayedId);
    Fixture.Tick(0.1);
    TestEqual(TEXT("delayed request reached adapter"), Fixture.Adapter->ExecuteCount, 2);
    TestEqual(TEXT("delayed request is pending"), Fixture.Host->Get_Diagnostics().Get_Pending(), 1);
    TestEqual(TEXT("no premature delayed signal"), Fixture.Listener->Completions.Num(), 1);
    TestTrue(TEXT("adapter retained delayed callback"),
        Fixture.Adapter->Fire(DelayedId, ECk_OnlineService_Result::Failed));
    Fixture.Tick(0.2);
    TestEqual(TEXT("delayed result delivered exactly once"), Fixture.Listener->Completions.Num(), 2);
    if (Fixture.Listener->Completions.Num() == 2)
    {
        TestEqual(TEXT("delayed completion identifies operation"),
            Fixture.Listener->Completions[1].Get_OperationId(), DelayedId);
        TestEqual(TEXT("delayed failure preserved"), Fixture.Listener->Completions[1].Get_Result(),
            ECk_OnlineService_Result::Failed);
    }
    TestEqual(TEXT("both generic delegates completed"), Fixture.Listener->RequestCompletionCount, 2);
    TestEqual(TEXT("two terminal operations recorded"), Fixture.Host->Get_Diagnostics().Get_Completed(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_LateDuplicate,
    "Ck.Online.Host.LateDuplicateCallback", ck_test_online::TestFlags)

bool FCkTest_Online_LateDuplicate::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id);
    Fixture.Tick(0.0);
    TestEqual(TEXT("request reached adapter"), Fixture.Adapter->ExecuteCount, 1);
    TestEqual(TEXT("request is pending"), Fixture.Host->Get_Diagnostics().Get_Pending(), 1);
    TestTrue(TEXT("first callback exists"), Fixture.Adapter->Fire(Id, ECk_OnlineService_Result::Succeeded));
    Fixture.Tick(0.1);
    TestEqual(TEXT("first callback completed operation"), Fixture.Listener->Completions.Num(), 1);
    TestEqual(TEXT("first callback completed generic delegate"), Fixture.Listener->RequestCompletionCount, 1);
    const auto CompletedBeforeReplay = Fixture.Host->Get_Diagnostics().Get_Completed();
    TestTrue(TEXT("adapter can replay retained callback"), Fixture.Adapter->Fire(Id, ECk_OnlineService_Result::Failed));
    Fixture.Tick(0.2);
    TestEqual(TEXT("late replay was diagnosed"), Fixture.Host->Get_Diagnostics().Get_IgnoredCallbacks(), 1);
    TestEqual(TEXT("replay does not change terminal count"), Fixture.Host->Get_Diagnostics().Get_Completed(),
        CompletedBeforeReplay);
    TestEqual(TEXT("replay does not fire a second signal"), Fixture.Listener->Completions.Num(), 1);
    TestEqual(TEXT("replay does not fire a second generic completion"), Fixture.Listener->RequestCompletionCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_CancelTimeout,
    "Ck.Online.Host.CancelAndTimeout", ck_test_online::TestFlags)

bool FCkTest_Online_CancelTimeout::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto CancelId = FGuid::NewGuid();
    const auto TimeoutId = FGuid::NewGuid();
    Fixture.Execute(CancelId, 2.0);
    Fixture.Execute(TimeoutId, 1.0);
    Fixture.Tick(0.0);
    TestEqual(TEXT("both requests reached adapter"), Fixture.Adapter->ExecuteCount, 2);
    TestEqual(TEXT("both are pending"), Fixture.Host->Get_Diagnostics().Get_Pending(), 2);
    Fixture.Cancel(CancelId);
    Fixture.Tick(0.5);
    TestTrue(TEXT("cancel propagated to adapter"), Fixture.Adapter->CancelledIds.Contains(CancelId));
    TestEqual(TEXT("cancel emitted a terminal completion"), Fixture.Listener->Completions.Num(), 1);
    if (Fixture.Listener->Completions.Num() == 1)
    {
        TestEqual(TEXT("cancel identifies operation"), Fixture.Listener->Completions[0].Get_OperationId(), CancelId);
        TestEqual(TEXT("cancel result"), Fixture.Listener->Completions[0].Get_Result(),
            ECk_OnlineService_Result::Cancelled);
    }
    Fixture.Tick(1.0);
    TestEqual(TEXT("deadline emitted second completion"), Fixture.Listener->Completions.Num(), 2);
    if (Fixture.Listener->Completions.Num() == 2)
    {
        TestEqual(TEXT("timeout identifies operation"), Fixture.Listener->Completions[1].Get_OperationId(), TimeoutId);
        TestEqual(TEXT("timeout wins at exact deadline"), Fixture.Listener->Completions[1].Get_Result(),
            ECk_OnlineService_Result::TimedOut);
    }
    TestEqual(TEXT("cancel counter"), Fixture.Host->Get_Diagnostics().Get_Cancelled(), 1);
    TestEqual(TEXT("timeout counter"), Fixture.Host->Get_Diagnostics().Get_TimedOut(), 1);
    TestEqual(TEXT("all execute requests terminated"), Fixture.Host->Get_Diagnostics().Get_Completed(), 2);
    TestEqual(TEXT("execute and cancel generic delegates each fired once"),
        Fixture.Listener->RequestCompletionCount, 3);
    TestTrue(TEXT("late timeout callback exists"),
        Fixture.Adapter->Fire(TimeoutId, ECk_OnlineService_Result::Succeeded));
    Fixture.Tick(1.1);
    TestEqual(TEXT("late callback ignored"), Fixture.Host->Get_Diagnostics().Get_IgnoredCallbacks(), 1);
    TestEqual(TEXT("no third terminal signal"), Fixture.Listener->Completions.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_Admission,
    "Ck.Online.Host.AdmissionAndStaleHandle", ck_test_online::TestFlags)

bool FCkTest_Online_Admission::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    auto InvalidHandle = FCk_Handle_OnlineService{};
    UCk_Utils_OnlineService_UE::Request_Execute(InvalidHandle,
        FCk_Request_OnlineService_Execute{FGuid::NewGuid()}, Fixture.Generic());
    TestEqual(TEXT("default handle rejected synchronously"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Failed_NotEnqueued);
    TestEqual(TEXT("default handle did not reach adapter"), Fixture.Adapter->ExecuteCount, 0);

    // CK reports the same ensure to both its diagnostic log and the editor message log.
    AddExpectedError(TEXT("Online operation requires a valid ID and a positive finite timeout."),
        EAutomationExpectedErrorFlags::Contains, 2);
    const auto RejectedBeforeInvalidId = Fixture.Listener->RequestCompletionCount;
    Fixture.Execute(FGuid{});
    TestEqual(TEXT("invalid ID rejected without queueing"), Fixture.Host->Get_Diagnostics().Get_Queued(), 0);
    TestEqual(TEXT("invalid ID cannot reach adapter"), Fixture.Adapter->ExecuteCount, 0);
    TestEqual(TEXT("invalid ID rejects exactly once"),
        Fixture.Listener->RequestCompletionCount, RejectedBeforeInvalidId + 1);
    TestEqual(TEXT("invalid ID reports not enqueued"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Failed_NotEnqueued);

    for (int32 Index = 0; Index < 64; ++Index)
    { Fixture.Execute(FGuid::NewGuid()); }
    TestEqual(TEXT("admission reaches hard cap"), Fixture.Host->Get_Diagnostics().Get_Queued(), 64);
    const auto RejectedBefore = Fixture.Listener->RequestCompletionCount;
    Fixture.Execute(FGuid::NewGuid());
    TestEqual(TEXT("cap rejects exactly one additional request synchronously"),
        Fixture.Listener->RequestCompletionCount, RejectedBefore + 1);
    TestEqual(TEXT("cap rejection reports not enqueued"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Failed_NotEnqueued);
    Fixture.Tick(0.0);
    TestEqual(TEXT("only admitted requests reached adapter"), Fixture.Adapter->ExecuteCount, 64);
    TestEqual(TEXT("pending count is capped"), Fixture.Host->Get_Diagnostics().Get_Pending(), 64);

    Fixture.Host->Request_Shutdown();
    Fixture.Tick(0.1);
    TestEqual(TEXT("adapter shut down once"), Fixture.Adapter->ShutdownCount, 1);
    TestTrue(TEXT("old handle invalid after shutdown"), ck::Is_NOT_Valid(Fixture.Handle));
    const auto ExecutedBeforeStale = Fixture.Adapter->ExecuteCount;
    Fixture.Execute(FGuid::NewGuid());
    TestEqual(TEXT("stale handle cannot reach adapter"), Fixture.Adapter->ExecuteCount, ExecutedBeforeStale);
    TestEqual(TEXT("stale handle rejects request"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Failed_NotEnqueued);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_IsolatedShutdown,
    "Ck.Online.Host.ContextIsolationAndListenerShutdown", ck_test_online::TestFlags)

bool FCkTest_Online_IsolatedShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto First = FFixture{};
    auto Second = FFixture{};
    if (NOT TestTrue(TEXT("first host started"), First.Start()) ||
        NOT TestTrue(TEXT("second host started"), Second.Start()))
    { return false; }
    First.Bind();
    Second.Bind();
    TestTrue(TEXT("contexts have distinct generations"),
        First.Host->Get_Diagnostics().Get_Generation() != Second.Host->Get_Diagnostics().Get_Generation());
    const auto FirstId = FGuid::NewGuid();
    const auto SecondId = FGuid::NewGuid();
    First.Execute(FirstId);
    Second.Execute(SecondId);
    First.Tick(0.0);
    Second.Tick(0.0);
    TestEqual(TEXT("first adapter received its request"), First.Adapter->ExecuteCount, 1);
    TestEqual(TEXT("second adapter received its request"), Second.Adapter->ExecuteCount, 1);

    First.Listener->OnCompletion = [&First]() { First.Host->Request_Shutdown(); };
    TestTrue(TEXT("first callback exists"), First.Adapter->Fire(FirstId, ECk_OnlineService_Result::Succeeded));
    First.Tick(0.1);
    TestEqual(TEXT("first signal fired once"), First.Listener->Completions.Num(), 1);
    TestFalse(TEXT("listener requested immediate admission stop"), First.Host->Get_IsAcceptingRequests());
    First.Tick(0.2);
    TestEqual(TEXT("first adapter shut down"), First.Adapter->ShutdownCount, 1);
    TestTrue(TEXT("first handle invalidated"), ck::Is_NOT_Valid(First.Handle));

    TestEqual(TEXT("second context remained pending"), Second.Host->Get_Diagnostics().Get_Pending(), 1);
    TestTrue(TEXT("second callback exists"), Second.Adapter->Fire(SecondId, ECk_OnlineService_Result::Succeeded));
    Second.Tick(0.1);
    TestEqual(TEXT("second context completed independently"), Second.Listener->Completions.Num(), 1);
    TestEqual(TEXT("second context did not shut down"), Second.Adapter->ShutdownCount, 0);
    TestTrue(TEXT("second context still accepts requests"), Second.Host->Get_IsAcceptingRequests());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_ReflectedRequest,
    "Ck.Online.Utils.ReflectedRequestDispatch", ck_test_online::TestFlags)

bool FCkTest_Online_ReflectedRequest::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();

    auto* Class = UCk_Utils_OnlineService_UE::StaticClass();
    auto* Function = Class->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_OnlineService_UE, Request_Execute));
    if (NOT TestNotNull(TEXT("Blueprint-callable request has a reflected UFunction"), Function))
    { return false; }
    auto* ServiceProperty = FindFProperty<FStructProperty>(Function, TEXT("InService"));
    auto* RequestProperty = FindFProperty<FStructProperty>(Function, TEXT("InRequest"));
    auto* DelegateProperty = FindFProperty<FDelegateProperty>(Function, TEXT("InDelegate"));
    auto* ReturnProperty = CastField<FStructProperty>(Function->GetReturnProperty());
    if (NOT TestNotNull(TEXT("reflected service parameter"), ServiceProperty) ||
        NOT TestNotNull(TEXT("reflected request parameter"), RequestProperty) ||
        NOT TestNotNull(TEXT("reflected delegate parameter"), DelegateProperty) ||
        NOT TestNotNull(TEXT("reflected return value"), ReturnProperty))
    { return false; }

    auto Params = TArray<uint8>{};
    Params.SetNumZeroed(Function->ParmsSize);
    Function->InitializeStruct(Params.GetData());
    ON_SCOPE_EXIT { Function->DestroyStruct(Params.GetData()); };
    ServiceProperty->CopyCompleteValue(ServiceProperty->ContainerPtrToValuePtr<void>(Params.GetData()),
        &Fixture.Handle);
    const auto Id = FGuid::NewGuid();
    const auto Request = FCk_Request_OnlineService_Execute{Id}.Set_Timeout(FCk_Time{2.0});
    RequestProperty->CopyCompleteValue(RequestProperty->ContainerPtrToValuePtr<void>(Params.GetData()), &Request);
    const auto Delegate = Fixture.Generic();
    DelegateProperty->CopyCompleteValue(DelegateProperty->ContainerPtrToValuePtr<void>(Params.GetData()), &Delegate);
    Class->GetDefaultObject()->ProcessEvent(Function, Params.GetData());

    auto Returned = FCk_Handle_OnlineService{};
    ReturnProperty->CopyCompleteValue(&Returned, ReturnProperty->ContainerPtrToValuePtr<void>(Params.GetData()));
    TestTrue(TEXT("reflected request returned same service"), Returned == Fixture.Handle);
    TestEqual(TEXT("reflected request entered production queue"), Fixture.Host->Get_Diagnostics().Get_Queued(), 1);
    Fixture.Tick(0.0);
    TestEqual(TEXT("reflected request reached adapter"), Fixture.Adapter->ExecuteCount, 1);
    TestTrue(TEXT("reflected request callback retained"),
        Fixture.Adapter->Fire(Id, ECk_OnlineService_Result::Succeeded));
    Fixture.Tick(0.1);
    TestEqual(TEXT("reflected request completed via production dispatch"), Fixture.Listener->Completions.Num(), 1);
    TestEqual(TEXT("reflected request generic completion fired"), Fixture.Listener->RequestCompletionCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_ExternalEntityTeardown,
    "Ck.Online.Host.ExternalEntityTeardown", ck_test_online::TestFlags)

bool FCkTest_Online_ExternalEntityTeardown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id);
    Fixture.Tick(0.0);
    TestEqual(TEXT("request reached adapter before teardown"), Fixture.Adapter->ExecuteCount, 1);
    TestEqual(TEXT("request was pending before teardown"), Fixture.Host->Get_Diagnostics().Get_Pending(), 1);

    auto Entity = FCk_Handle{Fixture.Handle};
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Entity);
    Fixture.Tick(0.1);
    TestEqual(TEXT("entity teardown closes adapter"), Fixture.Adapter->ShutdownCount, 1);
    TestTrue(TEXT("entity teardown invalidates handle"), ck::Is_NOT_Valid(Fixture.Handle));
    TestEqual(TEXT("pending request completed exactly once"), Fixture.Listener->Completions.Num(), 1);
    if (Fixture.Listener->Completions.Num() == 1)
    {
        TestEqual(TEXT("teardown cancels pending operation"), Fixture.Listener->Completions[0].Get_Result(),
            ECk_OnlineService_Result::Cancelled);
    }
    TestEqual(TEXT("teardown retained cancellation diagnostic"), Fixture.Host->Get_Diagnostics().Get_Cancelled(), 1);
    TestEqual(TEXT("teardown left no pending work"), Fixture.Host->Get_Diagnostics().Get_Pending(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_GenericShutdownWithPending,
    "Ck.Online.Host.GenericCompletionShutdownCancelsPending", ck_test_online::TestFlags)

bool FCkTest_Online_GenericShutdownWithPending::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto CompletedId = FGuid::NewGuid();
    const auto PendingId = FGuid::NewGuid();
    Fixture.Execute(CompletedId);
    Fixture.Execute(PendingId);
    Fixture.Tick(0.0);
    TestEqual(TEXT("both operations reached adapter"), Fixture.Adapter->ExecuteCount, 2);
    TestEqual(TEXT("both operations are pending"), Fixture.Host->Get_Diagnostics().Get_Pending(), 2);

    Fixture.Listener->OnRequestCompletion = [&Fixture]()
    {
        if (Fixture.Listener->RequestCompletionCount == 1)
        { Fixture.Host->Request_Shutdown(); }
    };
    TestTrue(TEXT("completed callback retained"),
        Fixture.Adapter->Fire(CompletedId, ECk_OnlineService_Result::Succeeded));
    Fixture.Tick(0.1);

    TestEqual(TEXT("completed and pending operations each signal once"), Fixture.Listener->Completions.Num(), 2);
    if (Fixture.Listener->Completions.Num() == 2)
    {
        TestEqual(TEXT("first operation completed"), Fixture.Listener->Completions[0].Get_OperationId(), CompletedId);
        TestEqual(TEXT("first operation succeeded"), Fixture.Listener->Completions[0].Get_Result(),
            ECk_OnlineService_Result::Succeeded);
        TestEqual(TEXT("second operation identified"), Fixture.Listener->Completions[1].Get_OperationId(), PendingId);
        TestEqual(TEXT("second operation cancelled during shutdown"), Fixture.Listener->Completions[1].Get_Result(),
            ECk_OnlineService_Result::Cancelled);
    }
    TestEqual(TEXT("generic completions each fire once"), Fixture.Listener->RequestCompletionCount, 2);
    if (Fixture.Listener->RequestResults.Num() == 2)
    {
        TestEqual(TEXT("first generic completion succeeded"), Fixture.Listener->RequestResults[0],
            ECk_Request_OperationResult::Succeeded);
        TestEqual(TEXT("second generic completion was cancelled"), Fixture.Listener->RequestResults[1],
            ECk_Request_OperationResult::Failed_Cancelled);
    }
    TestTrue(TEXT("shutdown cancelled second adapter operation"), Fixture.Adapter->CancelledIds.Contains(PendingId));
    TestEqual(TEXT("adapter shut down once"), Fixture.Adapter->ShutdownCount, 1);
    TestEqual(TEXT("two terminal executes recorded"), Fixture.Host->Get_Diagnostics().Get_Completed(), 2);
    TestEqual(TEXT("one cancellation recorded"), Fixture.Host->Get_Diagnostics().Get_Cancelled(), 1);
    TestTrue(TEXT("handle invalidated after dispatch"), ck::Is_NOT_Valid(Fixture.Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_FinalOwnerRelease,
    "Ck.Online.Host.FinalOwnerReleaseCancelsPending", ck_test_online::TestFlags)

bool FCkTest_Online_FinalOwnerRelease::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id);
    Fixture.Tick(0.0);
    TestEqual(TEXT("request reached adapter before final release"), Fixture.Adapter->ExecuteCount, 1);
    TestEqual(TEXT("operation pending before final release"), Fixture.Host->Get_Diagnostics().Get_Pending(), 1);

    Fixture.Host.Reset();
    TestEqual(TEXT("adapter shut down on final owner release"), Fixture.Adapter->ShutdownCount, 1);
    TestTrue(TEXT("pending work was cancelled at adapter"), Fixture.Adapter->CancelledIds.Contains(Id));
    TestEqual(TEXT("terminal signal fired once while owner was released"), Fixture.Listener->Completions.Num(), 1);
    if (Fixture.Listener->Completions.Num() == 1)
    {
        TestEqual(TEXT("terminal completion identifies pending work"),
            Fixture.Listener->Completions[0].Get_OperationId(), Id);
        TestEqual(TEXT("terminal completion is cancellation"), Fixture.Listener->Completions[0].Get_Result(),
            ECk_OnlineService_Result::Cancelled);
    }
    TestEqual(TEXT("generic completion fired once while owner was released"),
        Fixture.Listener->RequestCompletionCount, 1);
    TestEqual(TEXT("generic completion is cancelled"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Failed_Cancelled);
    TestTrue(TEXT("final release invalidated old handle"), ck::Is_NOT_Valid(Fixture.Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_ReleaseOwnerDuringShutdown,
    "Ck.Online.Host.ReleaseOwnerDuringShutdownDispatch", ck_test_online::TestFlags)

bool FCkTest_Online_ReleaseOwnerDuringShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id);
    Fixture.Tick(0.0);
    Fixture.Listener->OnCompletion = [&Fixture]() { Fixture.Host.Reset(); };

    Fixture.Host->Request_Shutdown();

    TestFalse(TEXT("listener released the last external owner"), Fixture.Host.IsValid());
    TestEqual(TEXT("shutdown signal completed once"), Fixture.Listener->Completions.Num(), 1);
    TestEqual(TEXT("generic completion survived owner release"), Fixture.Listener->RequestCompletionCount, 1);
    TestEqual(TEXT("generic completion reported cancellation"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Failed_Cancelled);
    TestTrue(TEXT("adapter work was cancelled"), Fixture.Adapter->CancelledIds.Contains(Id));
    TestEqual(TEXT("adapter shut down once"), Fixture.Adapter->ShutdownCount, 1);
    TestTrue(TEXT("service handle was invalidated"), ck::Is_NOT_Valid(Fixture.Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_ReplayShutdown,
    "Ck.Online.Host.ReplayedSignalShutdown", ck_test_online::TestFlags)

bool FCkTest_Online_ReplayShutdown::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id);
    Fixture.Tick(0.0);
    TestEqual(TEXT("request reached adapter"), Fixture.Adapter->ExecuteCount, 1);
    TestTrue(TEXT("callback retained"), Fixture.Adapter->Fire(Id, ECk_OnlineService_Result::Succeeded));
    Fixture.Tick(0.1);
    TestEqual(TEXT("signal fired before replay binding"), Fixture.Listener->Completions.Num(), 1);
    TestEqual(TEXT("host still running before replay binding"), Fixture.Adapter->ShutdownCount, 0);

    auto ReplayListener = TStrongObjectPtr<UCk_Test_OnlineServiceListener_UE>{
        NewObject<UCk_Test_OnlineServiceListener_UE>()};
    ReplayListener->OnCompletion = [&Fixture]() { Fixture.Host->Request_Shutdown(); };
    auto ReplayDelegate = FCk_Delegate_OnlineService_Completed{};
    ReplayDelegate.BindDynamic(ReplayListener.Get(), &UCk_Test_OnlineServiceListener_UE::OnCompleted);
    UCk_Utils_OnlineService_UE::BindTo_OnCompleted(Fixture.Handle, ReplayDelegate,
        ECk_Signal_BindingPolicy::FireIfPayloadInFlight,
        ECk_Signal_PostFireBehavior::DoNothing);

    TestEqual(TEXT("retained signal replayed synchronously exactly once"), ReplayListener->Completions.Num(), 1);
    if (ReplayListener->Completions.Num() == 1)
    {
        TestEqual(TEXT("replayed payload identifies operation"),
            ReplayListener->Completions[0].Get_OperationId(), Id);
    }
    TestEqual(TEXT("replay-triggered shutdown closed adapter"), Fixture.Adapter->ShutdownCount, 1);
    TestTrue(TEXT("replay-triggered shutdown invalidated handle"), ck::Is_NOT_Valid(Fixture.Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_WorkerCallback,
    "Ck.Online.Host.WorkerCallbackDrainsOnTick", ck_test_online::TestFlags)

bool FCkTest_Online_WorkerCallback::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id);
    Fixture.Tick(0.0);
    TestEqual(TEXT("request reached adapter before worker completion"), Fixture.Adapter->ExecuteCount, 1);
    TestEqual(TEXT("request pending before worker completion"), Fixture.Host->Get_Diagnostics().Get_Pending(), 1);

    auto Worker = Async(EAsyncExecution::ThreadPool, [&Fixture, Id]()
    { return Fixture.Adapter->Fire(Id, ECk_OnlineService_Result::Succeeded); });
    TestTrue(TEXT("worker invoked retained callback"), Worker.Get());
    TestEqual(TEXT("worker callback did not dispatch on worker"), Fixture.Listener->Completions.Num(), 0);
    TestEqual(TEXT("generic delegate did not dispatch on worker"), Fixture.Listener->RequestCompletionCount, 0);
    Fixture.Tick(0.1);
    TestEqual(TEXT("game-thread tick dispatched worker result once"), Fixture.Listener->Completions.Num(), 1);
    TestEqual(TEXT("game-thread tick dispatched generic result once"), Fixture.Listener->RequestCompletionCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_InboxOverflow,
    "Ck.Online.Host.InboxOverflowFailsPending", ck_test_online::TestFlags)

bool FCkTest_Online_InboxOverflow::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host started"), Fixture.Start()))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id);
    Fixture.Tick(0.0);
    TestEqual(TEXT("request reached adapter before overflow"), Fixture.Adapter->ExecuteCount, 1);
    TestEqual(TEXT("request pending before overflow"), Fixture.Host->Get_Diagnostics().Get_Pending(), 1);
    for (int32 Index = 0; Index < 257; ++Index)
    {
        if (NOT Fixture.Adapter->Fire(Id, ECk_OnlineService_Result::Succeeded))
        {
            AddError(TEXT("adapter lost callback before inbox overflow"));
            return false;
        }
    }
    Fixture.Tick(0.1);
    TestEqual(TEXT("overflow fails pending operation once"), Fixture.Listener->Completions.Num(), 1);
    if (Fixture.Listener->Completions.Num() == 1)
    {
        TestEqual(TEXT("overflow result is failed"), Fixture.Listener->Completions[0].Get_Result(),
            ECk_OnlineService_Result::Failed);
    }
    TestEqual(TEXT("overflow generic completion fires once"), Fixture.Listener->RequestCompletionCount, 1);
    TestEqual(TEXT("overflow cancelled native operation"), Fixture.Adapter->CancelledIds.Num(), 1);
    TestEqual(TEXT("overflow drained all retained callbacks as stale"),
        Fixture.Host->Get_Diagnostics().Get_IgnoredCallbacks(), 256);
    TestEqual(TEXT("overflow left no pending work"), Fixture.Host->Get_Diagnostics().Get_Pending(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_Online_DeadlineOverflow,
    "Ck.Online.Host.DeadlineOverflowFailsClosed", ck_test_online::TestFlags)

bool FCkTest_Online_DeadlineOverflow::RunTest(const FString& Parameters)
{
    using namespace ck_test_online;
    const auto MaxFinite = std::numeric_limits<double>::max();
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("host accepts finite initial clock"), Fixture.Start(MaxFinite)))
    { return false; }
    Fixture.Bind();
    const auto Id = FGuid::NewGuid();
    Fixture.Execute(Id, MaxFinite);
    TestEqual(TEXT("finite request was admitted to scheduler"), Fixture.Host->Get_Diagnostics().Get_Queued(), 1);
    Fixture.Tick(MaxFinite);
    TestEqual(TEXT("overflowed deadline never reaches adapter"), Fixture.Adapter->ExecuteCount, 0);
    TestEqual(TEXT("overflowed deadline completes once"), Fixture.Listener->Completions.Num(), 1);
    if (Fixture.Listener->Completions.Num() == 1)
    {
        TestEqual(TEXT("overflowed deadline identifies operation"),
            Fixture.Listener->Completions[0].Get_OperationId(), Id);
        TestEqual(TEXT("overflowed deadline fails closed"), Fixture.Listener->Completions[0].Get_Result(),
            ECk_OnlineService_Result::Failed);
    }
    TestEqual(TEXT("overflowed deadline generic completion fires once"),
        Fixture.Listener->RequestCompletionCount, 1);
    TestEqual(TEXT("overflowed deadline generic result failed"), Fixture.Listener->LastRequestResult,
        ECk_Request_OperationResult::Failed);
    TestEqual(TEXT("overflowed deadline left no pending work"), Fixture.Host->Get_Diagnostics().Get_Pending(), 0);
    Fixture.Tick(MaxFinite);
    TestEqual(TEXT("later tick does not repeat completion"), Fixture.Listener->Completions.Num(), 1);
    const auto RoundedId = FGuid::NewGuid();
    Fixture.Execute(RoundedId, 1.0);
    Fixture.Tick(MaxFinite);
    TestEqual(TEXT("rounded-back deadline never reaches adapter"), Fixture.Adapter->ExecuteCount, 0);
    TestEqual(TEXT("rounded-back deadline completes once"), Fixture.Listener->Completions.Num(), 2);
    if (Fixture.Listener->Completions.Num() == 2)
    {
        TestEqual(TEXT("rounded-back deadline identifies operation"),
            Fixture.Listener->Completions[1].Get_OperationId(), RoundedId);
        TestEqual(TEXT("rounded-back deadline fails closed"), Fixture.Listener->Completions[1].Get_Result(),
            ECk_OnlineService_Result::Failed);
    }
    TestEqual(TEXT("rounded-back deadline generic completion fires once"),
        Fixture.Listener->RequestCompletionCount, 2);
    TestEqual(TEXT("rounded-back deadline leaves no pending work"), Fixture.Host->Get_Diagnostics().Get_Pending(), 0);
    Fixture.Tick(MaxFinite);
    TestEqual(TEXT("later tick does not repeat rounded-back completion"), Fixture.Listener->Completions.Num(), 2);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
