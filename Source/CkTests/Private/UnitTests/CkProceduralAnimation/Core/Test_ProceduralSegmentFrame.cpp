#include "CkProceduralAnimation/Core/CkProceduralSegmentFrame.h"

#include "../../CkUnitTest_Common.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralSegmentFrameContinuityTest,
    "Ck.ProceduralAnimation.Rig.SegmentFrame.PoleCrossingKeepsContinuousRoll",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralSegmentFrameContinuityTest::RunTest(
        const FString&)
    -> bool
{
    // A real two-section leg's knee lies 180 cm from its hip. At this target the finite pole lies on the lower
    // segment's extension. Crossing it by 0.02 cm must not roll a box or an asymmetric leg mesh through 180 degrees.
    const auto Knee = FVector{90.0, 0.0, FMath::Sqrt(180.0 * 180.0 - 90.0 * 90.0)};
    const auto Pole = FVector{0.0, 0.0, 360.0};
    const auto End = Knee + (Knee - Pole).GetSafeNormal() * 180.0;
    const auto BeforeEnd = End - FVector{0.0, 0.0, 0.01};
    const auto AfterEnd = End + FVector{0.0, 0.0, 0.01};
    const auto Before = ck::ComputeProceduralSegmentFrame({Knee, BeforeEnd, Pole, {}});
    const auto RawAfter = ck::ComputeProceduralSegmentFrame({Knee, AfterEnd, Pole, {}});
    TestTrue(TEXT("Precondition: the finite pole projection has opposite roll branches"),
        FMath::RadiansToDegrees(Before.AngularDistance(RawAfter)) > 179.0);
    const auto Continuous = ck::ComputeProceduralSegmentFrame({Knee, AfterEnd, Pole, Before});
    TestTrue(TEXT("An infinitesimal target move does not create a 180-degree segment twist"),
        FMath::RadiansToDegrees(Before.AngularDistance(Continuous)) < 1.0);
    TestTrue(TEXT("Continuity never changes the segment's solved long axis"),
        Continuous.GetForwardVector().Equals((AfterEnd - Knee).GetSafeNormal(), 1.0e-8));
    auto Previous = Before;
    for (auto Step = -9; Step <= 10; ++Step)
    {
        const auto Target = End + FVector{0.0, 0.0, Step * 0.001};
        const auto Current = ck::ComputeProceduralSegmentFrame({Knee, Target, Pole, Previous});
        TestTrue(TEXT("The exact pole-axis crossing is continuous too"),
            FMath::RadiansToDegrees(Previous.AngularDistance(Current)) < 1.0);
        TestTrue(TEXT("Every intermediate long axis still points at its endpoint"),
            Current.GetForwardVector().Equals((Target - Knee).GetSafeNormal(), 1.0e-8));
        Previous = Current;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralSegmentFrameInitialTest,
    "Ck.ProceduralAnimation.Rig.SegmentFrame.FirstSolveKeepsAuthoredPlane",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralSegmentFrameInitialTest::RunTest(
        const FString&)
    -> bool
{
    const auto From = FVector{0.0, 0.0, 110.0};
    const auto To = FVector{150.0, 20.0, 8.0};
    const auto Pole = FVector{0.0, 0.0, 470.0};
    const auto Along = (To - From).GetSafeNormal();
    const auto ExpectedY = FVector::VectorPlaneProject(Pole - From, Along).GetSafeNormal();
    const auto First = ck::ComputeProceduralSegmentFrame({From, To, Pole, {}});
    TestTrue(TEXT("Without a settled prior pose the frame still follows the authored pole"),
        First.GetRightVector().Equals(ExpectedY, 1.0e-8));
    TestTrue(TEXT("The initial frame has a normalized rotation"), First.IsNormalized());
    return true;
}
#endif
