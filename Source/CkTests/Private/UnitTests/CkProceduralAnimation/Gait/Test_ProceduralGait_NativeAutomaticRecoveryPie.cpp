#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Query/CkJoltQuery_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/BodyPose/CkProceduralBodyPose_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralGaitSolver.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Fragment.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_native_automatic_recovery_pie
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto FrameSeconds = 1.0 / 60.0;
    constexpr auto DeadlineSeconds = 35.0;
    constexpr auto PendingSeconds = 2.5;
    constexpr auto Clearance = 60.0f;
    constexpr auto Reach = 140.0;
    constexpr auto QueryLimit = 4096;
    const auto Origin = FVector{120000.0, 389000.0, 5000.0};

    enum class EPhase : uint8
    {
        EstablishPlants, AwaitAirborne, AwaitGrounded, Pending, AwaitLanding,
        AwaitRemovedContact, AwaitRejectedTouchdown, RestoreRemovedContact, Complete
    };

    struct FSurface
    {
        FCk_Handle Entity;
        FCk_Handle_JoltBody Body;
        FVector Top = FVector::ZeroVector;
    };

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UWorld> World;
        FCk_Handle Root;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_SurfaceMotion Motion;
        FCk_Handle_ProceduralBodyPose BodyPose;
        TArray<FCk_Handle_ProceduralLeg> Legs;
        TStrongObjectPtr<UCk_ProceduralRig_Data> Rig;
        TStrongObjectPtr<UCk_ProceduralGait_Data> Preset;
        TArray<FSurface> Surfaces;
        FSurface Floor;
        FSurface Pedestal;
        TArray<FSurface> Patches;
        FDelegateHandle Monitor;
        EPhase Phase = EPhase::EstablishPlants;
        int32 Cycle = 0;
        double Elapsed = 0.0;
        double PhaseStarted = 0.0;
        uint64 Sequence = 0;
        uint64 Frame = 0;
        TArray<FVector> PreviousFeet;
        TArray<FVector> PendingStartFeet;
        TArray<int32> Footfalls;
        TArray<int32> PendingFootfalls;
        TArray<bool> SawResolved;
        FVector PendingStartBody = FVector::ZeroVector;
        FVector RemovedPoint = FVector::ZeroVector;
        FVector RemovedNormal = FVector::UpVector;
        FCk_Handle RemovedEntity;
        int32 RemovedLeg = INDEX_NONE;
        int32 RemovedFootfalls = 0;
        double RemovedAt = 0.0;
        double OldTouchdownAt = 0.0;
        bool QualifiedMiss = false;
        bool SawRejectedTouchdown = false;
        bool SawPendingMotion = false;
        bool Failed = false;
        bool Complete = false;
        bool CleanupStarted = false;
        int32 AcceptedSamples = 0;
        int32 PendingSamples = 0;
        int32 NativeQueries = 0;
        bool FixedTimeActive = false;
        bool OriginalFixedTime = false;
        double OriginalFixedDelta = 0.0;

        auto Require(bool InCondition, const TCHAR* InMessage) -> bool
        {
            if (InCondition) { return true; }
            if (NOT Failed) { Test->AddError(FString::Printf(TEXT("%s (cycle=%d phase=%d time=%.6f seq=%llu frame=%llu)"),
                InMessage, Cycle, static_cast<int32>(Phase), Elapsed, Sequence, Frame)); }
            Failed = true;
            return false;
        }

        auto OnPlanted(FCk_Handle_ProceduralLeg InLeg, FCk_ProceduralLeg_Footfall) -> void
        {
            const auto Index = Legs.IndexOfByKey(InLeg);
            if (Footfalls.IsValidIndex(Index)) { ++Footfalls[Index]; }
        }

        auto RestoreTime() -> void
        {
            if (NOT FixedTimeActive) { return; }
            FApp::SetFixedDeltaTime(OriginalFixedDelta);
            FApp::SetUseFixedTimeStep(OriginalFixedTime);
            FixedTimeActive = false;
        }

        ~FState()
        {
            if (Monitor.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Monitor); }
            RestoreTime();
        }
    };

    auto DoRay(FState& InOutState, const FVector& InPoint, const FVector& InNormal = FVector::UpVector)
        -> FCk_Jolt_HitResult
    {
        if (NOT InOutState.Require(InOutState.NativeQueries < QueryLimit, TEXT("Test-only native witness query cap")))
        { return {}; }
        ++InOutState.NativeQueries;
        return UCk_Utils_JoltQuery_UE::Get_RayCast(InOutState.World.Get(), InPoint + InNormal * 5.0,
            InPoint - InNormal * 5.0, FCk_Jolt_QueryFilter{});
    }

    auto DoAddSurface(FState& InOutState, const FVector& InTop, const FVector& InHalfExtents) -> FSurface
    {
        auto Surface = FSurface{};
        Surface.Top = InTop;
        Surface.Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InOutState.World.Get());
        UCk_Utils_Transform_UE::Add(Surface.Entity, FTransform{InTop - FVector{0.0, 0.0, InHalfExtents.Z}},
            ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
        Shape.Set_HalfExtents(InHalfExtents);
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_CollisionProfileName(TEXT("BlockAll"));
        Surface.Body = UCk_Utils_JoltBody_UE::Add(Surface.Entity, Spec);
        InOutState.Surfaces.Add(Surface);
        return Surface;
    }

    auto DoSteer(FState& InOutState, float InSpeed) -> void
    {
        UCk_Utils_SurfaceMotion_UE::Request_Steering(InOutState.Motion,
            FCk_Request_SurfaceMotion_Steering{FVector::ForwardVector, InSpeed}, {});
    }

    auto DoEnter(FState& InOutState, EPhase InPhase) -> void
    {
        InOutState.Phase = InPhase;
        InOutState.PhaseStarted = InOutState.Elapsed;
        InOutState.Test->AddInfo(FString::Printf(TEXT("NativeRecovery phase=%d cycle=%d seq=%llu frame=%llu time=%.6f rays=%d"),
            static_cast<int32>(InPhase), InOutState.Cycle, InOutState.Sequence, InOutState.Frame,
            InOutState.Elapsed, InOutState.NativeQueries));
    }

    auto DoRemoveSupport(FState& InOutState) -> void
    {
        DoSteer(InOutState, 0.0f);
        for (auto& Surface : InOutState.Surfaces)
        { if (ck::IsValid(Surface.Entity)) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Surface.Entity); } }
        InOutState.Patches.Reset();
        DoEnter(InOutState, EPhase::AwaitAirborne);
    }

    auto DoAddPatches(FState& InOutState, const FCk_ProceduralAnimation_DebugSnapshot& InSnapshot) -> void
    {
        DoSteer(InOutState, 0.0f);
        InOutState.Patches.Reset();
        for (const auto& Leg : InSnapshot.Get_Legs())
        {
            auto Point = Leg.Get_Targeting().Get_NeutralWorld();
            Point.Z = InOutState.Pedestal.Top.Z;
            InOutState.Patches.Add(DoAddSurface(InOutState, Point, FVector{14.0, 14.0, 12.0}));
        }
    }

    auto DoCheckPlant(FState& InOutState, const FCk_ProceduralAnimation_DebugSnapshot& InSnapshot, int32 InIndex) -> bool
    {
        const auto& Leg = InSnapshot.Get_Legs()[InIndex];
        const auto& Foot = Leg.Get_Foot();
        const auto Hit = DoRay(InOutState, Foot.Get_Position(), Foot.Get_Normal());
        const auto OwnPatch = InOutState.Patches.ContainsByPredicate([&Hit](const auto& InPatch)
            { return InPatch.Entity == Hit.Get_Entity(); });
        const auto SimHip = Leg.Get_Targeting().Get_HipWorld();
        const auto Offset = InSnapshot.Get_BodyPose().Get_Offset();
        const auto Body = InSnapshot.Get_Gait().Get_BodyTransform();
        const auto PosedHip = (Offset * Body).TransformPosition(Body.InverseTransformPosition(SimHip));
        return InOutState.Require(Foot.Get_Planted() && Foot.Get_ContactTrusted() && Hit.Get_HasHit()
            && OwnPatch && FVector::Dist(Hit.Get_Position(), Foot.Get_Position()) <= 0.05
            && FVector::DotProduct(Hit.Get_Normal(), Foot.Get_Normal()) > 0.99
            && FVector::Dist(SimHip, Foot.Get_Position()) <= Reach + KINDA_SMALL_NUMBER
            && FVector::Dist(PosedHip, Foot.Get_Position()) <= Reach + KINDA_SMALL_NUMBER,
            TEXT("Recovery plant requires its own fresh native patch and physical simulation/posed reach"));
    }

    auto DoCompose(FState& InOutState) -> void
    {
        InOutState.Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InOutState.World.Get());
        InOutState.Body = UCk_Utils_Transform_UE::Add(InOutState.Root,
            FTransform{Origin + FVector{0.0, 0.0, Clearance}}, ECk_Replication::DoesNotReplicate);
        auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
        auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
        const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0}, {-40.0, 30.0}, {-40.0, -30.0}};
        for (auto Index = 0; Index < Corners.Num(); ++Index)
        {
            const auto& Corner = Corners[Index];
            auto Placement = FCk_ProceduralLeg_Placement{FVector{Corner.X, Corner.Y, 0.0},
                FVector{Corner.X, Corner.Y * 2.0, -Clearance}};
            Placement.Set_PhaseOffset(Index == 0 || Index == 3 ? 0.0f : 0.5f);
            const auto Id = FName{*FString::Printf(TEXT("Leg%d"), Index)};
            auto Leg = FCk_ProceduralLeg_Spec{Id, Placement,
                FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 80.0f}}};
            Leg.Set_FootContactRadius(8.0f);
            Legs.Add(Leg);
            auto Parts = TArray<FCk_Handle_Transform>{};
            for (auto PartIndex = 0; PartIndex < 2; ++PartIndex)
            {
                auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOutState.Body);
                Parts.Add(UCk_Utils_Transform_UE::Add(Entity, FTransform{Origin}, ECk_Replication::DoesNotReplicate));
            }
            auto FootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOutState.Body);
            const auto Foot = UCk_Utils_Transform_UE::Add(FootEntity, FTransform{Origin}, ECk_Replication::DoesNotReplicate);
            auto Chain = FCk_ProceduralRig_Spec{Parts};
            Chain.Set_Foot(Foot);
            Chains.Emplace(Id, Chain);
        }
        InOutState.Rig.Reset(NewObject<UCk_ProceduralRig_Data>());
        InOutState.Rig->Set_Legs(Legs);
        InOutState.Preset.Reset(NewObject<UCk_ProceduralGait_Data>());
        auto Timing = InOutState.Preset->Get_Timing();
        Timing.Set_StepDuration(FCk_Time{0.24});
        InOutState.Preset->Set_Timing(Timing);
        const auto Walker = UCk_Utils_ProceduralAnimation_UE::Add_Walker(InOutState.Body,
            FCk_ProceduralWalker_Spec{InOutState.Rig.Get(), InOutState.Preset.Get()}.Set_Chains(Chains));
        InOutState.Gait = Walker.Get_Gait();
        auto MotionSpec = FCk_SurfaceMotion_Spec{};
        auto Contact = MotionSpec.Get_Contact();
        Contact.Set_Clearance(Clearance);
        Contact.Set_ProbeReach(200.0f);
        Contact.Set_HeightSource(ECk_SurfaceMotion_HeightSource::Rays);
        Contact.Set_WallPolicy(ECk_SurfaceMotion_WallPolicy::Slide);
        MotionSpec.Set_Contact(Contact);
        InOutState.Motion = UCk_Utils_SurfaceMotion_UE::Add(InOutState.Body, MotionSpec);
        auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOutState.Body);
        const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
            FTransform{Origin + FVector{0.0, 0.0, Clearance}}, ECk_Replication::DoesNotReplicate);
        InOutState.BodyPose = UCk_Utils_ProceduralBodyPose_UE::Add(InOutState.Gait,
            FCk_ProceduralBodyPose_Spec{Presentation});
        InOutState.Footfalls.Init(0, 4);
        InOutState.SawResolved.Init(false, 4);
        for (auto Index = 0; Index < 4; ++Index)
        {
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(InOutState.Root,
                FName{*FString::Printf(TEXT("Leg%d"), Index)});
            InOutState.Legs.Add(Leg);
            if (InOutState.Require(ck::IsValid(Leg), TEXT("Every native walker leg composes")))
            {
                (void)ck::UUtils_Signal_OnProceduralLeg_Planted::Bind<&FState::OnPlanted>(&InOutState, Leg,
                    ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
            }
        }
        InOutState.Require(ck::IsValid(InOutState.Gait) && ck::IsValid(InOutState.Motion)
            && ck::IsValid(InOutState.BodyPose), TEXT("Public native walker, Motion and presentation compose"));
    }

    auto DoObserve(FState& S, float InDeltaSeconds) -> void
    {
        if (S.Failed || S.Complete || S.CleanupStarted) { return; }
        S.Elapsed += FMath::Max(InDeltaSeconds, 0.0f);
        if (NOT S.Require(S.Elapsed <= DeadlineSeconds, TEXT("New native recovery fixture bounded deadline"))) { return; }
        const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(S.Root);
        if (NOT Snapshot.Get_Status().Get_HasAcceptedSample()) { return; }
        const auto Seq = Snapshot.Get_Sample().Get_Sequence();
        if (Seq == S.Sequence) { return; }
        if (NOT S.Require(S.Sequence == 0 || Seq == S.Sequence + 1,
            TEXT("Observer covers consecutive accepted solve publications"))) { return; }
        S.Sequence = Seq;
        S.Frame = Snapshot.Get_Sample().Get_FrameNumber();
        ++S.AcceptedSamples;
        const auto& DebugLegs = Snapshot.Get_Legs();
        if (NOT S.Require(DebugLegs.Num() == 4 && Snapshot.Get_Freshness().Get_MotionMatchesGaitFrame(),
            TEXT("Four legs and Motion belong to the accepted solve"))) { return; }
        const auto Body = Snapshot.Get_Gait().Get_BodyTransform();
        if (NOT S.Require(NOT Body.ContainsNaN() && Snapshot.Get_BodyPose().Get_Composed(),
            TEXT("Accepted body and actual presentation are available"))) { return; }
        auto TrustedPlants = 0;
        for (auto Index = 0; Index < 4; ++Index)
        {
            const auto& Leg = DebugLegs[Index];
            const auto& Foot = Leg.Get_Foot();
            if (NOT S.Require(Leg.Get_Id() == FName{*FString::Printf(TEXT("Leg%d"), Index)}
                && NOT Foot.Get_Position().ContainsNaN() && NOT Foot.Get_Rotation().ContainsNaN()
                && UCk_Utils_ProceduralLeg_UE::Get_Foot(S.Legs[Index]).Get_Position().Equals(Foot.Get_Position(), 1.0e-4),
                TEXT("Accepted leg identity and actual finite output"))) { return; }
            TrustedPlants += Foot.Get_Planted() && Foot.Get_ContactTrusted() ? 1 : 0;
            if (Leg.Get_AutomaticRecovery() == ck::EProceduralGaitAutomaticRecovery::WaitingForContact)
            {
                if (NOT S.Require(NOT Foot.Get_Planted() && NOT Foot.Get_ContactTrusted()
                    && NOT Leg.Get_HasReservation() && NOT Leg.Get_HasReachAnchor()
                    && NOT Leg.Get_NativeTouchdownResolved()
                    && FVector::Dist(Leg.Get_Targeting().Get_HipWorld(), Foot.Get_Position()) <= Reach + KINDA_SMALL_NUMBER,
                    TEXT("Pending leg has physical reach and no plant, trust, reservation or stance anchor"))) { return; }
                const auto PublicFoot = UCk_Utils_ProceduralLeg_UE::Get_Foot(S.Legs[Index]);
                if (NOT S.Require(PublicFoot.Get_Contact() == ECk_ProceduralLeg_FootContact::Guessed
                    && PublicFoot.Get_Phase() != ECk_ProceduralLeg_FootPhase::Planted,
                    TEXT("Pending public Foot also excludes trusted/planted publication"))) { return; }
                if (S.PreviousFeet.IsValidIndex(Index))
                {
                    // A geometric continuity ceiling for this fixture, not a new production tolerance or speed claim.
                    if (NOT S.Require(FVector::Dist(S.PreviousFeet[Index], Foot.Get_Position()) < 15.0,
                        TEXT("Pending emitted foot stays continuous at fixed 1/60 fixture time"))) { return; }
                }
            }
            if (Leg.Get_NativeTouchdownResolved())
            {
                if (NOT S.Require(Leg.Get_NativeTouchdownAttempted()
                    && Leg.Get_AutomaticRecovery() == ck::EProceduralGaitAutomaticRecovery::None,
                    TEXT("Resolved marker denotes this-solve admitted completion"))) { return; }
                if (NOT DoCheckPlant(S, Snapshot, Index)) { return; }
                S.SawResolved[Index] = true;
            }
        }
        S.PreviousFeet.Reset();
        for (const auto& Leg : DebugLegs) { S.PreviousFeet.Add(Leg.Get_Foot().Get_Position()); }
        const auto Grounded = Snapshot.Get_Motion().Get_Grounded();
        const auto PhaseTime = S.Elapsed - S.PhaseStarted;
        switch (S.Phase)
        {
            case EPhase::EstablishPlants:
                if (NOT S.Require(PhaseTime < 5.0, TEXT("Initial real native plants establish"))) { return; }
                if (TrustedPlants != 4 || NOT Grounded) { break; }
                for (const auto& Leg : DebugLegs)
                {
                    const auto Hit = DoRay(S, Leg.Get_Foot().Get_Position(), Leg.Get_Foot().Get_Normal());
                    if (NOT S.Require(Hit.Get_HasHit() && Hit.Get_Entity() == S.Floor.Entity,
                        TEXT("Initial plants actually contact the owned native floor"))) { return; }
                }
                DoRemoveSupport(S);
                break;
            case EPhase::AwaitAirborne:
                if (NOT S.Require(PhaseTime < 2.0, TEXT("Removing native ground causes actual Motion Airborne"))) { return; }
                if (Grounded || NOT Snapshot.Get_Gait().Get_Airborne()) { break; }
                if (NOT S.Require(TrustedPlants == 0 && NOT DoRay(S, Body.GetLocation() - FVector{0.0, 0.0, Clearance}).Get_HasHit(),
                    TEXT("Actual flight retires trust and has no former native support"))) { return; }
                S.Pedestal = DoAddSurface(S, Body.GetLocation() - FVector{0.0, 0.0, Clearance}, FVector{24.0, 8.0, 12.0});
                DoEnter(S, EPhase::AwaitGrounded);
                break;
            case EPhase::AwaitGrounded:
                if (NOT S.Require(PhaseTime < 2.0, TEXT("Central native body support regrounds Motion"))) { return; }
                if (NOT Grounded || NOT Snapshot.Get_Motion().Get_TrustedContact()) { break; }
                if (NOT S.Require(DoRay(S, S.Pedestal.Top).Get_Entity() == S.Pedestal.Entity,
                    TEXT("Body support is the owned native pedestal"))) { return; }
                S.PendingStartBody = Body.GetLocation();
                S.PendingStartFeet = S.PreviousFeet;
                S.PendingFootfalls = S.Footfalls;
                DoSteer(S, 2.0f);
                DoEnter(S, EPhase::Pending);
                break;
            case EPhase::Pending:
                ++S.PendingSamples;
                if (NOT S.Require(Grounded && TrustedPlants == 0 && S.Footfalls == S.PendingFootfalls,
                    TEXT("Central body support never fabricates a leg plant or touchdown signal"))) { return; }
                for (auto Index = 0; Index < 4; ++Index)
                {
                    if (NOT S.Require(DebugLegs[Index].Get_AutomaticRecovery()
                        == ck::EProceduralGaitAutomaticRecovery::WaitingForContact,
                        TEXT("Every no-contact leg remains genuinely waiting past landing duration"))) { return; }
                    S.SawPendingMotion |= FVector::Dist(S.PendingStartFeet[Index], S.PreviousFeet[Index]) > 0.1;
                }
                if (PhaseTime < PendingSeconds) { break; }
                if (NOT S.Require(S.SawPendingMotion && FVector::Dist(S.PendingStartBody, Body.GetLocation()) > 0.1,
                    TEXT("Pending output follows a real gently moving body rather than an expired frozen swing"))) { return; }
                DoAddPatches(S, Snapshot);
                S.SawResolved.Init(false, 4);
                DoEnter(S, EPhase::AwaitLanding);
                break;
            case EPhase::AwaitLanding:
                if (NOT S.Require(PhaseTime < 3.0, TEXT("Sustained native opportunity yields bounded acquisition"))) { return; }
                if (S.Cycle == 1 && S.RemovedLeg == INDEX_NONE)
                {
                    for (auto Index = 0; Index < 4; ++Index)
                    {
                        const auto& Leg = DebugLegs[Index];
                        const auto Alpha = Leg.Get_Foot().Get_SwingAlpha();
                        if (Leg.Get_AutomaticRecovery() != ck::EProceduralGaitAutomaticRecovery::Landing
                            || Alpha <= 0.0f || Alpha >= 0.3f) { continue; }
                        const auto Point = Leg.Get_Foot().Get_SwingTarget();
                        const auto Hit = DoRay(S, Point, Leg.Get_Foot().Get_Normal());
                        const auto PatchIndex = S.Patches.IndexOfByPredicate([&Hit](const auto& Patch)
                            { return Patch.Entity == Hit.Get_Entity(); });
                        if (NOT S.Require(Hit.Get_HasHit() && PatchIndex != INDEX_NONE,
                            TEXT("Removal targets an actually committed owned native patch"))) { return; }
                        S.RemovedLeg = Index;
                        S.RemovedPoint = Hit.Get_Position();
                        S.RemovedNormal = Hit.Get_Normal();
                        S.RemovedEntity = S.Patches[PatchIndex].Entity;
                        S.RemovedFootfalls = S.Footfalls[Index];
                        S.RemovedAt = S.Elapsed;
                        const auto CadenceScale = Snapshot.Get_Gait().Get_CadenceScale();
                        if (NOT S.Require(FMath::IsFinite(CadenceScale) && FMath::IsNearlyEqual(CadenceScale, 1.0f),
                            TEXT("Gentle fixture motion retains cadence one through committed landing"))) { return; }
                        // DoBuild_SolverSettings copies authored StepDuration and leaves Airborne at these defaults.
                        // Core starts native Landing with this clamped scale and advances phase by Dt / SwingDuration.
                        const auto LandingScale = FMath::Clamp(
                            ck::FProceduralGaitSettings{}.Get_Airborne().Get_LandingStepDurationScale(), 0.1f, 1.0f);
                        const auto SwingSeconds = FMath::Max(S.Preset->Get_Timing().Get_StepDuration().Get_Seconds()
                            * LandingScale / CadenceScale, static_cast<double>(KINDA_SMALL_NUMBER));
                        S.OldTouchdownAt = S.Elapsed + (1.0 - Alpha) * SwingSeconds;
                        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(S.Patches[PatchIndex].Entity);
                        DoEnter(S, EPhase::AwaitRemovedContact);
                        break;
                    }
                    break;
                }
                if (TrustedPlants != 4) { break; }
                for (auto Index = 0; Index < 4; ++Index)
                {
                    if (NOT S.Require(S.SawResolved[Index] && S.Footfalls[Index] > S.PendingFootfalls[Index],
                        TEXT("Each restored leg passes native commit and its active listener observes a new planted signal"))) { return; }
                    if (NOT DoCheckPlant(S, Snapshot, Index)) { return; }
                }
                if (S.Cycle == 0)
                {
                    ++S.Cycle;
                    S.SawPendingMotion = false;
                    DoRemoveSupport(S);
                }
                else
                {
                    if (NOT S.Require(S.QualifiedMiss && S.SawRejectedTouchdown,
                        TEXT("Second cycle includes qualified disappearance and an actual rejected native resolver"))) { return; }
                    S.Complete = true;
                    DoEnter(S, EPhase::Complete);
                }
                break;
            case EPhase::AwaitRemovedContact:
            case EPhase::AwaitRejectedTouchdown:
            {
                const auto& Leg = DebugLegs[S.RemovedLeg];
                const auto NoPrematurePlant = NOT Leg.Get_Foot().Get_Planted() && NOT Leg.Get_HasReachAnchor()
                    && S.Footfalls[S.RemovedLeg] == S.RemovedFootfalls && NOT Leg.Get_NativeTouchdownResolved();
                if (NOT NoPrematurePlant)
                {
                    S.Require(false, *FString::Printf(TEXT("Missing committed contact has no plant, reach anchor, footfall or resolved touchdown "
                        "(leg=%d recovery=%d phase=%d alpha=%.6f planted=%d contactTrusted=%d attempted=%d resolved=%d "
                        "reservation=%d reachAnchor=%d footfalls=%d baseline=%d qualifiedMiss=%d)"),
                        S.RemovedLeg, static_cast<int32>(Leg.Get_AutomaticRecovery()),
                        static_cast<int32>(UCk_Utils_ProceduralLeg_UE::Get_Foot(S.Legs[S.RemovedLeg]).Get_Phase()),
                        Leg.Get_Foot().Get_SwingAlpha(), static_cast<int32>(Leg.Get_Foot().Get_Planted()),
                        static_cast<int32>(Leg.Get_Foot().Get_ContactTrusted()),
                        static_cast<int32>(Leg.Get_NativeTouchdownAttempted()), static_cast<int32>(Leg.Get_NativeTouchdownResolved()),
                        static_cast<int32>(Leg.Get_HasReservation()), static_cast<int32>(Leg.Get_HasReachAnchor()),
                        S.Footfalls[S.RemovedLeg], S.RemovedFootfalls, static_cast<int32>(S.QualifiedMiss)));
                    return;
                }
                if (NOT S.QualifiedMiss)
                {
                    if (NOT DoRay(S, S.RemovedPoint, S.RemovedNormal).Get_HasHit())
                    {
                        if (NOT S.Require(S.Elapsed < S.OldTouchdownAt,
                            TEXT("Deferred native removal must be observed before the original touchdown horizon"))) { return; }
                        S.QualifiedMiss = true;
                        DoEnter(S, EPhase::AwaitRejectedTouchdown);
                    }
                    else if (NOT S.Require(S.Elapsed < S.OldTouchdownAt,
                        TEXT("Native patch removal qualified in time"))) { return; }
                }
                if (Leg.Get_NativeTouchdownAttempted())
                {
                    if (NOT S.Require(S.QualifiedMiss && NOT Leg.Get_NativeTouchdownResolved()
                        && Leg.Get_AutomaticRecovery() == ck::EProceduralGaitAutomaticRecovery::WaitingForContact
                        && NOT Leg.Get_HasReservation() && NOT Leg.Get_HasReachAnchor(),
                        TEXT("Actual failed final native callback atomically returns to non-reserving pending"))) { return; }
                    S.SawRejectedTouchdown = true;
                    DoEnter(S, EPhase::RestoreRemovedContact);
                }
                else if (NOT S.Require(S.Elapsed - S.RemovedAt < 1.0,
                    TEXT("Witness the final resolver rejection rather than infer it from an earlier cancellation"))) { return; }
                break;
            }
            case EPhase::RestoreRemovedContact:
                S.Patches.Add(DoAddSurface(S, S.RemovedPoint, FVector{14.0, 14.0, 12.0}));
                DoEnter(S, EPhase::AwaitLanding);
                break;
            case EPhase::Complete:
                break;
        }
    }

    auto DoCleanup(FState& S) -> void
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
        if (ck::IsValid(S.Root)) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(S.Root); }
        for (auto& Surface : S.Surfaces)
        { if (ck::IsValid(Surface.Entity)) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Surface.Entity); } }
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralGaitNativeAutomaticRecoveryPieTest,
    "Ck.ProceduralAnimation.Gait.NativeRecoveryPie",
    ck_test_native_automatic_recovery_pie::TestFlags)

auto
    FCkProceduralGaitNativeAutomaticRecoveryPieTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_native_automatic_recovery_pie;
    const auto State = MakeShared<FState>();
    State->Test = this;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld)
        {
            State->World = InWorld;
            State->Floor = DoAddSurface(*State, Origin, FVector{200.0, 200.0, 20.0});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Floor.Body) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->Floor.Body)
                && DoRay(*State, State->Floor.Top).Get_Entity() == State->Floor.Entity;
        }), 5.0, TEXT("Owned native floor exists before composing the walker")));
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
                    if (InTickingWorld != WeakWorld.Get()) { return; }
                    if (const auto Pinned = WeakState.Pin()) { DoObserve(*Pinned, InDeltaSeconds); }
                });
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Complete || State->Failed; }),
        40.0, TEXT("Both native automatic recovery cycles complete within their own bounded deadline")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("Both complete real-native cycles satisfy the recovery lifecycle"), State->Complete && NOT State->Failed);
            TestTrue(TEXT("Pending observed beyond both ordinary and automatic landing durations"), State->PendingSamples >= 120);
            TestTrue(TEXT("Final native rejection is actually attempted and atomically rejected"),
                State->QualifiedMiss && State->SawRejectedTouchdown);
            AddInfo(FString::Printf(TEXT("NativeRecovery accepted=%d pending=%d witnessRays=%d"),
                State->AcceptedSamples, State->PendingSamples, State->NativeQueries));
            DoCleanup(*State);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (ck::IsValid(State->Root)) { return false; }
            for (const auto& Surface : State->Surfaces)
            { if (ck::IsValid(Surface.Entity) || ck::IsValid(Surface.Body)) { return false; } }
            for (const auto& Surface : State->Surfaces)
            { if (DoRay(*State, Surface.Top).Get_HasHit()) { return false; } }
            return true;
        }), 5.0, TEXT("All owned entities/native terrain retire and listeners detach before EndPIE")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
