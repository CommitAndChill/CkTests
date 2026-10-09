#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkRuntimeMesh/Display/CkRuntimeMeshDisplay_Fragment.h"
#include "CkRuntimeMesh/Display/CkRuntimeMeshDisplay_Processor.h"
#include "CkRuntimeMesh/Display/CkRuntimeMeshDisplay_Utils.h"

#include "CkCore/ObjectPooling/CkObjectPooling_Subsystem.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsEditor_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectIterator.h"
#include "Jolt/Physics/PhysicsSystem.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_runtime_mesh_display
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto CubePath = TEXT("/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU");
    constexpr auto MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
    constexpr auto RejectionText = TEXT("RuntimeMeshDisplay Add rejected");
    constexpr auto MaxSetupTicks = 8;

    auto Tick(UWorld& InWorld, int32 InCount = 1) -> void
    {
        auto* Ecs = InWorld.GetSubsystem<UCk_EditorEcsWorld_Subsystem_UE>();
        for (auto Index = 0; Index < InCount; ++Index)
        { Ecs->Tick(1.0f / 60.0f); }
    }

    auto AddGeometry(UWorld& InWorld, FCk_Handle& OutOwner) -> FCk_Handle_RuntimeMesh
    {
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, CubePath);
        if (ck::Is_NOT_Valid(Mesh))
        { return {}; }

        OutOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(&InWorld);
        auto Spec = FCk_RuntimeMesh_Spec{};
        Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{Mesh});
        return UCk_Utils_RuntimeMesh_UE::Add(OutOwner, Spec);
    }

    auto AddReadyGeometry(UWorld& InWorld, FCk_Handle& OutOwner) -> FCk_Handle_RuntimeMesh
    {
        auto Geometry = AddGeometry(InWorld, OutOwner);
        Tick(InWorld);
        FlushAsyncLoading();
        Tick(InWorld, 2);
        return Geometry;
    }

    auto MakeDisplaySpec(FCk_Handle_RuntimeMesh InGeometry) -> FCk_RuntimeMeshDisplay_Spec
    {
        auto* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
        auto Visuals = FCk_RuntimeMeshDisplay_Visuals{};
        Visuals.Set_Materials({TSoftObjectPtr<UMaterialInterface>{Material}, TSoftObjectPtr<UMaterialInterface>{Material}});
        auto Spec = FCk_RuntimeMeshDisplay_Spec{};
        Spec.Set_Geometry(InGeometry);
        Spec.Set_Visuals(Visuals);
        return Spec;
    }

    auto WithMaterials(
        FCk_RuntimeMeshDisplay_Spec InSpec,
        const TArray<TSoftObjectPtr<UMaterialInterface>>& InMaterials) -> FCk_RuntimeMeshDisplay_Spec
    {
        auto Visuals = InSpec.Get_Visuals();
        Visuals.Set_Materials(InMaterials);
        InSpec.Set_Visuals(Visuals);
        return InSpec;
    }

    auto NewDisplayOwner(UWorld& InWorld) -> FCk_Handle_Transform
    {
        auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(&InWorld);
        return UCk_Utils_Transform_UE::Add(Owner, FTransform::Identity, ECk_Replication::DoesNotReplicate);
    }

    auto TickUntilSettled(UWorld& InWorld, const FCk_Handle_RuntimeMeshDisplay& InDisplay) -> ECk_RuntimeMesh_SetupState
    {
        for (auto Index = 0; Index < MaxSetupTicks; ++Index)
        {
            FlushAsyncLoading();
            Tick(InWorld);

            const auto State = UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(InDisplay);
            if (State != ECk_RuntimeMesh_SetupState::Pending)
            { return State; }
        }

        return ECk_RuntimeMesh_SetupState::Pending;
    }

    // Every instance outered to the world, registered or not, so a leaked unregistered component counts too.
    auto CountLiveDisplayComponents(const UWorld& InWorld) -> int32
    {
        auto Count = 0;
        for (auto It = TObjectIterator<UDynamicMeshComponent>{}; It; ++It)
        {
            if (ck::IsValid(*It) && It->GetTypedOuter<UWorld>() == &InWorld)
            { ++Count; }
        }

        return Count;
    }

    auto ExpectRejected(
        FAutomationTestBase& InTest,
        FCk_Handle_Transform& InOwner,
        const FCk_RuntimeMeshDisplay_Spec& InSpec,
        const TCHAR* InCase) -> void
    {
        const auto Display = UCk_Utils_RuntimeMeshDisplay_UE::Add(InOwner, InSpec);
        InTest.TestFalse(FString::Printf(TEXT("%s: Add returns an invalid handle"), InCase), ck::IsValid(Display));
        InTest.TestFalse(FString::Printf(TEXT("%s: no display fragment"), InCase), UCk_Utils_RuntimeMeshDisplay_UE::Has(InOwner));
        InTest.TestFalse(FString::Printf(TEXT("%s: no setup fragment"), InCase), InOwner.Has<ck::FFragment_RuntimeMeshDisplay_Setup>());
        InTest.TestFalse(FString::Printf(TEXT("%s: no setup tag"), InCase), InOwner.Has<ck::FTag_RuntimeMeshDisplay_NeedsSetup>());
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_DisplayOwnership,
    "Ck.RuntimeMesh.Display.OwnershipAndTransform", ck_test_runtime_mesh_display::Flags)

bool FCkTest_RuntimeMesh_DisplayOwnership::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_display;

    auto* World = UWorld::CreateWorld(EWorldType::Editor, false);
    if (NOT TestNotNull(TEXT("isolated world"), World))
    { return false; }

    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    auto SourceOwner = FCk_Handle{};
    const auto Geometry = AddReadyGeometry(*World, SourceOwner);
    if (NOT TestTrue(TEXT("public import Ready"), ck::IsValid(Geometry) &&
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Geometry) == ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    auto* Pool = World->GetSubsystem<UCk_ObjectPooling_Subsystem_UE>();
    if (NOT TestNotNull(TEXT("pool exists"), Pool))
    { return false; }

    auto* Jolt = World->GetSubsystem<UCk_Jolt_Subsystem>();
    const auto Physics = ck::IsValid(Jolt) ? Jolt->Get_PhysicsSystem().Pin() : TSharedPtr<JPH::PhysicsSystem>{};
    const auto BodyBaseline = Physics.IsValid() ? Physics->GetNumBodies() : 0u;

    auto Owner = NewDisplayOwner(*World);
    const auto PinnedBaseline = Pool->Get_NumPinnedUnique();
    const auto Display = UCk_Utils_RuntimeMeshDisplay_UE::Add(Owner, MakeDisplaySpec(Geometry));
    if (NOT TestTrue(TEXT("display accepted"), ck::IsValid(Display)))
    { return false; }

    TestEqual(TEXT("deferred display setup"), UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(Display), ECk_RuntimeMesh_SetupState::Pending);

    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(SourceOwner);
    CollectGarbage(RF_NoFlags);
    TickUntilSettled(*World, Display);
    Tick(*World, 3);

    TestFalse(TEXT("source owner destroyed"), ck::IsValid(SourceOwner));
    TestFalse(TEXT("geometry entity destroyed"), ck::IsValid(Geometry));
    if (NOT TestEqual(TEXT("display Ready without geometry entity"),
        UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(Display), ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    const auto Component = Display.Get<ck::FFragment_RuntimeMeshDisplay>().Get_Component();
    if (NOT TestTrue(TEXT("rooted component exists"), Component.IsValid()))
    { return false; }

    const auto DynamicMesh = TWeakObjectPtr<UDynamicMesh>{Component->GetDynamicMesh()};
    TestEqual(TEXT("one component pool pin"), Pool->Get_NumPinnedUnique(), PinnedBaseline + 1);
    TestEqual(TEXT("Chaos collision disabled"), Component->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("no Chaos simulation"), Component->IsSimulatingPhysics());
    TestFalse(TEXT("complex collision disabled"), Component->bEnableComplexCollision);
    TestEqual(TEXT("native tangent overlays supplied"), Component->GetTangentsType(), EDynamicMeshComponentTangentsMode::ExternallyProvided);
    TestEqual(TEXT("two material slots"), Component->GetNumMaterials(), 2);
    if (Physics.IsValid())
    { TestEqual(TEXT("display did not create Jolt collision"), Physics->GetNumBodies(), BodyBaseline); }

    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("component survives forced GC"), Component.IsValid());
    TestTrue(TEXT("component-owned mesh survives forced GC"), DynamicMesh.IsValid());

    auto Transform = UCk_Utils_Transform_UE::CastChecked(Owner);
    const auto Location = FVector{41, -27, 13};
    UCk_Utils_Transform_UE::Request_SetLocation(Transform, FCk_Request_Transform_SetLocation{Location}, {});
    Tick(*World);
    TestTrue(TEXT("display observes settled same-frame transform"), Component->GetComponentLocation().Equals(Location, 0.001));

    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner);
    Tick(*World, 3);
    TestEqual(TEXT("teardown restores pool pins"), Pool->Get_NumPinnedUnique(), PinnedBaseline);

    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("component released"), Component.IsValid());
    TestFalse(TEXT("dynamic mesh released"), DynamicMesh.IsValid());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_DisplayRejection,
    "Ck.RuntimeMesh.Display.RejectionAndPendingTeardown", ck_test_runtime_mesh_display::Flags)

bool FCkTest_RuntimeMesh_DisplayRejection::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_display;

    auto* World = UWorld::CreateWorld(EWorldType::Editor, false);
    if (NOT TestNotNull(TEXT("isolated world"), World))
    { return false; }

    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    auto SourceOwner = FCk_Handle{};
    const auto Geometry = AddReadyGeometry(*World, SourceOwner);
    if (NOT TestTrue(TEXT("source ready"), ck::IsValid(Geometry) &&
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Geometry) == ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    auto* Pool = World->GetSubsystem<UCk_ObjectPooling_Subsystem_UE>();
    if (NOT TestNotNull(TEXT("pool exists"), Pool))
    { return false; }

    const auto PinnedBaseline = Pool->Get_NumPinnedUnique();
    const auto LiveBaseline = CountLiveDisplayComponents(*World);
    const auto ValidSpec = MakeDisplaySpec(Geometry);
    const auto& ValidMaterials = ValidSpec.Get_Visuals().Get_Materials();

    // A single ensure site reports every rejection, and an ignore-after-first ensure policy may
    // collapse repeats, so the per-case proof is the returned handle and the absent fragments.
    AddExpectedError(RejectionText, EAutomationExpectedErrorFlags::Contains, 0);

    auto InvalidGeometryOwner = NewDisplayOwner(*World);
    ExpectRejected(*this, InvalidGeometryOwner, MakeDisplaySpec(FCk_Handle_RuntimeMesh{}), TEXT("invalid geometry handle"));

    auto PendingGeometryOwner = FCk_Handle{};
    const auto PendingGeometry = AddGeometry(*World, PendingGeometryOwner);
    if (TestTrue(TEXT("unticked geometry is Pending"), ck::IsValid(PendingGeometry) &&
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(PendingGeometry) == ECk_RuntimeMesh_SetupState::Pending))
    {
        auto Owner = NewDisplayOwner(*World);
        ExpectRejected(*this, Owner, MakeDisplaySpec(PendingGeometry), TEXT("geometry not Ready"));
    }

    for (const auto Count : {0, 1, ck::runtimemesh::display::MaxMaterialSlots + 1})
    {
        auto Slots = ValidMaterials;
        Slots.SetNum(Count);
        for (auto Index = ValidMaterials.Num(); Index < Count; ++Index)
        { Slots[Index] = ValidMaterials[0]; }

        auto Owner = NewDisplayOwner(*World);
        ExpectRejected(*this, Owner, WithMaterials(ValidSpec, Slots),
            *FString::Printf(TEXT("%d material slots"), Count));
    }

    {
        auto Owner = NewDisplayOwner(*World);
        ExpectRejected(*this, Owner, WithMaterials(ValidSpec, {ValidMaterials[0], TSoftObjectPtr<UMaterialInterface>{}}),
            TEXT("null material soft reference"));
    }

    auto Owner = NewDisplayOwner(*World);
    const auto Pending = UCk_Utils_RuntimeMeshDisplay_UE::Add(Owner, ValidSpec);
    if (NOT TestTrue(TEXT("valid display accepted"), ck::IsValid(Pending)))
    { return false; }

    TestEqual(TEXT("pending before teardown"), UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(Pending), ECk_RuntimeMesh_SetupState::Pending);

    const auto Duplicate = UCk_Utils_RuntimeMeshDisplay_UE::Add(Owner, ValidSpec);
    TestFalse(TEXT("second display on one owner rejected"), ck::IsValid(Duplicate));
    TestTrue(TEXT("rejected duplicate leaves the first display's setup intact"),
        Owner.Has<ck::FFragment_RuntimeMeshDisplay_Setup>() && Owner.Has<ck::FTag_RuntimeMeshDisplay_NeedsSetup>());
    TestEqual(TEXT("rejected duplicate leaves the first display Pending"),
        UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(Pending), ECk_RuntimeMesh_SetupState::Pending);

    // Destruction runs EndPlay on the tick it is requested and destroys the entity two ticks later. A handle in
    // teardown is garbage to the public getters (they ensure), so the cancelled state is read off the fragment
    // through the pending-kill policy, which only a test or a teardown processor may do.
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner);
    Tick(*World);
    if (TestTrue(TEXT("EndPlay state still inspectable"), ck::IsValid(Pending, ck::IsValid_Policy_IncludePendingKill{})))
    {
        const auto& Cancelled = Pending.Get<ck::FFragment_RuntimeMeshDisplay, ck::IsValid_Policy_IncludePendingKill>();
        TestEqual(TEXT("accepted pending display cancelled"), Cancelled.Get_SetupState(), ECk_RuntimeMesh_SetupState::Failed);
        TestEqual(TEXT("cancellation classified"), Cancelled.Get_SetupFailure(), ECk_RuntimeMeshDisplay_SetupFailure::Cancelled);
        TestFalse(TEXT("no component was published"), Cancelled.Get_Component().IsValid());
    }

    Tick(*World, 2);
    TestFalse(TEXT("cancelled display entity destroyed"), ck::IsValid(Pending, ck::IsValid_Policy_IncludePendingKill{}));

    CollectGarbage(RF_NoFlags);
    TestEqual(TEXT("no pool pins on rejected or cancelled displays"), Pool->Get_NumPinnedUnique(), PinnedBaseline);
    TestEqual(TEXT("no components on rejected or cancelled displays"), CountLiveDisplayComponents(*World), LiveBaseline);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_DisplayWorldCleanup,
    "Ck.RuntimeMesh.Display.WorldCleanupRelease", ck_test_runtime_mesh_display::Flags)

bool FCkTest_RuntimeMesh_DisplayWorldCleanup::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_display;

    auto* World = UWorld::CreateWorld(EWorldType::Editor, false);
    if (NOT TestNotNull(TEXT("isolated world"), World))
    { return false; }

    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    auto SourceOwner = FCk_Handle{};
    const auto Geometry = AddReadyGeometry(*World, SourceOwner);
    if (NOT TestTrue(TEXT("source ready"), ck::IsValid(Geometry) &&
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Geometry) == ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    auto* Pool = World->GetSubsystem<UCk_ObjectPooling_Subsystem_UE>();
    if (NOT TestNotNull(TEXT("pool exists"), Pool))
    { return false; }

    CollectGarbage(RF_NoFlags);
    const auto PinnedBaseline = Pool->Get_NumPinnedUnique();
    const auto LiveBaseline = CountLiveDisplayComponents(*World);

    auto Owner = NewDisplayOwner(*World);
    const auto Display = UCk_Utils_RuntimeMeshDisplay_UE::Add(Owner, MakeDisplaySpec(Geometry));
    if (NOT TestTrue(TEXT("display accepted"), ck::IsValid(Display))
        || NOT TestEqual(TEXT("display Ready"), TickUntilSettled(*World, Display), ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    const auto Component = Display.Get<ck::FFragment_RuntimeMeshDisplay>().Get_Component();
    if (NOT TestTrue(TEXT("component published"), Component.IsValid() && Component->IsRegistered()))
    { return false; }

    TestEqual(TEXT("one pool pin while displayed"), Pool->Get_NumPinnedUnique(), PinnedBaseline + 1);
    TestEqual(TEXT("one live component while displayed"), CountLiveDisplayComponents(*World), LiveBaseline + 1);

    ck::runtimemesh::display::ReleaseWorld(World);

    TestEqual(TEXT("world cleanup unpins the component"), Pool->Get_NumPinnedUnique(), PinnedBaseline);
    TestFalse(TEXT("display no longer references a component"),
        Display.Get<ck::FFragment_RuntimeMeshDisplay>().Get_Component().IsValid());

    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("world cleanup destroys the component"), Component.IsValid());
    TestEqual(TEXT("no live component after world cleanup"), CountLiveDisplayComponents(*World), LiveBaseline);

    ck::runtimemesh::display::ReleaseWorld(World);
    TestEqual(TEXT("a repeated world cleanup is a no-op"), Pool->Get_NumPinnedUnique(), PinnedBaseline);

    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner);
    Tick(*World, 3);
    TestFalse(TEXT("owner destroyed after world cleanup"), ck::IsValid(Owner, ck::IsValid_Policy_IncludePendingKill{}));
    TestEqual(TEXT("EndPlay after world cleanup releases nothing twice"), Pool->Get_NumPinnedUnique(), PinnedBaseline);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_DisplayChurn,
    "Ck.RuntimeMesh.Display.CreateDestroyChurn", ck_test_runtime_mesh_display::Flags)

bool FCkTest_RuntimeMesh_DisplayChurn::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_display;
    constexpr auto Iterations = 8;

    auto* World = UWorld::CreateWorld(EWorldType::Editor, false);
    if (NOT TestNotNull(TEXT("isolated world"), World))
    { return false; }

    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    auto SourceOwner = FCk_Handle{};
    const auto Geometry = AddReadyGeometry(*World, SourceOwner);
    if (NOT TestTrue(TEXT("source ready"), ck::IsValid(Geometry) &&
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Geometry) == ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    auto* Pool = World->GetSubsystem<UCk_ObjectPooling_Subsystem_UE>();
    if (NOT TestNotNull(TEXT("pool exists"), Pool))
    { return false; }

    const auto Spec = MakeDisplaySpec(Geometry);
    CollectGarbage(RF_NoFlags);
    const auto PinnedBaseline = Pool->Get_NumPinnedUnique();
    const auto LiveBaseline = CountLiveDisplayComponents(*World);

    for (auto Iteration = 0; Iteration < Iterations; ++Iteration)
    {
        auto Owner = NewDisplayOwner(*World);
        const auto Display = UCk_Utils_RuntimeMeshDisplay_UE::Add(Owner, Spec);
        if (NOT TestTrue(*FString::Printf(TEXT("iteration %d: display accepted"), Iteration), ck::IsValid(Display))
            || NOT TestEqual(*FString::Printf(TEXT("iteration %d: display Ready"), Iteration),
                TickUntilSettled(*World, Display), ECk_RuntimeMesh_SetupState::Ready))
        { return false; }

        TestEqual(*FString::Printf(TEXT("iteration %d: one pool pin while displayed"), Iteration),
            Pool->Get_NumPinnedUnique(), PinnedBaseline + 1);
        TestEqual(*FString::Printf(TEXT("iteration %d: one live component while displayed"), Iteration),
            CountLiveDisplayComponents(*World), LiveBaseline + 1);

        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner);
        Tick(*World, 3);
        CollectGarbage(RF_NoFlags);

        TestEqual(*FString::Printf(TEXT("iteration %d: pool pins back to baseline"), Iteration),
            Pool->Get_NumPinnedUnique(), PinnedBaseline);
        TestEqual(*FString::Printf(TEXT("iteration %d: live components back to baseline"), Iteration),
            CountLiveDisplayComponents(*World), LiveBaseline);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_DisplayTangents,
    "Ck.RuntimeMesh.Display.DegenerateUvTangents", ck_test_runtime_mesh_display::Flags)

bool FCkTest_RuntimeMesh_DisplayTangents::RunTest(const FString&)
{
    using namespace ck_test_runtime_mesh_display;

    auto* Asset = LoadObject<UStaticMesh>(nullptr, CubePath);
    if (NOT TestNotNull(TEXT("fixture"), Asset))
    { return false; }

    const auto Imported = ck::runtimemesh::geometry::Import(*Asset, {});
    if (NOT TestTrue(TEXT("native import Ready"), Imported.Get_IsReady()))
    { return false; }

    const auto& Source = Imported.Get_Geometry()->Get_Mesh();
    for (const auto MissingUV : {false, true})
    {
        auto Copy = Source;
        if (MissingUV)
        { Copy.Attributes()->SetNumUVLayers(0); }
        else
        {
            auto* UV = Copy.Attributes()->PrimaryUV();
            for (const auto ElementID : UV->ElementIndicesItr())
            { UV->SetElement(ElementID, FVector2f::ZeroVector); }
        }

        auto* Normals = Copy.Attributes()->PrimaryNormals();
        for (const auto ElementID : Normals->ElementIndicesItr())
        { Normals->SetElement(ElementID, Normals->GetElement(ElementID) * 10.0f); }

        TestTrue(TEXT("native tangent adapter handles missing/degenerate UV"), ck::runtimemesh::display::PrepareTangents(Copy));

        for (const auto TriangleID : Copy.TriangleIndicesItr())
        {
            const auto NormalIDs = Normals->GetTriangle(TriangleID);
            const auto TangentIDs = Copy.Attributes()->PrimaryTangents()->GetTriangle(TriangleID);
            const auto BitangentIDs = Copy.Attributes()->PrimaryBiTangents()->GetTriangle(TriangleID);
            for (auto Corner = 0; Corner < 3; ++Corner)
            {
                const auto N = Normals->GetElement(NormalIDs[Corner]).GetSafeNormal();
                const auto T = Copy.Attributes()->PrimaryTangents()->GetElement(TangentIDs[Corner]);
                const auto B = Copy.Attributes()->PrimaryBiTangents()->GetElement(BitangentIDs[Corner]);
                TestTrue(TEXT("finite unit tangent frame"), NOT T.ContainsNaN() && NOT B.ContainsNaN() &&
                    FMath::IsNearlyEqual(T.SizeSquared(), 1.0f, 0.001f) && FMath::IsNearlyEqual(B.SizeSquared(), 1.0f, 0.001f) &&
                    FMath::Abs(FVector3f::DotProduct(N, T)) < 0.001f && FMath::Abs(FVector3f::DotProduct(N, B)) < 0.001f &&
                    FMath::Abs(FVector3f::DotProduct(FVector3f::CrossProduct(T, B), N)) > 0.99f);
            }
        }

        TestEqual(TEXT("no fabricated UV layer"), Copy.Attributes()->NumUVLayers(), MissingUV ? 0 : 2);
    }

    TestEqual(TEXT("source overlays unchanged"), Source.Attributes()->NumUVLayers(), 2);
    TestFalse(TEXT("source has no generated tangents"), Source.Attributes()->HasTangentSpace());
    return true;
}

#endif
