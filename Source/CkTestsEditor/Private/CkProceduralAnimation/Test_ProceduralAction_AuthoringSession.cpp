#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_Factory.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"

#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_authoring
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringSession_AssetFactories,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringSession.AssetFactories",
    ck_test_procedural_action_authoring::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringSession_AssetFactories::
    RunTest(
        const FString&)
    -> bool
{
    auto* ClipFactory = NewObject<UCk_ProceduralAction_Factory>(GetTransientPackage());
    auto* PreviewFactory = NewObject<UCk_ProceduralActionPreview_Factory>(GetTransientPackage());
    auto* Clip = ClipFactory->FactoryCreateNew(UCk_ProceduralAction_Data::StaticClass(),
        GetTransientPackage(), FName(TEXT("CreatedActionClip")), RF_Transient, nullptr, GWarn);
    auto* Preview = PreviewFactory->FactoryCreateNew(UCk_ProceduralActionPreview_Config::StaticClass(),
        GetTransientPackage(), FName(TEXT("CreatedActionPreview")), RF_Transient, nullptr, GWarn);
    return TestTrue(TEXT("clip factory creates the authored runtime asset"),
            Clip != nullptr && Clip->IsA<UCk_ProceduralAction_Data>())
        && TestTrue(TEXT("preview factory creates a separate editor asset"),
            Preview != nullptr && Preview->IsA<UCk_ProceduralActionPreview_Config>());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringSession_DraftAndPlayback,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringSession.DraftAndPlayback",
    ck_test_procedural_action_authoring::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringSession_DraftAndPlayback::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
    auto Session = FCk_ProceduralActionAuthoringSession{Clip.Get()};
    if (NOT TestFalse(TEXT("empty draft cannot play"), Session.Get_CanPlay())
        || NOT TestFalse(TEXT("empty draft does not compile"), Session.Get_IsCompiled())
        || NOT TestTrue(TEXT("empty draft has field diagnostics"), Session.Get_Diagnostics().Num() > 0))
    { return false; }

    auto Body = FCk_ProceduralAction_BodyChannel{};
    Body.Set_Enabled(true);
    Clip->Set_BodyChannel(Body);
    auto Phase = FCk_ProceduralAction_Phase{FName(TEXT("Reach")), FCk_Time{1.0}};
    Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{10.0, 0.0, 0.0}, FQuat::Identity}});
    Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    Session.Refresh();
    if (NOT TestTrue(TEXT("edited clip compiles"), Session.Get_IsCompiled() && Session.Get_CanPlay()))
    { return false; }

    auto Pose = ck::procedural_action::FEvaluatedPose{};
    if (NOT TestTrue(TEXT("authored pose evaluates at a scrubbed time"),
        Session.EvaluateAt(FCk_Time{0.5}, Pose) && FMath::IsNearlyEqual(Pose.Get_Body().GetTranslation().X, 5.0)))
    { return false; }
    if (NOT TestTrue(TEXT("authored pose reaches the final key at the clip's end"),
        Session.EvaluateAt(FCk_Time{1.0}, Pose) && FMath::IsNearlyEqual(Pose.Get_Body().GetTranslation().X, 10.0)))
    { return false; }
    TestFalse(TEXT("a time past the clip does not evaluate"), Session.EvaluateAt(FCk_Time{1.5}, Pose));

    Phase.Set_Duration(FCk_Time{0.0});
    Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    Session.Refresh();
    return TestFalse(TEXT("invalid edit removes playable revision"), Session.Get_CanPlay())
        && TestFalse(TEXT("an invalid edit evaluates nothing"), Session.EvaluateAt(FCk_Time{0.5}, Pose))
        && TestTrue(TEXT("invalid edit reports field diagnostics"), Session.Get_Diagnostics().Num() > 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringSession_AuthoredRestFromTopology,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringSession.AuthoredRestFromTopology",
    ck_test_procedural_action_authoring::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringSession_AuthoredRestFromTopology::
    RunTest(
        const FString&)
    -> bool
{
    const auto SlotId = FName{TEXT("ReachSlot")};
    const auto LimbId = FName{TEXT("PhysicalArm")};
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
    auto Preview = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
        NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
    auto Slot = FCk_ProceduralAction_Slot{SlotId};
    Slot.Set_InitialSource(ECk_ProceduralAction_InitialSource::AuthoredRest);
    Slot.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
    Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
    auto Track = FCk_ProceduralAction_LimbTrack{SlotId};
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Hold")}, FCk_Time{0.5}};
    Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
    Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});

    const auto Root = FTransform{FVector{100.0, 0.0, 0.0}};
    Preview->Set_RootFrame(Root);
    Preview->Set_PresentationFrame(Root);
    Preview->Set_Layout(FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{
        FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
            TArray<float>{100.0f}, FTransform{FVector{20.0, 0.0, 0.0}}, FVector::UpVector}}});
    Preview->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{SlotId, LimbId}});
    auto PreviewBends = TMap<FName, FVector>{};
    PreviewBends.Add(SlotId, FVector::UpVector);
    Preview->Set_InitialBendDirections(PreviewBends);
    auto Session = FCk_ProceduralActionAuthoringSession{Clip.Get()};
    Session.SetPreviewConfig(Preview.Get());
    if (NOT TestTrue(TEXT("non-explicit authored rest plays without an initial-endpoint map"),
        Preview->Get_InitialLimbEndpoints().IsEmpty() && Session.Get_IsCompiled() && Session.Get_CanPlay()))
    { return false; }
    auto Pose = ck::procedural_action::FEvaluatedPose{};
    const auto StartPose = [&Session, &Pose]() { return Session.EvaluateAt(FCk_Time{}, Pose); };
    if (NOT TestTrue(TEXT("rest-normalized source resolves to zero offset from topology rest"),
        StartPose() && Pose.Get_Limbs().Num() == 1
            && Pose.Get_Limbs()[0].Get_Position().Equals(FVector::ZeroVector)))
    { return false; }
    if (NOT TestFalse(TEXT("topology rest ignores captured preview bend seed"),
        Pose.Get_Limbs()[0].Get_HasBend()))
    { return false; }

    Slot.Set_Space(ECk_ProceduralAction_TrackSpace::BodyLocal);
    Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
    Session.Refresh();
    if (NOT TestTrue(TEXT("body-local source resolves topology rest relative to root"),
        Session.Get_CanPlay() && StartPose() && Pose.Get_Limbs().Num() == 1
            && Pose.Get_Limbs()[0].Get_Position().Equals(FVector{20.0, 0.0, 0.0})))
    { return false; }

    Track.Set_BendKeys(TArray<FCk_ProceduralAction_VectorKey>{
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, FVector::RightVector},
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, FVector::RightVector}});
    Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
    Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    Session.Refresh();
    if (NOT TestTrue(TEXT("authored bend keys still override the rest initial source"),
        Session.Get_CanPlay() && StartPose() && Pose.Get_Limbs().Num() == 1
            && Pose.Get_Limbs()[0].Get_HasBend()
            && Pose.Get_Limbs()[0].Get_BendDirection().Equals(FVector::RightVector)))
    { return false; }
    Track.Set_BendKeys(TArray<FCk_ProceduralAction_VectorKey>{});
    Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
    Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});

    Preview->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{});
    Session.Refresh();
    if (NOT TestFalse(TEXT("unbound authored rest cannot silently use identity"), Session.Get_CanPlay()))
    { return false; }
    Preview->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{SlotId, LimbId}});

    Slot.Set_InitialSource(ECk_ProceduralAction_InitialSource::CapturedAcceptedPose);
    Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
    Session.Refresh();
    if (NOT TestTrue(TEXT("a bound captured slot previews from its limb's rest when no start is authored"), Session.Get_CanPlay()))
    { return false; }
    Preview->Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{});
    Session.Refresh();
    return TestFalse(TEXT("an unbound captured slot has no start to preview from"), Session.Get_CanPlay());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_AuthoringSession_AppearanceScale,
    "Ck.ProceduralAnimation.Action.Editor.AuthoringSession.AppearanceScale",
    ck_test_procedural_action_authoring::TestFlags)

auto
    FCkTest_ProceduralAction_AuthoringSession_AppearanceScale::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
        NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
    auto Preview = TStrongObjectPtr<UCk_ProceduralActionPreview_Config>{
        NewObject<UCk_ProceduralActionPreview_Config>(GetTransientPackage())};
    auto Body = FCk_ProceduralAction_BodyChannel{};
    Body.Set_Enabled(true);
    Clip->Set_BodyChannel(Body);
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Pulse")}, FCk_Time{0.5}};
    Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{1.0, 0.0, 0.0}, FQuat::Identity}});
    Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    auto Session = FCk_ProceduralActionAuthoringSession{Clip.Get()};
    Session.SetPreviewConfig(Preview.Get());
    auto Pose = ck::procedural_action::FEvaluatedPose{};
    if (NOT TestTrue(TEXT("unit-scale authored body pose is available"), Session.Get_CanPlay() && Session.EvaluateAt(FCk_Time{0.25}, Pose)))
    { return false; }
    const auto Before = Pose.Get_Body();
    Preview->Set_BodyPreviewMeshScale(FVector{1.5, 0.5, 2.0});
    Session.Refresh();
    if (NOT TestTrue(TEXT("visual-only scale preserves authored body pose"),
        Session.Get_CanPlay() && Session.EvaluateAt(FCk_Time{0.25}, Pose) && Pose.Get_Body().Equals(Before, 1.0e-4)))
    { return false; }
    Preview->Set_BodyPreviewMeshScale(FVector{1.0, 0.0, 1.0});
    Session.Refresh();
    if (NOT TestFalse(TEXT("zero visual scale disables preview playback"), Session.Get_CanPlay())
        || NOT TestTrue(TEXT("invalid visual scale has an actionable status"),
            Session.Get_StatusText(FCk_Time{}).ToString().Contains(TEXT("finite, nonzero"))))
    { return false; }
    Preview->Set_BodyPreviewMeshScale(FVector::OneVector);
    Session.Refresh();
    return TestTrue(TEXT("corrected visual scale restores authored playback"), Session.Get_CanPlay());
}

#endif
