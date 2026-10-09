#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_JoltSetupListener.h"

#include "CkCore/Format/CkFormat.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include "CkJolt/Body/CkJoltBody_Fragment.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/CkJolt_Utils.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/World.h>
#include <UObject/StrongObjectPtr.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <limits>

namespace ck_test_jolt_runtime_convex
{
    static TArray<FCk_Handle> Entities;
    static TArray<FCk_Handle_JoltBody> Bodies;
    static TArray<TStrongObjectPtr<UCk_Test_JoltSetupListener_UE>> Listeners;
    static FVector ObservedOffOriginCom = FVector::ZeroVector;
    static float ObservedOffOriginMassKg = 0.0f;
    static FVector ObservedImpulseVelocity = FVector::ZeroVector;
    static bool ObservedOffOriginRayHit = false;
    static double ObservedGravitySettledZ = 0.0;

    static auto MakeCubeSpec() -> FCk_JoltBody_Spec
    {
        auto Convex = FCk_JoltBody_RuntimeConvexSpec{};
        Convex.Set_PointsCm({FVector{-5, -5, -5}, FVector{5, -5, -5},
            FVector{5, 5, -5}, FVector{-5, 5, -5}, FVector{-5, -5, 5},
            FVector{5, -5, 5}, FVector{5, 5, 5}, FVector{-5, 5, 5}});
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::RuntimeConvex};
        Spec.Set_RuntimeConvex(Convex);
        Spec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
        Spec.Set_MassKg(1.0f);
        return Spec;
    }

    static auto AddBody(UWorld* InWorld, const FCk_JoltBody_Spec& InSpec,
        const FVector& InScale = FVector::OneVector,
        const FVector& InLocation = FVector{1000.0, 0.0, 0.0},
        const FQuat& InRotation = FQuat::Identity) -> FCk_Handle_JoltBody
    {
        auto* Ecs = InWorld->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
        auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Ecs->Get_Registry());
        auto Transform = FTransform::Identity;
        Transform.SetRotation(InRotation);
        Transform.SetScale3D(InScale);
        Transform.SetLocation(InLocation);
        UCk_Utils_Transform_UE::Add(Entity, Transform, ECk_Replication::DoesNotReplicate);
        Entities.Add(Entity);
        return UCk_Utils_JoltBody_UE::Add(Entity, InSpec);
    }

    static auto Listen(const FCk_Handle_JoltBody& InBody) -> UCk_Test_JoltSetupListener_UE*
    {
        auto Listener = TStrongObjectPtr{NewObject<UCk_Test_JoltSetupListener_UE>()};
        auto* Raw = Listener.Get();
        FCk_Delegate_JoltBody_OnSetupResolved Delegate;
        Delegate.BindUFunction(Raw, GET_FUNCTION_NAME_CHECKED(UCk_Test_JoltSetupListener_UE, OnSetupResolved));
        Listeners.Add(MoveTemp(Listener));
        if (NOT UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(InBody, Delegate))
        { return nullptr; }
        return Raw;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_JoltBody_RuntimeConvex_Setup,
    "Ck.Jolt.Body.RuntimeConvex.Setup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_JoltBody_RuntimeConvex_Setup::RunTest(const FString&)
{
    using namespace ck_test_jolt_runtime_convex;
    Entities.Reset(); Bodies.Reset(); Listeners.Reset();
    ObservedOffOriginCom = FVector::ZeroVector;
    ObservedOffOriginMassKg = 0.0f;
    ObservedImpulseVelocity = FVector::ZeroVector;
    ObservedOffOriginRayHit = false;
    ObservedGravitySettledZ = 0.0;
    bSuppressLogWarnings = true;
    // Same whitelist as Ck.Jolt.Body.Lifecycle: /Engine/Maps/Entry's default brush can trip the static-world bake.
    AddExpectedError(TEXT("BodySetup"), EAutomationExpectedErrorFlags::Contains, -1);
    AddExpectedError(TEXT("JoltBody setup failed for Entity"),
        EAutomationExpectedErrorFlags::Contains, -1);
    AddExpectedError(TEXT("RuntimeConvex spec rejected"), EAutomationExpectedErrorFlags::Contains, -1);
    AddExpectedError(ck::Format_UE(TEXT("RuntimeConvex input exceeds {} points"), ck::jolt_body::MaxRuntimeConvexPoints),
        EAutomationExpectedErrorFlags::Contains, 0);
    AddExpectedError(TEXT("collision profile"), EAutomationExpectedErrorFlags::Contains, 0);
    AddExpectedError(TEXT("setup waiter limit reached"), EAutomationExpectedErrorFlags::Contains, 0);
    AddExpectedError(TEXT("NO StaticMesh set"), EAutomationExpectedErrorFlags::Contains, 0);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, FString{TEXT("/Engine/Maps/Entry")}));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World) -> void
        {
            auto Ground = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
            auto GroundBox = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
            GroundBox.Set_HalfExtents(FVector{250.0, 250.0, 10.0});
            Ground.Set_ShapeDimensions(GroundBox);
            Ground.Set_MotionType(ECk_MotionType::Static);
            const auto GroundBody = AddBody(World, Ground, FVector::OneVector, FVector{0.0, 0.0, -10.0});
            TestTrue(TEXT("ground body composed"), ck::IsValid(GroundBody));

            auto Valid = MakeCubeSpec();
            Bodies.Add(AddBody(World, Valid, FVector::OneVector, FVector{0.0, 0.0, 100.0}));
            TestEqual(TEXT("new convex body is Pending before setup"),
                UCk_Utils_JoltBody_UE::Get_SetupState(Bodies.Last()), ECk_JoltBody_SetupState::Pending);
            auto* ReadyListener = Listen(Bodies.Last());
            TestNotNull(TEXT("valid setup waiter accepted"), ReadyListener);
            if (ReadyListener != nullptr)
            {
                auto ContactDelegate = FCk_Delegate_JoltBody_OnContact{};
                ContactDelegate.BindUFunction(ReadyListener,
                    GET_FUNCTION_NAME_CHECKED(UCk_Test_JoltSetupListener_UE, OnContact));
                UCk_Utils_JoltBody_UE::BindTo_OnJoltBodyContactAdded(Bodies.Last(), ContactDelegate);
            }

            auto BadMass = MakeCubeSpec();
            BadMass.Set_MassKg(0.0f);
            Bodies.Add(AddBody(World, BadMass));
            TestNotNull(TEXT("bad mass waiter accepted"), Listen(Bodies.Last()));
            FCk_Delegate_JoltBody_OnSetupResolved ExtraWaiter;
            ExtraWaiter.BindUFunction(Listeners[1].Get(),
                GET_FUNCTION_NAME_CHECKED(UCk_Test_JoltSetupListener_UE, OnSetupResolved));
            bool WaitersAccepted = true;
            for (int32 Index = 1; Index < ck::jolt_body::MaxSetupWaiters; ++Index)
            { WaitersAccepted &= UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(Bodies[1], ExtraWaiter); }
            TestTrue(TEXT("setup waiters up to the cap accepted"), WaitersAccepted);
            TestFalse(TEXT("setup waiter past the cap refused without body failure"),
                UCk_Utils_JoltBody_UE::TryPromise_OnSetupResolved(Bodies[1], ExtraWaiter));

            auto BadPoints = MakeCubeSpec();
            auto Convex = BadPoints.Get_RuntimeConvex();
            Convex.Set_PointsCm({FVector::ZeroVector});
            BadPoints.Set_RuntimeConvex(Convex);
            Bodies.Add(AddBody(World, BadPoints));
            TestNotNull(TEXT("bad points waiter accepted"), Listen(Bodies.Last()));

            auto BadScale = MakeCubeSpec();
            Bodies.Add(AddBody(World, BadScale, FVector{2.0, 2.0, 2.0}));
            TestNotNull(TEXT("nonunit scale waiter accepted"), Listen(Bodies.Last()));

            auto BadProfile = MakeCubeSpec();
            BadProfile.Set_CollisionProfileName(TEXT("CkMissingRuntimeConvexProfile"));
            Bodies.Add(AddBody(World, BadProfile));
            TestNotNull(TEXT("invalid profile waiter accepted"), Listen(Bodies.Last()));

            auto Flat = MakeCubeSpec();
            auto FlatConvex = Flat.Get_RuntimeConvex();
            FlatConvex.Set_PointsCm({FVector{-5,-5,0}, FVector{5,-5,0},
                FVector{5,5,0}, FVector{-5,5,0}});
            Flat.Set_RuntimeConvex(FlatConvex);
            Bodies.Add(AddBody(World, Flat));
            TestNotNull(TEXT("coplanar hull waiter accepted"), Listen(Bodies.Last()));

            auto Sleeping = MakeCubeSpec();
            Sleeping.Set_InitialSleepState(ECk_Jolt_SleepState::Asleep);
            Bodies.Add(AddBody(World, Sleeping));
            TestNotNull(TEXT("sleeping batch waiter accepted"), Listen(Bodies.Last()));
            const auto Victim = Entities.Last();
            Listeners[0]->_OnCallback = [this, Victim]()
            {
                TestTrue(TEXT("both activation batches finalized before first callback"),
                    UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Bodies[6]));
                auto ToDestroy = Victim;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(ToDestroy);
            };

            auto Interior = MakeCubeSpec();
            auto InteriorSpec = Interior.Get_RuntimeConvex();
            auto InteriorPoints = InteriorSpec.Get_PointsCm();
            for (int32 Index = 0; Index < 300; ++Index)
            { InteriorPoints.Add(FVector{double(Index % 7) - 3.0,
                double((Index / 7) % 7) - 3.0, double((Index / 49) % 7) - 3.0}); }
            InteriorSpec.Set_PointsCm(InteriorPoints);
            Interior.Set_RuntimeConvex(InteriorSpec);
            Bodies.Add(AddBody(World, Interior));
            TestNotNull(TEXT("over-256 input with eight-point hull waiter accepted"), Listen(Bodies.Last()));

            auto Overflow = MakeCubeSpec();
            auto OverflowSpec = Overflow.Get_RuntimeConvex();
            auto SpherePoints = TArray<FVector>{};
            constexpr int32 SphereCount = 512;
            SpherePoints.Reserve(SphereCount);
            for (int32 Index = 0; Index < SphereCount; ++Index)
            {
                const auto Z = 1.0 - 2.0 * (double(Index) + 0.5) / SphereCount;
                const auto Radius = FMath::Sqrt(1.0 - Z * Z);
                const auto Angle = 2.399963229728653 * double(Index);
                SpherePoints.Add(FVector{1000.0 * Radius * FMath::Cos(Angle),
                    1000.0 * Radius * FMath::Sin(Angle), 1000.0 * Z});
            }
            OverflowSpec.Set_PointsCm(SpherePoints);
            OverflowSpec.Set_HullToleranceCm(1.e-7f);
            Overflow.Set_RuntimeConvex(OverflowSpec);
            Bodies.Add(AddBody(World, Overflow));
            TestNotNull(TEXT("over-256 final hull waiter accepted"), Listen(Bodies.Last()));

            auto Oversized = MakeCubeSpec();
            auto OversizedSpec = Oversized.Get_RuntimeConvex();
            auto OversizedPoints = TArray<FVector>{};
            OversizedPoints.SetNum(ck::jolt_body::MaxRuntimeConvexPoints + 1);
            OversizedSpec.Set_PointsCm(OversizedPoints);
            Oversized.Set_RuntimeConvex(OversizedSpec);
            Oversized.Set_InitialSleepState(ECk_Jolt_SleepState::Asleep);
            Bodies.Add(AddBody(World, Oversized));
            TestEqual(TEXT("oversized input fails before setup"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies.Last()),
                ECk_JoltBody_SetupFailure::InvalidInput);
            TestNotNull(TEXT("oversized late waiter accepted"), Listen(Bodies.Last()));

            auto OffOrigin = MakeCubeSpec();
            auto OffOriginSpec = OffOrigin.Get_RuntimeConvex();
            OffOriginSpec.Set_PointsCm({FVector{500.0, -400.0, 300.0},
                FVector{540.0, -400.0, 300.0}, FVector{500.0, -340.0, 300.0},
                FVector{500.0, -400.0, 380.0}});
            OffOrigin.Set_RuntimeConvex(OffOriginSpec);
            OffOrigin.Set_MassKg(2.5f);
            OffOrigin.Set_ComSource(ECk_JoltBody_ComSource::ExplicitOffset);
            OffOrigin.Set_ComOffset(FVector{3.0, 4.0, -2.0});
            OffOrigin.Set_GravityFactor(0.0f);
            Bodies.Add(AddBody(World, OffOrigin, FVector::OneVector, FVector::ZeroVector));
            TestNotNull(TEXT("off-origin COM waiter accepted"), Listen(Bodies.Last()));

            auto ShapeScale = MakeCubeSpec();
            ShapeScale.Set_ShapeScale(FVector{2.0, 1.0, 1.0});
            Bodies.Add(AddBody(World, ShapeScale));
            TestNotNull(TEXT("shape-scale waiter accepted"), Listen(Bodies.Last()));

            auto TinyMass = MakeCubeSpec();
            TinyMass.Set_MassKg(1.e-42f);
            Bodies.Add(AddBody(World, TinyMass));
            TestNotNull(TEXT("tiny positive mass waiter accepted"), Listen(Bodies.Last()));

            auto LegacyBox = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
            LegacyBox.Set_ShapeDimensions(FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box});
            Bodies.Add(AddBody(World, LegacyBox));
            TestNotNull(TEXT("legacy explicit-shape waiter accepted"), Listen(Bodies.Last()));

            auto LegacyMissingMesh = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::StaticMeshAsset};
            Bodies.Add(AddBody(World, LegacyMissingMesh));
            TestNotNull(TEXT("legacy missing-mesh waiter accepted"), Listen(Bodies.Last()));

            auto NonFinite = MakeCubeSpec();
            auto NonFiniteSpec = NonFinite.Get_RuntimeConvex();
            auto NonFinitePoints = NonFiniteSpec.Get_PointsCm();
            NonFinitePoints[0].X = std::numeric_limits<double>::quiet_NaN();
            NonFiniteSpec.Set_PointsCm(NonFinitePoints);
            NonFinite.Set_RuntimeConvex(NonFiniteSpec);
            Bodies.Add(AddBody(World, NonFinite));
            TestNotNull(TEXT("non-finite point waiter accepted"), Listen(Bodies.Last()));

            const auto NonUnitRotation = MakeCubeSpec();
            Bodies.Add(AddBody(World, NonUnitRotation, FVector::OneVector,
                FVector{1000.0, 0.0, 0.0}, FQuat{0.5, 0.0, 0.0, 0.5}));
            TestNotNull(TEXT("non-unit finite rotation waiter accepted"), Listen(Bodies.Last()));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(120));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World) -> void
        {
            auto* Jolt = World->GetSubsystem<UCk_Jolt_Subsystem>();
            const auto Physics = Jolt != nullptr ? Jolt->Get_PhysicsSystem().Pin() : TSharedPtr<JPH::PhysicsSystem>{};
            if (Bodies.Num() != 17 || NOT Physics.IsValid())
            { AddError(TEXT("off-origin COM body or PhysicsSystem unavailable")); return; }
            const auto BodyId = Bodies[10].Get<ck::FFragment_JoltBody>().Get_BodyId();
            const auto FallingId = Bodies[0].Get<ck::FFragment_JoltBody>().Get_BodyId();
            const auto FallingCom = Physics->GetBodyInterface().GetCenterOfMassPosition(FallingId);
            ObservedGravitySettledZ = FallingCom.GetZ();
            const auto Com = Physics->GetBodyInterface().GetCenterOfMassPosition(BodyId);
            ObservedOffOriginCom = FVector{Com.GetX(), Com.GetY(), Com.GetZ()};
            {
                const auto Lock = JPH::BodyLockRead{Physics->GetBodyLockInterface(), BodyId};
                if (NOT Lock.Succeeded() || Lock.GetBody().GetMotionProperties() == nullptr)
                { AddError(TEXT("off-origin body lock or motion properties unavailable")); return; }
                ObservedOffOriginMassKg = 1.0f / Lock.GetBody().GetMotionProperties()->GetInverseMass();
            }
            const auto Ray = JPH::RRayCast{ck::jolt::Conv(FVector{505.0, -395.0, 400.0}),
                ck::jolt::Conv(FVector{0.0, 0.0, -150.0})};
            auto Hit = JPH::RayCastResult{};
            ObservedOffOriginRayHit = Physics->GetNarrowPhaseQuery().CastRay(Ray, Hit) &&
                Hit.mBodyID == BodyId;
            Physics->GetBodyInterface().AddImpulse(BodyId, JPH::Vec3{10.0f, 0.0f, 0.0f});
            const auto Velocity = Physics->GetBodyInterface().GetLinearVelocity(BodyId);
            ObservedImpulseVelocity = FVector{Velocity.GetX(), Velocity.GetY(), Velocity.GetZ()};
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            if (Bodies.Num() != 17 || Listeners.Num() != 17)
            { return false; }
            bool Passed = TestEqual(TEXT("valid body ready"),
                UCk_Utils_JoltBody_UE::Get_SetupState(Bodies[0]), ECk_JoltBody_SetupState::Ready);
            Passed &= TestTrue(TEXT("valid body admitted"), UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Bodies[0]));
            Passed &= TestEqual(TEXT("valid waiter once"), Listeners[0]->_Count, 1);
            Passed &= TestTrue(TEXT("gravity moved valid convex onto ground"),
                ObservedGravitySettledZ > 4.0 && ObservedGravitySettledZ < 15.0);
            Passed &= TestTrue(TEXT("convex generated physical ground contact"),
                Listeners[0]->_ContactCount > 0);
            Passed &= TestEqual(TEXT("zero mass rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[1]), ECk_JoltBody_SetupFailure::InvalidMass);
            Passed &= TestEqual(TEXT("accepted waiters each resolved once"), Listeners[1]->_Count,
                ck::jolt_body::MaxSetupWaiters);
            Passed &= TestFalse(TEXT("zero mass has no body"), UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Bodies[1]));
            Passed &= TestEqual(TEXT("one-point cloud rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[2]), ECk_JoltBody_SetupFailure::InvalidInput);
            Passed &= TestEqual(TEXT("invalid waiter once"), Listeners[2]->_Count, 1);
            Passed &= TestEqual(TEXT("nonunit scale rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[3]), ECk_JoltBody_SetupFailure::InvalidScale);
            Passed &= TestEqual(TEXT("invalid collision profile rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[4]), ECk_JoltBody_SetupFailure::InvalidProfile);
            Passed &= TestEqual(TEXT("coplanar hull rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[5]), ECk_JoltBody_SetupFailure::HullFailed);
            Passed &= TestEqual(TEXT("second batch callback survived first callback teardown"),
                Listeners[6]->_Count, 1);
            Passed &= TestEqual(TEXT("second batch was Ready before teardown"),
                Listeners[6]->_LastState, ECk_JoltBody_SetupState::Ready);
            Passed &= TestEqual(TEXT("over-256 inputs with small final hull are allowed"),
                UCk_Utils_JoltBody_UE::Get_SetupState(Bodies[7]), ECk_JoltBody_SetupState::Ready);
            Passed &= TestEqual(TEXT("native final-hull cap is terminal"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[8]),
                ECk_JoltBody_SetupFailure::HullLimitExceeded);
            Passed &= TestFalse(TEXT("native hull failure has diagnostic"),
                UCk_Utils_JoltBody_UE::Get_SetupDiagnostic(Bodies[8]).IsEmpty());
            Passed &= TestEqual(TEXT("oversized point array fails before setup"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[9]), ECk_JoltBody_SetupFailure::InvalidInput);
            Passed &= TestEqual(TEXT("oversized late waiter immediate once"), Listeners[9]->_Count, 1);
            Passed &= TestEqual(TEXT("oversized failed body preserves sleeping tag"),
                UCk_Utils_JoltBody_UE::Get_SleepState(Bodies[9]), ECk_Jolt_SleepState::Asleep);
            Passed &= TestEqual(TEXT("off-origin hull with COM offset admitted"),
                UCk_Utils_JoltBody_UE::Get_SetupState(Bodies[10]), ECk_JoltBody_SetupState::Ready);
            Passed &= TestTrue(TEXT("asymmetric local COM plus explicit offset preserves body origin"),
                ObservedOffOriginCom.Equals(FVector{513.0, -381.0, 318.0}, 0.01));
            Passed &= TestTrue(TEXT("explicit mass reaches Jolt body"),
                FMath::IsNearlyEqual(ObservedOffOriginMassKg, 2.5f, 0.001f));
            Passed &= TestTrue(TEXT("ray query hits admitted off-origin hull"), ObservedOffOriginRayHit);
            Passed &= TestTrue(TEXT("impulse response uses explicit mass"),
                ObservedImpulseVelocity.Equals(FVector{4.0, 0.0, 0.0}, 0.01));
            Passed &= TestEqual(TEXT("non-unit shape-scale rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[11]), ECk_JoltBody_SetupFailure::InvalidScale);
            Passed &= TestEqual(TEXT("tiny positive mass with infinite inverse rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[12]), ECk_JoltBody_SetupFailure::InvalidMass);
            Passed &= TestEqual(TEXT("legacy explicit shape still reaches Ready"),
                UCk_Utils_JoltBody_UE::Get_SetupState(Bodies[13]), ECk_JoltBody_SetupState::Ready);
            Passed &= TestEqual(TEXT("legacy missing mesh resolves failed"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[14]), ECk_JoltBody_SetupFailure::ShapeFailed);
            Passed &= TestEqual(TEXT("non-finite point rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[15]), ECk_JoltBody_SetupFailure::InvalidInput);
            Passed &= TestEqual(TEXT("non-unit finite rotation rejected"),
                UCk_Utils_JoltBody_UE::Get_SetupFailure(Bodies[16]), ECk_JoltBody_SetupFailure::InvalidInput);
            Passed &= TestEqual(TEXT("non-unit rotation waiter resolved"), Listeners[16]->_Count, 1);
            auto* Late = Listen(Bodies[0]);
            Passed &= TestNotNull(TEXT("late waiter accepted"), Late);
            Passed &= TestEqual(TEXT("late waiter immediate"), Late != nullptr ? Late->_Count : 0, 1);
            return Passed;
        }), TEXT("runtime convex setup results")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
