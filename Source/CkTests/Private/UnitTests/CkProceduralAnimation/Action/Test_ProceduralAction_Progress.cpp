#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionEvaluation.h"
#include "CkProceduralAnimation/Core/CkProceduralActionProgress.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"

#include "CkTween/CkTween_Easing_Utils.h"

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include <limits>

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

    // FCk_FloatRange_0to1's constructor clamps, so malformed authored values arrive through reflection, as a corrupt asset would.
    auto
    SetRawUnitValue(
        FCk_FloatRange_0to1& InOutValue,
        double InRaw)
        -> bool
    {
        auto* Property = FindFProperty<FDoubleProperty>(FCk_FloatRange_0to1::StaticStruct(), TEXT("_Value"));
        if (Property == nullptr)
        { return false; }
        Property->SetPropertyValue_InContainer(&InOutValue, InRaw);
        return true;
    }

    struct FKeyPair
    {
        double Time = 0.0;
        double Value = 0.0;
    };

    auto
    MakeKeys(
        std::initializer_list<FKeyPair> InPairs)
        -> TArray<FCk_ProceduralAction_ProgressKey>
    {
        auto Keys = TArray<FCk_ProceduralAction_ProgressKey>{};
        for (const auto& Pair : InPairs)
        { Keys.Emplace(FCk_FloatRange_0to1{Pair.Time}, FCk_FloatRange_0to1{Pair.Value}); }
        return Keys;
    }

    auto
    MakeCustom(
        const TArray<FCk_ProceduralAction_ProgressKey>& InKeys)
        -> FCk_ProceduralAction_ProgressCurve
    {
        auto Curve = FCk_ProceduralAction_ProgressCurve{};
        Curve.Set_Mode(ECk_ProceduralAction_ProgressMode::CustomCurve);
        Curve.Set_Keys(InKeys);
        return Curve;
    }

    auto
    MakeBodyClip(
        const FCk_ProceduralAction_ProgressCurve& InCurve,
        ECk_ProceduralAction_Easing InEasing)
        -> UCk_ProceduralAction_Data*
    {
        auto* Asset = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
        auto Body = FCk_ProceduralAction_BodyChannel{};
        Body.Set_Enabled(true);
        Asset->Set_BodyChannel(Body);
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Strike")}, FCk_Time{1.0}};
        Phase.Set_Easing(InEasing);
        Phase.Set_Progress(InCurve);
        Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{100.0, 0.0, 0.0}, FQuat::Identity}});
        Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Asset;
    }

    auto
    HasDiagnostic(
        const TArray<ck::procedural_action::FDiagnostic>& InDiagnostics,
        FName InCode,
        const FString& InFieldPath)
        -> bool
    {
        return InDiagnostics.ContainsByPredicate([&](const ck::procedural_action::FDiagnostic& InDiagnostic)
        { return InDiagnostic.Get_Code() == InCode && InDiagnostic.Get_FieldPath() == InFieldPath; });
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

        auto AssetClip = ck::procedural_action::FCompiledClip{};
        auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
        if (NOT TestTrue(TEXT("Shipped example compiles through the asset overload"),
            ck::procedural_action::Compile(*Asset, AssetClip, Diagnostics)))
        { return false; }

        auto PresetMismatches = 0;
        auto PresetPhases = 0;
        for (auto PhaseIndex = 0; PhaseIndex < AssetClip.Get_Phases().Num(); ++PhaseIndex)
        {
            if (Asset->Get_Phases()[PhaseIndex].Get_Progress().Get_Mode() != ECk_ProceduralAction_ProgressMode::Preset)
            { continue; }
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

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionProgressMonotoneTest,
    "Ck.ProceduralAnimation.Action.Progress.CustomCurveIsMonotoneAndBounded",
    ck_test_procedural_action_progress::TestFlags)

auto
FCkProceduralActionProgressMonotoneTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_progress;
    auto Stream = FRandomStream{20261012};
    for (auto Trial = 0; Trial < 24; ++Trial)
    {
        const auto KeyCount = Trial == 0 ? 2 : Stream.RandRange(3, 64);
        auto TimeGaps = TArray<double>{};
        auto ValueSteps = TArray<double>{};
        for (auto Gap = 0; Gap + 1 < KeyCount; ++Gap)
        {
            TimeGaps.Add(0.01 + Stream.FRand());
            ValueSteps.Add(Stream.FRand() < 0.25 ? 0.0 : 0.01 + Stream.FRand());
        }
        const auto PlateauIndex = KeyCount > 2 ? 1 : INDEX_NONE;
        if (PlateauIndex != INDEX_NONE)
        { ValueSteps[PlateauIndex] = 0.0; }
        ValueSteps[0] = FMath::Max(ValueSteps[0], 0.01);

        auto TimeTotal = 0.0;
        auto ValueTotal = 0.0;
        for (auto Gap = 0; Gap < TimeGaps.Num(); ++Gap)
        {
            TimeTotal += TimeGaps[Gap];
            ValueTotal += ValueSteps[Gap];
        }
        auto Keys = TArray<FCk_ProceduralAction_ProgressKey>{};
        auto TimeSum = 0.0;
        auto ValueSum = 0.0;
        Keys.Emplace(FCk_FloatRange_0to1{0.0}, FCk_FloatRange_0to1{0.0});
        for (auto Gap = 0; Gap + 1 < TimeGaps.Num(); ++Gap)
        {
            TimeSum += TimeGaps[Gap];
            ValueSum += ValueSteps[Gap];
            Keys.Emplace(FCk_FloatRange_0to1{TimeSum / TimeTotal}, FCk_FloatRange_0to1{ValueSum / ValueTotal});
        }
        Keys.Emplace(FCk_FloatRange_0to1{1.0}, FCk_FloatRange_0to1{1.0});

        auto Phase = ck::procedural_action::FCompiledPhase{};
        if (NOT TestTrue(TEXT("Valid nondecreasing keys build a monotone curve"),
            ck::procedural_action::BuildMonotoneProgress(Keys, Phase.Get_Progress())))
        { return false; }

        auto Previous = -1.0;
        auto Reversals = 0;
        auto OutOfRange = 0;
        for (auto Sample = 0; Sample < 1024; ++Sample)
        {
            const auto Raw = static_cast<double>(Sample) / 1023.0;
            const auto Value = ck::procedural_action::EvaluateProgress(Phase, Raw);
            if (Value < Previous)
            { ++Reversals; }
            if (NOT FMath::IsFinite(Value) || Value < 0.0 || Value > 1.0)
            { ++OutOfRange; }
            Previous = Value;
        }
        TestEqual(TEXT("Progress never decreases over 1024 samples"), Reversals, 0);
        TestEqual(TEXT("Progress stays in [0,1]"), OutOfRange, 0);
        TestEqual(TEXT("f(0) is exactly 0"), ck::procedural_action::EvaluateProgress(Phase, 0.0), 0.0);
        TestEqual(TEXT("f(1) is exactly 1"), ck::procedural_action::EvaluateProgress(Phase, 1.0), 1.0);

        auto KeyMisses = 0;
        for (const auto& Key : Keys)
        {
            if (ck::procedural_action::EvaluateProgress(Phase, Key.Get_Time().Get_Value()) != Key.Get_Value().Get_Value())
            { ++KeyMisses; }
        }
        TestEqual(TEXT("The curve passes through every key exactly"), KeyMisses, 0);

        if (PlateauIndex != INDEX_NONE)
        {
            const auto From = Keys[PlateauIndex].Get_Time().Get_Value();
            const auto To = Keys[PlateauIndex + 1].Get_Time().Get_Value();
            const auto Held = Keys[PlateauIndex].Get_Value().Get_Value();
            auto PlateauMisses = 0;
            for (auto Step = 1; Step < 16; ++Step)
            {
                if (ck::procedural_action::EvaluateProgress(Phase, From + (To - From) * Step / 16.0) != Held)
                { ++PlateauMisses; }
            }
            TestEqual(TEXT("A plateau is held exactly"), PlateauMisses, 0);
        }
    }

    auto* Asset = MakeBodyClip(MakeCustom(MakeKeys({{0.0, 0.0}, {0.5, 0.9}, {1.0, 1.0}})),
        ECk_ProceduralAction_Easing::Linear);
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("A custom-curve clip compiles"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    auto Pose = ck::procedural_action::FEvaluatedPose{};
    TestTrue(TEXT("Custom clip evaluates"), ck::procedural_action::Evaluate(Clip, {}, FCk_Time{0.5}, Pose));
    TestTrue(TEXT("Evaluation remaps elapsed time through the custom curve once"),
        FMath::IsNearlyEqual(Pose.Get_Body().GetTranslation().X, 90.0, 1.0e-9));

    auto* PresetAsset = MakeBodyClip(FCk_ProceduralAction_ProgressCurve{}, ECk_ProceduralAction_Easing::Linear);
    auto PresetClip = ck::procedural_action::FCompiledClip{};
    TestTrue(TEXT("Preset twin compiles"), ck::procedural_action::Compile(*PresetAsset, PresetClip, Diagnostics));
    TestNotEqual(TEXT("A custom curve is part of the content revision"), Clip.Get_ContentHash(), PresetClip.Get_ContentHash());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionProgressRejectsTest,
    "Ck.ProceduralAnimation.Action.Progress.RejectsReversalDuplicateNaNAndNonLinearEasing",
    ck_test_procedural_action_progress::TestFlags)

auto
FCkProceduralActionProgressRejectsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_progress;
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    const auto ExpectReject = [&](const FString& InWhat, const FCk_ProceduralAction_ProgressCurve& InCurve,
        ECk_ProceduralAction_Easing InEasing, FName InCode, const FString& InFieldPath) -> void
    {
        auto* Asset = MakeBodyClip(InCurve, InEasing);
        TestFalse(*FString::Printf(TEXT("%s rejects"), *InWhat), ck::procedural_action::Compile(*Asset, Clip, Diagnostics));
        TestTrue(*FString::Printf(TEXT("%s names %s at %s"), *InWhat, *InCode.ToString(), *InFieldPath),
            HasDiagnostic(Diagnostics, InCode, InFieldPath));
        TestEqual(*FString::Printf(TEXT("%s leaves no compiled phase"), *InWhat), Clip.Get_Phases().Num(), 0);
    };

    ExpectReject(TEXT("Reversal"), MakeCustom(MakeKeys({{0.0, 0.0}, {0.5, 0.6}, {0.7, 0.4}, {1.0, 1.0}})),
        ECk_ProceduralAction_Easing::Linear, TEXT("ProgressKeyValue"), TEXT("Phases[0].Progress.Keys[2].Value"));
    ExpectReject(TEXT("Duplicate time"), MakeCustom(MakeKeys({{0.0, 0.0}, {0.5, 0.3}, {0.5, 0.6}, {1.0, 1.0}})),
        ECk_ProceduralAction_Easing::Linear, TEXT("ProgressKeyTime"), TEXT("Phases[0].Progress.Keys[2].Time"));
    ExpectReject(TEXT("Non-Linear easing under a custom curve"), MakeCustom(MakeKeys({{0.0, 0.0}, {0.5, 0.3}, {1.0, 1.0}})),
        ECk_ProceduralAction_Easing::InQuad, TEXT("ProgressEasing"), TEXT("Phases[0].Easing"));
    ExpectReject(TEXT("Inexact end point"), MakeCustom(MakeKeys({{0.0, 0.0}, {1.0, 0.9}})),
        ECk_ProceduralAction_Easing::Linear, TEXT("ProgressEndpoints"), TEXT("Phases[0].Progress.Keys"));
    ExpectReject(TEXT("Single key"), MakeCustom(MakeKeys({{0.0, 0.0}})),
        ECk_ProceduralAction_Easing::Linear, TEXT("ProgressKeyCount"), TEXT("Phases[0].Progress.Keys"));

    auto TooMany = TArray<FCk_ProceduralAction_ProgressKey>{};
    for (auto Index = 0; Index <= 64; ++Index)
    {
        const auto Unit = static_cast<double>(Index) / 64.0;
        TooMany.Emplace(FCk_FloatRange_0to1{Unit}, FCk_FloatRange_0to1{Unit});
    }
    ExpectReject(TEXT("65 keys"), MakeCustom(TooMany), ECk_ProceduralAction_Easing::Linear,
        TEXT("ProgressKeyCount"), TEXT("Phases[0].Progress.Keys"));

    auto NaNTime = MakeKeys({{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}});
    if (NOT TestTrue(TEXT("Reflection reaches the unit value"),
        SetRawUnitValue(NaNTime[1].Get_Time(), std::numeric_limits<double>::quiet_NaN())))
    { return false; }
    ExpectReject(TEXT("NaN time"), MakeCustom(NaNTime), ECk_ProceduralAction_Easing::Linear,
        TEXT("ProgressKeyTime"), TEXT("Phases[0].Progress.Keys[1].Time"));
    auto NaNValue = MakeKeys({{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}});
    SetRawUnitValue(NaNValue[1].Get_Value(), std::numeric_limits<double>::quiet_NaN());
    ExpectReject(TEXT("NaN value"), MakeCustom(NaNValue), ECk_ProceduralAction_Easing::Linear,
        TEXT("ProgressKeyValue"), TEXT("Phases[0].Progress.Keys[1].Value"));

    auto PresetWithKeys = FCk_ProceduralAction_ProgressCurve{};
    PresetWithKeys.Set_Keys(MakeKeys({{0.0, 0.0}, {0.5, 0.8}, {1.0, 1.0}}));
    ExpectReject(TEXT("Keys under a Preset mode"), PresetWithKeys, ECk_ProceduralAction_Easing::Linear,
        TEXT("ProgressKeysInactive"), TEXT("Phases[0].Progress.Keys"));

    auto* PresetEased = MakeBodyClip(FCk_ProceduralAction_ProgressCurve{}, ECk_ProceduralAction_Easing::InQuad);
    TestTrue(TEXT("Preset mode keeps every existing easing"), ck::procedural_action::Compile(*PresetEased, Clip, Diagnostics));

    auto Draft = FCk_ProceduralAction_ClipDraft{};
    TestEqual(TEXT("Builder accepts a phase"), UCk_Utils_ProceduralActionBuilder_UE::Add_Phase(Draft,
        FCk_ProceduralAction_Phase{FName{TEXT("Strike")}, FCk_Time{1.0}}).Get_Status(), ECk_ProceduralAction_ReportStatus::Valid);
    const auto Before = Draft;
    const auto Reversed = UCk_Utils_ProceduralActionBuilder_UE::Set_PhaseProgress(Draft, FName{TEXT("Strike")},
        MakeCustom(MakeKeys({{0.0, 0.0}, {0.5, 0.6}, {0.7, 0.4}, {1.0, 1.0}})));
    TestEqual(TEXT("Builder rejects the same reversal"), Reversed.Get_Status(), ECk_ProceduralAction_ReportStatus::Invalid);
    TestTrue(TEXT("Builder reversal names the compiler field path"), Reversed.Get_Diagnostics().ContainsByPredicate(
        [](const FCk_ProceduralAction_Diagnostic& InDiagnostic)
        { return InDiagnostic.Get_FieldPath() == TEXT("Phases[0].Progress.Keys[2].Value"); }));
    TestTrue(TEXT("Rejected progress leaves the draft unchanged"),
        FCk_ProceduralAction_ClipDraft::StaticStruct()->CompareScriptStruct(&Before, &Draft, PPF_None));
    const auto InactiveKeys = UCk_Utils_ProceduralActionBuilder_UE::Set_PhaseProgress(Draft, FName{TEXT("Strike")},
        PresetWithKeys);
    TestEqual(TEXT("Builder rejects keys under a Preset mode"), InactiveKeys.Get_Status(),
        ECk_ProceduralAction_ReportStatus::Invalid);
    TestTrue(TEXT("Builder names the inactive keys"), InactiveKeys.Get_Diagnostics().ContainsByPredicate(
        [](const FCk_ProceduralAction_Diagnostic& InDiagnostic)
        { return InDiagnostic.Get_Code() == FName{TEXT("ProgressKeysInactive")}; }));
    TestTrue(TEXT("Rejected inactive keys leave the draft unchanged"),
        FCk_ProceduralAction_ClipDraft::StaticStruct()->CompareScriptStruct(&Before, &Draft, PPF_None));
    TestEqual(TEXT("Builder rejects an unknown phase"), UCk_Utils_ProceduralActionBuilder_UE::Set_PhaseProgress(Draft,
        FName{TEXT("Missing")}, MakeCustom(MakeKeys({{0.0, 0.0}, {1.0, 1.0}}))).Get_Status(), ECk_ProceduralAction_ReportStatus::Invalid);
    TestEqual(TEXT("Builder accepts a monotone curve"), UCk_Utils_ProceduralActionBuilder_UE::Set_PhaseProgress(Draft,
        FName{TEXT("Strike")}, MakeCustom(MakeKeys({{0.0, 0.0}, {0.5, 0.5}, {0.6, 0.5}, {1.0, 1.0}}))).Get_Status(),
        ECk_ProceduralAction_ReportStatus::Valid);
    TestEqual(TEXT("Accepted curve is stored on the phase"), Draft.Get_Phases()[0].Get_Progress().Get_Keys().Num(), 4);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionProgressSteepCurveTest,
    "Ck.ProceduralAnimation.Action.Progress.SteepCurveCrossesSeveralKeysAndWindowsInOrder",
    ck_test_procedural_action_progress::TestFlags)

auto
FCkProceduralActionProgressSteepCurveTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_progress;
    const auto PhaseId = FName{TEXT("Strike")};
    const auto At = [PhaseId](double InTime)
    { return FCk_ProceduralAction_TimeRef{PhaseId, FCk_FloatRange_0to1{InTime}}; };

    auto* Asset = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    auto Slot = FCk_ProceduralAction_Slot{FName{TEXT("Hand")}};
    Asset->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
    auto Track = FCk_ProceduralAction_LimbTrack{FName{TEXT("Hand")}};
    Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, FVector{0.0, 0.0, 0.0}},
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.2}, FVector{10.0, 0.0, 0.0}},
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.5}, FVector{20.0, 0.0, 0.0}},
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.8}, FVector{30.0, 0.0, 0.0}},
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, FVector{40.0, 0.0, 0.0}}});
    auto Phase = FCk_ProceduralAction_Phase{PhaseId, FCk_Time{1.0}};
    Phase.Set_Progress(MakeCustom(MakeKeys({{0.0, 0.0}, {0.4, 0.05}, {0.5, 0.95}, {1.0, 1.0}})));
    Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    Asset->Set_Markers(TArray<FCk_ProceduralAction_Marker>{
        FCk_ProceduralAction_Marker{FName{TEXT("Early")}, At(0.42)},
        FCk_ProceduralAction_Marker{FName{TEXT("Late")}, At(0.48)}});

    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Steep custom clip compiles"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    const auto& Critical = Clip.Get_Phases()[0].Get_CriticalTimes();
    const auto SteepCriticalTimes = Critical.FilterByPredicate([](const FCk_Time& InTime)
    { return InTime.Get_Seconds() > 0.4 && InTime.Get_Seconds() < 0.5; });
    TestEqual(TEXT("All three interior pose keys invert into the steep elapsed segment"), SteepCriticalTimes.Num(), 3);

    auto Initial = ck::procedural_action::FInitialPose{};
    Initial.Get_Limbs().Add(FName{TEXT("Hand")}, FTransform::Identity);
    auto Budget = FCk_ProceduralAction_Sampling{};
    Budget.Set_MaxAdvance(FCk_Time{1.0});
    Budget.Set_MaxEndpointTravelCm(1000.0f);
    Budget.Set_MaxSegmentAngularStepDegrees(180.0f);
    Budget.Set_MaxSamples(64);

    auto Interval = ck::procedural_action::FSampledInterval{};
    if (NOT TestTrue(TEXT("One interval spans the steep segment"), ck::procedural_action::SampleInterval(
        Clip, Initial, FCk_Time{0.4}, FCk_Time{0.5}, Budget, Interval)))
    { return false; }
    TestTrue(TEXT("The single interval reaches the end of the steep segment"),
        FMath::IsNearlyEqual(Interval.Get_AcceptedEnd().Get_Seconds(), 0.5, 1.0e-12));
    for (const auto KeyX : {10.0, 20.0, 30.0})
    {
        TestTrue(*FString::Printf(TEXT("A sample lands on the pose key at x=%g"), KeyX),
            Interval.Get_Samples().ContainsByPredicate([KeyX](const ck::procedural_action::FEvaluatedPose& InPose)
            { return InPose.Get_Limbs().Num() == 1 && FMath::IsNearlyEqual(InPose.Get_Limbs()[0].Get_Position().X, KeyX, 1.0e-6); }));
    }
    const auto& PointEvents = Interval.Get_CrossedEvents();
    if (TestEqual(TEXT("Both interior markers cross once"), PointEvents.Num(), 2))
    {
        TestEqual(TEXT("Markers cross in elapsed-time order"), PointEvents[0].Get_Id(), FName{TEXT("Early")});
        TestTrue(TEXT("Marker times stay on elapsed phase time"),
            FMath::IsNearlyEqual(PointEvents[0].Get_Time().Get_Seconds(), 0.42, 1.0e-12)
            && FMath::IsNearlyEqual(PointEvents[1].Get_Time().Get_Seconds(), 0.48, 1.0e-12));
    }

    Asset->Set_Windows(TArray<FCk_ProceduralAction_Window>{
        FCk_ProceduralAction_Window{FName{TEXT("First")}, At(0.41), At(0.43)},
        FCk_ProceduralAction_Window{FName{TEXT("Second")}, At(0.44), At(0.47)}});
    if (NOT TestTrue(TEXT("Windowed steep clip compiles"), ck::procedural_action::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    auto Events = TArray<ck::procedural_action::FCompiledEvent>{};
    auto From = FCk_Time{0.0};
    for (auto Guard = 0; Guard < 64 && From < Clip.Get_Duration(); ++Guard)
    {
        if (NOT TestTrue(TEXT("Each continuation samples"), ck::procedural_action::SampleInterval(
            Clip, Initial, From, Clip.Get_Duration(), Budget, Interval)))
        { return false; }
        Events.Append(Interval.Get_CrossedEvents());
        From = Interval.Get_AcceptedEnd();
    }
    TestTrue(TEXT("The whole clip is consumed"), From == Clip.Get_Duration());
    if (NOT TestEqual(TEXT("Two markers and two windows produce six crossings"), Events.Num(), 6))
    { return false; }
    auto StrictlyOrdered = true;
    for (auto Index = 1; Index < Events.Num(); ++Index)
    { StrictlyOrdered &= Events[Index - 1].Get_Time() < Events[Index].Get_Time(); }
    TestTrue(TEXT("Crossed events are strictly ordered by elapsed time"), StrictlyOrdered);
    for (const auto WindowId : {FName{TEXT("First")}, FName{TEXT("Second")}})
    {
        const auto Count = [&](ck::procedural_action::EEventKind InKind)
        {
            return Events.FilterByPredicate([&](const ck::procedural_action::FCompiledEvent& InEvent)
            { return InEvent.Get_Id() == WindowId && InEvent.Get_Kind() == InKind; }).Num();
        };
        TestEqual(*FString::Printf(TEXT("%s enters once"), *WindowId.ToString()), Count(ck::procedural_action::EEventKind::WindowEnter), 1);
        TestEqual(*FString::Printf(TEXT("%s exits once"), *WindowId.ToString()), Count(ck::procedural_action::EEventKind::WindowExit), 1);
    }
    return true;
}
#endif
