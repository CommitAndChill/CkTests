#include "CkTests/TenetIntegrity/CkTenetIntegrity_RequestProbe.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkEcs/World/CkEcsWorld.h"
#include "CkEntityTag/CkEntityTag_Fragment.h"
#include "CkEntityTag/CkEntityTag_Processor.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkGoap/Action/CkGoap_Action_Processor.h"
#include "CkGoap/Planner/CkGoap_Planner_Utils.h"
#include "CkGoap/WorldState/CkGoap_WorldState_Utils.h"

#include <Engine/World.h>
#include <Misc/AutomationTest.h>
#include <NativeGameplayTags.h>
#include <Serialization/MemoryReader.h>
#include <Serialization/MemoryWriter.h>
#include <Serialization/ObjectAndNameAsStringProxyArchive.h>
#include <UObject/StrongObjectPtr.h>
#include <limits>

// --------------------------------------------------------------------------------------------------------------------

auto
    UCkTenetIntegrity_RequestProbe::
    OnCompleted(
        FCk_Handle,
        ECk_Request_OperationResult InResult)
    -> void
{
    ++_CompletionCount;
    _Result = InResult;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCkTenetIntegrity_RequestProbe::
    OnTagUpdated(
        FCk_Handle InHandle,
        FName,
        ECk_EntityTagUpdate)
    -> void
{
    ++_TagCallbackCount;
    _ObservedConsistentGameplay &= UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(InHandle) == _ExpectedGameplay;
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCkTenetIntegrity_ConstructionProbe::
    DoConstruct_Implementation(
        FCk_Handle& InHandle) const
    -> void
{
    UCk_Utils_EntityTag_UE::Add(InHandle, TEXT("Constructed"));
}

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_request_tenet_integrity
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PlannerTag, "CkTests.TenetIntegrity.Planner");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(KnownKey, "CkTests.TenetIntegrity.Known");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(MissingKey, "CkTests.TenetIntegrity.Missing");

    auto
    DrainTags(
        ck::FEcsWorld& InWorld,
        FCk_Handle InHandle)
    -> void
    {
        if (InHandle.Has<ck::FFragment_EntityTag_Requests>())
        {
            const auto Processor = ck::FProcessor_EntityTag_HandleRequests{InWorld.Get_Registry()};
            Processor.ForEachEntity({}, InHandle, InHandle.Get<ck::FFragment_EntityTag_Requests>());
        }
    }

    auto
    DrainPlanner(
        ck::FEcsWorld& InWorld,
        FCk_Handle_Goap_Planner InPlanner)
    -> void
    {
        const auto Processor = ck::FProcessor_Goap_Planner_HandleRequests{InWorld.Get_Registry()};
        Processor.ForEachEntity({}, InPlanner,
            InPlanner.Get<ck::FFragment_Goap_Planner_Params>(), InPlanner.Get<ck::FFragment_Goap_Planner>(),
            InPlanner.Get<ck::FFragment_AStar_Tunables>(), InPlanner.Get<ck::FFragment_Goap_Planner_PlanState>(),
            InPlanner.Get<ck::FFragment_Goap_Planner_Goal>(), InPlanner.Get<ck::FFragment_Goap_Planner_WorldStateSource>(),
            InPlanner.Get<ck::FFragment_Goap_Planner_Requests>(), InPlanner.Get<ck::FFragment_Goap_Planner_SearchState>(),
            InPlanner.Get<ck::FFragment_Goap_Planner_Result>(), InPlanner.Get<ck::FFragment_Goap_Planner_PlanContext>());
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Construction_TenetIntegrity_BatchAdmission,
    "Ck.Construction.TenetIntegrity.BatchAdmission", ck_request_tenet_integrity::TestFlags)

auto
    FCk_Construction_TenetIntegrity_BatchAdmission::
    RunTest(const FString&)
    -> bool
{
    AddExpectedError(TEXT("every construction script class must be valid"), EAutomationExpectedErrorFlags::Contains, -1);
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    const auto UnrealWorld = TStrongObjectPtr<UWorld>{NewObject<UWorld>()};
    Root.Add<TWeakObjectPtr<UWorld>>(UnrealWorld.Get());
    const auto Probe = TStrongObjectPtr<UCkTenetIntegrity_RequestProbe>{NewObject<UCkTenetIntegrity_RequestProbe>()};
    auto Completion = FCk_Delegate_Request_OnCompleted{};
    Completion.BindDynamic(Probe.Get(), &UCkTenetIntegrity_RequestProbe::OnCompleted);

    UCk_Entity_ConstructionScript_PDA::Request_Construct_Multiple(Entity,
        {UCkTenetIntegrity_ConstructionProbe::StaticClass(), {}}, Completion);
    TestEqual(TEXT("rejected batch completes exactly once"), Probe->Get_CompletionCount(), 1);
    TestTrue(TEXT("rejected batch reports not enqueued"), Probe->Get_Result() == ECk_Request_OperationResult::Failed_NotEnqueued);
    TestFalse(TEXT("valid prefix never invokes construction"), Entity.Has<ck::FFragment_EntityTag_Requests>());

    UCk_Entity_ConstructionScript_PDA::Request_Construct_Multiple(Entity,
        {UCkTenetIntegrity_ConstructionProbe::StaticClass()}, Completion);
    TestEqual(TEXT("valid batch also completes once"), Probe->Get_CompletionCount(), 2);
    TestTrue(TEXT("valid batch succeeds"), Probe->Get_Result() == ECk_Request_OperationResult::Succeeded);
    ck_request_tenet_integrity::DrainTags(World, Entity);
    TestTrue(TEXT("valid construction actually runs"), UCk_Utils_EntityTag_UE::Has(Entity, TEXT("Constructed")));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityTag_TenetIntegrity_HydrationAdmission,
    "Ck.EntityTag.TenetIntegrity.HydrationAdmission", ck_request_tenet_integrity::TestFlags)

auto
    FCk_EntityTag_TenetIntegrity_HydrationAdmission::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_request_tenet_integrity;
    AddExpectedError(TEXT("EntityTag hydration payload malformed"), EAutomationExpectedErrorFlags::Contains, -1);
    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    UCk_Utils_EntityTag_UE::Add(Entity, TEXT("Original"));
    DrainTags(World, Entity);
    const auto* Handler = FCk_PersistenceHandlerRegistry::Find(FCk_SaveData_EntityTags::StaticStruct());
    if (NOT TestNotNull(TEXT("production EntityTag save handler exists"), Handler))
    { return false; }

    const auto Reject = [&](TArray<FName> InNames, TArray<int32> InCounts)
    {
        auto Payload = FCk_SaveData_EntityTags{};
        Payload.Set_TagNames(InNames).Set_Counts(InCounts);
        TestTrue(TEXT("malformed hydration is permanently rejected"),
            Handler->HydrationApply(Entity, FInstancedStruct::Make(Payload), {}) == ECk_Persistence_ApplyResult::Rejected);
        TestFalse(TEXT("malformed hydration enqueues no restore request"), Entity.Has<ck::FFragment_EntityTag_Requests>());
        TestTrue(TEXT("malformed hydration preserves the live set"), UCk_Utils_EntityTag_UE::Has(Entity, TEXT("Original")));
    };
    Reject({TEXT("Prefix"), TEXT("Missing")}, {1});
    Reject({NAME_None}, {1});
    Reject({TEXT("BadCount")}, {0});
    Reject({TEXT("Duplicate"), TEXT("Duplicate")}, {1, 2});

    const auto Saved = Handler->Produce(Entity);
    if (NOT TestTrue(TEXT("production capture emits original tags"), Saved.IsSet()))
    { return false; }
    UCk_Utils_EntityTag_UE::Add(Entity, TEXT("Extra"));
    DrainTags(World, Entity);
    TestTrue(TEXT("valid production payload is admitted"),
        Handler->HydrationApply(Entity, Saved.GetValue(), {}) == ECk_Persistence_ApplyResult::Applied);
    DrainTags(World, Entity);
    TestTrue(TEXT("restore retains saved name"), UCk_Utils_EntityTag_UE::Has(Entity, TEXT("Original")));
    TestFalse(TEXT("restore removes later name"), UCk_Utils_EntityTag_UE::Has(Entity, TEXT("Extra")));
    TestEqual(TEXT("restored storage matches original presence"), UCk_Utils_EntityTag_UE::ForEach_Entity(Entity, TEXT("Original")).Num(), 1);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_Goap_TenetIntegrity_GoalAndTuningAdmission,
    "Ck.Goap.TenetIntegrity.GoalAndTuningAdmission", ck_request_tenet_integrity::TestFlags)

auto
    FCk_Goap_TenetIntegrity_GoalAndTuningAdmission::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_request_tenet_integrity;
    AddExpectedError(TEXT("Cannot set a GOAP goal with an unresolved required key"), EAutomationExpectedErrorFlags::Contains, -1);
    AddExpectedError(TEXT("GOAP replan interval must be finite and nonnegative"), EAutomationExpectedErrorFlags::Contains, -1);
    AddExpectedError(TEXT("GOAP replan policy must be a supported value"), EAutomationExpectedErrorFlags::Contains, -1);
    AddExpectedError(TEXT("Cannot plan with an unresolved required GOAP goal key"), EAutomationExpectedErrorFlags::Contains, -1);
    auto World = ck::FEcsWorld{};
    auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    const auto UnrealWorld = TStrongObjectPtr<UWorld>{NewObject<UWorld>()};
    Root.Add<TWeakObjectPtr<UWorld>>(UnrealWorld.Get());
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto WSEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    auto WSParams = FCk_Goap_WorldState_Spec{};
    WSParams.Set_PreRegisteredKeys({KnownKey});
    const auto WS = UCk_Utils_Goap_WorldState_UE::Add(WSEntity, WSParams);
    auto Params = FCk_Goap_Planner_Spec{PlannerTag};
    Params.Set_WorldStateSource(WS).Set_PlanOnStart(false).Set_AllowPlanFailed(true);
    auto Planner = UCk_Utils_Goap_Planner_UE::Add(Owner, Params);
    const auto Probe = TStrongObjectPtr<UCkTenetIntegrity_RequestProbe>{NewObject<UCkTenetIntegrity_RequestProbe>()};
    auto Completion = FCk_Delegate_Request_OnCompleted{};
    Completion.BindDynamic(Probe.Get(), &UCkTenetIntegrity_RequestProbe::OnCompleted);

    UCk_Utils_Goap_Planner_UE::Request_SetGoal(Planner, {FCk_GoapWS_Condition_Authored{KnownKey, true}}, Completion);
    DrainPlanner(World, Planner);
    TestTrue(TEXT("known goal is accepted"), Probe->Get_Result() == ECk_Request_OperationResult::Succeeded);
    Planner.Try_Remove<ck::FTag_Goap_Planner_RequiresInitialPlan>();
    UCk_Utils_Goap_Planner_UE::Request_SetGoal(Planner,
        {FCk_GoapWS_Condition_Authored{KnownKey, false}, FCk_GoapWS_Condition_Authored{MissingKey, true}}, Completion);
    DrainPlanner(World, Planner);
    TestTrue(TEXT("missing required key rejects entire goal request"), Probe->Get_Result() == ECk_Request_OperationResult::Failed);
    const auto& AcceptedGoal = Planner.Get<ck::FFragment_Goap_Planner_Goal>();
    if (NOT TestEqual(TEXT("accepted goal is retained atomically"), AcceptedGoal.Get_GoalAuthored().Num(), 1))
    { return false; }
    TestTrue(TEXT("valid prefix does not change accepted goal"), AcceptedGoal.Get_GoalAuthored()[0].Get_Value());
    TestFalse(TEXT("rejected goal does not schedule planning"), Planner.Has<ck::FTag_Goap_Planner_RequiresInitialPlan>());

    UCk_Utils_Goap_Planner_UE::Request_SetGoal(Planner, {}, Completion);
    DrainPlanner(World, Planner);
    UCk_Utils_Goap_Planner_UE::Request_Plan(Planner, Completion);
    DrainPlanner(World, Planner);
    TestTrue(TEXT("intentionally empty goal produces a plan"),
        Planner.Get<ck::FFragment_Goap_Planner_PlanState>().Get_PlanStatus() == ECk_GoapPlanStatus::PlanFound);

    UCk_Utils_Goap_Planner_UE::Request_SetReplanInterval(Planner, 10.0f, Completion);
    UCk_Utils_Goap_Planner_UE::Request_SetReplanPolicy(Planner, ECk_Goap_ReplanPolicy::OnCostDirty, Completion);
    DrainPlanner(World, Planner);
    TestTrue(TEXT("runtime tuning reports success"), Probe->Get_Result() == ECk_Request_OperationResult::Succeeded);
    TestEqual(TEXT("runtime interval reaches AutoReplan input"), Planner.Get<ck::FFragment_Goap_Planner_Params>().Get_MinReplanIntervalSeconds(), 10.0f);
    TestTrue(TEXT("runtime policy reaches AutoReplan input"), Planner.Get<ck::FFragment_Goap_Planner_Params>().Get_ReplanPolicy() == ECk_Goap_ReplanPolicy::OnCostDirty);
    UCk_Utils_Goap_Planner_UE::Request_SetReplanInterval(Planner, -1.0f, Completion);
    DrainPlanner(World, Planner);
    TestTrue(TEXT("invalid interval reports failure"), Probe->Get_Result() == ECk_Request_OperationResult::Failed);
    TestEqual(TEXT("invalid tuning leaves interval intact"), Planner.Get<ck::FFragment_Goap_Planner_Params>().Get_MinReplanIntervalSeconds(), 10.0f);
    UCk_Utils_Goap_Planner_UE::Request_SetReplanInterval(Planner, std::numeric_limits<float>::quiet_NaN(), Completion);
    DrainPlanner(World, Planner);
    TestTrue(TEXT("nonfinite interval reports failure"), Probe->Get_Result() == ECk_Request_OperationResult::Failed);
    TestEqual(TEXT("nonfinite tuning leaves interval intact"), Planner.Get<ck::FFragment_Goap_Planner_Params>().Get_MinReplanIntervalSeconds(), 10.0f);
    UCk_Utils_Goap_Planner_UE::Request_SetReplanPolicy(Planner, static_cast<ECk_Goap_ReplanPolicy>(255), Completion);
    DrainPlanner(World, Planner);
    TestTrue(TEXT("invalid policy reports failure"), Probe->Get_Result() == ECk_Request_OperationResult::Failed);
    TestTrue(TEXT("invalid policy preserves accepted tuning"), Planner.Get<ck::FFragment_Goap_Planner_Params>().Get_ReplanPolicy() == ECk_Goap_ReplanPolicy::OnCostDirty);

    Planner.Try_Remove<ck::FTag_Goap_Planner_RequiresSetup>();
    Planner.Try_Remove<ck::FTag_Goap_Planner_RequiresInitialPlan>();
    Planner.AddOrGet<ck::FTag_Goap_Dirty_Cost>();
    const auto AutoReplan = [&]()
    {
        ck::FProcessor_Goap_Planner_AutoReplan::ForEachEntity({}, Planner,
            Planner.Get<ck::FFragment_Goap_Planner_Params>(), Planner.Get<ck::FFragment_Goap_Planner>(),
            Planner.Get<ck::FFragment_Goap_Planner_WorldStateSource>(), Planner.Get<ck::FFragment_Goap_Planner_ReplanThrottle>());
    };
    AutoReplan();
    TestFalse(TEXT("runtime throttle actually blocks a dirty replan"), Planner.Has<ck::FFragment_Goap_Planner_Requests>());
    UCk_Utils_Goap_Planner_UE::Request_SetReplanInterval(Planner, 0.0f, Completion);
    UCk_Utils_Goap_Planner_UE::Request_SetReplanPolicy(Planner, ECk_Goap_ReplanPolicy::Explicit, Completion);
    DrainPlanner(World, Planner);
    AutoReplan();
    TestFalse(TEXT("runtime explicit policy suppresses the same cost dirtiness"), Planner.Has<ck::FFragment_Goap_Planner_Requests>());
    UCk_Utils_Goap_Planner_UE::Request_SetReplanPolicy(Planner, ECk_Goap_ReplanPolicy::OnCostDirty, Completion);
    DrainPlanner(World, Planner);
    AutoReplan();
    TestTrue(TEXT("runtime cost policy and zero interval admit the pending replan"), Planner.Has<ck::FFragment_Goap_Planner_Requests>());

    auto InvalidOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    Params.Set_Goal({FCk_GoapWS_Condition_Authored{MissingKey, true}});
    auto InvalidPlanner = UCk_Utils_Goap_Planner_UE::Add(InvalidOwner, Params);
    UCk_Utils_Goap_Planner_UE::Request_Plan(InvalidPlanner, Completion);
    DrainPlanner(World, InvalidPlanner);
    TestTrue(TEXT("unresolved initial authored goal fails planning"),
        InvalidPlanner.Get<ck::FFragment_Goap_Planner_PlanState>().Get_PlanStatus() == ECk_GoapPlanStatus::PlanFailed);
    TestTrue(TEXT("unresolved initial goal completion is truthful"), Probe->Get_Result() == ECk_Request_OperationResult::Failed);
    TestFalse(TEXT("unresolved goal starts no AStar search"), InvalidPlanner.Has<ck::FTag_AStar_SearchActive>());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_EntityTag_TenetIntegrity_GameplayAndLegacyRoundTrip,
    "Ck.EntityTag.TenetIntegrity.GameplayAndLegacyRoundTrip", ck_request_tenet_integrity::TestFlags)

auto
    FCk_EntityTag_TenetIntegrity_GameplayAndLegacyRoundTrip::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_request_tenet_integrity;
    AddExpectedError(TEXT("EntityTag hydration payload malformed"), EAutomationExpectedErrorFlags::Contains, -1);
    auto World = ck::FEcsWorld{};
    const auto Root = UCk_Utils_EntityLifetime_UE::Get_TransientEntity(World.Get_Registry());
    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root, {});
    const FGameplayTag Explicit = KnownKey;
    UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(Entity, Explicit);
    UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(Entity, Explicit);
    UCk_Utils_EntityTag_UE::Add(Entity, Explicit.GetTagName());
    DrainTags(World, Entity);
    const auto* Handler = FCk_PersistenceHandlerRegistry::Find(FCk_SaveData_EntityTags::StaticStruct());
    if (NOT TestNotNull(TEXT("production EntityTag handler exists"), Handler))
    { return false; }
    auto Captured = Handler->Produce(Entity);
    if (NOT TestTrue(TEXT("mixed name/gameplay state captures"), Captured.IsSet()))
    { return false; }
    TestEqual(TEXT("new captures declare the extended schema"), Captured->Get<FCk_SaveData_EntityTags>().Get_Version(), 1);

    // The same tagged-property archive and FInstancedStruct serializer used by Snapshot CaptureV3.
    auto Bytes = TArray<uint8>{};
    {
        auto Writer = FMemoryWriter{Bytes, true};
        auto Archive = FObjectAndNameAsStringProxyArchive{Writer, true};
        Archive.SetIsPersistent(true);
        Captured->Serialize(Archive);
    }
    auto Loaded = FInstancedStruct{};
    {
        auto Reader = FMemoryReader{Bytes, true};
        auto Archive = FObjectAndNameAsStringProxyArchive{Reader, true};
        Archive.SetIsPersistent(true);
        Loaded.Serialize(Archive);
    }
    UCk_Utils_EntityTag_UE::Add_UsingGameplayTag(Entity, MissingKey);
    DrainTags(World, Entity);
    const auto Probe = TStrongObjectPtr<UCkTenetIntegrity_RequestProbe>{NewObject<UCkTenetIntegrity_RequestProbe>()};
    auto ExpectedGameplay = FGameplayTagContainer{};
    ExpectedGameplay.AddTag(Explicit);
    Probe->Set_ExpectedGameplay(MoveTemp(ExpectedGameplay));
    auto OnTag = FCk_Delegate_EntityTag_OnTagUpdated{};
    OnTag.BindDynamic(Probe.Get(), &UCkTenetIntegrity_RequestProbe::OnTagUpdated);
    UCk_Utils_EntityTag_UE::BindTo_OnTagUpdated(Entity, ECk_Signal_BindingPolicy::IgnorePayloadInFlight,
        ECk_Signal_PostFireBehavior::DoNothing, OnTag);
    TestTrue(TEXT("serialized new payload is admitted"),
        Handler->HydrationApply(Entity, Loaded, {}) == ECk_Persistence_ApplyResult::Applied);
    DrainTags(World, Entity);
    const auto& Restored = Entity.Get<ck::FFragment_EntityTag>();
    if (NOT TestEqual(TEXT("new restore retains only the saved explicit gameplay identity"), Restored.Get_GameplayTagCounts().Num(), 1))
    { return false; }
    TestEqual(TEXT("new restore retains the explicit reference count"), Restored.Get_GameplayTagCounts()[0]._Count, 2);
    const auto* NameCount = Restored.Get_Tags().FindByPredicate([Explicit](const ck::FEntityTagCount& InEntry)
    { return InEntry._Name == Explicit.GetTagName(); });
    if (NOT TestNotNull(TEXT("flattened explicit name exists"), NameCount))
    { return false; }
    TestEqual(TEXT("plain names and explicit gameplay counts remain distinct"), NameCount->_Count, 3);
    TestTrue(TEXT("restore callbacks observe the final gameplay view"), Probe->Get_ObservedConsistentGameplay());
    TestTrue(TEXT("a changed name actually notified the observer"), Probe->Get_TagCallbackCount() > 0);
    const auto CallbacksAfterRestore = Probe->Get_TagCallbackCount();
    TestTrue(TEXT("repeated hydration is admitted"),
        Handler->HydrationApply(Entity, Loaded, {}) == ECk_Persistence_ApplyResult::Applied);
    DrainTags(World, Entity);
    TestEqual(TEXT("identical restore emits no presence callbacks"), Probe->Get_TagCallbackCount(), CallbacksAfterRestore);

    auto Bad = Loaded.Get<FCk_SaveData_EntityTags>();
    const auto Reject = [&]()
    {
        const auto CallbacksBeforeRejection = Probe->Get_TagCallbackCount();
        TestTrue(TEXT("malformed extended payload is rejected atomically"),
            Handler->HydrationApply(Entity, FInstancedStruct::Make(Bad), {}) == ECk_Persistence_ApplyResult::Rejected);
        TestFalse(TEXT("extended rejection enqueues nothing"), Entity.Has<ck::FFragment_EntityTag_Requests>());
        TestEqual(TEXT("rejection emits no downstream presence callbacks"), Probe->Get_TagCallbackCount(), CallbacksBeforeRejection);
        const auto& Gameplay = Entity.Get<ck::FFragment_EntityTag>().Get_GameplayTagCounts();
        if (TestEqual(TEXT("rejection preserves explicit identity"), Gameplay.Num(), 1))
        { TestEqual(TEXT("rejection preserves explicit count"), Gameplay[0]._Count, 2); }
    };
    Bad.Set_Version(2);
    Reject();
    Bad.Set_Version(1).Set_GameplayCounts({});
    Reject();
    Bad.Set_GameplayCounts({4});
    Reject();
    Bad.Set_GameplayTags({Explicit, Explicit}).Set_GameplayCounts({1, 1});
    Reject();
    Bad.Set_GameplayTags({FGameplayTag{}}).Set_GameplayCounts({1});
    Reject();
    Bad.Set_GameplayTags({Explicit}).Set_GameplayCounts({0});
    Reject();

    auto Legacy = FCkTenetIntegrity_LegacyEntityTags{};
    Legacy.Set_TagNames({Explicit.GetTagName(), TEXT("LegacyName")}).Set_Counts({7, 3});
    Bytes.Reset();
    {
        auto Writer = FMemoryWriter{Bytes, true};
        auto Archive = FObjectAndNameAsStringProxyArchive{Writer, true};
        FCkTenetIntegrity_LegacyEntityTags::StaticStruct()->SerializeItem(Archive, &Legacy, nullptr);
    }
    auto LegacyLoaded = FCk_SaveData_EntityTags{};
    {
        auto Reader = FMemoryReader{Bytes, true};
        auto Archive = FObjectAndNameAsStringProxyArchive{Reader, true};
        FCk_SaveData_EntityTags::StaticStruct()->SerializeItem(Archive, &LegacyLoaded, nullptr);
    }
    TestEqual(TEXT("absent legacy version property defaults to zero"), LegacyLoaded.Get_Version(), 0);
    TestTrue(TEXT("legacy wire preserves exact name counts"), LegacyLoaded.Get_Counts() == Legacy.Get_Counts());
    Probe->Set_ExpectedGameplay({});
    const auto CallbacksBeforeLegacy = Probe->Get_TagCallbackCount();
    TestTrue(TEXT("serialized legacy payload remains supported"),
        Handler->HydrationApply(Entity, FInstancedStruct::Make(LegacyLoaded), {}) == ECk_Persistence_ApplyResult::Applied);
    DrainTags(World, Entity);
    TestTrue(TEXT("legacy restore clears unrepresentable explicit identities"), UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(Entity).IsEmpty());
    const auto& LegacyTags = Entity.Get<ck::FFragment_EntityTag>().Get_Tags();
    if (NOT TestEqual(TEXT("legacy restore retains both exact names"), LegacyTags.Num(), 2))
    { return false; }
    TestEqual(TEXT("legacy restore retains registered-looking name count without identity inference"), LegacyTags[0]._Count, 7);
    TestEqual(TEXT("legacy restore retains the plain name count"), LegacyTags[1]._Count, 3);
    TestTrue(TEXT("legacy callbacks observe already-cleared gameplay identities"), Probe->Get_ObservedConsistentGameplay());
    TestTrue(TEXT("legacy restore actually notifies its observer"), Probe->Get_TagCallbackCount() > CallbacksBeforeLegacy);
    return true;
}

#endif
