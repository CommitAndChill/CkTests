// An ordered EntityScript spawn whose script object cannot be created leaves its entity as it found it.
//
// Each test exhausts the bounded pool of UCk_AutoTest_EntityScript_OrderedBoundedPool_UE with two live holders, then
// requests that script on a fresh entity A. The spawn cannot create its script object and ensures by design ("Failed to
// Spawn New Entity using EntityScript", expected explicitly below and counted with the ensure tracker). Then:
//
// F1 AFailedSpawnLeavesNoOrderingOnItsEntity - A is alive, runs no script and carries no ordering sequence.
// F2 ARetryRunsEveryStepInRequestOrder - the holders are destroyed (their instances return to the pool), B is
//    requested and then A again, both in the default group, in one frame. No ensure fires (a stale sequence on A made
//    the retry's Add trip the duplicate-fragment ensure), and Construct, OnConstructed and BeginPlay all run B then A
//    (a stale sequence on A ran OnConstructed and BeginPlay A first, while Construct sorted by the requests: B first).
// F3 AnUnorderedRetryIsNotOrdered - A is retried with an unordered script. It carries no ordering sequence at
//    Construct or BeginPlay, so the ordered finish and BeginPlay steps never pick it up.
//
// Surface in Session Frontend: Ck.EntityScript.OrderedSpawnFailure.*

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkAutoTest_EntityScript_SpawnFailureFixtures.h"

#include "CkCore/Ensure/CkEnsure_Utils.h"
#include "CkCore/Format/CkFormat.h"
#include "CkCore/Object/CkObject_Utils.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"

#include <StructUtils/InstancedStruct.h>
#include <tuple>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_entity_script_ordered_spawn_failure
{
    using namespace ck::auto_test::entity_script_spawn_failure;

    // The listen server's own world; nothing here needs a remote client.
    constexpr auto NumPIEClients = 1;
    constexpr auto PIEReadyTimeoutSeconds = 30.0f;
    constexpr auto StepTimeoutSeconds = 10.0;
    constexpr auto FramesAfterFailure = 5;

    constexpr auto FailedSpawnError = TEXT("Failed to Spawn New Entity using EntityScript");

    struct FState
    {
        FCk_Handle _HolderA;
        FCk_Handle _HolderB;
        FCk_Handle _A;
        FCk_Handle _B;
        int32 _EnsuresBeforeFailure = 0;
        int32 _EnsuresAfterFailure = 0;
        int32 _EnsuresBeforeRetry = 0;
    };

    struct FConstructedListener
    {
        auto
        OnConstructed(
            FCk_Handle_EntityScript InHandle)
            -> void
        { Request_Record(EStep::OnConstructed, FCk_Handle{InHandle}); }
    };

    auto
        Get_Listener()
        -> FConstructedListener&
    {
        static auto Listener = FConstructedListener{};
        return Listener;
    }

    auto
        Get_OrderedClass()
        -> TSubclassOf<UCk_EntityScript_UE>
    { return UCk_AutoTest_EntityScript_OrderedBoundedPool_UE::StaticClass(); }

    auto
        Get_HasBegunPlay(
            const FCk_Handle& InEntity)
        -> bool
    {
        return Get_Records().ContainsByPredicate([&](const FStepRecord& InRecord) -> bool
        {
            return InRecord._Step == EStep::BeginPlay && InRecord._Entity == InEntity;
        });
    }

    auto
        Find_Record(
            EStep InStep,
            const FCk_Handle& InEntity)
        -> const FStepRecord*
    {
        return Get_Records().FindByPredicate([&](const FStepRecord& InRecord) -> bool
        {
            return InRecord._Step == InStep && InRecord._Entity == InEntity;
        });
    }

    // The order InStep ran A and B in, as "B,A".
    auto
        Format_Order(
            EStep InStep,
            const FState& InState)
        -> FString
    {
        auto Labels = TArray<FString>{};
        for (const auto& Record : Get_Records())
        {
            if (Record._Step != InStep)
            { continue; }

            if (Record._Entity == InState._A)
            { Labels.Add(TEXT("A")); }
            else if (Record._Entity == InState._B)
            { Labels.Add(TEXT("B")); }
        }
        return FString::Join(Labels, TEXT(","));
    }

    auto
        Do_StartPIE(
            FAutomationTestBase& InTest)
        -> void
    {
        Get_Records().Reset();

        // The failure path ensures by design; every occurrence is tolerated (an ensure whose line an earlier test
        // already printed is not printed again) and the tests count the fires with the ensure tracker instead.
        InTest.AddExpectedError(FailedSpawnError, EAutomationExpectedErrorFlags::Contains, -1);

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, FString{TEXT("/Engine/Maps/Entry")}));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(NumPIEClients, PIEReadyTimeoutSeconds));
    }

    auto
        Do_EndPIE()
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }

    // Two holders take the pool's two slots; then an ordered request on A cannot create its script object.
    auto
        Do_ExhaustThePoolAndFailA(
            FAutomationTestBase& InTest,
            const TSharedRef<FState>& InState)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&InTest, InState](UWorld* InServer) -> void
            {
                auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);
                InState->_HolderA = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
                InState->_HolderB = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
                InState->_A = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
                InState->_B = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);

                std::ignore = UCk_Utils_EntityScript_UE::Add(InState->_HolderA, Get_OrderedClass(), FInstancedStruct{});
                std::ignore = UCk_Utils_EntityScript_UE::Add(InState->_HolderB, Get_OrderedClass(), FInstancedStruct{});
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InState]() -> bool
            {
                return Get_HasBegunPlay(InState->_HolderA) && Get_HasBegunPlay(InState->_HolderB);
            }),
            StepTimeoutSeconds,
            TEXT("the two holders take the pool's two slots and begin play")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InState](UWorld*) -> void
            {
                InState->_EnsuresBeforeFailure = UCk_Utils_Ensure_UE::Get_EnsureCount();
                std::ignore = UCk_Utils_EntityScript_UE::Add(InState->_A, Get_OrderedClass(), FInstancedStruct{});
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterFailure));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([&InTest, InState]() -> bool
            {
                InState->_EnsuresAfterFailure = UCk_Utils_Ensure_UE::Get_EnsureCount();

                InTest.TestEqual(TEXT("the spawn that could not create its script object ensured once"),
                    InState->_EnsuresAfterFailure - InState->_EnsuresBeforeFailure, 1);
                InTest.TestTrue(TEXT("A survives the failed spawn"), ck::IsValid(InState->_A));
                InTest.TestFalse(TEXT("A runs no script after the failed spawn"),
                    ck::IsValid(InState->_A) && InState->_A.Has<ck::FFragment_EntityScript>());

                return true;
            }),
            TEXT("the ordered spawn on A fails")));
    }

    // Destroys the holders and waits until their instances are back in the pool.
    auto
        Do_FreeThePool(
            FAutomationTestBase& InTest,
            const TSharedRef<FState>& InState)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InState](UWorld*) -> void
            {
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(InState->_HolderA);
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(InState->_HolderB);
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&InTest,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                if (Server == nullptr)
                { return false; }

                const auto Stats = UCk_Utils_Object_UE::Get_ObjectPoolStats(Server, Get_OrderedClass(),
                    Get_OrderedClass()->GetDefaultObject());
                return Stats.Get_NumFree() >= UCk_AutoTest_EntityScript_OrderedBoundedPool_UE::PoolSize;
            }),
            StepTimeoutSeconds,
            TEXT("the holders' instances return to the pool")));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityScriptOrderedSpawnFailure_AFailedSpawnLeavesNoOrderingOnItsEntity,
    "Ck.EntityScript.OrderedSpawnFailure.AFailedSpawnLeavesNoOrderingOnItsEntity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptOrderedSpawnFailure_AFailedSpawnLeavesNoOrderingOnItsEntity::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_ordered_spawn_failure;

    const auto State = MakeShared<FState>();

    Do_StartPIE(*this);
    Do_ExhaustThePoolAndFailA(*this, State);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestFalse(TEXT("A carries no ordering sequence after its ordered spawn failed"),
                ck::IsValid(State->_A) && State->_A.Has<ck::FFragment_EntityScript_OrderingSequence>());
            return true;
        }),
        TEXT("a failed ordered spawn leaves no ordering sequence on its entity")));

    Do_EndPIE();

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityScriptOrderedSpawnFailure_ARetryRunsEveryStepInRequestOrder,
    "Ck.EntityScript.OrderedSpawnFailure.ARetryRunsEveryStepInRequestOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptOrderedSpawnFailure_ARetryRunsEveryStepInRequestOrder::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_ordered_spawn_failure;

    const auto State = MakeShared<FState>();

    Do_StartPIE(*this);
    Do_ExhaustThePoolAndFailA(*this, State);
    Do_FreeThePool(*this, State);

    // B first, then A's retry, in one frame: one spawn pass serves both, in request order.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld*) -> void
        {
            State->_EnsuresBeforeRetry = UCk_Utils_Ensure_UE::Get_EnsureCount();

            (void)ck::UUtils_Signal_OnConstructed::Bind<&FConstructedListener::OnConstructed>(&Get_Listener(), State->_B,
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
            (void)ck::UUtils_Signal_OnConstructed::Bind<&FConstructedListener::OnConstructed>(&Get_Listener(), State->_A,
                ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);

            std::ignore = UCk_Utils_EntityScript_UE::Add(State->_B, Get_OrderedClass(), FInstancedStruct{});
            std::ignore = UCk_Utils_EntityScript_UE::Add(State->_A, Get_OrderedClass(), FInstancedStruct{});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return Get_HasBegunPlay(State->_A) && Get_HasBegunPlay(State->_B);
        }),
        StepTimeoutSeconds,
        TEXT("B and the retried A begin play")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestEqual(TEXT("no ensure fired from the retry on (no duplicate ordering sequence on A)"),
                UCk_Utils_Ensure_UE::Get_EnsureCount() - State->_EnsuresBeforeRetry, 0);
            TestEqual(TEXT("Construct ran B then A"), Format_Order(EStep::Construct, *State), FString{TEXT("B,A")});
            TestEqual(TEXT("OnConstructed ran B then A"), Format_Order(EStep::OnConstructed, *State), FString{TEXT("B,A")});
            TestEqual(TEXT("BeginPlay ran B then A"), Format_Order(EStep::BeginPlay, *State), FString{TEXT("B,A")});

            return true;
        }),
        TEXT("a retry after a failed ordered spawn runs every step in request order")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld*) -> void
        {
            if (ck::IsValid(State->_B))
            { ck::UUtils_Signal_OnConstructed::Unbind<&FConstructedListener::OnConstructed>(&Get_Listener(), State->_B); }
            if (ck::IsValid(State->_A))
            { ck::UUtils_Signal_OnConstructed::Unbind<&FConstructedListener::OnConstructed>(&Get_Listener(), State->_A); }
        })));

    Do_EndPIE();

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityScriptOrderedSpawnFailure_AnUnorderedRetryIsNotOrdered,
    "Ck.EntityScript.OrderedSpawnFailure.AnUnorderedRetryIsNotOrdered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptOrderedSpawnFailure_AnUnorderedRetryIsNotOrdered::RunTest(const FString& Parameters)
{
    using namespace ck_entity_script_ordered_spawn_failure;

    const auto State = MakeShared<FState>();

    Do_StartPIE(*this);
    Do_ExhaustThePoolAndFailA(*this, State);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld*) -> void
        {
            State->_EnsuresBeforeRetry = UCk_Utils_Ensure_UE::Get_EnsureCount();
            std::ignore = UCk_Utils_EntityScript_UE::Add(State->_A,
                UCk_AutoTest_EntityScript_UnorderedRetry_UE::StaticClass(), FInstancedStruct{});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return Get_HasBegunPlay(State->_A);
        }),
        StepTimeoutSeconds,
        TEXT("the unordered retry on A begins play")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            const auto* Constructed = Find_Record(EStep::Construct, State->_A);
            const auto* Begun = Find_Record(EStep::BeginPlay, State->_A);

            TestEqual(TEXT("no ensure fired from the unordered retry on"),
                UCk_Utils_Ensure_UE::Get_EnsureCount() - State->_EnsuresBeforeRetry, 0);
            TestTrue(TEXT("the unordered retry constructed"), Constructed != nullptr);
            TestFalse(TEXT("A carried no ordering sequence at Construct"),
                Constructed != nullptr && Constructed->_HadOrderingSequence);
            TestFalse(TEXT("A carried no ordering sequence at BeginPlay (the ordered steps never saw it)"),
                Begun != nullptr && Begun->_HadOrderingSequence);
            TestFalse(TEXT("A carries no ordering sequence after BeginPlay"),
                ck::IsValid(State->_A) && State->_A.Has<ck::FFragment_EntityScript_OrderingSequence>());

            return true;
        }),
        TEXT("an unordered retry after a failed ordered spawn is not ordered")));

    Do_EndPIE();

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
