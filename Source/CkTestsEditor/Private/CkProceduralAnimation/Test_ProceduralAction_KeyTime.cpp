#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringCommands.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"

#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_key_time
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
        MakeBodyClip(
            const TArray<FCk_ProceduralAction_Phase>& InPhases)
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
        auto Body = FCk_ProceduralAction_BodyChannel{};
        Body.Set_Enabled(true);
        Body.Set_InitialSource(ECk_ProceduralAction_InitialSource::IdentityOffset);
        Clip->Set_BodyChannel(Body);
        Clip->Set_Phases(InPhases);
        return Clip;
    }

    auto
        MakeBodyPhase(
            FName InId,
            FCk_Time InDuration,
            ECk_ProceduralAction_Easing InEasing)
        -> FCk_ProceduralAction_Phase
    {
        auto Phase = FCk_ProceduralAction_Phase{InId, InDuration};
        Phase.Set_Easing(InEasing);
        Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{100.0, 0.0, 0.0}, FQuat::Identity}});
        return Phase;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_KeyTime_EasedCaptureReachesTheKeyAtItsCaptureTime,
    "Ck.ProceduralAnimation.Action.Editor.KeyTime.EasedCaptureReachesTheKeyAtItsCaptureTime",
    ck_test_procedural_action_key_time::TestFlags)

auto
    FCkTest_ProceduralAction_KeyTime_EasedCaptureReachesTheKeyAtItsCaptureTime::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_key_time;
    const auto Ease = FName{TEXT("Ease")};
    auto Clip = MakeBodyClip(TArray<FCk_ProceduralAction_Phase>{
        MakeBodyPhase(Ease, FCk_Time{1.0}, ECk_ProceduralAction_Easing::InQuad)});
    auto Session = FCk_ProceduralActionAuthoringSession{Clip.Get()};

    const auto CaptureTime = FCk_Time{0.5};
    auto Key = FCk_ProceduralActionKeyTime{};
    if (NOT TestTrue(TEXT("the capture time lies in the eased section"), Session.ElapsedToKey(CaptureTime, Key)))
    { return false; }
    TestEqual(TEXT("the key belongs to the section"), Key.Get_PhaseId(), Ease);
    TestTrue(TEXT("the key time is the eased progress (InQuad at half the section is a quarter)"),
        FMath::IsNearlyEqual(Key.Get_KeyTime().Get_Value(), 0.25, 1.0e-6));
    auto Back = FCk_Time{};
    TestTrue(TEXT("the key maps back to the capture time"),
        Session.KeyToElapsed(Ease, Key.Get_KeyTime(), Back) && FMath::IsNearlyEqual(Back.Get_Seconds(), 0.5, 1.0e-4));

    const auto Captured = FVector{40.0, 0.0, 0.0};
    auto Pose = FCk_ProceduralAction_PoseKey{Key.Get_KeyTime()};
    Pose.Set_BodyMode(ECk_ProceduralAction_ValueMode::Set);
    Pose.Set_Body(FTransform{Captured});
    auto Error = FString{};
    if (NOT TestTrue(TEXT("the pose is recorded at the key time"), FCk_ProceduralActionAuthoringCommands{Clip.Get()}.RecordPose(
        Ease, Pose, NAME_None, Error)))
    { AddInfo(Error); return false; }

    Session.Refresh();
    auto Evaluated = ck::procedural_action::FEvaluatedPose{};
    if (NOT TestTrue(TEXT("the captured clip evaluates at the capture time"), Session.EvaluateAt(CaptureTime, Evaluated)))
    { return false; }
    TestTrue(TEXT("the captured pose is reached at the elapsed time it was captured at"),
        Evaluated.Get_Body().GetTranslation().Equals(Captured, 1.0e-3));
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_KeyTime_SectionEndBelongsToTheEndingSection,
    "Ck.ProceduralAnimation.Action.Editor.KeyTime.SectionEndBelongsToTheEndingSection",
    ck_test_procedural_action_key_time::TestFlags)

auto
    FCkTest_ProceduralAction_KeyTime_SectionEndBelongsToTheEndingSection::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_key_time;
    const auto First = FName{TEXT("First")};
    const auto Second = FName{TEXT("Second")};
    auto Clip = MakeBodyClip(TArray<FCk_ProceduralAction_Phase>{
        MakeBodyPhase(First, FCk_Time{0.4}, ECk_ProceduralAction_Easing::OutCubic),
        MakeBodyPhase(Second, FCk_Time{0.6}, ECk_ProceduralAction_Easing::OutQuad)});
    auto Session = FCk_ProceduralActionAuthoringSession{Clip.Get()};

    auto Key = FCk_ProceduralActionKeyTime{};
    TestTrue(TEXT("time 0 is the first section's start"), Session.ElapsedToKey(FCk_Time{}, Key)
        && Key.Get_PhaseId() == First && Key.Get_KeyTime().Get_Value() == 0.0);
    TestTrue(TEXT("a section's end is that section's key at 1, never the next section's start"),
        Session.ElapsedToKey(FCk_Time{0.4}, Key) && Key.Get_PhaseId() == First
            && Key.Get_KeyTime().Get_Value() == 1.0);
    TestTrue(TEXT("the section's easing sets the key time inside it (OutQuad at half the section is three quarters)"),
        Session.ElapsedToKey(FCk_Time{0.7}, Key)
        && Key.Get_PhaseId() == Second && FMath::IsNearlyEqual(Key.Get_KeyTime().Get_Value(), 0.75, 1.0e-3));
    TestTrue(TEXT("the clip's end is the last section's key at 1"), Session.ElapsedToKey(FCk_Time{1.0}, Key)
        && Key.Get_PhaseId() == Second && Key.Get_KeyTime().Get_Value() == 1.0);
    return NOT HasAnyErrors();
}

#endif
