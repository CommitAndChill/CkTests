#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Processor.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include <Engine/World.h>
#include <Misc/AutomationTest.h>
#include <Misc/ScopeExit.h>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_entity_lifetime_ownership_spec
{
    inline constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
    Get_DependentCount(
        const FCk_Handle& InOwner,
        const FCk_Handle& InChild)
        -> int32
    {
        if (NOT InOwner.Has<ck::FFragment_LifetimeDependents>())
        { return 0; }

        return InOwner.Get<ck::FFragment_LifetimeDependents>().Get_Entities().FilterByPredicate(
            [&InChild](const FCk_Handle& InDependent) { return InDependent == InChild; }).Num();
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_Ownership_TransferIsAtomic,
    "Ck.EntityLifetime.TenetIntegrity.TransferIsAtomic", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_Ownership_TransferIsAtomic::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_entity_lifetime_ownership_spec;
    AddExpectedError(TEXT("Lifetime ownership would contain an invalid entity or cycle"),
        EAutomationExpectedErrorFlags::Contains, -1);

    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    auto Descendant = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Child, {});

    UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Child, Descendant);
    TestTrue(TEXT("rejected descendant ownership leaves the current forward link unchanged"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner);
    TestEqual(TEXT("rejection preserves the old owner's reverse link"), Get_DependentCount(Owner, Child), 1);
    TestEqual(TEXT("rejection adds no reverse link to the descendant"), Get_DependentCount(Descendant, Child), 0);

    // The pre-fix graph may now be cyclic. Inspect direct links only, never recurse over it.
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_Ownership_CrossRegistryTransfer,
    "Ck.EntityLifetime.TenetIntegrity.CrossRegistryTransfer", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_Ownership_CrossRegistryTransfer::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_entity_lifetime_ownership_spec;

    auto World = ck::FEcsWorld{};
    auto ForeignWorld = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    const auto ForeignRoot = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(ForeignWorld.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto ForeignOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(ForeignRoot, {});
    auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    const auto ForeignChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(ForeignOwner, {});

    UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Child, ForeignOwner);
    TestTrue(TEXT("cross-registry forward ownership is supported"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == ForeignOwner);
    TestEqual(TEXT("the old same-registry reverse link is removed"), Get_DependentCount(Owner, Child), 0);
    TestEqual(TEXT("foreign handles never enter a same-registry dependents array"),
        Get_DependentCount(ForeignOwner, Child), 0);
    TestEqual(TEXT("the foreign owner's local child is preserved"), Get_DependentCount(ForeignOwner, ForeignChild), 1);

    UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Child, Owner);
    UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Child, Owner);
    TestTrue(TEXT("transfer back restores the local forward link"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner);
    TestEqual(TEXT("idempotent transfer creates exactly one local reverse link"), Get_DependentCount(Owner, Child), 1);
    TestEqual(TEXT("transfer back preserves the foreign owner's local child"),
        Get_DependentCount(ForeignOwner, ForeignChild), 1);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_Ownership_InvalidTraversalSkipsCallback,
    "Ck.EntityLifetime.TenetIntegrity.InvalidTraversalSkipsCallback", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_Ownership_InvalidTraversalSkipsCallback::
    RunTest(const FString&)
    -> bool
{
    AddExpectedError(TEXT("Lifetime ownership would contain an invalid entity or cycle"),
        EAutomationExpectedErrorFlags::Contains, -1);

    auto CallbackCount = int32{0};
    const auto Found = UCk_Utils_EntityLifetime_UE::Get_EntityInOwnershipChain_If(FCk_Handle{},
        [&CallbackCount](const FCk_Handle&)
        {
            ++CallbackCount;
            return true;
        });

    TestFalse(TEXT("an invalid traversal has no result"), ck::IsValid(Found));
    TestEqual(TEXT("invalid input never reaches the caller's predicate"), CallbackCount, 0);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_Ownership_RejectedAdmission,
    "Ck.EntityLifetime.TenetIntegrity.RejectedAdmission", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_Ownership_RejectedAdmission::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_entity_lifetime_ownership_spec;
    AddExpectedError(TEXT("Lifetime ownership would contain an invalid entity or cycle"),
        EAutomationExpectedErrorFlags::Contains, -1);

    auto World = ck::FEcsWorld{};
    auto Registry = World.Get_Registry();
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(Registry);
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    auto Unowned = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
    auto StaleOwner = FCk_Handle{};
    {
        auto ExpiredWorld = ck::FEcsWorld{};
        StaleOwner = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(ExpiredWorld.Get_Registry());
    }

    for (const auto& RejectedOwner : TArray<FCk_Handle>{FCk_Handle{}, Child, StaleOwner})
    {
        UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Child, RejectedOwner);
        TestTrue(TEXT("invalid, self, and stale ownership preserve the accepted owner"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner);
        TestEqual(TEXT("rejected transfer preserves exactly one reverse link"), Get_DependentCount(Owner, Child), 1);
    }

    UCk_Utils_EntityLifetime_UE::Request_SetupEntityWithLifetimeOwner(Unowned, StaleOwner);
    TestFalse(TEXT("rejected setup publishes no ownership fragment"), Unowned.Has<ck::FFragment_LifetimeOwner>());

    // Simulate a corrupt imported graph at the real API boundary. Creation and traversal must reject
    // the whole chain before allocating a child or invoking caller code.
    Owner.Replace<ck::FFragment_LifetimeOwner>(Child);
    auto CallbackCount = int32{0};
    const auto Created = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner,
        [&CallbackCount](FCk_Handle&) { ++CallbackCount; });
    const auto Found = UCk_Utils_EntityLifetime_UE::Get_EntityInOwnershipChain_If(Child,
        [&CallbackCount](const FCk_Handle&) { ++CallbackCount; return true; });
    TestFalse(TEXT("a cyclic owner cannot admit a new child"), ck::IsValid(Created));
    TestFalse(TEXT("a cyclic chain cannot return an early predicate match"), ck::IsValid(Found));
    TestEqual(TEXT("rejected creation and traversal invoke no caller code"), CallbackCount, 0);
    TestEqual(TEXT("rejected creation preserves existing dependents"), Get_DependentCount(Owner, Child), 1);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_Ownership_TeardownOwner,
    "Ck.EntityLifetime.TenetIntegrity.TeardownOwner", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_Ownership_TeardownOwner::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_entity_lifetime_ownership_spec;
    constexpr auto InformEngineOfWorld = false;
    auto* GameWorld = UWorld::CreateWorld(EWorldType::Game, InformEngineOfWorld, TEXT("CkOwnershipTeardown"));
    if (NOT TestNotNull(TEXT("temporary game world is created"), GameWorld))
    { return false; }

    ON_SCOPE_EXIT { GameWorld->DestroyWorld(InformEngineOfWorld); };

    auto Registry = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_RegistryForWorld(*GameWorld);
    const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*GameWorld);
    if (NOT TestTrue(TEXT("game world exposes its ECS registry and transient entity"),
        ck::IsValid(Registry) && ck::IsValid(Root)))
    { return false; }
    auto DyingOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Unowned = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(DyingOwner);
    ck::FProcessor_EntityLifetime_DestructionPhase_Endplay::ForEachEntity(FCk_Time{}, DyingOwner);
    ck::FProcessor_EntityLifetime_DestructionPhase_Teardown::ForEachEntity(FCk_Time{}, DyingOwner);

    UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Child, DyingOwner);
    UCk_Utils_EntityLifetime_UE::Request_SetupEntityWithLifetimeOwner(Unowned, DyingOwner);
    for (const auto& Dependent : TArray<FCk_Handle>{Child, Unowned})
    {
        TestTrue(TEXT("teardown inheritance follows a fully published forward link"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Dependent, ECk_PendingKill_Policy::IncludePendingKill) == DyingOwner);
        TestEqual(TEXT("teardown inheritance follows a fully published reverse link"), Get_DependentCount(DyingOwner, Dependent), 1);
        TestTrue(TEXT("destroy initiation is inherited"), Dependent.Has<ck::FTag_DestroyEntity_Initiate>());
        TestTrue(TEXT("teardown is inherited"), Dependent.Has<ck::FTag_DestroyEntity_Teardown>());
        TestTrue(TEXT("world resolution follows a pending-kill owner"),
            UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(Dependent) == GameWorld);
    }
    TestEqual(TEXT("transfer removes the old reverse link before teardown"), Get_DependentCount(Root, Child), 0);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_Ownership_CallbackMutation,
    "Ck.EntityLifetime.TenetIntegrity.CallbackMutation", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_Ownership_CallbackMutation::
    RunTest(const FString&)
    -> bool
{
    AddExpectedError(TEXT("Lifetime ownership would contain an invalid entity or cycle"),
        EAutomationExpectedErrorFlags::Contains, -1);
    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto NewOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    const auto ReparentedMatch = UCk_Utils_EntityLifetime_UE::Get_EntityInOwnershipChain_If(Child,
        [&](const FCk_Handle& InCurrent)
        {
            if (InCurrent == Child)
            { UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Child, NewOwner); }
            return InCurrent == NewOwner;
        });
    TestTrue(TEXT("valid callback reparenting follows the updated chain"), ReparentedMatch == NewOwner);

    auto CallbackCount = int32{0};
    const auto DestroyedMatch = UCk_Utils_EntityLifetime_UE::Get_EntityInOwnershipChain_If(Child,
        [&](const FCk_Handle&)
        {
            ++CallbackCount;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Child);
            ck::FProcessor_EntityLifetime_DestructionPhase_Endplay::ForEachEntity(FCk_Time{}, Child);
            ck::FProcessor_EntityLifetime_DestructionPhase_Teardown::ForEachEntity(FCk_Time{}, Child);
            return true;
        });
    TestFalse(TEXT("a callback cannot return a match it destroyed"), ck::IsValid(DestroyedMatch));
    TestEqual(TEXT("destruction terminates traversal before another callback"), CallbackCount, 1);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_Ownership_CallbackRevisitIsRejected,
    "Ck.EntityLifetime.TenetIntegrity.CallbackRevisitIsRejected", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_Ownership_CallbackRevisitIsRejected::
    RunTest(const FString&)
    -> bool
{
    AddExpectedError(TEXT("Lifetime ownership would contain an invalid entity or cycle"),
        EAutomationExpectedErrorFlags::Contains, -1);
    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto First = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Second = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto CallbackCount = int32{0};
    const auto Found = UCk_Utils_EntityLifetime_UE::Get_EntityInOwnershipChain_If(First,
        [&](const FCk_Handle& InCurrent)
        {
            ++CallbackCount;
            // Bound the broken implementation: a third callback matches instead of hanging forever.
            if (CallbackCount >= 3)
            { return true; }

            if (InCurrent == First)
            {
                UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Second, Root);
                UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(First, Second);
            }
            else
            {
                UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(First, Root);
                UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Second, First);
            }
            return false;
        });

    TestFalse(TEXT("a repeated visit cannot return a match"), ck::IsValid(Found));
    TestEqual(TEXT("repeated visits are rejected before invoking the callback again"), CallbackCount, 2);
    return true;
}

#endif
