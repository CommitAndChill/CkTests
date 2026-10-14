#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Processor.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace ck
{
    struct FFragment_TestArticulationForeignWrite
    {
        CK_GENERATED_BODY(FFragment_TestArticulationForeignWrite);

        FCk_Handle_ProceduralArticulation Subject;
        FTransform ForeignPose = FTransform::Identity;
        uint64 PreArmIssued = 0;
        bool Armed = false;
        bool Issued = false;
    };

    class FProcessor_TestArticulationForeignWrite : public ck_exp::TProcessor<
        FProcessor_TestArticulationForeignWrite,
        FCk_Handle_Transform,
        TReadWrite<FFragment_TestArticulationForeignWrite>>
    {
    public:
        using Group = FGroup_Transform_Derived;
        using RunAfter = TDepList<FProcessor_ProceduralArticulation_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType InPart,
            FFragment_TestArticulationForeignWrite& InProbe)
            -> void
        {
            if (NOT InProbe.Armed || InProbe.Issued || NOT ck::IsValid(InProbe.Subject))
            { return; }
            const auto Issued = InProbe.Subject.Get<FFragment_ProceduralArticulation_Pose>().Get_IssuedRevision();
            if (Issued <= InProbe.PreArmIssued)
            { return; }

            auto Part = InPart;
            UCk_Utils_Transform_UE::Request_SetTransform(Part,
                FCk_Request_Transform_SetTransform{InProbe.ForeignPose}, {});
            InProbe.Issued = true;
        }
    };

    struct FFragment_TestArticulationDefaultTimeout
    {
        CK_GENERATED_BODY(FFragment_TestArticulationDefaultTimeout);

        uint64 PreArmIssued = 0;
        FCk_ArticulationControlToken Token;
        bool Armed = false;
        bool SawPendingDefaultPublication = false;
        bool Faulted = false;
        ECk_ArticulationControlResult ReleaseResult = ECk_ArticulationControlResult::Busy;
    };

    class FProcessor_TestArticulationDefaultTimeout : public ck_exp::TProcessor<
        FProcessor_TestArticulationDefaultTimeout,
        FCk_Handle_ProceduralArticulation,
        TReadWrite<FFragment_TestArticulationDefaultTimeout>>
    {
    public:
        using Group = FGroup_Transform_Derived;
        using RunAfter = TDepList<FProcessor_ProceduralArticulation_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType InSubject,
            FFragment_TestArticulationDefaultTimeout& InProbe)
            -> void
        {
            if (NOT InProbe.Armed || InProbe.SawPendingDefaultPublication)
            { return; }
            const auto& Pose = InSubject.Get<FFragment_ProceduralArticulation_Pose>();
            if (Pose.Get_IssuedRevision() <= InProbe.PreArmIssued)
            { return; }
            InProbe.SawPendingDefaultPublication = Pose.Get_PendingObservation()
                && Pose.Get_IssuedRevision() > Pose.Get_SettledRevision();
            if (NOT InProbe.SawPendingDefaultPublication)
            { return; }
            InProbe.Faulted = FCk_ProceduralArticulation_Control::FailPendingPublication(
                InSubject, InProbe.Token);
            InProbe.ReleaseResult = FCk_ProceduralArticulation_Control::ReleaseClaims(InSubject,
                InProbe.Token, FCk_ArticulationChannelSet{TArray<FName>{TEXT("InspectionArm")}, false});
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestArticulationForeignWrite);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestArticulationDefaultTimeout);

namespace ck_test_procedural_articulation
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto RootPose = FTransform{FQuat::Identity, FVector{100.0, 200.0, 2000.0}};

    struct FState
    {
        FCk_Handle_Transform ArmRoot;
        FCk_Handle_Transform Segment;
        FCk_Handle_ProceduralArticulation Arm;
        FCk_Handle_Transform EmptyRoot;
        FCk_Handle_Transform Presentation;
        FCk_Handle_ProceduralArticulation Empty;
        int64 SettledBeforePartLoss = 0;
    };

    auto
        MakeArmSpec(
        const FCk_Handle_Transform& InSegment)
        -> FCk_ProceduralArticulation_Spec
    {
        auto Limb = FCk_ProceduralLimb_Layout{
            FName{TEXT("InspectionArm")}, FTransform::Identity, TArray<float>{100.0f},
            FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
        auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}};
        auto Binding = FCk_ProceduralLimb_Binding{Limb.Get_Id(), TArray<FCk_Handle_Transform>{InSegment}};
        return FCk_ProceduralArticulation_Spec{Layout, TArray<FCk_ProceduralLimb_Binding>{Binding}};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationStandaloneTest,
    "Ck.ProceduralAnimation.Action.Articulation.NoGaitAndBodyOnly",
    ck_test_procedural_articulation::TestFlags)

auto
    FCkProceduralArticulationStandaloneTest::
    RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation;
    const auto State = MakeShared<FState>();
    AddExpectedError(TEXT("Procedural articulation Add rejected root"), EAutomationExpectedErrorFlags::Contains, 0);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto ArmEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->ArmRoot = UCk_Utils_Transform_UE::Add(ArmEntity, RootPose, ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->ArmRoot);
            State->Segment = UCk_Utils_Transform_UE::Add(SegmentEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            auto Invalid = MakeArmSpec(State->Segment);
            Invalid.Set_Presentation(State->Segment);
            const auto Rejected = UCk_Utils_ProceduralArticulation_UE::Add(State->ArmRoot, Invalid);
            TestFalse(TEXT("Duplicate presentation/segment rejects entire composition"), ck::IsValid(Rejected));
            TestFalse(TEXT("Rejected composition leaves no runnable articulation"),
                UCk_Utils_ProceduralArticulation_UE::Has(State->ArmRoot));

            auto BadSolverLimb = FCk_ProceduralLimb_Layout{
                FName{TEXT("BadSolver")}, FTransform::Identity, TArray<float>{100.0f},
                FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
            BadSolverLimb.Set_Solver(static_cast<ECk_ProceduralRig_ChainSolver>(255));
            auto BadSolver = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{BadSolverLimb}},
                TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                    BadSolverLimb.Get_Id(), TArray<FCk_Handle_Transform>{State->Segment}}}};
            TestFalse(TEXT("Unknown solver rejects before limb creation"),
                ck::IsValid(UCk_Utils_ProceduralArticulation_UE::Add(State->ArmRoot, BadSolver)));

            auto BadEnvelopeLayout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
            BadEnvelopeLayout.Set_BodyEnvelopeMin(FVector{1.0, 0.0, 0.0});
            auto BadEnvelope = FCk_ProceduralArticulation_Spec{BadEnvelopeLayout,
                TArray<FCk_ProceduralLimb_Binding>{}};
            TestFalse(TEXT("Inverted body envelope rejects before limb creation"),
                ck::IsValid(UCk_Utils_ProceduralArticulation_UE::Add(State->ArmRoot, BadEnvelope)));
            TestFalse(TEXT("Malformed layouts leave root uncomposed"),
                UCk_Utils_ProceduralArticulation_UE::Has(State->ArmRoot));

            auto SwivelLimb = FCk_ProceduralLimb_Layout{
                FName{TEXT("NeedsQuery")}, FTransform::Identity, TArray<float>{100.0f},
                FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
            SwivelLimb.Set_Clearance(ECk_ProceduralRig_Clearance::Swivel);
            auto NoFilter = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{SwivelLimb}},
                TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                    SwivelLimb.Get_Id(), TArray<FCk_Handle_Transform>{State->Segment}}}};
            TestFalse(TEXT("Dedicated Swivel without a Jolt query group rejects atomically"),
                ck::IsValid(UCk_Utils_ProceduralArticulation_UE::Add(State->ArmRoot, NoFilter)));

            State->Arm = UCk_Utils_ProceduralArticulation_UE::Add(State->ArmRoot, MakeArmSpec(State->Segment));
            TestTrue(TEXT("Dedicated articulation composes without gait"), ck::IsValid(State->Arm)
                && NOT UCk_Utils_ProceduralGait_UE::Has(State->ArmRoot));
            TestTrue(TEXT("Named limb is a direct root-owned child"),
                ck::IsValid(UCk_Utils_ProceduralArticulation_UE::Get_Limb(State->Arm, FName{TEXT("InspectionArm")})));

            auto EmptyEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->EmptyRoot = UCk_Utils_Transform_UE::Add(EmptyEntity, RootPose, ECk_Replication::DoesNotReplicate);
            auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->EmptyRoot);
            State->Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            auto EmptySpec = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}},
                TArray<FCk_ProceduralLimb_Binding>{}};
            EmptySpec.Set_Presentation(State->Presentation);
            State->Empty = UCk_Utils_ProceduralArticulation_UE::Add(State->EmptyRoot, EmptySpec);
            TestTrue(TEXT("Zero-limb subject composes without gait"), ck::IsValid(State->Empty)
                && UCk_Utils_ProceduralArticulation_UE::Get_Limbs(State->Empty).IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Arm) && ck::IsValid(State->Empty)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Arm) > 0
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Empty) > 0;
        }), WaitSeconds, TEXT("Articulation poses settle after transform requests")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Segment = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Segment);
            const auto Presentation = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Presentation);
            TestTrue(TEXT("One-link segment settles at the authored rest midpoint"),
                Segment.GetLocation().Equals(RootPose.GetLocation() + FVector{50.0, 0.0, 0.0}, 0.01));
            TestTrue(TEXT("Zero-limb presentation settles to its root"), Presentation.Equals(RootPose, 0.01));
            TestEqual(TEXT("Arm publisher remains operational"),
                UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Arm), ECk_ProceduralArticulation_Health::Operational);
            TestEqual(TEXT("Body-only publisher remains operational"),
                UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Empty), ECk_ProceduralArticulation_Health::Operational);
            State->SettledBeforePartLoss = UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Arm);
            auto Part = State->Segment.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Part);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Arm)
                == ECk_ProceduralArticulation_Health::Faulted;
        }), WaitSeconds, TEXT("A lost bound part faults publication")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("Lost part is diagnosed"), UCk_Utils_ProceduralArticulation_UE::Get_Fault(State->Arm)
                != ECk_ProceduralArticulation_Fault::None);
            TestEqual(TEXT("Fault does not certify a new settled pose"),
                UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Arm), State->SettledBeforePartLoss);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationForeignWriterTest,
    "Ck.ProceduralAnimation.Action.Articulation.ForeignWriteFaultsSettlement",
    ck_test_procedural_articulation::TestFlags)

auto
    FCkProceduralArticulationForeignWriterTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation;
    struct FForeignState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Segment;
        FCk_Handle_ProceduralArticulation Subject;
        uint64 PreArmSettled = 0;
        uint64 PreArmIssued = 0;
        const FTransform ForeignPose = FTransform{FVector{450.0, -80.0, 1900.0}};
    };
    const auto State = MakeShared<FForeignState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose, ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Segment = UCk_Utils_Transform_UE::Add(SegmentEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, MakeArmSpec(State->Segment));
            TestTrue(TEXT("Foreign-write subject composes"), ck::IsValid(State->Subject));
            State->Segment.Add<ck::FFragment_TestArticulationForeignWrite>();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Foreign-write subject first settles coherently")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Pose = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
            State->PreArmIssued = Pose.Get_IssuedRevision();
            State->PreArmSettled = Pose.Get_SettledRevision();
            TestEqual(TEXT("Arm from a coherent settled baseline"), State->PreArmIssued, State->PreArmSettled);
            auto& Probe = State->Segment.Get<ck::FFragment_TestArticulationForeignWrite>();
            Probe.Subject = State->Subject;
            Probe.ForeignPose = State->ForeignPose;
            Probe.PreArmIssued = State->PreArmIssued;
            Probe.Armed = true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->Segment.Get<ck::FFragment_TestArticulationForeignWrite>().Issued
                && UCk_Utils_ProceduralArticulation_UE::Get_Health(State->Subject)
                    == ECk_ProceduralArticulation_Health::Faulted;
        }), WaitSeconds, TEXT("Later derived writer overrides part before actual-settle observer")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Pose = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
            TestTrue(TEXT("Publisher issued a new revision after arming"), Pose.Get_IssuedRevision() > State->PreArmIssued);
            TestEqual(TEXT("Foreign settled transform never certifies that revision"),
                Pose.Get_SettledRevision(), State->PreArmSettled);
            TestTrue(TEXT("Actual settled transform is the foreign writer's pose"),
                UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Segment).Equals(State->ForeignPose, 0.01));
            TestEqual(TEXT("Observer reports settlement mismatch"),
                UCk_Utils_ProceduralArticulation_UE::Get_Fault(State->Subject),
                ECk_ProceduralArticulation_Fault::SettlementMismatch);
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationDefaultTimeoutTest,
    "Ck.ProceduralAnimation.Action.Articulation.DefaultPublicationTimeoutReleasesClaim",
    ck_test_procedural_articulation::TestFlags)

auto
    FCkProceduralArticulationDefaultTimeoutTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation;
    struct FTimeoutState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Segment;
        FCk_Handle_ProceduralArticulation Subject;
        uint64 SettledBeforeTimeout = 0;
    };
    const auto State = MakeShared<FTimeoutState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose, ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Segment = UCk_Utils_Transform_UE::Add(SegmentEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, MakeArmSpec(State->Segment));
            TestTrue(TEXT("Default-timeout subject composes"), ck::IsValid(State->Subject));
            State->Subject.Add<ck::FFragment_TestArticulationDefaultTimeout>();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (NOT ck::IsValid(State->Subject))
            { return false; }
            const auto& Pose = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
            return Pose.Get_SettledRevision() > 0 && Pose.Get_IssuedRevision() == Pose.Get_SettledRevision()
                && NOT Pose.Get_PendingObservation();
        }), WaitSeconds, TEXT("Default publisher settles before ownership")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto& Probe = State->Subject.Get<ck::FFragment_TestArticulationDefaultTimeout>();
            Probe.Token = FCk_ArticulationControlToken{91, 1};
            State->SettledBeforeTimeout = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>()
                .Get_SettledRevision();
            TestEqual(TEXT("Claim begins at a coherent settled default pose"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Subject, Probe.Token,
                    FCk_ArticulationChannelSet{TArray<FName>{FName{TEXT("InspectionArm")}}, false}),
                ECk_ArticulationControlResult::Accepted);
            Probe.PreArmIssued = State->SettledBeforeTimeout;
            Probe.Armed = true;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->Subject.Get<ck::FFragment_TestArticulationDefaultTimeout>()
                .SawPendingDefaultPublication;
        }), WaitSeconds, TEXT("Scheduler observes real default issue before transform settlement")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Probe = State->Subject.Get<ck::FFragment_TestArticulationDefaultTimeout>();
            TestTrue(TEXT("Timeout faults the actual pending default publication"), Probe.Faulted);
            TestEqual(TEXT("Timeout releases the finite owner claim"), Probe.ReleaseResult,
                ECk_ArticulationControlResult::Accepted);
            TestTrue(TEXT("Released limb has no claimant"),
                FCk_ProceduralArticulation_Control::GetClaimant(State->Subject,
                    FName{TEXT("InspectionArm")}, false) == FCk_ArticulationControlToken{});
            TestEqual(TEXT("Timeout leaves last settled revision unchanged"),
                State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>().Get_SettledRevision(),
                State->SettledBeforeTimeout);
            TestEqual(TEXT("Timeout has a distinct diagnostic"),
                UCk_Utils_ProceduralArticulation_UE::Get_Fault(State->Subject),
                ECk_ProceduralArticulation_Fault::SettlementTimeout);
            const auto& Health = State->Subject.Get<ck::FFragment_ProceduralArticulation_Health>();
            TestTrue(TEXT("Fault records live actual part transforms"),
                Health.Get_FaultObservedHandles().Contains(State->Segment)
                && Health.Get_FaultObservedPoses().Num() == Health.Get_FaultObservedHandles().Num());
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationClaimedHoldTest,
    "Ck.ProceduralAnimation.Action.Articulation.ClaimedPoseHoldsWithoutCandidate",
    ck_test_procedural_articulation::TestFlags)

auto
    FCkProceduralArticulationClaimedHoldTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation;
    struct FHoldState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Segment;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_ArticulationControlToken Token{92, 1};
        FCk_ArticulationStageKey Key;
        FTransform AcceptedSegment = FTransform::Identity;
        FTransform HeldSegment = FTransform::Identity;
        FCk_ProceduralArticulation_Pose HeldPose;
        uint64 AcceptedRevision = 0;
        bool HeldPoseCaptured = false;
    };
    const auto State = MakeShared<FHoldState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Segment = UCk_Utils_Transform_UE::Add(SegmentEntity, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(
                State->Root, MakeArmSpec(State->Segment));
            TestTrue(TEXT("Claimed-hold subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Subject)
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0; }),
        WaitSeconds, TEXT("Claimed-hold default pose settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Channels = FCk_ArticulationChannelSet{
                TArray<FName>{FName{TEXT("InspectionArm")}}, false};
            TestEqual(TEXT("Claimed-hold limb reserves"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(State->Subject,
                    State->Token, Channels), ECk_ArticulationControlResult::Accepted);
            const auto Target = FCk_ArticulationLimbTarget{FName{TEXT("InspectionArm")},
                FTransform{FQuat::Identity, RootPose.TransformPosition(FVector{60.0, 80.0, 0.0})},
                TOptional<FVector>{}, ECk_ArticulationReachPolicy::RejectUnreachable};
            const auto Candidates = TArray<FCk_ArticulationTokenCandidate>{
                FCk_ArticulationTokenCandidate{State->Token, 1,
                    TArray<FCk_ArticulationLimbTarget>{Target}, TOptional<FTransform>{}}};
            auto Evaluation = FCk_ArticulationBatchEvaluation{};
            TestEqual(TEXT("Claimed-hold target evaluates"),
                FCk_ProceduralArticulation_Control::TryEvaluateCandidateBatch(State->Subject,
                    Candidates, Evaluation), ECk_ArticulationControlResult::Accepted);
            auto Keys = TArray<FCk_ArticulationStageKey>{};
            TestEqual(TEXT("Claimed-hold target stages"),
                FCk_ProceduralArticulation_Control::TryStageCandidateBatch(State->Subject,
                    MoveTemp(Evaluation), Keys), ECk_ArticulationControlResult::Accepted);
            if (Keys.Num() == 1)
            { State->Key = Keys[0]; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (State->Key.Get_BatchNonce() == 0)
            { return false; }
            auto Actual = FCk_ProceduralArticulation_Pose{};
            if (FCk_ProceduralArticulation_Control::ObserveSettlement(State->Subject,
                    State->Key, State->AcceptedRevision, Actual) != ECk_ArticulationSettlement::Settled
                || Actual.Get_Limbs().Num() != 1 || Actual.Get_Limbs()[0].Get_Segments().Num() != 1)
            { return false; }
            State->AcceptedSegment = Actual.Get_Limbs()[0].Get_Segments()[0];
            return true;
        }), WaitSeconds, TEXT("Claimed-hold authored target actually settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            UCk_Utils_Transform_UE::Request_SetLocation(State->Root,
                FCk_Request_Transform_SetLocation{RootPose.GetLocation() + FVector{20.0, 0.0, 0.0}}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto& Pose = State->Subject.Get<ck::FFragment_ProceduralArticulation_Pose>();
            if (UCk_Utils_Transform_UE::Get_EntityCurrentLocation(State->Root).X
                    <= RootPose.GetLocation().X + 19.0
                || Pose.Get_SettledRevision() < State->AcceptedRevision + 3
                || Pose.Get_IssuedRevision() != Pose.Get_SettledRevision()
                || Pose.Get_PendingObservation())
            { return false; }
            State->HeldSegment = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Segment);
            State->HeldPoseCaptured = FCk_ProceduralArticulation_Control::CaptureAcceptedEntry(
                State->Subject, State->Token, Pose.Get_SettledRevision(), State->HeldPose)
                == ECk_ArticulationControlResult::Accepted;
            return State->HeldPoseCaptured;
        }),
        WaitSeconds, TEXT("Claimed-hold no-candidate default publisher settles after moving root")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("Claimed segment retains accepted world pose across default publications"),
                State->HeldSegment.GetLocation().Equals(State->AcceptedSegment.GetLocation(), 0.02)
                && State->HeldSegment.GetRotation().Equals(State->AcceptedSegment.GetRotation(), 1.0e-3));
            TestTrue(TEXT("Claimed settled pose remains capturable after default publications"),
                State->HeldPoseCaptured);
            TestTrue(TEXT("Claimed observation uses held segment"),
                State->HeldPose.Get_Limbs().Num() == 1
                && State->HeldPose.Get_Limbs()[0].Get_Segments().Num() == 1
                && State->HeldPose.Get_Limbs()[0].Get_Segments()[0].GetLocation().Equals(
                    State->HeldSegment.GetLocation(), 0.02));
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralArticulationCombinedClaimsTest,
    "Ck.ProceduralAnimation.Action.Articulation.CombinedBodyAndLimbClaims",
    ck_test_procedural_articulation::TestFlags)

auto
    FCkProceduralArticulationCombinedClaimsTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_articulation;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* InWorld)
        {
            auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            auto Root = UCk_Utils_Transform_UE::Add(Entity, RootPose, ECk_Replication::DoesNotReplicate);
            auto Parts = TArray<FCk_Handle_Transform>{};
            for (auto Index = 0; Index < 3; ++Index)
            {
                auto Child = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
                Parts.Add(UCk_Utils_Transform_UE::Add(Child, RootPose, ECk_Replication::DoesNotReplicate));
            }
            const auto Left = FName{TEXT("Left")};
            const auto Right = FName{TEXT("Right")};
            auto Layouts = TArray<FCk_ProceduralLimb_Layout>{
                FCk_ProceduralLimb_Layout{Left, FTransform::Identity, TArray<float>{100.0f},
                    FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector},
                FCk_ProceduralLimb_Layout{Right, FTransform::Identity, TArray<float>{100.0f},
                    FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector}};
            auto Bindings = TArray<FCk_ProceduralLimb_Binding>{
                FCk_ProceduralLimb_Binding{Left, TArray<FCk_Handle_Transform>{Parts[0]}},
                FCk_ProceduralLimb_Binding{Right, TArray<FCk_Handle_Transform>{Parts[1]}}};
            auto Spec = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{MoveTemp(Layouts)}, MoveTemp(Bindings)};
            Spec.Set_Presentation(Parts[2]);
            const auto Subject = UCk_Utils_ProceduralArticulation_UE::Add(Root, Spec);
            if (NOT TestTrue(TEXT("Combined-claim subject composes"), ck::IsValid(Subject)))
            { return; }
            const auto First = FCk_ArticulationControlToken{11, 1};
            const auto Second = FCk_ArticulationControlToken{12, 1};
            const auto Channels = FCk_ArticulationChannelSet{TArray<FName>{Left, Right}, true};
            TestEqual(TEXT("One token atomically claims body and two limbs"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(Subject, First, Channels),
                ECk_ArticulationControlResult::Accepted);
            TestEqual(TEXT("Another token cannot claim even a disjoint limb while body is held"),
                FCk_ProceduralArticulation_Control::TryReserveClaims(Subject, Second,
                    FCk_ArticulationChannelSet{TArray<FName>{Right}, false}),
                ECk_ArticulationControlResult::ClaimConflict);
            TestTrue(TEXT("Rejected claim leaves both limb owners unchanged"),
                FCk_ProceduralArticulation_Control::GetClaimant(Subject, Left, false) == First
                && FCk_ProceduralArticulation_Control::GetClaimant(Subject, Right, false) == First);
            TestEqual(TEXT("Whole body-and-limb claim releases atomically"),
                FCk_ProceduralArticulation_Control::ReleaseClaims(Subject, First, Channels),
                ECk_ArticulationControlResult::Accepted);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Entity);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
