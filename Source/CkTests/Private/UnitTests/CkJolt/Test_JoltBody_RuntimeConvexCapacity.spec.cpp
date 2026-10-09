#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_JoltSetupListener.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsEditor_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/CollisionLayers/CkJoltCollisionLayerTable.h"
#include "CkJolt/Settings/CkJolt_ProjectSettings.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"
#include "CkJolt/World/CkJoltWorld.h"

#include <Engine/World.h>
#include <Misc/ScopeExit.h>
#include <UObject/StrongObjectPtr.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace ck_test_jolt_runtime_convex_capacity
{
    static auto Add(UWorld& InWorld, FCk_Handle& OutEntity) -> FCk_Handle_JoltBody
    {
        auto* Ecs = InWorld.GetSubsystem<UCk_EditorEcsWorld_Subsystem_UE>();
        OutEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Ecs->Get_Registry());
        UCk_Utils_Transform_UE::Add(OutEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        auto Convex = FCk_JoltBody_RuntimeConvexSpec{};
        Convex.Set_PointsCm({FVector{-5,-5,-5}, FVector{5,-5,-5},
            FVector{5,5,-5}, FVector{-5,5,-5}, FVector{-5,-5,5},
            FVector{5,-5,5}, FVector{5,5,5}, FVector{-5,5,5}});
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::RuntimeConvex};
        Spec.Set_RuntimeConvex(Convex);
        Spec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
        Spec.Set_MassKg(1.0f);
        return UCk_Utils_JoltBody_UE::Add(OutEntity, Spec);
    }

    static auto Listen(const FCk_Handle_JoltBody& InBody,
        UCk_Test_JoltSetupListener_UE& InListener) -> bool
    {
        auto Delegate = FCk_Delegate_JoltBody_OnSetupResolved{};
        Delegate.BindUFunction(&InListener,
            GET_FUNCTION_NAME_CHECKED(UCk_Test_JoltSetupListener_UE, OnSetupResolved));
        return UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(InBody, Delegate);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_JoltBody_RuntimeConvex_BodyCapacity,
    "Ck.Jolt.Body.RuntimeConvex.BodyCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_JoltBody_RuntimeConvex_BodyCapacity::RunTest(const FString&)
{
    using namespace ck_test_jolt_runtime_convex_capacity;
    AddExpectedError(TEXT("CreateBody FAILED"), EAutomationExpectedErrorFlags::Contains, 0);
    auto* Settings = GetMutableDefault<UCk_Jolt_ProjectSettings_UE>();
    if (NOT TestNotNull(TEXT("Jolt settings exist"), Settings))
    { return false; }
    const auto RestoreMode = Settings->Get_EditorStaticWorldMode();
    ON_SCOPE_EXIT { Settings->TestOnly_Set_EditorStaticWorldMode(RestoreMode); };
    Settings->TestOnly_Set_EditorStaticWorldMode(ECk_Jolt_EditorStaticWorldMode::LiveExtract);

    auto* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("CkRuntimeConvexCapacity"));
    if (NOT TestNotNull(TEXT("capacity editor world exists"), World))
    { return false; }
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Ecs = World->GetSubsystem<UCk_EditorEcsWorld_Subsystem_UE>();
    auto* Jolt = World->GetSubsystem<UCk_Jolt_Subsystem>();
    if (NOT TestNotNull(TEXT("capacity ECS exists"), Ecs) ||
        NOT TestNotNull(TEXT("capacity Jolt exists"), Jolt))
    { return false; }

    auto Registry = Ecs->Get_Registry();
    auto* PhysicsContext = Registry.TryGetContext<TWeakPtr<JPH::PhysicsSystem>>();
    auto* WorldContext = Registry.TryGetContext<TSharedPtr<ck::FJoltWorld>>();
    if (NOT TestNotNull(TEXT("physics context exists"), PhysicsContext) ||
        NOT TestNotNull(TEXT("Jolt world context exists"), WorldContext))
    { return false; }
    auto& Layers = Jolt->Get_LayerTable();
    auto BroadPhase = ck::jolt::FCk_Jolt_BroadPhaseLayerInterface_Table{Layers};
    auto VsBroadPhase = ck::jolt::FCk_Jolt_ObjectVsBroadPhaseLayerFilter_Table{Layers};
    auto VsObject = ck::jolt::FCk_Jolt_ObjectLayerPairFilter_Table{Layers};
    auto Tiny = MakeShared<JPH::PhysicsSystem>();
    Tiny->Init(1, 0, 1024, 1024, BroadPhase, VsBroadPhase, VsObject);

    const auto OriginalPhysics = *PhysicsContext;
    const auto OriginalWorld = *WorldContext;
    *PhysicsContext = Tiny;
    WorldContext->Reset();
    ON_SCOPE_EXIT
    {
        *PhysicsContext = OriginalPhysics;
        *WorldContext = OriginalWorld;
    };

    auto FirstEntity = FCk_Handle{};
    auto First = Add(*World, FirstEntity);
    auto FirstListener = TStrongObjectPtr{NewObject<UCk_Test_JoltSetupListener_UE>()};
    TestTrue(TEXT("first capacity waiter accepted"), Listen(First, *FirstListener));
    Ecs->Tick(0.016f);
    TestEqual(TEXT("tiny system first body admitted"), Tiny->GetNumBodies(), 1u);
    TestEqual(TEXT("first setup ready"), UCk_Utils_JoltBody_UE::Get_SetupState(First),
        ECk_JoltBody_SetupState::Ready);

    auto SecondEntity = FCk_Handle{};
    auto Second = Add(*World, SecondEntity);
    auto SecondListener = TStrongObjectPtr{NewObject<UCk_Test_JoltSetupListener_UE>()};
    TestTrue(TEXT("second capacity waiter accepted"), Listen(Second, *SecondListener));
    Ecs->Tick(0.016f);
    TestEqual(TEXT("full tiny system reports terminal capacity"),
        UCk_Utils_JoltBody_UE::Get_SetupFailure(Second),
        ECk_JoltBody_SetupFailure::BodyCapacityExceeded);
    TestEqual(TEXT("capacity callback once"), SecondListener->_Count, 1);
    TestFalse(TEXT("second body never admitted"), UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Second));

    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(FirstEntity);
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(SecondEntity);
    Ecs->Tick(0.016f);
    TestEqual(TEXT("tiny system released admitted body"), Tiny->GetNumBodies(), 0u);
    return true;
}

#endif
