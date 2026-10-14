#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"

#include "EngineUtils.h"
#include "Tests/AutomationCommon.h"
#include "UObject/WeakObjectPtr.h"

namespace ck_test_procedural_action_preview_world
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr float TickSeconds = 1.0f / 60.0f;
    constexpr int32 MaxSettlementTicks = 60;

    auto
        DoTickUntil(
            FTestWorldWrapper& InFixture,
            TFunctionRef<bool()> InCondition)
        -> bool
    {
        for (int32 TickIndex = 0; TickIndex < MaxSettlementTicks; ++TickIndex)
        {
            if (InCondition())
            { return true; }
            if (NOT InFixture.TickTestWorld(TickSeconds))
            { return false; }
        }
        return InCondition();
    }

    auto
        DoCountSchedulers(
            UWorld& InWorld)
        -> int32
    {
        auto Count = int32{0};
        for (TActorIterator<ACk_EcsWorld_Actor_UE> It{&InWorld}; It; ++It)
        { ++Count; }
        return Count;
    }

    auto
        DoGetFirstScheduler(
            UWorld& InWorld)
        -> TWeakObjectPtr<ACk_EcsWorld_Actor_UE>
    {
        for (TActorIterator<ACk_EcsWorld_Actor_UE> It{&InWorld}; It; ++It)
        { return TWeakObjectPtr<ACk_EcsWorld_Actor_UE>{*It}; }
        return {};
    }

    auto
        DoMakeFloorSpec()
        -> FCk_JoltBody_Spec
    {
        auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
        Shape.Set_HalfExtents(FVector{250.0, 250.0, 10.0});
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        return Spec;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewWorld_ProductionSettlementAndJolt,
    "Ck.ProceduralAnimation.Action.Editor.PreviewWorld.ProductionSettlementAndJolt",
    ck_test_procedural_action_preview_world::TestFlags)

auto
    FCkTest_ProceduralAction_PreviewWorld_ProductionSettlementAndJolt::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_preview_world;

    auto Fixture = FTestWorldWrapper{};
    if (NOT TestTrue(TEXT("private Game world created"), Fixture.CreateTestWorld(EWorldType::Game)))
    {
        Fixture.ForwardErrorMessages(this);
        return false;
    }

    auto* World = Fixture.GetTestWorld();
    if (NOT TestNotNull(TEXT("private world available"), World)
        || NOT TestTrue(TEXT("private world begins play"), Fixture.BeginPlayInTestWorld()))
    {
        Fixture.ForwardErrorMessages(this);
        return false;
    }

    auto* Ecs = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    auto* Jolt = World->GetSubsystem<UCk_Jolt_Subsystem>();
    if (NOT TestNotNull(TEXT("production ECS subsystem"), Ecs)
        || NOT TestNotNull(TEXT("production Jolt subsystem"), Jolt)
        || NOT TestTrue(TEXT("game world began play"), World->HasBegunPlay())
        || NOT TestTrue(TEXT("production scheduler actors spawned"), DoCountSchedulers(*World) > 0))
    { return false; }

    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto Transform = UCk_Utils_Transform_UE::Add(Entity, FTransform::Identity,
        ECk_Replication::DoesNotReplicate);
    if (NOT TestTrue(TEXT("preview subject composed"), ck::IsValid(Transform)))
    { return false; }

    const auto Target = FVector{80.0, 40.0, 120.0};
    UCk_Utils_Transform_UE::Request_SetTransform(Transform,
        FCk_Request_Transform_SetTransform{FTransform{Target}}, {});
    if (NOT TestTrue(TEXT("Transform request remains deferred before world tick"),
        UCk_Utils_Transform_UE::Get_EntityCurrentLocation(Transform).Equals(FVector::ZeroVector, 0.01)))
    { return false; }
    if (NOT TestTrue(TEXT("deferred Transform request settled by world ticks"),
        DoTickUntil(Fixture, [&]()
        {
            return UCk_Utils_Transform_UE::Get_EntityCurrentLocation(Transform).Equals(Target, 0.01);
        })))
    {
        Fixture.ForwardErrorMessages(this);
        return false;
    }

    auto FloorEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    UCk_Utils_Transform_UE::Add(FloorEntity, FTransform{FVector{0.0, 0.0, -10.0}},
        ECk_Replication::DoesNotReplicate);
    auto FloorBody = UCk_Utils_JoltBody_UE::Add(FloorEntity, DoMakeFloorSpec());
    if (NOT TestTrue(TEXT("static floor composed"), ck::IsValid(FloorBody))
        || NOT TestTrue(TEXT("static floor added through production scheduler"),
            DoTickUntil(Fixture, [&]() { return UCk_Utils_JoltBody_UE::Get_IsBodyAdded(FloorBody); })))
    {
        Fixture.ForwardErrorMessages(this);
        return false;
    }

    const auto Hit = UCk_Utils_JoltQuery_UE::Get_RayCast(World,
        FVector{0.0, 0.0, 100.0}, FVector{0.0, 0.0, -100.0}, FCk_Jolt_QueryFilter{});
    TestTrue(TEXT("production Jolt ray hits composed floor"), Hit.Get_HasHit());
    if (Hit.Get_HasHit())
    {
        TestTrue(TEXT("floor contact lies at its top surface"),
            FMath::IsNearlyEqual(Hit.Get_Position().Z, 0.0, 1.0));
    }

    const auto WeakWorld = TWeakObjectPtr<UWorld>{World};
    const auto WeakScheduler = DoGetFirstScheduler(*World);
    const auto StaleEntity = Entity;
    const auto StaleFloorBody = FloorBody;
    const auto Destroyed = Fixture.DestroyTestWorld(true);
    Fixture.ForwardErrorMessages(this);
    TestTrue(TEXT("private world destroyed and collected"), Destroyed && NOT WeakWorld.IsValid());
    TestFalse(TEXT("production scheduler actor collected"), WeakScheduler.IsValid());
    TestFalse(TEXT("subject handle invalid after world teardown"), ck::IsValid(StaleEntity));
    TestFalse(TEXT("Jolt floor handle invalid after world teardown"), ck::IsValid(StaleFloorBody));
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewWorld_TeardownDuringRebuildHold,
    "Ck.ProceduralAnimation.Action.Editor.PreviewWorld.TeardownDuringRebuildHold",
    ck_test_procedural_action_preview_world::TestFlags)

auto
    FCkTest_ProceduralAction_PreviewWorld_TeardownDuringRebuildHold::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_preview_world;

    auto Fixture = FTestWorldWrapper{};
    if (NOT TestTrue(TEXT("private Game world created"), Fixture.CreateTestWorld(EWorldType::Game))
        || NOT TestTrue(TEXT("private world begins play"), Fixture.BeginPlayInTestWorld()))
    {
        Fixture.ForwardErrorMessages(this);
        return false;
    }

    auto* World = Fixture.GetTestWorld();
    auto* Ecs = World->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
    if (NOT TestNotNull(TEXT("production ECS subsystem"), Ecs)
        || NOT TestTrue(TEXT("production scheduler actors spawned"), DoCountSchedulers(*World) > 0))
    { return false; }

    auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto Transform = UCk_Utils_Transform_UE::Add(Entity, FTransform::Identity,
        ECk_Replication::DoesNotReplicate);
    if (NOT TestTrue(TEXT("load-hold subject composed"), ck::IsValid(Transform)))
    { return false; }

    Ecs->Set_LoadHold(ECk_EcsWorld_LoadHold::Rebuilding);
    UCk_Utils_Transform_UE::Request_SetTransform(Transform,
        FCk_Request_Transform_SetTransform{FTransform{FVector{50.0, 0.0, 0.0}}}, {});
    TestTrue(TEXT("real ECS rebuild hold is active"), Ecs->Get_IsLoadGateActive());
    TestTrue(TEXT("world ticks under rebuild hold"), Fixture.TickTestWorld(TickSeconds));

    const auto WeakWorld = TWeakObjectPtr<UWorld>{World};
    const auto WeakScheduler = DoGetFirstScheduler(*World);
    const auto StaleEntity = Entity;
    const auto Destroyed = Fixture.DestroyTestWorld(true);
    Fixture.ForwardErrorMessages(this);
    TestTrue(TEXT("world under rebuild hold destroyed and collected"), Destroyed && NOT WeakWorld.IsValid());
    TestFalse(TEXT("scheduler under rebuild hold collected"), WeakScheduler.IsValid());
    TestFalse(TEXT("queued subject handle invalid after teardown"), ck::IsValid(StaleEntity));
    return NOT HasAnyErrors();
}

#endif
