#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCrowdGym_NetworkRuntime.h"
#include "CkCrowdGym_SharedSimulation.h"
#include "CkGym_StartupSettings.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkPhysics/EulerIntegrator/CkEulerIntegrator_Fragment.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/PlayerController.h>
#include <Misc/AutomationTest.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_crowd_authority_terminal_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_GameState");
    constexpr auto ProbePath = TEXT("/Script/Angelscript.Ck_CrowdAuthorityTerminal_AdmissionProbe");

    auto Live(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }
    auto WorldRoot(UWorld* W) -> FCk_Handle
    { return IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) : FCk_Handle{}; }
    auto State(UWorld* W) -> ACkCrowdGym_GameState_UE*
    { return IsValid(W) ? W->GetGameState<ACkCrowdGym_GameState_UE>() : nullptr; }
    auto Children(const FCk_Handle& Owner, const FCk_Handle& World, TArray<FCk_Handle>& Out) -> bool
    {
        Out.Reset();
        return Live(Owner) && Live(World) &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Owner, World, Out);
    }
    auto Exact(const TArray<FCk_Handle>& Actual, std::initializer_list<FCk_Handle> Expected) -> bool
    {
        if (Actual.Num() != static_cast<int32>(Expected.size())) { return false; }
        for (const auto& H : Expected) { if (!Actual.Contains(H)) { return false; } }
        return true;
    }
    auto OwnerOf(const FCk_Handle& Child, const FCk_Handle& Parent) -> bool
    {
        return Live(Child) && Live(Parent) &&
            UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Parent;
    }

    struct FRun
    {
        TWeakObjectPtr<UWorld> Server;
        TWeakObjectPtr<ACkCrowdGym_GameState_UE> State;
        TWeakObjectPtr<UCkCrowdGym_SharedSimulation> Shared;
        FCk_Handle World, Source, Batch, Agents[2], Unknown;
        FTransform AgentPoses[2];
        int64 Generation = 0, Sequence = 0, BatchSerial = 0;
        UClass* StateClass = nullptr;
        UClass* ProbeClass = nullptr;
        ECkGym_StartupMode OldStartup = ECkGym_StartupMode::Cycler;
        FString OldLast;
        TArray<FString> OldRecents;
        bool Failed = false;
        int32 StableFrames = 0;
    };
    auto KnownAgentsFrozen(const FRun& R) -> bool
    {
        for (int32 I = 0; I < 2; ++I)
        {
            const auto& Agent = R.Agents[I];
            if (!Live(Agent) || !UCk_Utils_Transform_UE::Has(Agent) ||
                Agent.Has<ck::FFragment_EulerIntegrator>() ||
                Agent.Has<ck::FTag_EulerIntegrator_NeedsUpdate>())
            { return false; }
            const auto Transform = UCk_Utils_Transform_UE::Cast(Agent);
            if (!ck::IsValid(Transform) ||
                !UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform).Equals(
                    R.AgentPoses[I], 0.01f))
            { return false; }
        }
        return true;
    }
    auto FlagIs(UObject* Object, FName Name, bool Value) -> bool
    {
        const auto* P = IsValid(Object) ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr;
        return P && P->GetPropertyValue_InContainer(Object) == Value;
    }
    auto Probe(UObject* Object, FName Name, const FCk_Handle& ExpectedWorld) -> bool
    {
        auto* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        auto* WorldArg = Function ? FindFProperty<FStructProperty>(Function, TEXT("InWorld")) : nullptr;
        auto* Return = Function ? FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!Function || Function->NumParms != 2 || !WorldArg ||
            WorldArg->Struct != FCk_Handle::StaticStruct() || !Return ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm)) { return false; }
        FStructOnScope Params(Function);
        WorldArg->Struct->CopyScriptStruct(
            WorldArg->ContainerPtrToValuePtr<void>(Params.GetStructMemory()), &ExpectedWorld);
        Object->ProcessEvent(Function, Params.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Params.GetStructMemory());
    }
    auto Ready(const FRun& R) -> bool
    {
        auto* W = ck::auto_test::net::Get_ServerWorld();
        auto* S = State(W);
        auto* Shared = IsValid(S) ? S->FindComponentByClass<UCkCrowdGym_SharedSimulation>() : nullptr;
        if (!IsValid(W) || !IsValid(S) || S->GetClass() != R.StateClass ||
            !IsValid(W->GetAuthGameMode()) ||
            !IsValid(Shared) || !Shared->Get_IsReady() ||
            !S->Get_HasReadyServerSimulation() || !S->Get_HasCompleteSnapshot() ||
            !FlagIs(S, TEXT("SharedStarted"), true) ||
            !FlagIs(S, TEXT("SharedFailed"), false) ||
            S->Get_RowsSnapshot().Rows.Num() != 0 ||
            ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) { return false; }
        const auto Generation = S->Get_RowsSnapshot().WorldGeneration;
        for (int32 I = 0; I < 2; ++I)
        {
            auto* C = State(ck::auto_test::net::Get_ClientWorld(I));
            if (!IsValid(C) || C->GetClass() != R.StateClass ||
                !C->Get_HasCompleteSnapshot() ||
                C->Get_RowsSnapshot().WorldGeneration != Generation ||
                C->Get_RowsSnapshot().Rows.Num() != 0 ||
                !IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(W, I)))
            { return false; }
        }
        return true;
    }
    auto PublishedTwo(const FRun& R) -> bool
    {
        auto* S = R.State.Get(); auto* Shared = R.Shared.Get();
        if (!IsValid(S) || !IsValid(Shared) || !Shared->Get_IsReady() ||
            Shared->Get_IsBusy() || Shared->Get_HasFailed() ||
            (R.BatchSerial > 0 ? Shared->Get_LastCompletedBatchSerial() != R.BatchSerial :
                Shared->Get_LastCompletedBatchSerial() <= 0) ||
            !S->Get_HasCompleteSnapshot()) { return false; }
        const auto Snapshot = S->Get_RowsSnapshot();
        if (Snapshot.WorldGeneration != R.Generation || Snapshot.Rows.Num() != 2 ||
            Snapshot.SampleSequence <= R.Sequence) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* C = State(ck::auto_test::net::Get_ClientWorld(I));
            if (!IsValid(C) || !C->Get_HasCompleteSnapshot() ||
                C->Get_RowsSnapshot().WorldGeneration != R.Generation ||
                C->Get_RowsSnapshot().Rows.Num() != 2) { return false; }
        }
        return true;
    }
    class FRestore final : public IAutomationLatentCommand
    {
    public:
        explicit FRestore(TSharedRef<FRun> InRun) : Run(MoveTemp(InRun)) {}
        bool Update() override
        {
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = Run->OldStartup; S->LastGymName = Run->OldLast;
            S->RecentGymNames = Run->OldRecents; S->SaveConfig();
            return true;
        }
    private: TSharedRef<FRun> Run;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdAuthorityTerminal_Network,
    "Ck.Crowd.AuthorityTerminal.UnknownChild.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCrowdAuthorityTerminal_Network::RunTest(const FString& Parameters)
{
    using namespace ck_crowd_authority_terminal_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Crowd authority-terminal topology")); return false; }
    const auto R = MakeShared<FRun>();
    auto* Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    R->StateClass = FSoftClassPath(StatePath).TryLoadClass<ACkCrowdGym_GameState_UE>();
    R->ProbeClass = FSoftClassPath(ProbePath).TryLoadClass<UObject>();
    if (!TestTrue(TEXT("authored Crowd mode, state and test-only admission probe load"),
        Mode && R->StateClass && R->ProbeClass)) { return false; }
    // The terminal PrintString is a normal Log entry, which automation does not capture.
    // Its exact occurrence is verified from the physical run log; assertions below test behavior.
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    R->OldStartup = Settings->StartupMode; R->OldLast = Settings->LastGymName;
    R->OldRecents = Settings->RecentGymNames;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([R]() { return Ready(*R); }),
        60.0, TEXT("authored Crowd empty source and three-world projection ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* W)
        {
            auto* S = State(W);
            auto* Shared = IsValid(S) ? S->FindComponentByClass<UCkCrowdGym_SharedSimulation>() : nullptr;
            auto* ProbeObject = NewObject<UObject>(W, R->ProbeClass);
            if (!TestTrue(TEXT("real authored authority and open Common admission"),
                Ready(*R) && IsValid(Shared) && IsValid(ProbeObject) &&
                Probe(ProbeObject, TEXT("AdmissionOpen"), WorldRoot(W))))
            { R->Failed = true; return; }
            R->Server = W; R->State = S; R->Shared = Shared;
            R->World = WorldRoot(W);
            R->Generation = Shared->Get_WorldGeneration();
            R->Sequence = S->Get_RowsSnapshot().SampleSequence;
            if (!TestTrue(TEXT("actual authored source ready for owning control"),
                Live(R->World) && R->Generation > 0 &&
                Shared->Get_LastCompletedBatchSerial() == 0))
            { R->Failed = true; return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* W)
        {
            if (R->Failed) { return; }
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            auto* Session = IsValid(PC) ? PC->FindComponentByClass<UCkCrowdGym_Session_UE>() : nullptr;
            if (!TestTrue(TEXT("real local owner queues authored north-south control RPC"),
                IsValid(PC) && PC->IsLocalController() &&
                IsValid(PC->GetLocalPlayer()) && IsValid(Session) &&
                Session->GetIsReplicated() && Session->TryQueueControl(1)))
            { R->Failed = true; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([R]()
        { return R->Failed || PublishedTwo(*R); }),
        30.0, TEXT("real north-south agents commit and publish two rows in all worlds")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* W)
        {
            if (R->Failed) { return; }
            auto* S = State(W); auto* Shared = R->Shared.Get();
            if (!TestTrue(TEXT("exact published source still live"),
                S == R->State.Get() && PublishedTwo(*R) && IsValid(Shared)))
            { R->Failed = true; return; }
            R->BatchSerial = Shared->Get_LastCompletedBatchSerial();
            const auto Snapshot = S->Get_RowsSnapshot();
            FCk_Handle_CrowdAgent Typed[2];
            for (int32 I = 0; I < 2; ++I)
            {
                if (!TestTrue(TEXT("published real row resolves exact agent"),
                    Snapshot.Rows[I].ConfigurationRevision == 1 &&
                    Shared->TryResolveAgent(Snapshot.Rows[I].Id, 1, Typed[I])))
                { R->Failed = true; return; }
                R->Agents[I] = FCk_Handle{Typed[I]};
            }
            if (!TestTrue(TEXT("both published agents share exact batch owner"),
                Live(R->Agents[0]) && Live(R->Agents[1]) &&
                R->Agents[0] != R->Agents[1] &&
                UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(R->Agents[0]) &&
                UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(R->Agents[1])))
            { R->Failed = true; return; }
            R->Batch = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(R->Agents[0]);
            if (!TestTrue(TEXT("batch has one exact production source root"),
                OwnerOf(R->Agents[0], R->Batch) && OwnerOf(R->Agents[1], R->Batch) &&
                UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(R->Batch)))
            { R->Failed = true; return; }
            R->Source = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(R->Batch);
            TArray<FCk_Handle> SourceChildren, BatchChildren;
            if (!TestTrue(TEXT("exact retained source provenance and allocated cohort"),
                OwnerOf(R->Batch, R->Source) && OwnerOf(R->Source, R->World) &&
                Children(R->Source, R->World, SourceChildren) &&
                Children(R->Batch, R->World, BatchChildren) &&
                Exact(SourceChildren, {R->Batch}) &&
                Exact(BatchChildren, {R->Agents[0], R->Agents[1]})))
            { R->Failed = true; return; }
            for (int32 I = 0; I < 2; ++I)
            {
                const auto& Agent = R->Agents[I];
                if (!TestTrue(TEXT("known production agent initially has real Euler and transform"),
                    Live(Agent) && UCk_Utils_Transform_UE::Has(Agent) &&
                    Agent.Has<ck::FFragment_EulerIntegrator>() &&
                    Agent.Has<ck::FTag_EulerIntegrator_NeedsUpdate>()))
                { R->Failed = true; return; }
                const auto Transform = UCk_Utils_Transform_UE::Cast(Agent);
                if (!TestTrue(TEXT("known production agent transform cast is live"),
                    ck::IsValid(Transform))) { R->Failed = true; return; }
                R->AgentPoses[I] = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Transform);
            }
            R->Unknown = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(R->Source);
            int64 RejectedSerial = 777;
            if (!TestTrue(TEXT("real unknown direct child blocks append without new batch"),
                OwnerOf(R->Unknown, R->Source) &&
                Children(R->Source, R->World, SourceChildren) &&
                Exact(SourceChildren, {R->Batch, R->Unknown}) &&
                !Shared->BeginAppendScenario(1, RejectedSerial) && RejectedSerial == 0 &&
                Shared->Get_LastCompletedBatchSerial() == R->BatchSerial &&
                Shared->Get_WorldGeneration() == R->Generation))
            { R->Failed = true; return; }
            // Real production component tick processes the admitted cohort fault.
            Shared->TickComponent(0.0f, LEVELTICK_All, nullptr);
            auto* PostProbe = NewObject<UObject>(W, R->ProbeClass);
            if (!TestTrue(TEXT("same-stack Crowd failure closes authored and Common authority"),
                Shared->Get_HasFailed() && FlagIs(S, TEXT("SharedFailed"), true) &&
                FlagIs(S, TEXT("SharedStarted"), false) && IsValid(PostProbe) &&
                Probe(PostProbe, TEXT("ClosedAndReserveRejected"), R->World) &&
                Live(R->Unknown) && Live(R->Source) &&
                KnownAgentsFrozen(*R) &&
                Shared->Get_WorldGeneration() == R->Generation &&
                Shared->Get_LastCompletedBatchSerial() == R->BatchSerial))
            { R->Failed = true; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([R]()
        {
            auto* S = R->State.Get();
            return R->Failed || (IsValid(S) && !S->Get_HasCompleteSnapshot() &&
                !S->Get_HasReadyServerSimulation());
        }), 10.0, TEXT("native Crowd director retires without a new published generation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, R](UWorld* W)
        {
            if (R->Failed) { return; }
            auto* S = State(W); auto* ProbeObject = NewObject<UObject>(W, R->ProbeClass);
            TArray<FCk_Handle> ChildrenAfter;
            TestTrue(TEXT("terminal never destroys foreign child or retries publication"),
                IsValid(S) && !S->Get_HasCompleteSnapshot() &&
                R->Generation > 0 && Live(R->Unknown) && Live(R->Source) &&
                KnownAgentsFrozen(*R) &&
                Children(R->Source, R->World, ChildrenAfter) &&
                ChildrenAfter.Contains(R->Unknown) && IsValid(ProbeObject) &&
                Probe(ProbeObject, TEXT("ClosedAndReserveRejected"), R->World));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([this, R]() -> bool
        {
            if (R->Failed) { return true; }
            auto* W = R->Server.Get(); auto* S = R->State.Get();
            auto* ProbeObject = IsValid(W) ? NewObject<UObject>(W, R->ProbeClass) : nullptr;
            const bool Stable = IsValid(S) && !S->Get_HasCompleteSnapshot() &&
                FlagIs(S, TEXT("SharedFailed"), true) &&
                Live(R->Source) && Live(R->Unknown) &&
                KnownAgentsFrozen(*R) && IsValid(ProbeObject) &&
                Probe(ProbeObject, TEXT("ClosedAndReserveRejected"), R->World);
            if (!Stable)
            {
                AddError(TEXT("terminal Crowd source, foreign child, or Common closure changed during retained observation"));
                R->Failed = true; return true;
            }
            return ++R->StableFrames >= 30;
        }), 10.0, TEXT("terminal Crowd and Common closure remain stable for thirty real frames")));
    ADD_LATENT_AUTOMATION_COMMAND(FRestore(R));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
