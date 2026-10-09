#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkSelectionGym_BankCensus.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/Net/CkAutoTest_Sm_RecordingState.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkTimer/CkTimer_Utils.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace ck_selection_bank_census_test
{
    struct FRun
    {
        FCk_Handle WorldRoot;
        FCk_Handle ScenarioRoot;
        FCk_Handle Candidate;
        FCk_Handle SceneNode;
        FCk_Handle Ordinary;
        FCk_Handle Nested;
        FCk_Handle Timer;
        FCk_Handle Integer;
        FCk_Handle StateMachine;
        FCk_Handle DirectOutlier;
        FCk_Handle ForeignClientRoot;
        FCk_Handle RetiringLeaf;
        FString InitialCensusFailure;
        int32 CensusAttempts = 0;
        bool Built = false;
    };

    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    auto Build(FRun& Run) -> bool
    {
        Run.Candidate = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.ScenarioRoot);
        if (!Allocated(Run.Candidate)) { return false; }
        auto Transform = UCk_Utils_Transform_UE::Add(Run.Candidate,
            FTransform(FRotator::ZeroRotator, FVector(100, 0, 60)),
            ECk_Replication::DoesNotReplicate);
        if (!Allocated(Transform)) { return false; }
        Run.SceneNode = UCk_Utils_SceneNode_UE::Create(Transform,
            FTransform(FRotator::ZeroRotator, FVector(0, 150, 100)));
        if (!Allocated(Run.SceneNode)) { return false; }

        Run.Ordinary = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.Candidate);
        if (!Allocated(Run.Ordinary)) { return false; }
        if (!Allocated(UCk_Utils_Transform_UE::Add(Run.Ordinary,
            FTransform(FRotator::ZeroRotator, FVector(100, -150, 60)),
            ECk_Replication::DoesNotReplicate)))
        { return false; }
        Run.Nested = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.Ordinary);
        if (!Allocated(Run.Nested)) { return false; }

        auto TimerSpec = FCk_Timer_Spec(FCk_Time(30.0f));
        TimerSpec.Set_StartingState(ECk_Timer_State::Running)
                 .Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        Run.Timer = UCk_Utils_Timer_UE::Add(Run.Candidate, TimerSpec);
        if (!Allocated(Run.Timer)) { return false; }
        Run.Integer = UCk_Utils_IntegerAttribute_UE::Add(Run.Candidate,
            FCk_IntegerAttribute_Spec(TAG_Label_IntegerAttribute, 7),
            ECk_Replication::DoesNotReplicate);
        if (!Allocated(Run.Integer)) { return false; }
        Run.StateMachine = UCk_Utils_StateMachine_UE::Add(Run.Candidate,
            FCk_StateMachine_Spec(UCk_AutoTest_Sm_RecordingState_A::StaticClass()));
        if (!Allocated(Run.StateMachine)) { return false; }

        Run.DirectOutlier = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.WorldRoot);
        if (!Allocated(Run.DirectOutlier)) { return false; }
        if (!Allocated(UCk_Utils_Transform_UE::Add(Run.DirectOutlier,
            FTransform(FRotator::ZeroRotator, FVector(-100, 0, 60)),
            ECk_Replication::DoesNotReplicate)))
        { return false; }
        Run.RetiringLeaf = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run.Candidate);
        return Allocated(Run.RetiringLeaf);
    }

    auto HasExactCore(const TArray<FCk_Handle>& Rows, const FRun& Run) -> bool
    {
        const FCk_Handle Required[] = {
            Run.ScenarioRoot, Run.Candidate, Run.SceneNode, Run.Ordinary,
            Run.Nested, Run.Timer, Run.Integer, Run.StateMachine,
            Run.DirectOutlier, Run.RetiringLeaf };
        TSet<FCk_Handle> Unique;
        for (const auto& H : Rows) { Unique.Add(H); }
        if (Unique.Num() != Rows.Num()) { return false; }
        for (const auto& H : Required)
        { if (!Allocated(H) || !Unique.Contains(H)) { return false; } }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSelectionGym_BankCensus,
    "Ck.Selection.Gym.BankCensus.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkSelectionGym_BankCensus::RunTest(const FString& Parameters)
{
    using namespace ck_selection_bank_census_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Selection census topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            return IsValid(Server) && IsValid(Client) &&
                IsValid(Server->GetGameState()) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Server)) &&
                Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client));
        }), 60.0, TEXT("Selection census authority and client ECS worlds ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            Run->WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World);
            Run->ScenarioRoot = UCk_Utils_EntityLifetime_UE::
                Request_CreateEntity(Run->WorldRoot);
            Run->Built = Allocated(Run->ScenarioRoot) && Build(*Run);
            TestTrue(TEXT("real Selection-like root, nested scene/Timer/Integer/SM and direct outlier composed"),
                Run->Built);
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            if (IsValid(Client))
            {
                const auto ClientWorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Client);
                if (Allocated(ClientWorldRoot))
                {
                    Run->ForeignClientRoot = UCk_Utils_EntityLifetime_UE::
                        Request_CreateEntity(ClientWorldRoot);
                }
            }
            TestTrue(TEXT("foreign retained root is a real client-world direct child"),
                Allocated(Run->ForeignClientRoot));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (!Run->Built) { return true; }
            if (!Allocated(Run->StateMachine) ||
                !Run->StateMachine.Has<ck::FFragment_Sm>()) { return false; }
            const auto State = Run->StateMachine.Get<ck::FFragment_Sm>().Get_CurrentStateHandle();
            return Allocated(State);
        }), 15.0, TEXT("actual state-machine child materialized")));
    // Report before a bounded wait: the harness skips later actions on timeout.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!Run->Built) { return; }
            TArray<FCk_Handle> Initial;
            FString Why;
            const bool Complete = ck_selection_gym_census::TryCensusAllocatedBank(
                World, Run->ScenarioRoot, {Run->DirectOutlier}, Initial, Why);
            Run->CensusAttempts = 1;
            if (!Complete) { Run->InitialCensusFailure = Why; }
            AddInfo(FString::Printf(TEXT("initial whole-bank census: complete=%d reason=%s"),
                Complete, *Why));
        })));
    // A visible state can still own its retiring EntityScript request carrier.
    // Wait for the entire retained forest, rather than one construction receipt.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            if (!Run->Built) { return true; }
            TArray<FCk_Handle> Settled;
            FString Why;
            ++Run->CensusAttempts;
            return ck_selection_gym_census::TryCensusAllocatedBank(
                ck::auto_test::net::Get_ServerWorld(), Run->ScenarioRoot,
                {Run->DirectOutlier}, Settled, Why);
        }), 15.0, TEXT("complete allocated Selection source forest settled")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!Run->Built) { return; }
            AddInfo(FString::Printf(TEXT("census readiness attempts=%d initial rejection=%s"),
                Run->CensusAttempts, *Run->InitialCensusFailure));
            TArray<FCk_Handle> Rows;
            FString Why;
            const TArray<FCk_Handle> Roots{Run->DirectOutlier};
            const bool Complete = ck_selection_gym_census::TryCensusAllocatedBank(
                World, Run->ScenarioRoot, Roots, Rows, Why);
            const bool CoreComplete = HasExactCore(Rows, *Run);
            TestTrue(*FString::Printf(
                TEXT("whole retained forest: Complete=%d Reason=%s Rows=%d Core=%d"),
                Complete, *Why, Rows.Num(), CoreComplete),
                Complete && Why.IsEmpty() && CoreComplete);
            if (Complete && !CoreComplete)
            {
                const FCk_Handle Required[] = {Run->ScenarioRoot, Run->Candidate,
                    Run->SceneNode, Run->Ordinary, Run->Nested, Run->Timer,
                    Run->Integer, Run->StateMachine, Run->DirectOutlier, Run->RetiringLeaf};
                const TCHAR* Names[] = {TEXT("ScenarioRoot"), TEXT("Candidate"),
                    TEXT("SceneNode"), TEXT("Ordinary"), TEXT("Nested"), TEXT("Timer"),
                    TEXT("Integer"), TEXT("StateMachine"), TEXT("DirectOutlier"), TEXT("RetiringLeaf")};
                for (int32 Index = 0; Index < UE_ARRAY_COUNT(Required); ++Index)
                {
                    AddInfo(FString::Printf(TEXT("required %s: allocated=%d present=%d"),
                        Names[Index], Allocated(Required[Index]), Rows.Contains(Required[Index])));
                }
            }
            if (!Complete) { return; }
            const auto CurrentState =
                Run->StateMachine.Get<ck::FFragment_Sm>().Get_CurrentStateHandle();
            TestTrue(TEXT("runtime-created SM state is in the census"),
                Rows.Contains(CurrentState));

            TMap<int64, FCk_Handle> Keys;
            bool StableKeys = true;
            for (const auto& H : Rows)
            {
                int64 Key = 0;
                StableKeys &= ck_selection_gym_census::TryGetOpaqueEntityKey(
                    H, Run->WorldRoot, Key) && Key > 0 && !Keys.Contains(Key);
                if (StableKeys) { Keys.Add(Key, H); }
            }
            TArray<FCk_Handle> Again;
            StableKeys &= ck_selection_gym_census::TryCensusAllocatedBank(
                World, Run->ScenarioRoot, Roots, Again, Why) && Again.Num() == Rows.Num();
            for (const auto& H : Again)
            {
                int64 Key = 0;
                StableKeys &= ck_selection_gym_census::TryGetOpaqueEntityKey(
                    H, Run->WorldRoot, Key) && Keys.FindRef(Key) == H;
            }
            TestTrue(TEXT("full entity-ID keys unique and stable across adjacent real reads"), StableKeys);

            Rows.Reset();
            Rows.Add(Run->ScenarioRoot);
            const bool ScenarioAliasesOutput = ck_selection_gym_census::TryCensusAllocatedBank(
                World, Rows[0], Roots, Rows, Why);
            TestTrue(TEXT("ScenarioRoot input alias survives output clearing"),
                ScenarioAliasesOutput && HasExactCore(Rows, *Run));
            Rows.Reset();
            Rows.Add(Run->DirectOutlier);
            const bool RootsAliasOutput = ck_selection_gym_census::TryCensusAllocatedBank(
                World, Run->ScenarioRoot, Rows, Rows, Why);
            TestTrue(TEXT("direct-root array input alias survives output clearing"),
                RootsAliasOutput && HasExactCore(Rows, *Run));

            auto Reject = [&](const TCHAR* Label, const FCk_Handle& Root,
                const TArray<FCk_Handle>& Extra) {
                Rows.Reset(); Rows.Add(Run->WorldRoot); Why.Reset();
                const bool Accepted = ck_selection_gym_census::TryCensusAllocatedBank(
                    World, Root, Extra, Rows, Why);
                TestTrue(Label, !Accepted && Rows.IsEmpty() && !Why.IsEmpty());
            };
            Reject(TEXT("default ScenarioRoot rejects atomically"), {}, Roots);
            Reject(TEXT("duplicate direct root rejects atomically"),
                Run->ScenarioRoot, {Run->DirectOutlier, Run->DirectOutlier});
            if (Allocated(Run->ForeignClientRoot))
            { Reject(TEXT("foreign-world retained root rejects atomically"),
                Run->ScenarioRoot, {Run->ForeignClientRoot}); }
            int64 WrongRootKey = -1;
            TestTrue(TEXT("non-transient handle cannot certify opaque key world"),
                !ck_selection_gym_census::TryGetOpaqueEntityKey(
                    Run->Candidate, Run->ScenarioRoot, WrongRootKey) && WrongRootKey == 0);

            if (Allocated(Run->RetiringLeaf))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->RetiringLeaf); }
            const bool Initiated = Allocated(Run->RetiringLeaf) &&
                Run->RetiringLeaf.Has_Any<ck::FTag_DestroyEntity_Initiate,
                    ck::FTag_DestroyEntity_EndPlay,
                    ck::FTag_DestroyEntity_Teardown>();
            TestTrue(TEXT("known leaf is still allocated and actually initiated"), Initiated);
            Rows.Add(Run->WorldRoot); Why.Reset();
            const bool RetiringAccepted = ck_selection_gym_census::TryCensusAllocatedBank(
                World, Run->ScenarioRoot, Roots, Rows, Why);
            TestTrue(TEXT("initiated child rejects whole census without partial handles"),
                Initiated && !RetiringAccepted && Rows.IsEmpty() && !Why.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([Run](UWorld*) {
            if (Allocated(Run->ScenarioRoot))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->ScenarioRoot); }
            if (Allocated(Run->DirectOutlier))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->DirectOutlier); }
            if (Allocated(Run->ForeignClientRoot))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->ForeignClientRoot); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool {
            return !Allocated(Run->ScenarioRoot) && !Allocated(Run->DirectOutlier) &&
                !Allocated(Run->ForeignClientRoot);
        }), 30.0, TEXT("Selection census fixture roots physically deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
