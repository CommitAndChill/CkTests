#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionEvaluation.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_test_procedural_action_builder
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const auto BodyPulsePath = FString{TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA")};
    const auto LimbNodPath = FString{TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA")};
    const auto Valid = ECk_ProceduralAction_ReportStatus::Valid;
    const auto Invalid = ECk_ProceduralAction_ReportStatus::Invalid;

    auto
    HasDiagnostic(
        const FCk_ProceduralAction_ValidationReport& InReport,
        FName InCode,
        const FString& InFieldPath)
        -> bool
    {
        return InReport.Get_Diagnostics().ContainsByPredicate([&](const FCk_ProceduralAction_Diagnostic& InDiagnostic)
        { return InDiagnostic.Get_Code() == InCode && InDiagnostic.Get_FieldPath() == InFieldPath; });
    }

    auto
    DraftsEqual(
        const FCk_ProceduralAction_ClipDraft& InA,
        const FCk_ProceduralAction_ClipDraft& InB)
        -> bool
    { return FCk_ProceduralAction_ClipDraft::StaticStruct()->CompareScriptStruct(&InA, &InB, PPF_None); }

    template <typename TStruct>
    auto
    ArraysEqual(
        const TArray<TStruct>& InA,
        const TArray<TStruct>& InB)
        -> bool
    {
        if (InA.Num() != InB.Num())
        { return false; }
        for (auto Index = 0; Index < InA.Num(); ++Index)
        {
            if (NOT TStruct::StaticStruct()->CompareScriptStruct(&InA[Index], &InB[Index], PPF_None))
            { return false; }
        }
        return true;
    }

    auto
    MakeBodyDraft()
        -> FCk_ProceduralAction_ClipDraft
    {
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        auto Body = FCk_ProceduralAction_BodyChannel{};
        Body.Set_Enabled(true);
        Body.Set_InitialSource(ECk_ProceduralAction_InitialSource::IdentityOffset);
        UCk_Utils_ProceduralActionBuilder_UE::Set_BodyChannel(Draft, Body);
        UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft, FCk_ProceduralAction_Phase{FName{TEXT("Lean")}, FCk_Time{0.5}});
        return Draft;
    }

    auto
    MakeBodyPose(
        double InTime,
        const FVector& InTranslation)
        -> FCk_ProceduralAction_PoseKey
    {
        auto Pose = FCk_ProceduralAction_PoseKey{FCk_FloatRange_0to1{InTime}};
        Pose.Set_BodyMode(ECk_ProceduralAction_ValueMode::Set);
        Pose.Set_Body(FTransform{FQuat::Identity, InTranslation, FVector::OneVector});
        return Pose;
    }

    auto
    MakeLimbPose(
        double InTime,
        const TArray<FCk_ProceduralAction_LimbPoseTarget>& InLimbs)
        -> FCk_ProceduralAction_PoseKey
    {
        auto Pose = FCk_ProceduralAction_PoseKey{FCk_FloatRange_0to1{InTime}};
        Pose.Set_Limbs(InLimbs);
        return Pose;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderDuplicateTest,
    "Ck.ProceduralAnimation.Action.Builder.RejectsDuplicatePhaseAndSlot",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderDuplicateTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    auto Draft = FCk_ProceduralAction_ClipDraft{};
    TestEqual(TEXT("First phase accepted"), UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft,
        FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{0.4}}).Get_Status(), Valid);
    TestEqual(TEXT("First slot accepted"), UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft,
        FCk_ProceduralAction_Slot{FName{TEXT("Hand")}}).Get_Status(), Valid);
    const auto Before = Draft;

    const auto DuplicatePhase = UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft,
        FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{0.2}});
    TestEqual(TEXT("Duplicate phase rejected"), DuplicatePhase.Get_Status(), Invalid);
    TestTrue(TEXT("Duplicate phase names the would-be field"), HasDiagnostic(DuplicatePhase, TEXT("PhaseId"), TEXT("Phases[1].Id")));
    TestEqual(TEXT("Unnamed phase rejected"), UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft,
        FCk_ProceduralAction_Phase{NAME_None, FCk_Time{0.2}}).Get_Status(), Invalid);
    const auto ZeroDuration = UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft,
        FCk_ProceduralAction_Phase{FName{TEXT("Hold")}, FCk_Time{0.0}});
    TestEqual(TEXT("Zero duration rejected"), ZeroDuration.Get_Status(), Invalid);
    TestTrue(TEXT("Zero duration names the duration"), HasDiagnostic(ZeroDuration, TEXT("Duration"), TEXT("Phases[1].Duration")));
    TestEqual(TEXT("NaN duration rejected"), UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft,
        FCk_ProceduralAction_Phase{FName{TEXT("Hold")}, FCk_Time{std::numeric_limits<double>::quiet_NaN()}}).Get_Status(), Invalid);

    const auto DuplicateSlot = UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft, FCk_ProceduralAction_Slot{FName{TEXT("Hand")}});
    TestEqual(TEXT("Duplicate slot rejected"), DuplicateSlot.Get_Status(), Invalid);
    TestTrue(TEXT("Duplicate slot names the would-be field"), HasDiagnostic(DuplicateSlot, TEXT("SlotId"), TEXT("Slots[1].Id")));
    TestEqual(TEXT("Unnamed slot rejected"), UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft,
        FCk_ProceduralAction_Slot{NAME_None}).Get_Status(), Invalid);
    TestTrue(TEXT("Every rejection left the draft unchanged"), DraftsEqual(Before, Draft));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderOccupiedTimeTest,
    "Ck.ProceduralAnimation.Action.Builder.RejectsOccupiedKeyTime",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderOccupiedTimeTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    auto Draft = MakeBodyDraft();
    UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft, FCk_ProceduralAction_Slot{FName{TEXT("Hand")}});
    const auto Lean = FName{TEXT("Lean")};
    const auto Key = MakeBodyPose(0.5, FVector{1.0, 0.0, 0.0});
    TestEqual(TEXT("First body key accepted"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean, Key).Get_Status(), Valid);
    TestEqual(TEXT("First limb key accepted"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean,
        MakeLimbPose(0.5, {FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{10.0, 0.0, 0.0}}})).Get_Status(), Valid);
    const auto Before = Draft;

    const auto BodyAgain = UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean, Key);
    TestEqual(TEXT("Occupied body time rejected"), BodyAgain.Get_Status(), Invalid);
    TestTrue(TEXT("Occupied body time names the key"), HasDiagnostic(BodyAgain, TEXT("KeyTime"), TEXT("Phases[0].BodyKeys[0].Time")));

    const auto LimbAgain = UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean,
        MakeLimbPose(0.5, {FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{20.0, 0.0, 0.0}}}));
    TestEqual(TEXT("Occupied limb time rejected"), LimbAgain.Get_Status(), Invalid);
    TestTrue(TEXT("Occupied limb time names the key"),
        HasDiagnostic(LimbAgain, TEXT("KeyTime"), TEXT("Phases[0].LimbTracks[0].PositionKeys[0].Time")));

    auto WithOrientation = FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{20.0, 0.0, 0.0}};
    WithOrientation.Set_OrientationMode(ECk_ProceduralAction_ValueMode::Set);
    WithOrientation.Set_Orientation(FQuat{0.0, 0.0, 0.0, 2.0});
    const auto Malformed = UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean, MakeLimbPose(0.25, {WithOrientation}));
    TestEqual(TEXT("Malformed orientation rejected"), Malformed.Get_Status(), Invalid);
    TestTrue(TEXT("Malformed orientation names the would-be key"),
        HasDiagnostic(Malformed, TEXT("Quaternion"), TEXT("Phases[0].LimbTracks[0].OrientationKeys[0].Value")));

    TestEqual(TEXT("Unknown phase rejected"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, FName{TEXT("Missing")},
        MakeBodyPose(0.25, FVector::ZeroVector)).Get_Status(), Invalid);
    TestEqual(TEXT("Unknown slot rejected"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean,
        MakeLimbPose(0.25, {FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Tail")}, FVector::ZeroVector}})).Get_Status(), Invalid);
    TestEqual(TEXT("Nonfinite body key rejected"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean,
        MakeBodyPose(0.25, FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0})).Get_Status(), Invalid);
    TestTrue(TEXT("Every rejection left the draft unchanged"), DraftsEqual(Before, Draft));

    TestEqual(TEXT("A free time inserts in order"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Lean,
        MakeBodyPose(0.25, FVector::ZeroVector)).Get_Status(), Valid);
    const auto& BodyKeys = Draft.Get_Phases()[0].Get_BodyKeys();
    TestTrue(TEXT("Insertion keeps the key array strictly increasing"), BodyKeys.Num() == 2
        && BodyKeys[0].Get_Time().Get_Value() == 0.25 && BodyKeys[1].Get_Time().Get_Value() == 0.5);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderPoseAtomicTest,
    "Ck.ProceduralAnimation.Action.Builder.PoseKeyIsAtomic",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderPoseAtomicTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    auto Draft = MakeBodyDraft();
    UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft, FCk_ProceduralAction_Slot{FName{TEXT("Hand")}});
    const auto Before = Draft;

    auto Pose = MakeBodyPose(0.5, FVector{5.0, 0.0, 0.0});
    Pose.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{
        FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{10.0, 0.0, 0.0}},
        FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Unknown")}, FVector{10.0, 0.0, 0.0}}});
    const auto Report = UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, FName{TEXT("Lean")}, Pose);
    TestEqual(TEXT("A pose naming an unknown slot rejects"), Report.Get_Status(), Invalid);
    TestTrue(TEXT("The unknown slot is named by its pose field"), HasDiagnostic(Report, TEXT("UnknownSlot"), TEXT("Pose.Limbs[1].Slot")));
    TestTrue(TEXT("Neither the body key nor the valid limb track was written"), DraftsEqual(Before, Draft));

    Pose.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{
        FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{10.0, 0.0, 0.0}},
        FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{12.0, 0.0, 0.0}}});
    TestEqual(TEXT("A pose naming one slot twice rejects"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(
        Draft, FName{TEXT("Lean")}, Pose).Get_Status(), Invalid);
    TestTrue(TEXT("Still nothing written"), DraftsEqual(Before, Draft));

    Pose.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{
        FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{10.0, 0.0, 0.0}}});
    TestEqual(TEXT("A complete pose is accepted"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(
        Draft, FName{TEXT("Lean")}, Pose).Get_Status(), Valid);
    TestTrue(TEXT("The pose wrote the body key and one limb track"), Draft.Get_Phases()[0].Get_BodyKeys().Num() == 1
        && Draft.Get_Phases()[0].Get_LimbTracks().Num() == 1);
    const auto AfterAdd = Draft;
    TestEqual(TEXT("Adding again at the same time rejects; Replace is the update"),
        UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, FName{TEXT("Lean")}, Pose).Get_Status(), Invalid);
    TestTrue(TEXT("The duplicate add wrote nothing"), DraftsEqual(AfterAdd, Draft));

    auto Update = MakeBodyPose(0.5, FVector{7.0, 0.0, 0.0});
    Update.Set_Limbs(TArray<FCk_ProceduralAction_LimbPoseTarget>{
        FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Hand")}, FVector{15.0, 0.0, 0.0}}});
    TestEqual(TEXT("Replace updates an existing pose"), UCk_Utils_ProceduralActionBuilder_UE::Replace_PoseKey(
        Draft, FName{TEXT("Lean")}, Update).Get_Status(), Valid);
    TestEqual(TEXT("Replace changed the body value"), Draft.Get_Phases()[0].Get_BodyKeys()[0].Get_Translation().X, 7.0);
    TestEqual(TEXT("Replace changed the limb value"),
        Draft.Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys()[0].Get_Value().X, 15.0);
    const auto AfterReplace = Draft;
    Update.Set_Time(FCk_FloatRange_0to1{0.75});
    TestEqual(TEXT("Replace at an empty time rejects"), UCk_Utils_ProceduralActionBuilder_UE::Replace_PoseKey(
        Draft, FName{TEXT("Lean")}, Update).Get_Status(), Invalid);
    TestTrue(TEXT("The failed replace wrote nothing"), DraftsEqual(AfterReplace, Draft));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderBodyOnlyTest,
    "Ck.ProceduralAnimation.Action.Builder.BodyOnlyDraftBuildsAndCompiles",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderBodyOnlyTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    auto Draft = MakeBodyDraft();
    TestEqual(TEXT("Neutral pose"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(
        Draft, FName{TEXT("Lean")}, MakeBodyPose(0.0, FVector::ZeroVector)).Get_Status(), Valid);
    TestEqual(TEXT("Forward pose"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(
        Draft, FName{TEXT("Lean")}, MakeBodyPose(1.0, FVector{20.0, 0.0, 0.0})).Get_Status(), Valid);

    auto* Clip = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    const auto Built = UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, Clip);
    if (NOT TestEqual(TEXT("Body-only draft builds"), Built.Get_Status(), Valid))
    { return false; }
    TestEqual(TEXT("Built clip validates through the public surface"),
        UCk_Utils_ProceduralAction_UE::Validate_Data(Clip).Get_Status(), Valid);
    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Built clip compiles"), ck::procedural_action::Compile(*Clip, Compiled, Diagnostics)))
    { return false; }
    TestTrue(TEXT("Body channel carried"), Compiled.Get_HasBodyChannel());
    auto Pose = ck::procedural_action::FEvaluatedPose{};
    TestTrue(TEXT("Midpoint evaluates"), ck::procedural_action::Evaluate(Compiled, {}, FCk_Time{0.25}, Pose));
    TestTrue(TEXT("Linear body pose blends to the midpoint"), FMath::IsNearlyEqual(Pose.Get_Body().GetTranslation().X, 10.0, 1.0e-9));

    TestEqual(TEXT("Null destination rejects"), UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, nullptr).Get_Status(), Invalid);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderMultiLimbTest,
    "Ck.ProceduralAnimation.Action.Builder.MultiLimbDraftBuildsAndCompiles",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderMultiLimbTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    auto Draft = FCk_ProceduralAction_ClipDraft{};
    UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft, FCk_ProceduralAction_Slot{FName{TEXT("Left")}});
    UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft, FCk_ProceduralAction_Slot{FName{TEXT("Right")}});
    UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft, FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{0.4}});
    UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft, FCk_ProceduralAction_Phase{FName{TEXT("Return")}, FCk_Time{0.4}});
    TestEqual(TEXT("Right slot frame is root-relative"), UCk_Utils_ProceduralActionBuilder_UE::Set_SlotFrame(
        Draft, FName{TEXT("Right")}, ECk_ProceduralAction_TrackSpace::RootAtStart).Get_Status(), Valid);
    TestEqual(TEXT("Unsupported space rejects"), UCk_Utils_ProceduralActionBuilder_UE::Set_SlotFrame(
        Draft, FName{TEXT("Left")}, static_cast<ECk_ProceduralAction_TrackSpace>(0xFF)).Get_Status(), Invalid);
    TestEqual(TEXT("Unknown slot frame rejects"), UCk_Utils_ProceduralActionBuilder_UE::Set_SlotFrame(
        Draft, FName{TEXT("Tail")}, ECk_ProceduralAction_TrackSpace::BodyLocal).Get_Status(), Invalid);

    const auto Turned = FQuat{FVector::UpVector, UE_HALF_PI};
    auto MakeLimbs = [&](double InX, bool InTurned)
    {
        auto Left = FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Left")}, FVector{InX, 30.0, 0.0}};
        Left.Set_OrientationMode(ECk_ProceduralAction_ValueMode::Set);
        Left.Set_Orientation(InTurned ? Turned : FQuat::Identity);
        auto Right = FCk_ProceduralAction_LimbPoseTarget{FName{TEXT("Right")}, FVector{InX, -30.0, 0.0}};
        Right.Set_BendMode(ECk_ProceduralAction_ValueMode::Set);
        Right.Set_Bend(InTurned ? FVector::ForwardVector : FVector::UpVector);
        return TArray<FCk_ProceduralAction_LimbPoseTarget>{Left, Right};
    };
    struct FPoseStep
    {
        FName Phase;
        double Time = 0.0;
        double X = 0.0;
        bool Turned = false;
    };
    const auto Reach = FName{TEXT("Reach")};
    const auto Return = FName{TEXT("Return")};
    for (const auto& Step : {FPoseStep{Reach, 0.0, 50.0, false}, FPoseStep{Reach, 0.5, 70.0, true}, FPoseStep{Reach, 1.0, 90.0, true},
        FPoseStep{Return, 0.0, 90.0, true}, FPoseStep{Return, 1.0, 50.0, false}})
    {
        TestEqual(*FString::Printf(TEXT("Pose %s@%g accepted"), *Step.Phase.ToString(), Step.Time),
            UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Draft, Step.Phase, MakeLimbPose(Step.Time, MakeLimbs(Step.X, Step.Turned))).Get_Status(), Valid);
    }

    auto* Clip = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    if (NOT TestEqual(TEXT("Multi-limb draft builds"), UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, Clip).Get_Status(), Valid))
    { return false; }
    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Multi-limb clip compiles"), ck::procedural_action::Compile(*Clip, Compiled, Diagnostics)))
    { return false; }
    const auto* RightSlot = Compiled.Get_Slots().FindByPredicate([](const ck::procedural_action::FCompiledSlot& InSlot)
    { return InSlot.Get_Id() == FName{TEXT("Right")}; });
    TestTrue(TEXT("Slot frame applied to every track of the slot"), RightSlot != nullptr
        && RightSlot->Get_Space() == ECk_ProceduralAction_TrackSpace::RootAtStart);
    TestTrue(TEXT("Each phase carries both limb tracks"), Compiled.Get_Phases()[0].Get_LimbTracks().Num() == 2
        && Compiled.Get_Phases()[1].Get_LimbTracks().Num() == 2);

    auto Initial = ck::procedural_action::FInitialPose{};
    Initial.Get_Limbs().Add(FName{TEXT("Left")}, FTransform::Identity);
    Initial.Get_Limbs().Add(FName{TEXT("Right")}, FTransform::Identity);
    auto Pose = ck::procedural_action::FEvaluatedPose{};
    if (NOT TestTrue(TEXT("Mid-reach evaluates"), ck::procedural_action::Evaluate(Compiled, Initial, FCk_Time{0.2}, Pose)))
    { return false; }
    const auto* Left = Pose.Get_Limbs().FindByPredicate([](const ck::procedural_action::FLimbPose& InLimb)
    { return InLimb.Get_Slot() == FName{TEXT("Left")}; });
    const auto* Right = Pose.Get_Limbs().FindByPredicate([](const ck::procedural_action::FLimbPose& InLimb)
    { return InLimb.Get_Slot() == FName{TEXT("Right")}; });
    TestTrue(TEXT("Left lands on its pose key"), Left != nullptr && Left->Get_Position().Equals(FVector{70.0, 30.0, 0.0}, 1.0e-9)
        && Left->Get_HasOrientation() && Left->Get_Orientation().Equals(Turned, 1.0e-9));
    TestTrue(TEXT("Right lands on its pose key"), Right != nullptr && Right->Get_Position().Equals(FVector{70.0, -30.0, 0.0}, 1.0e-9)
        && Right->Get_HasBend() && Right->Get_BendDirection().Equals(FVector::ForwardVector, 1.0e-9));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderBuildFailureTest,
    "Ck.ProceduralAnimation.Action.Builder.BuildIntoFailureLeavesDestinationUnchanged",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderBuildFailureTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    auto Good = MakeBodyDraft();
    UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Good, FName{TEXT("Lean")}, MakeBodyPose(0.0, FVector::ZeroVector));
    UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Good, FName{TEXT("Lean")}, MakeBodyPose(1.0, FVector{20.0, 0.0, 0.0}));
    UCk_Utils_ProceduralActionBuilder_UE::Add_Window(Good, FCk_ProceduralAction_Window{FName{TEXT("Peak")},
        FCk_ProceduralAction_TimeRef{FName{TEXT("Lean")}, FCk_FloatRange_0to1{0.25}},
        FCk_ProceduralAction_TimeRef{FName{TEXT("Lean")}, FCk_FloatRange_0to1{0.75}}});
    auto* Destination = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    if (NOT TestEqual(TEXT("Destination seeded by a valid build"), UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Good, Destination).Get_Status(), Valid))
    { return false; }

    const auto BodyBefore = Destination->Get_BodyChannel();
    const auto SlotsBefore = Destination->Get_Slots();
    const auto PhasesBefore = Destination->Get_Phases();
    const auto MarkersBefore = Destination->Get_Markers();
    const auto WindowsBefore = Destination->Get_Windows();
    const auto VersionBefore = Destination->Get_CompatibilityVersion();

    auto Bad = MakeBodyDraft();
    UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Bad, FCk_ProceduralAction_Slot{FName{TEXT("Hand")}});
    UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Bad, FName{TEXT("Lean")}, MakeBodyPose(0.5, FVector::ZeroVector));
    const auto Failed = UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Bad, Destination);
    TestEqual(TEXT("A draft missing key endpoints does not build"), Failed.Get_Status(), Invalid);
    TestTrue(TEXT("The compiler diagnostic is surfaced with its field"), HasDiagnostic(Failed, TEXT("KeyEndpoints"), TEXT("Phases[0].BodyKeys")));
    TestTrue(TEXT("Diagnostics name the draft as their source"), Failed.Get_Diagnostics().Num() > 0
        && Failed.Get_Diagnostics()[0].Get_AssetPath() == TEXT("<draft>"));

    TestTrue(TEXT("BodyChannel unchanged"), FCk_ProceduralAction_BodyChannel::StaticStruct()->CompareScriptStruct(
        &BodyBefore, &Destination->Get_BodyChannel(), PPF_None));
    TestTrue(TEXT("Slots unchanged"), ArraysEqual(SlotsBefore, Destination->Get_Slots()));
    TestTrue(TEXT("Phases unchanged"), ArraysEqual(PhasesBefore, Destination->Get_Phases()));
    TestTrue(TEXT("Markers unchanged"), ArraysEqual(MarkersBefore, Destination->Get_Markers()));
    TestTrue(TEXT("Windows unchanged"), ArraysEqual(WindowsBefore, Destination->Get_Windows()));
    TestEqual(TEXT("Compatibility version unchanged"), Destination->Get_CompatibilityVersion(), VersionBefore);
    TestEqual(TEXT("Destination still validates"), UCk_Utils_ProceduralAction_UE::Validate_Data(Destination).Get_Status(), Valid);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderRoundTripTest,
    "Ck.ProceduralAnimation.Action.Builder.DraftFromDataRoundTrips",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderRoundTripTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    for (const auto& Path : {BodyPulsePath, LimbNodPath})
    {
        auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, *Path);
        if (NOT TestNotNull(*FString::Printf(TEXT("%s loads"), *Path), Source))
        { return false; }
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        if (NOT TestEqual(TEXT("Draft copies the clip"), UCk_Utils_ProceduralActionBuilder_UE::Make_Draft_FromData(Source, Draft).Get_Status(), Valid))
        { return false; }
        auto* Copy = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
        if (NOT TestEqual(TEXT("Draft builds into a second clip"), UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, Copy).Get_Status(), Valid))
        { return false; }
        auto SourceClip = ck::procedural_action::FCompiledClip{};
        auto CopyClip = ck::procedural_action::FCompiledClip{};
        auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
        TestTrue(TEXT("Source compiles"), ck::procedural_action::Compile(*Source, SourceClip, Diagnostics));
        TestTrue(TEXT("Copy compiles"), ck::procedural_action::Compile(*Copy, CopyClip, Diagnostics));
        TestEqual(TEXT("Round trip keeps the compiled content hash"), CopyClip.Get_ContentHash(), SourceClip.Get_ContentHash());
        TestTrue(TEXT("Round trip keeps the authored phases"), ArraysEqual(Source->Get_Phases(), Copy->Get_Phases()));
    }

    auto Draft = MakeBodyDraft();
    const auto NullReport = UCk_Utils_ProceduralActionBuilder_UE::Make_Draft_FromData(nullptr, Draft);
    TestEqual(TEXT("Null source rejects"), NullReport.Get_Status(), Invalid);
    TestTrue(TEXT("Rejected copy resets the output draft"), DraftsEqual(Draft, FCk_ProceduralAction_ClipDraft{}));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderRecipesTest,
    "Ck.ProceduralAnimation.Action.Builder.RecipesCompile",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderRecipesTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    const auto Hand = TArray<FName>{FName{TEXT("Hand")}};
    auto Compile = [&](const FCk_ProceduralAction_ClipDraft& InDraft, ck::procedural_action::FCompiledClip& OutClip) -> bool
    {
        auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
        return ck::procedural_action::Compile(ck::procedural_action::MakeClipSource(InDraft), OutClip, Diagnostics);
    };

    {
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        const auto Report = UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(
            FCk_ProceduralAction_RecipeSpec{ECk_ProceduralAction_Recipe::Blank, Hand}, Draft);
        auto Clip = ck::procedural_action::FCompiledClip{};
        TestEqual(TEXT("Blank recipe is valid"), Report.Get_Status(), Valid);
        TestTrue(TEXT("Blank recipe compiles"), Compile(Draft, Clip));
        TestTrue(TEXT("Blank is one keyless Motion phase of the execute duration"), Draft.Get_Phases().Num() == 1
            && Draft.Get_Phases()[0].Get_Id() == FName{TEXT("Motion")}
            && FMath::IsNearlyEqual(Draft.Get_Phases()[0].Get_Duration().Get_Seconds(), 0.2)
            && Draft.Get_Phases()[0].Get_BodyKeys().IsEmpty() && Draft.Get_Phases()[0].Get_LimbTracks().IsEmpty());
    }
    {
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        auto Spec = FCk_ProceduralAction_RecipeSpec{ECk_ProceduralAction_Recipe::BodyLean, TArray<FName>{}};
        Spec.Set_AmplitudeCm(40.0f);
        const auto Report = UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(Spec, Draft);
        auto Clip = ck::procedural_action::FCompiledClip{};
        TestEqual(TEXT("BodyLean recipe is valid"), Report.Get_Status(), Valid);
        TestTrue(TEXT("BodyLean recipe compiles"), Compile(Draft, Clip));
        TestTrue(TEXT("BodyLean enables the identity-offset body"), Draft.Get_BodyChannel().Get_Enabled()
            && Draft.Get_BodyChannel().Get_InitialSource() == ECk_ProceduralAction_InitialSource::IdentityOffset);
        if (TestEqual(TEXT("BodyLean has Prepare, Execute, Recover"), Draft.Get_Phases().Num(), 3))
        {
            TestTrue(TEXT("Pullback is a quarter amplitude"), Draft.Get_Phases()[0].Get_BodyKeys().Last().Get_Translation().Equals(FVector{-10.0, 0.0, 0.0}));
            TestTrue(TEXT("Lunge reaches the amplitude"), Draft.Get_Phases()[1].Get_BodyKeys().Last().Get_Translation().Equals(FVector{40.0, 0.0, 0.0}));
            TestTrue(TEXT("Recover returns to identity"), Draft.Get_Phases()[2].Get_BodyKeys().Last().Get_Translation().IsZero());
            TestTrue(TEXT("Every phase takes the spec easing"), Draft.Get_Phases().FilterByPredicate([](const FCk_ProceduralAction_Phase& InPhase)
            { return InPhase.Get_Easing() == ECk_ProceduralAction_Easing::InOutCubic; }).Num() == 3);
        }
    }
    {
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        const auto Report = UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(
            FCk_ProceduralAction_RecipeSpec{ECk_ProceduralAction_Recipe::Reach, Hand}, Draft);
        auto Clip = ck::procedural_action::FCompiledClip{};
        TestEqual(TEXT("Reach recipe is valid"), Report.Get_Status(), Valid);
        TestTrue(TEXT("Reach without an authored rest says so"), Report.Get_Diagnostics().ContainsByPredicate(
            [](const FCk_ProceduralAction_Diagnostic& InDiagnostic)
            { return InDiagnostic.Get_Severity() == ECk_ProceduralAction_DiagnosticSeverity::Warning
                && InDiagnostic.Get_Code() == FName{TEXT("RecipeRest")}; }));
        TestTrue(TEXT("Reach recipe compiles"), Compile(Draft, Clip));
        if (TestEqual(TEXT("Reach has three phases"), Draft.Get_Phases().Num(), 3))
        {
            const auto& Prepare = Draft.Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys();
            const auto& Execute = Draft.Get_Phases()[1].Get_LimbTracks()[0].Get_PositionKeys();
            const auto& Recover = Draft.Get_Phases()[2].Get_LimbTracks()[0].Get_PositionKeys();
            TestTrue(TEXT("Prepare joins Execute without a jump"), Prepare.Last().Get_Value().Equals(Execute[0].Get_Value()));
            TestTrue(TEXT("Execute joins Recover without a jump"), Execute.Last().Get_Value().Equals(Recover[0].Get_Value()));
            TestTrue(TEXT("Execute reaches the amplitude along the direction"), Execute.Last().Get_Value().Equals(FVector{20.0, 0.0, 0.0}));
            TestTrue(TEXT("Phase-end tangents are zero so phases meet at rest"), Execute.Last().Get_ArriveTangent().IsZero()
                && Recover[0].Get_LeaveTangent().IsZero());
            TestEqual(TEXT("Reach is Smooth"), Draft.Get_Phases()[1].Get_LimbTracks()[0].Get_Interpolation(),
                ECk_ProceduralAction_Interpolation::Smooth);
        }
    }
    {
        auto Draft = FCk_ProceduralAction_ClipDraft{};
        const auto Report = UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(
            FCk_ProceduralAction_RecipeSpec{ECk_ProceduralAction_Recipe::Sweep, Hand}, Draft);
        auto Clip = ck::procedural_action::FCompiledClip{};
        TestEqual(TEXT("Sweep recipe is valid"), Report.Get_Status(), Valid);
        TestTrue(TEXT("Sweep recipe compiles"), Compile(Draft, Clip));
        if (TestEqual(TEXT("Sweep is one Execute phase"), Draft.Get_Phases().Num(), 1))
        {
            const auto& Keys = Draft.Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys();
            if (TestEqual(TEXT("Sweep has start, apex and end"), Keys.Num(), 3))
            {
                TestTrue(TEXT("Apex is the direction turned 90 degrees about up, at mid-time"),
                    Keys[1].Get_Value().Equals(FVector{0.0, 20.0, 0.0}, 1.0e-9) && Keys[1].Get_Time().Get_Value() == 0.5);
                TestTrue(TEXT("Apex tangent is the Catmull-Rom chord"),
                    Keys[1].Get_LeaveTangent().Equals(Keys[2].Get_Value() - Keys[0].Get_Value(), 1.0e-9)
                    && Keys[1].Get_ArriveTangent().Equals(Keys[1].Get_LeaveTangent()));
            }
        }
    }
    {
        auto Draft = MakeBodyDraft();
        const auto Report = UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(FCk_ProceduralAction_RecipeSpec{
            ECk_ProceduralAction_Recipe::Reach, TArray<FName>{FName{TEXT("Left")}, FName{TEXT("Right")}}}, Draft);
        TestEqual(TEXT("Reach with two participants rejects"), Report.Get_Status(), Invalid);
        TestTrue(TEXT("Rejected recipe resets the output draft"), DraftsEqual(Draft, FCk_ProceduralAction_ClipDraft{}));
    }
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderContentLimitsTest,
    "Ck.ProceduralAnimation.Action.Builder.ContentLimitsRejectBeforeAllocation",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderContentLimitsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    namespace limits = ck::procedural_action::content_limits;

    auto Phased = FCk_ProceduralAction_ClipDraft{};
    auto Phases = TArray<FCk_ProceduralAction_Phase>{};
    for (auto Index = 0; Index < limits::MaxPhases; ++Index)
    { Phases.Emplace(FName{*FString::Printf(TEXT("P%d"), Index)}, FCk_Time{0.1}); }
    Phased.Set_Phases(Phases);
    const auto PhaseCeiling = UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Phased,
        FCk_ProceduralAction_Phase{FName{TEXT("OneTooMany")}, FCk_Time{0.1}});
    TestEqual(TEXT("Phase ceiling rejects"), PhaseCeiling.Get_Status(), Invalid);
    TestTrue(TEXT("Phase ceiling names the limit"), HasDiagnostic(PhaseCeiling, TEXT("ContentLimit"), TEXT("Phases")));
    TestEqual(TEXT("No phase was appended"), Phased.Get_Phases().Num(), limits::MaxPhases);

    auto Slotted = FCk_ProceduralAction_ClipDraft{};
    auto Slots = TArray<FCk_ProceduralAction_Slot>{};
    auto SlotIds = TArray<FName>{};
    for (auto Index = 0; Index <= limits::MaxSlots; ++Index)
    { SlotIds.Add(FName{*FString::Printf(TEXT("S%d"), Index)}); }
    for (auto Index = 0; Index < limits::MaxSlots; ++Index)
    { Slots.Emplace(SlotIds[Index]); }
    Slotted.Set_Slots(Slots);
    const auto SlotCeiling = UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Slotted, FCk_ProceduralAction_Slot{SlotIds.Last()});
    TestEqual(TEXT("Slot ceiling rejects"), SlotCeiling.Get_Status(), Invalid);
    TestTrue(TEXT("Slot ceiling names the limit"), HasDiagnostic(SlotCeiling, TEXT("ContentLimit"), TEXT("Slots")));
    TestEqual(TEXT("No slot was appended"), Slotted.Get_Slots().Num(), limits::MaxSlots);

    auto Recipe = FCk_ProceduralAction_ClipDraft{};
    const auto RecipeCeiling = UCk_Utils_ProceduralActionBuilder_UE::Make_FromRecipe(
        FCk_ProceduralAction_RecipeSpec{ECk_ProceduralAction_Recipe::Blank, SlotIds}, Recipe);
    TestEqual(TEXT("Recipe participant ceiling rejects"), RecipeCeiling.Get_Status(), Invalid);
    TestTrue(TEXT("Recipe ceiling names the limit"), HasDiagnostic(RecipeCeiling, TEXT("ContentLimit"), TEXT("Recipe.Slots")));
    TestTrue(TEXT("No recipe draft escapes"), DraftsEqual(Recipe, FCk_ProceduralAction_ClipDraft{}));

    auto Keyed = MakeBodyDraft();
    auto Lean = Keyed.Get_Phases()[0];
    auto Keys = TArray<FCk_ProceduralAction_TransformKey>{};
    Keys.Init(FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
        static_cast<int32>(limits::MaxKeys));
    Lean.Set_BodyKeys(Keys);
    Keyed.Set_Phases(TArray<FCk_ProceduralAction_Phase>{Lean});
    const auto KeyBefore = Keyed;
    const auto KeyCeiling = UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Keyed, FName{TEXT("Lean")},
        MakeBodyPose(0.5, FVector::ZeroVector));
    TestEqual(TEXT("Total key ceiling rejects"), KeyCeiling.Get_Status(), Invalid);
    TestTrue(TEXT("Key ceiling names the compiler's key count"), HasDiagnostic(KeyCeiling, TEXT("ContentLimit"), TEXT("Phases.Keys")));
    TestTrue(TEXT("No key was inserted"), DraftsEqual(KeyBefore, Keyed));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionBuilderWriteIntoTest,
    "Ck.ProceduralAnimation.Action.Builder.WriteIntoAcceptsUncompilableDraft",
    ck_test_procedural_action_builder::TestFlags)

auto
FCkProceduralActionBuilderWriteIntoTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_builder;
    auto Unkeyed = MakeBodyDraft();
    if (NOT TestEqual(TEXT("A mid-phase key is a valid builder edit"), UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Unkeyed,
        FName{TEXT("Lean")}, MakeBodyPose(0.5, FVector{0.0, 0.0, 4.0})).Get_Status(), Valid))
    { return false; }

    auto* Written = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    const auto WriteReport = UCk_Utils_ProceduralActionBuilder_UE::Write_Into(Unkeyed, Written);
    TestEqual(TEXT("Write_Into accepts a draft that does not compile yet"), WriteReport.Get_Status(), Valid);
    TestTrue(TEXT("Write_Into copied the authored phases"), ArraysEqual(Unkeyed.Get_Phases(), Written->Get_Phases()));
    TestTrue(TEXT("Write_Into copied the body channel"), FCk_ProceduralAction_BodyChannel::StaticStruct()->CompareScriptStruct(
        &Unkeyed.Get_BodyChannel(), &Written->Get_BodyChannel(), PPF_None));
    const auto Validation = UCk_Utils_ProceduralAction_UE::Validate_Data(Written);
    TestEqual(TEXT("The written clip still fails validation"), Validation.Get_Status(), Invalid);
    TestTrue(TEXT("Validation names the missing endpoint keys"), Validation.Get_Diagnostics().ContainsByPredicate(
        [](const FCk_ProceduralAction_Diagnostic& InDiagnostic) { return InDiagnostic.Get_Code() == FName{TEXT("KeyEndpoints")}; }));

    auto Seeded = MakeBodyDraft();
    UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Seeded, FName{TEXT("Lean")}, MakeBodyPose(0.0, FVector::ZeroVector));
    UCk_Utils_ProceduralActionBuilder_UE::Add_PoseKey(Seeded, FName{TEXT("Lean")}, MakeBodyPose(1.0, FVector{10.0, 0.0, 0.0}));
    auto* Built = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    if (NOT TestEqual(TEXT("Second destination seeded by a valid build"),
        UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Seeded, Built).Get_Status(), Valid))
    { return false; }
    const auto PhasesBefore = Built->Get_Phases();
    const auto Refused = UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Unkeyed, Built);
    TestEqual(TEXT("Build_Into still refuses the same draft"), Refused.Get_Status(), Invalid);
    TestTrue(TEXT("Build_Into names the missing endpoint keys"), HasDiagnostic(Refused, TEXT("KeyEndpoints"), TEXT("Phases[0].BodyKeys")));
    TestTrue(TEXT("The refused destination is unchanged"), ArraysEqual(PhasesBefore, Built->Get_Phases()));

    TestEqual(TEXT("Write_Into rejects a null destination"),
        UCk_Utils_ProceduralActionBuilder_UE::Write_Into(Unkeyed, nullptr).Get_Status(), Invalid);
    auto* WrongVersion = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    // The version is loaded and migrated, never set; an older asset is the only way to hold another one.
    const auto* VersionProperty = CastField<FIntProperty>(
        UCk_ProceduralAction_Data::StaticClass()->FindPropertyByName(FName{TEXT("_CompatibilityVersion")}));
    if (NOT TestNotNull(TEXT("the compatibility version is a reflected int"), VersionProperty))
    { return false; }
    VersionProperty->SetPropertyValue_InContainer(WrongVersion, 1);
    const auto VersionReport = UCk_Utils_ProceduralActionBuilder_UE::Write_Into(Unkeyed, WrongVersion);
    TestEqual(TEXT("Write_Into rejects an unknown compatibility version"), VersionReport.Get_Status(), Invalid);
    TestTrue(TEXT("The version rejection names the field"),
        HasDiagnostic(VersionReport, TEXT("Version"), TEXT("Destination.CompatibilityVersion")));
    TestTrue(TEXT("The version rejection expects the current version"), VersionReport.Get_Diagnostics().ContainsByPredicate(
        [](const FCk_ProceduralAction_Diagnostic& InDiagnostic)
        {
            return InDiagnostic.Get_Code() == FName{TEXT("Version")}
                && InDiagnostic.Get_Expected() == FString::FromInt(UCk_ProceduralAction_Data::CurrentCompatibilityVersion)
                && InDiagnostic.Get_Actual() == TEXT("1");
        }));
    TestTrue(TEXT("The rejected destination keeps no phases"), WrongVersion->Get_Phases().IsEmpty());
    return true;
}
#endif
