#include "CkSceneNode_StopReceiptProbe.h"

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_StopReceipt.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/StrongObjectPtr.h"

void UCk_SceneNode_StopReceiptProbe_UE::OnTimerUpdate(
    FCk_Handle_Timer InTimer, FCk_Chrono, FCk_Time)
{
    const int32 Slot = Timers.IndexOfByKey(InTimer);
    if (!UpdateCalls.IsValidIndex(Slot)) { return; }
    ++UpdateCalls[Slot];
    if (bClosed) { ++ClosedUpdates[Slot]; return; }
    ++ProducerWrites[Slot];
}

void UCk_SceneNode_StopReceiptProbe_UE::OnTimerStop(
    FCk_Handle_Timer InTimer, FCk_Chrono, FCk_Time)
{
    const int32 Slot = Timers.IndexOfByKey(InTimer);
    if (StopSignals.IsValidIndex(Slot)) { ++StopSignals[Slot]; }
}

int32 UCk_SceneNode_StopReceiptProbe_UE::Get_MinUpdateCalls() const
{
    if (UpdateCalls.Num() != 6) { return -1; }
    int32 Minimum = MAX_int32;
    for (const int32 Count : UpdateCalls) { Minimum = FMath::Min(Minimum, Count); }
    return Minimum;
}

int32 UCk_SceneNode_StopReceiptProbe_UE::Get_TotalProducerWrites() const
{
    int32 Total = 0;
    for (const int32 Count : ProducerWrites) { Total += Count; }
    return Total;
}

int32 UCk_SceneNode_StopReceiptProbe_UE::Get_MinClosedUpdates() const
{
    if (ClosedUpdates.Num() != 6) { return -1; }
    int32 Minimum = MAX_int32;
    for (const int32 Count : ClosedUpdates) { Minimum = FMath::Min(Minimum, Count); }
    return Minimum;
}

bool UCk_SceneNode_StopReceiptProbe_UE::Get_ExactlyOneStopSignalPerTimer() const
{
    if (StopSignals.Num() != 6) { return false; }
    for (const int32 Count : StopSignals)
    { if (Count != 1) { return false; } }
    return true;
}

int32 UCk_SceneNode_StopReceiptProbe_UE::Get_TotalStopSignals() const
{
    int32 Total = 0;
    for (const int32 Count : StopSignals) { Total += Count; }
    return Total;
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace ck_scene_stop_receipt_test
{
    struct FState
    {
        FCk_Handle WorldRoot;
        FCk_Handle Owner;
        TArray<FCk_Handle_Timer> Timers;
        TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
        TWeakObjectPtr<UCk_SceneNodeGym_StopReceipt_UE> Receipt;
        TStrongObjectPtr<UCk_SceneNode_StopReceiptProbe_UE> Probe;
        int32 WritesAtClose = -1;
        bool bSetup = false;
    };

    static bool Live(const FCk_Handle& H)
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    static bool CleanRunning(const FCk_Handle_Timer& Timer)
    {
        if (!Live(Timer) || !UCk_Utils_Timer_UE::Has(Timer) ||
            Timer.Has<ck::FTag_Timer_NeedsSetup>() ||
            !Timer.Has<ck::FTag_Timer_NeedsUpdate>())
        { return false; }
        return !Timer.Has<ck::FFragment_Timer_Requests>() &&
            UCk_Utils_Timer_UE::Get_CurrentState(Timer) == ECk_Timer_State::Running;
    }

    static bool CleanPaused(const FCk_Handle_Timer& Timer)
    {
        if (!Live(Timer) || !UCk_Utils_Timer_UE::Has(Timer) ||
            Timer.Has<ck::FTag_Timer_NeedsSetup>() ||
            Timer.Has<ck::FTag_Timer_NeedsUpdate>())
        { return false; }
        return !Timer.Has<ck::FFragment_Timer_Requests>() &&
            UCk_Utils_Timer_UE::Get_CurrentState(Timer) == ECk_Timer_State::Paused;
    }

    static bool AllRunning(const FState& S)
    {
        if (S.Timers.Num() != 6) { return false; }
        for (const auto& Timer : S.Timers)
        { if (!CleanRunning(Timer)) { return false; } }
        return true;
    }

    static bool AllPaused(const FState& S)
    {
        if (S.Timers.Num() != 6) { return false; }
        for (const auto& Timer : S.Timers)
        { if (!CleanPaused(Timer)) { return false; } }
        return true;
    }

    static bool EachOneQueuedStopRequest(const FState& S)
    {
        if (S.Timers.Num() != 6) { return false; }
        for (const auto& Timer : S.Timers)
        {
            if (!Live(Timer) || !UCk_Utils_Timer_UE::Has(Timer) ||
                !Timer.Has<ck::FFragment_Timer_Requests>() ||
                Timer.Get<ck::FFragment_Timer_Requests>().Get_Requests().Num() != 1)
            { return false; }
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeStopReceipt,
    "Ck.SceneNode.Network.StopReceipt.ExactTimers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeStopReceipt::RunTest(const FString& Parameters)
{
    using namespace ck_scene_stop_receipt_test;
    const auto S = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        UWorld* const Server = ck::auto_test::net::Get_ServerWorld();
        UWorld* const Client = ck::auto_test::net::Get_ClientWorld(0);
        return IsValid(Server) && IsValid(Client) &&
            IsValid(Server->GetGameState()) &&
            Server->GetGameState()->HasActorBegunPlay() &&
            IsValid(Server->GetAuthGameMode());
    }), 15.0, TEXT("authority GameState reaches BeginPlay")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* World)
    {
        if (!TestTrue(TEXT("real initialized authority and client worlds"),
            IsValid(World) && IsValid(World->GetAuthGameMode()) &&
            IsValid(World->GetGameState()) &&
            IsValid(ck::auto_test::net::Get_ClientWorld(0))))
        { return; }
        auto* const Source = World->GetGameState();
        auto* const SourceActorCDO =
            ACk_SceneNodeGym_SourceActor_UE::StaticClass()
                ->GetDefaultObject<ACk_SceneNodeGym_SourceActor_UE>();
        auto* const ScopeCDO = AActor::StaticClass()->GetDefaultObject<AActor>();
        if (!TestTrue(TEXT("actual native actor classes have default objects"),
            IsValid(SourceActorCDO) && !SourceActorCDO->GetIsReplicated() &&
            IsValid(ScopeCDO) && !ScopeCDO->GetIsReplicated()))
        { return; }
        TArray<TSubclassOf<AActor>> Classes;
        Classes.Init(ACk_SceneNodeGym_SourceActor_UE::StaticClass(), 17);
        auto* const Ledger =
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::TryBeginActorGeneration(
                Source, 1, Classes);
        if (!TestTrue(TEXT("real authority GameState retains one native actor ledger"),
            IsValid(Ledger) && Source->GetInstanceComponents().Contains(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_CapturedCount(Ledger) == 0))
        { return; }
        S->Ledger = Ledger;
        S->WorldRoot =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!TestTrue(TEXT("real authority transient ECS root"), Live(S->WorldRoot)))
        { return; }
        S->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->WorldRoot);
        if (!TestTrue(TEXT("real retained timer owner"), Live(S->Owner)))
        { return; }
        S->Probe.Reset(NewObject<UCk_SceneNode_StopReceiptProbe_UE>());
        if (!TestTrue(TEXT("retained test-owned timer signal probe"), S->Probe.IsValid()))
        { return; }
        auto Spec = FCk_Timer_Spec{FCk_Time{1000.0}};
        Spec.Set_StartingState(ECk_Timer_State::Running);
        for (int32 Slot = 0; Slot < 6; ++Slot)
        {
            auto Timer = UCk_Utils_Timer_UE::Add(S->Owner, Spec);
            if (!TestTrue(*FString::Printf(TEXT("real running timer role %d"), Slot),
                Live(Timer) && UCk_Utils_Timer_UE::Has(Timer)))
            { return; }
            S->Timers.Add(Timer);
        }
        S->Probe->Timers = S->Timers;
        S->Probe->UpdateCalls.Init(0, 6);
        S->Probe->ClosedUpdates.Init(0, 6);
        S->Probe->ProducerWrites.Init(0, 6);
        S->Probe->StopSignals.Init(0, 6);
        FCk_Delegate_Timer Update;
        Update.BindDynamic(S->Probe.Get(), &UCk_SceneNode_StopReceiptProbe_UE::OnTimerUpdate);
        FCk_Delegate_Timer Stop;
        Stop.BindDynamic(S->Probe.Get(), &UCk_SceneNode_StopReceiptProbe_UE::OnTimerStop);
        bool bAllBound = true;
        for (auto& Timer : S->Timers)
        {
            bAllBound = bAllBound &&
                UCk_Utils_Timer_UE::BindTo_OnUpdate(Timer, Update) == Timer &&
                UCk_Utils_Timer_UE::BindTo_OnStop(Timer, Stop) == Timer;
        }
        S->bSetup = TestTrue(TEXT("all six actual timer Update and Stop signals bound"),
            bAllBound);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this, FCk_NetAutoTest_Condition::CreateLambda([S]()
    { return S->bSetup && S->Probe.IsValid() &&
        S->Probe->Get_MinUpdateCalls() >= 2 && AllRunning(*S); }),
        15.0, TEXT("six actual running timers each update twice")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        auto* const Ledger = S->Ledger.Get();
        if (!TestTrue(TEXT("real running timer admission prerequisites"),
            S->bSetup && IsValid(Ledger) && Live(S->WorldRoot) &&
            Live(S->Owner) && AllRunning(*S) && S->Probe.IsValid() &&
            S->Probe->Get_MinUpdateCalls() >= 2 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger)))
        { return; }
        UWorld* const Client = ck::auto_test::net::Get_ClientWorld(0);
        if (!TestTrue(TEXT("real remote client world for foreign-root rejection"),
            IsValid(Client))) { return; }
        const FCk_Handle ClientRoot =
            UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Client);
        if (!TestTrue(TEXT("real remote root differs from authority registry"),
            Live(ClientRoot) && ClientRoot != S->WorldRoot)) { return; }

        const TArray<FCk_Handle_Timer> Empty;
        TArray<FCk_Handle_Timer> Duplicate = S->Timers;
        Duplicate[1] = Duplicate[0];
        TArray<FCk_Handle_Timer> NonTimer = S->Timers;
        if (!TestTrue(TEXT("retained owner lacks Timer feature"),
            !UCk_Utils_Timer_UE::Has(S->Owner))) { return; }
        NonTimer[1] = ck::StaticCast<FCk_Handle_Timer>(S->Owner);
        const auto OpenRejected =
            UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
                Ledger, S->Timers, S->WorldRoot) == nullptr;
        if (!TestTrue(TEXT("open ledger rejects Stop before any request or receipt"),
            OpenRejected && AllRunning(*S) &&
            S->Probe->Get_TotalStopSignals() == 0 &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger)))
        { return; }

        S->Probe->bClosed = true;
        S->WritesAtClose = S->Probe->Get_TotalProducerWrites();
        if (!TestTrue(TEXT("source closes its producer and actual actor ledger"),
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Request_Close(Ledger) &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_ClosureReason(Ledger) ==
                FName(TEXT("SourceRequestedClose")) && AllRunning(*S)))
        { return; }
        const auto DefaultRejected =
            UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
                nullptr, S->Timers, S->WorldRoot) == nullptr;
        const auto EmptyRejected =
            UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
                Ledger, Empty, S->WorldRoot) == nullptr;
        const auto DuplicateRejected =
            UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
                Ledger, Duplicate, S->WorldRoot) == nullptr;
        const auto NonTimerRejected =
            UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
                Ledger, NonTimer, S->WorldRoot) == nullptr;
        const auto ForeignRootRejected =
            UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
                Ledger, S->Timers, ClientRoot) == nullptr;
        if (!TestTrue(TEXT("closed-ledger malformed Stop inputs reject without occupying receipt"),
            DefaultRejected && EmptyRejected &&
            DuplicateRejected && NonTimerRejected && ForeignRootRejected &&
            AllRunning(*S) && S->Probe->Get_TotalStopSignals() == 0 &&
            S->Probe->Get_TotalProducerWrites() == S->WritesAtClose))
        { return; }
        auto* const Receipt = UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
            Ledger, S->Timers, S->WorldRoot);
        if (!TestTrue(TEXT("one real native Stop batch accepted and queued"),
            IsValid(Receipt) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_ExpectedCount(Receipt) == 6 &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_SucceededCount(Receipt) == 0 &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(Receipt) &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsFailed(Receipt)))
        { return; }
        S->Receipt = Receipt;
        const auto* const Second =
            UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
                Ledger, S->Timers, S->WorldRoot);
        TestTrue(TEXT("one immutable recorder per closed source, no duplicate Stop requests"),
            Second == nullptr && S->Receipt.IsValid() &&
            EachOneQueuedStopRequest(*S) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_SucceededCount(Receipt) == 0 &&
            S->Probe->Get_TotalProducerWrites() == S->WritesAtClose);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this, FCk_NetAutoTest_Condition::CreateLambda([S]()
    {
        return S->Receipt.IsValid() &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(S->Receipt.Get()) &&
            AllPaused(*S);
    }), 15.0, TEXT("six native Stop receipts and current Paused state")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        auto* const Receipt = S->Receipt.Get();
        if (!TestTrue(TEXT("retained native receipt completed six real Stops"),
            IsValid(Receipt) && S->Probe.IsValid() && AllPaused(*S) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(Receipt) &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsFailed(Receipt) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_ExpectedCount(Receipt) == 6 &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_SucceededCount(Receipt) == 6 &&
            S->Probe->Get_ExactlyOneStopSignalPerTimer() &&
            S->Probe->Get_MinClosedUpdates() >= 1 &&
            S->Probe->Get_TotalProducerWrites() == S->WritesAtClose))
        { return; }
        auto MutableTimer = S->Timers[0];
        const auto Returned = UCk_Utils_Timer_UE::Request_Resume(MutableTimer, {});
        TestTrue(TEXT("real Resume request targets one formerly stopped timer"),
            Returned == S->Timers[0]);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(
        this, FCk_NetAutoTest_Condition::CreateLambda([S]()
    {
        return S->Receipt.IsValid() && S->Timers.Num() == 6 &&
            CleanRunning(S->Timers[0]) &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(S->Receipt.Get());
    }), 15.0, TEXT("real Resume invalidates current Paused proof")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        auto* const Receipt = S->Receipt.Get();
        TestTrue(TEXT("historical Stop receipts remain but current Paused proof rejects Resume"),
            IsValid(Receipt) && S->Probe.IsValid() &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(Receipt) &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsFailed(Receipt) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_ExpectedCount(Receipt) == 6 &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_SucceededCount(Receipt) == 6 &&
            CleanRunning(S->Timers[0]) &&
            S->Probe->Get_TotalProducerWrites() == S->WritesAtClose &&
            S->Probe->Get_ExactlyOneStopSignalPerTimer());
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
