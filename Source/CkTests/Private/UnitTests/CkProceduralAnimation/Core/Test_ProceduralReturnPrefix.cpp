#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Core/CkProceduralReturnPrefix.h"

#include "Misc/AutomationTest.h"

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralReturnPrefixImprovesSafeHalfTest,
    "Ck.ProceduralAnimation.Action.Core.ReturnPrefixImprovesSafeHalf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralReturnPrefixImprovesSafeHalfTest::
    RunTest(const FString&)
    -> bool
{
    auto ProbeCount = 0;
    auto Interrupted = false;
    auto LastProbed = 0.0;
    auto AllProbesValid = true;
    const auto Safe = ck::procedural_action::FindReturnPrefix(
        [&](double InFraction)
        {
            AllProbesValid &= FMath::IsFinite(InFraction) && InFraction > 0.0 && InFraction < 1.0;
            LastProbed = InFraction;
            return InFraction <= 0.73
                ? ck::procedural_action::EReturnPrefixProbe::Qualified
                : ck::procedural_action::EReturnPrefixProbe::Rejected;
        }, ProbeCount, Interrupted);
    TestFalse(TEXT("Search did not interrupt"), Interrupted);
    TestTrue(TEXT("Full return is unsafe but a prefix above half is retained"),
        Safe.IsSet() && Safe.GetValue() > 0.5 && Safe.GetValue() <= 0.73);
    TestTrue(TEXT("Final evaluation uses the retained exact safe fraction"),
        Safe.IsSet() && FMath::IsNearlyEqual(LastProbed, Safe.GetValue(), 1.0e-9));
    TestTrue(TEXT("All probes are finite positive fractions below full"), AllProbesValid);
    TestTrue(TEXT("Discovery, four improvements, and final evaluation stay bounded"),
        ProbeCount <= 32);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralReturnPrefixNonMonotoneAndInterruptTest,
    "Ck.ProceduralAnimation.Action.Core.ReturnPrefixNonMonotoneAndInterrupt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralReturnPrefixNonMonotoneAndInterruptTest::
    RunTest(const FString&)
    -> bool
{
    auto ProbeCount = 0;
    auto Interrupted = false;
    auto LastProbed = 0.0;
    const auto NonMonotone = ck::procedural_action::FindReturnPrefix(
        [&](double InFraction)
        {
            LastProbed = InFraction;
            return InFraction <= 0.5 || (InFraction > 0.70 && InFraction < 0.73)
                ? ck::procedural_action::EReturnPrefixProbe::Qualified
                : ck::procedural_action::EReturnPrefixProbe::Rejected;
        }, ProbeCount, Interrupted);
    TestFalse(TEXT("Local rejected midpoint does not interrupt"), Interrupted);
    TestTrue(TEXT("Non-monotone path retains an actually qualified fraction"),
        NonMonotone.IsSet() && FMath::IsNearlyEqual(NonMonotone.GetValue(), 0.5, 1.0e-9));
    TestTrue(TEXT("Non-monotone final fraction was directly qualified"),
        NonMonotone.IsSet() && FMath::IsNearlyEqual(LastProbed, NonMonotone.GetValue(), 1.0e-9));
    TestTrue(TEXT("Non-monotone search respects the probe cap"), ProbeCount <= 32);

    const auto Aborted = ck::procedural_action::FindReturnPrefix(
        [](double InFraction)
        {
            return InFraction > 0.5
                ? ck::procedural_action::EReturnPrefixProbe::Interrupted
                : ck::procedural_action::EReturnPrefixProbe::Qualified;
        }, ProbeCount, Interrupted);
    TestFalse(TEXT("Interrupted probe never returns a prefix"), Aborted.IsSet());
    TestTrue(TEXT("Interrupted status propagates without staging"), Interrupted);
    TestTrue(TEXT("Interrupted search respects the probe cap"), ProbeCount <= 32);

    const auto NoSafe = ck::procedural_action::FindReturnPrefix(
        [](double InFraction)
        {
            return NOT FMath::IsFinite(InFraction) || InFraction <= 0.0 || InFraction >= 1.0
                ? ck::procedural_action::EReturnPrefixProbe::Interrupted
                : ck::procedural_action::EReturnPrefixProbe::Rejected;
        }, ProbeCount, Interrupted);
    TestFalse(TEXT("No valid positive safe fraction yields no result"), NoSafe.IsSet());
    TestFalse(TEXT("A valid-fraction rejection is not an interruption"), Interrupted);
    TestTrue(TEXT("No-safe search is finite"), ProbeCount <= 32);
    return true;
}

#endif
