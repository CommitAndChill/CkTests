#include "CkGym_WireTransformRuntime.h"
#include "../CkUnitTest_Common.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace ck_gym_wire_transform_test
{
    auto SameBits(double A, double B) -> bool
    { return FMemory::Memcmp(&A, &B, sizeof(double)) == 0; }
    auto SameBits(const FRotator& A, const FRotator& B) -> bool
    { return SameBits(A.Pitch,B.Pitch) && SameBits(A.Yaw,B.Yaw) && SameBits(A.Roll,B.Roll); }
    auto SameBits(const FQuat& A, const FQuat& B) -> bool
    {
        return SameBits(A.X,B.X) && SameBits(A.Y,B.Y) &&
            SameBits(A.Z,B.Z) && SameBits(A.W,B.W);
    }
    auto PutRotation(FTransform& T, const FQuat& Q) -> bool
    {
        const auto* P=FindFProperty<FStructProperty>(TBaseStructure<FTransform>::Get(),TEXT("Rotation"));
        if (!P || P->Struct!=TBaseStructure<FQuat>::Get()) { return false; }
        *P->ContainerPtrToValuePtr<FQuat>(&T)=Q;
        return true;
    }
    auto PutTranslation(FTransform& T, const FVector& V) -> bool
    {
        const auto* P=FindFProperty<FStructProperty>(TBaseStructure<FTransform>::Get(),TEXT("Translation"));
        if (!P || P->Struct!=TBaseStructure<FVector>::Get()) { return false; }
        *P->ContainerPtrToValuePtr<FVector>(&T)=V;
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkGymWireTransform,
    "Ck.Gym.WireTransform.IrisExactRoundTrip", ck::tests::kCkUnitTestFlags)

bool FCkGymWireTransform::RunTest(const FString& Parameters)
{
    using U = UCk_Gym_WireTransform_Runtime_UE;
    const FTransform Yaw180(FRotator(0,180,0), FVector(500,-2000,0));
    const FQuat Axis(FVector(1,2,3).GetSafeNormal(), FMath::DegreesToRadians(71.0));
    const FTransform Arbitrary(Axis, FVector(1.25,-8.5,10.0), FVector(2,3,4));
    for (const FTransform& Source : {FTransform::Identity, Yaw180, Arbitrary})
    {
        const FTransform Saved = Source;
        FTransform Expected;
        TestTrue(TEXT("public Iris round-trip succeeds"), U::TryGet_ExpectedIrisTransform(Source,Expected));
        TestTrue(TEXT("expected wire transform accepted"), U::Get_IsExactIrisTransform(Source,Expected));
        TestTrue(TEXT("authored or wire transform accepted"), U::Get_IsExactAuthoredOrIrisTransform(Source,Expected));
        TestTrue(TEXT("input quaternion remains unchanged"),
            ck_gym_wire_transform_test::SameBits(Saved.GetRotation(),Source.GetRotation()));
        FTransform Wrong = Expected;
        Wrong.SetTranslation(Expected.GetTranslation() + FVector(1,0,0));
        TestFalse(TEXT("received position perturbation rejected"), U::Get_IsExactIrisTransform(Source,Wrong));
        Wrong = Expected;
        Wrong.SetRotation(FQuat(-Expected.GetRotation().X,Expected.GetRotation().Y,
            Expected.GetRotation().Z,Expected.GetRotation().W));
        if (!ck_gym_wire_transform_test::SameBits(Wrong.GetRotation(),Expected.GetRotation()))
        { TestFalse(TEXT("received quaternion sign perturbation rejected"), U::Get_IsExactIrisTransform(Source,Wrong)); }
    }
    FTransform WireYaw;
    TestTrue(TEXT("yaw 180 expected wire form"), U::TryGet_ExpectedIrisTransform(Yaw180,WireYaw));
    TestFalse(TEXT("yaw 180 raw W is reconstructed by Iris"),
        ck_gym_wire_transform_test::SameBits(Yaw180.GetRotation(),WireYaw.GetRotation()));
    FTransform Alias = Yaw180;
    TestFalse(TEXT("alias output rejected"), U::TryGet_ExpectedIrisTransform(Alias,Alias));
    TestTrue(TEXT("alias input stays unchanged"),
        ck_gym_wire_transform_test::SameBits(Alias.GetRotation(),Yaw180.GetRotation()));

    FTransform SignedZero = FTransform::Identity;
    SignedZero.SetTranslation(FVector(-0.0, 0.0, 1.0));
    FTransform SignedExpected;
    TestTrue(TEXT("signed zero source accepted"), U::TryGet_ExpectedIrisTransform(SignedZero,SignedExpected));
    TestTrue(TEXT("signed zero is retained"),
        ck_gym_wire_transform_test::SameBits(SignedZero.GetTranslation().X,SignedExpected.GetTranslation().X));
    FTransform PlusZero = SignedExpected;
    PlusZero.SetTranslation(FVector(0.0,0.0,1.0));
    TestFalse(TEXT("signed zero change rejected"), U::Get_IsExactIrisTransform(SignedZero,PlusZero));

    FTransform Bad = FTransform::Identity;
    FQuat Malformed[] = {FQuat(0,0,0,0), FQuat(0,0,0,2),
        FQuat::Identity, FQuat::Identity};
    Malformed[2].X=std::numeric_limits<double>::quiet_NaN();
    Malformed[3].X=std::numeric_limits<double>::infinity();
    for (const FQuat& Q : Malformed)
    {
        TestTrue(TEXT("malformed test rotation installed without transform diagnostics"),
            ck_gym_wire_transform_test::PutRotation(Bad,Q));
        FTransform Out = Yaw180;
        TestFalse(TEXT("invalid source rejected"), U::TryGet_ExpectedIrisTransform(Bad,Out));
        TestTrue(TEXT("failure clears output"), Out.Equals(FTransform::Identity,0.0));
        const auto* Rotation=FindFProperty<FStructProperty>(TBaseStructure<FTransform>::Get(),TEXT("Rotation"));
        TestNotNull(TEXT("rotation reflection exists"), Rotation);
        if (Rotation)
        {
            const auto* Raw=Rotation->ContainerPtrToValuePtr<FQuat>(&Bad);
            TestTrue(TEXT("rejected source quaternion bits unchanged"),
                ck_gym_wire_transform_test::SameBits(Q,*Raw));
        }
        TestFalse(TEXT("invalid source not accepted as authored"),
            U::Get_IsExactAuthoredOrIrisTransform(Bad,Bad));
    }
    Bad = FTransform::Identity;
    FVector Infinite = FVector::ZeroVector;
    Infinite.X = std::numeric_limits<double>::infinity();
    TestTrue(TEXT("malformed test translation installed without transform diagnostics"),
        ck_gym_wire_transform_test::PutTranslation(Bad,Infinite));
    FTransform Out = Yaw180;
    TestFalse(TEXT("infinite translation rejected"), U::TryGet_ExpectedIrisTransform(Bad,Out));
    TestTrue(TEXT("infinite translation clears output"), Out.Equals(FTransform::Identity,0.0));
    const auto* Translation=FindFProperty<FStructProperty>(TBaseStructure<FTransform>::Get(),TEXT("Translation"));
    TestNotNull(TEXT("translation reflection exists"), Translation);
    if (Translation)
    {
        const auto* Before=Translation->ContainerPtrToValuePtr<FVector>(&Bad);
        const double RawX=Before->X;
        TestFalse(TEXT("malformed alias rejected"), U::TryGet_ExpectedIrisTransform(Bad,Bad));
        const auto* After=Translation->ContainerPtrToValuePtr<FVector>(&Bad);
        TestTrue(TEXT("malformed alias retains raw input"),
            ck_gym_wire_transform_test::SameBits(RawX,After->X));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkRotateTowardsWireRotator,
    "Ck.RotateTowardsGym.WireRotator.IrisExactRoundTrip", ck::tests::kCkUnitTestFlags)

bool FCkRotateTowardsWireRotator::RunTest(const FString& Parameters)
{
    using U = UCk_Gym_WireTransform_Runtime_UE;
    const FRotator RotatorSource(12.345, -179.123, 0.0);
    FRotator RotatorExpected;
    TestTrue(TEXT("rotator exact Iris round-trip succeeds"),
        U::TryGet_ExpectedIrisRotator(RotatorSource,RotatorExpected));
    TestTrue(TEXT("rotator exact wire value accepted"),
        U::Get_IsExactIrisRotator(RotatorSource,RotatorExpected));
    TestFalse(TEXT("rotator short-axis wire differs from source doubles"),
        ck_gym_wire_transform_test::SameBits(RotatorSource,RotatorExpected));
    FRotator RotatorWrong = RotatorExpected;
    RotatorWrong.Yaw += 360.0 / 65536.0;
    TestFalse(TEXT("one short-axis step perturbation rejected"),
        U::Get_IsExactIrisRotator(RotatorSource,RotatorWrong));
    FRotator RotatorAlias = RotatorSource;
    TestFalse(TEXT("rotator alias output rejected"),
        U::TryGet_ExpectedIrisRotator(RotatorAlias,RotatorAlias));
    TestTrue(TEXT("rotator alias input remains raw-identical"),
        ck_gym_wire_transform_test::SameBits(RotatorAlias,RotatorSource));
    FRotator RotatorInvalid = RotatorSource;
    RotatorInvalid.Pitch = std::numeric_limits<double>::quiet_NaN();
    RotatorExpected = RotatorSource;
    TestFalse(TEXT("nonfinite rotator source rejected"),
        U::TryGet_ExpectedIrisRotator(RotatorInvalid,RotatorExpected));
    TestTrue(TEXT("nonfinite rotator failure clears output"),
        ck_gym_wire_transform_test::SameBits(RotatorExpected,FRotator::ZeroRotator));
    TestFalse(TEXT("nonfinite received rotator rejected"),
        U::Get_IsExactIrisRotator(RotatorSource,RotatorInvalid));
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        for (double Invalid : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity(),
                               1000001.0, -1000001.0,
                               std::numeric_limits<double>::max(),
                               -std::numeric_limits<double>::max()})
        {
            FRotator BadAxis = RotatorSource;
            double* Field = Axis == 0 ? &BadAxis.Pitch : Axis == 1 ? &BadAxis.Yaw : &BadAxis.Roll;
            *Field = Invalid;
            const FRotator Before = BadAxis;
            FRotator Cleared = RotatorSource;
            TestFalse(TEXT("each unsupported finite/nonfinite axis rejected before Iris"),
                U::TryGet_ExpectedIrisRotator(BadAxis,Cleared));
            TestTrue(TEXT("unsupported axis clears output"),
                ck_gym_wire_transform_test::SameBits(Cleared,FRotator::ZeroRotator));
            TestTrue(TEXT("rejected axis raw bits retained"),
                ck_gym_wire_transform_test::SameBits(BadAxis,Before));
        }
    }
    return true;
}
#endif
