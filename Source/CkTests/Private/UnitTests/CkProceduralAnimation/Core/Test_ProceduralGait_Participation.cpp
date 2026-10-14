#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"

#include "../../CkUnitTest_Common.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_procedural_gait_participation
{
    constexpr auto Dt = FCk_Time{1.0 / 60.0};

    auto
        MakeInputs()
        -> TArray<ck::FProceduralGaitLegInput>
    {
        auto Inputs = TArray<ck::FProceduralGaitLegInput>{};
        Inputs.SetNum(2);
        Inputs[0].Set_IdealTarget(FVector{0.0, 0.0, 0.0});
        Inputs[1].Set_IdealTarget(FVector{50.0, 0.0, 0.0});
        Inputs[0].Set_PhaseOffset(0.0f);
        Inputs[1].Set_PhaseOffset(0.5f);
        return Inputs;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitParticipationTest,
    "Ck.ProceduralAnimation.Action.GaitCore.ParticipationAndTakeoffGuard",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralGaitParticipationTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_participation;
    auto Solver = ck::FProceduralGaitSolver{};
    const auto Initial = TArray<FVector>{FVector::ZeroVector, FVector{50.0, 0.0, 0.0}};
    TestTrue(TEXT("Two-leg solver resets"), Solver.Reset(Initial));
    auto Inputs = MakeInputs();
    auto Outputs = TArray<ck::FProceduralGaitLegOutput>{};
    Outputs.SetNum(2);
    auto Denied = TArray<ck::FProceduralGaitDeniedTakeoff>{};
    const auto Deny = [](const ck::FProceduralGaitSolver&, int32, bool) { return false; };

    Inputs[0].Set_Participating(false);
    TestTrue(TEXT("Withdrawal input is accepted"), Solver.StepWithTakeoffGuard(
        Dt, 0.0f, FVector::ZeroVector, Inputs, Outputs, false, {}, Deny, Denied));
    TestEqual(TEXT("Structural enabled count is unchanged"), Solver.NumEnabledLegs(), 2);
    TestEqual(TEXT("Effective participant count excludes withdrawn leg"), Solver.NumParticipatingLegs(), 1);
    TestFalse(TEXT("Withdrawn leg has no ordinary participation"), Solver.IsLegParticipating(0));
    TestFalse(TEXT("Withdrawn leg is not a planted support output"), Outputs[0].Get_Planted());
    TestFalse(TEXT("Withdrawn leg cannot queue a catch step"), Solver.RequestStep(0, FVector{100.0, 0.0, 0.0}));
    TestFalse(TEXT("Withdrawal clears plant trust"), Solver.GetLegState(0).Get_Plant().Get_Trusted());

    Inputs[0].Set_Participating(true);
    Inputs[0].Set_Enabled(false);
    TestTrue(TEXT("Structural disable remains independent"), Solver.StepWithTakeoffGuard(
        Dt, 0.0f, FVector::ZeroVector, Inputs, Outputs, false, {}, Deny, Denied));
    TestEqual(TEXT("Structural disable removes one enabled leg"), Solver.NumEnabledLegs(), 1);
    Inputs[0].Set_Enabled(true);
    Inputs[0].Set_IdealTarget(FVector{100.0, 0.0, 0.0});
    TestTrue(TEXT("Re-enable with denied takeoff advances"), Solver.StepWithTakeoffGuard(
        Dt, 0.0f, FVector::ZeroVector, Inputs, Outputs, false, {}, Deny, Denied));
    TestTrue(TEXT("Re-enable path reports denial"), Denied.ContainsByPredicate(
        [](const ck::FProceduralGaitDeniedTakeoff& InDenied)
        { return InDenied.Get_LegIndex() == 0 && NOT InDenied.Get_Emergency(); }));
    TestFalse(TEXT("Re-enabled leg did not swing through guard"), Solver.GetLegState(0).Get_Swing().Get_Active());
    TestFalse(TEXT("Denied takeoff cannot invent trusted plant"), Solver.GetLegState(0).Get_Plant().Get_Trusted());

    TestTrue(TEXT("Airborne takeoff transitions"), Solver.Step(Dt, 0.0f, FVector::ZeroVector,
        Inputs, Outputs, true));
    TestTrue(TEXT("Landing transition with denied takeoff advances"), Solver.StepWithTakeoffGuard(
        Dt, 0.0f, FVector::ZeroVector, Inputs, Outputs, false, {}, Deny, Denied));
    TestEqual(TEXT("Airborne landing denial is bounded by leg count"), Denied.Num(), 2);
    TestFalse(TEXT("Landing guard prevented first swing"), Solver.GetLegState(0).Get_Swing().Get_Active());
    TestFalse(TEXT("Landing guard prevented second swing"), Solver.GetLegState(1).Get_Swing().Get_Active());

    // Repeated scheduled/emergency attempts must never bypass the guard or emit more than one denial per leg per Step.
    auto SawScheduledDenial = false;
    for (auto Frame = 0; Frame < 300; ++Frame)
    {
        Inputs[0].Set_IdealTarget(FVector{200.0, 0.0, 0.0});
        if (NOT Solver.StepWithTakeoffGuard(Dt, 100.0f, FVector{100.0, 0.0, 0.0},
            Inputs, Outputs, false, {}, Deny, Denied))
        { return false; }
        TestTrue(TEXT("Denied takeoffs stay bounded by topology"), Denied.Num() <= 2);
        if (Denied.ContainsByPredicate([](const ck::FProceduralGaitDeniedTakeoff& InDenied)
            { return InDenied.Get_LegIndex() == 0; }))
        { SawScheduledDenial = true; }
        TestFalse(TEXT("Guarded leg never starts an ordinary swing"), Solver.GetLegState(0).Get_Swing().Get_Active());
    }
    TestTrue(TEXT("Scheduled or emergency lane reached guard"), SawScheduledDenial);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitGuardParityTest,
    "Ck.ProceduralAnimation.Action.GaitCore.DefaultPathParityAndPreparedSwing",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralGaitGuardParityTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_participation;
    auto Legacy = ck::FProceduralGaitSolver{};
    auto Guarded = ck::FProceduralGaitSolver{};
    const auto Initial = TArray<FVector>{FVector::ZeroVector, FVector{50.0, 0.0, 0.0}};
    if (NOT TestTrue(TEXT("Default and guarded solvers reset"), Legacy.Reset(Initial) && Guarded.Reset(Initial)))
    { return false; }
    auto Inputs = MakeInputs();
    auto LegacyOutputs = TArray<ck::FProceduralGaitLegOutput>{};
    auto GuardedOutputs = TArray<ck::FProceduralGaitLegOutput>{};
    LegacyOutputs.SetNum(2);
    GuardedOutputs.SetNum(2);
    auto Denied = TArray<ck::FProceduralGaitDeniedTakeoff>{};
    const auto Allow = [](const ck::FProceduralGaitSolver&, int32, bool) { return true; };
    auto SampledMidflight = false;
    for (auto Frame = 0; Frame < 180; ++Frame)
    {
        const auto Airborne = Frame >= 30 && Frame < 45;
        Inputs[1].Set_Enabled(Frame < 80 || Frame >= 85);
        Inputs[0].Set_IdealTarget(FVector{static_cast<double>(Frame) * 2.0, 0.0, 0.0});
        Inputs[1].Set_IdealTarget(FVector{50.0 + static_cast<double>(Frame) * 2.0, 0.0, 0.0});
        Inputs[0].Set_TargetIsFoothold(true);
        Inputs[1].Set_TargetIsFoothold(true);
        if (NOT Legacy.Step(Dt, 120.0f, FVector{120.0, 0.0, 0.0}, Inputs, LegacyOutputs, Airborne)
            || NOT Guarded.StepWithTakeoffGuard(Dt, 120.0f, FVector{120.0, 0.0, 0.0},
                Inputs, GuardedOutputs, Airborne, {}, Allow, Denied))
        { return false; }
        TestTrue(TEXT("Allow-all guard preserves exact first foot output"),
            LegacyOutputs[0].Get_Position().Equals(GuardedOutputs[0].Get_Position(), 1.0e-6)
            && LegacyOutputs[0].Get_Rotation().Equals(GuardedOutputs[0].Get_Rotation(), 1.0e-6));
        TestTrue(TEXT("Allow-all guard preserves exact second foot output"),
            LegacyOutputs[1].Get_Position().Equals(GuardedOutputs[1].Get_Position(), 1.0e-6)
            && LegacyOutputs[1].Get_Rotation().Equals(GuardedOutputs[1].Get_Rotation(), 1.0e-6));
        TestTrue(TEXT("Allow-all guard reports no denial"), Denied.IsEmpty());
        const auto& Swing = Guarded.GetLegState(0).Get_Swing();
        if (NOT Airborne && Swing.Get_Active() && Swing.Get_Phase() > 0.2f && Swing.Get_Phase() < 0.8f)
        {
            auto Sample = ck::FProceduralGaitLegOutput{};
            if (Guarded.SamplePreparedSwingPose(Guarded.GetLegState(0), Inputs[0],
                Guarded.Get_LastCadenceScale(), Sample))
            {
                const auto Matches = Sample.Get_Position().Equals(GuardedOutputs[0].Get_Position(), 0.01)
                    && Sample.Get_Rotation().Equals(GuardedOutputs[0].Get_Rotation(), 0.001);
                TestTrue(TEXT("Prepared sampler matches this production midflight sample"), Matches);
                SampledMidflight = true;
            }
        }
    }
    TestTrue(TEXT("Prepared sampler matches an actual midflight production output"), SampledMidflight);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitAtomicReplantTest,
    "Ck.ProceduralAnimation.Action.GaitCore.AtomicTrustedReplant",
    ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralGaitAtomicReplantTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_participation;
    auto Solver = ck::FProceduralGaitSolver{};
    const auto Initial = TArray<FVector>{FVector::ZeroVector, FVector{50.0, 0.0, 0.0}};
    if (NOT TestTrue(TEXT("Replant fixture resets"), Solver.Reset(Initial)))
    { return false; }
    auto Inputs = MakeInputs();
    Inputs[0].Set_Participating(false);
    Inputs[1].Set_Participating(false);
    auto Outputs = TArray<ck::FProceduralGaitLegOutput>{};
    Outputs.SetNum(2);
    if (NOT TestTrue(TEXT("Both limbs withdraw"),
        Solver.Step(Dt, 0.0f, FVector::ZeroVector, Inputs, Outputs)))
    { return false; }
    const auto Invalid = TArray<ck::FProceduralGaitTrustedReplant>{
        ck::FProceduralGaitTrustedReplant{0, FVector{10.0, 0.0, 0.0}, FQuat::Identity, FVector::UpVector},
        ck::FProceduralGaitTrustedReplant{0, FVector{60.0, 0.0, 0.0}, FQuat::Identity, FVector::UpVector}};
    TestFalse(TEXT("Duplicate full-set handoff rejects atomically"), Solver.AdmitTrustedReplants(Invalid));
    TestEqual(TEXT("Rejected batch changes no participation"), Solver.NumParticipatingLegs(), 0);
    const auto Valid = TArray<ck::FProceduralGaitTrustedReplant>{
        ck::FProceduralGaitTrustedReplant{0, FVector{10.0, 0.0, 0.0}, FQuat::Identity, FVector::UpVector},
        ck::FProceduralGaitTrustedReplant{1, FVector{60.0, 0.0, 0.0}, FQuat::Identity, FVector::UpVector}};
    TestTrue(TEXT("Whole trusted handoff accepts"), Solver.AdmitTrustedReplants(Valid));
    TestEqual(TEXT("Both limbs participate after the handoff"), Solver.NumParticipatingLegs(), 2);
    TestTrue(TEXT("First actual settled contact becomes trusted plant"),
        Solver.GetLegState(0).Get_Plant().Get_Trusted()
            && Solver.GetLegState(0).Get_Plant().Get_Position().Equals(FVector{10.0, 0.0, 0.0}));
    TestTrue(TEXT("Second actual settled contact becomes trusted plant"),
        Solver.GetLegState(1).Get_Plant().Get_Trusted()
            && Solver.GetLegState(1).Get_Plant().Get_Position().Equals(FVector{60.0, 0.0, 0.0}));
    return true;
}

#endif

// --------------------------------------------------------------------------------------------------------------------
