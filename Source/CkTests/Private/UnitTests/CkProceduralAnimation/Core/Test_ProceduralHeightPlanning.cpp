#include "CkProceduralAnimation/Core/CkProceduralBodyEnvelope.h"
#include "CkProceduralAnimation/Core/CkProceduralBodySupport.h"
#include "CkProceduralAnimation/Core/CkProceduralHeightPlanning.h"
#include "CkProceduralAnimation/Core/CkProceduralFeetPlane.h"
#include "CkProceduralAnimation/Core/CkProceduralFoothold.h"
#include "CkProceduralAnimation/Core/CkProceduralFootProbe.h"
#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"
#include "CkProceduralAnimation/Core/CkProceduralSurfaceMotion.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_HeightPlanning.h"

#include "../../CkUnitTest_Common.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

#if WITH_EDITOR
#include "CkJolt/CollisionLayers/CkJoltCollisionLayer_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/BodyPose/CkProceduralBodyPose_Utils.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Fragment.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Processor.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Utils.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/World.h>
#include <Misc/ScopeExit.h>
#include <Misc/App.h>
#include <Misc/OutputDevice.h>
#include <Misc/OutputDeviceRedirector.h>
#include <Misc/ScopeLock.h>
#include <UObject/StrongObjectPtr.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#endif

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningBeamStanceTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.BeamFloorGoalPreservesSimulationAndPosedAnchors",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningBeamStanceTest::
    RunTest(const FString&)
    -> bool
{
    const auto Body = FTransform{FVector{0.0, 0.0, 124.0}};
    const auto Pose = FTransform{FVector{0.0, 0.0, 2.0}};
    const auto Hip = FVector{45.0, 28.0, 0.0};
    const auto Floor = FVector{50.0, 55.0, 0.0};
    const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{45.0, 15.0, 60.0}, Hip, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-45.0, -28.0, 150.0}, FVector{-45.0, -28.0, 0.0}, 70.0f}};
    const auto WithoutPose = ck::Get_ProceduralHeightPlanningInterval(Body, {}, FVector::UpVector,
        Anchors, Hip, Floor, 92.0f, 60.0);
    const auto Interval = ck::Get_ProceduralHeightPlanningInterval(Body, TOptional<FTransform>{Pose}, FVector::UpVector,
        Anchors, Hip, Floor, 92.0f, 60.0);
    TestTrue(TEXT("Both the simulation-only and coupled interval exist"), WithoutPose.IsSet() && Interval.IsSet());
    if (NOT WithoutPose.IsSet() || NOT Interval.IsSet())
    { return false; }
    TestTrue(TEXT("The prospective floor contact requires a downward goal"), Interval->Y < 0.0);
    TestTrue(TEXT("The elevated trusted plant bounds downward travel in the simulation frame"),
        FMath::IsNearlyEqual(Interval->X, -44.001, 1.0e-4));
    TestTrue(TEXT("The raised posed hip independently tightens the floor-contact upper bound"),
        FMath::IsNearlyEqual(WithoutPose->Y - Interval->Y, 2.0, 1.0e-4));
    constexpr auto Drop = -39.0;
    TestTrue(TEXT("A root height of 85 cm belongs to the coupled interval"), Interval->X <= Drop && Interval->Y >= Drop);
    const auto Goal = FTransform{FVector{0.0, 0.0, 124.0 + Drop}};
    const auto PosedGoal = Pose * Goal;
    TestTrue(TEXT("The prospective floor point fits the unchanged 0.92 front-chain bound in both frames"),
        FVector::Dist(Goal.TransformPosition(Hip), Floor) <= 92.0
            && FVector::Dist(PosedGoal.TransformPosition(Hip), Floor) <= 92.0);
    for (const auto& Anchor : Anchors)
    {
        TestTrue(TEXT("Every existing anchor remains within its actual chain in both frames"),
            FVector::Dist(Goal.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld()) <= Anchor.Get_Reach()
                && FVector::Dist(PosedGoal.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld()) <= Anchor.Get_Reach());
    }
    TestTrue(TEXT("The level authored box has geometric room above the beam; native collision proof is separate"),
        Goal.GetLocation().Z - 22.0 > 60.0 && PosedGoal.GetLocation().Z - 22.0 > 60.0);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningBeamUnseededStanceTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.BeamStanceGoalIsNotRestrictedByFixedFloorSeed",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningBeamUnseededStanceTest::
    RunTest(const FString&)
    -> bool
{
    const auto Body = FTransform{FVector{0.0, 0.0, 124.0}};
    const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{45.0, -15.0, 60.0}, FVector{45.0, -28.0, 0.0}, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{45.0, 15.0, 60.0}, FVector{45.0, 28.0, 0.0}, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-45.0, -15.0, 60.0}, FVector{-45.0, -28.0, 0.0}, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-45.0, 15.0, 60.0}, FVector{-45.0, 28.0, 0.0}, 100.0f}};
    constexpr auto RequiredRootZ = 83.15;
    const auto RequiredTranslation = RequiredRootZ - Body.GetLocation().Z;
    const auto Stance = ck::Get_ProceduralHeightStanceInterval(Body, {}, FVector::UpVector, Anchors, 64.0);
    TestTrue(TEXT("The four current physical chains admit the required beam-height goal"),
        Stance.IsSet() && Stance->X <= RequiredTranslation && Stance->Y >= RequiredTranslation);
    const auto Goal = FTransform{FVector{0.0, 0.0, RequiredRootZ}};
    for (const auto& Anchor : Anchors)
    {
        TestTrue(TEXT("Each uniform 100 cm trusted chain fits before and at the proposed height"),
            FVector::Dist(Body.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld()) <= 100.0
                && FVector::Dist(Goal.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld()) <= 100.0);
    }

    // The old cached seed came from a query hip+(30,27,-64). Its floor hit preserves that query's XY. A goal must
    // receive a fresh production foothold search; this stored floor point is not a bound on every possible landing.
    const auto Hip = FVector{45.0, 28.0, 0.0};
    const auto SeedQuery = Body.TransformPosition(Hip) + FVector{30.0, 27.0, -64.0};
    const auto FixedFloorSeed = FVector{SeedQuery.X, SeedQuery.Y, 0.0};
    const auto SeedBound = ck::Get_ProceduralHeightPlanningInterval(Body, {}, FVector::UpVector,
        Anchors, Hip, FixedFloorSeed, 92.0f, 64.0);
    TestTrue(TEXT("The old fixed floor seed has an interval, but excludes the required stance-feasible goal"),
        SeedBound.IsSet() && SeedBound->Y < RequiredTranslation);
    TestTrue(TEXT("The fixed seed exceeds its unchanged 0.92 admission reach at that goal"),
        FVector::Dist(Goal.TransformPosition(Hip), FixedFloorSeed) > 92.0);
    for (const auto InvalidReach : {0.0f, -1.0f})
    {
        const auto InvalidAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
            ck::FProceduralSurfaceReachPaceAnchor{Anchors[0].Get_FootWorld(), Anchors[0].Get_HipLocal(), InvalidReach}};
        TestFalse(TEXT("Raw non-positive current reach rejects before physical tolerance is added"),
            ck::Get_ProceduralHeightStanceInterval(Body, {}, FVector::UpVector, InvalidAnchors, 64.0).IsSet());
    }

    // Measured accepted Posts solve 1380: a fixed native floor observation is only the seed for the
    // production ClampTowardHip replay. A proposal must not pretend that seed itself was admitted.
    const auto PostsBody = FTransform{FVector{-263.310203953, 0.0, 154.0}};
    const auto PostsHip = FVector{45.0, -28.0, 0.0};
    const auto PostsSeed = FVector{-200.5390625, -39.28125, 0.0};
    const auto PostsAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-273.734375, -81.1875, 90.0}, PostsHip, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-266.21875, 81.0625, 90.0}, FVector{45.0, 28.0, 0.0}, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-303.703125, -90.84375, 90.0}, FVector{-45.0, -28.0, 0.0}, 120.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-300.0078125, 91.15625, 90.0}, FVector{-45.0, 28.0, 0.0}, 120.0f}};
    TestFalse(TEXT("The measured fixed floor seed cannot fit the unchanged 64 cm translation band"),
        ck::Get_ProceduralHeightPlanningInterval(PostsBody, TOptional<FTransform>{FTransform::Identity}, FVector::UpVector,
            PostsAnchors, PostsHip, PostsSeed, 92.0f, 64.0).IsSet());
    const auto Reprojection = ck::Get_ProceduralHeightReprojectionDelta(PostsBody,
        TOptional<FTransform>{FTransform::Identity}, FVector::UpVector, PostsAnchors, PostsHip, PostsSeed, 92.0f, 64.0);
    TestTrue(TEXT("A native reprojection may be proposed at exactly the unchanged lower translation bound"),
        Reprojection.IsSet() && FMath::IsNearlyEqual(Reprojection.GetValue(), -64.0, 1.0e-9));
    if (NOT Reprojection.IsSet())
    { return false; }
    auto PostsGoal = PostsBody;
    PostsGoal.AddToTranslation(FVector::UpVector * Reprojection.GetValue());
    for (const auto& Anchor : PostsAnchors)
    {
        TestTrue(TEXT("The proposed height retains each measured physical stance contact"),
            FVector::Dist(PostsGoal.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld()) <= Anchor.Get_Reach());
    }
    TestTrue(TEXT("The proposal does not turn the still-unreachable fixed seed into an admitted landing"),
        FVector::Dist(PostsGoal.TransformPosition(PostsHip), PostsSeed) > 92.0);
    const auto RaisedPose = TOptional<FTransform>{FTransform{FVector{0.0, 0.0, 20.0}}};
    const auto RaisedProposal = ck::Get_ProceduralHeightReprojectionDelta(PostsBody, RaisedPose, FVector::UpVector,
        PostsAnchors, PostsHip, PostsSeed, 92.0f, 64.0);
    TestTrue(TEXT("A raised presentation pose still receives a bounded proposal rather than landing permission"),
        RaisedProposal.IsSet() && FMath::Abs(RaisedProposal.GetValue()) <= 64.0);
    if (NOT RaisedProposal.IsSet())
    { return false; }
    auto RaisedGoal = PostsBody;
    RaisedGoal.AddToTranslation(FVector::UpVector * RaisedProposal.GetValue());
    const auto RaisedPresentation = RaisedPose.GetValue() * RaisedGoal;
    for (const auto& Anchor : PostsAnchors)
    {
        TestTrue(TEXT("The fallback proposal retains all measured contacts in both body frames"),
            FVector::Dist(RaisedGoal.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld()) <= Anchor.Get_Reach()
                && FVector::Dist(RaisedPresentation.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld())
                    <= Anchor.Get_Reach());
    }

    // The returned point was native Usable in solve1378; using it here is fixed-point proposal geometry only.
    const auto MeasuredBody = FTransform{FVector{-263.310319812, 0.0, 154.0}};
    const auto ReturnedNativePoint = FVector{-202.9296875, -37.75, 0.0};
    const auto NativeInterval = ck::Get_ProceduralHeightPlanningInterval(MeasuredBody, {}, FVector::UpVector,
        PostsAnchors, PostsHip, ReturnedNativePoint, 92.0f, 64.0);
    TestTrue(TEXT("The measured returned native point has a strict interval above the failed retained-foot plane"),
        NativeInterval.IsSet() && FMath::IsNearlyEqual(NativeInterval->X, -64.0, 1.0e-9)
            && FMath::IsNearlyEqual(NativeInterval->Y, -63.82032573975846, 1.0e-9));
    const auto Interior = ck::Get_ProceduralHeightInteriorRetryDelta(MeasuredBody, {}, FVector::UpVector,
        PostsAnchors, PostsHip, ReturnedNativePoint, 92.0f, 64.0, -64.0);
    TestTrue(TEXT("The native-witness proposal is the strict interior midpoint, not a contact admission"),
        Interior.IsSet() && FMath::IsNearlyEqual(Interior.GetValue(), -63.91016286987923, 1.0e-9));
    if (NOT Interior.IsSet() || NOT NativeInterval.IsSet())
    { return false; }
    auto InteriorBody = MeasuredBody;
    InteriorBody.AddToTranslation(FVector::UpVector * Interior.GetValue());
    TestTrue(TEXT("The interior remains downward, strictly above the failed goal and below the exact force bound"),
        Interior.GetValue() > -64.0 && Interior.GetValue() < NativeInterval->Y
            && Interior.GetValue() < 0.0 && InteriorBody.GetLocation().Z > 90.0
            && FVector::Dist(InteriorBody.TransformPosition(PostsHip), ReturnedNativePoint) < 92.0);
    for (const auto& Anchor : PostsAnchors)
    {
        TestTrue(TEXT("The measured interior preserves every unchanged authored physical stance sphere"),
            FVector::Dist(InteriorBody.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld()) <= Anchor.Get_Reach());
    }
    TestFalse(TEXT("A failed delta at the force endpoint cannot produce a distinct interior retry"),
        ck::Get_ProceduralHeightInteriorRetryDelta(MeasuredBody, {}, FVector::UpVector,
            PostsAnchors, PostsHip, ReturnedNativePoint, 92.0f, 64.0, NativeInterval->Y).IsSet());

    auto DescendedBody = MeasuredBody;
    DescendedBody.AddToTranslation(FVector{0.0, 0.0, -6.666666666666667});
    const auto StableProposal = ck::Get_ProceduralHeightReprojectionDelta(DescendedBody, {}, FVector::UpVector,
        PostsAnchors, PostsHip, PostsSeed, 92.0f, 64.0);
    TestTrue(TEXT("A partial descent recomputes the retained world plane instead of lowering the next endpoint"),
        StableProposal.IsSet() && FMath::IsNearlyEqual(DescendedBody.GetLocation().Z + StableProposal.GetValue(), 90.0, 1.0e-9));
    if (NOT StableProposal.IsSet())
    { return false; }
    const auto StableInterior = ck::Get_ProceduralHeightInteriorRetryDelta(DescendedBody, {}, FVector::UpVector,
        PostsAnchors, PostsHip, ReturnedNativePoint, 92.0f, 64.0, StableProposal.GetValue());
    TestTrue(TEXT("The same retained world geometry has the same interior world goal next frame"),
        StableInterior.IsSet() && FMath::IsNearlyEqual(DescendedBody.GetLocation().Z + StableInterior.GetValue(),
            InteriorBody.GetLocation().Z, 1.0e-9));

    for (const auto PoseZ : {-2.0, 2.0})
    {
        const auto PoseOffset = TOptional<FTransform>{FTransform{FVector{0.0, 0.0, PoseZ}}};
        const auto Proposal = ck::Get_ProceduralHeightReprojectionDelta(MeasuredBody, PoseOffset, FVector::UpVector,
            PostsAnchors, PostsHip, PostsSeed, 92.0f, 64.0);
        const auto ExpectedRootZ = PoseZ < 0.0 ? 92.0 : 90.0;
        TestTrue(TEXT("Initial downward proposals respect both simulation and posed retained-foot planes"),
            Proposal.IsSet() && FMath::IsNearlyEqual(MeasuredBody.GetLocation().Z + Proposal.GetValue(), ExpectedRootZ, 1.0e-9));
        TestFalse(TEXT("Both frames must fit the fresh force point above their retained planes before an interior exists"),
            ck::Get_ProceduralHeightInteriorRetryDelta(MeasuredBody, PoseOffset, FVector::UpVector,
                PostsAnchors, PostsHip, ReturnedNativePoint, 92.0f, 64.0, -64.0).IsSet());
    }


    // The later native packet has a nonempty fixed-seed interval entirely below the retained feet.
    const auto LaterBody = FTransform{FVector{-255.990956768, 0.0, 147.333319575}};
    const auto LaterAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-273.3359375, -81.15625, 90.0}, PostsHip, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-265.71875, 81.0625, 90.0}, FVector{45.0, 28.0, 0.0}, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-303.2578125, -90.84375, 90.0}, FVector{-45.0, -28.0, 0.0}, 120.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-299.625, 91.125, 90.0}, FVector{-45.0, 28.0, 0.0}, 120.0f}};
    const auto LaterSeed = FVector{-184.703125, -49.71875, 0.0};
    const auto LaterInterval = ck::Get_ProceduralHeightPlanningInterval(LaterBody, {}, FVector::UpVector,
        LaterAnchors, PostsHip, LaterSeed, 92.0f, 64.0);
    const auto LaterPlane = ck::Get_ProceduralHeightRetainedPlaneLower(LaterBody, {}, FVector::UpVector, LaterAnchors);
    TestTrue(TEXT("The measured ordinary interval exists below the retained-foot plane"),
        LaterInterval.IsSet() && LaterPlane.IsSet()
            && FMath::IsNearlyEqual(LaterInterval->Y, -61.885991943, 1.0e-8)
            && FMath::IsNearlyEqual(LaterPlane.GetValue(), -57.333319575, 1.0e-9)
            && LaterInterval->Y < LaterPlane.GetValue());
    if (NOT LaterPlane.IsSet())
    { return false; }
    auto AtPlane = LaterBody;
    AtPlane.AddToTranslation(FVector::UpVector * LaterPlane.GetValue());
    for (const auto& Anchor : LaterAnchors)
    {
        TestTrue(TEXT("The exact retained-plane boundary places each hip at its measured foot height"),
            FMath::IsNearlyZero(FVector::DotProduct(AtPlane.TransformPosition(Anchor.Get_HipLocal())
                - Anchor.Get_FootWorld(), FVector::UpVector), 1.0e-9));
    }
    const auto LaterNative = ck::Get_ProceduralHeightPlanningInterval(LaterBody, {}, FVector::UpVector,
        LaterAnchors, PostsHip, FVector{-188.1328125, -46.875, 0.0}, 92.0f, 64.0);
    TestTrue(TEXT("Routing does not claim the measured returned native seed fits above the retained plane"),
        LaterNative.IsSet() && LaterNative->Y < LaterPlane.GetValue());
    for (const auto PoseZ : {-2.0, 2.0})
    {
        const auto Plane = ck::Get_ProceduralHeightRetainedPlaneLower(LaterBody,
            TOptional<FTransform>{FTransform{FVector{0.0, 0.0, PoseZ}}}, FVector::UpVector, LaterAnchors);
        TestTrue(TEXT("The more restrictive simulation or posed foot plane determines the lower bound"),
            Plane.IsSet() && FMath::IsNearlyEqual(Plane.GetValue(), LaterPlane.GetValue() - FMath::Min(PoseZ, 0.0), 1.0e-9));
    }
    const auto Rotation = FQuat{FVector::RightVector, FMath::DegreesToRadians(90.0)};
    const auto RotatedBody = FTransform{Rotation, Rotation.RotateVector(LaterBody.GetLocation())};
    auto RotatedAnchors = LaterAnchors;
    for (auto& Anchor : RotatedAnchors)
    {
        Anchor = ck::FProceduralSurfaceReachPaceAnchor{Rotation.RotateVector(Anchor.Get_FootWorld()),
            Anchor.Get_HipLocal(), Anchor.Get_Reach()};
    }
    const auto RotatedPlane = ck::Get_ProceduralHeightRetainedPlaneLower(RotatedBody, {},
        Rotation.RotateVector(FVector::UpVector), RotatedAnchors);
    TestTrue(TEXT("Retained-foot planes use the supplied support normal rather than world Z"),
        RotatedPlane.IsSet() && FMath::IsNearlyEqual(RotatedPlane.GetValue(), LaterPlane.GetValue(), 1.0e-9));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningRecoveryPostsTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.RecoveryRetryPinnedPosts03",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralHeightPlanningRecoveryPostsTest::
    RunTest(const FString& InParameters)
    -> bool
{
    // Pinned Posts03 geometry (D37820D4): observations offer a scalar, never native landing authority.
    const auto Body = FTransform{FVector{119919.72136446973, 365000.0, 699.2531139708732}};
    const auto Pose = TOptional<FTransform>{FTransform{FQuat{-0.008119643144865945,
        0.018167381623409706, -8.130389829736624e-9, 0.9998019892159394},
        FVector{0.0, 0.0, -1.9670324772122698}}};
    const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        {FVector{119994.0625, 364973.1875, 675.0}, FVector{45.0, -28.0, 0.0}, 100.0f},
        {FVector{119994.0625, 365026.8125, 675.0}, FVector{45.0, 28.0, 0.0}, 100.0f},
        {FVector{119890.5703125, 364940.25, 600.0}, FVector{-45.0, -28.0, 0.0}, 120.0f},
        {FVector{119885.5390625, 365057.625, 600.0}, FVector{-45.0, 28.0, 0.0}, 120.0f}};
    const auto Legs = TArray<ck::FProceduralHeightRecoveryLegGeometry>{
        {FVector{45.0, -28.0, 0.0}, FVector{119994.0625, 364973.1875, 675.0},
            TOptional<FVector>{FVector{119993.1640625, 364947.5625, 600.0}}, 92.0f},
        {FVector{45.0, 28.0, 0.0}, FVector{119994.0625, 365026.8125, 675.0},
            TOptional<FVector>{FVector{119993.1640625, 365052.4375, 600.0}}, 92.0f},
        {FVector{-45.0, -28.0, 0.0}, FVector{119888.0390625, 364937.8125, 600.0},
            TOptional<FVector>{FVector{119889.6171875, 364933.75, 600.0}}, 110.4000015258789f},
        {FVector{-45.0, 28.0, 0.0}, FVector{119888.0390625, 365062.1875, 600.0},
            TOptional<FVector>{FVector{119889.6171875, 365066.25, 600.0}}, 110.4000015258789f}};
    const auto Retry = ck::Get_ProceduralHeightRecoveryRetryDelta(Body, Pose, FVector::UpVector,
        Anchors, Legs, 64.0, 15.259259493781528);
    TestTrue(TEXT("Pinned Posts03 preserves its first goal and offers the exact strictly smaller union endpoint"),
        Retry.IsSet() && FMath::IsNearlyEqual(*Retry, 4.7488366704726985, 1.0e-9));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningRecoveryUnionsTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.RecoveryRetryClosedUnionsAnd65Components",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralHeightPlanningRecoveryUnionsTest::
    RunTest(const FString& InParameters)
    -> bool
{
    const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        {FVector::ZeroVector, FVector::ZeroVector, 1000.0f}};
    const auto Offer = [&](const TArray<ck::FProceduralHeightRecoveryLegGeometry>& InLegs, double InFirst)
    {
        return ck::Get_ProceduralHeightRecoveryRetryDelta(FTransform::Identity, {}, FVector::UpVector,
            Anchors, InLegs, 200.0, InFirst);
    };
    // First leg has [-2,2] OR [4,8]; the second clips only the second component to [4,6].
    auto Legs = TArray<ck::FProceduralHeightRecoveryLegGeometry>{
        {FVector::ZeroVector, FVector::ZeroVector, TOptional<FVector>{FVector{0.0, 0.0, 6.0}}, 2.0f},
        {FVector::ZeroVector, FVector{0.0, 0.0, 2.0}, {}, 4.0f}};
    auto Retry = Offer(Legs, 9.0);
    TestTrue(TEXT("Alternatives are OR within each leg, with AND across supporting legs"),
        Retry.IsSet() && FMath::IsNearlyEqual(*Retry, 6.0, 1.0e-12));
    Retry = Offer(Legs, 5.0);
    TestTrue(TEXT("A component straddling the failed goal contributes no inset; a lower disjoint endpoint survives"),
        Retry.IsSet() && FMath::IsNearlyEqual(*Retry, 2.0, 1.0e-12));
    Retry = Offer(Legs, 6.0);
    TestTrue(TEXT("An endpoint equal to the failed goal is excluded without erasing the lower component"),
        Retry.IsSet() && FMath::IsNearlyEqual(*Retry, 2.0, 1.0e-12));
    TestFalse(TEXT("Neither a straddling interval nor endpoint equality fabricates a smaller scalar"),
        Offer({Legs[1]}, 6.0).IsSet());
    TestFalse(TEXT("Disjoint leg constraints do not acquire a convex-hull bridge"),
        Offer({Legs[0], {FVector::ZeroVector, FVector{0.0, 0.0, 3.0}, {}, 0.5f}}, 9.0).IsSet());
    TestFalse(TEXT("An empty alternative does not make the valid nonpositive alternative an offer"),
        Offer({{FVector::ZeroVector, FVector{10.0, 0.0, 0.0},
            TOptional<FVector>{FVector{0.0, 0.0, -2.0}}, 1.0f}}, 9.0).IsSet());
    Retry = Offer({{FVector::ZeroVector, FVector{10.0, 0.0, 0.0},
        TOptional<FVector>{FVector{0.0, 0.0, 3.0}}, 1.0f}}, 9.0);
    TestTrue(TEXT("One geometrically empty alternative preserves its valid sibling"),
        Retry.IsSet() && FMath::IsNearlyEqual(*Retry, 4.0, 1.0e-12));

    // Same radius, different perpendicular distances realize [-1,2k] OR [2k+1,130].
    // Each supporting leg cuts one new gap. The 64th intersection needs all 65 components.
    Legs.Reset();
    const auto Point = [](double InLower, double InUpper)
    {
        const auto Half = (InUpper - InLower) * 0.5;
        return FVector{FMath::Sqrt(256.0 * 256.0 - Half * Half), 0.0, (InLower + InUpper) * 0.5};
    };
    for (auto Index = 0; Index < 64; ++Index)
    {
        Legs.Emplace(FVector::ZeroVector, Point(-1.0, 2.0 * Index),
            TOptional<FVector>{Point(2.0 * Index + 1.0, 130.0)}, 256.0f);
        if (Index > 0)
        {
            Retry = Offer(Legs, 129.0);
            TestTrue(TEXT("Every prefix preserves all closed components through the fixed 65-component bound"),
                Retry.IsSet() && FMath::IsNearlyEqual(*Retry, 2.0 * Index, 1.0e-8));
        }
    }
    const auto ExtraLeg = Legs[0];
    Legs.Add(ExtraLeg);
    TestFalse(TEXT("A 65th supporting geometry group is rejected before unbounded work"), Offer(Legs, 129.0).IsSet());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningRecoveryMalformedTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.RecoveryRetryRejectsMalformedAndBoundaryInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralHeightPlanningRecoveryMalformedTest::
    RunTest(const FString& InParameters)
    -> bool
{
    const auto NaN = std::numeric_limits<double>::quiet_NaN();
    const auto Inf = std::numeric_limits<double>::infinity();
    const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        {FVector::ZeroVector, FVector::ZeroVector, 100.0f}};
    const auto Good = ck::FProceduralHeightRecoveryLegGeometry{FVector::ZeroVector, FVector{0.0, 0.0, 2.0}, {}, 1.0f};
    const auto Offer = [&](const FTransform& InBody, const TOptional<FTransform>& InPose, const FVector& InUp,
        const TArray<ck::FProceduralHeightRecoveryLegGeometry>& InLegs, double InMax, double InFirst)
    {
        return ck::Get_ProceduralHeightRecoveryRetryDelta(InBody, InPose, InUp, Anchors, InLegs, InMax, InFirst);
    };
    const auto Exact = Offer(FTransform::Identity, {}, FVector::UpVector, {Good}, 64.0, 4.0);
    TestTrue(TEXT("A single observation has its exact positive endpoint"),
        Exact.IsSet() && *Exact == 3.0);
    const auto Posed = Offer(FTransform::Identity, TOptional<FTransform>{FTransform{FVector{0.0, 0.0, 0.5}}},
        FVector::UpVector, {Good}, 64.0, 4.0);
    TestTrue(TEXT("Presentation and simulation force intervals intersect without a force tolerance expansion"),
        Posed.IsSet() && *Posed == 2.5);
    for (const auto First : {NaN, Inf, -1.0, 0.0, 0.001})
    { TestFalse(TEXT("Malformed or nonpositive first goals cannot authorize a retry"),
        Offer(FTransform::Identity, {}, FVector::UpVector, {Good}, 64.0, First).IsSet()); }
    for (const auto Max : {NaN, Inf, -1.0, 0.0})
    { TestFalse(TEXT("Invalid authored translation bounds decline the entire offer"),
        Offer(FTransform::Identity, {}, FVector::UpVector, {Good}, Max, 4.0).IsSet()); }
    for (const auto Up : {FVector::ZeroVector, FVector{0.0, 0.0, 2.0}, FVector{NaN, 0.0, 1.0}})
    { TestFalse(TEXT("The helper requires finite normalized Up"),
        Offer(FTransform::Identity, {}, Up, {Good}, 64.0, 4.0).IsSet()); }
    for (const auto Bad : {
        ck::FProceduralHeightRecoveryLegGeometry{FVector{NaN, 0.0, 0.0}, FVector::ZeroVector, {}, 1.0f},
        ck::FProceduralHeightRecoveryLegGeometry{FVector::ZeroVector, FVector{Inf, 0.0, 0.0}, {}, 1.0f},
        ck::FProceduralHeightRecoveryLegGeometry{FVector::ZeroVector, FVector{std::numeric_limits<double>::max(), 0.0, 0.0}, {}, 1.0f},
        ck::FProceduralHeightRecoveryLegGeometry{FVector::ZeroVector, FVector::ZeroVector, TOptional<FVector>{FVector{NaN}}, 1.0f},
        ck::FProceduralHeightRecoveryLegGeometry{FVector::ZeroVector, FVector::ZeroVector, {}, 0.0f},
        ck::FProceduralHeightRecoveryLegGeometry{FVector::ZeroVector, FVector::ZeroVector, {}, -1.0f},
        ck::FProceduralHeightRecoveryLegGeometry{FVector::ZeroVector, FVector::ZeroVector, {}, std::numeric_limits<float>::infinity()}})
    { TestFalse(TEXT("A malformed alternative or group rejects the whole input rather than publishing a subset"),
        Offer(FTransform::Identity, {}, FVector::UpVector, {Good, Bad}, 64.0, 4.0).IsSet()); }
    TestFalse(TEXT("Missing supporting geometry cannot produce an offer"),
        Offer(FTransform::Identity, {}, FVector::UpVector, {}, 64.0, 4.0).IsSet());
    TestFalse(TEXT("Nonrigid body transforms are rejected"),
        Offer(FTransform{FQuat::Identity, FVector::ZeroVector, FVector{2.0}}, {}, FVector::UpVector, {Good}, 64.0, 4.0).IsSet());
    TestFalse(TEXT("A malformed presentation transform is rejected"),
        Offer(FTransform::Identity, TOptional<FTransform>{FTransform{FVector{NaN, 0.0, 0.0}}},
            FVector::UpVector, {Good}, 64.0, 4.0).IsSet());
    TestFalse(TEXT("Without a physical stance interval no recovery offer is invented"),
        ck::Get_ProceduralHeightRecoveryRetryDelta(FTransform::Identity, {}, FVector::UpVector,
            TArray<ck::FProceduralSurfaceReachPaceAnchor>{}, TArray<ck::FProceduralHeightRecoveryLegGeometry>{Good}, 64.0, 4.0).IsSet());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningConflictingPlantTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.RejectsFloorGoalConflictingWithElevatedPlant",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningConflictingPlantTest::
    RunTest(const FString&)
    -> bool
{
    const auto Hip = FVector{45.0, 28.0, 0.0};
    const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{-45.0, -28.0, 190.0}, FVector{-45.0, -28.0, 0.0}, 100.0f}};
    const auto Interval = ck::Get_ProceduralHeightPlanningInterval(FTransform{FVector{0.0, 0.0, 124.0}}, {},
        FVector::UpVector, Anchors, Hip, FVector{50.0, 55.0, 0.0}, 92.0f, 60.0);
    TestFalse(TEXT("A plant requiring root Z >=90 cannot coexist with this floor landing requiring Z <88"), Interval.IsSet());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningUpwardRecoveryTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.AllowsBoundedUpwardRecovery",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningUpwardRecoveryTest::
    RunTest(const FString&)
    -> bool
{
    const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{0.0, 0.0, 70.0}, FVector::ZeroVector, 50.0f}};
    const auto Interval = ck::Get_ProceduralHeightPlanningInterval(FTransform{FVector{0.0, 0.0, 85.0}}, {},
        FVector::UpVector, Anchors, FVector::ZeroVector, FVector{0.0, 0.0, 150.0}, 50.0f, 60.0);
    TestTrue(TEXT("A prospective high contact can require upward recovery"), Interval.IsSet());
    if (NOT Interval.IsSet())
    { return false; }
    TestTrue(TEXT("The lower bound raises the root by 15 cm"), FMath::IsNearlyEqual(Interval->X, 15.0, 1.0e-4));
    TestTrue(TEXT("The existing lower plant caps upward travel at 35 cm plus its existing numerical tolerance"),
        FMath::IsNearlyEqual(Interval->Y, 35.001, 1.0e-4));
    const auto UpwardProposal = ck::Get_ProceduralHeightReprojectionDelta(FTransform{FVector{0.0, 0.0, 85.0}}, {},
        FVector::UpVector, Anchors, FVector::ZeroVector, FVector{0.0, 0.0, 150.0}, 50.0f, 60.0);
    TestTrue(TEXT("The deferred positive-height result retains its original upper stance endpoint"),
        UpwardProposal.IsSet() && FMath::IsNearlyEqual(UpwardProposal.GetValue(), 35.001, 1.0e-9));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningMalformedTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.RejectsMalformedIntervals",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningMalformedTest::
    RunTest(const FString&)
    -> bool
{
    const auto Empty = TArrayView<const ck::FProceduralSurfaceReachPaceAnchor>{};
    const auto NaN = std::numeric_limits<double>::quiet_NaN();
    const auto Infinity = std::numeric_limits<double>::infinity();
    auto NonFiniteCandidateHip = FVector::ZeroVector;
    NonFiniteCandidateHip.X = NaN;
    auto NonFiniteCandidatePoint = FVector::ZeroVector;
    NonFiniteCandidatePoint.X = Infinity;
    const auto Query = [&](const FTransform& InBody, const TOptional<FTransform>& InPose, const FVector& InUp,
        const FVector& InHip, const FVector& InFoot, float InReach, double InTranslation)
        -> TOptional<FVector2D>
    {
        return ck::Get_ProceduralHeightPlanningInterval(InBody, InPose, InUp, Empty, InHip, InFoot, InReach, InTranslation);
    };
    TestFalse(TEXT("A non-unit up vector is rejected"),
        Query(FTransform::Identity, {}, FVector{0.0, 0.0, 2.0}, FVector::ZeroVector, FVector::ZeroVector, 100.0f, 60.0).IsSet());
    TestFalse(TEXT("A non-rigid body is rejected"), Query(FTransform{FQuat::Identity, FVector::ZeroVector, FVector{2.0}}, {},
        FVector::UpVector, FVector::ZeroVector, FVector::ZeroVector, 100.0f, 60.0).IsSet());
    TestFalse(TEXT("A non-rigid optional pose is rejected"), Query(FTransform::Identity,
        TOptional<FTransform>{FTransform{FQuat::Identity, FVector::ZeroVector, FVector{2.0}}}, FVector::UpVector,
        FVector::ZeroVector, FVector::ZeroVector, 100.0f, 60.0).IsSet());
    TestFalse(TEXT("A non-finite candidate hip is rejected"), Query(FTransform::Identity, {}, FVector::UpVector,
        NonFiniteCandidateHip, FVector::ZeroVector, 100.0f, 60.0).IsSet());
    TestFalse(TEXT("A non-finite candidate contact is rejected"), Query(FTransform::Identity, {}, FVector::UpVector,
        FVector::ZeroVector, NonFiniteCandidatePoint, 100.0f, 60.0).IsSet());
    TestFalse(TEXT("Non-positive candidate reach is rejected"), Query(FTransform::Identity, {}, FVector::UpVector,
        FVector::ZeroVector, FVector::ZeroVector, 0.0f, 60.0).IsSet());
    TestFalse(TEXT("An invalid translation budget is rejected"), Query(FTransform::Identity, {}, FVector::UpVector,
        FVector::ZeroVector, FVector::ZeroVector, 100.0f, NaN).IsSet());
    TestFalse(TEXT("A negative translation budget cannot invert the interval"), Query(FTransform::Identity, {}, FVector::UpVector,
        FVector::ZeroVector, FVector::ZeroVector, 100.0f, -60.0).IsSet());
    const auto BadAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector::ZeroVector, FVector::ZeroVector, -1.0f}};
    TestFalse(TEXT("One malformed trusted anchor rejects the complete interval"),
        ck::Get_ProceduralHeightPlanningInterval(FTransform::Identity, {}, FVector::UpVector, BadAnchors,
            FVector::ZeroVector, FVector::ZeroVector, 100.0f, 60.0).IsSet());
    TestFalse(TEXT("Lateral separation beyond reach cannot be repaired by any height"), Query(FTransform::Identity, {},
        FVector::UpVector, FVector::ZeroVector, FVector{101.0, 0.0, 0.0}, 100.0f, 60.0).IsSet());

    // Pure exported intervals accept finite double geometry; native float representability belongs to native queries.
    // Assign finite components directly so the reproduction itself never performs overflowing vector arithmetic.
    const auto MaxFinite = std::numeric_limits<double>::max();
    auto MaxPoint = FVector::ZeroVector;
    MaxPoint.X = MaxFinite;
    auto MinPoint = FVector::ZeroVector;
    MinPoint.X = -MaxFinite;
    const auto OppositeFiniteAnchor = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{MinPoint, MaxPoint, 100.0f}};
    TestFalse(TEXT("Finite direct stance hip-minus-foot overflow rejects before diagnostic vector subtraction"),
        ck::Get_ProceduralHeightStanceInterval(FTransform::Identity, {}, FVector::UpVector,
            OppositeFiniteAnchor, 60.0).IsSet());
    TestFalse(TEXT("Finite candidate-only hip-minus-foot overflow rejects through the exported planning API"),
        Query(FTransform::Identity, {}, FVector::UpVector, MaxPoint, MinPoint, 100.0f, 60.0).IsSet());

    const auto MaxBody = FTransform{MaxPoint};
    const auto MaxPose = TOptional<FTransform>{FTransform{MaxPoint}};
    const auto CancellingAnchor = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector::ZeroVector, MinPoint, 100.0f}};
    const auto SimulationCancellation = ck::Get_ProceduralHeightStanceInterval(MaxBody, {}, FVector::UpVector,
        CancellingAnchor, 60.0);
    TestTrue(TEXT("Finite maximum translation and opposite local hip retain a valid cancelling simulation interval"),
        SimulationCancellation.IsSet() && SimulationCancellation->X == -60.0 && SimulationCancellation->Y == 60.0);
    TestFalse(TEXT("A required posed frame whose finite translations overflow composition rejects after valid simulation geometry"),
        ck::Get_ProceduralHeightStanceInterval(MaxBody, MaxPose, FVector::UpVector, CancellingAnchor, 60.0).IsSet());
    TestFalse(TEXT("Candidate-only planning also rejects a required overflowing posed frame"),
        Query(MaxBody, MaxPose, FVector::UpVector, MinPoint, FVector::ZeroVector, 100.0f, 60.0).IsSet());

    auto DiagonalMax = MaxPoint;
    DiagonalMax.Y = MaxFinite;
    const auto RotatedBody = FTransform{FQuat{FVector::UpVector, UE_DOUBLE_PI * 0.25}};
    TestFalse(TEXT("A finite candidate whose rigid rotation overflows its transformed hip rejects before vector diagnostics"),
        Query(RotatedBody, {}, FVector::UpVector, DiagonalMax, FVector::ZeroVector, 100.0f, 60.0).IsSet());
    const auto NearUnitScaledBody = FTransform{FQuat::Identity, FVector::ZeroVector, FVector{1.00005, 1.0, 1.0}};
    TestFalse(TEXT("Scale inside the existing rigid-frame tolerance cannot overflow a finite transformed candidate hip"),
        Query(NearUnitScaledBody, {}, FVector::UpVector, MaxPoint, FVector::ZeroVector, 100.0f, 60.0).IsSet());

    const auto TransformOverflowAnchor = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector::ZeroVector, MaxPoint, 100.0f}};
    TestFalse(TEXT("Stance rejects a finite local hip whose body translation overflows the transformed point"),
        ck::Get_ProceduralHeightStanceInterval(MaxBody, {}, FVector::UpVector, TransformOverflowAnchor, 60.0).IsSet());
    TestFalse(TEXT("Finite along-up separation whose squared distance overflows rejects the entire interval"),
        Query(FTransform::Identity, {}, FVector::UpVector, FVector{0.0, 0.0, 1.0e200},
            FVector::ZeroVector, 100.0f, 60.0).IsSet());
    const auto PartialThenOverflow = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector::ZeroVector, FVector::ZeroVector, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{MinPoint, MaxPoint, 100.0f}};
    TestFalse(TEXT("An overflowing later anchor cannot publish the earlier stance subset"),
        ck::Get_ProceduralHeightStanceInterval(FTransform::Identity, {}, FVector::UpVector,
            PartialThenOverflow, 60.0).IsSet());
    TestFalse(TEXT("An overflowing later stance anchor cannot publish a candidate planning interval"),
        ck::Get_ProceduralHeightPlanningInterval(FTransform::Identity, {}, FVector::UpVector,
            PartialThenOverflow, FVector::ZeroVector, FVector::ZeroVector, 100.0f, 60.0).IsSet());

    const auto EqualHugeAnchor = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{MaxPoint, MaxPoint, 100.0f}};
    const auto EqualHugeStance = ck::Get_ProceduralHeightStanceInterval(FTransform::Identity, {}, FVector::UpVector,
        EqualHugeAnchor, 60.0);
    TestTrue(TEXT("Equal finite maximum hip and foot coordinates remain valid pure stance geometry"),
        EqualHugeStance.IsSet() && EqualHugeStance->X == -60.0 && EqualHugeStance->Y == 60.0);
    const auto EqualHugePlanning = Query(FTransform::Identity, {}, FVector::UpVector, MaxPoint, MaxPoint, 100.0f, 60.0);
    TestTrue(TEXT("Equal finite maximum candidate coordinates do not acquire a native float cap"),
        EqualHugePlanning.IsSet() && EqualHugePlanning->X == -60.0 && EqualHugePlanning->Y == 60.0);
    const auto HugeOrigin = FVector{1.0e300, 0.0, 0.0};
    const auto TranslatedPlanning = Query(FTransform{HugeOrigin}, {}, FVector::UpVector,
        FVector::ZeroVector, HugeOrigin, 100.0f, 60.0);
    TestTrue(TEXT("A common finite huge world translation cancels without changing the pure planning interval"),
        TranslatedPlanning.IsSet() && TranslatedPlanning->X == -60.0 && TranslatedPlanning->Y == 60.0);

    const auto EmptyMaximum = ck::Get_ProceduralHeightStanceInterval(MaxBody, MaxPose, FVector::UpVector, Empty, MaxFinite);
    TestTrue(TEXT("Empty stance returns the exact finite authored band without composing an unused pose"),
        EmptyMaximum.IsSet() && EmptyMaximum->X == -MaxFinite && EmptyMaximum->Y == MaxFinite);
    TestFalse(TEXT("Empty stance still validates a malformed optional rigid frame before returning its band"),
        ck::Get_ProceduralHeightStanceInterval(FTransform::Identity,
            TOptional<FTransform>{FTransform{FQuat::Identity, FVector::ZeroVector, FVector{2.0}}},
            FVector::UpVector, Empty, MaxFinite).IsSet());

    const auto ValidAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector::ZeroVector, FVector::ZeroVector, 100.0f}};
    const auto PlaneBody = FTransform{FVector{0.0, 0.0, 60.0}};
    const auto PlaneAnchorsValid = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector::ZeroVector, FVector::ZeroVector, 100.0f}};
    TestFalse(TEXT("An empty retained set cannot supply an ordinary descent plane"),
        ck::Get_ProceduralHeightRetainedPlaneLower(PlaneBody, {}, FVector::UpVector, {}).IsSet());
    TestFalse(TEXT("A zero support normal rejects before retained-plane arithmetic"),
        ck::Get_ProceduralHeightRetainedPlaneLower(PlaneBody, {}, FVector::ZeroVector, PlaneAnchorsValid).IsSet());
    TestFalse(TEXT("An unrepresentable body location rejects before retained-plane transforms"),
        ck::Get_ProceduralHeightRetainedPlaneLower(FTransform{FVector{1.0e308}}, {},
            FVector::UpVector, PlaneAnchorsValid).IsSet());
    TestFalse(TEXT("An unrepresentable pose rejects before composition"),
        ck::Get_ProceduralHeightRetainedPlaneLower(PlaneBody, TOptional<FTransform>{FTransform{FVector{1.0e308}}},
            FVector::UpVector, PlaneAnchorsValid).IsSet());
    for (const auto InvalidReach : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()})
    {
        const auto InvalidAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
            ck::FProceduralSurfaceReachPaceAnchor{FVector::ZeroVector, FVector::ZeroVector, InvalidReach}};
        TestFalse(TEXT("Malformed retained reach cannot supply a descent plane"),
            ck::Get_ProceduralHeightRetainedPlaneLower(PlaneBody, {}, FVector::UpVector, InvalidAnchors).IsSet());
    }
    for (const auto ExtremeHip : {false, true})
    {
        const auto InvalidAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
            ck::FProceduralSurfaceReachPaceAnchor{ExtremeHip ? FVector::ZeroVector : FVector{1.0e308},
                ExtremeHip ? FVector{1.0e308} : FVector::ZeroVector, 100.0f}};
        TestFalse(TEXT("Unrepresentable retained points reject before transforms or subtraction"),
            ck::Get_ProceduralHeightRetainedPlaneLower(PlaneBody, {}, FVector::UpVector, InvalidAnchors).IsSet());
    }

    const auto Reprojection = [&](const FVector& InHip, const FVector& InPoint, float InReach, double InTranslation)
        -> TOptional<double>
    {
        return ck::Get_ProceduralHeightReprojectionDelta(FTransform{FVector{0.0, 0.0, 60.0}}, {},
            FVector::UpVector, ValidAnchors, InHip, InPoint, InReach, InTranslation);
    };
    for (const auto InvalidReach : {0.0f, -1.0f, std::numeric_limits<float>::infinity()})
    {
        TestFalse(TEXT("Malformed candidate reach cannot turn fixed-seed failure into a fallback"),
            Reprojection(FVector::ZeroVector, FVector::ZeroVector, InvalidReach, 64.0).IsSet());
    }
    TestFalse(TEXT("An extreme finite seed rejects before vector arithmetic or native conversion"),
        Reprojection(FVector::ZeroVector, FVector{1.0e308}, 92.0f, 64.0).IsSet());
    TestFalse(TEXT("An extreme finite hip rejects before transformed vector arithmetic"),
        Reprojection(FVector{1.0e308}, FVector::ZeroVector, 92.0f, 64.0).IsSet());
    auto InvalidPoint = FVector::ZeroVector;
    InvalidPoint.X = NaN;
    TestFalse(TEXT("A non-finite observation cannot trigger the fallback"),
        Reprojection(FVector::ZeroVector, InvalidPoint, 92.0f, 64.0).IsSet());
    const auto ExtremeAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{1.0e308}, FVector::ZeroVector, 100.0f}};
    TestFalse(TEXT("An extreme stance point rejects before the existing interval's distance arithmetic"),
        ck::Get_ProceduralHeightReprojectionDelta(FTransform::Identity, {}, FVector::UpVector, ExtremeAnchors,
            FVector::ZeroVector, FVector{0.0, 0.0, -10.0}, 92.0f, 64.0).IsSet());
    TestFalse(TEXT("An invalid translation budget cannot trigger fallback"),
        Reprojection(FVector::ZeroVector, FVector::ZeroVector, 92.0f, NaN).IsSet());
    TestFalse(TEXT("A zero proposal is not a height trial"),
        Reprojection(FVector::ZeroVector, FVector{0.0, 0.0, 60.0}, 92.0f, 64.0).IsSet());
    TestFalse(TEXT("A missing stance never grants a fallback"),
        ck::Get_ProceduralHeightReprojectionDelta(FTransform::Identity, {}, FVector::UpVector, Empty,
            FVector::ZeroVector, FVector{0.0, 0.0, -10.0}, 92.0f, 64.0).IsSet());
    const auto Conflicting = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{0.0, 0.0, -101.0}, FVector::ZeroVector, 100.0f},
        ck::FProceduralSurfaceReachPaceAnchor{FVector{0.0, 0.0, 101.0}, FVector::ZeroVector, 100.0f}};
    TestFalse(TEXT("Mutually incompatible current plants reject the complete fallback proposal"),
        ck::Get_ProceduralHeightReprojectionDelta(FTransform::Identity, {}, FVector::UpVector, Conflicting,
            FVector::ZeroVector, FVector{0.0, 0.0, -10.0}, 92.0f, 64.0).IsSet());
    const auto Retry = [&](const FVector& InPoint, float InReach, double InFailed)
    {
        return ck::Get_ProceduralHeightInteriorRetryDelta(FTransform{FVector{0.0, 0.0, 60.0}}, {},
            FVector::UpVector, ValidAnchors, FVector::ZeroVector, InPoint, InReach, 64.0, InFailed);
    };
    for (const auto InvalidFailed : {NaN, Infinity, 0.0, 1.0, -0.0005, -65.0})
    {
        TestFalse(TEXT("A malformed, non-downward, no-op or out-of-band failed goal cannot seed an interior"),
            Retry(FVector::ZeroVector, 92.0f, InvalidFailed).IsSet());
    }
    for (const auto InvalidReach : {0.0f, -1.0f, std::numeric_limits<float>::infinity()})
    {
        TestFalse(TEXT("Malformed native admission reach rejects the pure interior"),
            Retry(FVector::ZeroVector, InvalidReach, -60.0).IsSet());
    }
    TestFalse(TEXT("An extreme finite native point rejects before interior arithmetic"),
        Retry(FVector{1.0e308}, 92.0f, -60.0).IsSet());
    TestFalse(TEXT("A non-finite returned point cannot become a retry proposal"), Retry(InvalidPoint, 92.0f, -60.0).IsSet());
    TestFalse(TEXT("An interior requires a nonempty actual retained stance"),
        ck::Get_ProceduralHeightInteriorRetryDelta(FTransform::Identity, {}, FVector::UpVector, Empty,
            FVector::ZeroVector, FVector::ZeroVector, 92.0f, 64.0, -10.0).IsSet());
    TestFalse(TEXT("Conflicting retained stance spheres reject before native-point intersection"),
        ck::Get_ProceduralHeightInteriorRetryDelta(FTransform::Identity, {}, FVector::UpVector, Conflicting,
            FVector::ZeroVector, FVector::ZeroVector, 92.0f, 64.0, -10.0).IsSet());
    const auto PlaneAnchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{0.0, 0.0, 5.0}, FVector::ZeroVector, 100.0f}};
    TestFalse(TEXT("A zero-width force and retained-plane intersection provides no strict interior"),
        ck::Get_ProceduralHeightInteriorRetryDelta(FTransform{FVector{0.0, 0.0, 10.0}}, {}, FVector::UpVector,
            PlaneAnchors, FVector::ZeroVector, FVector::ZeroVector, 5.0f, 64.0, -5.0).IsSet());
    TestFalse(TEXT("A midpoint that rounds to an endpoint is not a distinct interior proposal"),
        ck::Get_ProceduralHeightInteriorRetryDelta(FTransform{FVector{0.0, 0.0, 16.0}}, {}, FVector::UpVector,
            ValidAnchors, FVector::ZeroVector, FVector::ZeroVector, 8.0f, 64.0,
            std::nextafter(-8.0, -Infinity)).IsSet());
    TestFalse(TEXT("The existing no-op tolerance still excludes a tiny nonzero interior"),
        ck::Get_ProceduralHeightInteriorRetryDelta(FTransform{FVector{0.0, 0.0, 0.0015}}, {}, FVector::UpVector,
            ValidAnchors, FVector::ZeroVector, FVector::ZeroVector, 0.0014f, 64.0, -0.0015).IsSet());
    const auto AboveHip = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
        ck::FProceduralSurfaceReachPaceAnchor{FVector{0.0, 0.0, 70.0}, FVector::ZeroVector, 100.0f}};
    TestFalse(TEXT("A retained plane above the hip cannot create a non-downward deferred fallback"),
        ck::Get_ProceduralHeightReprojectionDelta(FTransform{FVector{0.0, 0.0, 60.0}}, {}, FVector::UpVector,
            AboveHip, FVector::ZeroVector, FVector::ZeroVector, 92.0f, 64.0).IsSet());

    auto Witness = TOptional<ck::FProceduralGaitHeightRetryWitness>{ck::FProceduralGaitHeightRetryWitness{}};
    auto Rays = 7;
    TestFalse(TEXT("An unsafe native admission prerequisite rejects without a query"),
        ck::Get_IsProceduralHeightPlanAdmitted({}, FTransform::Identity, {}, ck::FProceduralGaitHeightCandidate{}, Rays,
            2048, ck::EProceduralHeightPlanStage::Landing, &Witness));
    TestTrue(TEXT("Native retry output resets before the earliest invalid-gait return and leaves rays unchanged"),
        NOT Witness.IsSet() && Rays == 7);

    auto SeedFailure = ck::EProceduralHeightSeedFailure::TrustedDownFinalForceReject;
    auto InvalidRays = 0;
    TestFalse(TEXT("Invalid native gait rejects with a reused seed-failure output"),
        ck::Get_IsProceduralHeightPlanAdmitted({}, FTransform::Identity, {}, ck::FProceduralGaitHeightCandidate{},
            InvalidRays, 2048, ck::EProceduralHeightPlanStage::Landing, nullptr, nullptr, &SeedFailure));
    TestTrue(TEXT("Invalid gait clears seed failure before any native ray"),
        SeedFailure == ck::EProceduralHeightSeedFailure::None && InvalidRays == 0);
    SeedFailure = ck::EProceduralHeightSeedFailure::TrustedInitialDownClampReprobeMiss;
    TestFalse(TEXT("Invalid Recovery cannot retain another call's seed cause"),
        ck::Get_IsProceduralHeightPlanAdmitted({}, FTransform::Identity, {}, ck::FProceduralGaitHeightCandidate{},
            InvalidRays, 2048, ck::EProceduralHeightPlanStage::Recovery, nullptr, nullptr, &SeedFailure));
    TestTrue(TEXT("Invalid Recovery resets the reused output without a ray"),
        SeedFailure == ck::EProceduralHeightSeedFailure::None && InvalidRays == 0);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningPoseAdmissionTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.PoseAdmissionChecksEmptyAndAnchoredCandidates",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningPoseAdmissionTest::
    RunTest(const FString&)
    -> bool
{
    const auto Proposed = FTransform{FQuat{FVector::RightVector, 0.2}, FVector{0.0, 0.0, 10.0}};
    const auto Empty = TArrayView<const ck::FProceduralBodyPoseReachAnchor>{};
    const auto Baseline = ck::ProjectProceduralBodyPoseToReach(FTransform::Identity, FTransform::Identity, Proposed, Empty);
    TestTrue(TEXT("The default empty callback preserves existing unconstrained callers"),
        Baseline.IsSet() && Baseline->Get_Offset().Equals(Proposed, 1.0e-4) && Baseline->Get_Fraction() == 1.0f);
    auto Calls = 0;
    const auto Admission = [&](const FTransform& InOffset) -> bool
    {
        ++Calls;
        return InOffset.GetLocation().Z <= 3.0
            && InOffset.GetRotation().AngularDistance(FQuat::Identity) <= 0.061;
    };
    const auto Projected = ck::ProjectProceduralBodyPoseToReach(FTransform::Identity, FTransform::Identity,
        Proposed, Empty, Admission);
    TestTrue(TEXT("Empty anchors do not bypass collision admission"), Projected.IsSet() && Calls > 1
        && Projected->Get_Fraction() > 0.0f && Projected->Get_Fraction() < 1.0f
        && Admission(Projected->Get_Offset()));
    const auto Anchors = TArray<ck::FProceduralBodyPoseReachAnchor>{
        ck::FProceduralBodyPoseReachAnchor{FVector::ZeroVector, FVector::ZeroVector, 2.0f}};
    const auto Anchored = ck::ProjectProceduralBodyPoseToReach(FTransform::Identity, FTransform::Identity,
        Proposed, Anchors, Admission);
    TestTrue(TEXT("The tighter physical anchor and admission both constrain the same returned candidate"),
        Anchored.IsSet() && Anchored->Get_Offset().GetLocation().Z <= 2.001 && Admission(Anchored->Get_Offset()));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningPoseBaseTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.PoseAdmissionRequiresProvenBase",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningPoseBaseTest::
    RunTest(const FString&)
    -> bool
{
    const auto Empty = TArrayView<const ck::FProceduralBodyPoseReachAnchor>{};
    const auto Previous = FTransform{FVector{0.0, 0.0, 2.0}};
    const auto Proposed = FTransform{FVector{0.0, 0.0, 10.0}};
    const auto OnlyPrevious = [&](const FTransform& InOffset) -> bool
    { return InOffset.Equals(Previous, 1.0e-6); };
    const auto Held = ck::ProjectProceduralBodyPoseToReach(FTransform::Identity, Previous, Proposed, Empty, OnlyPrevious);
    TestTrue(TEXT("If every interpolation candidate is blocked, the proved previous base is retained"),
        Held.IsSet() && Held->Get_Fraction() == 0.0f && Held->Get_Offset().Equals(Previous, 1.0e-6));
    const auto IdentityOnly = [](const FTransform& InOffset) -> bool
    { return InOffset.Equals(FTransform::Identity, 1.0e-6); };
    const auto Rebased = ck::ProjectProceduralBodyPoseToReach(FTransform::Identity, Previous, Proposed, Empty, IdentityOnly);
    TestTrue(TEXT("Identity fallback is returned only after its own admission proof"), Rebased.IsSet()
        && Rebased->Get_UsedIdentityBase() && Rebased->Get_Offset().Equals(FTransform::Identity, 1.0e-6));
    auto Calls = 0;
    const auto None = ck::ProjectProceduralBodyPoseToReach(FTransform::Identity, Previous, Proposed, Empty,
        [&](const FTransform&) -> bool
        {
            ++Calls;
            return false;
        });
    TestFalse(TEXT("Rejecting proposal, previous, and identity returns no pose"), None.IsSet());
    TestEqual(TEXT("No interpolation runs without an admitted base"), Calls, 3);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningPoseMalformedTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.MalformedPoseNeverCallsAdmission",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningPoseMalformedTest::
    RunTest(const FString&)
    -> bool
{
    auto Calls = 0;
    const auto Admission = [&](const FTransform&) -> bool
    {
        ++Calls;
        return true;
    };
    const auto Anchors = TArray<ck::FProceduralBodyPoseReachAnchor>{
        ck::FProceduralBodyPoseReachAnchor{FVector::ZeroVector, FVector::ZeroVector, -1.0f}};
    TestFalse(TEXT("Malformed reach is rejected before collision admission"),
        ck::ProjectProceduralBodyPoseToReach(FTransform::Identity, FTransform::Identity, FTransform::Identity,
            Anchors, Admission).IsSet());
    const auto Empty = TArrayView<const ck::FProceduralBodyPoseReachAnchor>{};
    TestFalse(TEXT("A scaled previous offset is rejected before collision admission"),
        ck::ProjectProceduralBodyPoseToReach(FTransform::Identity,
            FTransform{FQuat::Identity, FVector::ZeroVector, FVector{2.0}}, FTransform::Identity, Empty, Admission).IsSet());
    TestEqual(TEXT("Invalid inputs cause zero downstream admission callbacks"), Calls, 0);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningSettingsTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.AdaptiveSettingsPreserveDisabledCallers",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningSettingsTest::
    RunTest(const FString&)
    -> bool
{
    const auto Default = FCk_SurfaceMotion_Spec{};
    TestFalse(TEXT("Existing callers do not opt into adaptive body height"), Default.Get_AdaptiveHeight().Get_Enabled());
    TestTrue(TEXT("Default motion settings remain valid without body dimensions"), ck::IsValid(Default));
    auto Settings = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true);
    TestFalse(TEXT("Enabled adaptive height requires positive box dimensions"), ck::IsValid(Settings));
    Settings.Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0});
    TestTrue(TEXT("Authored Beast dimensions and default positive margin are valid"), ck::IsValid(Settings));
    Settings.Set_CollisionMargin(0.0f);
    TestFalse(TEXT("An enabled zero margin is rejected"), ck::IsValid(Settings));
    Settings.Set_CollisionMargin(std::numeric_limits<float>::infinity());
    TestFalse(TEXT("An enabled non-finite margin is rejected"), ck::IsValid(Settings));
    Settings.Set_CollisionMargin(1.0f).Set_BodyHalfExtents(FVector{50.0, -28.0, 22.0});
    TestFalse(TEXT("An enabled negative dimension is rejected"), ck::IsValid(Settings));
    Settings.Set_Enabled(false);
    TestTrue(TEXT("Disabled mode ignores its unused envelope authoring"), ck::IsValid(Settings));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningEnvelopeUnavailableTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.EnvelopeRejectsUnavailableContextWithoutQueries",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralHeightPlanningEnvelopeUnavailableTest::
    RunTest(const FString&)
    -> bool
{
    const auto Settings = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
        .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0});
    const auto Filter = FCk_Jolt_QueryFilter{};
    auto Queries = 0;
    TestFalse(TEXT("Unavailable context is rejected rather than counted as empty geometry"),
        ck::Get_IsProceduralBodyEnvelopePathClear(nullptr, Settings, Filter,
            FTransform::Identity, FTransform{FVector{10.0, 0.0, 0.0}}, Queries, 64));
    TestEqual(TEXT("Unavailable context issues no native queries"), Queries, 0);
    TestFalse(TEXT("A zero budget rejects without a native query"),
        ck::Get_IsProceduralBodyEnvelopePathClear(nullptr, Settings, Filter,
            FTransform::Identity, FTransform::Identity, Queries, 0));
    TestEqual(TEXT("Zero budget remains untouched"), Queries, 0);
    Queries = 64;
    TestFalse(TEXT("An exhausted budget rejects without a native query"),
        ck::Get_IsProceduralBodyEnvelopePathClear(nullptr, Settings, Filter,
            FTransform::Identity, FTransform::Identity, Queries, 64));
    TestEqual(TEXT("Exhausted query count cannot overrun"), Queries, 64);
    Queries = 0;
    const auto Invalid = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true);
    TestFalse(TEXT("Malformed box dimensions are rejected without entering native physics"),
        ck::Get_IsProceduralBodyEnvelopePathClear(nullptr, Invalid, Filter,
            FTransform::Identity, FTransform::Identity, Queries, 64));
    TestEqual(TEXT("Malformed box issues no native queries"), Queries, 0);
    return true;
}

#if WITH_EDITOR

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralHeightPlanningEnvelopeWorldTest,
    "Ck.ProceduralAnimation.Core.HeightPlanning.EnvelopeChecksRealJoltSweepRotationAndBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
    FCkProceduralHeightPlanningEnvelopeWorldTest::
    RunTest(const FString&)
    -> bool
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* Jolt = InServer->GetSubsystem<UCk_Jolt_Subsystem>();
            const auto Physics = ck::IsValid(Jolt) ? Jolt->Get_PhysicsSystem().Pin() : nullptr;
            if (NOT TestTrue(TEXT("A real PIE Jolt physics world is available"), Physics.IsValid()))
            { return; }
            const auto Signature = ck::jolt::TryDerive_SignatureFromProfile(TEXT("BlockAll"), ECk_Jolt_BodyDomain::Static);
            if (NOT TestTrue(TEXT("The fixture derives a real BlockAll collision layer"), Signature.IsSet()))
            { return; }
            const auto Layer = JPH::ObjectLayer{Jolt->Get_LayerTable().Get_OrRegisterLayer(*Signature)};
            auto& Bodies = Physics->GetBodyInterface();
            auto BodyIds = TArray<JPH::BodyID>{};
            ON_SCOPE_EXIT
            {
                for (const auto Id : BodyIds)
                {
                    Bodies.RemoveBody(Id);
                    Bodies.DestroyBody(Id);
                }
            };
            const auto AddBox = [&](const FVector& InCenter, const FVector& InHalfExtents) -> bool
            {
                auto Shape = JPH::Ref<JPH::Shape>{new JPH::BoxShape{JPH::Vec3{
                    static_cast<float>(InHalfExtents.X), static_cast<float>(InHalfExtents.Y), static_cast<float>(InHalfExtents.Z)}}};
                auto Settings = JPH::BodyCreationSettings{Shape.GetPtr(),
                    JPH::RVec3{static_cast<float>(InCenter.X), static_cast<float>(InCenter.Y), static_cast<float>(InCenter.Z)},
                    JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layer};
                const auto Id = Bodies.CreateAndAddBody(Settings, JPH::EActivation::DontActivate);
                if (Id.IsInvalid())
                { return false; }
                BodyIds.Add(Id);
                return true;
            };
            const auto Origin = FVector{100000.0, 100000.0, 20000.0};
            const auto RotationOrigin = Origin + FVector{400.0, 0.0, 0.0};
            const auto PoseOrigin = Origin + FVector{800.0, 0.0, 0.0};
            if (NOT TestTrue(TEXT("Translation and rotation blockers and the pose ceiling are present"),
                    AddBox(Origin, FVector{10.0}) && AddBox(RotationOrigin + FVector{14.0, 14.0, 0.0}, FVector{1.0})
                        && AddBox(PoseOrigin + FVector{0.0, 0.0, 30.0}, FVector{30.0, 30.0, 5.0})))
            { return; }
            const auto Filter = FCk_Jolt_QueryFilter{}.Set_BodySelection(ECk_Jolt_QueryBodySelection::StaticNonSensor);
            const auto Envelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true).Set_BodyHalfExtents(FVector{10.0});
            const auto Probe = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box}.Set_HalfExtents(FVector{11.05});
            const auto Start = FTransform{Origin + FVector{-100.0, 0.0, 0.0}};
            const auto End = FTransform{Origin + FVector{100.0, 0.0, 0.0}};
            TestTrue(TEXT("Translation endpoints are independently clear in the native Jolt world"),
                UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, Start.GetLocation(), FRotator::ZeroRotator, Probe, Filter).IsEmpty()
                    && UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, End.GetLocation(), FRotator::ZeroRotator, Probe, Filter).IsEmpty());
            auto Queries = 0;
            auto SweepHit = TOptional<FCk_Jolt_HitResult>{};
            TestFalse(TEXT("A clear endpoint cannot bypass a blocker crossed by the swept box"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter, Start, End, Queries, 3, &SweepHit));
            TestEqual(TEXT("The translation blocker is detected by the second counted native query"), Queries, 2);
            TestTrue(TEXT("Only the actual translation sweep publishes a blocking hit"), SweepHit.IsSet()
                && SweepHit->Get_HasHit() && SweepHit->Get_Fraction() > 0.0f && SweepHit->Get_Fraction() < 1.0f);
            Queries = 0;
            TestFalse(TEXT("An initial overlap is rejected even when the destination is clear"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter, FTransform{Origin}, End, Queries, 3, &SweepHit));
            TestEqual(TEXT("Initial overlap rejects on the first native query"), Queries, 1);
            TestFalse(TEXT("An initial overlap clears the prior hit without inventing sweep authority"), SweepHit.IsSet());

            const auto RodEnvelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                .Set_BodyHalfExtents(FVector{22.0, 2.0, 2.0});
            const auto Rod = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box}.Set_HalfExtents(FVector{23.05, 3.05, 3.05});
            const auto QuarterTurn = FQuat{FVector::UpVector, UE_DOUBLE_HALF_PI};
            TestTrue(TEXT("Both endpoints of the rotating rod are independently clear"),
                UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, RotationOrigin, FRotator::ZeroRotator, Rod, Filter).IsEmpty()
                    && UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, RotationOrigin, QuarterTurn.Rotator(), Rod, Filter).IsEmpty());
            TestFalse(TEXT("The actual midpoint rod overlaps the off-axis blocker"),
                UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, RotationOrigin,
                    FQuat{FVector::UpVector, UE_DOUBLE_PI * 0.25}.Rotator(), Rod, Filter).IsEmpty());
            Queries = 0;
            SweepHit = FCk_Jolt_HitResult{}.Set_HasHit(true);
            TestFalse(TEXT("Intermediate rotation collision is rejected despite both clear endpoint poses"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, RodEnvelope, Filter, FTransform{RotationOrigin},
                    FTransform{QuarterTurn, RotationOrigin}, Queries, 3, &SweepHit));
            TestTrue(TEXT("Rotation rejection remains inside its native query cap"), Queries > 0 && Queries <= 3);
            TestEqual(TEXT("The conservative rotation enclosure detects overlap at its initial placement"), Queries, 1);
            TestFalse(TEXT("Conservative rotation overlap does not publish a directional sweep hit"), SweepHit.IsSet());

            // Beam01 raw3626/3634: measured rounded simulation transforms and actual authored native beam.
            // This isolates obstacle acquisition; it does not assert gait/stance admission or route completion.
            const auto BeamOrigin = FVector{120000.0, 345000.0, 600.0};
            if (NOT TestTrue(TEXT("The measured Beam regression owns the actual authored static beam"),
                    AddBox(BeamOrigin + FVector{0.0, 0.0, 30.0}, FVector{1000.0, 15.0, 30.0})))
            { return; }
            const auto BeamEnvelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0}).Set_CollisionMargin(1.0f);
            const auto BeamStart = FTransform{FQuat{0.0, 0.0, 0.908144137, 0.418657648},
                BeamOrigin + FVector{1053.977355, 0.0, 82.472258}};
            const auto BeamEnd = FTransform{FQuat{0.0, 0.0, 0.917639995, 0.397412682},
                BeamOrigin + FVector{1051.977350, 0.0, 82.498459}};
            const auto BeamPose = FTransform{FQuat{0.001120444, -0.001629393, 0.0, 0.999998045},
                FVector{0.0, 0.0, 0.585328}};
            auto Overlap = TOptional<ck::FProceduralBodyEnvelopeOverlapObservation>{};
            Queries = 0;
            TestTrue(TEXT("The measured exact stationary simulation box is natively clear"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, BeamEnvelope, Filter,
                    BeamStart, BeamStart, Queries, 3, &SweepHit, &Overlap));
            TestEqual(TEXT("The measured stationary simulation proof retains its three-query cost"), Queries, 3);
            Queries = 0;
            TestTrue(TEXT("The measured exact stationary presentation box is natively clear"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, BeamEnvelope, Filter,
                    BeamPose * BeamStart, BeamPose * BeamStart, Queries, 3, &SweepHit, &Overlap));
            Queries = 0;
            SweepHit = FCk_Jolt_HitResult{}.Set_HasHit(true);
            TestFalse(TEXT("The measured rotating path still rejects its conservative native enclosure"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, BeamEnvelope, Filter,
                    BeamStart, BeamEnd, Queries, 3, &SweepHit, &Overlap));
            TestEqual(TEXT("The measured rotation rejects on the original first native overlap query"), Queries, 1);
            TestFalse(TEXT("An enclosure observation never fabricates a sweep hit"), SweepHit.IsSet());
            TestTrue(TEXT("The failed enclosure preserves its existing native start-overlap contacts"),
                Overlap.IsSet() && Overlap->Get_Kind() == ck::EProceduralBodyEnvelopeOverlapKind::StartEnclosureOverlap
                    && NOT Overlap->Get_Hits().IsEmpty());
            const FCk_Jolt_HitResult* BeamContact = nullptr;
            if (Overlap.IsSet())
            {
                for (const auto& Hit : Overlap->Get_Hits())
                {
                    if (Hit.Get_HasHit() && NOT Hit.Get_Position().ContainsNaN() && NOT Hit.Get_Normal().ContainsNaN()
                        && Hit.Get_Normal().IsNormalized() && Hit.Get_Normal().X > KINDA_SMALL_NUMBER
                        && (BeamContact == nullptr || Hit.Get_Normal().X > BeamContact->Get_Normal().X))
                    { BeamContact = &Hit; }
                }
            }
            TestTrue(TEXT("Native enclosure evidence contains a finite unit contact opposing measured minus-X steering"),
                BeamContact != nullptr);
            if (BeamContact != nullptr)
            {
                const auto Inset = BeamContact->Get_Position() - BeamContact->Get_Normal() * BeamEnvelope.Get_CollisionMargin();
                // Gym Beast clearance64 + ProbeReachBeyondClearance115 (Fixture MakeMotionParams).
                constexpr auto BeamProbeReach = 179.0;
                const auto Top = UCk_Utils_JoltQuery_UE::Get_RayCast(InServer,
                    Inset + FVector::UpVector * BeamProbeReach, Inset - FVector::UpVector * BeamProbeReach, Filter);
                TestTrue(TEXT("An independent native inset ray actually resolves the authored beam top"),
                    Top.Get_HasHit() && Top.Get_Fraction() > 0.0f && Top.Get_Fraction() <= 1.0f
                        && NOT Top.Get_Position().ContainsNaN() && NOT Top.Get_Normal().ContainsNaN()
                        && Top.Get_Normal().Z > KINDA_SMALL_NUMBER
                        && FMath::IsNearlyEqual(Top.Get_Position().Z - BeamOrigin.Z, 60.0, 0.01));
            }
            auto RaisedStart = BeamStart;
            RaisedStart.SetLocation(BeamOrigin + FVector{1053.977355, 0.0, 83.15});
            auto RaisedEnd = BeamEnd;
            RaisedEnd.SetLocation(BeamOrigin + FVector{1051.977350, 0.0, 83.15});
            Queries = 0;
            TestTrue(TEXT("The raised 83.15 cm simulation turn is admitted only by the complete native hull proof"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, BeamEnvelope, Filter,
                    RaisedStart, RaisedEnd, Queries, 3, &SweepHit, &Overlap));
            TestEqual(TEXT("The raised simulation turn uses all three original native proof queries"), Queries, 3);
            TestFalse(TEXT("A clear raised path resets the prior overlap observation"), Overlap.IsSet());
            // Match SurfaceMotion's center-arc enclosure padding for a translated presentation offset.
            auto PosedBeamEnvelope = BeamEnvelope;
            const auto BeamTurn = RaisedStart.GetRotation().AngularDistance(RaisedEnd.GetRotation());
            const auto BeamOrbit = BeamPose.GetTranslation().Size() * (1.0 - FMath::Cos(BeamTurn * 0.5));
            PosedBeamEnvelope.Set_BodyHalfExtents(BeamEnvelope.Get_BodyHalfExtents() + FVector{BeamOrbit});
            Queries = 0;
            TestTrue(TEXT("The raised turn also proves the measured actual presentation envelope clear"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, PosedBeamEnvelope, Filter,
                    BeamPose * RaisedStart, BeamPose * RaisedEnd, Queries, 3, &SweepHit, &Overlap));
            TestEqual(TEXT("The raised presentation turn uses all three original native proof queries"), Queries, 3);

            // A ready world reaches the old arithmetic defect; a null world would mask it. Disabled settings still
            // cannot send malformed geometry to FVector length/inflation or native float conversion.
            for (const auto Enabled : {false, true})
            {
                auto HugeEnvelope = BeamEnvelope;
                HugeEnvelope.Set_Enabled(Enabled).Set_BodyHalfExtents(FVector{1.0e308});
                SweepHit = FCk_Jolt_HitResult{}.Set_HasHit(true);
                Overlap = ck::FProceduralBodyEnvelopeOverlapObservation{
                    ck::EProceduralBodyEnvelopeOverlapKind::EndEnclosureOverlap,
                    TArray<FCk_Jolt_HitResult>{FCk_Jolt_HitResult{}.Set_HasHit(true)}};
                Queries = 0;
                TestFalse(TEXT("Extreme finite authored extents reject before unsafe vector arithmetic in a ready world"),
                    ck::Get_IsProceduralBodyEnvelopePathClear(InServer, HugeEnvelope, Filter,
                        BeamStart, BeamEnd, Queries, 3, &SweepHit, &Overlap));
                TestEqual(TEXT("Extreme finite rejection performs zero native queries"), Queries, 0);
                TestFalse(TEXT("Extreme finite rejection resets prepopulated sweep authority"), SweepHit.IsSet());
                TestFalse(TEXT("Extreme finite rejection resets prepopulated overlap observations"), Overlap.IsSet());
            }

            const auto ClearStart = FTransform{Origin + FVector{-100.0, 0.0, 200.0}};
            const auto ClearEnd = FTransform{Origin + FVector{100.0, 0.0, 200.0}};
            Queries = 0;
            SweepHit = FCk_Jolt_HitResult{}.Set_HasHit(true);
            TestTrue(TEXT("A genuinely clear translated box path is accepted"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter, ClearStart, ClearEnd, Queries, 3, &SweepHit));
            TestEqual(TEXT("A complete clear path consumes exactly three native queries"), Queries, 3);
            TestFalse(TEXT("A clear path clears a stale prior blocking hit"), SweepHit.IsSet());
            Queries = 0;
            TestFalse(TEXT("A two-query cap cannot silently skip the clear endpoint proof"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter, ClearStart, ClearEnd, Queries, 2));
            TestEqual(TEXT("An exhausted real-world query budget is never exceeded"), Queries, 2);
            Queries = 0;
            TestFalse(TEXT("A live world does not bypass a zero query budget"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter, ClearStart, ClearEnd, Queries, 0));
            TestEqual(TEXT("Zero budget performs no native calls even with valid physics"), Queries, 0);

            const auto PoseBody = FTransform{PoseOrigin};
            const auto PoseProposal = FTransform{FVector{0.0, 0.0, 30.0}};
            const auto Anchors = TArray<ck::FProceduralBodyPoseReachAnchor>{
                ck::FProceduralBodyPoseReachAnchor{FVector::ZeroVector, PoseOrigin - FVector{0.0, 0.0, 50.0}, 100.0f}};
            const auto ReachOnly = ck::ProjectProceduralBodyPoseToReach(PoseBody, FTransform::Identity, PoseProposal, Anchors);
            TestTrue(TEXT("The complete proposed pose remains within physical reach before applying the real ceiling"),
                ReachOnly.IsSet() && ReachOnly->Get_Fraction() == 1.0f);
            Queries = 0;
            const auto CollisionLimited = ck::ProjectProceduralBodyPoseToReach(PoseBody, FTransform::Identity,
                PoseProposal, Anchors, [&](const FTransform& InOffset) -> bool
                {
                    return ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter,
                        PoseBody, InOffset * PoseBody, Queries, 64);
                });
            TestTrue(TEXT("An actual Jolt ceiling constrains presentation despite valid physical limb reach"),
                CollisionLimited.IsSet() && CollisionLimited->Get_Fraction() > 0.0f
                    && CollisionLimited->Get_Fraction() < 1.0f);
            if (CollisionLimited.IsSet())
            {
                TestTrue(TEXT("The returned presentation box is independently clear beneath the ceiling"),
                    UCk_Utils_JoltQuery_UE::Get_Overlap(InServer,
                        (CollisionLimited->Get_Offset() * PoseBody).GetLocation(), FRotator::ZeroRotator, Probe, Filter).IsEmpty());
                TestTrue(TEXT("The returned presentation retains its trusted physical anchor"),
                    FVector::Dist((CollisionLimited->Get_Offset() * PoseBody).GetLocation(), Anchors[0].Get_FootWorld()) <= 100.001);
            }
            TestTrue(TEXT("Coupled pose projection remains within its 64-query budget"), Queries > 0 && Queries <= 64);
            Queries = 0;
            const auto OverlappingPrevious = FTransform{FVector{0.0, 0.0, 30.0}};
            const auto Rejected = ck::ProjectProceduralBodyPoseToReach(PoseBody, OverlappingPrevious,
                FTransform{FVector{0.0, 0.0, 40.0}}, Anchors, [&](const FTransform& InOffset) -> bool
                {
                    return ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter,
                        OverlappingPrevious * PoseBody, InOffset * PoseBody, Queries, 64);
                });
            TestFalse(TEXT("A clear identity endpoint cannot fabricate a path out of an initially overlapping presentation"),
                Rejected.IsSet());

            // A level Beast box has 1.95 cm of room at each end of this 50 cm high corridor after margin and convex
            // padding. Yaw changes no point's height, so the complete turn is clear without spending vertical room.
            const auto YawOrigin = Origin + FVector{1200.0, 0.0, 0.0};
            if (NOT TestTrue(TEXT("The pure-yaw regression has an actual static floor and ceiling"),
                    AddBox(YawOrigin - FVector{0.0, 0.0, 10.0}, FVector{200.0, 200.0, 10.0})
                        && AddBox(YawOrigin + FVector{0.0, 0.0, 60.0}, FVector{200.0, 200.0, 10.0})))
            { return; }
            const auto YawEnvelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0});
            const auto PaddedYawExtents = FVector{51.05, 29.05, 23.05};
            const auto YawProbe = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box}.Set_HalfExtents(PaddedYawExtents);
            const auto YawStart = FTransform{YawOrigin + FVector{0.0, 0.0, 25.0}};
            const auto YawEnd = FTransform{QuarterTurn, YawOrigin + FVector{10.0, 0.0, 25.0}};
            const auto YawMidpoint = FQuat{FVector::UpVector, UE_DOUBLE_PI * 0.25};
            TestTrue(TEXT("Native endpoint and midpoint Beast boxes are clear between the floor and ceiling"),
                UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, YawStart.GetLocation(), FRotator::ZeroRotator, YawProbe, Filter).IsEmpty()
                    && UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, YawEnd.GetLocation(), QuarterTurn.Rotator(), YawProbe, Filter).IsEmpty()
                    && UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, YawOrigin + FVector{5.0, 0.0, 25.0},
                        YawMidpoint.Rotator(), YawProbe, Filter).IsEmpty());
            const auto OldScalarInflation = 2.0 * PaddedYawExtents.Size() * FMath::Sin(UE_DOUBLE_HALF_PI * 0.25);
            const auto OldScalarProbe = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box}
                .Set_HalfExtents(PaddedYawExtents + FVector{OldScalarInflation});
            TestFalse(TEXT("The previous scalar Z inflation falsely occupied this actually clear yaw corridor"),
                UCk_Utils_JoltQuery_UE::Get_Overlap(InServer, YawStart.GetLocation(), YawMidpoint.Rotator(),
                    OldScalarProbe, Filter).IsEmpty());
            Queries = 0;
            SweepHit = FCk_Jolt_HitResult{}.Set_HasHit(true);
            TestTrue(TEXT("Axis-aware enclosure accepts the full clear yaw turn and translation"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, YawEnvelope, Filter, YawStart, YawEnd, Queries, 3, &SweepHit));
            TestEqual(TEXT("The clear yaw path retains all three native envelope proof queries"), Queries, 3);
            TestFalse(TEXT("The clear yaw path still clears prior sweep authority"), SweepHit.IsSet());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

namespace ck
{
    namespace test_posts_recovery
    {
        constexpr auto TestPath = TEXT("Project.Functional Tests.CkTests.AutoTests.AutoTests_CkTests_Level.Ck_AutoTest_ProceduralAnimation_PostsBeastPosedReach");

        auto CurrentTest() -> FAutomationTestBase*
        {
            auto* Test = FAutomationTestFramework::Get().GetCurrentTest();
            return Test != nullptr && Test->GetTestFullName() == TestPath ? Test : nullptr;
        }

        struct FProof
        {
            FCk_Handle_ProceduralGait Root;
            TWeakObjectPtr<UWorld> World;
            uint64 Frame = 0;
            uint64 Solve = 0;
            bool RetryObserved = false;
            bool MovingRetryObserved = false;
            bool FirstSuccessObserved = false;
            bool Finalized = false;
            bool AwaitingAfter = false;
            FDelegateHandle EndHandle;

            ~FProof()
            {
                Finalize(CurrentTest());
                FAutomationTestFramework::Get().OnTestEndEvent.Remove(EndHandle);
            }

            auto Finalize(FAutomationTestBase* Test) -> void
            {
                if (NOT Finalized && Test != nullptr && Test == CurrentTest())
                {
                    Finalized = true;
                    Test->TestFalse(TEXT("Posts recovery proof closes without an outstanding before callback"), AwaitingAfter);
                    Test->AddInfo(FString::Printf(TEXT("PostsRecoveryProof summary retry%d moving%d firstSuccess%d"),
                        RetryObserved, MovingRetryObserved, FirstSuccessObserved));
                    Test->TestTrue(TEXT("The original Posts route actually exercised its mandatory second Recovery"), RetryObserved);
                    Test->TestTrue(TEXT("The original Posts route actually accepted a moving second Recovery"), MovingRetryObserved);
                }
            }

            auto Observe(FCk_Handle_ProceduralGait InGait, bool InBefore) -> void
            {
                auto* Test = CurrentTest();
                if (Finalized || Test == nullptr || InGait != Root || NOT World.IsValid()
                    || UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(InGait) != World.Get()
                    || NOT InGait.Has<FFragment_ProceduralGait>()
                    || NOT InGait.Has<FFragment_ProceduralHeightPlanning_Test>())
                { return; }
                if (InBefore)
                {
                    AwaitingAfter = true;
                    Frame = GFrameCounter;
                    Solve = InGait.Get<FFragment_ProceduralGait>().Get_SolveSequence();
                    return;
                }
                AwaitingAfter = false;
                const auto& Receipt = InGait.Get<FFragment_ProceduralHeightPlanning_Test>();
                if (NOT Test->TestTrue(TEXT("Posts dispatch receipts retain their original four slots"),
                    NOT Receipt.Overflow && Receipt.Count >= 0 && Receipt.Count <= 4))
                { return; }
                if (Receipt.Count == 0)
                { return; }
                if (NOT Test->TestTrue(TEXT("Posts dispatch proof joins the same before/after Surface call"),
                    Frame == GFrameCounter && Receipt.Frame == Frame && Receipt.Solve == Solve))
                { return; }
                auto PreviousRays = 0;
                auto PreviousEnvelopes = 0;
                auto PreviousTrials = 0;
                auto RecoveryCount = 0;
                for (auto Index = 0; Index < Receipt.Count; ++Index)
                {
                    const auto& Attempt = Receipt.Attempts[Index];
                    Test->TestTrue(TEXT("Posts attempts retain monotonic unchanged planning budgets"),
                        Attempt.Trials > PreviousTrials && Attempt.Trials <= 4
                            && Attempt.Rays >= PreviousRays && Attempt.Rays <= 2048
                            && Attempt.Envelopes >= PreviousEnvelopes && Attempt.Envelopes <= 64);
                    Test->TestTrue(TEXT("Posts accepted attempt is last; rejected dispatch never mutates its body"),
                        Attempt.Completed && (Attempt.Accepted ? Index == Receipt.Count - 1
                            : Attempt.BodyBefore.Equals(Attempt.BodyAfter, 1.0e-9)));
                    PreviousRays = Attempt.Rays;
                    PreviousEnvelopes = Attempt.Envelopes;
                    PreviousTrials = Attempt.Trials;
                    RecoveryCount += Attempt.Stage == EProceduralHeightPlanStage::Recovery ? 1 : 0;
                }
                Test->TestTrue(TEXT("Posts cannot dispatch a third Recovery"), RecoveryCount <= 2);
                if (RecoveryCount == 0)
                { return; }
                const auto& First = Receipt.Attempts[0];
                Test->TestTrue(TEXT("Recovery preserves the original first ordinary goal"),
                    First.Stage == EProceduralHeightPlanStage::Recovery && First.Route == 0);
                if (First.Accepted)
                {
                    Test->TestEqual(TEXT("First Recovery success dispatches exactly once"), Receipt.Count, 1);
                    if (NOT FirstSuccessObserved)
                    {
                        FirstSuccessObserved = true;
                        Test->AddInfo(FString::Printf(TEXT("PostsRecoveryProof firstSuccess frame%llu solve%llu count%d"),
                            Frame, Solve, Receipt.Count));
                    }
                    return;
                }
                if (RecoveryCount != 2)
                { return; }
                if (NOT Test->TestEqual(TEXT("A Recovery retry ends this whole-body proposal path"), Receipt.Count, 2))
                { return; }
                const auto& Second = Receipt.Attempts[1];
                const auto& A = First.Candidate;
                const auto& B = Second.Candidate;
                const auto Qualified = First.Completed && NOT First.Accepted
                    && First.SeedFailure == EProceduralHeightSeedFailure::TrustedDownFinalForceReject
                    && First.Delta > 0.001 && FMath::IsFinite(First.Delta)
                    && Second.Stage == EProceduralHeightPlanStage::Recovery && Second.Route == 0
                    && Second.Delta > 0.001 && Second.Delta < First.Delta && FMath::IsFinite(Second.Delta)
                    && A.Get_Leg() == B.Get_Leg() && A.Get_HipLocal() == B.Get_HipLocal()
                    && A.Get_FootWorld() == B.Get_FootWorld() && A.Get_PointWorld() == B.Get_PointWorld()
                    && A.Get_NormalWorld() == B.Get_NormalWorld() && A.Get_Reach() == B.Get_Reach()
                    && A.Get_AdmissionReach() == B.Get_AdmissionReach() && A.Get_RequiresRelief() == B.Get_RequiresRelief()
                    && First.BodyBefore.Equals(First.BodyAfter, 1.0e-9)
                    && First.BodyAfter.Equals(Second.BodyBefore, 1.0e-9)
                    && NOT Receipt.CooperativeThirdOffered && NOT Receipt.InteriorOffered;
                if (NOT Test->TestTrue(TEXT("Only a completed classified first force rejection admits one strictly smaller same-candidate retry"), Qualified))
                { return; }
                const auto Moving = Second.Accepted && NOT Second.BodyAfter.Equals(Second.BodyBefore, 1.0e-9);
                const auto EmitProof = [&](const TCHAR* InKind)
                {
                    const auto Registry = Root.Get_RegistryView().Get_RegistryHandle();
                    Test->AddInfo(FString::Printf(TEXT("PostsRecoveryProof %s test[%s] root[ProceduralAnimation.Posts.Beast4] world%p registry%u:%u entity%u frame%llu solve%llu first%.17g second%.17g count%d rays%d envelopes%d trials%d"),
                        InKind, TestPath, World.Get(), static_cast<uint32>(Registry.SlotIndex),
                        static_cast<uint32>(Registry.Generation), static_cast<uint32>(Root.Get_Entity().Get_ID()),
                        Frame, Solve, First.Delta, Second.Delta, Receipt.Count, Second.Rays, Second.Envelopes, Second.Trials));
                };
                if (NOT RetryObserved)
                { EmitProof(TEXT("firstRetry")); }
                if (Moving && NOT MovingRetryObserved)
                { EmitProof(TEXT("movingRetry")); }
                RetryObserved = true;
                MovingRetryObserved |= Moving;
            }
        };
    }

    struct FFragment_TestPostsRecoveryProof
    {
        CK_GENERATED_BODY(FFragment_TestPostsRecoveryProof);
        TSharedPtr<test_posts_recovery::FProof> State;
    };

    // Only test observations and owned native terrain change here; no gait/foot/solver state is injected.
    struct FFragment_TestHeightPlanningObservation
    {
        CK_GENERATED_BODY(FFragment_TestHeightPlanningObservation);
        TFunction<void(FCk_Handle_ProceduralGait, bool, double)> Observe;
        bool PendingBefore = false;
        uint64 BeforeFrame = 0;
        uint64 BeforeSolve = 0;
    };

    class FProcessor_TestPostsRecoveryAttach : public ck_exp::TProcessor<
        FProcessor_TestPostsRecoveryAttach, FCk_Handle_ProceduralGait, TReadOnly<FFragment_ProceduralGait>>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_HandleRequests>;
        using TProcessor::TProcessor;

        static auto ForEachEntity(TimeType, HandleType InGait, const FFragment_ProceduralGait&) -> void
        {
            if (test_posts_recovery::CurrentTest() == nullptr
                || UCk_Utils_Handle_UE::Get_DebugName(InGait) != FName{TEXT("ProceduralAnimation.Posts.Beast4")}
                || InGait.Has<FFragment_TestPostsRecoveryProof>())
            { return; }
            auto* World = UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(InGait);
            if (ck::Is_NOT_Valid(World))
            { return; }
            if (InGait.Has<FFragment_TestHeightPlanningObservation>()
                && InGait.Get<FFragment_TestHeightPlanningObservation>().Observe)
            {
                test_posts_recovery::CurrentTest()->AddError(TEXT("Posts recovery attachment refuses to overwrite an existing observer"));
                return;
            }
            const auto Proof = MakeShared<test_posts_recovery::FProof>();
            Proof->Root = InGait;
            Proof->World = World;
            const auto WeakProof = TWeakPtr<test_posts_recovery::FProof>{Proof};
            Proof->EndHandle = FAutomationTestFramework::Get().OnTestEndEvent.AddLambda(
                [WeakProof](FAutomationTestBase* InTest)
                {
                    if (const auto State = WeakProof.Pin())
                    { State->Finalize(InTest); }
                });
            InGait.Add<FFragment_TestPostsRecoveryProof>().State = Proof;
            if (NOT InGait.Has<FFragment_ProceduralHeightPlanning_Test>())
            { InGait.Add<FFragment_ProceduralHeightPlanning_Test>(); }
            if (NOT InGait.Has<FFragment_TestHeightPlanningObservation>())
            { InGait.Add<FFragment_TestHeightPlanningObservation>(); }
            InGait.Get<FFragment_TestHeightPlanningObservation>().Observe =
                [Proof](FCk_Handle_ProceduralGait InRoot, bool InBefore, double) { Proof->Observe(InRoot, InBefore); };
        }
    };

    class FProcessor_TestHeightPlanningBeforeMotion : public ck_exp::TProcessor<
        FProcessor_TestHeightPlanningBeforeMotion, FCk_Handle_ProceduralGait,
        TReadWrite<FFragment_TestHeightPlanningObservation>,
        TReadOnly<FFragment_ProceduralGait>,
        TReadOnly<FFragment_SurfaceMotion_Params>,
        TReadOnly<FFragment_SurfaceMotion>,
        TReadOnly<FFragment_SurfaceMotion_Support>,
        TReadOnly<FFragment_Transform>,
        TExclude<FTag_SurfaceMotion_NeedsSetup>,
        TExclude<FFragment_SurfaceMotion_Failure>,
        TExclude<FTag_DestroyEntity_Initiate>,
        CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_HandleRequests, FProcessor_TestPostsRecoveryAttach>;
        using RunBefore = TDepList<FProcessor_SurfaceMotion_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType InDt,
            HandleType InGait,
            FFragment_TestHeightPlanningObservation& InObservation,
            const FFragment_ProceduralGait&,
            const FFragment_SurfaceMotion_Params&,
            const FFragment_SurfaceMotion&,
            const FFragment_SurfaceMotion_Support&,
            const FFragment_Transform&)
            -> void
        {
            if (InObservation.Observe)
            {
                if (auto* Test = FAutomationTestFramework::Get().GetCurrentTest())
                { Test->TestFalse(TEXT("Height observer never overwrites an unconsumed before token"), InObservation.PendingBefore); }
                InObservation.PendingBefore = true;
                InObservation.BeforeFrame = GFrameCounter;
                InObservation.BeforeSolve = InGait.Get<FFragment_ProceduralGait>().Get_SolveSequence();
                InObservation.Observe(InGait, true, InDt.Get_Seconds());
            }
        }
    };

    class FProcessor_TestHeightPlanningAfterMotion : public ck_exp::TProcessor<
        FProcessor_TestHeightPlanningAfterMotion, FCk_Handle_ProceduralGait,
        TReadWrite<FFragment_TestHeightPlanningObservation>,
        TReadOnly<FFragment_ProceduralGait>,
        TReadOnly<FFragment_SurfaceMotion_Params>,
        TReadOnly<FFragment_SurfaceMotion>,
        TReadOnly<FFragment_SurfaceMotion_Support>,
        TReadOnly<FFragment_Transform>>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_Update, FProcessor_TestHeightPlanningBeforeMotion>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType InDt,
            HandleType InGait,
            FFragment_TestHeightPlanningObservation& InObservation,
            const FFragment_ProceduralGait&,
            const FFragment_SurfaceMotion_Params&,
            const FFragment_SurfaceMotion&,
            const FFragment_SurfaceMotion_Support&,
            const FFragment_Transform&)
            -> void
        {
            if (InObservation.Observe && InObservation.PendingBefore)
            {
                const auto Frame = InObservation.BeforeFrame;
                const auto Solve = InObservation.BeforeSolve;
                InObservation.PendingBefore = false;
                const auto HasReceipt = InGait.Has<FFragment_ProceduralHeightPlanning_Test>();
                const auto Joined = HasReceipt && Frame == GFrameCounter
                    && InGait.Get<FFragment_ProceduralHeightPlanning_Test>().Frame == Frame
                    && InGait.Get<FFragment_ProceduralHeightPlanning_Test>().Solve == Solve;
                if (auto* Test = FAutomationTestFramework::Get().GetCurrentTest())
                { Test->TestTrue(TEXT("Height observer consumes the actual same-frame input-solve producer receipt"), Joined); }
                if (Joined)
                { InObservation.Observe(InGait, false, InDt.Get_Seconds()); }
            }
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestHeightPlanningBeforeMotion);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestHeightPlanningAfterMotion);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestPostsRecoveryAttach);

namespace ck_test_height_planning_native
{
    struct FHeightSeedRecord
    {
        uint64 W[40]{};
        auto
        Known(
            int32 Slot) const
            -> bool
        {
            return (W[0] & (uint64{1} << (Slot - 1))) != 0;
        }
        auto
        Point(
            int32 Slot,
            FVector& Out) const
            -> bool
        {
            double V[3]{};
            for (auto A = 0; A < 3; ++A)
            {
                if (NOT Known(Slot + A))
                {
                    return false;
                }
                FMemory::Memcpy(&V[A], &W[Slot + A], sizeof(double));
                if (NOT FMath::IsFinite(V[A]))
                {
                    return false;
                }
            }
            Out = FVector{V[0], V[1], V[2]};
            return true;
        }
    };

    struct FHeightSeedSink final : FOutputDevice
    {
        FCriticalSection Mutex;
        const FName DirectCategory{TEXT("LogTemp")};
        FHeightSeedRecord Rows[64]{};
        uint64 Key[6]{};
        uint64 Frame = 0;
        uint64 Solve = 0;
        int32 Count = 0;
        bool Invalid = false;
        bool Overflow = false;
        // Synchronous delivery; Serialize reads only fixed scalars while holding its mutex.
        auto
        CanBeUsedOnMultipleThreads() const
            -> bool override
        {
            return true;
        }
        auto
        Serialize(
            const TCHAR* Message,
            ELogVerbosity::Type,
            const FName& Category)
            -> void override
        {
            if (Category != DirectCategory || FCString::Strncmp(Message, TEXT("HST1 "), 5) != 0)
            {
                return;
            }
            const auto Lock = FScopeLock{&Mutex};
            if (FCString::Strlen(Message) != 684)
            {
                Invalid = true;
                return;
            }
            auto R = FHeightSeedRecord{};
            for (auto W = 0; W < 40; ++W)
            {
                for (auto D = 0; D < 16; ++D)
                {
                    const auto C = Message[5 + W * 17 + D];
                    const auto N = C >= '0' && C <= '9' ? C - '0' : C >= 'a' && C <= 'f' ? C - 'a' + 10 : -1;
                    if (N < 0)
                    {
                        Invalid = true;
                        return;
                    }
                    R.W[W] = (R.W[W] << 4) | static_cast<uint64>(N);
                }
                if (W != 39 && Message[5 + W * 17 + 16] != ' ')
                {
                    Invalid = true;
                    return;
                }
            }
            for (auto S = 1; S <= 8; ++S)
            {
                if (NOT R.Known(S))
                {
                    Invalid = true;
                    return;
                }
            }
            // One fixed world/registry/root/leg identity for this entire sink lifetime.
            for (auto S = 1; S <= 6; ++S)
            {
                if (R.W[S] != Key[S - 1])
                {
                    Invalid = true;
                    return;
                }
            }
            if (R.W[7] != Frame || R.W[8] != Solve || (R.W[0] >> 38) != 0 || NOT R.Known(36) || NOT R.Known(37) ||
                NOT R.Known(38) || R.W[37] != 1)
            {
                Invalid = true;
                return;
            }
            if (R.W[38] == 1)
            {
                Overflow = true;
                return;
            }
            if (R.W[38] != 0 || Count == 64 || R.W[36] != static_cast<uint64>(Count + 1))
            {
                Invalid = true;
                return;
            }
            for (auto S = 1; S < 40; ++S)
            {
                if (NOT R.Known(S) && R.W[S] != 0)
                {
                    Invalid = true;
                    return;
                }
            }
            Rows[Count++] = R;
        }
    };

    enum class EScenario : uint8 { Preparatory, BlockedPartial, StaleContact, Ordinary, NondefaultTarget, DiscardedStraight };

    constexpr auto SupportTop = 75.0;
    constexpr auto Clearance = 64.0;
    constexpr auto InitialBodyZ = SupportTop + Clearance;
    constexpr auto FrontRestX = 45.0;
    constexpr auto RearRestX = -100.0;

    struct FState;
    auto ComposeMotion(FState& S) -> void;

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UWorld> World;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_SurfaceMotion Motion;
        FCk_Handle_ProceduralBodyPose Pose;
        TStrongObjectPtr<UCk_ProceduralRig_Data> Rig;
        TStrongObjectPtr<UCk_ProceduralGait_Data> Preset;
        TArray<FCk_Handle_ProceduralLeg> Legs;
        TArray<JPH::BodyID> NativeBodies;
        TArray<JPH::BodyID> PlatformIds;
        TArray<FVector> PlatformTops;
        JPH::BodyID EnvelopeBlocker;
        float DefaultTargetFraction = 0.0f;
        float ConstructedTargetFraction = 0.0f;
        uint64 ExpectedDispatchFrame = 0;
        uint64 ExpectedDispatchSolve = 0;
        int32 DispatchReceiptSamples = 0;
        int32 CooperativeThirdSamples = 0;
        int32 InteriorFourthSamples = 0;
        bool MotionMature = false;
        bool OrdinaryBlockerRemoved = false;
        TArray<FCk_ProceduralLeg_Foot> FeetBefore;
        TArray<ck::FProceduralGaitCommittedLanding> LandingsBefore;
        EScenario Scenario = EScenario::Preparatory;
        FVector Origin = FVector::ZeroVector;
        FTransform Before = FTransform::Identity;
        FTransform Partial = FTransform::Identity;
        FTransform Full = FTransform::Identity;
        FTransform AcceptedInput = FTransform::Identity;
        FVector GoalBefore = FVector::ZeroVector;
        uint64 BeforeFrame = 0;
        uint64 AcceptedFrame = 0;
        int32 WitnessRays = 0;
        int32 WitnessQueries = 0;
        int32 Samples = 0;
        bool Driving = false;
        bool Witness = false;
        bool SeedFailureChecked = false;
        bool Mutated = false;
        bool Accepted = false;
        bool Complete = false;
        bool Failed = false;
        bool FixedTime = false;
        bool OriginalFixedTime = false;
        double OriginalFixedDelta = 0.0;

        ~FState()
        {
            if (FixedTime)
            {
                FApp::SetUseFixedTimeStep(OriginalFixedTime);
                FApp::SetFixedDeltaTime(OriginalFixedDelta);
            }
        }

        auto
        Require(
            bool InCondition,
            const TCHAR* InMessage)
            -> bool
        {
            if (NOT InCondition)
            {
                Test->AddError(FString::Printf(TEXT("Height scenario%d: %s"), static_cast<int32>(Scenario), InMessage));
                Failed = true;
            }
            return InCondition;
        }

        auto
        AddBox(
            const FVector& InCenter,
            const FVector& InHalfExtents)
            -> bool
        {
            if (NOT World.IsValid())
            { return false; }
            auto* Jolt = World->GetSubsystem<UCk_Jolt_Subsystem>();
            if (ck::Is_NOT_Valid(Jolt))
            { return false; }
            const auto Physics = Jolt->Get_PhysicsSystem().Pin();
            const auto Signature = ck::jolt::TryDerive_SignatureFromProfile(TEXT("BlockAll"), ECk_Jolt_BodyDomain::Static);
            if (NOT Physics.IsValid() || NOT Signature.IsSet())
            { return false; }
            const auto Layer = JPH::ObjectLayer{Jolt->Get_LayerTable().Get_OrRegisterLayer(*Signature)};
            auto Shape = JPH::Ref<JPH::Shape>{new JPH::BoxShape{JPH::Vec3{
                static_cast<float>(InHalfExtents.X), static_cast<float>(InHalfExtents.Y), static_cast<float>(InHalfExtents.Z)}}};
            const auto Id = Physics->GetBodyInterface().CreateAndAddBody(JPH::BodyCreationSettings{Shape.GetPtr(),
                JPH::RVec3{static_cast<float>(InCenter.X), static_cast<float>(InCenter.Y), static_cast<float>(InCenter.Z)},
                JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layer},
                JPH::EActivation::DontActivate);
            if (Id.IsInvalid())
            { return false; }
            NativeBodies.Add(Id);
            return true;
        }


        auto
        Get_IsMatureSupport(
            const FTransform& InCurrent,
            int32 InExcludedPlatform = INDEX_NONE)
            -> bool
        {
            if (Legs.Num() != 4 || PlatformIds.Num() != 4 || PlatformTops.Num() != 4
                || NOT Gait.Has<ck::FFragment_ProceduralGait_FeetPlane>())
            { return false; }
            const auto Debug = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(Gait);
            if (NOT Debug.Get_Status().Get_HasAcceptedSample() || Debug.Get_Gait().Get_Airborne()
                || Debug.Get_Sample().Get_FrameNumber() != GFrameCounter - 1)
            { return false; }
            const auto Plane = UCk_Utils_ProceduralGait_UE::Get_FeetPlane(Gait);
            if (Plane.Get_State() != ECk_ProceduralGait_FeetPlane::Fitted
                || NOT Plane.Get_Normal().Equals(FVector::UpVector, 1.0e-3)
                || NOT FMath::IsNearlyEqual(Plane.Get_Point().Z, Origin.Z + SupportTop, 1.0e-3))
            { return false; }
            const auto& Feet = Gait.Get<ck::FFragment_ProceduralGait_FeetPlane>().Get_Support();
            if (Feet.Get_Point().ContainsNaN() || Feet.Get_Normal().ContainsNaN() || Feet.Get_Origin().ContainsNaN()
                || Feet.Get_Basis().ContainsNaN() || NOT Feet.Get_Basis().IsNormalized()
                || Feet.Get_FootprintMin().ContainsNaN() || Feet.Get_FootprintMax().ContainsNaN())
            { return false; }
            const auto Local = Feet.Get_Basis().Inverse().RotateVector(InCurrent.GetLocation() - Feet.Get_Origin());
            constexpr auto Margin = Clearance * 0.25;
            if (Local.X < Feet.Get_FootprintMin().X - Margin || Local.X > Feet.Get_FootprintMax().X + Margin
                || Local.Y < Feet.Get_FootprintMin().Y - Margin || Local.Y > Feet.Get_FootprintMax().Y + Margin)
            { return false; }
            auto Minimum = FVector2D{TNumericLimits<double>::Max(), TNumericLimits<double>::Max()};
            auto Maximum = FVector2D{-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max()};
            auto RemainingFeet = TArray<ck::FProceduralFeetPlaneFoot>{};
            auto Count = 0;
            for (auto Index = 0; Index < Legs.Num(); ++Index)
            {
                if (Index == InExcludedPlatform)
                { continue; }
                if (ck::Is_NOT_Valid(Legs[Index]) || NOT NativeBodies.Contains(PlatformIds[Index]))
                { return false; }
                const auto Foot = UCk_Utils_ProceduralLeg_UE::Get_Foot(Legs[Index]);
                if (Foot.Get_Phase() != ECk_ProceduralLeg_FootPhase::Planted
                    || Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted
                    || Foot.Get_Position().ContainsNaN() || Foot.Get_Normal().ContainsNaN())
                { return false; }
                const auto Difference = Foot.Get_Position() - PlatformTops[Index];
                if (FMath::Abs(Difference.X) >= 8.0 || FMath::Abs(Difference.Y) >= 8.0 || FMath::Abs(Difference.Z) > 1.0e-3)
                { return false; }
                const auto Point = Feet.Get_Basis().Inverse().RotateVector(Foot.Get_Position() - Feet.Get_Origin());
                const auto Normal = Feet.Get_Basis().Inverse().RotateVector(Foot.Get_Normal()).GetSafeNormal();
                RemainingFeet.Emplace(Point, static_cast<float>(FMath::Max(0.0, Normal.Z)));
                Minimum.X = FMath::Min(Minimum.X, Point.X); Minimum.Y = FMath::Min(Minimum.Y, Point.Y);
                Maximum.X = FMath::Max(Maximum.X, Point.X); Maximum.Y = FMath::Max(Maximum.Y, Point.Y);
                ++Count;
            }
            auto RemainingPlane = ck::FProceduralFeetPlane{};
            if (ck::FitProceduralFeetPlane(RemainingFeet, ck::ProceduralFeetPlaneMaxAngleDegrees, RemainingPlane)
                != ck::EProceduralFeetPlaneResult::Fitted)
            { return false; }
            const auto RemainingPoint = Feet.Get_Origin() + Feet.Get_Basis().RotateVector(RemainingPlane.Get_Point());
            const auto RemainingNormal = Feet.Get_Basis().RotateVector(RemainingPlane.Get_Normal());
            // This independent pure fit does not publish a plane or replace any native-contact proof.
            return Count >= 3 && RemainingNormal.Equals(FVector::UpVector, 1.0e-3)
                && FMath::IsNearlyEqual(RemainingPoint.Z, Origin.Z + SupportTop, 1.0e-3)
                && Local.X >= Minimum.X - Margin && Local.X <= Maximum.X + Margin
                && Local.Y >= Minimum.Y - Margin && Local.Y <= Maximum.Y + Margin
                && InCurrent.GetLocation().Z <= RemainingPoint.Z + Clearance + 1.0e-3;
        }

        auto
        Get_HasSufficientPlanningInput(
            const FTransform& InCurrent)
            -> bool
        {
            const auto& State = Gait.Get<ck::FFragment_ProceduralGait>();
            const auto& Planning = State.Get_HeightPlanning();
            const auto& Stance = State.Get_ReachStance();
            const auto& Intent = Gait.Get<ck::FFragment_SurfaceMotion>();
            const auto& Support = Gait.Get<ck::FFragment_SurfaceMotion_Support>();
            if (NOT Get_IsMatureSupport(InCurrent) || NOT Planning.Get_HasSample()
                || Planning.Get_Frame() != GFrameCounter - 1 || Planning.Get_SolveSequence() != State.Get_SolveSequence()
                || NOT Stance.Get_HasSample() || Stance.Get_SolveSequence() != State.Get_SolveSequence()
                || NOT Planning.Get_BodyAtSolve().Equals(InCurrent, 1.0e-3) || NOT Stance.Get_BodyAtSolve().Equals(InCurrent, 1.0e-3)
                || Support.Get_EvaluatedFrame() != Planning.Get_Frame() || NOT Support.Get_EvaluatedBody().Equals(InCurrent, 1.0e-3)
                || NOT Planning.Get_Direction().Equals(Intent.Get_Direction(), 1.0e-3)
                || NOT FMath::IsNearlyEqual(Planning.Get_Speed(), Intent.Get_Speed())
                || UCk_Utils_SurfaceMotion_UE::Get_Support(Motion) != ECk_SurfaceMotion_Support::Grounded
                || Stance.Get_Anchors().IsEmpty())
            { return false; }
            for (const auto& Anchor : Stance.Get_Anchors())
            {
                if (ck::Is_NOT_Valid(Anchor.Get_Leg())
                    || UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Anchor.Get_Leg()) != Gait.ConvertToHandle())
                { return false; }
                const auto Foot = UCk_Utils_ProceduralLeg_UE::Get_Foot(Anchor.Get_Leg());
                if (Foot.Get_Phase() != ECk_ProceduralLeg_FootPhase::Planted
                    || Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted
                    || NOT Foot.Get_Position().Equals(Anchor.Get_FootWorld(), 1.0e-3))
                { return false; }
            }
            const auto Pace = Support.Get_ReachPaceState();
            return Pace != ECk_SurfaceMotion_ReachPaceState::PhysicalOverride
                && (Support.Get_AdaptiveHeightActive() || Pace == ECk_SurfaceMotion_ReachPaceState::Pacing
                    || Pace == ECk_SurfaceMotion_ReachPaceState::Blocked);
        }

        auto
        CheckDiscardedStraight(
            const FTransform& InCurrent,
            const TOptional<FTransform>& InOffset,
            const ck::FProceduralGaitHeightCandidate& InCandidate)
            -> bool
        {
            const auto& Tunables = Gait.Get<ck::FFragment_ProceduralGait_Tunables>();
            const auto& Probe = Tunables.Get_Probe();
            const auto Up = InCurrent.GetRotation().GetAxisZ();
            const auto Point = InCandidate.Get_PointWorld();
            const auto Force = Tunables.Get_Step().Get_ForceStepReachFraction() * InCandidate.Get_Reach();
            const auto Target = Tunables.Get_Step().Get_TargetReachFraction() * InCandidate.Get_Reach();
            const auto Radial = FVector::VectorPlaneProject(Point - InCurrent.GetLocation(), Up).GetSafeNormal();
            const auto Axis = ck::FProceduralGaitSolver::ComputeTraceAxis(Up, Radial, Probe.Get_OutwardLean());
            const auto Crossing = Point + Axis * (0.8 * Probe.Get_Up());
            auto Half = FVector{4.0, 4.0, Probe.Get_Up() * 0.25};
            const auto ThinAxis = FMath::Abs(Axis.X) > FMath::Abs(Axis.Y) ? 0 : 1;
            Half[ThinAxis] = 1.0;
            if (NOT Require(Up.Equals(FVector::UpVector, 1.0e-9) && InCandidate.Get_NormalWorld().Equals(Up, 1.0e-3) &&
                                Probe.Get_OutwardLean() > 0.0 && Probe.Get_Up() > 0.0 &&
                                FMath::Abs(Crossing[ThinAxis] - Point[ThinAxis]) > Half[ThinAxis] + 2.0 &&
                                Tunables.Get_Foothold().Get_MaxAngle() == 60.0f,
                            TEXT("Single authored fallback wall stays off the unchanged real sample column")))
            {
                return false;
            }
            const auto Physics = World->GetSubsystem<UCk_Jolt_Subsystem>()->Get_PhysicsSystem().Pin();
            if (NOT Require(Physics.IsValid() && GLog != nullptr,
                            TEXT("Fallback native world and direct receipt sink exist")))
            {
                return false;
            }
            if (NOT Require(AddBox(Crossing, Half), TEXT("Single native wall is added after the real level sample")))
            {
                return false;
            }
            const auto Wall = NativeBodies.Last();
            ON_SCOPE_EXIT
            {
                Physics->GetBodyInterface().RemoveBody(Wall);
                Physics->GetBodyInterface().DestroyBody(Wall);
                NativeBodies.RemoveSingle(Wall);
            };
            auto Goal = InCurrent;
            const auto HipBefore = Goal.TransformPosition(InCandidate.Get_HipLocal());
            Goal.AddToTranslation(Up * (Point.Z + Force * 1.10 - HipBefore.Z));
            const auto Hip = Goal.TransformPosition(InCandidate.Get_HipLocal());
            const auto PublicLegs = UCk_Utils_ProceduralGait_UE::Get_Legs(Gait);
            const auto LegIndex = PublicLegs.IndexOfByKey(InCandidate.Get_Leg());
            if (NOT Require(LegIndex != INDEX_NONE && LegIndex < 4,
                            TEXT("Real candidate retains its composed public leg identity")))
            {
                return false;
            }
            auto Root = Gait.ConvertToHandle();
            const auto OldName = UCk_Utils_Handle_UE::Get_DebugName(Root);
            auto Sink = FHeightSeedSink{};
            const auto Registry = Root.Get_RegistryView().Get_RegistryHandle();
            Sink.Key[0] = reinterpret_cast<UPTRINT>(World.Get());
            Sink.Key[1] = static_cast<uint32>(Registry.SlotIndex);
            Sink.Key[2] = static_cast<uint32>(Registry.Generation);
            Sink.Key[3] = static_cast<uint32>(Root.Get_Entity().Get_ID());
            Sink.Key[4] = static_cast<uint32>(InCandidate.Get_Leg().Get_Entity().Get_ID());
            Sink.Key[5] = static_cast<uint64>(LegIndex);
            Sink.Frame = GFrameCounter;
            Sink.Solve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
            GLog->AddOutputDevice(&Sink);
            UCk_Utils_Handle_UE::Set_DebugName(Root, TEXT("CkAutomationHeightSeed"));
            auto Registered = true;
            ON_SCOPE_EXIT
            {
                UCk_Utils_Handle_UE::Set_DebugName(Root, OldName);
                if (Registered)
                {
                    GLog->RemoveOutputDevice(&Sink);
                }
            };
            auto ControlRays = 0;
            const auto Control = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Goal, InOffset, InCandidate, ControlRays,
                                                                        2048, ck::EProceduralHeightPlanStage::Landing);
            auto Cause = ck::EProceduralHeightSeedFailure::TrustedDownFinalForceReject;
            auto CauseRays = 0;
            const auto WithCause = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Goal, InOffset, InCandidate, CauseRays,
                                                                          2048, ck::EProceduralHeightPlanStage::Landing,
                                                                          nullptr, nullptr, &Cause);
            WitnessRays += ControlRays + CauseRays;
            UCk_Utils_Handle_UE::Set_DebugName(Root, OldName);
            GLog->RemoveOutputDevice(&Sink);
            Registered = false;
            const auto Lock = FScopeLock{&Sink.Mutex};
            if (NOT Require(
                    NOT Sink.Invalid && NOT Sink.Overflow && Sink.Count == 2,
                    TEXT("Two exact synchronous HST1 rows qualify the scene; missing or malformed receipts fail")) ||
                NOT Require(
                    NOT Control && Control == WithCause && ControlRays > 0 && ControlRays == CauseRays &&
                        Cause == ck::EProceduralHeightSeedFailure::None,
                    TEXT("Discarded straight fallback has no force cause and optional output preserves native work")))
            {
                return false;
            }
            for (auto Index = 0; Index < 2; ++Index)
            {
                const auto& Row = Sink.Rows[Index];
                for (const auto Slot : {9, 10, 11, 12, 13, 14, 15, 16, 26, 27, 28, 29, 30, 35})
                {
                    if (NOT Require(Row.Known(Slot),
                                    TEXT("Actual fallback decision fields are known, never invented defaults")))
                    {
                        return false;
                    }
                }
                auto Initial = FVector{};
                auto Straight = FVector{};
                auto Selected = FVector{};
                auto Final = FVector{};
                if (NOT Require(Row.Point(17, Initial) && Row.Point(20, Straight) && Row.Point(23, Selected) &&
                                    Row.Point(32, Final),
                                TEXT("Actual trusted, straight, selected and final native points are finite")))
                {
                    return false;
                }
                if (NOT Require(
                        Row.W[9] == static_cast<uint64>(ck::EProceduralHeightPlanStage::Landing) && Row.W[10] == 0 &&
                            Row.W[11] == static_cast<uint64>(ControlRays) && Row.W[12] == 1 && Row.W[13] == 1 &&
                            Row.W[14] == 1 && Row.W[15] == 0 && Row.W[16] == 0 && Initial.Equals(Selected, 1.0e-6) &&
                            Final.Equals(Selected, 1.0e-6) && FVector::Dist(Hip, Initial) <= Target &&
                            FVector::Dist(Hip, Straight) > Force && FMath::Abs(Initial.Z - Point.Z) > 2.0 &&
                            FMath::Abs(Straight.Z - Point.Z) < 1.0e-3 && Row.W[26] == 1 &&
                            Row.W[27] == static_cast<uint64>(ck::EProceduralFootholdVerdict::TooSteep) &&
                            Row.W[28] == static_cast<uint64>(ck::EProceduralHeightSeedFailure::None) &&
                            Row.W[29] == 0 && Row.W[30] == static_cast<uint64>(Index) && Row.W[35] == 0 &&
                            Row.Known(31) == (Index == 1) &&
                            (Index == 0 || Row.W[31] == static_cast<uint64>(ck::EProceduralHeightSeedFailure::None)),
                        TEXT("Actual face branch discards out-of-force floor and retains TooSteep face without seed "
                             "cause")))
                {
                    return false;
                }
            }
            for (auto Slot = 1; Slot < 40; ++Slot)
            {
                if (Slot == 30 || Slot == 31 || Slot == 36)
                {
                    continue;
                }
                if (NOT Require(Sink.Rows[0].Known(Slot) == Sink.Rows[1].Known(Slot) &&
                                    Sink.Rows[0].W[Slot] == Sink.Rows[1].W[Slot],
                                TEXT("Null and nonnull cause calls have identical actual native branch receipts")))
                {
                    return false;
                }
            }
            return true;
        }

        auto
        CheckSeedFailure(
            const FTransform& InCurrent,
            const FTransform& InAdmitted,
            const TOptional<FTransform>& InOffset,
            const ck::FProceduralGaitHeightCandidate& InCandidate)
            -> bool
        {
            using EFailure = ck::EProceduralHeightSeedFailure;
            using EStage = ck::EProceduralHeightPlanStage;
            const auto& Tunables = Gait.Get<ck::FFragment_ProceduralGait_Tunables>();
            const auto& Probe = Tunables.Get_Probe();
            const auto Up = InCurrent.GetRotation().GetAxisZ();
            const auto ActualTargetFraction = Tunables.Get_Step().Get_TargetReachFraction();
            if (NOT Require(ActualTargetFraction == ConstructedTargetFraction
                    && (Scenario != EScenario::NondefaultTarget || ActualTargetFraction != DefaultTargetFraction),
                    TEXT("Native seed causes use the actual fresh-preset target fraction, including the nondefault control")))
            { return false; }
            Test->AddInfo(FString::Printf(TEXT("HeightSeedNative scenario%d target%.9g default%.9g force%.9g reach%.9g probeUp%.9g probeDown%.9g"),
                static_cast<int32>(Scenario), ActualTargetFraction, DefaultTargetFraction,
                Tunables.Get_Step().Get_ForceStepReachFraction(), InCandidate.Get_Reach(),
                Probe.Get_Up(), Probe.Get_Down()));
            const auto TargetReach = ActualTargetFraction * InCandidate.Get_Reach();
            const auto ForceReach = Tunables.Get_Step().Get_ForceStepReachFraction() * InCandidate.Get_Reach();
            if (NOT Require(Up.Equals(FVector::UpVector, 1.0e-9) && FMath::IsFinite(TargetReach)
                    && TargetReach > 0.0f && ForceReach > TargetReach && FMath::IsFinite(Probe.Get_Down())
                    && Probe.Get_Down() > 0.0f, TEXT("Native seed-cause fixture has actual finite probe/reach settings")))
            { return false; }
            // Independent witnesses reproduce the public probe geometry, not its admission or cause classifier.
            // These rays are test work and never contribute to the production caller's ray counter.
            const auto ProbeNative = [&](const FTransform& InGoal, const FVector& InPoint, float InLean)
                -> TOptional<FCk_Jolt_HitResult>
            {
                const auto Radial = FVector::VectorPlaneProject(InPoint - InGoal.GetLocation(), Up).GetSafeNormal();
                for (auto Attempt = 0; Attempt < ck::ProceduralGroundProbeAttempts; ++Attempt)
                {
                    const auto Span = ck::MakeProceduralGroundProbeSpan(Attempt, Probe.Get_Up(), Probe.Get_Down(), InLean);
                    const auto Axis = ck::FProceduralGaitSolver::ComputeTraceAxis(Up, Radial, Span.Get_OutwardLean());
                    const auto Hit = UCk_Utils_JoltQuery_UE::Get_RayCast(World.Get(),
                        InPoint + Axis * Span.Get_UpDistance(), InPoint - Axis * Span.Get_DownDistance(),
                        Probe.Get_QueryFilter());
                    ++WitnessRays;
                    if (Hit.Get_HasHit() && Hit.Get_Fraction() > 0.0f && Hit.Get_Fraction() <= 1.0f
                        && NOT Hit.Get_Position().ContainsNaN() && NOT Hit.Get_Normal().ContainsNaN()
                        && FVector::DotProduct(Hit.Get_Normal(), -Axis) < 0.0)
                    { return Hit; }
                }
                return {};
            };
            auto Failure = EFailure::TrustedDownFinalForceReject;
            auto LastComparedRays = 0;
            const auto Compare = [&](const FTransform& InGoal, EStage InStage, EFailure InExpectedFailure,
                TOptional<bool> InExpectedResult = {}, const ck::FProceduralGaitHeightCandidate* InObserved = nullptr) -> bool
            {
                const auto& Candidate = InObserved != nullptr ? *InObserved : InCandidate;
                auto ControlRays = 0;
                const auto Control = ck::Get_IsProceduralHeightPlanAdmitted(Gait, InGoal, InOffset, Candidate,
                    ControlRays, 2048, InStage);
                WitnessRays += ControlRays;
                auto CauseRays = 0;
                Failure = EFailure::TrustedDownFinalForceReject;
                const auto WithCause = ck::Get_IsProceduralHeightPlanAdmitted(Gait, InGoal, InOffset, Candidate,
                    CauseRays, 2048, InStage, nullptr, nullptr, &Failure);
                WitnessRays += CauseRays;
                LastComparedRays = CauseRays;
                return Require(Control == WithCause && ControlRays == CauseRays && CauseRays > 0,
                        TEXT("Optional seed cause preserves this exact native result and ray count"))
                    && Require(NOT InExpectedResult.IsSet() || WithCause == InExpectedResult.GetValue(),
                        TEXT("Qualified native seed scenario returns its required result"))
                    && Require(Failure == InExpectedFailure, TEXT("Landing and Recovery publish only their exact stage-specific native cause"));
            };
            if (NOT Compare(InAdmitted, EStage::Landing, EFailure::None, TOptional<bool>{true}))
            { return false; }
            if (NOT Require(InOffset.IsSet(), TEXT("Outer posed-reach control uses the real composed pose offset")))
            { return false; }
            auto FarOffset = InOffset;
            FarOffset->AddToTranslation(Up * 10000.0);
            auto OuterControlRays = 0;
            const auto OuterControl = ck::Get_IsProceduralHeightPlanAdmitted(Gait, InAdmitted, FarOffset, InCandidate,
                OuterControlRays, 2048);
            WitnessRays += OuterControlRays;
            auto OuterCauseRays = 0;
            Failure = EFailure::TrustedInitialDownClampReprobeMiss;
            const auto OuterWithCause = ck::Get_IsProceduralHeightPlanAdmitted(Gait, InAdmitted, FarOffset, InCandidate,
                OuterCauseRays, 2048, EStage::Landing, nullptr, nullptr, &Failure);
            WitnessRays += OuterCauseRays;
            if (NOT Require(NOT OuterControl && NOT OuterWithCause && OuterControlRays == OuterCauseRays
                    && OuterCauseRays > 0 && Failure == EFailure::None,
                    TEXT("The same admitted native seed rejected only by outer posed reach publishes no seed cause")))
            { return false; }
            auto High = InCurrent;
            High.AddToTranslation(Up * 10000.0);
            const auto Initial = ProbeNative(High, InCandidate.Get_PointWorld(), Probe.Get_OutwardLean());
            if (NOT Require(Initial.IsSet() && Initial->Get_Normal().Z > 0.99,
                    TEXT("High-goal initial down probe has an actual trusted level native hit")))
            { return false; }
            const auto ClampPoint = [&](const FTransform& InGoal, const FVector& InHit) -> FVector
            {
                const auto Basis = InGoal.GetRotation();
                return Basis.RotateVector(ck::FProceduralGaitSolver::ClampToReach(
                    Basis.Inverse().RotateVector(InGoal.TransformPosition(InCandidate.Get_HipLocal())),
                    Basis.Inverse().RotateVector(InHit), TargetReach));
            };
            const auto HighClamp = ClampPoint(High, Initial->Get_Position());
            if (NOT Require(FVector::Dist(High.TransformPosition(InCandidate.Get_HipLocal()), Initial->Get_Position()) > TargetReach
                    && NOT ProbeNative(High, HighClamp, 0.0f).IsSet(),
                    TEXT("Actual trusted initial hit clamps to a real straight-reprobe miss at the high hip")))
            { return false; }
            if (NOT Compare(High, EStage::Landing, EFailure::TrustedInitialDownClampReprobeMiss, TOptional<bool>{false})
                || NOT Compare(High, EStage::Recovery, EFailure::None, TOptional<bool>{false})
                || NOT Compare(InAdmitted, EStage::Recovery, EFailure::None)
                || NOT Compare(InAdmitted, EStage::Transit, EFailure::None))
            { return false; }
            auto Raised = InCurrent;
            const auto Hip = InCurrent.TransformPosition(InCandidate.Get_HipLocal());
            // The retained tops and ordinary floor already belong to this fixture. Use actual tunables to place
            // the virtual hip beyond force reach while the clamped straight probe can still reach that geometry.
            Raised.AddToTranslation(Up * (Origin.Z + SupportTop + ForceReach + Probe.Get_Down() * 0.5 - Hip.Z));
            const auto RaisedInitial = ProbeNative(Raised, InCandidate.Get_PointWorld(), Probe.Get_OutwardLean());
            if (NOT Require(RaisedInitial.IsSet() && RaisedInitial->Get_Normal().Z > 0.99,
                    TEXT("Force-reject setup retains an actual trusted initial down hit")))
            { return false; }
            const auto RaisedHit = ProbeNative(Raised, ClampPoint(Raised, RaisedInitial->Get_Position()), 0.0f);
            if (NOT Require(FVector::Dist(Raised.TransformPosition(InCandidate.Get_HipLocal()), RaisedInitial->Get_Position()) > TargetReach
                    && RaisedHit.IsSet() && RaisedHit->Get_Normal().Z > 0.99
                    && FVector::Dist(Raised.TransformPosition(InCandidate.Get_HipLocal()), RaisedHit->Get_Position()) > ForceReach,
                    TEXT("Clamped reprobe actually hits native level geometry beyond the unchanged force limit")))
            { return false; }
            if (NOT Compare(Raised, EStage::Landing, EFailure::TrustedDownFinalForceReject, TOptional<bool>{false})
                || NOT Compare(Raised, EStage::Recovery, EFailure::TrustedDownFinalForceReject, TOptional<bool>{false}))
            { return false; }
            auto BudgetControlRays = 2048;
            const auto BudgetControl = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Raised, InOffset, InCandidate,
                BudgetControlRays, 2048, EStage::Recovery);
            auto BudgetCauseRays = 2048;
            Failure = EFailure::TrustedDownFinalForceReject;
            const auto BudgetWithCause = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Raised, InOffset, InCandidate,
                BudgetCauseRays, 2048, EStage::Recovery, nullptr, nullptr, &Failure);
            if (NOT Require(NOT BudgetControl && NOT BudgetWithCause && BudgetControlRays == 2048
                    && BudgetCauseRays == 2048 && Failure == EFailure::None,
                    TEXT("Actual Recovery remaining-budget preflight rejects without a native ray or retained cause")))
            { return false; }

            // Remove only an owned support body synchronously, leaving the live published sample unchanged.
            // Qualify both seed branches independently through production admission before asserting the aggregate.
            const auto Roster = UCk_Utils_ProceduralGait_UE::Get_Legs(Gait);
            const auto& Observations = Gait.Get<ck::FFragment_ProceduralGait>().Get_HeightPlanning().Get_Candidates();
            const ck::FProceduralGaitHeightCandidate* TopSeed = nullptr;
            const ck::FProceduralGaitHeightCandidate* FloorSeed = nullptr;
            auto FirstLegSeeds = 0;
            for (const auto& Observed : Observations)
            {
                if (Roster.IsEmpty() || Observed.Get_Leg() != Roster[0])
                { continue; }
                ++FirstLegSeeds;
                if (FMath::IsNearlyEqual(Observed.Get_PointWorld().Z, Origin.Z + SupportTop, 1.0e-3))
                { TopSeed = &Observed; }
                else if (FMath::IsNearlyEqual(Observed.Get_PointWorld().Z, Origin.Z, 1.0e-3))
                { FloorSeed = &Observed; }
            }
            if (NOT Require(FirstLegSeeds == 2 && TopSeed != nullptr && FloorSeed != nullptr
                    && Legs.Num() == 4 && Roster[0] == Legs[0] && PlatformIds.Num() == 4,
                    TEXT("Mixed Recovery coverage requires the real first leg's published owned-top and floor alternatives")))
            { return false; }
            const auto Physics = World->GetSubsystem<UCk_Jolt_Subsystem>()->Get_PhysicsSystem().Pin();
            if (NOT Require(Physics.IsValid(), TEXT("Owned terrain is available for a synchronous freshness withdrawal")))
            { return false; }
            auto& Bodies = Physics->GetBodyInterface();
            Bodies.RemoveBody(PlatformIds[0]);
            auto Restored = false;
            ON_SCOPE_EXIT
            {
                if (NOT Restored)
                { Bodies.AddBody(PlatformIds[0], JPH::EActivation::DontActivate); }
            };
            if (NOT Compare(Raised, EStage::Landing, EFailure::None, TOptional<bool>{false}, TopSeed)
                || NOT Require(LastComparedRays == 1, TEXT("Withdrawn published top fails the actual one-ray freshness branch"))
                || NOT Compare(Raised, EStage::Landing, EFailure::TrustedDownFinalForceReject, TOptional<bool>{false}, FloorSeed)
                || NOT Compare(Raised, EStage::Recovery, EFailure::None, TOptional<bool>{false}))
            { return false; }
            Bodies.AddBody(PlatformIds[0], JPH::EActivation::DontActivate);
            Restored = true;
            Test->AddInfo(FString::Printf(TEXT("HeightRecoveryNative scenario%d allForce1 clampMissNone1 mixedFreshnessForceNone1 budgetNoRays1"),
                static_cast<int32>(Scenario)));
            return Compare(InAdmitted, EStage::Landing, EFailure::None, TOptional<bool>{true});
        }

        auto
        CheckDispatchReceipt()
            -> bool
        {
            if (NOT Require(Gait.Has<ck::FFragment_ProceduralHeightPlanning_Test>(),
                    TEXT("Composed native scene retains its test-attached actual dispatch receipt")))
            { return false; }
            const auto& Receipt = Gait.Get<ck::FFragment_ProceduralHeightPlanning_Test>();
            if (NOT Require(NOT Receipt.Overflow && Receipt.Count >= 0 && Receipt.Count <= 4,
                    TEXT("Actual Surface dispatch never publishes overflow or a fifth recorded attempt")))
            { return false; }
            if (Receipt.Count == 0)
            { return true; }
            if (NOT Require(Receipt.Frame == GFrameCounter && ExpectedDispatchFrame == GFrameCounter
                    && Receipt.Solve == ExpectedDispatchSolve,
                    TEXT("Actual dispatch receipt belongs to this Surface frame and composed planning solve")))
            { return false; }
            ++DispatchReceiptSamples;
            auto PreviousRays = 0;
            auto PreviousEnvelopes = 0;
            auto PreviousTrials = 0;
            for (auto Index = 0; Index < Receipt.Count; ++Index)
            {
                const auto& Attempt = Receipt.Attempts[Index];
                if (NOT Require(Attempt.Completed && Attempt.Route <= 3
                        && Attempt.Trials > PreviousTrials && Attempt.Trials <= 4
                        && Attempt.Rays >= PreviousRays && Attempt.Rays <= 2048
                        && Attempt.Envelopes >= PreviousEnvelopes && Attempt.Envelopes <= 64,
                        TEXT("Actual completed calls retain monotonic charged budgets within four/2048/64")))
                { return false; }
                if (NOT Require(Attempt.Accepted || Attempt.BodyBefore.Equals(Attempt.BodyAfter, 1.0e-9),
                        TEXT("Rejected actual height admission publishes no partial body transform"))
                    || NOT Require(NOT Attempt.Accepted || Index == Receipt.Count - 1,
                        TEXT("Actual success terminates the charged attempt sequence")))
                { return false; }
                if (Attempt.Accepted && Attempt.Trials <= 2)
                {
                    if (NOT Require(NOT Receipt.CooperativeThirdOffered,
                            TEXT("An actual first/second success preserves priority over cooperative replacement")))
                    { return false; }
                }
                PreviousRays = Attempt.Rays;
                PreviousEnvelopes = Attempt.Envelopes;
                PreviousTrials = Attempt.Trials;
            }
            if (NOT Receipt.CooperativeThirdOffered)
            { return true; }
            ++CooperativeThirdSamples;
            if (NOT Require(Receipt.Count >= 3 && Receipt.PlanningConsumed,
                    TEXT("Actual cooperative third consumes the outer dispatcher")))
            { return false; }
            for (auto Index = 0; Index < 2; ++Index)
            {
                const auto& Attempt = Receipt.Attempts[Index];
                if (NOT Require(NOT Attempt.Accepted && Attempt.Trials == Index + 1
                        && Attempt.Stage == ck::EProceduralHeightPlanStage::Landing
                        && (Attempt.SeedFailure == ck::EProceduralHeightSeedFailure::TrustedInitialDownClampReprobeMiss
                            || Attempt.SeedFailure == ck::EProceduralHeightSeedFailure::TrustedDownFinalForceReject),
                        TEXT("Actual first two Landing calls independently carry qualifying native seed failures")))
                { return false; }
            }
            const auto& Cooperative = Receipt.Attempts[2];
            if (NOT Require(Cooperative.Route == 3 && Cooperative.Trials == 3
                    && NOT Cooperative.Candidate.Get_RequiresRelief() && Cooperative.WitnessRequested,
                    TEXT("Actual third call is nonrelief cooperative with its own requested witness")))
            { return false; }
            if (Cooperative.Accepted)
            {
                return Require(Receipt.Count == 3 && NOT Receipt.InteriorOffered,
                    TEXT("Actual cooperative success ends at three without an interior call"));
            }
            if (NOT Require(Receipt.Count == 4 && Receipt.Attempts[3].Trials == 4,
                    TEXT("Rejected cooperative third spends exactly one fourth slot without a fifth")))
            { return false; }
            if (NOT Receipt.InteriorOffered)
            { return true; }
            ++InteriorFourthSamples;
            if (NOT Require(Receipt.ValidatedWitnessTrial == 3 && Cooperative.Witness.IsSet()
                    && Receipt.Attempts[3].Route == 2,
                    TEXT("Actual interior fourth uses the validated same-call third witness")))
            { return false; }
            const auto& WitnessCandidate = Cooperative.Witness->Get_Candidate();
            return Require(WitnessCandidate.Get_Leg() == Cooperative.Candidate.Get_Leg()
                    && WitnessCandidate.Get_PointWorld().Equals(Cooperative.Candidate.Get_PointWorld(), 1.0e-9)
                    && WitnessCandidate.Get_NormalWorld().Equals(Cooperative.Candidate.Get_NormalWorld(), 1.0e-9)
                    && Receipt.Attempts[3].Candidate.Get_Leg() == Cooperative.Candidate.Get_Leg(),
                TEXT("Actual validated witness and interior call retain the cooperative native candidate owner"));
        }

        auto
        Observe(
            FCk_Handle_ProceduralGait InGait,
            bool InBefore,
            double InDt)
            -> void
        {
            if (Complete || Failed || ck::Is_NOT_Valid(InGait))
            { return; }
            if (UCk_Utils_ProceduralGait_UE::Get_Status(InGait) != ECk_ProceduralAnimation_Status::Ready)
            { return; }
            if (ck::Is_NOT_Valid(Pose))
            { return; }
            if (UCk_Utils_ProceduralBodyPose_UE::Get_Status(Pose) != ECk_ProceduralAnimation_Status::Ready)
            { return; }
            const auto StartupBody = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
            if (ck::Is_NOT_Valid(Motion))
            {
                if (InBefore && UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(Gait) == 4
                    && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(Gait) == 4 && Get_IsMatureSupport(StartupBody))
                { ComposeMotion(*this); }
                return;
            }
            if (UCk_Utils_SurfaceMotion_UE::Get_Status(Motion) != ECk_ProceduralAnimation_Status::Ready)
            { return; }
            if (NOT InGait.Has<ck::FFragment_SurfaceMotion>() || NOT InGait.Has<ck::FFragment_SurfaceMotion_Support>())
            { return; }
            const auto& GaitState = InGait.Get<ck::FFragment_ProceduralGait>();
            if (InBefore)
            {
                ExpectedDispatchFrame = GFrameCounter;
                ExpectedDispatchSolve = GaitState.Get_SolveSequence();
            }
            const auto& Planning = GaitState.Get_HeightPlanning();
            const auto& Support = InGait.Get<ck::FFragment_SurfaceMotion_Support>();
            const auto Current = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
            if (NOT MotionMature)
            {
                if (InBefore && Get_IsMatureSupport(Current) && UCk_Utils_SurfaceMotion_UE::Get_Support(Motion) == ECk_SurfaceMotion_Support::Grounded
                    && Planning.Get_HasSample() && Planning.Get_Frame() == GFrameCounter - 1
                    && Planning.Get_SolveSequence() == GaitState.Get_SolveSequence()
                    && Planning.Get_BodyAtSolve().Equals(Current, 1.0e-3)
                    && Support.Get_EvaluatedFrame() == Planning.Get_Frame() && Support.Get_EvaluatedBody().Equals(Current, 1.0e-3)
                    && InGait.Get<ck::FFragment_SurfaceMotion>().Get_Speed() == 0.0f
                    && Planning.Get_Direction().Equals(InGait.Get<ck::FFragment_SurfaceMotion>().Get_Direction(), 1.0e-3)
                    && Planning.Get_Speed() == 0.0f)
                { MotionMature = true; }
                if (NOT MotionMature)
                { return; }
            }
            if (InBefore)
            {
                ++Samples;
                if (NOT Driving)
                {
                    if (UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(Gait) == 4
                        && UCk_Utils_SurfaceMotion_UE::Get_Support(Motion) == ECk_SurfaceMotion_Support::Grounded)
                    {
                        UCk_Utils_SurfaceMotion_UE::Request_Steering(Motion,
                            FCk_Request_SurfaceMotion_Steering{FVector::ForwardVector, 180.0f}, {});
                        Driving = true;
                    }
                    return;
                }
                if (Accepted && GFrameCounter > AcceptedFrame)
                {
                    Complete = Require(Current.Equals(AcceptedInput, 1.0e-3)
                        && Planning.Get_Frame() == GFrameCounter - 1
                        && Planning.Get_BodyAtSolve().Equals(Current, 1.0e-3)
                        && Planning.Get_Direction().Equals(InGait.Get<ck::FFragment_SurfaceMotion>().Get_Direction(), 1.0e-3)
                        && FMath::IsNearlyEqual(Planning.Get_Speed(), InGait.Get<ck::FFragment_SurfaceMotion>().Get_Speed()),
                        TEXT("Next input agrees with actual accepted body and fresh production planning"));
                    return;
                }
                Before = Current;
                BeforeFrame = GFrameCounter;
                GoalBefore = Support.Get_AdaptiveHeightGoal();
                FeetBefore.Reset();
                for (const auto& Leg : Legs)
                { FeetBefore.Add(UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg)); }
                LandingsBefore = GaitState.Get_LandingPlanning().Get_Landings();
                Witness = false;
                if (Mutated || NOT Planning.Get_HasSample() || Planning.Get_Frame() != GFrameCounter - 1
                    || NOT Planning.Get_BodyAtSolve().Equals(Current, 1.0e-3)
                    || NOT FMath::IsFinite(InDt) || InDt <= 0.0 || WitnessRays >= 20000 || WitnessQueries >= 20000)
                { return; }
                if (NOT Get_HasSufficientPlanningInput(Current))
                { return; }
                const auto Up = UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion);
                const auto Offset = TOptional<FTransform>{UCk_Utils_ProceduralBodyPose_UE::Get_Offset(Pose)};
                auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{};
                for (const auto& Anchor : GaitState.Get_ReachStance().Get_Anchors())
                { Anchors.Emplace(Anchor.Get_FootWorld(), Anchor.Get_HipLocal(), Anchor.Get_Reach()); }
                const auto Envelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                    .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0}).Set_CollisionMargin(1.0f);
                const auto Filter = FCk_Jolt_QueryFilter{}.Set_BodySelection(ECk_Jolt_QueryBodySelection::StaticNonSensor);
                // Independent native witnesses consume separate test counters, never the production planning budget.
                // They establish the distinction at this real input, not the identity of production's chosen full goal.
                for (const auto& Candidate : Planning.Get_Candidates())
                {
                    if (NOT Candidate.Get_RequiresRelief())
                    { continue; }
                    const auto Interval = ck::Get_ProceduralHeightPlanningInterval(Current, Offset, Up, Anchors,
                        Candidate.Get_HipLocal(), Candidate.Get_PointWorld(), Candidate.Get_AdmissionReach(), 64.0);
                    if (NOT Interval.IsSet() || Interval->Y >= -200.0 * InDt)
                    { continue; }
                    Full = Current;
                    Full.AddToTranslation(Up * Interval->Y);
                    auto Rays = 0;
                    const auto LegsAdmitted = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Full, Offset, Candidate, Rays, 2048);
                    WitnessRays += Rays;
                    if (NOT LegsAdmitted)
                    { continue; }
                    auto Queries = 0;
                    auto Sweep = TOptional<FCk_Jolt_HitResult>{};
                    const auto FullClear = ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                        Current, Full, Queries, 64, &Sweep);
                    WitnessQueries += Queries;
                    const auto OrdinaryClearControl = Scenario == EScenario::Ordinary && OrdinaryBlockerRemoved;
                    if (OrdinaryClearControl ? NOT FullClear : FullClear || NOT Sweep.IsSet() || NOT Sweep->Get_HasHit())
                    { continue; }
                    if (OrdinaryClearControl)
                    {
                        Queries = 0;
                        const auto FullPresentationClear = ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                            Offset.GetValue() * Current, Offset.GetValue() * Full, Queries, 64);
                        WitnessQueries += Queries;
                        if (NOT FullPresentationClear)
                        { continue; }
                    }
                    Partial = Current;
                    Partial.AddToTranslation(Up * (-200.0 * InDt));
                    Rays = 0;
                    const auto PartialLegs = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Partial, Offset, Candidate,
                        Rays, 2048, ck::EProceduralHeightPlanStage::PreparatoryBody);
                    WitnessRays += Rays;
                    Queries = 0;
                    const auto PartialClear = ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                        Current, Partial, Queries, 64)
                        && ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                            Offset.GetValue() * Current, Offset.GetValue() * Partial, Queries, 64);
                    WitnessQueries += Queries;
                    if (NOT PartialLegs || NOT PartialClear)
                    { continue; }
                    if (NOT SeedFailureChecked)
                    {
                        SeedFailureChecked = true;
                        if (NOT CheckSeedFailure(Current, Full, Offset, Candidate))
                        { return; }
                        if (Scenario == EScenario::DiscardedStraight && NOT CheckDiscardedStraight(Current, Offset, Candidate))
                        { return; }
                        auto PreparatoryFailure = ck::EProceduralHeightSeedFailure::TrustedInitialDownClampReprobeMiss;
                        auto PreparatoryRays = 0;
                        const auto PreparatoryAdmitted = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Partial, Offset, Candidate,
                            PreparatoryRays, 2048, ck::EProceduralHeightPlanStage::PreparatoryBody, nullptr, nullptr,
                            &PreparatoryFailure);
                        WitnessRays += PreparatoryRays;
                        if (NOT Require(PreparatoryAdmitted && PreparatoryRays > 0
                                && PreparatoryFailure == ck::EProceduralHeightSeedFailure::None,
                                TEXT("Successful native PreparatoryBody clears the reused seed cause")))
                        { return; }
                    }
                    if (Scenario == EScenario::Ordinary && NOT OrdinaryBlockerRemoved)
                    {
                        // This is a separate envelope body; every native retained foot stays on its own platform.
                        if (NOT Require(Get_IsMatureSupport(Current), TEXT("Ordinary control starts from mature native feet support")))
                        { return; }
                        const auto FeetSupportBefore = Gait.Get<ck::FFragment_ProceduralGait_FeetPlane>().Get_Support();
                        const auto BlockerCenter = Origin + FVector{801.0, 0.0, 37.5};
                        for (const auto& Foot : FeetBefore)
                        {
                            const auto D = Foot.Get_Position() - BlockerCenter;
                            if (NOT Require(FMath::Abs(D.X) > 20.0 || FMath::Abs(D.Y) > 20.0 || FMath::Abs(D.Z) > 37.5,
                                    TEXT("Dedicated ordinary envelope blocker owns no retained foot contact")))
                            { return; }
                        }
                        if (NOT Require(NOT EnvelopeBlocker.IsInvalid() && NativeBodies.Contains(EnvelopeBlocker),
                                TEXT("Dedicated ordinary envelope blocker remains owned before removal")))
                        { return; }
                        const auto Physics = World->GetSubsystem<UCk_Jolt_Subsystem>()->Get_PhysicsSystem().Pin();
                        Physics->GetBodyInterface().RemoveBody(EnvelopeBlocker);
                        Physics->GetBodyInterface().DestroyBody(EnvelopeBlocker);
                        NativeBodies.RemoveSingle(EnvelopeBlocker);
                        OrdinaryBlockerRemoved = true;
                        const auto& FeetSupportAfter = Gait.Get<ck::FFragment_ProceduralGait_FeetPlane>().Get_Support();
                        if (NOT Require(Get_IsMatureSupport(Current)
                                && FeetSupportAfter.Get_Point().Equals(FeetSupportBefore.Get_Point(), 1.0e-9)
                                && FeetSupportAfter.Get_Normal().Equals(FeetSupportBefore.Get_Normal(), 1.0e-9)
                                && FeetSupportAfter.Get_Origin().Equals(FeetSupportBefore.Get_Origin(), 1.0e-9)
                                && FeetSupportAfter.Get_Basis().Equals(FeetSupportBefore.Get_Basis(), 1.0e-9)
                                && FeetSupportAfter.Get_FootprintMin().Equals(FeetSupportBefore.Get_FootprintMin(), 1.0e-9)
                                && FeetSupportAfter.Get_FootprintMax().Equals(FeetSupportBefore.Get_FootprintMax(), 1.0e-9),
                                TEXT("Ordinary blocker removal preserves actual retained feet support and footprint")))
                        { return; }
                        Rays = 0;
                        const auto FullNativeAfterRemoval = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Full, Offset, Candidate, Rays, 2048);
                        WitnessRays += Rays;
                        Queries = 0;
                        const auto FullPathsAfterRemoval = ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                                Current, Full, Queries, 64)
                            && ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                                Offset.GetValue() * Current, Offset.GetValue() * Full, Queries, 64);
                        WitnessQueries += Queries;
                        if (NOT Require(FullNativeAfterRemoval && FullPathsAfterRemoval,
                                TEXT("Mature ordinary control retains native admission and clears both full body paths")))
                        { return; }
                    }
                    Witness = true;
                    if (Scenario == EScenario::BlockedPartial)
                    {
                        const auto Bottom = Current.GetLocation().Z - 23.05;
                        Require(AddBox(FVector{Current.GetLocation().X, Current.GetLocation().Y + 20.0,
                            Bottom - 100.0 * InDt - 0.5}, FVector{2.0, 2.0, 0.5}), TEXT("Owned partial-path obstacle created"));
                        Rays = 0;
                        Require(ck::Get_IsProceduralHeightPlanAdmitted(Gait, Full, Offset, Candidate, Rays, 2048),
                            TEXT("Partial-path obstacle retains full native leg admission"));
                        WitnessRays += Rays;
                        Queries = 0;
                        Require(ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                                Current, Current, Queries, 64)
                            && NOT ck::Get_IsProceduralBodyEnvelopePathClear(World.Get(), Envelope, Filter,
                                Current, Partial, Queries, 64), TEXT("Native obstacle leaves input clear but blocks actual partial"));
                        WitnessQueries += Queries;
                        Mutated = true;
                    }
                    else if (Scenario == EScenario::StaleContact)
                    {
                        const auto Physics = World->GetSubsystem<UCk_Jolt_Subsystem>()->Get_PhysicsSystem().Pin();
                        // Floor is slot0; select an actually retained native platform rather than guessing a contact.
                        auto Platform = int32{INDEX_NONE};
                        for (auto Index = 0; Index < FeetBefore.Num(); ++Index)
                        {
                            const auto& Foot = FeetBefore[Index];
                            const auto Center = PlatformTops[Index];
                            const auto Difference = Foot.Get_Position() - Center;
                            if (Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted
                                && Foot.Get_Contact() == ECk_ProceduralLeg_FootContact::Trusted
                                && FMath::Abs(Difference.Z) < 1.0e-3
                                && FMath::Abs(Difference.X) < 8.0 && FMath::Abs(Difference.Y) < 8.0)
                            { Platform = Index + 1; break; }
                        }
                        if (NOT Require(Platform != INDEX_NONE, TEXT("A retained native platform is selected for stale-contact test")))
                        { return; }
                        Physics->GetBodyInterface().RemoveBody(NativeBodies[Platform]);
                        Physics->GetBodyInterface().DestroyBody(NativeBodies[Platform]);
                        NativeBodies.RemoveAt(Platform);
                        if (NOT Require(Get_IsMatureSupport(Current, Platform - 1),
                                TEXT("Remaining retained native platforms preserve the root footprint and no-descent support premise")))
                        { return; }
                        Rays = 0;
                        Require(NOT ck::Get_IsProceduralHeightPlanAdmitted(Gait, Partial, Offset, Candidate,
                            Rays, 2048, ck::EProceduralHeightPlanStage::PreparatoryBody), TEXT("Actual stale native retained contact rejects"));
                        WitnessRays += Rays;
                        auto StaleFailure = ck::EProceduralHeightSeedFailure::TrustedDownFinalForceReject;
                        auto StaleRays = 0;
                        Require(NOT ck::Get_IsProceduralHeightPlanAdmitted(Gait, Partial, Offset, Candidate,
                                StaleRays, 2048, ck::EProceduralHeightPlanStage::PreparatoryBody, nullptr, nullptr,
                                &StaleFailure)
                            && StaleFailure == ck::EProceduralHeightSeedFailure::None && StaleRays == Rays,
                            TEXT("Actual retained-contact loss is not a seed failure and the optional output adds no ray"));
                        WitnessRays += StaleRays;
                        auto LostLandingRays = 0;
                        const auto LostLanding = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Full, Offset, Candidate,
                            LostLandingRays, 2048);
                        WitnessRays += LostLandingRays;
                        StaleFailure = ck::EProceduralHeightSeedFailure::TrustedInitialDownClampReprobeMiss;
                        auto LostLandingCauseRays = 0;
                        const auto LostLandingWithCause = ck::Get_IsProceduralHeightPlanAdmitted(Gait, Full, Offset, Candidate,
                            LostLandingCauseRays, 2048, ck::EProceduralHeightPlanStage::Landing, nullptr, nullptr,
                            &StaleFailure);
                        WitnessRays += LostLandingCauseRays;
                        Require(NOT LostLanding && NOT LostLandingWithCause && LostLandingRays > 0
                                && LostLandingRays == LostLandingCauseRays
                                && StaleFailure == ck::EProceduralHeightSeedFailure::None,
                            TEXT("Previously admitted Landing loses native sample/retained contact without publishing a seed cause"));
                        Mutated = true;
                    }
                    break;
                }
                return;
            }
            if (NOT CheckDispatchReceipt())
            { return; }
            if (BeforeFrame != GFrameCounter)
            { return; }
            Require(Support.Get_HeightPlanningTrials() <= 4 && Support.Get_HeightPlanningRays() <= 2048
                && Support.Get_HeightEnvelopeQueries() <= 64, TEXT("Production shared planning limits remain unchanged"));
            for (auto Index = 0; Index < Legs.Num(); ++Index)
            {
                const auto Foot = UCk_Utils_ProceduralLeg_UE::Get_Foot(Legs[Index]);
                Require(Foot.Get_Phase() == FeetBefore[Index].Get_Phase()
                    && Foot.Get_Contact() == FeetBefore[Index].Get_Contact()
                    && Foot.Get_Foothold() == FeetBefore[Index].Get_Foothold()
                    && Foot.Get_SwingAlpha() == FeetBefore[Index].Get_SwingAlpha()
                    && Foot.Get_Normal().Equals(FeetBefore[Index].Get_Normal(), 1.0e-9)
                    && Foot.Get_Position().Equals(FeetBefore[Index].Get_Position(), 1.0e-9),
                    TEXT("Surface publication grants no foot phase, contact trust or target position"));
            }
            const auto& AfterLandings = GaitState.Get_LandingPlanning().Get_Landings();
            Require(AfterLandings.Num() == LandingsBefore.Num(), TEXT("Surface publication creates no landing commitment"));
            for (auto Index = 0; Index < FMath::Min(AfterLandings.Num(), LandingsBefore.Num()); ++Index)
            {
                Require(AfterLandings[Index].Get_Leg() == LandingsBefore[Index].Get_Leg()
                    && AfterLandings[Index].Get_Cast() == LandingsBefore[Index].Get_Cast()
                    && AfterLandings[Index].Get_NormalWorld().Equals(LandingsBefore[Index].Get_NormalWorld(), 1.0e-9)
                    && AfterLandings[Index].Get_Constraint().Get_PointWorld().Equals(
                        LandingsBefore[Index].Get_Constraint().Get_PointWorld(), 1.0e-9)
                    && AfterLandings[Index].Get_Constraint().Get_HipLocal().Equals(
                        LandingsBefore[Index].Get_Constraint().Get_HipLocal(), 1.0e-9)
                    && AfterLandings[Index].Get_Constraint().Get_AdmissionReach()
                        == LandingsBefore[Index].Get_Constraint().Get_AdmissionReach()
                    && AfterLandings[Index].Get_Constraint().Get_Reach() == LandingsBefore[Index].Get_Constraint().Get_Reach(),
                    TEXT("Surface publication does not replace a committed owner or target"));
            }
            if (NOT Witness)
            { return; }
            const auto Goal = Support.Get_AdaptiveHeightGoal();
            const auto GoalDescended = Goal.Z < Before.GetLocation().Z - 1.0e-6;
            if (Mutated)
            {
                Require(Support.Get_EvaluatedFrame() == GFrameCounter && Support.Get_HeightPlanningTrials() > 0,
                    TEXT("Rejected case executes actual current-frame production planning"));
                Require(Support.Get_EvaluatedBody().GetLocation().Z >= Before.GetLocation().Z - 1.0e-3,
                    TEXT("Rejected case retains height under unchanged ordinary support-plane integration"));
                Complete = Require(NOT (Support.Get_AdaptiveHeightPreparing() && GoalDescended)
                    && (Goal.Equals(GoalBefore, 1.0e-6) || NOT Support.Get_AdaptiveHeightActive()),
                    TEXT("Rejected partial publishes no partial body-height goal"));
                return;
            }
            const auto ExpectedPreparing = Scenario != EScenario::Ordinary;
            if (Support.Get_AdaptiveHeightActive() && Support.Get_AdaptiveHeightPreparing() == ExpectedPreparing
                && GoalDescended)
            {
                if (ExpectedPreparing)
                {
                    Require(NOT Goal.Equals(Full.GetLocation(), 1.0e-6), TEXT("Blocked full-goal witness never becomes the partial destination"));
                    Require(Goal.Equals(Partial.GetLocation(), 1.0e-3), TEXT("Preparatory goal is the exact bounded vertical partial"));
                }
                Require(Support.Get_EvaluatedFrame() == GFrameCounter, TEXT("Accepted body and height publish in the same physics frame"));
                Accepted = true;
                AcceptedFrame = GFrameCounter;
                AcceptedInput = Support.Get_EvaluatedBody();
            }
        }
    };

    auto
    ComposeMotion(
        FState& S)
        -> void
    {
        auto MotionSpec = FCk_SurfaceMotion_Spec{};
        auto Contact = MotionSpec.Get_Contact();
        Contact.Set_Clearance(64.0f).Set_ProbeReach(179.0f).Set_HeightSource(ECk_SurfaceMotion_HeightSource::PlantedFeet);
        MotionSpec.Set_Contact(Contact).Set_AdaptiveHeight(FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
            .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0}).Set_CollisionMargin(1.0f));
        auto Movement = MotionSpec.Get_Movement();
        Movement.Set_ClearanceSpeed(200.0f);
        MotionSpec.Set_Movement(Movement);
        S.Motion = UCk_Utils_SurfaceMotion_UE::Add(S.Body, MotionSpec);
        if (NOT S.Require(ck::IsValid(S.Motion) && ck::IsValid(S.Pose), TEXT("Public native walker composes")))
        { return; }
    }

    auto
    Compose(
        FState& S)
        -> void
    {
        S.OriginalFixedTime = FApp::UseFixedTimeStep();
        S.OriginalFixedDelta = FApp::GetFixedDeltaTime();
        FApp::SetFixedDeltaTime(1.0 / 90.0);
        FApp::SetUseFixedTimeStep(true);
        S.FixedTime = true;
        if (NOT S.Require(S.AddBox(S.Origin + FVector{0.0, 0.0, -25.0}, FVector{1000.0, 300.0, 25.0}), TEXT("Native floor created")))
        { return; }
        auto Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(S.World.Get());
        S.Body = UCk_Utils_Transform_UE::Add(Root,
            FTransform{S.Origin + FVector{801.0, 0.0, InitialBodyZ}}, ECk_Replication::DoesNotReplicate);
        if (NOT S.Require(ck::IsValid(S.Body), TEXT("Native walker transform composes")))
        { return; }
        auto Specs = TArray<FCk_ProceduralLeg_Spec>{};
        auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
        for (auto Index = 0; Index < 4; ++Index)
        {
            const auto Front = Index < 2;
            const auto Side = Index % 2 == 0 ? -1.0 : 1.0;
            const auto Hip = FVector{Front ? 5.0 : -35.0, Side * 27.0, 0.0};
            const auto Rest = FVector{Front ? FrontRestX : RearRestX, Side * 45.0, -Clearance};
            S.Require(S.AddBox(S.Origin + FVector{801.0 + Rest.X, Rest.Y, 37.5}, FVector{8.0, 8.0, 37.5}),
                TEXT("Native retained-foot platform created"));
            if (S.Failed)
            { return; }
            S.PlatformIds.Add(S.NativeBodies.Last());
            S.PlatformTops.Add(S.Origin + FVector{801.0 + Rest.X, Rest.Y, SupportTop});
            const auto Id = FName{*FString::Printf(TEXT("HeightLeg%d"), Index)};
            auto Spec = FCk_ProceduralLeg_Spec{Id, FCk_ProceduralLeg_Placement{Hip, Rest},
                FCk_ProceduralLeg_ChainGeometry{TArray<float>{Front ? 50.0f : 60.0f, Front ? 50.0f : 60.0f}}};
            Spec.Set_FootContactRadius(2.0f);
            Specs.Add(Spec);
            auto Parts = TArray<FCk_Handle_Transform>{};
            for (auto Part = 0; Part < 2; ++Part)
            {
                auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S.Body);
                Parts.Add(UCk_Utils_Transform_UE::Add(PartEntity, FTransform{S.Origin}, ECk_Replication::DoesNotReplicate));
            }
            auto Chain = FCk_ProceduralRig_Spec{Parts};
            auto FootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S.Body);
            Chain.Set_Foot(UCk_Utils_Transform_UE::Add(FootEntity,
                FTransform{S.Origin}, ECk_Replication::DoesNotReplicate));
            Chains.Emplace(Id, Chain);
        }
        const auto PreparatoryGeometry = S.Scenario == EScenario::Preparatory || S.Scenario == EScenario::NondefaultTarget
            || S.Scenario == EScenario::DiscardedStraight;
        const auto EnvelopeBlockerHalfHeight = PreparatoryGeometry ? 50.0 : 37.5;
        const auto EnvelopeBlockerX = PreparatoryGeometry ? 780.0 : 801.0;
        { S.Require(S.AddBox(S.Origin + FVector{EnvelopeBlockerX, 0.0, EnvelopeBlockerHalfHeight}, FVector{20.0, 20.0, EnvelopeBlockerHalfHeight}), TEXT("Full-path native body obstacle created")); }
        if (NOT S.Failed)
        { S.EnvelopeBlocker = S.NativeBodies.Last(); }
        if (S.Failed)
        { return; }
        S.Rig.Reset(NewObject<UCk_ProceduralRig_Data>());
        S.Rig->Set_Legs(Specs);
        S.Preset.Reset(NewObject<UCk_ProceduralGait_Data>());
        auto Step = S.Preset->Get_Step();
        S.DefaultTargetFraction = Step.Get_TargetReachFraction();
        if (S.Scenario == EScenario::NondefaultTarget)
        {
            // A separate fresh construction changes the actual target sphere without retuning a live gait.
            const auto Nondefault = (Step.Get_TargetReachFraction() + Step.Get_ForceStepReachFraction()) * 0.5f;
            Step.Set_TargetReachFraction(Nondefault);
            if (NOT S.Require(ck::IsValid(Step) && Nondefault != S.DefaultTargetFraction,
                    TEXT("Fresh nondefault target fraction passes the existing native step validator")))
            { return; }
            S.Preset->Set_Step(Step);
        }
        if (S.Scenario == EScenario::DiscardedStraight)
        {
            auto Foothold = S.Preset->Get_Foothold();
            Foothold.Set_MaxAngle(60.0f);
            S.Preset->Set_Foothold(Foothold);
        }
        S.ConstructedTargetFraction = Step.Get_TargetReachFraction();
        S.Gait = UCk_Utils_ProceduralAnimation_UE::Add_Walker(S.Body,
            FCk_ProceduralWalker_Spec{S.Rig.Get(), S.Preset.Get()}.Set_Chains(Chains)).Get_Gait();
        if (NOT S.Require(ck::IsValid(S.Gait), TEXT("Public native gait composes before optional features")))
        { return; }
        auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S.Body);
        const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
            FTransform{S.Origin + FVector{801.0, 0.0, InitialBodyZ}}, ECk_Replication::DoesNotReplicate);
        S.Pose = UCk_Utils_ProceduralBodyPose_UE::Add(S.Gait, FCk_ProceduralBodyPose_Spec{Presentation});
        if (NOT S.Require(ck::IsValid(S.Pose), TEXT("Public native pose composes before optional motion")))
        { return; }
        for (auto Index = 0; Index < 4; ++Index)
        {
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(S.Body, FName{*FString::Printf(TEXT("HeightLeg%d"), Index)});
            if (NOT S.Require(ck::IsValid(Leg), TEXT("Public composed leg exists")))
            { return; }
            S.Legs.Add(Leg);
        }
        S.Gait.Add<ck::FFragment_ProceduralHeightPlanning_Test>();
        auto* State = &S;
        // Queued latent commands retain the shared state until cleanup clears this non-owning callback.
        S.Gait.Add<ck::FFragment_TestHeightPlanningObservation>().Observe =
            [State](FCk_Handle_ProceduralGait InGait, bool InBefore, double InDt) { State->Observe(InGait, InBefore, InDt); };
    }

    auto
    Enqueue(
        FAutomationTestBase* InTest)
        -> void
    {
        for (const auto Scenario : {EScenario::Preparatory, EScenario::BlockedPartial, EScenario::StaleContact,
            EScenario::Ordinary, EScenario::NondefaultTarget, EScenario::DiscardedStraight})
        {
            const auto State = MakeShared<FState>();
            State->Test = InTest;
            State->Scenario = Scenario;
            State->Origin = FVector{120000.0, 397000.0 + 1000.0 * static_cast<int32>(Scenario), 5000.0};
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
                [State](UWorld* InWorld) { State->World = InWorld; Compose(*State); })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
                FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Complete || State->Failed; }), 15.0,
                TEXT("Actual native height-planning scenario obtains mandatory production coverage")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
                [State](UWorld*)
                {
                    State->Require(State->Complete && NOT State->Failed, TEXT("Mandatory production scenario completed"));
                    State->Require(State->SeedFailureChecked, TEXT("Fresh native seed-cause and output-reset checks were reached"));
                    State->Require(State->DispatchReceiptSamples > 0, TEXT("Actual composed Surface dispatch receipts were observed"));
                    State->Test->AddInfo(FString::Printf(TEXT("HeightDispatchNative scenario%d actualReceipts%d cooperativeThird%d interiorFourth%d"),
                        static_cast<int32>(State->Scenario), State->DispatchReceiptSamples, State->CooperativeThirdSamples,
                        State->InteriorFourthSamples));
                    State->Test->AddInfo(FString::Printf(TEXT("HeightNative scenario%d samples%d accepted%d mutation%d independentWitnessRays%d independentWitnessQueries%d"),
                        static_cast<int32>(State->Scenario), State->Samples, State->Accepted, State->Mutated,
                        State->WitnessRays, State->WitnessQueries));
                    if (ck::IsValid(State->Gait) && State->Gait.Has<ck::FFragment_TestHeightPlanningObservation>())
                    { State->Gait.Get<ck::FFragment_TestHeightPlanningObservation>().Observe = {}; }
                    if (ck::IsValid(State->Body))
                    { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Body); }
                    auto* Jolt = State->World.IsValid() ? State->World->GetSubsystem<UCk_Jolt_Subsystem>() : nullptr;
                    const auto Physics = ck::IsValid(Jolt) ? Jolt->Get_PhysicsSystem().Pin() : nullptr;
                    if (State->Require(Physics.IsValid(), TEXT("Native world remains available for owned terrain cleanup")))
                    {
                        for (const auto Id : State->NativeBodies)
                        {
                            Physics->GetBodyInterface().RemoveBody(Id);
                            Physics->GetBodyInterface().DestroyBody(Id);
                        }
                    }
                    State->NativeBodies.Reset();
                    FApp::SetUseFixedTimeStep(State->OriginalFixedTime);
                    FApp::SetFixedDeltaTime(State->OriginalFixedDelta);
                    State->FixedTime = false;
                })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
                FCk_NetAutoTest_Condition::CreateLambda([State] { return ck::Is_NOT_Valid(State->Body); }), 5.0,
                TEXT("Owned height walker retires before the next scenario")));
        }
    }
}

// CONTACT_CORRECTION_NATIVE_REGRESSION_BEGIN
// Production Core paced outputs, native world and native authored-envelope predicate.
// Measured rounded body/anchors use explicitly constructed native warmup, not a bit-exact historical replay.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralSurfaceMotionNativeContactCorrectionTest,
    "Ck.ProceduralAnimation.SurfaceMotion.ContactCorrection.NativePostsAndEnvelopeAtomicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
    FCkProceduralSurfaceMotionNativeContactCorrectionTest::
    RunTest(const FString&)
    -> bool
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto* Jolt = InServer->GetSubsystem<UCk_Jolt_Subsystem>();
            const auto Physics = ck::IsValid(Jolt) ? Jolt->Get_PhysicsSystem().Pin() : nullptr;
            if (NOT TestTrue(TEXT("Replay requires the existing real PIE Jolt world"), Physics.IsValid()))
            { return; }
            const auto Signature = ck::jolt::TryDerive_SignatureFromProfile(TEXT("BlockAll"), ECk_Jolt_BodyDomain::Static);
            if (NOT TestTrue(TEXT("Replay uses the authored BlockAll static profile"), Signature.IsSet()))
            { return; }
            const auto Layer = JPH::ObjectLayer{Jolt->Get_LayerTable().Get_OrRegisterLayer(*Signature)};
            auto& Bodies = Physics->GetBodyInterface();
            auto BodyIds = TArray<JPH::BodyID>{};
            ON_SCOPE_EXIT
            {
                for (const auto Id : BodyIds)
                {
                    Bodies.RemoveBody(Id);
                    Bodies.DestroyBody(Id);
                }
            };
            const auto AddBox = [&](const FVector& InCenter, const FVector& InHalfExtents) -> bool
            {
                auto Shape = JPH::Ref<JPH::Shape>{new JPH::BoxShape{JPH::Vec3{
                    static_cast<float>(InHalfExtents.X), static_cast<float>(InHalfExtents.Y), static_cast<float>(InHalfExtents.Z)}}};
                auto Creation = JPH::BodyCreationSettings{Shape.GetPtr(),
                    JPH::RVec3{static_cast<float>(InCenter.X), static_cast<float>(InCenter.Y), static_cast<float>(InCenter.Z)},
                    JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layer};
                const auto Id = Bodies.CreateAndAddBody(Creation, JPH::EActivation::DontActivate);
                if (Id.IsInvalid())
                { return false; }
                BodyIds.Add(Id);
                return true;
            };

            // Gym_Fixture: Posts is a wide stress course; one Beast lane is centered at Y0.
            const auto Origin = FVector{120000.0, 365000.0, 600.0};
            auto GeometryReady = AddBox(Origin + FVector{0.0, 0.0, -25.0}, FVector{1550.0, 660.0, 25.0});
            for (auto Index = 0; Index < 9; ++Index)
            {
                const auto HalfHeight = Index % 2 == 0 ? 37.5 : 15.0;
                GeometryReady &= AddBox(Origin + FVector{-760.0 + 190.0 * Index, 0.0, HalfHeight},
                    FVector{30.0, 30.0, HalfHeight});
            }
            for (auto Index = 0; Index < 8; ++Index)
            {
                const auto X = -760.0 + 190.0 * (Index + 0.5);
                GeometryReady &= AddBox(Origin + FVector{X, -100.0, 45.0}, FVector{20.0, 20.0, 45.0});
                GeometryReady &= AddBox(Origin + FVector{X, 100.0, 45.0}, FVector{20.0, 20.0, 45.0});
            }
            if (NOT TestTrue(TEXT("The exact authored floor and25 post boxes were created"), GeometryReady && BodyIds.Num() == 26))
            { return; }

            // Native geometry/admission-layer controls from Posts08; not a private foothold-search replay.
            const auto CheckNativeSeedGeometry = [&]() -> void
            {
                const auto NativeSeedBody = FVector{119732.22093399265, 365000.0, 751.16699553658543};
                const auto NativeSeedHip = NativeSeedBody + FVector{45.0, -28.0, 0.0};
                const auto NativeSeedPlant = FVector{119720.1171875, 364918.21875, 690.0};
                const auto NativeSeedLead = FVector{25.0, 0.0, 0.0};
                const auto NativeSeedForce = 0.92f * 100.0f;
                const auto NativeSeedSearch = 0.3f * 100.0f;
                const auto NativeSeedOffset = NativeSeedPlant - NativeSeedHip;
                const auto NativeSeedLeadSquared = NativeSeedLead.SizeSquared();
                const auto NativeSeedAlong = FVector::DotProduct(NativeSeedOffset, NativeSeedLead);
                const auto NativeSeedConstant = NativeSeedOffset.SizeSquared()
                    - FMath::Square(static_cast<double>(NativeSeedForce));
                const auto NativeSeedRoot = FMath::Sqrt(NativeSeedAlong * NativeSeedAlong
                    - NativeSeedLeadSquared * NativeSeedConstant);
                const auto NativeSeedLower = FMath::Max(0.5, (-NativeSeedAlong - NativeSeedRoot) / NativeSeedLeadSquared);
                const auto NativeSeedUpper = FMath::Min(FMath::Min(1.0,
                    static_cast<double>(NativeSeedSearch) / FMath::Sqrt(NativeSeedLeadSquared)),
                    (-NativeSeedAlong + NativeSeedRoot) / NativeSeedLeadSquared);
                const auto NativeSeedMidpoint = NativeSeedPlant + NativeSeedLead * ((NativeSeedLower + NativeSeedUpper) * 0.5);
                const auto NativeSeedNaive = FVector{119734.4375, 364918.21875, 690.0};
                const auto NativeSeedSelected = FVector{119734.4453125, 364918.21875, 690.0};
                TestTrue(TEXT("Posts08 arithmetic keeps the original force float product and bounded midpoint"),
                    NativeSeedForce == 92.0f && NativeSeedUpper > NativeSeedLower
                        && NativeSeedMidpoint.X > Origin.X - 265.0);
                TestTrue(TEXT("Posts08 selected geometry is exactly one native float ULP inward from the naive entry"),
                    std::nextafter(static_cast<float>(NativeSeedNaive.X), static_cast<float>(NativeSeedHip.X))
                        == static_cast<float>(NativeSeedSelected.X));

                const auto NativeSeedFilter = FCk_Jolt_QueryFilter{}
                    .Set_BodySelection(ECk_Jolt_QueryBodySelection::StaticNonSensor);
                const auto NativeSeedOldHit = UCk_Utils_JoltQuery_UE::Get_RayCast(InServer,
                    NativeSeedMidpoint + FVector::UpVector * 100.0, NativeSeedMidpoint - FVector::UpVector * 200.0,
                    NativeSeedFilter);
                const auto NativeSeedNaiveHit = UCk_Utils_JoltQuery_UE::Get_RayCast(InServer,
                    NativeSeedNaive + FVector::UpVector * 100.0, NativeSeedNaive - FVector::UpVector * 200.0,
                    NativeSeedFilter);
                const auto NativeSeedSelectedHit = UCk_Utils_JoltQuery_UE::Get_RayCast(InServer,
                    NativeSeedSelected + FVector::UpVector * 100.0, NativeSeedSelected - FVector::UpVector * 200.0,
                    NativeSeedFilter);
                // Same trusted-ray predicate as Get_TrustedHit, with the actual straight ray direction.
                const auto IsNativeSeedTrusted = [](const FCk_Jolt_HitResult& InHit) -> bool
                {
                    return InHit.Get_HasHit() && InHit.Get_Fraction() > 0.0f && InHit.Get_Fraction() <= 1.0f
                        && NOT InHit.Get_Position().ContainsNaN() && NOT InHit.Get_Normal().ContainsNaN()
                        && NOT InHit.Get_Normal().IsNearlyZero()
                        && FVector::DotProduct(InHit.Get_Normal(), -FVector::UpVector) < -KINDA_SMALL_NUMBER;
                };
                const auto NativeSeedOldTrusted = IsNativeSeedTrusted(NativeSeedOldHit);
                const auto NativeSeedNaiveTrusted = IsNativeSeedTrusted(NativeSeedNaiveHit);
                const auto NativeSeedSelectedTrusted = IsNativeSeedTrusted(NativeSeedSelectedHit);
                TestTrue(TEXT("Native Posts08 old midpoint yields an actual positive-fraction front-facing hit"), NativeSeedOldTrusted);
                TestTrue(TEXT("Native Posts08 naive entry yields an actual positive-fraction front-facing hit"), NativeSeedNaiveTrusted);
                TestTrue(TEXT("Native Posts08 selected float-grid point yields an actual positive-fraction front-facing hit"), NativeSeedSelectedTrusted);
                if (NOT NativeSeedOldTrusted || NOT NativeSeedNaiveTrusted || NOT NativeSeedSelectedTrusted)
                {
                    return;
                }
                TestEqual(TEXT("Native Posts08 old midpoint falls through the flank gap to the authored floor"),
                    NativeSeedOldHit.Get_Position().Z, 600.0);
                TestTrue(TEXT("Native Posts08 old midpoint floor contact fails the unchanged force reach"),
                    FVector::Dist(NativeSeedOldHit.Get_Position(), NativeSeedHip) > NativeSeedForce);
                TestEqual(TEXT("Native Posts08 naive entry hits the occupied authored flank top"),
                    NativeSeedNaiveHit.Get_Position().Z, 690.0);
                TestTrue(TEXT("Native Posts08 naive float entry contact fails the unchanged strict force reach"),
                    FVector::Dist(NativeSeedNaiveHit.Get_Position(), NativeSeedHip) > NativeSeedForce);
                TestEqual(TEXT("Native Posts08 selected point hits the occupied authored flank top"),
                    NativeSeedSelectedHit.Get_Position().Z, 690.0);
                TestTrue(TEXT("Native Posts08 selected hit passes the unchanged strict force reach without epsilon"),
                    FVector::Dist(NativeSeedSelectedHit.Get_Position(), NativeSeedHip) <= NativeSeedForce);
                TestTrue(TEXT("Native Posts08 selected hit remains inside the exact occupied flank footprint"),
                    NativeSeedSelectedHit.Get_Position().X >= Origin.X - 305.0
                        && NativeSeedSelectedHit.Get_Position().X <= Origin.X - 265.0
                        && NativeSeedSelectedHit.Get_Position().Y >= Origin.Y - 120.0
                        && NativeSeedSelectedHit.Get_Position().Y <= Origin.Y - 80.0);
                constexpr auto NativeSeedTrialLimit = 100.0 + 1.0e-3;
                TestTrue(TEXT("Native Posts08 selected hit fits the actual first physical trial hip"),
                    FVector::Dist(NativeSeedSelectedHit.Get_Position(),
                        FVector{119779.22093789675, 364972.0, 751.42876755786631}) <= NativeSeedTrialLimit);
                TestTrue(TEXT("Native Posts08 selected hit fits the actual first posed trial hip"),
                    FVector::Dist(NativeSeedSelectedHit.Get_Position(),
                        FVector{119779.21672253267, 364971.99966359645, 752.28927801104169}) <= NativeSeedTrialLimit);
                TestTrue(TEXT("Native Posts08 selected hit fits the actual latest physical trial hip"),
                    FVector::Dist(NativeSeedSelectedHit.Get_Position(),
                        FVector{119779.22093812027, 364972.0, 753.38922234505389}) <= NativeSeedTrialLimit);
                TestTrue(TEXT("Native Posts08 selected hit fits the actual latest posed trial hip"),
                    FVector::Dist(NativeSeedSelectedHit.Get_Position(),
                        FVector{119779.21672275619, 364971.99966359645, 754.24973279822927}) <= NativeSeedTrialLimit);
                const auto NativeSeedOutboard = FVector{5.0, -27.0, 0.0}.GetSafeNormal();
                TestTrue(TEXT("Native Posts08 selected hit passes the production own-side helper at authored tolerance"),
                    ck::Get_IsFootholdOnOwnSide(NativeSeedSelectedHit.Get_Position() - NativeSeedHip,
                        NativeSeedOutboard, 0.1f * 100.0f));
                TestTrue(TEXT("Native Posts08 selected hit passes the production level-enough helper at authored max angle"),
                    ck::Get_IsFootholdLevelEnough(NativeSeedSelectedHit.Get_Normal(), 90.0f));
                TestFalse(TEXT("Native Posts08 selected hit is not under the authored Beast body by the production helper"),
                    ck::Get_IsFootholdUnderBody(static_cast<float>(NativeSeedSelectedHit.Get_Position().Y - NativeSeedBody.Y),
                        static_cast<float>(NativeSeedHip.Z - NativeSeedSelectedHit.Get_Position().Z), 28.0f, 64.0f, 5.0f));
                TestTrue(TEXT("Native Posts08 authored zero contact radius retains the production reservation opt-out"),
                    ck::Get_IsProceduralFootContactAvailable(NativeSeedSelectedHit.Get_Position(), 0.0f, 0,
                        TArrayView<const ck::FProceduralFootReservation>{}));
                const auto NativeSeedVisibility = UCk_Utils_JoltQuery_UE::Get_RayCast(InServer,
                    NativeSeedHip, NativeSeedSelectedHit.Get_Position(), NativeSeedFilter);
                // Exact Get_Occluder predicate, including its existing endpoint tolerance; this is not a new API.
                const auto NativeSeedBlocks = NativeSeedVisibility.Get_HasHit() && NativeSeedVisibility.Get_Fraction() > 0.0f
                    && NOT NativeSeedVisibility.Get_Position().ContainsNaN()
                    && FVector::Dist(NativeSeedVisibility.Get_Position(), NativeSeedSelectedHit.Get_Position()) > 3.0f;
                TestFalse(TEXT("Native Posts08 selected hit has no native hip occluder under the unchanged production predicate"),
                    NativeSeedBlocks);
            };
            CheckNativeSeedGeometry();

            // These measured update summaries reconstruct a pair; they do not identify a prior admission callback.
            const auto EndpointBody = FTransform{FQuat::Identity,
                FVector{119919.00778735173, 365000.0, 698.14999999999998}};
            const auto EndpointPreviousOffset = FTransform{
                FQuat{-0.0021661020893385123, 0.0013873400645486639,
                    -4.4194315023543123e-09, 0.99999669163916938},
                FVector{0.0, 0.0, -0.73025063034718951}};
            const auto EndpointPublishedOffset = FTransform{
                FQuat{-0.0020963662327518522, 0.0013295090444141,
                    -5.2459698968327211e-09, 0.99999691882241271},
                FVector{0.0, 0.0, -0.70879998432257774}};
            const auto EndpointPrevious = EndpointPreviousOffset * EndpointBody;
            const auto EndpointPublished = EndpointPublishedOffset * EndpointBody;
            const auto EndpointEnvelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0}).Set_CollisionMargin(1.0f);
            const auto EndpointFilter = FCk_Jolt_QueryFilter{}
                .Set_BodySelection(ECk_Jolt_QueryBodySelection::StaticNonSensor);
            auto EndpointQueries = 0;
            auto EndpointSweep = TOptional<FCk_Jolt_HitResult>{};
            auto EndpointOverlap = TOptional<ck::FProceduralBodyEnvelopeOverlapObservation>{};
            const auto PreviousStationaryClear = ck::Get_IsProceduralBodyEnvelopePathClear(InServer,
                EndpointEnvelope, EndpointFilter, EndpointPrevious, EndpointPrevious,
                EndpointQueries, 3, &EndpointSweep, &EndpointOverlap);
            TestTrue(TEXT("The measured previous stationary pose is a clear three-query control"),
                PreviousStationaryClear && EndpointQueries == 3 && NOT EndpointSweep.IsSet() && NOT EndpointOverlap.IsSet());
            EndpointQueries = 0;
            const auto MeasuredPairPathClear = ck::Get_IsProceduralBodyEnvelopePathClear(InServer,
                EndpointEnvelope, EndpointFilter, EndpointPrevious, EndpointPublished,
                EndpointQueries, 3, &EndpointSweep, &EndpointOverlap);
            TestTrue(TEXT("Default admission retains the measured clear three-query swept enclosure"),
                MeasuredPairPathClear && EndpointQueries == 3 && NOT EndpointSweep.IsSet() && NOT EndpointOverlap.IsSet());
            EndpointQueries = 0;
            const auto PublishedStationaryClear = ck::Get_IsProceduralBodyEnvelopePathClear(InServer,
                EndpointEnvelope, EndpointFilter, EndpointPublished, EndpointPublished,
                EndpointQueries, 3, &EndpointSweep, &EndpointOverlap);
            TestTrue(TEXT("The measured published stationary pose exposes an actual first-query overlap"),
                NOT PublishedStationaryClear && EndpointQueries == 1 && NOT EndpointSweep.IsSet()
                    && EndpointOverlap.IsSet()
                    && EndpointOverlap->Get_Kind() == ck::EProceduralBodyEnvelopeOverlapKind::StartEnclosureOverlap
                    && NOT EndpointOverlap->Get_Hits().IsEmpty() && EndpointOverlap->Get_Hits()[0].Get_HasHit());
            constexpr auto StrictEndpoint = ck::EProceduralBodyEnvelopeEndpointValidation::ExactStationaryEndpoint;
            EndpointQueries = 0;
            const auto StrictMeasuredPairPathClear = ck::Get_IsProceduralBodyEnvelopePathClear(InServer,
                EndpointEnvelope, EndpointFilter, EndpointPrevious, EndpointPublished,
                EndpointQueries, 4, &EndpointSweep, &EndpointOverlap, StrictEndpoint);
            TestTrue(TEXT("Strict admission rejects the native blocked endpoint at the fourth query"),
                NOT StrictMeasuredPairPathClear && EndpointQueries == 4 && NOT EndpointSweep.IsSet()
                    && EndpointOverlap.IsSet()
                    && EndpointOverlap->Get_Kind() == ck::EProceduralBodyEnvelopeOverlapKind::ExactEndpointOverlap
                    && NOT EndpointOverlap->Get_Hits().IsEmpty() && EndpointOverlap->Get_Hits()[0].Get_HasHit());
            TestTrue(TEXT("A strictly admitted measured pose path retains a clear stationary endpoint"),
                NOT StrictMeasuredPairPathClear || PublishedStationaryClear);
            const auto RejectedEndpointOverlap = EndpointOverlap;
            EndpointQueries = 0;
            TestTrue(TEXT("Strict admission accepts the clear previous stationary pose within four queries"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, EndpointEnvelope, EndpointFilter,
                    EndpointPrevious, EndpointPrevious, EndpointQueries, 4, &EndpointSweep, &EndpointOverlap, StrictEndpoint));
            TestTrue(TEXT("The clear strict control spends four queries and resets prior overlap output"),
                EndpointQueries == 4 && NOT EndpointSweep.IsSet() && NOT EndpointOverlap.IsSet());
            EndpointQueries = 0;
            EndpointSweep = FCk_Jolt_HitResult{}.Set_HasHit(true);
            EndpointOverlap = RejectedEndpointOverlap;
            TestFalse(TEXT("Strict admission rejects when only the original three-query budget remains"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, EndpointEnvelope, EndpointFilter,
                    EndpointPrevious, EndpointPublished, EndpointQueries, 3, &EndpointSweep, &EndpointOverlap, StrictEndpoint));
            TestTrue(TEXT("Exhaustion issues no fourth query and fabricates no sweep or endpoint overlap"),
                EndpointQueries == 3 && NOT EndpointSweep.IsSet() && NOT EndpointOverlap.IsSet());
            EndpointQueries = 0;
            EndpointSweep = FCk_Jolt_HitResult{}.Set_HasHit(true);
            EndpointOverlap = ck::FProceduralBodyEnvelopeOverlapObservation{
                ck::EProceduralBodyEnvelopeOverlapKind::ExactEndpointOverlap, TArray<FCk_Jolt_HitResult>{}};
            TestFalse(TEXT("An invalid endpoint-validation discriminator rejects before any native query"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, EndpointEnvelope, EndpointFilter,
                    EndpointPrevious, EndpointPublished, EndpointQueries, 4, &EndpointSweep, &EndpointOverlap,
                    static_cast<ck::EProceduralBodyEnvelopeEndpointValidation>(255)));
            TestTrue(TEXT("Invalid endpoint-validation mode resets both optional outputs with zero queries"),
                EndpointQueries == 0 && NOT EndpointSweep.IsSet() && NOT EndpointOverlap.IsSet());
            const auto EndpointAnchors = TArray<ck::FProceduralBodyPoseReachAnchor>{};
            auto EndpointProjectionQueries = 0;
            auto EndpointAdmissionCalls = 0;
            const auto EndpointProjection = ck::ProjectProceduralBodyPoseToReach(EndpointBody,
                EndpointPreviousOffset, EndpointPublishedOffset, EndpointAnchors,
                [&](const FTransform& InOffset) -> bool
                {
                    ++EndpointAdmissionCalls;
                    return ck::Get_IsProceduralBodyEnvelopePathClear(InServer, EndpointEnvelope, EndpointFilter,
                        EndpointPrevious, InOffset * EndpointBody, EndpointProjectionQueries, 64,
                        nullptr, nullptr, StrictEndpoint);
                });
            TestTrue(TEXT("Real pose projection returns an admitted offset before the blocked measured proposal"),
                EndpointProjection.IsSet() && EndpointProjection->Get_Fraction() < 1.0f);
            TestTrue(TEXT("Strict projection retains its eleven-callback and unchanged native-query ceilings"),
                EndpointAdmissionCalls > 0 && EndpointAdmissionCalls <= 11
                    && EndpointProjectionQueries > 0 && EndpointProjectionQueries <= 44);
            if (EndpointProjection.IsSet())
            {
                const auto ProjectedEndpoint = EndpointProjection->Get_Offset() * EndpointBody;
                auto ProjectedStationaryQueries = 0;
                TestTrue(TEXT("The real projection's admitted endpoint passes a fresh native stationary proof"),
                    ck::Get_IsProceduralBodyEnvelopePathClear(InServer, EndpointEnvelope, EndpointFilter,
                        ProjectedEndpoint, ProjectedEndpoint, ProjectedStationaryQueries, 3));
            }

            // Defaults below are the actual Beast MakeMotionParams/default fields, not a new tuning policy.
            const auto Settings = ck::FProceduralSurfaceMotionSettings{}
                .Set_Clearance(64.0f).Set_ProbeReach(179.0f).Set_ContactGrace(FCk_Time{0.12})
                .Set_ConfirmAngleDegrees(30.0f).Set_ConfirmTime(FCk_Time{0.075})
                .Set_SurfaceTurnRateDegrees(240.0f).Set_ClearanceSpeed(200.0f)
                .Set_Gravity(FVector{0.0, 0.0, -980.0}).Set_SteerFloor(0.4f)
                .Set_MaxStepHeight(1.3f * 64.0f).Set_WallPolicy(ck::EProceduralSurfaceWallPolicy::Slide);
            const auto Dt = FCk_Time{1.0 / 90.0};
            constexpr auto Speed = 180.0f;
            constexpr auto ReachTolerance = 1.0e-3;
            const auto MeasuredBody = FTransform{Origin + FVector{-631.434040, 0.0, 131.607492}};
            const auto MeasuredPosed = FTransform{FRotator{-0.080003, 0.000099, -0.141603}.Quaternion(),
                Origin + FVector{-631.434040, 0.0, 131.754415}};
            const auto PoseOffset = MeasuredPosed.GetRelativeTransform(MeasuredBody);
            TestTrue(TEXT("Derived offset reproduces the measured rounded presentation transform"),
                (PoseOffset * MeasuredBody).Equals(MeasuredPosed, 1.0e-6));
            const auto Anchors = TArray<ck::FProceduralSurfaceReachPaceAnchor>{
                {Origin + FVector{-660.046875, -81.375, 90.0}, FVector{45.0, -28.0, 0.0}, 100.0f},
                {Origin + FVector{-669.929688, -108.40625, 90.0}, FVector{-45.0, -28.0, 0.0}, 120.0f},
                {Origin + FVector{-654.351562, 83.46875, 90.0}, FVector{-45.0, 28.0, 0.0}, 120.0f}};
            const auto Plants = TArray<ck::FProceduralSurfacePlantedContact>{
                {Anchors[0].Get_FootWorld(), FVector::UpVector},
                {Anchors[1].Get_FootWorld(), FVector::UpVector},
                {Anchors[2].Get_FootWorld(), FVector::UpVector}};
            const auto AnchorView = TArrayView<const ck::FProceduralSurfaceReachPaceAnchor>{Anchors};
            const auto PlantView = TOptional<TArrayView<const ck::FProceduralSurfacePlantedContact>>{
                TArrayView<const ck::FProceduralSurfacePlantedContact>{Plants}};
            const auto NoFeet = TOptional<ck::FProceduralSurfaceFeetSupport>{};
            const auto OptionalPose = TOptional<FTransform>{PoseOffset};

            // The production bridge is copied exactly: every callback calls the actual Jolt query utility.
            const auto Filter = FCk_Jolt_QueryFilter{}.Set_BodySelection(ECk_Jolt_QueryBodySelection::StaticNonSensor);
            auto NativeRays = 0;
            auto Phase = 0;
            int32 RaysByPhase[4] = {};
            auto ZeroHits = TArray<ck::FProceduralSurfaceHit>{};
            const auto RayCast = [&](const FVector& InStart, const FVector& InEnd) -> ck::FProceduralSurfaceHit
            {
                ++NativeRays;
                ++RaysByPhase[Phase];
                const auto Hit = UCk_Utils_JoltQuery_UE::Get_RayCast(InServer, InStart, InEnd, Filter);
                const auto Result = ck::FProceduralSurfaceHit{}.Set_Hit(Hit.Get_HasHit())
                    .Set_Position(Hit.Get_Position()).Set_Normal(Hit.Get_Normal()).Set_Fraction(Hit.Get_Fraction());
                if (Phase == 1 && Hit.Get_HasHit())
                { ZeroHits.Add(Result); }
                return Result;
            };

            // Unlogged internal StartState is unknown. Four fresh zero-scale native steps construct it explicitly.
            // They do not manufacture a support point, assume a30cm contact, or reconstruct the original frame bit-for-bit.
            auto WarmBody = MeasuredBody;
            auto WarmState = ck::FProceduralSurfaceMotionState{};
            for (auto Index = 0; Index < 4; ++Index)
            {
                ck::StepProceduralSurfaceMotion(Settings, FVector::ForwardVector, Speed, Dt, RayCast, NoFeet,
                    WarmBody, WarmState, 0.0f, PlantView);
            }
            TestTrue(TEXT("Constructed native warmup at scale0 retains the measured translation and rotation"),
                WarmBody.Equals(MeasuredBody, 1.0e-6));
            Phase = 1;
            auto ZeroBody = MeasuredBody;
            auto ZeroState = WarmState;
            ck::StepProceduralSurfaceMotion(Settings, FVector::ForwardVector, Speed, Dt, RayCast, NoFeet,
                ZeroBody, ZeroState, 0.0f, PlantView);
            TestTrue(TEXT("Public scale0 contract retains translation while refreshing actual native contact"),
                ZeroBody.Equals(MeasuredBody, 1.0e-6));
            auto FreshSelectedPoint = false;
            for (const auto& Hit : ZeroHits)
            {
                FreshSelectedPoint |= Hit.Get_Position().Equals(ZeroState.Get_SupportPoint(), 1.0e-6)
                    && FVector::DotProduct(Hit.Get_Normal().GetSafeNormal(), ZeroState.Get_SupportNormal()) > 0.999999;
            }
            const auto HasFreshSupport = ZeroState.Get_Grounded() && ZeroState.Get_ContactTrusted()
                && ZeroState.Get_SupportNormal().IsNormalized() && FreshSelectedPoint;
            if (NOT TestTrue(TEXT("Zero trial selected a fresh actual native point; its height is measured, never assumed"), HasFreshSupport))
            { return; }
            const auto PointLocal = ZeroState.Get_SupportPoint() - Origin;
            const auto Normal = ZeroState.Get_SupportNormal();
            AddInfo(FString::Printf(TEXT("[NativeContactCorrectionReplay] CONSTRUCTED_STATE warmup4, measured rounded6dp body/pose;")
                TEXT(" zeroSource%d pointLocal(%.12f,%.12f,%.12f) normal(%.12f,%.12f,%.12f) fresh%d rays%d"),
                static_cast<int32>(ZeroState.Get_ContactSource()), PointLocal.X, PointLocal.Y, PointLocal.Z,
                Normal.X, Normal.Y, Normal.Z, FreshSelectedPoint, RaysByPhase[1]));

            const auto Envelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0});
            auto AdmissionCalls = 0;
            auto EnvelopeQueries = 0;
            auto OfferedBody = FTransform::Identity;
            const auto NativeAdmission = [&](const FTransform& InCandidate) -> bool
            {
                ++AdmissionCalls;
                OfferedBody = InCandidate;
                return ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter,
                        ZeroBody, InCandidate, EnvelopeQueries, 6)
                    && ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter,
                        PoseOffset * ZeroBody, PoseOffset * InCandidate, EnvelopeQueries, 6);
            };
            Phase = 2;
            auto Body = MeasuredBody;
            auto State = WarmState;
            const auto Outcome = ck::StepProceduralSurfaceMotionPaced(Settings, FVector::ForwardVector, Speed, Dt,
                RayCast, NoFeet, AnchorView, OptionalPose, Body, State, PlantView,
                TOptional<ck::FProceduralSurfaceContactCorrectionAdmission>{ck::FProceduralSurfaceContactCorrectionAdmission{NativeAdmission}});
            const auto Delta = Body.GetLocation() - ZeroBody.GetLocation();
            TestTrue(TEXT("Native-paced Core actually returns the admitted nonzero normal correction while forward pace stays zero"),
                AdmissionCalls == 1 && Body.Equals(OfferedBody, 1.0e-9) && Outcome.Get_Scale() == 0.0f
                && Outcome.Get_Trials() == 8 && NOT Outcome.Get_PhysicalOverride()
                && FVector::DotProduct(Delta, Normal) < 0.0
                && FVector::VectorPlaneProject(Delta, Normal).Size() <= 1.0e-8
                && Delta.Size() <= Settings.Get_ClearanceSpeed() * Dt.Get_Seconds() + 1.0e-8
                && Body.GetRotation().Equals(ZeroBody.GetRotation(), 1.0e-9));
            TestTrue(TEXT("Simulation and actual posed envelope paths were natively admitted within the existing proof cost"),
                EnvelopeQueries == 6);
            TestTrue(TEXT("Returned support remains the actual fresh selected native point and one-step contact state"),
                State.Get_SupportPoint().Equals(ZeroState.Get_SupportPoint(), 1.0e-9)
                && State.Get_SupportNormal().Equals(ZeroState.Get_SupportNormal(), 1.0e-9)
                && State.Get_ContactSource() == ZeroState.Get_ContactSource()
                && State.Get_MissingContact() == ZeroState.Get_MissingContact()
                && State.Get_CandidateSeen() == ZeroState.Get_CandidateSeen()
                && State.Get_Velocity().Equals((Body.GetLocation() - MeasuredBody.GetLocation()) / Dt.Get_Seconds(), 1.0e-6));
            const auto Posed = PoseOffset * Body;
            for (auto Index = 0; Index < Anchors.Num(); ++Index)
            {
                const auto& Anchor = Anchors[Index];
                const auto Sim = FVector::Dist(Body.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld());
                const auto Drawn = FVector::Dist(Posed.TransformPosition(Anchor.Get_HipLocal()), Anchor.Get_FootWorld());
                TestTrue(FString::Printf(TEXT("Native accepted anchor%d retains original simulation/posed R%.12f: %.12f/%.12f"),
                    Index, static_cast<double>(Anchor.Get_Reach()), Sim, Drawn),
                    Sim <= Anchor.Get_Reach() + ReachTolerance && Drawn <= Anchor.Get_Reach() + ReachTolerance);
            }
            AddInfo(FString::Printf(TEXT("[ContactCorrectionNative] constructedWarmup4 zeroSource%d point(%.12f,%.12f,%.12f)")
                TEXT(" returnedDelta(%.12f,%.12f,%.12f) scale%.12f trials%d rays%d envelope%d"),
                static_cast<int32>(ZeroState.Get_ContactSource()), PointLocal.X, PointLocal.Y, PointLocal.Z,
                Delta.X, Delta.Y, Delta.Z, static_cast<double>(Outcome.Get_Scale()), Outcome.Get_Trials(),
                RaysByPhase[2], EnvelopeQueries));

            // Exact projected padded-box bottoms derive obstacle height without adjusting any measured field.
            // Y20 lies off Motion's Y0 ray lane and inside the authored lateral box. Native baseline proofs below
            // are mandatory: this arithmetic is obstacle authoring, not permission to assume either baseline clear.
            const auto Padded = Envelope.Get_BodyHalfExtents() + FVector{Envelope.Get_CollisionMargin() + 0.05};
            const auto BottomZ = [&Padded](const FTransform& InBody)
            {
                const auto Rotation = InBody.GetRotation();
                return InBody.GetLocation().Z - FMath::Abs(Rotation.GetAxisX().Z) * Padded.X
                    - FMath::Abs(Rotation.GetAxisY().Z) * Padded.Y - FMath::Abs(Rotation.GetAxisZ().Z) * Padded.Z;
            };
            const auto OriginalBottom = FMath::Min(BottomZ(ZeroBody), BottomZ(PoseOffset * ZeroBody));
            const auto CorrectedBottom = FMath::Max(BottomZ(Body), BottomZ(PoseOffset * Body));
            if (NOT TestTrue(TEXT("Exact original/corrected simulation and posed box bottoms leave an obstacle authoring interval"),
                OriginalBottom > CorrectedBottom + 0.1)) { return; }
            const auto ObstacleTop = FVector{MeasuredBody.GetLocation().X, MeasuredBody.GetLocation().Y + 20.0,
                0.5 * (OriginalBottom + CorrectedBottom)};
            AddInfo(FString::Printf(TEXT("[ContactCorrectionNative] obstacleTopLocal(%.12f,%.12f,%.12f)")
                TEXT(" originalLowestBottomLocal%.12f correctedHighestBottomLocal%.12f; measured poses/anchors unchanged"),
                ObstacleTop.X - Origin.X, ObstacleTop.Y - Origin.Y, ObstacleTop.Z - Origin.Z,
                OriginalBottom - Origin.Z, CorrectedBottom - Origin.Z));
            if (NOT TestTrue(TEXT("A separate owned native obstacle is created for envelope rejection"),
                AddBox(ObstacleTop - FVector{0.0, 0.0, 0.5}, FVector{2.0, 2.0, 0.5}))) { return; }
            auto BaselineQueries = 0;
            if (NOT TestTrue(TEXT("Both unchanged baseline bodies remain natively clear before testing atomic rejection"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter, ZeroBody, ZeroBody, BaselineQueries, 6)
                && ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Envelope, Filter,
                    PoseOffset * ZeroBody, PoseOffset * ZeroBody, BaselineQueries, 6))) { return; }
            Phase = 3;
            AdmissionCalls = 0;
            EnvelopeQueries = 0;
            auto RejectedBody = MeasuredBody;
            auto RejectedState = WarmState;
            const auto RejectedOutcome = ck::StepProceduralSurfaceMotionPaced(Settings, FVector::ForwardVector, Speed, Dt,
                RayCast, NoFeet, AnchorView, OptionalPose, RejectedBody, RejectedState, PlantView,
                TOptional<ck::FProceduralSurfaceContactCorrectionAdmission>{ck::FProceduralSurfaceContactCorrectionAdmission{NativeAdmission}});
            TestTrue(TEXT("A native occupied correction is rejected before any corrected body/support/velocity is published"),
                AdmissionCalls == 1 && OfferedBody.Equals(Body, 1.0e-9) && EnvelopeQueries > 0 && EnvelopeQueries <= 6
                && RejectedOutcome.Get_Scale() == 0.0f && RejectedOutcome.Get_Trials() == 8
                && NOT RejectedOutcome.Get_PhysicalOverride() && RejectedBody.Equals(ZeroBody, 1.0e-9)
                && RejectedState.Get_SupportPoint().Equals(ZeroState.Get_SupportPoint(), 1.0e-9)
                && RejectedState.Get_SupportNormal().Equals(ZeroState.Get_SupportNormal(), 1.0e-9)
                && RejectedState.Get_Velocity().Equals(ZeroState.Get_Velocity(), 1.0e-9)
                && RejectedState.Get_ContactSource() == ZeroState.Get_ContactSource()
                && RejectedState.Get_MissingContact() == ZeroState.Get_MissingContact()
                && RejectedState.Get_CandidateSeen() == ZeroState.Get_CandidateSeen());
            TestTrue(TEXT("Cached correction and native admission add no surface rays or ninth replay"),
                RaysByPhase[2] == RaysByPhase[3] && RaysByPhase[2] == Outcome.Get_Trials() * RaysByPhase[1]
                && NativeRays <= 512);
        })));
    ck_test_height_planning_native::Enqueue(this);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
// CONTACT_CORRECTION_NATIVE_REGRESSION_END


#endif

#endif

// --------------------------------------------------------------------------------------------------------------------
