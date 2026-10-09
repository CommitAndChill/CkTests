#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityDebugOverlay/Provider/CkDebugOverlay_ObservationBank.h"
#include "CkEntityDebugOverlay/Provider/CkDebugOverlay_Registry.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace ck_selection_bank_test
{
    struct FRun
    {
        FCk_Handle Foreign;
        FCk_Handle RetainedClientSource;
        FCk_Handle Retiring;
        FCk_Handle LiveTimerOwner;
        bool Failed = false;
    };

    auto TimerProvider() -> TSharedPtr<ICk_DebugOverlay_Provider>
    {
        for (const auto& Candidate : FCk_DebugOverlay_Registry::Get().CreateAll())
        {
            if (Candidate.IsValid() &&
                Candidate->Get_ProviderTag().ToString() ==
                    TEXT("Ck.OnScreenDebugger.Provider.Timer"))
            { return Candidate; }
        }
        return {};
    }

    auto Observe(ICk_DebugOverlay_Provider& Provider, const FCk_Handle& Source,
                 const FString& ExpectedRow, const FString& ExpectedCompact) -> bool
    {
        const auto Fields = Provider.Get_FieldTags();
        if (Fields.Num() != 1 || NOT Fields[0].Tag.IsValid())
        { return false; }
        auto Config = FCk_DebugOverlay_ProviderConfig{};
        Config.EnabledFields.AddTag(Fields[0].Tag);
        auto Section = FCk_DebugOverlay_Section{};
        const bool CanProvide = Provider.CanProvide(Source);
        if (CanProvide)
        { Provider.Collect(Source, Config, Section); }
        const auto Compact = Provider.Get_CompactToken(Source, Config);
        if (ExpectedRow.IsEmpty())
        { return NOT CanProvide && Section.Rows.IsEmpty() && Compact.IsEmpty(); }
        return CanProvide && Section.ProviderTag == Provider.Get_ProviderTag() &&
            Section.SortPriority == Provider.Get_SortPriority() &&
            Section.Rows.Num() == 1 &&
            Section.Rows[0].FieldTag == Fields[0].Tag &&
            Section.Rows[0].Value.ToString() == ExpectedRow &&
            Compact == ExpectedCompact;
    }

    auto Row(const FCk_Handle& Source, ECk_Timer_State State)
        -> FCk_DebugOverlay_SourceObservation
    {
        auto Result = FCk_DebugOverlay_SourceObservation{};
        Result.Source = Source;
        Result.Timers.Add(FCk_DebugOverlay_TimerFact{FGameplayTag{}, State});
        return Result;
    }

    auto Publish(UWorld* World, const FCk_Handle& WorldRoot,
                 const FCk_Handle& ScenarioRoot, uint64 G, uint64 Q,
                 const TArray<FCk_DebugOverlay_SourceObservation>& Rows) -> bool
    {
        return FCk_DebugOverlay_ObservationBankUtils::TryPublish(
            World, WorldRoot, ScenarioRoot, G, Q, Rows);
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSelectionObservationBankNetwork,
    "Ck.Selection.Gym.ObservationBank.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSelectionObservationBankNetwork::RunTest(const FString& Parameters)
{
    using namespace ck_selection_bank_test;
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            return IsValid(Server) && IsValid(Client0) && IsValid(Client1) &&
                IsValid(Server->GetGameState()) &&
                Server->GetGameState()->HasActorBegunPlay() &&
                Client0->GetNetMode() == NM_Client &&
                Client1->GetNetMode() == NM_Client &&
                ck::IsValid(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Server)) &&
                ck::IsValid(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client0)) &&
                ck::IsValid(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client1));
        }), 30.0, TEXT("three real network ECS worlds ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            auto Provider = TimerProvider();
            const auto Root = IsValid(World)
                ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World)
                : FCk_Handle{};
            if (NOT TestTrue(TEXT("canonical Timer provider and authority root"),
                Provider.IsValid() && ck::IsValid(Root))) { Run->Failed = true; return; }
            auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            Run->LiveTimerOwner = Owner;
            if (NOT TestTrue(TEXT("real authority Timer owner"), ck::IsValid(Owner)))
            { Run->Failed = true; return; }
            auto Spec = FCk_Timer_Spec{FCk_Time{10.0}};
            Spec.Set_StartingState(ECk_Timer_State::Paused);
            const auto Captured = UCk_Utils_Timer_UE::TryAddCapturedPaused(Owner, Spec);
            auto PassiveServer = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("real paused authority Timer constructed"),
                ck::IsValid(Captured.Timer) && UCk_Utils_Timer_UE::Has_Any(Owner) &&
                Observe(*Provider, Owner, TEXT("[1] Unnamed(Paused)"), TEXT("Tmr:1")) &&
                ck::IsValid(PassiveServer) &&
                !Publish(World, Root, PassiveServer, 1, 1,
                    {Row(PassiveServer, ECk_Timer_State::Paused)})))
            { Run->Failed = true; }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            auto Provider = TimerProvider();
            const auto Root = IsValid(World)
                ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World)
                : FCk_Handle{};
            if (NOT TestTrue(TEXT("foreign client world and provider"),
                Provider.IsValid() && ck::IsValid(Root)))
            { Run->Failed = true; return; }
            Run->Foreign = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("foreign client passive source has no bank"),
                ck::IsValid(Run->Foreign) &&
                Observe(*Provider, Run->Foreign, {}, {})))
            { Run->Failed = true; }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Failed) { return; }
            auto Provider = TimerProvider();
            const auto Root = IsValid(World)
                ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World)
                : FCk_Handle{};
            if (NOT TestTrue(TEXT("exact client bank prerequisites"),
                Provider.IsValid() && ck::IsValid(Root) &&
                ck::IsValid(Run->Foreign) && World->GetNetMode() == NM_Client))
            { Run->Failed = true; return; }
            const auto Scenario = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("fresh scenario root"), ck::IsValid(Scenario)))
            { Run->Failed = true; return; }
            auto A = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Scenario);
            const auto B = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            Run->RetainedClientSource = B;
            if (NOT TestTrue(TEXT("real retained source topology"),
                ck::IsValid(Scenario) && ck::IsValid(A) && ck::IsValid(B) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(B) == Root &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(A) == Scenario))
            { Run->Failed = true; return; }

            auto Initial = TArray<FCk_DebugOverlay_SourceObservation>{
                Row(A, ECk_Timer_State::Paused),
                Row(B, ECk_Timer_State::Running)};
            Initial[0].Integers.Add(FCk_DebugOverlay_IntegerFact{FGameplayTag{}, 3, 4});
            Initial[0].HasSm = true;
            Initial[0].SmStates.Add(FCk_DebugOverlay_SmStateFact{
                0, TEXT("Idle"), ECk_SmRunStatus::Running});
            Initial[0].HasDebugHistory = true;
            Initial[0].SmHistory.Add(FCk_DebugOverlay_SmHistoryFact{
                TEXT("Start"), TEXT("Idle")});
            auto Staged = Initial;
            Staged[0].Timers[0].State = ECk_Timer_State::Running;
            if (NOT TestTrue(TEXT("staged candidate remains invisible"),
                Observe(*Provider, A, {}, {}) &&
                Observe(*Provider, B, {}, {}) &&
                Publish(World, Root, Scenario, 1, 1, Initial) &&
                Observe(*Provider, A, TEXT("[1] Unnamed(Paused)"), TEXT("Tmr:1")) &&
                Observe(*Provider, B, TEXT("[1] Unnamed(Running)"), TEXT("Tmr:1"))))
            { Run->Failed = true; return; }

            // A bank row becomes unavailable if its source gains partial simulation state.
            A.Add<ck::FFragment_Timer_Params>(ECk_Timer_Behavior::PauseOnDone);
            auto PartialLookup = Initial[0];
            const bool LookupRejected =
                NOT FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    A, PartialLookup) &&
                PartialLookup.Source == FCk_Handle{} &&
                PartialLookup.Timers.IsEmpty() &&
                Observe(*Provider, A, {}, {});
            const bool RemovedPartial = A.Try_Remove<ck::FFragment_Timer_Params>();
            auto RecoveredLookup = FCk_DebugOverlay_SourceObservation{};
            const bool LookupRecovered = RemovedPartial &&
                FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    A, RecoveredLookup) &&
                RecoveredLookup.Source == A &&
                RecoveredLookup.Timers.Num() == 1 &&
                RecoveredLookup.Timers[0].State == ECk_Timer_State::Paused &&
                Observe(*Provider, A, TEXT("[1] Unnamed(Paused)"), TEXT("Tmr:1")) &&
                Root.Has<FCk_DebugOverlay_ObservationBank>() &&
                Root.Get<FCk_DebugOverlay_ObservationBank>().Generation == 1 &&
                Root.Get<FCk_DebugOverlay_ObservationBank>().Sequence == 1;
            if (NOT RemovedPartial)
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(A); }
            if (NOT TestTrue(TEXT("partial state hides published source; removal restores prior facts"),
                LookupRejected && LookupRecovered))
            { Run->Failed = true; return; }
            auto LateInvalid = Staged;
            LateInvalid.Add(Row({}, ECk_Timer_State::Paused));
            auto Foreign = Initial;
            Foreign.Add(Row(Run->Foreign, ECk_Timer_State::Paused));
            auto BadState = Initial;
            BadState[1].Timers[0].State = static_cast<ECk_Timer_State>(255);
            auto BadDepth = Initial;
            BadDepth[1].HasSm = true;
            BadDepth[1].SmStates.Add(FCk_DebugOverlay_SmStateFact{
                0, TEXT("Root"), ECk_SmRunStatus::Running});
            BadDepth[1].SmStates.Add(FCk_DebugOverlay_SmStateFact{
                2, TEXT("SkippedDepth"), ECk_SmRunStatus::Running});
            auto ChangedChain = Initial;
            ChangedChain[0].SmStates[0].StateName = TEXT("Different");
            const auto Simulated = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            auto SimSpec = FCk_Timer_Spec{FCk_Time{10.0}};
            SimSpec.Set_StartingState(ECk_Timer_State::Paused);
            const auto SimTimer = ck::IsValid(Simulated)
                ? UCk_Utils_Timer_UE::TryAddCapturedPaused(Simulated, SimSpec)
                : FCk_Timer_AddCapturedResult{};
            auto Simulation = Initial;
            Simulation.Add(Row(Simulated, ECk_Timer_State::Paused));
            // A partial local simulation fragment cannot enter a newer whole bank.
            auto PartialTimerParams =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            auto PartialTimerRequests =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            auto PartialIntegerMin =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            auto PartialIntegerMax =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            auto PartialIntegerModifier =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            auto PartialSmRequests =
                UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("partial simulation sources allocated"),
                ck::IsValid(PartialTimerParams) &&
                ck::IsValid(PartialTimerRequests) &&
                ck::IsValid(PartialIntegerMin) &&
                ck::IsValid(PartialIntegerMax) &&
                ck::IsValid(PartialIntegerModifier) &&
                ck::IsValid(PartialSmRequests)))
            { Run->Failed = true; return; }
            PartialTimerParams.Add<ck::FFragment_Timer_Params>(
                ECk_Timer_Behavior::PauseOnDone);
            PartialTimerRequests.Add<ck::FFragment_Timer_Requests>();
            PartialIntegerMin.Add<ck::FFragment_IntegerAttribute_Min>(0);
            PartialIntegerMax.Add<ck::FFragment_IntegerAttribute_Max>(0);
            PartialIntegerModifier.Add<ck::FFragment_IntegerAttributeModifier>();
            PartialSmRequests.Add<ck::FFragment_Sm_Requests>();
            const auto RejectPartial = [&](const FCk_Handle& Source) -> bool
            {
                auto Candidate = Initial;
                Candidate.Add(Row(Source, ECk_Timer_State::Paused));
                return NOT Publish(World, Root, Scenario, 1, 2, Candidate);
            };
            const bool AllPartialRejected =
                RejectPartial(PartialTimerParams) &&
                RejectPartial(PartialTimerRequests) &&
                RejectPartial(PartialIntegerMin) &&
                RejectPartial(PartialIntegerMax) &&
                RejectPartial(PartialIntegerModifier) &&
                RejectPartial(PartialSmRequests);
            // Remove deliberately incomplete state before ordinary processors tick.
            const bool ClearedTimerParams =
                PartialTimerParams.Try_Remove<ck::FFragment_Timer_Params>();
            const bool ClearedTimerRequests =
                PartialTimerRequests.Try_Remove<ck::FFragment_Timer_Requests>();
            const bool ClearedIntegerMin =
                PartialIntegerMin.Try_Remove<ck::FFragment_IntegerAttribute_Min>();
            const bool ClearedIntegerMax =
                PartialIntegerMax.Try_Remove<ck::FFragment_IntegerAttribute_Max>();
            const bool ClearedIntegerModifier =
                PartialIntegerModifier.Try_Remove<ck::FFragment_IntegerAttributeModifier>();
            const bool ClearedSmRequests =
                PartialSmRequests.Try_Remove<ck::FFragment_Sm_Requests>();
            const bool ClearedAllPartial = ClearedTimerParams && ClearedTimerRequests &&
                ClearedIntegerMin && ClearedIntegerMax &&
                ClearedIntegerModifier && ClearedSmRequests;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PartialTimerParams);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PartialTimerRequests);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PartialIntegerMin);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PartialIntegerMax);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PartialIntegerModifier);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PartialSmRequests);
            if (NOT TestTrue(TEXT("partial timer, attribute and SM sources reject without replacing bank"),
                AllPartialRejected && ClearedAllPartial &&
                Root.Has<FCk_DebugOverlay_ObservationBank>() &&
                Root.Get<FCk_DebugOverlay_ObservationBank>().Generation == 1 &&
                Root.Get<FCk_DebugOverlay_ObservationBank>().Sequence == 1 &&
                Observe(*Provider, A, TEXT("[1] Unnamed(Paused)"), TEXT("Tmr:1")) &&
                Observe(*Provider, B, TEXT("[1] Unnamed(Running)"), TEXT("Tmr:1"))))
            { Run->Failed = true; return; }
            auto Replicated = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (ck::IsValid(Replicated))
            {
                UCk_Utils_Net_UE::Add(Replicated, FCk_Net_ConnectionSettings{
                    ECk_Replication::Replicates,
                    ECk_Net_NetModeType::Client,
                    ECk_Net_EntityNetRole::Proxy});
            }
            auto Replication = Initial;
            Replication.Add(Row(Replicated, ECk_Timer_State::Paused));
            Run->Retiring = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            auto Retiring = Initial;
            Retiring.Add(Row(Run->Retiring, ECk_Timer_State::Paused));
            if (ck::IsValid(Run->Retiring))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->Retiring); }
            if (NOT TestTrue(TEXT("invalid later row, foreign, enum, simulation, replication and retiring reject atomically"),
                ck::IsValid(SimTimer.Timer) && ck::IsValid(Replicated) &&
                !Publish(World, Root, Scenario, 1, 2, LateInvalid) &&
                !Publish(World, Root, Scenario, 1, 2, Foreign) &&
                !Publish(World, Root, Scenario, 1, 2, BadState) &&
                !Publish(World, Root, Scenario, 1, 2, BadDepth) &&
                !Publish(World, Root, Scenario, 1, 1, ChangedChain) &&
                !Publish(World, Root, Scenario, 1, 2, Simulation) &&
                !Publish(World, Root, Scenario, 1, 2, Replication) &&
                !Publish(World, Root, Scenario, 1, 2, Retiring) &&
                !Publish(nullptr, Root, Scenario, 1, 2, Initial) &&
                Observe(*Provider, A, TEXT("[1] Unnamed(Paused)"), TEXT("Tmr:1")) &&
                Observe(*Provider, B, TEXT("[1] Unnamed(Running)"), TEXT("Tmr:1"))))
            { Run->Failed = true; return; }
            auto OldFacts = FCk_DebugOverlay_SourceObservation{};
            if (NOT TestTrue(TEXT("impossible depth on later source preserves all prior provider facts"),
                FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    A, OldFacts) &&
                OldFacts.Source == A && OldFacts.Timers.Num() == 1 &&
                OldFacts.Timers[0].State == ECk_Timer_State::Paused &&
                OldFacts.Integers.Num() == 1 &&
                OldFacts.Integers[0].Base == 3 &&
                OldFacts.Integers[0].Final == 4 &&
                OldFacts.HasSm &&
                OldFacts.SmStates.Num() == 1 &&
                OldFacts.SmStates[0].Depth == 0 &&
                OldFacts.SmStates[0].StateName == TEXT("Idle") &&
                OldFacts.SmStates[0].Status == ECk_SmRunStatus::Running &&
                OldFacts.HasDebugHistory &&
                OldFacts.SmHistory.Num() == 1 &&
                OldFacts.SmHistory[0].FromStateName == TEXT("Start") &&
                OldFacts.SmHistory[0].ToStateName == TEXT("Idle") &&
                Root.Has<FCk_DebugOverlay_ObservationBank>() &&
                Root.Get<FCk_DebugOverlay_ObservationBank>().Generation == 1 &&
                Root.Get<FCk_DebugOverlay_ObservationBank>().Sequence == 1))
            { Run->Failed = true; return; }

            const auto NewScenario = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("fresh replacement scenario root"),
                ck::IsValid(NewScenario)))
            { Run->Failed = true; return; }
            const auto C = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(NewScenario);
            if (NOT TestTrue(TEXT("replacement scenario and source live"),
                ck::IsValid(NewScenario) && ck::IsValid(C)))
            { Run->Failed = true; return; }
            auto Next = TArray<FCk_DebugOverlay_SourceObservation>{
                Row(B, ECk_Timer_State::Paused),
                Row(C, ECk_Timer_State::Running)};
            Next[0].Timers.Add(FCk_DebugOverlay_TimerFact{
                FGameplayTag{}, ECk_Timer_State::Running});
            if (NOT TestTrue(TEXT("strictly newer whole-bank root replacement"),
                Publish(World, Root, NewScenario, 2, 1, Next) &&
                Observe(*Provider, A, {}, {}) &&
                Observe(*Provider, B,
                    TEXT("[2] Unnamed(Paused), Unnamed(Running)"), TEXT("Tmr:2")) &&
                Observe(*Provider, C, TEXT("[1] Unnamed(Running)"), TEXT("Tmr:1")) &&
                Publish(World, Root, NewScenario, 2, 1, Next) &&
                !Publish(World, Root, Scenario, 2, 1, Next)))
            { Run->Failed = true; return; }
            auto Changed = Next;
            Changed[0].Integers.Add(FCk_DebugOverlay_IntegerFact{
                FGameplayTag{}, 8, 9});
            auto ChangedSm = Next;
            ChangedSm[0].HasSm = true;
            ChangedSm[0].SmStates.Add(FCk_DebugOverlay_SmStateFact{
                0, TEXT("Idle"), ECk_SmRunStatus::Stopped});
            auto Withdraw = Next;
            Withdraw[0].Timers.Reset();
            if (NOT TestTrue(TEXT("same-epoch value/shape mutation rejects; explicit empty withdraws"),
                !Publish(World, Root, NewScenario, 2, 1, Changed) &&
                !Publish(World, Root, NewScenario, 2, 1, ChangedSm) &&
                !Publish(World, Root, NewScenario, 1, 9, Next) &&
                Observe(*Provider, B,
                    TEXT("[2] Unnamed(Paused), Unnamed(Running)"), TEXT("Tmr:2")) &&
                Publish(World, Root, NewScenario, 2, 2, Withdraw) &&
                Observe(*Provider, B, {}, {}) &&
                Observe(*Provider, C, TEXT("[1] Unnamed(Running)"), TEXT("Tmr:1"))))
            { Run->Failed = true; return; }

            auto Alias = FCk_DebugOverlay_SourceObservation{};
            Alias.Source = C;
            const bool AliasResult =
                FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    Alias.Source, Alias);
            auto DefaultOut = Alias;
            DefaultOut.Source = {};
            const bool DefaultResult =
                FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                    DefaultOut.Source, DefaultOut);
            if (NOT TestTrue(TEXT("published output alias and rejected default clear"),
                AliasResult && Alias.Source == C && Alias.Timers.Num() == 1 &&
                !DefaultResult && DefaultOut.Source == FCk_Handle{} &&
                DefaultOut.Timers.IsEmpty()))
            { Run->Failed = true; }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return NOT Run->Retiring.IsValid(ck::IsValid_Policy_IncludePendingKill{}); }),
        15.0, TEXT("real rejected retiring source physically deallocates")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (Run->Failed) { return; }
            const auto Root = IsValid(World)
                ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World)
                : FCk_Handle{};
            if (NOT TestTrue(TEXT("exact client root still live"),
                ck::IsValid(Root)))
            { Run->Failed = true; return; }
            const auto Scenario = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (NOT TestTrue(TEXT("fresh post-retirement scenario"),
                ck::IsValid(Scenario)))
            { Run->Failed = true; return; }
            const auto StaleRow = TArray<FCk_DebugOverlay_SourceObservation>{
                Row(Run->Retiring, ECk_Timer_State::Paused)};
            if (NOT TestTrue(TEXT("physically retired source cannot enter later bank"),
                !Run->Retiring.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                !Publish(World, Root, Scenario, 3, 1, StaleRow)))
            { Run->Failed = true; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]()
        {
            auto Out = FCk_DebugOverlay_SourceObservation{};
            Out.Source = Run->RetainedClientSource;
            return TestFalse(TEXT("all PIE worlds drained"),
                NOT ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()) &&
                TestFalse(TEXT("retained local source has no bank after world teardown"),
                    FCk_DebugOverlay_ObservationBankUtils::TryGetPublishedSource(
                        Run->RetainedClientSource, Out)) &&
                TestTrue(TEXT("failed teardown lookup clears output"),
                    Out.Source == FCk_Handle{} && Out.Timers.IsEmpty()) &&
                TestFalse(TEXT("all real network bank assertions completed"), Run->Failed);
        }), TEXT("world teardown and no skipped bank assertion"), true));
    return true;
}

#endif
