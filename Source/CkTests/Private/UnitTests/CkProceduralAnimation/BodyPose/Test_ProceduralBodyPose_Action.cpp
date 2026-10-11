#include "CkProceduralAnimation/Core/CkProceduralBodySupport.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_body_pose_action
{
    constexpr auto DistanceTolerance = 1.0e-4;
    constexpr auto RotationTolerance = 1.0e-5;

    auto
        Get_QuatsMatch(
            const FQuat& InA,
            const FQuat& InB)
        -> bool
    {
        return InA.AngularDistance(InB) <= RotationTolerance;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralBodyPoseActionComposeRigidOffsetTest,
    "Ck.ProceduralAnimation.BodyPoseAction.ComposeRigidOffset",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralBodyPoseActionComposeRigidOffsetTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_body_pose_action;

    const auto Jab = FTransform{FVector{24.0, 0.0, 0.0}};
    const auto FromIdentity = ck::ComposeProceduralBodyPoseAction(FTransform::Identity, Jab);
    if (NOT TestTrue(TEXT("An identity base composes with a translation"), FromIdentity.IsSet()))
    { return false; }
    TestTrue(TEXT("An identity base yields the action's translation"),
        FromIdentity->GetLocation().Equals(FVector{24.0, 0.0, 0.0}, DistanceTolerance));
    TestTrue(TEXT("An identity base and a pure translation keep the rotation identity"),
        Get_QuatsMatch(FromIdentity->GetRotation(), FQuat::Identity));

    const auto Yaw = FQuat{FVector::UpVector, FMath::DegreesToRadians(90.0)};
    const auto Roll = FQuat{FVector::ForwardVector, FMath::DegreesToRadians(90.0)};
    if (NOT TestFalse(TEXT("Precondition: the rotation pair does not commute"), Get_QuatsMatch(Yaw * Roll, Roll * Yaw)))
    { return false; }

    const auto YawThenRoll = ck::ComposeProceduralBodyPoseAction(FTransform{Yaw}, FTransform{Roll});
    const auto RollThenYaw = ck::ComposeProceduralBodyPoseAction(FTransform{Roll}, FTransform{Yaw});
    if (NOT TestTrue(TEXT("Both orders compose"), YawThenRoll.IsSet() && RollThenYaw.IsSet()))
    { return false; }
    TestTrue(TEXT("A yaw base with a roll action composes as Base * Action"),
        Get_QuatsMatch(YawThenRoll->GetRotation(), (Yaw * Roll).GetNormalized()));
    TestTrue(TEXT("A roll base with a yaw action composes as Base * Action"),
        Get_QuatsMatch(RollThenYaw->GetRotation(), (Roll * Yaw).GetNormalized()));
    TestTrue(TEXT("The action rotates in the base's frame: the body's forward, rolled then yawed, points along +Y"),
        YawThenRoll->GetRotation().RotateVector(FVector::ForwardVector).Equals(FVector::RightVector, DistanceTolerance));
    TestTrue(TEXT("The reversed order sends the body's forward along +Z"),
        RollThenYaw->GetRotation().RotateVector(FVector::ForwardVector).Equals(FVector::UpVector, DistanceTolerance));

    const auto Pitch = FQuat{FVector::RightVector, FMath::DegreesToRadians(30.0)};
    const auto TiltedBase = FTransform{Pitch, FVector{0.0, 0.0, -5.0}};
    const auto FromTilt = ck::ComposeProceduralBodyPoseAction(TiltedBase, FTransform{FVector{10.0, 0.0, 0.0}});
    if (NOT TestTrue(TEXT("A tilted base composes with a translation"), FromTilt.IsSet()))
    { return false; }
    TestTrue(FString::Printf(TEXT("The action's translation is in the simulation body's axes, not rotated by the base tilt (got %s)"),
            *FromTilt->GetLocation().ToString()),
        FromTilt->GetLocation().Equals(FVector{10.0, 0.0, -5.0}, DistanceTolerance));
    TestTrue(TEXT("A pure translation keeps the base's tilt"), Get_QuatsMatch(FromTilt->GetRotation(), Pitch));

    for (const auto& Composed : {*FromIdentity, *YawThenRoll, *RollThenYaw, *FromTilt})
    {
        TestTrue(TEXT("The composed offset has unit scale"), Composed.GetScale3D().Equals(FVector::OneVector, DistanceTolerance));
        TestTrue(TEXT("The composed offset has a normalized rotation"), Composed.GetRotation().IsNormalized());
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralBodyPoseActionAdmissionTest,
    "Ck.ProceduralAnimation.BodyPoseAction.Admission",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralBodyPoseActionAdmissionTest::
    RunTest(const FString&)
    -> bool
{
    const auto NaN = std::numeric_limits<double>::quiet_NaN();
    const auto Infinity = std::numeric_limits<double>::infinity();

    auto ZeroRotation = FTransform::Identity;
    ZeroRotation.SetRotation(FQuat{0.0, 0.0, 0.0, 0.0});
    auto LongRotation = FTransform::Identity;
    LongRotation.SetRotation(FQuat{0.0, 0.0, 0.0, 2.0});
    auto NaNRotation = FTransform::Identity;
    NaNRotation.SetRotation(FQuat{NaN, 0.0, 0.0, 1.0});
    auto ScaledUp = FTransform::Identity;
    ScaledUp.SetScale3D(FVector{1.5});
    auto ScaledToZero = FTransform::Identity;
    ScaledToZero.SetScale3D(FVector::ZeroVector);

    const auto DoExpectRejected = [&](const FTransform& InAction, const TCHAR* InCase) -> void
    {
        TestFalse(FString::Printf(TEXT("%s: the action is not admissible"), InCase),
            ck::Get_IsProceduralBodyPoseActionAdmissible(InAction));
        TestFalse(FString::Printf(TEXT("%s: composing it yields no offset"), InCase),
            ck::ComposeProceduralBodyPoseAction(FTransform::Identity, InAction).IsSet());
        TestFalse(FString::Printf(TEXT("%s: composing onto it as a base yields no offset"), InCase),
            ck::ComposeProceduralBodyPoseAction(InAction, FTransform::Identity).IsSet());
    };

    DoExpectRejected(FTransform{FVector{NaN, 0.0, 0.0}}, TEXT("A NaN translation"));
    DoExpectRejected(FTransform{FVector{0.0, Infinity, 0.0}}, TEXT("An infinite translation"));
    DoExpectRejected(NaNRotation, TEXT("A NaN rotation"));
    DoExpectRejected(ZeroRotation, TEXT("A zero quaternion"));
    DoExpectRejected(LongRotation, TEXT("A non-normalized quaternion"));
    DoExpectRejected(ScaledUp, TEXT("A scale of 1.5"));
    DoExpectRejected(ScaledToZero, TEXT("A scale of 0"));

    TestTrue(TEXT("Identity is admissible"), ck::Get_IsProceduralBodyPoseActionAdmissible(FTransform::Identity));
    const auto Turned = FTransform{FQuat{FVector::UpVector, FMath::DegreesToRadians(35.0)}, FVector{12.0, -3.0, 4.0}};
    TestTrue(TEXT("A unit rotation with a finite translation is admissible"), ck::Get_IsProceduralBodyPoseActionAdmissible(Turned));
    TestTrue(TEXT("An admissible action composes"), ck::ComposeProceduralBodyPoseAction(FTransform::Identity, Turned).IsSet());

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif

// --------------------------------------------------------------------------------------------------------------------
