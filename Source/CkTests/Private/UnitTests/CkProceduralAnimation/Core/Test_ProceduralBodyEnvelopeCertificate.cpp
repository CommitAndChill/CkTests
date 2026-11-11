#include "CkProceduralAnimation/Core/CkProceduralBodyEnvelope.h"
#include "../../../../../../../CkFoundation/Source/CkProceduralAnimation/Private/CkProceduralBodyEnvelopeCertificate.h"
#include "CkJolt/CkJolt_Utils.h"
#include "../../CkUnitTest_Common.h"
#include <Math/Int128.h>

#include <bit>
#include <cmath>
#include <limits>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Physics/PhysicsSettings.h>

#if WITH_DEV_AUTOMATION_TESTS

#if WITH_EDITOR
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"
#include "CkJolt/CollisionLayers/CkJoltCollisionLayer_Utils.h"
#include <Engine/World.h>
#include <Misc/ScopeExit.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#endif

namespace ck_test_body_certificate
{
    using namespace ck_body_envelope_certificate;

    // For these bounded controls M*D and N*2^-E fit int64 exactly. This comparison does no floating arithmetic.
    auto CompareDyadicToRational(double InValue, int64 InNumerator, int64 InDenominator) -> int32
    {
        const auto Bits = std::bit_cast<uint64>(InValue);
        const auto Magnitude = Bits & 0x7fffffffffffffffULL;
        if (Magnitude == 0)
        { return InNumerator == 0 ? 0 : (InNumerator > 0 ? -1 : 1); }
        const auto Exponent = static_cast<int32>((Bits >> 52) & 0x7ffULL) - 1023 - 52;
        if (Exponent < -60 || Exponent >= 0)
        { return 2; }
        auto Mantissa = static_cast<int64>((Bits & 0x000fffffffffffffULL) | 0x0010000000000000ULL);
        if ((Bits >> 63) != 0)
        { Mantissa = -Mantissa; }
        const auto Left = Mantissa * InDenominator;
        const auto Right = InNumerator * (int64{1} << -Exponent);
        return Left < Right ? -1 : (Left > Right ? 1 : 0);
    }

    auto ContainsRational(FInterval InValue, int64 InNumerator, int64 InDenominator) -> bool
    {
        if (InDenominator < 0)
        { InNumerator = -InNumerator; InDenominator = -InDenominator; }
        const auto Lo = CompareDyadicToRational(InValue._Lo, InNumerator, InDenominator);
        const auto Hi = CompareDyadicToRational(InValue._Hi, InNumerator, InDenominator);
        return InValue._Valid && Lo != 2 && Hi != 2 && Lo <= 0 && Hi >= 0;
    }

    auto CompareSquaredDyadicToRational(double InValue, uint32 InNumerator, uint32 InDenominator) -> int32
    {
        const auto Bits = std::bit_cast<uint64>(InValue);
        const auto Exponent = static_cast<int32>((Bits >> 52) & 0x7ffULL) - 1023 - 52;
        if ((Bits >> 63) != 0 || Exponent < -60 || Exponent > -40)
        { return 2; }
        const auto Mantissa = (Bits & 0x000fffffffffffffULL) | 0x0010000000000000ULL;
        const auto Low = Mantissa & 0xffffffffULL;
        const auto High = Mantissa >> 32;
        const auto LL = Low * Low;
        const auto Cross = Low * High;
        const auto ProductLow = LL + (Cross << 33);
        const auto ProductHigh = High * High + (Cross >> 31) + (ProductLow < LL ? 1ULL : 0ULL);
        auto Left = FUInt128{ProductHigh, ProductLow};
        Left *= InDenominator;
        const auto Shift = -2 * Exponent;
        const auto Right = FUInt128{static_cast<uint64>(InNumerator) << (Shift - 64), 0};
        return Left < Right ? -1 : (Left > Right ? 1 : 0);
    }

    auto Contains(FInterval InValue, double InPoint) -> bool
    { return InValue._Valid && InValue._Lo <= InPoint && InValue._Hi >= InPoint; }

    auto OldYawHalfExtents(const FVector& InAuthored, float InMargin, double InDegrees) -> FVector
    {
        const auto Padded = InAuthored + FVector{InMargin + JPH::cDefaultConvexRadius};
        const auto Inflation = 2.0 * Padded.Size() * FMath::Sin(FMath::DegreesToRadians(InDegrees) * 0.25);
        return Padded + FVector{Inflation, Inflation, 0.0};
    }

    auto OldHalfExtents(const FVector& InAuthored, const FQuat& InStart, const FQuat& InEnd) -> FVector
    {
        const auto Padded = InAuthored + FVector{1.0f + JPH::cDefaultConvexRadius};
        const auto Inflation = 2.0 * Padded.Size() * FMath::Sin(InStart.AngularDistance(InEnd) * 0.25);
        const auto Midpoint = FQuat::Slerp(InStart, InEnd, 0.5).GetNormalized();
        const auto Relative = (InEnd.GetShortestArcWith(InStart) * InStart.Inverse()).GetNormalized();
        const auto RelativeAxis = FVector{Relative.X, Relative.Y, Relative.Z};
        const auto AxisSizeSquared = RelativeAxis.SizeSquared();
        auto AxisInflation = FVector{Inflation};
        if (InStart.AngularDistance(InEnd) > 1.0e-6 && NOT RelativeAxis.ContainsNaN()
            && FMath::IsFinite(AxisSizeSquared) && AxisSizeSquared > 1.0e-12)
        {
            const auto Axis = Midpoint.UnrotateVector(RelativeAxis / FMath::Sqrt(AxisSizeSquared));
            if (NOT Axis.ContainsNaN() && Axis.IsNormalized())
            {
                AxisInflation = Inflation * FVector{FMath::Sqrt(FMath::Max(0.0, 1.0 - Axis.X * Axis.X)),
                    FMath::Sqrt(FMath::Max(0.0, 1.0 - Axis.Y * Axis.Y)), FMath::Sqrt(FMath::Max(0.0, 1.0 - Axis.Z * Axis.Z))};
            }
        }
        return Padded + AxisInflation;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyCertificateArithmeticTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeCertificate.ExactRationalArithmeticAndUnderflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkBodyCertificateArithmeticTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_body_envelope_certificate;
    using namespace ck_test_body_certificate;
    if (NOT Get_IsSupportedArithmetic())
    { AddInfo(TEXT("Unsupported arithmetic mode: production certificate is disabled")); return true; }
    for (int64 A = -7; A <= 7; ++A)
    {
        for (int64 B = -7; B <= 7; ++B)
        {
            const auto X = Point(static_cast<double>(A) / 8.0);
            const auto Y = Point(static_cast<double>(B) / 8.0);
            TestTrue(TEXT("Addition encloses the exact dyadic rational"), ContainsRational(Add(X, Y), A + B, 8));
            TestTrue(TEXT("Subtraction encloses the exact dyadic rational"), ContainsRational(Subtract(X, Y), A - B, 8));
            TestTrue(TEXT("Multiplication encloses the exact dyadic rational"), ContainsRational(Multiply(X, Y), A * B, 64));
            if (B != 0)
            { TestTrue(TEXT("Division encloses the exact rational, using integer comparisons"), ContainsRational(Divide(X, Y), A, B)); }
        }
    }
    for (int64 Numerator = 1; Numerator <= 15; ++Numerator)
    {
        const auto Root = SquareRoot(Point(static_cast<double>(Numerator) / 8.0));
        TestTrue(TEXT("Square root lower endpoint squares below its input"),
            Root._Valid && CompareSquaredDyadicToRational(Root._Lo, static_cast<uint32>(Numerator), 8) <= 0);
        const auto UpperComparison = CompareSquaredDyadicToRational(Root._Hi, static_cast<uint32>(Numerator), 8);
        TestTrue(TEXT("Square root upper endpoint squares above its input"),
            Root._Valid && UpperComparison != 2 && UpperComparison >= 0);
    }
    const auto Zero = Multiply(Point(-0.0), Point(1.0));
    TestTrue(TEXT("Signed zero remains algebraically exact"), Zero._Valid && Zero._Lo == 0.0 && Zero._Hi == 0.0);
    const auto Underflow = Multiply(Point(std::numeric_limits<double>::min()), Point(0.5));
    TestTrue(TEXT("FTZ-sensitive non-exact operations cover the signed minimum-normal interval"),
        Underflow._Valid && Underflow._Lo <= -std::numeric_limits<double>::min()
            && Underflow._Hi >= std::numeric_limits<double>::min());
    TestFalse(TEXT("Subnormal input leaves decline"), Point(std::numeric_limits<double>::denorm_min())._Valid);
    TestFalse(TEXT("Overflow declines"), Multiply(Point(std::numeric_limits<double>::max()), Point(2.0))._Valid);
    TestFalse(TEXT("A denominator containing zero declines before division"), Divide(Point(1.0), {-1.0, 1.0, true})._Valid);
    TestFalse(TEXT("A negative square root declines"), SquareRoot(Point(-1.0))._Valid);
    TestFalse(TEXT("A malformed endpoint ordering declines"), Add({2.0, 1.0, true}, Point(1.0))._Valid);
    TestFalse(TEXT("Default arithmetic state cannot become valid"), Multiply({}, Point(0.0))._Valid);
    const auto FloatBoundary = NativeFloat(Point(std::numeric_limits<float>::min()));
    TestTrue(TEXT("Float outward expansion widens the subnormal neighbor to zero"),
        FloatBoundary._Valid && FloatBoundary._Lo == 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyCertificateMatrixTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeCertificate.NativePolynomialAndInverse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkBodyCertificateMatrixTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_body_envelope_certificate;
    using namespace ck_test_body_certificate;
    if (NOT Get_IsSupportedArithmetic())
    { AddInfo(TEXT("Unsupported arithmetic mode: production certificate is disabled")); return true; }
    for (const auto Rotator : {FRotator::ZeroRotator, FRotator{14.0, 28.0, 7.0}, FRotator{-31.0, 57.0, 19.0}})
    {
        const auto Q = ck::jolt::Conv(Rotator.Quaternion());
        auto Bounds = FMatrixInterval{};
        Get_NativeMatrixBounds(ck::jolt::Conv(Q), Bounds);
        const auto Actual = JPH::Mat44::sRotation(Q);
        const float Values[] = {Q.GetX(), Q.GetY(), Q.GetZ(), Q.GetW()};
        for (int32 Row = 0; Row < 3; ++Row)
        {
            for (int32 Col = 0; Col < 3; ++Col)
            {
                const float ActualValue = Actual.GetColumn3(Col)[Row];
                TestTrue(TEXT("Native SSE/scalar matrix coefficient lies in its polynomial interval"), Contains(Bounds[Row][Col], ActualValue));
                if (Row == Col)
                {
                    const auto Y = Values[(Row + 1) % 3];
                    const auto Z = Values[(Row + 2) % 3];
                    const float Scalar = (1.0f - (Y + Y) * Y) - (Z + Z) * Z;
                    const float Reassociated = 1.0f - 2.0f * (Y * Y + Z * Z);
                    const float Fused = std::fma(-2.0f * Y, Y, std::fma(-2.0f * Z, Z, 1.0f));
                    TestTrue(TEXT("Explicit sequential, reassociated and fused diagonal arrangements are enclosed"),
                        Contains(Bounds[Row][Col], Scalar) && Contains(Bounds[Row][Col], Reassociated)
                            && Contains(Bounds[Row][Col], Fused));
                }
                else
                {
                    const auto Remaining = 3 - Row - Col;
                    const auto Sign = (Col == (Row + 1) % 3) ? -1.0f : 1.0f;
                    const float Scalar = (Values[Row] + Values[Row]) * Values[Col]
                        + Sign * (Values[Remaining] + Values[Remaining]) * Values[3];
                    const float Reassociated = 2.0f * (Values[Row] * Values[Col] + Sign * Values[Remaining] * Values[3]);
                    const float Fused = std::fma(2.0f * Values[Row], Values[Col], Sign * 2.0f * Values[Remaining] * Values[3]);
                    TestTrue(TEXT("Explicit scalar, reassociated and fused off-diagonal arrangements are enclosed"),
                        Contains(Bounds[Row][Col], Scalar) && Contains(Bounds[Row][Col], Reassociated)
                            && Contains(Bounds[Row][Col], Fused));
                }
            }
        }
    }
    auto Identity = FMatrixInterval{};
    for (int32 Row = 0; Row < 3; ++Row)
    {
        for (int32 Col = 0; Col < 3; ++Col)
        { Identity[Row][Col] = Point(Row == Col ? 1.0 : 0.0); }
    }
    auto Inverse = FMatrixInterval{};
    Get_InverseBounds(Identity, Inverse);
    TestTrue(TEXT("Identity inverse encloses one and exact zero"), Contains(Inverse[0][0], 1.0) && Contains(Inverse[1][0], 0.0));
    auto Aliased = Identity;
    Get_InverseBounds(Aliased, Aliased);
    TestTrue(TEXT("Private inverse output may alias its input without partial evaluation"),
        Contains(Aliased[0][0], 1.0) && Contains(Aliased[1][0], 0.0) && Contains(Aliased[2][2], 1.0));
    Identity[2] = Identity[1];
    auto SingularInverse = Inverse;
    Get_InverseBounds(Identity, SingularInverse);
    TestFalse(TEXT("An uncertain/singular determinant declines"), SingularInverse[0][0]._Valid);
    for (auto Row = 0; Row < 3; ++Row)
    {
        for (auto Column = 0; Column < 3; ++Column)
        {
            TestFalse(TEXT("Rejected private inverse clears every previously valid output component"), SingularInverse[Row][Column]._Valid);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyCertificateDomainTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeCertificate.CandidateDomainAndRepresentedLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkBodyCertificateDomainTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_body_envelope_certificate;
    using namespace ck_test_body_certificate;
    const auto Extents = FVector{50.0, 28.0, 22.0};
    const auto EndQ = FRotator{0.0, 20.0, 0.0}.Quaternion();
    const auto Frame = FQuat::Slerp(FQuat::Identity, EndQ, 0.5).GetNormalized().Rotator();
    const auto Old = OldYawHalfExtents(Extents, 1.0f, 20.0);
    const auto Try = [&](const FTransform& InStart, const FTransform& InEnd, const FVector& InOld)
    { return TryGet_CertifiedHalfExtents(Extents, 1.0f, InStart, InEnd, Frame, InOld); };
    const auto End = FTransform{EndQ, FVector{1.0, 0.0, 0.0}};
    const auto Candidate = Try(FTransform::Identity, End, Old);
    if (Get_IsSupportedArithmetic())
    {
        TestTrue(TEXT("A bounded near-origin yaw proposal obtains a complete rounded-box certificate"), Candidate.IsSet());
        if (Candidate.IsSet())
        {
            const auto Native = ck::jolt::Conv(*Candidate);
            const auto NativeOld = ck::jolt::Conv(Old);
            TestTrue(TEXT("Finally represented components never exceed old native dimensions"),
                Native.GetX() <= NativeOld.GetX() && Native.GetY() <= NativeOld.GetY() && Native.GetZ() <= NativeOld.GetZ());
            TestTrue(TEXT("The positive certificate actually tightens this yaw proposal"), Candidate->X < Old.X || Candidate->Y < Old.Y);
        }
    }
    else
    { TestFalse(TEXT("Unsupported arithmetic always retains old dimensions"), Candidate.IsSet()); }
    TestFalse(TEXT("Stationary rotation always keeps its original enclosure"), Try(FTransform::Identity, FTransform::Identity, Old).IsSet());
    TestFalse(TEXT("Nonunit scale always retains old geometry"),
        Try(FTransform::Identity, FTransform{EndQ, FVector::ZeroVector, FVector{1.00001}}, Old).IsSet());
    TestFalse(TEXT("An ambiguous shortest-arc sign retains old geometry"),
        Try(FTransform::Identity, FTransform{FQuat{1.0, 0.0, 0.0, 0.0}, FVector::ZeroVector}, Old).IsSet());
    TestFalse(TEXT("Raw endpoint norm error is charged rather than normalized away"),
        Try(FTransform::Identity, FTransform{FQuat{0.0, 0.0, EndQ.Z * 1.01, EndQ.W * 1.01}, FVector::ZeroVector}, Old).IsSet());
    TestFalse(TEXT("Dimensions that cannot cover the authored box decline"), Try(FTransform::Identity, End, FVector{2.0}).IsSet());
    const auto ConstantLargeAxis = FVector{0.0, 392000.0, 0.0};
    const auto ConstantAxisCertificate = Try(FTransform{ConstantLargeAxis},
        FTransform{EndQ, ConstantLargeAxis + FVector{1.0, 0.0, 0.0}}, Old);
    if (Get_IsSupportedArithmetic())
    {
        TestTrue(TEXT("A large exactly constant native axis needs no translated-center evaluation error"), ConstantAxisCertificate.IsSet());
    }
    TestFalse(TEXT("The same large axis receives its full error bound once its native direction is nonzero"),
        Try(FTransform{ConstantLargeAxis}, FTransform{EndQ, ConstantLargeAxis + FVector{1.0, 1.0, 0.0}}, Old).IsSet());
    const auto TinyAuthorEnd = FVector{1.0, 1.0e-50, 0.0};
    TestTrue(TEXT("The zero-direction control includes a nonzero authored component rounded to native zero"),
        TinyAuthorEnd.Y != 0.0 && ck::jolt::Conv(TinyAuthorEnd).GetY() == 0.0f);
    if (Get_IsSupportedArithmetic())
    {
        TestTrue(TEXT("Native zero evaluation keeps the nonzero authored endpoint discrepancy within the full certificate"),
            Try(FTransform::Identity, FTransform{EndQ, TinyAuthorEnd}, Old).IsSet());
    }
    const auto Far = FVector{392000.0, 392000.0, 392000.0};
    TestFalse(TEXT("Large native translated-center evaluation error is not silently omitted"),
        Try(FTransform{Far}, FTransform{EndQ, Far + FVector{1.0, 0.0, 0.0}}, Old).IsSet());
    // Crossing the binary32 binade changes only the separately rounded end-overlap center.
    // S and both signed D values are exact; S+D equals each authored endpoint in both cases.
    constexpr auto Binade = 131072.0;
    constexpr auto HalfUpperUlp = 0.0078125;
    const auto PairedStart = FVector{Binade};
    const auto MinusEnd = PairedStart - FVector{HalfUpperUlp};
    const auto PlusEnd = PairedStart + FVector{HalfUpperUlp};
    const auto NativeStart = ck::jolt::Conv(PairedStart);
    const auto NativeMinusDirection = ck::jolt::Conv(MinusEnd - PairedStart);
    const auto NativePlusDirection = ck::jolt::Conv(PlusEnd - PairedStart);
    const auto NativeMinusEnd = ck::jolt::Conv(MinusEnd);
    const auto NativePlusEnd = ck::jolt::Conv(PlusEnd);
    for (auto Axis = 0; Axis < 3; ++Axis)
    {
        TestTrue(TEXT("Paired source start and signed cast directions are exactly represented"),
            NativeStart[Axis] == Binade && NativeMinusDirection[Axis] == -HalfUpperUlp
                && NativePlusDirection[Axis] == HalfUpperUlp);
        TestTrue(TEXT("Both nominal native cast ends exactly equal the authored endpoints"),
            static_cast<double>(NativeStart[Axis]) + NativeMinusDirection[Axis] == MinusEnd[Axis]
                && static_cast<double>(NativeStart[Axis]) + NativePlusDirection[Axis] == PlusEnd[Axis]);
        TestTrue(TEXT("Only the separate upper-binade end overlap introduces a half-ULP discrepancy"),
            static_cast<double>(NativeMinusEnd[Axis]) == MinusEnd[Axis]
                && NativePlusEnd[Axis] == Binade && PlusEnd[Axis] - NativePlusEnd[Axis] == HalfUpperUlp);
    }
    const auto PairedRotation = FRotator{0.0, 1.0, 0.0}.Quaternion();
    const auto PairedFrame = FQuat::Slerp(FQuat::Identity, PairedRotation, 0.5).GetNormalized().Rotator();
    const auto PairedOld = OldYawHalfExtents(Extents, 1.0f, 1.0);
    const auto MinusCertificate = TryGet_CertifiedHalfExtents(Extents, 1.0f, FTransform{PairedStart},
        FTransform{PairedRotation, MinusEnd}, PairedFrame, PairedOld);
    const auto PlusCertificate = TryGet_CertifiedHalfExtents(Extents, 1.0f, FTransform{PairedStart},
        FTransform{PairedRotation, PlusEnd}, PairedFrame, PairedOld);
    if (Get_IsSupportedArithmetic())
    {
        TestTrue(TEXT("The complete lower-binade production certificate admits its bounded center path"), MinusCertificate.IsSet());
        TestFalse(TEXT("Separate end-overlap rounding alone makes the paired production certificate decline"), PlusCertificate.IsSet());
    }
    else
    {
        TestFalse(TEXT("Unsupported arithmetic declines the lower paired candidate"), MinusCertificate.IsSet());
        TestFalse(TEXT("Unsupported arithmetic declines the upper paired candidate"), PlusCertificate.IsSet());
    }
    auto Queries = 17;
    auto Observation = ck::FProceduralBodyEnvelopeEnclosureObservation{};
    TestFalse(TEXT("Invalid public composition does not perform any native query"),
        ck::Get_IsProceduralBodyEnvelopePathClear(nullptr, FCk_SurfaceMotion_AdaptiveHeight{}, FCk_Jolt_QueryFilter{},
            FTransform::Identity, End, Queries, 20, nullptr, nullptr,
            ck::EProceduralBodyEnvelopeEndpointValidation::ConservativeEnclosure, &Observation));
    TestEqual(TEXT("Arithmetic rejection grants no native query credit or charge"), Queries, 17);
    TestFalse(TEXT("Invalid public composition publishes no enclosure"), Observation.Get_HasObservation());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyCertificateArchivedTuplesTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeCertificate.CompleteArchivedTuples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkBodyCertificateArchivedTuplesTest::
    RunTest(const FString&)
    -> bool
{
    // Original full tuples from Offline-Enclosure-Experiment-03/Evidence.json (AAC0B758...); no collider IDs.
    struct FTuple
    {
        const TCHAR* _Name;
        FVector _Start;
        FVector _End;
        FQuat _Q0;
        FQuat _Q1;
        FVector _Half;
    };
    const FTuple Tuples[] = {
        {TEXT("Beast4"), {119577.7848657318, 392450.7671547844, 803.7803469705958},
            {119577.78125, 392457.09375, 804.0},
            {-0.180685326776078, -0.008880898502661523, 0.9834996066505589, 0.001570365500017835},
            {0.0, 0.0, 0.7071067811865475, 0.7071067811865476}, {50.0, 28.0, 22.0}},
        {TEXT("Crawler6"), {119431.92940350738, 391550.0, 725.3257300400668},
            {119429.98495526728, 391550.0, 727.075733456158}, {0.0, -0.0, 0.0, 1.0},
            {0.0, -0.020360808516330337, 0.0, 0.9997926972510659}, {26.0, 30.0, 14.0}},
        {TEXT("Tentacled6"), {119421.6925903887, 392000.0, 722.7062159834134},
            {119421.6925903887, 392000.0, 724.7062198875176}, {0.0, -0.0, 0.0, 1.0},
            {0.0, -0.023269002435474268, 0.0, 0.9997292401073693}, {30.0, 30.0, 20.0}},
        {TEXT("MeasuredBeam"), {121053.977355, 345000.0, 682.472258},
            {121051.97735, 345000.0, 682.498459}, {0.0, 0.0, 0.908144137, 0.418657648},
            {0.0, 0.0, 0.917639995, 0.397412682}, {50.0, 28.0, 22.0}}
    };
    for (const auto& Tuple : Tuples)
    {
        const auto Old = ck_test_body_certificate::OldHalfExtents(Tuple._Half, Tuple._Q0, Tuple._Q1);
        const auto Frame = FQuat::Slerp(Tuple._Q0, Tuple._Q1, 0.5).GetNormalized().Rotator();
        const auto Candidate = ck_body_envelope_certificate::TryGet_CertifiedHalfExtents(Tuple._Half, 1.0f,
            FTransform{Tuple._Q0, Tuple._Start}, FTransform{Tuple._Q1, Tuple._End}, Frame, Old);
        AddInfo(FString::Printf(TEXT("Archived full tuple %s certificate=%s"), Tuple._Name,
            Candidate.IsSet() ? TEXT("selected") : TEXT("old fallback")));
        if (Candidate.IsSet())
        {
            const auto Native = ck::jolt::Conv(*Candidate);
            const auto NativeOld = ck::jolt::Conv(Old);
            TestTrue(TEXT("Archived selected represented geometry is componentwise no larger than old"),
                Native.GetX() <= NativeOld.GetX() && Native.GetY() <= NativeOld.GetY() && Native.GetZ() <= NativeOld.GetZ());
        }
    }
    return true;
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkBodyCertificateNativeCompositionTest,
    "Ck.ProceduralAnimation.Core.BodyEnvelopeCertificate.NativeCompositionAndOldFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
    FCkBodyCertificateNativeCompositionTest::
    RunTest(const FString&)
    -> bool
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            using namespace ck_test_body_certificate;
            auto* Jolt = InServer->GetSubsystem<UCk_Jolt_Subsystem>();
            const auto Physics = ck::IsValid(Jolt) ? Jolt->Get_PhysicsSystem().Pin() : nullptr;
            if (NOT TestTrue(TEXT("The native composition fixture owns a real Jolt world"), Physics.IsValid()))
            { return; }
            const auto Extents = FVector{50.0, 28.0, 22.0};
            const auto Settings = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                .Set_BodyHalfExtents(Extents).Set_CollisionMargin(1.0f);
            const auto Filter = FCk_Jolt_QueryFilter{}.Set_BodySelection(ECk_Jolt_QueryBodySelection::StaticNonSensor);
            const auto Origin = FVector{1000.0, 1000.0, 8000.0};
            const auto Start = FTransform{Origin};
            const auto End = FTransform{FRotator{0.0, 20.0, 0.0}.Quaternion(), Origin + FVector{1.0, 0.0, 0.0}};
            auto Observation = ck::FProceduralBodyEnvelopeEnclosureObservation{};
            auto Queries = 0;
            TestTrue(TEXT("A certified rotating path still requires the real three-query native composition"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Settings, Filter, Start, End, Queries, 3,
                    nullptr, nullptr, ck::EProceduralBodyEnvelopeEndpointValidation::ConservativeEnclosure, &Observation));
            TestEqual(TEXT("Clear composition charges start overlap, sweep and end overlap"), Queries, 3);
            const auto Old = OldHalfExtents(Extents, Start.GetRotation(), End.GetRotation());
            TestTrue(TEXT("Public observation carries finally selected geometry no larger than old"),
                Observation.Get_HasObservation() && Observation.Get_HalfExtents().X <= Old.X
                    && Observation.Get_HalfExtents().Y <= Old.Y && Observation.Get_HalfExtents().Z <= Old.Z);
            if (ck_body_envelope_certificate::Get_IsSupportedArithmetic())
            {
                const auto NativeOld = ck::jolt::Conv(Old);
                const auto& Selected = Observation.Get_NativeHalfExtents();
                TestTrue(TEXT("The supported real native composition actually selected a strictly tighter represented enclosure"),
                    Observation.Get_HasObservation() && (Selected.X < NativeOld.GetX()
                        || Selected.Y < NativeOld.GetY() || Selected.Z < NativeOld.GetZ()));
            }
            Queries = 0;
            TestTrue(TEXT("Exact stationary endpoint mode retains its fourth native query"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Settings, Filter, Start, End, Queries, 4,
                    nullptr, nullptr, ck::EProceduralBodyEnvelopeEndpointValidation::ExactStationaryEndpoint));
            TestEqual(TEXT("Exact endpoint mode charges exactly four queries"), Queries, 4);
            Queries = 0;
            TestFalse(TEXT("The certificate cannot bypass a two-query budget"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Settings, Filter, Start, End, Queries, 2));
            TestEqual(TEXT("Budget rejection retains its original charge"), Queries, 2);
            auto ScaledEnd = End;
            ScaledEnd.SetScale3D(FVector{1.00001});
            Queries = 0;
            TestTrue(TEXT("A tolerated old rigid input still uses native queries when the optional certificate declines"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Settings, Filter, Start, ScaledEnd, Queries, 3,
                    nullptr, nullptr, ck::EProceduralBodyEnvelopeEndpointValidation::ConservativeEnclosure, &Observation));
            TestTrue(TEXT("Certificate decline preserves every old half-extent bit"), Observation.Get_HalfExtents() == Old);
            const auto Signature = ck::jolt::TryDerive_SignatureFromProfile(TEXT("BlockAll"), ECk_Jolt_BodyDomain::Static);
            if (NOT TestTrue(TEXT("The initial-overlap fixture derives its real layer"), Signature.IsSet()))
            { return; }
            const auto Layer = JPH::ObjectLayer{Jolt->Get_LayerTable().Get_OrRegisterLayer(*Signature)};
            auto Shape = JPH::Ref<JPH::Shape>{new JPH::BoxShape{JPH::Vec3{1.0f, 1.0f, 1.0f}}};
            auto BodySettings = JPH::BodyCreationSettings{Shape.GetPtr(), ck::jolt::Conv(Origin),
                JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layer};
            auto& Bodies = Physics->GetBodyInterface();
            const auto Id = Bodies.CreateAndAddBody(BodySettings, JPH::EActivation::DontActivate);
            if (NOT TestFalse(TEXT("The native blocker was created"), Id.IsInvalid()))
            { return; }
            ON_SCOPE_EXIT { Bodies.RemoveBody(Id); Bodies.DestroyBody(Id); };
            Queries = 0;
            auto Overlap = TOptional<ck::FProceduralBodyEnvelopeOverlapObservation>{};
            TestFalse(TEXT("The optional enclosure does not grant clearance across an actual initial overlap"),
                ck::Get_IsProceduralBodyEnvelopePathClear(InServer, Settings, Filter, Start, End, Queries, 3, nullptr, &Overlap));
            TestEqual(TEXT("Actual initial overlap still rejects in query slot one"), Queries, 1);
            TestTrue(TEXT("The observation keeps actual start-overlap provenance"), Overlap.IsSet()
                && Overlap->Get_Kind() == ck::EProceduralBodyEnvelopeOverlapKind::StartEnclosureOverlap
                && NOT Overlap->Get_Hits().IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

#endif
