#include "CkProceduralAnimation/Core/CkProceduralActionMotion.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMotionShapeTest,
    "Ck.ProceduralAnimation.Action.Motion.RejectsIncomparableGeometry",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionMotionShapeTest::RunTest(
    const FString&)
    -> bool
{
    const auto Limb = FCk_ProceduralArticulation_LimbPose{TEXT("Arm"), FTransform::Identity,
        FTransform::Identity, {FTransform::Identity}, 0.0f, ECk_ProceduralRig_ChainState::Clear,
        ECk_ProceduralArticulation_ContactEvidence::None};
    const auto Previous = FCk_ProceduralArticulation_Pose{FTransform::Identity,
        FTransform::Identity, false, {Limb}};
    auto Travel = 0.0f;
    auto Angular = 0.0f;
    ck::procedural_action::MeasureMotion(Previous, Previous, Travel, Angular);
    TestEqual(TEXT("Unchanged endpoint has zero travel"), Travel, 0.0f);
    TestEqual(TEXT("Unchanged segments have zero angular motion"), Angular, 0.0f);
    const auto ExpectUnbounded = [&](const TCHAR* InReason, const FCk_ProceduralArticulation_Pose& InCurrent)
    {
        ck::procedural_action::MeasureMotion(Previous, InCurrent, Travel, Angular);
        TestTrue(InReason, NOT FMath::IsFinite(Travel) && NOT FMath::IsFinite(Angular));
    };
    ExpectUnbounded(TEXT("Missing limb cannot look stationary"), FCk_ProceduralArticulation_Pose{});
    ExpectUnbounded(TEXT("Body channel mismatch rejects"), FCk_ProceduralArticulation_Pose{
        FTransform::Identity, FTransform::Identity, true, {Limb}});
    ExpectUnbounded(TEXT("Duplicate identities reject"), FCk_ProceduralArticulation_Pose{
        FTransform::Identity, FTransform::Identity, false, {Limb, Limb}});
    const auto LongerLimb = FCk_ProceduralArticulation_LimbPose{TEXT("Arm"), FTransform::Identity,
        FTransform::Identity, {FTransform::Identity, FTransform::Identity}, 0.0f,
        ECk_ProceduralRig_ChainState::Clear, ECk_ProceduralArticulation_ContactEvidence::None};
    ExpectUnbounded(TEXT("Changed link count cannot truncate the comparison"),
        FCk_ProceduralArticulation_Pose{FTransform::Identity, FTransform::Identity, false, {LongerLimb}});
    const auto OtherLimb = FCk_ProceduralArticulation_LimbPose{TEXT("Other"), FTransform::Identity,
        FTransform::Identity, {FTransform::Identity}, 0.0f, ECk_ProceduralRig_ChainState::Clear,
        ECk_ProceduralArticulation_ContactEvidence::None};
    ExpectUnbounded(TEXT("Replaced identity cannot look stationary"), FCk_ProceduralArticulation_Pose{
        FTransform::Identity, FTransform::Identity, false, {OtherLimb}});
    auto Nonfinite = FTransform::Identity;
    Nonfinite.SetTranslation(FVector{std::numeric_limits<double>::infinity(), 0.0, 0.0});
    ExpectUnbounded(TEXT("Nonfinite root rejects before measuring"), FCk_ProceduralArticulation_Pose{
        Nonfinite, FTransform::Identity, false, {Limb}});
    const auto Moved = FCk_ProceduralArticulation_LimbPose{TEXT("Arm"), FTransform::Identity,
        FTransform{FVector{3.0, 4.0, 0.0}}, {FTransform{FQuat{FVector::UpVector, PI / 6.0}}}, 0.0f,
        ECk_ProceduralRig_ChainState::Clear, ECk_ProceduralArticulation_ContactEvidence::None};
    ck::procedural_action::MeasureMotion(Previous, FCk_ProceduralArticulation_Pose{
        FTransform::Identity, FTransform::Identity, false, {Moved}}, Travel, Angular);
    TestTrue(TEXT("Valid endpoint travel measured in centimetres"), FMath::IsNearlyEqual(Travel, 5.0f));
    TestTrue(TEXT("Valid link rotation measured in degrees"), FMath::IsNearlyEqual(Angular, 30.0f, 0.001f));

    const auto MakeScaled = [](const FVector& InScale, const FQuat& InRotation)
    {
        const auto ScaledLimb = FCk_ProceduralArticulation_LimbPose{TEXT("Arm"), FTransform::Identity,
            FTransform::Identity, {FTransform{InRotation, FVector::ZeroVector, InScale}}, 0.0f,
            ECk_ProceduralRig_ChainState::Clear, ECk_ProceduralArticulation_ContactEvidence::None};
        return FCk_ProceduralArticulation_Pose{FTransform::Identity, FTransform::Identity, false, {ScaledLimb}};
    };
    const auto VisualScale = FVector{2.0, 0.5, -1.5};
    const auto Scaled = MakeScaled(VisualScale, FQuat::Identity);
    ck::procedural_action::MeasureMotion(Scaled,
        MakeScaled(VisualScale, FQuat{FVector::UpVector, PI / 6.0}), Travel, Angular);
    TestEqual(TEXT("Stable visual scale cannot add solver endpoint travel"), Travel, 0.0f);
    TestTrue(TEXT("Stable nonuniform mirrored visual scale preserves angular measurement"),
        FMath::IsNearlyEqual(Angular, 30.0f, 0.001f));
    ck::procedural_action::MeasureMotion(Scaled, MakeScaled(FVector::OneVector, FQuat::Identity), Travel, Angular);
    TestTrue(TEXT("Changed visual scale is not silently sampled as zero motion"),
        NOT FMath::IsFinite(Travel) && NOT FMath::IsFinite(Angular));
    TestFalse(TEXT("Degenerate visual scale rejects"),
        ck::procedural_action::IsMotionPoseValid(MakeScaled(FVector{1.0, 0.0, 1.0}, FQuat::Identity)));
    TestFalse(TEXT("Nonfinite visual scale rejects"), ck::procedural_action::IsMotionPoseValid(
        MakeScaled(FVector{std::numeric_limits<double>::infinity(), 1.0, 1.0}, FQuat::Identity)));
    return true;
}
#endif
