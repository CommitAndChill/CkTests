#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionEvaluation.h"
#include "CkProceduralAnimation/Core/CkProceduralActionProgress.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"

#include "CkTween/CkTween_Easing_Utils.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_test_procedural_action_progress
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const auto BodyPulsePath = FString{TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA")};
    const auto LimbNodPath = FString{TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA")};

    auto
    ToTween(
        ECk_ProceduralAction_Easing InEasing)
        -> ECk_TweenEasing
    {
        switch (InEasing)
        {
        case ECk_ProceduralAction_Easing::InQuad: return ECk_TweenEasing::InQuad;
        case ECk_ProceduralAction_Easing::OutQuad: return ECk_TweenEasing::OutQuad;
        case ECk_ProceduralAction_Easing::InOutQuad: return ECk_TweenEasing::InOutQuad;
        case ECk_ProceduralAction_Easing::InCubic: return ECk_TweenEasing::InCubic;
        case ECk_ProceduralAction_Easing::OutCubic: return ECk_TweenEasing::OutCubic;
        case ECk_ProceduralAction_Easing::InOutCubic: return ECk_TweenEasing::InOutCubic;
        default: return ECk_TweenEasing::Linear;
        }
    }

    auto
    MakeIdentityInitial(
        const ck::procedural_action::FCompiledClip& InClip)
        -> ck::procedural_action::FInitialPose
    {
        auto Initial = ck::procedural_action::FInitialPose{};
        for (const auto& Slot : InClip.Get_Slots())
        { Initial.Get_Limbs().Add(Slot.Get_Id(), FTransform::Identity); }
        return Initial;
    }

    auto
    PosesMatch(
        const ck::procedural_action::FEvaluatedPose& InA,
        const ck::procedural_action::FEvaluatedPose& InB)
        -> bool
    {
        if (InA.Get_PhaseIndex() != InB.Get_PhaseIndex() || NOT InA.Get_Body().Equals(InB.Get_Body(), 1.0e-12)
            || InA.Get_Limbs().Num() != InB.Get_Limbs().Num())
        { return false; }
        for (auto Index = 0; Index < InA.Get_Limbs().Num(); ++Index)
        {
            const auto& A = InA.Get_Limbs()[Index];
            const auto& B = InB.Get_Limbs()[Index];
            if (A.Get_Slot() != B.Get_Slot() || NOT A.Get_Position().Equals(B.Get_Position(), 1.0e-12)
                || NOT A.Get_Orientation().Equals(B.Get_Orientation(), 1.0e-12)
                || NOT A.Get_BendDirection().Equals(B.Get_BendDirection(), 1.0e-12)
                || A.Get_HasOrientation() != B.Get_HasOrientation() || A.Get_HasBend() != B.Get_HasBend())
            { return false; }
        }
        return true;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionProgressOldClipsTest,
    "Ck.ProceduralAnimation.Action.Progress.OldClipsEvaluateIdentically",
    ck_test_procedural_action_progress::TestFlags)

auto
FCkProceduralActionProgressOldClipsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_progress;
    for (const auto& Path : {BodyPulsePath, LimbNodPath})
    {
        auto* Asset = LoadObject<UCk_ProceduralAction_Data>(nullptr, *Path);
        if (NOT TestNotNull(*FString::Printf(TEXT("Shipped example %s loads"), *Path), Asset))
        { return false; }

        // LimbNod was saved with its frame on the track; loading it must move that frame onto the slot.
        if (Path == LimbNodPath)
        {
            TestTrue(TEXT("The shipped limb example loads with its single slot framed rest-normalized"),
                Asset->Get_Slots().Num() == 1
                && Asset->Get_Slots()[0].Get_Space() == ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        }

        auto AssetClip = ck::procedural_action::FCompiledClip{};
        auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
        if (NOT TestTrue(TEXT("Shipped example compiles through the asset overload"),
            ck::procedural_action::Compile(*Asset, AssetClip, Diagnostics)))
        { return false; }

        auto PresetMismatches = 0;
        auto PresetPhases = 0;
        for (auto PhaseIndex = 0; PhaseIndex < AssetClip.Get_Phases().Num(); ++PhaseIndex)
        {
            ++PresetPhases;
            const auto& Phase = AssetClip.Get_Phases()[PhaseIndex];
            for (auto Sample = 0; Sample <= 256; ++Sample)
            {
                const auto Raw = static_cast<double>(Sample) / 256.0;
                const auto Expected = static_cast<double>(UCk_Utils_TweenEasing_UE::Apply(
                    ToTween(Phase.Get_Easing()), FCk_FloatRange_0to1{Raw}));
                if (FMath::Abs(ck::procedural_action::EvaluateProgress(Phase, Raw) - Expected) > 1.0e-12)
                { ++PresetMismatches; }
            }
        }
        TestTrue(TEXT("Shipped example has preset phases to compare"), PresetPhases > 0);
        TestEqual(TEXT("Preset progress equals the tween easing at 257 samples per phase"), PresetMismatches, 0);

        auto Draft = FCk_ProceduralAction_ClipDraft{};
        const auto DraftReport = UCk_Utils_ProceduralActionBuilder_UE::Make_Draft_FromData(Asset, Draft);
        if (NOT TestEqual(TEXT("Draft copies the shipped example"), DraftReport.Get_Status(), ECk_ProceduralAction_ReportStatus::Valid))
        { return false; }
        auto DraftClip = ck::procedural_action::FCompiledClip{};
        if (NOT TestTrue(TEXT("Draft compiles through the source view"),
            ck::procedural_action::Compile(ck::procedural_action::MakeClipSource(Draft), DraftClip, Diagnostics)))
        { return false; }
        TestEqual(TEXT("Asset and draft views produce one content hash"), AssetClip.Get_ContentHash(), DraftClip.Get_ContentHash());
        TestEqual(TEXT("Asset and draft views produce one duration"), AssetClip.Get_Duration().Get_Seconds(), DraftClip.Get_Duration().Get_Seconds());

        const auto Initial = MakeIdentityInitial(AssetClip);
        auto Times = TArray<double>{};
        for (const auto& Phase : AssetClip.Get_Phases())
        {
            const auto Start = Phase.Get_Start().Get_Seconds();
            const auto Duration = Phase.Get_Duration().Get_Seconds();
            Times.Add(Start);
            Times.Add(Start + Duration);
            for (const auto CriticalTime : Phase.Get_CriticalTimes())
            { Times.Add(CriticalTime.Get_Seconds()); }
            for (auto Interior = 1; Interior <= 7; ++Interior)
            { Times.Add(Start + Duration * static_cast<double>(Interior) / 8.0); }
        }
        auto PoseMismatches = 0;
        for (const auto Time : Times)
        {
            auto AssetPose = ck::procedural_action::FEvaluatedPose{};
            auto DraftPose = ck::procedural_action::FEvaluatedPose{};
            const auto AssetEvaluates = ck::procedural_action::Evaluate(AssetClip, Initial, FCk_Time{Time}, AssetPose);
            const auto DraftEvaluates = ck::procedural_action::Evaluate(DraftClip, Initial, FCk_Time{Time}, DraftPose);
            if (NOT AssetEvaluates || NOT DraftEvaluates || NOT PosesMatch(AssetPose, DraftPose))
            { ++PoseMismatches; }
        }
        TestEqual(TEXT("Asset and draft clips evaluate identically at keys, boundaries and interior times"), PoseMismatches, 0);
    }
    return true;
}

#endif
