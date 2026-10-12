#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCrowdGym_SharedSimulation.h"
#include "CkCrowd/Agent/CkCrowdAgent_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkPhysics/Acceleration/CkAcceleration_Utils.h"
#include "CkPhysics/EulerIntegrator/CkEulerIntegrator_Fragment.h"
#include "CkPhysics/Velocity/CkVelocity_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/World.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <Misc/AutomationTest.h>

namespace ck_crowd_shared_simulation_test
{
    constexpr int64 FirstSequence = 101;
    const FVector Origin(350.0, -450.0, 150.0);

    auto IsLive(const FCk_Handle& Handle) -> bool
    {
        return ck::IsValid(Handle) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Handle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto IsGone(const FCk_Handle& Handle) -> bool
    { return ck::Is_NOT_Valid(Handle, ck::IsValid_Policy_IncludePendingKill{}); }

    auto Transient(UWorld& World) -> FCk_Handle
    { return UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(World); }

    auto Direct(const FCk_Handle& Owner) -> TSet<FCk_Handle>
    {
        TSet<FCk_Handle> Result;
        if (!IsLive(Owner)) { return Result; }
        for (const FCk_Handle& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owner))
        { if (IsLive(Child)) { Result.Add(Child); } }
        return Result;
    }

    auto SoleNew(const FCk_Handle& Owner, const TSet<FCk_Handle>& Before,
                 FCk_Handle& Out) -> bool
    {
        Out = {};
        TSet<FCk_Handle> Added = Direct(Owner);
        for (const auto& Old : Before) { Added.Remove(Old); }
        if (Added.Num() != 1) { return false; }
        Out = *Added.CreateConstIterator();
        return IsLive(Out) && Out.Has<ck::FFragment_LifetimeOwner>() &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Out) == Owner;
    }

    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AGameStateBase> State;
        TWeakObjectPtr<UCkCrowdGym_SharedSimulation> Component;
        FCk_Handle Root;
        TArray<FCk_Handle> Batches;
        TArray<FCk_Handle> Agents;
        int64 Generation = 0;
        int64 BatchSerial = 0;
        int64 MaxAgentSerial = 0;
        int32 Count = 0;
        bool bInitialized = false;
        bool bAllPhases = true;
    };

    auto ReadyFor(const FRun& Run, int64 Serial) -> bool
    {
        const auto* Component = Run.Component.Get();
        return IsValid(Component) && !Component->Get_HasFailed() &&
            !Component->Get_IsBusy() && Component->Get_IsReady() &&
            Component->Get_LastCompletedBatchSerial() == Serial;
    }

    auto ExpectedPosition(int32 Scenario, int32 Index) -> FVector
    {
        const FVector Centre = Origin + FVector(0.0, 0.0, 100.0);
        if (Scenario == 0) { return Centre + FVector(0.0, Index == 0 ? 600.0 : -600.0, 0.0); }
        if (Scenario == 1) { return Centre + FVector(Index == 0 ? 600.0 : -600.0, 0.0, 0.0); }
        if (Scenario == 2)
        {
            if (Index < 2) { return Centre + FVector(0.0, Index == 0 ? 600.0 : -600.0, 0.0); }
            return Centre + FVector(Index == 2 ? 600.0 : -600.0, 0.0, 0.0);
        }
        const double Angle = 2.0 * PI * Index / 5.0;
        return Centre + FVector(600.0 * FMath::Cos(Angle), 600.0 * FMath::Sin(Angle), 0.0);
    }

    auto ExpectedColor(int32 Scenario, int32 Index) -> FLinearColor
    {
        if (Scenario == 1)
        {
            return Index == 0 ? FLinearColor(0.85, 1.0, 0.42, 0.6) :
                                FLinearColor(1.0, 0.85, 0.42, 0.6);
        }
        if (Scenario == 0 || Scenario == 2)
        {
            const FLinearColor Colors[4] = {
                FLinearColor(0.42, 0.85, 1.0, 0.6), FLinearColor(1.0, 0.42, 0.85, 0.6),
                FLinearColor(0.85, 1.0, 0.42, 0.6), FLinearColor(1.0, 0.85, 0.42, 0.6)};
            return Colors[Index];
        }
        const double Hue = (360.0 / 5.0) * Index;
        return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue * 255.0 / 360.0), 200, 220);
    }

    auto CaptureAdmittedBatch(FRun& Run, int32 Scenario, const TSet<FCk_Handle>& Before) -> bool
    {
        FCk_Handle Batch;
        if (!SoleNew(Run.Root, Before, Batch)) { return false; }
        const int32 Expected = Scenario == 3 ? 5 : Scenario == 2 ? 4 : 2;
        const TSet<FCk_Handle> Children = Direct(Batch);
        if (Children.Num() != Expected) { return false; }
        TSet<int32> SeenPositions;
        for (const FCk_Handle& Child : Children)
        {
            if (!IsLive(Child) || !Child.Has<ck::FFragment_LifetimeOwner>() ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) != Batch ||
                !UCk_Utils_Transform_UE::Has(Child) ||
                !UCk_Utils_CrowdAgent_UE::Has(Child) ||
                !Child.Has<ck::FFragment_CrowdAgent_Tunables>() ||
                !Child.Has<ck::FFragment_CrowdAgent_PathFollow>() ||
                !Child.Has<ck::FFragment_CrowdAgent_DesiredVelocity>() ||
                !UCk_Utils_Velocity_UE::Has(Child) ||
                !UCk_Utils_Acceleration_UE::Has(Child) ||
                !Child.Has<ck::FFragment_EulerIntegrator>() ||
                !Child.Has<ck::FTag_EulerIntegrator_NeedsUpdate>())
            { return false; }
            const auto Transform = UCk_Utils_Transform_UE::CastChecked(Child);
            const FVector Position = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                Transform).GetLocation();
            const auto Crowd = UCk_Utils_CrowdAgent_UE::CastChecked(Child);
            bool Matched = false;
            for (int32 Index = 0; Index < Expected; ++Index)
            {
                if (SeenPositions.Contains(Index) ||
                    !Position.Equals(ExpectedPosition(Scenario, Index), 0.1) ||
                    !UCk_Utils_CrowdAgent_UE::Get_DebugColor(Crowd).Equals(
                        ExpectedColor(Scenario, Index), 0.001f)) { continue; }
                SeenPositions.Add(Index);
                Matched = true;
                break;
            }
            if (!Matched) { return false; }
            Run.Agents.Add(Child);
        }
        if (SeenPositions.Num() != Expected) { return false; }
        Run.Batches.Add(Batch);
        return true;
    }

    auto Sample(FAutomationTestBase& Test, FRun& Run, int64 Sequence,
                int32 ExpectedCount, int64 MinimumNewSerial = 0) -> bool
    {
        auto* Component = Run.Component.Get();
        if (!IsValid(Component) || !ReadyFor(Run, Run.BatchSerial)) { return false; }
        TArray<FCkCrowdRemoteAgentRow> Rows;
        FCkCrowdRemoteAgentDetail Detail;
        if (!Test.TestTrue(TEXT("complete server Crowd roster sample"),
            Component->TrySampleRosterAtSequence(Sequence, {}, nullptr, 0, Rows, Detail)) ||
            !Test.TestEqual(TEXT("exact committed Crowd roster count"), Rows.Num(), ExpectedCount))
        { return false; }
        TSet<FCkCrowdRemoteAgentId> Ids;
        TSet<FCk_Handle> ResolvedAgents;
        int64 MaxSerial = Run.MaxAgentSerial;
        for (const auto& Row : Rows)
        {
            const bool Valid = Row.Id.WorldGeneration == Run.Generation &&
                Row.Id.AgentSerial > 0 && Row.SampleSequence == Sequence &&
                Row.ConfigurationRevision == 1 && Row.OwnerSerial == 0 &&
                Row.RadiusCm == 42.0f && Row.HeightCm == 192.0f &&
                Row.WorldTransform.IsValid() &&
                Row.Position.Equals(Row.WorldTransform.GetLocation(), 0.1f) &&
                FCkCrowdRemoteDebugValidation::IsValid(Row) && !Ids.Contains(Row.Id);
            if (!Test.TestTrue(TEXT("typed world-owned Crowd row"), Valid)) { return false; }
            Ids.Add(Row.Id);
            FCk_Handle_CrowdAgent Resolved;
            const bool ResolvedValid = Component->TryResolveAgent(
                Row.Id, Row.ConfigurationRevision, Resolved);
            const FCk_Handle Agent{Resolved};
            if (!Test.TestTrue(TEXT("each published ID resolves one distinct captured agent"),
                ResolvedValid && IsLive(Agent) && Run.Agents.Contains(Agent) &&
                !ResolvedAgents.Contains(Agent) &&
                UCk_Utils_Transform_UE::Has(Agent) && UCk_Utils_CrowdAgent_UE::Has(Agent)))
            { return false; }
            const auto Transform = UCk_Utils_Transform_UE::CastChecked(Agent);
            const auto Crowd = UCk_Utils_CrowdAgent_UE::CastChecked(Agent);
            if (!Test.TestTrue(TEXT("published pose and color match the resolved production agent"),
                Row.WorldTransform.Equals(
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform), 0.01f) &&
                Row.BaseColor.Equals(UCk_Utils_CrowdAgent_UE::Get_DebugColor(Crowd), 0.001f)))
            { return false; }
            ResolvedAgents.Add(Agent);
            MaxSerial = FMath::Max(MaxSerial, Row.Id.AgentSerial);
        }
        if (!Test.TestEqual(TEXT("resolved ID count equals captured agent count"),
            ResolvedAgents.Num(), Run.Agents.Num())) { return false; }
        for (const FCk_Handle& Agent : Run.Agents)
        {
            if (!Test.TestTrue(TEXT("every captured agent has one published ID"),
                ResolvedAgents.Contains(Agent))) { return false; }
        }
        if (MinimumNewSerial > 0)
        {
            for (const auto& Row : Rows)
            {
                if (Row.Id.AgentSerial <= Run.MaxAgentSerial ||
                    Row.Id.AgentSerial < MinimumNewSerial) { return false; }
            }
        }
        TArray<FCkCrowdRemoteAgentRow> Repeated;
        FCkCrowdRemoteAgentDetail RepeatedDetail;
        if (!Test.TestTrue(TEXT("repeated same-epoch sample remains pure"),
            Component->TrySampleRosterAtSequence(Sequence, {}, nullptr, 0,
                Repeated, RepeatedDetail)) || Repeated.Num() != Rows.Num()) { return false; }
        for (int32 Index = 0; Index < Rows.Num(); ++Index)
        {
            if (!(Rows[Index].Id == Repeated[Index].Id) ||
                Rows[Index].SampleSequence != Repeated[Index].SampleSequence ||
                !Rows[Index].WorldTransform.Equals(Repeated[Index].WorldTransform, 0.01f))
            { return false; }
        }
        if (ExpectedCount > 0)
        {
            FCkCrowdRemoteAgentDetail Selected;
            TArray<FCkCrowdRemoteAgentRow> SelectedRows;
            if (!Test.TestTrue(TEXT("selected detail shares world sample epoch"),
                Component->TrySampleRosterAtSequence(Sequence, {}, &Rows[0].Id, 41,
                    SelectedRows, Selected)) || SelectedRows.Num() != ExpectedCount ||
                !(Selected.Id == Rows[0].Id) || Selected.SampleSequence != Sequence ||
                Selected.SelectionRequestSerial != 41 ||
                Selected.ConfigurationRevision != Rows[0].ConfigurationRevision ||
                !(Selected.Sample.Id == Rows[0].Id)) { return false; }
            FCk_Handle_CrowdAgent Resolved;
            if (!Test.TestTrue(TEXT("selected ID resolves exact authoritative agent"),
                Component->TryResolveAgent(Rows[0].Id, 1, Resolved)) ||
                !Run.Agents.Contains(FCk_Handle{Resolved})) { return false; }
            FCk_Handle_CrowdAgent RejectedAgent = Resolved;
            int64 RejectedRevision = 77;
            if (!Test.TestFalse(TEXT("zero revision cannot resolve agent"),
                Component->TryResolveAgent(Rows[0].Id, 0, RejectedAgent)) ||
                ck::IsValid(RejectedAgent) ||
                !Test.TestFalse(TEXT("zero revision cannot advance configuration"),
                    Component->TryAdvanceConfigurationRevision(
                        Rows[0].Id, 0, RejectedRevision)) || RejectedRevision != 0)
            { return false; }
        }
        TArray<FCkCrowdRemoteAgentRow> Reject = Rows;
        FCkCrowdRemoteAgentDetail RejectDetail;
        if (!Test.TestFalse(TEXT("zero sample sequence rejects"),
            Component->TrySampleRosterAtSequence(0, {}, nullptr, 0, Reject, RejectDetail)) ||
            !Test.TestEqual(TEXT("zero sequence clears candidate rows"), Reject.Num(), 0))
        { return false; }
        Reject = Rows;
        if (!Test.TestFalse(TEXT("MAX sample sequence rejects"),
            Component->TrySampleRosterAtSequence(TNumericLimits<int64>::Max(), {},
                nullptr, 0, Reject, RejectDetail)) || Reject.Num() != 0) { return false; }
        if (ExpectedCount > 0)
        {
            FCkCrowdRemoteAgentId Missing;
            Missing.WorldGeneration = Run.Generation;
            Missing.AgentSerial = MaxSerial + 100;
            Reject = Rows;
            if (!Test.TestFalse(TEXT("missing selected ID rejects without partial rows"),
                Component->TrySampleRosterAtSequence(Sequence, {}, &Missing, 42,
                    Reject, RejectDetail)) || Reject.Num() != 0) { return false; }
            Reject = Rows;
            if (!Test.TestFalse(TEXT("zero selected request serial rejects"),
                Component->TrySampleRosterAtSequence(Sequence, {}, &Rows[0].Id, 0,
                    Reject, RejectDetail)) || Reject.Num() != 0) { return false; }
        }
        if (!Test.TestEqual(TEXT("sampling leaves completed batch serial unchanged"),
            Component->Get_LastCompletedBatchSerial(), Run.BatchSerial) ||
            !Test.TestTrue(TEXT("sampling leaves exact batch membership unchanged"),
                Direct(Run.Root).Num() == Run.Batches.Num())) { return false; }
        Run.MaxAgentSerial = MaxSerial;
        return true;
    }

    class FAlwaysDestroy final : public IAutomationLatentCommand
    {
    public:
        explicit FAlwaysDestroy(TSharedRef<FRun> Run) : SavedRun(MoveTemp(Run)) {}
        bool Update() override
        {
            if (auto* Component = SavedRun->Component.Get())
            { Component->DestroyComponent(); }
            return true;
        }
    private:
        TSharedRef<FRun> SavedRun;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdSharedSimulation_Network,
    "Ck.Crowd.SharedSimulation.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCrowdSharedSimulation_Network::RunTest(const FString& Parameters)
{
    using namespace ck_crowd_shared_simulation_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Crowd shared-simulation topology")); return false; }
    const TSharedRef<FRun> Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || !IsValid(Server->GetGameState<AGameStateBase>()) ||
                !IsLive(Transient(*Server))) { return false; }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                UWorld* Client = ck::auto_test::net::Get_ClientWorld(Index);
                if (!IsValid(Client) || !IsValid(Client->GetGameState<AGameStateBase>()) ||
                    !IsLive(Transient(*Client)) ||
                    !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, Index)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("one authority GameState and two correlated remote clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            auto* State = IsValid(World) ? World->GetGameState<AGameStateBase>() : nullptr;
            if (!TestTrue(TEXT("real authority GameState"), IsValid(State) &&
                State->HasAuthority() && State->GetWorld() == World)) { return; }
            Run->World = World;
            Run->State = State;
            auto* Component = NewObject<UCkCrowdGym_SharedSimulation>(State);
            State->AddInstanceComponent(Component);
            Component->RegisterComponent();
            Run->Component = Component;
            const FCk_Handle WorldRoot = Transient(*World);
            const auto Before = Direct(WorldRoot);
            Run->bInitialized = TestTrue(TEXT("GameState component initializes one world cohort"),
                Component->TryInitialize(FTransform(FRotator::ZeroRotator, Origin)));
            Run->bAllPhases &= Run->bInitialized;
            if (!Run->bInitialized) { return; }
            Run->bAllPhases &= TestTrue(TEXT("sole world-root child is independent of a PC"),
                SoleNew(WorldRoot, Before, Run->Root));
            Run->Generation = Component->Get_WorldGeneration();
            Run->bAllPhases &= TestTrue(TEXT("positive ready empty world generation"),
                Run->Generation > 0 && Component->Get_IsReady() &&
                !Component->Get_IsBusy() && !Component->Get_HasFailed() &&
                Component->Get_LastCompletedBatchSerial() == 0 &&
                Direct(Run->Root).IsEmpty());
            int64 Ignored = 77;
            Run->bAllPhases &= TestFalse(TEXT("same component cannot reinitialize"),
                Component->TryInitialize(FTransform::Identity));
            Run->bAllPhases &= TestFalse(TEXT("negative scenario rejected"),
                Component->BeginAppendScenario(-1, Ignored));
            Run->bAllPhases &= TestEqual(TEXT("negative scenario clears serial"), Ignored, int64{0});
            Ignored = 77;
            Run->bAllPhases &= TestFalse(TEXT("unknown scenario rejected"),
                Component->BeginAppendScenario(4, Ignored));
            Run->bAllPhases &= TestTrue(TEXT("invalid scenarios leave empty world unchanged"),
                Ignored == 0 && Direct(Run->Root).IsEmpty() &&
                Component->Get_IsReady() && Component->Get_WorldGeneration() == Run->Generation);
        })));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
            {
                auto* State = IsValid(World) ? World->GetGameState<AGameStateBase>() : nullptr;
                if (!TestTrue(TEXT("real remote client GameState"), IsValid(State) &&
                    !State->HasAuthority() && State->GetWorld() == World)) { return; }
                const FCk_Handle WorldRoot = Transient(*World);
                const TSet<FCk_Handle> Before = Direct(WorldRoot);
                auto* Component = NewObject<UCkCrowdGym_SharedSimulation>(State);
                State->AddInstanceComponent(Component);
                Component->RegisterComponent();
                TestFalse(TEXT("client cannot initialize shared simulation"),
                    Component->TryInitialize(FTransform(FRotator::ZeroRotator, Origin)));
                bool Unchanged = !Component->Get_IsReady() && !Component->Get_HasFailed() &&
                    Component->Get_WorldGeneration() == 0;
                const TSet<FCk_Handle> After = Direct(WorldRoot);
                Unchanged &= Before.Num() == After.Num();
                for (const auto& Handle : Before) { Unchanged &= After.Contains(Handle); }
                TestTrue(TEXT("client rejection adds no Crowd ECS world root"), Unchanged);
                Component->DestroyComponent();
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            auto* Component = Run->Component.Get();
            if (!TestTrue(TEXT("authority shared component remains ready"),
                IsValid(Component) && Component->Get_IsReady() && IsLive(Run->Root))) { return; }
            const auto Before = Direct(Run->Root);
            int64 Serial = 0;
            const bool Accepted = Component->BeginAppendScenario(0, Serial);
            Run->bAllPhases &= TestTrue(TEXT("north/south batch admitted"), Accepted);
            if (!Accepted) { return; }
            Run->BatchSerial = Serial;
            Run->bAllPhases &= TestTrue(TEXT("first complete batch composed before publication"),
                Serial > 0 && Component->Get_IsBusy() &&
                Component->Get_LastCompletedBatchSerial() == 0 &&
                CaptureAdmittedBatch(*Run, 0, Before));
            TArray<FCkCrowdRemoteAgentRow> Unpublished;
            FCkCrowdRemoteAgentDetail UnpublishedDetail;
            Run->bAllPhases &= TestFalse(TEXT("pending append cannot publish subset"),
                Component->TrySampleRosterAtSequence(FirstSequence, {}, nullptr, 0,
                    Unpublished, UnpublishedDetail));
            Run->bAllPhases &= TestEqual(TEXT("pending append has no sampled rows"),
                Unpublished.Num(), 0);
            int64 BusySerial = 77;
            Run->bAllPhases &= TestFalse(TEXT("second request cannot mutate pending batch"),
                Component->BeginAppendScenario(1, BusySerial));
            Run->bAllPhases &= TestTrue(TEXT("busy rejection leaves pending serial and membership"),
                BusySerial == 0 && Component->Get_IsBusy() &&
                Component->Get_LastCompletedBatchSerial() == 0 &&
                Direct(Run->Root).Num() == 1);
            BusySerial = 77;
            Run->bAllPhases &= TestFalse(TEXT("clear cannot interrupt pending append"),
                Component->BeginClear(BusySerial));
            Run->bAllPhases &= TestEqual(TEXT("busy clear returns no serial"),
                BusySerial, int64{0});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->BatchSerial > 0 && ReadyFor(*Run, Run->BatchSerial); }),
        20.0, TEXT("north/south MoveTo completions commit atomically")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            Run->Count = 2;
            Run->bAllPhases &= Sample(*this, *Run, FirstSequence, Run->Count);
        })));
    const int32 Scenarios[3] = {1, 2, 3};
    const int32 AddCounts[3] = {2, 4, 5};
    for (int32 Step = 0; Step < 3; ++Step)
    {
        const int32 Scenario = Scenarios[Step];
        const int32 NewCount = AddCounts[Step];
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, Scenario](UWorld*)
            {
                auto* Component = Run->Component.Get();
                if (!TestTrue(TEXT("committed roster ready before next append"),
                    IsValid(Component) && Component->Get_IsReady())) { return; }
                const auto Before = Direct(Run->Root);
                int64 Serial = 0;
                const bool Accepted = Component->BeginAppendScenario(Scenario, Serial);
                Run->bAllPhases &= TestTrue(TEXT("authored next scenario admitted"), Accepted);
                if (!Accepted) { return; }
                Run->bAllPhases &= TestTrue(TEXT("new batch serial and exact initial geometry"),
                    Serial > Run->BatchSerial && CaptureAdmittedBatch(*Run, Scenario, Before));
                Run->BatchSerial = Serial;
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
            { return Run->BatchSerial > 0 && ReadyFor(*Run, Run->BatchSerial); }),
            20.0, TEXT("entire added Crowd scenario committed")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, NewCount, Step](UWorld*)
            {
                Run->Count += NewCount;
                Run->bAllPhases &= Sample(*this, *Run, FirstSequence + Step + 1, Run->Count);
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            auto* Component = Run->Component.Get();
            if (!TestTrue(TEXT("all thirteen world agents ready before clear"),
                IsValid(Component) && Component->Get_IsReady() && Run->Count == 13 &&
                Run->Agents.Num() == 13 && Run->Batches.Num() == 4)) { return; }
            int64 Serial = 0;
            const bool Accepted = Component->BeginClear(Serial);
            Run->bAllPhases &= TestTrue(TEXT("clear admitted as pending retirement"), Accepted);
            if (!Accepted) { return; }
            Run->bAllPhases &= TestTrue(TEXT("clear withholds successful partial roster"),
                Serial > Run->BatchSerial && Component->Get_IsBusy() &&
                !Component->Get_IsReady() && Component->Get_LastCompletedBatchSerial() ==
                    Run->BatchSerial);
            Run->BatchSerial = Serial;
            TArray<FCkCrowdRemoteAgentRow> Reject;
            FCkCrowdRemoteAgentDetail Detail;
            Run->bAllPhases &= TestFalse(TEXT("pending clear cannot publish subset"),
                Component->TrySampleRosterAtSequence(200, {}, nullptr, 0, Reject, Detail));
            Run->bAllPhases &= TestEqual(TEXT("pending clear has no sampled rows"), Reject.Num(), 0);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (!ReadyFor(*Run, Run->BatchSerial)) { return false; }
            for (const auto& Batch : Run->Batches) { if (!IsGone(Batch)) { return false; } }
            for (const auto& Agent : Run->Agents) { if (!IsGone(Agent)) { return false; } }
            return IsLive(Run->Root) && Direct(Run->Root).IsEmpty();
        }), 20.0, TEXT("all old Crowd batch descendants retire before empty ready commit")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            Run->Count = 0;
            Run->bAllPhases &= TestTrue(TEXT("clear preserves world generation"),
                Run->Component.IsValid() &&
                Run->Component->Get_WorldGeneration() == Run->Generation);
            Run->Batches.Reset();
            Run->Agents.Reset();
            Run->bAllPhases &= Sample(*this, *Run, 201, 0);
            const auto Before = Direct(Run->Root);
            int64 Serial = 0;
            const bool Accepted = Run->Component->BeginAppendScenario(0, Serial);
            Run->bAllPhases &= TestTrue(TEXT("same world accepts fresh post-clear scenario"), Accepted);
            if (!Accepted) { return; }
            Run->bAllPhases &= TestTrue(TEXT("post-clear batch uses fresh serial and exact geometry"),
                Serial > Run->BatchSerial && CaptureAdmittedBatch(*Run, 0, Before));
            Run->BatchSerial = Serial;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        { return Run->BatchSerial > 0 && ReadyFor(*Run, Run->BatchSerial); }),
        20.0, TEXT("new post-clear pair commits under retained world generation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            Run->Count = 2;
            Run->bAllPhases &= Sample(*this, *Run, 202, 2, Run->MaxAgentSerial + 1);
            Run->bAllPhases &= TestTrue(TEXT("same world root survives every batch and reset"),
                Run->Component.IsValid() && IsLive(Run->Root) &&
                Run->Component->Get_WorldGeneration() == Run->Generation &&
                Direct(Run->Root).Num() == 1);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FAlwaysDestroy(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            if (!IsGone(Run->Root)) { return false; }
            for (const auto& Batch : Run->Batches) { if (!IsGone(Batch)) { return false; } }
            for (const auto& Agent : Run->Agents) { if (!IsGone(Agent)) { return false; } }
            return true;
        }), 20.0, TEXT("GameState component EndPlay retires full world-owned tree")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            return TestTrue(TEXT("all shared-simulation phases and worlds complete"),
                Run->bInitialized && Run->bAllPhases &&
                ck::auto_test::net::Get_AllPIEWorlds().Num() == 3 &&
                IsGone(Run->Root));
        }), TEXT("authoritative shared Crowd simulation lifetime")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
