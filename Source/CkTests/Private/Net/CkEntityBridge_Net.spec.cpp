// The Ck Entity Bridge across machines: one entity per machine, promise order, whose spawn params win, which machines
// get a local entity, and the two ways an Actor ends up with no Ready entity (destroyed first, never finishes).
//
// Surface in Session Frontend: Ck.EntityBridge.Net.*

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Format/CkFormat.h"

#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkTests/Net/CkAutoTest_EntityBridgeSubject.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/StrongObjectPtr.h"

#include <tuple>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_entity_bridge_net
{
    constexpr auto PIEReadyTimeoutSeconds = 30.0f;
    constexpr auto BridgeReadyTimeoutSeconds = 15.0;
    constexpr auto FramesAfterLateBind = 10;

    constexpr auto OneEntityPerMachine_NumRemoteClients = 2;
    constexpr auto ContinueWithoutFinish_FramesObserved = 30;

    using SubjectClassType = TSubclassOf<ACk_AutoTest_EntityBridgeSubject>;
    using CapturerPtrType = TSharedRef<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>;

    auto
        Do_StartPIE(
            int32 InNumClients)
        -> void
    {
        // CkActorRelay x Iris ambient noise under multi-client PIE, as in the other net specs. Restored by Do_EndPIE.
        FAutomationTestBase::bSuppressLogErrors = true;
        FAutomationTestBase::bSuppressLogWarnings = true;

        // InNumClients counts the listen server's own window, so it is also the number of PIE worlds.
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(InNumClients, FString{TEXT("/Engine/Maps/Entry")}));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(InNumClients, PIEReadyTimeoutSeconds));
    }

    auto
        Do_EndPIE(
            FAutomationTestBase& InTest)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        // Restore static log suppression after teardown so subsequent tests see default semantics.
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(&InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
            {
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("restore log suppression statics")));
    }

    auto
        Do_Spawn(
            FAutomationTestBase& InTest,
            UWorld* InWorld,
            SubjectClassType InClass)
        -> ACk_AutoTest_EntityBridgeSubject*
    {
        auto SpawnInfo = FActorSpawnParameters{};
        SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        auto* Subject = InWorld->SpawnActor<ACk_AutoTest_EntityBridgeSubject>(InClass, FTransform::Identity, SpawnInfo);

        if (ck::Is_NOT_Valid(Subject))
        { InTest.AddError(ck::Format_UE(TEXT("spawn of [{}] returned null"), GetNameSafe(InClass.Get()))); }

        return Subject;
    }

    auto
        Get_IsReady(
            UWorld* InWorld,
            SubjectClassType InClass)
        -> bool
    {
        const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(InWorld, InClass);

        if (ck::Is_NOT_Valid(Subject))
        { return false; }

        return Subject->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::Ready;
    }

    auto
        Get_ActorCount(
            UWorld* InWorld,
            SubjectClassType InClass)
        -> int32
    {
        if (ck::Is_NOT_Valid(InWorld, ck::IsValid_Policy_NullptrOnly{}))
        { return 0; }

        auto Count = 0;

        for (auto It = TActorIterator<ACk_AutoTest_EntityBridgeSubject>(InWorld); It; ++It)
        {
            if (It->GetClass() == InClass.Get())
            { ++Count; }
        }

        return Count;
    }

    auto
        Get_Script(
            const FCk_Handle& InEntity)
        -> const UCk_AutoTest_EntityBridge_EntityScript_UE*
    {
        if (ck::Is_NOT_Valid(InEntity))
        { return nullptr; }

        return Cast<UCk_AutoTest_EntityBridge_EntityScript_UE>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(InEntity));
    }

    // FFragment_PendingReplication offers no read-only query. With no class default object, ConsumeFirst returns the
    // first entry recorded for the class; it removes an entry only when this check is about to fail the test anyway.
    auto
        Get_HasPendingReplicationEntry(
            UWorld* InWorld,
            UClass* InScriptClass,
            const FCk_Handle& InEntity)
        -> bool
    {
        if (ck::Is_NOT_Valid(InWorld, ck::IsValid_Policy_NullptrOnly{}) || ck::Is_NOT_Valid(InEntity))
        { return false; }

        auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld);

        if (ck::Is_NOT_Valid(TransientEntity))
        { return false; }

        if (NOT TransientEntity.Has<ck::FFragment_PendingReplication>())
        { return false; }

        const auto Pending = TransientEntity.Get<ck::FFragment_PendingReplication>().ConsumeFirst(InScriptClass, nullptr, InEntity);

        return ck::IsValid(Pending);
    }

    auto
        Do_TestState(
            FAutomationTestBase& InTest,
            const ACk_AutoTest_EntityBridgeSubject& InSubject,
            ECk_EntityBridge_State InExpected,
            const TCHAR* InMachine)
        -> void
    {
        const auto State = InSubject.Get_EntityBridge()->Get_State();

        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] bridge state is [{}] (is [{}])"), InMachine, InExpected, State),
            State == InExpected);
    }

    auto
        Do_TestReplication(
            FAutomationTestBase& InTest,
            const FCk_Handle& InEntity,
            ECk_Replication InExpected,
            const TCHAR* InMachine)
        -> void
    {
        if (ck::Is_NOT_Valid(InEntity))
        {
            InTest.AddError(ck::Format_UE(TEXT("[{}] has no entity to read the replication of"), InMachine));
            return;
        }

        const auto Replication = UCk_Utils_Net_UE::Get_EntityReplication(InEntity);

        InTest.TestTrue(*ck::Format_UE(TEXT("[{}] entity replication is [{}] (is [{}])"), InMachine, InExpected, Replication),
            Replication == InExpected);
    }

    auto
        Do_TestEventsInOrder(
            FAutomationTestBase& InTest,
            const ACk_AutoTest_EntityBridgeSubject& InSubject,
            const TCHAR* InMachine)
        -> void
    {
        using namespace ck::auto_test::entity_bridge;

        const auto Expected = TArray<FName>{
            FName{EventName_ScriptBeginPlay},
            FName{EventName_Ready}};

        const auto& Events = InSubject.Get_Events();

        if (Events == Expected)
        { return; }

        auto Observed = TArray<FString>{};

        for (const auto& Event : Events)
        { Observed.Add(Event.ToString()); }

        InTest.AddError(ck::Format_UE(TEXT("[{}] event list is [{}], expected [ScriptBeginPlay, Ready]"),
            InMachine, FString::Join(Observed, TEXT(", "))));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_OneEntityPerMachine,
    "Ck.EntityBridge.Net.OneEntityPerMachine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_OneEntityPerMachine::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = OneEntityPerMachine_NumRemoteClients + 1;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject::StaticClass()};

    // Set by the per-frame observer, never cleared: true once client N's entity carried the retry tag.
    auto ClientCarriedRetryTag = MakeShared<TArray<bool>>();
    ClientCarriedRetryTag->Init(false, OneEntityPerMachine_NumRemoteClients);

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass](UWorld* InServer) -> void
        {
            std::ignore = Do_Spawn(*this, InServer, SubjectClass);
        })));

    // Observes every frame from the link to Ready; records, asserts nothing.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass, ClientCarriedRetryTag]() -> bool
        {
            auto AllReady = Get_IsReady(ck::auto_test::net::Get_ServerWorld(), SubjectClass);

            for (auto ClientIdx = 0; ClientIdx < OneEntityPerMachine_NumRemoteClients; ++ClientIdx)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(ClientIdx);
                const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(Client, SubjectClass);

                if (ck::Is_NOT_Valid(Subject))
                {
                    AllReady = false;
                    continue;
                }

                const auto Entity = Subject->Get_EntityBridge()->Get_Entity();

                if (ck::IsValid(Entity) && Entity.Has<ck::FTag_EntityScript_PendingReplicationRetry>())
                { (*ClientCarriedRetryTag)[ClientIdx] = true; }

                AllReady = AllReady && Get_IsReady(Client, SubjectClass);
            }

            return AllReady;
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the subject's bridge is Ready on the server and on both clients")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass, ClientCarriedRetryTag]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* ServerSubject = ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass);

            if (ck::Is_NOT_Valid(ServerSubject))
            {
                AddError(TEXT("the server has no subject"));
                return false;
            }

            const auto ServerEntity = ServerSubject->Get_EntityBridge()->Get_Entity();

            Do_TestState(*this, *ServerSubject, ECk_EntityBridge_State::Ready, TEXT("Server"));
            TestTrue(TEXT("[Server] entity is valid"), ck::IsValid(ServerEntity));
            Do_TestReplication(*this, ServerEntity, ECk_Replication::Replicates, TEXT("Server"));
            TestEqual(TEXT("[Server] exactly one entity is linked to the subject"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Server, ServerSubject), 1);

            for (auto ClientIdx = 0; ClientIdx < OneEntityPerMachine_NumRemoteClients; ++ClientIdx)
            {
                const auto Machine = ck::Format_UE(TEXT("Client {}"), ClientIdx);
                auto* Client = ck::auto_test::net::Get_ClientWorld(ClientIdx);
                const auto* ClientSubject = ACk_AutoTest_EntityBridgeSubject::Find(Client, SubjectClass);

                if (ck::Is_NOT_Valid(ClientSubject))
                {
                    AddError(ck::Format_UE(TEXT("[{}] the subject did not replicate"), Machine));
                    continue;
                }

                const auto ClientEntity = ClientSubject->Get_EntityBridge()->Get_Entity();

                Do_TestState(*this, *ClientSubject, ECk_EntityBridge_State::Ready, *Machine);
                TestTrue(*ck::Format_UE(TEXT("[{}] entity is valid"), Machine), ck::IsValid(ClientEntity));
                TestTrue(*ck::Format_UE(TEXT("[{}] the entity's Actor is this client's Actor"), Machine),
                    UCk_Utils_EntityBridge_UE::Get_Actor(ClientEntity) == ClientSubject);
                TestTrue(*ck::Format_UE(TEXT("[{}] Get_HasEntity and Get_IsEntityReady agree with the bridge"), Machine),
                    UCk_Utils_EntityBridge_UE::Get_HasEntity(ClientSubject) &&
                    UCk_Utils_EntityBridge_UE::Get_IsEntityReady(ClientSubject));
                TestEqual(*ck::Format_UE(TEXT("[{}] exactly one entity is linked to the subject"), Machine),
                    ck::auto_test::entity_bridge::Get_LinkedEntityCount(Client, ClientSubject), 1);
                TestFalse(*ck::Format_UE(TEXT("[{}] the entity never carried the pending-replication retry tag"), Machine),
                    (*ClientCarriedRetryTag)[ClientIdx]);
                TestFalse(*ck::Format_UE(TEXT("[{}] no pending-replication entry was recorded for the script class"), Machine),
                    Get_HasPendingReplicationEntry(Client, ClientSubject->Get_EntityScriptClass().Get(), ClientEntity));
            }

            return true;
        }),
        TEXT("one entity per machine, the client's bound to its replicated Actor")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_PromisesOncePerMachineInOrder,
    "Ck.EntityBridge.Net.PromisesOncePerMachineInOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_PromisesOncePerMachineInOrder::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = 2;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject::StaticClass()};

    auto ServerCapturer = CapturerPtrType{MakeShared<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>(
        NewObject<UCk_AutoTest_EntityBridge_Capturer_UE>(GetTransientPackage()))};
    auto ClientCapturer = CapturerPtrType{MakeShared<TStrongObjectPtr<UCk_AutoTest_EntityBridge_Capturer_UE>>(
        NewObject<UCk_AutoTest_EntityBridge_Capturer_UE>(GetTransientPackage()))};

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass](UWorld* InServer) -> void
        {
            std::ignore = Do_Spawn(*this, InServer, SubjectClass);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            return Get_IsReady(ck::auto_test::net::Get_ServerWorld(), SubjectClass) &&
                Get_IsReady(ck::auto_test::net::Get_ClientWorld(0), SubjectClass);
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the subject's bridge is Ready on the server and the client")));

    // A Ready promise bound after Ready fires before the bind call returns.
    const auto BindAfterReady = [this, SubjectClass](UWorld* InWorld, UCk_AutoTest_EntityBridge_Capturer_UE& InCapturer, const TCHAR* InMachine) -> void
    {
        auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(InWorld, SubjectClass);

        if (ck::Is_NOT_Valid(Subject))
        {
            AddError(ck::Format_UE(TEXT("[{}] the subject is missing"), InMachine));
            return;
        }

        auto ReadyDelegate = FCk_Delegate_EntityBridge_OnEntityReady{};
        ReadyDelegate.BindDynamic(&InCapturer, &UCk_AutoTest_EntityBridge_Capturer_UE::OnEntityReady);
        Subject->Get_EntityBridge()->Promise_OnEntityReady(ReadyDelegate);

        TestEqual(*ck::Format_UE(TEXT("[{}] Ready bound after Ready fires immediately"), InMachine), InCapturer._ReadyCount, 1);
    };

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([BindAfterReady, ServerCapturer](UWorld* InServer) -> void
        {
            BindAfterReady(InServer, **ServerCapturer, TEXT("Server"));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([BindAfterReady, ClientCapturer](UWorld* InClient) -> void
        {
            BindAfterReady(InClient, **ClientCapturer, TEXT("Client"));
        })));

    // Gives a promise that would fire twice the frames to do it in.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterLateBind));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass, ServerCapturer, ClientCapturer]() -> bool
        {
            const auto* ServerSubject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);
            const auto* ClientSubject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ClientWorld(0), SubjectClass);

            if (ck::Is_NOT_Valid(ServerSubject) || ck::Is_NOT_Valid(ClientSubject))
            {
                AddError(TEXT("the subject is missing on the server or the client"));
                return false;
            }

            Do_TestEventsInOrder(*this, *ServerSubject, TEXT("Server"));
            Do_TestEventsInOrder(*this, *ClientSubject, TEXT("Client"));

            TestEqual(TEXT("[Server] Ready bound after Ready fired once in total"), (*ServerCapturer)->_ReadyCount, 1);
            TestEqual(TEXT("[Client] Ready bound after Ready fired once in total"), (*ClientCapturer)->_ReadyCount, 1);

            return true;
        }),
        TEXT("ScriptBeginPlay, then Ready, once each per machine")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_AuthorityParamsWin,
    "Ck.EntityBridge.Net.AuthorityParamsWin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_AuthorityParamsWin::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = 2;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject::StaticClass()};

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass](UWorld* InServer) -> void
        {
            std::ignore = Do_Spawn(*this, InServer, SubjectClass);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            return Get_IsReady(ck::auto_test::net::Get_ServerWorld(), SubjectClass) &&
                Get_IsReady(ck::auto_test::net::Get_ClientWorld(0), SubjectClass);
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the subject's bridge is Ready on the server and the client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass]() -> bool
        {
            using namespace ck::auto_test::entity_bridge;

            const auto* ServerSubject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);
            const auto* ClientSubject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ClientWorld(0), SubjectClass);

            if (ck::Is_NOT_Valid(ServerSubject) || ck::Is_NOT_Valid(ClientSubject))
            {
                AddError(TEXT("the subject is missing on the server or the client"));
                return false;
            }

            // The client did request with its own value; otherwise the next check proves nothing.
            TestEqual(TEXT("[Client] the Actor requested with the non-authority param value"),
                ClientSubject->Get_ParamValue(), ParamValue_NonAuthority);

            const auto* ServerScript = Get_Script(ServerSubject->Get_EntityBridge()->Get_Entity());
            const auto* ClientScript = Get_Script(ClientSubject->Get_EntityBridge()->Get_Entity());

            if (ck::Is_NOT_Valid(ServerScript) || ck::Is_NOT_Valid(ClientScript))
            {
                AddError(TEXT("the script instance is missing on the server or the client"));
                return false;
            }

            TestEqual(TEXT("[Server] the script instance received the authority's param value"),
                ServerScript->Get_ParamValue(), ParamValue_Authority);
            TestEqual(TEXT("[Client] the script instance received the authority's param value"),
                ClientScript->Get_ParamValue(), ParamValue_Authority);

            return true;
        }),
        TEXT("the authority's spawn params reach the client's script instance")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_LocalScriptOnReplicatedActor,
    "Ck.EntityBridge.Net.LocalScriptOnReplicatedActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_LocalScriptOnReplicatedActor::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = 2;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_LocalScript::StaticClass()};

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass](UWorld* InServer) -> void
        {
            std::ignore = Do_Spawn(*this, InServer, SubjectClass);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            return Get_IsReady(ck::auto_test::net::Get_ServerWorld(), SubjectClass) &&
                Get_IsReady(ck::auto_test::net::Get_ClientWorld(0), SubjectClass);
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the subject's bridge is Ready on the server and the client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass]() -> bool
        {
            using namespace ck::auto_test::entity_bridge;

            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            const auto* ServerSubject = ACk_AutoTest_EntityBridgeSubject::Find(Server, SubjectClass);
            const auto* ClientSubject = ACk_AutoTest_EntityBridgeSubject::Find(Client, SubjectClass);

            if (ck::Is_NOT_Valid(ServerSubject) || ck::Is_NOT_Valid(ClientSubject))
            {
                AddError(TEXT("the subject is missing on the server or the client"));
                return false;
            }

            const auto ServerEntity = ServerSubject->Get_EntityBridge()->Get_Entity();
            const auto ClientEntity = ClientSubject->Get_EntityBridge()->Get_Entity();

            Do_TestState(*this, *ServerSubject, ECk_EntityBridge_State::Ready, TEXT("Server"));
            Do_TestState(*this, *ClientSubject, ECk_EntityBridge_State::Ready, TEXT("Client"));

            TestEqual(TEXT("[Server] exactly one entity is linked to the subject"), Get_LinkedEntityCount(Server, ServerSubject), 1);
            TestEqual(TEXT("[Client] exactly one entity is linked to the subject"), Get_LinkedEntityCount(Client, ClientSubject), 1);

            Do_TestReplication(*this, ServerEntity, ECk_Replication::DoesNotReplicate, TEXT("Server"));
            Do_TestReplication(*this, ClientEntity, ECk_Replication::DoesNotReplicate, TEXT("Client"));

            TestFalse(TEXT("[Client] the entity has no replication driver"),
                ck::IsValid(ClientEntity) && UCk_Utils_EntityReplicationDriver_UE::Has(ClientEntity));

            const auto* ServerScript = Get_Script(ServerEntity);
            const auto* ClientScript = Get_Script(ClientEntity);

            if (ck::Is_NOT_Valid(ServerScript) || ck::Is_NOT_Valid(ClientScript))
            {
                AddError(TEXT("the script instance is missing on the server or the client"));
                return false;
            }

            TestEqual(TEXT("[Server] the script instance received the server's own param value"),
                ServerScript->Get_ParamValue(), ParamValue_Authority);
            TestEqual(TEXT("[Client] the script instance received the client's own param value"),
                ClientScript->Get_ParamValue(), ParamValue_NonAuthority);

            return true;
        }),
        TEXT("a non-replicating script on a replicated Actor builds a local entity on each machine")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_NonReplicatedActorIsLocal,
    "Ck.EntityBridge.Net.NonReplicatedActorIsLocal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_NonReplicatedActorIsLocal::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = 2;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_NotReplicated::StaticClass()};

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass](UWorld* InServer) -> void
        {
            std::ignore = Do_Spawn(*this, InServer, SubjectClass);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass](UWorld* InClient) -> void
        {
            std::ignore = Do_Spawn(*this, InClient, SubjectClass);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            return Get_IsReady(ck::auto_test::net::Get_ServerWorld(), SubjectClass) &&
                Get_IsReady(ck::auto_test::net::Get_ClientWorld(0), SubjectClass);
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("each world's own subject is Ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass]() -> bool
        {
            const auto Check = [this, SubjectClass](UWorld* InWorld, const TCHAR* InMachine) -> void
            {
                const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(InWorld, SubjectClass);

                if (ck::Is_NOT_Valid(Subject))
                {
                    AddError(ck::Format_UE(TEXT("[{}] the subject is missing"), InMachine));
                    return;
                }

                TestEqual(*ck::Format_UE(TEXT("[{}] the world holds only its own subject"), InMachine),
                    Get_ActorCount(InWorld, SubjectClass), 1);
                Do_TestState(*this, *Subject, ECk_EntityBridge_State::Ready, InMachine);
                Do_TestReplication(*this, Subject->Get_EntityBridge()->Get_Entity(), ECk_Replication::DoesNotReplicate, InMachine);
                TestEqual(*ck::Format_UE(TEXT("[{}] exactly one entity is linked to the subject"), InMachine),
                    ck::auto_test::entity_bridge::Get_LinkedEntityCount(InWorld, Subject), 1);
            };

            Check(ck::auto_test::net::Get_ServerWorld(), TEXT("Server"));
            Check(ck::auto_test::net::Get_ClientWorld(0), TEXT("Client"));

            return true;
        }),
        TEXT("a non-replicated Actor gets a local entity in whichever world spawns it")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_ClientAuthoritativeActorIsLocal,
    "Ck.EntityBridge.Net.ClientAuthoritativeActorIsLocal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_ClientAuthoritativeActorIsLocal::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = 2;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject::StaticClass()};

    // Set by the spawn and by the per-frame observer, never cleared.
    auto WasEverAwaitingReplica = MakeShared<bool>(false);

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass, WasEverAwaitingReplica](UWorld* InClient) -> void
        {
            const auto* Subject = Do_Spawn(*this, InClient, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            // The premise: a replicating Actor with local authority on a client.
            TestTrue(TEXT("[Client] the subject replicates"), Subject->GetIsReplicated());
            TestTrue(TEXT("[Client] the client-spawned subject has local authority"), Subject->GetLocalRole() == ROLE_Authority);

            // BeginPlay already requested inside SpawnActor.
            if (Subject->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::AwaitingReplica)
            { *WasEverAwaitingReplica = true; }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass, WasEverAwaitingReplica]() -> bool
        {
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ClientWorld(0), SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return false; }

            const auto State = Subject->Get_EntityBridge()->Get_State();

            if (State == ECk_EntityBridge_State::AwaitingReplica)
            { *WasEverAwaitingReplica = true; }

            return State == ECk_EntityBridge_State::Ready;
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the client-spawned subject is Ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass, WasEverAwaitingReplica]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(Client, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            {
                AddError(TEXT("[Client] the subject is missing"));
                return false;
            }

            Do_TestState(*this, *Subject, ECk_EntityBridge_State::Ready, TEXT("Client"));
            Do_TestReplication(*this, Subject->Get_EntityBridge()->Get_Entity(), ECk_Replication::DoesNotReplicate, TEXT("Client"));
            TestFalse(TEXT("[Client] the bridge was never AwaitingReplica"), *WasEverAwaitingReplica);
            TestEqual(TEXT("[Client] exactly one entity is linked to the subject"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(Client, Subject), 1);
            TestEqual(TEXT("[Server] the client-spawned subject did not reach the server"),
                Get_ActorCount(ck::auto_test::net::Get_ServerWorld(), SubjectClass), 0);

            return true;
        }),
        TEXT("a replicating Actor spawned by a client gets a local entity")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_ActorDestroyedBeforeSpawn,
    "Ck.EntityBridge.Net.ActorDestroyedBeforeSpawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_ActorDestroyedBeforeSpawn::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = 2;
    constexpr auto FramesAfterDestroy = 2;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject::StaticClass()};

    struct FObservations
    {
        TWeakObjectPtr<AActor> _Subject;
        int64 _BridgeAndScriptEnsuresBefore = 0;
    };

    auto Observations = MakeShared<FObservations>();

    const auto Get_BridgeAndScriptEnsureCount = []() -> int64
    {
        return ck::auto_test::entity_bridge::Get_EnsureCount_FromFiles(TEXT("EntityBridge")) +
            ck::auto_test::entity_bridge::Get_EnsureCount_FromFiles(TEXT("EntityScript"));
    };

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass, Observations, Get_BridgeAndScriptEnsureCount](UWorld* InServer) -> void
        {
            Observations->_BridgeAndScriptEnsuresBefore = Get_BridgeAndScriptEnsureCount();

            auto* Subject = Do_Spawn(*this, InServer, SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return; }

            Observations->_Subject = Subject;

            // Same frame: the spawn request queued by BeginPlay has not been processed yet.
            Subject->Destroy();
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterDestroy));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Observations, Get_BridgeAndScriptEnsureCount]() -> bool
        {
            if (Observations->_Subject.IsExplicitlyNull())
            {
                AddError(TEXT("the subject was never spawned"));
                return false;
            }

            TestEqual(TEXT("[Server] no entity is linked to the destroyed subject"),
                ck::auto_test::entity_bridge::Get_LinkedEntityCount(ck::auto_test::net::Get_ServerWorld(), Observations->_Subject), 0);
            TestEqual(TEXT("no EntityBridge or EntityScript ensure fired"),
                Get_BridgeAndScriptEnsureCount() - Observations->_BridgeAndScriptEnsuresBefore, int64{0});

            return true;
        }),
        TEXT("an Actor destroyed before its spawn is processed never gets an entity")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityBridgeNet_ContinueWithoutFinishIsNeverReady,
    "Ck.EntityBridge.Net.ContinueWithoutFinishIsNeverReady",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityBridgeNet_ContinueWithoutFinishIsNeverReady::RunTest(const FString& Parameters)
{
    using namespace ck_entity_bridge_net;

    constexpr auto NumPIEClients = 2;
    constexpr auto ObservationTimeoutSeconds = 30.0;

    const auto SubjectClass = SubjectClassType{ACk_AutoTest_EntityBridgeSubject_NeverFinishes::StaticClass()};

    struct FObservations
    {
        int32 _FramesObserved = 0;
        bool _ServerLeftLinked = false;
        bool _ClientBecameReady = false;
    };

    auto Observations = MakeShared<FObservations>();

    Do_StartPIE(NumPIEClients);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, SubjectClass](UWorld* InServer) -> void
        {
            std::ignore = Do_Spawn(*this, InServer, SubjectClass);
        })));

    // The positive half: the server did link, so the silence that follows is the script's, not a spawn that never ran.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass]() -> bool
        {
            const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);

            if (ck::Is_NOT_Valid(Subject))
            { return false; }

            return Subject->Get_EntityBridge()->Get_State() == ECk_EntityBridge_State::Linked;
        }),
        BridgeReadyTimeoutSeconds,
        TEXT("the server's bridge is Linked")));

    // Observes ContinueWithoutFinish_FramesObserved frames, one per poll; records, asserts nothing.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SubjectClass, Observations]() -> bool
        {
            const auto* ServerSubject = ACk_AutoTest_EntityBridgeSubject::Find(ck::auto_test::net::Get_ServerWorld(), SubjectClass);

            if (ck::Is_NOT_Valid(ServerSubject) ||
                ServerSubject->Get_EntityBridge()->Get_State() != ECk_EntityBridge_State::Linked)
            { Observations->_ServerLeftLinked = true; }

            if (Get_IsReady(ck::auto_test::net::Get_ClientWorld(0), SubjectClass))
            { Observations->_ClientBecameReady = true; }

            ++Observations->_FramesObserved;

            return Observations->_FramesObserved >= ContinueWithoutFinish_FramesObserved;
        }),
        ObservationTimeoutSeconds,
        TEXT("observed the bridge for the frame budget")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SubjectClass, Observations]() -> bool
        {
            TestFalse(TEXT("[Server] the bridge stayed Linked on every observed frame"), Observations->_ServerLeftLinked);
            TestFalse(TEXT("[Client] the bridge never became Ready"), Observations->_ClientBecameReady);

            const auto Check = [this, SubjectClass](UWorld* InWorld, const TCHAR* InMachine) -> void
            {
                const auto* Subject = ACk_AutoTest_EntityBridgeSubject::Find(InWorld, SubjectClass);

                if (ck::Is_NOT_Valid(Subject))
                { return; }

                const auto& Events = Subject->Get_Events();

                TestEqual(*ck::Format_UE(TEXT("[{}] Ready never fired and the script never began play"), InMachine),
                    Events.Num(), 0);
            };

            Check(ck::auto_test::net::Get_ServerWorld(), TEXT("Server"));
            Check(ck::auto_test::net::Get_ClientWorld(0), TEXT("Client"));

            return true;
        }),
        TEXT("a script that never finishes construction is never Ready")));

    Do_EndPIE(*this);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
