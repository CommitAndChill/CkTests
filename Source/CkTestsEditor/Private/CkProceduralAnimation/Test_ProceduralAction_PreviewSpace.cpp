#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionPreviewSpace.h"

namespace ck_test_procedural_action_preview_space
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
        MakeSlot(
            ECk_ProceduralAction_TrackSpace InSpace)
        -> ck::procedural_action::FCompiledSlot
    {
        return ck::procedural_action::FCompiledSlot{FName{TEXT("Arm")},
            ECk_ProceduralAction_InitialSource::CapturedAcceptedPose, InSpace};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewSpace_TrackConversion,
    "Ck.ProceduralAnimation.Action.Editor.PreviewSpace.TrackConversion",
    ck_test_procedural_action_preview_space::TestFlags)

auto
    FCkTest_ProceduralAction_PreviewSpace_TrackConversion::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_preview_space;
    auto* Config = NewObject<UCk_ProceduralActionPreview_Config>();
    Config->Set_RootFrame(FTransform{FVector{100.0, 0.0, 0.0}});
    Config->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{FName{TEXT("Arm")}, FName{TEXT("PhysicalArm")}}});
    Config->Set_Layout(FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{
        FCk_ProceduralLimb_Layout{FName{TEXT("PhysicalArm")}, FTransform::Identity,
            TArray<float>{40.0f, 60.0f}, FTransform{FVector{20.0, 0.0, 0.0}}, FVector::UpVector}}});
    auto Frame = FTransform::Identity;
    auto Scale = 0.0f;
    TestTrue(TEXT("body local frame resolves"),
        ck::procedural_action_editor::ResolveSlotFrame(*Config,
            MakeSlot(ECk_ProceduralAction_TrackSpace::BodyLocal), FTransform::Identity, Frame, Scale));
    TestTrue(TEXT("body local coordinate converts into world"),
        ck::procedural_action_editor::ToWorldPosition(Frame, Scale, FVector{5.0, 0.0, 0.0})
            .Equals(FVector{105.0, 0.0, 0.0}));
    TestTrue(TEXT("rest normalized frame resolves through explicit slot mapping"),
        ck::procedural_action_editor::ResolveSlotFrame(*Config,
            MakeSlot(ECk_ProceduralAction_TrackSpace::LimbRestNormalized), Config->Get_RootFrame(), Frame, Scale));
    TestEqual(TEXT("normalization uses canonical chain length"), Scale, 100.0f);
    const auto World = ck::procedural_action_editor::ToWorldPosition(Frame, Scale, FVector{0.5, 0.0, 0.0});
    TestTrue(TEXT("normalized position uses rest endpoint frame"), World.Equals(FVector{170.0, 0.0, 0.0}));
    TestTrue(TEXT("gizmo inverse conversion returns track coordinate"),
        ck::procedural_action_editor::ToTrackPosition(Frame, Scale, World).Equals(FVector{0.5, 0.0, 0.0}));
    return NOT HasAnyErrors();
}

#endif
