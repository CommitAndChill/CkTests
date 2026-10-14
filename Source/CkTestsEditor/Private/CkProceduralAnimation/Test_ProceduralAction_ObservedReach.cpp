#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionPreviewSpace.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_EditorObservedReach,
    "Ck.ProceduralAnimation.Action.Editor.ObservedReach.SettledHipIgnoresPresentationAndScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkTest_ProceduralAction_EditorObservedReach::
    RunTest(
        const FString&)
    -> bool
{
    const auto Center = FVector{150.0, 10.0, 50.0};
    const auto Segment = FTransform{FQuat::Identity, Center, FVector{-2.0, 3.0, 0.5}};
    const auto Limb = FCk_ProceduralArticulation_LimbPose{FName{TEXT("Front")},
        FTransform::Identity, FTransform::Identity, TArray<FTransform>{Segment},
        0.0f, ECk_ProceduralRig_ChainState::Clear,
        ECk_ProceduralArticulation_ContactEvidence::None};
    const auto Root = FTransform{FQuat::Identity, FVector{700.0, -60.0, 0.0}};
    const auto PosedBody = FTransform{FQuat::Identity, FVector{900.0, 30.0, 0.0}};
    const auto Pose = FCk_ProceduralArticulation_Pose{Root, PosedBody, true,
        TArray<FCk_ProceduralArticulation_LimbPose>{Limb}};
    auto Hip = FVector::ZeroVector;
    TestTrue(TEXT("settled first segment yields hip"),
        ck::procedural_action_editor::TryGetObservedHip(Pose.Get_Limbs()[0], 100.0, Hip));
    TestTrue(TEXT("body presentation and mirrored part scale cannot move the settled hip"),
        Hip.Equals(FVector{100.0, 10.0, 50.0}, 0.001));
    TestFalse(TEXT("empty observation cannot claim a reach center"),
        ck::procedural_action_editor::TryGetObservedHip(
            FCk_ProceduralArticulation_LimbPose{}, 100.0, Hip));
    return NOT HasAnyErrors();
}

#endif
