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

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "String/LexFromString.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

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
    FCkProceduralActionPlayOptionsAccessorsTest,
    "Ck.ProceduralAnimation.Action.Clip.PlayOptionsAccessors",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionPlayOptionsAccessorsTest::RunTest(
    const FString&)
    -> bool
{
    const auto Slots = TArray<FCk_ProceduralAction_SlotBinding>{
        FCk_ProceduralAction_SlotBinding{TEXT("Nod"), TEXT("NodArm")}};
    const auto Anchors = TArray<FCk_ProceduralAction_AnchorBinding>{
        FCk_ProceduralAction_AnchorBinding{TEXT("Inspection"), FTransform::Identity}};
    auto Tuning = FCk_ProceduralAction_Tuning{};
    auto Playback = Tuning.Get_Playback();
    Playback.Set_PlayRate(1.25f);
    Tuning.Set_Playback(Playback);

    auto Soft = FCk_ProceduralAction_PlaySpec{};
    auto* SoftFluent = &Soft.Set_SlotBindings(Slots).Set_AnchorBindings(Anchors).Set_Tuning(Tuning);
    TestTrue(TEXT("Soft play setters retain the concrete fluent type"), SoftFluent == &Soft);
    TestEqual(TEXT("Soft play stores named bindings"), Soft.Get_SlotBindings().Num(), 1);
    TestEqual(TEXT("Soft play stores named anchors"), Soft.Get_AnchorBindings().Num(), 1);
    TestEqual(TEXT("Setting tuning selects Override"), Soft.Get_TuningMode(),
        ECk_ProceduralAction_TuningMode::Override);
    TestTrue(TEXT("Selected tuning remains coherent"), Soft.Get_HasConsistentTuningSelection());
    Soft.Set_TuningMode(ECk_ProceduralAction_TuningMode::PlayerDefault);
    TestEqual(TEXT("Returning to PlayerDefault clears inactive tuning"),
        Soft.Get_Tuning().Get_Playback().Get_PlayRate(), 1.0f);

    auto Loaded = FCk_ProceduralAction_LoadedPlaySpec{};
    auto* LoadedFluent = &Loaded.Set_SlotBindings(Slots).Set_AnchorBindings(Anchors).Set_Tuning(Tuning);
    TestTrue(TEXT("Loaded play setters retain the concrete fluent type"), LoadedFluent == &Loaded);
    TestEqual(TEXT("Loaded play stores named bindings"), Loaded.Get_SlotBindings().Num(), 1);
    TestEqual(TEXT("Loaded play stores named anchors"), Loaded.Get_AnchorBindings().Num(), 1);
    TestEqual(TEXT("Loaded tuning selects Override"), Loaded.Get_TuningMode(),
        ECk_ProceduralAction_TuningMode::Override);
    Loaded.Set_TuningMode(ECk_ProceduralAction_TuningMode::PlayerDefault);
    TestTrue(TEXT("Loaded PlayerDefault is coherent"), Loaded.Get_HasConsistentTuningSelection());

    auto Inconsistent = FCk_ProceduralAction_PlaySpec{};
    auto* TuningProperty = FindFProperty<FStructProperty>(
        FCk_ProceduralAction_PlayOptions::StaticStruct(), TEXT("_Tuning"));
    if (NOT TestNotNull(TEXT("Blueprint tuning property exists"), TuningProperty)) { return false; }
    *TuningProperty->ContainerPtrToValuePtr<FCk_ProceduralAction_Tuning>(&Inconsistent) = Tuning;
    TestFalse(TEXT("Direct reflected edit cannot leave ignored inactive tuning unnoticed"),
        Inconsistent.Get_HasConsistentTuningSelection());

    const auto DefaultTuning = FCk_ProceduralAction_Tuning{};
    const auto DefaultSeconds = DefaultTuning.Get_Sampling().Get_MaxAdvance().Get_Seconds();
    const auto DefaultText = LexToString(DefaultSeconds);
    auto BlueprintSeconds = double{};
    LexFromString(BlueprintSeconds, FStringView{*DefaultText, DefaultText.Len()});
    TestTrue(TEXT("Blueprint text roundtrip exercises a rounded default"),
        BlueprintSeconds != DefaultSeconds);

    auto RoundedTuning = DefaultTuning;
    auto RoundedSampling = RoundedTuning.Get_Sampling();
    RoundedSampling.Set_MaxAdvance(FCk_Time{BlueprintSeconds});
    RoundedTuning.Set_Sampling(RoundedSampling);
    auto Rounded = FCk_ProceduralAction_LoadedPlaySpec{};
    *TuningProperty->ContainerPtrToValuePtr<FCk_ProceduralAction_Tuning>(&Rounded) = RoundedTuning;
    TestTrue(TEXT("Untouched Blueprint PlayerDefault accepts only its text-rounded default"),
        Rounded.Get_HasConsistentTuningSelection());

    RoundedSampling.Set_MaxAdvance(FCk_Time{0.02});
    RoundedTuning.Set_Sampling(RoundedSampling);
    *TuningProperty->ContainerPtrToValuePtr<FCk_ProceduralAction_Tuning>(&Rounded) = RoundedTuning;
    TestFalse(TEXT("A modified sampling value still requires explicit Override"),
        Rounded.Get_HasConsistentTuningSelection());
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
            const auto Params = ck::FProceduralActionClipParams{
                TSoftObjectPtr<UCk_ProceduralAction_Data>{Sparse},
                TArray<FCk_ProceduralAction_SlotBinding>{
                    FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, LimbId}},
                TArray<FCk_ProceduralAction_AnchorBinding>{}};
            auto Initial = ck::procedural_action::FInitialPose{};
            if (NOT TestTrue(TEXT("Runtime initial capture resolves"),
                ck_procedural_action_player_service::CaptureClipInitial(
                    State->Subject, Params, CompiledFragment, Entry, Default, Initial)))
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
            TestTrue(TEXT("Existing soft clip enters pending load before service"),
                ck::IsValid(State->CancelAction)
                    && UCk_Utils_ProceduralAction_UE::Get_State(State->CancelAction)
                        == ECk_ProceduralAction_State::PendingLoad);
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
            const auto Admission = UCk_Utils_ProceduralActionPlayer_UE::Get_AdmissionReport(
                State->Player, FCk_ProceduralAction_PlaySpec{
                    TSoftObjectPtr<UCk_ProceduralAction_Data>{Source}});
            TestEqual(TEXT("Resident body clip passes advisory static admission"),
                Admission.Get_Status(), ECk_ProceduralAction_ReportStatus::Valid);
            TestTrue(TEXT("Admission report cannot reserve or promise dynamic claims"),
                Admission.Get_Advisory());
            auto InactiveTuning = FCk_ProceduralAction_PlaySpec{
                TSoftObjectPtr<UCk_ProceduralAction_Data>{Source}};
            auto* TuningProperty = FindFProperty<FStructProperty>(
                FCk_ProceduralAction_PlayOptions::StaticStruct(), TEXT("_Tuning"));
            if (NOT TestNotNull(TEXT("Reflected play tuning is editable by Blueprint"), TuningProperty))
            { return; }
            auto Ignored = FCk_ProceduralAction_Tuning{};
            auto IgnoredPlayback = Ignored.Get_Playback();
            IgnoredPlayback.Set_PlayRate(1.25f);
            Ignored.Set_Playback(IgnoredPlayback);
            *TuningProperty->ContainerPtrToValuePtr<FCk_ProceduralAction_Tuning>(&InactiveTuning) = Ignored;
            const auto InactiveReport = UCk_Utils_ProceduralActionPlayer_UE::Get_AdmissionReport(
                State->Player, InactiveTuning);
            TestEqual(TEXT("Advisory rejects a Blueprint-authored ignored tuning value"),
                InactiveReport.Get_Status(), ECk_ProceduralAction_ReportStatus::Invalid);
            TestTrue(TEXT("Advisory identifies inactive tuning"),
                InactiveReport.Get_Diagnostics().ContainsByPredicate([](const auto& InDiagnostic)
                { return InDiagnostic.Get_Code() == FName{TEXT("InactiveTuning")}; }));
            auto InvalidBindings = FCk_ProceduralAction_PlaySpec{
                TSoftObjectPtr<UCk_ProceduralAction_Data>{Source}};
            InvalidBindings.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                FCk_ProceduralAction_SlotBinding{FName{TEXT("UnknownSlot")}, FName{TEXT("UnknownLimb")}}});
            const auto BindingReport = UCk_Utils_ProceduralActionPlayer_UE::Get_AdmissionReport(
                State->Player, InvalidBindings);
            TestEqual(TEXT("Advisory rejects mismatched slot bindings"), BindingReport.Get_Status(),
                ECk_ProceduralAction_ReportStatus::Invalid);
            TestTrue(TEXT("Advisory names the binding field"),
                BindingReport.Get_Diagnostics().ContainsByPredicate([](const auto& InDiagnostic)
                { return InDiagnostic.Get_Code() == FName{TEXT("SlotBindingCount")}
                    && InDiagnostic.Get_FieldPath() == TEXT("SlotBindings"); }));
            const auto Missing = UCk_Utils_ProceduralActionPlayer_UE::Get_AdmissionReport(
                State->Player, FCk_ProceduralAction_PlaySpec{
                    TSoftObjectPtr<UCk_ProceduralAction_Data>{FSoftObjectPath{TEXT(
                        "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_NotResident_DA.Ck_ProceduralAction_NotResident_DA")}}});
            TestEqual(TEXT("Advisory query does not load missing content"),
                Missing.Get_Status(), ECk_ProceduralAction_ReportStatus::ContentNotResident);
            auto Mutable = TStrongObjectPtr<UCk_ProceduralAction_Data>{
                DuplicateObject<UCk_ProceduralAction_Data>(Source, GetTransientPackage())};
            if (NOT TestTrue(TEXT("Loaded fixture has a first phase"), NOT Mutable->Get_Phases().IsEmpty()))
            { return; }
            const auto FirstPhaseId = Mutable->Get_Phases()[0].Get_Id();
            Mutable->Set_Windows(TArray<FCk_ProceduralAction_Window>{
                FCk_ProceduralAction_Window{FName{TEXT("TimeZero")},
                    FCk_ProceduralAction_TimeRef{FirstPhaseId, FCk_FloatRange_0to1{0.0}},
                    FCk_ProceduralAction_TimeRef{FirstPhaseId, FCk_FloatRange_0to1{0.37}}}});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(State->Scope,
                State->Player, FCk_ProceduralAction_LoadedPlaySpec{Mutable.Get()});
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
                const auto InvalidAction = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(
                    InvalidScope, State->Player,
                    FCk_ProceduralAction_LoadedPlaySpec{InvalidClip.Get()});
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
    FCkProceduralActionMixedLimbBatchTest,
    "Ck.ProceduralAnimation.Action.Clip.MixedManualAndClipShareSettledBatch",
    ck_test_procedural_action_clip::TestFlags)

auto
FCkProceduralActionMixedLimbBatchTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clip;
    struct FMixedState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle ClipScope;
        FCk_Handle ManualScope;
        FCk_Handle_ProceduralAction Clip;
        FCk_Handle_ProceduralAction Manual;
        FCk_ProceduralAction_Identity ClipIdentity;
        FCk_ProceduralAction_Identity ManualIdentity;
        FCk_ProceduralAction_Receipt JointClipReceipt;
        FCk_ProceduralAction_Receipt JointManualReceipt;
    };
    const auto State = MakeShared<FMixedState>();
    const auto ClipLimb = FName{TEXT("ManualArm")};
    const auto ManualLimb = FName{TEXT("OtherArm")};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, ClipLimb, ManualLimb](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose,
                ECk_Replication::DoesNotReplicate);
            auto ClipPartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto ClipPart = UCk_Utils_Transform_UE::Add(ClipPartEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            auto ManualPartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto ManualPart = UCk_Utils_Transform_UE::Add(ManualPartEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto ClipLayout = FCk_ProceduralLimb_Layout{ClipLimb, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            const auto ManualLayout = FCk_ProceduralLimb_Layout{ManualLimb,
                FTransform{FQuat::Identity, FVector{0.0, 100.0, 0.0}},
                TArray<float>{100.0f}, FTransform{FQuat::Identity, FVector{100.0, 0.0, 0.0}},
                FVector::UpVector};
            const auto Layout = FCk_ProceduralArticulation_Layout{
                TArray<FCk_ProceduralLimb_Layout>{ClipLayout, ManualLayout}};
            const auto Bindings = TArray<FCk_ProceduralLimb_Binding>{
                FCk_ProceduralLimb_Binding{ClipLimb, TArray<FCk_Handle_Transform>{ClipPart}},
                FCk_ProceduralLimb_Binding{ManualLimb, TArray<FCk_Handle_Transform>{ManualPart}}};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(
                State->Root, FCk_ProceduralArticulation_Spec{Layout, Bindings});
            TestTrue(TEXT("Mixed subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Mixed subject has a settled entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, ClipLimb, ManualLimb](UWorld*)
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
            TestTrue(TEXT("Mixed player attaches"), ck::IsValid(State->Player));
            State->ManualScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Control = FCk_ProceduralAction_ControlSpec{TArray<FName>{ManualLimb}};
            Control.Set_InputTimeout(FCk_Time{30.0});
            State->Manual = UCk_Utils_ProceduralAction_UE::Create_Manual(
                State->ManualScope, State->Player, Control);
            State->ManualIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Manual);
            TestTrue(TEXT("Manual action reserves before clip load"), ck::IsValid(State->Manual));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Manual)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Manual)
                    == ECk_ProceduralAction_State::Ready;
        }), WaitSeconds, TEXT("Manual claim is ready before the managed clip begins")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, ClipLimb](UWorld*)
        {
            State->ClipScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            auto Play = FCk_ProceduralAction_PlaySpec{
                TSoftObjectPtr<UCk_ProceduralAction_Data>{LimbClipPath}};
            Play.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, ClipLimb}});
            State->Clip = UCk_Utils_ProceduralAction_UE::Create_FromClip(
                State->ClipScope, State->Player, Play);
            State->ClipIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Clip);
            TestTrue(TEXT("Disjoint clip action reserves"), ck::IsValid(State->Clip));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Manual) && ck::IsValid(State->Clip)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Manual)
                    == ECk_ProceduralAction_State::Ready
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Clip)
                    == ECk_ProceduralAction_State::Playing;
        }), WaitSeconds, TEXT("Disjoint claims acquired and clip playback started")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State, ClipLimb, ManualLimb](UWorld*)
        {
            const auto ManualState = UCk_Utils_ProceduralAction_UE::Get_State(State->Manual);
            const auto ClipState = UCk_Utils_ProceduralAction_UE::Get_State(State->Clip);
            const auto ManualResult = UCk_Utils_ProceduralAction_UE::Get_Result(State->Manual);
            const auto ClipResult = UCk_Utils_ProceduralAction_UE::Get_Result(State->Clip);
            const auto ManualPage = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->ManualIdentity, 0, 8);
            const auto ClipPage = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->ClipIdentity, 0, 8);
            const auto ManualClaimant = FCk_ProceduralArticulation_Control::GetClaimant(
                State->Subject, ManualLimb, false);
            const auto ClipClaimant = FCk_ProceduralArticulation_Control::GetClaimant(
                State->Subject, ClipLimb, false);
            auto ClipRecordKinds = FString{};
            for (const auto& Record : ClipPage.Get_Records())
            { ClipRecordKinds += FString::Printf(TEXT("%d,"), static_cast<int32>(Record.Get_Kind())); }
            UE_LOG(LogTemp, Display, TEXT("[MixedClipDiagnostic] manualState=%d manualOutcome=%d manualReason=%s manualRecords=%d manualTerminal=%d manualClaim=%lld clipState=%d clipOutcome=%d clipReason=%s clipRead=%d clipRecords=%d clipKinds=%s clipTerminal=%d clipClaim=%lld clipAccepted=%d"),
                static_cast<int32>(ManualState), static_cast<int32>(ManualResult.Get_Outcome()),
                *ManualResult.Get_Reason().ToString(), ManualPage.Get_Records().Num(),
                ManualPage.Get_Terminal() ? 1 : 0,
                static_cast<int64>(ManualClaimant.Get_InstanceId()), static_cast<int32>(ClipState),
                static_cast<int32>(ClipResult.Get_Outcome()), *ClipResult.Get_Reason().ToString(),
                static_cast<int32>(ClipPage.Get_Result()), ClipPage.Get_Records().Num(),
                *ClipRecordKinds, ClipPage.Get_Terminal() ? 1 : 0,
                static_cast<int64>(ClipClaimant.Get_InstanceId()),
                UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Clip).Get_HasAcceptedPose() ? 1 : 0);
            const auto Radians = FMath::DegreesToRadians(1.0f);
            auto Target = FCk_ProceduralAction_LimbTarget{ManualLimb,
                FTransform{FQuat{FVector::UpVector, Radians},
                    FVector{FMath::Cos(Radians) - 1.0f, FMath::Sin(Radians), 0.0f}}};
            Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            auto Batch = FCk_ProceduralAction_PoseBatch{};
            Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
            UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(State->Manual,
                FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            const auto ManualReceipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Manual);
            const auto ClipReceipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Clip);
            if (NOT ManualReceipt.Get_HasAcceptedPose() || NOT ClipReceipt.Get_HasAcceptedPose()
                || ManualReceipt.Get_Stamp().Get_SubjectPoseRevision()
                    != ClipReceipt.Get_Stamp().Get_SubjectPoseRevision())
            { return false; }
            State->JointManualReceipt = ManualReceipt;
            State->JointClipReceipt = ClipReceipt;
            return true;
        }), WaitSeconds, TEXT("Manual and clip jointly settle an accepted batch")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, ClipLimb, ManualLimb](UWorld*)
        {
            const auto& ManualReceipt = State->JointManualReceipt;
            const auto& ClipReceipt = State->JointClipReceipt;
            TestTrue(TEXT("Clip and manual have actual receipts"),
                ManualReceipt.Get_HasAcceptedPose() && ClipReceipt.Get_HasAcceptedPose());
            if (ManualReceipt.Get_HasAcceptedPose() && ClipReceipt.Get_HasAcceptedPose())
            {
                TestEqual(TEXT("Joint batch stamps one settled subject revision"),
                    ManualReceipt.Get_Stamp().Get_SubjectPoseRevision(),
                    ClipReceipt.Get_Stamp().Get_SubjectPoseRevision());
                TestTrue(TEXT("Manual receipt contains only its claimed limb"),
                    ManualReceipt.Get_CurrentPose().Get_Limbs().Num() == 1
                        && ManualReceipt.Get_CurrentPose().Get_Limbs()[0].Get_Id() == ManualLimb);
                TestTrue(TEXT("Clip receipt contains only its claimed limb"),
                    ClipReceipt.Get_CurrentPose().Get_Limbs().Num() == 1
                        && ClipReceipt.Get_CurrentPose().Get_Limbs()[0].Get_Id() == ClipLimb);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->ClipIdentity, 0, 64).Get_Terminal();
        }), WaitSeconds, TEXT("Mixed clip finishes and returns while manual claim remains")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("Manual action remains ready after disjoint clip return"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Manual),
                ECk_ProceduralAction_State::Ready);
            UCk_Utils_ProceduralAction_UE::Request_End(State->Manual,
                FCk_Request_ProceduralAction_End{}, {});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->ManualIdentity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Manual return finishes after mixed clip")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
