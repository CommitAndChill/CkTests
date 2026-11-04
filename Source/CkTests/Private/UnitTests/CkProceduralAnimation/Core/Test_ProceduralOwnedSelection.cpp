#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_procedural_owned_selection
{
    using FPurpose = ck::EProceduralGaitOwnedPurpose;
    using FKind = ck::EProceduralGaitBeginKind;
    using FOwnedBeginEvent = ck::FProceduralGaitBeginEvent;
    using FRejection = ck::EProceduralGaitOwnerRejection;
    using FResolver = ck::FProceduralGaitNativeRecoveryResolver;
    using FTouchdown = ck::FProceduralGaitNativeTouchdown;

    struct FHarness
    {
        ck::FProceduralGaitSolver Solver;
        TArray<ck::FProceduralGaitLegInput> Inputs;
        TArray<ck::FProceduralGaitLegOutput> Outputs;
        TArray<ck::FProceduralGaitDeniedTakeoff> Denied;

        auto Init(float InOffset = 0.0f, bool InSettle = false) -> bool
        {
            auto Settings = ck::FProceduralGaitSettings{};
            Settings.Get_Cadence().Set_CycleDuration(FCk_Time{1.0}).Set_SwingWindow(0.25f)
                .Set_MaxSimultaneousSwings(1);
            Settings.Get_Step().Set_Threshold(25.0f).Set_Duration(FCk_Time{0.1});
            Settings.Get_Settle().Set_AtRest(InSettle).Set_Delay(FCk_Time{}).Set_ThresholdFraction(0.1f);
            Solver.Set_Settings(Settings);
            const auto Initial = TArray<FVector>{FVector::ZeroVector, FVector{0.0, 80.0, 0.0}};
            for (auto Index = 0; Index < Initial.Num(); ++Index)
            {
                Inputs.Add(ck::FProceduralGaitLegInput{}.Set_IdealTarget(Initial[Index])
                    .Set_Hip(Initial[Index] + FVector{0.0, 0.0, 50.0}).Set_Reach(100.0f)
                    .Set_FootContactRadius(2.0f).Set_NativeRecoveryEnabled(true)
                    .Set_PhaseOffset(Index == 0 ? InOffset : 0.5f));
            }
            Outputs.SetNum(Initial.Num());
            return Solver.Reset(Initial);
        }

        auto Selection(int32 InIndex, const FVector& InTarget, FPurpose InPurpose, uint8 InSlot = 0) const
            -> ck::FProceduralGaitOwnedSelection
        {
            auto Out = ck::FProceduralGaitOwnedSelection{};
            Out.LegIndex = InIndex;
            Out.Input = Inputs[InIndex];
            Out.Input.Set_IdealTarget(InTarget).Set_TargetValid(true).Set_TargetTrusted(true).Set_TargetIsFoothold(true);
            Out.ExpectedPlant = Solver.GetLegState(InIndex).Get_Plant().Get_Position();
            Out.Purpose = InPurpose;
            Out.Token = {1, 1, 1, InSlot};
            if (InPurpose == FPurpose::LegacyFinal)
            {
                Out.Input.Set_PlannedReplantIntent(ck::FProceduralGaitPlannedReplantIntent{}.Set_Episode(1)
                    .Set_Frame(1).Set_ConsumerInputSolve(1).Set_ExpectedPlant(Out.ExpectedPlant)
                    .Set_Target(InTarget).Set_ExitHip(FVector{90.0, 0.0, 60.0}));
            }
            return Out;
        }

        auto Tick(const ck::FProceduralGaitOwnerContext& InOwner,
            TFunctionRef<bool(const ck::FProceduralGaitSolver&, int32, bool)> InGuard,
            TOptional<FResolver> InResolver = {}, FCk_Time InDt = FCk_Time{0.01}, bool InAirborne = false) -> bool
        {
            return Solver.StepWithTakeoffGuardAndOwner(InDt, 0.0f, FVector::ZeroVector, Inputs, Outputs,
                InAirborne, {}, InGuard, Denied, InOwner, InResolver);
        }
    };

    const auto Allow = [](const ck::FProceduralGaitSolver&, int32, bool) { return true; };
    const auto Resolve = [](const ck::FProceduralGaitSolver&, int32, const FVector& InPoint,
        const FVector&, const FVector& InNormal) -> TOptional<FTouchdown>
    { return FTouchdown{InPoint, InNormal, true}; };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOwnedSelectionMalformedTest,
    "Ck.ProceduralAnimation.Gait.OwnedSelection.MalformedAndReadOnly", ck::tests::kCkUnitTestFlags)

auto
    FCkOwnedSelectionMalformedTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_owned_selection;
    for (auto Case = 0; Case < 12; ++Case)
    {
        auto H = FHarness{};
        if (NOT TestTrue(TEXT("Public fixture resets"), H.Init()))
        { return false; }
        TestTrue(TEXT("Seed an actual clock advance"), H.Solver.Step(FCk_Time{0.01}, 10.0f, FVector::ZeroVector, H.Inputs, H.Outputs));
        const auto Clock = H.Solver.GetGaitClock();
        const auto Advance = H.Solver.GetLastClockAdvance();
        auto CallbackCount = 0;
        const auto Current = [&]() { ++CallbackCount; return true; };
        const auto Reject = [&](FRejection) { ++CallbackCount; };
        const auto Begin = [&](const FOwnedBeginEvent&) { ++CallbackCount; return true; };
        const auto Guard = [&](const ck::FProceduralGaitSolver&, int32, bool) { ++CallbackCount; return true; };
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        auto Selection = H.Selection(0, FVector{70.0, 0.0, 0.0}, FPurpose::AuxiliaryEmergency);
        const auto Nan = std::numeric_limits<double>::quiet_NaN();
        switch (Case)
        {
            case 0: Selection.LegIndex = -1; break;
            case 1: Selection.Input.Set_IdealTarget(FVector{Nan, 0.0, 0.0}); break;
            case 2: Selection.ExpectedPlant = FVector{Nan, 0.0, 0.0}; break;
            case 3: Selection.Input.Set_Hip(FVector{1.0, 0.0, 60.0}); break;
            case 4: Selection.Input.Set_Reach(99.0f); break;
            case 5: Selection.Input.Set_PhaseOffset(0.5f); break;
            case 6: Selection.Input.Set_Participating(false); break;
            case 7: Selection.Token.Episode = 0; break;
            case 8: Selection.Token.Frame = 0; break;
            case 9: Selection.Token.Slot = 4; break;
            case 10: Selection.Input.Set_NativeRecoveryEnabled(false); break;
            case 11: Selection.Purpose = static_cast<FPurpose>(255); break;
        }
        Owner.Selection = Selection;
        H.Outputs[0].Set_Position(FVector{123.0, 456.0, 789.0});
        TestFalse(TEXT("Malformed ownership rejects the entire Step"), H.Tick(Owner, Guard, FResolver{Resolve}));
        TestEqual(TEXT("No callback is reached"), CallbackCount, 0);
        TestEqual(TEXT("Clock is unchanged"), H.Solver.GetGaitClock(), Clock);
        TestEqual(TEXT("Last advance is unchanged"), H.Solver.GetLastClockAdvance(), Advance);
        TestTrue(TEXT("Caller output is unchanged"), H.Outputs[0].Get_Position() == FVector{123.0, 456.0, 789.0});
        TestEqual(TEXT("No partial swing starts"), H.Solver.GetLegState(0).Get_StartsThisStep(), uint8{0});
    }

    auto H = FHarness{};
    TestTrue(TEXT("Read-only fixture resets"), H.Init());
    auto Callbacks = 0;
    const auto Current = [&]() { ++Callbacks; return true; };
    const auto Reject = [&](FRejection) { ++Callbacks; };
    const auto Begin = [&](const FOwnedBeginEvent&) { ++Callbacks; return true; };
    auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
    Owner.Selection = H.Selection(0, FVector{70.0, 0.0, 0.0}, FPurpose::AuxiliaryEmergency);
    for (const auto Dt : {FCk_Time{}, FCk_Time{-0.01}})
    {
        TestTrue(TEXT("Nonpositive Step evaluates without owner authority"), H.Tick(Owner, Allow, FResolver{Resolve}, Dt));
        TestEqual(TEXT("No owner callback on nonpositive time"), Callbacks, 0);
        TestFalse(TEXT("Read-only evaluation cannot start a swing"), H.Solver.GetLegState(0).Get_Swing().Get_Active());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOwnedSelectionWindowsTest,
    "Ck.ProceduralAnimation.Gait.OwnedSelection.ActualPreparationWindows", ck::tests::kCkUnitTestFlags)

auto
    FCkOwnedSelectionWindowsTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_owned_selection;
    for (auto Slot = uint8{1}; Slot < 4; ++Slot)
    {
        auto H = FHarness{};
        TestTrue(TEXT("Closed-window settle fixture resets"), H.Init(0.5f, true));
        auto Starts = 0;
        auto Rejects = 0;
        auto Guards = 0;
        const auto Current = []() { return true; };
        const auto Reject = [&](FRejection) { ++Rejects; };
        const auto Begin = [&](const FOwnedBeginEvent&) { ++Starts; return true; };
        const auto Guard = [&](const ck::FProceduralGaitSolver&, int32, bool) { ++Guards; return true; };
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        Owner.Selection = H.Selection(0, FVector{10.0, 0.0, 0.0}, Slot == 3 ? FPurpose::LegacyFinal : FPurpose::Preparation, Slot);
        TestTrue(TEXT("Closed-window owned Step is accepted"), H.Tick(Owner, Guard, FResolver{Resolve}));
        TestFalse(TEXT("The actual window is closed"), H.Solver.IsWindowOpen(0.5f));
        TestEqual(TEXT("Settle cannot charge a preparation/final slot"), Starts, 0);
        TestEqual(TEXT("Closed-window wait does not exhaust ownership"), Rejects, 0);
        TestEqual(TEXT("A settle-only owned decision never calls the guard"), Guards, 0);
        TestFalse(TEXT("Owned M/F/Q remains planted"), H.Solver.GetLegState(0).Get_Swing().Get_Active());
    }
    auto H = FHarness{};
    TestTrue(TEXT("Open-window fixture resets"), H.Init());
    auto Starts = 0;
    auto EventValid = false;
    const auto Current = []() { return true; };
    const auto Reject = [](FRejection) {};
    const auto Begin = [&](const FOwnedBeginEvent& Event)
    {
        ++Starts;
        EventValid = Event.Kind == FKind::Normal && Event.Owned && Event.WindowOpen && Event.Budget && Event.OnSchedule
            && NOT Event.Emergency && NOT Event.CatchStep && NOT Event.RecoveryWants && NOT Event.ReachPaceRelief;
        return true;
    };
    auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
    Owner.Selection = H.Selection(0, FVector{10.0, 0.0, 0.0}, FPurpose::Preparation, 1);
    TestTrue(TEXT("Open-window preparation Step accepted"), H.Tick(Owner, Allow, FResolver{Resolve}));
    TestEqual(TEXT("Exactly one actual owned begin"), Starts, 1);
    TestTrue(TEXT("Begin receipt contains actual schedule facts"), EventValid);
    TestTrue(TEXT("Preparation commits native contact"), H.Solver.GetLegState(0).Get_Swing().Get_CommittedNativeContact());
    TestFalse(TEXT("Preparation does not fake a legacy replant"), H.Solver.GetLegState(0).Get_Swing().Get_PlannedReplant());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOwnedSelectionRevocationTest,
    "Ck.ProceduralAnimation.Gait.OwnedSelection.RevocationAndFrozenVisits", ck::tests::kCkUnitTestFlags)

auto
    FCkOwnedSelectionRevocationTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_owned_selection;
    {
        auto H = FHarness{};
        TestTrue(TEXT("Stale fixture resets"), H.Init());
        auto Rejects = 0;
        auto Guards = 0;
        auto Begins = 0;
        const auto Current = []() { return false; };
        const auto Reject = [&](FRejection) { ++Rejects; };
        const auto Begin = [&](const FOwnedBeginEvent&) { ++Begins; return true; };
        const auto Guard = [&](const ck::FProceduralGaitSolver&, int32, bool) { ++Guards; return true; };
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        Owner.Selection = H.Selection(0, FVector{70.0, 0.0, 0.0}, FPurpose::AuxiliaryEmergency);
        TestTrue(TEXT("Stale selection falls back to independent ordinary input"), H.Tick(Owner, Guard, FResolver{Resolve}));
        TestEqual(TEXT("Stale lease rejected once"), Rejects, 1);
        TestEqual(TEXT("Derived owned Emergency cannot call the guard"), Guards, 0);
        TestEqual(TEXT("No begin event"), Begins, 0);
    }
    for (const auto PressureFirst : {false, true})
    {
        auto H = FHarness{};
        TestTrue(TEXT("Guard revocation fixture resets"), H.Init());
        const auto OwnedIndex = PressureFirst ? 1 : 0;
        const auto Other = 1 - OwnedIndex;
        H.Inputs[Other].Set_IdealTarget(FVector{60.0, Other * 80.0, 0.0});
        if (PressureFirst)
        {
            H.Inputs[OwnedIndex].Set_ReachPaceTrial(ck::FProceduralGaitReachPaceTrial{}
                .Set_Hip(FVector{120.0, 80.0, 0.0}));
        }
        auto Live = true;
        auto Rejects = 0;
        auto OwnedBegins = 0;
        auto Guards = TArray<int32>{0, 0};
        const auto Current = [&]() { return Live; };
        const auto Reject = [&](FRejection) { ++Rejects; Live = false; };
        const auto Begin = [&](const FOwnedBeginEvent& Event) { OwnedBegins += Event.Owned ? 1 : 0; return true; };
        const auto Guard = [&](const ck::FProceduralGaitSolver&, int32 Index, bool)
        {
            ++Guards[Index];
            if (Index == OwnedIndex)
            { Live = false; }
            return true;
        };
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        Owner.Selection = H.Selection(OwnedIndex, FVector{70.0, OwnedIndex * 80.0, 0.0}, FPurpose::AuxiliaryEmergency);
        TestTrue(TEXT("Late revocation Step accepted"), H.Tick(Owner, Guard, FResolver{Resolve}));
        TestEqual(TEXT("Exactly one reject"), Rejects, 1);
        TestEqual(TEXT("Owned guard is not replayed"), Guards[OwnedIndex], 1);
        TestEqual(TEXT("Unvisited ordinary guard runs once after priority restoration"), Guards[Other], 1);
        TestEqual(TEXT("Revoked derived Emergency is not charged"), OwnedBegins, 0);
        TestEqual(TEXT("Revoked leg stays planted"), H.Solver.GetLegState(OwnedIndex).Get_StartsThisStep(), uint8{0});
        TestEqual(TEXT("Other independent ordinary action starts"), H.Solver.GetLegState(Other).Get_StartsThisStep(), uint8{1});
    }
    {
        auto H = FHarness{};
        TestTrue(TEXT("Earlier ordinary conflict fixture resets"), H.Init());
        H.Inputs[1].Set_PhaseOffset(0.0f);
        H.Inputs[0].Set_IdealTarget(FVector{50.0, 0.0, 0.0});
        auto Live = true;
        auto Ordinary = 0;
        auto Owned = 0;
        const auto Current = [&]() { return Live; };
        const auto Reject = [&](FRejection) { Live = false; };
        const auto Begin = [&](const FOwnedBeginEvent& Event)
        {
            Event.Owned ? ++Owned : ++Ordinary;
            return false; // An ordinary admitted start cannot be vetoed.
        };
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        Owner.Selection = H.Selection(1, FVector{70.0, 80.0, 0.0}, FPurpose::AuxiliaryEmergency);
        TestTrue(TEXT("Earlier ordinary begin accepted"), H.Tick(Owner, Allow, FResolver{Resolve}));
        TestEqual(TEXT("Ordinary begin observed"), Ordinary, 1);
        TestEqual(TEXT("No later stale owned begin"), Owned, 0);
        TestEqual(TEXT("Ordinary false observer return is ignored"), H.Solver.GetLegState(0).Get_StartsThisStep(), uint8{1});
        TestEqual(TEXT("Owned overlay does not leak into fallback"), H.Solver.GetLegState(1).Get_StartsThisStep(), uint8{0});
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOwnedSelectionCommittedContactTest,
    "Ck.ProceduralAnimation.Gait.OwnedSelection.CommittedNativeContact", ck::tests::kCkUnitTestFlags)

auto
    FCkOwnedSelectionCommittedContactTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_owned_selection;
    for (const auto ResolveSucceeds : {false, true})
    {
        auto H = FHarness{};
        TestTrue(TEXT("Committed contact fixture resets"), H.Init());
        auto Live = true;
        auto Commits = 0;
        auto Emergency = false;
        const auto Current = [&]() { return Live; };
        const auto Reject = [&](FRejection) { Live = false; };
        const auto Begin = [&](const FOwnedBeginEvent& Event)
        {
            ++Commits;
            Emergency = Event.Owned && Event.Emergency && Event.Trusted;
            Live = false; // A successful charge has no subsequent liveness veto before begin.
            return true;
        };
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        const auto Fixed = FVector{70.0, 0.0, 0.0};
        Owner.Selection = H.Selection(0, Fixed, FPurpose::AuxiliaryEmergency);
        TestTrue(TEXT("RR owned Emergency begins"), H.Tick(Owner, Allow, FResolver{Resolve}));
        TestEqual(TEXT("One atomic commit"), Commits, 1);
        TestTrue(TEXT("RR keeps its actual Emergency classification"), Emergency);
        TestTrue(TEXT("Committed native contact survives lease loss"), H.Solver.GetLegState(0).Get_Swing().Get_CommittedNativeContact());
        TestFalse(TEXT("RR is not PlannedReplant"), H.Solver.GetLegState(0).Get_Swing().Get_PlannedReplant());
        auto Resolutions = 0;
        auto ExactTarget = false;
        const auto Complete = [&](const ck::FProceduralGaitSolver&, int32 Index, const FVector& Point,
            const FVector& Validated, const FVector& Normal) -> TOptional<FTouchdown>
        {
            ++Resolutions;
            ExactTarget = Index == 0 && Point == Fixed && Validated == Fixed;
            return ResolveSucceeds ? TOptional<FTouchdown>{FTouchdown{Point, Normal, true}} : TOptional<FTouchdown>{};
        };
        H.Inputs[0].Set_IdealTarget(FVector{-10.0, 0.0, 0.0}).Set_TargetTrusted(false)
            .Set_LandingGround(ck::EProceduralGaitLandingGround::None);
        TestTrue(TEXT("Ordinary Step continues the begun swing"), H.Solver.Step(FCk_Time{0.2}, 0.0f,
            FVector::ZeroVector, H.Inputs, H.Outputs, false, {}, FResolver{Complete}));
        TestEqual(TEXT("Fresh native touchdown is required"), Resolutions, 1);
        TestTrue(TEXT("Expired overlay never retargets to ordinary fallback"), ExactTarget);
        TestTrue(TEXT("Attempt flag is actual"), H.Outputs[0].Get_NativeTouchdownAttempted());
        TestEqual(TEXT("Resolution remains the actual resolver result"), H.Outputs[0].Get_NativeTouchdownResolved(), ResolveSucceeds);
        TestEqual(TEXT("Failed native resolution never manufactures trust"), H.Solver.GetLegState(0).Get_Plant().Get_Trusted(), ResolveSucceeds);
        if (ResolveSucceeds)
        { TestTrue(TEXT("Actual published plant is the resolved contact"), H.Outputs[0].Get_Position() == Fixed); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOwnedSelectionBeginPathsTest,
    "Ck.ProceduralAnimation.Gait.OwnedSelection.AllBeginPaths", ck::tests::kCkUnitTestFlags)

auto
    FCkOwnedSelectionBeginPathsTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_owned_selection;
    for (auto Path = 0; Path < 3; ++Path)
    {
        auto H = FHarness{};
        TestTrue(TEXT("Alternate path fixture resets"), H.Init());
        if (Path == 0)
        {
            H.Inputs[0].Set_Enabled(false);
            TestTrue(TEXT("Disable through production Step"), H.Solver.Step(FCk_Time{0.01}, 0.0f, FVector::ZeroVector, H.Inputs, H.Outputs));
            H.Inputs[0].Set_Enabled(true).Set_IdealTarget(FVector{10.0, 0.0, 0.0});
        }
        else
        {
            if (Path == 1)
            {
                for (auto& Input : H.Inputs)
                { Input.Set_NativeRecoveryEnabled(false); }
            }
            TestTrue(TEXT("Enter airborne through production Step"), H.Solver.Step(FCk_Time{0.01}, 0.0f,
                FVector::ZeroVector, H.Inputs, H.Outputs, true));
        }
        auto Live = true;
        auto Events = 0;
        auto BeforeMutation = false;
        auto Kind = FKind::Normal;
        const auto Current = [&]() { return Live; };
        const auto Reject = [&](FRejection) { Live = false; };
        const auto Begin = [&](const FOwnedBeginEvent& Event)
        {
            ++Events;
            Kind = Event.Kind;
            BeforeMutation = NOT Event.Owned && NOT H.Solver.GetLegState(Event.LegIndex).Get_Swing().Get_Active();
            return false;
        };
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        TestTrue(TEXT("Alternate actual begin completes"), H.Tick(Owner, Allow, FResolver{Resolve}, FCk_Time{0.2}));
        TestEqual(TEXT("First conflicting actual begin observed"), Events, 1);
        TestTrue(TEXT("Observation precedes swing mutation"), BeforeMutation);
        TestEqual(TEXT("Exact alternate begin kind"), static_cast<uint8>(Kind), static_cast<uint8>(
            Path == 0 ? FKind::Reenabled : Path == 1 ? FKind::AirReturn : FKind::NativeRecovery));
        if (Path == 2)
        {
            TestTrue(TEXT("Native recovery can begin and resolve in the same Step"), H.Outputs[0].Get_NativeTouchdownAttempted()
                && H.Outputs[0].Get_NativeTouchdownResolved() && H.Outputs[0].Get_Planted());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOwnedSelectionLedgerTest,
    "Ck.ProceduralAnimation.Gait.OwnedSelection.GuardedFourCommitBoundary", ck::tests::kCkUnitTestFlags)

auto
    FCkOwnedSelectionLedgerTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_owned_selection;
    auto H = FHarness{};
    TestTrue(TEXT("Four-slot fixture resets"), H.Init());
    auto Commits = 0;
    auto AllowTakeoff = false;
    const auto Current = []() { return true; };
    const auto Reject = [](FRejection) {};
    const auto Begin = [&](const FOwnedBeginEvent& Event)
    {
        if (NOT Event.Owned || Commits >= 4 || Event.Token.Slot != Commits)
        { return false; }
        ++Commits;
        return true;
    };
    const auto Guard = [&](const ck::FProceduralGaitSolver&, int32, bool) { return AllowTakeoff; };
    for (auto Slot = uint8{0}; Slot < 4; ++Slot)
    {
        auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
        const auto Target = FVector{70.0 - Slot * 10.0, 0.0, 0.0};
        auto Selected = H.Selection(0, Target, Slot == 0 ? FPurpose::AuxiliaryEmergency
            : Slot == 3 ? FPurpose::LegacyFinal : FPurpose::Preparation, Slot);
        if (Slot == 3)
        {
            auto Intent = Selected.Input.Get_PlannedReplantIntent().GetValue();
            Intent.Set_ExitHip(FVector{-35.0, 0.0, 60.0});
            Selected.Input.Set_PlannedReplantIntent(Intent);
        }
        Owner.Selection = Selected;
        AllowTakeoff = false;
        TestTrue(TEXT("Denied start is an accepted solve"), H.Tick(Owner, Guard, FResolver{Resolve}));
        TestEqual(TEXT("Denied guard spends no slot"), Commits, static_cast<int32>(Slot));
        AllowTakeoff = true;
        TestTrue(TEXT("Admitted guarded start charges once"), H.Tick(Owner, Guard, FResolver{Resolve}));
        TestEqual(TEXT("One slot at the begin boundary"), Commits, static_cast<int32>(Slot + 1));
        TestTrue(TEXT("Committed swing resolves through production Step"), H.Solver.Step(FCk_Time{0.2}, 0.0f,
            FVector::ZeroVector, H.Inputs, H.Outputs, false, {}, FResolver{Resolve}));
        TestTrue(TEXT("Actual native completion"), H.Outputs[0].Get_NativeTouchdownResolved());
        H.Inputs[0].Set_IdealTarget(Target);
    }
    auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
    auto Fifth = H.Selection(0, FVector{30.0, 0.0, 0.0}, FPurpose::LegacyFinal, 3);
    auto Intent = Fifth.Input.Get_PlannedReplantIntent().GetValue();
    Intent.Set_ExitHip(FVector{-45.0, 0.0, 60.0});
    Fifth.Input.Set_PlannedReplantIntent(Intent);
    Owner.Selection = Fifth;
    TestTrue(TEXT("Already charged owner can withhold another begin"), H.Tick(Owner, Allow, FResolver{Resolve}));
    TestEqual(TEXT("Exactly four callback commits"), Commits, 4);
    TestEqual(TEXT("False atomic charge cannot start a fifth swing"), H.Solver.GetLegState(0).Get_StartsThisStep(), uint8{0});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOwnedSelectionOrdinaryEquivalenceTest,
    "Ck.ProceduralAnimation.Gait.OwnedSelection.OrdinaryEquivalence", ck::tests::kCkUnitTestFlags)

auto
    FCkOwnedSelectionOrdinaryEquivalenceTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_owned_selection;
    auto Old = FHarness{};
    auto Observed = FHarness{};
    TestTrue(TEXT("Old fixture resets"), Old.Init());
    TestTrue(TEXT("Observed fixture resets"), Observed.Init());
    Old.Inputs[0].Set_IdealTarget(FVector{60.0, 0.0, 0.0});
    Observed.Inputs = Old.Inputs;
    auto Live = true;
    const auto Current = [&]() { return Live; };
    const auto Reject = [&](FRejection) { Live = false; };
    const auto Begin = [](const FOwnedBeginEvent&) { return false; };
    auto Owner = ck::FProceduralGaitOwnerContext{Current, Reject, Begin};
    for (auto Tick = 0; Tick < 20; ++Tick)
    {
        TestTrue(TEXT("Old public entry accepted"), Old.Solver.StepWithTakeoffGuard(FCk_Time{0.01}, 0.0f,
            FVector::ZeroVector, Old.Inputs, Old.Outputs, false, {}, Allow, Old.Denied, FResolver{Resolve}));
        TestTrue(TEXT("Observer-only public entry accepted"), Observed.Tick(Owner, Allow, FResolver{Resolve}));
        TestEqual(TEXT("Clock behavior unchanged"), Old.Solver.GetGaitClock(), Observed.Solver.GetGaitClock());
        for (auto Leg = 0; Leg < 2; ++Leg)
        {
            TestTrue(TEXT("Ordinary emitted position unchanged"), Old.Outputs[Leg].Get_Position() == Observed.Outputs[Leg].Get_Position());
            TestEqual(TEXT("Ordinary planted phase unchanged"), Old.Outputs[Leg].Get_Planted(), Observed.Outputs[Leg].Get_Planted());
            TestEqual(TEXT("Ordinary actual starts unchanged"), Old.Solver.GetLegState(Leg).Get_StartsThisStep(),
                Observed.Solver.GetLegState(Leg).Get_StartsThisStep());
        }
    }
    return true;
}

#endif
