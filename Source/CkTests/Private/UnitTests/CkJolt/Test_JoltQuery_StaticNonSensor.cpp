#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkJolt/CollisionLayers/CkJoltCollisionLayer_Utils.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/World.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_JoltQuery_StaticNonSensorClosest,
    "Ck.Jolt.Query.StaticNonSensorClosest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkTest_JoltQuery_StaticNonSensorClosest::
    RunTest(const FString& Parameters)
    -> bool
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* Jolt = InServer->GetSubsystem<UCk_Jolt_Subsystem>();
            const auto Physics = ck::IsValid(Jolt) ? Jolt->Get_PhysicsSystem().Pin() : nullptr;
            if (NOT TestTrue(TEXT("Jolt world available"), Physics.IsValid()))
            { return; }

            const auto StaticSignature = ck::jolt::TryDerive_SignatureFromProfile(
                TEXT("BlockAll"), ECk_Jolt_BodyDomain::Static);
            const auto DynamicSignature = ck::jolt::TryDerive_SignatureFromProfile(
                TEXT("BlockAll"), ECk_Jolt_BodyDomain::Dynamic);
            if (NOT TestTrue(TEXT("BlockAll static signature"), StaticSignature.IsSet()) ||
                NOT TestTrue(TEXT("BlockAll dynamic signature"), DynamicSignature.IsSet()))
            { return; }

            const auto StaticLayer = JPH::ObjectLayer{Jolt->Get_LayerTable().Get_OrRegisterLayer(*StaticSignature)};
            const auto DynamicLayer = JPH::ObjectLayer{Jolt->Get_LayerTable().Get_OrRegisterLayer(*DynamicSignature)};
            auto Shape = JPH::Ref<JPH::Shape>{new JPH::BoxShape{JPH::Vec3{20.0f, 20.0f, 20.0f}}};
            auto& Bodies = Physics->GetBodyInterface();

            auto AddBox = [&](float InX, JPH::EMotionType InMotion, JPH::ObjectLayer InLayer,
                bool InSensor) -> JPH::BodyID
            {
                auto Settings = JPH::BodyCreationSettings{Shape.GetPtr(),
                    JPH::RVec3{InX, 0.0, 20000.0}, JPH::Quat::sIdentity(), InMotion, InLayer};
                Settings.mIsSensor = InSensor;
                Settings.mGravityFactor = 0.0f;
                return Bodies.CreateAndAddBody(Settings, JPH::EActivation::DontActivate);
            };

            const auto Sensor = AddBox(150.0f, JPH::EMotionType::Static, StaticLayer, true);
            const auto Dynamic = AddBox(300.0f, JPH::EMotionType::Dynamic, DynamicLayer, false);
            const auto Wall = AddBox(500.0f, JPH::EMotionType::Static, StaticLayer, false);
            if (NOT TestTrue(TEXT("sensor, dynamic blocker, and static wall created"),
                !Sensor.IsInvalid() && !Dynamic.IsInvalid() && !Wall.IsInvalid()))
            {
                for (const auto Id : {Sensor, Dynamic, Wall})
                {
                    if (Id.IsInvalid())
                    { continue; }
                    Bodies.RemoveBody(Id);
                    Bodies.DestroyBody(Id);
                }
                return;
            }

            auto Sphere = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Sphere};
            Sphere.Set_Radius(10.0f);
            const auto Start = FVector{0.0, 0.0, 20000.0};
            const auto End = FVector{600.0, 0.0, 20000.0};

            const auto DefaultHit = UCk_Utils_JoltQuery_UE::Get_ShapeCast(
                InServer, Start, End, FRotator::ZeroRotator, Sphere, FCk_Jolt_QueryFilter{});
            TestTrue(TEXT("default filter still sees a nearer body"),
                DefaultHit.Get_HasHit() && DefaultHit.Get_Fraction() < 0.6f);

            auto WorldOnly = FCk_Jolt_QueryFilter{};
            WorldOnly.Set_BodySelection(ECk_Jolt_QueryBodySelection::StaticNonSensor);
            WorldOnly.Set_CollideWithBackFacesConvex(true);
            const auto WallHit = UCk_Utils_JoltQuery_UE::Get_ShapeCast(
                InServer, Start, End, FRotator::ZeroRotator, Sphere, WorldOnly);
            TestTrue(TEXT("static non-sensor policy skips sensor and dynamic body to reach wall"),
                WallHit.Get_HasHit() && WallHit.Get_Fraction() > 0.7f && WallHit.Get_Fraction() < 0.9f);

            const auto StationaryWall = UCk_Utils_JoltQuery_UE::Get_ShapeCast(
                InServer, FVector{500.0, 0.0, 20000.0}, FVector{500.0, 0.0, 20000.0},
                FRotator::ZeroRotator, Sphere, WorldOnly);
            TestTrue(TEXT("stationary sphere inside static wall reports initial overlap"),
                StationaryWall.Get_HasHit() && StationaryWall.Get_Fraction() == 0.0f);

            const auto StationarySensor = UCk_Utils_JoltQuery_UE::Get_ShapeCast(
                InServer, FVector{150.0, 0.0, 20000.0}, FVector{150.0, 0.0, 20000.0},
                FRotator::ZeroRotator, Sphere, WorldOnly);
            TestFalse(TEXT("stationary sphere inside static probe sensor is clear"),
                StationarySensor.Get_HasHit());

            for (const auto Id : {Sensor, Dynamic, Wall})
            {
                Bodies.RemoveBody(Id);
                Bodies.DestroyBody(Id);
            }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
