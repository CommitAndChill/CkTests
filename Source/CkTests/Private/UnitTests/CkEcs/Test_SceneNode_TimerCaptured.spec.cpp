#include "CkSceneNode_TimerCapturedProbe.h"

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNodeGym_StopReceipt.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Registry/CkRegistry.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkRecord/Record/CkRecord_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>
#include <variant>

void UCk_SceneNode_TimerCapturedProbe_UE::OnUpdate(
    FCk_Handle_Timer Timer, FCk_Chrono, FCk_Time)
{
    const int32 Slot = Timers.IndexOfByKey(Timer);
    if (!Updates.IsValidIndex(Slot)) { return; }
    ++Updates[Slot];
    if (bClosed) { ++ClosedUpdates[Slot]; return; }
    ++ProducerWrites[Slot];
}

void UCk_SceneNode_TimerCapturedProbe_UE::OnStop(
    FCk_Handle_Timer Timer, FCk_Chrono, FCk_Time)
{
    const int32 Slot = Timers.IndexOfByKey(Timer);
    if (Stops.IsValidIndex(Slot)) { ++Stops[Slot]; }
}

void UCk_SceneNode_TimerCapturedProbe_UE::OnResumeCompleted(
    FCk_Handle Timer, ECk_Request_OperationResult Result)
{
    const int32 Slot = Timers.IndexOfByPredicate(
        [Timer](const FCk_Handle_Timer& Candidate)
        { return FCk_Handle(Candidate) == Timer; });
    if (!ResumeCallbacks.IsValidIndex(Slot)) { return; }
    ++ResumeCallbacks[Slot];
    if (Result == ECk_Request_OperationResult::Succeeded)
    { ++ResumeSucceeded[Slot]; }
}

bool UCk_SceneNode_TimerCapturedProbe_UE::AllUpdatedAtLeast(int32 Count) const
{
    if (Updates.Num() != 6) { return false; }
    for (const int32 Value : Updates)
    { if (Value < Count) { return false; } }
    return true;
}

bool UCk_SceneNode_TimerCapturedProbe_UE::AllResumeSucceededOnce() const
{
    if (ResumeCallbacks.Num() != 6 || ResumeSucceeded.Num() != 6)
    { return false; }
    for (int32 I = 0; I < 6; ++I)
    { if (ResumeCallbacks[I] != 1 || ResumeSucceeded[I] != 1) { return false; } }
    return true;
}

bool UCk_SceneNode_TimerCapturedProbe_UE::AllStoppedOnce() const
{
    if (Stops.Num() != 6) { return false; }
    for (const int32 Value : Stops)
    { if (Value != 1) { return false; } }
    return true;
}

int32 UCk_SceneNode_TimerCapturedProbe_UE::TotalWrites() const
{
    int32 Result = 0;
    for (const int32 Value : ProducerWrites) { Result += Value; }
    return Result;
}

int32 UCk_SceneNode_TimerCapturedProbe_UE::TotalClosedUpdates() const
{
    int32 Result = 0;
    for (const int32 Value : ClosedUpdates) { Result += Value; }
    return Result;
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace ck_scene_timer_captured_test
{
    using FRecord = ck::TUtils_RecordOfEntities<ck::FFragment_RecordOfTimers>;

    struct FState
    {
        FCk_Handle WorldRoot;
        FCk_Handle Owners[6];
        FCk_Timer_AddCapturedResult Captures[6];
        TWeakObjectPtr<UCk_SceneNodeGym_ActorLedger_UE> Ledger;
        TWeakObjectPtr<UCk_SceneNodeGym_StopReceipt_UE> Stop;
        TStrongObjectPtr<UCk_SceneNode_TimerCapturedProbe_UE> Probe;
        int32 WritesAtClose = -1;
        bool bFrozen = false;
        double ResumeWaitFirstPoll = -1.0;
        bool bResumeWaitDiagnosticEmitted = false;
        bool bResumeSampleTaken = false;
        bool bResumeMotionObserved = false;
        uint64 ResumeSampleFrame = 0;
        int32 ResumeSampleUpdates[6]{};
    };

    bool Live(const FCk_Handle& H)
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    int32 Population(const FCk_Handle& WorldRoot)
    {
        if (!Live(WorldRoot)) { return -1; }
        int32 Result = 0;
        WorldRoot.Get_RegistryView().View<ck::FTag_EntityJustCreated>().ForEach(
            [&Result](FCk_Entity) { ++Result; });
        return Result;
    }

    FCk_Timer_Spec Recipe()
    {
        auto Spec = FCk_Timer_Spec{FCk_Time{0.0}};
        Spec.Set_CountDirection(ECk_Timer_CountDirection::CountUp);
        Spec.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        Spec.Set_StartingState(ECk_Timer_State::Paused);
        return Spec;
    }

    bool Empty(const FCk_Timer_AddCapturedResult& Result)
    {
        return !Result.bAllocationAttempted &&
            Result.RawEntity == FCk_Handle{} &&
            Result.Timer == FCk_Handle_Timer{};
    }

    bool Frozen(const FState& S)
    {
        for (int32 I = 0; I < 6; ++I)
        {
            const auto& Capture = S.Captures[I];
            if (!Live(S.Owners[I]) || !Live(Capture.RawEntity) ||
                FCk_Handle(Capture.Timer) != Capture.RawEntity ||
                !Capture.bAllocationAttempted ||
                !Capture.RawEntity.Has<ck::FFragment_LifetimeOwner>() ||
                Capture.RawEntity.Get<ck::FFragment_LifetimeOwner>()
                    .Get_Entity() != S.Owners[I] ||
                !UCk_Utils_Timer_UE::Has(Capture.Timer) ||
                !S.Owners[I].Has<ck::FFragment_RecordOfTimers>() ||
                !FRecord::Get_ContainsEntry(S.Owners[I], Capture.Timer) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(
                    S.Owners[I]).Num() != 1 ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(
                    S.Owners[I])[0] != Capture.RawEntity)
            { return false; }
            for (int32 J = 0; J < I; ++J)
            { if (Capture.RawEntity == S.Captures[J].RawEntity) { return false; } }
        }
        return true;
    }

    bool AllPausedClean(const FState& S)
    {
        if (!Frozen(S)) { return false; }
        for (const auto& Capture : S.Captures)
        {
            const auto& Timer = Capture.Timer;
            if (Timer.Has_Any<ck::FTag_Timer_NeedsSetup,
                    ck::FTag_Timer_NeedsUpdate,
                    ck::FFragment_Timer_Requests>() ||
                UCk_Utils_Timer_UE::Get_CurrentState(Timer) !=
                    ECk_Timer_State::Paused)
            { return false; }
        }
        return true;
    }

    bool AllRunning(const FState& S)
    {
        if (!Frozen(S)) { return false; }
        for (const auto& Capture : S.Captures)
        {
            const auto& Timer = Capture.Timer;
            if (Timer.Has<ck::FTag_Timer_NeedsSetup>() ||
                !Timer.Has<ck::FTag_Timer_NeedsUpdate>() ||
                UCk_Utils_Timer_UE::Get_CurrentState(Timer) !=
                    ECk_Timer_State::Running)
            { return false; }
        }
        return true;
    }

    bool AllRunningDrained(const FState& S)
    {
        if (!AllRunning(S)) { return false; }
        for (const auto& Capture : S.Captures)
        {
            const auto& Timer = Capture.Timer;
            if (Timer.Has<ck::FFragment_Timer_Requests>()) { return false; }
        }
        return true;
    }

    void ReportResumeWait(const FState& S)
    {
        const auto* Probe = S.Probe.Get();
        FString Detail = FString::Printf(
            TEXT("CapturedAdmission Resume wait: probe=%d frozen=%d receipts=%d updates=%d runningDrained=%d sample=%d observed=%d sampleFrame=%llu currentFrame=%llu"),
            Probe != nullptr, Frozen(S),
            Probe != nullptr && Probe->AllResumeSucceededOnce(),
            Probe != nullptr && Probe->AllUpdatedAtLeast(2), AllRunningDrained(S),
            S.bResumeSampleTaken, S.bResumeMotionObserved,
            static_cast<unsigned long long>(S.ResumeSampleFrame),
            static_cast<unsigned long long>(GFrameCounter));
        for (int32 I = 0; I < 6; ++I)
        {
            const auto& Capture = S.Captures[I];
            const auto& Timer = Capture.Timer;
            const bool bLive = Live(Timer);
            const bool bTimer = bLive && UCk_Utils_Timer_UE::Has(Timer);
            FString Requests = TEXT("unreadable");
            if (bTimer)
            {
                Requests = TEXT("none");
                if (Timer.Has<ck::FFragment_Timer_Requests>())
                {
                    const auto& Pending = Timer.Get<ck::FFragment_Timer_Requests>()
                        .Get_Requests();
                    Requests = FString::Printf(TEXT("count=%d"), Pending.Num());
                    for (const auto& Request : Pending)
                    {
                        if (const auto* Manipulate =
                            std::get_if<FCk_Request_Timer_Manipulate>(&Request))
                        {
                            Requests += FString::Printf(TEXT(" Manipulate:%d"),
                                static_cast<int32>(Manipulate->Get_Manipulate()));
                        }
                        else if (std::get_if<FCk_Request_Timer_Jump>(&Request))
                        { Requests += TEXT(" Jump"); }
                        else if (std::get_if<FCk_Request_Timer_Consume>(&Request))
                        { Requests += TEXT(" Consume"); }
                        else
                        { Requests += TEXT(" Other"); }
                    }
                }
            }
            const int32 Callbacks = Probe != nullptr &&
                Probe->ResumeCallbacks.IsValidIndex(I)
                ? Probe->ResumeCallbacks[I] : -1;
            const int32 Succeeded = Probe != nullptr &&
                Probe->ResumeSucceeded.IsValidIndex(I)
                ? Probe->ResumeSucceeded[I] : -1;
            const int32 Updates = Probe != nullptr && Probe->Updates.IsValidIndex(I)
                ? Probe->Updates[I] : -1;
            Detail += FString::Printf(
                TEXT(" | slot=%d rawLive=%d live=%d feature=%d callback=%d succeeded=%d update=%d baselineUpdate=%d setup=%d needsUpdate=%d state=%d requests=[%s]"),
                I, Live(Capture.RawEntity), bLive, bTimer,
                Callbacks, Succeeded, Updates, S.ResumeSampleUpdates[I],
                bTimer && Timer.Has<ck::FTag_Timer_NeedsSetup>(),
                bTimer && Timer.Has<ck::FTag_Timer_NeedsUpdate>(),
                bTimer ? static_cast<int32>(UCk_Utils_Timer_UE::Get_CurrentState(Timer)) : -1,
                *Requests);
        }
        UE_LOG(LogTemp, Display, TEXT("%s"), *Detail);
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeTimerCapturedAdmission,
    "Ck.SceneNode.Network.StopReceipt.CapturedAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSceneNodeTimerCapturedAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_scene_timer_captured_test;
    const auto S = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        auto* World = ck::auto_test::net::Get_ServerWorld();
        return IsValid(World) && IsValid(ck::auto_test::net::Get_ClientWorld(0)) &&
            IsValid(World->GetGameState()) &&
            World->GetGameState()->HasActorBegunPlay() &&
            IsValid(World->GetAuthGameMode());
    }), 15.0, TEXT("real authority and client worlds are ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld* World)
    {
        if (!TestTrue(TEXT("real initialized authority source"),
            IsValid(World) && IsValid(World->GetGameState()) &&
            IsValid(ck::auto_test::net::Get_ClientWorld(0)))) { return; }
        auto* Source = World->GetGameState();
        auto* SourceCDO = ACk_SceneNodeGym_SourceActor_UE::StaticClass()
            ->GetDefaultObject<ACk_SceneNodeGym_SourceActor_UE>();
        auto* ScopeCDO = ACk_SceneNodeGym_ScopeActor_UE::StaticClass()
            ->GetDefaultObject<ACk_SceneNodeGym_ScopeActor_UE>();
        if (!TestTrue(TEXT("actual source and scope actor classes materialized"),
            IsValid(SourceCDO) && SourceCDO->GetIsReplicated() &&
            !SourceCDO->IsReplicatingMovement() &&
            IsValid(ScopeCDO) && ScopeCDO->GetIsReplicated() &&
            !ScopeCDO->IsReplicatingMovement())) { return; }
        TArray<TSubclassOf<AActor>> Classes;
        Classes.Init(ACk_SceneNodeGym_SourceActor_UE::StaticClass(), 17);
        auto* Ledger = UCk_SceneNodeGym_ActorLedgerRuntime_UE::
            TryBeginActorGeneration(Source, 1, Classes);
        S->Ledger = Ledger;
        S->WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!TestTrue(TEXT("real source ledger and transient ECS root"),
            IsValid(Ledger) && Source->GetInstanceComponents().Contains(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            Live(S->WorldRoot))) { return; }
        for (auto& Owner : S->Owners)
        {
            Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(S->WorldRoot);
            if (!TestTrue(TEXT("fresh retained timer owner"), Live(Owner) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owner).IsEmpty() &&
                !Owner.Has<ck::FFragment_RecordOfTimers>())) { return; }
        }
        const auto Spec = Recipe();
        const int32 BeforeInvalid = Population(S->WorldRoot);
        auto Running = Spec;
        Running.Set_StartingState(ECk_Timer_State::Running);
        auto BadBehavior = Spec;
        BadBehavior.Set_Behavior(static_cast<ECk_Timer_Behavior>(255));
        auto NotFinite = FCk_Timer_Spec{
            FCk_Time{std::numeric_limits<double>::quiet_NaN()}};
        NotFinite.Set_Behavior(ECk_Timer_Behavior::ResetOnDone);
        const bool AllRejected =
            Empty(UCk_Utils_Timer_UE::TryAddCapturedPaused({}, Spec)) &&
            Empty(UCk_Utils_Timer_UE::TryAddCapturedPaused(S->Owners[0], Running)) &&
            Empty(UCk_Utils_Timer_UE::TryAddCapturedPaused(S->Owners[0], BadBehavior)) &&
            Empty(UCk_Utils_Timer_UE::TryAddCapturedPaused(S->Owners[0], NotFinite));
        if (!TestTrue(TEXT("invalid owner, Running, enum and NaN reject before allocation"),
            AllRejected && Population(S->WorldRoot) == BeforeInvalid &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(
                S->Owners[0]).IsEmpty())) { return; }

        auto RetiringOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
            S->WorldRoot);
        const auto RetiringIdentity = RetiringOwner;
        if (!TestTrue(TEXT("retiring negative owner starts as an empty real leaf"),
            Live(RetiringIdentity) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(
                RetiringIdentity).IsEmpty())) { return; }
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(RetiringOwner);
        const int32 BeforeRetiringAttempt = Population(S->WorldRoot);
        if (!TestTrue(TEXT("pending-destroy owner rejects before allocation"),
            ck::IsValid(RetiringIdentity,
                ck::IsValid_Policy_IncludePendingKill{}) &&
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                RetiringIdentity,
                ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            Empty(UCk_Utils_Timer_UE::TryAddCapturedPaused(
                RetiringIdentity, Spec)) &&
            Population(S->WorldRoot) == BeforeRetiringAttempt &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(
                RetiringIdentity).IsEmpty())) { return; }

        for (int32 I = 0; I < 6; ++I)
        {
            S->Captures[I] = UCk_Utils_Timer_UE::TryAddCapturedPaused(
                S->Owners[I], Spec);
            const auto& Capture = S->Captures[I];
            if (!TestTrue(*FString::Printf(TEXT("captured dormant timer %d"), I),
                Capture.bAllocationAttempted && Live(Capture.RawEntity) &&
                FCk_Handle(Capture.Timer) == Capture.RawEntity &&
                !Capture.Timer.Has<ck::FTag_Timer_NeedsUpdate>() &&
                Capture.Timer.Has<ck::FTag_Timer_NeedsSetup>() &&
                !Capture.Timer.Has<ck::FFragment_Timer_Requests>() &&
                Capture.RawEntity.Has<ck::FFragment_LifetimeOwner>() &&
                Capture.RawEntity.Get<ck::FFragment_LifetimeOwner>()
                    .Get_Entity() == S->Owners[I] &&
                S->Owners[I].Has<ck::FFragment_RecordOfTimers>() &&
                FRecord::Get_ContainsEntry(S->Owners[I], Capture.Timer)))
            { return; }
        }
        if (!TestTrue(TEXT("all six distinct actual creator identities are frozen"),
            Frozen(*S))) { return; }
        const int32 BeforeSecond = Population(S->WorldRoot);
        if (!TestTrue(TEXT("existing Timer record rejects a second captured add"),
            Empty(UCk_Utils_Timer_UE::TryAddCapturedPaused(S->Owners[0], Spec)) &&
            Population(S->WorldRoot) == BeforeSecond && Frozen(*S))) { return; }

        S->Probe.Reset(NewObject<UCk_SceneNode_TimerCapturedProbe_UE>());
        if (!TestTrue(TEXT("test-owned callback probe retained"), S->Probe.IsValid()))
        { return; }
        for (const auto& Capture : S->Captures)
        { S->Probe->Timers.Add(Capture.Timer); }
        S->Probe->Updates.Init(0, 6);
        S->Probe->ClosedUpdates.Init(0, 6);
        S->Probe->ProducerWrites.Init(0, 6);
        S->Probe->Stops.Init(0, 6);
        S->Probe->ResumeCallbacks.Init(0, 6);
        S->Probe->ResumeSucceeded.Init(0, 6);
        FCk_Delegate_Timer Update;
        Update.BindDynamic(S->Probe.Get(),
            &UCk_SceneNode_TimerCapturedProbe_UE::OnUpdate);
        FCk_Delegate_Timer Stop;
        Stop.BindDynamic(S->Probe.Get(),
            &UCk_SceneNode_TimerCapturedProbe_UE::OnStop);
        bool bAllBound = Update.IsBound() && Stop.IsBound();
        for (auto& Timer : S->Probe->Timers)
        {
            bAllBound = bAllBound &&
                UCk_Utils_Timer_UE::BindTo_OnUpdate(Timer, Update) == Timer &&
                UCk_Utils_Timer_UE::BindTo_OnStop(Timer, Stop) == Timer;
        }
        S->bFrozen = TestTrue(TEXT("all six native callbacks bind after frozen identities"),
            bAllBound && Frozen(*S));
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([S]()
    {
        return S->bFrozen && AllPausedClean(*S);
    }), 15.0, TEXT("six captured zero timers finish setup while Paused")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        if (!TestTrue(TEXT("dormant captured timers have not fired before Resume"),
            S->Probe.IsValid() && AllPausedClean(*S) &&
            !S->Probe->AllUpdatedAtLeast(1) &&
            S->Probe->TotalWrites() == 0 && Frozen(*S))) { return; }
        FCk_Delegate_Request_OnCompleted Resume;
        Resume.BindDynamic(S->Probe.Get(),
            &UCk_SceneNode_TimerCapturedProbe_UE::OnResumeCompleted);
        if (!TestTrue(TEXT("one real Resume receipt delegate bound before requests"),
            Resume.IsBound())) { return; }
        for (int32 I = 0; I < 6; ++I)
        {
            auto Timer = S->Captures[I].Timer;
            if (!TestTrue(*FString::Printf(TEXT("real Resume request %d retained identity"), I),
                UCk_Utils_Timer_UE::Request_Resume(Timer, Resume) ==
                    S->Captures[I].Timer && Timer == S->Captures[I].Timer))
            { return; }
        }
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([S]()
    {
        bool bReady = false;
        if (S->Probe.IsValid() && S->Probe->AllResumeSucceededOnce() &&
            S->Probe->AllUpdatedAtLeast(1) && AllRunningDrained(*S))
        {
            if (!S->bResumeSampleTaken)
            {
                S->bResumeSampleTaken = true;
                S->ResumeSampleFrame = GFrameCounter;
                for (int32 I = 0; I < 6; ++I)
                { S->ResumeSampleUpdates[I] = S->Probe->Updates[I]; }
            }
            else if (GFrameCounter > S->ResumeSampleFrame &&
                S->Probe->AllUpdatedAtLeast(2))
            {
                bReady = true;
                for (int32 I = 0; I < 6; ++I)
                { bReady = bReady && S->Probe->Updates[I] > S->ResumeSampleUpdates[I]; }
                S->bResumeMotionObserved = bReady;
            }
        }
        if (!bReady)
        {
            const double Now = FPlatformTime::Seconds();
            if (S->ResumeWaitFirstPoll < 0.0)
            { S->ResumeWaitFirstPoll = Now; }
            // FCk_Latent_WaitUntil checks its timeout after this predicate and
            // suppresses later RunOnServer actions once the session fails.
            if (!S->bResumeWaitDiagnosticEmitted &&
                Now - S->ResumeWaitFirstPoll >= 14.0)
            {
                S->bResumeWaitDiagnosticEmitted = true;
                ReportResumeWait(*S);
            }
        }
        return bReady;
    }), 15.0, TEXT("six Resume receipts and advancing updates on distinct frames")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        auto* Ledger = S->Ledger.Get();
        if (!TestTrue(TEXT("six actual running producers and receipts before close"),
            S->Probe.IsValid() && S->Probe->AllResumeSucceededOnce() &&
            S->Probe->AllUpdatedAtLeast(2) && S->bResumeMotionObserved &&
            GFrameCounter > S->ResumeSampleFrame && AllRunningDrained(*S) &&
            Frozen(*S) && IsValid(Ledger) &&
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger)))
        { return; }
        ReportResumeWait(*S); // one successful post-ECS sample before closure
        S->Probe->bClosed = true;
        S->WritesAtClose = S->Probe->TotalWrites();
        if (!TestTrue(TEXT("source closes callback before Stop admission"),
            UCk_SceneNodeGym_ActorLedgerRuntime_UE::Request_Close(Ledger) &&
            !UCk_SceneNodeGym_ActorLedgerRuntime_UE::Get_IsOpen(Ledger) &&
            S->Probe->TotalWrites() == S->WritesAtClose)) { return; }
        TArray<FCk_Handle_Timer> Timers;
        for (const auto& Capture : S->Captures) { Timers.Add(Capture.Timer); }
        auto* Receipt = UCk_SceneNodeGym_StopRuntime_UE::TryStop_KnownTimers(
            Ledger, Timers, S->WorldRoot);
        S->Stop = Receipt;
        TestTrue(TEXT("one Stop batch accepts actual drained ResetOnDone timer state"),
            IsValid(Receipt) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_ExpectedCount(Receipt) == 6 &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_SucceededCount(Receipt) == 0 &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsFailed(Receipt) &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(Receipt) &&
            Frozen(*S) && S->Probe->TotalWrites() == S->WritesAtClose);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([S]()
    {
        return S->Stop.IsValid() &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(S->Stop.Get()) &&
            AllPausedClean(*S);
    }), 15.0, TEXT("six real Stop receipts drain without rearming")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, S](UWorld*)
    {
        auto* Receipt = S->Stop.Get();
        TestTrue(TEXT("captured identity, Stop signals and closed producer remain exact"),
            IsValid(Receipt) && Frozen(*S) && AllPausedClean(*S) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_IsComplete(Receipt) &&
            UCk_SceneNodeGym_StopRuntime_UE::Get_SucceededCount(Receipt) == 6 &&
            !UCk_SceneNodeGym_StopRuntime_UE::Get_IsFailed(Receipt) &&
            S->Probe.IsValid() && S->Probe->AllStoppedOnce() &&
            S->Probe->TotalClosedUpdates() >= 6 &&
            S->Probe->TotalWrites() == S->WritesAtClose);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif
