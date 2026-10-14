#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionReceiptStore.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkTest_ProceduralActionClipFixture.h"
#include "CkTest_ProceduralActionReentry.h"
#include "../CkTest_ProceduralWalkerFixture.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_diagnostics
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 20.0;
    const auto ArmId = FName{TEXT("DiagnosticArm")};
    const auto LocomotionReturnTimeout = FCk_Time{0.6};
    constexpr auto RecordsPerInstance = 4;
    // Nothing consumes the borrowed leg's stream, so it needs room for every frame the clip holds and returns.
    constexpr auto HeldRecords = 4096;

    struct FState
    {
        ck_test_procedural_walker_fixture::FFloor Floor;
        ck_test_procedural_walker_fixture::FWalker Walker;
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE> Listener;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
    };

    auto
        Get_AreEqual(
            const FCk_ProceduralAction_FailureContext& InA,
            const FCk_ProceduralAction_FailureContext& InB)
        -> bool
    {
        return FCk_ProceduralAction_FailureContext::StaticStruct()->CompareScriptStruct(&InA, &InB, PPF_None);
    }

    auto
        Get_TerminalRecord(
            const FCk_Handle_ProceduralActionPlayer& InPlayer,
            FCk_ProceduralAction_Identity InIdentity,
            FCk_ProceduralAction_Record& OutRecord)
        -> bool
    {
        // A page may hold at most the per-action budget plus the reserved closure and terminal records.
        const auto PageSize = InPlayer.Get<ck::FFragment_ProceduralActionPlayer_Params>().Get_Capacity()
            .Get_RecordsPerInstance() + 2;
        const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(InPlayer, InIdentity, 0, PageSize);
        const auto* Terminal = Page.Get_Records().FindByPredicate([](const FCk_ProceduralAction_Record& InRecord)
        { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal; });
        if (Terminal == nullptr)
        { return false; }
        OutRecord = *Terminal;
        return true;
    }

    auto
        BindFinished(
            FState& InState)
        -> void
    {
        InState.Listener = TStrongObjectPtr<UCk_Test_ProceduralActionReentry_UE>{
            NewObject<UCk_Test_ProceduralActionReentry_UE>()};
        auto Finished = FCk_Delegate_ProceduralAction_OnFinished{};
        Finished.BindDynamic(InState.Listener.Get(), &UCk_Test_ProceduralActionReentry_UE::OnFinished);
        UCk_Utils_ProceduralAction_UE::BindTo_OnFinished(InState.Action, Finished);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionDiagnosticsReturnTimeoutTest,
    "Ck.ProceduralAnimation.Action.Diagnostics.ReturnTimeoutCarriesElapsedAndLimit",
    ck_test_procedural_action_diagnostics::TestFlags)

auto
    FCkProceduralActionDiagnosticsReturnTimeoutTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_diagnostics;
    using namespace ck_test_procedural_walker_fixture;
    const auto State = MakeShared<FState>();

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
            // A long support hold keeps the floor removal from interrupting the borrow, and a retry interval longer
            // than the deadline keeps probe exhaustion from ending the return first: only the deadline can fail it.
            auto Support = FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
            Support.Set_SupportHoldTimeout(FCk_Time{10.0});
            auto Return = FCk_ProceduralLimbControl_Return{};
            Return.Set_Duration(FCk_Time{0.2});
            Return.Set_Timeout(LocomotionReturnTimeout);
            Return.Set_MaxProbeAttempts(16);
            Return.Set_RetryInterval(FCk_Time{30.0});
            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
            Locomotion.Set_Support(Support);
            Locomotion.Set_Return(Return);
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            Spec.Set_Locomotion(Locomotion);
            auto Capacity = Spec.Get_Capacity();
            Capacity.Set_RecordsPerInstance(HeldRecords);
            Spec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Walker.Subject, Spec);
            TestTrue(TEXT("Borrowing player composes"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Walker.Root);
            State->Clip = ck_test_procedural_action_clip_fixture::MakeNodClip(TArray<FName>{LegId(0)}, FCk_Time{1.5});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                ck_test_procedural_action_clip_fixture::MakePlay(State->Clip.Get(), TArray<FName>{LegId(0)}));
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            BindFinished(*State);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Action)
                && UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Playing;
        }), WaitSeconds, TEXT("Walking limb is borrowed")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*) { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Floor.Entity); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return NOT ck::IsValid(State->Floor.Body); }),
        WaitSeconds, TEXT("Jolt floor removal completes before the clip ends and returns")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestEqual(TEXT("The clip is still playing when the floor is gone"),
                UCk_Utils_ProceduralAction_UE::Get_State(State->Action), ECk_ProceduralAction_State::Playing);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("No-floor return reaches its deadline")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->Action);
            TestEqual(TEXT("A completed action whose return fails reports RecoveryFailed"),
                Result.Get_Outcome(), ECk_ProceduralAction_Outcome::RecoveryFailed);
            TestEqual(TEXT("Recovery failed"), Result.Get_Recovery(), ECk_ProceduralAction_Recovery::Failed);
            const auto TimedOut = Result.Get_Reason() == FName{TEXT("ReturnTimeout")}
                || Result.Get_Reason() == FName{TEXT("ReturnSettlementTimeout")};
            TestTrue(*FString::Printf(TEXT("The return failed on its deadline, reason [%s]"),
                *Result.Get_Reason().ToString()), TimedOut);
            TestEqual(TEXT("OnFinished fires once"), State->Listener->FinishedCount, 1);
            const auto& Context = State->Listener->LastTerminal.Get_Context();
            TestTrue(*FString::Printf(TEXT("The limit [%f] is the walking limb's pinned return timeout"),
                Context.Get_Limit().Get_Seconds()), Context.Get_Limit() == LocomotionReturnTimeout);
            TestTrue(*FString::Printf(TEXT("Elapsed [%f] reached the limit"), Context.Get_Elapsed().Get_Seconds()),
                Context.Get_Elapsed() >= Context.Get_Limit());
            TestTrue(TEXT("The context names the returning limb"),
                Context.Get_LimbIds() == TArray<FName>{LegId(0)});
            TestTrue(TEXT("A time limit carries no count budget"),
                Context.Get_BudgetUnit().IsNone() && Context.Get_Observed() == 0 && Context.Get_Budget() == 0);
            auto Terminal = FCk_ProceduralAction_Record{};
            if (TestTrue(TEXT("The terminal record is retained"),
                Get_TerminalRecord(State->Player, State->Identity, Terminal)))
            {
                TestTrue(TEXT("The terminal record carries the same context"),
                    Get_AreEqual(Terminal.Get_Context(), Context));
            }
            const auto Text = UCk_Utils_ProceduralAction_UE::Get_ResultText(Result, Context).ToString();
            TestTrue(*FString::Printf(TEXT("Result text [%s] names the reason"), *Text),
                Text.Contains(Result.Get_Reason().ToString()));
            TestTrue(*FString::Printf(TEXT("Result text [%s] renders the limit"), *Text), Text.Contains(TEXT("0.600")));
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Walker.Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionDiagnosticsReceiptOverflowTest,
    "Ck.ProceduralAnimation.Action.Diagnostics.ReceiptOverflowCarriesPendingAndBudget",
    ck_test_procedural_action_diagnostics::TestFlags)

auto
    FCkProceduralActionDiagnosticsReceiptOverflowTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_diagnostics;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity,
                FTransform{FVector{100.0, 200.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Segment = UCk_Utils_Transform_UE::Add(SegmentEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            const auto Limb = FCk_ProceduralLimb_Layout{ArmId, FTransform::Identity,
                TArray<float>{100.0f}, FTransform{FVector{100.0, 0.0, 0.0}}, FVector::UpVector};
            State->Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, FCk_ProceduralArticulation_Spec{
                FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{Limb}},
                TArray<FCk_ProceduralLimb_Binding>{FCk_ProceduralLimb_Binding{
                    ArmId, TArray<FCk_Handle_Transform>{Segment}}}});
            TestTrue(TEXT("Dedicated subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->Subject)
                && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(State->Subject) > 0;
        }), WaitSeconds, TEXT("Dedicated subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            auto Spec = FCk_ProceduralActionPlayer_Spec{};
            auto Capacity = Spec.Get_Capacity();
            Capacity.Set_RecordsPerInstance(RecordsPerInstance);
            Spec.Set_Capacity(Capacity);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(State->Subject, Spec);
            TestTrue(TEXT("Player with a small record budget composes"), ck::IsValid(State->Player));
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Clip = ck_test_procedural_action_clip_fixture::MakeNodClip(TArray<FName>{ArmId}, FCk_Time{1.0});
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->Scope, State->Player,
                ck_test_procedural_action_clip_fixture::MakePlay(State->Clip.Get(), TArray<FName>{ArmId}));
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            BindFinished(*State);
        })));
    // Every accepted sample publishes a record nobody consumes, so the clip overflows its stream before it ends.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return UCk_Utils_ProceduralAction_UE::Get_State(State->Action) == ECk_ProceduralAction_State::Finished;
        }), WaitSeconds, TEXT("Overflow ends the action")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(State->Action);
            TestEqual(TEXT("Unconsumed receipts past the per-action budget report ReceiptOverflow"),
                Result.Get_Outcome(), ECk_ProceduralAction_Outcome::ReceiptOverflow);
            TestEqual(TEXT("OnFinished fires once"), State->Listener->FinishedCount, 1);
            const auto& Context = State->Listener->LastTerminal.Get_Context();
            TestEqual(TEXT("The measured quantity is unacknowledged records"),
                Context.Get_BudgetUnit(), FName{TEXT("PendingReceiptRecords")});
            TestEqual(TEXT("The budget is the player's RecordsPerInstance"),
                Context.Get_Budget(), static_cast<int64>(RecordsPerInstance));
            TestTrue(*FString::Printf(TEXT("Observed records [%lld] reached the budget [%lld]"),
                Context.Get_Observed(), Context.Get_Budget()), Context.Get_Observed() >= Context.Get_Budget());
            TestTrue(TEXT("The context names the claimed arm"), Context.Get_LimbIds() == TArray<FName>{ArmId});
            TestTrue(TEXT("A count budget carries no time limit"),
                Context.Get_Elapsed() == FCk_Time{} && Context.Get_Limit() == FCk_Time{});
            auto Terminal = FCk_ProceduralAction_Record{};
            if (TestTrue(TEXT("The terminal record is retained"),
                Get_TerminalRecord(State->Player, State->Identity, Terminal)))
            {
                TestEqual(TEXT("The terminal record keeps the outcome"),
                    Terminal.Get_Result().Get_Outcome(), ECk_ProceduralAction_Outcome::ReceiptOverflow);
                TestTrue(TEXT("The terminal record carries the same context"),
                    Get_AreEqual(Terminal.Get_Context(), Context));
            }
            const auto Text = UCk_Utils_ProceduralAction_UE::Get_ResultText(Result, Context).ToString();
            TestTrue(*FString::Printf(TEXT("Result text [%s] renders the observed count"), *Text),
                Text.Contains(FString::Printf(TEXT("Observed: %lld PendingReceiptRecords"), Context.Get_Observed())));
            TestTrue(*FString::Printf(TEXT("Result text [%s] renders the budget"), *Text),
                Text.Contains(FString::Printf(TEXT("Budget: %d PendingReceiptRecords"), RecordsPerInstance)));
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionDiagnosticsUnknownReasonTest,
    "Ck.ProceduralAnimation.Action.Diagnostics.UnknownReasonRendersIdentifier",
    ck_test_procedural_action_diagnostics::TestFlags)

auto
    FCkProceduralActionDiagnosticsUnknownReasonTest::
    RunTest(
        const FString&)
    -> bool
{
    const auto Unknown = FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::Rejected,
        ECk_ProceduralAction_Recovery::NotRequired, FName{TEXT("ReasonNoReleaseKnows")}};
    const auto Bare = UCk_Utils_ProceduralAction_UE::Get_ResultText(Unknown, {}).ToString();
    TestTrue(*FString::Printf(TEXT("[%s] renders the unknown reason by identifier"), *Bare),
        Bare.Contains(TEXT("ReasonNoReleaseKnows")));
    TestTrue(*FString::Printf(TEXT("[%s] renders the outcome"), *Bare), Bare.Contains(TEXT("Rejected")));
    TestTrue(*FString::Printf(TEXT("[%s] renders the recovery"), *Bare), Bare.Contains(TEXT("NotRequired")));
    TestFalse(*FString::Printf(TEXT("[%s] shows no context that was not captured"), *Bare),
        Bare.Contains(TEXT("Phase")) || Bare.Contains(TEXT("Limbs")) || Bare.Contains(TEXT("Elapsed"))
            || Bare.Contains(TEXT("Limit")) || Bare.Contains(TEXT("Observed")) || Bare.Contains(TEXT("Budget")));

    auto Context = FCk_ProceduralAction_FailureContext{};
    Context.Set_PhaseId(FName{TEXT("Windup")});
    Context.Set_Elapsed(FCk_Time{1.25});
    const auto Partial = UCk_Utils_ProceduralAction_UE::Get_ResultText(Unknown, Context).ToString();
    TestTrue(*FString::Printf(TEXT("[%s] renders the captured phase"), *Partial), Partial.Contains(TEXT("Windup")));
    TestTrue(*FString::Printf(TEXT("[%s] renders the captured elapsed time"), *Partial),
        Partial.Contains(TEXT("1.250")));
    TestFalse(*FString::Printf(TEXT("[%s] omits the uncaptured limit and budget"), *Partial),
        Partial.Contains(TEXT("Limit")) || Partial.Contains(TEXT("Budget")) || Partial.Contains(TEXT("Limbs")));

    const auto NoReason = UCk_Utils_ProceduralAction_UE::Get_ResultText(FCk_ProceduralAction_Result{}, {}).ToString();
    TestFalse(*FString::Printf(TEXT("[%s] renders an empty reason as absent, not as None"), *NoReason),
        NoReason.Contains(TEXT("Reason")));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionDiagnosticsTerminalRecordTest,
    "Ck.ProceduralAnimation.Action.Diagnostics.ContextRidesTerminalRecord",
    ck_test_procedural_action_diagnostics::TestFlags)

auto
    FCkProceduralActionDiagnosticsTerminalRecordTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_diagnostics;
    auto Store = ck::procedural_action::FReceiptStore{};
    if (NOT TestTrue(TEXT("Capacity configured"), Store.Configure(FCk_ProceduralAction_Capacity{})))
    { return false; }
    auto WithContext = FCk_ProceduralAction_Identity{};
    auto WithoutContext = FCk_ProceduralAction_Identity{};
    if (NOT TestTrue(TEXT("Two streams reserved"),
        Store.Reserve(TArray<FName>{ArmId}, false, WithContext) == ck::procedural_action::EReceiptStoreWrite::Accepted
        && Store.Reserve(TArray<FName>{FName{TEXT("OtherArm")}}, false, WithoutContext)
            == ck::procedural_action::EReceiptStoreWrite::Accepted))
    { return false; }

    auto Context = FCk_ProceduralAction_FailureContext{};
    Context.Set_PhaseId(FName{TEXT("Strike")});
    Context.Set_LimbIds(TArray<FName>{ArmId});
    Context.Set_Elapsed(FCk_Time{0.75});
    Context.Set_Limit(FCk_Time{0.5});
    const auto Result = FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::AcquisitionTimeout,
        ECk_ProceduralAction_Recovery::Succeeded, FName{TEXT("AcquisitionTimeout")}};

    const auto Pending = FCk_ProceduralAction_Result{ECk_ProceduralAction_Outcome::AcquisitionTimeout,
        ECk_ProceduralAction_Recovery::Pending, FName{TEXT("AcquisitionTimeout")}};
    TestTrue(TEXT("A context does not make a pending recovery terminal"),
        Store.Finish(WithContext, Pending, Context) == ck::procedural_action::EReceiptStoreWrite::InvalidRecord);
    TestTrue(TEXT("Finish with a context is accepted"),
        Store.Finish(WithContext, Result, Context) == ck::procedural_action::EReceiptStoreWrite::Accepted);
    TestTrue(TEXT("Finish without a context is still accepted"),
        Store.Finish(WithoutContext, Result) == ck::procedural_action::EReceiptStoreWrite::Accepted);

    const auto Find = [&Store](FCk_ProceduralAction_Identity InIdentity) -> TOptional<FCk_ProceduralAction_Record>
    {
        const auto Page = Store.Read(InIdentity, 0, 64);
        const auto* Terminal = Page.Get_Records().FindByPredicate([](const FCk_ProceduralAction_Record& InRecord)
        { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal; });
        return Terminal != nullptr ? TOptional<FCk_ProceduralAction_Record>{*Terminal}
            : TOptional<FCk_ProceduralAction_Record>{};
    };
    const auto Carried = Find(WithContext);
    if (TestTrue(TEXT("Terminal record is readable"), Carried.IsSet()))
    {
        TestTrue(TEXT("Terminal record carries the captured context"), Get_AreEqual(Carried->Get_Context(), Context));
        TestEqual(TEXT("Terminal record keeps the result"), Carried->Get_Result().Get_Outcome(),
            ECk_ProceduralAction_Outcome::AcquisitionTimeout);
    }
    const auto Plain = Find(WithoutContext);
    if (TestTrue(TEXT("Contextless terminal record is readable"), Plain.IsSet()))
    {
        TestTrue(TEXT("The two-argument Finish records an empty context"),
            Get_AreEqual(Plain->Get_Context(), FCk_ProceduralAction_FailureContext{}));
    }
    return true;
}

#endif
