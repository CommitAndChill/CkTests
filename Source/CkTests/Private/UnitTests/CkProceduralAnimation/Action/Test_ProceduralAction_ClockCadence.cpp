#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkTest_ProceduralActionClipFixture.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace ck_test_procedural_action_clock
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto ClipPath = FSoftObjectPath{TEXT(
        "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA")};

    struct FCase
    {
        const TCHAR* Name;
        double DeltaSeconds;
        double InjectedHitchSeconds;
    };
    constexpr FCase Cases[] = {
        {TEXT("30Hz"), 1.0 / 30.0, 0.0},
        {TEXT("60Hz"), 1.0 / 60.0, 0.0},
        {TEXT("120Hz"), 1.0 / 120.0, 0.0},
        {TEXT("100msHitch"), 1.0 / 60.0, 0.1},
        {TEXT("250msHitch"), 1.0 / 60.0, 0.25}};

    struct FState
    {
        const bool OriginalFixed = FApp::UseFixedTimeStep();
        const double OriginalDelta = FApp::GetFixedDeltaTime();
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        float ObservedHitchWorldDelta = -1.0f;
        double ObservedHitchAppDelta = -1.0;
        bool SawHitchWorldTick = false;

        ~FState()
        {
            FApp::SetFixedDeltaTime(OriginalDelta);
            FApp::SetUseFixedTimeStep(OriginalFixed);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionClockCadenceTest,
    "Ck.ProceduralAnimation.Action.Clip.AcceptedClockCadenceAndHitches",
    ck_test_procedural_action_clock::TestFlags)

auto
FCkProceduralActionClockCadenceTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clock;
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity,
                FTransform{FVector{100.0, 200.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
            auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
            Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
            Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
            auto Spec = FCk_ProceduralArticulation_Spec{Layout,
                TArray<FCk_ProceduralLimb_Binding>{}};
            Spec.Set_Presentation(Presentation);
            auto Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            auto PlayerSpec = FCk_ProceduralActionPlayer_Spec{};
            auto Capacity = PlayerSpec.Get_Capacity();
            Capacity.Set_RecordsPerInstance(512);
            PlayerSpec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(Subject, PlayerSpec);
            TestTrue(TEXT("Cadence player attaches"), ck::IsValid(State->Player));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Player); }), WaitSeconds, TEXT("Cadence player ready")));

    for (const auto& Case : Cases)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, State, Case](UWorld*)
            {
                FApp::SetFixedDeltaTime(Case.DeltaSeconds);
                FApp::SetUseFixedTimeStep(true);
                State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
                auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr,
                    *ClipPath.ToString());
                if (NOT TestNotNull(TEXT("BodyPulse source loads for narrow-window cadence"),
                    Source))
                { return; }
                auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
                    DuplicateObject<UCk_ProceduralAction_Data>(Source, GetTransientPackage())};
                if (NOT TestNotNull(TEXT("Cadence clip duplicates"), Clip.Get()))
                { return; }
                auto Windows = Clip->Get_Windows();
                Windows.Add(FCk_ProceduralAction_Window{FName{TEXT("HitchNarrow")},
                    FCk_ProceduralAction_TimeRef{FName{TEXT("Windup")}, FCk_FloatRange_0to1{0.25}},
                    FCk_ProceduralAction_TimeRef{FName{TEXT("Windup")}, FCk_FloatRange_0to1{0.254}}});
                Clip->Set_Windows(MoveTemp(Windows));
                State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                    FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{Clip.Get()}}.Set_Delivery(ECk_ProceduralAction_Delivery::Explicit));
                State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
                TestTrue(FString::Printf(TEXT("%s action reserves"), Case.Name),
                    ck::IsValid(State->Action));
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                return ck::IsValid(State->Action)
                    && UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Action)
                        .Get_HasAcceptedPose();
            }), WaitSeconds, TEXT("Cadence action publishes first accepted pose")));
        if (Case.InjectedHitchSeconds > 0.0)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
                [State, Case](UWorld*)
                {
                    State->SawHitchWorldTick = false;
                    State->ObservedHitchWorldDelta = -1.0f;
                    State->ObservedHitchAppDelta = -1.0;
                    FApp::SetFixedDeltaTime(Case.InjectedHitchSeconds);
                })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                FCk_NetAutoTest_Condition::CreateLambda([State, Case]
                {
                    auto* World = ck::auto_test::net::Get_ServerWorld();
                    if (World == nullptr) { return false; }
                    State->ObservedHitchWorldDelta = World->GetDeltaSeconds();
                    State->ObservedHitchAppDelta = FApp::GetDeltaTime();
                    State->SawHitchWorldTick = FMath::IsNearlyEqual(
                        State->ObservedHitchWorldDelta,
                        static_cast<float>(Case.InjectedHitchSeconds), 1.0e-4f);
                    return State->SawHitchWorldTick;
                }), 2.0, TEXT("Injected hitch reaches a completed production world tick")));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, State, Case](UWorld*)
                {
                    TestTrue(FString::Printf(TEXT("%s hitch %.3fs reaches production world (observed world %.6fs, FApp %.6fs)"),
                        Case.Name, Case.InjectedHitchSeconds,
                        State->ObservedHitchWorldDelta, State->ObservedHitchAppDelta),
                        State->SawHitchWorldTick);
                    FApp::SetFixedDeltaTime(Case.DeltaSeconds);
                })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                return ck::IsValid(State->Player)
                    && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                        State->Player, State->Identity, 0, 64).Get_Terminal();
            }), WaitSeconds, TEXT("Cadence action returns and terminates")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, State, Case](UWorld*)
            {
                auto Cursor = int64{0};
                auto Accepted = 0;
                auto BoundaryOrder = TArray<ECk_ProceduralAction_EventKind>{};
                auto NarrowOrder = TArray<ECk_ProceduralAction_EventKind>{};
                auto Terminal = ECk_ProceduralAction_Outcome::None;
                for (auto PageIndex = 0; PageIndex < 32; ++PageIndex)
                {
                    const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                        State->Player, State->Identity, Cursor, 64);
                    for (const auto& Record : Page.Get_Records())
                    {
                        if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal)
                        { Terminal = Record.Get_Result().Get_Outcome(); }
                        if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose)
                        { continue; }
                        ++Accepted;
                        const auto& Interval = Record.Get_Receipt().Get_Interval();
                        TestTrue(FString::Printf(TEXT("%s accepted interval has ordered times"), Case.Name),
                            Interval.Get_FromTime() <= Interval.Get_ToTime());
                        for (const auto& Event : Interval.Get_Events())
                        {
                            if (FMath::IsNearlyEqual(Event.Get_Time().Get_Seconds(),
                                0.2, 1.0e-5))
                            { BoundaryOrder.Add(Event.Get_Kind()); }
                            if (Event.Get_Id() == FName{TEXT("HitchNarrow")})
                            { NarrowOrder.Add(Event.Get_Kind()); }
                        }
                    }
                    Cursor = Page.Get_LastStreamSequence();
                    if (NOT Page.Get_HasMore()) { break; }
                }
                TestTrue(FString::Printf(TEXT("%s has accepted runtime geometry"), Case.Name),
                    Accepted > 0);
                TestTrue(FString::Printf(TEXT("%s preserves exit-point-enter order"), Case.Name),
                    BoundaryOrder.Num() == 3
                        && BoundaryOrder[0] == ECk_ProceduralAction_EventKind::WindowExit
                        && BoundaryOrder[1] == ECk_ProceduralAction_EventKind::Point
                        && BoundaryOrder[2] == ECk_ProceduralAction_EventKind::WindowEnter);
                TestTrue(FString::Printf(TEXT("%s preserves sub-millisecond window enter and exit"),
                    Case.Name), NarrowOrder.Num() == 2
                        && NarrowOrder[0] == ECk_ProceduralAction_EventKind::WindowEnter
                        && NarrowOrder[1] == ECk_ProceduralAction_EventKind::WindowExit);
                TestEqual(FString::Printf(TEXT("%s completes after return"), Case.Name),
                    Terminal, ECk_ProceduralAction_Outcome::Completed);
                UCk_Utils_ProceduralActionPlayer_UE::Request_ConsumeReceipts(State->Player,
                    FCk_Request_ProceduralActionPlayer_ConsumeReceipts{State->Identity, Cursor}, {});
                auto Scope = State->Scope;
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Scope);
                FApp::SetFixedDeltaTime(State->OriginalDelta);
                FApp::SetUseFixedTimeStep(State->OriginalFixed);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([State]
            {
                return ck::IsValid(State->Player)
                    && UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(
                        State->Player).IsEmpty();
            }), WaitSeconds, TEXT("Previous cadence stream is consumed and released")));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionReturnSamplingCadenceTest,
    "Ck.ProceduralAnimation.Action.Clip.ReturnSamplingHonorsFrameTimeAndBudget",
    ck_test_procedural_action_clock::TestFlags)

auto
FCkProceduralActionReturnSamplingCadenceTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_clock;
    struct FReturnCase
    {
        FCk_Handle_Transform Root;
        FCk_Handle_Transform Part;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_Handle_ProceduralAction Companion;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UCk_ProceduralAction_Data> CompanionClip;
        FTransform RestPart;
        FVector DisplacedEndpoint = FVector::ZeroVector;
        int64 CompanionSequence = 0;
        bool CompanionPublishedDuringReturn = false;
    };
    struct FReturnState
    {
        const bool OriginalFixed = FApp::UseFixedTimeStep();
        const double OriginalDelta = FApp::GetFixedDeltaTime();
        FReturnCase Cases[3];

        ~FReturnState()
        {
            FApp::SetFixedDeltaTime(OriginalDelta);
            FApp::SetUseFixedTimeStep(OriginalFixed);
            for (auto& Case : Cases)
            {
                if (ck::IsValid(Case.Root))
                {
                    auto Root = Case.Root.ConvertToHandle();
                    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
                }
            }
        }
    };
    const auto State = MakeShared<FReturnState>();
    const auto ReturnArm = FName{TEXT("ReturnArm")};
    const auto CompanionArm = FName{TEXT("CompanionArm")};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, ReturnArm, CompanionArm](UWorld* InWorld)
        {
            FApp::SetFixedDeltaTime(1.0 / 30.0);
            FApp::SetUseFixedTimeStep(true);
            for (auto Index = 0; Index < 3; ++Index)
            {
                auto& Case = State->Cases[Index];
                auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
                Case.Root = UCk_Utils_Transform_UE::Add(RootEntity,
                    FTransform{FVector{Index * 1000.0, 0.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
                auto Layouts = TArray<FCk_ProceduralLimb_Layout>{};
                auto Bindings = TArray<FCk_ProceduralLimb_Binding>{};
                for (const auto Id : {ReturnArm, CompanionArm})
                {
                    auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Case.Root);
                    const auto Part = UCk_Utils_Transform_UE::Add(PartEntity,
                        FTransform::Identity, ECk_Replication::DoesNotReplicate);
                    if (Id == ReturnArm) { Case.Part = Part; }
                    Layouts.Emplace(Id, FTransform{FVector{0.0, Id == ReturnArm ? 0.0 : 200.0, 0.0}},
                        TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector);
                    Bindings.Emplace(Id, TArray<FCk_Handle_Transform>{Part});
                }
                Case.Subject = UCk_Utils_ProceduralArticulation_UE::Add(Case.Root,
                    FCk_ProceduralArticulation_Spec{FCk_ProceduralArticulation_Layout{Layouts}, Bindings});
                TestTrue(FString::Printf(TEXT("Return case %d composes a real two-arm subject"), Index),
                    ck::IsValid(Case.Subject));
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            for (const auto& Case : State->Cases)
            {
                if (ck::Is_NOT_Valid(Case.Subject)
                    || UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(Case.Subject) <= 0)
                { return false; }
            }
            return true;
        }), WaitSeconds, TEXT("Return sampling subjects publish settled rest poses")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State, ReturnArm, CompanionArm](UWorld* InWorld)
        {
            TestTrue(TEXT("Return regression runs at a serviced 30Hz production world tick"),
                FMath::IsNearlyEqual(InWorld->GetDeltaSeconds(), 1.0f / 30.0f, 1.0e-4f));
            for (auto Index = 0; Index < 3; ++Index)
            {
                auto& Case = State->Cases[Index];
                Case.RestPart = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Case.Part);
                auto Spec = FCk_ProceduralActionPlayer_Spec{};
                auto Tuning = Spec.Get_Tuning();
                if (Index == 2)
                {
                    auto Transition = Tuning.Get_Transition();
                    Transition.Set_EntryBlend(FCk_Time{1.0 / 960.0});
                    Tuning.Set_Transition(Transition);
                }
                TestTrue(TEXT("Return duration remains 0.2s and its original deadline remains 1s"),
                    Tuning.Get_Transition().Get_DedicatedReturn() == FCk_Time{0.2}
                        && Tuning.Get_Transition().Get_ReturnTimeout() == FCk_Time{1.0});
                auto Sampling = Tuning.Get_Sampling();
                Sampling.Set_MaxAdvance(FCk_Time{1.0 / 960.0});
                Sampling.Set_MaxSamples(Index == 2 ? 1 : 64);
                Tuning.Set_Sampling(Sampling);
                Spec.Set_Tuning(Tuning);
                auto Capacity = Spec.Get_Capacity();
                Capacity.Set_RecordsPerInstance(512);
                Spec.Set_Capacity(Capacity);
                Case.Player = UCk_Utils_ProceduralActionPlayer_UE::Add(Case.Subject, Spec);
                TestTrue(TEXT("Fine-sampled return player attaches"), ck::IsValid(Case.Player));
                Case.Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Case.Root);
                const auto Angle = FMath::DegreesToRadians(Index == 2 ? 1.0 : 90.0);
                const auto Offset = FVector{FMath::Cos(Angle) - 1.0, FMath::Sin(Angle), 0.0};
                Case.Clip = ck_test_procedural_action_clip_fixture::MakeHoldClip(ReturnArm,
                    ECk_ProceduralAction_TrackSpace::LimbRestNormalized, Offset, FCk_Time{4.0});
                Case.DisplacedEndpoint = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Case.Root).TransformPosition(
                    FVector{100.0 * FMath::Cos(Angle), 100.0 * FMath::Sin(Angle), 0.0});
                Case.Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(Case.Scope, Case.Player,
                    ck_test_procedural_action_clip_fixture::MakePlay(Case.Clip.Get(), {ReturnArm}));
                TestTrue(TEXT("Displaced return action reserves"), ck::IsValid(Case.Action));
                if (Index == 1)
                {
                    Case.CompanionClip = ck_test_procedural_action_clip_fixture::MakeNodClip(
                        {CompanionArm}, FCk_Time{4.0});
                    Case.Companion = UCk_Utils_ProceduralAction_UE::Create_FromClip(Case.Scope, Case.Player,
                        ck_test_procedural_action_clip_fixture::MakePlay(Case.CompanionClip.Get(), {CompanionArm}));
                    TestTrue(TEXT("Disjoint fine-sampled companion reserves"), ck::IsValid(Case.Companion));
                }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State, ReturnArm]
        {
            for (const auto& Case : State->Cases)
            {
                if (ck::Is_NOT_Valid(Case.Action)
                    || UCk_Utils_ProceduralAction_UE::Get_State(Case.Action) != ECk_ProceduralAction_State::Playing)
                { return false; }
                const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(Case.Action);
                if (NOT Receipt.Get_HasAcceptedPose()) { return false; }
                const auto* Limb = Receipt.Get_CurrentPose().Get_Limbs().FindByPredicate(
                    [ReturnArm](const auto& InLimb) { return InLimb.Get_Id() == ReturnArm; });
                if (Limb == nullptr || NOT Limb->Get_AcceptedEndpoint().GetLocation().Equals(Case.DisplacedEndpoint, 0.02))
                { return false; }
            }
            if (ck::Is_NOT_Valid(State->Cases[1].Companion)) { return false; }
            const auto Companion = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(State->Cases[1].Companion);
            return Companion.Get_HasAcceptedPose();
        }), WaitSeconds, TEXT("Each return starts only after its authored displaced endpoint is accepted")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            State->Cases[1].CompanionSequence = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(
                State->Cases[1].Companion).Get_Stamp().Get_SampleSequence();
            for (auto& Case : State->Cases)
            {
                TestTrue(TEXT("Displaced endpoint exists in actual bound part before cancellation"),
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Case.Part).TransformPosition(
                        FVector{50.0, 0.0, 0.0}).Equals(Case.DisplacedEndpoint, 0.02));
                UCk_Utils_ProceduralAction_UE::Request_Cancel(Case.Action,
                    FCk_Request_ProceduralAction_Cancel{FName{TEXT("ReturnCadence")}}, {});
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            auto& Joint = State->Cases[1];
            if (ck::IsValid(Joint.Companion) && ck::IsValid(Joint.Action))
            {
                const auto ActionState = UCk_Utils_ProceduralAction_UE::Get_State(Joint.Action);
                if (ActionState == ECk_ProceduralAction_State::Returning
                    || ActionState == ECk_ProceduralAction_State::Cancelling)
                {
                    const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(Joint.Companion);
                    Joint.CompanionPublishedDuringReturn |= Receipt.Get_HasAcceptedPose()
                        && Receipt.Get_Stamp().Get_SampleSequence() > Joint.CompanionSequence;
                }
            }
            for (const auto& Case : State->Cases)
            {
                if (ck::Is_NOT_Valid(Case.Action)
                    || UCk_Utils_ProceduralAction_UE::Get_State(Case.Action) != ECk_ProceduralAction_State::Finished)
                { return false; }
            }
            return true;
        }), WaitSeconds, TEXT("Return sampling reaches each original finite deadline or successful handoff")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            for (auto Index = 0; Index < 2; ++Index)
            {
                const auto& Case = State->Cases[Index];
                const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(Case.Action);
                TestEqual(FString::Printf(TEXT("Return case %d preserves cancellation outcome"), Index),
                    Result.Get_Outcome(), ECk_ProceduralAction_Outcome::Cancelled);
                TestEqual(FString::Printf(TEXT("Return case %d finishes within the unchanged 1s deadline"), Index),
                    Result.Get_Recovery(), ECk_ProceduralAction_Recovery::Succeeded);
                TestTrue(FString::Printf(TEXT("Return case %d actually settles at its original default part pose"), Index),
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Case.Part).Equals(Case.RestPart, 0.02));
            }
            TestTrue(TEXT("Fine-sampled disjoint clip positively publishes during the other arm's return"),
                State->Cases[1].CompanionPublishedDuringReturn);
            const auto BudgetResult = UCk_Utils_ProceduralAction_UE::Get_Result(State->Cases[2].Action);
            TestEqual(TEXT("Sampling-constrained return preserves the original cancellation outcome"),
                BudgetResult.Get_Outcome(), ECk_ProceduralAction_Outcome::Cancelled);
            TestEqual(TEXT("Constrained sampling reports failed recovery rather than bypassing its budget"),
                BudgetResult.Get_Recovery(), ECk_ProceduralAction_Recovery::Failed);
            TestTrue(TEXT("Constrained sampling fails on its unchanged return deadline"),
                BudgetResult.Get_Reason() == FName{TEXT("ReturnTimeout")}
                    || BudgetResult.Get_Reason() == FName{TEXT("ReturnSettlementTimeout")});
            for (auto& Case : State->Cases)
            {
                auto Root = Case.Root.ConvertToHandle();
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
            }
            FApp::SetFixedDeltaTime(State->OriginalDelta);
            FApp::SetUseFixedTimeStep(State->OriginalFixed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            for (const auto& Case : State->Cases)
            {
                if (ck::IsValid(Case.Root.ConvertToHandle(), ck::IsValid_Policy_IncludePendingKill{}))
                { return false; }
            }
            return true;
        }), WaitSeconds, TEXT("Return sampling roots and their owned transients finish destruction")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*)
        {
            for (auto& Case : State->Cases)
            {
                Case.Part = {};
                Case.Subject = {};
                Case.Player = {};
                Case.Scope = {};
                Case.Action = {};
                Case.Companion = {};
                Case.Root = {};
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
