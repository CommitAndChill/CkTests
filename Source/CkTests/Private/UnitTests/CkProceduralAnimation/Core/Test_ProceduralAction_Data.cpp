#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/Core/CkProceduralActionEvaluation.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"

#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_test_procedural_action_data
{
    auto
    MakeClip(
        double InDurationSeconds = 1.0)
        -> UCk_ProceduralAction_Data*
    {
        auto* Asset = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
        auto Body = FCk_ProceduralAction_BodyChannel{};
        Body.Set_Enabled(true);
        Asset->Set_BodyChannel(Body);
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{InDurationSeconds}};
        Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{10.0, 0.0, 0.0}, FQuat::Identity}});
        Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Asset;
    }

    auto
    Compile(
        UCk_ProceduralAction_Data& InAsset,
        ck::procedural_action::FCompiledClip& OutClip,
        TArray<ck::procedural_action::FDiagnostic>& OutDiagnostics)
        -> bool
    { return ck::procedural_action::Compile(InAsset, OutClip, OutDiagnostics); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionInvalidClipTest,
    "Ck.ProceduralAnimation.Action.InvalidClipRejectsAtomically",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionInvalidClipTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip();
    auto Phase = Asset->Get_Phases()[0];
    auto Keys = Phase.Get_BodyKeys();
    Keys[1].Set_Rotation(FQuat{0.0, 0.0, 0.0, 2.0});
    Phase.Set_BodyKeys(Keys);
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});

    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    TestFalse(TEXT("Malformed quaternion rejected"), ck_test_procedural_action_data::Compile(*Asset, Compiled, Diagnostics));
    TestEqual(TEXT("No partial compiled clip"), Compiled.Get_Phases().Num(), 0);
    TestEqual(TEXT("Out clip reset"), Compiled.Get_Duration().Get_Seconds(), 0.0);
    TestTrue(TEXT("Field-addressed diagnostic"), Diagnostics.ContainsByPredicate([](const auto& InDiagnostic)
    { return InDiagnostic.Get_FieldPath().Contains(TEXT("BodyKeys[1]")) && InDiagnostic.Get_Code() == FName{TEXT("TransformKey")}; }));

    auto Valid = ck_test_procedural_action_data::MakeClip();
    Valid->Set_Markers(TArray<FCk_ProceduralAction_Marker>{
        FCk_ProceduralAction_Marker{FName{TEXT("A")}, FCk_ProceduralAction_TimeRef{FName{TEXT("Missing")}, FCk_FloatRange_0to1{0.5}}}});
    TestFalse(TEXT("Bad phase reference rejected"), ck_test_procedural_action_data::Compile(*Valid, Compiled, Diagnostics));
    TestTrue(TEXT("Specific phase-reference diagnostic"), Diagnostics.ContainsByPredicate([](const auto& InDiagnostic)
    { return InDiagnostic.Get_Code() == FName{TEXT("PhaseReference")}; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionRevisionTest,
    "Ck.ProceduralAnimation.Action.CompiledRevisionIsIndependentOfAssetEdits",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionRevisionTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip();
    auto OldClip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Original revision compiles"), ck_test_procedural_action_data::Compile(*Asset, OldClip, Diagnostics)))
    { return false; }
    auto Phase = Asset->Get_Phases()[0];
    auto Keys = Phase.Get_BodyKeys();
    Keys[1].Set_Translation(FVector{20.0, 0.0, 0.0});
    Phase.Set_BodyKeys(Keys);
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    auto NewClip = ck::procedural_action::FCompiledClip{};
    if (NOT TestTrue(TEXT("Edited revision compiles"), ck_test_procedural_action_data::Compile(*Asset, NewClip, Diagnostics)))
    { return false; }
    TestTrue(TEXT("Content revision differs"), OldClip.Get_ContentHash() != NewClip.Get_ContentHash());
    auto OldPose = ck::procedural_action::FEvaluatedPose{};
    auto NewPose = ck::procedural_action::FEvaluatedPose{};
    const auto Initial = ck::procedural_action::FInitialPose{};
    TestTrue(TEXT("Pinned old value evaluates"), ck::procedural_action::Evaluate(OldClip, Initial, FCk_Time{1.0}, OldPose));
    TestTrue(TEXT("New value evaluates"), ck::procedural_action::Evaluate(NewClip, Initial, FCk_Time{1.0}, NewPose));
    TestTrue(TEXT("Old pose unchanged"), FMath::IsNearlyEqual(OldPose.Get_Body().GetTranslation().X, 10.0));
    TestTrue(TEXT("New pose updated"), FMath::IsNearlyEqual(NewPose.Get_Body().GetTranslation().X, 20.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionContentBoundsTest,
    "Ck.ProceduralAnimation.Action.ContentBoundsRejectBeforeCompileCopy",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionContentBoundsTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip();
    auto Phases = TArray<FCk_ProceduralAction_Phase>{};
    Phases.Init(Asset->Get_Phases()[0], ck::procedural_action::content_limits::MaxPhases + 1);
    Asset->Set_Phases(Phases);
    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    TestFalse(TEXT("Phase ceiling rejects before compiling arrays"),
        ck_test_procedural_action_data::Compile(*Asset, Compiled, Diagnostics));
    TestTrue(TEXT("Phase ceiling has field diagnostic"), Diagnostics.ContainsByPredicate([](const auto& InDiagnostic)
    { return InDiagnostic.Get_Code() == FName{TEXT("ContentLimit")}
        && InDiagnostic.Get_FieldPath() == TEXT("Phases"); }));
    TestEqual(TEXT("No oversized partial clip escapes"), Compiled.Get_Phases().Num(), 0);

    Asset = ck_test_procedural_action_data::MakeClip();
    auto Phase = Asset->Get_Phases()[0];
    auto Tracks = TArray<FCk_ProceduralAction_LimbTrack>{};
    Tracks.Init(FCk_ProceduralAction_LimbTrack{FName{TEXT("Oversized")}},
        ck::procedural_action::content_limits::MaxTracksPerPhase + 1);
    Phase.Set_LimbTracks(Tracks);
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    TestFalse(TEXT("Per-phase track ceiling rejects before compiling keys"),
        ck_test_procedural_action_data::Compile(*Asset, Compiled, Diagnostics));
    TestTrue(TEXT("Track ceiling has field diagnostic"), Diagnostics.ContainsByPredicate([](const auto& InDiagnostic)
    { return InDiagnostic.Get_Code() == FName{TEXT("ContentLimit")}
        && InDiagnostic.Get_FieldPath().Contains(TEXT("LimbTracks")); }));
    Asset = ck_test_procedural_action_data::MakeClip();
    Phase = Asset->Get_Phases()[0];
    auto BodyKeys = TArray<FCk_ProceduralAction_TransformKey>{};
    BodyKeys.Init(Phase.Get_BodyKeys()[0],
        static_cast<int32>(ck::procedural_action::content_limits::MaxKeys + 1));
    Phase.Set_BodyKeys(MoveTemp(BodyKeys));
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    TestFalse(TEXT("Total key ceiling rejects before key compilation"),
        ck_test_procedural_action_data::Compile(*Asset, Compiled, Diagnostics));
    TestTrue(TEXT("Total key ceiling has field diagnostic"), Diagnostics.ContainsByPredicate([](const auto& InDiagnostic)
    { return InDiagnostic.Get_Code() == FName{TEXT("ContentLimit")}
        && InDiagnostic.Get_FieldPath() == TEXT("Phases.Keys"); }));
    Asset = ck_test_procedural_action_data::MakeClip();
    auto Markers = TArray<FCk_ProceduralAction_Marker>{};
    for (auto Index = 0; Index < 65; ++Index)
    {
        Markers.Emplace(FName{*FString::Printf(TEXT("Marker%d"), Index)},
            FCk_ProceduralAction_TimeRef{FName{TEXT("Reach")}, FCk_FloatRange_0to1{0.5}});
    }
    Asset->Set_Markers(MoveTemp(Markers));
    TestFalse(TEXT("Unsplittable equal-time crossing cluster rejected"),
        ck_test_procedural_action_data::Compile(*Asset, Compiled, Diagnostics));
    TestTrue(TEXT("Equal-time cluster has capacity diagnostic"), Diagnostics.ContainsByPredicate([](const auto& InDiagnostic)
    { return InDiagnostic.Get_Code() == FName{TEXT("EventCapacity")}; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionEventOrderTest,
    "Ck.ProceduralAnimation.Action.NarrowWindowAndEqualTimeOrder",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionEventOrderTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip();
    auto At = [](double InTime)
    { return FCk_ProceduralAction_TimeRef{FName{TEXT("Reach")}, FCk_FloatRange_0to1{InTime}}; };
    Asset->Set_Markers(TArray<FCk_ProceduralAction_Marker>{
        FCk_ProceduralAction_Marker{FName{TEXT("Point")}, At(0.045)},
        FCk_ProceduralAction_Marker{FName{TEXT("SecondPoint")}, At(0.045)}});
    Asset->Set_Windows(TArray<FCk_ProceduralAction_Window>{
        FCk_ProceduralAction_Window{FName{TEXT("Closing")}, At(0.04), At(0.045)},
        FCk_ProceduralAction_Window{FName{TEXT("Opening")}, At(0.045), At(0.06)}});
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Valid narrow windows compile"), ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    auto Initial = ck::procedural_action::FInitialPose{};
    auto Budget = FCk_ProceduralAction_Sampling{};
    Budget.Set_MaxAdvance(FCk_Time{0.1});
    Budget.Set_MaxSamples(32);
    auto Interval = ck::procedural_action::FSampledInterval{};
    if (NOT TestTrue(TEXT("100 ms hitch sampled"), ck::procedural_action::SampleInterval(
        Clip, Initial, FCk_Time{0.0}, FCk_Time{0.1}, Budget, Interval)))
    { return false; }
    TestEqual(TEXT("Hitch first stops before opening exposure"), Interval.Get_AcceptedEnd().Get_Seconds(), 0.04);
    TestTrue(TEXT("Window boundary is reported"), Interval.Get_StoppedAtWindowBoundary());
    TestTrue(TEXT("Unserviced hitch remains backlog"), FMath::IsNearlyEqual(Interval.Get_Backlog().Get_Seconds(), 0.06));
    auto Events = Interval.Get_CrossedEvents();
    for (const auto ExpectedEnd : {0.045, 0.06, 0.1})
    {
        const auto From = Interval.Get_AcceptedEnd();
        if (NOT TestTrue(TEXT("Backlog resumes at the accepted boundary"), ck::procedural_action::SampleInterval(
            Clip, Initial, From, FCk_Time{0.1}, Budget, Interval)))
        { return false; }
        TestEqual(TEXT("Each narrow window retains its own interval"), Interval.Get_AcceptedEnd().Get_Seconds(), ExpectedEnd);
        Events.Append(Interval.Get_CrossedEvents());
    }
    TestEqual(TEXT("Every edge crossed exactly once"), Events.Num(), 6);
    if (Events.Num() == 6)
    {
        TestEqual(TEXT("Equal time exit first"), Events[1].Get_Kind(), ck::procedural_action::EEventKind::WindowExit);
        TestEqual(TEXT("First authored point next"), Events[2].Get_Id(), FName{TEXT("Point")});
        TestEqual(TEXT("Second authored point next"), Events[3].Get_Id(), FName{TEXT("SecondPoint")});
        TestEqual(TEXT("Equal time enter last"), Events[4].Get_Kind(), ck::procedural_action::EEventKind::WindowEnter);
    }
    TestEqual(TEXT("Hitch backlog is fully consumed"), Interval.Get_Backlog().Get_Seconds(), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionCriticalPathTest,
    "Ck.ProceduralAnimation.Action.NarrowExcursionCannotBeSkipped",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionCriticalPathTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip();
    auto Phase = Asset->Get_Phases()[0];
    Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.5}, FVector{10.0, 0.0, 0.0}, FQuat::Identity},
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector::ZeroVector, FQuat::Identity}});
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Excursion compiles"), ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    auto Budget = FCk_ProceduralAction_Sampling{};
    Budget.Set_MaxAdvance(FCk_Time{1.0});
    Budget.Set_MaxSamples(64);
    Budget.Set_MaxEndpointTravelCm(2.0f);
    auto Interval = ck::procedural_action::FSampledInterval{};
    if (NOT TestTrue(TEXT("Excursion samples"), ck::procedural_action::SampleInterval(
        Clip, {}, FCk_Time{0.0}, FCk_Time{1.0}, Budget, Interval)))
    { return false; }
    auto Peak = 0.0;
    for (const auto& Sample : Interval.Get_Samples())
    { Peak = FMath::Max(Peak, Sample.Get_Body().GetTranslation().X); }
    TestTrue(TEXT("Interior excursion was observed"), Peak > 9.9);
    TestTrue(TEXT("Budget forced subdivisions"), Interval.Get_Samples().Num() > 2);
    TestTrue(TEXT("Full interval accepted"), FMath::IsNearlyEqual(Interval.Get_AcceptedEnd().Get_Seconds(), 1.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionInvalidInitialTest,
    "Ck.ProceduralAnimation.Action.MalformedInitialPoseRejected",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionInvalidInitialTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip();
    auto BodyChannel = Asset->Get_BodyChannel();
    BodyChannel.Set_InitialSource(ECk_ProceduralAction_InitialSource::CapturedAcceptedPose);
    Asset->Set_BodyChannel(BodyChannel);
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Clip compiles"), ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    auto Initial = ck::procedural_action::FInitialPose{};
    Initial.Set_Body(FTransform{FQuat::Identity,
        FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, FVector::OneVector});
    auto Pose = ck::procedural_action::FEvaluatedPose{};
    TestFalse(TEXT("NaN body entry rejected"), ck::procedural_action::Evaluate(Clip, Initial, FCk_Time{0.0}, Pose));
    Initial.Set_Body(FTransform::Identity);
    TestTrue(TEXT("Rigid body entry accepted"), ck::procedural_action::Evaluate(Clip, Initial, FCk_Time{0.0}, Pose));
    Asset->Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{FName{TEXT("Hand")}}});
    if (NOT TestTrue(TEXT("Limb clip compiles"), ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    Initial.Get_Limbs().Add(FName{TEXT("Hand")}, FTransform{FQuat::Identity,
        FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, FVector::OneVector});
    TestFalse(TEXT("NaN limb entry rejected"), ck::procedural_action::Evaluate(Clip, Initial, FCk_Time{0.0}, Pose));
    Initial.Get_Limbs().Add(FName{TEXT("Hand")}, FTransform::Identity);
    Initial.Get_BendDirections().Add(FName{TEXT("Hand")}, FVector::ZeroVector);
    TestFalse(TEXT("Zero bend rejected"), ck::procedural_action::Evaluate(Clip, Initial, FCk_Time{0.0}, Pose));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionNoNumericalProgressTest,
    "Ck.ProceduralAnimation.Action.UnrepresentableSubstepRejected",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionNoNumericalProgressTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip(1.0e300);
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Asset->Get_Phases()[0],
        FCk_ProceduralAction_Phase{FName{TEXT("Late")}, FCk_Time{1.0e300}}});
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Large finite timeline compiles"), ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    auto Budget = FCk_ProceduralAction_Sampling{};
    Budget.Set_MaxAdvance(FCk_Time{1.0});
    auto Interval = ck::procedural_action::FSampledInterval{};
    TestFalse(TEXT("Unrepresentable increment cannot publish a duplicate sample"),
        ck::procedural_action::SampleInterval(Clip, {}, FCk_Time{1.0e300}, FCk_Time{1.0e300 + 1.0e290}, Budget, Interval));
    TestEqual(TEXT("No duplicate sample"), Interval.Get_Samples().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionSlotFrameTest,
    "Ck.ProceduralAnimation.Action.SlotFrameValidated",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionSlotFrameTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip();
    auto Hand = FCk_ProceduralAction_Slot{FName{TEXT("Hand")}};
    Hand.Set_Space(static_cast<ECk_ProceduralAction_TrackSpace>(0xFF));
    Asset->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Hand});
    auto First = Asset->Get_Phases()[0];
    First.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{FCk_ProceduralAction_LimbTrack{FName{TEXT("Hand")}}});
    auto Second = FCk_ProceduralAction_Phase{FName{TEXT("Release")}, FCk_Time{0.1}};
    Second.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{FCk_ProceduralAction_LimbTrack{FName{TEXT("Hand")}}});
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{First, Second});
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    TestFalse(TEXT("A slot in an unsupported space is rejected"),
        ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics));
    TestTrue(TEXT("Frame diagnostic names the slot's space"), Diagnostics.ContainsByPredicate([](const auto& InDiagnostic)
    { return InDiagnostic.Get_Code() == FName{TEXT("TrackSpace")}
        && InDiagnostic.Get_FieldPath() == TEXT("Slots[0].Space")
        && InDiagnostic.Get_Slot() == FName{TEXT("Hand")}; }));
    TestEqual(TEXT("Rejected clip is empty"), Clip.Get_Phases().Num(), 0);

    Hand.Set_Space(ECk_ProceduralAction_TrackSpace::RootAtStart);
    Asset->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Hand});
    TestTrue(TEXT("A valid slot frame compiles"), ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics));
    TestTrue(TEXT("The compiled slot reports its frame"), Clip.Get_Slots().Num() == 1
        && Clip.Get_Slots()[0].Get_Space() == ECk_ProceduralAction_TrackSpace::RootAtStart);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBudgetAndPhaseTest,
    "Ck.ProceduralAnimation.Action.BudgetBacklogAndPhaseBarrier",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionBudgetAndPhaseTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = ck_test_procedural_action_data::MakeClip(0.1);
    auto Second = FCk_ProceduralAction_Phase{FName{TEXT("Return")}, FCk_Time{0.1}};
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Asset->Get_Phases()[0], Second});
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Two phases compile"), ck_test_procedural_action_data::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    auto Budget = FCk_ProceduralAction_Sampling{};
    Budget.Set_MaxAdvance(FCk_Time{0.02});
    Budget.Set_MaxSamples(3);
    Budget.Set_MaxEndpointTravelCm(100.0f);
    auto Interval = ck::procedural_action::FSampledInterval{};
    const auto Initial = ck::procedural_action::FInitialPose{};
    if (NOT TestTrue(TEXT("Budgeted prefix produced"), ck::procedural_action::SampleInterval(
        Clip, Initial, FCk_Time{0.0}, FCk_Time{0.2}, Budget, Interval)))
    { return false; }
    TestTrue(TEXT("Budget leaves explicit backlog"), Interval.Get_BudgetExhausted() && Interval.Get_Backlog().Get_Seconds() > 0.0);
    TestTrue(TEXT("No interval skipped"), Interval.Get_AcceptedEnd().Get_Seconds() < 0.1);
    Budget.Set_MaxSamples(32);
    if (NOT TestTrue(TEXT("Larger budget reaches phase end"), ck::procedural_action::SampleInterval(
        Clip, Initial, FCk_Time{0.0}, FCk_Time{0.2}, Budget, Interval)))
    { return false; }
    TestTrue(TEXT("Stops at phase barrier"), Interval.Get_StoppedAtPhaseBoundary());
    TestTrue(TEXT("No unobserved phase advance"), FMath::IsNearlyEqual(Interval.Get_AcceptedEnd().Get_Seconds(), 0.1, 1.0e-6));
    return true;
}
#endif
