#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkProceduralAnimationDebugger/Model/CkProceduralAnimationDebugger_Model.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_debugger_pie
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 15.0;
    const auto ClipPath = FSoftObjectPath{TEXT(
        "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA")};

    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Presentation;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        TSharedPtr<FCkProceduralAnimationDebugger_Model> Model;
        int64 FirstSequence = 0;
        int64 LastSequence = 0;
        int32 InitialCount = 0;
    };
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionDebuggerCopiedReceiptsTest,
    "Ck.ProceduralAnimation.Action.Debugger.CopiedReceiptsAndGameplayGap",
    ck_test_procedural_action_debugger_pie::TestFlags)

auto
    FCkProceduralActionDebuggerCopiedReceiptsTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_debugger_pie;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity,
                FTransform{FVector{0.0, 51000.0, 5000.0}}, ECk_Replication::DoesNotReplicate);
            auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
            Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
            Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
            auto Spec = FCk_ProceduralArticulation_Spec{Layout,
                TArray<FCk_ProceduralLimb_Binding>{}};
            Spec.Set_Presentation(State->Presentation);
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            TestTrue(TEXT("Body-only articulation composes without gait"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Standalone articulation settles in the real PIE world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            TestTrue(TEXT("New discovery includes standalone articulation"),
                UCk_Utils_ProceduralAnimation_Debug_UE::Get_Articulations(InWorld).Contains(
                    State->Root.ConvertToHandle()));
            TestFalse(TEXT("Existing Get_Entities remains a gait-only roster"),
                UCk_Utils_ProceduralAnimation_Debug_UE::Get_Entities(InWorld).Contains(
                    State->Root.ConvertToHandle()));

            State->Model = MakeShared<FCkProceduralAnimationDebugger_Model>();
            State->Model->Set_World(InWorld);
            State->Model->Refresh();
            TestTrue(TEXT("Debugger roster includes the standalone subject"),
                State->Model->Get_Rows().ContainsByPredicate([State](const auto& InRow)
                    { return InRow.Entity == State->Root.ConvertToHandle() && InRow.Summary.Get_HasArticulation()
                        && NOT InRow.Summary.Get_HasGait(); }));
            TestTrue(TEXT("Body-only articulation can be selected"),
                State->Model->Select(State->Root.ConvertToHandle()));
            TestEqual(TEXT("Body-only subject has no invented gait history"), State->Model->Get_History().Get_Count(), 0);

            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Standalone subject admits a player"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{ClipPath}});
            TestTrue(TEXT("Saved BodyPulse starts a managed action"), ck::IsValid(State->Action));
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Player)
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, 0, 64).Get_Terminal();
        }), WaitSeconds, TEXT("BodyPulse produces a terminal public receipt stream")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Before = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 64);
            TestEqual(TEXT("Gameplay receipt page is readable"), Before.Get_Result(),
                ECk_ProceduralAction_ReadResult::Succeeded);
            TestFalse(TEXT("BodyPulse fits a bounded debugger page"), Before.Get_HasMore());
            State->InitialCount = Before.Get_Records().Num();
            TestTrue(TEXT("BodyPulse has several receipts to test later consumption"), State->InitialCount > 1);
            if (State->InitialCount <= 1) { return; }
            State->FirstSequence = Before.Get_Records()[0].Get_StreamSequence();
            State->LastSequence = Before.Get_LastStreamSequence();

            State->Model->Refresh();
            TestTrue(TEXT("Selected model discovers the action identity"),
                State->Model->Get_ActionStreams().ContainsByPredicate([State](const auto& InIdentity)
                    { return InIdentity.Get_Value() == State->Identity.Get_Value(); }));
            TestEqual(TEXT("Model copies the public page"),
                State->Model->Get_ActionRecording().Get_Records().Num(), State->InitialCount);
            TestEqual(TEXT("Copied stream carries no gap before gameplay consumption"),
                State->Model->Get_ActionRecording().Get_Gaps().Num(), 0);

            const auto After = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 64);
            TestEqual(TEXT("Debugger selection and refresh do not consume gameplay receipts"),
                After.Get_Records().Num(), State->InitialCount);
            TestEqual(TEXT("Debugger read preserves the public stream cursor"),
                After.Get_LastStreamSequence(), State->LastSequence);

            UCk_Utils_ProceduralActionPlayer_UE::Request_ConsumeReceipts(State->Player,
                FCk_Request_ProceduralActionPlayer_ConsumeReceipts{State->Identity, State->FirstSequence}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 64);
            return Page.Get_Result() == ECk_ProceduralAction_ReadResult::Gap
                && Page.Get_FirstMissingSequence() == 1
                && Page.Get_LastMissingSequence() == State->FirstSequence
                && NOT Page.Get_Records().IsEmpty()
                && Page.Get_Records()[0].Get_StreamSequence() > State->FirstSequence;
        }), WaitSeconds, TEXT("Explicit gameplay consumption exposes its missing receipt range")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            // Reset creates a new consumer cursor. The already-copied model record remains independent until this reset.
            State->Model->Reset();
            State->Model->Set_World(InWorld);
            TestTrue(TEXT("Fresh debugger consumer reselects the subject"),
                State->Model->Select(State->Root.ConvertToHandle()));
            const auto& Recording = State->Model->Get_ActionRecording();
            TestTrue(TEXT("Explicit gameplay consumption appears as a copied-stream gap"),
                Recording.Get_Gaps().Num() == 1
                    && Recording.Get_Gaps()[0].Get_First() == 1
                    && Recording.Get_Gaps()[0].Get_Last() == State->FirstSequence);
            TestTrue(TEXT("Remaining receipts were copied after the gameplay gap"),
                NOT Recording.Get_Records().IsEmpty());
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::Is_NOT_Valid(State->Root); }),
        WaitSeconds, TEXT("Selected subject is destroyed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto CopiedCount = State->Model->Get_ActionRecording().Get_Records().Num();
            State->Model->Refresh();
            TestTrue(TEXT("Destroyed selected subject is reported gone"), State->Model->Get_SelectedGone());
            TestEqual(TEXT("Copied receipts outlive the source entity"),
                State->Model->Get_ActionRecording().Get_Records().Num(), CopiedCount);
            TestTrue(TEXT("The independent copy survives entity destruction"), CopiedCount > 0);
            State->Model->ReleaseSession();
            TestTrue(TEXT("ReleaseSession is terminal"), State->Model->Get_IsReleased());
            TestEqual(TEXT("ReleaseSession clears copied receipts"),
                State->Model->Get_ActionRecording().Get_Records().Num(), 0);
            TestEqual(TEXT("ReleaseSession clears copied gaps"),
                State->Model->Get_ActionRecording().Get_Gaps().Num(), 0);
            TestEqual(TEXT("ReleaseSession clears the subject roster"), State->Model->Get_Rows().Num(), 0);
            State->Model.Reset();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionDebuggerManagedStreamTest,
    "Ck.ProceduralAnimation.Debugger.ActionPie.ManagedStreamKeepsRecording",
    ck_test_procedural_action_debugger_pie::TestFlags)

auto
    FCkProceduralActionDebuggerManagedStreamTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_debugger_pie;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity,
                FTransform{FVector{0.0, 52000.0, 5000.0}}, ECk_Replication::DoesNotReplicate);
            auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
            Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
            Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
            auto Spec = FCk_ProceduralArticulation_Spec{Layout,
                TArray<FCk_ProceduralLimb_Binding>{}};
            Spec.Set_Presentation(State->Presentation);
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            TestTrue(TEXT("Body-only articulation composes without gait"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Standalone articulation settles in the real PIE world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            State->Model = MakeShared<FCkProceduralAnimationDebugger_Model>();
            State->Model->Set_World(InWorld);
            State->Model->Refresh();
            TestTrue(TEXT("Body-only articulation can be selected"),
                State->Model->Select(State->Root.ConvertToHandle()));

            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Standalone subject admits a player"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto* Clip = LoadObject<UCk_ProceduralAction_Data>(nullptr, *ClipPath.ToString());
            if (NOT TestNotNull(TEXT("Saved BodyPulse loads"), Clip)) { return; }
            const auto Start = UCk_Utils_ProceduralAction_UE::Play_Loaded(State->Scope, State->Player,
                FCk_ProceduralAction_LoadedPlaySpec{Clip}, {});
            State->Action = Start.Get_Action();
            State->Identity = Start.Get_Identity();
            TestTrue(TEXT("Saved BodyPulse starts with managed delivery"), ck::IsValid(State->Action)
                && UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(State->Player, State->Identity));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            State->Model->Refresh();
            const auto& Recording = State->Model->Get_ActionRecording();
            const auto Result = Recording.Get_LastReadResult();
            const auto StillRecording = Result == ECk_ProceduralAction_ReadResult::Succeeded
                || Result == ECk_ProceduralAction_ReadResult::Pending
                || Result == ECk_ProceduralAction_ReadResult::Gap;
            return Recording.Get_Terminal() || NOT StillRecording;
        }), WaitSeconds, TEXT("The debugger copies the managed stream until its terminal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Recording = State->Model->Get_ActionRecording();
            TestEqual(TEXT("The debugger auto-selected the managed stream"), Recording.Get_Identity().Get_Value(),
                State->Identity.Get_Value());
            const auto Result = Recording.Get_LastReadResult();
            TestTrue(TEXT("Every read of the managed stream was accepted, so recording never stopped"),
                Result == ECk_ProceduralAction_ReadResult::Succeeded
                    || Result == ECk_ProceduralAction_ReadResult::Pending
                    || Result == ECk_ProceduralAction_ReadResult::Gap);
            TestTrue(TEXT("The copied recording reached the stream's terminal"), Recording.Get_Terminal());
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
            State->Model->ReleaseSession();
            State->Model.Reset();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

// --------------------------------------------------------------------------------------------------------------------
