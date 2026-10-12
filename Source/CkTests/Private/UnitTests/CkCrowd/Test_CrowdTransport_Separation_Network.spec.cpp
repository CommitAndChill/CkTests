#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCrowdGym_NetworkRuntime.h"
#include "CkCrowdRemoteBodyProjector.h"
#include "CkGym_StartupSettings.h"
#include "CkCrowd/Agent/CkCrowdAgent_Fragment.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkCrowd/Debug/CkCrowd_RemoteDebugRegistry.h"
#include "CkCrowd/Debug/CkCrowd_RemoteDebug_Utils.h"
#include "Test_CrowdRemoteDebug_ProviderProbe.h"
#include "CkCrowdDebugger/ViewModel/CkCrowdDebugger_ViewModel.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <EngineUtils.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <Misc/AutomationTest.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>
#include <UObject/UObjectIterator.h>

namespace ck_crowd_transport_fixture
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_PlayerController");
    constexpr auto FloorPath = TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_Floor");

    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
        H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    auto Gone(const FCk_Handle& H) -> bool
    { return ck::Is_NOT_Valid(H, ck::IsValid_Policy_IncludePendingKill{}); }
    auto Transient(UWorld* W) -> FCk_Handle
    { return IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) : FCk_Handle{}; }
    auto Crowd(UWorld* W) -> TSet<FCk_Handle>
    {
        TSet<FCk_Handle> Result;
        const auto Root = Transient(W);
        if (!Live(Root)) { return Result; }
        const auto Registry = Root.Get_RegistryView();
        Registry.View<ck::FFragment_CrowdAgent_Tunables>().ForEach(
            [&Result, &Registry](FCk_Entity Entity, const ck::FFragment_CrowdAgent_Tunables&)
            { Result.Add(FCk_Handle{Entity, Registry.Get_RegistryHandle()}); });
        return Result;
    }
    auto SameCommit(const FCkCrowdGym_OwnerCommit& A, const FCkCrowdGym_OwnerCommit& B) -> bool
    { return FCkCrowdGym_OwnerCommit::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None); }
    auto SameRows(const TArray<FCkCrowdRemoteAgentRow>& A, const TArray<FCkCrowdRemoteAgentRow>& B) -> bool
    {
        if (A.Num() != B.Num()) { return false; }
        for (int32 I = 0; I < A.Num(); ++I)
        { if (!FCkCrowdRemoteAgentRow::StaticStruct()->CompareScriptStruct(&A[I], &B[I], PPF_None)) { return false; } }
        return true;
    }
    auto LocalPC(UWorld* W) -> APlayerController*
    {
        auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
        const auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
        return IsValid(LP) && PC->IsLocalController() && PC->GetWorld() == W &&
            LP->GetWorld() == W && LP->GetPlayerController(W) == PC ? PC : nullptr;
    }
    auto State(UWorld* W) -> ACkCrowdGym_GameState_UE*
    { return IsValid(W) ? W->GetGameState<ACkCrowdGym_GameState_UE>() : nullptr; }
    auto Session(APlayerController* PC) -> UCkCrowdGym_Session_UE*
    { return IsValid(PC) ? PC->FindComponentByClass<UCkCrowdGym_Session_UE>() : nullptr; }
    auto ControlFeedback(APlayerController* PC, int64 Serial) -> bool
    {
        auto* SerialProperty = IsValid(PC) ? FindFProperty<FInt64Property>(PC->GetClass(), TEXT("LastControlSerial")) : nullptr;
        auto* OutcomeProperty = IsValid(PC) ? FindFProperty<FIntProperty>(PC->GetClass(), TEXT("LastControlOutcome")) : nullptr;
        return SerialProperty && OutcomeProperty && SerialProperty->GetPropertyValue_InContainer(PC) == Serial &&
            OutcomeProperty->GetPropertyValue_InContainer(PC) == int32(ECkCrowdGym_RequestOutcome::Accepted);
    }
    auto Projector(ACkCrowdGym_GameState_UE* S) -> UCkCrowdRemoteBodyProjector*
    {
        const auto* Property = FindFProperty<FObjectPropertyBase>(
            ACkCrowdGym_GameState_UE::StaticClass(), TEXT("Projector"));
        return IsValid(S) && Property ? Cast<UCkCrowdRemoteBodyProjector>(
            Property->GetObjectPropertyValue_InContainer(S)) : nullptr;
    }

    // Real reflected native functions, including real owning Reliable RPC dispatch. No private
    // member writes, fabricated pending state, delegate completions, or provider substitution.
    struct FCall
    {
        UObject* Target = nullptr;
        UFunction* Function = nullptr;
        TUniquePtr<FStructOnScope> Params;
        bool Valid = true;
        FCall(UObject* InTarget, FName Name, EFunctionFlags Required = FUNC_None)
            : Target(InTarget), Function(IsValid(InTarget) ? InTarget->FindFunction(Name) : nullptr)
        {
            Valid = Function && Function->HasAllFunctionFlags(Required);
            if (Valid) { Params = MakeUnique<FStructOnScope>(Function); }
        }
        void I64(FName Name, int64 Value)
        {
            auto* P = Function ? FindFProperty<FInt64Property>(Function, Name) : nullptr;
            if (!Valid || !P) { Valid = false; return; }
            P->SetPropertyValue_InContainer(Params->GetStructMemory(), Value);
        }
        void I32(FName Name, int32 Value)
        {
            auto* P = Function ? FindFProperty<FIntProperty>(Function, Name) : nullptr;
            if (!Valid || !P) { Valid = false; return; }
            P->SetPropertyValue_InContainer(Params->GetStructMemory(), Value);
        }
        void Bool(FName Name, bool Value)
        {
            auto* P = Function ? FindFProperty<FBoolProperty>(Function, Name) : nullptr;
            if (!Valid || !P) { Valid = false; return; }
            P->SetPropertyValue_InContainer(Params->GetStructMemory(), Value);
        }
        void Id(FName Name, const FCkCrowdRemoteAgentId& Value)
        {
            auto* P = Function ? FindFProperty<FStructProperty>(Function, Name) : nullptr;
            if (!Valid || !P || P->Struct != FCkCrowdRemoteAgentId::StaticStruct())
            { Valid = false; return; }
            *P->ContainerPtrToValuePtr<FCkCrowdRemoteAgentId>(Params->GetStructMemory()) = Value;
        }
        bool Send()
        { if (!Valid || !IsValid(Target)) { return false; }
          Target->ProcessEvent(Function, Params->GetStructMemory()); return true; }
        bool ReturnBool() const
        {
            const auto* P = Function ? FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue")) : nullptr;
            return Valid && P && P->GetPropertyValue_InContainer(Params->GetStructMemory());
        }
    };
    auto QueueControl(UCkCrowdGym_Session_UE* S, int32 Row) -> bool
    { FCall C(S, TEXT("TryQueueControl")); C.I32(TEXT("Row"), Row); return C.Send() && C.ReturnBool(); }
    auto QueueSelection(UCkCrowdGym_Session_UE* S, FCkCrowdRemoteAgentId Id, int64 Serial,
                        bool Clear = false) -> bool
    { FCall C(S, TEXT("TryQueueSelection")); C.Id(TEXT("Id"), Id); C.I64(TEXT("Serial"), Serial);
      C.Bool(TEXT("bClear"), Clear); return C.Send() && C.ReturnBool(); }
    auto Copy(UCkCrowdGym_Session_UE* S, FCkCrowdGym_OwnerCommit& Out) -> bool
    {
        Out = {};
        FCall C(S, TEXT("TryCopyRetainedCommit"));
        auto* P = C.Function ? FindFProperty<FStructProperty>(C.Function, TEXT("Out")) : nullptr;
        if (!P || P->Struct != FCkCrowdGym_OwnerCommit::StaticStruct() ||
            !C.Send() || !C.ReturnBool()) { return false; }
        Out = *P->ContainerPtrToValuePtr<FCkCrowdGym_OwnerCommit>(C.Params->GetStructMemory());
        return FCkCrowdGymWireValidation::IsValidCommit(Out);
    }
    auto Ack(UCkCrowdGym_Session_UE* S, int64 Delivery) -> bool
    { FCall C(S, TEXT("TryAcknowledgeCommit")); C.I64(TEXT("DeliverySerial"), Delivery);
      return C.Send() && C.ReturnBool(); }
    auto Teardown(UCkCrowdGym_Session_UE* S) -> bool
    { FCall C(S, TEXT("TryTeardownObsoletePawnSession")); return C.Send() && C.ReturnBool(); }
    auto RawSelection(UCkCrowdGym_Session_UE* S, FCkCrowdRemoteAgentId Id, int64 Serial,
                      int64 Generation) -> bool
    {
        FCall C(S, TEXT("Server_RequestSelection"), static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
        C.I64(TEXT("Serial"), Serial); C.Id(TEXT("Id"), Id); C.Bool(TEXT("bClear"), false);
        C.I64(TEXT("Generation"), Generation);
        C.I64(TEXT("SessionGeneration"), IsValid(S) ? S->Get_TransportSessionGeneration() : 0);
        return C.Send();
    }
    auto RawAck(UCkCrowdGym_Session_UE* S, const FCkCrowdGym_OwnerCommit& Commit,
                int64 SessionGeneration) -> bool
    {
        FCall C(S, TEXT("Server_AcknowledgeCommit"), static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
        C.I64(TEXT("Generation"), Commit.Snapshot.WorldGeneration);
        C.I64(TEXT("DeliverySerial"), Commit.DeliverySerial);
        C.I64(TEXT("SessionGeneration"), SessionGeneration); return C.Send();
    }
    auto RawTeardown(UCkCrowdGym_Session_UE* S, int64 Generation, int64 SessionGeneration) -> bool
    {
        FCall C(S, TEXT("Server_TeardownSession"), static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
        C.I64(TEXT("Generation"), Generation); C.I64(TEXT("SessionGeneration"), SessionGeneration);
        return C.Send();
    }

    struct FFixture
    {
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        UClass* FloorClass = nullptr;
        bool Dedicated = false;
        TWeakObjectPtr<UWorld> Server;
        TWeakObjectPtr<APlayerController> Remote[2];
        TWeakObjectPtr<APlayerController> Local[2];
        TWeakObjectPtr<APawn> OriginalPawn;
        TWeakObjectPtr<APawn> ReplacementPawn;
        TWeakObjectPtr<APawn> ClientOriginalPawn;
        bool OldTick[2]{};
        bool CapturedTicks = false;
        bool CapturedEndpoints = false;
        bool SharedRetirementRequested = false;
        FCkCrowdGym_OwnerCommit Installed[2]; // actual consumer-applied complete frozen packages
        FCkCrowdGym_OwnerCommit Frozen;
        TSharedPtr<FCkCrowdDebugger_ViewModel> Models[2];
        FCkCrowdDebugger_AgentSnapshot OldPick;
        FCkCrowdRemoteSelectionEnvelope PausedEnvelope;
        bool SawWholeInstall[2]{};
        FCkCrowdRemoteSelectionEnvelope Envelope[2]; // complete test-consumer installation, not debugger UI
        FCkCrowdRemoteSelectionEnvelope FrozenEnvelope;
        FCkCrowdRemoteSelectionReceipt OldReceipt;
        int64 Issued[2]{};
        int64 Generation = 0;
        int64 OldSession = 0;
        int64 BeforeAccepted[2]{};
        int64 BeforeBatch = 0;
        int64 WarmControlSerial = 0;
        int64 BeforeControlAccepted = 0;
        TSet<FCkCrowdRemoteAgentId> Ids;
        TSet<FCk_Handle> CapturedAgents;
        FDelegateHandle GoneHook;
        bool GoneHookFired = false;
        ECkGym_StartupMode OldStartup = ECkGym_StartupMode::Cycler;
        FString OldLast;
        TArray<FString> OldRecents;

        UCkCrowdGym_Session_UE* Client(int32 I) const { return Session(Local[I].Get()); }
        UCkCrowdGym_Session_UE* Authority(int32 I) const { return Session(Remote[I].Get()); }
        UCkCrowdGym_SharedSimulation* Shared() const
        { auto* S = State(Server.Get()); return S ? S->FindComponentByClass<UCkCrowdGym_SharedSimulation>() : nullptr; }
        void Restore()
        {
            Models[0].Reset(); Models[1].Reset();
            if (GoneHook.IsValid()) { FWorldDelegates::OnWorldPreActorTick.Remove(GoneHook); GoneHook.Reset(); }
            if (CapturedTicks)
            { for (int32 I = 0; I < 2; ++I) { if (Local[I].IsValid()) { Local[I]->SetActorTickEnabled(OldTick[I]); } } }
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = OldStartup; S->LastGymName = OldLast; S->RecentGymNames = OldRecents; S->SaveConfig();
        }
    };

    auto FloorReady(UWorld* W, UClass* Class) -> bool
    {
        if (!IsValid(W) || !Class) { return false; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(W, Class); It; ++It)
        {
            if (!IsValid(*It) || !It->GetActorLocation().Equals(FVector::ZeroVector) ||
                !It->GetActorRotation().IsNearlyZero() || !It->GetActorScale3D().Equals(FVector(20, 20, 0.5)))
            { return false; }
            ++Count;
        }
        return Count == 1;
    }
    auto Ready(const FFixture& F) -> bool
    {
        auto* W = ck::auto_test::net::Get_ServerWorld();
        auto* S = State(W);
        const auto Snap = S ? S->Get_RowsSnapshot() : FCkCrowdGym_RowsSnapshot{};
        if (!IsValid(W) || !IsValid(S) || !S->IsA(F.StateClass) ||
            !IsValid(W->GetAuthGameMode()) || W->GetAuthGameMode()->GetClass() != F.Mode ||
            !S->Get_HasReadyServerSimulation() || !FCkCrowdGymWireValidation::IsValidReadySnapshot(Snap) ||
            Snap.Rows.Num() != 0 || !FloorReady(W, F.FloorClass) || ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* C = ck::auto_test::net::Get_ClientWorld(I);
            auto* CS = State(C);
            auto* PC = LocalPC(C);
            auto* Remote = ck::auto_test::net::TryGet_RemoteClientPlayerController(W, I);
            TArray<FCkCrowdRemoteAgentRow> Rows;
            if (!FloorReady(C, F.FloorClass) || !IsValid(CS) || !CS->IsA(F.StateClass) || !CS->Get_HasPublishedCatalog() ||
                !IsValid(PC) || !PC->IsA(F.PCClass) || !IsValid(PC->GetPawn()) ||
                !IsValid(Remote) || !Remote->IsA(F.PCClass) || !IsValid(Remote->GetPawn()) ||
                !IsValid(Session(PC)) || !Session(PC)->GetIsReplicated() ||
                !IsValid(Session(Remote)) || !CS->Get_HasCompleteSnapshot() ||
                CS->Get_RowsSnapshot().WorldGeneration != Snap.WorldGeneration ||
                !FCkCrowdRemoteDebugRegistry::TryCopyRows(*C, *PC, *PC->GetLocalPlayer(), Rows))
            { return false; }
        }
        return true;
    }

    auto WorldProof(FAutomationTestBase& Test, FFixture& F, int32 Count) -> bool
    {
        auto* S = State(F.Server.Get());
        auto* Shared = F.Shared();
        if (!Test.TestTrue(TEXT("sole real registered authority simulation"), IsValid(S) && IsValid(Shared) &&
            S->Get_HasReadyServerSimulation() && F.Server->GetNetMode() ==
            (F.Dedicated ? NM_DedicatedServer : NM_ListenServer))) { return false; }
        const auto Snap = S->Get_RowsSnapshot();
        if (!Test.TestTrue(TEXT("complete same-generation server snapshot and exact all-registry Crowd count"),
            FCkCrowdGymWireValidation::IsValidReadySnapshot(Snap) && Snap.WorldGeneration == F.Generation &&
            Snap.Rows.Num() == Count && Crowd(F.Server.Get()).Num() == Count)) { return false; }
        TArray<UCkCrowdGym_SharedSimulation*> Simulations;
        S->GetComponents(Simulations);
        if (!Test.TestEqual(TEXT("one GameState SharedSimulation default component"), Simulations.Num(), 1)) { return false; }
        for (const auto& Row : Snap.Rows)
        {
            FCk_Handle_CrowdAgent Agent;
            if (!Test.TestTrue(TEXT("each world wire ID resolves sole production roster, revision unchanged"),
                Row.ConfigurationRevision == 1 && Shared->TryResolveAgent(Row.Id, 1, Agent) &&
                Crowd(F.Server.Get()).Contains(FCk_Handle{Agent}))) { return false; }
            F.CapturedAgents.Add(FCk_Handle{Agent});
        }
        const auto WorldEntity = Transient(F.Server.Get());
        if (!Test.TestTrue(TEXT("live authoritative registry before station enumeration"), Live(WorldEntity))) { return false; }
        const auto Stations = UCk_Utils_EntityTag_UE::ForEach_Entity(WorldEntity,
            FName(TEXT("Gym.Crowd.Separation")));
        if (!Test.TestTrue(TEXT("exact authored world station transform"), Stations.Num() == 1 &&
            Live(Stations[0]) && UCk_Utils_Transform_UE::Has(Stations[0]) &&
            UCk_Utils_Transform_UE::Get_EntityCurrentTransform(UCk_Utils_Transform_UE::Cast(Stations[0])).Equals(
                FTransform(FRotator(0, 180, 0), FVector(500, 0, 0))))) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* PC = F.Local[I].Get();
            if (!Test.TestTrue(TEXT("live exact local controller before world projection checks"),
                IsValid(PC) && LocalPC(PC->GetWorld()) == PC)) { return false; }
            auto* W = PC->GetWorld();
            auto* CS = State(W);
            auto* P = Projector(CS);
            const auto ClientSnap = CS ? CS->Get_RowsSnapshot() : FCkCrowdGym_RowsSnapshot{};
            if (!Test.TestTrue(TEXT("remote world has zero Crowd features and one live projector"),
                Crowd(W).Num() == 0 && IsValid(P) && !P->HasFailed() &&
                ClientSnap.WorldGeneration == F.Generation && ClientSnap.Rows.Num() == Count)) { return false; }
            int32 Projectors = 0;
            for (TObjectIterator<UCkCrowdRemoteBodyProjector> It; It; ++It)
            { if (It->GetWorld() == W && !It->IsRetired()) { ++Projectors; } }
            if (!Test.TestEqual(TEXT("one projector across all client owner sessions"), Projectors, 1)) { return false; }
            TArray<UCkCrowdGym_SharedSimulation*> ClientSimulations;
            CS->GetComponents(ClientSimulations);
            if (!Test.TestTrue(TEXT("remote native default subobject never initializes Crowd"),
                ClientSimulations.Num() == 1 && ClientSimulations[0]->Get_WorldGeneration() == 0 &&
                !ClientSimulations[0]->Get_IsReady())) { return false; }
        }
        for (auto* W : ck::auto_test::net::Get_AllPIEWorlds())
        { if (!Test.TestTrue(TEXT("single authored replicated floor at origin with exact scale"), FloorReady(W, F.FloorClass))) { return false; } }
        if (!F.Dedicated)
        {
            auto* Host = LocalPC(F.Server.Get()); TArray<FCkCrowdRemoteAgentRow> Rows;
            if (!Test.TestTrue(TEXT("listen host neutral provider consumes authority without a projector"), IsValid(Host) &&
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*F.Server.Get(), *Host, *Host->GetLocalPlayer(), Rows) && Rows.Num() == Count))
            { return false; }
        }
        return Test.TestTrue(TEXT("listen host and dedicated server never create remote body projector"), Projector(S) == nullptr);
    }
    auto Settled(const FFixture& F, int32 Count) -> bool
    {
        const auto* S = State(F.Server.Get());
        if (!S || !S->Get_HasCompleteSnapshot() || S->Get_RowsSnapshot().Rows.Num() != Count) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            auto* PC = F.Local[I].Get();
            auto* CS = IsValid(PC) ? State(PC->GetWorld()) : nullptr;
            auto* P = Projector(CS);
            if (!CS || !P) { return false; }
            const auto Snap = CS->Get_RowsSnapshot();
            if (Snap.Rows.Num() != Count || !P->IsReadyFor(Snap.WorldGeneration, Snap.SampleSequence)) { return false; }
        }
        return true;
    }
    auto SameEnvelope(const FCkCrowdRemoteSelectionEnvelope& A, const FCkCrowdRemoteSelectionEnvelope& B) -> bool
    { return FCkCrowdRemoteSelectionEnvelope::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None); }
    auto Receipt(const FCkCrowdGym_OwnerCommit& C, const UCkCrowdGym_Session_UE& S) -> FCkCrowdRemoteSelectionReceipt
    { FCkCrowdRemoteSelectionReceipt R; R.WorldGeneration = C.Snapshot.WorldGeneration;
      R.SessionGeneration = S.Get_TransportSessionGeneration(); R.DeliverySerial = C.DeliverySerial;
      R.RequestSerial = C.RequestSerial; return R; }
    auto CopyEnvelope(FFixture& F, int32 Owner, FCkCrowdRemoteSelectionEnvelope& Out) -> bool
    { return UCk_Utils_CrowdRemoteDebug_UE::TryCopySelectionEnvelope(F.Local[Owner].Get(), Out); }
    auto Matches(const FCkCrowdRemoteSelectionEnvelope& E, const FCkCrowdGym_OwnerCommit& C,
                 const UCkCrowdGym_Session_UE& S) -> bool
    {
        const auto R = Receipt(C, S);
        return FCkCrowdRemoteDebugValidation::IsValid(E) && E.bRetainedDelivery &&
            E.WorldGeneration == C.Snapshot.WorldGeneration && E.SampleSequence == C.Snapshot.SampleSequence &&
            E.SessionGeneration == S.Get_TransportSessionGeneration() && SameRows(E.Rows, C.Snapshot.Rows) &&
            E.SelectedId == C.SelectedId && E.SelectionRequestSerial == C.SelectionRequestSerial &&
            E.bHasDetail == C.bHasDetail &&
            FCkCrowdRemoteAgentDetail::StaticStruct()->CompareScriptStruct(&E.Detail, &C.Detail, PPF_None) &&
            FCkCrowdRemoteSelectionReceipt::StaticStruct()->CompareScriptStruct(&E.Receipt, &R, PPF_None) &&
            uint8(E.Outcome) == uint8(C.Outcome);
    }
    auto InstallEnvelopeAndReceipt(FAutomationTestBase& Test, FFixture& F, int32 Owner,
                                  const FCkCrowdGym_OwnerCommit& C) -> bool
    {
        FCkCrowdRemoteSelectionEnvelope E, Again;
        if (!Test.TestTrue(TEXT("neutral whole envelope matches actual retained wire package"),
            CopyEnvelope(F, Owner, E) && Matches(E, C, *F.Client(Owner)) &&
            CopyEnvelope(F, Owner, Again) && SameEnvelope(E, Again) && F.Authority(Owner)->HasServerRetained()))
        { return false; }
        F.Envelope[Owner] = E; // whole validated rows/detail/outcome installed before explicit receipt
        return Test.TestTrue(TEXT("only installed complete neutral envelope supplies exact receipt"),
            SameEnvelope(F.Envelope[Owner], E) && UCk_Utils_CrowdRemoteDebug_UE::TryAcknowledgeSelection(
                F.Local[Owner].Get(), F.Envelope[Owner].Receipt));
    }
    auto RejectOtherKind(FAutomationTestBase& Test, FFixture& F, int32 Owner,
                         const FCkCrowdGym_OwnerCommit& C) -> bool
    {
        FCkCrowdRemoteSelectionEnvelope E = F.Envelope[Owner], Default;
        E.WorldGeneration = F.Generation;
        FCkCrowdGym_OwnerCommit After;
        return Test.TestFalse(TEXT("neutral selection getter never hides another retained kind behind latest"),
            CopyEnvelope(F, Owner, E)) && Test.TestTrue(TEXT("failed whole copy clears output"), SameEnvelope(E, Default)) &&
            Test.TestFalse(TEXT("selection receipt cannot release actual Control delivery"),
                UCk_Utils_CrowdRemoteDebug_UE::TryAcknowledgeSelection(F.Local[Owner].Get(), Receipt(C, *F.Client(Owner)))) &&
            Test.TestTrue(TEXT("wrong-kind receipt preserves entire package and authority slot"),
                Copy(F.Client(Owner), After) && SameCommit(C, After) && F.Authority(Owner)->HasServerRetained());
    }
    auto ApplyAndAck(FAutomationTestBase& Test, FFixture& F, int32 Owner,
                     ECkCrowdGym_RequestKind Kind, ECkCrowdGym_RequestOutcome Outcome,
                     int32 Count) -> bool
    {
        FCkCrowdGym_OwnerCommit Commit;
        if (!Test.TestTrue(TEXT("consumer copies complete correlated owner package"), Copy(F.Client(Owner), Commit) &&
            Commit.Kind == Kind && Commit.Outcome == Outcome && Commit.Snapshot.WorldGeneration == F.Generation &&
            Commit.Snapshot.Rows.Num() == Count)) { return false; }
        if (Kind == ECkCrowdGym_RequestKind::Selection)
        { F.Installed[Owner] = Commit; return InstallEnvelopeAndReceipt(Test, F, Owner, Commit); }
        if (!RejectOtherKind(Test, F, Owner, Commit)) { return false; }
        F.Installed[Owner] = Commit; // Install entire validated rows/detail/result first.
        return Test.TestTrue(TEXT("only explicit consumer receipt acknowledges installed delivery"),
            SameCommit(F.Installed[Owner], Commit) && Ack(F.Client(Owner), F.Installed[Owner].DeliverySerial));
    }

    auto TickModel(FFixture& F, int32 Owner) -> bool
    {
        auto* PC = F.Local[Owner].Get();
        if (!F.Models[Owner].IsValid() || !IsValid(PC) || !IsValid(PC->GetWorld())) { return false; }
        F.Models[Owner]->Tick(PC->GetWorld(), PC->GetWorld()->GetDeltaSeconds(), PC);
        return F.Models[Owner]->Get_IsRemote();
    }
    auto ModelRowsMatch(const FCkCrowdDebugger_ViewModel& M) -> bool
    {
        const auto& E = M.Get_RemoteEnvelope(); const auto& A = M.Get_AllAgents();
        if (!FCkCrowdRemoteDebugValidation::IsValid(E)) { return false; }
        if (E.Outcome != ECkCrowdRemoteSelectionOutcome::Accepted)
        { return A.IsEmpty() && M.Get_SelectedSnapshot() == nullptr; }
        if (A.Num() != E.Rows.Num()) { return false; }
        for (int32 I = 0; I < A.Num(); ++I)
        {
            if (!A[I].IsRemote || Live(A[I].Handle) || Live(A[I].OwnerHandle) ||
                !(A[I].RemoteId == E.Rows[I].Id) || A[I].RemoteSession != E.SessionGeneration ||
                A[I].RemoteSample != E.SampleSequence || A[I].Position != E.Rows[I].Position) { return false; }
        }
        const auto* Selected = M.Get_SelectedSnapshot();
        if (!E.bHasDetail) { return Selected == nullptr; }
        if (!Selected || !(Selected->RemoteId == E.SelectedId) || Selected->PlannedPath != E.Detail.PlannedPath ||
            Selected->Neighbors.Num() != E.Detail.Neighbors.Num()) { return false; }
        for (int32 I = 0; I < Selected->Neighbors.Num(); ++I)
        {
            const auto& Neighbor = Selected->Neighbors[I]; const auto& WireNeighbor = E.Detail.Neighbors[I];
            if (Live(Neighbor.Handle) || !(Neighbor.RemoteId == WireNeighbor.Id) ||
                Neighbor.Distance != WireNeighbor.DistanceCm || Neighbor.RelativeOffset != WireNeighbor.RelativeOffset)
            { return false; }
        }
        return true;
    }
    auto MakeModel(FFixture& F, int32 Owner) -> void
    {
        F.Models[Owner] = MakeShared<FCkCrowdDebugger_ViewModel>(); F.SawWholeInstall[Owner] = false;
        // Called synchronously by the actual consumer after whole installation, before its receipt.
        F.Models[Owner]->OnAgentListChanged.AddLambda([&F, Owner](const TArray<FCkCrowdDebugger_AgentSnapshot>&)
        {
            FCkCrowdGym_OwnerCommit C;
            auto* CS = F.Client(Owner); auto* AS = F.Authority(Owner);
            if (F.Models[Owner].IsValid() && IsValid(CS) && IsValid(AS) && Copy(CS, C) &&
                C.Kind == ECkCrowdGym_RequestKind::Selection && AS->HasServerRetained() &&
                Matches(F.Models[Owner]->Get_RemoteEnvelope(), C, *CS) && ModelRowsMatch(*F.Models[Owner]))
            { F.SawWholeInstall[Owner] = true; }
        });
    }

    struct FStep { FString Name; TFunction<bool()> Run; bool Wait = false; double Timeout = 30.0; };
    class FScenario final : public IAutomationLatentCommand
    {
    public:
        FScenario(FAutomationTestBase* InTest, TSharedRef<FFixture> InFixture, TArray<FStep>&& InSteps)
            : Test(InTest), Fixture(MoveTemp(InFixture)), Steps(MoveTemp(InSteps)) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index >= Steps.Num()) { return true; }
            if (Started < 0) { Started = FPlatformTime::Seconds(); }
            auto& Step = Steps[Index];
            // Every action/poll runs with live real endpoints; pawn absence is intentionally legal.
            // Unexpected world/controller/session disappearance fails without a weak-pointer dereference.
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            bool Context = IsValid(Server) && IsValid(State(Server)) &&
                IsValid(Server->GetAuthGameMode()) && ck::auto_test::net::Get_AllPIEWorlds().Num() == 3;
            if (Fixture->CapturedEndpoints)
            { Context = Context && Fixture->Server.Get() == Server &&
                (Fixture->SharedRetirementRequested || IsValid(Fixture->Shared())); }
            for (int32 I = 0; Context && I < 2; ++I)
            {
                auto* W = ck::auto_test::net::Get_ClientWorld(I);
                auto* PC = LocalPC(W);
                auto* Remote = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
                Context = IsValid(State(W)) && IsValid(PC) && IsValid(Session(PC)) &&
                    IsValid(Remote) && IsValid(Session(Remote));
                if (Fixture->CapturedEndpoints)
                { Context = Context && Fixture->Local[I].Get() == PC && Fixture->Remote[I].Get() == Remote; }
            }
            const bool Passed = Context && Step.Run();
            if (Passed) { ++Index; Started = -1; return Index >= Steps.Num(); }
            if (Context && Step.Wait && FPlatformTime::Seconds() - Started < Step.Timeout) { return false; }
            const auto Message = FString::Printf(TEXT("Crowd transport phase failed: %s"), *Step.Name);
            Test->AddError(Message); ck::auto_test::net::FailSession(Message);
            return true;
        }
    private:
        FAutomationTestBase* Test = nullptr;
        TSharedRef<FFixture> Fixture;
        TArray<FStep> Steps;
        int32 Index = 0;
        double Started = -1;
    };
    class FAlwaysRestore final : public IAutomationLatentCommand
    {
    public:
        explicit FAlwaysRestore(TSharedRef<FFixture> InFixture) : F(MoveTemp(InFixture)) {}
        bool Update() override
        {
            F->Restore();
            if (auto* S = State(F->Server.Get())) { S->Request_RetireShared(); }
            return true;
        }
    private: TSharedRef<FFixture> F;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdTransport_Separation_Network,
    "Ck.Crowd.Transport.Separation.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCrowdTransport_Separation_Network::RunTest(const FString& Parameters)
{
    using namespace ck_crowd_transport_fixture;
    const auto F = MakeShared<FFixture>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, F->Dedicated))
    { AddError(TEXT("unsupported Crowd transport topology")); return false; }
    F->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    F->StateClass = FSoftClassPath(StatePath).TryLoadClass<ACkCrowdGym_GameState_UE>();
    F->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    F->FloorClass = FSoftClassPath(FloorPath).TryLoadClass<AActor>();
    if (!TestTrue(TEXT("real registered Separation mode/state/controller classes load"),
        F->Mode && F->StateClass && F->PCClass && F->FloorClass)) { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    F->OldStartup = Settings->StartupMode; F->OldLast = Settings->LastGymName;
    F->OldRecents = Settings->RecentGymNames;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() { return Ready(*F); }),
        60.0, TEXT("authored Separation startup, replicated native sessions and world projectors ready")));

    TArray<FStep> Steps;
    auto Act = [&Steps](FString Name, TFunction<bool()> Body) { Steps.Add({MoveTemp(Name), MoveTemp(Body), false, 30}); };
    auto Wait = [&Steps](FString Name, TFunction<bool()> Body) { Steps.Add({MoveTemp(Name), MoveTemp(Body), true, 30}); };
    Act(TEXT("capture real correlated endpoints"), [this, F]()
    {
        F->Server = ck::auto_test::net::Get_ServerWorld();
        F->Generation = State(F->Server.Get())->Get_RowsSnapshot().WorldGeneration;
        for (int32 I = 0; I < 2; ++I)
        {
            F->Remote[I] = ck::auto_test::net::TryGet_RemoteClientPlayerController(F->Server.Get(), I);
            F->Local[I] = LocalPC(ck::auto_test::net::Get_ClientWorld(I));
            F->OldTick[I] = F->Local[I]->IsActorTickEnabled();
            TArray<UCkCrowdGym_Session_UE*> Sessions;
            F->Local[I]->GetComponents(Sessions);
            if (!TestTrue(TEXT("one actual AS DefaultComponent owning RPC endpoint"), Sessions.Num() == 1 &&
                Sessions[0] == F->Client(I) && Sessions[0]->GetIsReplicated())) { return false; }
        }
        F->CapturedEndpoints = true;
        return WorldProof(*this, *F, 0);
    });

    Act(TEXT("default-false legacy provider uses actual world/local owner context"), [this, F]()
    {
        auto* PC = F->Local[0].Get();
        auto* Legacy = NewObject<UCkTest_CrowdRemoteDebug_ProviderProbe>(PC->GetWorld());
        if (!IsValid(Legacy)) { return false; }
        FCkCrowdRemoteSelectionEnvelope E; E.WorldGeneration = F->Generation;
        const FCkCrowdRemoteSelectionEnvelope Default;
        int64 Issued = 999;
        if (!TestFalse(TEXT("null facade copy rejects and resets seeded output"), UCk_Utils_CrowdRemoteDebug_UE::TryCopySelectionEnvelope(nullptr, E)) ||
            !SameEnvelope(E, Default)) { return false; }
        E.WorldGeneration = F->Generation;
        if (!TestFalse(TEXT("remote authority controller has no owning local facade context"), UCk_Utils_CrowdRemoteDebug_UE::TryCopySelectionEnvelope(F->Remote[0].Get(), E)) ||
            !SameEnvelope(E, Default)) { return false; }
        if (!TestFalse(TEXT("null facade next request rejects and resets issued output"), UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(nullptr, {}, false, Issued)) ||
            Issued != 0 || !TestFalse(TEXT("null facade receipt rejects"), UCk_Utils_CrowdRemoteDebug_UE::TryAcknowledgeSelection(nullptr, {}))) { return false; }
        E.WorldGeneration = F->Generation; Issued = 999;
        return TestFalse(TEXT("legacy provider envelope defaults false"), Legacy->CopySelectionEnvelope(
            *PC->GetWorld(), *PC, *PC->GetLocalPlayer(), E)) && SameEnvelope(E, Default) &&
            TestFalse(TEXT("legacy receipt defaults false"), Legacy->AcknowledgeSelectionFromOwningLocalController(
                *PC->GetWorld(), *PC, *PC->GetLocalPlayer(), {})) &&
            TestFalse(TEXT("legacy next selection defaults false"), Legacy->RequestNextSelectionFromOwningLocalController(
                *PC->GetWorld(), *PC, *PC->GetLocalPlayer(), {}, false, Issued)) && Issued == 0;
    });

    // First exercise the unchanged authored AS consumer with Tick enabled. These two owner RPCs
    // must update its actual feedback properties and auto-ACK the native retained commit.
    for (int32 Row : {1, 5})
    {
        const int32 Count = Row == 1 ? 2 : 0;
        Act(TEXT("authored AS Tick consumer receives actual owning control"), [this, F, Row]()
        {
            F->WarmControlSerial = F->Authority(0)->Get_ControlAcceptedWatermark() + 1;
            return TestTrue(TEXT("authored controller Tick remains enabled for production feedback gate"),
                F->Local[0]->IsActorTickEnabled()) && QueueControl(F->Client(0), Row);
        });
        Wait(TEXT("authored feedback and auto-ACK converge without fixture receipt"), [F, Count]()
        {
            FCkCrowdGym_OwnerCommit C;
            return ControlFeedback(F->Local[0].Get(), F->WarmControlSerial) &&
                F->Authority(0)->Get_ControlAcceptedWatermark() == F->WarmControlSerial &&
                !F->Authority(0)->HasServerRetained() && !Copy(F->Client(0), C) && Settled(*F, Count);
        });
        Act(TEXT("production-consumed append/clear keeps sole world cohort"), [this, F, Count]() { return WorldProof(*this, *F, Count); });
    }
    Act(TEXT("switch remote PCs to explicit fixture consumer after authored feedback gate"), [F]()
    {
        F->CapturedTicks = true;
        for (int32 I = 0; I < 2; ++I) { F->Local[I]->SetActorTickEnabled(false); }
        return true;
    });

    const int32 Counts[] = {2, 4, 8, 13, 0};
    for (int32 Row = 1; Row <= 5; ++Row)
    {
        const int32 Owner = (Row - 1) % 2;
        const int32 Count = Counts[Row - 1];
        Act(FString::Printf(TEXT("owning RPC control row %d"), Row), [F, Owner, Row]() { return QueueControl(F->Client(Owner), Row); });
        Wait(TEXT("frozen control delivery"), [F, Owner, Count]()
        { FCkCrowdGym_OwnerCommit C; return Copy(F->Client(Owner), C) && C.Kind == ECkCrowdGym_RequestKind::Control &&
            C.Outcome == ECkCrowdGym_RequestOutcome::Accepted && C.Snapshot.Rows.Num() == Count && Settled(*F, Count); });
        Act(TEXT("validate exact roster append/clear and install before ACK"), [this, F, Owner, Count, Row]()
        {
            if (!WorldProof(*this, *F, Count)) { return false; }
            const auto Snapshot = State(F->Server.Get())->Get_RowsSnapshot();
            TSet<FCkCrowdRemoteAgentId> Next;
            for (const auto& Agent : Snapshot.Rows) { Next.Add(Agent.Id); }
            if (Row < 5)
            { for (const auto& Old : F->Ids) { if (!TestTrue(TEXT("append preserves every existing stable ID"), Next.Contains(Old))) { return false; } } }
            else
            { for (const auto& Agent : F->CapturedAgents) { if (!TestTrue(TEXT("Z commits only after every old authoritative agent retired"), Gone(Agent))) { return false; } } }
            F->Ids = MoveTemp(Next);
            return ApplyAndAck(*this, *F, Owner, ECkCrowdGym_RequestKind::Control,
                ECkCrowdGym_RequestOutcome::Accepted, Count);
        });
        Wait(TEXT("authority releases acknowledged control slot"), [F, Owner]() { return !F->Authority(Owner)->HasServerRetained(); });
    }

    // Real exported debugger consumers: no fabricated provider/session or callback invocation.
    Act(TEXT("ViewModel cohort uses actual owning Control RPC"), [F]() { return QueueControl(F->Client(1), 1); });
    Wait(TEXT("ViewModel pair is genuinely published"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(1), C) && C.Outcome == ECkCrowdGym_RequestOutcome::Accepted && Settled(*F, 2); });
    Act(TEXT("install Control without debugger Selection receipt"), [this, F]() { return WorldProof(*this, *F, 2) &&
        ApplyAndAck(*this, *F, 1, ECkCrowdGym_RequestKind::Control, ECkCrowdGym_RequestOutcome::Accepted, 2); });
    Wait(TEXT("ViewModel control receipt barrier"), [F]() { return !F->Authority(1)->HasServerRetained(); });
    for (int32 Owner = 0; Owner < 2; ++Owner)
    {
        Act(TEXT("mount real ViewModel over exact local owner"), [this, F, Owner]()
        { MakeModel(*F, Owner); return TickModel(*F, Owner) && ModelRowsMatch(*F->Models[Owner]) &&
            TestEqual(TEXT("remote ViewModel row count"), F->Models[Owner]->Get_AgentCount(), 2); });
        Act(TEXT("copied ViewModel row sends next owning request"), [F, Owner]()
        { const auto& A = F->Models[Owner]->Get_AllAgents(); return A.IsValidIndex(Owner) &&
            F->Models[Owner]->TrySelectRemoteAgent(A[Owner]); });
        Wait(TEXT("actual ViewModel selection reaches retained slot"), [F, Owner]() { FCkCrowdGym_OwnerCommit C;
            return Copy(F->Client(Owner), C) && C.Kind == ECkCrowdGym_RequestKind::Selection && C.bHasDetail; });
        Act(TEXT("production ViewModel installs whole value before receipt"), [this, F, Owner]()
        { FCkCrowdGym_OwnerCommit C; if (!Copy(F->Client(Owner), C) || !TickModel(*F, Owner)) { return false; }
          const auto& M = *F->Models[Owner];
          return TestTrue(TEXT("delegate observed whole copied cache while actual slot retained"), F->SawWholeInstall[Owner] &&
              Matches(M.Get_RemoteEnvelope(), C, *F->Client(Owner)) && ModelRowsMatch(M) && M.Get_RemoteSelectedId() == C.SelectedId); });
        Wait(TEXT("ViewModel exact receipt reaches authority"), [F, Owner]() { return !F->Authority(Owner)->HasServerRetained(); });
    }
    Act(TEXT("both real ViewModels retain independent selected identity"), [this, F]()
    { return TestTrue(TEXT("different owning PCs select different actual wire identities"),
        !(F->Models[0]->Get_RemoteSelectedId() == F->Models[1]->Get_RemoteSelectedId())); });
    Act(TEXT("pause owner model and queue actual retained Selection"), [F]()
    { F->OldPick = F->Models[0]->Get_AllAgents()[0]; F->PausedEnvelope = F->Models[0]->Get_RemoteEnvelope();
      F->Models[0]->Set_Paused(true); F->SawWholeInstall[0] = false;
      const int64 High = F->Authority(0)->Get_SelectionAcceptedWatermark();
      return UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(F->Local[0].Get(), F->OldPick.RemoteId, false, F->Issued[0]) &&
          F->Issued[0] == High + 1; });
    Wait(TEXT("paused owner actual Selection retained"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(0), C) && C.RequestSerial == F->Issued[0] && C.bHasDetail; });
    Act(TEXT("peer advances real world while model is paused"), [F]() { return QueueControl(F->Client(1), 2); });
    Wait(TEXT("peer newer Control and four actual projected bodies"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(1), C) && C.Kind == ECkCrowdGym_RequestKind::Control && Settled(*F, 4); });
    Act(TEXT("pause and pure model getters cannot consume retained result"), [this, F]()
    { FCkCrowdGym_OwnerCommit Before, After;
      if (!Copy(F->Client(0), Before) || !TickModel(*F, 0) || !Copy(F->Client(0), After)) { return false; }
      return TestFalse(TEXT("paused model refuses a real copied pick"), F->Models[0]->TrySelectRemoteAgent(F->OldPick)) &&
          TestFalse(TEXT("paused model refuses clear"), F->Models[0]->TryClearRemoteSelection()) &&
          TestTrue(TEXT("pause preserves installed cache and entire pending delivery"),
          SameEnvelope(F->PausedEnvelope, F->Models[0]->Get_RemoteEnvelope()) && SameCommit(Before, After) &&
          F->Authority(0)->HasServerRetained()) && ApplyAndAck(*this, *F, 1, ECkCrowdGym_RequestKind::Control,
              ECkCrowdGym_RequestOutcome::Accepted, 4) && WorldProof(*this, *F, 4); });
    Wait(TEXT("peer Control is independently acknowledged"), [F]() { return !F->Authority(1)->HasServerRetained(); });
    Act(TEXT("unpause installs genuine older complete retained package"), [this, F]()
    { FCkCrowdGym_OwnerCommit C; if (!Copy(F->Client(0), C)) { return false; }
      F->Models[0]->Set_Paused(false);
      return TickModel(*F, 0) && TestTrue(TEXT("retained whole package wins over newer world"), F->SawWholeInstall[0] &&
          Matches(F->Models[0]->Get_RemoteEnvelope(), C, *F->Client(0)) && C.Snapshot.Rows.Num() == 2); });
    Wait(TEXT("unpaused real receipt releases original owner"), [F]() { return !F->Authority(0)->HasServerRetained(); });
    Wait(TEXT("ViewModel follows continuous newer whole copy"), [F]() { return TickModel(*F, 0) &&
        ModelRowsMatch(*F->Models[0]) && F->Models[0]->Get_AgentCount() == 4 &&
        F->Models[0]->Get_RemoteEnvelope().SampleSequence > F->OldPick.RemoteSample; });
    Act(TEXT("old published pick rejected without issuing a request"), [this, F]()
    { const auto H = F->Authority(0)->Get_SelectionAcceptedWatermark();
      return TestFalse(TEXT("stale actual copied pick stamp"), F->Models[0]->TrySelectRemoteAgent(F->OldPick)) &&
          TestTrue(TEXT("stale pick leaves authority watermark and slot unchanged"),
              H == F->Authority(0)->Get_SelectionAcceptedWatermark() && !F->Authority(0)->HasServerRetained()); });
    Act(TEXT("queue legitimate selection before closing consumer"), [F]()
    { F->Models[0]->Set_Paused(true); const auto& A = F->Models[0]->Get_AllAgents(); const int64 Previous = F->Issued[0];
      return !A.IsEmpty() && UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(F->Local[0].Get(), A[0].RemoteId, false, F->Issued[0]) &&
          F->Issued[0] == Previous + 1; });
    Wait(TEXT("closed consumer has actual pending package"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(0), C) && C.RequestSerial == F->Issued[0] && C.bHasDetail; });
    Act(TEXT("reopened production consumer installs complete retained selection"), [this, F]()
    { FCkCrowdGym_OwnerCommit C; if (!Copy(F->Client(0), C)) { return false; }
      F->Models[0].Reset(); MakeModel(*F, 0);
      return TickModel(*F, 0) && TestTrue(TEXT("new consumer does not reset actual issued serial or skip install"),
          F->SawWholeInstall[0] && Matches(F->Models[0]->Get_RemoteEnvelope(), C, *F->Client(0)) && ModelRowsMatch(*F->Models[0])); });
    Wait(TEXT("reopened consumer exact receipt barrier"), [F]() { return !F->Authority(0)->HasServerRetained(); });
    Act(TEXT("production ViewModel clear uses actual next owning serial"), [F]() { return F->Models[0]->TryClearRemoteSelection(); });
    Wait(TEXT("ViewModel clear actual delivery"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(0), C) && C.Kind == ECkCrowdGym_RequestKind::Selection &&
            C.RequestSerial == F->Issued[0] + 1 && !C.SelectedId.IsValid() && !C.bHasDetail; });
    Act(TEXT("whole clear cache precedes ViewModel receipt"), [this, F]()
    { F->SawWholeInstall[0] = false; return TickModel(*F, 0) && TestTrue(TEXT("clear actually removes selected presentation"),
        F->SawWholeInstall[0] && !F->Models[0]->Get_RemoteSelectedId().IsValid() && ModelRowsMatch(*F->Models[0])); });
    Wait(TEXT("ViewModel clear receipt received"), [F]() { return !F->Authority(0)->HasServerRetained(); });
    Act(TEXT("retire ViewModel cohort before unchanged native cases"), [F]()
    { F->Models[0].Reset(); F->Models[1].Reset(); return QueueControl(F->Client(1), 5); });
    Wait(TEXT("ViewModel cohort really retired"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(1), C) && C.Kind == ECkCrowdGym_RequestKind::Control && Settled(*F, 0); });
    Act(TEXT("whole retirement Control installation"), [this, F]() { return WorldProof(*this, *F, 0) &&
        ApplyAndAck(*this, *F, 1, ECkCrowdGym_RequestKind::Control, ECkCrowdGym_RequestOutcome::Accepted, 0); });
    Wait(TEXT("ViewModel retirement receipt barrier"), [F]() { return !F->Authority(1)->HasServerRetained(); });

    Act(TEXT("fresh pair after Z via other owner"), [F]() { return QueueControl(F->Client(1), 1); });
    Wait(TEXT("fresh pair reaches all worlds"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(1), C) && C.Outcome == ECkCrowdGym_RequestOutcome::Accepted && Settled(*F, 2); });
    Act(TEXT("consumer installs pair"), [this, F]() { return WorldProof(*this, *F, 2) && ApplyAndAck(*this, *F, 1,
        ECkCrowdGym_RequestKind::Control, ECkCrowdGym_RequestOutcome::Accepted, 2); });
    Wait(TEXT("pair ACK received"), [F]() { return !F->Authority(1)->HasServerRetained(); });

    for (int32 Owner = 0; Owner < 2; ++Owner)
    {
        Act(TEXT("independent per-owner selection through neutral facade"), [F, Owner]() { return UCk_Utils_CrowdRemoteDebug_UE::TryRequestSelection(F->Local[Owner].Get(),
            State(F->Server.Get())->Get_RowsSnapshot().Rows[Owner].Id, 100 + Owner); });
        Wait(TEXT("complete selection delivery"), [F, Owner]() { FCkCrowdGym_OwnerCommit C;
            return Copy(F->Client(Owner), C) && C.Kind == ECkCrowdGym_RequestKind::Selection && C.bHasDetail; });
        Act(TEXT("copy selection pure and retain independent identity"), [this, F, Owner]()
        {
            FCkCrowdGym_OwnerCommit C, Repeated;
            if (!Copy(F->Client(Owner), C) || !Copy(F->Client(Owner), Repeated)) { return false; }
            FCkCrowdRemoteSelectionEnvelope E, Again;
            if (!CopyEnvelope(*F, Owner, E) || !CopyEnvelope(*F, Owner, Again) || !Matches(E, C, *F->Client(Owner))) { return false; }
            for (int32 Field = 0; Field < 4; ++Field)
            {
                auto Wrong = E.Receipt;
                if (Field == 0) { ++Wrong.WorldGeneration; }
                if (Field == 1) { ++Wrong.SessionGeneration; }
                if (Field == 2) { ++Wrong.DeliverySerial; }
                if (Field == 3) { ++Wrong.RequestSerial; }
                if (!TestFalse(TEXT("each wrong receipt tuple field rejects"), UCk_Utils_CrowdRemoteDebug_UE::TryAcknowledgeSelection(F->Local[Owner].Get(), Wrong))) { return false; }
            }
            FCkCrowdRemoteSelectionEnvelope After;
            if (!CopyEnvelope(*F, Owner, After) || !SameEnvelope(E, Again) || !SameEnvelope(E, After)) { return false; }
            F->Envelope[Owner] = E;
            F->Installed[Owner] = C;
            if (!TestTrue(TEXT("pure whole copies and rejected receipts preserve accepted owner watermark"),
                F->Authority(Owner)->Get_SelectionAcceptedWatermark() == C.RequestSerial &&
                F->Client(Owner)->Get_SelectionAcceptedWatermark() == C.RequestSerial)) { return false; }
            return TestTrue(TEXT("copy neither ACKs nor replaces frozen rows/detail"), SameCommit(C, Repeated) &&
                F->Authority(Owner)->HasServerRetained() && C.SelectionRequestSerial == 100 + Owner);
        });
    }
    Act(TEXT("two owners retain different selected IDs"), [this, F]() { F->Frozen = F->Installed[0]; F->FrozenEnvelope = F->Envelope[0];
        return TestTrue(TEXT("owner selection identity is independent"), !(F->Installed[0].SelectedId == F->Installed[1].SelectedId)) &&
            InstallEnvelopeAndReceipt(*this, *F, 1, F->Installed[1]); });
    Wait(TEXT("peer can issue a control while selection remains unacknowledged"), [F]() { return !F->Authority(1)->HasServerRetained(); });
    Act(TEXT("peer control advances latest while frozen owner holds result"), [F]() { return QueueControl(F->Client(1), 2); });
    Wait(TEXT("latest newer than retained selection"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(1), C) && C.Snapshot.Rows.Num() == 4 &&
            State(F->Server.Get())->Get_RowsSnapshot().SampleSequence > F->Frozen.Snapshot.SampleSequence && Settled(*F, 4); });
    Act(TEXT("frozen precedence and stale duplicate owning RPC"), [this, F]()
    {
        FCkCrowdGym_OwnerCommit C, AfterGetters;
        TArray<FCkCrowdRemoteAgentRow> Rows;
        FCkCrowdRemoteAgentDetail Detail;
        auto* PC = F->Local[0].Get();
        if (!Copy(F->Client(0), C)) { return false; }
        FCkCrowdRemoteSelectionEnvelope E, Again;
        if (!CopyEnvelope(*F, 0, E) || !CopyEnvelope(*F, 0, Again) || !SameEnvelope(E, F->FrozenEnvelope) || !SameEnvelope(E, Again)) { return false; }
        F->BeforeAccepted[0] = F->Authority(0)->Get_SelectionAcceptedWatermark();
        return TestTrue(TEXT("retained selection survives peer publication and pure provider copies"), SameCommit(C, F->Frozen) &&
            FCkCrowdRemoteDebugRegistry::TryCopyRows(*PC->GetWorld(), *PC, *PC->GetLocalPlayer(), Rows) &&
            SameRows(Rows, F->Frozen.Snapshot.Rows) &&
            FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*PC->GetWorld(), *PC, *PC->GetLocalPlayer(),
                C.SelectedId, C.SelectionRequestSerial, Detail) &&
            FCkCrowdRemoteAgentDetail::StaticStruct()->CompareScriptStruct(&Detail, &F->Frozen.Detail, PPF_None) &&
            Copy(F->Client(0), AfterGetters) && SameCommit(AfterGetters, F->Frozen) && F->Authority(0)->HasServerRetained()) &&
            TestFalse(TEXT("one owner unacked slot blocks another kind locally"), QueueControl(F->Client(0), 1)) &&
            RawSelection(F->Client(0), C.SelectedId, C.RequestSerial, F->Generation);
    });
    Act(TEXT("explicit consumer acknowledges both frozen packages"), [this, F]()
    { return InstallEnvelopeAndReceipt(*this, *F, 0, F->Frozen) && ApplyAndAck(*this, *F, 1,
        ECkCrowdGym_RequestKind::Control, ECkCrowdGym_RequestOutcome::Accepted, 4); });
    Wait(TEXT("both owner slots released"), [F]() { return !F->Authority(0)->HasServerRetained() && !F->Authority(1)->HasServerRetained(); });
    Act(TEXT("ordered ACK barrier proves stale duplicate did not alter accepted state"), [this, F]()
    { return TestTrue(TEXT("preceding stale RPC cannot change accepted watermark or cohort"),
        F->Authority(0)->Get_SelectionAcceptedWatermark() == F->BeforeAccepted[0]) && WorldProof(*this, *F, 4); });
    Act(TEXT("owner-only clear selection"), [F]() { return QueueSelection(F->Client(0), {}, 110, true); });
    Wait(TEXT("accepted clear carries no fabricated detail"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(0), C) && C.SelectionRequestSerial == 110 && !C.SelectedId.IsValid() && !C.bHasDetail; });
    Act(TEXT("clear does not retire peer selection or shared cohort"), [this, F]()
    {
        FCkCrowdGym_RowsSnapshot Rows; FCkCrowdRemoteAgentId Id; FCkCrowdRemoteAgentDetail Detail;
        int64 Serial = 0; bool HasDetail = false;
        return TestTrue(TEXT("peer continuous selection remains coherent"), F->Client(1)->TryCopyLatestView(
            Rows, Id, Serial, Detail, HasDetail) && HasDetail && Id == F->Installed[1].SelectedId && Serial == 101) &&
            ApplyAndAck(*this, *F, 0, ECkCrowdGym_RequestKind::Selection, ECkCrowdGym_RequestOutcome::Accepted, 4);
    });
    Wait(TEXT("clear ACK received"), [F]() { return !F->Authority(0)->HasServerRetained(); });

    // Each new test consumer cache is empty; issuing serials remains actual per-PC session state.
    for (int32 Owner = 0; Owner < 2; ++Owner)
    {
        for (int32 Reopen = 0; Reopen < 2; ++Reopen)
        {
            const int64 Expected = (Owner == 0 ? 111 : 102) + Reopen;
            Act(TEXT("new/reopened neutral consumer requests next serial"), [this, F, Owner, Expected]()
            {
                F->Envelope[Owner] = {}; // close/reopen only the test-owned installed cache
                int64 Rejected = 999;
                if (!TestFalse(TEXT("default ID rejected without issuing a serial"), UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(
                    F->Local[Owner].Get(), {}, false, Rejected)) || Rejected != 0) { return false; }
                const auto Id = State(F->Server.Get())->Get_RowsSnapshot().Rows[Owner].Id;
                if (!UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(F->Local[Owner].Get(), Id, false, F->Issued[Owner]) ||
                    !TestEqual(TEXT("next serial respects caller-issued high and reopened cache"), F->Issued[Owner], Expected)) { return false; }
                Rejected = 999;
                return TestFalse(TEXT("pending next request rejects atomically"), UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(
                    F->Local[Owner].Get(), Id, false, Rejected)) && Rejected == 0;
            });
            Wait(TEXT("actual next-selection owning delivery"), [F, Owner, Expected]()
            { FCkCrowdGym_OwnerCommit C; return Copy(F->Client(Owner), C) && C.RequestSerial == Expected && C.bHasDetail; });
            Act(TEXT("reopened consumer installs whole envelope before receipt"), [this, F, Owner]()
            { return ApplyAndAck(*this, *F, Owner, ECkCrowdGym_RequestKind::Selection, ECkCrowdGym_RequestOutcome::Accepted, 4); });
            Wait(TEXT("next-selection exact receipt releases slot"), [F, Owner]() { return !F->Authority(Owner)->HasServerRetained(); });
        }
    }

    // Invalid-first and legitimate duplicate share a fresh serial. The legitimate public queue
    // establishes local correlation before the network ticks; the actual server rejects the first
    // payload and cannot reinterpret its serial as accepted when the valid duplicate arrives.
    for (int32 Case = 0; Case < 3; ++Case)
    {
        const int64 Serial = 200 + Case;
        const auto Outcome = Case == 2 ? ECkCrowdGym_RequestOutcome::Stale : ECkCrowdGym_RequestOutcome::Invalid;
        Act(TEXT("real malformed/default/wrong-generation owning RPC"), [F, Case, Serial]()
        {
            F->BeforeAccepted[0] = F->Authority(0)->Get_SelectionAcceptedWatermark();
            F->BeforeBatch = F->Shared()->Get_LastCompletedBatchSerial();
            const auto Valid = State(F->Server.Get())->Get_RowsSnapshot().Rows[0].Id;
            FCkCrowdRemoteAgentId Invalid;
            if (Case == 1) { Invalid.WorldGeneration = F->Generation; }
            if (Case == 2) { Invalid = Valid; }
            return RawSelection(F->Client(0), Invalid, Serial, Case == 2 ? F->Generation + 1 : F->Generation) &&
                QueueSelection(F->Client(0), Valid, Serial);
        });
        Wait(TEXT("wire rejection delivered to actual pending consumer"), [F, Serial, Outcome]()
        { FCkCrowdGym_OwnerCommit C; return Copy(F->Client(0), C) && C.RequestSerial == Serial && C.Outcome == Outcome; });
        Act(TEXT("rejected attempt cannot forge accepted watermark/configuration/roster"), [this, F, Outcome]()
        { return TestTrue(TEXT("actual rejection leaves shared authority untouched"),
            F->Authority(0)->Get_SelectionAcceptedWatermark() == F->BeforeAccepted[0] &&
            F->Shared()->Get_LastCompletedBatchSerial() == F->BeforeBatch) && WorldProof(*this, *F, 4) &&
            ApplyAndAck(*this, *F, 0, ECkCrowdGym_RequestKind::Selection, Outcome, 4); });
        Wait(TEXT("rejection receipt releases slot"), [F]() { return !F->Authority(0)->HasServerRetained(); });
    }

    // Accepted package is retained across valid-pawn bypass attempts, then explicit context teardown.
    Act(TEXT("retain accepted selection for lifecycle cases"), [F]() { return QueueSelection(F->Client(0),
        State(F->Server.Get())->Get_RowsSnapshot().Rows[0].Id, 210); });
    Wait(TEXT("retained lifecycle selection"), [F]() { FCkCrowdGym_OwnerCommit C; return Copy(F->Client(0), C) && C.bHasDetail; });
    Act(TEXT("live pawn cannot bypass ACK with teardown RPC"), [F]()
    {
        if (!Copy(F->Client(0), F->Frozen)) { return false; }
        F->OldSession = F->Client(0)->Get_TransportSessionGeneration();
        F->OriginalPawn = F->Remote[0]->GetPawn();
        return RawTeardown(F->Client(0), F->Generation, F->OldSession);
    });
    // A legitimate ACK is an ordered Reliable barrier behind the attempted teardown. If the
    // teardown illegally ended a live session, authority generation changes and this check fails.
    Act(TEXT("consumer applies retained selection and ACKs after live teardown attempt"), [this, F]()
    { F->Installed[0] = F->Frozen; return InstallEnvelopeAndReceipt(*this, *F, 0, F->Installed[0]); });
    Wait(TEXT("ordered live teardown/ACK barrier completes"), [F]() { return !F->Authority(0)->HasServerRetained(); });
    Act(TEXT("live teardown did not advance server session generation"), [this, F]()
    { return TestTrue(TEXT("only consumer ACK released valid-pawn session"),
        F->Authority(0)->Get_TransportSessionGeneration() == F->OldSession &&
        F->Client(0)->Get_TransportSessionGeneration() == F->OldSession); });
    Act(TEXT("retain another accepted selection before unpossession"), [F]() { return QueueSelection(F->Client(0),
        State(F->Server.Get())->Get_RowsSnapshot().Rows[0].Id, 211); });
    Wait(TEXT("second lifecycle selection is fully correlated"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(0), C) && C.RequestSerial == 211 && C.Outcome == ECkCrowdGym_RequestOutcome::Accepted; });
    Act(TEXT("capture second frozen accepted lifecycle result"), [F]()
    { if (!Copy(F->Client(0), F->Frozen) || !CopyEnvelope(*F, 0, F->FrozenEnvelope)) { return false; }
      F->OldReceipt = F->FrozenEnvelope.Receipt; return true; });
    Act(TEXT("server unpossesses original admitted pawn"), [F]() { F->Remote[0]->UnPossess(); return true; });
    Wait(TEXT("client observes actual unpossession"), [F]() { return F->Local[0]->GetPawn() == nullptr; });
    Act(TEXT("old accepted ACK cannot bypass ended pawn context"), [this, F]()
    { FCkCrowdRemoteSelectionEnvelope E = F->FrozenEnvelope, Default;
      return TestFalse(TEXT("neutral Accepted receipt rejects ended actual pawn"), UCk_Utils_CrowdRemoteDebug_UE::TryAcknowledgeSelection(F->Local[0].Get(), F->OldReceipt)) &&
          TestFalse(TEXT("neutral Accepted copy rejects ended pawn"), CopyEnvelope(*F, 0, E)) && SameEnvelope(E, Default) &&
          RawAck(F->Client(0), F->Frozen, F->OldSession); });
    // Teardown follows the invalid Accepted ACK on the same Reliable endpoint. If that ACK
    // wrongly clears the accepted slot, teardown cannot advance generation and the wait fails.
    Act(TEXT("explicit obsolete session teardown while unpossessed"), [this, F]()
    { FCkCrowdGym_OwnerCommit C;
      return TestFalse(TEXT("pure copy cannot install accepted state after pawn ended"), Copy(F->Client(0), C)) &&
          Teardown(F->Client(0)); });
    Wait(TEXT("session generation advances and owner slot is reusable"), [F]() { return
        F->Client(0)->Get_TransportSessionGeneration() == F->OldSession + 1 && !F->Authority(0)->HasServerRetained(); });
    Act(TEXT("restore original pawn after explicit teardown"), [F]()
    { if (!F->OriginalPawn.IsValid()) { return false; } F->Remote[0]->Possess(F->OriginalPawn.Get()); return true; });
    Wait(TEXT("repossession arrives"), [F]() { return IsValid(F->Local[0]->GetPawn()); });
    Act(TEXT("new session owns fresh accepted selection"), [F]() { return QueueSelection(F->Client(0),
        State(F->Server.Get())->Get_RowsSnapshot().Rows[0].Id, 220); });
    Wait(TEXT("fresh accepted package proves no permanent busy slot"), [F]() { FCkCrowdGym_OwnerCommit C;
        return Copy(F->Client(0), C) && C.Outcome == ECkCrowdGym_RequestOutcome::Accepted && C.RequestSerial == 220; });
    Act(TEXT("replace admitted pawn with actual mode pawn"), [this, F]()
    {
        if (!Copy(F->Client(0), F->Frozen)) { return false; }
        if (!TestFalse(TEXT("old-session receipt cannot release new retained package"), UCk_Utils_CrowdRemoteDebug_UE::TryAcknowledgeSelection(F->Local[0].Get(), F->OldReceipt)) ||
            !CopyEnvelope(*F, 0, F->FrozenEnvelope)) { return false; }
        F->OldSession = F->Client(0)->Get_TransportSessionGeneration();
        F->ClientOriginalPawn = F->Local[0]->GetPawn();
        auto* W = F->Server.Get();
        if (!IsValid(W) || !IsValid(W->GetAuthGameMode()) || !W->GetAuthGameMode()->DefaultPawnClass || !F->OriginalPawn.IsValid()) { return false; }
        FActorSpawnParameters Params;
        auto* Pawn = W->SpawnActor<APawn>(W->GetAuthGameMode()->DefaultPawnClass,
            F->OriginalPawn->GetActorTransform(), Params);
        if (!IsValid(Pawn)) { return false; }
        F->ReplacementPawn = Pawn; F->Remote[0]->Possess(Pawn); return true;
    });
    Wait(TEXT("actual replacement pawn replicates"), [F]() { FCkCrowdGym_OwnerCommit C;
        return IsValid(F->Local[0]->GetPawn()) && F->ReplacementPawn.IsValid() &&
            F->Local[0]->GetPawn() != F->ClientOriginalPawn.Get() &&
            F->Local[0]->GetPawn()->GetClass() == F->ReplacementPawn->GetClass() && !Copy(F->Client(0), C); });
    Act(TEXT("replacement session explicitly tears down old accepted context"), [this, F]()
    { FCkCrowdRemoteSelectionEnvelope E = F->FrozenEnvelope, Default;
      return TestFalse(TEXT("replacement pawn cannot receipt old admitted pawn package"), UCk_Utils_CrowdRemoteDebug_UE::TryAcknowledgeSelection(F->Local[0].Get(), F->FrozenEnvelope.Receipt)) &&
          TestFalse(TEXT("replacement pawn cannot copy old Accepted envelope"), CopyEnvelope(*F, 0, E)) && SameEnvelope(E, Default) && Teardown(F->Client(0)); });
    Wait(TEXT("replacement no longer inherits retained slot"), [F]() { return
        F->Client(0)->Get_TransportSessionGeneration() == F->OldSession + 1 && !F->Authority(0)->HasServerRetained(); });

    Act(TEXT("queue actual neutral selection before authority unpossesses"), [F]()
    {
        F->BeforeAccepted[0] = F->Authority(0)->Get_SelectionAcceptedWatermark();
        F->OriginalPawn = F->Remote[0]->GetPawn();
        if (!F->OriginalPawn.IsValid() || !IsValid(F->Local[0]->GetPawn())) { return false; }
        const auto Id = State(F->Server.Get())->Get_RowsSnapshot().Rows[0].Id;
        if (!UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(F->Local[0].Get(), Id, false, F->Issued[0])) { return false; }
        // Actual remote owning RPC is queued; authority loses its real pawn before next network dispatch.
        F->Remote[0]->UnPossess(); return true;
    });
    Wait(TEXT("actual Selection Gone envelope arrives with no pawn"), [F]()
    { FCkCrowdRemoteSelectionEnvelope E; return F->Local[0]->GetPawn() == nullptr && CopyEnvelope(*F, 0, E) &&
        E.bRetainedDelivery && E.Outcome == ECkCrowdRemoteSelectionOutcome::Gone && E.Receipt.RequestSerial == F->Issued[0]; });
    Act(TEXT("install complete Selection Gone before pawn-free neutral receipt"), [this, F]()
    { return TestTrue(TEXT("Gone selection never advances Accepted watermark"),
        F->Authority(0)->Get_SelectionAcceptedWatermark() == F->BeforeAccepted[0] && F->Client(0)->Get_SelectionAcceptedWatermark() == F->BeforeAccepted[0]) &&
        [&]()
        {
            FCkCrowdGym_OwnerCommit C; FCkCrowdRemoteSelectionEnvelope E;
            if (!Copy(F->Client(0), C) || C.Kind != ECkCrowdGym_RequestKind::Selection ||
                C.Outcome != ECkCrowdGym_RequestOutcome::Gone || C.Snapshot.Rows.Num() != 4 ||
                !CopyEnvelope(*F, 0, E) || !Matches(E, C, *F->Client(0))) { return false; }
            F->Installed[0] = C; F->Envelope[0] = E;
            MakeModel(*F, 0);
            return TickModel(*F, 0) && TestTrue(TEXT("actual pawn-free Gone is wholly installed before production receipt"),
                F->SawWholeInstall[0] && SameEnvelope(F->Models[0]->Get_RemoteEnvelope(), E) &&
                ModelRowsMatch(*F->Models[0]) && F->Models[0]->Get_AgentCount() == 0 &&
                !F->Models[0]->Get_RemoteSelectedId().IsValid());
        }() && WorldProof(*this, *F, 4); });
    Wait(TEXT("pawn-free Selection receipt releases actual authority slot"), [F]() { return !F->Authority(0)->HasServerRetained(); });
    Act(TEXT("restore actual pawn before existing pending-Control loss gate"), [F]()
    { if (!F->OriginalPawn.IsValid()) { return false; } F->Remote[0]->Possess(F->OriginalPawn.Get()); return true; });
    Wait(TEXT("actual owning pawn restored before Control admission"), [F]() { return IsValid(F->Local[0]->GetPawn()); });

    Act(TEXT("arm loss only after actual control batch admission"), [F]()
    {
        F->GoneHookFired = false;
        F->BeforeControlAccepted = F->Authority(0)->Get_ControlAcceptedWatermark();
        F->GoneHook = FWorldDelegates::OnWorldPreActorTick.AddLambda([F](UWorld* W, ELevelTick, float)
        {
            if (W == F->Server.Get() && !F->GoneHookFired && F->Remote[0].IsValid() && IsValid(F->Shared()) && F->Shared()->Get_IsBusy())
            { F->OriginalPawn = F->Remote[0]->GetPawn(); F->Remote[0]->UnPossess(); F->GoneHookFired = true; }
        });
        return QueueControl(F->Client(0), 1);
    });
    Wait(TEXT("real pending control yields terminal Gone with no pawn"), [F]()
    { FCkCrowdGym_OwnerCommit C; return F->GoneHookFired && F->Local[0]->GetPawn() == nullptr && Copy(F->Client(0), C) &&
        C.Kind == ECkCrowdGym_RequestKind::Control && C.Outcome == ECkCrowdGym_RequestOutcome::Gone; });
    Act(TEXT("remove bounded admission-loss observer"), [F]() { FWorldDelegates::OnWorldPreActorTick.Remove(F->GoneHook);
        F->GoneHook.Reset(); return true; });
    Wait(TEXT("all six world agents commit independently of departed pawn context"), [F]() { return Settled(*F, 6); });
    Act(TEXT("install terminal result before pawn-free ACK"), [this, F]()
    { return TestTrue(TEXT("terminal Gone never installs an Accepted watermark"),
        F->Authority(0)->Get_ControlAcceptedWatermark() == F->BeforeControlAccepted &&
        F->Client(0)->Get_ControlAcceptedWatermark() == F->BeforeControlAccepted) &&
        ApplyAndAck(*this, *F, 0, ECkCrowdGym_RequestKind::Control, ECkCrowdGym_RequestOutcome::Gone, 4) && WorldProof(*this, *F, 6); });
    Wait(TEXT("terminal Gone releases actual owner slot"), [F]() { return !F->Authority(0)->HasServerRetained(); });

    Act(TEXT("restore actual pawn for final public serial-boundary gate"), [F]()
    { if (!F->OriginalPawn.IsValid()) { return false; } F->Remote[0]->Possess(F->OriginalPawn.Get()); return true; });
    Wait(TEXT("actual owner pawn restored for serial-boundary gate"), [F]() { return IsValid(F->Local[0]->GetPawn()); });
    Act(TEXT("public caller-issued final legal Selection serial"), [F]()
    { return UCk_Utils_CrowdRemoteDebug_UE::TryRequestSelection(F->Local[0].Get(), {}, TNumericLimits<int64>::Max() - 1, true); });
    Wait(TEXT("real accepted final legal clear delivery"), [F]()
    { FCkCrowdGym_OwnerCommit C; return Copy(F->Client(0), C) && C.Kind == ECkCrowdGym_RequestKind::Selection &&
        C.Outcome == ECkCrowdGym_RequestOutcome::Accepted && C.RequestSerial == TNumericLimits<int64>::Max() - 1; });
    Act(TEXT("complete final legal clear installation and exact receipt"), [this, F]()
    { return ApplyAndAck(*this, *F, 0, ECkCrowdGym_RequestKind::Selection, ECkCrowdGym_RequestOutcome::Accepted, 6); });
    Wait(TEXT("final legal serial ACK releases real slot"), [F]() { return !F->Authority(0)->HasServerRetained(); });
    Act(TEXT("overflow rejects atomically with a free slot and actual pawn"), [this, F]()
    {
        const auto Id = State(F->Server.Get())->Get_RowsSnapshot().Rows[0].Id;
        const auto High = F->Authority(0)->Get_SelectionAcceptedWatermark();
        int64 Issued = 999;
        FCkCrowdGym_OwnerCommit C;
        return TestEqual(TEXT("caller-issued high is final legal serial"), High, TNumericLimits<int64>::Max() - 1) &&
            TestFalse(TEXT("next serial overflow rejects before addition"), UCk_Utils_CrowdRemoteDebug_UE::TryRequestNextSelection(F->Local[0].Get(), Id, false, Issued)) && Issued == 0 &&
            TestFalse(TEXT("caller-issued MAX sentinel rejects"), UCk_Utils_CrowdRemoteDebug_UE::TryRequestSelection(F->Local[0].Get(), Id, TNumericLimits<int64>::Max())) &&
            TestTrue(TEXT("overflow leaves accepted high, free slot and complete cohort untouched"),
                F->Authority(0)->Get_SelectionAcceptedWatermark() == High && F->Client(0)->Get_SelectionAcceptedWatermark() == High &&
                !F->Authority(0)->HasServerRetained() && !Copy(F->Client(0), C)) && WorldProof(*this, *F, 6);
    });

    Act(TEXT("authority explicit terminal retirement reaches all worlds"), [F]()
    { State(F->Server.Get())->Request_RetireShared(); F->SharedRetirementRequested = true; return true; });
    Wait(TEXT("all providers/projectors/cohorts retired and owner endpoints empty"), [F]()
    {
        for (auto* W : ck::auto_test::net::Get_AllPIEWorlds())
        {
            auto* S = State(W);
            if (!S || S->Get_HasCompleteSnapshot() || Crowd(W).Num() != 0) { return false; }
            if (auto* P = Projector(S)) { if (!P->IsRetired()) { return false; } }
            for (auto It = W->GetPlayerControllerIterator(); It; ++It)
            {
                auto* PC = It->Get(); auto* Endpoint = Session(PC);
                if (!Endpoint) { continue; }
                FCkCrowdGym_OwnerCommit C;
                if (Endpoint->HasServerRetained() || Copy(Endpoint, C) ||
                    Endpoint->Get_ControlAcceptedWatermark() != 0 || Endpoint->Get_SelectionAcceptedWatermark() != 0)
                { return false; }
                if (PC->IsLocalController() && IsValid(PC->GetLocalPlayer()))
                { TArray<FCkCrowdRemoteAgentRow> Rows;
                  if (FCkCrowdRemoteDebugRegistry::TryCopyRows(*W, *PC, *PC->GetLocalPlayer(), Rows)) { return false; } }
            }
        }
        for (const auto& Agent : F->CapturedAgents) { if (!Gone(Agent)) { return false; } }
        return true;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FScenario(this, F, MoveTemp(Steps)));
    ADD_LATENT_AUTOMATION_COMMAND(FAlwaysRestore(F));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
