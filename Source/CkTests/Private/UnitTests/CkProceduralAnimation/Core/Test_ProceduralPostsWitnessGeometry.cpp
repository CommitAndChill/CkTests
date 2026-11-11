#include "../../CkUnitTest_Common.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralBodyEnvelope.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Fragment_Data.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include <Engine/World.h>

namespace ck_test_posts_witness_geometry
{
    struct FState
    {
        TArray<FCk_Handle> Owners;
        TArray<FCk_Handle_JoltBody> Bodies;
        bool Composed = false;
    };

    const auto Origin = FVector{120000.0, 365000.0, 600.0};

    auto
    IsReady(const FState& InState)
        -> bool
    {
        if (NOT InState.Composed || InState.Bodies.Num() != 26)
        { return false; }
        for (const auto& Body : InState.Bodies)
        {
            if (NOT ck::IsValid(Body) || NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Body))
            { return false; }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralPostsWitnessGeometryTest,
    "Ck.ProceduralAnimation.Diagnostic.NativePostsWitnessGeometryOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralPostsWitnessGeometryTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_posts_witness_geometry;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            const auto AddBox = [&](const FVector& InCenter, const FVector& InHalfExtents) -> bool
            {
                auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
                if (NOT TestTrue(TEXT("Terrain public owner composes"), ck::IsValid(Owner)))
                { return false; }
                State->Owners.Add(Owner);
                const auto Transform = UCk_Utils_Transform_UE::Add(Owner, FTransform{Origin + InCenter},
                    ECk_Replication::DoesNotReplicate);
                if (NOT TestTrue(TEXT("Terrain public transform composes"), ck::IsValid(Transform)))
                { return false; }
                auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
                Shape.Set_HalfExtents(InHalfExtents);
                auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
                Spec.Set_ShapeDimensions(Shape).Set_MotionType(ECk_MotionType::Static)
                    .Set_CollisionProfileName(TEXT("BlockAll"));
                const auto Body = UCk_Utils_JoltBody_UE::Add(Owner, Spec);
                if (NOT TestTrue(TEXT("Terrain public Jolt body composes"), ck::IsValid(Body)))
                { return false; }
                State->Bodies.Add(Body);
                return true;
            };
            if (NOT AddBox(FVector{0.0, 0.0, -25.0}, FVector{1550.0, 660.0, 25.0}))
            { return; }
            for (auto Index = 0; Index < 9; ++Index)
            {
                const auto HalfHeight = Index % 2 == 0 ? 37.5 : 15.0;
                if (NOT AddBox(FVector{-760.0 + 190.0 * Index, 0.0, HalfHeight},
                    FVector{30.0, 30.0, HalfHeight}))
                { return; }
            }
            for (auto Index = 0; Index < 8; ++Index)
            {
                const auto X = -760.0 + 190.0 * (Index + 0.5);
                if (NOT AddBox(FVector{X, -100.0, 45.0}, FVector{20.0, 20.0, 45.0})
                    || NOT AddBox(FVector{X, 100.0, 45.0}, FVector{20.0, 20.0, 45.0}))
                { return; }
            }
            State->Composed = true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return IsReady(*State); }), 10.0,
        TEXT("All 26 public native terrain bodies become added before geometry queries")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            if (IsReady(*State))
            {
                // Frozen numeric proposals only: no walker, gait, actual-plant or staging state is written.
                const auto Filter = FCk_ProceduralGait_Probe{}.Get_QueryFilter();
                TestTrue(TEXT("Authored Beast defaults retain Visibility/Block/Any/no convex backfaces"),
                    Filter.Get_Channel() == ECC_Visibility
                    && Filter.Get_MinResponse() == ECk_Jolt_PairInteraction::Block
                    && Filter.Get_BodySelection() == ECk_Jolt_QueryBodySelection::Any
                    && NOT Filter.Get_CollideWithBackFacesConvex());
                const auto Envelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                    .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0}).Set_CollisionMargin(1.0f);
                const auto PoseRotation = FQuat{3.188813242797593e-132, 2.3456205097479706e-131,
                    -4.831605996953514e-137, 1.0};
                const auto MakeBody = [](double InX) -> FTransform
                { return FTransform{FQuat::Identity, Origin + FVector{InX, 0.0, 98.1500244140625}}; };
                const auto MakePosed = [&](double InX) -> FTransform
                { return FTransform{PoseRotation, MakeBody(InX).GetLocation()}; };
                auto EnvelopeQueries = 0;
                auto EnvelopeCalls = 0;
                const auto CheckEnvelope = [&](const FString& InLabel, const FTransform& InStart,
                    const FTransform& InEnd) -> void
                {
                    auto Queries = 0;
                    auto Sweep = TOptional<FCk_Jolt_HitResult>{};
                    auto Overlap = TOptional<ck::FProceduralBodyEnvelopeOverlapObservation>{};
                    const auto Clear = ck::Get_IsProceduralBodyEnvelopePathClear(InWorld, Envelope, Filter,
                        InStart, InEnd, Queries, 3, &Sweep, &Overlap);
                    ++EnvelopeCalls;
                    EnvelopeQueries += Queries;
                    AddInfo(FString::Printf(TEXT("POSTS_GEOMETRY envelope %s clear=%d queries=%d sweep=%d overlap=%d kind=%d"),
                        *InLabel, Clear, Queries, Sweep.IsSet(), Overlap.IsSet(),
                        Overlap.IsSet() ? static_cast<int32>(Overlap->Get_Kind()) : -1));
                    TestTrue(*InLabel, Clear);
                };
                const auto Xs = TArray<double>{798.6298168008216, 790.0, 794.3125, 792.15625, 841.1640625};
                for (auto Index = 0; Index < Xs.Num(); ++Index)
                {
                    CheckEnvelope(FString::Printf(TEXT("sim stationary X=%.17g"), Xs[Index]), MakeBody(Xs[Index]), MakeBody(Xs[Index]));
                    CheckEnvelope(FString::Printf(TEXT("posed stationary X=%.17g"), Xs[Index]), MakePosed(Xs[Index]), MakePosed(Xs[Index]));
                    if (Index > 0)
                    {
                        CheckEnvelope(FString::Printf(TEXT("sim path %d"), Index), MakeBody(Xs[Index - 1]), MakeBody(Xs[Index]));
                        CheckEnvelope(FString::Printf(TEXT("posed path %d"), Index), MakePosed(Xs[Index - 1]), MakePosed(Xs[Index]));
                    }
                }
                const auto Hips = TArray<FVector>{{45.0, -28.0, 0.0}, {45.0, 28.0, 0.0},
                    {-45.0, -28.0, 0.0}, {-45.0, 28.0, 0.0}};
                auto Feet = TArray<FVector>{{782.421875, -25.78125, 75.0}, {746.3828125, 25.75, 75.0},
                    {767.2265625, -62.25, 0.0}, {777.0546875, 29.53125, 75.0}};
                const auto StanceXs = TArray<double>{Xs[0], Xs[0], Xs[1], Xs[1], Xs[1], Xs[2], Xs[3], Xs[3], Xs[4]};
                auto Rays = 0;
                for (auto Stance = 0; Stance < 9; ++Stance)
                {
                    if (Stance == 1) { Feet[3] = FVector{747.078125, 63.40625, 0.0}; }
                    if (Stance == 3) { Feet[1] = FVector{775.546875, 29.34375, 75.0}; }
                    if (Stance == 4) { Feet[0] = FVector{789.1875, -26.8125, 75.0}; }
                    if (Stance == 7) { Feet[1] = FVector{789.0, 29.34375, 75.0}; }
                    for (auto Leg = 0; Leg < 4; ++Leg)
                    {
                        const auto Target = Origin + Feet[Leg];
                        const auto Hip = MakeBody(StanceXs[Stance]).TransformPosition(Hips[Leg]);
                        const auto Hit = UCk_Utils_JoltQuery_UE::Get_RayCast(InWorld, Hip, Target, Filter);
                        ++Rays;
                        // Exact production occluder test; endpoint hits within 3 cm remain nonblocking.
                        const auto Blocks = Hit.Get_HasHit() && Hit.Get_Fraction() > 0.0f
                            && NOT Hit.Get_Position().ContainsNaN() && FVector::Dist(Hit.Get_Position(), Target) > 3.0f;
                        AddInfo(FString::Printf(TEXT("POSTS_GEOMETRY stance=%d leg=%d blocks=%d hit=%d fraction=%.17g hitP=%s target=%s gap=%.17g"),
                            Stance, Leg, Blocks, Hit.Get_HasHit(), Hit.Get_Fraction(), *Hit.Get_Position().ToString(),
                            *Target.ToString(), Hit.Get_HasHit() ? FVector::Dist(Hit.Get_Position(), Target) : -1.0));
                        TestFalse(*FString::Printf(TEXT("stance%d leg%d native hip visibility"), Stance, Leg), Blocks);
                    }
                }
                const auto Proposed = TArray<FVector>{{747.078125, 63.40625, 0.0}, {775.546875, 29.34375, 75.0},
                    {789.1875, -26.8125, 75.0}, {789.0, 29.34375, 75.0}};
                for (auto Index = 0; Index < Proposed.Num(); ++Index)
                {
                    const auto Point = Origin + Proposed[Index];
                    const auto Hit = UCk_Utils_JoltQuery_UE::Get_RayCast(InWorld,
                        Point + FVector::UpVector * 10.0, Point - FVector::UpVector * 10.0, Filter);
                    ++Rays;
                    const auto Trusted = Hit.Get_HasHit() && Hit.Get_Fraction() > 0.0f && Hit.Get_Fraction() <= 1.0f
                        && NOT Hit.Get_Position().ContainsNaN() && NOT Hit.Get_Normal().ContainsNaN()
                        && NOT Hit.Get_Normal().IsNearlyZero()
                        && FVector::DotProduct(Hit.Get_Normal(), -FVector::UpVector) < -KINDA_SMALL_NUMBER;
                    const auto Matches = Trusted && Hit.Get_Position().Equals(Point, 1.0e-3)
                        && Hit.Get_Normal().Equals(FVector::UpVector, 1.0e-3);
                    AddInfo(FString::Printf(TEXT("POSTS_GEOMETRY proposed=%d trusted=%d matches=%d fraction=%.17g hitP=%s normal=%s"),
                        Index, Trusted, Matches, Hit.Get_Fraction(), *Hit.Get_Position().ToString(), *Hit.Get_Normal().ToString()));
                    TestTrue(*FString::Printf(TEXT("proposal%d actual native downward floor/top hit"), Index), Matches);
                }
                AddInfo(FString::Printf(TEXT("POSTS_GEOMETRY totals envelopeCalls=%d envelopeQueries=%d directRays=%d total=%d; geometry only"),
                    EnvelopeCalls, EnvelopeQueries, Rays, EnvelopeQueries + Rays));
                TestTrue(TEXT("Finite standalone diagnostic inventory"), EnvelopeCalls == 18 && EnvelopeQueries <= 54 && Rays == 40);
            }
            else
            { AddError(TEXT("Terrain was not ready: geometry was not evaluated")); }
            for (auto& Owner : State->Owners)
            {
                if (ck::IsValid(Owner))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owner); }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
