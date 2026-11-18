// Pins the two moments of the Ck Entity Bridge on the server and on a client, each bound before and after the fact:
//
//   - Linked (C++ Promise_OnEntityLinked): fires when the Actor and Entity are linked, before the script constructs.
//   - Ready (Promise_OnEntityReady, delegate and C++ callback): fires once the script has begun play and, on a client,
//     replicated values have been applied.
//
// "Before" is bound from the subject's own BeginPlay, ahead of its Request_SpawnLinkedEntity, on every machine, so it is
// before the link by construction. "After" is bound by the test once the bridge is Ready, and must fire at bind time.
//
// Surface in Session Frontend: Ck.EntityBridge.Net.Promises

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Format/CkFormat.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"

#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_entity_bridge_net_promises
{
    using CapturerPtrType = TSharedRef<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>;

    // What the C++ callbacks bound after the fact observed. The delegates bound after the fact report to a capturer.
    struct FLateCallbacks
    {
        int32 _LinkedCount = 0;
        FCk_Handle _LinkedEntity;

        int32 _ReadyCount = 0;
        FCk_Handle _ReadyEntity;
    };

    using LateCallbacksPtrType = TSharedRef<FLateCallbacks>;

    auto
        Find_Probe(
            UWorld* InWorld)
        -> ACk_AutoTest_EntityBridgeSubject_LinkedProbe*
    {
        return Cast<ACk_AutoTest_EntityBridgeSubject_LinkedProbe>(ACk_AutoTest_EntityBridgeSubject::Find(
            InWorld, ACk_AutoTest_EntityBridgeSubject_LinkedProbe::StaticClass()));
    }

    auto
        Get_IsProbeReady(
            UWorld* InWorld)
        -> bool
    {
        const auto* Probe = Find_Probe(InWorld);

        if (ck::Is_NOT_Valid(Probe))
        { return false; }

        return Probe->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::Ready;
    }

    auto
        Get_EventCount(
            const TArray<FName>& InEvents,
            const TCHAR* InEvent)
        -> int32
    {
        const auto EventName = FName{InEvent};
        auto Count = 0;

        for (const auto& Event : InEvents)
        {
            if (Event == EventName)
            { ++Count; }
        }

        return Count;
    }

    // Binds every moment after the bridge is Ready; each must fire before its bind call returns.
    auto
        Do_BindAfterReady(
            FAutomationTestBase& InTest,
            ACk_AutoTest_EntityBridgeSubject_LinkedProbe& InProbe,
            UCk_AutoTest_EntityBridge_Capturer_UE& InCapturer,
            const LateCallbacksPtrType& InLate,
            const TCHAR* InMachine)
        -> void
    {
        auto* Bridge = InProbe.Get_EntityBridge().Get();

        UCk_Utils_EntityBridge_UE::Promise_OnEntityLinked(&InProbe, [InLate](AActor*, FCk_Handle InEntity) -> void
        {
            ++InLate->_LinkedCount;
            InLate->_LinkedEntity = InEntity;
        });

        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Linked bound after the link fires at bind time"), InMachine),
            InLate->_LinkedCount, 1);

        auto ReadyDelegate = FCk_Delegate_EntityBridge_OnEntityReady{};
        ReadyDelegate.BindDynamic(&InCapturer, &UCk_AutoTest_EntityBridge_Capturer_UE::OnEntityReady);
        Bridge->Promise_OnEntityReady(ReadyDelegate);

        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Ready (delegate) bound after the fact fires at bind time"), InMachine),
            InCapturer._ReadyCount, 1);

        UCk_Utils_EntityBridge_UE::Promise_OnEntityReady(&InProbe, [InLate](AActor*, FCk_Handle InEntity) -> void
        {
            ++InLate->_ReadyCount;
            InLate->_ReadyEntity = InEntity;
        });

        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Ready (C++ callback) bound after the fact fires at bind time"), InMachine),
            InLate->_ReadyCount, 1);
    }

    auto
        Do_AssertMachine(
            FAutomationTestBase& InTest,
            UWorld* InWorld,
            const UCk_AutoTest_EntityBridge_Capturer_UE& InCapturer,
            const FLateCallbacks& InLate,
            const TCHAR* InMachine)
        -> void
    {
        using namespace ck::auto_test::entity_bridge;

        const auto* Probe = Find_Probe(InWorld);

        if (ck::Is_NOT_Valid(Probe))
        {
            InTest.AddError(ck::Format_UE(TEXT("[{}] the subject is missing"), InMachine));
            return;
        }

        const auto* Bridge = Probe->Get_EntityBridge().Get();
        const auto Entity = Bridge->Get_Entity();

        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] bridge state is Ready (is [{}])"), InMachine, Bridge->Get_State()),
            Bridge->Get_State() == ECk_EntityBridge_State::Ready);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] bridge has a valid entity"), InMachine), ck::IsValid(Entity));

        // Bound before: Linked, recorded by the probe apart from its event list.
        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Linked bound before the link fired once"), InMachine),
            Probe->Get_LinkedFireCount(), 1);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] Linked fired with the bridge's entity"), InMachine),
            Probe->Get_LinkedEntity() == Entity);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] Linked fired with the subject Actor"), InMachine),
            Probe->Get_LinkedActor().Get() == Probe);
        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Linked fired before the script constructed (no event recorded yet)"), InMachine),
            Probe->Get_EventCountAtLinked(), 0);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] bridge state was Linked when Linked fired (was [{}])"), InMachine,
                Probe->Get_StateAtLinked()),
            Probe->Get_StateAtLinked() == ECk_EntityBridge_State::Linked);

        // Bound before: Ready, recorded in the probe's event list after the script's BeginPlay.
        const auto& Events = Probe->Get_Events();
        const auto BeginPlayIndex = Events.IndexOfByKey(FName{EventName_ScriptBeginPlay});
        const auto ReadyIndex = Events.IndexOfByKey(FName{EventName_Ready});

        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] the script began play once"), InMachine),
            Get_EventCount(Events, EventName_ScriptBeginPlay), 1);
        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Ready bound before fired once"), InMachine),
            Get_EventCount(Events, EventName_Ready), 1);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] Ready fired after the script began play"), InMachine),
            BeginPlayIndex != INDEX_NONE && ReadyIndex != INDEX_NONE && BeginPlayIndex < ReadyIndex);

        // Bound after: still exactly once each after further frames, with the bridge's entity.
        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Linked bound after fired once in total"), InMachine),
            InLate._LinkedCount, 1);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] Linked bound after fired with the bridge's entity"), InMachine),
            InLate._LinkedEntity == Entity);
        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Ready (delegate) bound after fired once in total"), InMachine),
            InCapturer._ReadyCount, 1);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] Ready (delegate) bound after fired with the bridge's entity and Actor"), InMachine),
            InCapturer._ReadyEntity == Entity && InCapturer._ReadyActor.Get() == Probe);
        InTest.TestEqual(*ck::Format_UE(TEXT("[{}] Ready (C++ callback) bound after fired once in total"), InMachine),
            InLate._ReadyCount, 1);
        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] Ready (C++ callback) bound after fired with the bridge's entity"), InMachine),
            InLate._ReadyEntity == Entity);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_Promises,
    "Ck.EntityBridge.Net.Promises",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_Promises::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net_promises;

    // CkActorRelay x Iris ambient noise under multi-client PIE, as in the other net specs. Restored at the end via a
    // trailing latent command.
    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    constexpr auto NumPIEClients = 2;
    constexpr auto ExpectedTotalWorlds = 2;
    constexpr auto ReadyTimeoutSeconds = 30.0f;
    constexpr auto BridgeReadyTimeoutSeconds = 15.0;
    constexpr auto FramesAfterLateBind = 10;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};

    // Owned by the spec for the whole run; bound dynamic delegates only weak-reference them.
    auto ServerCapturer = CapturerPtrType{MakeShared<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>(
        NewObject<UCk_AutoTest_EntityBridge_Capturer_UE>(GetTransientPackage()))};
    auto ClientCapturer = CapturerPtrType{MakeShared<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>(
        NewObject<UCk_AutoTest_EntityBridge_Capturer_UE>(GetTransientPackage()))};

    auto ServerLate = LateCallbacksPtrType{MakeShared<FLateCallbacks>()};
    auto ClientLate = LateCallbacksPtrType{MakeShared<FLateCallbacks>()};

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(ExpectedTotalWorlds, ReadyTimeoutSeconds));

    // ---- Stage 1 - the server spawns the probe; its BeginPlay binds all three moments, then requests. ----------------

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            const auto* Probe = InServer->SpawnActor<ACk_AutoTest_EntityBridgeSubject_LinkedProbe>(
                ACk_AutoTest_EntityBridgeSubject_LinkedProbe::StaticClass(), FTransform::Identity, SpawnInfo);

            if (ck::Is_NOT_Valid(Probe))
            { AddError(TEXT("server-side spawn of ACk_AutoTest_EntityBridgeSubject_LinkedProbe returned null")); }
        })));

    // ---- Stage 2 - wait until the bridge is Ready on both machines. ------------------------------------------------

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_IsProbeReady(ck::auto_test::net::Get_ServerWorld()) &&
                Get_IsProbeReady(ck::auto_test::net::Get_ClientWorld(0));
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the probe's bridge is Ready on the server and the client")));

    // ---- Stage 3 - bind every moment again, after the fact, on both machines. -----------------------------------------

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, ServerCapturer, ServerLate](UWorld* InServer) -> void
        {
            auto* Probe = Find_Probe(InServer);

            if (ck::Is_NOT_Valid(Probe))
            {
                AddError(TEXT("Stage 3: the server has no probe"));
                return;
            }

            Do_BindAfterReady(*this, *Probe, *ServerCapturer->Get(), ServerLate, TEXT("Server"));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, ClientCapturer, ClientLate](UWorld* InClient) -> void
        {
            auto* Probe = Find_Probe(InClient);

            if (ck::Is_NOT_Valid(Probe))
            {
                AddError(TEXT("Stage 3: the client has no probe"));
                return;
            }

            Do_BindAfterReady(*this, *Probe, *ClientCapturer->Get(), ClientLate, TEXT("Client"));
        })));

    // Gives a promise that would fire twice the frames to do it in.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterLateBind));

    // ---- Assertions ------------------------------------------------------------------------------------------------

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, ServerCapturer, ClientCapturer, ServerLate, ClientLate]() -> bool
        {
            Do_AssertMachine(*this, ck::auto_test::net::Get_ServerWorld(), **ServerCapturer, *ServerLate, TEXT("Server"));
            Do_AssertMachine(*this, ck::auto_test::net::Get_ClientWorld(0), **ClientCapturer, *ClientLate, TEXT("Client"));

            // A client Ready means replicated values have been applied.
            const auto* ClientProbe = Find_Probe(ck::auto_test::net::Get_ClientWorld(0));

            if (ck::IsValid(ClientProbe))
            {
                TestTrue(TEXT("[Client] replication was complete when Ready fired"),
                    ClientProbe->Get_WasReplicationCompleteAtReady());
            }

            return true;
        }),
        TEXT("Linked and Ready hold on server and client, bound before and after the fact")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

    // Restore static log suppression after teardown so subsequent tests see default semantics.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
        {
            FAutomationTestBase::bSuppressLogErrors = false;
            FAutomationTestBase::bSuppressLogWarnings = false;
            return true;
        }),
        TEXT("restore log suppression statics")));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
