#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTest_ProceduralActionReentry.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_procedural_action_manual
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto LimbId = FName{TEXT("ManualArm")};
    const auto OtherLimbId = FName{TEXT("OtherArm")};
    const auto RootPose = FTransform{FQuat::Identity, FVector{100.0, 200.0, 2000.0}};

    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Segment;
        FCk_Handle_Transform OtherSegment;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle OtherScope;
        FCk_Handle_ProceduralAction Action;
        FCk_Handle_ProceduralAction OtherAction;
        FCk_ProceduralAction_Identity Identity;
        FCk_ProceduralAction_Identity OtherIdentity;
        FCk_ProceduralAction_DebugInstance CapturedAction;
        FCk_ProceduralAction_DebugSubject CapturedSubject;
        TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE> Listener;
    };

    struct FDestroyOnCompletionState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE> Listener;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManualBendSelectionTest,
    "Ck.ProceduralAnimation.Action.Manual.BendSelection",
    ck_test_procedural_action_manual::TestFlags)

auto
    FCkProceduralActionManualBendSelectionTest::
    RunTest(
        const FString&)
    -> bool
{
    auto Target = FCk_ProceduralAction_LimbTarget{
        ck_test_procedural_action_manual::LimbId, FTransform::Identity};
    TestFalse(TEXT("A new manual target leaves the authored bend in control"), Target.Get_HasBend());
    TestTrue(TEXT("An inactive bend retains its authoring value"),
        Target.Get_BendDirection().Equals(FVector::UpVector));

    Target.Set_BendDirection(FVector::DownVector);
    TestTrue(TEXT("The explicit setter activates the bend override"), Target.Get_HasBend());
    TestTrue(TEXT("The explicit direction is stored without silent normalization"),
        Target.Get_BendDirection().Equals(FVector::DownVector));

    Target.Clear_BendDirection();
    TestFalse(TEXT("Clearing restores authored bend selection"), Target.Get_HasBend());
    TestTrue(TEXT("Clearing retains the inactive authoring value"),
        Target.Get_BendDirection().Equals(FVector::DownVector));

    Target.Set_BendDirection(FVector::ZeroVector);
    TestTrue(TEXT("Invalid explicit values remain marked for service rejection"),
        Target.Get_HasBend() && Target.Get_BendDirection().IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManualAcceptanceTest,
    "Ck.ProceduralAnimation.Action.Manual.AcceptanceAndOwnerLoss",
    ck_test_procedural_action_manual::TestFlags)

auto
    FCkProceduralActionManualAcceptanceTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_manual;
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
            auto OtherSegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->OtherSegment = UCk_Utils_Transform_UE::Add(OtherSegmentEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Limb = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            const auto OtherLimb = FCk_ProceduralLimb_Layout{OtherLimbId,
                FTransform{FQuat::Identity, FVector{0.0, 100.0, 0.0}},
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            const auto Spec = FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb, OtherLimb}},
                TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                    LimbId, TArray<FCk_Handle_Transform>{State->Segment}},
                    FCk_ProceduralLimb_Binding{OtherLimbId,
                        TArray<FCk_Handle_Transform>{State->OtherSegment}}}};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            TestTrue(TEXT("Subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Subject has actual settled entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Player attaches to subject"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}};
            Control.Set_InputTimeout(FCk_Time{30.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(
                State->Scope, State->Player, Control);
            TestTrue(TEXT("Manual action reserves a scoped identity"), ck::IsValid(State->Action));
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            State->Listener = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
                NewObject<UCk_Test_ProceduralActionReentry_UE>()};
            State->Listener->Scope = State->Scope;
            State->Listener->Player = State->Player;
            State->OtherScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto OtherControl = FCk_ProceduralAction_ControlSpec{TArray<FName>{OtherLimbId}};
            OtherControl.Set_InputTimeout(FCk_Time{30.0});
            State->OtherAction = UCk_Utils_ProceduralAction_UE::Create_Manual(
                State->OtherScope, State->Player, OtherControl);
            TestTrue(TEXT("Disjoint action reserves another identity"), ck::IsValid(State->OtherAction));
            State->OtherIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->OtherAction);
            auto StateDelegate = FCk_Delegate_ProceduralAction_OnStateChanged{};
            StateDelegate.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnStateChanged);
            UCk_Utils_ProceduralAction_UE::BindTo_OnStateChanged(State->OtherAction, StateDelegate);
            auto FinishedDelegate = FCk_Delegate_ProceduralAction_OnFinished{};
            FinishedDelegate.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnFinished);
            UCk_Utils_ProceduralAction_UE::BindTo_OnFinished(State->OtherAction, FinishedDelegate);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                    == ECk_ProceduralAction_State::Ready
                && ck::IsValid(State->OtherAction)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->OtherAction)
                    == ECk_ProceduralAction_State::Ready;
        }), WaitSeconds, TEXT("Disjoint manual claims capture settled entries")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto Tuning = FCk_ProceduralAction_Tuning{};
            auto Sampling = Tuning.Get_Sampling();
            Sampling.Set_MaxEndpointTravelCm(0.1f);
            Tuning.Set_Sampling(Sampling);
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnTuningCompleted);
            UCk_Utils_ProceduralActionPlayer_UE::Request_UpdateTuning(State->Player,
                FCk_Request_ProceduralActionPlayer_UpdateTuning{Tuning}, Completion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener->TuningCompletionCount == 1; }),
        WaitSeconds, TEXT("New default tuning applies only to later actions")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Invalid = FCk_ProceduralAction_PoseBatch{};
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnInvalidCompleted);
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Invalid}, Completion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->Listener->InvalidCompletionCount == 1;
        }), WaitSeconds, TEXT("Invalid manual batch completes before the next submit")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Invalid batch completes once"),
                State->Listener->InvalidCompletionCount, 1);
            TestEqual(TEXT("Invalid batch reports failure"),
                State->Listener->InvalidResult, ECk_Request_OperationResult::Failed);
            TestEqual(TEXT("Incomplete batch leaves manual control ready"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Action),
                ECk_ProceduralAction_State::Ready);
            const auto Radians = FMath::DegreesToRadians(1.0f);
            const auto Offset = FTransform{FQuat{FVector::UpVector, Radians},
                FVector{FMath::Cos(Radians) - 1.0f, FMath::Sin(Radians), 0.0f}};
            auto Target = FCk_ProceduralAction_LimbTarget{LimbId, Offset};
            Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnRequestCompleted);
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, Completion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action).Get_HasAcceptedPose();
        }), WaitSeconds, TEXT("Actual settled manual pose produces accepted receipt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Listener->Created)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Listener->Created)
                    == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("Completion-created conflicting action receives next-frame rejection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 8);
            TestTrue(TEXT("Accepted receipt is readable by identity"), Page.Get_Records().Num() > 0);
            TestTrue(TEXT("Accepted receipt identifies an observed revision"),
                UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action)
                    .Get_Stamp().Get_SubjectPoseRevision() > 0);
            TestEqual(TEXT("Accepted receipt retains admitted tuning revision"),
                UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action)
                    .Get_Stamp().Get_TuningRevision(), int64{1});
            const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action);
            State->CapturedAction = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ActionSnapshot(State->Action);
            const auto& Diagnostic = State->CapturedAction;
            TestTrue(TEXT("Native diagnostic matches accepted identity and pinned tuning"),
                Diagnostic.Get_Available() && Diagnostic.Get_Identity().Get_Value() == State->Identity.Get_Value()
                && Diagnostic.Get_TuningRevision() == Receipt.Get_Stamp().Get_TuningRevision()
                && Diagnostic.Get_NextSampleSequence() > Receipt.Get_Stamp().Get_SampleSequence());
            TestTrue(TEXT("Ready manual diagnostic identifies the active input watchdog"),
                Diagnostic.Get_Watchdogs().Num() == 1
                && Diagnostic.Get_Watchdogs()[0].Get_Name() == FName{TEXT("Input")}
                && Diagnostic.Get_Watchdogs()[0].Get_Remaining() > FCk_Time{});
            State->CapturedSubject = UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(State->Subject);
            const auto& SubjectDiagnostic = State->CapturedSubject;
            TestTrue(TEXT("Standalone diagnostic contains both disjoint owners without gait support"),
                SubjectDiagnostic.Get_Available() && SubjectDiagnostic.Get_Controllers().Num() == 2
                && SubjectDiagnostic.Get_Leases().IsEmpty() && SubjectDiagnostic.Get_Contacts().IsEmpty());
            TestFalse(TEXT("Wrong feature handle is an unavailable action diagnostic"),
                UCk_Utils_ProceduralAnimation_Debug_UE::Get_ActionSnapshot(State->Root).Get_Available());
            TestFalse(TEXT("Default handle is an unavailable subject diagnostic"),
                UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot({}).Get_Available());
            const auto HasGeometry = Receipt.Get_PreviousPose().Get_Limbs().Num() == 1
                && Receipt.Get_CurrentPose().Get_Limbs().Num() == 1
                && Receipt.Get_PreviousPose().Get_Limbs()[0].Get_Segments().Num() == 1
                && Receipt.Get_CurrentPose().Get_Limbs()[0].Get_Segments().Num() == 1;
            TestTrue(TEXT("Accepted receipt has complete claimed geometry"), HasGeometry);
            if (HasGeometry)
            {
                TestTrue(TEXT("Accepted endpoint moved from its settled entry"),
                    FVector::Distance(Receipt.Get_PreviousPose().Get_Limbs()[0]
                        .Get_AcceptedEndpoint().GetTranslation(),
                        Receipt.Get_CurrentPose().Get_Limbs()[0]
                        .Get_AcceptedEndpoint().GetTranslation()) > 1.0);
                TestTrue(TEXT("Accepted segment pivoted from its settled entry"),
                    FMath::RadiansToDegrees(Receipt.Get_PreviousPose().Get_Limbs()[0]
                        .Get_Segments()[0].GetRotation().AngularDistance(
                            Receipt.Get_CurrentPose().Get_Limbs()[0].Get_Segments()[0].GetRotation())) > 0.5);
            }
            TestEqual(TEXT("Submit completion fires exactly once"), State->Listener->CompletionCount, 1);
            TestEqual(TEXT("Submit completion reports success"), State->Listener->LastResult,
                ECk_Request_OperationResult::Succeeded);
            TestTrue(TEXT("Completion reentrancy created a separate scoped action"),
                ck::IsValid(State->Listener->Created));
            TestEqual(TEXT("Conflicting reentrant action rejects without queueing"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Listener->Created).Get_Outcome(),
                ECk_ProceduralAction_Outcome::Rejected);
            TestEqual(TEXT("Other claimed action remains idle and ready"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->OtherAction),
                ECk_ProceduralAction_State::Ready);
            auto BeyondReach = FCk_ProceduralAction_LimbTarget{OtherLimbId,
                FTransform{FQuat::Identity, FVector{1.0, 0.0, 0.0}}};
            BeyondReach.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            auto InvalidBatch = FCk_ProceduralAction_PoseBatch{};
            InvalidBatch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{BeyondReach});
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnInvalidCompleted);
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->OtherAction,
                FCk_Request_ProceduralAction_SubmitPoseBatch{InvalidBatch}, Completion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return State->Listener->InvalidCompletionCount == 2; }),
        WaitSeconds, TEXT("Unsolved disjoint manual batch fails without waiting for publication timeout")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Unsolved batch reports failure"), State->Listener->InvalidResult,
                ECk_Request_OperationResult::Failed);
            TestEqual(TEXT("Unsolved action retains its claim for a corrected submission"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->OtherAction),
                ECk_ProceduralAction_State::Ready);
            TestEqual(TEXT("Disjoint held action is not cancelled by another token's bad batch"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Action),
                ECk_ProceduralAction_State::Ready);
            State->Listener->DestroyAtFinishedScope = State->OtherScope;
            UCk_Utils_ProceduralAction_UE::Request_End(State->OtherAction,
                FCk_Request_ProceduralAction_End{}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->OtherIdentity, 0, 8);
            return Page.Get_Terminal();
        }), WaitSeconds, TEXT("Disjoint return settles while accepted action holds")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Live action emits one terminal signal"), State->Listener->FinishedCount, 1);
            TestEqual(TEXT("Finished state listener destroyed its scope once"),
                State->Listener->FinishedStateDestroyCount, 1);
            TestEqual(TEXT("Terminal signal carries action identity"),
                State->Listener->LastTerminal.Get_Identity().Get_Value(),
                State->OtherIdentity.Get_Value());
            TestTrue(TEXT("Committed transitions emit state signal"),
                State->Listener->StateChangeCount >= 2);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Accepted action remains ready through disjoint return"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Action),
                ECk_ProceduralAction_State::Ready);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Scope);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 8);
            return Page.Get_Terminal();
        }), WaitSeconds, TEXT("Scope destruction preserves root return and terminal stream")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 8);
            TestTrue(TEXT("Terminal stream retains accepted history"), Page.Get_Records().Num() >= 2);
            TestTrue(TEXT("Copied diagnostics retain their observation after owner loss and recovery"),
                State->CapturedAction.Get_Available()
                && State->CapturedAction.Get_State() == ECk_ProceduralAction_State::Ready
                && State->CapturedSubject.Get_Controllers().Num() == 2
                && UCk_Utils_ProceduralAnimation_Debug_UE::Get_ControlSnapshot(State->Subject)
                    .Get_Controllers().IsEmpty());
            if (NOT Page.Get_Records().IsEmpty())
            {
                const auto& Terminal = Page.Get_Records().Last();
                TestEqual(TEXT("Owner loss is the terminal outcome"), Terminal.Get_Result().Get_Outcome(),
                    ECk_ProceduralAction_Outcome::OwnerLost);
                TestEqual(TEXT("Owner loss completed dedicated recovery"),
                    Terminal.Get_Result().Get_Recovery(), ECk_ProceduralAction_Recovery::Succeeded);
            }
            TestTrue(TEXT("Limb returns near its rest position"),
                UCk_Utils_Transform_UE::Get_EntityCurrentTransform(State->Segment).GetTranslation()
                    .Equals(RootPose.GetTranslation() + FVector{50.0, 0.0, 0.0}, 1.0));
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionCompletionDestroysScopeTest,
    "Ck.ProceduralAnimation.Action.Manual.FailureCompletionDestroysScope",
    ck_test_procedural_action_manual::TestFlags)

auto
    FCkProceduralActionCompletionDestroysScopeTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_manual;
    const auto State = MakeShared<FDestroyOnCompletionState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Segment = UCk_Utils_Transform_UE::Add(SegmentEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Limb = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root,
                FCk_ProceduralArticulation_Spec{
                    FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
                    TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{LimbId,
                        TArray<FCk_Handle_Transform>{Segment}}}});
            TestTrue(TEXT("Reentrant failure subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Reentrant failure subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}});
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            State->Listener = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
                NewObject<UCk_Test_ProceduralActionReentry_UE>()};
            State->Listener->DestroyScope = State->Scope;
            TestTrue(TEXT("Reentrant failure action reserves"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                    == ECk_ProceduralAction_State::Ready;
        }), WaitSeconds, TEXT("Reentrant failure action acquires")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto BeyondReach = FCk_ProceduralAction_LimbTarget{LimbId,
                FTransform{FQuat::Identity, FVector{1.0, 0.0, 0.0}}};
            BeyondReach.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{BeyondReach});
            auto Completion = FCk_Delegate_Request_OnCompleted{};
            Completion.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnDestroyingSubmitCompleted);
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, Completion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->Listener->DestroyingCompletionCount == 1
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Failed completion destroys scope with one retained terminal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Invalid solve completes once before owner loss"),
                State->Listener->DestroyingCompletionCount, 1);
            TestEqual(TEXT("Invalid solve reports failure"),
                State->Listener->DestroyingResult, ECk_Request_OperationResult::Failed);
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 8);
            TestTrue(TEXT("Owner-loss terminal retained after callback destroys scope"),
                Page.Get_Terminal() && NOT Page.Get_Records().IsEmpty());
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionQueuedRequestsAfterDestroyTest,
    "Ck.ProceduralAnimation.Action.Manual.QueuedRequestsCancelAfterCallbackDestroysScope",
    ck_test_procedural_action_manual::TestFlags)

auto
    FCkProceduralActionQueuedRequestsAfterDestroyTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_manual;
    const auto State = MakeShared<FDestroyOnCompletionState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Segment = UCk_Utils_Transform_UE::Add(SegmentEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Limb = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root,
                FCk_ProceduralArticulation_Spec{
                    FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
                    TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{LimbId,
                        TArray<FCk_Handle_Transform>{Segment}}}});
            TestTrue(TEXT("Queued-request subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Queued-request subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_Manual(State->Scope,
                State->Player, FCk_ProceduralAction_ControlSpec{TArray<FName>{LimbId}});
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            State->Listener = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
                NewObject<UCk_Test_ProceduralActionReentry_UE>()};
            State->Listener->DestroyScope = State->Scope;
            TestTrue(TEXT("Queued-request action reserves"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action)
                    == ECk_ProceduralAction_State::Ready;
        }), WaitSeconds, TEXT("Queued-request action acquires")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Target = FCk_ProceduralAction_LimbTarget{LimbId, FTransform::Identity};
            Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
            auto DestroyingCompletion = FCk_Delegate_Request_OnCompleted{};
            DestroyingCompletion.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnDestroyingSubmitCompleted);
            auto EndCompletion = FCk_Delegate_Request_OnCompleted{};
            EndCompletion.BindDynamic(State->Listener.Get(),
                &UCk_Test_ProceduralActionReentry_UE::OnInvalidCompleted);
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Action,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, DestroyingCompletion);
            UCk_Utils_ProceduralAction_UE::Request_End(State->Action,
                FCk_Request_ProceduralAction_End{}, EndCompletion);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->Listener->DestroyingCompletionCount == 1
                && State->Listener->InvalidCompletionCount == 1
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Queued commands finish after callback destroys scope")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Busy submit fails once"), State->Listener->DestroyingCompletionCount, 1);
            TestEqual(TEXT("Busy submit reports failure"), State->Listener->DestroyingResult,
                ECk_Request_OperationResult::Failed);
            TestEqual(TEXT("Queued End completes once"), State->Listener->InvalidCompletionCount, 1);
            TestEqual(TEXT("Queued End is cancelled after scope destruction"),
                State->Listener->InvalidResult, ECk_Request_OperationResult::Failed_Cancelled);
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 8);
            TestTrue(TEXT("Destroyed scope does not claim a completed playback"),
                Page.Get_Terminal() && NOT Page.Get_Records().IsEmpty()
                    && Page.Get_Records().Last().Get_Result().Get_Outcome()
                        == ECk_ProceduralAction_Outcome::OwnerLost);
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
