#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

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
                    FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{Clip.Get()}});
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

#endif
