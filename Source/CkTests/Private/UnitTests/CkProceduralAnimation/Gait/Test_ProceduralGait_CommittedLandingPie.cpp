#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/BodyPose/CkProceduralBodyPose_Utils.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Fragment.h"
#include "CkProceduralAnimation/Gait/CkProceduralGaitParticipation_Control.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Fragment.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Fragment.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Processor.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck
{
    // Test-owned observations only. The production solver, receipts and publication are never written by this fixture.
    struct FFragment_TestCommittedLandingPhysicsObservation
    {
        CK_GENERATED_BODY(FFragment_TestCommittedLandingPhysicsObservation);

        uint64 Frame = 0;
        uint64 SourceFrame = 0;
        uint64 SourceSequence = 0;
        FTransform Body = FTransform::Identity;
        FTransform PoseOffset = FTransform::Identity;
        TArray<FProceduralGaitCommittedLanding> Landings;
        bool WrongConsumerAccepted = false;
    };

    class FProcessor_TestCommittedLandingBeforeMotion : public ck_exp::TProcessor<
        FProcessor_TestCommittedLandingBeforeMotion,
        FCk_Handle_ProceduralGait,
        TReadWrite<FFragment_TestCommittedLandingPhysicsObservation>>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_HandleRequests>;
        using RunBefore = TDepList<FProcessor_SurfaceMotion_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType InGait,
            FFragment_TestCommittedLandingPhysicsObservation& InObservation)
            -> void
        {
            InObservation = {};
            if (UCk_Utils_ProceduralGait_UE::Get_Status(InGait) != ECk_ProceduralAnimation_Status::Ready)
            { return; }
            const auto Transform = UCk_Utils_Transform_UE::Cast(InGait);
            const auto Pose = UCk_Utils_ProceduralBodyPose_UE::Cast(InGait);
            if (ck::Is_NOT_Valid(Transform) || ck::Is_NOT_Valid(Pose))
            { return; }
            const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(InGait);
            InObservation.Frame = GFrameCounter;
            InObservation.SourceFrame = Snapshot.Get_Sample().Get_FrameNumber();
            InObservation.SourceSequence = Snapshot.Get_Sample().Get_Sequence();
            InObservation.Body = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform);
            InObservation.PoseOffset = UCk_Utils_ProceduralBodyPose_UE::Get_Offset(Pose);
            InObservation.Landings = Get_ProceduralGaitLandingConstraints(InGait, InObservation.Body,
                EProceduralGaitLandingConsumer::SurfaceMotion);
            InObservation.WrongConsumerAccepted = NOT Get_ProceduralGaitLandingConstraints(InGait,
                InObservation.Body, EProceduralGaitLandingConsumer::BodyPose).IsEmpty();
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestCommittedLandingBeforeMotion);

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_gait_committed_landing_pie
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto FrameSeconds = 1.0 / 90.0;
    constexpr auto DeadlineSeconds = 20.0;
    constexpr auto Clearance = 60.0f;
    constexpr auto Reach = 140.0;
    constexpr auto WitnessRayLimit = 4096;
    // Historical normal-update acquisition accounting19*E+98*S with E,S<=4. Observed full-pipeline rays are reported
    // separately; this estimate is not asserted as a new universal native-recovery or rig query budget.
    constexpr auto AcquisitionRayEstimate = 19 * 4 + 98 * 4;
    const auto BaseOrigin = FVector{120000.0, 393000.0, 5000.0};

    enum class EScenario : uint8 { MotionAndPose, Lifecycle };
    enum class EPhase : uint8
    {
        Settle, Drive, AwaitClaimPublication, AwaitDisabled, AwaitWithdrawal, AwaitAirborne, AwaitRecovery, Complete
    };

    struct FSurface
    {
        FCk_Handle Entity;
        FCk_Handle_JoltBody Body;
        FVector Top = FVector::ZeroVector;
        FVector HalfExtents = FVector::ZeroVector;
    };

    struct FObservedLanding
    {
        FCk_Handle_ProceduralLeg Leg;
        FVector Point = FVector::ZeroVector;
        int32 Footfalls = 0;
        uint64 NativeProofSequence = 0;
    };

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UWorld> World;
        EScenario Scenario = EScenario::MotionAndPose;
        EPhase Phase = EPhase::Settle;
        FVector Origin = FVector::ZeroVector;
        FCk_Handle Root;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_SurfaceMotion Motion;
        FCk_Handle_ProceduralBodyPose Pose;
        TArray<FCk_Handle_ProceduralLeg> Legs;
        TStrongObjectPtr<UCk_ProceduralRig_Data> Rig;
        TStrongObjectPtr<UCk_ProceduralGait_Data> Preset;
        TArray<FSurface> Surfaces;
        FSurface Floor;
        TArray<int32> Footfalls;
        TArray<FObservedLanding> ObservedLandings;
        FDelegateHandle Monitor;
        uint64 Sequence = 0;
        double Elapsed = 0.0;
        double PhaseStarted = 0.0;
        int32 Samples = 0;
        int32 MotionSamples = 0;
        int32 PoseSamples = 0;
        int32 MultiSamples = 0;
        int32 DistinctPoseSamples = 0;
        int32 BindingMotionSamples = 0;
        int32 TrustedCompletions = 0;
        int32 Retargets = 0;
        int32 WitnessRays = 0;
        int32 MaxGaitRays = 0;
        int32 MaxPaceTrials = 0;
        int32 MaxPaceRays = 0;
        uint64 ClaimSourceSequence = 0;
        uint64 TurnSourceSequence = 0;
        uint64 BindingSequence = 0;
        uint64 FloorLossSequence = 0;
        uint64 FloorLossFrame = 0;
        int32 TrustedCompletionsAtTurn = 0;
        int32 BoundaryMisses = 0;
        FTransform TurnStartBody = FTransform::Identity;
        FTransform TurnFullBody = FTransform::Identity;
        TArray<FObservedLanding> TurnOwners;
        TArray<FCk_Handle_ProceduralLeg> BindingOwners;
        FVector DriveStart = FVector::ZeroVector;
        FVector RemovedTop = FVector::ZeroVector;
        FCk_Handle_ProceduralLeg Disabled;
        FCk_Handle_ProceduralLeg Withdrawn;
        bool SawTurn = false;
        bool SawHeight = false;
        bool SawRecoveryLanding = false;
        bool SawRecoveryResolved = false;
        bool LeaseActive = false;
        bool BodyClaimActive = false;
        bool SawBodyClaimCycle = false;
        bool SawPostClaimPublication = false;
        bool BindingTurnRequested = false;
        bool ForwardResumed = false;
        bool ConstrainedOwnerCompleted = false;
        bool TerrainRestored = false;
        bool HasDiagnosticState = false;
        FTransform DiagnosticBody = FTransform::Identity;
        bool DiagnosticGrounded = false;
        ECk_SurfaceMotion_ReachPaceState DiagnosticPace = ECk_SurfaceMotion_ReachPaceState::Free;
        bool Failed = false;
        bool Complete = false;
        bool CleanupStarted = false;
        bool FixedTimeActive = false;
        bool OriginalFixedTime = false;
        double OriginalFixedDelta = 0.0;
        const FCk_ArticulationControlToken Token{9219, 1};
        const FCk_ArticulationControlToken BodyToken{9220, 1};

        auto
        Require(
            bool InCondition,
            const TCHAR* InMessage)
            -> bool
        {
            if (InCondition)
            { return true; }
            if (NOT Failed)
            {
                const auto Position = DiagnosticBody.GetLocation();
                const auto Local = Position - Origin;
                Test->AddError(FString::Printf(TEXT("%s (phase=%d elapsed=%.6f seq=%llu frame=%llu state%d "
                    "rootWorld(%.9g,%.9g,%.9g) local(%.9g,%.9g,%.9g) yaw%.9g grounded%d pace%d)"),
                    InMessage, static_cast<int32>(Phase), Elapsed, Sequence, GFrameCounter, HasDiagnosticState,
                    Position.X, Position.Y, Position.Z, Local.X, Local.Y, Local.Z, DiagnosticBody.Rotator().Yaw,
                    DiagnosticGrounded, static_cast<int32>(DiagnosticPace)));
            }
            Failed = true;
            return false;
        }

        auto
        OnPlanted(
            FCk_Handle_ProceduralLeg InLeg,
            FCk_ProceduralLeg_Footfall)
            -> void
        {
            const auto Index = Legs.IndexOfByKey(InLeg);
            if (Footfalls.IsValidIndex(Index))
            { ++Footfalls[Index]; }
        }

        auto
        RestoreTime()
            -> void
        {
            if (NOT FixedTimeActive)
            { return; }
            FApp::SetFixedDeltaTime(OriginalFixedDelta);
            FApp::SetUseFixedTimeStep(OriginalFixedTime);
            FixedTimeActive = false;
        }

        ~FState()
        {
            if (Monitor.IsValid())
            { FWorldDelegates::OnWorldPostActorTick.Remove(Monitor); }
            RestoreTime();
        }
    };

    auto
    DoRay(
        FState& InState,
        const FVector& InPoint,
        const FVector& InNormal = FVector::UpVector)
        -> FCk_Jolt_HitResult
    {
        if (NOT InState.Require(InState.WitnessRays < WitnessRayLimit, TEXT("Fixture witness ray cap")))
        { return {}; }
        ++InState.WitnessRays;
        return UCk_Utils_JoltQuery_UE::Get_RayCast(InState.World.Get(), InPoint + InNormal * 4.0,
            InPoint - InNormal * 4.0, FCk_Jolt_QueryFilter{});
    }

    auto
    DoAddSurface(
        FState& InState,
        const FVector& InTop,
        const FVector& InHalfExtents)
        -> FSurface
    {
        auto Surface = FSurface{};
        Surface.Top = InTop;
        Surface.HalfExtents = InHalfExtents;
        Surface.Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InState.World.Get());
        UCk_Utils_Transform_UE::Add(Surface.Entity, FTransform{InTop - FVector{0.0, 0.0, InHalfExtents.Z}},
            ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
        Shape.Set_HalfExtents(InHalfExtents);
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_CollisionProfileName(TEXT("BlockAll"));
        Surface.Body = UCk_Utils_JoltBody_UE::Add(Surface.Entity, Spec);
        InState.Surfaces.Add(Surface);
        return Surface;
    }

    auto
    DoSteer(
        FState& InState,
        const FVector& InDirection,
        float InSpeed)
        -> void
    {
        UCk_Utils_SurfaceMotion_UE::Request_Steering(InState.Motion,
            FCk_Request_SurfaceMotion_Steering{InDirection, InSpeed}, {});
    }

    auto
    DoEnter(
        FState& InState,
        EPhase InPhase)
        -> void
    {
        InState.Phase = InPhase;
        InState.PhaseStarted = InState.Elapsed;
        InState.Test->AddInfo(FString::Printf(TEXT("CommittedLanding phase%d seq%llu time%.6f witness%d"),
            static_cast<int32>(InPhase), InState.Sequence, InState.Elapsed, InState.WitnessRays));
    }

    auto
    DoCompose(
        FState& InState)
        -> void
    {
        InState.Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InState.World.Get());
        InState.Body = UCk_Utils_Transform_UE::Add(InState.Root,
            FTransform{InState.Origin + FVector{0.0, 0.0, Clearance}}, ECk_Replication::DoesNotReplicate);
        auto LegSpecs = TArray<FCk_ProceduralLeg_Spec>{};
        auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
        const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0}, {-40.0, 30.0}, {-40.0, -30.0}};
        for (auto Index = 0; Index < Corners.Num(); ++Index)
        {
            const auto& Corner = Corners[Index];
            auto Placement = FCk_ProceduralLeg_Placement{FVector{Corner.X, Corner.Y, 0.0},
                FVector{Corner.X + 80.0, Corner.Y * 2.0, -Clearance}};
            Placement.Set_PhaseOffset(Index == 0 || Index == 3 ? 0.0f : 0.5f);
            const auto Id = FName{*FString::Printf(TEXT("Leg%d"), Index)};
            auto LegSpec = FCk_ProceduralLeg_Spec{Id, Placement,
                FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 80.0f}}};
            LegSpec.Set_FootContactRadius(8.0f);
            LegSpecs.Add(LegSpec);
            auto Parts = TArray<FCk_Handle_Transform>{};
            for (auto Part = 0; Part < 2; ++Part)
            {
                auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InState.Body);
                Parts.Add(UCk_Utils_Transform_UE::Add(Entity, FTransform{InState.Origin},
                    ECk_Replication::DoesNotReplicate));
            }
            auto FootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InState.Body);
            const auto Foot = UCk_Utils_Transform_UE::Add(FootEntity, FTransform{InState.Origin},
                ECk_Replication::DoesNotReplicate);
            auto Chain = FCk_ProceduralRig_Spec{Parts};
            Chain.Set_Foot(Foot);
            Chains.Emplace(Id, Chain);
        }
        InState.Rig.Reset(NewObject<UCk_ProceduralRig_Data>());
        InState.Rig->Set_Legs(LegSpecs);
        InState.Preset.Reset(NewObject<UCk_ProceduralGait_Data>());
        auto Timing = InState.Preset->Get_Timing();
        // Rest distance sqrt(80^2+30^2+60^2)=104.404 is inside the unchanged0.8*140 target fraction. Longer swings retain
        // ordinary targets for a measured50-degree stationary turn, qualified against actual native and planted points.
        Timing.Set_CycleDuration(FCk_Time{1.6});
        Timing.Set_StepDuration(FCk_Time{0.8});
        Timing.Set_MaxSimultaneousSwings(2);
        InState.Preset->Set_Timing(Timing);
        for (const auto& LegSpec : LegSpecs)
        {
            const auto RestDistance = FVector::Dist(LegSpec.Get_Placement().Get_HipLocal(),
                LegSpec.Get_Placement().Get_RestFootLocal());
            if (NOT InState.Require(ck::IsValid(LegSpec)
                && FMath::IsFinite(RestDistance)
                && RestDistance <= InState.Preset->Get_Step().Get_TargetReachFraction() * Reach,
                TEXT("Authored rest is finite and within target reach; public Add_Walker owns admission")))
            { return; }
        }
        if (NOT InState.Require(ck::IsValid(Timing) && Timing.Get_MaxSimultaneousSwings() <= LegSpecs.Num(),
            TEXT("Fixture timing admits through the actual gait validator")))
        { return; }
        InState.Gait = UCk_Utils_ProceduralAnimation_UE::Add_Walker(InState.Body,
            FCk_ProceduralWalker_Spec{InState.Rig.Get(), InState.Preset.Get()}.Set_Chains(Chains)).Get_Gait();
        auto MotionSpec = FCk_SurfaceMotion_Spec{};
        auto Contact = MotionSpec.Get_Contact();
        Contact.Set_Clearance(Clearance);
        Contact.Set_ProbeReach(200.0f);
        Contact.Set_HeightSource(ECk_SurfaceMotion_HeightSource::Rays);
        Contact.Set_WallPolicy(ECk_SurfaceMotion_WallPolicy::Slide);
        MotionSpec.Set_Contact(Contact);
        if (InState.Scenario == EScenario::MotionAndPose)
        {
            auto Movement = MotionSpec.Get_Movement();
            Movement.Set_SurfaceTurnRate(4500.0f);
            MotionSpec.Set_Movement(Movement);
        }
        InState.Motion = UCk_Utils_SurfaceMotion_UE::Add(InState.Body, MotionSpec);
        auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InState.Body);
        const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
            FTransform{InState.Origin + FVector{0.0, 0.0, Clearance}}, ECk_Replication::DoesNotReplicate);
        auto PoseSpec = FCk_ProceduralBodyPose_Spec{Presentation};
        auto Conform = PoseSpec.Get_Conform();
        Conform.Set_Mode(ECk_ProceduralBodyPose_ConformMode::PlantedFeet);
        PoseSpec.Set_Conform(Conform);
        InState.Pose = UCk_Utils_ProceduralBodyPose_UE::Add(InState.Gait, PoseSpec);
        InState.Gait.Add<ck::FFragment_TestCommittedLandingPhysicsObservation>();
        InState.Footfalls.Init(0, 4);
        for (auto Index = 0; Index < 4; ++Index)
        {
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(InState.Root,
                FName{*FString::Printf(TEXT("Leg%d"), Index)});
            InState.Legs.Add(Leg);
            if (InState.Require(ck::IsValid(Leg), TEXT("Public native leg composes")))
            {
                (void)ck::UUtils_Signal_OnProceduralLeg_Planted::Bind<&FState::OnPlanted>(&InState, Leg,
                    ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
            }
        }
        InState.Require(ck::IsValid(InState.Gait) && ck::IsValid(InState.Motion) && ck::IsValid(InState.Pose),
            TEXT("Public gait, Motion and BodyPose compose"));
    }

    auto
    DoCheckReach(
        FState& InState,
        const ck::FProceduralGaitCommittedLanding& InLanding,
        const FTransform& InBody,
        const FTransform& InPoseOffset)
        -> bool
    {
        const auto& Constraint = InLanding.Get_Constraint();
        const auto SimHip = InBody.TransformPosition(Constraint.Get_HipLocal());
        const auto PosedHip = (InPoseOffset * InBody).TransformPosition(Constraint.Get_HipLocal());
        const auto SimReach = FVector::Dist(SimHip, Constraint.Get_PointWorld());
        const auto PosedReach = FVector::Dist(PosedHip, Constraint.Get_PointWorld());
        if (NOT SimHip.Equals(PosedHip, 1.0e-3))
        { ++InState.DistinctPoseSamples; }
        return InState.Require(SimReach <= Constraint.Get_AdmissionReach()
            && PosedReach <= Constraint.Get_Reach() + 1.0e-3,
            TEXT("Every committed target retains strict native simulation admission and physical posed reach"));
    }

    auto
    DoTryArmTurn(
        FState& S,
        const FCk_ProceduralAnimation_DebugSnapshot& InSnapshot,
        const TArray<ck::FProceduralGaitCommittedLanding>& InLandings,
        const FTransform& InBody,
        const FTransform& InOffset)
        -> bool
    {
        const auto Local = InBody.GetLocation() - S.Origin;
        if (InLandings.Num() < 2 || Local.X >= 180.0 || FMath::Abs(Local.Z - Clearance) > 0.05
            || NOT InSnapshot.Get_Motion().Get_Grounded()
            || NOT UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(S.Motion).Equals(FVector::UpVector, 1.0e-6))
        { return false; }
        auto Full = InBody;
        Full.SetRotation((FQuat{FVector::UpVector, FMath::DegreesToRadians(50.0)} * InBody.GetRotation()).GetNormalized());
        auto FailsPending = false;
        for (const auto& Landing : InLandings)
        {
            const auto& C = Landing.Get_Constraint();
            if (Landing.Get_Cast() != ck::EProceduralFootholdCast::Down
                || NOT Landing.Get_NormalWorld().Equals(FVector::UpVector, 1.0e-6)
                || FMath::Abs(C.Get_PointWorld().Z - S.Origin.Z) > 0.05)
            { return false; }
            FailsPending |= FVector::Dist(Full.TransformPosition(C.Get_HipLocal()), C.Get_PointWorld()) > C.Get_AdmissionReach();
        }
        if (NOT FailsPending)
        { return false; }
        auto PlantedCount = 0;
        for (auto Index = 0; Index < InSnapshot.Get_Legs().Num(); ++Index)
        {
            const auto& Foot = InSnapshot.Get_Legs()[Index].Get_Foot();
            if (NOT Foot.Get_Planted() || NOT Foot.Get_ContactTrusted())
            { continue; }
            ++PlantedCount;
            const auto Hip = UCk_Utils_ProceduralLeg_UE::Get_Placement(S.Legs[Index]).Get_HipLocal();
            if (FVector::Dist(Full.TransformPosition(Hip), Foot.Get_Position()) > Reach + 1.0e-3
                || FVector::Dist((InOffset * Full).TransformPosition(Hip), Foot.Get_Position()) > Reach + 1.0e-3)
            { return false; }
        }
        if (PlantedCount < 2)
        { return false; }
        for (const auto& Leg : InSnapshot.Get_Legs())
        {
            const auto& Foot = Leg.Get_Foot();
            if (NOT Foot.Get_Planted() || NOT Foot.Get_ContactTrusted())
            { continue; }
            const auto Hit = DoRay(S, Foot.Get_Position(), Foot.Get_Normal());
            if (NOT S.Require(Hit.Get_HasHit() && Hit.Get_Entity() == S.Floor.Entity
                && Hit.Get_Position().Equals(Foot.Get_Position(), 0.05)
                && Hit.Get_Normal().Equals(FVector::UpVector, 1.0e-6),
                TEXT("Binding trigger retains independently witnessed owned flat-floor plants")))
            { return false; }
        }
        S.TurnStartBody = InBody;
        S.TurnFullBody = Full;
        S.TurnSourceSequence = S.Sequence;
        S.TrustedCompletionsAtTurn = S.TrustedCompletions;
        S.TurnOwners.Reset();
        S.BindingOwners.Reset();
        for (const auto& Landing : InLandings)
        {
            const auto Index = S.Legs.IndexOfByKey(Landing.Get_Leg());
            const auto& C = Landing.Get_Constraint();
            S.TurnOwners.Add({Landing.Get_Leg(), C.Get_PointWorld(), S.Footfalls[Index], S.Sequence});
            const auto P = C.Get_PointWorld();
            const auto H = InBody.TransformPosition(C.Get_HipLocal());
            const auto Binding = FVector::Dist(Full.TransformPosition(C.Get_HipLocal()), P) > C.Get_AdmissionReach();
            if (Binding)
            { S.BindingOwners.Add(Landing.Get_Leg()); }
            S.Test->AddInfo(FString::Printf(TEXT("CommittedLanding turn arm seq%llu owner%s point(%.9g,%.9g,%.9g) "
                "hip(%.9g,%.9g,%.9g) full50Sim%.9g full50Posed%.9g nativeBound%.9g physical%.9g plants%d bindingOwner%d"),
                S.Sequence, *UCk_Utils_ProceduralLeg_UE::Get_Id(Landing.Get_Leg()).ToString(), P.X, P.Y, P.Z,
                H.X, H.Y, H.Z, FVector::Dist(Full.TransformPosition(C.Get_HipLocal()), P),
                FVector::Dist((InOffset * Full).TransformPosition(C.Get_HipLocal()), P),
                C.Get_AdmissionReach(), C.Get_Reach(), PlantedCount, Binding));
        }
        DoSteer(S, InBody.GetRotation().GetAxisY(), 0.0f);
        S.BindingTurnRequested = true;
        return true;
    }

    auto
    DoObserve(
        FState& S,
        float InDeltaSeconds)
        -> void
    {
        if (S.Failed || S.Complete || S.CleanupStarted)
        { return; }
        S.Elapsed += FMath::Max(InDeltaSeconds, 0.0f);
        if (NOT S.Require(S.Elapsed <= DeadlineSeconds, TEXT("Committed landing fixture bounded deadline")))
        { return; }
        const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(S.Root);
        if (NOT Snapshot.Get_Status().Get_HasAcceptedSample())
        { return; }
        const auto Sequence = Snapshot.Get_Sample().Get_Sequence();
        if (Sequence == S.Sequence)
        { return; }
        if (NOT S.Require(S.Sequence == 0 || Sequence == S.Sequence + 1,
            TEXT("Consecutive actual accepted solves are observed")))
        { return; }
        S.Sequence = Sequence;
        ++S.Samples;
        const auto Body = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(S.Body);
        const auto Offset = UCk_Utils_ProceduralBodyPose_UE::Get_Offset(S.Pose);
        S.HasDiagnosticState = true;
        S.DiagnosticBody = Body;
        S.DiagnosticGrounded = Snapshot.Get_Motion().Get_Grounded();
        S.DiagnosticPace = Snapshot.Get_Motion().Get_ReachPaceState();
        if (NOT S.Require(Snapshot.Get_Freshness().Get_GaitFresh()
            && Snapshot.Get_Freshness().Get_MotionMatchesGaitFrame()
            && Snapshot.Get_Gait().Get_BodyTransform().Equals(Body, 1.0e-3)
            && Snapshot.Get_Legs().Num() == 4, TEXT("Accepted gait, actual body and Motion refer to this solve")))
        { return; }
        const auto Landings = ck::Get_ProceduralGaitLandingConstraints(S.Gait, Body,
            ck::EProceduralGaitLandingConsumer::BodyPose);
        if (NOT S.Require(ck::Get_ProceduralGaitLandingConstraints(S.Gait, Body,
                ck::EProceduralGaitLandingConsumer::SurfaceMotion).IsEmpty(),
            TEXT("Current post-Gait publication cannot masquerade as previous-frame Motion input")))
        { return; }
        // Native EndPlay follows the gait producer. Establish the independent loss epoch before checking receipts
        // copied earlier in that same publication; from the very next accepted solve both representations must expire.
        if (S.Phase == EPhase::AwaitAirborne && S.FloorLossSequence == 0)
        {
            const auto Missing = NOT DoRay(S, S.RemovedTop).Get_HasHit();
            const auto Retired = ck::Is_NOT_Valid(S.Floor.Body)
                || NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(S.Floor.Body);
            if (Missing && Retired)
            {
                S.FloorLossSequence = Sequence;
                S.FloorLossFrame = GFrameCounter;
                S.Test->AddInfo(FString::Printf(TEXT("CommittedLanding native floor loss seq%llu frame%llu"),
                    Sequence, GFrameCounter));
            }
        }
        if (S.Phase == EPhase::AwaitRecovery && NOT S.TerrainRestored && ck::IsValid(S.Floor.Body)
            && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(S.Floor.Body)
            && DoRay(S, S.Floor.Top).Get_Entity() == S.Floor.Entity)
        {
            S.TerrainRestored = true;
            S.Test->AddInfo(FString::Printf(TEXT("CommittedLanding native floor restored seq%llu frame%llu"),
                Sequence, GFrameCounter));
        }
        if (S.FloorLossSequence > 0 && Sequence > S.FloorLossSequence && NOT S.TerrainRestored)
        {
            const auto HasProducer = Snapshot.Get_Legs().ContainsByPredicate([](const auto& InLeg)
            { return InLeg.Get_HasCommittedLanding(); });
            if (NOT S.Require(NOT HasProducer && Landings.IsEmpty(),
                TEXT("Both ordinary producer and helper expire on the first solve after independent native terrain loss")))
            { return; }
        }
        // The producer snapshot precedes BodyPose. Checking only the filtered helper result would hide an offset
        // regression: an overstretched producer entry could simply disappear from that result.
        for (auto Index = 0; Index < Snapshot.Get_Legs().Num(); ++Index)
        {
            const auto& Producer = Snapshot.Get_Legs()[Index];
            if (NOT Producer.Get_HasCommittedLanding())
            { continue; }
            const auto* Copied = Landings.FindByPredicate([&S, Index](const auto& InLanding)
            { return InLanding.Get_Leg() == S.Legs[Index]; });
            if (NOT S.Require(Copied != nullptr, TEXT("Every current producer entry survives actual BodyPose containment")))
            { return; }
            const auto HipLocal = UCk_Utils_ProceduralLeg_UE::Get_Placement(S.Legs[Index]).Get_HipLocal();
            if (NOT S.Require(Copied->Get_Constraint().Get_PointWorld().Equals(Producer.Get_CommittedLandingWorld(), 1.0e-3)
                && Copied->Get_Constraint().Get_AdmissionReach() == Producer.Get_CommittedLandingAdmissionReach()
                && FVector::Dist(Body.TransformPosition(HipLocal), Producer.Get_CommittedLandingWorld())
                    <= Producer.Get_CommittedLandingAdmissionReach()
                && FVector::Dist((Offset * Body).TransformPosition(HipLocal), Producer.Get_CommittedLandingWorld())
                    <= Reach + 1.0e-3,
                TEXT("Unfiltered producer point retains native admission and physical actual presentation reach")))
            { return; }
        }
        S.MaxGaitRays = FMath::Max(S.MaxGaitRays, Snapshot.Get_Gait().Get_RaysLastSolve());
        S.MaxPaceTrials = FMath::Max(S.MaxPaceTrials, Snapshot.Get_Motion().Get_ReachPaceTrials());
        S.MaxPaceRays = FMath::Max(S.MaxPaceRays, Snapshot.Get_Motion().Get_ReachPaceRays());
        // Fixed1/90 uses one existing0.016s integration substep. Scale and final feedback describe that same trial set.
        if (NOT S.Require(S.MaxPaceTrials <= 8, TEXT("Existing single-substep Motion reach-pace trial cap is unchanged")))
        { return; }
        const auto& Before = S.Gait.Get<ck::FFragment_TestCommittedLandingPhysicsObservation>();
        if (Before.Frame == GFrameCounter && NOT Before.Landings.IsEmpty())
        {
            ++S.MotionSamples;
            if (NOT S.Require(Before.SourceFrame + 1 == Before.Frame
                && Before.SourceSequence + 1 == Sequence && NOT Before.WrongConsumerAccepted,
                TEXT("Physics receives only the immediately previous accepted sequence, not a current Pose sample")))
            { return; }
            const auto PhysicsCanRetain = Snapshot.Get_Motion().Get_Grounded()
                && Snapshot.Get_Motion().Get_ReachPaceState() != ECk_SurfaceMotion_ReachPaceState::PhysicalOverride;
            const auto RemovingTerrain = S.Phase == EPhase::AwaitAirborne || S.Phase == EPhase::AwaitRecovery;
            if (NOT S.Require(PhysicsCanRetain || RemovingTerrain,
                TEXT("Ordinary grounded fixture motion does not silently skip landing containment")))
            { return; }
            for (const auto& Landing : Before.Landings)
            {
                if (PhysicsCanRetain && NOT DoCheckReach(S, Landing, Body, Before.PoseOffset))
                { return; }
            }
            const auto& Support = S.Motion.Get<ck::FFragment_SurfaceMotion_Support>();
            if (S.BindingTurnRequested && PhysicsCanRetain && Support.Get_ReachPaceScale() < 1.0f
                && Support.Get_ReachPaceFeedback().IsEmpty())
            { ++S.BindingMotionSamples; }
        }
        if (S.BindingTurnRequested && S.BindingSequence == 0 && Sequence > S.TurnSourceSequence)
        {
            const auto& Support = S.Motion.Get<ck::FFragment_SurfaceMotion_Support>();
            const auto SameReceipts = S.TurnOwners.Num() == Before.Landings.Num()
                && NOT S.TurnOwners.ContainsByPredicate([&Before](const auto& InOwner)
                {
                    return NOT Before.Landings.ContainsByPredicate([&InOwner](const auto& InLanding)
                    {
                        return InLanding.Get_Leg() == InOwner.Leg
                            && InLanding.Get_Constraint().Get_PointWorld().Equals(InOwner.Point, 1.0e-3);
                    });
                });
            const auto Scale = Support.Get_ReachPaceScale();
            const auto ExpectedRotation = FQuat::Slerp(S.TurnStartBody.GetRotation(), S.TurnFullBody.GetRotation(), Scale).GetNormalized();
            S.Test->AddInfo(FString::Printf(TEXT("CommittedLanding turn outcome seq%llu source%llu scale%.9g "
                "centerDelta%.9g actualYaw%.9g expectedYaw%.9g grounded%d pace%d feedback%d receipts%d"),
                Sequence, Before.SourceSequence, Scale, FVector::Dist(Body.GetLocation(), S.TurnStartBody.GetLocation()),
                Body.Rotator().Yaw, ExpectedRotation.Rotator().Yaw, Snapshot.Get_Motion().Get_Grounded(),
                static_cast<int32>(Support.Get_ReachPaceState()), Support.Get_ReachPaceFeedback().Num(), SameReceipts));
            if (NOT S.Require(Sequence == S.TurnSourceSequence + 1 && Before.SourceSequence == S.TurnSourceSequence
                && Before.Frame == GFrameCounter && SameReceipts && Scale < 1.0f
                && Support.Get_ReachPaceState() != ECk_SurfaceMotion_ReachPaceState::PhysicalOverride
                && Support.Get_ReachPaceFeedback().IsEmpty() && S.BindingMotionSamples > 0
                && Body.GetLocation().Equals(S.TurnStartBody.GetLocation(), 1.0e-3)
                && Body.GetRotation().Equals(ExpectedRotation, 1.0e-3),
                TEXT("The next actual Motion update paces the armed native turn and applies the computed bounded rotation")))
            { return; }
            S.BindingSequence = Sequence;
            for (auto& Owner : S.TurnOwners)
            { Owner.Footfalls = S.Footfalls[S.Legs.IndexOfByKey(Owner.Leg)]; }
        }
        if (NOT Snapshot.Get_Motion().Get_Grounded()
            && NOT S.Require(Landings.IsEmpty(), TEXT("Current unsupported flight has no ordinary landing publication")))
        { return; }
        if (Landings.Num() > 1)
        { ++S.MultiSamples; }
        if (NOT Landings.IsEmpty())
        {
            ++S.PoseSamples;
            auto MismatchedBody = Body;
            MismatchedBody.AddToTranslation(FVector{0.02, 0.0, 0.0});
            if (NOT S.Require(ck::Get_ProceduralGaitLandingConstraints(S.Gait, MismatchedBody,
                    ck::EProceduralGaitLandingConsumer::BodyPose).IsEmpty(),
                TEXT("An actual positive publication rejects a mismatched source body")))
            { return; }
            if (NOT S.Require(ck::Get_ProceduralGaitLandingConstraints(S.Gait, Body,
                    static_cast<ck::EProceduralGaitLandingConsumer>(255)).IsEmpty(),
                TEXT("An invalid consumer rejects a positively published real-native sample")))
            { return; }
            if (NOT S.Require(UCk_Utils_ProceduralAnimation_Debug_UE::Get_RaysLastSolve(S.Gait)
                    == Snapshot.Get_Gait().Get_RaysLastSolve(),
                TEXT("Copied landing reads do not consume production gait rays")))
            { return; }
        }
        for (const auto& Landing : Landings)
        {
            const auto Index = S.Legs.IndexOfByKey(Landing.Get_Leg());
            if (NOT S.Require(Snapshot.Get_Legs().IsValidIndex(Index), TEXT("Published landing owns an actual fixture leg")))
            { return; }
            const auto& DebugLeg = Snapshot.Get_Legs()[Index];
            if (NOT S.Require(DebugLeg.Get_Foot().Get_SwingActive() && DebugLeg.Get_Foot().Get_SwingTargetTrusted()
                && DebugLeg.Get_AutomaticRecovery() == ck::EProceduralGaitAutomaticRecovery::None
                && DebugLeg.Get_HasCommittedLanding()
                && DebugLeg.Get_CommittedLandingWorld().Equals(Landing.Get_Constraint().Get_PointWorld(), 1.0e-3)
                && DebugLeg.Get_CommittedLandingAdmissionReach() == Landing.Get_Constraint().Get_AdmissionReach()
                && UCk_Utils_ProceduralLeg_UE::Get_Placement(Landing.Get_Leg()).Get_HipLocal()
                    .Equals(Landing.Get_Constraint().Get_HipLocal(), 1.0e-6),
                TEXT("Copied constraint matches the real ordinary native committed target and leg")))
            { return; }
            if (NOT DoCheckReach(S, Landing, Body, Offset))
            { return; }
            const auto Hit = DoRay(S, Landing.Get_Constraint().Get_PointWorld(), Landing.Get_NormalWorld());
            const auto OwnSurface = S.Surfaces.ContainsByPredicate([&Hit](const auto& InSurface)
            { return InSurface.Entity == Hit.Get_Entity(); });
            const auto BoundaryMiss = S.Phase == EPhase::AwaitAirborne && Sequence == S.FloorLossSequence
                && GFrameCounter == S.FloorLossFrame && NOT Hit.Get_HasHit();
            if (BoundaryMiss)
            { ++S.BoundaryMisses; }
            if (NOT S.Require(BoundaryMiss || (Hit.Get_HasHit() && OwnSurface
                && Hit.Get_Position().Equals(Landing.Get_Constraint().Get_PointWorld(), 0.05)
                && FVector::DotProduct(Hit.Get_Normal(), Landing.Get_NormalWorld()) > 0.99),
                TEXT("Independent bounded native witness confirms the owned committed contact")))
            { return; }
            auto* Previous = S.ObservedLandings.FindByPredicate([&Landing](const auto& InObserved)
            { return InObserved.Leg == Landing.Get_Leg(); });
            if (Previous == nullptr)
            {
                S.ObservedLandings.Add({Landing.Get_Leg(), Landing.Get_Constraint().Get_PointWorld(), S.Footfalls[Index],
                    BoundaryMiss ? 0 : Sequence});
            }
            else
            {
                if (NOT Previous->Point.Equals(Landing.Get_Constraint().Get_PointWorld(), 1.0e-3))
                {
                    if (S.Footfalls[Index] == Previous->Footfalls)
                    { ++S.Retargets; }
                    Previous->Point = Landing.Get_Constraint().Get_PointWorld();
                    Previous->Footfalls = S.Footfalls[Index];
                }
                if (NOT BoundaryMiss)
                { Previous->NativeProofSequence = Sequence; }
            }
        }
        for (auto& Previous : S.ObservedLandings)
        {
            const auto Index = S.Legs.IndexOfByKey(Previous.Leg);
            const auto& Foot = Snapshot.Get_Legs()[Index].Get_Foot();
            if (S.Footfalls[Index] > Previous.Footfalls && Foot.Get_Planted() && Foot.Get_ContactTrusted()
                && Foot.Get_Position().Equals(Previous.Point, 0.05))
            {
                ++S.TrustedCompletions;
                const auto* TurnOwner = S.TurnOwners.FindByPredicate([&Previous](const auto& InOwner)
                { return InOwner.Leg == Previous.Leg; });
                if (NOT S.ConstrainedOwnerCompleted && S.BindingSequence > 0 && Sequence > S.BindingSequence
                    && TurnOwner != nullptr && S.BindingOwners.Contains(Previous.Leg)
                    && S.Footfalls[Index] > TurnOwner->Footfalls
                    && S.TrustedCompletions > S.TrustedCompletionsAtTurn)
                {
                    const auto Replaced = NOT Previous.Point.Equals(TurnOwner->Point, 1.0e-3);
                    if (NOT S.Require(NOT Replaced || Previous.NativeProofSequence >= S.BindingSequence,
                        TEXT("A binding owner's replacement point has its own observed native proof after binding")))
                    { return; }
                    const auto Hit = DoRay(S, Foot.Get_Position(), Foot.Get_Normal());
                    const auto Owned = S.Surfaces.ContainsByPredicate([&Hit](const auto& InSurface)
                    { return InSurface.Entity == Hit.Get_Entity(); });
                    if (NOT S.Require(Hit.Get_HasHit() && Owned && Hit.Get_Position().Equals(Foot.Get_Position(), 0.05),
                        TEXT("A constrained owner completes after binding on its actual native contact or fresh native retarget")))
                    { return; }
                    S.ConstrainedOwnerCompleted = true;
                    const auto Point = Foot.Get_Position();
                    S.Test->AddInfo(FString::Printf(TEXT("CommittedLanding constrained completion seq%llu binding%llu owner%s "
                        "point(%.9g,%.9g,%.9g) armedPoint(%.9g,%.9g,%.9g) proofSeq%llu replaced%d footfalls%d bindingBaseline%d"), Sequence, S.BindingSequence,
                        *UCk_Utils_ProceduralLeg_UE::Get_Id(Previous.Leg).ToString(), Point.X, Point.Y, Point.Z,
                        TurnOwner->Point.X, TurnOwner->Point.Y, TurnOwner->Point.Z, Previous.NativeProofSequence, Replaced,
                        S.Footfalls[Index], TurnOwner->Footfalls));
                }
                Previous.Footfalls = S.Footfalls[Index];
            }
        }
        const auto Local = Body.GetLocation() - S.Origin;
        S.SawTurn |= FMath::Abs(Body.Rotator().Yaw) > 10.0;
        S.SawHeight |= Local.Z > Clearance + 6.0;
        const auto HasLeg = [&Landings](const FCk_Handle_ProceduralLeg& InLeg) -> bool
        {
            return Landings.ContainsByPredicate([&InLeg](const auto& InLanding)
            { return InLanding.Get_Leg() == InLeg; });
        };
        switch (S.Phase)
        {
            case EPhase::Settle:
                if (UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(S.Gait) != 4
                    || NOT Snapshot.Get_Motion().Get_Grounded())
                { break; }
                S.DriveStart = Body.GetLocation();
                DoSteer(S, FVector::ForwardVector, S.Scenario == EScenario::MotionAndPose ? 90.0f : 45.0f);
                DoEnter(S, EPhase::Drive);
                break;
            case EPhase::Drive:
                if (S.Scenario == EScenario::MotionAndPose)
                {
                    if (NOT S.BindingTurnRequested)
                    {
                        (void)DoTryArmTurn(S, Snapshot, Landings, Body, Offset);
                        if (S.Failed)
                        { return; }
                        if (NOT S.Require(S.BindingTurnRequested || Local.X < 180.0,
                            TEXT("A numerically binding native turn must qualify before leaving the authored flat region")))
                        { return; }
                    }
                    if (S.BindingTurnRequested && NOT S.ForwardResumed && S.BindingMotionSamples > 0
                        && S.BindingSequence > 0 && S.ConstrainedOwnerCompleted && S.SawTurn
                        && S.TrustedCompletions > S.TrustedCompletionsAtTurn)
                    {
                        DoSteer(S, FVector::ForwardVector, 90.0f);
                        S.ForwardResumed = true;
                    }
                    if (Local.X > 300.0 && S.ForwardResumed && S.BindingMotionSamples > 0
                        && S.ConstrainedOwnerCompleted
                        && S.SawTurn && S.SawHeight && S.MultiSamples > 0
                        && S.MotionSamples > 0 && S.DistinctPoseSamples > 0 && S.TrustedCompletions > 0)
                    { S.Complete = true; DoEnter(S, EPhase::Complete); }
                }
                else if (Landings.Num() > 1 && S.MotionSamples > 0)
                {
                    const auto Subject = UCk_Utils_ProceduralArticulation_UE::Cast(S.Root);
                    const auto Channels = FCk_ArticulationChannelSet{TArray<FName>{}, true};
                    if (NOT S.Require(ck::IsValid(Subject), TEXT("Real walker has its composed articulation subject")))
                    { return; }
                    if (NOT S.Require(FCk_ProceduralArticulation_Control::TryReserveClaims(Subject, S.BodyToken, Channels)
                        == ECk_ArticulationControlResult::Accepted, TEXT("Existing control facade reserves the actual body channel")))
                    { return; }
                    S.BodyClaimActive = true;
                    if (NOT S.Require(ck::Get_ProceduralGaitLandingConstraints(S.Gait, Body,
                            ck::EProceduralGaitLandingConsumer::BodyPose).IsEmpty(),
                        TEXT("Actual body claim immediately excludes a previously positive ordinary publication")))
                    { return; }
                    if (NOT S.Require(FCk_ProceduralArticulation_Control::ReleaseClaims(Subject, S.BodyToken, Channels)
                        == ECk_ArticulationControlResult::Accepted, TEXT("The exact owned body claim releases")))
                    { return; }
                    S.BodyClaimActive = false;
                    if (NOT S.Require(ck::Get_ProceduralGaitLandingConstraints(S.Gait, Body,
                            ck::EProceduralGaitLandingConsumer::BodyPose).IsEmpty(),
                        TEXT("Releasing a claim cannot resurrect the old claims-revision publication")))
                    { return; }
                    S.SawBodyClaimCycle = true;
                    S.ClaimSourceSequence = Sequence;
                    DoEnter(S, EPhase::AwaitClaimPublication);
                }
                break;
            case EPhase::AwaitClaimPublication:
                if (Sequence <= S.ClaimSourceSequence || Landings.IsEmpty())
                { break; }
                S.SawPostClaimPublication = true;
                S.Disabled = Landings[0].Get_Leg();
                UCk_Utils_ProceduralLeg_UE::Request_EnableDisable(S.Disabled,
                    FCk_Request_ProceduralLeg_EnableDisable{ECk_EnableDisable::Disable}, {});
                DoEnter(S, EPhase::AwaitDisabled);
                break;
            case EPhase::AwaitDisabled:
                if (UCk_Utils_ProceduralLeg_UE::Get_EnableDisable(S.Disabled) != ECk_EnableDisable::Disable)
                { break; }
                if (NOT S.Require(NOT HasLeg(S.Disabled), TEXT("A real disabled committed owner is no longer published")))
                { return; }
                if (Landings.IsEmpty())
                { break; }
                {
                    auto Participation = FCk_ProceduralGaitParticipation_Snapshot{};
                    if (FCk_ProceduralGaitParticipation_Control::GetSnapshot(S.Gait, Participation)
                        != ECk_ProceduralGaitParticipation_Result::Accepted)
                    { break; }
                    S.Withdrawn = Landings[0].Get_Leg();
                    const auto Ids = TArray<FName>{UCk_Utils_ProceduralLeg_UE::Get_Id(S.Withdrawn)};
                    auto Observation = FCk_ProceduralGaitParticipation_Observation{};
                    const auto Result = FCk_ProceduralGaitParticipation_Control::TryWithdraw(S.Gait, S.Token, Ids,
                        FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::CountOnly, 1},
                        Participation, Observation);
                    if (NOT S.Require(Result == ECk_ProceduralGaitParticipation_Result::Accepted,
                        TEXT("A fresh real-swing owner withdraws under the existing supported lease policy")))
                    { return; }
                    S.LeaseActive = true;
                    const auto AfterWithdrawal = ck::Get_ProceduralGaitLandingConstraints(S.Gait, Body,
                        ck::EProceduralGaitLandingConsumer::BodyPose);
                    if (NOT S.Require(NOT AfterWithdrawal.ContainsByPredicate([&S](const auto& InLanding)
                        { return InLanding.Get_Leg() == S.Withdrawn; }),
                        TEXT("Withdrawal removes its actual commitment before another solver publication")))
                    { return; }
                    DoEnter(S, EPhase::AwaitWithdrawal);
                }
                break;
            case EPhase::AwaitWithdrawal:
                if (NOT S.Require(NOT HasLeg(S.Withdrawn), TEXT("Subsequent real solves exclude the withdrawn owner")))
                { return; }
                if (S.Elapsed - S.PhaseStarted < 0.2)
                { break; }
                if (NOT S.Require(FCk_ProceduralGaitParticipation_Control::FailRecovery(S.Gait, S.Token,
                    FName{TEXT("CommittedLandingFixtureRelease")}) == ECk_ProceduralGaitParticipation_Result::Accepted,
                    TEXT("Existing participation facade releases the owned test lease")))
                { return; }
                S.LeaseActive = false;
                DoSteer(S, FVector::ForwardVector, 0.0f);
                S.RemovedTop = S.Floor.Top;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(S.Floor.Entity);
                DoEnter(S, EPhase::AwaitAirborne);
                break;
            case EPhase::AwaitAirborne:
                if (Snapshot.Get_Motion().Get_Grounded() || S.FloorLossSequence == 0)
                { break; }
                if (NOT S.Require(Landings.IsEmpty(), TEXT("Actual unsupported flight publishes no ordinary landing constraints")))
                { return; }
                S.Floor = DoAddSurface(S, S.RemovedTop, FVector{800.0, 500.0, 20.0});
                DoEnter(S, EPhase::AwaitRecovery);
                break;
            case EPhase::AwaitRecovery:
                for (auto Index = 0; Index < Snapshot.Get_Legs().Num(); ++Index)
                {
                    const auto& Leg = Snapshot.Get_Legs()[Index];
                    if (Leg.Get_AutomaticRecovery() != ck::EProceduralGaitAutomaticRecovery::None)
                    {
                        S.SawRecoveryLanding |= Leg.Get_AutomaticRecovery() == ck::EProceduralGaitAutomaticRecovery::Landing;
                        if (NOT S.Require(NOT HasLeg(S.Legs[Index]),
                            TEXT("Actual automatic recovery cannot masquerade as an ordinary commitment")))
                        { return; }
                    }
                    S.SawRecoveryResolved |= Leg.Get_NativeTouchdownResolved();
                }
                if (S.SawRecoveryLanding && S.SawRecoveryResolved && Snapshot.Get_Motion().Get_Grounded())
                { S.Complete = true; DoEnter(S, EPhase::Complete); }
                break;
            case EPhase::Complete:
                break;
        }
    }

    auto
    DoCleanup(
        FState& S)
        -> void
    {
        S.CleanupStarted = true;
        if (S.Monitor.IsValid())
        {
            FWorldDelegates::OnWorldPostActorTick.Remove(S.Monitor);
            S.Monitor.Reset();
        }
        for (const auto& Leg : S.Legs)
        {
            if (ck::IsValid(Leg))
            { ck::UUtils_Signal_OnProceduralLeg_Planted::Unbind<&FState::OnPlanted>(&S, Leg); }
        }
        S.RestoreTime();
        if (ck::IsValid(S.Gait) && S.LeaseActive)
        {
            (void)FCk_ProceduralGaitParticipation_Control::FailRecovery(S.Gait, S.Token, FName{TEXT("FixtureCleanup")});
            S.LeaseActive = false;
        }
        if (ck::IsValid(S.Root) && S.BodyClaimActive)
        {
            (void)FCk_ProceduralArticulation_Control::ReleaseClaims(
                UCk_Utils_ProceduralArticulation_UE::Cast(S.Root), S.BodyToken,
                FCk_ArticulationChannelSet{TArray<FName>{}, true});
            S.BodyClaimActive = false;
        }
        if (ck::IsValid(S.Root))
        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(S.Root); }
        for (auto& Surface : S.Surfaces)
        {
            if (ck::IsValid(Surface.Entity))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Surface.Entity); }
        }
    }

    auto
    DoRun(
        FAutomationTestBase* InTest,
        EScenario InScenario)
        -> bool
    {
        const auto State = MakeShared<FState>();
        State->Test = InTest;
        State->Scenario = InScenario;
        State->Origin = BaseOrigin + FVector{0.0, InScenario == EScenario::Lifecycle ? 2000.0 : 0.0, 0.0};
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [State](UWorld* InWorld)
            {
                State->World = InWorld;
                State->Floor = DoAddSurface(*State, State->Origin, FVector{800.0, 500.0, 20.0});
                if (State->Scenario == EScenario::MotionAndPose)
                { DoAddSurface(*State, State->Origin + FVector{350.0, 0.0, 12.0}, FVector{150.0, 450.0, 6.0}); }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                for (const auto& Surface : State->Surfaces)
                {
                    if (ck::Is_NOT_Valid(Surface.Body) || NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Surface.Body))
                    { return false; }
                }
                return DoRay(*State, State->Floor.Top).Get_Entity() == State->Floor.Entity;
            }), 5.0, TEXT("Every explicit native fixture surface is ready before composition")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [State](UWorld*)
            {
                DoCompose(*State);
                State->OriginalFixedTime = FApp::UseFixedTimeStep();
                State->OriginalFixedDelta = FApp::GetFixedDeltaTime();
                FApp::SetFixedDeltaTime(FrameSeconds);
                FApp::SetUseFixedTimeStep(true);
                State->FixedTimeActive = true;
                const auto WeakState = TWeakPtr<FState>{State};
                const auto WeakWorld = State->World;
                State->Monitor = FWorldDelegates::OnWorldPostActorTick.AddLambda(
                    [WeakState, WeakWorld](UWorld* InTickingWorld, ELevelTick, float InDeltaSeconds)
                    {
                        if (InTickingWorld != WeakWorld.Get())
                        { return; }
                        if (const auto Pinned = WeakState.Pin())
                        { DoObserve(*Pinned, InDeltaSeconds); }
                    });
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Complete || State->Failed; }),
            25.0, TEXT("Real ordinary committed landing scenario reaches its bounded terminal state")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [InTest, State](UWorld*)
            {
                InTest->TestTrue(TEXT("Native committed landing scenario completes"), State->Complete && NOT State->Failed);
                InTest->TestTrue(TEXT("Both production consumers have positive real-native samples"),
                    State->MotionSamples > 0 && State->PoseSamples > 0);
                InTest->TestTrue(TEXT("Multiple actual ordinary commitments are observed together"), State->MultiSamples > 0);
                if (State->Scenario == EScenario::MotionAndPose)
                {
                    InTest->TestTrue(TEXT("Actual Motion translates, turns and follows the native raised step"),
                        State->SawTurn && State->SawHeight);
                    InTest->TestTrue(TEXT("Positive commitments actually pace Motion without a planted reach rejection or physical override"),
                        State->BindingMotionSamples > 0 && State->BindingSequence > 0 && State->ForwardResumed);
                    InTest->TestTrue(TEXT("Actual nonidentity presentation and trusted completion are observed"),
                        State->DistinctPoseSamples > 0 && State->ConstrainedOwnerCompleted
                            && State->TrustedCompletions > State->TrustedCompletionsAtTurn);
                }
                else
                {
                    InTest->TestTrue(TEXT("A released body claim requires a fresh subsequent native publication"),
                        State->SawBodyClaimCycle && State->SawPostClaimPublication);
                    InTest->TestTrue(TEXT("Native recovery is actually entered and resolved after terrain removal"),
                        State->SawRecoveryLanding && State->SawRecoveryResolved);
                }
                InTest->AddInfo(FString::Printf(TEXT("CommittedLanding samples%d Motion%d Pose%d multi%d posed%d binding%d "
                    "trustedCompletions%d retargets%d fullGaitRays%d historicalAcquisitionEstimate%d paceTrials%d paceRays%d witness%d/%d"),
                    State->Samples, State->MotionSamples, State->PoseSamples, State->MultiSamples, State->DistinctPoseSamples,
                    State->BindingMotionSamples,
                    State->TrustedCompletions, State->Retargets, State->MaxGaitRays, AcquisitionRayEstimate,
                    State->MaxPaceTrials, State->MaxPaceRays, State->WitnessRays, WitnessRayLimit));
                InTest->AddInfo(FString::Printf(TEXT("CommittedLanding epochs arm%llu binding%llu completion%d "
                    "trustedAtArm%d floorLoss%llu lossFrame%llu boundaryMisses%d restored%d"),
                    State->TurnSourceSequence, State->BindingSequence, State->ConstrainedOwnerCompleted,
                    State->TrustedCompletionsAtTurn, State->FloorLossSequence, State->FloorLossFrame,
                    State->BoundaryMisses, State->TerrainRestored));
                DoCleanup(*State);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                if (ck::IsValid(State->Root))
                { return false; }
                for (const auto& Surface : State->Surfaces)
                {
                    if (ck::IsValid(Surface.Entity) || ck::IsValid(Surface.Body))
                    { return false; }
                }
                for (const auto& Surface : State->Surfaces)
                {
                    if (DoRay(*State, Surface.Top).Get_HasHit())
                    { return false; }
                }
                return true;
            }), 5.0, TEXT("All owned native terrain and walker entities retire before EndPIE")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
        return true;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralGaitCommittedLandingMotionPieTest,
    "Ck.ProceduralAnimation.Gait.CommittedLandingPie.MotionAndPresentation",
    ck_test_procedural_gait_committed_landing_pie::TestFlags)

auto
    FCkProceduralGaitCommittedLandingMotionPieTest::
    RunTest(
        const FString&)
    -> bool
{
    return ck_test_procedural_gait_committed_landing_pie::DoRun(this,
        ck_test_procedural_gait_committed_landing_pie::EScenario::MotionAndPose);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralGaitCommittedLandingLifecyclePieTest,
    "Ck.ProceduralAnimation.Gait.CommittedLandingPie.LifecycleAndTerrainRemoval",
    ck_test_procedural_gait_committed_landing_pie::TestFlags)

auto
    FCkProceduralGaitCommittedLandingLifecyclePieTest::
    RunTest(
        const FString&)
    -> bool
{
    return ck_test_procedural_gait_committed_landing_pie::DoRun(this,
        ck_test_procedural_gait_committed_landing_pie::EScenario::Lifecycle);
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
