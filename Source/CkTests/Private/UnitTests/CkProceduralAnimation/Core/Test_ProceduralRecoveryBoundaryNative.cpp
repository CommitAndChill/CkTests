#include "../../CkUnitTest_Common.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/BodyPose/CkProceduralBodyPose_Utils.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Fragment.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Fragment.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Processor.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/World.h>
#include <Misc/App.h>
#include <UObject/StrongObjectPtr.h>

namespace ck_test_recovery_boundary_native
{
    enum class EScenario : uint8 { BoundaryContinuation, OriginalSuccess, MixedSeedCauses, LaterLegFailure };

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UWorld> World;
        EScenario Scenario = EScenario::BoundaryContinuation;
        FVector Origin = FVector::ZeroVector;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_SurfaceMotion Motion;
        FCk_Handle_ProceduralBodyPose Pose;
        FCk_Handle FloorEntity;
        FCk_Handle ObstacleEntity;
        FCk_Handle_JoltBody Floor;
        FCk_Handle_JoltBody Obstacle;
        TArray<FCk_Handle_ProceduralLeg> Legs;
        TArray<FCk_ProceduralLeg_Foot> BeforeFeet;
        TStrongObjectPtr<UCk_ProceduralRig_Data> Rig;
        TStrongObjectPtr<UCk_ProceduralGait_Data> Preset;
        FTransform BeforeBody = FTransform::Identity;
        ECk_SurfaceMotion_ReachPaceState BeforePace = ECk_SurfaceMotion_ReachPaceState::Free;
        uint64 BeforeFrame = 0;
        uint64 BeforeSolve = 0;
        FTransform ShiftedBody = FTransform::Identity;
        TArray<FCk_ProceduralLeg_Foot> RetainedShiftFeet;
        uint64 ShiftSolve = 0;
        bool ShiftRequested = false;
        FCk_Handle AcquisitionEntity;
        FCk_Handle_JoltBody AcquisitionBody;
        bool AcquisitionComplete = false;
        uint64 AcquisitionSolve = 0;
        FTransform OriginalAcquisitionBody = FTransform::Identity;
        FTransform AcquisitionTransform = FTransform::Identity;
        FString AcquisitionFirst;
        FString AcquisitionLast;
        bool BeforeHeightActive = false;
        bool BeforePreparing = false;
        FVector BeforeHeightGoal = FVector::ZeroVector;
        FVector BeforeSupportNormal = FVector::ZeroVector;
        int32 Phase = 0;
        int32 DrivingFrames = 0;
        bool Complete = false;
        bool Failed = false;
        bool FixedTime = false;
        bool OriginalFixedTime = false;
        double OriginalFixedDelta = 0.0;

        ~FState()
        {
            RestoreTime();
        }

        auto
        RestoreTime()
            -> void
        {
            if (FixedTime)
            {
                FApp::SetUseFixedTimeStep(OriginalFixedTime);
                FApp::SetFixedDeltaTime(OriginalFixedDelta);
                FixedTime = false;
            }
        }

        auto
        Require(
            bool InCondition,
            const TCHAR* InMessage)
            -> bool
        {
            if (NOT InCondition)
            {
                Test->AddError(FString::Printf(TEXT("RecoveryBoundaryNative scenario%d phase%d: %s"),
                    static_cast<int32>(Scenario), Phase, InMessage));
                Failed = true;
            }
            return InCondition;
        }

        auto
        AddBox(
            const FVector& InCenter,
            const FVector& InHalfExtents,
            FCk_Handle& OutEntity,
            FCk_Handle_JoltBody& OutBody)
            -> bool
        {
            OutEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World.Get());
            if (NOT Require(ck::IsValid(OutEntity), TEXT("Independent native terrain owner composes")))
            { return false; }
            const auto Transform = UCk_Utils_Transform_UE::Add(OutEntity, FTransform{Origin + InCenter},
                ECk_Replication::DoesNotReplicate);
            if (NOT Require(ck::IsValid(Transform), TEXT("Public native terrain transform composes")))
            { return false; }
            auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
            Shape.Set_HalfExtents(InHalfExtents);
            auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
            Spec.Set_ShapeDimensions(Shape).Set_MotionType(ECk_MotionType::Static).Set_CollisionProfileName(TEXT("BlockAll"));
            OutBody = UCk_Utils_JoltBody_UE::Add(OutEntity, Spec);
            return Require(ck::IsValid(OutBody), TEXT("Native terrain is attributed through the public body API"));
        }

        auto
        ReadFeet() const
            -> TArray<FCk_ProceduralLeg_Foot>
        {
            auto Feet = TArray<FCk_ProceduralLeg_Foot>{};
            for (const auto& Leg : Legs)
            { Feet.Add(UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg)); }
            return Feet;
        }

        auto
        RequestAcquisitionAngle(float InAngle)
            -> void
        {
            auto Foothold = Preset->Get_Foothold();
            Foothold.Set_MaxAngle(InAngle);
            UCk_Utils_ProceduralGait_UE::Request_ApplyPreset(Gait,
                FCk_Request_ProceduralGait_ApplyPreset{Preset->Get_Timing(), Preset->Get_Step(),
                    Preset->Get_Probe(), Foothold}, {});
        }

        // Copy accepted producer output only. The public candidate DTO has no Cast field.
        auto
        ReadAcquisition(bool InRequireHeld, bool InRequireIdeal)
            -> bool
        {
            const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(Gait);
            const auto Solve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
            const auto Known = Snapshot.Get_Status().Get_HasAcceptedSample()
                && Snapshot.Get_Sample().Get_Sequence() == Solve
                && Snapshot.Get_Sample().Get_FrameNumber() <= GFrameCounter
                && Snapshot.Get_Gait().Get_BodyTransform().Equals(
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body), 1.0e-6);
            auto Line = FString::Printf(TEXT("AMIX world=%s root=%s phase=%d known=%d frame=%llu solve=%llu"),
                *GetNameSafe(World.Get()), *Gait.Get_Entity().ToString(), Phase, Known,
                Snapshot.Get_Sample().Get_FrameNumber(), Solve);
            auto Held = false;
            auto Ideal = false;
            auto Search = false;
            auto SelectedRoof = false;
            if (Known && Snapshot.Get_Legs().Num() == 3)
            {
                const auto& Leg = Snapshot.Get_Legs()[1];
                const auto IdentityMatches = Leg.Get_Id() == FName{TEXT("BoundaryLeg1")}
                    && Leg.Get_LegEntityId() == Legs[1].Get_Entity().ToString();
                Line += FString::Printf(TEXT(" leg=%s identity=%d chosen=%d"),
                    *Leg.Get_LegEntityId(), IdentityMatches, Leg.Get_ChosenFoothold());
                const auto& Targeting = Leg.Get_Targeting();
                Line += FString::Printf(TEXT(" hip=(%.17g,%.17g,%.17g) query=(%.17g,%.17g,%.17g)"),
                    Targeting.Get_HipWorld().X, Targeting.Get_HipWorld().Y, Targeting.Get_HipWorld().Z,
                    Targeting.Get_QueryTarget().X, Targeting.Get_QueryTarget().Y, Targeting.Get_QueryTarget().Z);
                if (IdentityMatches && Leg.Get_Footholds().Num() <= 24)
                {
                    for (auto Index = 0; Index < Leg.Get_Footholds().Num(); ++Index)
                    {
                        const auto& Candidate = Leg.Get_Footholds()[Index];
                        const auto Roof = Candidate.Get_Verdict() == ck::EProceduralFootholdVerdict::Usable
                            && FMath::IsNearlyEqual(Candidate.Get_Position().Z, Origin.Z + 75.0, 1.0e-3)
                            && Candidate.Get_Normal().Equals(FVector::UpVector, 1.0e-3);
                        Held |= Roof && Candidate.Get_Source() == ck::EProceduralFootholdSource::Held;
                        Ideal |= Roof && Candidate.Get_Source() == ck::EProceduralFootholdSource::Ideal;
                        Search |= Roof && Index == Leg.Get_ChosenFoothold()
                            && Candidate.Get_Source() != ck::EProceduralFootholdSource::Held
                            && Candidate.Get_Source() != ck::EProceduralFootholdSource::Ideal
                            && Candidate.Get_Source() != ck::EProceduralFootholdSource::None;
                        SelectedRoof |= Roof && Index == Leg.Get_ChosenFoothold();
                        Line += FString::Printf(TEXT(" c%d=%d/%d@(%.17g,%.17g,%.17g)n(%.17g,%.17g,%.17g)"),
                            Index, static_cast<int32>(Candidate.Get_Source()), static_cast<int32>(Candidate.Get_Verdict()),
                            Candidate.Get_Position().X, Candidate.Get_Position().Y, Candidate.Get_Position().Z,
                            Candidate.Get_Normal().X, Candidate.Get_Normal().Y, Candidate.Get_Normal().Z);
                    }
                }
            }
            AcquisitionLast = Line;
            if (AcquisitionFirst.IsEmpty())
            { AcquisitionFirst = Line; }
            auto AllPlants = UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(Gait) == 3;
            for (const auto& Foot : ReadFeet())
            {
                AllPlants &= Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted
                    && FMath::IsNearlyEqual(Foot.Get_Position().Z, Origin.Z + 75.0, 1.0e-3);
            }
            return Known && SelectedRoof && (InRequireHeld ? (Held && AllPlants) : Search)
                && (NOT InRequireIdeal || Ideal);
        }

        auto
        ObserveAcquisition()
            -> void
        {
            // Save first/latest actual output even when a settling precondition never becomes true.
            ReadAcquisition(false, false);
            if (Phase == 9)
            {
                const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(Gait);
                const auto& State = Gait.Get<ck::FFragment_ProceduralGait>();
                const auto& Stance = State.Get_ReachStance();
                if (NOT Snapshot.Get_Status().Get_HasAcceptedSample()
                    || Snapshot.Get_Sample().Get_Sequence() != State.Get_SolveSequence()
                    || Snapshot.Get_Legs().Num() != 3
                    || NOT Snapshot.Get_Gait().Get_BodyTransform().Equals(AcquisitionTransform, 1.0e-6)
                    || NOT UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body).Equals(AcquisitionTransform, 1.0e-6)
                    || NOT Stance.Get_HasSample() || Stance.Get_SolveSequence() != State.Get_SolveSequence()
                    || NOT Stance.Get_BodyAtSolve().Equals(AcquisitionTransform, 1.0e-6)
                    || NOT Snapshot.Get_Legs()[1].Get_Targeting().Get_HipWorld().Equals(
                        Origin + FVector{-10.0, 15.0, 95.0}, 1.0e-3)
                    || NOT Snapshot.Get_Legs()[1].Get_Targeting().Get_QueryTarget().Equals(
                        Origin + FVector{10.0, 20.0, 30.0}, 1.0e-3))
                { return; }
                if (NOT AddBox(FVector{0.0, 17.5, 85.0}, FVector{1.5, 1.5, 2.0},
                        AcquisitionEntity, AcquisitionBody))
                { return; }
                RequestAcquisitionAngle(45.0f);
                Phase = 5;
                return;
            }
            if (Phase == 5)
            {
                if (ck::Is_NOT_Valid(AcquisitionBody)
                    || NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(AcquisitionBody)
                    || Gait.Get<ck::FFragment_ProceduralGait_Tunables>().Get_Foothold().Get_MaxAngle() != 45.0f)
                { return; }
                if (ReadAcquisition(false, false))
                {
                    AcquisitionSolve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
                    Phase = 6;
                }
                return;
            }
            if (Phase == 6)
            {
                if (Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence() <= AcquisitionSolve
                    || NOT ReadAcquisition(true, false))
                { return; }
                Test->AddInfo(AcquisitionLast);
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(AcquisitionEntity);
                Phase = 7;
                return;
            }
            if (Phase == 7)
            {
                if (ck::IsValid(AcquisitionEntity, ck::IsValid_Policy_IncludePendingKill{}))
                { return; }
                RequestAcquisitionAngle(Preset->Get_Foothold().Get_MaxAngle());
                UCk_Utils_Transform_UE::Request_SetTransform(Body,
                    FCk_Request_Transform_SetTransform{OriginalAcquisitionBody}, {});
                AcquisitionSolve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
                Phase = 8;
                return;
            }
            if (Phase == 8)
            {
                const auto& State = Gait.Get<ck::FFragment_ProceduralGait>();
                const auto& Stance = State.Get_ReachStance();
                if (Gait.Get<ck::FFragment_ProceduralGait_Tunables>().Get_Foothold().Get_MaxAngle()
                        != Preset->Get_Foothold().Get_MaxAngle()
                    || Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence() <= AcquisitionSolve
                    || NOT UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body).Equals(OriginalAcquisitionBody, 1.0e-6)
                    || NOT Stance.Get_HasSample() || Stance.Get_SolveSequence() != State.Get_SolveSequence()
                    || NOT Stance.Get_BodyAtSolve().Equals(OriginalAcquisitionBody, 1.0e-6)
                    || UCk_Utils_ProceduralBodyPose_UE::Get_Status(Pose) != ECk_ProceduralAnimation_Status::Ready
                    || NOT ReadAcquisition(true, true))
                { return; }
                Test->AddInfo(AcquisitionLast);
                AcquisitionComplete = true;
                Phase = 0;
            }
        }

        auto
        Compose()
            -> void
        {
            OriginalFixedTime = FApp::UseFixedTimeStep();
            OriginalFixedDelta = FApp::GetFixedDeltaTime();
            FApp::SetFixedDeltaTime(1.0 / 90.0);
            FApp::SetUseFixedTimeStep(true);
            FixedTime = true;
            auto Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World.Get());
            Body = UCk_Utils_Transform_UE::Add(Root, FTransform{Origin + FVector{0.0, 0.0, 90.0}},
                ECk_Replication::DoesNotReplicate);
            if (NOT Require(ck::IsValid(Body), TEXT("Walker body composes at its actual native floor clearance"))
                || NOT AddBox(FVector{0.0, 0.0, 0.0}, FVector{400.0, 200.0, 75.0}, FloorEntity, Floor))
            { return; }
            auto Specs = TArray<FCk_ProceduralLeg_Spec>{};
            auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
            const FVector Hips[] = {FVector{20.0, -27.0, 0.0}, FVector{20.0, 15.0, 0.0},
                FVector{-20.0, Scenario == EScenario::LaterLegFailure ? 15.0 : 27.0, 0.0}};
            const FVector Rests[] = {FVector{40.0, -45.0, -65.0},
                FVector{40.0, Scenario == EScenario::OriginalSuccess ? 30.0 : 20.0, -65.0},
                FVector{-40.0, Scenario == EScenario::LaterLegFailure ? 20.0 : 45.0, -65.0}};
            for (auto Index = 0; Index < 3; ++Index)
            {
                const auto Id = FName{*FString::Printf(TEXT("BoundaryLeg%d"), Index)};
                auto Spec = FCk_ProceduralLeg_Spec{Id, FCk_ProceduralLeg_Placement{Hips[Index], Rests[Index]},
                    FCk_ProceduralLeg_ChainGeometry{TArray<float>{50.0f, 50.0f}}};
                Spec.Set_FootContactRadius(2.0f);
                Specs.Add(Spec);
                auto Parts = TArray<FCk_Handle_Transform>{};
                for (auto Part = 0; Part < 2; ++Part)
                {
                    auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Body);
                    Parts.Add(UCk_Utils_Transform_UE::Add(PartEntity, FTransform{Origin}, ECk_Replication::DoesNotReplicate));
                }
                auto Chain = FCk_ProceduralRig_Spec{Parts};
                auto FootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Body);
                Chain.Set_Foot(UCk_Utils_Transform_UE::Add(FootEntity, FTransform{Origin}, ECk_Replication::DoesNotReplicate));
                Chains.Emplace(Id, Chain);
            }
            Rig.Reset(NewObject<UCk_ProceduralRig_Data>());
            Rig->Set_Legs(Specs);
            Preset.Reset(NewObject<UCk_ProceduralGait_Data>());
            auto Probe = Preset->Get_Probe();
            Probe.Set_OutwardLean(0.0f);
            Preset->Set_Probe(Probe);
            auto Step = Preset->Get_Step();
            Step.Set_Threshold(1000.0f);
            Preset->Set_Step(Step);
            Gait = UCk_Utils_ProceduralAnimation_UE::Add_Walker(Body,
                FCk_ProceduralWalker_Spec{Rig.Get(), Preset.Get()}.Set_Chains(Chains)).Get_Gait();
            if (NOT Require(ck::IsValid(Gait), TEXT("Three-leg native walker composes through Add_Walker")))
            { return; }
            for (auto Index = 0; Index < 3; ++Index)
            {
                const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(Body,
                    FName{*FString::Printf(TEXT("BoundaryLeg%d"), Index)});
                if (NOT Require(ck::IsValid(Leg), TEXT("Every public supporting leg exists")))
                { return; }
                Legs.Add(Leg);
            }
            auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Body);
            const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
                FTransform{Origin + FVector{0.0, 0.0, 90.0}}, ECk_Replication::DoesNotReplicate);
            auto PoseSpec = FCk_ProceduralBodyPose_Spec{Presentation};
            auto Spring = PoseSpec.Get_Spring();
            Spring.Set_MaxAttitudeLag(0.0f);
            PoseSpec.Set_Spring(Spring);
            Pose = UCk_Utils_ProceduralBodyPose_UE::Add(Gait, PoseSpec);
            if (NOT Require(ck::IsValid(Pose), TEXT("Actual native presentation composes through the public pose API")))
            { return; }
            Gait.Add<ck::FFragment_ProceduralHeightPlanning_Test>();
            Gait.Add<ck::FFragment_ProceduralBodyPreparation_Test>();
        }

        auto
        ObserveBefore(
            double InDt)
            -> void
        {
            if (Complete || Failed || UCk_Utils_ProceduralGait_UE::Get_Status(Gait) != ECk_ProceduralAnimation_Status::Ready)
            { return; }
            if (NOT Require(FMath::IsNearlyEqual(InDt, 1.0 / 90.0, 1.0e-6), TEXT("One fixed native integration substep is observed")))
            { return; }
            if (Scenario == EScenario::MixedSeedCauses && Phase >= 5)
            {
                ObserveAcquisition();
                return;
            }
            if (Phase == 4)
            {
                const auto Current = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
                const auto& GaitState = Gait.Get<ck::FFragment_ProceduralGait>();
                const auto& Stance = GaitState.Get_ReachStance();
                if (NOT Current.Equals(ShiftedBody, 1.0e-6) || NOT Stance.Get_HasSample()
                    || GaitState.Get_SolveSequence() <= ShiftSolve || Stance.Get_SolveSequence() != GaitState.Get_SolveSequence()
                    || NOT Stance.Get_BodyAtSolve().Equals(Current, 1.0e-6)
                    || UCk_Utils_ProceduralBodyPose_UE::Get_Status(Pose) != ECk_ProceduralAnimation_Status::Ready)
                { return; }
                const auto Feet = ReadFeet();
                if (NOT Require(Feet.Num() == 3 && RetainedShiftFeet.Num() == 3
                        && UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(Gait) == 3,
                        TEXT("Public shifted-body setup obtains a new real stance solve with all original native plants")))
                { return; }
                for (auto Index = 0; Index < 3; ++Index)
                {
                    if (NOT Require(Feet[Index].Get_Position() == RetainedShiftFeet[Index].Get_Position()
                            && Feet[Index].Get_Normal() == RetainedShiftFeet[Index].Get_Normal()
                            && Feet[Index].Get_Phase() == RetainedShiftFeet[Index].Get_Phase()
                            && Feet[Index].Get_Contact() == RetainedShiftFeet[Index].Get_Contact(),
                            TEXT("Public Y translation preserves actual native plants rather than reseeding private foot state")))
                    { return; }
                }
                Phase = 0;
            }
            if (Phase == 0)
            {
                if (NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Floor)
                    || UCk_Utils_ProceduralBodyPose_UE::Get_Status(Pose) != ECk_ProceduralAnimation_Status::Ready
                    || UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(Gait) != 3)
                { return; }
                const auto Plane = UCk_Utils_ProceduralGait_UE::Get_FeetPlane(Gait);
                if (Plane.Get_State() != ECk_ProceduralGait_FeetPlane::Fitted
                    || NOT Plane.Get_Normal().Equals(FVector::UpVector, 1.0e-3))
                { return; }
                for (const auto& Foot : ReadFeet())
                {
                    if (NOT Require(Foot.Get_Phase() == ECk_ProceduralLeg_FootPhase::Planted
                            && FMath::IsNearlyEqual(Foot.Get_Position().Z, Origin.Z + 75.0, 1.0e-3),
                            TEXT("Maturation supplies real trusted plants on the authored native roof")))
                    { return; }
                }
                if (Scenario == EScenario::MixedSeedCauses && NOT AcquisitionComplete)
                {
                    OriginalAcquisitionBody = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
                    AcquisitionTransform = OriginalAcquisitionBody;
                    AcquisitionTransform.AddToTranslation(FVector{-30.0, 0.0, 5.0});
                    UCk_Utils_Transform_UE::Request_SetTransform(Body,
                        FCk_Request_Transform_SetTransform{AcquisitionTransform}, {});
                    Phase = 9;
                    return;
                }
                if (Scenario == EScenario::MixedSeedCauses && NOT ShiftRequested)
                {
                    RetainedShiftFeet = ReadFeet();
                    ShiftedBody = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
                    ShiftedBody.AddToTranslation(FVector{0.0, 50.0, 0.0});
                    ShiftSolve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
                    UCk_Utils_Transform_UE::Request_SetTransform(Body, FCk_Request_Transform_SetTransform{ShiftedBody}, {});
                    ShiftRequested = true;
                    Phase = 4;
                    return;
                }
                auto Spec = FCk_SurfaceMotion_Spec{};
                auto Contact = Spec.Get_Contact();
                Contact.Set_Clearance(15.0f).Set_ProbeReach(179.0f)
                    .Set_HeightSource(ECk_SurfaceMotion_HeightSource::PlantedFeet);
                Spec.Set_Contact(Contact).Set_AdaptiveHeight(FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                    .Set_BodyHalfExtents(FVector{50.0, 28.0, 13.0}).Set_CollisionMargin(1.0f));
                Motion = UCk_Utils_SurfaceMotion_UE::Add(Body, Spec);
                if (NOT Require(ck::IsValid(Motion), TEXT("Adaptive motion is publicly added to the mature native stance")))
                { return; }
                Phase = 1;
                return;
            }
            if (Phase == 1)
            {
                if (UCk_Utils_SurfaceMotion_UE::Get_Status(Motion) != ECk_ProceduralAnimation_Status::Ready
                    || UCk_Utils_SurfaceMotion_UE::Get_Support(Motion) != ECk_SurfaceMotion_Support::Grounded)
                { return; }
                if (Scenario == EScenario::MixedSeedCauses)
                {
                    const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(Gait);
                    const auto& State = Gait.Get<ck::FFragment_ProceduralGait>();
                    const auto& Planning = State.Get_HeightPlanning();
                    if (NOT Planning.Get_HasSample() || Planning.Get_SolveSequence() != State.Get_SolveSequence()
                        || NOT Snapshot.Get_Status().Get_HasAcceptedSample()
                        || Snapshot.Get_Sample().Get_Sequence() != Planning.Get_SolveSequence()
                        || Snapshot.Get_Sample().Get_FrameNumber() != Planning.Get_Frame()
                        || NOT Planning.Get_BodyAtSolve().Equals(
                            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body), 1.0e-6))
                    { return; }
                    auto Count = 0;
                    auto First = FVector::ZeroVector;
                    auto Distinct = false;
                    for (const auto& Candidate : Planning.Get_Candidates())
                    {
                        if (Candidate.Get_Leg() != Legs[1])
                        { continue; }
                        if (Count == 0)
                        { First = Candidate.Get_PointWorld(); }
                        else
                        { Distinct |= NOT First.Equals(Candidate.Get_PointWorld(), 1.0e-3); }
                        ++Count;
                    }
                    if (Count != 2 || NOT Distinct)
                    { return; }
                    Test->AddInfo(FString::Printf(TEXT("AMIX planning frame=%llu solve=%llu FRcandidates=%d distinct=%d"),
                        Planning.Get_Frame(), Planning.Get_SolveSequence(), Count, Distinct));
                }
                const auto ObstacleCenter = Scenario == EScenario::MixedSeedCauses
                    ? FVector{74.0, 50.0, 76.25} : FVector{54.0, 0.0, 76.25};
                if (NOT AddBox(ObstacleCenter, FVector{1.5, 5.0, 1.25}, ObstacleEntity, Obstacle))
                { return; }
                Phase = 2;
                return;
            }
            if (Phase == 2)
            {
                if (NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Obstacle))
                { return; }
                UCk_Utils_SurfaceMotion_UE::Request_Steering(Motion,
                    FCk_Request_SurfaceMotion_Steering{FVector::ForwardVector, 180.0f}, {});
                Phase = 3;
                return;
            }
            if (NOT Require(UCk_Utils_SurfaceMotion_UE::Get_Status(Motion) == ECk_ProceduralAnimation_Status::Ready
                    && UCk_Utils_ProceduralBodyPose_UE::Get_Status(Pose) == ECk_ProceduralAnimation_Status::Ready,
                    TEXT("Actual simulation and presentation remain ready before the native Recovery episode")))
            { return; }
            ++DrivingFrames;
            BeforeBody = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
            BeforeFeet = ReadFeet();
            BeforePace = UCk_Utils_SurfaceMotion_UE::Get_ReachPaceState(Motion);
            BeforeFrame = GFrameCounter;
            BeforeSolve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
            BeforeHeightActive = UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightActive(Motion);
            BeforePreparing = UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightPreparing(Motion);
            BeforeHeightGoal = UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightGoal(Motion);
            BeforeSupportNormal = UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion);
        }

        auto
        ObserveNegative(
            const ck::FFragment_ProceduralHeightPlanning_Test& Dispatch,
            const ck::FProceduralHeightPlanningTestAttempt& Attempt)
            -> void
        {
            const auto& Inner = Attempt.Recovery;
            if (NOT Require(Dispatch.Frame == BeforeFrame && Dispatch.Solve == BeforeSolve && NOT Dispatch.Overflow
                    && Dispatch.Count == 1 && Attempt.Route == 0 && Attempt.Trials == 1
                    && BeforePace == ECk_SurfaceMotion_ReachPaceState::Blocked && Attempt.Completed && NOT Attempt.Accepted
                    && Attempt.SeedFailure == ck::EProceduralHeightSeedFailure::None
                    && NOT Inner.AdmissionAccepted && Attempt.BodyBefore.Equals(BeforeBody, 1.0e-9)
                    && Attempt.BodyBefore.Equals(Attempt.BodyAfter, 1.0e-9) && Attempt.Preparing == BeforePreparing
                    && Attempt.Rays >= 0 && Attempt.Rays <= 2048 && Attempt.Envelopes >= 0 && Attempt.Envelopes <= 64
                    && Gait.Get<ck::FFragment_ProceduralBodyPreparation_Test>().ObstacleContinuationIssued,
                    TEXT("Actual classified obstacle Recovery rejects once without a force-retry cause or body-height publication")))
            { return; }
            if (Scenario == EScenario::MixedSeedCauses)
            {
                if (NOT Require(Inner.FirstFailedLeg == 1 && Inner.OriginalSeedCount == 2
                        && NOT Inner.Qualified && NOT Inner.Consumed && NOT Inner.BoundaryAccepted && NOT Inner.LaterLegFailed
                        && Inner.BoundarySeedCount == 0 && Inner.GeometryPointCount == 0 && Inner.ContinuationRays == 0
                        && Inner.OriginalCallsPerLeg[0] == 1 && Inner.OriginalCallsPerLeg[1] == 2 && Inner.OriginalCallsPerLeg[2] == 0
                        && Attempt.Rays == Inner.OriginalRaysBeforeContinuation,
                        TEXT("Mixed actual first-leg causes issue no construction, boundary call or continuation ray")))
                { return; }
                auto UnderBody = false;
                auto Inboard = false;
                for (auto Index = 0; Index < 2; ++Index)
                {
                    const auto& Seed = Inner.OriginalSeeds[Index];
                    if (NOT Require(Seed.MetadataFresh && Seed.NativeHit && Seed.Cast == ck::EProceduralFootholdCast::Down
                            && Seed.Rays > 0 && NOT Seed.Point.ContainsNaN() && Seed.Normal.IsNormalized(),
                            TEXT("Both mixed alternatives actually pass freshness and return their native Down verdicts")))
                    { return; }
                    UnderBody |= Seed.Verdict == ck::EProceduralFootholdVerdict::UnderBody;
                    Inboard |= Seed.Verdict == ck::EProceduralFootholdVerdict::Inboard;
                }
                if (NOT Require(UnderBody && Inboard && Inner.OriginalSeeds[0].Point != Inner.OriginalSeeds[1].Point
                        && FMath::IsNearlyEqual(BeforeBody.GetLocation().Y, Origin.Y + 50.0, 1.0e-6),
                        TEXT("The fixed public translated fixture must observe distinct native UnderBody and Inboard failures")))
                { return; }
            }
            else
            {
                if (NOT Require(Inner.FirstFailedLeg == 1 && Inner.OriginalSeedCount >= 1 && Inner.OriginalSeedCount <= 2
                        && Inner.Qualified && Inner.Consumed && Inner.BoundaryAccepted && Inner.LaterLegFailed
                        && Inner.BoundarySeedCount >= 1 && Inner.BoundarySeedCount <= 2
                        && Inner.GeometryPointCount >= 1 && Inner.GeometryPointCount <= 4
                        && Inner.ContinuationRays > 0 && Inner.ContinuationRays <= 8
                        && Inner.OriginalCallsPerLeg[0] == 1 && Inner.OriginalCallsPerLeg[1] == Inner.OriginalSeedCount
                        && Inner.OriginalCallsPerLeg[2] >= 1 && Inner.OriginalCallsPerLeg[2] <= 2,
                        TEXT("One FR boundary success reaches a later original-leg failure without a second continuation")))
                { return; }
                for (auto Index = 0; Index < Inner.OriginalSeedCount; ++Index)
                {
                    const auto& Seed = Inner.OriginalSeeds[Index];
                    if (NOT Require(Seed.MetadataFresh && Seed.NativeHit && Seed.Cast == ck::EProceduralFootholdCast::Down
                            && Seed.Verdict == ck::EProceduralFootholdVerdict::UnderBody && Seed.Rays > 0,
                            TEXT("The first failed FR leg actually qualifies from native Down/UnderBody seeds")))
                    { return; }
                }
                const auto& Boundary = Inner.BoundarySeeds[Inner.BoundarySeedCount - 1];
                if (NOT Require(Boundary.NativeHit && Boundary.Verdict == ck::EProceduralFootholdVerdict::Usable
                        && Boundary.Rays > 0 && Boundary.Rays <= 4,
                        TEXT("The first continuation really admits its native boundary before the later failure")))
                { return; }
            }
            const auto Current = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
            const auto Velocity = UCk_Utils_SurfaceMotion_UE::Get_Velocity(Motion);
            if (NOT Require(Current.Equals(BeforeBody, 1.0e-6)
                    && UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightActive(Motion) == BeforeHeightActive
                    && UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightPreparing(Motion) == BeforePreparing
                    && UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightGoal(Motion).Equals(BeforeHeightGoal, 1.0e-6)
                    && UCk_Utils_SurfaceMotion_UE::Get_Support(Motion) == ECk_SurfaceMotion_Support::Grounded
                    && UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion).Equals(BeforeSupportNormal, 1.0e-6)
                    && NOT Velocity.ContainsNaN() && Velocity.IsNearlyZero(1.0e-6),
                    TEXT("Rejected A adoption preserves actual body and height group with coherent ordinary grounded zero outcome")))
            { return; }
            const auto Feet = ReadFeet();
            if (NOT Require(Feet.Num() == 3 && BeforeFeet.Num() == 3, TEXT("All real supporting feet remain present after rejection")))
            { return; }
            for (auto Index = 0; Index < 3; ++Index)
            {
                if (NOT Require(Feet[Index].Get_Position() == BeforeFeet[Index].Get_Position()
                        && Feet[Index].Get_Normal() == BeforeFeet[Index].Get_Normal()
                        && Feet[Index].Get_Phase() == BeforeFeet[Index].Get_Phase()
                        && Feet[Index].Get_Contact() == BeforeFeet[Index].Get_Contact()
                        && Feet[Index].Get_Foothold() == BeforeFeet[Index].Get_Foothold()
                        && Feet[Index].Get_SwingAlpha() == BeforeFeet[Index].Get_SwingAlpha(),
                        TEXT("Failed same-call Recovery publishes no foot, trust, target or swing replacement")))
                { return; }
            }
            Test->AddInfo(FString::Printf(TEXT("RecoveryBoundaryNegative scenario%d frame%llu solve%llu mixed%d laterFailed%d firstLeg%d seeds%d boundary%d geometry%d extraRays%d outerCount%d cause%d rearLeafKnown0"),
                static_cast<int32>(Scenario), GFrameCounter, BeforeSolve, Scenario == EScenario::MixedSeedCauses,
                Inner.LaterLegFailed, Inner.FirstFailedLeg, Inner.OriginalSeedCount, Inner.BoundarySeedCount,
                Inner.GeometryPointCount, Inner.ContinuationRays, Dispatch.Count, static_cast<int32>(Attempt.SeedFailure)));
            Complete = NOT Failed;
        }

        auto
        ObserveAfter()
            -> void
        {
            if (Complete || Failed || Phase != 3 || BeforeFrame != GFrameCounter)
            { return; }
            const auto& Dispatch = Gait.Get<ck::FFragment_ProceduralHeightPlanning_Test>();
            if (Dispatch.Count == 0)
            { return; }
            const auto& Attempt = Dispatch.Attempts[0];
            if (Attempt.Stage != ck::EProceduralHeightPlanStage::Recovery || Attempt.Delta <= 0.0)
            { return; }
            if (Scenario == EScenario::MixedSeedCauses || Scenario == EScenario::LaterLegFailure)
            { ObserveNegative(Dispatch, Attempt); return; }
            if (NOT Require(Dispatch.Frame == BeforeFrame && Dispatch.Solve == BeforeSolve && NOT Dispatch.Overflow
                    && Dispatch.Count == 1 && Attempt.Route == 0 && Attempt.Trials == 1
                    && BeforePace == ECk_SurfaceMotion_ReachPaceState::Blocked
                    && Attempt.Completed && Attempt.Accepted && Attempt.Recovery.AdmissionAccepted
                    && BeforeFeet.Num() == 3
                    && BeforeBody.GetLocation().Z - BeforeFeet[1].Get_Position().Z + Attempt.Delta > 65.0 * 0.25
                    && Attempt.Delta <= 200.0 / 90.0
                    && Attempt.BodyBefore.Equals(BeforeBody, 1.0e-9)
                    && Attempt.Envelopes > 0 && Attempt.Envelopes <= 64,
                    TEXT("The original positive obstacle Recovery completes its unchanged goal as one ordinary charged trial")))
            { return; }
            const auto& Inner = Attempt.Recovery;
            if (Scenario == EScenario::BoundaryContinuation)
            {
                if (NOT Require(Gait.Get<ck::FFragment_ProceduralBodyPreparation_Test>().ObstacleContinuationIssued
                        && Inner.FirstFailedLeg == 1 && Inner.OriginalSeedCount >= 1 && Inner.OriginalSeedCount <= 2
                        && Inner.Qualified && Inner.Consumed && Inner.BoundaryAccepted && NOT Inner.LaterLegFailed
                        && Inner.BoundarySeedCount >= 1 && Inner.BoundarySeedCount <= 2
                        && Inner.GeometryPointCount >= 1 && Inner.GeometryPointCount <= 4
                        && Inner.ContinuationRays > 0 && Inner.ContinuationRays <= 8,
                        TEXT("The actual first failed supporting leg consumes one same-call bounded native continuation")))
                { return; }
                for (auto Index = 0; Index < Inner.OriginalSeedCount; ++Index)
                {
                    const auto& Seed = Inner.OriginalSeeds[Index];
                    Require(Seed.MetadataFresh && Seed.NativeHit && Seed.Cast == ck::EProceduralFootholdCast::Down
                        && Seed.Verdict == ck::EProceduralFootholdVerdict::UnderBody && Seed.Rays > 0,
                        TEXT("Every actual original seed passes freshness and ends in native Down/UnderBody"));
                }
                const auto& Boundary = Inner.BoundarySeeds[Inner.BoundarySeedCount - 1];
                Require(Boundary.NativeHit && Boundary.Verdict == ck::EProceduralFootholdVerdict::Usable
                    && NOT Boundary.MetadataFresh && Boundary.Rays > 0 && Boundary.Rays <= 4
                    && Inner.OriginalCallsPerLeg[0] == 1 && Inner.OriginalCallsPerLeg[2] == 1
                    && Inner.OriginalCallsPerLeg[1] == Inner.OriginalSeedCount,
                    TEXT("A fresh admitted boundary replaces only the missing leg; earlier and later old successes are each called once"));
            }
            else
            {
                Require(Inner.FirstFailedLeg == INDEX_NONE && NOT Inner.Consumed && NOT Inner.BoundaryAccepted
                    && Inner.BoundarySeedCount == 0 && Inner.GeometryPointCount == 0 && Inner.ContinuationRays == 0
                    && Inner.OriginalCallsPerLeg[0] == 1 && Inner.OriginalCallsPerLeg[1] == 1 && Inner.OriginalCallsPerLeg[2] == 1,
                    TEXT("An original first-seed success completes unchanged without construction or additional native calls"));
            }
            const auto AfterFeet = ReadFeet();
            for (auto Index = 0; Index < BeforeFeet.Num(); ++Index)
            {
                Require(BeforeFeet[Index].Get_Position() == AfterFeet[Index].Get_Position()
                    && BeforeFeet[Index].Get_Normal() == AfterFeet[Index].Get_Normal()
                    && BeforeFeet[Index].Get_Phase() == AfterFeet[Index].Get_Phase()
                    && BeforeFeet[Index].Get_Contact() == AfterFeet[Index].Get_Contact(),
                    TEXT("A native body-height Recovery publishes no plant or foot-state replacement"));
            }
            Test->AddInfo(FString::Printf(TEXT("RecoveryBoundaryNative scenario%d frame%llu delta%.9f seeds%d boundary%d geometry%d extraRays%d outerCount%d"),
                static_cast<int32>(Scenario), static_cast<unsigned long long>(GFrameCounter), Attempt.Delta,
                Inner.OriginalSeedCount, Inner.BoundarySeedCount, Inner.GeometryPointCount, Inner.ContinuationRays, Dispatch.Count));
            Complete = NOT Failed;
        }
    };
}

namespace ck
{
    struct FFragment_TestRecoveryBoundaryNative
    {
        CK_GENERATED_BODY(FFragment_TestRecoveryBoundaryNative);
        TSharedPtr<ck_test_recovery_boundary_native::FState> State;
    };

    class FProcessor_TestRecoveryBoundaryBeforeSetup : public ck_exp::TProcessor<FProcessor_TestRecoveryBoundaryBeforeSetup,
        FCk_Handle_ProceduralGait, TReadWrite<FFragment_TestRecoveryBoundaryNative>, TReadOnly<FFragment_ProceduralGait>,
        TExclude<FTag_DestroyEntity_Initiate>, CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_Physics;
        using RunBefore = TDepList<FProcessor_SurfaceMotion_Setup>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType InDt,
            HandleType InGait,
            FFragment_TestRecoveryBoundaryNative& InObserver,
            const FFragment_ProceduralGait&)
            -> void
        {
            if (InObserver.State.IsValid() && (InObserver.State->Phase == 0 || InObserver.State->Phase == 4)
                && NOT InGait.Has<FFragment_SurfaceMotion_Params>() && NOT InGait.Has<FFragment_SurfaceMotion>()
                && NOT InGait.Has<FFragment_SurfaceMotion_Support>() && NOT InGait.Has<FTag_SurfaceMotion_NeedsSetup>())
            { InObserver.State->ObserveBefore(InDt.Get_Seconds()); }
        }
    };

    class FProcessor_TestRecoveryBoundaryBefore : public ck_exp::TProcessor<FProcessor_TestRecoveryBoundaryBefore,
        FCk_Handle_ProceduralGait, TReadWrite<FFragment_TestRecoveryBoundaryNative>, TReadOnly<FFragment_ProceduralGait>,
        TExclude<FTag_DestroyEntity_Initiate>, CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_HandleRequests>;
        using RunBefore = TDepList<FProcessor_SurfaceMotion_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType InDt,
            HandleType,
            FFragment_TestRecoveryBoundaryNative& InObserver,
            const FFragment_ProceduralGait&)
            -> void
        {
            if (InObserver.State.IsValid() && InObserver.State->Phase != 0 && InObserver.State->Phase != 4)
            { InObserver.State->ObserveBefore(InDt.Get_Seconds()); }
        }
    };

    class FProcessor_TestRecoveryBoundaryAfter : public ck_exp::TProcessor<FProcessor_TestRecoveryBoundaryAfter,
        FCk_Handle_ProceduralGait, TReadWrite<FFragment_TestRecoveryBoundaryNative>, TReadOnly<FFragment_ProceduralGait>,
        TExclude<FTag_DestroyEntity_Initiate>, CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_Update, FProcessor_TestRecoveryBoundaryBefore>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType,
            FFragment_TestRecoveryBoundaryNative& InObserver,
            const FFragment_ProceduralGait&)
            -> void
        {
            if (InObserver.State.IsValid())
            { InObserver.State->ObserveAfter(); }
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestRecoveryBoundaryBeforeSetup);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestRecoveryBoundaryBefore);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestRecoveryBoundaryAfter);

namespace ck_test_recovery_boundary_native
{
    auto
    Enqueue(
        FAutomationTestBase* InTest,
        EScenario InScenario)
        -> void
    {
        const auto State = MakeShared<FState>();
        State->Test = InTest;
        State->Scenario = InScenario;
        State->Origin = FVector{120000.0, 440000.0 + 1000.0 * static_cast<int32>(InScenario), 5000.0};
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [State](UWorld* InWorld)
            {
                State->World = InWorld; State->Compose();
                if (ck::IsValid(State->Gait))
                { State->Gait.Add<ck::FFragment_TestRecoveryBoundaryNative>().State = State; }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Complete || State->Failed; }), 15.0,
            TEXT("Actual public native walker reaches its mandatory positive Recovery branch")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [State](UWorld*)
            {
                State->Require(State->Complete && NOT State->Failed,
                    TEXT("Native original-success/continuation coverage is mandatory and cannot pass by timeout"));
                State->Test->AddInfo(FString::Printf(TEXT("RecoveryBoundaryNative scenario%d phase%d driveFrames%d complete%d"),
                    static_cast<int32>(State->Scenario), State->Phase, State->DrivingFrames, State->Complete));
                if (ck::IsValid(State->Gait) && State->Gait.Has<ck::FFragment_TestRecoveryBoundaryNative>())
                { State->Gait.Get<ck::FFragment_TestRecoveryBoundaryNative>().State.Reset(); }
                if (State->Scenario == EScenario::MixedSeedCauses)
                {
                    State->Test->AddInfo(State->AcquisitionFirst);
                    State->Test->AddInfo(State->AcquisitionLast);
                }
                if (ck::IsValid(State->AcquisitionEntity))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->AcquisitionEntity); }
                if (ck::IsValid(State->ObstacleEntity))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->ObstacleEntity); }
                if (ck::IsValid(State->FloorEntity))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->FloorEntity); }
                if (ck::IsValid(State->Body))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Body); }
                State->RestoreTime();
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralRecoveryBoundaryNativeContinuationTest,
    "Ck.ProceduralAnimation.SurfaceMotion.HeightPlanning.NativeRecoveryBoundarySameCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralRecoveryBoundaryNativeContinuationTest::
RunTest(
    const FString&)
    -> bool
{
    ck_test_recovery_boundary_native::Enqueue(this, ck_test_recovery_boundary_native::EScenario::BoundaryContinuation);
    ck_test_recovery_boundary_native::Enqueue(this, ck_test_recovery_boundary_native::EScenario::MixedSeedCauses);
    ck_test_recovery_boundary_native::Enqueue(this, ck_test_recovery_boundary_native::EScenario::LaterLegFailure);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralRecoveryBoundaryNativeOriginalSuccessTest,
    "Ck.ProceduralAnimation.SurfaceMotion.HeightPlanning.NativeRecoveryOriginalSuccessNoContinuation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralRecoveryBoundaryNativeOriginalSuccessTest::
RunTest(
    const FString&)
    -> bool
{
    ck_test_recovery_boundary_native::Enqueue(this, ck_test_recovery_boundary_native::EScenario::OriginalSuccess);
    return true;
}

#endif
