#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Fragment.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_ClipBinding.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkTest_ProceduralActionClipFixture.h"
#include "../CkTest_ProceduralWalkerFixture.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace ck_test_procedural_action_clip
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto ClipPath = FSoftObjectPath{TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA")};
    const auto LimbClipPath = FSoftObjectPath{TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA")};
    const auto RootPose = FTransform{FQuat::Identity, FVector{100.0, 200.0, 2000.0}};

    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Presentation;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        FCk_Handle MissingScope;
        FCk_Handle_ProceduralAction MissingAction;
        FCk_ProceduralAction_Identity MissingIdentity;
        FCk_Handle CancelScope;
        FCk_Handle_ProceduralAction CancelAction;
        FCk_ProceduralAction_Identity CancelIdentity;
        bool DestroyRequestedWithOpenWindow = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionPlaySpecAccessorsTest,
    "Ck.ProceduralAnimation.Action.Clip.PlaySpecAccessors",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionPlaySpecAccessorsTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    const auto Slots = TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{TEXT("Nod"), TEXT("NodArm")}};
    auto Play = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{LimbClipPath}};
    TestEqual(TEXT("A play delivers explicitly by default"), Play.Get_Delivery(),
        ECk_ProceduralAction_Delivery::Explicit);
    auto* Fluent = &Play.Set_SlotBindings(Slots).Set_Delivery(ECk_ProceduralAction_Delivery::Auto);
    TestTrue(TEXT("Play setters retain the concrete fluent type"), Fluent == &Play);
    TestEqual(TEXT("Play stores named bindings"), Play.Get_SlotBindings().Num(), 1);
    TestEqual(TEXT("Play stores its delivery"), Play.Get_Delivery(), ECk_ProceduralAction_Delivery::Auto);
    TestTrue(TEXT("Play keeps the clip it was made with"), Play.Get_Clip().ToSoftObjectPath() == LimbClipPath);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionSavedLimbNodCompileTest,
    "Ck.ProceduralAnimation.Action.Clip.SavedLimbNodCompiles",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionSavedLimbNodCompileTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    auto* Asset = LoadObject<UCk_ProceduralAction_Data>(nullptr, *LimbClipPath.ToString());
    if (NOT TestNotNull(TEXT("Persisted LimbNod clip loads with its expected type"), Asset))
    { return false; }
    auto Compiled = ck::procedural_action::FCompiledClip{};
    auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
    const auto Compiles = ck::procedural_action::Compile(*Asset, Compiled, Diagnostics);
    const auto PublicReport = UCk_Utils_ProceduralAction_UE::Validate_Data(Asset);
    for (const auto& Diagnostic : Diagnostics)
    {
        AddInfo(FString::Printf(TEXT("LimbNod compile diagnostic: %s [%s], expected %s, actual %s"),
            *Diagnostic.Get_FieldPath(), *Diagnostic.Get_Code().ToString(),
            *Diagnostic.Get_Expected(), *Diagnostic.Get_Actual()));
    }
    TestTrue(TEXT("Persisted LimbNod clip compiles"), Compiles);
    TestEqual(TEXT("Public data validation uses compiler verdict"), PublicReport.Get_Status(),
        Compiles ? ECk_ProceduralAction_ReportStatus::Valid
            : ECk_ProceduralAction_ReportStatus::Invalid);
    TestFalse(TEXT("Static data validation does not claim admission"), PublicReport.Get_Advisory());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionAuthoredRestSparseEntryTest,
    "Ck.ProceduralAnimation.Action.Clip.AuthoredRestSparseEntryUsesTopology",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionAuthoredRestSparseEntryTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    struct FRestState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
    };
    const auto State = MakeShared<FRestState>();
    const auto LimbId = FName{TEXT("NodArm")};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, LimbId](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
            auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Part = UCk_Utils_Transform_UE::Add(PartEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Layout = FCk_ProceduralLimb_Layout{LimbId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root,
                FCk_ProceduralArticulation_Spec{
                    FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Layout}},
                    TArray<FCk_ProceduralLimb_Binding>{
                        FCk_ProceduralLimb_Binding{LimbId, TArray<FCk_Handle_Transform>{Part}}}});
            TestTrue(TEXT("Sparse-entry subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Sparse-entry subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, LimbId](UWorld*)
        {
            auto Default = FCk_ProceduralArticulation_Pose{};
            const auto DefaultResult = FCk_ProceduralArticulation_Control::GetDefaultPose(
                State->Subject, Default);
            if (NOT TestEqual(TEXT("Topology default resolves"), DefaultResult,
                ECk_ArticulationControlResult::Accepted)
                || NOT TestEqual(TEXT("Default contains one limb"), Default.Get_Limbs().Num(), 1))
            { return; }
            const auto& RestLimb = Default.Get_Limbs()[0];
            const auto DisplacedWorld = FTransform{
                FQuat{FVector::UpVector, FMath::DegreesToRadians(1.0f)},
                RootPose.TransformPosition(FVector{100.0f * FMath::Cos(FMath::DegreesToRadians(1.0f)),
                    100.0f * FMath::Sin(FMath::DegreesToRadians(1.0f)), 0.0f})};
            const auto DisplacedLimb = FCk_ProceduralArticulation_LimbPose{LimbId,
                DisplacedWorld, DisplacedWorld, RestLimb.Get_Segments(), 0.0f,
                RestLimb.Get_ChainState(), RestLimb.Get_ContactEvidence()};
            const auto Entry = FCk_ProceduralArticulation_Pose{Default.Get_RootFrame(),
                Default.Get_BodyPresentation(), Default.Get_HasBodyPresentation(),
                TArray<FCk_ProceduralArticulation_LimbPose>{DisplacedLimb}};
            auto* Saved = LoadObject<UCk_ProceduralAction_Data>(nullptr, *LimbClipPath.ToString());
            if (NOT TestNotNull(TEXT("AuthoredRest source clip loads"), Saved)) { return; }
            auto* Sparse = DuplicateObject<UCk_ProceduralAction_Data>(Saved, GetTransientPackage());
            if (NOT TestNotNull(TEXT("Sparse clone exists"), Sparse)) { return; }
            if (NOT TestTrue(TEXT("Source has a keyed follow-up phase"),
                Saved->Get_Phases().Num() == 1))
            { return; }
            Sparse->Set_Phases(TArray<FCk_ProceduralAction_Phase>{
                FCk_ProceduralAction_Phase{FName{TEXT("Sparse")}, FCk_Time{0.25}},
                Saved->Get_Phases()[0]});
            Sparse->Set_Markers({});
            Sparse->Set_Windows({});
            auto Compiled = ck::procedural_action::FCompiledClip{};
            auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
            if (NOT TestTrue(TEXT("Sparse AuthoredRest clip compiles"),
                ck::procedural_action::Compile(*Sparse, Compiled, Diagnostics)))
            { return; }
            TSharedPtr<const ck::procedural_action::FCompiledClip> SharedClip =
                MakeShared<ck::procedural_action::FCompiledClip>(MoveTemp(Compiled));
            const auto CompiledFragment = ck::FFragment_ProceduralAction_Compiled{
                SharedClip, TArray<FName>{LimbId}};
            auto Initial = ck::procedural_action::FInitialPose{};
            if (NOT TestTrue(TEXT("Runtime initial capture resolves"),
                ck_procedural_action_player_service::CaptureClipInitial(
                    State->Subject, CompiledFragment, Entry, Default, Initial)))
            { return; }
            const auto* InitialRest = Initial.Get_Limbs().Find(FName{TEXT("Nod")});
            if (NOT TestNotNull(TEXT("Sparse slot has a topology initial value"), InitialRest))
            { return; }
            TestTrue(TEXT("Displaced accepted entry does not replace authored rest"),
                InitialRest->GetTranslation().IsNearlyZero(1.0e-4));
            auto Evaluated = ck::procedural_action::FEvaluatedPose{};
            if (TestTrue(TEXT("Sparse channel evaluates"), ck::procedural_action::Evaluate(
                *SharedClip, Initial, FCk_Time{0.125}, Evaluated)))
            {
                TestTrue(TEXT("Sparse channel holds topology rest through phase"),
                    Evaluated.Get_Limbs().Num() == 1
                        && Evaluated.Get_Limbs()[0].Get_Position().IsNearlyZero(1.0e-4));
            }
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionManagedBodyPulseTest,
    "Ck.ProceduralAnimation.Action.Clip.ManagedBodyPulseAcceptedTimeline",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionManagedBodyPulseTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
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
            TestTrue(TEXT("Body-only subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Body-only entry settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Player attaches"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto SoftClip = TSoftObjectPtr<UCk_ProceduralAction_Data>{ClipPath};
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(
                State->Scope, State->Player, FCk_ProceduralAction_PlaySpec{SoftClip});
            TestTrue(TEXT("On-disk soft clip reserves an action"), ck::IsValid(State->Action));
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            const auto Retained = UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(
                State->Player);
            TestTrue(TEXT("Reserved identity is publicly discoverable"), Retained.Num() == 1
                && Retained[0].Get_Value() == State->Identity.Get_Value());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Player)
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, 0, 64).Get_Terminal();
        }), WaitSeconds, TEXT("Managed clip settles and returns before terminal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Cursor = int64{0};
            auto AcceptedCount = 0;
            auto Compiled = ck::procedural_action::FCompiledClip{};
            auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
            auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, *ClipPath.ToString());
            if (NOT TestNotNull(TEXT("Fixture content remains resident"), Source)
                || NOT TestTrue(TEXT("Fixture window schedule compiles"),
                    ck::procedural_action::Compile(*Source, Compiled, Diagnostics)))
            { return; }
            auto InitialWindows = TArray<FName>{};
            for (const auto& Event : Compiled.Get_Events())
            {
                if (Event.Get_Time() != FCk_Time{}) { continue; }
                if (Event.Get_Kind() == ck::procedural_action::EEventKind::WindowEnter)
                { InitialWindows.Add(Event.Get_Id()); }
            }
            auto HasBoundaryExit = false;
            auto HasBoundaryPoint = false;
            auto HasBoundaryEnter = false;
            auto HasFirstWindowInterval = false;
            auto HasClosingWindowInterval = false;
            auto HasFollowingWindowInterval = false;
            auto BoundaryOrder = TArray<ECk_ProceduralAction_EventKind>{};
            auto HasFinalObservedPose = false;
            auto TerminalOutcome = ECk_ProceduralAction_Outcome::None;
            auto TerminalRecovery = ECk_ProceduralAction_Recovery::NotRequired;
            for (auto PageIndex = 0; PageIndex < 16; ++PageIndex)
            {
                const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, Cursor, 64);
                TestEqual(TEXT("Retained page remains readable"), Page.Get_Result(),
                    ECk_ProceduralAction_ReadResult::Succeeded);
                for (const auto& Record : Page.Get_Records())
                {
                    if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose)
                    {
                        ++AcceptedCount;
                        const auto& Receipt = Record.Get_Receipt();
                        const auto& Interval = Receipt.Get_Interval();
                        if (Interval.Get_ToTime() > Interval.Get_FromTime())
                        {
                            const auto HasOnlyA = Interval.Get_Windows().Num() == 1
                                && Interval.Get_Windows().Contains(FName{TEXT("WindowA")});
                            const auto HasOnlyB = Interval.Get_Windows().Num() == 1
                                && Interval.Get_Windows().Contains(FName{TEXT("WindowB")});
                            if (Interval.Get_FromTime() == FCk_Time{})
                            { HasFirstWindowInterval |= Interval.Get_Windows() == InitialWindows; }
                            if (Interval.Get_ToTime() == FCk_Time{0.2})
                            { HasClosingWindowInterval |= HasOnlyA; }
                            if (Interval.Get_FromTime() == FCk_Time{0.2})
                            { HasFollowingWindowInterval |= HasOnlyB; }
                        }
                        TestTrue(TEXT("Compiled clip stamp is positive"),
                            Receipt.Get_Stamp().Get_ContentRevision() > 0);
                        TestTrue(TEXT("Accepted pose carries actual subject revision"),
                            Receipt.Get_Stamp().Get_SubjectPoseRevision() > 0);
                        if (Receipt.Get_Interval().Get_ToTime() >= FCk_Time{0.4}
                            && Receipt.Get_CurrentPose().Get_HasBodyPresentation())
                        {
                            HasFinalObservedPose |= Receipt.Get_CurrentPose().Get_BodyPresentation()
                                .GetTranslation().Equals(
                                    RootPose.GetTranslation() + FVector{1.0, 0.0, 0.0}, 0.2);
                        }
                        for (const auto& Event : Receipt.Get_Interval().Get_Events())
                        {
                            if (Event.Get_Time() != FCk_Time{0.2}) { continue; }
                            BoundaryOrder.Add(Event.Get_Kind());
                            HasBoundaryExit |= Event.Get_Kind() == ECk_ProceduralAction_EventKind::WindowExit
                                && Event.Get_Id() == FName{TEXT("WindowA")};
                            HasBoundaryPoint |= Event.Get_Kind() == ECk_ProceduralAction_EventKind::Point
                                && Event.Get_Id() == FName{TEXT("PhaseBoundary")};
                            HasBoundaryEnter |= Event.Get_Kind() == ECk_ProceduralAction_EventKind::WindowEnter
                                && Event.Get_Id() == FName{TEXT("WindowB")};
                        }
                    }
                    else if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal)
                    {
                        TerminalOutcome = Record.Get_Result().Get_Outcome();
                        TerminalRecovery = Record.Get_Result().Get_Recovery();
                    }
                }
                Cursor = Page.Get_LastStreamSequence();
                if (NOT Page.Get_HasMore()) { break; }
            }
            TestTrue(TEXT("Managed clip emitted observed accepted intervals"), AcceptedCount > 1);
            TestTrue(TEXT("Body reached the fixture's final offset before return"), HasFinalObservedPose);
            TestTrue(TEXT("Window A exits at the exact phase boundary"), HasBoundaryExit);
            TestTrue(TEXT("Point marker survives the phase boundary"), HasBoundaryPoint);
            TestTrue(TEXT("Window B enters at the exact phase boundary"), HasBoundaryEnter);
            TestTrue(TEXT("First positive interval matches the fixture's time-zero window schedule"), HasFirstWindowInterval);
            TestTrue(TEXT("Closing interval retains A and does not expose B early"), HasClosingWindowInterval);
            TestTrue(TEXT("Following interval exposes B and has closed A"), HasFollowingWindowInterval);
            TestTrue(TEXT("Equal-time events retain exit, point, enter order"),
                BoundaryOrder.Num() == 3
                    && BoundaryOrder[0] == ECk_ProceduralAction_EventKind::WindowExit
                    && BoundaryOrder[1] == ECk_ProceduralAction_EventKind::Point
                    && BoundaryOrder[2] == ECk_ProceduralAction_EventKind::WindowEnter);
            TestEqual(TEXT("Terminal follows completed playback"), TerminalOutcome,
                ECk_ProceduralAction_Outcome::Completed);
            TestEqual(TEXT("Terminal follows observed return"), TerminalRecovery,
                ECk_ProceduralAction_Recovery::Succeeded);
            State->MissingScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto MissingPath = FSoftObjectPath{TEXT(
                "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_Missing_DA.Ck_ProceduralAction_Missing_DA")};
            State->MissingAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(
                State->MissingScope, State->Player, FCk_ProceduralAction_PlaySpec{
                    TSoftObjectPtr<UCk_ProceduralAction_Data>{MissingPath}});
            TestTrue(TEXT("Missing nonnull soft path reserves a stream"),
                ck::IsValid(State->MissingAction));
            State->MissingIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->MissingAction);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->MissingIdentity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Missing on-disk clip terminates its reserved stream")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->MissingIdentity, 0, 8);
            TestEqual(TEXT("Missing soft asset has typed load failure"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->MissingAction).Get_Outcome(),
                ECk_ProceduralAction_Outcome::AssetFailed);
            TestEqual(TEXT("Missing clip reserves closure and terminal records"),
                Page.Get_Records().Num(), 2);
            if (Page.Get_Records().Num() == 2)
            {
                TestEqual(TEXT("Missing clip closes an empty window set"),
                    Page.Get_Records()[0].Get_Kind(), ECk_ProceduralAction_RecordKind::WindowsClosed);
                TestTrue(TEXT("Missing clip has no active windows to close"),
                    Page.Get_Records()[0].Get_ClosedWindowIds().IsEmpty());
                TestEqual(TEXT("Missing clip terminates after closure"),
                    Page.Get_Records()[1].Get_Kind(), ECk_ProceduralAction_RecordKind::Terminal);
            }
            State->CancelScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->CancelAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(
                State->CancelScope, State->Player, FCk_ProceduralAction_PlaySpec{
                    TSoftObjectPtr<UCk_ProceduralAction_Data>{ClipPath}});
            TestTrue(TEXT("Existing soft clip waits for admission before service"),
                ck::IsValid(State->CancelAction)
                    && (UCk_Utils_ProceduralAction_UE::Get_State(State->CancelAction)
                            == ECk_ProceduralAction_State::PendingLoad
                        || UCk_Utils_ProceduralAction_UE::Get_State(State->CancelAction)
                            == ECk_ProceduralAction_State::PendingAdmission));
            State->CancelIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->CancelAction);
            UCk_Utils_ProceduralAction_UE::Request_Cancel(State->CancelAction,
                FCk_Request_ProceduralAction_Cancel{FName{TEXT("CancelBeforePlayback")}}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->CancelIdentity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Pending-load cancellation commits its terminal record")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->CancelAction);
            TestEqual(TEXT("Pending-load cancel is the terminal outcome"), Result.Get_Outcome(),
                ECk_ProceduralAction_Outcome::Cancelled);
            TestEqual(TEXT("No claim needs recovery before load admission"), Result.Get_Recovery(),
                ECk_ProceduralAction_Recovery::NotRequired);
            TestFalse(TEXT("Cancelled action releases rooted loading state"),
                State->CancelAction.Has<ck::FFragment_ProceduralAction_Loading>());
            CollectGarbage(RF_NoFlags, true);
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->CancelIdentity, 0, 8);
            TestTrue(TEXT("Cancelled stream remains readable after GC"), Page.Get_Terminal());
            TestEqual(TEXT("Cancelled clip keeps closure and terminal after GC"),
                Page.Get_Records().Num(), 2);
            if (Page.Get_Records().Num() == 2)
            {
                TestEqual(TEXT("Cancelled clip closes before terminal"),
                    Page.Get_Records()[0].Get_Kind(), ECk_ProceduralAction_RecordKind::WindowsClosed);
                TestTrue(TEXT("Cancelled clip has no active windows"),
                    Page.Get_Records()[0].Get_ClosedWindowIds().IsEmpty());
                TestEqual(TEXT("Cancelled clip terminates without an accepted pose"),
                    Page.Get_Records()[1].Get_Kind(), ECk_ProceduralAction_RecordKind::Terminal);
            }
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionLoadedClipSnapshotTest,
    "Ck.ProceduralAnimation.Action.Clip.LoadedClipSnapshotsTransientContent",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionLoadedClipSnapshotTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
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
            TestTrue(TEXT("Loaded clip subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Loaded clip entry settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                State->Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("Loaded clip player attaches"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, *ClipPath.ToString());
            if (NOT TestNotNull(TEXT("Saved noncombat body clip loads"), Source)) { return; }
            auto Mutable = TStrongObjectPtr<UCk_ProceduralAction_Data>{
                DuplicateObject<UCk_ProceduralAction_Data>(Source, GetTransientPackage())};
            if (NOT TestTrue(TEXT("Loaded fixture has a first phase"), NOT Mutable->Get_Phases().IsEmpty()))
            { return; }
            const auto FirstPhaseId = Mutable->Get_Phases()[0].Get_Id();
            Mutable->Set_Windows(TArray<FCk_ProceduralAction_Window>{
                FCk_ProceduralAction_Window{FName{TEXT("TimeZero")},
                    FCk_ProceduralAction_TimeRef{FirstPhaseId, FCk_FloatRange_0to1{0.0}},
                    FCk_ProceduralAction_TimeRef{FirstPhaseId, FCk_FloatRange_0to1{0.37}}}});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{Mutable.Get()}});
            TestTrue(TEXT("Loaded entry reserves an action"), ck::IsValid(State->Action));
            if (ck::Is_NOT_Valid(State->Action)) { return; }
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            TestEqual(TEXT("Loaded entry skips asynchronous loading"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Action),
                ECk_ProceduralAction_State::PendingAdmission);
            TestTrue(TEXT("Loaded entry owns compiled content immediately"),
                State->Action.Has<ck::FFragment_ProceduralAction_Compiled>());
            Mutable->Set_Phases({});
            Mutable.Reset();
            CollectGarbage(RF_NoFlags, true);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Player)
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, 0, 64).Get_Terminal();
        }), WaitSeconds, TEXT("Loaded transient body clip settles after source mutation and GC")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 64);
            TestTrue(TEXT("Detached loaded clip published accepted geometry"),
                Page.Get_Records().ContainsByPredicate([](const auto& InRecord)
                { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose; }));
            auto SawInitialWindow = false;
            auto SawClosingWindow = false;
            auto SawAfterWindow = false;
            auto WindowEnd = FCk_Time{};
            for (const auto& Record : Page.Get_Records())
            {
                if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose) { continue; }
                const auto& Interval = Record.Get_Receipt().Get_Interval();
                if (Interval.Get_ToTime() <= Interval.Get_FromTime()) { continue; }
                if (Interval.Get_FromTime() == FCk_Time{})
                { SawInitialWindow = Interval.Get_Windows().Contains(FName{TEXT("TimeZero")}); }
                for (const auto& Event : Interval.Get_Events())
                {
                    if (Event.Get_Id() == FName{TEXT("TimeZero")}
                        && Event.Get_Kind() == ECk_ProceduralAction_EventKind::WindowExit)
                    {
                        WindowEnd = Event.Get_Time();
                        SawClosingWindow = Interval.Get_ToTime() == WindowEnd
                            && Interval.Get_Windows().Contains(FName{TEXT("TimeZero")});
                    }
                }
                if (WindowEnd > FCk_Time{} && Interval.Get_FromTime() == WindowEnd)
                { SawAfterWindow = Interval.Get_Windows().IsEmpty(); }
            }
            TestTrue(TEXT("Time-zero window exposes the first positive accepted interval"), SawInitialWindow);
            TestTrue(TEXT("Non-binary window exit retains exposure through its exact boundary"), SawClosingWindow);
            TestTrue(TEXT("Next accepted interval begins at the exact closed boundary"), SawAfterWindow);
            TestEqual(TEXT("Detached loaded clip completes after source collection"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Action).Get_Outcome(),
                ECk_ProceduralAction_Outcome::Completed);
            auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, *ClipPath.ToString());
            if (TestNotNull(TEXT("Invalid loaded clip source loads"), Source))
            {
                auto InvalidClip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
                    DuplicateObject<UCk_ProceduralAction_Data>(Source, GetTransientPackage())};
                InvalidClip->Set_Phases({});
                const auto InvalidReport = UCk_Utils_ProceduralAction_UE::Validate_Data(
                    InvalidClip.Get());
                TestEqual(TEXT("Invalid loaded content exposes structured validation status"),
                    InvalidReport.Get_Status(), ECk_ProceduralAction_ReportStatus::Invalid);
                TestTrue(TEXT("Invalid loaded content names its field"),
                    InvalidReport.Get_Diagnostics().ContainsByPredicate([](const auto& InDiagnostic)
                    { return InDiagnostic.Get_FieldPath() == TEXT("Phases"); }));
                auto InvalidScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
                const auto InvalidAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(
                    InvalidScope, State->Player,
                    FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{InvalidClip.Get()}});
                TestTrue(TEXT("Invalid loaded content reserves a typed terminal stream"),
                    ck::IsValid(InvalidAction));
                if (ck::IsValid(InvalidAction))
                {
                    TestEqual(TEXT("Invalid loaded content finishes synchronously"),
                        UCk_Utils_ProceduralAction_UE::Get_State(InvalidAction),
                        ECk_ProceduralAction_State::Finished);
                    TestEqual(TEXT("Invalid loaded content reports compiler failure"),
                        UCk_Utils_ProceduralAction_UE::Get_Result(InvalidAction).Get_Outcome(),
                        ECk_ProceduralAction_Outcome::InvalidContent);
                    TestTrue(TEXT("Invalid loaded content retains a terminal stream"),
                        UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(State->Player,
                            UCk_Utils_ProceduralAction_UE::Get_Identity(InvalidAction), 0, 8)
                            .Get_Terminal());
                }
            }
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionClipOwnerLossTest,
    "Ck.ProceduralAnimation.Action.Clip.OwnerLossClosesOpenWindowBeforeReturn",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionClipOwnerLossTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
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
            TestTrue(TEXT("Owner-loss subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Owner-loss entry settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto PlayerSpec = FCk_ProceduralActionPlayer_Spec{};
            auto Tuning = PlayerSpec.Get_Tuning();
            auto Playback = Tuning.Get_Playback();
            Playback.Set_PlayRate(0.1f);
            Tuning.Set_Playback(Playback);
            PlayerSpec.Set_Tuning(Tuning);
            auto Capacity = PlayerSpec.Get_Capacity();
            Capacity.Set_RecordsPerInstance(512);
            PlayerSpec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, PlayerSpec);
            TestTrue(TEXT("Owner-loss player attaches"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope,
                State->Player, FCk_ProceduralAction_PlaySpec{
                    TSoftObjectPtr<UCk_ProceduralAction_Data>{ClipPath}});
            TestTrue(TEXT("Owner-loss clip reserves"), ck::IsValid(State->Action));
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (ck::Is_NOT_Valid(State->Action)) { return false; }
            const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action);
            if (NOT Receipt.Get_HasAcceptedPose()) { return false; }
            const auto& Interval = Receipt.Get_Interval();
            if (NOT Interval.Get_Windows().Contains(FName{TEXT("WindowA")}))
            { return false; }
            if (NOT State->DestroyRequestedWithOpenWindow)
            {
                State->DestroyRequestedWithOpenWindow = true;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Scope);
            }
            return true;
        }), WaitSeconds, TEXT("Window A opens before scope destruction")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->Identity, 0, 64).Get_Terminal();
        }), WaitSeconds, TEXT("Owner-loss return settles and terminal persists")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Cursor = int64{0};
            auto ClosureIndex = int32{-1};
            auto TerminalIndex = int32{-1};
            auto RecordIndex = 0;
            auto ClosedA = false;
            auto TerminalResult = FCk_ProceduralAction_Result{};
            for (auto PageIndex = 0; PageIndex < 16; ++PageIndex)
            {
                const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->Identity, Cursor, 64);
                for (const auto& Record : Page.Get_Records())
                {
                    if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::WindowsClosed)
                    {
                        ClosureIndex = RecordIndex;
                        ClosedA |= Record.Get_ClosedWindowIds().Contains(FName{TEXT("WindowA")});
                    }
                    if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal)
                    {
                        TerminalIndex = RecordIndex;
                        TerminalResult = Record.Get_Result();
                    }
                    ++RecordIndex;
                }
                Cursor = Page.Get_LastStreamSequence();
                if (NOT Page.Get_HasMore()) { break; }
            }
            TestTrue(TEXT("Open window is explicitly closed before terminal"),
                State->DestroyRequestedWithOpenWindow && ClosedA
                    && ClosureIndex >= 0 && TerminalIndex > ClosureIndex);
            TestEqual(TEXT("Destroyed scope records OwnerLost"), TerminalResult.Get_Outcome(),
                ECk_ProceduralAction_Outcome::OwnerLost);
            TestEqual(TEXT("Return settles after owner loss"), TerminalResult.Get_Recovery(),
                ECk_ProceduralAction_Recovery::Succeeded);
            const auto Retained = UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(
                State->Player);
            TestTrue(TEXT("Terminal stream discoverable after action destruction"),
                Retained.Num() == 1 && Retained[0].Get_Value() == State->Identity.Get_Value());
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionDisjointClipsBatchTest,
    "Ck.ProceduralAnimation.Action.Clip.DisjointClipsShareSettledBatch",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionDisjointClipsBatchTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    struct FDisjointState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle_ProceduralAction First;
        FCk_Handle_ProceduralAction Second;
        FCk_ProceduralAction_Identity FirstIdentity;
        FCk_ProceduralAction_Identity SecondIdentity;
        FCk_ProceduralAction_Receipt JointFirstReceipt;
        FCk_ProceduralAction_Receipt JointSecondReceipt;
    };
    const auto State = MakeShared<FDisjointState>();
    const auto FirstLimb = FName{TEXT("FirstArm")};
    const auto SecondLimb = FName{TEXT("SecondArm")};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, FirstLimb, SecondLimb](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
            auto FirstPartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto FirstPart = UCk_Utils_Transform_UE::Add(FirstPartEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            auto SecondPartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto SecondPart = UCk_Utils_Transform_UE::Add(SecondPartEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto FirstLayout = FCk_ProceduralLimb_Layout{FirstLimb, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            const auto SecondLayout = FCk_ProceduralLimb_Layout{SecondLimb,
                FTransform{FQuat::Identity, FVector{0.0, 100.0, 0.0}},
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            const auto Layout = FCk_ProceduralArticulation_Layout{
                TArray<FCk_ProceduralLimb_Layout>{FirstLayout, SecondLayout}};
            const auto Bindings = TArray<FCk_ProceduralLimb_Binding>{
                FCk_ProceduralLimb_Binding{FirstLimb, TArray<FCk_Handle_Transform>{FirstPart}},
                FCk_ProceduralLimb_Binding{SecondLimb, TArray<FCk_Handle_Transform>{SecondPart}}};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(
                State->Root, FCk_ProceduralArticulation_Spec{Layout, Bindings});
            TestTrue(TEXT("Two-arm subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Two-arm subject has a settled entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, FirstLimb, SecondLimb](UWorld*)
        {
            auto PlayerSpec = FCk_ProceduralActionPlayer_Spec{};
            auto Tuning = PlayerSpec.Get_Tuning();
            auto Playback = Tuning.Get_Playback();
            Playback.Set_PlayRate(0.1f);
            Tuning.Set_Playback(Playback);
            PlayerSpec.Set_Tuning(Tuning);
            auto Capacity = PlayerSpec.Get_Capacity();
            Capacity.Set_RecordsPerInstance(256);
            PlayerSpec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, PlayerSpec);
            TestTrue(TEXT("Two-arm player attaches"), ck::IsValid(State->Player));
            const auto PlayNod = [this, State](FName InLimb) -> FCk_Handle_ProceduralAction
            {
                auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
                auto Spec = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{LimbClipPath}};
                Spec.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                    FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, InLimb}});
                const auto Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(Scope, State->Player, Spec);
                TestTrue(TEXT("Disjoint clip action reserves"), ck::IsValid(Action));
                return Action;
            };
            State->First = PlayNod(FirstLimb);
            State->Second = PlayNod(SecondLimb);
            State->FirstIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->First);
            State->SecondIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Second);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto FirstReceipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->First);
            const auto SecondReceipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Second);
            if (NOT FirstReceipt.Get_HasAcceptedPose() || NOT SecondReceipt.Get_HasAcceptedPose()
                || FirstReceipt.Get_Stamp().Get_SubjectPoseRevision()
                    != SecondReceipt.Get_Stamp().Get_SubjectPoseRevision())
            { return false; }
            State->JointFirstReceipt = FirstReceipt;
            State->JointSecondReceipt = SecondReceipt;
            return true;
        }), WaitSeconds, TEXT("Both clips jointly settle an accepted batch")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, FirstLimb, SecondLimb](UWorld*)
        {
            const auto& FirstReceipt = State->JointFirstReceipt;
            const auto& SecondReceipt = State->JointSecondReceipt;
            TestEqual(TEXT("Joint batch stamps one settled subject revision"),
                FirstReceipt.Get_Stamp().Get_SubjectPoseRevision(),
                SecondReceipt.Get_Stamp().Get_SubjectPoseRevision());
            TestTrue(TEXT("First receipt contains only its claimed limb"),
                FirstReceipt.Get_CurrentPose().Get_Limbs().Num() == 1
                    && FirstReceipt.Get_CurrentPose().Get_Limbs()[0].Get_Id() == FirstLimb);
            TestTrue(TEXT("Second receipt contains only its claimed limb"),
                SecondReceipt.Get_CurrentPose().Get_Limbs().Num() == 1
                    && SecondReceipt.Get_CurrentPose().Get_Limbs()[0].Get_Id() == SecondLimb);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->FirstIdentity, 0, 8).Get_Terminal()
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->SecondIdentity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Both disjoint clips finish and return")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("First clip completes"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->First).Get_Outcome(),
                ECk_ProceduralAction_Outcome::Completed);
            TestEqual(TEXT("Second clip completes"),
                UCk_Utils_ProceduralAction_UE::Get_Result(State->Second).Get_Outcome(),
                ECk_ProceduralAction_Outcome::Completed);
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionWalkingLimbWithoutSupportTest,
    "Ck.ProceduralAnimation.Action.Clip.WalkingLimbWithoutSupportPolicyIsRejected",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionWalkingLimbWithoutSupportTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    using namespace ck_test_procedural_walker_fixture;
    struct FWalkerState
    {
        FFloor Floor;
        FWalker Walker;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle_ProceduralAction Action;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
    };
    const auto State = MakeShared<FWalkerState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { State->Floor = AddFloor(InWorld); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsFloorReady(State->Floor); }),
        WaitSeconds, TEXT("Walker floor is present in Jolt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld* InWorld) { State->Walker = AddWalker(InWorld, TArray<FName>{LegId(0)}); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsWalkerSettled(State->Walker); }),
        WaitSeconds, TEXT("Walker settles over four planted feet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Spec = FCk_ProceduralActionPlayer_Spec{};
            TestEqual(TEXT("The default locomotion policy borrows no walking limb"),
                Spec.Get_Locomotion().Get_Support().Get_Mode(), ECk_ProceduralLimbControl_SupportMode::Disabled);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject, Spec);
            TestTrue(TEXT("Default player composes on the walker"), ck::IsValid(State->Player));
            auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            State->Clip = ck_test_procedural_action_clip_fixture::MakeNodClip(TArray<FName>{LegId(0)}, FCk_Time{0.5});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(Scope, State->Player,
                ck_test_procedural_action_clip_fixture::MakePlay(State->Clip.Get(), TArray<FName>{LegId(0)}));
            TestTrue(TEXT("The walking-limb clip reserves an action"), ck::IsValid(State->Action));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("The walking-limb clip finishes at admission")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->Action);
            TestEqual(TEXT("A walking limb without a support policy is rejected"), Result.Get_Outcome(),
                ECk_ProceduralAction_Outcome::Rejected);
            TestEqual(TEXT("A rejection at admission needs no recovery"), Result.Get_Recovery(),
                ECk_ProceduralAction_Recovery::NotRequired);
            TestEqual(TEXT("The rejection names the missing support"), Result.Get_Reason(),
                FName{TEXT("SupportUnavailable")});
            TestTrue(TEXT("No accepted pose was published"),
                NOT UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action).Get_HasAcceptedPose());
            const auto Control = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(State->Walker.Subject, LegId(0));
            TestEqual(TEXT("The leg stays under the gait"), Control.Get_State(), ECk_ProceduralLimbControl_State::Default);
            TestTrue(TEXT("The leg keeps participating in the gait"), Control.Get_Participating());
            TestEqual(TEXT("The walker keeps four planted feet"),
                UCk_Utils_ProceduralGait_UE::Get_PlantedCount(State->Walker.Gait), 4);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Walker.Root);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor.Entity);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
