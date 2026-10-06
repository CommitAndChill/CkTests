#include "CkGym_WireTransformRuntime.h"

#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/Serialization/QuatNetSerializers.h"
#include "Iris/Serialization/RotatorNetSerializers.h"
#include "Iris/Serialization/VectorNetSerializers.h"
#include "UObject/NoExportTypes.h"
#include "UObject/UnrealType.h"

namespace ck_gym_wire_transform
{
    static_assert(sizeof(decltype(FQuat::X)) == sizeof(double));
    static_assert(sizeof(decltype(FVector::X)) == sizeof(double));
    struct FParts
    {
        FQuat Rotation;
        FVector Translation;
        FVector Scale;
    };

    auto TryReadParts(const FTransform& Value, FParts& Out) -> bool
    {
        const auto* Type = TBaseStructure<FTransform>::Get();
        const auto* Rotation = Type ? FindFProperty<FStructProperty>(Type, TEXT("Rotation")) : nullptr;
        const auto* Translation = Type ? FindFProperty<FStructProperty>(Type, TEXT("Translation")) : nullptr;
        const auto* Scale = Type ? FindFProperty<FStructProperty>(Type, TEXT("Scale3D")) : nullptr;
        if (!Rotation || Rotation->Struct != TBaseStructure<FQuat>::Get() ||
            !Translation || Translation->Struct != TBaseStructure<FVector>::Get() ||
            !Scale || Scale->Struct != TBaseStructure<FVector>::Get()) { return false; }
        // Reflected member reads avoid FTransform getters' NaN diagnostics and sanitizing writes.
        Out.Rotation = *Rotation->ContainerPtrToValuePtr<FQuat>(&Value);
        Out.Translation = *Translation->ContainerPtrToValuePtr<FVector>(&Value);
        Out.Scale = *Scale->ContainerPtrToValuePtr<FVector>(&Value);
        return true;
    }

    auto IsFinite(const FVector& V) -> bool
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
    }

    auto IsFinite(const FQuat& Q) -> bool
    {
        return FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) &&
            FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W);
    }

    auto IsSupported(const FTransform& T) -> bool
    {
        FParts P;
        return TryReadParts(T, P) && IsFinite(P.Translation) && IsFinite(P.Scale) &&
            IsFinite(P.Rotation) && P.Rotation.SizeSquared() > 0.0 && P.Rotation.IsNormalized();
    }

    auto SameBits(double A, double B) -> bool
    {
        return FMemory::Memcmp(&A, &B, sizeof(double)) == 0;
    }

    auto IsFinite(const FRotator& R) -> bool
    {
        return FMath::IsFinite(R.Pitch) && FMath::IsFinite(R.Yaw) && FMath::IsFinite(R.Roll);
    }

    // CompressAxisToShort calls RoundToInt after scaling by 65536/360.
    // Keep every axis well inside int32 conversion range before invoking Iris.
    auto IsSupportedRotatorSource(const FRotator& R) -> bool
    {
        constexpr double MaxSafeWindingDegrees = 1000000.0;
        return IsFinite(R) && FMath::Abs(R.Pitch) <= MaxSafeWindingDegrees &&
            FMath::Abs(R.Yaw) <= MaxSafeWindingDegrees &&
            FMath::Abs(R.Roll) <= MaxSafeWindingDegrees;
    }

    auto SameBits(const FRotator& A, const FRotator& B) -> bool
    {
        return SameBits(A.Pitch, B.Pitch) && SameBits(A.Yaw, B.Yaw) && SameBits(A.Roll, B.Roll);
    }

    auto SameBits(const FVector& A, const FVector& B) -> bool
    {
        return SameBits(A.X, B.X) && SameBits(A.Y, B.Y) && SameBits(A.Z, B.Z);
    }

    auto SameBits(const FQuat& A, const FQuat& B) -> bool
    {
        return SameBits(A.X, B.X) && SameBits(A.Y, B.Y) &&
            SameBits(A.Z, B.Z) && SameBits(A.W, B.W);
    }

    auto SameBits(const FTransform& A, const FTransform& B) -> bool
    {
        FParts AP, BP;
        return TryReadParts(A, AP) && TryReadParts(B, BP) &&
            SameBits(AP.Translation, BP.Translation) && SameBits(AP.Scale, BP.Scale) &&
            SameBits(AP.Rotation, BP.Rotation);
    }

    auto HasExpectedDescriptor() -> bool
    {
        static const bool Supported = []() -> bool
        {
        const auto* Transform = TBaseStructure<FTransform>::Get();
        if (!Transform) { return false; }
        const auto Descriptor = UE::Net::FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(Transform);
        if (!Transform || !Descriptor || Descriptor->MemberCount != 3 ||
            !Descriptor->MemberProperties || !Descriptor->MemberSerializerDescriptors) { return false; }
        bool Rotation = false, Translation = false, Scale = false;
        for (uint16 I = 0; I < Descriptor->MemberCount; ++I)
        {
            const auto* Property = CastField<FStructProperty>(Descriptor->MemberProperties[I]);
            const auto* Serializer = Descriptor->MemberSerializerDescriptors[I].Serializer;
            if (!Property || !Serializer) { return false; }
            if (Property->GetFName() == FName(TEXT("Rotation")))
            {
                if (Property->Struct != TBaseStructure<FQuat>::Get() ||
                    Serializer != &UE_NET_GET_SERIALIZER(UE::Net::FUnitQuatNetSerializer)) { return false; }
                Rotation = true;
            }
            else if (Property->GetFName() == FName(TEXT("Translation")))
            {
                if (Property->Struct != TBaseStructure<FVector>::Get() ||
                    Serializer != &UE_NET_GET_SERIALIZER(UE::Net::FVectorNetSerializer)) { return false; }
                Translation = true;
            }
            else if (Property->GetFName() == FName(TEXT("Scale3D")))
            {
                if (Property->Struct != TBaseStructure<FVector>::Get() ||
                    Serializer != &UE_NET_GET_SERIALIZER(UE::Net::FVectorNetSerializer)) { return false; }
                Scale = true;
            }
            else { return false; }
        }
        return Rotation && Translation && Scale;
        }();
        return Supported;
    }

    auto HasExpectedRotatorDescriptor() -> bool
    {
        static const bool Supported = []() -> bool
        {
            const auto* Type = TBaseStructure<FRotator>::Get();
            if (!Type) { return false; }
            const auto Descriptor = UE::Net::FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(Type);
            return Descriptor && Descriptor->MemberCount == 1 &&
                Descriptor->MemberSerializerDescriptors &&
                Descriptor->MemberSerializerDescriptors[0].Serializer ==
                    &UE_NET_GET_SERIALIZER(UE::Net::FRotatorNetSerializer);
        }();
        return Supported;
    }

    auto TryWireRotator(const FRotator& Source, FRotator& Out) -> bool
    {
        Out = FRotator::ZeroRotator;
        const auto& Serializer = UE_NET_GET_SERIALIZER(UE::Net::FRotatorNetSerializer);
        if (!Serializer.Quantize || !Serializer.Serialize || !Serializer.Deserialize ||
            !Serializer.Dequantize || Serializer.QuantizedTypeSize > 64 ||
            Serializer.QuantizedTypeAlignment > 16 || !Serializer.DefaultConfig) { return false; }

        alignas(16) uint8 Quantized[64]{};
        alignas(16) uint8 Received[64]{};
        alignas(4) uint32 Wire[8]{};
        UE::Net::FNetSerializationContext QuantizeContext;
        UE::Net::FNetQuantizeArgs QuantizeArgs{};
        QuantizeArgs.NetSerializerConfig = Serializer.DefaultConfig;
        QuantizeArgs.Version = Serializer.Version;
        QuantizeArgs.Source = reinterpret_cast<UPTRINT>(&Source);
        QuantizeArgs.Target = reinterpret_cast<UPTRINT>(Quantized);
        Serializer.Quantize(QuantizeContext, QuantizeArgs);
        if (QuantizeContext.HasErrorOrOverflow()) { return false; }

        UE::Net::FNetBitStreamWriter Writer;
        Writer.InitBytes(Wire, sizeof(Wire));
        UE::Net::FNetSerializationContext WriteContext(&Writer);
        UE::Net::FNetSerializeArgs WriteArgs{};
        WriteArgs.NetSerializerConfig = Serializer.DefaultConfig;
        WriteArgs.Version = Serializer.Version;
        WriteArgs.Source = reinterpret_cast<UPTRINT>(Quantized);
        Serializer.Serialize(WriteContext, WriteArgs);
        if (WriteContext.HasErrorOrOverflow() || Writer.IsOverflown()) { return false; }
        const uint32 Bits = Writer.GetPosBits();
        Writer.CommitWrites();
        if (Bits == 0 || Bits > sizeof(Wire) * 8) { return false; }

        UE::Net::FNetBitStreamReader Reader;
        Reader.InitBits(Wire, Bits);
        UE::Net::FNetSerializationContext ReadContext(&Reader);
        UE::Net::FNetDeserializeArgs ReadArgs{};
        ReadArgs.NetSerializerConfig = Serializer.DefaultConfig;
        ReadArgs.Version = Serializer.Version;
        ReadArgs.Target = reinterpret_cast<UPTRINT>(Received);
        Serializer.Deserialize(ReadContext, ReadArgs);
        if (ReadContext.HasErrorOrOverflow() || Reader.IsOverflown() ||
            Reader.GetPosBits() != Bits) { return false; }

        FRotator Candidate = FRotator::ZeroRotator;
        UE::Net::FNetSerializationContext DequantizeContext;
        UE::Net::FNetDequantizeArgs DequantizeArgs{};
        DequantizeArgs.NetSerializerConfig = Serializer.DefaultConfig;
        DequantizeArgs.Version = Serializer.Version;
        DequantizeArgs.Source = reinterpret_cast<UPTRINT>(Received);
        DequantizeArgs.Target = reinterpret_cast<UPTRINT>(&Candidate);
        Serializer.Dequantize(DequantizeContext, DequantizeArgs);
        if (DequantizeContext.HasErrorOrOverflow() || !IsFinite(Candidate)) { return false; }
        Out = Candidate;
        return true;
    }

    auto TryWireRotation(const FQuat& Source, FQuat& Out) -> bool
    {
        Out = FQuat::Identity;
        const auto& Serializer = UE_NET_GET_SERIALIZER(UE::Net::FUnitQuatNetSerializer);
        if (!Serializer.Quantize || !Serializer.Serialize || !Serializer.Deserialize ||
            !Serializer.Dequantize || Serializer.QuantizedTypeSize > 64 ||
            Serializer.QuantizedTypeAlignment > 16 ||
            !Serializer.DefaultConfig) { return false; }

        alignas(16) uint8 Quantized[64]{};
        alignas(16) uint8 Received[64]{};
        alignas(4) uint32 Wire[8]{};
        UE::Net::FNetSerializationContext QuantizeContext;
        UE::Net::FNetQuantizeArgs QuantizeArgs{};
        QuantizeArgs.NetSerializerConfig = Serializer.DefaultConfig;
        QuantizeArgs.Version = Serializer.Version;
        QuantizeArgs.Source = reinterpret_cast<UPTRINT>(&Source);
        QuantizeArgs.Target = reinterpret_cast<UPTRINT>(Quantized);
        Serializer.Quantize(QuantizeContext, QuantizeArgs);
        if (QuantizeContext.HasErrorOrOverflow()) { return false; }

        UE::Net::FNetBitStreamWriter Writer;
        Writer.InitBytes(Wire, sizeof(Wire));
        UE::Net::FNetSerializationContext WriteContext(&Writer);
        UE::Net::FNetSerializeArgs WriteArgs{};
        WriteArgs.NetSerializerConfig = Serializer.DefaultConfig;
        WriteArgs.Version = Serializer.Version;
        WriteArgs.Source = reinterpret_cast<UPTRINT>(Quantized);
        Serializer.Serialize(WriteContext, WriteArgs);
        if (WriteContext.HasErrorOrOverflow() || Writer.IsOverflown()) { return false; }
        const uint32 Bits = Writer.GetPosBits();
        Writer.CommitWrites();
        if (Bits == 0 || Bits > sizeof(Wire) * 8) { return false; }

        UE::Net::FNetBitStreamReader Reader;
        Reader.InitBits(Wire, Bits);
        UE::Net::FNetSerializationContext ReadContext(&Reader);
        UE::Net::FNetDeserializeArgs ReadArgs{};
        ReadArgs.NetSerializerConfig = Serializer.DefaultConfig;
        ReadArgs.Version = Serializer.Version;
        ReadArgs.Target = reinterpret_cast<UPTRINT>(Received);
        Serializer.Deserialize(ReadContext, ReadArgs);
        if (ReadContext.HasErrorOrOverflow() || Reader.IsOverflown() ||
            Reader.GetPosBits() != Bits) { return false; }

        FQuat Candidate = FQuat::Identity;
        UE::Net::FNetSerializationContext DequantizeContext;
        UE::Net::FNetDequantizeArgs DequantizeArgs{};
        DequantizeArgs.NetSerializerConfig = Serializer.DefaultConfig;
        DequantizeArgs.Version = Serializer.Version;
        DequantizeArgs.Source = reinterpret_cast<UPTRINT>(Received);
        DequantizeArgs.Target = reinterpret_cast<UPTRINT>(&Candidate);
        Serializer.Dequantize(DequantizeContext, DequantizeArgs);
        if (DequantizeContext.HasErrorOrOverflow() || !IsFinite(Candidate) ||
            !Candidate.IsNormalized()) { return false; }
        Out = Candidate;
        return true;
    }
}

bool UCk_Gym_WireTransform_Runtime_UE::TryGet_ExpectedIrisTransform(
    const FTransform& Source, FTransform& OutExpected)
{
    // One object cannot simultaneously retain its input and be cleared as a failed output.
    if (&Source == &OutExpected) { return false; }
    ck_gym_wire_transform::FParts Parts;
    const bool ValidSource = ck_gym_wire_transform::TryReadParts(Source, Parts) &&
        ck_gym_wire_transform::IsFinite(Parts.Translation) &&
        ck_gym_wire_transform::IsFinite(Parts.Scale) &&
        ck_gym_wire_transform::IsFinite(Parts.Rotation) &&
        Parts.Rotation.SizeSquared() > 0.0 && Parts.Rotation.IsNormalized();
    OutExpected = FTransform::Identity;
    if (!ValidSource || !ck_gym_wire_transform::HasExpectedDescriptor()) { return false; }
    FQuat WireRotation;
    if (!ck_gym_wire_transform::TryWireRotation(Parts.Rotation, WireRotation)) { return false; }
    OutExpected = FTransform(WireRotation, Parts.Translation, Parts.Scale);
    return true;
}

bool UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
    const FTransform& Source, const FTransform& Actual)
{
    if (!ck_gym_wire_transform::IsSupported(Actual)) { return false; }
    FTransform Expected;
    return TryGet_ExpectedIrisTransform(Source, Expected) &&
        ck_gym_wire_transform::SameBits(Expected, Actual);
}

bool UCk_Gym_WireTransform_Runtime_UE::Get_IsExactAuthoredOrIrisTransform(
    const FTransform& Authored, const FTransform& Actual)
{
    if (!ck_gym_wire_transform::IsSupported(Authored) ||
        !ck_gym_wire_transform::IsSupported(Actual) ||
        !ck_gym_wire_transform::HasExpectedDescriptor()) { return false; }
    return ck_gym_wire_transform::SameBits(Authored, Actual) ||
        Get_IsExactIrisTransform(Authored, Actual);
}

bool UCk_Gym_WireTransform_Runtime_UE::TryGet_ExpectedIrisRotator(
    const FRotator& Source, FRotator& OutExpected)
{
    if (&Source == &OutExpected) { return false; }
    const bool ValidSource = ck_gym_wire_transform::IsSupportedRotatorSource(Source);
    OutExpected = FRotator::ZeroRotator;
    if (!ValidSource || !ck_gym_wire_transform::HasExpectedRotatorDescriptor()) { return false; }
    return ck_gym_wire_transform::TryWireRotator(Source, OutExpected);
}

bool UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisRotator(
    const FRotator& Source, const FRotator& Actual)
{
    if (!ck_gym_wire_transform::IsFinite(Actual)) { return false; }
    FRotator Expected;
    return TryGet_ExpectedIrisRotator(Source, Expected) &&
        ck_gym_wire_transform::SameBits(Expected, Actual);
}
