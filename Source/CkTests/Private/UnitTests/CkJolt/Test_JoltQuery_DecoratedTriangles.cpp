#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Fragment.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Query/CkJoltOccupancy_Session.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

// Ground-nav triangle extraction must accept the axis-correcting decorator used by every upright capsule.
// A plain box does not reproduce this failure: GetTriangles* is a leaf-only Jolt API.
namespace ck_test_jolt_decorated_triangles
{
    static FCk_Handle_JoltBody GBody;
    const auto Centre = FVector{3000.0, -2000.0, 20000.0};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_JoltQuery_DecoratedTriangles,
    "Ck.Jolt.Query.DecoratedTriangles.WorldSpaceCapsule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_JoltQuery_DecoratedTriangles::RunTest(const FString& Parameters)
{
    using namespace ck_test_jolt_decorated_triangles;
    bSuppressLogWarnings = true;
    // Entry's unrelated brush has no runtime BodySetup; same fixture allowance as BoxOccupancy.
    AddExpectedError(TEXT("BodySetup"), EAutomationExpectedErrorFlags::Contains, -1);
    GBody = FCk_Handle_JoltBody{};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            auto* Ecs = InWorld->GetSubsystem<UCk_EcsWorld_Subsystem_UE>();
            if (NOT TestNotNull(TEXT("PIE ECS subsystem"), Ecs))
            { return; }
            auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Ecs->Get_Registry());
            UCk_Utils_Transform_UE::Add(Entity, FTransform{Centre}, ECk_Replication::DoesNotReplicate);
            auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
            Spec.Set_ShapeDimensions(FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Capsule});
            Spec.Set_MotionType(ECk_MotionType::Static);
            GBody = UCk_Utils_JoltBody_UE::Add(Entity, Spec);
            TestTrue(TEXT("static capsule composed"), ck::IsValid(GBody));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return ck::IsValid(GBody) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(GBody); }),
        15.0, TEXT("capsule body entered physics")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            if (NOT TestTrue(TEXT("capsule survives until query"), ck::IsValid(GBody)))
            { return; }
            auto Session = ck::jolt::FCk_Jolt_QuerySession{InWorld};
            const auto BodyId = static_cast<uint64>(GBody.Get<ck::FFragment_JoltBody>().Get_BodyId().GetIndexAndSequenceNumber());
            auto Whole = ck::jolt::FCk_Jolt_TriangleSoup{};
            auto Region = ck::jolt::FCk_Jolt_TriangleSoup{};
            const auto Bounds = FBox{Centre - FVector{300.0}, Centre + FVector{300.0}};
            const auto WholeCount = Session.Get_StaticBodyTriangles(BodyId, Whole);
            const auto RegionCount = Session.Get_StaticBodyTrianglesInAABox(BodyId, Bounds, Region);
            TestTrue(TEXT("decorated capsule supplies whole-body triangles"), WholeCount > 0);
            TestEqual(TEXT("enclosing region agrees with whole-body extraction"), RegionCount, WholeCount);
            TestEqual(TEXT("triangle count agrees with vertices"), Whole._Vertices.Num(), WholeCount * 3);
            auto Extent = FBox{ForceInit};
            for (const auto& Vertex : Whole._Vertices)
            {
                Extent += Vertex;
                if (NOT Bounds.IsInsideOrOn(Vertex))
                {
                    AddError(TEXT("decorator/body transform was lost: triangle is outside world-space bounds"));
                    break;
                }
            }
            // The upright wrap rotates Jolt's Y-axis capsule onto Z: its long axis must come out vertical, which the
            // bounds check alone cannot tell from a capsule still lying along Y.
            const auto Size = Extent.GetSize();
            TestTrue(TEXT("decorator rotation was applied: the capsule is tallest along Z"), Size.Z > Size.X && Size.Z > Size.Y);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
