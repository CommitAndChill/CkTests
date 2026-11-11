#include "CkCore/Format/CkFormat.h"

#include "CkJolt/Query/CkJoltBallistic_Math.h"

#include <Misc/AutomationTest.h>

// --------------------------------------------------------------------------------------------------------------------
// Pins the free-flight step the ballistic cast replays (ck::jolt::ballistic::Advance_OneStep) against closed forms:
// with no damping a semi-implicit Euler flight lands on the discrete parabola exactly, damping shortens it, a zero
// gravity factor leaves the vertical speed alone, and the speed clamp caps the velocity after a single step.
// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_jolt_ballistic_math
{
    constexpr auto kTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

    constexpr float StepDt = 1.0f / 60.0f;
    constexpr int32 NumSteps = 120;

    const FVector StartLocation = FVector{100.0, -200.0, 300.0};
    const FVector3f StartVelocity = FVector3f{600.0f, 0.0f, 500.0f};
    const FVector3f Gravity = FVector3f{0.0f, 0.0f, -980.0f};

    auto Make_Flight(
        float InLinearDamping,
        float InGravityFactor,
        float InMaxLinearVelocity)
        -> FCk_Jolt_BallisticFlight
    {
        auto Flight = FCk_Jolt_BallisticFlight{};
        Flight.Set_LinearDamping(InLinearDamping);
        Flight.Set_GravityFactor(InGravityFactor);
        Flight.Set_MaxLinearVelocity(InMaxLinearVelocity);
        return Flight;
    }

    auto Fly(
        const FCk_Jolt_BallisticFlight& InFlight,
        FVector3f& OutVelocity)
        -> FVector
    {
        auto Location = StartLocation;
        OutVelocity = StartVelocity;

        for (auto Step = 0; Step < NumSteps; ++Step)
        { ck::jolt::ballistic::Advance_OneStep(OutVelocity, Location, InFlight, Gravity, StepDt); }

        return Location;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_JoltBallisticCast_Math_ZeroDampingMatchesTheDiscreteParabola,
    "Ck.Jolt.BallisticCast.Math.ZeroDampingMatchesTheDiscreteParabola",
    ck_test_jolt_ballistic_math::kTestFlags)

bool FCkTest_JoltBallisticCast_Math_ZeroDampingMatchesTheDiscreteParabola::RunTest(const FString& Parameters)
{
    using namespace ck_test_jolt_ballistic_math;

    auto Velocity = FVector3f::ZeroVector;
    const auto Final = Fly(Make_Flight(0.0f, 1.0f, 50000.0f), Velocity);

    // Velocity is updated before the move, so after N steps the position is P0 + V0*T + g*Dt^2*N(N+1)/2.
    const auto Dt = static_cast<double>(StepDt);
    const auto T = Dt * NumSteps;
    const auto Expected = StartLocation + FVector{StartVelocity} * T + 0.5 * FVector{Gravity} * T * (T + Dt);

    const auto Error = FVector::Dist(Final, Expected);
    TestTrue(ck::Format_UE(TEXT("final location {} matches the discrete parabola {} within 0.01 cm (error {})"),
        Final.ToString(), Expected.ToString(), Error), Error <= 0.01);

    // A step that did nothing would leave the start location; the flight must have gone somewhere.
    TestTrue(TEXT("the flight moved"), FVector::Dist(Final, StartLocation) > 100.0);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_JoltBallisticCast_Math_DampingShortensTheFlight,
    "Ck.Jolt.BallisticCast.Math.DampingShortensTheFlight",
    ck_test_jolt_ballistic_math::kTestFlags)

bool FCkTest_JoltBallisticCast_Math_DampingShortensTheFlight::RunTest(const FString& Parameters)
{
    using namespace ck_test_jolt_ballistic_math;

    auto UndampedVelocity = FVector3f::ZeroVector;
    const auto Undamped = Fly(Make_Flight(0.0f, 1.0f, 50000.0f), UndampedVelocity);

    auto DampedVelocity = FVector3f::ZeroVector;
    const auto Damped = Fly(Make_Flight(0.2f, 1.0f, 50000.0f), DampedVelocity);

    const auto Shortfall = Undamped.X - Damped.X;
    TestTrue(ck::Format_UE(TEXT("damping 0.2 lands more than 20 cm short in X (undamped X {}, damped X {})"),
        Undamped.X, Damped.X), Shortfall > 20.0);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_JoltBallisticCast_Math_GravityFactorScalesTheFall,
    "Ck.Jolt.BallisticCast.Math.GravityFactorScalesTheFall",
    ck_test_jolt_ballistic_math::kTestFlags)

bool FCkTest_JoltBallisticCast_Math_GravityFactorScalesTheFall::RunTest(const FString& Parameters)
{
    using namespace ck_test_jolt_ballistic_math;

    auto WeightlessVelocity = FVector3f::ZeroVector;
    const auto Weightless = Fly(Make_Flight(0.0f, 0.0f, 50000.0f), WeightlessVelocity);

    TestTrue(ck::Format_UE(TEXT("gravity factor 0 keeps the Z velocity at {} (got {})"),
        StartVelocity.Z, WeightlessVelocity.Z), FMath::IsNearlyEqual(WeightlessVelocity.Z, StartVelocity.Z, 1e-4f));

    // The weightless flight rises in a straight line; the weighted one must fall below it.
    auto WeightedVelocity = FVector3f::ZeroVector;
    const auto Weighted = Fly(Make_Flight(0.0f, 1.0f, 50000.0f), WeightedVelocity);
    TestTrue(ck::Format_UE(TEXT("gravity factor 1 ends lower than gravity factor 0 ({} vs {})"),
        Weighted.Z, Weightless.Z), Weighted.Z < Weightless.Z - 100.0);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_JoltBallisticCast_Math_SpeedIsClamped,
    "Ck.Jolt.BallisticCast.Math.SpeedIsClamped",
    ck_test_jolt_ballistic_math::kTestFlags)

bool FCkTest_JoltBallisticCast_Math_SpeedIsClamped::RunTest(const FString& Parameters)
{
    using namespace ck_test_jolt_ballistic_math;

    auto Velocity = FVector3f{540.0f, 0.0f, 720.0f};
    TestTrue(TEXT("the launch speed is 900"), FMath::IsNearlyEqual(Velocity.Size(), 900.0f, 1e-3f));

    auto Location = StartLocation;
    ck::jolt::ballistic::Advance_OneStep(Velocity, Location, Make_Flight(0.0f, 1.0f, 100.0f), Gravity, StepDt);

    TestTrue(ck::Format_UE(TEXT("one step caps the speed at 100 (got {})"), Velocity.Size()),
        FMath::IsNearlyEqual(Velocity.Size(), 100.0f, 1e-3f));

    const auto Moved = FVector::Dist(Location, StartLocation);
    TestTrue(ck::Format_UE(TEXT("the move uses the clamped speed: {} cm in one step"), Moved),
        FMath::IsNearlyEqual(Moved, 100.0 * StepDt, 1e-3));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------
