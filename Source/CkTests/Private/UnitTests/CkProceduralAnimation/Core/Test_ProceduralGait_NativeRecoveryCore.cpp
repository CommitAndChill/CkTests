#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Pure public-core contracts. Resolver answers below are test inputs, not evidence of native world contact.
namespace ck_test_procedural_native_recovery
{
    using FResolver = ck::FProceduralGaitNativeRecoveryResolver;
    using FTouchdown = ck::FProceduralGaitNativeTouchdown;
    using ERecovery = ck::EProceduralGaitAutomaticRecovery;
    constexpr auto Dt = FCk_Time{0.025};

    struct FHarness
    {
        ck::FProceduralGaitSolver Solver;
        TArray<FVector> Initial;
        TArray<ck::FProceduralGaitLegInput> Inputs;
        TArray<ck::FProceduralGaitLegOutput> Outputs;

        auto
            Init(
                int32 InLegs = 1,
                bool InNative = true)
            -> bool
        {
            auto Settings = ck::FProceduralGaitSettings{};
            Settings.Get_Step().Set_Duration(FCk_Time{0.2});
            Settings.Get_Cadence().Set_CycleDuration(FCk_Time{0.8}).Set_MaxSimultaneousSwings(2);
            Settings.Get_Airborne().Set_LandingStepDurationScale(0.5f).Set_TuckLift(20.0f).Set_FollowSpeed(8.0f);
            Solver.Set_Settings(Settings);
            for (auto Index = 0; Index < InLegs; ++Index)
            {
                const auto Point = FVector{0.0, -20.0 + 40.0 * Index, 0.0};
                Initial.Add(Point);
                Inputs.Add(ck::FProceduralGaitLegInput{}
                    .Set_IdealTarget(Point).Set_Hip(Point + FVector{0.0, 0.0, 60.0})
                    .Set_Reach(100.0f).Set_FootContactRadius(2.0f).Set_PhaseOffset(0.5f * Index)
                    .Set_TargetValid(false).Set_TargetTrusted(false).Set_NativeRecoveryEnabled(InNative));
            }
            Outputs.SetNum(InLegs);
            return Solver.Reset(Initial);
        }

        auto
            Tick(
                TOptional<FResolver> InResolver = {},
                bool InAirborne = false,
                FCk_Time InDt = Dt)
            -> bool
        {
            return Solver.Step(InDt, 0.0f, FVector::ZeroVector, Inputs, Outputs, InAirborne, {}, InResolver);
        }

        auto
            EnterWaiting()
            -> bool
        { return Tick({}, true) && Tick(); }

        auto
            SetNativeTargets()
            -> void
        {
            for (auto Index = 0; Index < Inputs.Num(); ++Index)
            {
                Inputs[Index].Set_IdealTarget(Initial[Index] + FVector{10.0, 0.0, 0.0})
                    .Set_TargetValid(true).Set_TargetTrusted(true).Set_TargetIsFoothold(true);
            }
        }

        auto
            FinishLanding(
                TOptional<FResolver> InResolver)
            -> bool
        {
            for (auto Frame = 0; Frame < 16; ++Frame)
            {
                if (NOT Tick(InResolver))
                { return false; }
                auto Landing = false;
                for (auto Index = 0; Index < Inputs.Num(); ++Index)
                { Landing |= Solver.GetLegState(Index).Get_AutomaticRecovery() == ERecovery::Landing; }
                if (NOT Landing)
                { return true; }
            }
            return false;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkNativeRecoveryLegacyAndWaitingTest,
    "Ck.ProceduralAnimation.Gait.NativeRecoveryCore.LegacyAndWaitingCapabilityLatch",
    ck::tests::kCkUnitTestFlags)

auto
    FCkNativeRecoveryLegacyAndWaitingTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_native_recovery;
    auto Legacy = FHarness{};
    if (NOT TestTrue(TEXT("Legacy core fixture initializes without native policy"), Legacy.Init(1, false)))
    { return false; }
    TestTrue(TEXT("Legacy airborne and reground calls remain accepted"), Legacy.EnterWaiting());
    auto LegacyLanded = false;
    for (auto Frame = 0; Frame < 16; ++Frame)
    {
        if (NOT Legacy.Tick())
        { break; }
        LegacyLanded |= Legacy.Outputs[0].Get_Planted() && NOT Legacy.Solver.GetLegState(0).Get_Swing().Get_Active();
    }
    TestTrue(TEXT("Default core keeps its old automatic untrusted rest-return completion"), LegacyLanded
        && NOT Legacy.Solver.GetLegState(0).Get_Plant().Get_Trusted()
        && Legacy.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);

    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Opted-in fixture enters actual airborne then grounded waiting"), H.Init() && H.EnterWaiting()))
    { return false; }
    const auto Start = H.Solver.GetLegState(0).Get_Emitted().Get_Position();
    auto Continuous = true;
    auto Previous = Start;
    for (auto Frame = 0; Frame < 20; ++Frame)
    {
        H.Inputs[0].Set_IdealTarget(H.Initial[0] + FVector{20.0 + Frame, 0.0, 0.0});
        Continuous &= H.Tick() && NOT H.Outputs[0].Get_Planted()
            && NOT H.Solver.GetLegState(0).Get_Plant().Get_Trusted()
            && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active()
            && NOT H.Outputs[0].Get_Position().ContainsNaN()
            && FVector::Dist(H.Outputs[0].Get_Position(), H.Inputs[0].Get_Hip()) <= 100.001
            && FVector::Dist(Previous, H.Outputs[0].Get_Position()) < 20.0;
        Previous = H.Outputs[0].Get_Position();
    }
    TestTrue(TEXT("Waiting follows finite reach-bounded motion beyond the former landing duration"), Continuous
        && FVector::Dist(Start, Previous) > 1.0
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact);

    for (auto Missing = 0; Missing < 2; ++Missing)
    {
        auto L = FHarness{};
        if (NOT TestTrue(TEXT("Capability-loss fixture initializes"), L.Init() && L.EnterWaiting()))
        { return false; }
        L.SetNativeTargets();
        auto Calls = 0;
        const auto Resolve = [&](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
            const FVector&, const FVector& Normal) -> TOptional<FTouchdown>
        { ++Calls; return FTouchdown{Point, Normal, true}; };
        const auto Resolver = TOptional<FResolver>{Resolve};
        TestTrue(TEXT("A trusted target with a resolver starts native Landing"), L.Tick(Resolver)
            && L.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::Landing && Calls == 0);
        if (Missing == 0)
        { L.Inputs[0].Set_NativeRecoveryEnabled(false); }
        TestTrue(TEXT("Capability or resolver loss at completion returns to pending"),
            L.FinishLanding(Missing == 0 ? Resolver : TOptional<FResolver>{})
            && L.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
            && NOT L.Solver.GetLegState(0).Get_Plant().Get_Trusted() && NOT L.Outputs[0].Get_Planted()
            && NOT L.Outputs[0].Get_NativeTouchdownAttempted() && NOT L.Outputs[0].Get_NativeTouchdownResolved()
            && Calls == 0);
        L.Inputs[0].Set_NativeRecoveryEnabled(false);
        TestTrue(TEXT("Missing policy stays latched rather than completing a legacy guessed plant"), L.Tick()
            && L.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
            && NOT L.Outputs[0].Get_Planted());
    }

    auto Mixed = FHarness{};
    if (NOT TestTrue(TEXT("Mixed native-waiting and ordinary fixture initializes"), Mixed.Init(2, false)))
    { return false; }
    auto MixedSettings = Mixed.Solver.Get_Settings();
    MixedSettings.Get_Cadence().Set_MaxSimultaneousSwings(1);
    Mixed.Solver.Set_Settings(MixedSettings);
    Mixed.Inputs[0].Set_NativeRecoveryEnabled(true);
    Mixed.Inputs[1].Set_TargetValid(true).Set_TargetTrusted(true);
    TestTrue(TEXT("Only the opted-in leg waits after the shared airborne episode"), Mixed.EnterWaiting());
    auto OrdinaryReady = false;
    for (auto Frame = 0; Frame < 16; ++Frame)
    {
        if (NOT Mixed.Tick())
        { break; }
        if (Mixed.Outputs[1].Get_Planted() && Mixed.Solver.GetLegState(1).Get_Plant().Get_Trusted()
            && NOT Mixed.Solver.GetLegState(1).Get_Swing().Get_Active())
        {
            OrdinaryReady = true;
            break;
        }
    }
    if (NOT TestTrue(TEXT("The legacy peer has a trusted ordinary plant before the scheduling check"), OrdinaryReady))
    { return false; }
    // A valid but untrusted rest input cannot release the waiting leg. Its error exceeds the existing emergency
    // threshold, so allowing Waiting into ordinary priority would starve the peer despite there being no actual swing.
    Mixed.Inputs[0].Set_NativeRecoveryEnabled(false).Set_TargetValid(true).Set_TargetTrusted(false)
        .Set_Hip(Mixed.Initial[0] + FVector{0.0, 0.0, 40.0})
        .Set_IdealTarget(Mixed.Initial[0] + FVector{60.0, 0.0, 0.0});
    Mixed.Inputs[1].Set_IdealTarget(Mixed.Initial[1] + FVector{40.0, 0.0, 0.0}).Set_TargetIsFoothold(true);
    auto PeerStarted = false;
    auto WaitingUnplanted = true;
    auto InItsWindow = false;
    for (auto Frame = 0; Frame < 40; ++Frame)
    {
        if (NOT Mixed.Solver.Step(Dt, 0.0f, FVector::ZeroVector, Mixed.Inputs, Mixed.Outputs, false, TOptional<float>{100.0f}))
        { break; }
        WaitingUnplanted &= Mixed.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
            && NOT Mixed.Solver.GetLegState(0).Get_Swing().Get_Active() && NOT Mixed.Outputs[0].Get_Planted();
        if (Mixed.Solver.GetLegState(1).Get_Swing().Get_Active())
        {
            PeerStarted = NOT Mixed.Solver.GetLegState(1).Get_Swing().Get_CatchStep()
                && Mixed.Solver.GetLegState(1).Get_AutomaticRecovery() == ERecovery::None;
            InItsWindow = Mixed.Solver.IsWindowOpen(Mixed.Inputs[1].Get_PhaseOffset());
            break;
        }
    }
    TestTrue(TEXT("Waiting claims neither ordinary phase priority nor the sole swing slot"),
        WaitingUnplanted && PeerStarted && InItsWindow);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkNativeRecoveryAtomicLandingTest,
    "Ck.ProceduralAnimation.Gait.NativeRecoveryCore.AtomicLandingAndCommittedTarget",
    ck::tests::kCkUnitTestFlags)

auto
    FCkNativeRecoveryAtomicLandingTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_native_recovery;
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Atomic landing fixture enters waiting"), H.Init() && H.EnterWaiting()))
    { return false; }
    H.SetNativeTargets();
    auto Calls = 0;
    auto Accept = false;
    const auto NativeNormal = FVector{0.0, 0.2, 1.0}.GetSafeNormal();
    const auto Resolve = [&](const ck::FProceduralGaitSolver& Solver, int32 Index, const FVector& Point,
        const FVector&, const FVector&) -> TOptional<FTouchdown>
    {
        ++Calls;
        TestTrue(TEXT("Resolver runs before committing this recovery plant"),
            Solver.GetLegState(Index).Get_AutomaticRecovery() == ERecovery::Landing
            && Solver.GetLegState(Index).Get_Swing().Get_Active()
            && NOT Solver.GetLegState(Index).Get_Plant().Get_Trusted());
        return Accept ? TOptional<FTouchdown>{FTouchdown{Point, NativeNormal, true}} : TOptional<FTouchdown>{};
    };
    const auto Resolver = TOptional<FResolver>{Resolve};
    const auto Origin = H.Solver.GetLegState(0).Get_Emitted().Get_Position();
    TestTrue(TEXT("Fresh native target begins at the actual emitted pose with the existing landing duration"), H.Tick(Resolver)
        && H.Solver.GetLegState(0).Get_Swing().Get_StartPosition().Equals(Origin, 1.0e-6)
        && FMath::IsNearlyEqual(H.Solver.GetLegState(0).Get_Swing().Get_DurationScale(), 0.5f)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::Landing && Calls == 0);
    const auto Committed = H.Solver.GetLegState(0).Get_Swing().Get_Target();
    H.Inputs[0].Set_IdealTarget(H.Initial[0] - FVector{40.0, 0.0, 0.0}).Set_TargetValid(true).Set_TargetTrusted(false);
    TestTrue(TEXT("An untrusted valid rest target cannot retarget a committed native recovery"), H.Tick(Resolver)
        && H.Solver.GetLegState(0).Get_Swing().Get_Target().Equals(Committed, 1.0e-6) && Calls == 0);
    TestTrue(TEXT("Rejected native completion does not create a plant or retained target reservation"), H.FinishLanding(Resolver)
        && Calls == 1 && NOT H.Outputs[0].Get_Planted() && H.Outputs[0].Get_NativeTouchdownAttempted()
        && NOT H.Outputs[0].Get_NativeTouchdownResolved()
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active()
        && NOT H.Solver.GetLegState(0).Get_Swing().Get_TargetTrusted()
        && NOT H.Solver.GetLegState(0).Get_Plant().Get_Trusted());

    H.SetNativeTargets();
    Accept = true;
    TestTrue(TEXT("A later admitted opportunity starts another actual swing"), H.Tick(Resolver)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::Landing);
    TestTrue(TEXT("Accepted synchronous touchdown atomically commits its native normal and rotation"), H.FinishLanding(Resolver)
        && Calls == 2 && H.Outputs[0].Get_Planted() && H.Outputs[0].Get_NativeTouchdownAttempted()
        && H.Outputs[0].Get_NativeTouchdownResolved()
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None
        && H.Solver.GetLegState(0).Get_Plant().Get_Trusted()
        && H.Outputs[0].Get_Normal().Equals(NativeNormal, 1.0e-6)
        && H.Outputs[0].Get_Rotation().GetAxisZ().Equals(NativeNormal, 1.0e-6));
    TestTrue(TEXT("Touchdown markers and callback authority apply only to the completing solve"), H.Tick(Resolver)
        && Calls == 2 && NOT H.Outputs[0].Get_NativeTouchdownAttempted() && NOT H.Outputs[0].Get_NativeTouchdownResolved());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkNativeRecoveryFailureContractsTest,
    "Ck.ProceduralAnimation.Gait.NativeRecoveryCore.InvalidReadOnlyAndMalformedResolver",
    ck::tests::kCkUnitTestFlags)

auto
    FCkNativeRecoveryFailureContractsTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_native_recovery;
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Failure-path fixture enters waiting"), H.Init() && H.EnterWaiting()))
    { return false; }
    H.SetNativeTargets();
    auto Calls = 0;
    const auto Resolve = [&](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
        const FVector&, const FVector& Normal) -> TOptional<FTouchdown>
    { ++Calls; return FTouchdown{Point, Normal, true}; };
    const auto Resolver = TOptional<FResolver>{Resolve};
    const auto Emitted = H.Solver.GetLegState(0).Get_Emitted().Get_Position();
    const auto Clock = H.Solver.GetGaitClock();
    const auto SentinelRotation = FQuat{FVector::ForwardVector, FMath::DegreesToRadians(30.0)};
    H.Outputs[0].Set_Position(FVector{123.0, 456.0, 789.0}).Set_Normal(FVector::RightVector)
        .Set_Rotation(SentinelRotation).Set_SwingAlpha(0.37f).Set_Planted(true)
        .Set_NativeTouchdownAttempted(true).Set_NativeTouchdownResolved(true);
    H.Inputs[0].Set_FootContactRadius(0.0f);
    TestFalse(TEXT("Opted-in native input with zero radius is rejected"), H.Tick(Resolver));
    TestTrue(TEXT("Rejected input invokes no resolver and preserves solver and caller output"), Calls == 0
        && H.Solver.GetLegState(0).Get_Emitted().Get_Position().Equals(Emitted, 0.0)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && H.Solver.GetGaitClock() == Clock && H.Outputs[0].Get_Position().Equals(FVector{123.0, 456.0, 789.0}, 0.0)
        && H.Outputs[0].Get_Normal().Equals(FVector::RightVector, 0.0)
        && H.Outputs[0].Get_Rotation().Equals(SentinelRotation, 0.0)
        && H.Outputs[0].Get_SwingAlpha() == 0.37f && H.Outputs[0].Get_Planted()
        && H.Outputs[0].Get_NativeTouchdownAttempted() && H.Outputs[0].Get_NativeTouchdownResolved());
    H.Inputs[0].Set_FootContactRadius(2.0f);
    TestTrue(TEXT("Valid zero-delta evaluation returns evaluated output without committing or resolving"), H.Tick(Resolver, false, FCk_Time{})
        && Calls == 0 && H.Solver.GetLegState(0).Get_Emitted().Get_Position().Equals(Emitted, 0.0)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && H.Solver.GetGaitClock() == Clock && NOT H.Outputs[0].Get_NativeTouchdownAttempted()
        && NOT H.Outputs[0].Get_NativeTouchdownResolved());
    const auto OldHip = H.Inputs[0].Get_Hip();
    const auto BeforePlant = H.Solver.GetLegState(0).Get_Plant().Get_Position();
    H.Inputs[0].Set_Hip(Emitted + FVector{200.0, 0.0, 0.0});
    TestTrue(TEXT("Zero-time waiting observation projects against a moved hip without committing that projection"),
        H.Tick(Resolver, false, FCk_Time{}) && Calls == 0 && NOT H.Outputs[0].Get_Planted()
        && NOT H.Outputs[0].Get_NativeTouchdownAttempted() && NOT H.Outputs[0].Get_NativeTouchdownResolved()
        && NOT H.Outputs[0].Get_Position().ContainsNaN()
        && FVector::Dist(H.Outputs[0].Get_Position(), H.Inputs[0].Get_Hip()) <= H.Inputs[0].Get_Reach() + 1.0e-3
        && FVector::Dist(H.Outputs[0].Get_Position(), Emitted) > 1.0
        && H.Solver.GetLegState(0).Get_Emitted().Get_Position().Equals(Emitted, 0.0)
        && H.Solver.GetLegState(0).Get_Plant().Get_Position().Equals(BeforePlant, 0.0)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && H.Solver.GetGaitClock() == Clock);
    H.Inputs[0].Set_Hip(OldHip);
    TestTrue(TEXT("The callback fixture reaches the last unfinished landing phase"),
        H.Tick(Resolver) && H.Tick(Resolver) && H.Tick(Resolver)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::Landing && Calls == 0);
    const auto BeforePhase = H.Solver.GetLegState(0).Get_Swing().Get_Phase();
    const auto BeforeEmission = H.Solver.GetLegState(0).Get_Emitted().Get_Position();
    TestTrue(TEXT("Zero time cannot call or commit a nearly completing native landing"), H.Tick(Resolver, false, FCk_Time{})
        && Calls == 0 && H.Solver.GetLegState(0).Get_Swing().Get_Phase() == BeforePhase
        && H.Solver.GetLegState(0).Get_Emitted().Get_Position().Equals(BeforeEmission, 0.0)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::Landing
        && NOT H.Outputs[0].Get_NativeTouchdownAttempted() && NOT H.Outputs[0].Get_NativeTouchdownResolved());
    TestTrue(TEXT("The next positive solve performs exactly one native completion"), H.Tick(Resolver)
        && Calls == 1 && H.Outputs[0].Get_NativeTouchdownAttempted() && H.Outputs[0].Get_NativeTouchdownResolved());

    for (auto Form = 0; Form < 5; ++Form)
    {
        auto Bad = FHarness{};
        if (NOT TestTrue(TEXT("Malformed-result fixture enters waiting"), Bad.Init() && Bad.EnterWaiting()))
        { return false; }
        Bad.SetNativeTargets();
        if (Form == 4)
        {
            Bad.Inputs[0].Set_PosedHip(TOptional<FVector>{Bad.Inputs[0].Get_Hip() + FVector{200.0, 0.0, 0.0}});
            TestTrue(TEXT("Posed-only rejection has a physically reachable simulation target"),
                FVector::Dist(Bad.Inputs[0].Get_IdealTarget(), Bad.Inputs[0].Get_Hip()) < Bad.Inputs[0].Get_Reach()
                && FVector::Dist(Bad.Inputs[0].Get_IdealTarget(), Bad.Inputs[0].Get_PosedHip().GetValue()) > Bad.Inputs[0].Get_Reach());
        }
        auto Attempts = 0;
        const auto Reject = [&](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
            const FVector&, const FVector&) -> TOptional<FTouchdown>
        {
            ++Attempts;
            if (Form == 0)
            { return FTouchdown{}; }
            if (Form == 1)
            { return FTouchdown{Point, FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0}, true}; }
            if (Form == 2)
            { return FTouchdown{Bad.Inputs[0].Get_Hip() + FVector{200.0, 0.0, 0.0}, FVector::UpVector, true}; }
            if (Form == 3)
            { return FTouchdown{Point, FVector{0.0, 0.0, 2.0}, true}; }
            return FTouchdown{Point, FVector::UpVector, true};
        };
        const auto RejectedResolver = TOptional<FResolver>{Reject};
        TestTrue(TEXT("Trusted acquisition can start before the later malformed contact result"), Bad.Tick(RejectedResolver));
        TestTrue(TEXT("Untrusted, malformed-normal, simulation-reach and posed-only reach failures cannot partially commit"),
            Bad.FinishLanding(RejectedResolver) && Attempts == 1 && NOT Bad.Outputs[0].Get_Planted()
            && Bad.Outputs[0].Get_NativeTouchdownAttempted() && NOT Bad.Outputs[0].Get_NativeTouchdownResolved()
            && NOT Bad.Solver.GetLegState(0).Get_Plant().Get_Trusted()
            && NOT Bad.Solver.GetLegState(0).Get_Swing().Get_Active()
            && Bad.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkNativeRecoveryEvolvingReservationsTest,
    "Ck.ProceduralAnimation.Gait.NativeRecoveryCore.EvolvingAcceptedPlantReservations",
    ck::tests::kCkUnitTestFlags)

auto
    FCkNativeRecoveryEvolvingReservationsTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_native_recovery;
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Two-leg public-core fixture enters waiting"), H.Init(2) && H.EnterWaiting()))
    { return false; }
    H.SetNativeTargets();
    auto Calls = 0;
    auto SawEarlierPlant = false;
    const auto Resolve = [&](const ck::FProceduralGaitSolver& Solver, int32 Index, const FVector& Point,
        const FVector&, const FVector& Normal) -> TOptional<FTouchdown>
    {
        ++Calls;
        if (Index == 0)
        { return FTouchdown{Point, Normal, true}; }
        const auto& First = Solver.GetLegState(0);
        SawEarlierPlant = First.Get_Plant().Get_Trusted() && NOT First.Get_Swing().Get_Active()
            && First.Get_AutomaticRecovery() == ERecovery::None;
        return FTouchdown{First.Get_Plant().Get_Position(), Normal, true};
    };
    const auto Resolver = TOptional<FResolver>{Resolve};
    TestTrue(TEXT("Automatic landing admits both prospective targets before normal swing accounting"), H.Tick(Resolver)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::Landing
        && H.Solver.GetLegState(1).Get_AutomaticRecovery() == ERecovery::Landing);
    TestTrue(TEXT("Second callback sees the first accepted plant and cannot occupy its reserved sphere"), H.FinishLanding(Resolver)
        && Calls == 2 && SawEarlierPlant
        && H.Outputs[0].Get_Planted() && H.Outputs[0].Get_NativeTouchdownResolved()
        && NOT H.Outputs[1].Get_Planted() && H.Outputs[1].Get_NativeTouchdownAttempted()
        && NOT H.Outputs[1].Get_NativeTouchdownResolved()
        && H.Solver.GetLegState(1).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && NOT H.Solver.GetLegState(1).Get_Plant().Get_Trusted());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkNativeRecoveryLifecycleTest,
    "Ck.ProceduralAnimation.Gait.NativeRecoveryCore.LifecycleAndFlightOwnershipExceptions",
    ck::tests::kCkUnitTestFlags)

auto
    FCkNativeRecoveryLifecycleTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_native_recovery;
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Lifecycle fixture enters waiting"), H.Init() && H.EnterWaiting()))
    { return false; }
    const auto Emitted = H.Solver.GetLegState(0).Get_Emitted().Get_Position();
    auto Copy = H.Solver;
    const auto Turn = FQuat{FVector::UpVector, FMath::DegreesToRadians(90.0)};
    Copy.TransformState(Turn);
    TestTrue(TEXT("Copy and support-frame rotation retain recovery and rotate its actual emitted origin"),
        Copy.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && Copy.GetLegState(0).Get_Emitted().Get_Position().Equals(Turn.RotateVector(Emitted), 1.0e-6)
        && H.Solver.GetLegState(0).Get_Emitted().Get_Position().Equals(Emitted, 0.0));
    H.Solver.SetPlantedPose(0, FVector{900.0, 900.0, 900.0}, FVector::UpVector, true);
    TestTrue(TEXT("An inactive Waiting swing is not an authority to finish through SetPlantedPose"),
        H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && NOT H.Solver.GetLegState(0).Get_Plant().Get_Trusted()
        && H.Solver.GetLegState(0).Get_Emitted().Get_Position().Equals(Emitted, 0.0));
    const auto InvalidInitial = TArray<FVector>{FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}};
    TestFalse(TEXT("Malformed Reset rejects before clearing pending state"), H.Solver.Reset(InvalidInitial));
    TestTrue(TEXT("Failed Reset retains recovery"), H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact);
    TestTrue(TEXT("Successful Reset clears recovery through default leg construction"), H.Solver.Reset(H.Initial)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);

    auto Disabled = FHarness{};
    if (NOT TestTrue(TEXT("Disable fixture enters waiting"), Disabled.Init() && Disabled.EnterWaiting()))
    { return false; }
    Disabled.Inputs[0].Set_Enabled(false);
    TestTrue(TEXT("Disable cancels automatic recovery"), Disabled.Tick()
        && Disabled.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);
    const auto Frozen = Disabled.Solver.GetLegState(0).Get_Emitted().Get_Position() + FVector{1.0, 0.0, 0.0};
    TestTrue(TEXT("Frozen disabled-pose synchronization retains its existing authority"),
        Disabled.Solver.SetDisabledPose(0, Frozen, FQuat::Identity, FVector::UpVector)
        && Disabled.Solver.GetLegState(0).Get_Emitted().Get_Position().Equals(Frozen, 0.0));
    Disabled.Inputs[0].Set_Enabled(true);
    TestTrue(TEXT("Re-enable during flight stays outside automatic native recovery"), Disabled.Tick({}, true)
        && Disabled.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);
    TestTrue(TEXT("Its first grounded return keeps the legacy re-enable exception"), Disabled.Tick()
        && Disabled.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);
    TestTrue(TEXT("A later actual flight may opt in again"), Disabled.Tick({}, true)
        && Disabled.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact);

    auto Action = FHarness{};
    if (NOT TestTrue(TEXT("Action-owner fixture enters waiting"), Action.Init() && Action.EnterWaiting()))
    { return false; }
    Action.Inputs[0].Set_Participating(false);
    TestTrue(TEXT("Withdrawal cancels automatic state without publishing a supporting plant"), Action.Tick({}, true)
        && Action.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None
        && NOT Action.Outputs[0].Get_Planted() && NOT Action.Solver.GetLegState(0).Get_Plant().Get_Trusted());
    const auto Replants = TArray<ck::FProceduralGaitTrustedReplant>{{0, Action.Initial[0], FQuat::Identity, FVector::UpVector}};
    TestTrue(TEXT("Explicit all-leg action handoff keeps its separate trusted authority"), Action.Solver.AdmitTrustedReplants(Replants));
    Action.Inputs[0].Set_Participating(true);
    TestTrue(TEXT("Action return during flight does not start automatic recovery behind its owner"), Action.Tick({}, true)
        && Action.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);
    TestTrue(TEXT("Action return's next grounded positive step stays legacy"), Action.Tick()
        && Action.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);
    TestTrue(TEXT("A later distinct flight can recover natively after the handoff"), Action.Tick({}, true)
        && Action.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkNativeRecoveryCatchOwnershipTest,
    "Ck.ProceduralAnimation.Gait.NativeRecoveryCore.CatchStartsFromWaitingEmission",
    ck::tests::kCkUnitTestFlags)

auto
    FCkNativeRecoveryCatchOwnershipTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_native_recovery;
    auto H = FHarness{};
    if (NOT TestTrue(TEXT("Catch fixture enters waiting"), H.Init() && H.EnterWaiting()))
    { return false; }
    TestTrue(TEXT("Explicit catch queues while waiting"), H.Solver.RequestStep(0, H.Initial[0] + FVector{15.0, 0.0, 0.0}));
    auto Denied = TArray<ck::FProceduralGaitDeniedTakeoff>{};
    const auto Deny = [](const ck::FProceduralGaitSolver&, int32, bool) -> bool { return false; };
    TestTrue(TEXT("Denied catch does not clear recovery or create a plant"), H.Solver.StepWithTakeoffGuard(Dt,
        0.0f, FVector::ZeroVector, H.Inputs, H.Outputs, false, {}, Deny, Denied)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::WaitingForContact
        && H.Solver.HasPendingStep(0) && NOT H.Outputs[0].Get_Planted() && Denied.Num() == 1);
    const auto Origin = H.Solver.GetLegState(0).Get_Emitted().Get_Position();
    const auto Rotation = H.Solver.GetLegState(0).Get_Emitted().Get_Rotation();
    const auto Allow = [](const ck::FProceduralGaitSolver&, int32, bool) -> bool { return true; };
    TestTrue(TEXT("Only a successful catch starts from the last actual emitted pose and clears recovery"),
        H.Solver.StepWithTakeoffGuard(Dt, 0.0f, FVector::ZeroVector, H.Inputs, H.Outputs, false, {}, Allow, Denied)
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None
        && H.Solver.GetLegState(0).Get_Swing().Get_CatchStep()
        && H.Solver.GetLegState(0).Get_Swing().Get_StartPosition().Equals(Origin, 1.0e-6)
        && H.Solver.GetLegState(0).Get_Swing().Get_StartRotation().Equals(Rotation, 1.0e-6)
        && NOT H.Solver.HasPendingStep(0) && NOT H.Outputs[0].Get_Planted());
    auto Calls = 0;
    const auto Resolve = [&](const ck::FProceduralGaitSolver&, int32, const FVector& Point,
        const FVector&, const FVector& Normal) -> TOptional<FTouchdown>
    { ++Calls; return FTouchdown{Point, Normal, true}; };
    auto Landed = false;
    for (auto Frame = 0; Frame < 20; ++Frame)
    {
        if (NOT H.Tick(TOptional<FResolver>{Resolve}))
        { break; }
        Landed |= H.Outputs[0].Get_Planted() && NOT H.Solver.GetLegState(0).Get_Swing().Get_Active();
    }
    TestTrue(TEXT("Existing catch completion is exempt from the automatic native resolver"), Landed && Calls == 0
        && H.Solver.GetLegState(0).Get_AutomaticRecovery() == ERecovery::None);
    return true;
}

#endif
