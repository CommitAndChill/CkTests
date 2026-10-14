#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Fragment.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Processor.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionMotion.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Gait/CkProceduralGaitParticipation_Control.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Fragment.h"
#include "CkProceduralAnimation/LimbControl/CkProceduralLimbControl_Control.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/CkTests_Log.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck
{
    struct FFragment_TestDetachDuringActionReturn
    {
        CK_GENERATED_BODY(FFragment_TestDetachDuringActionReturn);

        FCk_Handle_ProceduralLeg Leg;
        TArray<FCk_Handle_Transform> Parts;
        TArray<FTransform> DetachedPartPoses;
        bool Armed = false;
        bool SawPendingReturn = false;
        bool DetachEnqueued = false;
        bool SawDetached = false;
        bool SawPostDetachRequest = false;
        bool SawPostDetachPoseChange = false;
        int32 PostDetachChecks = 0;
    };

    class FProcessor_TestDetachDuringActionReturn : public ck_exp::TProcessor<
        FProcessor_TestDetachDuringActionReturn,
        FCk_Handle_ProceduralArticulation,
        TReadWrite<FFragment_TestDetachDuringActionReturn>>
    {
    public:
        using Group = FGroup_Transform_Derived;
        using RunAfter = TDepList<FProcessor_ProceduralArticulation_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType InSubject,
            FFragment_TestDetachDuringActionReturn& InProbe)
            -> void
        {
            if (NOT InProbe.Armed || NOT ck::IsValid(InProbe.Leg))
            { return; }
            if (NOT InProbe.DetachEnqueued)
            {
                if (NOT InSubject.Has<FFragment_ProceduralLimbControl>())
                { return; }
                const auto& Controllers = InSubject.Get<FFragment_ProceduralLimbControl>().Get_Controllers();
                const auto Returning = Controllers.ContainsByPredicate([](const auto& InController)
                {
                    return InController.Get_ReturnState()
                        == ECk_ProceduralLimbControl_ReturnState::Returning;
                });
                const auto& Pose = InSubject.Get<FFragment_ProceduralArticulation_Pose>();
                if (NOT Returning || NOT Pose.Get_PendingObservation()
                    || Pose.Get_IssuedRevision() <= Pose.Get_SettledRevision())
                { return; }
                InProbe.SawPendingReturn = true;
                auto Leg = InProbe.Leg;
                UCk_Utils_ProceduralLeg_UE::Request_Detach(Leg, FCk_Request_ProceduralLeg_Detach{}, {});
                InProbe.DetachEnqueued = true;
                return;
            }
            if (UCk_Utils_ProceduralLeg_UE::Get_IsAttached(InProbe.Leg))
            { return; }
            if (NOT InProbe.SawDetached)
            {
                InProbe.SawDetached = true;
                for (const auto& Part : InProbe.Parts)
                { InProbe.DetachedPartPoses.Add(UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Part)); }
            }
            for (auto Index = 0; Index < InProbe.Parts.Num(); ++Index)
            {
                const auto& Part = InProbe.Parts[Index];
                if (Part.Has<FFragment_Transform_Requests>())
                {
                    const auto& Requests = Part.Get<FFragment_Transform_Requests>();
                    InProbe.SawPostDetachRequest |= NOT Requests.Get_LocationRequests().IsEmpty()
                        || NOT Requests.Get_RotationRequests().IsEmpty()
                        || Requests.Get_ScaleRequests().IsSet();
                }
                InProbe.SawPostDetachPoseChange |= NOT UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Part)
                    .Equals(InProbe.DetachedPartPoses[Index], 0.02);
            }
            ++InProbe.PostDetachChecks;
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestDetachDuringActionReturn);

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_gait_recovery_pie
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 20.0;
    const auto LegId = FName{TEXT("Leg0")};
    const auto BodyLocation = FVector{0.0, 47000.0, 5060.0};
    const auto FloorLocation = FVector{0.0, 47000.0, 4975.0};

    struct FState
    {
        FCk_Handle Root;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_Handle Floor;
        FCk_Handle_JoltBody FloorBody;
        FDelegateHandle RecoveryMonitor;
        double RecoveryElapsedSeconds = 0.0;
        double LastAcceptedElapsedSeconds = 0.0;
        double PreviousHoldElapsedSeconds = -1.0;
        int64 PreviousHoldRevision = 0;
        FTransform PreviousHoldSegment = FTransform::Identity;
        bool ObservedNoCandidateHold = false;
        bool HoldInvariantFailed = false;
        bool RecoveryCandidateDiagnosisCaptured = false;
        FString RecoveryCandidateDiagnosis;
        bool SupportInterruptionDiagnosisCaptured = false;
        bool LateRecoveryDiagnosisCaptured = false;
        FString LaterRecoveryDiagnostics;
        bool DenseReturnPathCaptured = false;
        FString DenseReturnPathDiagnostics;
        int32 ReturnWorldFrames = 0;
        int32 ReturnAcceptedAdvances = 0;
        double LastCountedReturnElapsed = 0.0;
        uint64 LastRecoveryGeneration = 0;
        double RecoveryGenerationOrigin = 0.0;
        double RecoveryGenerationMaxElapsed = 0.0;
        bool RecoveryGenerationTerminalLogged = false;
        TArray<FString> RecoveryLandingDiagnostics;
        bool ReattachmentHeldEndpointCaptured = false;
        bool ObservedSettledReattachment = false;
        bool ReattachmentPreservedEndpoint = true;
        bool ReattachmentKeptRecoveryClock = true;
        bool ObservedSupportHoldInterruption = false;
        FVector ReattachmentHeldEndpoint = FVector::ZeroVector;
        TArray<FString> RecoveryTransitions;
        FString LastRecoveryTransition;
        TArray<FString> RecoveryProbeTransitions;
        FString LastRecoveryProbeKey;
        FString LastRecoveryProbe;
        bool RecoveryFixedTimeActive = false;
        bool OriginalFixedTime = false;
        double OriginalFixedDeltaSeconds = 0.0;
        int32 ExplicitRecoveryWorldFrames = 0;
        int32 ExplicitRecoveryAcceptedAdvances = 0;
        double ExplicitRecoveryLastAcceptedSeconds = 0.0;
        double ExplicitRecoveryMaxWorldDeltaSeconds = 0.0;

        auto RestoreFixedTime() -> void
        {
            if (NOT RecoveryFixedTimeActive) { return; }
            FApp::SetFixedDeltaTime(OriginalFixedDeltaSeconds);
            FApp::SetUseFixedTimeStep(OriginalFixedTime);
            RecoveryFixedTimeActive = false;
        }

        ~FState()
        {
            RestoreFixedTime();
            if (RecoveryMonitor.IsValid())
            { FWorldDelegates::OnWorldPostActorTick.Remove(RecoveryMonitor); }
        }
    };

    auto
        DoAddFloor(UWorld* InWorld, FState& InOutState)
        -> void
    {
        InOutState.Floor = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        UCk_Utils_Transform_UE::Add(InOutState.Floor, FTransform{FloorLocation},
            ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
        Shape.Set_HalfExtents(FVector{800.0, 800.0, 25.0});
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_CollisionProfileName(TEXT("BlockAll"));
        InOutState.FloorBody = UCk_Utils_JoltBody_UE::Add(InOutState.Floor, Spec);
    }

    auto
        DoAddWalker(UWorld* InWorld, FState& InOutState)
        -> void
    {
        InOutState.Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        InOutState.Body = UCk_Utils_Transform_UE::Add(InOutState.Root,
            FTransform{BodyLocation}, ECk_Replication::DoesNotReplicate);
        auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
        const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0}, {-40.0, 30.0}, {-40.0, -30.0}};
        for (auto Index = 0; Index < Corners.Num(); ++Index)
        {
            const auto& Corner = Corners[Index];
            auto Placement = FCk_ProceduralLeg_Placement{
                FVector{Corner.X, Corner.Y, 0.0}, FVector{Corner.X, Corner.Y * 2.0, -60.0}};
            Placement.Set_PhaseOffset(Index == 0 || Index == 3 ? 0.0f : 0.5f);
            Legs.Emplace(FName{*FString::Printf(TEXT("Leg%d"), Index)}, Placement,
                FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 80.0f}});
        }
        auto* Rig = NewObject<UCk_ProceduralRig_Data>();
        Rig->Set_Legs(Legs);
        auto Segments = TArray<FCk_Handle_Transform>{};
        for (auto Index = 0; Index < 2; ++Index)
        {
            auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOutState.Body);
            Segments.Add(UCk_Utils_Transform_UE::Add(Entity, FTransform{BodyLocation},
                ECk_Replication::DoesNotReplicate));
        }
        auto FootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOutState.Body);
        const auto Foot = UCk_Utils_Transform_UE::Add(FootEntity, FTransform{BodyLocation},
            ECk_Replication::DoesNotReplicate);
        auto RigSpec = FCk_ProceduralRig_Spec{Segments};
        RigSpec.Set_Foot(Foot);
        const auto Chains = TArray<FCk_ProceduralWalker_LegChain>{
            FCk_ProceduralWalker_LegChain{LegId, RigSpec}};
        const auto Walker = UCk_Utils_ProceduralAnimation_UE::Add_Walker(InOutState.Body,
            Rig, NewObject<UCk_ProceduralGait_Data>(), Chains);
        InOutState.Gait = Walker.Get_Gait();
        InOutState.Subject = UCk_Utils_ProceduralArticulation_UE::Cast(InOutState.Root);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitNoFloorFailureAndExplicitRecoveryTest,
    "Ck.ProceduralAnimation.Action.Gait.NoFloorFailureAndExplicitRecovery",
    ck_test_procedural_gait_recovery_pie::TestFlags)

auto
    FCkProceduralGaitNoFloorFailureAndExplicitRecoveryTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_recovery_pie;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddFloor(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->FloorBody) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->FloorBody); }),
        WaitSeconds, TEXT("Recovery floor is present in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            DoAddWalker(InWorld, *State);
            TestTrue(TEXT("Bound walker and same-home articulation compose"),
                ck::IsValid(State->Gait) && ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0
                && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait) == 4;
        }), WaitSeconds, TEXT("Bound walker settles over four planted feet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            auto Support = FCk_ProceduralLimbControl_Support{
                ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
            Support.Set_SupportHoldTimeout(FCk_Time{0.5});
            Locomotion.Set_Support(Support);
            auto Return = FCk_ProceduralLimbControl_Return{};
            Return.Set_Duration(FCk_Time{0.2});
            Return.Set_Timeout(FCk_Time{0.8});
            Return.Set_MaxProbeAttempts(2);
            Locomotion.Set_Return(Return);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Locomotion(Locomotion);
            const auto Limb = UCk_Utils_ProceduralArticulation_UE::Get_Limb(State->Subject, LegId);
            TestTrue(TEXT("The bound legacy leg is a live controllable limb"), ck::IsValid(Limb));
            auto Layout = FCk_ProceduralLimb_Layout{};
            TestTrue(TEXT("The canonical legacy layout resolves by bound limb identity"),
                UCk_Utils_ProceduralLimb_UE::Try_GetLayout(Limb, Layout));
            TestEqual(TEXT("Legacy public layout identifies canonical gait role"),
                Layout.Get_Role(),
                ECk_ProceduralLimb_Role::Locomotor);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, Spec);
            TestTrue(TEXT("Player admits configured locomotor borrowing"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LegId}};
            Control.Set_InputTimeout(FCk_Time{10.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope, State->Player, Control);
            TestTrue(TEXT("Manual action admits the bound walking limb"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                    == ECk_ProceduralAction_State::Ready
                && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State()
                    == ECk_ProceduralLimbControl_State::Controlled
                && NOT FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                    State->Gait, LegId).Get_Participating();
        }), WaitSeconds, TEXT("Walking limb is borrowed under live support protection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return NOT ck::IsValid(State->FloorBody); }),
        WaitSeconds, TEXT("Jolt floor removal completes before return")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            UCk_Utils_ProceduralAction_UE::Request_End(State->Action,
                FCk_Request_ProceduralAction_End{}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State()
                == ECk_ProceduralLimbControl_State::Unplanted;
        }), WaitSeconds, TEXT("No-floor return reaches finite Unplanted failure")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            const auto Before = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId);
            TestTrue(TEXT("No-floor failure retains a diagnostic reason"), NOT Before.Get_Reason().IsNone());
            DoAddFloor(InWorld, *State);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->FloorBody) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->FloorBody); }),
        WaitSeconds, TEXT("Floor is restored before explicit recovery")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            for (auto Index = 1; Index < 4; ++Index)
            {
                const auto Id = FName{*FString::Printf(TEXT("Leg%d"), Index)};
                if (NOT FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                    State->Gait, Id).Get_TrustedSupport())
                { return false; }
            }
            return true;
        }), WaitSeconds, TEXT("Three independent gait contacts trust the restored floor")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld)
        {
            State->OriginalFixedTime = FApp::UseFixedTimeStep();
            State->OriginalFixedDeltaSeconds = FApp::GetFixedDeltaTime();
            FApp::SetFixedDeltaTime(1.0 / 60.0);
            FApp::SetUseFixedTimeStep(true);
            State->RecoveryFixedTimeActive = true;
            const auto WeakState = TWeakPtr<FState>{State};
            const auto WeakWorld = TWeakObjectPtr<UWorld>{InWorld};
            State->RecoveryMonitor = FWorldDelegates::OnWorldPostActorTick.AddLambda(
                [WeakState, WeakWorld](UWorld* InTickingWorld, ELevelTick, float InDeltaSeconds)
                {
                    if (InTickingWorld != WeakWorld.Get()) { return; }
                    const auto Pinned = WeakState.Pin();
                    if (NOT Pinned.IsValid() || NOT ck::IsValid(Pinned->Subject)) { return; }
                    Pinned->RecoveryElapsedSeconds += FMath::Max(InDeltaSeconds, 0.0f);
                    const auto Control = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                        Pinned->Subject, LegId);
                    const auto& Controllers = Pinned->Subject.Get<ck::FFragment_ProceduralLimbControl>()
                        .Get_Controllers();
                    const auto ReturnState = Controllers.IsEmpty()
                        ? ECk_ProceduralLimbControl_ReturnState::None
                        : Controllers[0].Get_ReturnState();
                    const auto Lease = Controllers.IsEmpty()
                        ? FCk_ProceduralGaitParticipation_Observation{}
                        : FCk_ProceduralGaitParticipation_Control::GetObservation(
                            Pinned->Gait, Controllers[0].Get_Token());
                    if (NOT Controllers.IsEmpty()
                        && ReturnState == ECk_ProceduralLimbControl_ReturnState::Returning)
                    {
                        ++Pinned->ExplicitRecoveryWorldFrames;
                        Pinned->ExplicitRecoveryMaxWorldDeltaSeconds = FMath::Max(
                            Pinned->ExplicitRecoveryMaxWorldDeltaSeconds,
                            static_cast<double>(InDeltaSeconds));
                        const auto AcceptedSeconds = Lease.Get_AcceptedElapsed().Get_Seconds();
                        if (AcceptedSeconds > Pinned->ExplicitRecoveryLastAcceptedSeconds + 1.0e-6)
                        {
                            ++Pinned->ExplicitRecoveryAcceptedAdvances;
                            Pinned->ExplicitRecoveryLastAcceptedSeconds = AcceptedSeconds;
                        }
                        auto Candidate = FCk_ProceduralGaitParticipation_RecoveryCandidate{};
                        const auto Evaluation = FCk_ProceduralGaitParticipation_Control::EvaluateRecovery(
                            Pinned->Gait, Controllers[0].Get_Token(), Lease.Get_AcceptedElapsed(),
                            Lease.Get_AcceptedElapsed(), Candidate);
                        const auto Debug = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(Pinned->Root);
                        if (NOT Debug.Get_Legs().IsEmpty())
                        {
                            const auto& Leg = Debug.Get_Legs()[0];
                            const auto& Publication = Pinned->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
                            const auto Key = FString::Printf(
                                TEXT("generation=%llu evaluate=%d targetValid=%d probeHit=%d probeState=%d "
                                    "probeAttempts=%d footholds=%d chosen=%d landingGround=%d"),
                                static_cast<unsigned long long>(Lease.Get_RecoveryGeneration()),
                                static_cast<int32>(Evaluation), Leg.Get_Targeting().Get_TargetValid(),
                                Leg.Get_Probe().Get_Hit(), static_cast<int32>(Leg.Get_Probe().Get_State()),
                                Leg.Get_Probe().Get_AttemptCount(), Leg.Get_Footholds().Num(),
                                Leg.Get_ChosenFoothold(), static_cast<int32>(Leg.Get_LandingGround()));
                            Pinned->LastRecoveryProbe = FString::Printf(
                                TEXT("t=%.3f accepted=%.3f %s issued=%llu settled=%llu pending=%d"),
                                Pinned->RecoveryElapsedSeconds, Lease.Get_AcceptedElapsed().Get_Seconds(),
                                *Key, static_cast<unsigned long long>(Publication.Get_IssuedRevision()),
                                static_cast<unsigned long long>(Publication.Get_SettledRevision()),
                                Publication.Get_PendingObservation());
                            if (Key != Pinned->LastRecoveryProbeKey)
                            {
                                Pinned->LastRecoveryProbeKey = Key;
                                if (Pinned->RecoveryProbeTransitions.Num() < 16)
                                { Pinned->RecoveryProbeTransitions.Add(Pinned->LastRecoveryProbe); }
                            }
                        }
                    }
                    if (NOT Controllers.IsEmpty()
                        && ReturnState == ECk_ProceduralLimbControl_ReturnState::Returning)
                    {
                        auto ReturnActual = FCk_ProceduralArticulation_Pose{};
                        if (FCk_ProceduralLimbControl_Control::Get_ReturnActualPose(
                            Pinned->Subject, Controllers[0].Get_Token(), ReturnActual))
                        {
                            const auto* Limb = ReturnActual.Get_Limbs().FindByPredicate([](const auto& InLimb)
                            { return InLimb.Get_Id() == LegId; });
                            if (Limb != nullptr)
                            {
                                if (NOT Pinned->ReattachmentHeldEndpointCaptured)
                                {
                                    Pinned->ReattachmentHeldEndpoint = Limb->Get_AcceptedEndpoint().GetLocation();
                                    Pinned->ReattachmentHeldEndpointCaptured = true;
                                }
                                if (Controllers[0].Get_ReattachmentSettledRevision() > 0
                                    && NOT Pinned->ObservedSettledReattachment)
                                {
                                    Pinned->ObservedSettledReattachment = true;
                                    Pinned->ReattachmentPreservedEndpoint &= Limb->Get_AcceptedEndpoint()
                                        .GetLocation().Equals(Pinned->ReattachmentHeldEndpoint, 0.02);
                                    Pinned->ReattachmentKeptRecoveryClock &= Lease.Get_AcceptedElapsed()
                                        == FCk_Time{};
                                }
                            }
                        }
                    }
                    if (NOT Controllers.IsEmpty())
                    { Pinned->LastAcceptedElapsedSeconds = Lease.Get_AcceptedElapsed().Get_Seconds(); }
                    const auto Transition = FString::Printf(
                        TEXT("control=%d reason=%s return=%d lease=%d leaseReason=%s controllers=%d"),
                        static_cast<int32>(Control.Get_State()), *Control.Get_Reason().ToString(),
                        static_cast<int32>(ReturnState), static_cast<int32>(Lease.Get_State()),
                        *Lease.Get_Reason().ToString(), Controllers.Num());
                    if (Transition != Pinned->LastRecoveryTransition)
                    {
                        Pinned->LastRecoveryTransition = Transition;
                        if (Pinned->RecoveryTransitions.Num() < 64)
                        {
                            Pinned->RecoveryTransitions.Add(FString::Printf(TEXT("t=%.3f accepted=%.3f %s"),
                                Pinned->RecoveryElapsedSeconds, Lease.Get_AcceptedElapsed().Get_Seconds(),
                                *Transition));
                        }
                    }
                });
            UCk_Utils_ProceduralArticulation_UE::Request_RecoverLimbs(State->Subject,
                FCk_Request_ProceduralArticulation_RecoverLimbs{TArray<FName>{LegId}}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State()
                == ECk_ProceduralLimbControl_State::Default;
        }), WaitSeconds, TEXT("Explicit recovery replants only after actual settled contact")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (State->RecoveryMonitor.IsValid())
            {
                FWorldDelegates::OnWorldPostActorTick.Remove(State->RecoveryMonitor);
                State->RecoveryMonitor.Reset();
            }
            State->RestoreFixedTime();
            const auto Transitions = FString::Printf(
                TEXT("[GaitRecovery transitions] elapsed=%.3f worldFrames=%d acceptedAdvances=%d "
                    "maxWorldDelta=%.6f count=%d %s"),
                State->RecoveryElapsedSeconds, State->ExplicitRecoveryWorldFrames,
                State->ExplicitRecoveryAcceptedAdvances,
                State->ExplicitRecoveryMaxWorldDeltaSeconds, State->RecoveryTransitions.Num(),
                *FString::Join(State->RecoveryTransitions, TEXT(" | ")));
            AddInfo(Transitions);
            ck::tests::Log(TEXT("{}"), Transitions);
            const auto ProbeHistory = FString::Printf(TEXT("[GaitRecovery probe] %s | last=%s"),
                *FString::Join(State->RecoveryProbeTransitions, TEXT(" | ")),
                *State->LastRecoveryProbe);
            AddInfo(ProbeHistory);
            ck::tests::Log(TEXT("{}"), ProbeHistory);
            const auto After = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId);
            if (After.Get_State() != ECk_ProceduralLimbControl_State::Default)
            {
                const auto& Controllers = State->Subject.Get<ck::FFragment_ProceduralLimbControl>()
                    .Get_Controllers();
                auto Lease = FCk_ProceduralGaitParticipation_Observation{};
                auto Candidate = FCk_ProceduralGaitParticipation_RecoveryCandidate{};
                auto CandidateResult = ECk_ProceduralGaitParticipation_Result::InvalidToken;
                auto ReturnState = ECk_ProceduralLimbControl_ReturnState::None;
                if (NOT Controllers.IsEmpty())
                {
                    ReturnState = Controllers[0].Get_ReturnState();
                    Lease = FCk_ProceduralGaitParticipation_Control::GetObservation(
                        State->Gait, Controllers[0].Get_Token());
                    CandidateResult = FCk_ProceduralGaitParticipation_Control::EvaluateRecovery(
                        State->Gait, Controllers[0].Get_Token(), Lease.Get_AcceptedElapsed(),
                        Lease.Get_AcceptedElapsed(), Candidate);
                }
                const auto Limb = FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                    State->Gait, LegId);
                const auto& Bindings = State->Subject.Get<ck::FFragment_ProceduralArticulation_Bindings>()
                    .Get_Bindings();
                const auto Actual = NOT Bindings.IsEmpty() && NOT Bindings[0].Get_Segments().IsEmpty()
                    ? UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Bindings[0].Get_Segments().Last())
                        .GetLocation() : FVector::ZeroVector;
                const auto Target = NOT Candidate.Get_Targets().IsEmpty()
                    ? Candidate.Get_Targets()[0].Get_RequestedEndpointWorld().GetLocation()
                    : FVector::ZeroVector;
                const auto& Publication = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
                const auto Debug = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(State->Root);
                const auto DebugFoot = NOT Debug.Get_Legs().IsEmpty()
                    ? Debug.Get_Legs()[0].Get_Foot() : FCk_ProceduralAnimation_DebugFoot{};
                const auto DebugLanding = NOT Debug.Get_Legs().IsEmpty()
                    ? Debug.Get_Legs()[0].Get_LandingPointWorld() : FVector::ZeroVector;
                AddInfo(FString::Printf(TEXT("[GaitRecovery diagnostic] control=%d reason=%s controllers=%d return=%d lease=%d leaseReason=%s generation=%llu solve=%llu accepted=%.3f candidate=%d actual=%s target=%s gaitFoot=%s gaitLanding=%s gaitSwing=%.3f gaitPlanted=%d limbExists=%d structural=%d participating=%d trusted=%d unplanted=%d limbReason=%s articulation=%d fault=%d issuedRevision=%llu settledRevision=%llu pending=%d gaitStatus=%d planted=%d"),
                    static_cast<int32>(After.Get_State()), *After.Get_Reason().ToString(),
                    Controllers.Num(), static_cast<int32>(ReturnState),
                    static_cast<int32>(Lease.Get_State()), *Lease.Get_Reason().ToString(),
                    static_cast<unsigned long long>(Lease.Get_RecoveryGeneration()),
                    static_cast<unsigned long long>(Lease.Get_SolveSequence()),
                    Lease.Get_AcceptedElapsed().Get_Seconds(), static_cast<int32>(CandidateResult),
                    *Actual.ToString(), *Target.ToString(), *DebugFoot.Get_Position().ToString(),
                    *DebugLanding.ToString(), DebugFoot.Get_SwingAlpha(),
                    DebugFoot.Get_Planted(), Limb.Get_Exists(),
                    Limb.Get_StructurallyEnabled(), Limb.Get_Participating(), Limb.Get_TrustedSupport(),
                    Limb.Get_Unplanted(), *Limb.Get_Reason().ToString(),
                    static_cast<int32>(UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Subject)),
                    static_cast<int32>(UCk_Utils_ProceduralArticulation_UE::Get_Fault(State->Subject)),
                    static_cast<unsigned long long>(Publication.Get_IssuedRevision()),
                    static_cast<unsigned long long>(Publication.Get_SettledRevision()),
                    Publication.Get_PendingObservation(),
                    static_cast<int32>(UCk_Utils_ProceduralGait_UE::Get_Status(State->Gait)),
                    UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait)));
            }
            TestTrue(TEXT("Trusted handoff restores gait participation"),
                After.Get_Participating() && After.Get_TrustedSupport());
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitMovedRootReturnTrustsActualContactTest,
    "Ck.ProceduralAnimation.Action.Gait.MovedRootReturnTrustsActualContact",
    ck_test_procedural_gait_recovery_pie::TestFlags)

auto
    FCkProceduralGaitMovedRootReturnTrustsActualContactTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_recovery_pie;
    const auto State = MakeShared<FState>();
    const auto RevisionBeforeMove = MakeShared<int64>(0);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddFloor(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->FloorBody)
            && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->FloorBody); }),
        WaitSeconds, TEXT("Moved-root return floor exists in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddWalker(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Subject)
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0
            && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait) == 4; }),
        WaitSeconds, TEXT("Moved-root walker publishes and plants four contacts")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            Locomotion.Set_Support(FCk_ProceduralLimbControl_Support{
                ECk_ProceduralLimbControl_SupportMode::CountOnly, 3});
            auto Return = FCk_ProceduralLimbControl_Return{};
            Return.Set_Duration(FCk_Time{0.25});
            Return.Set_Timeout(FCk_Time{2.0});
            Return.Set_MaxProbeAttempts(3);
            Locomotion.Set_Return(Return);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Locomotion(Locomotion);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, Spec);
            TestTrue(TEXT("Moved-root player admits locomotor policy"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LegId}};
            Control.Set_InputTimeout(FCk_Time{10.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, Control);
            TestTrue(TEXT("Moved-root manual action is admitted"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Action)
            && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                == ECk_ProceduralAction_State::Ready
            && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State()
                == ECk_ProceduralLimbControl_State::Controlled; }),
        WaitSeconds, TEXT("Moved-root locomotor leg is under manual control")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State, RevisionBeforeMove](UWorld*)
        {
            *RevisionBeforeMove = UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject);
            UCk_Utils_Transform_UE::Request_SetLocation(State->Body,
                FCk_Request_Transform_SetLocation{BodyLocation + FVector{45.0, 0.0, 0.0}}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State, RevisionBeforeMove]
        { return UCk_Utils_Transform_UE::Get_EntityCurrentLocation(State->Body).X > 44.0
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject)
                > *RevisionBeforeMove; }),
        WaitSeconds, TEXT("Controlled limb republishes after actual body root motion")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld)
        {
            const auto WeakState = TWeakPtr<FState>{State};
            const auto WeakWorld = TWeakObjectPtr<UWorld>{InWorld};
            State->RecoveryMonitor = FWorldDelegates::OnWorldPostActorTick.AddLambda(
                [WeakState, WeakWorld](UWorld* InTickingWorld, ELevelTick, float InDeltaSeconds)
                {
                    if (InTickingWorld != WeakWorld.Get()) { return; }
                    const auto Pinned = WeakState.Pin();
                    if (NOT Pinned.IsValid() || NOT ck::IsValid(Pinned->Subject)) { return; }
                    Pinned->RecoveryElapsedSeconds += FMath::Max(InDeltaSeconds, 0.0f);
                    const auto Control = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                        Pinned->Subject, LegId);
                    const auto& Controllers = Pinned->Subject.Get<ck::FFragment_ProceduralLimbControl>()
                        .Get_Controllers();
                    const auto ReturnState = Controllers.IsEmpty()
                        ? ECk_ProceduralLimbControl_ReturnState::None
                        : Controllers[0].Get_ReturnState();
                    const auto Lease = Controllers.IsEmpty()
                        ? FCk_ProceduralGaitParticipation_Observation{}
                        : FCk_ProceduralGaitParticipation_Control::GetObservation(
                            Pinned->Gait, Controllers[0].Get_Token());
                    if (Lease.Get_Reason() == FName{TEXT("SupportHoldTimeout")})
                    { Pinned->ObservedSupportHoldInterruption = true; }
                    if (NOT Controllers.IsEmpty()
                        && Controllers[0].Get_ReturnState()
                            == ECk_ProceduralLimbControl_ReturnState::Returning
                        && Controllers[0].Get_ReattachmentSettledRevision() > 0)
                    {
                        ++Pinned->ReturnWorldFrames;
                        const auto Accepted = Lease.Get_AcceptedElapsed().Get_Seconds();
                        if (Accepted > Pinned->LastCountedReturnElapsed + 1.0e-6)
                        {
                            ++Pinned->ReturnAcceptedAdvances;
                            Pinned->LastCountedReturnElapsed = Accepted;
                        }
                    }
                    if (NOT Controllers.IsEmpty()
                        && ReturnState == ECk_ProceduralLimbControl_ReturnState::Returning)
                    {
                        auto ReturnActual = FCk_ProceduralArticulation_Pose{};
                        if (FCk_ProceduralLimbControl_Control::Get_ReturnActualPose(
                            Pinned->Subject, Controllers[0].Get_Token(), ReturnActual))
                        {
                            const auto* Limb = ReturnActual.Get_Limbs().FindByPredicate([](const auto& InLimb)
                            { return InLimb.Get_Id() == LegId; });
                            if (Limb != nullptr && NOT Pinned->ReattachmentHeldEndpointCaptured)
                            {
                                Pinned->ReattachmentHeldEndpoint = Limb->Get_AcceptedEndpoint().GetLocation();
                                Pinned->ReattachmentHeldEndpointCaptured = true;
                            }
                            if (Limb != nullptr && Controllers[0].Get_ReattachmentSettledRevision() > 0
                                && NOT Pinned->ObservedSettledReattachment)
                            {
                                Pinned->ObservedSettledReattachment = true;
                                Pinned->ReattachmentPreservedEndpoint &= Limb->Get_AcceptedEndpoint()
                                    .GetLocation().Equals(Pinned->ReattachmentHeldEndpoint, 0.02);
                                Pinned->ReattachmentKeptRecoveryClock &= Lease.Get_AcceptedElapsed()
                                    == FCk_Time{};
                            }
                        }
                    }
                    auto Candidate = FCk_ProceduralGaitParticipation_RecoveryCandidate{};
                    if (NOT Controllers.IsEmpty())
                    { Pinned->LastAcceptedElapsedSeconds = Lease.Get_AcceptedElapsed().Get_Seconds(); }
                    const auto CandidateResult = Controllers.IsEmpty()
                        ? ECk_ProceduralGaitParticipation_Result::InvalidToken
                        : FCk_ProceduralGaitParticipation_Control::EvaluateRecovery(
                            Pinned->Gait, Controllers[0].Get_Token(), Lease.Get_AcceptedElapsed(),
                            Lease.Get_AcceptedElapsed(), Candidate);
                    const auto Target = Candidate.Get_Targets().IsEmpty()
                        ? FVector::ZeroVector : Candidate.Get_Targets()[0].Get_RequestedEndpointWorld().GetLocation();
                    if (NOT Controllers.IsEmpty() && Lease.Get_RecoveryGeneration() > 0)
                    {
                        const auto Accepted = Lease.Get_AcceptedElapsed().Get_Seconds();
                        if (Lease.Get_RecoveryGeneration() != Pinned->LastRecoveryGeneration)
                        {
                            if (Pinned->LastRecoveryGeneration > 0
                                && Pinned->RecoveryLandingDiagnostics.Num() < 8)
                            {
                                Pinned->RecoveryLandingDiagnostics.Add(FString::Printf(
                                    TEXT("generation=%llu replaced at accepted=%.3f maxAccepted=%.3f"),
                                    static_cast<unsigned long long>(Pinned->LastRecoveryGeneration), Accepted,
                                    Pinned->RecoveryGenerationMaxElapsed));
                            }
                            Pinned->LastRecoveryGeneration = Lease.Get_RecoveryGeneration();
                            Pinned->RecoveryGenerationOrigin = Accepted;
                            Pinned->RecoveryGenerationMaxElapsed = Accepted;
                            Pinned->RecoveryGenerationTerminalLogged = false;
                        }
                        Pinned->RecoveryGenerationMaxElapsed = FMath::Max(
                            Pinned->RecoveryGenerationMaxElapsed, Accepted);
                        if (NOT Pinned->RecoveryGenerationTerminalLogged
                            && Accepted - Pinned->RecoveryGenerationOrigin >= 0.25 - 1.0e-5
                            && Pinned->RecoveryLandingDiagnostics.Num() < 8)
                        {
                            auto HandoffKey = FCk_ProceduralGaitParticipation_HandoffKey{};
                            const auto Handoff = FCk_ProceduralGaitParticipation_Control::PrepareTrustedHandoff(
                                Pinned->Gait, Controllers[0].Get_Token(), HandoffKey);
                            auto ActualPose = FCk_ProceduralArticulation_Pose{};
                            FCk_ProceduralLimbControl_Control::Get_ReturnActualPose(Pinned->Subject,
                                Controllers[0].Get_Token(), ActualPose);
                            const auto* ActualLimb = ActualPose.Get_Limbs().FindByPredicate([](const auto& InLimb)
                            { return InLimb.Get_Id() == LegId; });
                            Pinned->RecoveryLandingDiagnostics.Add(FString::Printf(
                                TEXT("generation=%llu terminal accepted=%.3f origin=%.3f handoff=%d "
                                    "actual=%s target=%s distance=%.3f leaseReason=%s"),
                                static_cast<unsigned long long>(Lease.Get_RecoveryGeneration()), Accepted,
                                Pinned->RecoveryGenerationOrigin, static_cast<int32>(Handoff),
                                ActualLimb == nullptr ? TEXT("none")
                                    : *ActualLimb->Get_AcceptedEndpoint().GetLocation().ToString(),
                                *Target.ToString(), ActualLimb == nullptr ? -1.0
                                    : FVector::Distance(ActualLimb->Get_AcceptedEndpoint().GetLocation(), Target),
                                *Lease.Get_Reason().ToString()));
                            Pinned->RecoveryGenerationTerminalLogged = true;
                        }
                    }
                    if (NOT Pinned->DenseReturnPathCaptured && NOT Controllers.IsEmpty()
                        && Controllers[0].Get_ReattachmentSettledRevision() > 0
                        && Lease.Get_AcceptedElapsed() == FCk_Time{})
                    {
                        auto Actual = FCk_ProceduralArticulation_Pose{};
                        auto Budget = FCk_ProceduralLimbControl_MotionBudget{};
                        if (FCk_ProceduralLimbControl_Control::Get_ReturnActualPose(Pinned->Subject,
                                Controllers[0].Get_Token(), Actual)
                            && FCk_ProceduralLimbControl_Control::Get_ReturnMotionBudget(Pinned->Subject,
                                Controllers[0].Get_Token(), Budget))
                        {
                            auto Refinements = TArray<FString>{};
                            for (const auto SampleCount : {64, 128})
                            {
                                auto Previous = Actual;
                                auto SumTravel = 0.0;
                                auto SumAngular = 0.0;
                                auto MaxTravel = 0.0;
                                auto MaxAngular = 0.0;
                                auto FailedSample = int32{INDEX_NONE};
                                auto FailedResult = 0;
                                for (auto SampleIndex = 1; SampleIndex <= SampleCount; ++SampleIndex)
                                {
                                    auto Step = FCk_ProceduralGaitParticipation_RecoveryCandidate{};
                                    const auto Proposed = FCk_Time{0.25 * SampleIndex / SampleCount};
                                    const auto RecoveryResult = FCk_ProceduralGaitParticipation_Control::
                                        EvaluateRecovery(Pinned->Gait, Controllers[0].Get_Token(), FCk_Time{},
                                            Proposed, Step);
                                    if (RecoveryResult != ECk_ProceduralGaitParticipation_Result::Accepted)
                                    {
                                        FailedSample = SampleIndex;
                                        FailedResult = static_cast<int32>(RecoveryResult);
                                        break;
                                    }
                                    const auto Tokens = TArray<FCk_ArticulationTokenCandidate>{
                                        FCk_ArticulationTokenCandidate{Controllers[0].Get_Token(), 0,
                                            Step.Get_Targets(), TOptional<FTransform>{}}};
                                    auto Evaluation = FCk_ArticulationBatchEvaluation{};
                                    const auto EvaluateResult = FCk_ProceduralArticulation_Control::
                                        TryEvaluateCandidateBatch(Pinned->Subject, Tokens, Evaluation);
                                    if (EvaluateResult != ECk_ArticulationControlResult::Accepted
                                        || Evaluation.Get_PredictedTokenPoses().Num() != 1)
                                    {
                                        FailedSample = SampleIndex;
                                        FailedResult = 1000 + static_cast<int32>(EvaluateResult);
                                        break;
                                    }
                                    const auto& Predicted = Evaluation.Get_PredictedTokenPoses()[0];
                                    auto Travel = 0.0f;
                                    auto Angular = 0.0f;
                                    ck::procedural_action::MeasureMotion(Previous, Predicted, Travel, Angular);
                                    if (NOT FMath::IsFinite(Travel) || NOT FMath::IsFinite(Angular))
                                    {
                                        FailedSample = SampleIndex;
                                        FailedResult = 2000;
                                        break;
                                    }
                                    SumTravel += Travel;
                                    SumAngular += Angular;
                                    MaxTravel = FMath::Max(MaxTravel, static_cast<double>(Travel));
                                    MaxAngular = FMath::Max(MaxAngular, static_cast<double>(Angular));
                                    Previous = Predicted;
                                }
                                const auto RequiredPublications = FMath::Max(
                                    FMath::CeilToInt(SumTravel / Budget.Get_MaxEndpointTravelCm()),
                                    FMath::CeilToInt(SumAngular / Budget.Get_MaxSegmentAngularStepDegrees()));
                                Refinements.Add(FString::Printf(TEXT("n=%d sumTravel=%.3f sumAngular=%.3f "
                                    "maxChordTravel=%.3f maxChordAngular=%.3f sampledMinPublications=%d "
                                    "failedSample=%d failedResult=%d"), SampleCount, SumTravel, SumAngular,
                                    MaxTravel, MaxAngular, RequiredPublications, FailedSample, FailedResult));
                            }
                            Pinned->DenseReturnPathDiagnostics = FString::Join(Refinements, TEXT(" | "));
                            Pinned->DenseReturnPathCaptured = true;
                        }
                    }
                    const auto DiagnoseFirst = NOT Pinned->RecoveryCandidateDiagnosisCaptured;
                    const auto DiagnoseSupport = NOT Pinned->SupportInterruptionDiagnosisCaptured
                        && Lease.Get_AcceptedElapsed() >= FCk_Time{0.04};
                    const auto DiagnoseLate = NOT Pinned->LateRecoveryDiagnosisCaptured
                        && Lease.Get_AcceptedElapsed() >= FCk_Time{0.18};
                    if ((DiagnoseFirst || DiagnoseSupport || DiagnoseLate)
                        && NOT Controllers.IsEmpty()
                        && CandidateResult == ECk_ProceduralGaitParticipation_Result::Accepted)
                    {
                        auto Previous = FCk_ProceduralArticulation_Pose{};
                        auto Budget = FCk_ProceduralLimbControl_MotionBudget{};
                        if (FCk_ProceduralLimbControl_Control::Get_ReturnActualPose(Pinned->Subject,
                                Controllers[0].Get_Token(), Previous)
                            && FCk_ProceduralLimbControl_Control::Get_ReturnMotionBudget(Pinned->Subject,
                                Controllers[0].Get_Token(), Budget))
                        {
                            auto Details = TArray<FString>{};
                            for (const auto ProposedSeconds : {0.0, 1.0 / 960.0, 1.0 / 60.0})
                            {
                                auto Step = FCk_ProceduralGaitParticipation_RecoveryCandidate{};
                                const auto Proposed = Lease.Get_AcceptedElapsed() + FCk_Time{ProposedSeconds};
                                const auto Sample = FCk_ProceduralGaitParticipation_Control::EvaluateRecovery(
                                    Pinned->Gait, Controllers[0].Get_Token(), Lease.Get_AcceptedElapsed(),
                                    Proposed, Step);
                                if (Sample != ECk_ProceduralGaitParticipation_Result::Accepted)
                                {
                                    Details.Add(FString::Printf(TEXT("dt=%.6f sample=%d"), ProposedSeconds,
                                        static_cast<int32>(Sample)));
                                    continue;
                                }
                                const auto Tokens = TArray<FCk_ArticulationTokenCandidate>{
                                    FCk_ArticulationTokenCandidate{Controllers[0].Get_Token(), 0,
                                        Step.Get_Targets(), TOptional<FTransform>{}}};
                                auto Evaluation = FCk_ArticulationBatchEvaluation{};
                                const auto Evaluated = FCk_ProceduralArticulation_Control::TryEvaluateCandidateBatch(
                                    Pinned->Subject, Tokens, Evaluation);
                                auto Travel = 0.0f;
                                auto Angular = 0.0f;
                                auto RootTravel = -1.0;
                                if (Evaluated == ECk_ArticulationControlResult::Accepted
                                    && Evaluation.Get_PredictedTokenPoses().Num() == 1)
                                {
                                    const auto& Predicted = Evaluation.Get_PredictedTokenPoses()[0];
                                    ck::procedural_action::MeasureMotion(Previous, Predicted, Travel, Angular);
                                    RootTravel = FVector::Distance(Previous.Get_RootFrame().GetTranslation(),
                                        Predicted.Get_RootFrame().GetTranslation());
                                }
                                Details.Add(FString::Printf(TEXT("dt=%.6f evaluate=%d root=%.3f travel=%.3f/%.3f angular=%.3f/%.3f"),
                                    ProposedSeconds, static_cast<int32>(Evaluated), RootTravel, Travel,
                                    Budget.Get_MaxEndpointTravelCm(), Angular,
                                    Budget.Get_MaxSegmentAngularStepDegrees()));
                            }
                            const auto Diagnosis = FString::Printf(
                                TEXT("wall=%.3f service=%.3f accepted=%.3f lease=%d reason=%s "
                                    "settledReattach=%llu root=%s endpoint=%s segment=%s target=%s | %s"),
                                Pinned->RecoveryElapsedSeconds,
                                Pinned->Subject.Get<ck::FFragment_ProceduralLimbControl>()
                                    .Get_ServiceTime().Get_Seconds(),
                                Lease.Get_AcceptedElapsed().Get_Seconds(),
                                static_cast<int32>(Lease.Get_State()), *Lease.Get_Reason().ToString(),
                                static_cast<unsigned long long>(
                                    Controllers[0].Get_ReattachmentSettledRevision()),
                                *Previous.Get_RootFrame().GetTranslation().ToString(),
                                Previous.Get_Limbs().IsEmpty() ? TEXT("none")
                                    : *Previous.Get_Limbs()[0].Get_AcceptedEndpoint().GetLocation().ToString(),
                                Previous.Get_Limbs().IsEmpty() || Previous.Get_Limbs()[0].Get_Segments().IsEmpty()
                                    ? TEXT("none")
                                    : *Previous.Get_Limbs()[0].Get_Segments().Last().ToString(),
                                *Target.ToString(), *FString::Join(Details, TEXT(" | ")));
                            if (DiagnoseFirst)
                            {
                                Pinned->RecoveryCandidateDiagnosis = Diagnosis;
                                Pinned->RecoveryCandidateDiagnosisCaptured = true;
                            }
                            if (DiagnoseSupport)
                            {
                                Pinned->LaterRecoveryDiagnostics += TEXT("support: ") + Diagnosis + TEXT(" || ");
                                Pinned->SupportInterruptionDiagnosisCaptured = true;
                            }
                            if (DiagnoseLate)
                            {
                                Pinned->LaterRecoveryDiagnostics += TEXT("late: ") + Diagnosis;
                                Pinned->LateRecoveryDiagnosisCaptured = true;
                            }
                        }
                    }
                    const auto& Bindings = Pinned->Subject.Get<ck::FFragment_ProceduralArticulation_Bindings>()
                        .Get_Bindings();
                    const auto Actual = Bindings.IsEmpty() || Bindings[0].Get_Segments().IsEmpty()
                        ? FVector::ZeroVector : UCk_Utils_Transform_UE::Get_EntityCurrentLocation(
                            Bindings[0].Get_Segments().Last());
                    if (NOT Controllers.IsEmpty() && NOT Bindings.IsEmpty()
                        && NOT Bindings[0].Get_Segments().IsEmpty()
                        && Lease.Get_AcceptedElapsed() > FCk_Time{})
                    {
                        const auto Revision = UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(
                            Pinned->Subject);
                        const auto Segment = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                            Bindings[0].Get_Segments().Last());
                        if (Revision > Pinned->PreviousHoldRevision
                            && FMath::IsNearlyEqual(Lease.Get_AcceptedElapsed().Get_Seconds(),
                                Pinned->PreviousHoldElapsedSeconds, 1.0e-6))
                        {
                            Pinned->ObservedNoCandidateHold = true;
                            Pinned->HoldInvariantFailed |= NOT Segment.GetLocation().Equals(
                                    Pinned->PreviousHoldSegment.GetLocation(), 0.02)
                                || NOT Segment.GetRotation().Equals(
                                    Pinned->PreviousHoldSegment.GetRotation(), 1.0e-3);
                        }
                        Pinned->PreviousHoldElapsedSeconds = Lease.Get_AcceptedElapsed().Get_Seconds();
                        Pinned->PreviousHoldRevision = Revision;
                        Pinned->PreviousHoldSegment = Segment;
                    }
                    const auto StateKey = FString::Printf(
                        TEXT("control=%d reason=%s return=%d lease=%d leaseReason=%s candidate=%d reattach=%llu controllers=%d"),
                        static_cast<int32>(Control.Get_State()), *Control.Get_Reason().ToString(),
                        static_cast<int32>(ReturnState), static_cast<int32>(Lease.Get_State()),
                        *Lease.Get_Reason().ToString(), static_cast<int32>(CandidateResult),
                        static_cast<unsigned long long>(Controllers.IsEmpty()
                            ? 0 : Controllers[0].Get_ReattachmentSettledRevision()), Controllers.Num());
                    const auto Transition = FString::Printf(
                        TEXT("control=%d reason=%s return=%d lease=%d leaseReason=%s candidate=%d reattach=%llu actual=%s target=%s controllers=%d"),
                        static_cast<int32>(Control.Get_State()), *Control.Get_Reason().ToString(),
                        static_cast<int32>(ReturnState), static_cast<int32>(Lease.Get_State()),
                        *Lease.Get_Reason().ToString(), static_cast<int32>(CandidateResult),
                        static_cast<unsigned long long>(Controllers.IsEmpty()
                            ? 0 : Controllers[0].Get_ReattachmentSettledRevision()),
                        *Actual.ToString(), *Target.ToString(), Controllers.Num());
                    if (StateKey != Pinned->LastRecoveryTransition)
                    {
                        Pinned->LastRecoveryTransition = StateKey;
                        if (Pinned->RecoveryTransitions.Num() < 64)
                        {
                            Pinned->RecoveryTransitions.Add(FString::Printf(TEXT("t=%.3f accepted=%.3f %s"),
                                Pinned->RecoveryElapsedSeconds, Lease.Get_AcceptedElapsed().Get_Seconds(),
                                *Transition));
                        }
                    }
                });
            UCk_Utils_ProceduralAction_UE::Request_End(State->Action,
                FCk_Request_ProceduralAction_End{}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto Control = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                State->Subject, LegId);
            return Control.Get_State() == ECk_ProceduralLimbControl_State::Default
                && Control.Get_Participating() && Control.Get_TrustedSupport()
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                    == ECk_ProceduralAction_State::Finished
                && UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Recovery()
                    == ECk_ProceduralAction_Recovery::Succeeded;
        }), WaitSeconds, TEXT("Moved-root return handoff follows actual trusted touchdown")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (State->RecoveryMonitor.IsValid())
            {
                FWorldDelegates::OnWorldPostActorTick.Remove(State->RecoveryMonitor);
                State->RecoveryMonitor.Reset();
            }
            const auto Transitions = FString::Printf(
                TEXT("[MovedRootReturn transitions] elapsed=%.3f lastAccepted=%.3f count=%d %s"),
                State->RecoveryElapsedSeconds, State->LastAcceptedElapsedSeconds,
                State->RecoveryTransitions.Num(),
                *FString::Join(State->RecoveryTransitions, TEXT(" | ")));
            AddInfo(Transitions);
            ck::tests::Log(TEXT("{}"), Transitions);
            const auto CandidateDiagnosis = FString::Printf(TEXT("[MovedRootReturn candidate] %s"),
                *State->RecoveryCandidateDiagnosis);
            AddInfo(CandidateDiagnosis);
            ck::tests::Log(TEXT("{}"), CandidateDiagnosis);
            const auto LaterDiagnosis = FString::Printf(TEXT("[MovedRootReturn later] %s"),
                *State->LaterRecoveryDiagnostics);
            AddInfo(LaterDiagnosis);
            ck::tests::Log(TEXT("{}"), LaterDiagnosis);
            const auto PathDiagnosis = FString::Printf(
                TEXT("[MovedRootReturn sampled path] worldFrames=%d acceptedAdvances=%d %s"),
                State->ReturnWorldFrames, State->ReturnAcceptedAdvances,
                *State->DenseReturnPathDiagnostics);
            AddInfo(PathDiagnosis);
            ck::tests::Log(TEXT("{}"), PathDiagnosis);
            const auto LandingDiagnosis = FString::Printf(TEXT("[MovedRootReturn landing] %s"),
                *FString::Join(State->RecoveryLandingDiagnostics, TEXT(" | ")));
            AddInfo(LandingDiagnosis);
            ck::tests::Log(TEXT("{}"), LandingDiagnosis);
            TestFalse(TEXT("Any late no-candidate publication preserves the last accepted claimed chain"),
                State->HoldInvariantFailed);
            TestTrue(TEXT("Moved root publishes one verified reattachment before return clock advances"),
                State->ObservedSettledReattachment && State->ReattachmentKeptRecoveryClock);
            TestTrue(TEXT("Reattachment keeps the actual held endpoint in world space"),
                State->ReattachmentPreservedEndpoint);
            TestTrue(TEXT("Support hold interruption remains recorded during trusted recovery"),
                State->ObservedSupportHoldInterruption);
            const auto Limb = FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                State->Gait, LegId);
            const auto Control = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId);
            const auto ActionState = UCk_Utils_ProceduralAction_UE::Get_State(State->Action);
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->Action);
            const auto& Controllers = State->Subject.Get<ck::FFragment_ProceduralLimbControl>().Get_Controllers();
            const auto Lease = Controllers.IsEmpty()
                ? FCk_ProceduralGaitParticipation_Observation{}
                : FCk_ProceduralGaitParticipation_Control::GetObservation(
                    State->Gait, Controllers[0].Get_Token());
            const auto Diagnostic = FString::Printf(
                TEXT("[MovedRootReturn diagnostic] action=%d outcome=%d recovery=%d control=%d reason=%s controllers=%d lease=%d leaseReason=%s accepted=%.3f limbParticipating=%d limbTrusted=%d limbUnplanted=%d limbReason=%s root=%s revision=%llu"),
                static_cast<int32>(ActionState), static_cast<int32>(Result.Get_Outcome()),
                static_cast<int32>(Result.Get_Recovery()), static_cast<int32>(Control.Get_State()),
                *Control.Get_Reason().ToString(), Controllers.Num(), static_cast<int32>(Lease.Get_State()),
                *Lease.Get_Reason().ToString(), Lease.Get_AcceptedElapsed().Get_Seconds(),
                Limb.Get_Participating(), Limb.Get_TrustedSupport(), Limb.Get_Unplanted(),
                *Limb.Get_Reason().ToString(),
                *UCk_Utils_Transform_UE::Get_EntityCurrentLocation(State->Body).ToString(),
                static_cast<unsigned long long>(UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject)));
            AddInfo(Diagnostic);
            ck::tests::Log(TEXT("{}"), Diagnostic);
            TestTrue(TEXT("Moved-root return restores planted locomotion without an unplanted marker"),
                Limb.Get_Participating() && Limb.Get_TrustedSupport() && NOT Limb.Get_Unplanted());
            const auto Debug = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(State->Root);
            TestTrue(TEXT("Bound returned leg publishes its actual foot at the trusted contact"),
                NOT Debug.Get_Legs().IsEmpty()
                    && Debug.Get_Legs()[0].Get_Rig().Get_Foot().Get_Transform().GetLocation()
                        .Equals(Debug.Get_Legs()[0].Get_Foot().Get_Position(), 3.0)
                    && FMath::Abs(Debug.Get_Legs()[0].Get_Rig().Get_Foot().Get_Transform()
                        .GetLocation().Z - 5000.0) < 3.0);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitContactLossCancelsBorrowTest,
    "Ck.ProceduralAnimation.Action.Gait.ContactLossCancelsBorrow",
    ck_test_procedural_gait_recovery_pie::TestFlags)

auto
    FCkProceduralGaitContactLossCancelsBorrowTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_recovery_pie;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddFloor(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->FloorBody)
            && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->FloorBody); }),
        WaitSeconds, TEXT("Contact-loss floor exists in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddWalker(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Subject)
            && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait) == 4; }),
        WaitSeconds, TEXT("Contact-loss walker has four actual planted feet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            auto Support = FCk_ProceduralLimbControl_Support{
                ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
            Support.Set_SupportHoldTimeout(FCk_Time{0.2});
            Locomotion.Set_Support(Support);
            auto Return = FCk_ProceduralLimbControl_Return{};
            Return.Set_Duration(FCk_Time{0.2});
            Return.Set_Timeout(FCk_Time{1.0});
            Return.Set_MaxProbeAttempts(2);
            Locomotion.Set_Return(Return);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Locomotion(Locomotion);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, Spec);
            TestTrue(TEXT("Contact-loss player admits the policy"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LegId}};
            Control.Set_InputTimeout(FCk_Time{10.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, Control);
            TestTrue(TEXT("Contact-loss manual action is admitted"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Action)
            && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                == ECk_ProceduralAction_State::Ready
            && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State()
                == ECk_ProceduralLimbControl_State::Controlled; }),
        WaitSeconds, TEXT("Contact-loss limb is controlled under protection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return NOT ck::IsValid(State->FloorBody); }),
        WaitSeconds, TEXT("Contact-loss floor leaves Jolt before checking support")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto ActionState = UCk_Utils_ProceduralAction_UE::Get_State(State->Action);
            return ActionState == ECk_ProceduralAction_State::Cancelling
                || ActionState == ECk_ProceduralAction_State::Returning
                || ActionState == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("Support loss autonomously closes the borrowed action")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->Action);
            const auto Limb = FCk_ProceduralGaitParticipation_Control::GetLimbObservation(State->Gait, LegId);
            const auto& Controllers = State->Subject.Get<ck::FFragment_ProceduralLimbControl>().Get_Controllers();
            const auto Lease = Controllers.IsEmpty()
                ? FCk_ProceduralGaitParticipation_Observation{}
                : FCk_ProceduralGaitParticipation_Control::GetObservation(
                    State->Gait, Controllers[0].Get_Token());
            const auto Diagnostic = FString::Printf(
                TEXT("[ContactLoss diagnostic] action=%d outcome=%d recovery=%d controllers=%d lease=%d leaseReason=%s limbParticipating=%d limbTrusted=%d limbUnplanted=%d planted=%d floorLive=%d"),
                static_cast<int32>(UCk_Utils_ProceduralAction_UE::Get_State(State->Action)),
                static_cast<int32>(Result.Get_Outcome()), static_cast<int32>(Result.Get_Recovery()),
                Controllers.Num(), static_cast<int32>(Lease.Get_State()), *Lease.Get_Reason().ToString(),
                Limb.Get_Participating(), Limb.Get_TrustedSupport(), Limb.Get_Unplanted(),
                UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait), ck::IsValid(State->FloorBody));
            AddInfo(Diagnostic);
            ck::tests::Log(TEXT("{}"), Diagnostic);
            TestEqual(TEXT("Action cancellation attributes contact loss to support"),
                Result.Get_Outcome(), ECk_ProceduralAction_Outcome::SupportLost);
            TestFalse(TEXT("Borrowed leg cannot continue normal gait after contact loss"),
                FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                    State->Gait, LegId).Get_Participating());
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitDetachDuringActionReturnTest,
    "Ck.ProceduralAnimation.Action.Gait.DetachDuringPendingReturn",
    ck_test_procedural_gait_recovery_pie::TestFlags)

auto
    FCkProceduralGaitDetachDuringActionReturnTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_recovery_pie;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddFloor(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->FloorBody) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->FloorBody); }),
        WaitSeconds, TEXT("Detach-return floor is present in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddWalker(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Subject)
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0
            && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait) == 4; }),
        WaitSeconds, TEXT("Detach-return walker has four settled planted contacts")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            auto Support = FCk_ProceduralLimbControl_Support{
                ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
            Support.Set_SupportHoldTimeout(FCk_Time{2.0});
            Locomotion.Set_Support(Support);
            auto Return = FCk_ProceduralLimbControl_Return{};
            Return.Set_Duration(FCk_Time{0.75});
            Return.Set_Timeout(FCk_Time{3.0});
            Locomotion.Set_Return(Return);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Locomotion(Locomotion);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, Spec);
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LegId}};
            Control.Set_InputTimeout(FCk_Time{10.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, Control);
            TestTrue(TEXT("Detach-return action admits one bound locomotor limb"),
                ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Action)
            && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Ready
            && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State()
                == ECk_ProceduralLimbControl_State::Controlled; }),
        WaitSeconds, TEXT("Detach-return action controls the walking limb")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto Target = FCk_ProceduralAction_LimbTarget{
                LegId, FTransform{FVector{0.0, 0.0, 0.005}}};
            Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action).Get_HasAcceptedPose(); }),
        WaitSeconds, TEXT("Detach-return action has an actual accepted moved pose")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto& Probe = State->Subject.AddOrGet<ck::FFragment_TestDetachDuringActionReturn>();
            const auto Legs = UCk_Utils_ProceduralGait_UE::Get_Legs(State->Gait);
            TestTrue(TEXT("Detach-return walker has its first live leg"),
                Legs.IsValidIndex(0) && ck::IsValid(Legs[0]));
            if (NOT Legs.IsValidIndex(0) || NOT ck::IsValid(Legs[0]))
            { return; }
            Probe.Leg = Legs[0];
            const auto& Bindings = State->Subject.Get<ck::FFragment_ProceduralArticulation_Bindings>()
                .Get_Bindings();
            const auto* Binding = Bindings.FindByPredicate([](const auto& InBinding)
            { return InBinding.Get_LimbId() == LegId; });
            TestTrue(TEXT("Detach-return articulation retains the bound parts"), Binding != nullptr);
            if (Binding == nullptr) { return; }
            Probe.Parts = Binding->Get_Segments();
            if (Binding->Get_Terminal() != FCk_Handle_Transform{})
            { Probe.Parts.Add(Binding->Get_Terminal()); }
            Probe.Armed = true;
            UCk_Utils_ProceduralAction_UE::Request_End(State->Action,
                FCk_Request_ProceduralAction_End{}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Subject.Get<ck::FFragment_TestDetachDuringActionReturn>().DetachEnqueued; }),
        WaitSeconds, TEXT("Return publication is pending when public detach is requested")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Subject.Get<ck::FFragment_TestDetachDuringActionReturn>().SawDetached; }),
        WaitSeconds, TEXT("Public detach removes the leg's structural attachment")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                == ECk_ProceduralAction_State::Finished
            && State->Subject.Get<ck::FFragment_TestDetachDuringActionReturn>().PostDetachChecks >= 3; }),
        WaitSeconds, TEXT("Detached return reaches a terminal state without later part writes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Probe = State->Subject.Get<ck::FFragment_TestDetachDuringActionReturn>();
            TestTrue(TEXT("Detach was enqueued during a genuinely pending return publication"),
                Probe.SawPendingReturn && Probe.DetachEnqueued);
            TestTrue(TEXT("Detached parts were observed for at least three later publisher passes"),
                Probe.SawDetached && Probe.PostDetachChecks >= 3);
            TestFalse(TEXT("No transform request reaches released parts after detachment"),
                Probe.SawPostDetachRequest);
            TestFalse(TEXT("Detached part world poses remain unchanged after release"),
                Probe.SawPostDetachPoseChange);
            TestEqual(TEXT("Failed detachment recovery replaces the completed return outcome"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Outcome(),
                ECk_ProceduralAction_Outcome::RecoveryFailed);
            TestEqual(TEXT("Detached return reports failed recovery"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Recovery(),
                ECk_ProceduralAction_Recovery::Failed);
            TestEqual(TEXT("Detachment releases the whole action claim"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Subject, LegId, false)
                    .Get_InstanceId(), uint64{0});
            TestFalse(TEXT("Detached leg cannot be treated as attached"),
                UCk_Utils_ProceduralLeg_UE::Get_IsAttached(Probe.Leg));
            const auto Limb = FCk_ProceduralGaitParticipation_Control::GetLimbObservation(State->Gait, LegId);
            TestFalse(TEXT("Structurally detached leg is not trusted support"), Limb.Get_TrustedSupport());
            TestFalse(TEXT("Structurally detached leg does not participate in gait"), Limb.Get_Participating());
            TestEqual(TEXT("Detached control is unavailable rather than a recoverable unplanted limb"),
                UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State(),
                ECk_ProceduralLimbControl_State::Unavailable);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralGaitDisableDuringActionReturnTest,
    "Ck.ProceduralAnimation.Action.Gait.DisableDuringReturn",
    ck_test_procedural_gait_recovery_pie::TestFlags)

auto
    FCkProceduralGaitDisableDuringActionReturnTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_gait_recovery_pie;
    const auto State = MakeShared<FState>();
    const auto DisabledLeg = MakeShared<FCk_Handle_ProceduralLeg>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddFloor(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->FloorBody) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(State->FloorBody); }),
        WaitSeconds, TEXT("Disable-return floor is present in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { DoAddWalker(InWorld, *State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Subject)
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0
            && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Gait) == 4; }),
        WaitSeconds, TEXT("Disable-return walker has four settled planted contacts")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            Locomotion.Set_Support(FCk_ProceduralLimbControl_Support{
                ECk_ProceduralLimbControl_SupportMode::CountOnly, 3});
            auto Return = FCk_ProceduralLimbControl_Return{};
            Return.Set_Duration(FCk_Time{0.75});
            Return.Set_Timeout(FCk_Time{3.0});
            Locomotion.Set_Return(Return);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Locomotion(Locomotion);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, Spec);
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LegId}};
            Control.Set_InputTimeout(FCk_Time{10.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, Control);
            TestTrue(TEXT("Disable-return action admits one bound locomotor limb"),
                ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Action)
            && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Ready
            && UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State()
                == ECk_ProceduralLimbControl_State::Controlled; }),
        WaitSeconds, TEXT("Disable-return action controls the walking limb")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto Target = FCk_ProceduralAction_LimbTarget{
                LegId, FTransform{FVector{0.0, 0.0, 0.005}}};
            Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action).Get_HasAcceptedPose(); }),
        WaitSeconds, TEXT("Disable-return action accepts an actual moved pose")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, DisabledLeg](UWorld*)
        {
            const auto Legs = UCk_Utils_ProceduralGait_UE::Get_Legs(State->Gait);
            if (NOT TestTrue(TEXT("Disable-return walker has a live first leg"),
                Legs.IsValidIndex(0) && ck::IsValid(Legs[0])))
            { return; }
            *DisabledLeg = Legs[0];
            UCk_Utils_ProceduralAction_UE::Request_End(State->Action,
                FCk_Request_ProceduralAction_End{}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
            == ECk_ProceduralAction_State::Returning; }),
        WaitSeconds, TEXT("Disable-return action enters Returning before structural override")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, DisabledLeg](UWorld*)
        {
            if (NOT TestTrue(TEXT("Disable-return leg remains live at override"), ck::IsValid(*DisabledLeg)))
            { return; }
            UCk_Utils_ProceduralLeg_UE::Request_EnableDisable(*DisabledLeg,
                FCk_Request_ProceduralLeg_EnableDisable{ECk_EnableDisable::Disable}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State, DisabledLeg]
        { return ck::IsValid(*DisabledLeg)
            && NOT FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                State->Gait, LegId).Get_StructurallyEnabled()
            && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                == ECk_ProceduralAction_State::Finished; }),
        WaitSeconds, TEXT("Disabled returning limb forces finite terminal recovery failure")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, DisabledLeg](UWorld*)
        {
            TestTrue(TEXT("Disabled limb remains attached but loses structural support"),
                UCk_Utils_ProceduralLeg_UE::Get_IsAttached(*DisabledLeg)
                    && NOT FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                        State->Gait, LegId).Get_StructurallyEnabled());
            TestEqual(TEXT("Disable during return ends with failed recovery"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Recovery(),
                ECk_ProceduralAction_Recovery::Failed);
            TestEqual(TEXT("Disable during return reports recovery failure"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Outcome(),
                ECk_ProceduralAction_Outcome::RecoveryFailed);
            TestEqual(TEXT("Disabled return releases the action claim"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Subject, LegId, false)
                    .Get_InstanceId(), uint64{0});
            TestFalse(TEXT("Disabled returning limb is not trusted support"),
                FCk_ProceduralGaitParticipation_Control::GetLimbObservation(
                    State->Gait, LegId).Get_TrustedSupport());
            TestEqual(TEXT("Disabled control is unavailable"),
                UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Subject, LegId).Get_State(),
                ECk_ProceduralLimbControl_State::Unavailable);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

// --------------------------------------------------------------------------------------------------------------------
