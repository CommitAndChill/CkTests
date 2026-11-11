#include "CkProceduralAnimation/Core/CkProceduralBodyEnvelope.h"
#include "CkJolt/CkJolt_Utils.h"

#include "../../CkUnitTest_Common.h"

#include <limits>
#include <Jolt/Math/Mat44.h>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_body_plane_clip
{
    auto
        Hit(
            double InZ = 70.0)
        -> FCk_Jolt_HitResult
    {
        return FCk_Jolt_HitResult{}.Set_HasHit(true).Set_Fraction(0.5f)
            .Set_Position(FVector{100.0, 200.0, InZ}).Set_Normal(FVector::UpVector);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyPlaneClipRepresentedTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeClip.RepresentedPlaneAndRigidPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkBodyPlaneClipRepresentedTest::
    RunTest(const FString& InParameters)
    -> bool
{
    const auto Current = FTransform{FVector{100.0, 200.0, 100.0}};
    const auto Partial = FTransform{FVector{100.0, 200.0, 90.0}};
    const auto Observation = ck::FProceduralBodyEnvelopeEnclosureObservation{
        Current, Partial, FVector{51.05, 29.05, 23.0500001}, FRotator::ZeroRotator};
    const auto Hit = ck_test_body_plane_clip::Hit();
    const auto Candidate = ck::TryGet_ProceduralBodyPlaneClip(Observation, Current, Partial, Hit, FVector::UpVector);
    TestTrue(TEXT("A valid numeric enclosure constructs one intermediate proposal"), Candidate.IsSet());
    if (NOT Candidate.IsSet())
    { return false; }
    const auto Expected = static_cast<double>(static_cast<float>(Hit.Get_Position().Z
        + Observation.Get_WorldUpRadius() + 0.1));
    TestEqual(TEXT("Height is the exact single native-float conversion"), Candidate->GetLocation().Z, Expected);
    TestTrue(TEXT("Represented height is strictly above its conservative padded plane and between both bodies"),
        Expected > Hit.Get_Position().Z + Observation.Get_WorldUpRadius() && Expected > 90.0 && Expected < 100.0);
    TestTrue(TEXT("The proposal preserves both horizontal coordinates, quaternion and scale exactly"),
        Candidate->GetLocation().X == Current.GetLocation().X && Candidate->GetLocation().Y == Current.GetLocation().Y
            && Candidate->GetRotation() == Current.GetRotation() && Candidate->GetScale3D() == Current.GetScale3D());
    TestTrue(TEXT("Native shape extents retain the actual float representation"),
        Observation.Get_NativeHalfExtents().Z == static_cast<double>(static_cast<float>(23.0500001)));
    TestTrue(TEXT("The numeric proposal carries no assertion of entity attribution"), NOT ck::IsValid(Hit.Get_Entity()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyPlaneClipRejectsTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeClip.RejectsMalformedAndUnjoinedInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkBodyPlaneClipRejectsTest::
    RunTest(const FString& InParameters)
    -> bool
{
    const auto Current = FTransform{FVector{100.0, 200.0, 100.0}};
    const auto Partial = FTransform{FVector{100.0, 200.0, 90.0}};
    const auto Observation = ck::FProceduralBodyEnvelopeEnclosureObservation{
        Current, Partial, FVector{51.05, 29.05, 23.05}, FRotator::ZeroRotator};
    const auto Hit = ck_test_body_plane_clip::Hit();
    const auto Rejects = [&](const ck::FProceduralBodyEnvelopeEnclosureObservation& InObservation,
        const FTransform& InCurrent, const FTransform& InPartial, const FCk_Jolt_HitResult& InHit,
        const FVector& InUp = FVector::UpVector) -> bool
    {
        return NOT ck::TryGet_ProceduralBodyPlaneClip(InObservation, InCurrent, InPartial, InHit, InUp).IsSet();
    };
    TestTrue(TEXT("Default enclosure is unknown"), Rejects({}, Current, Partial, Hit));
    TestTrue(TEXT("Default hit is not a plane"), Rejects(Observation, Current, Partial, {}));
    for (const auto Fraction : {0.0f, -0.5f, 1.0f, 2.0f,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        auto Invalid = Hit;
        Invalid.Set_Fraction(Fraction);
        TestTrue(TEXT("Only a finite interior sweep fraction qualifies"), Rejects(Observation, Current, Partial, Invalid));
    }
    for (const auto Normal : {FVector::ForwardVector, -FVector::UpVector, FVector{0.0, 0.0, 2.0},
        FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0}})
    {
        auto Invalid = Hit;
        Invalid.Set_Normal(Normal);
        TestTrue(TEXT("Only the exact world-Up hit normal qualifies"), Rejects(Observation, Current, Partial, Invalid));
    }
    auto Invalid = Hit;
    Invalid.Set_Position(FVector{0.0, 0.0, std::numeric_limits<double>::infinity()});
    TestTrue(TEXT("Nonfinite plane rejects"), Rejects(Observation, Current, Partial, Invalid));
    TestTrue(TEXT("Tilted support cannot opt into this world-Up constructor"),
        Rejects(Observation, Current, Partial, Hit, FVector{0.0, 0.1, 1.0}.GetSafeNormal()));
    auto Changed = Partial;
    Changed.AddToTranslation(FVector{1.0, 0.0, 0.0});
    TestTrue(TEXT("An unrelated horizontal endpoint rejects"), Rejects(Observation, Current, Changed, Hit));
    Changed = Partial;
    Changed.SetRotation(FRotator{0.0, 10.0, 0.0}.Quaternion());
    TestTrue(TEXT("A changed endpoint quaternion rejects"), Rejects(Observation, Current, Changed, Hit));
    Changed = Partial;
    Changed.SetScale3D(FVector{2.0});
    TestTrue(TEXT("A changed endpoint scale rejects"), Rejects(Observation, Current, Changed, Hit));
    TestTrue(TEXT("A different current transform cannot reuse the observation"),
        Rejects(Observation, FTransform{FVector{100.0, 200.0, 101.0}}, Partial, Hit));
    for (const auto Plane : {50.0, 100.0})
    {
        TestTrue(TEXT("A represented proposal outside the strict interval rejects"),
            Rejects(Observation, Current, Partial, ck_test_body_plane_clip::Hit(Plane)));
    }
    const auto HighCurrent = FTransform{FVector{0.0, 0.0, 100000010.0}};
    const auto HighPartial = FTransform{FVector{0.0, 0.0, 99999990.0}};
    const auto CoarseObservation = ck::FProceduralBodyEnvelopeEnclosureObservation{
        HighCurrent, HighPartial, FVector{0.5}, FRotator::ZeroRotator};
    TestTrue(TEXT("A float grid that loses strict plane separation rejects without searching"),
        Rejects(CoarseObservation, HighCurrent, HighPartial, ck_test_body_plane_clip::Hit(100000000.0)));
    const auto InvalidObservation = ck::FProceduralBodyEnvelopeEnclosureObservation{
        Current, Partial, FVector{std::numeric_limits<double>::infinity()}, FRotator::ZeroRotator};
    TestFalse(TEXT("Unrepresentable native extents never become an observation"), InvalidObservation.Get_HasObservation());
    auto Reused = Observation;
    auto Queries = 0;
    TestFalse(TEXT("Unavailable world rejects without native work and clears reused geometry"),
        ck::Get_IsProceduralBodyEnvelopePathClear(nullptr, FCk_SurfaceMotion_AdaptiveHeight{}, FCk_Jolt_QueryFilter{},
            Current, Partial, Queries, 64, nullptr, nullptr,
            ck::EProceduralBodyEnvelopeEndpointValidation::ConservativeEnclosure, &Reused));
    TestFalse(TEXT("Old observation cannot survive any early failure"), Reused.Get_HasObservation());
    TestEqual(TEXT("Unavailable-world rejection spends no query"), Queries, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyPlaneClipFloatBoundTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeClip.ConservativeNativeFloatSupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkBodyPlaneClipFloatBoundTest::
    RunTest(const FString& InParameters)
    -> bool
{
    // Pure arithmetic controls, not native collision/admission evidence.
    for (const auto Rotation : {FRotator::ZeroRotator, FRotator{23.0, 37.0, -11.0}, FRotator{-42.0, 109.0, 17.0}})
    {
        const auto Current = FTransform{Rotation.Quaternion(), FVector{100.0, 200.0, 100.0}};
        const auto Partial = FTransform{Rotation.Quaternion(), FVector{100.0, 200.0, 90.0}};
        const auto Observation = ck::FProceduralBodyEnvelopeEnclosureObservation{
            Current, Partial, FVector{51.0500001, 29.0500001, 23.0500001}, Rotation};
        TestTrue(TEXT("Finite rigid rotated enclosure is represented"), Observation.Get_HasObservation());
        if (NOT Observation.Get_HasObservation())
        { return false; }
        const auto Matrix = JPH::Mat44::sRotation(ck::jolt::Conv(Rotation.Quaternion()));
        const auto Extents = ck::jolt::Conv(Observation.Get_HalfExtents());
        for (const auto X : {-1.0f, 1.0f})
        {
            for (const auto Y : {-1.0f, 1.0f})
            {
                for (const auto Z : {-1.0f, 1.0f})
                {
                    const auto Corner = Matrix.Multiply3x3(JPH::Vec3{
                        X * Extents.GetX(), Y * Extents.GetY(), Z * Extents.GetZ()});
                    TestTrue(TEXT("Every actual float-transformed corner is bounded without a height search"),
                        FMath::Abs(static_cast<double>(Corner.GetZ())) <= Observation.Get_WorldUpRadius());
                }
            }
        }
    }
    return true;
}

#endif
