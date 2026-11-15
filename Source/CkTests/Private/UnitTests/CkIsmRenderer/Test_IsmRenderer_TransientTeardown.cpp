#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Enums/CkEnums.h"
#include "CkCore/Validation/CkIsValid.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include "CkIsmRenderer/CkIsmSubsystem.h"
#include "CkIsmRenderer/Proxy/CkIsmProxy_Fragment.h"
#include "CkIsmRenderer/Proxy/CkIsmProxy_Utils.h"
#include "CkIsmRenderer/Renderer/CkIsmRenderer_TransientFactory.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_tests_ismrenderer_transient_teardown
{
    constexpr auto kTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto kEntryMapPath = TEXT("/Engine/Maps/Entry");
    constexpr auto kReadyTimeoutSeconds = 30.0;
    constexpr auto kCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
    constexpr auto kIsmMaterialPath =
        TEXT("/CkFoundation/CkUsf/GeneratedLooks/M_CkUsf_Look_PerInstanceHue.M_CkUsf_Look_PerInstanceHue");

    struct FScenario
    {
        TWeakObjectPtr<UWorld> _World;
        TWeakObjectPtr<UCk_IsmRenderer_Subsystem_UE> _Subsystem;
        TWeakObjectPtr<UCk_IsmRenderer_Data> _Data;

        FCk_Handle _OwnerA;
        FCk_Handle _OwnerB;
        FCk_Handle _OwnerC;
        FCk_Handle _OwnerD;
        FCk_Handle_IsmProxy _ProxyA;
        FCk_Handle_IsmProxy _ProxyC;

        TWeakObjectPtr<ACk_IsmRenderer_Actor_UE> _Renderer;
        FCk_Handle _RendererEntity;

        int32 _BaselineRendererActors = 0;
        TSet<FCk_Entity> _BaselineLifetimeEntities;
        int32 _BaselineLiveTransientRenderers = 0;

        int32 _MaxRendererActorsDuringRemoval = 0;
        uint64 _PendingObservedOnFrame = 0;
        uint64 _RendererGoneOnFrame = 0;
    };

    auto MakeAuthorityNetSettings() -> FCk_Net_ConnectionSettings
    {
        return FCk_Net_ConnectionSettings{
            ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::ClientAndHost,
            ECk_Net_EntityNetRole::Authority};
    }

    auto CountRendererActors(UWorld* InWorld) -> int32
    {
        auto Count = 0;

        if (NOT ck::IsValid(InWorld))
        { return Count; }

        for (TActorIterator<ACk_IsmRenderer_Actor_UE> It(InWorld); It; ++It)
        {
            if (IsValid(*It))
            { ++Count; }
        }

        return Count;
    }

    auto CollectLifetimeEntities(UWorld* InWorld) -> TSet<FCk_Entity>
    {
        auto Entities = TSet<FCk_Entity>{};

        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld);

        if (ck::Is_NOT_Valid(TransientEntity))
        { return Entities; }

        TransientEntity.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity InEntity, const ck::FFragment_LifetimeOwner&)
        {
            if (ck::IsValid(ck::MakeHandle(InEntity, TransientEntity), ck::IsValid_Policy_IncludePendingKill{}))
            { Entities.Add(InEntity); }
        });

        return Entities;
    }

    auto CountEntitiesNotIn(UWorld* InWorld, const TSet<FCk_Entity>& InBaseline) -> int32
    {
        auto Count = 0;

        for (const auto& Entity : CollectLifetimeEntities(InWorld))
        {
            if (NOT InBaseline.Contains(Entity))
            { ++Count; }
        }

        return Count;
    }

    auto IsFullyDestroyed(const FCk_Handle& InHandle) -> bool
    {
        return NOT ck::IsValid(InHandle, ck::IsValid_Policy_IncludePendingKill{});
    }

    auto CreateProxyOwner(
        UWorld* InWorld,
        UCk_IsmRenderer_Data* InData,
        ECk_EnableDisable InStartingState,
        FCk_Handle& OutOwner) -> FCk_Handle_IsmProxy
    {
        OutOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);

        if (ck::Is_NOT_Valid(OutOwner))
        { return {}; }

        UCk_Utils_Net_UE::Add(OutOwner, MakeAuthorityNetSettings());

        auto Transform = UCk_Utils_Transform_UE::Add(OutOwner, FTransform::Identity, ECk_Replication::DoesNotReplicate);

        if (ck::Is_NOT_Valid(Transform))
        { return {}; }

        auto ProxyParams = FCk_IsmProxy_Spec{InData};
        ProxyParams.Set_StartingState(InStartingState);

        return UCk_Utils_IsmProxy_UE::Add(Transform, ProxyParams);
    }

    auto HasLiveInstance(const FScenario& InScenario, const FCk_Handle_IsmProxy& InProxy) -> bool
    {
        const auto* Subsystem = InScenario._Subsystem.Get();

        if (NOT ck::IsValid(Subsystem) || NOT ck::IsValid(InProxy) || NOT InProxy.Has<ck::FFragment_IsmProxy>())
        { return false; }

        auto* IsmComponent = Subsystem->Find_IsmComponent(InScenario._Data.Get()).Get();

        return ck::IsValid(IsmComponent) &&
            IsmComponent->IsValidId(InProxy.Get<ck::FFragment_IsmProxy>().Get_IsmInstanceIndex());
    }

    auto DestroyEntity(FCk_Handle& InHandle) -> void
    {
        if (ck::IsValid(InHandle))
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(InHandle); }
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_IsmRenderer_TransientTeardown,
    "CkTests.UnitTests.CkIsmRenderer.IsmRenderer.TransientTeardown",
    ck_tests_ismrenderer_transient_teardown::kTestFlags)

bool FCkTest_IsmRenderer_TransientTeardown::RunTest(const FString&)
{
    using namespace ck_tests_ismrenderer_transient_teardown;

    const auto Scenario = MakeShared<FScenario>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, kEntryMapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, kReadyTimeoutSeconds));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Scenario](UWorld* InWorld) -> void
            {
                auto* Mesh = LoadObject<UStaticMesh>(nullptr, kCubeMeshPath);
                if (NOT TestTrue(TEXT("transient ISM test mesh loads"), ck::IsValid(Mesh)))
                { return; }

                auto* Subsystem = InWorld->GetSubsystem<UCk_IsmRenderer_Subsystem_UE>();
                if (NOT TestTrue(TEXT("ISM renderer subsystem exists in the game world"), ck::IsValid(Subsystem)))
                { return; }

                auto* Material = LoadObject<UMaterialInterface>(nullptr, kIsmMaterialPath);
                if (NOT TestTrue(TEXT("an ISM-usable material loads"), ck::IsValid(Material)))
                { return; }

                const auto Overrides = TArray<FCk_MeshMaterialOverride>{FCk_MeshMaterialOverride{0, Material}};
                auto* Data = UCk_Utils_IsmRenderer_TransientFactory_UE::GetOrCreate_ForMeshWithMaterialsAndCustomData(
                    InWorld, Mesh, Overrides, 1, ECk_Mobility::Movable);
                if (NOT TestTrue(TEXT("transient factory creates renderer data"), ck::IsValid(Data)))
                { return; }

                TestTrue(TEXT("factory data is marked as factory-created"), Data->Get_IsTransientFactoryCreated());

                Scenario->_World = InWorld;
                Scenario->_Subsystem = Subsystem;
                Scenario->_Data = Data;
                Scenario->_BaselineRendererActors = CountRendererActors(InWorld);
                Scenario->_BaselineLifetimeEntities = CollectLifetimeEntities(InWorld);
                Scenario->_BaselineLiveTransientRenderers = Subsystem->Get_LiveTransientRendererCount();

                Scenario->_ProxyA = CreateProxyOwner(InWorld, Data, ECk_EnableDisable::Enable, Scenario->_OwnerA);
                const auto ProxyB = CreateProxyOwner(InWorld, Data, ECk_EnableDisable::Disable, Scenario->_OwnerB);

                TestTrue(TEXT("enabled proxy A was added"), ck::IsValid(Scenario->_ProxyA));
                TestTrue(TEXT("disabled proxy B was added"), ck::IsValid(ProxyB));
                TestEqual(TEXT("both proxies hold a reference from the moment they are added"), Subsystem->Get_ProxyRefCount(Data), 2);
            })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda(
            [Scenario]() -> bool
            { return HasLiveInstance(*Scenario, Scenario->_ProxyA); }),
        kReadyTimeoutSeconds,
        TEXT("proxy A's setup spawns the shared renderer and adds its instance")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda(
            [this, Scenario]() -> bool
            {
                auto* Subsystem = Scenario->_Subsystem.Get();
                auto* Data = Scenario->_Data.Get();
                if (NOT ck::IsValid(Subsystem) || NOT ck::IsValid(Data))
                {
                    AddError(TEXT("subsystem or renderer data was lost before the shared-renderer checks"));
                    return true;
                }

                Scenario->_Renderer = Subsystem->Find_IsmRenderer(Data);
                if (NOT TestTrue(TEXT("one renderer actor serves both owners"), Scenario->_Renderer.IsValid()))
                { return true; }

                Scenario->_RendererEntity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Scenario->_Renderer.Get());

                TestTrue(TEXT("the renderer actor carries its EntityScript entity"), ck::IsValid(Scenario->_RendererEntity));
                TestEqual(TEXT("exactly one renderer actor was spawned for the shared data"),
                    CountRendererActors(Scenario->_World.Get()), Scenario->_BaselineRendererActors + 1);
                TestEqual(TEXT("the live transient renderer count includes the shared renderer"),
                    Subsystem->Get_LiveTransientRendererCount(), Scenario->_BaselineLiveTransientRenderers + 1);
                TestTrue(TEXT("the high-water mark covers the live transient renderers"),
                    Subsystem->Get_TransientRendererHighWaterMark() >= Subsystem->Get_LiveTransientRendererCount());
                TestFalse(TEXT("a renderer with live proxies is never pending teardown"), Subsystem->Get_IsPendingTeardown(Data));

                Subsystem->Request_RecountProxyRefs();
                TestEqual(TEXT("the recount agrees with the acquired references"), Subsystem->Get_ProxyRefCount(Data), 2);
                return true;
            }),
        TEXT("two owners share one renderer")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [Scenario](UWorld*) -> void
            { DestroyEntity(Scenario->_OwnerA); })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda(
            [Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                return ck::IsValid(Subsystem) && Subsystem->Get_ProxyRefCount(Scenario->_Data.Get()) == 1;
            }),
        kReadyTimeoutSeconds,
        TEXT("owner A's proxy releases its reference at EndPlay")));

    // Negative window (proving the renderer survives), so it cannot become a condition: the positive release above
    // proves the machinery ran, and a pending teardown is applied on the very next world tick, well inside 5 frames.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda(
            [this, Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                const auto* Data = Scenario->_Data.Get();
                if (NOT ck::IsValid(Subsystem) || NOT ck::IsValid(Data))
                {
                    AddError(TEXT("subsystem or renderer data was lost before the disabled-proxy checks"));
                    return true;
                }

                TestTrue(TEXT("the renderer outlives one owner's proxies while a disabled proxy holds it"),
                    Scenario->_Renderer.IsValid() && Subsystem->Find_IsmRenderer(Data) == Scenario->_Renderer.Get());
                TestFalse(TEXT("a disabled proxy keeps its renderer out of teardown"), Subsystem->Get_IsPendingTeardown(Data));
                TestEqual(TEXT("no renderer actor was added or removed"),
                    CountRendererActors(Scenario->_World.Get()), Scenario->_BaselineRendererActors + 1);
                return true;
            }),
        TEXT("a disabled proxy keeps the shared renderer alive")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [Scenario](UWorld* InWorld) -> void
            {
                Scenario->_MaxRendererActorsDuringRemoval = CountRendererActors(InWorld);
                DestroyEntity(Scenario->_OwnerB);
            })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda(
            [Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                if (NOT ck::IsValid(Subsystem))
                { return false; }

                Scenario->_MaxRendererActorsDuringRemoval = FMath::Max(
                    Scenario->_MaxRendererActorsDuringRemoval, CountRendererActors(Scenario->_World.Get()));

                if (Scenario->_PendingObservedOnFrame == 0 && Subsystem->Get_IsPendingTeardown(Scenario->_Data.Get()))
                { Scenario->_PendingObservedOnFrame = GFrameCounter; }

                if (Scenario->_Renderer.IsValid())
                { return false; }

                Scenario->_RendererGoneOnFrame = GFrameCounter;
                return true;
            }),
        kReadyTimeoutSeconds,
        TEXT("the last proxy's release tears the renderer actor down")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda(
            [this, Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                const auto* Data = Scenario->_Data.Get();
                if (NOT ck::IsValid(Subsystem) || NOT ck::IsValid(Data))
                {
                    AddError(TEXT("subsystem or renderer data was lost before the teardown checks"));
                    return true;
                }

                TestTrue(TEXT("the zero-proxy renderer was marked pending before it was destroyed"),
                    Scenario->_PendingObservedOnFrame != 0);
                TestTrue(TEXT("the renderer is destroyed on the tick after its count reaches zero"),
                    Scenario->_RendererGoneOnFrame - Scenario->_PendingObservedOnFrame <= 2);
                TestTrue(TEXT("removing proxies never spawns a renderer"),
                    Scenario->_MaxRendererActorsDuringRemoval <= Scenario->_BaselineRendererActors + 1);

                TestEqual(TEXT("no proxy references remain"), Subsystem->Get_ProxyRefCount(Data), 0);
                TestFalse(TEXT("the teardown is no longer pending"), Subsystem->Get_IsPendingTeardown(Data));
                TestEqual(TEXT("the renderer map, component cache and shadow caches hold nothing for the data"),
                    Subsystem->Get_CachedEntryCount(Data), 0);
                TestFalse(TEXT("the find-only lookup does not resurrect a component"),
                    Subsystem->Find_IsmComponent(Data).IsValid());
                TestEqual(TEXT("the renderer actor count is back to baseline"),
                    CountRendererActors(Scenario->_World.Get()), Scenario->_BaselineRendererActors);
                TestEqual(TEXT("the live transient renderer count is back to baseline"),
                    Subsystem->Get_LiveTransientRendererCount(), Scenario->_BaselineLiveTransientRenderers);
                return true;
            }),
        TEXT("zero proxies destroy the renderer and purge its caches on the next tick")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda(
            [Scenario]() -> bool
            {
                return IsFullyDestroyed(Scenario->_RendererEntity) &&
                    IsFullyDestroyed(Scenario->_OwnerA) &&
                    IsFullyDestroyed(Scenario->_OwnerB);
            }),
        kReadyTimeoutSeconds,
        TEXT("the renderer's EntityScript entity and both owners are destroyed")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda(
            [this, Scenario]() -> bool
            {
                TestEqual(TEXT("no entity created since the baseline is still alive"),
                    CountEntitiesNotIn(Scenario->_World.Get(), Scenario->_BaselineLifetimeEntities), 0);
                return true;
            }),
        TEXT("teardown leaves no entity behind")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Scenario](UWorld* InWorld) -> void
            {
                const auto ProxyD = CreateProxyOwner(InWorld, Scenario->_Data.Get(), ECk_EnableDisable::Enable, Scenario->_OwnerD);
                TestTrue(TEXT("proxy D was added with no renderer alive"), ck::IsValid(ProxyD));
                DestroyEntity(Scenario->_OwnerD);
            })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda(
            [Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                return ck::IsValid(Subsystem) &&
                    IsFullyDestroyed(Scenario->_OwnerD) &&
                    Subsystem->Get_CachedEntryCount(Scenario->_Data.Get()) == 0;
            }),
        kReadyTimeoutSeconds,
        TEXT("a proxy destroyed in the frame it was added leaves no renderer behind")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda(
            [this, Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                const auto* Data = Scenario->_Data.Get();
                if (NOT ck::IsValid(Subsystem) || NOT ck::IsValid(Data))
                {
                    AddError(TEXT("subsystem or renderer data was lost before the never-set-up proxy checks"));
                    return true;
                }

                TestEqual(TEXT("the renderer actor count is back to baseline"),
                    CountRendererActors(Scenario->_World.Get()), Scenario->_BaselineRendererActors);
                TestTrue(TEXT("no renderer exists for the data"), Subsystem->Find_IsmRenderer(Data) == nullptr);
                TestEqual(TEXT("the short-lived proxy released its reference"), Subsystem->Get_ProxyRefCount(Data), 0);
                TestFalse(TEXT("nothing is pending teardown"), Subsystem->Get_IsPendingTeardown(Data));
                return true;
            }),
        TEXT("a proxy that dies in its first frame does not leak a renderer")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Scenario](UWorld* InWorld) -> void
            {
                auto* Data = Scenario->_Data.Get();
                if (NOT TestTrue(TEXT("the rooted factory data survives the renderer teardown"), ck::IsValid(Data)))
                { return; }

                Scenario->_ProxyC = CreateProxyOwner(InWorld, Data, ECk_EnableDisable::Enable, Scenario->_OwnerC);
                TestTrue(TEXT("proxy C was added to the torn-down data"), ck::IsValid(Scenario->_ProxyC));
            })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda(
            [Scenario]() -> bool
            { return HasLiveInstance(*Scenario, Scenario->_ProxyC); }),
        kReadyTimeoutSeconds,
        TEXT("re-acquiring after teardown respawns the renderer and adds the instance")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda(
            [this, Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                const auto* Data = Scenario->_Data.Get();
                if (NOT ck::IsValid(Subsystem) || NOT ck::IsValid(Data))
                {
                    AddError(TEXT("subsystem or renderer data was lost before the respawn checks"));
                    return true;
                }

                const auto* Renderer = Subsystem->Find_IsmRenderer(Data);
                const auto* IsmComponent = Subsystem->Find_IsmComponent(Data).Get();

                TestTrue(TEXT("a fresh renderer actor was spawned"), ck::IsValid(Renderer));
                TestEqual(TEXT("the respawned renderer holds one reference"), Subsystem->Get_ProxyRefCount(Data), 1);
                TestEqual(TEXT("exactly one renderer actor exists again"),
                    CountRendererActors(Scenario->_World.Get()), Scenario->_BaselineRendererActors + 1);
                TestTrue(TEXT("the respawned ISM component is registered"),
                    ck::IsValid(IsmComponent) && IsmComponent->IsRegistered());
                TestEqual(TEXT("the respawned ISM component renders proxy C's single instance"),
                    ck::IsValid(IsmComponent) ? IsmComponent->GetNumInstances() : -1, 1);
                return true;
            }),
        TEXT("re-acquire after teardown respawns and renders")));

    // C's EndPlay is the first thing to resolve the renderer after it is destroyed from outside, so a resolving
    // lookup there would respawn it with no proxy left to release it.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda(
            [Scenario](UWorld* InWorld) -> void
            {
                if (const auto* Subsystem = Scenario->_Subsystem.Get(); ck::IsValid(Subsystem))
                {
                    if (auto* Renderer = Subsystem->Find_IsmRenderer(Scenario->_Data.Get()))
                    { Renderer->Destroy(); }
                }

                Scenario->_MaxRendererActorsDuringRemoval = CountRendererActors(InWorld);
                DestroyEntity(Scenario->_OwnerC);
            })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this,
        FCk_NetAutoTest_Condition::CreateLambda(
            [Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                if (NOT ck::IsValid(Subsystem))
                { return false; }

                Scenario->_MaxRendererActorsDuringRemoval = FMath::Max(
                    Scenario->_MaxRendererActorsDuringRemoval, CountRendererActors(Scenario->_World.Get()));

                return IsFullyDestroyed(Scenario->_OwnerC) && Subsystem->Get_CachedEntryCount(Scenario->_Data.Get()) == 0;
            }),
        kReadyTimeoutSeconds,
        TEXT("the last proxy of an externally destroyed renderer is removed and its cache entries purged")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda(
            [this, Scenario]() -> bool
            {
                const auto* Subsystem = Scenario->_Subsystem.Get();
                const auto* Data = Scenario->_Data.Get();
                if (NOT ck::IsValid(Subsystem) || NOT ck::IsValid(Data))
                {
                    AddError(TEXT("subsystem or renderer data was lost before the removal checks"));
                    return true;
                }

                TestEqual(TEXT("removing the last proxy never creates a renderer"),
                    Scenario->_MaxRendererActorsDuringRemoval, Scenario->_BaselineRendererActors);
                TestTrue(TEXT("no renderer exists for the data"), Subsystem->Find_IsmRenderer(Data) == nullptr);
                TestEqual(TEXT("no proxy references remain"), Subsystem->Get_ProxyRefCount(Data), 0);
                return true;
            }),
        TEXT("teardown never creates a renderer")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_IsmRenderer_EditorWorldKeepsTransientRenderer,
    "CkTests.UnitTests.CkIsmRenderer.IsmRenderer.EditorWorldKeepsTransientRenderer",
    ck_tests_ismrenderer_transient_teardown::kTestFlags)

bool FCkTest_IsmRenderer_EditorWorldKeepsTransientRenderer::RunTest(const FString&)
{
    using namespace ck_tests_ismrenderer_transient_teardown;

    auto* Mesh = LoadObject<UStaticMesh>(nullptr, kCubeMeshPath);
    if (NOT TestTrue(TEXT("transient ISM test mesh loads"), ck::IsValid(Mesh)))
    { return true; }

    auto* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("CkIsmRendererTeardownEditorWorld"));
    if (NOT TestTrue(TEXT("a standalone editor world was created"), ck::IsValid(World)))
    { return true; }

    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };

    TestFalse(TEXT("the standalone world is not a game world"), World->IsGameWorld());

    auto* Subsystem = World->GetSubsystem<UCk_IsmRenderer_Subsystem_UE>();
    if (NOT TestTrue(TEXT("the ISM renderer subsystem exists in editor worlds"), ck::IsValid(Subsystem)))
    { return true; }

    auto* Data = UCk_Utils_IsmRenderer_TransientFactory_UE::GetOrCreate_ForMesh(World, Mesh, ECk_Mobility::Movable);
    if (NOT TestTrue(TEXT("transient factory creates renderer data in the editor world"), ck::IsValid(Data)))
    { return true; }

    auto* Renderer = Subsystem->GetOrCreate_IsmRenderer(Data);
    if (NOT TestTrue(TEXT("an editor-world renderer was spawned"), ck::IsValid(Renderer)))
    { return true; }

    Subsystem->Request_AcquireProxyRef(Data);
    Subsystem->Request_ReleaseProxyRef(Data);

    TestEqual(TEXT("the editor-world proxy count returns to zero"), Subsystem->Get_ProxyRefCount(Data), 0);
    TestFalse(TEXT("an editor world never schedules a renderer teardown"), Subsystem->Get_IsPendingTeardown(Data));
    TestTrue(TEXT("the editor-world renderer survives its last release"),
        IsValid(Renderer) && Subsystem->Find_IsmRenderer(Data) == Renderer);

    return true;
}

#endif
