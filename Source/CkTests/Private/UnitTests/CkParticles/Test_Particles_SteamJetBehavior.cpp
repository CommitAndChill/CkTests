// Behavior-math gate for CkParticles BehaviorId 47 (SteamJet) — a continuous jet of steam along local +X.
//
// Drives UCkParticles_DataInterface::Execute_Stage_CPU, the SAME CPU mirror a CPU sim runs, so it needs no
// Niagara system, no template asset, no RHI and no forked engine.
//
// SteamJet is an ORIGINAL design, not a port: there is no source corpus, so every expected value below is a
// DESIGN constant declared in CkFoundation/Source/CkParticles/Cookbook/SteamJet.md, and the expectations are
// re-derived here from the same closed form rather than read off a reference system.
//
// The load-bearing claims:
//   - CLOSED FORM. Drag is integrated analytically, so position and velocity are functions of (Age, Seed)
//     alone — the same evaluation at two DeltaTimes must agree to the last float;
//   - the 7 degree cone around +X and the 520-680 uu/s launch speed, decayed by exp(-k t);
//   - the drag RANGE: along its launch direction a particle has travelled v0 (1 - exp(-k t)) / k, with v0
//     recomputed from the behavior's own rand chain (salt 3);
//   - the alpha envelope (zero at birth and at death), the growing size, the late dissolve, and the
//     translucent smoke renderer (VisTag 2) for every live particle.
//
// Cannot pass vacuously: an id the dispatch does not know falls back to Gravity, which writes VisTag 0, an
// opaque alpha of 1 and a velocity straight down.

#include "Misc/AutomationTest.h"

#include "CkParticles/DataInterface/CkParticles_DataInterface.h"
#include "CkParticles/ScriptDefinition/CkParticles_ScriptDefinition_Naming.h"

#include "../CkUnitTest_Common.h"

// --------------------------------------------------------------------------------------------------------------------

using ck::tests::kCkUnitTestFlags;

namespace ck_test_particles_steamjet
{
    constexpr auto kBehaviorId = 47;

    // The cadence row: continuous rate, no burst, one-second loop, 0.9 s life.
    constexpr auto kLoop      = 1.0f;
    constexpr auto kLifetime  = 0.9f;
    constexpr auto kBurst     = 0;
    constexpr auto kSpawnRate = 70.0f;

    constexpr auto kRowAssetName = TEXT("PS_CkParticles_Template_SteamJet");

    // Design constants (Cookbook/SteamJet.md).
    constexpr auto kDrag         = 2.2f;
    constexpr auto kSpeedLo      = 520.0f;
    constexpr auto kSpeedHi      = 680.0f;
    constexpr auto kApertureDeg  = 7.0f;
    constexpr auto kBuoyancy     = 35.0f;
    constexpr auto kSmokeVisTag  = 2;

    constexpr auto kTolerance = 1.0e-3f;

    // Mirrors CkParticles_Rand (Common.ush) — the behavior's own chain, so v0 and the cone angle are recomputed
    // rather than bounded loosely.
    auto Rand(int32 InSeed, int32 InSalt) -> float
    {
        uint32 n = uint32(InSeed) * 747796405u + uint32(InSalt) * 2891336453u + 1u;
        n ^= n >> 16;
        n *= 2246822519u;
        n ^= n >> 13;
        n *= 3266489917u;
        n ^= n >> 16;
        return float(n & 0x00FFFFFFu) / 16777216.0f;
    }

    auto Get_LaunchSpeed(int32 InSeed) -> float
    {
        return FMath::Lerp(kSpeedLo, kSpeedHi, Rand(InSeed, 3));
    }

    auto Get_Birth(int32 InSeed) -> FVector3f
    {
        const auto R  = 7.0f * FMath::Sqrt(Rand(InSeed, 1));
        const auto Th = 6.28318530718f * Rand(InSeed, 2);
        return FVector3f(0.0f, R * FMath::Cos(Th), R * FMath::Sin(Th));
    }

    auto Evaluate(float InAge, int32 InSeed, float InDeltaTime) -> FCk_Particles_StageResult
    {
        return UCkParticles_DataInterface::Execute_Stage_CPU(
            kBehaviorId, InDeltaTime, InAge, kLifetime,
            FVector3f::ZeroVector, FVector3f::ZeroVector, InSeed, InAge);
    }

    auto Evaluate(float InAge, int32 InSeed) -> FCk_Particles_StageResult
    {
        return Evaluate(InAge, InSeed, 1.0f / 60.0f);
    }

    auto Evaluate_AtNormalizedAge(float InNormalizedAge, int32 InSeed) -> FCk_Particles_StageResult
    {
        return Evaluate(InNormalizedAge * kLifetime, InSeed);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_Particles_SteamJetBehavior,
    "CkTests.UnitTests.CkParticles.SteamJetBehavior",
    kCkUnitTestFlags)

bool FCkTest_Particles_SteamJetBehavior::RunTest(const FString& Parameters)
{
    using namespace ck_test_particles_steamjet;

    // ---- The roster ----
    {
        TestTrue(TEXT("the roster includes behavior 47"), ck::particles::NumBehaviors >= kBehaviorId + 1);
        TestEqual(TEXT("behavior 47 is named SteamJet"),
            ck::particles::Get_BehaviorName(kBehaviorId), FName(TEXT("SteamJet")));
        TestEqual(TEXT("behavior 47 binds no CkUsf look — VisTag 2 draws through the smoke renderer's own material"),
            ck::particles::Get_BehaviorLookName(kBehaviorId), NAME_None);
        TestEqual(TEXT("behavior 47's shader file follows the roster convention"),
            ck::particles::Get_BehaviorShaderIncludePath(kBehaviorId),
            FString(TEXT("/CkParticles/Behaviors/Behavior_SteamJet.ush")));
    }

    // ---- The cadence row ----
    {
        const auto* RowSpec = static_cast<const ck::particles::FCk_ParticlesTemplateSpec*>(nullptr);
        for (const auto& Spec : ck::particles::Get_TemplateSpecs())
        {
            if (FString(Spec.AssetName) == FString(kRowAssetName))
            { RowSpec = &Spec; break; }
        }

        if (TestNotNull(TEXT("the cadence table declares a PS_CkParticles_Template_SteamJet row"), RowSpec))
        {
            TestEqual(TEXT("row loop duration"),     RowSpec->LoopDuration,     kLoop,      kTolerance);
            TestEqual(TEXT("row particle lifetime"), RowSpec->ParticleLifetime, kLifetime,  kTolerance);
            TestEqual(TEXT("row bursts nothing — a continuous jet"), RowSpec->BurstCount, kBurst);
            TestEqual(TEXT("row spawn rate"),        RowSpec->SpawnRate,        kSpawnRate, kTolerance);
            TestEqual(TEXT("the row declares no renderers of its own — the shared smoke sprite draws it"),
                RowSpec->RendererOverrides.Num(), 0);
            TestFalse(TEXT("the row declares no ribbon emitter"), RowSpec->RibbonEmitter.Get_IsDeclared());
        }

        TestTrue(TEXT("behavior 47 spawns through the SteamJet row"),
            ck::particles::Get_BehaviorTemplateSystemObjectPath(kBehaviorId).EndsWith(
                FString::Printf(TEXT(".%s"), kRowAssetName)));

        const auto ServedIds = ck::particles::Get_BehaviorIdsForTemplateAsset(kRowAssetName);
        TestTrue(TEXT("the SteamJet row serves behavior 47 alone, so its baked script compiles one behavior"),
            ServedIds.Num() == 1 && ServedIds[0] == kBehaviorId);
    }

    // ---- Closed form: the frame cadence never reaches the output ----
    {
        auto Mismatches = 0;
        for (auto Seed = 0; Seed < 64; ++Seed)
        {
            for (const auto Age : { 0.0f, 0.05f, 0.31f, 0.6f, 0.9f })
            {
                const auto At60 = Evaluate(Age, Seed, 1.0f / 60.0f);
                const auto At15 = Evaluate(Age, Seed, 1.0f / 15.0f);

                const auto Identical = At60.Position == At15.Position
                    && At60.Velocity == At15.Velocity
                    && At60.Size     == At15.Size
                    && At60.Color    == At15.Color;

                if (NOT Identical)
                { ++Mismatches; }
            }
        }

        TestEqual(TEXT("the same (Age, Seed) at DeltaTime 1/60 and 1/15 is bit-identical"), Mismatches, 0);
    }

    // ---- Direction and launch speed ----
    {
        constexpr auto Age = 0.001f;

        const auto MaxAngleDeg = kApertureDeg + 0.01f;
        const auto Decay       = FMath::Exp(-kDrag * Age);

        auto OutsideCone  = 0;
        auto OutsideSpeed = 0;
        auto SpeedMin     = TNumericLimits<float>::Max();
        auto SpeedMax     = 0.0f;

        for (auto Seed = 0; Seed < 64; ++Seed)
        {
            const auto Out   = Evaluate(Age, Seed);
            const auto Speed = Out.Velocity.Size();

            const auto CosToX   = Speed > 0.0f ? Out.Velocity.X / Speed : -1.0f;
            const auto AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosToX, -1.0f, 1.0f)));

            if (AngleDeg > MaxAngleDeg)
            { ++OutsideCone; }

            if (Speed < kSpeedLo * Decay - 0.1f || Speed > kSpeedHi * Decay + 0.1f)
            { ++OutsideSpeed; }

            SpeedMin = FMath::Min(SpeedMin, Speed);
            SpeedMax = FMath::Max(SpeedMax, Speed);
        }

        TestEqual(TEXT("every launch direction sits inside the 7 degree cone around +X"), OutsideCone, 0);
        TestEqual(TEXT("every launch speed sits in [520, 680] * exp(-k t)"), OutsideSpeed, 0);
        TestTrue(*FString::Printf(TEXT("the 64 seeds spread across the speed range [%f, %f]"), SpeedMin, SpeedMax),
            SpeedMax - SpeedMin > 0.5f * (kSpeedHi - kSpeedLo) * Decay);
    }

    // ---- The drag range, recomputed from the behavior's own rand chain ----
    {
        auto RangeMisses = 0;
        auto AxialMisses = 0;

        for (auto Seed = 0; Seed < 64; ++Seed)
        {
            const auto Out = Evaluate(kLifetime, Seed);

            const auto V0       = Get_LaunchSpeed(Seed);
            const auto Expected = V0 * (1.0f - FMath::Exp(-kDrag * kLifetime)) / kDrag;

            const auto Lift   = FVector3f(0.0f, 0.0f, kBuoyancy * kLifetime * kLifetime);
            const auto Travel = Out.Position - Get_Birth(Seed) - Lift;

            if (FMath::Abs(Travel.Size() - Expected) > kTolerance)
            {
                ++RangeMisses;
                AddInfo(FString::Printf(TEXT("seed %d: travelled %f along its launch line, expected %f"),
                    Seed, Travel.Size(), Expected));
            }

            const auto Aperture = FMath::DegreesToRadians(kApertureDeg) * FMath::Sqrt(Rand(Seed, 4));
            if (FMath::Abs(Travel.X - Expected * FMath::Cos(Aperture)) > kTolerance)
            { ++AxialMisses; }
        }

        TestEqual(TEXT("at Age = Lifetime each particle has travelled v0 (1 - exp(-k t)) / k along its launch line"),
            RangeMisses, 0);
        TestEqual(TEXT("...and its +X share is that travel times cos(cone angle)"), AxialMisses, 0);
    }

    // ---- Envelope, size, dissolve, renderer ----
    {
        auto BirthAlpha  = 0;
        auto DeathAlpha  = 0;
        auto MidAlpha    = 0;
        auto Shrinking   = 0;
        auto WrongTag    = 0;
        auto EarlyErode  = 0;
        auto NotEroding  = 0;

        for (auto Seed = 0; Seed < 64; ++Seed)
        {
            if (Evaluate_AtNormalizedAge(0.0f, Seed).Color.A != 0.0f)
            { ++BirthAlpha; }

            if (Evaluate_AtNormalizedAge(1.0f, Seed).Color.A != 0.0f)
            { ++DeathAlpha; }

            const auto Mid = Evaluate_AtNormalizedAge(0.3f, Seed);
            if (Mid.Color.A <= 0.0f)
            { ++MidAlpha; }

            if (Evaluate_AtNormalizedAge(0.9f, Seed).Size.X <= Evaluate_AtNormalizedAge(0.1f, Seed).Size.X)
            { ++Shrinking; }

            if (Mid.VisTag != kSmokeVisTag)
            { ++WrongTag; }

            for (const auto N : { 0.0f, 0.1f, 0.2f, 0.29f })
            {
                if (Evaluate_AtNormalizedAge(N, Seed).Dynamic.X != 0.0f)
                { ++EarlyErode; }
            }

            auto Previous = 0.0f;
            for (const auto N : { 0.4f, 0.6f, 0.8f, 1.0f })
            {
                const auto Erosion = Evaluate_AtNormalizedAge(N, Seed).Dynamic.X;
                if (Erosion <= Previous)
                { ++NotEroding; }
                Previous = Erosion;
            }
        }

        TestEqual(TEXT("alpha is zero at birth"), BirthAlpha, 0);
        TestEqual(TEXT("alpha is zero at death"), DeathAlpha, 0);
        TestEqual(TEXT("alpha is positive at n = 0.3"), MidAlpha, 0);
        TestEqual(TEXT("the puff grows over life"), Shrinking, 0);
        TestEqual(TEXT("a live particle draws through the smoke renderer (VisTag 2)"), WrongTag, 0);
        TestEqual(TEXT("the dissolve stays off before n = 0.3"), EarlyErode, 0);
        TestEqual(TEXT("...and rises strictly after it"), NotEroding, 0);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------
