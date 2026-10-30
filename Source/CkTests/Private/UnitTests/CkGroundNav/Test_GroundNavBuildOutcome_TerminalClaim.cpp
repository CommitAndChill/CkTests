#include "CkGroundNavBuildOutcome_TestTypes.h"

#include "../CkUnitTest_Common.h"

#include <UObject/StrongObjectPtr.h>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_GroundNavBuildOutcome_TerminalClaim,
    "CkTests.UnitTests.CkGroundNav.BuildOutcome.TerminalClaim",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_GroundNavBuildOutcome_TerminalClaim::RunTest(const FString& Parameters)
{
    const auto Generic = TStrongObjectPtr<UCk_GroundNavBuildOutcomeTest_Listener_UE>{
        NewObject<UCk_GroundNavBuildOutcomeTest_Listener_UE>()};
    auto LegacyRequest = FCk_Request_GroundNavVolume_Build{};
    auto GenericDelegate = FCk_Delegate_Request_OnCompleted{};
    GenericDelegate.BindDynamic(Generic.Get(), &UCk_GroundNavBuildOutcomeTest_Listener_UE::OnGeneric);
    LegacyRequest.Set_CompletionDelegate(GenericDelegate);
    Generic->RequestToReenter = &LegacyRequest;
    LegacyRequest.TryFireCompletion({}, ECk_Request_OperationResult::Failed);
    TestEqual(TEXT("legacy-only reentrant completion is claimed before generic notification"), Generic->GenericCount, 1);

    auto Request = FCk_Request_GroundNavVolume_Build{};
    Request.Set_RequestSequence(701);
    Request.Set_BuildAdmission(ECk_GroundNav_BuildAdmission::Accepted, 9);
    Request.Set_CompletionDelegate(GenericDelegate);
    Generic->GenericCount = 0;
    Generic->RequestToReenter = &Request;
    Generic->CollectDuringGeneric = true;
    auto Events = TArray<int32>{};
    Generic->Events = &Events;

    auto* Typed = NewObject<UCk_GroundNavBuildOutcomeTest_Listener_UE>();
    const auto WeakTyped = TWeakObjectPtr<UCk_GroundNavBuildOutcomeTest_Listener_UE>{Typed};
    Typed->Events = &Events;
    auto TypedDelegate = FCk_Delegate_GroundNavVolume_OnBuildResolved{};
    TypedDelegate.BindDynamic(Typed, &UCk_GroundNavBuildOutcomeTest_Listener_UE::OnResolved);
    Request.Set_OnBuildResolved(TypedDelegate);
    Request.TryFireCompletion({}, ECk_Request_OperationResult::Failed, ECk_GroundNav_BakeStatus::StaleGeometry);

    TestTrue(TEXT("typed callback target survives collection in the generic callback"), WeakTyped.IsValid());
    if (NOT WeakTyped.IsValid())
    { return false; }
    TestEqual(TEXT("reentry does not replay generic delivery"), Generic->GenericCount, 1);
    TestEqual(TEXT("typed delivery occurs once"), Typed->TypedCount, 1);
    TestTrue(TEXT("generic delivery precedes typed delivery"), Events == TArray<int32>{1, 2});
    TestEqual(TEXT("original request token is retained"), Typed->LastOutcome.Get_RequestSequence(), int64{701});
    TestEqual(TEXT("original attempt is retained"), Typed->LastOutcome.Get_AttemptSequence(), int64{9});
    TestTrue(TEXT("original stale cause survives reentrant cancellation"),
        Typed->LastOutcome.Get_HasBakeStatus() &&
        Typed->LastOutcome.Get_BakeStatus() == ECk_GroundNav_BakeStatus::StaleGeometry &&
        Typed->LastOutcome.Get_OperationResult() == ECk_Request_OperationResult::Failed);
    TestFalse(TEXT("an invalid volume is carried without a checked cast"), ck::IsValid(Typed->LastVolume));

    Generic->CollectDuringGeneric = false;
    Generic->Events = nullptr;
    Generic->GenericCount = 0;
    Generic->RequestToReenter = nullptr;
    auto Queue = UCk_GroundNavBuildOutcomeTest_Listener_UE::FRequestList{};
    auto QueuedRequest = FCk_Request_GroundNavVolume_Build{};
    QueuedRequest.Set_RequestSequence(702);
    QueuedRequest.Set_CompletionDelegate(GenericDelegate);
    QueuedRequest.Set_OnBuildResolved(TypedDelegate);
    Queue.Emplace(QueuedRequest);
    Generic->QueueToCancel = &Queue;
    Typed->Events = nullptr;
    Typed->TypedCount = 0;
    ck::request::FireCancelledForPending(FCk_Handle{}, Queue);
    ck::request::FireCancelledForPending(FCk_Handle{}, Queue);
    TestEqual(TEXT("concrete variant cancellation claims generic notification before reentry"), Generic->GenericCount, 1);
    TestEqual(TEXT("concrete variant cancellation resolves typed notification once"), Typed->TypedCount, 1);
    TestTrue(TEXT("queued cancellation has no admitted attempt or borrowed bake cause"),
        Typed->LastOutcome.Get_RequestSequence() == 702 && Typed->LastOutcome.Get_AttemptSequence() == 0 &&
        Typed->LastOutcome.Get_Admission() == ECk_GroundNav_BuildAdmission::NotEvaluated &&
        Typed->LastOutcome.Get_OperationResult() == ECk_Request_OperationResult::Failed_Cancelled &&
        NOT Typed->LastOutcome.Get_HasBakeStatus());
    return true;
}
