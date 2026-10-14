#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include "CkProceduralAnimation/BodyPose/CkProceduralBodyPose_Utils.h"
#include "CkProceduralAnimation/BodyPose/CkProceduralBodyPose_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralLimbSolve.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/Rig/CkProceduralRig_Utils.h"
#include "CkProceduralAnimation/Rig/CkProceduralRig_Processor.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_legacy_capture
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto InitialBody = FTransform{FVector{0.0, 0.0, 2000.0}};
    const auto MovedBody = FTransform{FVector{12.0, 0.0, 2000.0}};
    const auto AutoOneId = FName{TEXT("AutoOne")};
    const auto AutoTwoId = FName{TEXT("AutoTwo")};
    const auto FabrikTwoId = FName{TEXT("FabrikTwo")};
    const auto CurveEightId = FName{TEXT("CurveEight")};

    struct FCase
    {
        FName Id;
        ECk_ProceduralRig_ChainSolver Solver = ECk_ProceduralRig_ChainSolver::Auto;
        TArray<float> Lengths;
        TArray<FCk_Handle_Transform> Segments;
    };

    struct FState
    {
        FCk_Handle Root;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_Transform Presentation;
        FCk_Handle_ProceduralBodyPose BodyPose;
        TArray<FCase> Cases;
        uint64 NextSolveSequence = 3;
        uint64 LastConvergenceSequence = 0;
        int32 StableSamples = 0;
        TArray<FTransform> LastObserved;
        TArray<FTransform> FrozenBeforeBodyMove;
        TArray<FTransform> DetachedBeforeRelease;
        FVector FrozenFootLocal = FVector::ZeroVector;
        int64 LegacySampleRevisionBeforePlayer = 0;
    };

    auto
        MakeLeg(
            const FCase& InCase,
            int32 InIndex)
        -> FCk_ProceduralLeg_Spec
    {
        const auto Hip = FVector{static_cast<double>(InIndex * 90), static_cast<double>(InIndex % 2 == 0 ? -35 : 35), 0.0};
        const auto Rest = Hip + FVector{0.0, InIndex % 2 == 0 ? -60.0 : 60.0, -45.0};
        auto Placement = FCk_ProceduralLeg_Placement{Hip, Rest};
        Placement.Set_PhaseOffset(static_cast<float>(InIndex) * 0.2f);
        auto Chain = FCk_ProceduralLeg_ChainGeometry{InCase.Lengths};
        Chain.Set_PoleLocal(Hip + FVector{0.0, InIndex % 2 == 0 ? -40.0 : 40.0, 90.0});
        return FCk_ProceduralLeg_Spec{InCase.Id, Placement, Chain};
    }

    auto
        MakeSegments(
            FCk_Handle_Transform& InBody,
            int32 InCount)
        -> TArray<FCk_Handle_Transform>
    {
        auto Result = TArray<FCk_Handle_Transform>{};
        for (auto Index = 0; Index < InCount; ++Index)
        {
            auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InBody);
            Result.Add(UCk_Utils_Transform_UE::Add(Entity, InitialBody, ECk_Replication::DoesNotReplicate));
        }
        return Result;
    }

    auto
        Get_IsRigSettled(
            const TSharedRef<FState>& InState)
        -> bool
    {
        const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(InState->Root);
        return Snapshot.Get_Sample().Get_Sequence() >= InState->NextSolveSequence
            && Snapshot.Get_Status().Get_RigStatus() == ECk_ProceduralAnimation_Status::Ready
            && Snapshot.Get_Freshness().Get_RigMatchesGaitSequence()
            && NOT Snapshot.Get_Freshness().Get_RigPosePending();
    }

    auto
        Get_IsRigConverged(
            const TSharedRef<FState>& InState)
        -> bool
    {
        if (NOT Get_IsRigSettled(InState))
        { return false; }
        const auto Sequence = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(InState->Root)
            .Get_Sample().Get_Sequence();
        if (Sequence == InState->LastConvergenceSequence)
        { return InState->StableSamples >= 3; }

        auto Observed = TArray<FTransform>{};
        if (ck::IsValid(InState->BodyPose))
        { Observed.Add(UCk_Utils_ProceduralBodyPose_UE::Get_Offset(InState->BodyPose)); }
        for (const auto& Case : InState->Cases)
        {
            for (const auto& Segment : Case.Segments)
            { Observed.Add(UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Segment)); }
        }
        auto Stable = Observed.Num() == InState->LastObserved.Num();
        for (auto Index = 0; Stable && Index < Observed.Num(); ++Index)
        { Stable = Observed[Index].Equals(InState->LastObserved[Index], 0.02); }
        InState->StableSamples = Stable ? InState->StableSamples + 1 : 0;
        InState->LastObserved = MoveTemp(Observed);
        InState->LastConvergenceSequence = Sequence;
        return InState->StableSamples >= 3;
    }

    auto
        ResetConvergence(
            FState& InState,
            uint64 InNextSequence)
        -> void
    {
        InState.NextSolveSequence = InNextSequence;
        InState.LastConvergenceSequence = 0;
        InState.StableSamples = 0;
        InState.LastObserved.Reset();
    }

    auto
        FormatPose(
            const FTransform& InPose)
        -> FString
    {
        const auto P = InPose.GetLocation();
        const auto Q = InPose.GetRotation();
        return FString::Printf(TEXT("p=%.6f,%.6f,%.6f q=%.9f,%.9f,%.9f,%.9f"),
            P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W);
    }

    auto
        RecordPhase(
            FAutomationTestBase& InTest,
            const FState& InState,
            const TCHAR* InPhase)
        -> void
    {
        const auto Snapshot = UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(InState.Root);
        InTest.AddInfo(FString::Printf(TEXT("LEGACY_POSE phase=%s gaitSolve=%llu body %s"),
            InPhase, static_cast<unsigned long long>(Snapshot.Get_Sample().Get_Sequence()),
            *FormatPose(UCk_Utils_Transform_UE::Get_EntityCurrentTransform(InState.Body))));
        const auto Body = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(InState.Body);
        const auto Offset = ck::IsValid(InState.BodyPose)
            ? UCk_Utils_ProceduralBodyPose_UE::Get_Offset(InState.BodyPose) : FTransform::Identity;
        const auto PosedBody = Offset * Body;
        InTest.AddInfo(FString::Printf(TEXT("LEGACY_POSE phase=%s bodyOffset %s posedBody %s"),
            InPhase, *FormatPose(Offset), *FormatPose(PosedBody)));
        if (ck::IsValid(InState.Presentation))
        {
            InTest.AddInfo(FString::Printf(TEXT("LEGACY_POSE phase=%s presentation %s"), InPhase,
                *FormatPose(UCk_Utils_Transform_UE::Get_EntityCurrentTransform(InState.Presentation))));
        }
        for (const auto& Case : InState.Cases)
        {
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(InState.Root, Case.Id);
            const auto Status = UCk_Utils_ProceduralLeg_UE::Get_Status(Leg);
            const auto Foot = UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg);
            const auto Placement = UCk_Utils_ProceduralLeg_UE::Get_Placement(Leg);
            const auto Chain = UCk_Utils_ProceduralLeg_UE::Get_ChainGeometry(Leg);
            const auto Hip = PosedBody.TransformPosition(Placement.Get_HipLocal());
            const auto Pole = PosedBody.TransformPosition(Chain.Get_PoleLocal());
            InTest.AddInfo(FString::Printf(TEXT("LEGACY_POSE phase=%s limb=%s links=%d solver=%d status=%d hip=%.6f,%.6f,%.6f pole=%.6f,%.6f,%.6f foot=%.6f,%.6f,%.6f"),
                InPhase, *Case.Id.ToString(), Case.Segments.Num(), static_cast<int32>(Case.Solver),
                static_cast<int32>(Status), Hip.X, Hip.Y, Hip.Z, Pole.X, Pole.Y, Pole.Z,
                Foot.Get_Position().X, Foot.Get_Position().Y, Foot.Get_Position().Z));
            for (auto Index = 0; Index < Case.Segments.Num(); ++Index)
            {
                const auto Part = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Case.Segments[Index]);
                InTest.AddInfo(FString::Printf(TEXT("LEGACY_POSE phase=%s limb=%s segment=%d %s"),
                    InPhase, *Case.Id.ToString(), Index, *FormatPose(Part)));
                InTest.TestTrue(FString::Printf(TEXT("%s %s segment %d is finite"), InPhase, *Case.Id.ToString(), Index),
                    NOT Part.ContainsNaN() && Part.GetRotation().IsNormalized());
            }
        }
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationLegacyPoseCaptureTest,
    "Ck.ProceduralAnimation.Action.ExtractionBaseline.LegacySettledPoses",
    ck_test_procedural_legacy_capture::TestFlags)

auto
    FCkProceduralArticulationLegacyPoseCaptureTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_legacy_capture;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Body = UCk_Utils_Transform_UE::Add(State->Root, InitialBody, ECk_Replication::DoesNotReplicate);
            State->Cases = TArray<FCase>{
                FCase{AutoOneId, ECk_ProceduralRig_ChainSolver::Auto, TArray<float>{100.0f}},
                FCase{AutoTwoId, ECk_ProceduralRig_ChainSolver::Auto, TArray<float>{60.0f, 70.0f}},
                FCase{FabrikTwoId, ECk_ProceduralRig_ChainSolver::Fabrik, TArray<float>{60.0f, 70.0f}},
                FCase{CurveEightId, ECk_ProceduralRig_ChainSolver::Curve,
                    TArray<float>{25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f}}};

            auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
            auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
            for (auto Index = 0; Index < State->Cases.Num(); ++Index)
            {
                auto& Case = State->Cases[Index];
                Legs.Add(MakeLeg(Case, Index));
                Case.Segments = MakeSegments(State->Body, Case.Lengths.Num());
                auto Rig = FCk_ProceduralRig_Spec{Case.Segments};
                Rig.Set_Solver(Case.Solver);
                Chains.Emplace(Case.Id, Rig);
            }
            auto* Rig = NewObject<UCk_ProceduralRig_Data>();
            Rig->Set_Legs(Legs);
            const auto Walker = UCk_Utils_ProceduralAnimation_UE::Add_Walker(
                State->Body, Rig, NewObject<UCk_ProceduralGait_Data>(), Chains);
            State->Gait = Walker.Get_Gait();
            TestTrue(TEXT("Production legacy walker with 1/2/8-link solver cases composes"), ck::IsValid(State->Gait));
            const auto Articulation = UCk_Utils_ProceduralArticulation_UE::Cast(State->Root);
            TestTrue(TEXT("Legacy walker has a same-home articulation"), ck::IsValid(Articulation));
            for (const auto& Case : State->Cases)
            {
                const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, Case.Id);
                const auto Limb = UCk_Utils_ProceduralArticulation_UE::Get_Limb(Articulation, Case.Id);
                TestTrue(FString::Printf(TEXT("%s generic limb is the existing leg entity"), *Case.Id.ToString()),
                    ck::IsValid(Limb) && Limb.ConvertToHandle() == Leg.ConvertToHandle());
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsRigConverged(State); }),
        WaitSeconds, TEXT("Legacy 1/2/8-link part requests settle and stabilize")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            RecordPhase(*this, *State, TEXT("initial"));
            auto Preview = TArray<ck::FProceduralRig_SelectedChain>{};
            TestTrue(TEXT("Shared rig preview solves settled default inputs"),
                ck::FProcessor_ProceduralRig_Update::EvaluateSubjectCandidate(State->Gait,
                    TArrayView<const ck::FProceduralRig_WorldTarget>{}, TOptional<FTransform>{}, Preview));
            TestEqual(TEXT("Shared rig preview has each live chain"), Preview.Num(), State->Cases.Num());
            for (const auto& Chain : Preview)
            {
                const auto* Case = State->Cases.FindByPredicate(
                    [&](const FCase& InCase) { return InCase.Id == Chain.Get_Id(); });
                if (Case == nullptr)
                { continue; }
                TestEqual(FString::Printf(TEXT("%s preview part count"), *Case->Id.ToString()),
                    Chain.Get_PartPoses().Num(), Case->Segments.Num());
                for (auto PartIndex = 0; PartIndex < Case->Segments.Num()
                    && PartIndex < Chain.Get_PartPoses().Num(); ++PartIndex)
                {
                    const auto Actual = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Case->Segments[PartIndex]);
                    TestTrue(FString::Printf(TEXT("%s preview part %d matches production settlement"),
                        *Case->Id.ToString(), PartIndex),
                        Chain.Get_PartPoses()[PartIndex].Equals(Actual, 0.02));
                }
            }
            auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Body);
            State->Presentation = UCk_Utils_Transform_UE::Add(Entity, InitialBody, ECk_Replication::DoesNotReplicate);
            auto Gait = State->Gait;
            State->BodyPose = UCk_Utils_ProceduralBodyPose_UE::Add(Gait, FCk_ProceduralBodyPose_Spec{State->Presentation});
            TestTrue(TEXT("Legacy body presentation composes"), ck::IsValid(State->BodyPose));
            auto BodyPose = State->BodyPose;
            UCk_Utils_ProceduralBodyPose_UE::Request_SetActionOffset(BodyPose,
                FCk_Request_ProceduralBodyPose_SetActionOffset{FTransform{FVector{7.0, 0.0, 0.0}}, 17}, {});
            ResetConvergence(*State, UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(State->Root)
                .Get_Sample().Get_Sequence() + 3);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_IsRigConverged(State) && ck::IsValid(State->BodyPose)
                && UCk_Utils_ProceduralBodyPose_UE::Get_ActionSample(State->BodyPose).Get_SampleId() == 17
                && UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Presentation).Equals(
                    UCk_Utils_ProceduralBodyPose_UE::Get_ActionSample(State->BodyPose).Get_WorldTransform(), 0.01);
        }), WaitSeconds, TEXT("Legacy body action pose settled on presentation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            RecordPhase(*this, *State, TEXT("body-offset"));
            auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, AutoTwoId);
            UCk_Utils_ProceduralLeg_UE::Request_EnableDisable(Leg,
                FCk_Request_ProceduralLeg_EnableDisable{ECk_EnableDisable::Disable}, {});
            ResetConvergence(*State, UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(State->Root)
                .Get_Sample().Get_Sequence() + 3);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_IsRigConverged(State)
                && UCk_Utils_ProceduralLeg_UE::Get_Status(
                    UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, AutoTwoId)) == ECk_ProceduralLeg_Status::Disabled;
        }), WaitSeconds, TEXT("Legacy disabled leg settles before body movement")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Body = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Body);
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, AutoTwoId);
            State->FrozenFootLocal = Body.InverseTransformPosition(UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg).Get_Position());
            State->FrozenBeforeBodyMove.Reset();
            for (const auto& Segment : State->Cases[1].Segments)
            { State->FrozenBeforeBodyMove.Add(UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Segment)); }
            auto BodyHandle = State->Body;
            UCk_Utils_Transform_UE::Request_SetTransform(BodyHandle, FCk_Request_Transform_SetTransform{MovedBody}, {});
            ResetConvergence(*State, UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(State->Root)
                .Get_Sample().Get_Sequence() + 3);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_IsRigConverged(State)
                && UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Body).Equals(MovedBody, 0.01);
        }), WaitSeconds, TEXT("Legacy frozen foot follows moved body through settled rig")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            RecordPhase(*this, *State, TEXT("frozen"));
            const auto Body = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Body);
            const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, AutoTwoId);
            const auto Foot = UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg).Get_Position();
            TestTrue(TEXT("Disabled foot retains its body-local position"),
                Body.InverseTransformPosition(Foot).Equals(State->FrozenFootLocal, 0.02));
            const auto PosedBody = UCk_Utils_ProceduralBodyPose_UE::Get_Offset(State->BodyPose) * Body;
            const auto Placement = UCk_Utils_ProceduralLeg_UE::Get_Placement(Leg);
            const auto Chain = UCk_Utils_ProceduralLeg_UE::Get_ChainGeometry(Leg);
            const auto Hip = PosedBody.TransformPosition(Placement.Get_HipLocal());
            const auto Pole = PosedBody.TransformPosition(Chain.Get_PoleLocal());
            auto Joints = TArray<FVector>{};
            Joints.SetNumZeroed(Chain.Get_SegmentLengths().Num() + 1);
            const auto Solved = ck::SolveProceduralLimb(ECk_ProceduralRig_ChainSolver::Auto,
                Chain.Get_SegmentLengths(), Hip, Foot, Pole, Body.GetRotation(), Joints);
            TestTrue(TEXT("Observed frozen target remains solvable"), Solved);
            for (auto Index = 0; Index < State->Cases[1].Segments.Num(); ++Index)
            {
                const auto After = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                    State->Cases[1].Segments[Index]);
                TestTrue(FString::Printf(TEXT("Disabled segment %d follows moved body"), Index),
                    FVector::Distance(After.GetLocation(), State->FrozenBeforeBodyMove[Index].GetLocation()) > 1.0);
                if (Solved)
                {
                    const auto Expected = FTransform{
                        ck::Get_ProceduralLimbSegmentRotation(Joints[Index], Joints[Index + 1], Pole),
                        (Joints[Index] + Joints[Index + 1]) * 0.5};
                    TestTrue(FString::Printf(TEXT("Disabled segment %d matches settled target solve"), Index),
                        After.Equals(Expected, 0.02));
                }
            }
            State->DetachedBeforeRelease.Reset();
            for (const auto& Segment : State->Cases[2].Segments)
            { State->DetachedBeforeRelease.Add(UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Segment)); }
            auto DetachLeg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, FabrikTwoId);
            UCk_Utils_ProceduralLeg_UE::Request_Detach(DetachLeg,
                FCk_Request_ProceduralLeg_Detach{ECk_ProceduralLeg_ReleasedPartsOwnership::KeepBodyOwned}, {});
            ResetConvergence(*State, UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(State->Root)
                .Get_Sample().Get_Sequence() + 3);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_IsRigConverged(State)
                && UCk_Utils_ProceduralLeg_UE::Get_Status(
                    UCk_Utils_ProceduralLeg_UE::TryGet_Leg(State->Root, FabrikTwoId)) == ECk_ProceduralLeg_Status::Detached;
        }), WaitSeconds, TEXT("Legacy detached leg stops taking rig poses")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            RecordPhase(*this, *State, TEXT("detached"));
            for (auto Index = 0; Index < State->Cases[2].Segments.Num(); ++Index)
            {
                TestTrue(FString::Printf(TEXT("Detached segment %d holds its last published pose"), Index),
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Cases[2].Segments[Index])
                        .Equals(State->DetachedBeforeRelease[Index], 0.01));
            }
            auto BodyPose = State->BodyPose;
            UCk_Utils_ProceduralBodyPose_UE::Request_SetActionOffset(BodyPose,
                FCk_Request_ProceduralBodyPose_SetActionOffset{FTransform::Identity, 18}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralBodyPose_UE::Get_ActionSample(State->BodyPose).Get_SampleId() == 18
                && UCk_Utils_ProceduralBodyPose_UE::Get_ActionOffset(State->BodyPose).Equals(FTransform::Identity);
        }), WaitSeconds, TEXT("Legacy action offset returns to neutral before player installation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Subject = UCk_Utils_ProceduralArticulation_UE::Cast(State->Root);
            const auto Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                Subject, FCk_ProceduralActionPlayer_Spec{});
            if (NOT TestTrue(TEXT("Player installs after neutral legacy offset"), ck::IsValid(Player)))
            { return; }
            State->LegacySampleRevisionBeforePlayer =
                UCk_Utils_ProceduralBodyPose_UE::Get_ActionSample(State->BodyPose).Get_PoseRevision();
            AddExpectedError(TEXT("Procedural body pose Request_SetActionOffset rejected body pose"),
                EAutomationExpectedErrorFlags::Contains, 2);
            auto BodyPose = State->BodyPose;
            UCk_Utils_ProceduralBodyPose_UE::Request_SetActionOffset(BodyPose,
                FCk_Request_ProceduralBodyPose_SetActionOffset{FTransform{FVector{9.0, 0.0, 0.0}}, 19}, {});
            TestTrue(TEXT("Player-owned subject does not enqueue legacy body action"),
                NOT State->BodyPose.Has<ck::FFragment_ProceduralBodyPose_Requests>()
                || State->BodyPose.Get<ck::FFragment_ProceduralBodyPose_Requests>().Get_Requests().IsEmpty());
            TestTrue(TEXT("Rejected legacy action does not alter accepted offset"),
                UCk_Utils_ProceduralBodyPose_UE::Get_ActionOffset(State->BodyPose).Equals(FTransform::Identity));
            ResetConvergence(*State, UCk_Utils_ProceduralAnimation_Debug_UE::Get_Snapshot(State->Root)
                .Get_Sample().Get_Sequence() + 3);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsRigSettled(State); }),
        WaitSeconds, TEXT("Player-owned walker continues settling without legacy sample publication")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Sample = UCk_Utils_ProceduralBodyPose_UE::Get_ActionSample(State->BodyPose);
            TestEqual(TEXT("Player-owned publications do not advance legacy body action revision"),
                Sample.Get_PoseRevision(), State->LegacySampleRevisionBeforePlayer);
            TestEqual(TEXT("Player-owned publications retain the last legacy sample identity"),
                Sample.Get_SampleId(), int64{18});
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// Frozen input/output pairs come from LEGACY_POSE phase=initial in LegacyPose-R5.txt, captured through the
// unchanged production Rig publisher. The inputs are explicit so gait timing cannot change this numerical oracle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationExtractionOracleTest,
    "Ck.ProceduralAnimation.Action.ExtractionBaseline.FrozenLegacySolverOracle",
    ck_test_procedural_legacy_capture::TestFlags)

auto
    FCkProceduralArticulationExtractionOracleTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_legacy_capture;
    struct FExpectedPart
    {
        FVector Center;
        FQuat Rotation;
    };
    struct FOracleCase
    {
        const TCHAR* Name;
        ECk_ProceduralRig_ChainSolver Solver;
        TArray<float> Lengths;
        FVector Hip;
        FVector Foot;
        FVector Pole;
        TArray<FExpectedPart> Parts;
    };
    const auto Cases = TArray<FOracleCase>{
        {TEXT("AutoOne"), ECk_ProceduralRig_ChainSolver::Auto, {100.0f},
            {0.0, -35.0, 2000.0}, {0.0, -95.0, 1965.745809}, {0.0, -75.0, 2090.0},
            {{{0.0, -78.421981, 1975.210253}, {0.611513480, -0.355036990, -0.611513480, 0.355036990}}}},
        {TEXT("AutoTwo"), ECk_ProceduralRig_ChainSolver::Auto, {60.0f, 70.0f},
            {90.0, 35.0, 2000.0}, {90.0, 95.0, 1965.745809}, {90.0, 75.0, 2090.0},
            {{{90.0, 59.420455, 2017.425309}, {0.628658023, 0.323711430, 0.628658023, 0.323711430}},
             {{90.0, 89.420455, 2000.298214}, {0.056542985, 0.704842458, 0.056542985, 0.704842458}}}},
        {TEXT("FabrikTwo"), ECk_ProceduralRig_ChainSolver::Fabrik, {60.0f, 70.0f},
            {180.0, -35.0, 2000.0}, {180.0, -95.0, 1965.745809}, {180.0, -75.0, 2090.0},
            {{{180.0, -59.404605, 2017.447500}, {0.323425679, -0.628805081, -0.323425679, 0.628805081}},
             {{180.0, -89.400702, 2000.344526}, {0.704832673, -0.056664827, -0.704832673, 0.056664827}}}},
        {TEXT("CurveEight"), ECk_ProceduralRig_ChainSolver::Curve,
            {25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f, 25.0f},
            {270.0, 35.0, 2000.0}, {270.0, 95.0, 1965.745809}, {270.0, 75.0, 2090.0},
            {{{270.0, 41.504728, 2010.674199}, {0.680796572, 0.191091675, 0.680796572, 0.191091675}},
             {{270.0, 55.337433, 2031.475132}, {0.672706986, 0.217865350, 0.672706986, 0.217865350}},
             {{270.0, 71.518344, 2050.426579}, {0.653065278, 0.271119425, 0.653065278, 0.271119425}},
             {{270.0, 92.825900, 2060.315429}, {0.520848092, 0.478243939, 0.520848092, 0.478243939}},
             {{270.0, 109.224986, 2049.518233}, {-0.698016225, -0.113019244, 0.698016225, 0.113019244}},
             {{270.0, 112.397427, 2025.180764}, {0.706769220, -0.021846515, -0.706769220, 0.021846515}},
             {{270.0, 108.481905, 2000.606346}, {0.701402613, -0.089634674, -0.701402613, 0.089634674}},
             {{270.0, 100.169217, 1977.126962}, {0.691102051, -0.149592630, -0.691102051, 0.149592630}}}}
    };

    for (const auto& Case : Cases)
    {
        auto Joints = TArray<FVector>{};
        Joints.SetNumZeroed(Case.Lengths.Num() + 1);
        if (NOT TestTrue(FString::Printf(TEXT("%s solve succeeds"), Case.Name),
            ck::SolveProceduralLimb(Case.Solver, Case.Lengths, Case.Hip, Case.Foot, Case.Pole,
                FQuat::Identity, Joints)))
        { continue; }
        for (auto Index = 0; Index < Case.Parts.Num(); ++Index)
        {
            const auto Center = (Joints[Index] + Joints[Index + 1]) * 0.5;
            const auto Rotation = ck::Get_ProceduralLimbSegmentRotation(Joints[Index], Joints[Index + 1], Case.Pole);
            TestTrue(FString::Printf(TEXT("%s part %d legacy center"), Case.Name, Index),
                Center.Equals(Case.Parts[Index].Center, 0.02));
            TestTrue(FString::Printf(TEXT("%s part %d legacy orientation"), Case.Name, Index),
                Rotation.Equals(Case.Parts[Index].Rotation, 0.001));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationLegacyOptionalBindingsTest,
    "Ck.ProceduralAnimation.Action.Articulation.LegacyOptionalBindings",
    ck_test_procedural_legacy_capture::TestFlags)

auto
    FCkProceduralArticulationLegacyOptionalBindingsTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_legacy_capture;
    struct FOptionalState
    {
        FCk_Handle_ProceduralArticulation Partial;
        FCk_Handle_ProceduralArticulation Empty;
        FCk_Handle_ProceduralBodyPose EmptyBodyPose;
        FCk_Handle_Transform EmptyPresentation;
    };
    const auto State = MakeShared<FOptionalState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            const auto First = FCase{FName{TEXT("Unbound")}, ECk_ProceduralRig_ChainSolver::Auto,
                TArray<float>{100.0f}};
            const auto Middle = FCase{FName{TEXT("Middle")}, ECk_ProceduralRig_ChainSolver::Auto,
                TArray<float>{100.0f}};
            const auto Last = FCase{FName{TEXT("Last")}, ECk_ProceduralRig_ChainSolver::Auto,
                TArray<float>{100.0f}};
            auto* Rig = NewObject<UCk_ProceduralRig_Data>();
            Rig->Set_Legs(TArray<FCk_ProceduralLeg_Spec>{MakeLeg(First, 0), MakeLeg(Middle, 1), MakeLeg(Last, 2)});
            auto PartialRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            auto PartialBody = UCk_Utils_Transform_UE::Add(PartialRoot, InitialBody, ECk_Replication::DoesNotReplicate);
            const auto MiddlePart = MakeSegments(PartialBody, 1);
            const auto LastPart = MakeSegments(PartialBody, 1);
            const auto ReversedChains = TArray<FCk_ProceduralWalker_LegChain>{
                FCk_ProceduralWalker_LegChain{Last.Id, FCk_ProceduralRig_Spec{LastPart}},
                FCk_ProceduralWalker_LegChain{Middle.Id, FCk_ProceduralRig_Spec{MiddlePart}}};
            const auto PartialWalker = UCk_Utils_ProceduralAnimation_UE::Add_Walker(
                PartialBody, Rig, NewObject<UCk_ProceduralGait_Data>(), ReversedChains);
            State->Partial = UCk_Utils_ProceduralArticulation_UE::Cast(PartialRoot);
            TestTrue(TEXT("Partial walker and same-home articulation compose"),
                ck::IsValid(PartialWalker.Get_Gait()) && ck::IsValid(State->Partial));
            TestEqual(TEXT("All simulation legs remain in gait"), PartialWalker.Get_Legs().Num(), 3);
            const auto Bound = UCk_Utils_ProceduralArticulation_UE::Get_Limbs(State->Partial);
            TestEqual(TEXT("Only visual-chain legs become controllable limbs"), Bound.Num(), 2);
            if (Bound.Num() == 2)
            {
                TestTrue(TEXT("First binding follows topology order, not chain input order"),
                    Bound[0].ConvertToHandle() == PartialWalker.Get_Legs()[1].ConvertToHandle());
                TestTrue(TEXT("Second binding follows topology order, not chain input order"),
                    Bound[1].ConvertToHandle() == PartialWalker.Get_Legs()[2].ConvertToHandle());
            }
            TestFalse(TEXT("Unbound gait leg has no controllable articulation limb"),
                ck::IsValid(UCk_Utils_ProceduralArticulation_UE::Get_Limb(State->Partial, First.Id)));
            TestEqual(TEXT("Claiming an unbound simulation leg rejects explicitly"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Partial,
                    FCk_ArticulationControlToken{700, 1}, FCk_ArticulationChannelSet{TArray<FName>{First.Id}, false}),
                ECk_ArticulationControlResult::InvalidChannels);

            auto EmptyRoot = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            auto EmptyBody = UCk_Utils_Transform_UE::Add(EmptyRoot,
                FTransform{FVector{1000.0, 0.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
            const auto EmptyWalker = UCk_Utils_ProceduralAnimation_UE::Add_Walker(
                EmptyBody, Rig, NewObject<UCk_ProceduralGait_Data>(), {});
            State->Empty = UCk_Utils_ProceduralArticulation_UE::Cast(EmptyRoot);
            TestTrue(TEXT("Zero visual-chain walker retains gait and articulation"),
                ck::IsValid(EmptyWalker.Get_Gait()) && ck::IsValid(State->Empty));
            TestEqual(TEXT("Zero visual-chain walker retains all simulation legs"), EmptyWalker.Get_Legs().Num(), 3);
            TestTrue(TEXT("Zero visual-chain articulation has no invented limbs"),
                UCk_Utils_ProceduralArticulation_UE::Get_Limbs(State->Empty).IsEmpty());
            auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(EmptyBody);
            State->EmptyPresentation = UCk_Utils_Transform_UE::Add(PresentationEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            auto EmptyGait = EmptyWalker.Get_Gait();
            State->EmptyBodyPose = UCk_Utils_ProceduralBodyPose_UE::Add(EmptyGait,
                FCk_ProceduralBodyPose_Spec{State->EmptyPresentation});
            TestTrue(TEXT("Body presentation composes without visual chains"), ck::IsValid(State->EmptyBodyPose));
            auto BodyPose = State->EmptyBodyPose;
            UCk_Utils_ProceduralBodyPose_UE::Request_SetActionOffset(BodyPose,
                FCk_Request_ProceduralBodyPose_SetActionOffset{FTransform{FVector{7.0, 0.0, 0.0}}, 42}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Partial) && ck::IsValid(State->Empty)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Partial) > 0
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Empty) > 0
                && UCk_Utils_ProceduralBodyPose_UE::Get_ActionSample(State->EmptyBodyPose).Get_SampleId() == 42;
        }), WaitSeconds, TEXT("Partial rig and body-only presentation settle")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto BoundToken = FCk_ArticulationControlToken{701, 1};
            const auto BoundChannels = FCk_ArticulationChannelSet{TArray<FName>{TEXT("Middle"), TEXT("Last")}, false};
            TestEqual(TEXT("Bound limbs admit a claim"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Partial, BoundToken, BoundChannels),
                ECk_ArticulationControlResult::Accepted);
            auto BoundActual = FCk_ProceduralArticulation_Pose{};
            TestEqual(TEXT("Bound limb entry is the actual settled pose"),
                FCk_ProceduralArticulation_Control::CaptureAcceptedEntry(State->Partial, BoundToken,
                    UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Partial), BoundActual),
                ECk_ArticulationControlResult::Accepted);
            TestEqual(TEXT("Observed pose contains only bound limbs"), BoundActual.Get_Limbs().Num(), 2);
            const auto BodyToken = FCk_ArticulationControlToken{702, 1};
            TestEqual(TEXT("Body-only articulation admits a body claim"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Empty, BodyToken,
                    FCk_ArticulationChannelSet{TArray<FName>{}, true}),
                ECk_ArticulationControlResult::Accepted);
            auto BodyActual = FCk_ProceduralArticulation_Pose{};
            TestEqual(TEXT("Body-only entry is the actual settled pose"),
                FCk_ProceduralArticulation_Control::CaptureAcceptedEntry(State->Empty, BodyToken,
                    UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Empty), BodyActual),
                ECk_ArticulationControlResult::Accepted);
            TestTrue(TEXT("Body-only observation includes actual presentation and no limbs"),
                BodyActual.Get_HasBodyPresentation() && BodyActual.Get_Limbs().IsEmpty()
                && BodyActual.Get_BodyPresentation().Equals(
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->EmptyPresentation), 0.01));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

// --------------------------------------------------------------------------------------------------------------------
