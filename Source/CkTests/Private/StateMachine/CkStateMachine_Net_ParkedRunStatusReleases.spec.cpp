// A parked run status is applied once the replay pipeline drains, whichever path consumes the last replayed event.
//
// A Paused/Stopped status that arrives while replayed transitions are queued is parked
// (ck::statemachine::MirrorRunStatus_OrDeferWhileReplaying) so it lands after them. Before the fix only the commit of
// a replayed transition applied it, so an event the replay consumed WITHOUT a commit, when it was the last one queued,
// left the status parked forever: the replica stayed Running after the authority paused or stopped.
//
// R1 ALaggedRingEndingPausedPausesOnceTheQueueDrains - a Running replica in A, watermark 0, receives through the
//    WithHistory receive handler first event 1 (A -> B) and then, before event 1 is applied, a ring that rolled past
//    it, ending at B (seq 100), with status Paused. The handler dispatches history before status, so lag recovery
//    appends the snap A -> B (seq 100) behind event 1 and Paused parks. Event 1 commits B; the snap is then consumed
//    without a commit (the replica already stands in B). Expected: B, watermark 100, Paused, nothing parked, one
//    entry into B.
// R2 ALaggedRingEndingStoppedStopsOnce - the same with Stopped: Stopped, nothing parked, OnSmStopped fired once.
// R3 AMisaddressedLastEventReleasesTheParkedStatus - the last queued event is addressed to a sub-SM, so the replay
//    drops it. The receive handler routes events by address, so it never queues a misaddressed event on a root SM;
//    the event is placed on the replay queue directly, the seam the relay and the receive handler write to.
//    Expected: A, Paused, nothing parked.
// R4 ADroppedLastEventReleasesTheParkedStatus - through the receive handler: event 1 (C -> B) with status Paused. The
//    replica stands in A and nothing is missing before seq 1, so the replay drops it. Expected: A, Paused, nothing
//    parked, watermark 0.
// R5 ADiscardedTransitionReleasesTheParkedStop - through the receive handler: a Paused replica receives event 1
//    (A -> B) with status Stopped. The commit discards a transition on an SM that is not Running. Expected: Stopped,
//    nothing parked, OnSmStopped fired once.
//
// All five drive a DoesNotReplicate SM on the server world through the same seams the receive pipeline uses, as
// CkStateMachine_Net_RunStatusMirrorDefers.spec.cpp does.
//
// Surface in Session Frontend: Ck.StateMachine.Net.ParkedRunStatus.*

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkStateMachine/Net/CkStateMachine_NetContext.h"
#include "CkStateMachine/Net/CkStateMachine_NetContextUtils.h"
#include "CkStateMachine/Net/CkStateMachine_RepData.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment_Data.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"

#include "CkTests/Net/CkAutoTest_Sm_Recorder.h"
#include "CkTests/Net/CkAutoTest_Sm_RecordingState.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <StructUtils/InstancedStruct.h>
#include <tuple>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_sm_parked_run_status_test
{
    constexpr auto NumPIEClients = 1;
    constexpr auto PIEReadyTimeoutSeconds = 30.0f;
    constexpr auto StepTimeoutSeconds = 8.0;
    constexpr auto FramesAfterDrain = 5;

    static FCk_Handle_StateMachine GSm;
    static FCk_Handle              GSmOwner;

    struct FStoppedListener
    {
        int32 _Count = 0;

        auto
        OnStopped(
            FCk_Handle_StateMachine,
            FCk_Sm_Payload_OnStopped)
            -> void
        { ++_Count; }
    };

    auto
        Get_StoppedListener()
        -> FStoppedListener&
    {
        static auto Listener = FStoppedListener{};
        return Listener;
    }

    auto
        Get_A() -> TSubclassOf<UCk_SmState_EntityScript>
    { return UCk_AutoTest_Sm_RecordingState_A::StaticClass(); }

    auto
        Get_B() -> TSubclassOf<UCk_SmState_EntityScript>
    { return UCk_AutoTest_Sm_RecordingState_B::StaticClass(); }

    auto
        Get_C() -> TSubclassOf<UCk_SmState_EntityScript>
    { return UCk_AutoTest_Sm_RecordingState_C::StaticClass(); }

    auto
        Make_Event(
            TSubclassOf<UCk_SmState_EntityScript> InPrevious,
            TSubclassOf<UCk_SmState_EntityScript> InNew,
            int32 InSeq)
        -> FCk_Sm_TransitionEvent
    { return FCk_Sm_TransitionEvent{InPrevious, InNew, InSeq, /*Fingerprint*/ 0}; }

    auto
        Make_History(
            const TArray<FCk_Sm_TransitionEvent>& InEvents,
            ECk_SmRunStatus InStatus)
        -> FInstancedStruct
    {
        auto Payload = FCk_RepData_StateMachine_WithHistory{};
        Payload.Set_History(InEvents);
        Payload.Set_RunStatus(InStatus);
        return FInstancedStruct::Make(Payload);
    }

    // The WithHistory receive handler: what the replicated-fragment dispatch calls on a client.
    auto
        Request_Receive(
            FAutomationTestBase& InTest,
            const FInstancedStruct& InNew,
            const TOptional<FInstancedStruct>& InOld)
        -> void
    {
        const auto* Handler = FCk_PersistenceHandlerRegistry::Find(FCk_RepData_StateMachine_WithHistory::StaticStruct());
        if (Handler == nullptr || NOT Handler->NetApply)
        {
            InTest.AddError(TEXT("no WithHistory receive handler is registered"));
            return;
        }

        auto Entity = FCk_Handle{GSm};
        std::ignore = Handler->NetApply(Entity, InNew, InOld);
    }

    auto
        Get_HasParkedStatus() -> bool
    { return ck::IsValid(GSm) && GSm.Has<ck::FFragment_Sm_DeferredRunStatusMirror>(); }

    auto
        Get_Watermark() -> int32
    {
        return ck::IsValid(GSm) && GSm.Has<ck::FFragment_Sm_ClientReplayState>()
            ? GSm.Get<ck::FFragment_Sm_ClientReplayState>().Get_ClientLastAppliedSeq()
            : 0;
    }

    auto
        Get_QueueLength() -> int32
    {
        return ck::IsValid(GSm) && GSm.Has<ck::FFragment_Sm_ReplayQueue>()
            ? GSm.Get<ck::FFragment_Sm_ReplayQueue>().Get_Queue().Num()
            : 0;
    }

    auto
        Get_EnterCount(
            TSubclassOf<UCk_SmState_EntityScript> InState)
        -> int32
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* Recorder = Server != nullptr ? Server->GetSubsystem<UCk_AutoTest_Sm_RecorderSubsystem>() : nullptr;
        if (Recorder == nullptr)
        { return -1; }

        auto Count = 0;
        for (const auto& Event : Recorder->Get_EventsForState(InState))
        {
            if (Event.Kind == ECk_AutoTest_Sm_EventKind::EnterState)
            { ++Count; }
        }
        return Count;
    }

    // PIE, one Running SM in A with the OnSmStopped counter bound.
    auto
        Do_Setup(
            FAutomationTestBase& InTest)
        -> void
    {
        FAutomationTestBase::bSuppressLogErrors = true;
        FAutomationTestBase::bSuppressLogWarnings = true;
        GSm = {};
        GSmOwner = {};
        Get_StoppedListener()._Count = 0;

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, FString{TEXT("/Engine/Maps/Entry")}));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(NumPIEClients, PIEReadyTimeoutSeconds));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&InTest](UWorld* InServer) -> void
            {
                GSmOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InServer, {});
                if (ck::Is_NOT_Valid(GSmOwner))
                { InTest.AddError(TEXT("failed to create the transient owner entity")); return; }

                // Defaults: AutoStart OnSetup, DoesNotReplicate, WithHistory.
                GSm = UCk_Utils_StateMachine_UE::Add(GSmOwner, FCk_StateMachine_Spec{Get_A()});
                if (ck::Is_NOT_Valid(GSm))
                { InTest.AddError(TEXT("failed to add the SM")); }
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&InTest,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                return ck::IsValid(GSm)
                    && UCk_Utils_StateMachine_UE::Get_CurrentStateClass(GSm).Get() == Get_A().Get()
                    && UCk_Utils_StateMachine_UE::Get_RunStatus(GSm) == ECk_SmRunStatus::Running;
            }),
            StepTimeoutSeconds,
            TEXT("the SM starts and enters A")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
            {
                auto Sm = GSm;
                (void)ck::UUtils_Signal_OnSmStopped::Bind<&FStoppedListener::OnStopped>(&Get_StoppedListener(), Sm,
                    ECk_Signal_BindingPolicy::IgnorePayloadInFlight, ECk_Signal_PostFireBehavior::DoNothing);
            })));
    }

    // Waits until nothing replayed is left in flight, then lets a few frames pass.
    auto
        Do_WaitForTheDrain(
            FAutomationTestBase& InTest)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&InTest,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                return ck::IsValid(GSm) && NOT UCk_Utils_StateMachine_UE::Get_HasReplayTransitionsInFlight(GSm);
            }),
            StepTimeoutSeconds,
            TEXT("the replay queue drains")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterDrain));
    }

    auto
        Do_Teardown(
            FAutomationTestBase& InTest)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
            {
                if (ck::IsValid(GSm))
                {
                    auto Sm = GSm;
                    ck::UUtils_Signal_OnSmStopped::Unbind<&FStoppedListener::OnStopped>(&Get_StoppedListener(), Sm);
                }
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
            {
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                GSm = {};
                GSmOwner = {};
                return true;
            }),
            TEXT("restore log suppression statics")));
    }

    // R1/R2: event 1 queued, then a ring that rolled past it, ending at B (seq 100), with InStatus.
    auto
        Do_DeliverALaggedRing(
            FAutomationTestBase& InTest,
            ECk_SmRunStatus InStatus)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([&InTest, InStatus](UWorld*) -> void
            {
                const auto First = Make_History({Make_Event(Get_A(), Get_B(), 1)}, ECk_SmRunStatus::Running);
                Request_Receive(InTest, First, {});

                const auto Rolled = Make_History({Make_Event(Get_B(), Get_A(), 99), Make_Event(Get_A(), Get_B(), 100)}, InStatus);
                Request_Receive(InTest, Rolled, First);

                // The sequence under test: both events queued, the status parked, the SM still Running.
                InTest.TestEqual(TEXT("setup: lag recovery queued the snap behind event 1"), Get_QueueLength(), 2);
                InTest.TestTrue(TEXT("setup: the status parked behind the queued events"), Get_HasParkedStatus());
                InTest.TestEqual(TEXT("setup: the SM is still Running"),
                    static_cast<int32>(UCk_Utils_StateMachine_UE::Get_RunStatus(GSm)), static_cast<int32>(ECk_SmRunStatus::Running));
            })));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_ParkedRunStatus_ALaggedRingEndingPausedPausesOnceTheQueueDrains,
    "Ck.StateMachine.Net.ParkedRunStatus.ALaggedRingEndingPausedPausesOnceTheQueueDrains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_ParkedRunStatus_ALaggedRingEndingPausedPausesOnceTheQueueDrains::RunTest(const FString& Parameters)
{
    using namespace ck_sm_parked_run_status_test;

    Do_Setup(*this);
    Do_DeliverALaggedRing(*this, ECk_SmRunStatus::Paused);
    Do_WaitForTheDrain(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("the replica stands in B"),
                UCk_Utils_StateMachine_UE::Get_CurrentStateClass(GSm).Get(), Get_B().Get());
            TestEqual(TEXT("the watermark is at the ring's last seq"), Get_Watermark(), 100);
            TestEqual(TEXT("the replica is Paused, as the authority is"),
                static_cast<int32>(UCk_Utils_StateMachine_UE::Get_RunStatus(GSm)), static_cast<int32>(ECk_SmRunStatus::Paused));
            TestFalse(TEXT("no status is left parked"), Get_HasParkedStatus());
            TestEqual(TEXT("B was entered exactly once"), Get_EnterCount(Get_B()), 1);
            return true;
        }),
        TEXT("a parked Paused applies when the last event is consumed without a commit")));

    Do_Teardown(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_ParkedRunStatus_ALaggedRingEndingStoppedStopsOnce,
    "Ck.StateMachine.Net.ParkedRunStatus.ALaggedRingEndingStoppedStopsOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_ParkedRunStatus_ALaggedRingEndingStoppedStopsOnce::RunTest(const FString& Parameters)
{
    using namespace ck_sm_parked_run_status_test;

    Do_Setup(*this);
    Do_DeliverALaggedRing(*this, ECk_SmRunStatus::Stopped);
    Do_WaitForTheDrain(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("the replica stands in B"),
                UCk_Utils_StateMachine_UE::Get_CurrentStateClass(GSm).Get(), Get_B().Get());
            TestEqual(TEXT("the watermark is at the ring's last seq"), Get_Watermark(), 100);
            TestEqual(TEXT("the replica is Stopped, as the authority is"),
                static_cast<int32>(UCk_Utils_StateMachine_UE::Get_RunStatus(GSm)), static_cast<int32>(ECk_SmRunStatus::Stopped));
            TestFalse(TEXT("no status is left parked"), Get_HasParkedStatus());
            TestEqual(TEXT("OnSmStopped fired exactly once"), Get_StoppedListener()._Count, 1);
            TestEqual(TEXT("B was entered exactly once"), Get_EnterCount(Get_B()), 1);
            return true;
        }),
        TEXT("a parked Stopped applies once when the last event is consumed without a commit")));

    Do_Teardown(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_ParkedRunStatus_AMisaddressedLastEventReleasesTheParkedStatus,
    "Ck.StateMachine.Net.ParkedRunStatus.AMisaddressedLastEventReleasesTheParkedStatus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_ParkedRunStatus_AMisaddressedLastEventReleasesTheParkedStatus::RunTest(const FString& Parameters)
{
    using namespace ck_sm_parked_run_status_test;

    Do_Setup(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld*) -> void
        {
            auto Event = Make_Event(Get_A(), Get_B(), 1);
            ck::statemachine::FSubSmAddress::Make({UCk_SmState_EntityScript::Get_StateTagForClass(Get_B())}, {0})
                .Request_WriteTo(Event);

            auto Entity = FCk_Handle{GSm};
            Entity.AddOrGet<ck::FFragment_Sm_ReplayQueue>().Get_Queue().Add(Event);
            ck::statemachine::MirrorRunStatus_OrDeferWhileReplaying(Entity, ECk_SmRunStatus::Paused);

            TestTrue(TEXT("setup: the status parked behind the queued event"), Get_HasParkedStatus());
        })));

    Do_WaitForTheDrain(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("the replica stays in A (the misaddressed event was dropped)"),
                UCk_Utils_StateMachine_UE::Get_CurrentStateClass(GSm).Get(), Get_A().Get());
            TestEqual(TEXT("the replica is Paused"),
                static_cast<int32>(UCk_Utils_StateMachine_UE::Get_RunStatus(GSm)), static_cast<int32>(ECk_SmRunStatus::Paused));
            TestFalse(TEXT("no status is left parked"), Get_HasParkedStatus());
            return true;
        }),
        TEXT("a parked status applies when the last event is dropped as misaddressed")));

    Do_Teardown(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_ParkedRunStatus_ADroppedLastEventReleasesTheParkedStatus,
    "Ck.StateMachine.Net.ParkedRunStatus.ADroppedLastEventReleasesTheParkedStatus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_ParkedRunStatus_ADroppedLastEventReleasesTheParkedStatus::RunTest(const FString& Parameters)
{
    using namespace ck_sm_parked_run_status_test;

    Do_Setup(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld*) -> void
        {
            Request_Receive(*this, Make_History({Make_Event(Get_C(), Get_B(), 1)}, ECk_SmRunStatus::Paused), {});

            TestEqual(TEXT("setup: event 1 is queued"), Get_QueueLength(), 1);
            TestTrue(TEXT("setup: the status parked behind the queued event"), Get_HasParkedStatus());
        })));

    Do_WaitForTheDrain(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("the replica stays in A (the event did not describe it and was dropped)"),
                UCk_Utils_StateMachine_UE::Get_CurrentStateClass(GSm).Get(), Get_A().Get());
            TestEqual(TEXT("the watermark did not move"), Get_Watermark(), 0);
            TestEqual(TEXT("the replica is Paused"),
                static_cast<int32>(UCk_Utils_StateMachine_UE::Get_RunStatus(GSm)), static_cast<int32>(ECk_SmRunStatus::Paused));
            TestFalse(TEXT("no status is left parked"), Get_HasParkedStatus());
            return true;
        }),
        TEXT("a parked status applies when the last event is dropped by the where-it-stands check")));

    Do_Teardown(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_ParkedRunStatus_ADiscardedTransitionReleasesTheParkedStop,
    "Ck.StateMachine.Net.ParkedRunStatus.ADiscardedTransitionReleasesTheParkedStop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_ParkedRunStatus_ADiscardedTransitionReleasesTheParkedStop::RunTest(const FString& Parameters)
{
    using namespace ck_sm_parked_run_status_test;

    Do_Setup(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld*) -> void
        {
            // Paused with nothing queued applies at once; the next delivery's event then queues behind a Stopped.
            const auto Paused = Make_History({}, ECk_SmRunStatus::Paused);
            Request_Receive(*this, Paused, {});
            Request_Receive(*this, Make_History({Make_Event(Get_A(), Get_B(), 1)}, ECk_SmRunStatus::Stopped), Paused);

            TestEqual(TEXT("setup: the replica is Paused"),
                static_cast<int32>(UCk_Utils_StateMachine_UE::Get_RunStatus(GSm)), static_cast<int32>(ECk_SmRunStatus::Paused));
            TestTrue(TEXT("setup: Stopped parked behind the queued event"), Get_HasParkedStatus());
        })));

    Do_WaitForTheDrain(*this);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("the replica is Stopped, as the authority is"),
                static_cast<int32>(UCk_Utils_StateMachine_UE::Get_RunStatus(GSm)), static_cast<int32>(ECk_SmRunStatus::Stopped));
            TestFalse(TEXT("no status is left parked"), Get_HasParkedStatus());
            TestEqual(TEXT("OnSmStopped fired exactly once"), Get_StoppedListener()._Count, 1);
            return true;
        }),
        TEXT("a parked Stopped applies when the commit discards the transition of a non-Running SM")));

    Do_Teardown(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
