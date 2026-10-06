#include "Misc/AutomationTest.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Processor.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/World/CkEcsWorld.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_allocated_lifetime_census_test
{
    using Runtime = UCk_EntityScriptGym_Spawn_Runtime_UE;
    auto Allocated(const FCk_Handle& H) -> bool
    { return Runtime::Get_IsEntityAllocated(H); }

    // Real destruction phases with their actual production tag prerequisites; no tag injection.
    auto ToTeardown(FCk_Handle& H) -> bool
    {
        if (!ck::IsValid(H)) { return false; }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(H);
        if (!Allocated(H) || !H.Has<ck::FTag_DestroyEntity_Initiate>()) { return false; }
        if (!H.Has<ck::FTag_DestroyEntity_EndPlay>())
        { ck::FProcessor_EntityLifetime_DestructionPhase_Endplay::ForEachEntity(FCk_Time::ZeroSecond(), H); }
        if (!H.Has<ck::FTag_DestroyEntity_EndPlay>()) { return false; }
        if (!H.Has<ck::FTag_DestroyEntity_Teardown>())
        { ck::FProcessor_EntityLifetime_DestructionPhase_Teardown::ForEachEntity(FCk_Time::ZeroSecond(), H); }
        return Allocated(H) && H.Has<ck::FTag_DestroyEntity_Teardown>();
    }
    auto Finalize(ck::FEcsWorld& World, FCk_Handle& H) -> bool
    {
        if (!Allocated(H) || !H.Has<ck::FTag_DestroyEntity_Teardown>()) { return false; }
        ck::FProcessor_EntityLifetime_DestructionPhase_Await::ForEachEntity(FCk_Time::ZeroSecond(), H);
        if (!H.Has<ck::FTag_DestroyEntity_Await>()) { return false; }
        ck::FProcessor_EntityLifetime_DestructionPhase_Finalize::ForEachEntity(FCk_Time::ZeroSecond(), H);
        if (!H.Has<ck::FTag_DestroyEntity_Finalize>()) { return false; }
        auto Destroy = ck::FProcessor_EntityLifetime_DestroyEntity{World.Get_Registry()};
        Destroy.DoTick(FCk_Time::ZeroSecond());
        return !Allocated(H);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkAllocatedLifetimeCensus_Production,
    "Ck.EntityScriptGym.AllocatedLifetimeCensus.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCkAllocatedLifetimeCensus_Production::RunTest(const FString& Parameters)
{
    using namespace ck_allocated_lifetime_census_test;
    ck::FEcsWorld World, ForeignWorld;
    auto& Registry = World.Get_Registry();
    const auto Context = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(Registry);
    const auto ForeignContext = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(ForeignWorld.Get_Registry());
    TArray<FCk_Handle> Out;
    if (!TestTrue(TEXT("actual transient is an allocated legitimate empty root"),
        Runtime::TryGet_AllocatedDirectLifetimeDependents(Context, Context, Out) && Out.IsEmpty())) { return false; }
    auto Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Context);
    auto Other = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Context);
    auto Bare = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
    if (!TestTrue(TEXT("production roots and bare allocation exist"), Allocated(Root) && Allocated(Other) && Allocated(Bare))) { return false; }
    TestTrue(TEXT("legitimate production leaf succeeds empty"),
        Runtime::TryGet_AllocatedDirectLifetimeDependents(Root, Context, Out) && Out.IsEmpty());
    auto Reject = [&](const FCk_Handle& R, const FCk_Handle& C)
    { Out = {Other}; return !Runtime::TryGet_AllocatedDirectLifetimeDependents(R, C, Out) && Out.IsEmpty(); };
    TestTrue(TEXT("default root rejects and clears output"), Reject({}, Context));
    TestTrue(TEXT("default context rejects and clears output"), Reject(Root, {}));
    TestTrue(TEXT("same entity with unset registry rejects"), Reject(FCk_Handle{Root.Get_Entity(), FCk_RegistryHandle::Unset()}, Context));
    TestTrue(TEXT("registry-only partial handle rejects"), Reject(FCk_Handle{FCk_Entity{}, Registry.Get_RegistryHandle()}, Context));
    TestTrue(TEXT("allocated bare root is not silently a leaf"), Reject(Bare, Context));
    TestTrue(TEXT("allocated bare context rejects"), Reject(Root, Bare));
    TestTrue(TEXT("foreign registry rejects without adopting its entities"), Reject(Root, ForeignContext));

    auto Live = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    auto Pending = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    auto Moved = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    auto Retired = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    auto Grandchild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Live);
    if (!TestTrue(TEXT("all production descendants allocated"),
        Allocated(Live) && Allocated(Pending) && Allocated(Moved) && Allocated(Retired) && Allocated(Grandchild))) { return false; }
    UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Moved, Other);
    if (!TestTrue(TEXT("real transferred child owner"),
        UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Moved) && UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Moved) == Other)) { return false; }
    if (!TestTrue(TEXT("real retired child deallocates"), ToTeardown(Retired) && Finalize(World, Retired))) { return false; }
    if (!TestTrue(TEXT("real production child reaches allocated teardown"),
        ToTeardown(Pending) && Allocated(Pending) && !ck::IsValid(Pending))) { return false; }
    const auto Before = Root.Get<ck::FFragment_LifetimeDependents>().Get_Entities();
    TestTrue(TEXT("real owner retains a stale deallocated dependent entry"), Before.Contains(Retired));
    // Context validity tests allocation in Root's registry, not default lifetime validity.
    const auto ExistingDependents = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Root);
    TestTrue(TEXT("existing getter includes allocated teardown child and omits deallocated stale child"),
        ExistingDependents.Num() == 2 && ExistingDependents.Contains(Live) &&
        ExistingDependents.Contains(Pending) && !ExistingDependents.Contains(Retired));
    TestTrue(TEXT("allocated census includes direct teardown child only"),
        Runtime::TryGet_AllocatedDirectLifetimeDependents(Root, Context, Out) && Out.Num() == 2 &&
        Out.Contains(Live) && Out.Contains(Pending) && !Out.Contains(Grandchild) && !Out.Contains(Moved) && !Out.Contains(Retired));
    TestTrue(TEXT("query does not prune or mutate owner list"),
        Before == Root.Get<ck::FFragment_LifetimeDependents>().Get_Entities());
    TestTrue(TEXT("transferred child is observed under actual new owner"),
        Runtime::TryGet_AllocatedDirectLifetimeDependents(Other, Context, Out) && Out.Num() == 1 && Out.Contains(Moved));
    Out = {Root};
    TestTrue(TEXT("input can alias caller output array safely"),
        Runtime::TryGet_AllocatedDirectLifetimeDependents(Out[0], Context, Out) && Out.Num() == 2);
    if (!TestTrue(TEXT("root reaches real allocated teardown"), ToTeardown(Root) && Allocated(Root) && !ck::IsValid(Root))) { return false; }
    TestTrue(TEXT("allocated teardown root still enumerates its allocated direct children"),
        Runtime::TryGet_AllocatedDirectLifetimeDependents(Root, Context, Out) && Out.Num() == 2 && Out.Contains(Live) && Out.Contains(Pending));
    if (!TestTrue(TEXT("pending child actually deallocates"), Finalize(World, Pending))) { return false; }
    TestTrue(TEXT("stale child omitted while allocated teardown root remains accepted"),
        Runtime::TryGet_AllocatedDirectLifetimeDependents(Root, Context, Out) && Out.Num() == 1 && Out.Contains(Live));
    if (!TestTrue(TEXT("root actually deallocates"), Finalize(World, Root))) { return false; }
    TestTrue(TEXT("deallocated root explicitly rejects empty"), Reject(Root, Context));
    TestTrue(TEXT("allocated child with deallocated owner is malformed, not a leaf"), Reject(Live, Context));
    FCk_Handle Expired;
    {
        ck::FEcsWorld Temporary;
        Expired = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
            UCk_Utils_EntityLifetime_UE::Get_TransientEntity(Temporary.Get_Registry()));
        if (!TestTrue(TEXT("temporary production child allocated"), Allocated(Expired))) { return false; }
    }
    TestTrue(TEXT("expired registry rejects without an ensure"), Reject(Expired, Context));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkFrozenRetirementLedgerProof_Production,
    "Ck.EntityScriptGym.FrozenRetirementLedgerProof.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCkFrozenRetirementLedgerProof_Production::RunTest(const FString& Parameters)
{
    using namespace ck_allocated_lifetime_census_test;
    ck::FEcsWorld World, ForeignWorld;
    auto& Registry = World.Get_Registry();
    const auto Context = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(Registry);
    const auto ForeignContext = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(ForeignWorld.Get_Registry());
    auto Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Context);
    auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    auto Bare = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Registry);
    auto Foreign = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(ForeignContext);
    if (!TestTrue(TEXT("production represented roots and malformed controls allocated"),
        Allocated(Context) && Allocated(Root) && Allocated(Child) &&
        Allocated(Bare) && Allocated(Foreign))) { return false; }

    const TArray<FCk_Handle> Complete = {Root, Child};
    const TArray<FCk_Handle> RootOnly = {Root};
    const auto OriginalDependents = Root.Get<ck::FFragment_LifetimeDependents>().Get_Entities();
    TestTrue(TEXT("real represented frozen closure is proven"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, Context));
    TestFalse(TEXT("unknown direct and native child rejects"),
        Runtime::TryProve_FrozenRetirementLedger(RootOnly, RootOnly, Context));
    TestFalse(TEXT("allocated known member absent from retiring rejects"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, RootOnly, Context));
    TestFalse(TEXT("retiring member absent from known rejects"),
        Runtime::TryProve_FrozenRetirementLedger(RootOnly, Complete, Context));
    TestFalse(TEXT("default context rejects when live root needs census"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, FCk_Handle{}));
    TestFalse(TEXT("partial handle context rejects"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete,
            FCk_Handle{Root.Get_Entity(), FCk_RegistryHandle::Unset()}));
    TestFalse(TEXT("bare allocated root is not a proven leaf"),
        Runtime::TryProve_FrozenRetirementLedger({Bare}, {Bare}, Context));
    TestFalse(TEXT("bare allocated context rejects"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, Bare));
    TestFalse(TEXT("foreign allocated context rejects"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, ForeignContext));
    TestFalse(TEXT("foreign allocated ledger member rejects"),
        Runtime::TryProve_FrozenRetirementLedger({Foreign}, {Foreign}, Context));
    TestTrue(TEXT("failed proofs neither mutate nor begin destruction"),
        Allocated(Root) && Allocated(Child) && ck::IsValid(Root) && ck::IsValid(Child) &&
        !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Root,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
        !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Child,
            ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
        Root.Get<ck::FFragment_LifetimeDependents>().Get_Entities() == OriginalDependents);

    if (!TestTrue(TEXT("real child reaches allocated teardown"), ToTeardown(Child))) { return false; }
    TestTrue(TEXT("allocated teardown child remains in frozen proof"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, Context));
    if (!TestTrue(TEXT("real root reaches allocated teardown"), ToTeardown(Root))) { return false; }
    TestTrue(TEXT("allocated teardown root remains in frozen proof"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, Context));
    if (!TestTrue(TEXT("real child deallocates"), Finalize(World, Child))) { return false; }
    TestTrue(TEXT("deallocated known child identity remains accepted"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, Context));
    TestTrue(TEXT("real root deallocates"), Finalize(World, Root));
    TestTrue(TEXT("all-deallocated frozen ledger remains vacuously proven"),
        Runtime::TryProve_FrozenRetirementLedger(Complete, Complete, FCk_Handle{}));
    TestTrue(TEXT("empty ledger remains vacuously proven"),
        Runtime::TryProve_FrozenRetirementLedger({}, {}, FCk_Handle{}));

    // The current owner is authoritative even if a malformed reverse list omits it.
    // Build both entities through the production creator, then remove only the
    // parent's reverse fragment to exercise total rejection of the malformed graph.
    {
        ck::FEcsWorld MissingReverseWorld;
        auto MissingContext = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(
            MissingReverseWorld.Get_Registry());
        auto MissingRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(MissingContext);
        auto MissingChild = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(MissingRoot);
        if (!TestTrue(TEXT("missing-reverse control has real allocated owner and child"),
            Allocated(MissingRoot) && Allocated(MissingChild) &&
            MissingRoot.Has<ck::FFragment_LifetimeDependents>() &&
            MissingRoot.Get<ck::FFragment_LifetimeDependents>().Get_Entities().Contains(MissingChild) &&
            MissingChild.Has<ck::FFragment_LifetimeOwner>() &&
            MissingChild.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == MissingRoot))
        { return false; }
        MissingRoot.Remove<ck::FFragment_LifetimeDependents>();
        if (!TestTrue(TEXT("reverse link is missing but actual child owner remains"),
            !MissingRoot.Has<ck::FFragment_LifetimeDependents>() &&
            MissingChild.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == MissingRoot))
        { return false; }
        TestFalse(TEXT("allocated owner scan rejects child hidden from reverse link and frozen ledger"),
            Runtime::TryProve_FrozenRetirementLedger({MissingRoot}, {MissingRoot}, MissingContext));
        TestTrue(TEXT("rejected missing-reverse proof does not mutate or begin destruction"),
            Allocated(MissingRoot) && Allocated(MissingChild) &&
            !MissingRoot.Has<ck::FFragment_LifetimeDependents>() &&
            MissingChild.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == MissingRoot &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(MissingRoot,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(MissingChild,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy));
    }

    // Transfer through the production API, advance the old root through teardown
    // while its reverse set is clean, then restore its former read-set to model a
    // stale foreign dependent. This does not destroy the actual transferred child.
    {
        ck::FEcsWorld StaleReadSetWorld;
        auto StaleContext = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(
            StaleReadSetWorld.Get_Registry());
        auto StaleRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(StaleContext);
        auto ForeignParent = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(StaleContext);
        auto Transferred = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(StaleRoot);
        if (!TestTrue(TEXT("stale-read-set control has real allocated entities"),
            Allocated(StaleRoot) && Allocated(ForeignParent) && Allocated(Transferred) &&
            StaleRoot.Has<ck::FFragment_LifetimeDependents>()))
        { return false; }
        const auto FormerReadSet = StaleRoot.Get<ck::FFragment_LifetimeDependents>();
        UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Transferred, ForeignParent);
        if (!TestTrue(TEXT("production transfer moves actual owner and reverse entry"),
            Transferred.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == ForeignParent &&
            !StaleRoot.Get<ck::FFragment_LifetimeDependents>().Get_Entities().Contains(Transferred) &&
            ForeignParent.Get<ck::FFragment_LifetimeDependents>().Get_Entities().Contains(Transferred)))
        { return false; }
        if (!TestTrue(TEXT("real old root reaches teardown with clean reverse set"),
            ToTeardown(StaleRoot) && Allocated(Transferred) && ck::IsValid(Transferred) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Transferred,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(ForeignParent,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            Transferred.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == ForeignParent &&
            StaleRoot.Has<ck::FFragment_LifetimeDependents>() &&
            !StaleRoot.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities().Contains(Transferred)))
        { return false; }
        auto& StaleRegistry = StaleReadSetWorld.Get_Registry();
        if (!TestTrue(TEXT("pending root is still an allocated represented registry entity"),
            Allocated(StaleRoot) && StaleRegistry.IsValid(StaleRoot.Get_Entity()) &&
            StaleRoot.Has<ck::FFragment_LifetimeDependents>()))
        { return false; }
        StaleRoot.Get<ck::FFragment_LifetimeDependents,
            ck::IsValid_Policy_IncludePendingKill>() = FormerReadSet;
        if (!TestTrue(TEXT("pending root now retains malformed foreign read-set"),
            Allocated(Transferred) && ck::IsValid(Transferred) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Transferred,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(ForeignParent,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            StaleRoot.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities().Contains(Transferred)))
        { return false; }
        const auto FrozenStaleReadSet = StaleRoot.Get<ck::FFragment_LifetimeDependents,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entities();
        const auto FrozenForeignReadSet = ForeignParent.Get<ck::FFragment_LifetimeDependents>().Get_Entities();
        const auto FrozenCurrentOwner = Transferred.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity();
        TestFalse(TEXT("pending root rejects allocated transferred foreign raw read-set"),
            Runtime::TryProve_FrozenRetirementLedger({StaleRoot}, {StaleRoot}, StaleContext));
        TestTrue(TEXT("rejected pending proof preserves allocation phase, owner, and both read-sets"),
            Allocated(StaleRoot) && Allocated(Transferred) &&
            ck::IsValid(Transferred) && ck::IsValid(ForeignParent) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(Transferred,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(ForeignParent,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            StaleRoot.Has<ck::FTag_DestroyEntity_Teardown>() &&
            Transferred.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == FrozenCurrentOwner &&
            StaleRoot.Get<ck::FFragment_LifetimeDependents,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entities() == FrozenStaleReadSet &&
            ForeignParent.Get<ck::FFragment_LifetimeDependents>().Get_Entities() == FrozenForeignReadSet);
    }
    return true;
}
#endif
