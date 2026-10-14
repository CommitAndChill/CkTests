#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/Core/CkProceduralActionEvaluation.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"
#include "CkProceduralAnimation/Core/CkProceduralClipTimeMapping.h"

#include "Misc/AutomationTest.h"

#include <limits>

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralClipExactWindowTimeTest,
    "Ck.ProceduralAnimation.Action.Core.ExactWindowBoundaryTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkProceduralClipExactWindowTimeTest::
    RunTest(const FString&)
    -> bool
{
    constexpr auto FromSeconds = 1.0876169244541334;
    constexpr auto BoundarySeconds = 1.9874354248101536;
    constexpr auto RequestedSeconds = 2.4975514057388297;
    const auto Fraction = (BoundarySeconds - FromSeconds) / (RequestedSeconds - FromSeconds);
    const auto Reconstructed = FromSeconds + (RequestedSeconds - FromSeconds) * Fraction;
    TestTrue(TEXT("Awkward binary time actually exposes the round-trip error"),
        Reconstructed != BoundarySeconds);
    auto Exact = FCk_Time{};
    TestTrue(TEXT("Production time resolver accepts a paired critical sample"),
        ck::procedural_action::ResolveClipSampleTime(FCk_Time{FromSeconds},
            FCk_Time{RequestedSeconds}, FCk_Time{BoundarySeconds},
            TArray<double>{Fraction}, TArray<FCk_Time>{FCk_Time{BoundarySeconds}}, Fraction, Exact));
    TestEqual(TEXT("Resolved receipt boundary is the exact sampled time"),
        Exact.Get_Seconds(), BoundarySeconds);
    TestFalse(TEXT("Zero fraction cannot advance a clip interval"),
        ck::procedural_action::ResolveClipSampleTime(FCk_Time{FromSeconds},
            FCk_Time{RequestedSeconds}, FCk_Time{BoundarySeconds},
            TArray<double>{Fraction}, TArray<FCk_Time>{FCk_Time{BoundarySeconds}}, 0.0, Exact));
    TestFalse(TEXT("Critical pair at interval start cannot certify an interval"),
        ck::procedural_action::ResolveClipSampleTime(FCk_Time{FromSeconds},
            FCk_Time{RequestedSeconds}, FCk_Time{BoundarySeconds},
            TArray<double>{Fraction}, TArray<FCk_Time>{FCk_Time{FromSeconds}}, Fraction, Exact));
    TestFalse(TEXT("Nonfinite critical time rejects before exact-match publication"),
        ck::procedural_action::ResolveClipSampleTime(FCk_Time{FromSeconds},
            FCk_Time{RequestedSeconds}, FCk_Time{BoundarySeconds},
            TArray<double>{Fraction},
            TArray<FCk_Time>{FCk_Time{std::numeric_limits<double>::quiet_NaN()}}, Fraction, Exact));
    TestFalse(TEXT("Nonfinite critical fraction rejects the paired plan"),
        ck::procedural_action::ResolveClipSampleTime(FCk_Time{FromSeconds},
            FCk_Time{RequestedSeconds}, FCk_Time{BoundarySeconds},
            TArray<double>{std::numeric_limits<double>::quiet_NaN()},
            TArray<FCk_Time>{FCk_Time{BoundarySeconds}}, Fraction, Exact));

    auto* Asset = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
    if (NOT TestNotNull(TEXT("Transient time-zero window clip exists"), Asset)) { return false; }
    auto Body = FCk_ProceduralAction_BodyChannel{};
    Body.Set_Enabled(true);
    Asset->Set_BodyChannel(Body);
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{1.0}};
    Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
        FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{1.0, 0.0, 0.0}, FQuat::Identity}});
    Asset->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
    const auto At = [](double InNormalized)
    { return FCk_ProceduralAction_TimeRef{FName{TEXT("Reach")}, FCk_FloatRange_0to1{InNormalized}}; };
    Asset->Set_Windows(TArray<FCk_ProceduralAction_Window>{
        FCk_ProceduralAction_Window{FName{TEXT("Active")}, At(0.0), At(0.37)}});
    auto Clip = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    if (NOT TestTrue(TEXT("Window opening at authored time zero compiles"),
        ck::procedural_action::Compile(*Asset, Clip, Diagnostics)))
    { return false; }
    TestTrue(TEXT("Compiled clip retains the opening event at time zero"),
        Clip.Get_Events().ContainsByPredicate([](const auto& InEvent)
        {
            return InEvent.Get_Time() == FCk_Time{}
                && InEvent.Get_Kind() == ck::procedural_action::EEventKind::WindowEnter;
        }));
    auto Budget = FCk_ProceduralAction_Sampling{};
    Budget.Set_MaxAdvance(FCk_Time{1.0});
    Budget.Set_MaxSamples(8);
    auto Interval = ck::procedural_action::FSampledInterval{};
    if (NOT TestTrue(TEXT("Compiled time-zero clip samples through the first window edge"),
        ck::procedural_action::SampleInterval(Clip, ck::procedural_action::FInitialPose{},
            FCk_Time{}, FCk_Time{1.0}, Budget, Interval)))
    { return false; }
    TestTrue(TEXT("First publication stops exactly at the window exit"),
        Interval.Get_StoppedAtWindowBoundary() && Interval.Get_AcceptedEnd() == Clip.Get_Events().Last().Get_Time());
    TestTrue(TEXT("First interval carries both opening and closing boundary events"),
        Interval.Get_CrossedEvents().Num() == 2);
    return true;
}

#endif
