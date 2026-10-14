#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"

#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_preview_subject
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
        MakeLeg(
            FName InId,
            const FVector& InDirection)
        -> FCk_ProceduralLeg_Spec
    {
        auto Chain = FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 60.0f}};
        Chain.Set_PoleLocal(InDirection * 40.0 + FVector{0.0, 0.0, 60.0});
        return FCk_ProceduralLeg_Spec{InId,
            FCk_ProceduralLeg_Placement{InDirection * 20.0, InDirection * 70.0 + FVector{0.0, 0.0, -65.0}}, Chain};
    }

    auto
        MakeWalkingClip(
            UCk_ProceduralRig_Data* InRig,
            UCk_ProceduralGait_Data* InGait)
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
        auto Subject = FCk_ProceduralAction_EditorSubject{};
        Subject.Set_WalkingRig(TSoftObjectPtr<UCk_ProceduralRig_Data>{InRig});
        Subject.Set_WalkingGait(TSoftObjectPtr<UCk_ProceduralGait_Data>{InGait});
        Subject.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
            FCk_ProceduralAction_SlotBinding{FName{TEXT("Claw")}, FName{TEXT("Leg0")}}});
        Clip->Set_EditorSubject(Subject);
        return Clip;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewSubject_DerivesThePreview,
    "Ck.ProceduralAnimation.Action.Editor.PreviewSubject.DerivesThePreview",
    ck_test_procedural_action_preview_subject::TestFlags)

auto
    FCkTest_ProceduralAction_PreviewSubject_DerivesThePreview::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_preview_subject;
    auto Rig = TStrongObjectPtr<UCk_ProceduralRig_Data>{NewObject<UCk_ProceduralRig_Data>(GetTransientPackage())};
    auto Gait = TStrongObjectPtr<UCk_ProceduralGait_Data>{NewObject<UCk_ProceduralGait_Data>(GetTransientPackage())};
    Rig->Set_Legs(TArray<FCk_ProceduralLeg_Spec>{MakeLeg(FName{TEXT("Leg0")}, FVector::ForwardVector),
        MakeLeg(FName{TEXT("Leg1")}, FVector::RightVector), MakeLeg(FName{TEXT("Leg2")}, FVector::BackwardVector),
        MakeLeg(FName{TEXT("Leg3")}, FVector::LeftVector)});
    auto Clip = MakeWalkingClip(Rig.Get(), Gait.Get());

    auto Walking = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{ck::procedural_action_editor::DerivePreviewConfig(*Clip)};
    if (NOT TestTrue(TEXT("a walking subject derives a transient preview"), Walking.IsValid() && Walking->HasAnyFlags(RF_Transient)))
    { return false; }
    TestEqual(TEXT("the preview walks"), Walking->Get_SubjectMode(), ECk_ProceduralActionPreview_SubjectMode::Walking);
    TestTrue(TEXT("the preview uses the subject's rig and gait"),
        Walking->Get_WalkingRig() == Rig.Get() && Walking->Get_WalkingGait() == Gait.Get());
    TestTrue(TEXT("the root stands at the rig's clearance"),
        Walking->Get_RootFrame().GetTranslation().Equals(FVector{0.0, 0.0, 65.0}));
    TestTrue(TEXT("the presentation starts on the root"), Walking->Get_PresentationFrame().Equals(Walking->Get_RootFrame()));
    TestTrue(TEXT("a floor 800 cm across with its top at Z 0"), Walking->Get_HasStaticFloor()
        && Walking->Get_StaticFloorHalfExtents().Equals(FVector{400.0, 400.0, 25.0})
        && Walking->Get_StaticFloorFrame().GetTranslation().Equals(FVector{0.0, 0.0, -25.0}));
    if (NOT TestEqual(TEXT("one limb binding per leg"), Walking->Get_LimbBindings().Num(), 4))
    { return false; }
    const auto& Front = Walking->Get_LimbBindings()[0];
    TestTrue(TEXT("a leg binding has a frame per segment, starting at the hip"), Front.Get_SegmentFrames().Num() == 2
        && Front.Get_SegmentFrames()[0].GetTranslation().Equals(FVector{20.0, 0.0, 65.0}));
    TestTrue(TEXT("the terminal frame is the rest foot"), Front.Get_TerminalFrame().GetTranslation().Equals(FVector{70.0, 0.0, 0.0}));
    TestEqual(TEXT("support counts all but two legs"),
        Walking->Get_WalkingLocomotion().Get_Support().Get_MinimumTrustedContacts(), 2);
    TestEqual(TEXT("support counts contacts"),
        Walking->Get_WalkingLocomotion().Get_Support().Get_Mode(), ECk_ProceduralLimbControl_SupportMode::CountOnly);
    TestTrue(TEXT("the clip's slot bindings bind the preview"), Walking->Get_SlotBindings().Num() == 1
        && Walking->Get_SlotBindings()[0].Get_LimbId() == FName{TEXT("Leg0")});

    auto Scenario = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
        NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
    Scenario->Set_StaticObstacles(TArray<FCk_ProceduralActionPreview_Obstacle>{FCk_ProceduralActionPreview_Obstacle{}});
    Scenario->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{FName{TEXT("Claw")}, FName{TEXT("Leg3")}}});
    auto Subject = Clip->Get_EditorSubject();
    Subject.Set_Scenario(TSoftObjectPtr<UObject>{Scenario.Get()});
    Clip->Set_EditorSubject(Subject);
    auto FromScenario = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{ck::procedural_action_editor::DerivePreviewConfig(*Clip)};
    TestTrue(TEXT("a scenario is copied, never shared"), FromScenario.IsValid() && FromScenario.Get() != Scenario.Get());
    TestEqual(TEXT("the scenario's authored fields replace the derived ones"), FromScenario->Get_StaticObstacles().Num(), 1);
    TestTrue(TEXT("the clip's slot bindings replace the scenario's"),
        FromScenario->Get_SlotBindings().Num() == 1 && FromScenario->Get_SlotBindings()[0].Get_LimbId() == FName{TEXT("Leg0")});

    auto BodyOnly = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
    auto Body = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{ck::procedural_action_editor::DerivePreviewConfig(*BodyOnly)};
    TestTrue(TEXT("an empty subject previews the body alone"), Body.IsValid()
        && Body->Get_SubjectMode() == ECk_ProceduralActionPreview_SubjectMode::Standalone
        && Body->Get_Layout().Get_Limbs().IsEmpty() && Body->Get_LimbBindings().IsEmpty());
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewSubject_LegacyPreviewBecomesTheSubject,
    "Ck.ProceduralAnimation.Action.Editor.PreviewSubject.LegacyPreviewBecomesTheSubject",
    ck_test_procedural_action_preview_subject::TestFlags)

auto
    FCkTest_ProceduralAction_PreviewSubject_LegacyPreviewBecomesTheSubject::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_preview_subject;
    auto Legacy = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
        NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
    auto* OwnedRig = NewObject<UCk_ProceduralRig_Data>(Legacy.Get(), FName{TEXT("WalkingRig")});
    auto* OwnedGait = NewObject<UCk_ProceduralGait_Data>(Legacy.Get(), FName{TEXT("WalkingGait")});
    OwnedRig->Set_Legs(TArray<FCk_ProceduralLeg_Spec>{MakeLeg(FName{TEXT("Leg0")}, FVector::ForwardVector)});
    Legacy->Set_SubjectMode(ECk_ProceduralActionPreview_SubjectMode::Walking);
    Legacy->Set_WalkingRig(OwnedRig);
    Legacy->Set_WalkingGait(OwnedGait);
    Legacy->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{FName{TEXT("Claw")}, FName{TEXT("Leg0")}}});

    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
    const auto Walking = ck::procedural_action_editor::MakeSubjectFromLegacyPreview(*Legacy, *Clip);
    const auto* CopiedRig = Walking.Get_WalkingRig().Get();
    TestTrue(TEXT("a rig living inside the legacy preview is copied into the clip"),
        CopiedRig != nullptr && CopiedRig != OwnedRig && CopiedRig->GetOuter() == Clip.Get()
            && CopiedRig->Get_Legs().Num() == 1);
    TestTrue(TEXT("so is its gait"), Walking.Get_WalkingGait().Get() != nullptr
        && Walking.Get_WalkingGait().Get()->GetOuter() == Clip.Get());
    TestTrue(TEXT("a walking preview leaves no scenario"), Walking.Get_Scenario().IsNull());
    TestEqual(TEXT("the slot bindings move to the clip"), Walking.Get_SlotBindings().Num(), 1);

    Legacy->Set_SubjectMode(ECk_ProceduralActionPreview_SubjectMode::Standalone);
    Legacy->Set_Layout(FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{
        FCk_ProceduralLimb_Layout{FName{TEXT("Arm")}, FTransform::Identity, TArray<float>{80.0f},
            FTransform{FVector{80.0, 0.0, 0.0}}, FVector::UpVector}}});
    const auto Dedicated = ck::procedural_action_editor::MakeSubjectFromLegacyPreview(*Legacy, *Clip);
    TestEqual(TEXT("a standalone preview gives its layout"), Dedicated.Get_Layout().Get_Limbs().Num(), 1);
    TestTrue(TEXT("and stays the scenario, so its appearance and initial pose survive"),
        Dedicated.Get_Scenario().Get() == Legacy.Get());
    TestTrue(TEXT("a dedicated subject has no rig"), Dedicated.Get_WalkingRig().IsNull());
    return NOT HasAnyErrors();
}

#endif
