#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionEvaluation.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"

#include "../../CkUnitTest_Common.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_test_procedural_action_carried_start
{
    const auto Hand = FName{TEXT("Hand")};
    const auto Reach = FName{TEXT("Reach")};
    const auto Return = FName{TEXT("Return")};

    auto
    MakeLimbClip()
        -> UCk_ProceduralAction_Data*
    {
        auto* Asset = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
        Asset->Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{Hand}});
        return Asset;
    }

    auto
    MakeTrack(
        const TArray<FCk_ProceduralAction_VectorKey>& InPositionKeys)
        -> FCk_ProceduralAction_LimbTrack
    {
        auto Track = FCk_ProceduralAction_LimbTrack{Hand};
        Track.Set_PositionKeys(InPositionKeys);
        return Track;
    }

    auto
    MakePhase(
        FName InId,
        const FCk_ProceduralAction_LimbTrack& InTrack)
        -> FCk_ProceduralAction_Phase
    {
        auto Phase = FCk_ProceduralAction_Phase{InId, FCk_Time{1.0}};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{InTrack});
        return Phase;
    }

    auto
    KeyAt(
        double InTime,
        const FVector& InValue)
        -> FCk_ProceduralAction_VectorKey
    { return FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{InTime}, InValue}; }

    auto
    HandAt(
        const ck::procedural_action::FCompiledClip& InClip,
        const ck::procedural_action::FInitialPose& InInitial,
        double InSeconds,
        ck::procedural_action::FLimbPose& OutLimb)
        -> bool
    {
        auto Pose = ck::procedural_action::FEvaluatedPose{};
        if (NOT ck::procedural_action::Evaluate(InClip, InInitial, FCk_Time{InSeconds}, Pose) || Pose.Get_Limbs().Num() != 1)
        { return false; }

        OutLimb = Pose.Get_Limbs()[0];
        return true;
    }

    auto
    HasEndpointDiagnostic(
        const TArray<ck::procedural_action::FDiagnostic>& InDiagnostics,
        const FString& InFieldSuffix)
        -> bool
    {
        return InDiagnostics.ContainsByPredicate([&](const ck::procedural_action::FDiagnostic& InDiagnostic)
        { return InDiagnostic.Get_Code() == FName{TEXT("KeyEndpoints")} && InDiagnostic.Get_FieldPath().EndsWith(InFieldSuffix); });
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionCarriedStartFromInitialTest,
    "Ck.ProceduralAnimation.Action.CarriedStart.TrackWithoutStartKeyMovesFromTheInitialPose",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionCarriedStartFromInitialTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_carried_start;
    auto* Asset = MakeLimbClip();
    auto Track = MakeTrack(TArray<FCk_ProceduralAction_VectorKey>{KeyAt(1.0, FVector{30.0, 0.0, 0.0})});
    const auto Yaw90 = FQuat{FVector::UpVector, FMath::DegreesToRadians(90.0)};
    Track.Set_OrientationKeys(TArray<FCk_ProceduralAction_OrientationKey>{
        FCk_ProceduralAction_OrientationKey{FCk_FloatRange_0to1{1.0}, Yaw90}});
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{MakePhase(Reach, Track)});

    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("A track with only its end key compiles"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics)))
    { return false; }

    auto Initial = ck::procedural_action::FInitialPose{};
    Initial.Get_Limbs().Add(Hand, FTransform{FVector{10.0, 0.0, 0.0}});
    auto Limb = ck::procedural_action::FLimbPose{};

    if (HandAt(Clip, Initial, 0.0, Limb))
    { TestTrue(TEXT("At the phase start the limb is where it was"), Limb.Get_Position().Equals(FVector{10.0, 0.0, 0.0}, 1.0e-6)); }
    else
    { AddError(TEXT("Evaluate failed at 0 s")); }

    if (HandAt(Clip, Initial, 0.5, Limb))
    {
        TestTrue(TEXT("Halfway the limb is halfway from where it was to the key"), Limb.Get_Position().Equals(FVector{20.0, 0.0, 0.0}, 1.0e-6));
        TestTrue(TEXT("Halfway the orientation is halfway from the initial one to the key"),
            Limb.Get_Orientation().Equals(FQuat{FVector::UpVector, FMath::DegreesToRadians(45.0)}, 1.0e-6));
    }
    else
    { AddError(TEXT("Evaluate failed at 0.5 s")); }

    if (HandAt(Clip, Initial, 1.0, Limb))
    {
        TestTrue(TEXT("At the phase end the limb is at the key"), Limb.Get_Position().Equals(FVector{30.0, 0.0, 0.0}, 1.0e-6));
        TestTrue(TEXT("At the phase end the orientation is the key"), Limb.Get_Orientation().Equals(Yaw90, 1.0e-6));
    }
    else
    { AddError(TEXT("Evaluate failed at 1 s")); }

    Initial.Get_Limbs().Add(Hand, FTransform{FVector{-50.0, 0.0, 0.0}});
    if (HandAt(Clip, Initial, 0.5, Limb))
    { TestTrue(TEXT("The same clip starts from a different initial pose"), Limb.Get_Position().Equals(FVector{-10.0, 0.0, 0.0}, 1.0e-6)); }
    else
    { AddError(TEXT("Evaluate failed at 0.5 s with the second initial pose")); }

    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionCarriedStartFromPreviousPhaseTest,
    "Ck.ProceduralAnimation.Action.CarriedStart.LaterPhaseMovesFromThePreviousPhaseEnd",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionCarriedStartFromPreviousPhaseTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_carried_start;
    auto* Asset = MakeLimbClip();
    const auto Out = MakePhase(Reach, MakeTrack(TArray<FCk_ProceduralAction_VectorKey>{
        KeyAt(0.0, FVector::ZeroVector), KeyAt(1.0, FVector{40.0, 0.0, 0.0})}));
    const auto Back = MakePhase(Return, MakeTrack(TArray<FCk_ProceduralAction_VectorKey>{KeyAt(1.0, FVector{0.0, 20.0, 0.0})}));
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Out, Back});

    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("The two-phase clip compiles"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics)))
    { return false; }

    auto Initial = ck::procedural_action::FInitialPose{};
    Initial.Get_Limbs().Add(Hand, FTransform::Identity);
    auto Limb = ck::procedural_action::FLimbPose{};
    if (HandAt(Clip, Initial, 1.5, Limb))
    {
        TestTrue(TEXT("Halfway through the second phase the limb is between the first phase's end and its key"),
            Limb.Get_Position().Equals(FVector{20.0, 10.0, 0.0}, 1.0e-6));
    }
    else
    { AddError(TEXT("Evaluate failed at 1.5 s")); }

    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionCarriedStartBodyTest,
    "Ck.ProceduralAnimation.Action.CarriedStart.BodyWithoutStartKeyMovesFromItsStart",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionCarriedStartBodyTest::RunTest(
    const FString&)
    -> bool
{
    auto* Asset = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    auto Body = FCk_ProceduralAction_BodyChannel{};
    Body.Set_Enabled(true);
    Body.Set_InitialSource(ECk_ProceduralAction_InitialSource::IdentityOffset);
    Asset->Set_BodyChannel(Body);
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Lean")}, FCk_Time{1.0}};
    Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{10.0, 0.0, 0.0}, FQuat::Identity}});
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});

    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("A body phase with only its end key compiles"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics)))
    { return false; }

    const auto Initial = ck::procedural_action::FInitialPose{};
    auto Pose = ck::procedural_action::FEvaluatedPose{};
    TestTrue(TEXT("Halfway the body is halfway from its start to the key"),
        ck::procedural_action::Evaluate(Clip, Initial, FCk_Time{0.5}, Pose) && Pose.Get_Body().GetTranslation().Equals(FVector{5.0, 0.0, 0.0}, 1.0e-6));
    TestTrue(TEXT("At the end the body is at its only key"),
        ck::procedural_action::Evaluate(Clip, Initial, FCk_Time{1.0}, Pose) && Pose.Get_Body().GetTranslation().Equals(FVector{10.0, 0.0, 0.0}, 1.0e-6));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionCarriedStartLimitsTest,
    "Ck.ProceduralAnimation.Action.CarriedStart.EndKeyAndBendStartKeyStayRequired",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionCarriedStartLimitsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_carried_start;
    auto* Asset = MakeLimbClip();
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{MakePhase(Reach, MakeTrack(
        TArray<FCk_ProceduralAction_VectorKey>{KeyAt(0.5, FVector{10.0, 0.0, 0.0})}))});
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    TestFalse(TEXT("A position track without its end key is rejected"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics));
    TestTrue(TEXT("The diagnostic names the position keys"), HasEndpointDiagnostic(Diagnostics, TEXT("PositionKeys")));

    auto Bend = MakeTrack(TArray<FCk_ProceduralAction_VectorKey>{KeyAt(1.0, FVector{10.0, 0.0, 0.0})});
    Bend.Set_BendKeys(TArray<FCk_ProceduralAction_VectorKey>{KeyAt(1.0, FVector::UpVector)});
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{MakePhase(Reach, Bend)});
    Diagnostics.Reset();
    TestFalse(TEXT("A bend track without its start key is rejected"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics));
    TestTrue(TEXT("The diagnostic names the bend keys"), HasEndpointDiagnostic(Diagnostics, TEXT("BendKeys")));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionCarriedStartMirrorTest,
    "Ck.ProceduralAnimation.Action.CarriedStart.MirroredTrackKeepsItsMissingStartKey",
    ck::tests::kCkUnitTestFlags)

auto
FCkProceduralActionCarriedStartMirrorTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_carried_start;
    const auto Left = FName{TEXT("Left")};
    const auto Right = FName{TEXT("Right")};
    auto Draft = FCk_ProceduralAction_ClipDraft{};
    UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft, FCk_ProceduralAction_Phase{Reach, FCk_Time{1.0}});
    UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft, FCk_ProceduralAction_Slot{Right});
    UCk_Utils_ProceduralActionBuilder_UE::Add_Slot(Draft, FCk_ProceduralAction_Slot{Left});
    if (NOT TestEqual(TEXT("A key at the phase end only is a valid builder edit"),
        UCk_Utils_ProceduralActionBuilder_UE::Add_LimbKey(Draft, Reach, FCk_FloatRange_0to1{1.0},
            FCk_ProceduralAction_LimbPoseTarget{Right, FVector{70.0, 55.0, 85.0}}).Get_Status(), ECk_ProceduralAction_ReportStatus::Valid))
    { return false; }

    TestEqual(TEXT("The track mirrors"), UCk_Utils_ProceduralActionBuilder_UE::Mirror_Slot(Draft,
        FCk_ProceduralAction_MirrorSpec{Right, Left, ECk_ProceduralAction_MirrorPlane::XZ}).Get_Status(), ECk_ProceduralAction_ReportStatus::Valid);

    auto* Built = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    if (NOT TestEqual(TEXT("The mirrored draft builds"), UCk_Utils_ProceduralActionBuilder_UE::Build_Into(Draft, Built).Get_Status(),
        ECk_ProceduralAction_ReportStatus::Valid))
    { return false; }

    const auto* Mirrored = Built->Get_Phases()[0].Get_LimbTracks().FindByPredicate(
        [&](const FCk_ProceduralAction_LimbTrack& InTrack) { return InTrack.Get_Slot() == Left; });
    if (NOT TestNotNull(TEXT("The destination track exists"), Mirrored))
    { return false; }

    TestEqual(TEXT("The destination has the one end key"), Mirrored->Get_PositionKeys().Num(), 1);
    TestTrue(TEXT("The destination key is the source reflected across XZ at the same time"), Mirrored->Get_PositionKeys().Num() == 1
        && Mirrored->Get_PositionKeys()[0].Get_Time().Get_Value() == 1.0
        && Mirrored->Get_PositionKeys()[0].Get_Value().Equals(FVector{70.0, -55.0, 85.0}, 1.0e-6));
    return NOT HasAnyErrors();
}
#endif
