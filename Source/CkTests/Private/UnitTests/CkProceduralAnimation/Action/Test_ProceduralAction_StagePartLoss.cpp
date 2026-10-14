#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Processor.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkTest_ProceduralActionClipFixture.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace ck
{
    struct FFragment_TestActionStagePartLoss
    {
        CK_GENERATED_BODY(FFragment_TestActionStagePartLoss);

        FCk_Handle_Transform Part;
        uint64 PreArmIssued = 0;
        uint64 StagedRevision = 0;
        bool Armed = false;
        bool SawPendingStage = false;
        bool DestroyRequested = false;
    };

    class FProcessor_TestActionStagePartLoss : public ck_exp::TProcessor<
        FProcessor_TestActionStagePartLoss,
        FCk_Handle_ProceduralArticulation,
        TReadWrite<FFragment_TestActionStagePartLoss>>
    {
    public:
        using Group = FGroup_Transform_Derived;
        using RunAfter = TDepList<FProcessor_ProceduralArticulation_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType InSubject,
            FFragment_TestActionStagePartLoss& InProbe)
            -> void
        {
            if (NOT InProbe.Armed || InProbe.DestroyRequested)
            { return; }
            const auto& Pose = InSubject.Get<FFragment_ProceduralArticulation_Pose>();
            if (Pose.Get_IssuedRevision() <= InProbe.PreArmIssued
                || NOT Pose.Get_PendingObservation()
                || Pose.Get_IssuedRevision() <= Pose.Get_SettledRevision())
            { return; }

            InProbe.SawPendingStage = true;
            InProbe.StagedRevision = Pose.Get_IssuedRevision();
            auto Part = InProbe.Part.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Part);
            InProbe.DestroyRequested = true;
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestActionStagePartLoss);

namespace ck_test_procedural_action_stage_part_loss
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto LimbId = FName{TEXT("StageLossArm")};
    const auto RootPose = FTransform{FQuat::Identity, FVector{100.0, 200.0, 2000.0}};

    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Segment;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        uint64 PreArmSettled = 0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionStagePartLossTest,
    "Ck.ProceduralAnimation.Action.Publication.PartLossAfterStage",
    ck_test_procedural_action_stage_part_loss::TestFlags)

auto
    FCkProceduralActionStagePartLossTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_stage_part_loss;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Segment = UCk_Utils_Transform_UE::Add(SegmentEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Limb = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            const auto Spec = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
                TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                    LimbId, TArray<FCk_Handle_Transform>{State->Segment}}}};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            TestTrue(TEXT("Part-loss subject composes"), ck::IsValid(State->Subject));
            if (ck::IsValid(State->Subject))
            { State->Subject.Add<ck::FFragment_TestActionStagePartLoss>(); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Subject first settles before action admission")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Part-loss player attaches"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Clip = ck_test_procedural_action_clip_fixture::MakeNodClip(TArray<FName>{LimbId}, FCk_Time{0.4});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                ck_test_procedural_action_clip_fixture::MakePlay(State->Clip.Get(), TArray<FName>{LimbId}));
            TestTrue(TEXT("Part-loss action reserves identity"), ck::IsValid(State->Action));
            if (ck::IsValid(State->Action))
            { State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action); }

            const auto& Pose = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
            State->PreArmSettled = Pose.Get_SettledRevision();
            TestEqual(TEXT("Part-loss probe arms from coherent publication"),
                Pose.Get_IssuedRevision(), State->PreArmSettled);
            TestFalse(TEXT("No publication is pending when probe arms"), Pose.Get_PendingObservation());
            auto& Probe = State->Subject.Get<ck::FFragment_TestActionStagePartLoss>();
            Probe.Part = State->Segment;
            Probe.PreArmIssued = Pose.Get_IssuedRevision();
            Probe.Armed = true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (NOT ck::IsValid(State->Subject))
            { return false; }
            const auto& Probe = State->Subject.Get<ck::FFragment_TestActionStagePartLoss>();
            return Probe.DestroyRequested
                && UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Subject)
                    == ECk_ProceduralArticulation_Health::Faulted;
        }), WaitSeconds, TEXT("Staged action loses its part before settlement observation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Probe = State->Subject.Get<ck::FFragment_TestActionStagePartLoss>();
            const auto& Pose = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
            TestTrue(TEXT("Action stage was issued and pending when the part was destroyed"),
                Probe.SawPendingStage && Probe.StagedRevision > Probe.PreArmIssued);
            TestEqual(TEXT("Destroyed part cannot certify staged revision"),
                Pose.Get_SettledRevision(), State->PreArmSettled);
            TestEqual(TEXT("Observer diagnoses the lost staged part"),
                UCk_Utils_ProceduralArticulation_UE::Get_Fault(State->Subject),
                ECk_ProceduralArticulation_Fault::SettlementMismatch);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Player)
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Failed stage retains a terminal action stream")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 8);
            TestTrue(TEXT("Terminal remains readable by the reserved action identity"),
                Page.Get_Terminal() && NOT Page.Get_HasMore());
            auto TerminalCount = int32{0};
            auto AcceptedCount = int32{0};
            for (const auto& Record : Page.Get_Records())
            {
                if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal)
                {
                    ++TerminalCount;
                    TestEqual(TEXT("Staged part loss remains a publication failure"),
                        Record.Get_Result().Get_Outcome(),
                        ECk_ProceduralAction_Outcome::PublicationFailed);
                }
                if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose)
                { ++AcceptedCount; }
            }
            TestEqual(TEXT("Exactly one terminal record is retained"), TerminalCount, 1);
            TestEqual(TEXT("No failed staged sample is published as accepted"), AcceptedCount, 0);
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
