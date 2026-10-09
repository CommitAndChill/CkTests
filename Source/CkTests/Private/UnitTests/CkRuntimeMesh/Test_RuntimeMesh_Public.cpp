#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Processor.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkCore/Time/CkTime.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkTest_RuntimeMeshListener.h"
#include "../CkUnitTest_Common.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

namespace ck_test_runtime_mesh_public
{
    constexpr auto InformEngineOfWorld = false;
    constexpr TCHAR CpuPath[] =
        TEXT("/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU");

    struct FFixture
    {
        UWorld* World = nullptr;
        TStrongObjectPtr<UStaticMesh> Asset;
        FCk_Handle WorldEntity;
        FCk_Handle Owner;
        FCk_Handle ResultOwner;
        FCk_Handle_RuntimeMesh Source;

        auto Initialize() -> bool
        {
            World = UWorld::CreateWorld(EWorldType::Game, InformEngineOfWorld,
                TEXT("RuntimeMeshPublicFixture"));
            if (World == nullptr)
            { return false; }
            World->AddToRoot();
            WorldEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
            Asset.Reset(LoadObject<UStaticMesh>(nullptr, CpuPath));
            if (NOT Asset.IsValid() || ck::Is_NOT_Valid(WorldEntity))
            { return false; }
            Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
            ResultOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
            auto Spec = FCk_RuntimeMesh_Spec{};
            Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{Asset.Get()});
            Source = UCk_Utils_RuntimeMesh_UE::Add(Owner, Spec);
            if (ck::Is_NOT_Valid(Source))
            { return false; }
            ck::FProcessor_RuntimeMesh_Setup{WorldEntity.Get_RegistryView()}.ForEachEntity(
                FCk_Time{}, Source, Source.Get<ck::FFragment_RuntimeMesh>(),
                Source.Get<ck::FFragment_RuntimeMesh_PendingImport>());
            return UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source)
                == ECk_RuntimeMesh_SetupState::Ready;
        }

        ~FFixture()
        {
            if (World != nullptr)
            {
                ck::runtimemesh::CancelWorld(World);
                World->RemoveFromRoot();
                World->DestroyWorld(InformEngineOfWorld);
            }
        }

        auto Drain() -> void
        {
            ck::FProcessor_RuntimeMesh_Drain{WorldEntity.Get_RegistryView()}.DoTick(
                FCk_Time{1.0f / 60.0f});
        }
    };

    auto MakeRequest(const FFixture& InFixture, const FVector& InPosition)
        -> FCk_Request_RuntimeMesh_Slice
    {
        auto Request = FCk_Request_RuntimeMesh_Slice{};
        Request.Set_OperationID(FGuid::NewGuid());
        Request.Set_ResultOwner(InFixture.ResultOwner);
        auto Plane = FCk_RuntimeMesh_PlaneLocal{};
        Plane.Set_PositionCm(InPosition);
        Plane.Set_Normal(FVector::ForwardVector);
        Plane.Set_Tangent(FVector::RightVector);
        Request.Set_Plane(Plane);
        return Request;
    }

    auto Receiver(UCk_Test_RuntimeMeshListener_UE* InListener)
        -> FCk_Delegate_RuntimeMesh_OnSliceResolved
    {
        auto Delegate = FCk_Delegate_RuntimeMesh_OnSliceResolved{};
        Delegate.BindDynamic(InListener, &UCk_Test_RuntimeMeshListener_UE::OnSliceResolved);
        return Delegate;
    }

    auto Completion(UCk_Test_RuntimeMeshListener_UE* InListener)
        -> FCk_Delegate_Request_OnCompleted
    {
        auto Delegate = FCk_Delegate_Request_OnCompleted{};
        Delegate.BindDynamic(InListener, &UCk_Test_RuntimeMeshListener_UE::OnRequestCompleted);
        return Delegate;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicCut,
    "Ck.RuntimeMesh.Public.Cut",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicCut::RunTest(const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked CPU fixture becomes Ready"), Fixture.Initialize()))
    { return false; }
    const auto SourceVolume = UCk_Utils_RuntimeMesh_UE::Get_Metrics(
        Fixture.Source).Get_VolumeCm3();
    auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    Listener->Source = Fixture.Source;
    Listener->ResultOwner = Fixture.ResultOwner;
    auto Request = MakeRequest(Fixture, FVector{5, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(
        Fixture.Source, Request, Receiver(Listener.Get()), Completion(Listener.Get()));
    TestEqual(TEXT("submission is deferred"), Listener->Results.Num(), 0);
    Fixture.Drain();
    if (NOT TestEqual(TEXT("one typed terminal"), Listener->Results.Num(), 1))
    { return false; }
    const auto& Result = Listener->Results[0];
    TestEqual(TEXT("success"), Result.Get_Outcome(), ECk_RuntimeMesh_SliceOutcome::Succeeded);
    TestTrue(TEXT("both Ready"), UCk_Utils_RuntimeMesh_UE::Get_SetupState(
        Result.Get_Positive()) == ECk_RuntimeMesh_SetupState::Ready
        && UCk_Utils_RuntimeMesh_UE::Get_SetupState(
            Result.Get_Negative()) == ECk_RuntimeMesh_SetupState::Ready);
    TestTrue(TEXT("each half is 500 cm3"), FMath::Abs(
        Result.Get_PositiveMetrics().Get_VolumeCm3() - 500.0) <= 0.01
        && FMath::Abs(Result.Get_NegativeMetrics().Get_VolumeCm3() - 500.0) <= 0.01);
    TestEqual(TEXT("source unchanged"), UCk_Utils_RuntimeMesh_UE::Get_Metrics(
        Fixture.Source).Get_VolumeCm3(), SourceVolume);
    TestEqual(TEXT("generic completion once"), Listener->Completions.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicReentrantAndCleanup,
    "Ck.RuntimeMesh.Public.ReentrantAndCleanup",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicReentrantAndCleanup::RunTest(const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked CPU fixture becomes Ready"), Fixture.Initialize()))
    { return false; }
    auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    Listener->Source = Fixture.Source;
    Listener->ResultOwner = Fixture.ResultOwner;
    Listener->EnqueueOnFirst = true;
    Listener->ReentrantOperationID = FGuid::NewGuid();
    auto Request = MakeRequest(Fixture, FVector{30, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(
        Fixture.Source, Request, Receiver(Listener.Get()), Completion(Listener.Get()));
    Fixture.Drain();
    TestEqual(TEXT("callback request waits next main pass"), Listener->Results.Num(), 1);
    TestEqual(TEXT("typed then generic"), Listener->CallbackOrder,
        TArray<FName>{TEXT("typed"), TEXT("generic")});
    ck::runtimemesh::CancelWorld(Fixture.World);
    TestEqual(TEXT("later request cancelled once"), Listener->Results.Num(), 2);
    TestEqual(TEXT("later generic cancelled once"), Listener->Completions.Num(), 2);
    TestEqual(TEXT("cancel category"), Listener->Results[1].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::FailedCancelled);
    Fixture.Drain();
    TestEqual(TEXT("no redelivery"), Listener->Results.Num(), 2);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicDistinctReceiversAndBudget,
    "Ck.RuntimeMesh.Public.DistinctReceiversAndBudget",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicDistinctReceiversAndBudget::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    static_assert(ck::FProcessor_RuntimeMesh_Drain::PumpPolicy
        == ECk_ProcessorPumpPolicy::SkipPump);
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
    { return false; }
    auto First = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto Second = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto FirstRequest = MakeRequest(Fixture, FVector{30, 0, 0});
    auto SecondRequest = MakeRequest(Fixture, FVector{30, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, FirstRequest,
        Receiver(First.Get()), Completion(First.Get()));
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, SecondRequest,
        Receiver(Second.Get()), Completion(Second.Get()));
    TestEqual(TEXT("neither receiver runs on submission"), First->Results.Num(), 0);
    TestEqual(TEXT("second receiver also waits"), Second->Results.Num(), 0);
    Fixture.Drain();
    TestEqual(TEXT("first receiver exactly once"), First->Results.Num(), 1);
    TestEqual(TEXT("second receiver exactly once"), Second->Results.Num(), 1);
    TestEqual(TEXT("first correlation"), First->Results[0].Get_OperationID(),
        FirstRequest.Get_OperationID());
    TestEqual(TEXT("second correlation"), Second->Results[0].Get_OperationID(),
        SecondRequest.Get_OperationID());
    TestEqual(TEXT("first completion"), First->Completions.Num(), 1);
    TestEqual(TEXT("second completion"), Second->Completions.Num(), 1);

    auto Pending = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    for (auto Index = 0; Index < ck::runtimemesh::QueueCapacity; ++Index)
    {
        auto Request = MakeRequest(Fixture, FVector{30, 0, 0});
        UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
            Receiver(Pending.Get()), Completion(Pending.Get()));
    }
    TestEqual(TEXT("a full queue stays deferred"), Pending->Results.Num(), 0);
    Fixture.Drain();
    TestEqual(TEXT("a main pass drains at most the per-tick budget"), Pending->Results.Num(),
        ck::runtimemesh::DrainBudgetPerTick);
    TestEqual(TEXT("completion count matches budget"), Pending->Completions.Num(),
        ck::runtimemesh::DrainBudgetPerTick);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicRejectionAndCancellation,
    "Ck.RuntimeMesh.Public.RejectionAndCancellation",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicRejectionAndCancellation::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
    { return false; }
    auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto Request = MakeRequest(Fixture, FVector{30, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    AddExpectedError(TEXT("operation ID already in flight"),
        EAutomationExpectedErrorFlags::Contains, 0);
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    TestEqual(TEXT("duplicate rejected synchronously"), Listener->Results.Num(), 1);
    TestEqual(TEXT("duplicate has typed reason"),
        Listener->Results[0].Get_Outcome(), ECk_RuntimeMesh_SliceOutcome::InvalidRequest);
    TestEqual(TEXT("duplicate generic rejection"),
        Listener->Completions[0], ECk_Request_OperationResult::Failed_NotEnqueued);
    Fixture.Drain();
    TestEqual(TEXT("original request still executes"), Listener->Results.Num(), 2);
    TestEqual(TEXT("original completion arrives once"), Listener->Completions.Num(), 2);

    auto OwnerToDestroy = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
        Fixture.WorldEntity);
    auto Cancelled = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto CancelRequest = MakeRequest(Fixture, FVector{5, 0, 0});
    CancelRequest.Set_ResultOwner(OwnerToDestroy);
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, CancelRequest,
        Receiver(Cancelled.Get()), Completion(Cancelled.Get()));
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(OwnerToDestroy);
    Fixture.Drain();
    TestEqual(TEXT("destroyed result owner cancels"), Cancelled->Results.Num(), 1);
    TestEqual(TEXT("no partial result after owner destruction"),
        Cancelled->Results[0].Get_Outcome(), ECk_RuntimeMesh_SliceOutcome::FailedCancelled);
    TestEqual(TEXT("cancel generic"), Cancelled->Completions[0],
        ECk_Request_OperationResult::Failed_Cancelled);

    auto SourceCancelled = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto SourceRequest = MakeRequest(Fixture, FVector{5, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, SourceRequest,
        Receiver(SourceCancelled.Get()), Completion(SourceCancelled.Get()));
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Fixture.Source);
    Fixture.Drain();
    TestEqual(TEXT("BeginDestroy source cancels before EndPlay"),
        SourceCancelled->Results.Num(), 1);
    TestEqual(TEXT("source cancellation typed reason"),
        SourceCancelled->Results[0].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::FailedCancelled);
    TestEqual(TEXT("source cancellation generic reason"),
        SourceCancelled->Completions[0],
        ECk_Request_OperationResult::Failed_Cancelled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicDeadTypedReceiver,
    "Ck.RuntimeMesh.Public.DeadTypedReceiver",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicDeadTypedReceiver::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
    { return false; }
    auto Generic = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto* TypedObject = NewObject<UCk_Test_RuntimeMeshListener_UE>(
        GetTransientPackage());
    auto WeakTyped = TWeakObjectPtr<UCk_Test_RuntimeMeshListener_UE>{TypedObject};
    auto Request = MakeRequest(Fixture, FVector{5, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
        Receiver(TypedObject), Completion(Generic.Get()));
    TypedObject = nullptr;
    CollectGarbage(RF_NoFlags);
    if (NOT TestFalse(TEXT("unrooted typed receiver was collected"), WeakTyped.IsValid()))
    { return false; }
    Fixture.Drain();
    TestEqual(TEXT("surviving generic receiver completes once"), Generic->Completions.Num(), 1);
    TestEqual(TEXT("dead typed receiver cancels"), Generic->Completions[0],
        ECk_Request_OperationResult::Failed_Cancelled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicSynchronousRejections,
    "Ck.RuntimeMesh.Public.SynchronousRejections",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicSynchronousRejections::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
    { return false; }
    auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto Spec = FCk_RuntimeMesh_Spec{};
    Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{Fixture.Asset.Get()});
    auto PendingEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
        Fixture.WorldEntity);
    auto Pending = UCk_Utils_RuntimeMesh_UE::Add(PendingEntity, Spec);
    if (NOT TestTrue(TEXT("second source is pending"), ck::IsValid(Pending)))
    { return false; }
    auto Request = MakeRequest(Fixture, FVector{5, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Pending, Request,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    TestEqual(TEXT("NotReady is synchronous"), Listener->Results.Num(), 1);
    TestEqual(TEXT("typed NotReady"), Listener->Results[0].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::NotReady);
    TestEqual(TEXT("generic NotEnqueued"), Listener->Completions[0],
        ECk_Request_OperationResult::Failed_NotEnqueued);

    // Both the malformed plane and the self-owned result owner below trip the one request-validity ensure.
    AddExpectedError(TEXT("rejected invalid request/owner/receiver"),
        EAutomationExpectedErrorFlags::Contains, 0);
    auto Invalid = MakeRequest(Fixture, FVector{5, 0, 0});
    auto Plane = Invalid.Get_Plane();
    Plane.Set_Normal(FVector::ZeroVector);
    Invalid.Set_Plane(Plane);
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Invalid,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    TestEqual(TEXT("malformed plane is synchronous"), Listener->Results.Num(), 2);
    TestEqual(TEXT("malformed typed reason"), Listener->Results[1].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::InvalidRequest);
    TestEqual(TEXT("malformed generic result"), Listener->Completions[1],
        ECk_Request_OperationResult::Failed_NotEnqueued);

    for (auto Index = 0; Index < ck::runtimemesh::QueueCapacity; ++Index)
    {
        auto Queued = MakeRequest(Fixture, FVector{30, 0, 0});
        UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Queued,
            Receiver(Listener.Get()), Completion(Listener.Get()));
    }
    auto Overflow = MakeRequest(Fixture, FVector{30, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Overflow,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    TestEqual(TEXT("queue saturation rejects synchronously"), Listener->Results.Num(), 3);
    TestEqual(TEXT("typed saturation reason"), Listener->Results[2].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::RejectedLimit);
    TestEqual(TEXT("generic saturation result"), Listener->Completions[2],
        ECk_Request_OperationResult::Failed_NotEnqueued);
    auto SelfOwned = MakeRequest(Fixture, FVector{5, 0, 0});
    SelfOwned.Set_ResultOwner(Fixture.Source.ConvertToHandle());
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, SelfOwned,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    TestEqual(TEXT("source cannot own its own derived outputs"),
        Listener->Results[3].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::InvalidRequest);
    ck::runtimemesh::CancelWorld(Fixture.World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicSourceEndPlayCancellation,
    "Ck.RuntimeMesh.Public.SourceEndPlayCancellation",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicSourceEndPlayCancellation::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
    { return false; }
    auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto Request = MakeRequest(Fixture, FVector{5, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    const auto& Current = Fixture.Source.Get<ck::FFragment_RuntimeMesh>();
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Fixture.Source);
    ck::FProcessor_RuntimeMesh_SourceEndPlay::ForEachEntity(
        FCk_Time{}, Fixture.Source, Current);
    TestEqual(TEXT("source EndPlay cancels once"), Listener->Results.Num(), 1);
    TestEqual(TEXT("typed cancellation"), Listener->Results[0].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::FailedCancelled);
    TestEqual(TEXT("generic cancellation"), Listener->Completions[0],
        ECk_Request_OperationResult::Failed_Cancelled);
    Fixture.Drain();
    TestEqual(TEXT("cancelled request never executes"), Listener->Results.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicCallbackTeardownLatch,
    "Ck.RuntimeMesh.Public.CallbackTeardownLatch",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicCallbackTeardownLatch::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    for (auto Mode = 0; Mode < 2; ++Mode)
    {
        auto Fixture = FFixture{};
        if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
        { return false; }
        auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
            NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
        Listener->Source = Fixture.Source;
        Listener->ResultOwner = Fixture.ResultOwner;
        Listener->DestroySourceOnFirst = Mode == 0;
        Listener->DestroyOwnerOnFirst = Mode == 1;
        auto Request = MakeRequest(Fixture, FVector{5, 0, 0});
        UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
            Receiver(Listener.Get()), Completion(Listener.Get()));
        Fixture.Drain();
        if (NOT TestEqual(TEXT("typed result committed"), Listener->Results.Num(), 1))
        { return false; }
        TestEqual(TEXT("typed success"), Listener->Results[0].Get_Outcome(),
            ECk_RuntimeMesh_SliceOutcome::Succeeded);
        TestEqual(TEXT("generic is still latched success"), Listener->Completions[0],
            ECk_Request_OperationResult::Succeeded);
        TestEqual(TEXT("callback order"), Listener->CallbackOrder,
            TArray<FName>{TEXT("typed"), TEXT("generic")});
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicGenericReceiverDiesInTypedCallback,
    "Ck.RuntimeMesh.Public.GenericReceiverDiesInTypedCallback",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicGenericReceiverDiesInTypedCallback::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
    { return false; }
    auto Typed = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto Generic = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    Typed->DestroyGenericReceiverOnFirst = true;
    Typed->GenericReceiverToDestroy = Generic.Get();
    auto Request = MakeRequest(Fixture, FVector{30, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
        Receiver(Typed.Get()), Completion(Generic.Get()));
    Fixture.Drain();
    TestEqual(TEXT("typed completed"), Typed->Results.Num(), 1);
    TestEqual(TEXT("dead generic was not invoked"), Generic->Completions.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PublicQueuedGCAndIndependentRecut,
    "Ck.RuntimeMesh.Public.QueuedGCAndIndependentRecut",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PublicQueuedGCAndIndependentRecut::RunTest(
    const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_public;
    auto Fixture = FFixture{};
    if (NOT TestTrue(TEXT("cooked fixture imports"), Fixture.Initialize()))
    { return false; }
    TestFalse(TEXT("Ready source releases setup batch"),
        Fixture.Source.Has<ck::FFragment_RuntimeMesh_PendingImport>());
    Fixture.Asset.Reset();
    auto Listener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto Request = MakeRequest(Fixture, FVector{5, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(Fixture.Source, Request,
        Receiver(Listener.Get()), Completion(Listener.Get()));
    CollectGarbage(RF_NoFlags);
    TestEqual(TEXT("GC did not run queued operation"), Listener->Results.Num(), 0);
    Fixture.Drain();
    if (NOT TestEqual(TEXT("queued operation resolves after GC"), Listener->Results.Num(), 1))
    { return false; }
    const auto Positive = Listener->Results[0].Get_Positive();
    TestTrue(TEXT("derived result under independent owner"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Positive) == Fixture.ResultOwner);
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Fixture.Source);
    CollectGarbage(RF_NoFlags);
    TestEqual(TEXT("derived result survives source teardown"),
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Positive),
        ECk_RuntimeMesh_SetupState::Ready);
    TestTrue(TEXT("derived volume survives source teardown"),
        FMath::Abs(UCk_Utils_RuntimeMesh_UE::Get_Metrics(
            Positive).Get_VolumeCm3() - 500.0) <= 0.01);

    auto RecutSource = Positive;
    auto RecutListener = TStrongObjectPtr<UCk_Test_RuntimeMeshListener_UE>{
        NewObject<UCk_Test_RuntimeMeshListener_UE>(GetTransientPackage())};
    auto Recut = MakeRequest(Fixture, FVector{7.5, 0, 0});
    UCk_Utils_RuntimeMesh_UE::Request_Slice(RecutSource, Recut,
        Receiver(RecutListener.Get()), Completion(RecutListener.Get()));
    Fixture.Drain();
    TestEqual(TEXT("derived geometry can be cut again"), RecutListener->Results.Num(), 1);
    TestEqual(TEXT("recut succeeds"), RecutListener->Results[0].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::Succeeded);
    return true;
}
#endif
