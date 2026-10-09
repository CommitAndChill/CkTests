#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_JoltSetupListener.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsEditor_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Settings/CkJolt_ProjectSettings.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"

#include <Engine/World.h>
#include <Misc/ScopeExit.h>
#include <UObject/StrongObjectPtr.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace ck_test_jolt_runtime_convex_editor
{
    static auto MakeSpec() -> FCk_JoltBody_Spec
    {
        auto Convex = FCk_JoltBody_RuntimeConvexSpec{};
        Convex.Set_PointsCm({FVector{-5,-5,-5}, FVector{5,-5,-5},
            FVector{5,5,-5}, FVector{-5,5,-5}, FVector{-5,-5,5},
            FVector{5,-5,5}, FVector{5,5,5}, FVector{-5,5,5}});
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::RuntimeConvex};
        Spec.Set_RuntimeConvex(Convex);
        Spec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
        Spec.Set_MassKg(1.0f);
        return Spec;
    }

    static auto Add(UWorld& InWorld, FCk_Handle& OutEntity) -> FCk_Handle_JoltBody
    {
        auto* Ecs = InWorld.GetSubsystem<UCk_EditorEcsWorld_Subsystem_UE>();
        if (ck::Is_NOT_Valid(Ecs))
        { return {}; }

        OutEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Ecs->Get_Registry());
        UCk_Utils_Transform_UE::Add(OutEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        return UCk_Utils_JoltBody_UE::Add(OutEntity, MakeSpec());
    }

    static auto Bind(UCk_Test_JoltSetupListener_UE& InListener) -> FCk_Delegate_JoltBody_OnSetupResolved
    {
        auto Delegate = FCk_Delegate_JoltBody_OnSetupResolved{};
        Delegate.BindUFunction(&InListener,
            GET_FUNCTION_NAME_CHECKED(UCk_Test_JoltSetupListener_UE, OnSetupResolved));
        return Delegate;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_JoltBody_RuntimeConvex_EditorWorld,
    "Ck.Jolt.Body.RuntimeConvex.EditorWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_JoltBody_RuntimeConvex_EditorWorld::RunTest(const FString&)
{
    using namespace ck_test_jolt_runtime_convex_editor;
    AddExpectedError(TEXT("RuntimeConvex Jolt world unavailable or closing"),
        EAutomationExpectedErrorFlags::Contains, -1);
    auto* Settings = GetMutableDefault<UCk_Jolt_ProjectSettings_UE>();
    if (NOT TestNotNull(TEXT("Jolt settings exist"), Settings))
    { return false; }
    const auto RestoreMode = Settings->Get_EditorStaticWorldMode();
    ON_SCOPE_EXIT { Settings->TestOnly_Set_EditorStaticWorldMode(RestoreMode); };

    Settings->TestOnly_Set_EditorStaticWorldMode(ECk_Jolt_EditorStaticWorldMode::Disabled);
    {
        auto* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("CkRuntimeConvexEditorOff"));
        if (NOT TestNotNull(TEXT("disabled editor world exists"), World))
        { return false; }
        ON_SCOPE_EXIT { World->DestroyWorld(false); };
        auto Entity = FCk_Handle{};
        auto Body = Add(*World, Entity);
        TestTrue(TEXT("unsupported editor world still yields observable body handle"), ck::IsValid(Body));
        TestEqual(TEXT("disabled editor world fails synchronously"),
            UCk_Utils_JoltBody_UE::Get_SetupFailure(Body), ECk_JoltBody_SetupFailure::UnsupportedWorld);
        auto Listener = TStrongObjectPtr{NewObject<UCk_Test_JoltSetupListener_UE>()};
        TestTrue(TEXT("late unsupported-world waiter accepted"),
            UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(Body, Bind(*Listener)));
        TestEqual(TEXT("late unsupported-world callback immediate"), Listener->_Count, 1);
    }

    Settings->TestOnly_Set_EditorStaticWorldMode(ECk_Jolt_EditorStaticWorldMode::LiveExtract);
    auto* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("CkRuntimeConvexEditorOn"));
    if (NOT TestNotNull(TEXT("enabled editor world exists"), World))
    { return false; }
    bool Destroyed = false;
    ON_SCOPE_EXIT
    {
        if (NOT Destroyed)
        { World->DestroyWorld(false); }
    };
    auto* Ecs = World->GetSubsystem<UCk_EditorEcsWorld_Subsystem_UE>();
    auto* Jolt = World->GetSubsystem<UCk_Jolt_Subsystem>();
    if (NOT TestNotNull(TEXT("editor ECS exists"), Ecs) ||
        NOT TestNotNull(TEXT("editor Jolt exists"), Jolt))
    { return false; }
    const auto Physics = Jolt->Get_PhysicsSystem().Pin();
    if (NOT TestTrue(TEXT("editor PhysicsSystem exists"), Physics.IsValid()))
    { return false; }
    const auto Baseline = Physics->GetNumBodies();

    auto ReadyEntity = FCk_Handle{};
    auto ReadyBody = Add(*World, ReadyEntity);
    Ecs->Tick(0.016f);
    TestEqual(TEXT("editor setup reached Ready"), UCk_Utils_JoltBody_UE::Get_SetupState(ReadyBody),
        ECk_JoltBody_SetupState::Ready);
    TestEqual(TEXT("editor body admitted"), Physics->GetNumBodies(), Baseline + 1);
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(ReadyEntity);
    Ecs->Tick(0.016f);
    TestEqual(TEXT("editor EndPlay releases body"), Physics->GetNumBodies(), Baseline);

    auto DestroyBeforeSetup = FCk_Handle{};
    auto DestroyBeforeSetupBody = Add(*World, DestroyBeforeSetup);
    auto DestroyListener = TStrongObjectPtr{NewObject<UCk_Test_JoltSetupListener_UE>()};
    TestTrue(TEXT("pre-destroy waiter accepted"),
        UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(DestroyBeforeSetupBody, Bind(*DestroyListener)));
    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(DestroyBeforeSetup);
    AddExpectedError(TEXT("setup promise rejected after BeginDestroy"),
        EAutomationExpectedErrorFlags::Contains, 0);
    TestFalse(TEXT("new waiter refused after BeginDestroy"),
        UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(DestroyBeforeSetupBody, Bind(*DestroyListener)));
    Ecs->Tick(0.016f);
    TestEqual(TEXT("pre-admission destruction cancels accepted waiter"), DestroyListener->_LastFailure,
        ECk_JoltBody_SetupFailure::Cancelled);
    TestEqual(TEXT("pre-admission destruction callback once"), DestroyListener->_Count, 1);
    TestEqual(TEXT("pre-admission destruction creates no body"), Physics->GetNumBodies(), Baseline);

    auto PendingEntity = FCk_Handle{};
    auto PendingBody = Add(*World, PendingEntity);
    auto Listener = TStrongObjectPtr{NewObject<UCk_Test_JoltSetupListener_UE>()};
    TestTrue(TEXT("pending editor waiter accepted"),
        UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(PendingBody, Bind(*Listener)));
    AddExpectedError(TEXT("setup promise rejected during world cleanup"),
        EAutomationExpectedErrorFlags::Contains, 0);
    Listener->_OnCallback = [this, World, PendingBody, RawListener = Listener.Get()]()
    {
        TestFalse(TEXT("cleanup callback cannot register a new waiter"),
            UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(PendingBody, Bind(*RawListener)));
        auto ReentrantEntity = FCk_Handle{};
        const auto Reentrant = Add(*World, ReentrantEntity);
        TestEqual(TEXT("cleanup callback cannot enqueue a new pending setup"),
            UCk_Utils_JoltBody_UE::Get_SetupFailure(Reentrant),
            ECk_JoltBody_SetupFailure::UnsupportedWorld);
    };
    World->DestroyWorld(false);
    Destroyed = true;
    TestEqual(TEXT("world close fires pending waiter once"), Listener->_Count, 1);
    TestEqual(TEXT("world close reports cancelled"), Listener->_LastFailure,
        ECk_JoltBody_SetupFailure::Cancelled);
    return true;
}

#endif
