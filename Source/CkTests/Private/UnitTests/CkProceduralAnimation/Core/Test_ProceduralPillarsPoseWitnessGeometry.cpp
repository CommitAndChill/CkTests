#include "../../CkUnitTest_Common.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralBodyEnvelope.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include <Engine/World.h>

namespace ck_test_pillars_pose_witness_geometry
{
    struct FState
    {
        TArray<FCk_Handle> Owners;
        TArray<FCk_Handle_JoltBody> Bodies;
        bool Composed = false;
    };

    auto
    IsReady(const FState& InState)
        -> bool
    {
        if (NOT InState.Composed || InState.Bodies.Num() != 34)
        { return false; }
        for (const auto& Body : InState.Bodies)
        {
            if (NOT ck::IsValid(Body) || NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Body))
            { return false; }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralPillarsPoseWitnessGeometryTest,
    "Ck.ProceduralAnimation.Diagnostic.NativePillarsPoseWitnessGeometryOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralPillarsPoseWitnessGeometryTest::RunTest(const FString&)
    -> bool
{
    using namespace ck_test_pillars_pose_witness_geometry;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            const auto Origin = FVector{120000.0, 392000.0, 600.0};
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
            for (const auto LaneY : TArray<double>{-450.0, 0.0, 450.0})
            {
                for (auto Column = 0; Column < 11; ++Column)
                {
                    const auto Height = FMath::Min(200.0, 60.0 + 40.0 * FMath::Min(Column, 10 - Column));
                    const auto Stagger = Column % 2 == 0 ? -20.0 : 20.0;
                    if (NOT AddBox(FVector{-800.0 + 160.0 * Column, LaneY + Stagger, Height * 0.5},
                        FVector{60.0, 60.0, Height * 0.5}))
                    { return; }
                }
            }
            State->Composed = true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return IsReady(*State); }), 10.0,
        TEXT("All 34 public native Pillars bodies become added before geometry queries")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            if (IsReady(*State))
            {
                const auto Filter = FCk_SurfaceMotion_Contact{}.Get_QueryFilter();
                TestTrue(TEXT("Actual Surface defaults retain Visibility/Block/Any/no convex backfaces"),
                    Filter.Get_Channel() == ECC_Visibility
                    && Filter.Get_MinResponse() == ECk_Jolt_PairInteraction::Block
                    && Filter.Get_BodySelection() == ECk_Jolt_QueryBodySelection::Any
                    && NOT Filter.Get_CollideWithBackFacesConvex());
                const auto Envelope = FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                    .Set_BodyHalfExtents(FVector{30.0, 30.0, 20.0}).Set_CollisionMargin(1.0f);
                const auto BodyRotation = FQuat{0.029644227910354617, -0.10631718727691271,
                    -0.0029867734851028753, 0.99388578550318019};
                const auto BeforeBody = FTransform{BodyRotation,
                    FVector{119600.72098916213, 391993.18688275106, 806.98631426430063}};
                const auto ZeroBody = FTransform{BodyRotation,
                    FVector{119600.72098916213, 391993.06835047837, 803.78031145410205}};
                const auto OldOffset = FTransform{FQuat{-0.054646376463002758, -0.0081516485405444435,
                    5.6078543224107956e-05, 0.99847249387288195}, FVector{0.0, 0.0, -6.4328205475744058}};
                const auto PresentedRotation = FQuat{-0.024743619865949074, -0.11409504079163371,
                    -0.0089779738709246486, 0.99312107566391816};
                const auto OldPresentedBefore = FTransform{PresentedRotation,
                    FVector{119602.08160380773, 391993.56185741082, 800.71022455907712}};
                const auto OldPresentedZero = FTransform{PresentedRotation,
                    FVector{119602.08160380773, 391993.44332513813, 797.50422174887854}};
                TestTrue(TEXT("Captured old offset composes to the exact recorded presentation pairs"),
                    (OldOffset * BeforeBody).Equals(OldPresentedBefore, 1.0e-9)
                    && (OldOffset * ZeroBody).Equals(OldPresentedZero, 1.0e-9));
                // Scratch geometry only. No Core, Surface, BodyPose, gait or actual plant state exists here.
                auto CandidateOffset = OldOffset;
                CandidateOffset.SetTranslation(FVector::ZeroVector);
                const auto Candidate = CandidateOffset * ZeroBody;
                TestTrue(TEXT("Candidate preserves exact local rotation/scale while removing translation only"),
                    CandidateOffset.GetRotation() == OldOffset.GetRotation()
                    && CandidateOffset.GetScale3D() == OldOffset.GetScale3D()
                    && CandidateOffset.GetTranslation() == FVector::ZeroVector);
                auto TotalQueries = 0;
                auto Calls = 0;
                const auto CheckClear = [&](const TCHAR* InName, const FTransform& InStart,
                    const FTransform& InEnd) -> void
                {
                    auto Queries = 0;
                    auto Sweep = TOptional<FCk_Jolt_HitResult>{};
                    auto Overlap = TOptional<ck::FProceduralBodyEnvelopeOverlapObservation>{};
                    const auto Clear = ck::Get_IsProceduralBodyEnvelopePathClear(InWorld, Envelope, Filter,
                        InStart, InEnd, Queries, 3, &Sweep, &Overlap);
                    ++Calls;
                    TotalQueries += Queries;
                    AddInfo(FString::Printf(TEXT("PILLARS_GEOMETRY %s clear=%d queries=%d sweep=%d overlap=%d"),
                        InName, Clear, Queries, Sweep.IsSet(), Overlap.IsSet()));
                    TestTrue(InName, Clear && Queries == 3 && NOT Sweep.IsSet() && NOT Overlap.IsSet());
                };
                CheckClear(TEXT("captured simulation Before to actual zero body"), BeforeBody, ZeroBody);
                CheckClear(TEXT("captured old presentation start stationary"), OldPresentedBefore, OldPresentedBefore);
                auto OldQueries = 0;
                auto OldSweep = TOptional<FCk_Jolt_HitResult>{};
                auto OldOverlap = TOptional<ck::FProceduralBodyEnvelopeOverlapObservation>{};
                const auto OldClear = ck::Get_IsProceduralBodyEnvelopePathClear(InWorld, Envelope, Filter,
                    OldPresentedBefore, OldPresentedZero, OldQueries, 3, &OldSweep, &OldOverlap);
                ++Calls;
                TotalQueries += OldQueries;
                const auto IsRecordedSweep = NOT OldClear && OldQueries == 2 && OldSweep.IsSet() && NOT OldOverlap.IsSet();
                TestTrue(TEXT("Captured old presented pair rejects at sweep after its clear start"), IsRecordedSweep);
                AddInfo(FString::Printf(TEXT("PILLARS_GEOMETRY old posed path clear=%d queries=%d sweep=%d overlap=%d"),
                    OldClear, OldQueries, OldSweep.IsSet(), OldOverlap.IsSet()));
                if (OldSweep.IsSet())
                {
                    const auto& Hit = *OldSweep;
                    const auto IsOwned = State->Owners.Contains(Hit.Get_Entity());
                    const auto Attributed = Hit.Get_HasHit() && FMath::IsFinite(Hit.Get_Fraction())
                        && Hit.Get_Fraction() > 0.0f && Hit.Get_Fraction() < 1.0f
                        && NOT Hit.Get_Position().ContainsNaN() && NOT Hit.Get_Normal().ContainsNaN()
                        && NOT Hit.Get_Normal().IsNearlyZero() && ck::IsValid(Hit.Get_Entity()) && IsOwned;
                    TestTrue(TEXT("Native blocking sweep has finite live attribution to this fixture"), Attributed);
                    TestTrue(TEXT("Native sweep hits the measured pillar region"),
                        Hit.Get_Position().Equals(FVector{119620.0, 392023.28125, 780.0}, 0.1));
                    AddInfo(FString::Printf(TEXT("PILLARS_GEOMETRY old sweep fraction=%.17g point=(%.17g,%.17g,%.17g) normal=(%.17g,%.17g,%.17g) liveOwned=%d entity=%s"),
                        Hit.Get_Fraction(), Hit.Get_Position().X, Hit.Get_Position().Y, Hit.Get_Position().Z,
                        Hit.Get_Normal().X, Hit.Get_Normal().Y, Hit.Get_Normal().Z, Attributed, *Hit.Get_Entity().ToString()));
                }
                CheckClear(TEXT("old presented start to translation-neutral candidate"), OldPresentedBefore, Candidate);
                CheckClear(TEXT("translation-neutral candidate endpoint stationary"), Candidate, Candidate);
                AddInfo(FString::Printf(TEXT("PILLARS_GEOMETRY envelopeCalls=%d nativeQueries=%d; geometry only"), Calls, TotalQueries));
                TestTrue(TEXT("Finite geometry-only inventory"), Calls == 5 && TotalQueries <= 15);
            }
            else
            { AddError(TEXT("Native terrain not ready: Pillars geometry was not evaluated")); }
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
