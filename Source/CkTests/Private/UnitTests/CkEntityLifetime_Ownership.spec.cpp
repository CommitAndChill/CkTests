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

#if WITH_DEV_AUTOMATION_TESTS
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_NetworkQueries_OwnerlessTerminal,
    "Ck.EntityLifetime.NetworkQueries.OwnerlessTerminal", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_NetworkQueries_OwnerlessTerminal::
    RunTest(const FString&)
    -> bool
{
    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    const auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    const auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    TestFalse(TEXT("the production transient root has no lifetime owner"), Root.Has<ck::FFragment_LifetimeOwner>());
    TestFalse(TEXT("the private registry needs no UObject world for these queries"), Root.Has<TWeakObjectPtr<UWorld>>());
    for (const auto& Entity : TArray<FCk_Handle>{Root, Owner, Child})
    {
        TestEqual(TEXT("ownerless no-net chain has no role"),
            UCk_Utils_EntityLifetime_UE::Get_EntityNetRole(Entity), ECk_Net_EntityNetRole::None);
        TestEqual(TEXT("ownerless no-net chain has unknown mode"),
            UCk_Utils_EntityLifetime_UE::Get_EntityNetMode(Entity), ECk_Net_NetModeType::Unknown);
        TestEqual(TEXT("the public net role query preserves the terminal result"),
            UCk_Utils_Net_UE::Get_EntityNetRole(Entity), ECk_Net_EntityNetRole::None);
        TestEqual(TEXT("the public net mode query preserves the terminal result"),
            UCk_Utils_Net_UE::Get_EntityNetMode(Entity), ECk_Net_NetModeType::Unknown);
        TestFalse(TEXT("ownerless no-net chain grants no authority"), UCk_Utils_Net_UE::Get_HasAuthority(Entity));
        TestFalse(TEXT("network observation does not add params"), Entity.Has<ck::FFragment_Net_Params>());
    }
    TestFalse(TEXT("network observation does not give the root an owner"), Root.Has<ck::FFragment_LifetimeOwner>());
    TestTrue(TEXT("network observation preserves the accepted child owner"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_NetworkQueries_ExplicitAndInheritedPriority,
    "Ck.EntityLifetime.NetworkQueries.ExplicitAndInheritedPriority", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_NetworkQueries_ExplicitAndInheritedPriority::
    RunTest(const FString&)
    -> bool
{
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    const auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    UCk_Utils_Net_UE::Add(Root, FCk_Net_ConnectionSettings{
        ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::Host, ECk_Net_EntityNetRole::Authority});
    TestFalse(TEXT("the child was created before explicit net params and must resolve inheritance"),
        Child.Has<ck::FFragment_Net_Params>());
    TestEqual(TEXT("descendants inherit the nearest explicit role"),
        UCk_Utils_Net_UE::Get_EntityNetRole(Child), ECk_Net_EntityNetRole::Authority);
    TestEqual(TEXT("descendants inherit the nearest explicit mode"),
        UCk_Utils_Net_UE::Get_EntityNetMode(Child), ECk_Net_NetModeType::Host);
    TestTrue(TEXT("descendants inherit the explicit authority tag"), UCk_Utils_Net_UE::Get_HasAuthority(Child));

    UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{
        ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy});
    TestEqual(TEXT("nearer explicit role wins over a different ancestor"),
        UCk_Utils_Net_UE::Get_EntityNetRole(Child), ECk_Net_EntityNetRole::Proxy);
    TestEqual(TEXT("nearer explicit mode wins over a different ancestor"),
        UCk_Utils_Net_UE::Get_EntityNetMode(Child), ECk_Net_NetModeType::Client);
    TestFalse(TEXT("nearer params without authority do not fall through to an authoritative ancestor"),
        UCk_Utils_Net_UE::Get_HasAuthority(Child));
    Owner.Add<ck::FTag_HasAuthority>();
    TestTrue(TEXT("the explicit authority tag remains authoritative independently of the role enum"),
        UCk_Utils_Net_UE::Get_HasAuthority(Child));

    Owner.Replace<ck::FFragment_LifetimeOwner>(FCk_Handle{});
    TestEqual(TEXT("local explicit role still precedes ownership traversal"),
        UCk_Utils_Net_UE::Get_EntityNetRole(Owner), ECk_Net_EntityNetRole::Proxy);
    TestEqual(TEXT("local explicit mode still precedes ownership traversal"),
        UCk_Utils_Net_UE::Get_EntityNetMode(Owner), ECk_Net_NetModeType::Client);
    TestTrue(TEXT("local explicit net/tag priority remains unchanged before malformed ancestry"),
        UCk_Utils_Net_UE::Get_HasAuthority(Owner));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_NetworkQueries_InvalidAndMalformedChains,
    "Ck.EntityLifetime.NetworkQueries.InvalidAndMalformedChains", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_NetworkQueries_InvalidAndMalformedChains::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_entity_lifetime_ownership_spec;
    AddExpectedError(TEXT("Lifetime ownership would contain an invalid entity or cycle"),
        EAutomationExpectedErrorFlags::Contains, -1);
    auto Stale = FCk_Handle{};
    {
        auto Expired = ck::FEcsWorld{};
        Stale = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(Expired.Get_Registry());
    }
    for (const auto& Invalid : TArray<FCk_Handle>{FCk_Handle{}, Stale})
    {
        TestEqual(TEXT("invalid or stale input has no role"),
            UCk_Utils_Net_UE::Get_EntityNetRole(Invalid), ECk_Net_EntityNetRole::None);
        TestEqual(TEXT("invalid or stale input has unknown mode"),
            UCk_Utils_Net_UE::Get_EntityNetMode(Invalid), ECk_Net_NetModeType::Unknown);
        TestFalse(TEXT("invalid or stale input grants no authority"), UCk_Utils_Net_UE::Get_HasAuthority(Invalid));
    }
    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    const auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    // Existing ownership fixtures use raw fragment replacement only to exercise malformed imported graphs.
    for (const auto& RejectedOwner : TArray<FCk_Handle>{FCk_Handle{}, Stale, Child})
    {
        Owner.Replace<ck::FFragment_LifetimeOwner>(RejectedOwner);
        TestEqual(TEXT("malformed, stale-owner or cyclic chain has no role"),
            UCk_Utils_Net_UE::Get_EntityNetRole(Child), ECk_Net_EntityNetRole::None);
        TestEqual(TEXT("malformed, stale-owner or cyclic chain has unknown mode"),
            UCk_Utils_Net_UE::Get_EntityNetMode(Child), ECk_Net_NetModeType::Unknown);
        TestFalse(TEXT("malformed, stale-owner or cyclic chain grants no authority"),
            UCk_Utils_Net_UE::Get_HasAuthority(Child));
        TestTrue(TEXT("rejection never rewrites the malformed forward edge"),
            Owner.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == RejectedOwner);
        TestTrue(TEXT("rejection never rewrites a child edge"),
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner);
        TestEqual(TEXT("rejection preserves the child reverse link"), Get_DependentCount(Owner, Child), 1);
        TestEqual(TEXT("rejection preserves the root reverse link"), Get_DependentCount(Root, Owner), 1);
        TestFalse(TEXT("rejection adds no net params"), Child.Has<ck::FFragment_Net_Params>());
        TestFalse(TEXT("rejection grants no authority tag"), Child.Has<ck::FTag_HasAuthority>());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_NetworkQueries_PendingOwnerBlocksAncestor,
    "Ck.EntityLifetime.NetworkQueries.PendingOwnerBlocksAncestor", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_NetworkQueries_PendingOwnerBlocksAncestor::
    RunTest(const FString&)
    -> bool
{
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    const auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner, {});
    UCk_Utils_Net_UE::Add(Root, FCk_Net_ConnectionSettings{
        ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::Host, ECk_Net_EntityNetRole::Authority});
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner);
    ck::FProcessor_EntityLifetime_DestructionPhase_Endplay::ForEachEntity(FCk_Time{}, Owner);
    ck::FProcessor_EntityLifetime_DestructionPhase_Teardown::ForEachEntity(FCk_Time{}, Owner);
    if (NOT TestTrue(TEXT("the child remains default-valid while only its owner reached teardown"),
            ck::IsValid(Child) && NOT ck::IsValid(Owner)
            && ck::IsValid(Owner, ck::IsValid_Policy_IncludePendingKill{})))
    { return false; }
    TestTrue(TEXT("the further ancestor is still default-valid and explicitly authoritative"),
        ck::IsValid(Root) && UCk_Utils_Net_UE::Get_HasAuthority(Root));
    TestEqual(TEXT("an ending owner blocks role inheritance without an ensure"),
        UCk_Utils_Net_UE::Get_EntityNetRole(Child), ECk_Net_EntityNetRole::None);
    TestEqual(TEXT("an ending owner blocks mode inheritance without an ensure"),
        UCk_Utils_Net_UE::Get_EntityNetMode(Child), ECk_Net_NetModeType::Unknown);
    TestFalse(TEXT("authority does not skip an ending owner to reach a live authoritative grandparent"),
        UCk_Utils_Net_UE::Get_HasAuthority(Child));
    TestTrue(TEXT("the child forward edge is unchanged"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Owner);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_NetworkQueries_StrictOwnerQueryStillRejectsRoot,
    "Ck.EntityLifetime.NetworkQueries.StrictOwnerQueryStillRejectsRoot", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_NetworkQueries_StrictOwnerQueryStillRejectsRoot::
    RunTest(const FString&)
    -> bool
{
    AddExpectedError(TEXT("does NOT have a LifetimeOwner"), EAutomationExpectedErrorFlags::Contains, -1);
    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    TestFalse(TEXT("the strict owner-required operation still rejects a legitimate ownerless terminal"),
        ck::IsValid(UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Root)));
    TestFalse(TEXT("strict rejection does not invent an owner"), Root.Has<ck::FFragment_LifetimeOwner>());
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "CkCore/Ensure/CkEnsure_Utils.h"
#include "CkVoiceChat/Net/CkVoiceChat_RepData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_ContainerTryAdd_LocalOnlyNoMutation,
    "Ck.EntityLifetime.ContainerTryAdd.LocalOnlyNoMutation", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_ContainerTryAdd_LocalOnlyNoMutation::
    RunTest(const FString&)
    -> bool
{
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    const auto Before = UCk_Utils_Ensure_UE::Get_EnsureCount();
    for (auto* Handle : {&Root, &Child})
    {
        TestFalse(TEXT("local-only setup has no explicit network params"), UCk_Utils_Net_UE::Has(*Handle));
        TestEqual(TEXT("optional container creation declines local-only root and child"),
            UCk_Utils_Net_UE::TryAddContainerFragment<FCk_RepData_VoiceChat>(*Handle), ECk_AddedOrNot::NotAdded);
        TestFalse(TEXT("decline does not invent network params"), UCk_Utils_Net_UE::Has(*Handle));
        TestFalse(TEXT("decline publishes no container entry"),
            Handle->Has<ck::TFragment_ContainerEntryRef<FCk_RepData_VoiceChat>>());
    }
    TestTrue(TEXT("root remains ownerless"), NOT Root.Has<ck::FFragment_LifetimeOwner>());
    TestTrue(TEXT("child keeps its actual lifetime owner"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Root);
    TestEqual(TEXT("decline preserves reverse dependency cardinality"),
        ck_entity_lifetime_ownership_spec::Get_DependentCount(Root, Child), 1);
    TestEqual(TEXT("local-only optional decline raises no ensure"), UCk_Utils_Ensure_UE::Get_EnsureCount(), Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_ContainerTryAdd_DefaultAndStale,
    "Ck.EntityLifetime.ContainerTryAdd.DefaultAndStale", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_ContainerTryAdd_DefaultAndStale::
    RunTest(const FString&)
    -> bool
{
    auto Default = FCk_Handle{};
    auto Stale = FCk_Handle{};
    {
        auto ExpiredWorld = ck::FEcsWorld{};
        Stale = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(ExpiredWorld.Get_Registry());
    }
    const auto Before = UCk_Utils_Ensure_UE::Get_EnsureCount();
    for (auto* Handle : {&Default, &Stale})
    {
        if (NOT TestFalse(TEXT("rejected input is actually invalid"), ck::IsValid(*Handle)))
        { return false; }
        TestEqual(TEXT("optional container creation rejects invalid handles before fragment reads"),
            UCk_Utils_Net_UE::TryAddContainerFragment<FCk_RepData_VoiceChat>(*Handle), ECk_AddedOrNot::NotAdded);
        TestFalse(TEXT("rejection does not revive the input"), ck::IsValid(*Handle));
    }
    TestEqual(TEXT("default and stale rejection raise no ensure"), UCk_Utils_Ensure_UE::Get_EnsureCount(), Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityLifetime_ContainerTryAdd_ExplicitNetPrerequisites,
    "Ck.EntityLifetime.ContainerTryAdd.ExplicitNetPrerequisites", ck_entity_lifetime_ownership_spec::TestFlags)

auto
    FCk_EntityLifetime_ContainerTryAdd_ExplicitNetPrerequisites::
    RunTest(const FString&)
    -> bool
{
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Local = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto Client = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    UCk_Utils_Net_UE::Add(Local, FCk_Net_ConnectionSettings{
        ECk_Replication::DoesNotReplicate, ECk_Net_NetModeType::Host, ECk_Net_EntityNetRole::Authority});
    UCk_Utils_Net_UE::Add(Host, FCk_Net_ConnectionSettings{
        ECk_Replication::Replicates, ECk_Net_NetModeType::Host, ECk_Net_EntityNetRole::Authority});
    UCk_Utils_Net_UE::Add(Client, FCk_Net_ConnectionSettings{
        ECk_Replication::Replicates, ECk_Net_NetModeType::Client, ECk_Net_EntityNetRole::Proxy});
    const auto Before = UCk_Utils_Ensure_UE::Get_EnsureCount();
    for (auto* Handle : {&Local, &Host, &Client})
    {
        TestTrue(TEXT("explicit network params remain present"), UCk_Utils_Net_UE::Has(*Handle));
        TestFalse(TEXT("pure fixture has no replication driver"),
            Handle->Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>());
        TestEqual(TEXT("existing local, host-without-driver and client declines remain unchanged"),
            UCk_Utils_Net_UE::TryAddContainerFragment<FCk_RepData_VoiceChat>(*Handle), ECk_AddedOrNot::NotAdded);
        TestFalse(TEXT("no partial container entry is published"),
            Handle->Has<ck::TFragment_ContainerEntryRef<FCk_RepData_VoiceChat>>());
    }
    TestEqual(TEXT("existing explicit host role is preserved"),
        UCk_Utils_Net_UE::Get_EntityNetRole(Host), ECk_Net_EntityNetRole::Authority);
    TestEqual(TEXT("existing explicit client mode is preserved"),
        UCk_Utils_Net_UE::Get_EntityNetMode(Client), ECk_Net_NetModeType::Client);
    TestEqual(TEXT("existing guarded declines raise no ensure"), UCk_Utils_Ensure_UE::Get_EnsureCount(), Before);
    return true;
}
#endif
