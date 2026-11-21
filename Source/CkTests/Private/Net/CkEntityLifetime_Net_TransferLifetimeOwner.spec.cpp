// A server-side UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner must reach clients
// (CkFoundation/Source/CkEcs/DESIGN_ReplicatedLifetimeOwner.md). Subject: ACk_AutoTest_NetSubject_LifetimeTransfer_UE.
//
// C++ rather than AngelScript net rows: the transfer has no UFUNCTION or script binding, and the replication-driver
// net rows in this folder are hand-written C++ specs too.
//
// OrdinaryTeardown is green on today's code and must stay green: it guards the client destroy cascade against leaks.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"

#include "CkCore/Format/CkFormat.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkTests/Net/CkAutoTest_NetSubject_LifetimeTransfer.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_entity_lifetime_net_transfer_spec
{
    using namespace ck::auto_test::lifetime_transfer;

    inline constexpr auto TestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

    inline constexpr auto NumPIEClients = 2;
    inline constexpr auto ExpectedTotalWorlds = 2;
    inline constexpr auto PIEReadyTimeoutSeconds = 30.0f;
    inline constexpr auto BuildTimeoutSeconds = 10.0;
    inline constexpr auto ConvergeTimeoutSeconds = 10.0;

    // OrdinaryTeardown destroys A + C + D (one level) and G + W + C2 (two levels); B is the only replicated member left.
    inline constexpr auto NumTeardownDestroyedMembers = 6;
    inline constexpr auto NumMembersAfterTeardown = NumReplicatedMembers - NumTeardownDestroyedMembers;

    struct FWorldRoles
    {
        FCk_Handle OwnerA;
        FCk_Handle OwnerB;
        FCk_Handle Child;
        FCk_Handle GrandOwner;
        FCk_Handle MidOwner;
        FCk_Handle ChildOfMid;
        FCk_Handle Holder;
        FCk_Handle Root;
        FCk_Handle DefinitionChild;
    };

    struct FState
    {
        FWorldRoles Server;
        FWorldRoles Client;
        int32 ClientMembersBefore = INDEX_NONE;
        int32 OwnerAExpectedDependentsBefore = INDEX_NONE;
        int32 OwnerBExpectedDependentsBefore = INDEX_NONE;
        FCk_Handle ClientChildBeforeTransfer;
    };

    // ----------------------------------------------------------------------------------------------------------------

    auto
        Get_IsAlive(
            const FCk_Handle& InHandle)
        -> bool
    {
        return ck::IsValid(InHandle) &&
            NOT UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    // Past Finalize: every step of the entity's own destruction, including anything it does to its dependents, is done.
    auto
        Get_IsReclaimed(
            const FCk_Handle& InHandle)
        -> bool
    {
        return NOT ck::IsValid(InHandle, ck::IsValid_Policy_IncludePendingKill{});
    }

    auto
        Get_Owner(
            const FCk_Handle& InHandle)
        -> FCk_Handle
    {
        if (Get_IsReclaimed(InHandle) || NOT InHandle.Has<ck::FFragment_LifetimeOwner>())
        { return {}; }

        return UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(InHandle, ECk_PendingKill_Policy::IncludePendingKill);
    }

    auto
        Get_IsAliveUnder(
            const FCk_Handle& InHandle,
            const FCk_Handle& InOwner)
        -> bool
    {
        return Get_IsAlive(InHandle) && NOT Get_IsReclaimed(InOwner) && Get_Owner(InHandle) == InOwner;
    }

    auto
        Get_Driver(
            const FCk_Handle& InHandle)
        -> const UCk_Fragment_EntityReplicationDriver_Rep*
    {
        if (NOT ck::IsValid(InHandle) || NOT UCk_Utils_EntityReplicationDriver_UE::Has(InHandle))
        { return nullptr; }

        return InHandle.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
    }

    auto
        Get_IsPublished(
            const FCk_Handle& InHandle)
        -> bool
    {
        const auto* Driver = Get_Driver(InHandle);
        return Driver != nullptr && (Driver->Get_ReplicationData_EntityScript().Get_EntityScriptClass().Get() != nullptr
            || NOT Driver->Get_ReplicationData().Get_ConstructionInfos().IsEmpty());
    }

    auto
        Get_ConstructCount(
            const FCk_Handle& InHandle)
        -> int32
    {
        if (NOT ck::IsValid(InHandle) || NOT InHandle.Has<FFragment_LifetimeTransfer_ConstructCount>())
        { return 0; }

        return InHandle.Get<FFragment_LifetimeTransfer_ConstructCount>()._Count;
    }

    auto
        Describe_Entity(
            const FWorldRoles& InRoles,
            const FCk_Handle& InHandle)
        -> FString
    {
        if (Get_IsReclaimed(InHandle))
        { return TEXT("none"); }

        if (InHandle == InRoles.OwnerA) { return TEXT("A"); }
        if (InHandle == InRoles.OwnerB) { return TEXT("B"); }
        if (InHandle == InRoles.Holder) { return TEXT("H"); }
        if (InHandle == InRoles.GrandOwner) { return TEXT("G"); }
        if (InHandle == InRoles.MidOwner) { return TEXT("W"); }
        if (InHandle == InRoles.Child) { return TEXT("C"); }
        if (InHandle == InRoles.ChildOfMid) { return TEXT("C2"); }
        if (InHandle == InRoles.DefinitionChild) { return TEXT("D"); }
        if (InHandle == InRoles.Root) { return TEXT("Root"); }

        if (UCk_Utils_EntityLifetime_UE::Get_IsTransientEntity(InHandle))
        { return TEXT("the transient entity"); }

        return ck::Format_UE(TEXT("an unrelated entity [{}]"), InHandle);
    }

    auto
        Describe_State(
            const FWorldRoles& InRoles,
            const FCk_Handle& InHandle)
        -> FString
    {
        if (Get_IsReclaimed(InHandle))
        { return TEXT("destroyed"); }

        const auto Owner = Describe_Entity(InRoles, Get_Owner(InHandle));

        if (NOT Get_IsAlive(InHandle))
        { return ck::Format_UE(TEXT("being destroyed, lifetime owner {}"), Owner); }

        return ck::Format_UE(TEXT("alive, lifetime owner {}"), Owner);
    }

    auto
        Describe_Driver(
            const FWorldRoles& InRoles,
            const UCk_Fragment_EntityReplicationDriver_Rep* InDriver)
        -> FString
    {
        if (InDriver == nullptr)
        { return TEXT("null"); }

        if (InDriver == Get_Driver(InRoles.OwnerA)) { return TEXT("A's driver"); }
        if (InDriver == Get_Driver(InRoles.OwnerB)) { return TEXT("B's driver"); }

        return TEXT("another driver");
    }

    auto
        Check(
            FAutomationTestBase* InTest,
            bool InCondition,
            const FString& InMessage)
        -> bool
    {
        if (NOT InCondition)
        { InTest->AddError(InMessage); }

        return InCondition;
    }

    // ----------------------------------------------------------------------------------------------------------------

    template <typename T_Tag>
    auto
        Find_Alive(
            UWorld* InWorld,
            int32& OutNumAlive)
        -> FCk_Handle
    {
        OutNumAlive = 0;

        if (InWorld == nullptr)
        { return {}; }

        auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld);
        if (ck::Is_NOT_Valid(Transient))
        { return {}; }

        auto Found = FCk_Handle{};
        Transient.View<T_Tag>().ForEach([&](FCk_Entity InEntity)
        {
            const auto Handle = ck::MakeHandle(InEntity, Transient);
            if (NOT Get_IsAlive(Handle))
            { return; }

            ++OutNumAlive;
            if (ck::Is_NOT_Valid(Found))
            { Found = Handle; }
        });

        return Found;
    }

    auto
        Get_AliveMembersWithDriver(
            UWorld* InWorld)
        -> TArray<FCk_Handle>
    {
        auto Members = TArray<FCk_Handle>{};

        if (InWorld == nullptr)
        { return Members; }

        auto Transient = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld);
        if (ck::Is_NOT_Valid(Transient))
        { return Members; }

        Transient.View<FTag_LifetimeTransfer_Member>().ForEach([&](FCk_Entity InEntity)
        {
            const auto Handle = ck::MakeHandle(InEntity, Transient);
            if (Get_IsAlive(Handle) && UCk_Utils_EntityReplicationDriver_UE::Has(Handle))
            { Members.Emplace(Handle); }
        });

        return Members;
    }

    template <typename T_Tag>
    auto
        DoResolve(
            UWorld* InWorld,
            const FString& InName,
            FCk_Handle& OutHandle,
            TArray<FString>& OutProblems)
        -> void
    {
        auto NumAlive = 0;
        OutHandle = Find_Alive<T_Tag>(InWorld, NumAlive);

        if (NumAlive != 1)
        { OutProblems.Emplace(ck::Format_UE(TEXT("{} found alive {} times (expected once)"), InName, NumAlive)); }
    }

    // Resolves every role in InWorld, then checks the starting topology. Empty means ready.
    auto
        Get_TopologyProblems(
            UWorld* InWorld,
            bool InIsServer,
            FWorldRoles& OutRoles)
        -> TArray<FString>
    {
        if (InWorld == nullptr)
        { return TArray<FString>{ FString{TEXT("world unavailable")} }; }

        auto Problems = TArray<FString>{};
        DoResolve<FTag_LifetimeTransfer_OwnerA>(InWorld, TEXT("A"), OutRoles.OwnerA, Problems);
        DoResolve<FTag_LifetimeTransfer_OwnerB>(InWorld, TEXT("B"), OutRoles.OwnerB, Problems);
        DoResolve<FTag_LifetimeTransfer_Child>(InWorld, TEXT("C"), OutRoles.Child, Problems);
        DoResolve<FTag_LifetimeTransfer_GrandOwner>(InWorld, TEXT("G"), OutRoles.GrandOwner, Problems);
        DoResolve<FTag_LifetimeTransfer_MidOwner>(InWorld, TEXT("W"), OutRoles.MidOwner, Problems);
        DoResolve<FTag_LifetimeTransfer_ChildOfMid>(InWorld, TEXT("C2"), OutRoles.ChildOfMid, Problems);
        DoResolve<FTag_LifetimeTransfer_Holder>(InWorld, TEXT("H"), OutRoles.Holder, Problems);
        DoResolve<FTag_LifetimeTransfer_Root>(InWorld, TEXT("Root"), OutRoles.Root, Problems);
        DoResolve<FTag_LifetimeTransfer_DefinitionChild>(InWorld, TEXT("D"), OutRoles.DefinitionChild, Problems);

        if (NOT Problems.IsEmpty())
        { return Problems; }

        const auto ExpectUnder = [&](const FCk_Handle& InChild, const FCk_Handle& InOwner)
        {
            if (NOT Get_IsAliveUnder(InChild, InOwner))
            {
                Problems.Emplace(ck::Format_UE(TEXT("expected {} alive under {}; {} is {}"),
                    Describe_Entity(OutRoles, InChild), Describe_Entity(OutRoles, InOwner),
                    Describe_Entity(OutRoles, InChild), Describe_State(OutRoles, InChild)));
            }
        };

        ExpectUnder(OutRoles.Child, OutRoles.OwnerA);
        ExpectUnder(OutRoles.ChildOfMid, OutRoles.MidOwner);
        ExpectUnder(OutRoles.MidOwner, OutRoles.GrandOwner);
        ExpectUnder(OutRoles.Holder, OutRoles.OwnerB);
        ExpectUnder(OutRoles.DefinitionChild, OutRoles.OwnerA);
        ExpectUnder(OutRoles.OwnerA, OutRoles.Root);
        ExpectUnder(OutRoles.OwnerB, OutRoles.Root);
        ExpectUnder(OutRoles.GrandOwner, OutRoles.Root);

        if (UCk_Utils_EntityReplicationDriver_UE::Has(OutRoles.Holder))
        { Problems.Emplace(TEXT("H has a replication driver; it must be a driverless plain entity")); }

        // On a client the driver fragment is linked when construction finishes; on the server the replication data is
        // published by the replicate processor after Construct.
        const auto ExpectReplicated = [&](const FCk_Handle& InHandle)
        {
            if (NOT UCk_Utils_EntityReplicationDriver_UE::Has(InHandle))
            { Problems.Emplace(ck::Format_UE(TEXT("{} has no replication driver yet"), Describe_Entity(OutRoles, InHandle))); }
            else if (InIsServer && NOT Get_IsPublished(InHandle))
            { Problems.Emplace(ck::Format_UE(TEXT("{} has no published EntityScript replication data yet"), Describe_Entity(OutRoles, InHandle))); }
        };

        ExpectReplicated(OutRoles.OwnerA);
        ExpectReplicated(OutRoles.OwnerB);
        ExpectReplicated(OutRoles.Child);
        ExpectReplicated(OutRoles.GrandOwner);
        ExpectReplicated(OutRoles.MidOwner);
        ExpectReplicated(OutRoles.ChildOfMid);
        ExpectReplicated(OutRoles.DefinitionChild);

        return Problems;
    }

    auto
        Request_Transfer(
            FAutomationTestBase* InTest,
            FCk_Handle& InEntity,
            const FCk_Handle& InNewOwner,
            const TCHAR* InWhat)
        -> void
    {
        if (NOT Get_IsAlive(InEntity) || NOT Get_IsAlive(InNewOwner))
        {
            InTest->AddError(ck::Format_UE(TEXT("server: cannot transfer {}: an entity is not alive (setup failed?)"), FString{InWhat}));
            return;
        }

        UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(InEntity, InNewOwner);
    }

    // ----------------------------------------------------------------------------------------------------------------

    auto
        Add_StartAndBuild(
            FAutomationTestBase* InTest,
            const TSharedRef<FState>& InState,
            bool InWaitForClient)
        -> void
    {
        // CkActorRelay x Iris ambient noise under multi-client PIE; the verdicts are the assertions. Restored in Add_End.
        FAutomationTestBase::bSuppressLogErrors = true;
        FAutomationTestBase::bSuppressLogWarnings = true;

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, FString{TEXT("/Engine/Maps/Entry")}));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(ExpectedTotalWorlds, PIEReadyTimeoutSeconds));

        const auto SpawnSubject = FCk_NetAutoTest_ServerAction::CreateLambda([InTest](UWorld* InServer) -> void
        {
            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            const auto* Subject = InServer->SpawnActor<ACk_AutoTest_NetSubject_LifetimeTransfer_UE>(
                ACk_AutoTest_NetSubject_LifetimeTransfer_UE::StaticClass(), FTransform::Identity, SpawnInfo);

            if (Subject == nullptr)
            { InTest->AddError(TEXT("server: SpawnActor of the lifetime-transfer subject returned null")); }
        });
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(SpawnSubject));

        const auto ServerBuilt = FCk_NetAutoTest_Condition::CreateLambda([InState]() -> bool
        {
            return Get_TopologyProblems(ck::auto_test::net::Get_ServerWorld(), true, InState->Server).IsEmpty();
        });
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest, ServerBuilt, BuildTimeoutSeconds,
            TEXT("server: subject built (A, B, G, W, C, C2 replicated and published, H driverless under B)")));

        const auto ServerBuiltVerdict = FCk_NetAutoTest_Assertion::CreateLambda([InTest, InState]() -> bool
        {
            const auto Problems = Get_TopologyProblems(ck::auto_test::net::Get_ServerWorld(), true, InState->Server);
            return Check(InTest, Problems.IsEmpty(),
                ck::Format_UE(TEXT("server: subject setup incomplete: {}"), FString::Join(Problems, TEXT("; "))));
        });
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest, ServerBuiltVerdict, TEXT("server subject setup")));

        if (NOT InWaitForClient)
        { return; }

        const auto ClientBuilt = FCk_NetAutoTest_Condition::CreateLambda([InState]() -> bool
        {
            return Get_TopologyProblems(ck::auto_test::net::Get_ClientWorld(0), false, InState->Client).IsEmpty();
        });
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest, ClientBuilt, BuildTimeoutSeconds,
            TEXT("client: subject replicated with the server's starting topology (C under A, C2 under W under G, H under B)")));

        const auto ClientBuiltVerdict = FCk_NetAutoTest_Assertion::CreateLambda([InTest, InState]() -> bool
        {
            const auto Problems = Get_TopologyProblems(ck::auto_test::net::Get_ClientWorld(0), false, InState->Client);
            return Check(InTest, Problems.IsEmpty(),
                ck::Format_UE(TEXT("client: starting topology not reached: {}"), FString::Join(Problems, TEXT("; "))));
        });
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest, ClientBuiltVerdict, TEXT("client subject setup")));
    }

    auto
        Add_End(
            FAutomationTestBase* InTest)
        -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        const auto Restore = FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
        {
            FAutomationTestBase::bSuppressLogErrors = false;
            FAutomationTestBase::bSuppressLogWarnings = false;
            return true;
        });
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest, Restore, TEXT("restore log suppression statics")));
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_TransferBetweenReplicatedOwners_ClientOwnerFollows,
    "Ck.EntityLifetime.Net.TransferBetweenReplicatedOwners_ClientOwnerFollows",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_TransferBetweenReplicatedOwners_ClientOwnerFollows::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    const auto Transfer = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.Child, State->Server.OwnerB, TEXT("C from A to B"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(Transfer));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: C's lifetime owner becomes B after the server moves C from A to B")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        auto Passed = Check(this, Get_IsAliveUnder(State->Server.Child, State->Server.OwnerB),
            ck::Format_UE(TEXT("server: C should be alive under B after the transfer; C is {}"),
                Describe_State(State->Server, State->Server.Child)));

        Passed = Check(this, Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB),
            ck::Format_UE(TEXT("client: C should be alive under B, its owner on the server; C is {}"),
                Describe_State(State->Client, State->Client.Child))) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict, TEXT("client lifetime owner follows a transfer")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_TransferUnderDriverlessChild_ClientOwnerIsNearestReplicatedAncestor,
    "Ck.EntityLifetime.Net.TransferUnderDriverlessChild_ClientOwnerIsNearestReplicatedAncestor",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_TransferUnderDriverlessChild_ClientOwnerIsNearestReplicatedAncestor::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    const auto Transfer = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.Child, State->Server.Holder, TEXT("C from A to H"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(Transfer));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: C's lifetime owner becomes B (H's nearest replicated ancestor) after the server moves C under H")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        auto Passed = Check(this, Get_IsAliveUnder(State->Server.Child, State->Server.Holder),
            ck::Format_UE(TEXT("server: C should be alive under H after the transfer; C is {}"),
                Describe_State(State->Server, State->Server.Child)));

        Passed = Check(this, Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB),
            ck::Format_UE(TEXT("client: C should be alive under B, the nearest replicated ancestor of the server's owner H "
                "(not H, not the transient entity); C is {}"),
                Describe_State(State->Client, State->Client.Child))) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict,
        TEXT("client lifetime owner is the nearest replicated ancestor of a driverless server owner")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_TransferThenDestroyOldOwner_ChildSurvivesOnClient,
    "Ck.EntityLifetime.Net.TransferThenDestroyOldOwner_ChildSurvivesOnClient",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_TransferThenDestroyOldOwner_ChildSurvivesOnClient::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    // Same server frame: the owner rewrite and the old owner's destroy race to the client.
    const auto TransferAndDestroy = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.Child, State->Server.OwnerB, TEXT("C from A to B"));
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Server.OwnerA);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(TransferAndDestroy));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsReclaimed(State->Client.OwnerA) && Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: A destroyed and C alive under B")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        auto Passed = Check(this, Get_IsAliveUnder(State->Server.Child, State->Server.OwnerB),
            ck::Format_UE(TEXT("server: C should be alive under B; C is {}"), Describe_State(State->Server, State->Server.Child)));

        Passed = Check(this, Get_IsReclaimed(State->Client.OwnerA),
            ck::Format_UE(TEXT("client: A should be destroyed; A is {}"), Describe_State(State->Client, State->Client.OwnerA))) && Passed;

        Passed = Check(this, Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB),
            ck::Format_UE(TEXT("client: C should outlive its old owner A and end up under B; C is {}"),
                Describe_State(State->Client, State->Client.Child))) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict, TEXT("transferred child survives its old owner's destroy on the client")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_TransferOutOfChildThenDestroyItsOwner_ChildSurvivesOnClient,
    "Ck.EntityLifetime.Net.TransferOutOfChildThenDestroyItsOwner_ChildSurvivesOnClient",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_TransferOutOfChildThenDestroyItsOwner_ChildSurvivesOnClient::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    // C2 leaves W (itself a replicated child of G); destroying G takes W with it on every machine.
    const auto TransferAndDestroy = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.ChildOfMid, State->Server.OwnerB, TEXT("C2 from W to B"));
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Server.GrandOwner);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(TransferAndDestroy));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsReclaimed(State->Client.GrandOwner)
            && Get_IsReclaimed(State->Client.MidOwner)
            && Get_IsAliveUnder(State->Client.ChildOfMid, State->Client.OwnerB);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: G and W destroyed and C2 alive under B")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        auto Passed = Check(this, Get_IsAliveUnder(State->Server.ChildOfMid, State->Server.OwnerB),
            ck::Format_UE(TEXT("server: C2 should be alive under B; C2 is {}"), Describe_State(State->Server, State->Server.ChildOfMid)));

        Passed = Check(this, Get_IsReclaimed(State->Client.GrandOwner) && Get_IsReclaimed(State->Client.MidOwner),
            ck::Format_UE(TEXT("client: G and W should be destroyed; G is {}, W is {}"),
                Describe_State(State->Client, State->Client.GrandOwner),
                Describe_State(State->Client, State->Client.MidOwner))) && Passed;

        Passed = Check(this, Get_IsAliveUnder(State->Client.ChildOfMid, State->Client.OwnerB),
            ck::Format_UE(TEXT("client: C2 should survive the destroy of its old owner's owner G and end up under B; C2 is {}"),
                Describe_State(State->Client, State->Client.ChildOfMid))) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict,
        TEXT("child transferred out of a replicated child survives its old ancestor's destroy on the client")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_TransferAwayAndBackThenDestroyInterimOwner_ChildStaysUnderOriginalOwner,
    "Ck.EntityLifetime.Net.TransferAwayAndBackThenDestroyInterimOwner_ChildStaysUnderOriginalOwner",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_TransferAwayAndBackThenDestroyInterimOwner_ChildStaysUnderOriginalOwner::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    // A -> B, converged on the client before moving back, so the client really held C under B.
    const auto ToB = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.Child, State->Server.OwnerB, TEXT("C from A to B"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(ToB));

    const auto ClientUnderB = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, ClientUnderB, ConvergeTimeoutSeconds,
        TEXT("client: C moves under B (first leg)")));

    const auto ClientUnderBVerdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        return Check(this, Get_IsAliveUnder(State->Client.Child, State->Client.OwnerB),
            ck::Format_UE(TEXT("client: after the first leg C should be alive under B; C is {}"),
                Describe_State(State->Client, State->Client.Child)));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, ClientUnderBVerdict, TEXT("first leg A -> B")));

    const auto BackToA = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.Child, State->Server.OwnerA, TEXT("C from B back to A"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(BackToA));

    const auto ClientUnderA = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsAliveUnder(State->Client.Child, State->Client.OwnerA);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, ClientUnderA, ConvergeTimeoutSeconds,
        TEXT("client: C moves back under A (second leg)")));

    const auto DestroyB = FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld*) -> void
    {
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Server.OwnerB);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(DestroyB));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsReclaimed(State->Client.OwnerB) && Get_IsAliveUnder(State->Client.Child, State->Client.OwnerA);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: B destroyed and C alive under A")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        auto Passed = Check(this, Get_IsAliveUnder(State->Server.Child, State->Server.OwnerA),
            ck::Format_UE(TEXT("server: C should be alive under A; C is {}"), Describe_State(State->Server, State->Server.Child)));

        Passed = Check(this, Get_IsReclaimed(State->Client.OwnerB),
            ck::Format_UE(TEXT("client: B should be destroyed; B is {}"), Describe_State(State->Client, State->Client.OwnerB))) && Passed;

        Passed = Check(this, Get_IsAliveUnder(State->Client.Child, State->Client.OwnerA),
            ck::Format_UE(TEXT("client: C should stay alive under A after its interim owner B is destroyed; C is {}"),
                Describe_State(State->Client, State->Client.Child))) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict, TEXT("A -> B -> A then destroy B keeps C under A on the client")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_OrdinaryTeardown_DestroysEveryReplicatedMemberOnClient,
    "Ck.EntityLifetime.Net.OrdinaryTeardown_DestroysEveryReplicatedMemberOnClient",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_OrdinaryTeardown_DestroysEveryReplicatedMemberOnClient::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    const auto CountBefore = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto ServerMembers = Get_AliveMembersWithDriver(ck::auto_test::net::Get_ServerWorld()).Num();
        State->ClientMembersBefore = Get_AliveMembersWithDriver(ck::auto_test::net::Get_ClientWorld(0)).Num();

        auto Passed = Check(this, ServerMembers == NumReplicatedMembers,
            ck::Format_UE(TEXT("server: expected {} live replicated members before the teardown, counted {}"),
                NumReplicatedMembers, ServerMembers));

        Passed = Check(this, State->ClientMembersBefore == NumReplicatedMembers,
            ck::Format_UE(TEXT("client: expected {} live replicated members before the teardown, counted {}"),
                NumReplicatedMembers, State->ClientMembersBefore)) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, CountBefore, TEXT("member count before the teardown")));

    const auto Destroy = FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld*) -> void
    {
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Server.OwnerA);
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Server.GrandOwner);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(Destroy));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        const auto& Client = State->Client;
        return Get_IsReclaimed(Client.OwnerA)
            && Get_IsReclaimed(Client.Child)
            && Get_IsReclaimed(Client.GrandOwner)
            && Get_IsReclaimed(Client.MidOwner)
            && Get_IsReclaimed(Client.ChildOfMid)
            && Get_AliveMembersWithDriver(ck::auto_test::net::Get_ClientWorld(0)).Num() == NumMembersAfterTeardown;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: A, C, G, W, C2 all destroyed and only B left among the subject's replicated members")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto& Client = State->Client;
        const auto Survivors = Get_AliveMembersWithDriver(ck::auto_test::net::Get_ClientWorld(0));

        auto SurvivorDescriptions = TArray<FString>{};
        for (const auto& Survivor : Survivors)
        {
            SurvivorDescriptions.Emplace(ck::Format_UE(TEXT("{} ({})"),
                Describe_Entity(Client, Survivor), Describe_State(Client, Survivor)));
        }

        auto Passed = Check(this,
            Get_IsReclaimed(Client.OwnerA) && Get_IsReclaimed(Client.Child) && Get_IsReclaimed(Client.GrandOwner)
                && Get_IsReclaimed(Client.MidOwner) && Get_IsReclaimed(Client.ChildOfMid),
            ck::Format_UE(TEXT("client: every member of the destroyed subtrees should be destroyed; A is {}, C is {}, G is {}, W is {}, C2 is {}"),
                Describe_State(Client, Client.OwnerA), Describe_State(Client, Client.Child),
                Describe_State(Client, Client.GrandOwner), Describe_State(Client, Client.MidOwner),
                Describe_State(Client, Client.ChildOfMid)));

        Passed = Check(this, Survivors.Num() == NumMembersAfterTeardown && Get_IsAlive(Client.OwnerB),
            ck::Format_UE(TEXT("client: live replicated members went from {} to {}, expected {} (only B); survivors: {}"),
                State->ClientMembersBefore, Survivors.Num(), NumMembersAfterTeardown,
                FString::Join(SurvivorDescriptions, TEXT(", ")))) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict, TEXT("ordinary teardown leaves no replicated member behind on the client")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_Transfer_RepublishesOwnerAndReleasesConstructionDependencyOnServer,
    "Ck.EntityLifetime.Net.Transfer_RepublishesOwnerAndReleasesConstructionDependencyOnServer",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_Transfer_RepublishesOwnerAndReleasesConstructionDependencyOnServer::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, false);

    // Precondition: C was built inside A's construction, so it starts as A's construction dependent. Without this the
    // "flag cleared" assertion below would hold on any code.
    const auto Precondition = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto* DriverA = Get_Driver(State->Server.OwnerA);
        const auto* DriverB = Get_Driver(State->Server.OwnerB);
        const auto* DriverC = Get_Driver(State->Server.Child);

        if (NOT Check(this, DriverA != nullptr && DriverB != nullptr && DriverC != nullptr,
            TEXT("server: A, B and C must all have a replication driver")))
        { return false; }

        const auto& Data = DriverC->Get_ReplicationData_EntityScript();

        auto Passed = Check(this, Data.Get_IsOwningEntityDriverDependentOnThis(),
            TEXT("server precondition: C is not marked a construction dependent of A, so this row would pass for the wrong reason"));

        Passed = Check(this, Data.Get_OwningEntityDriver().Get() == DriverA,
            ck::Format_UE(TEXT("server precondition: C should publish A's driver as its owner; it publishes {}"),
                Describe_Driver(State->Server, Data.Get_OwningEntityDriver().Get()))) && Passed;

        State->OwnerAExpectedDependentsBefore = DriverA->Get_ExpectedNumberOfDependentReplicationDrivers();
        State->OwnerBExpectedDependentsBefore = DriverB->Get_ExpectedNumberOfDependentReplicationDrivers();

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Precondition, TEXT("C starts as A's construction dependent")));

    const auto Transfer = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.Child, State->Server.OwnerB, TEXT("C from A to B"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(Transfer));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        const auto* DriverA = Get_Driver(State->Server.OwnerA);
        const auto* DriverB = Get_Driver(State->Server.OwnerB);
        const auto* DriverC = Get_Driver(State->Server.Child);

        if (DriverA == nullptr || DriverB == nullptr || DriverC == nullptr)
        { return false; }

        const auto& Data = DriverC->Get_ReplicationData_EntityScript();
        return NOT Data.Get_IsOwningEntityDriverDependentOnThis()
            && Data.Get_OwningEntityDriver().Get() == DriverB
            && DriverA->Get_ExpectedNumberOfDependentReplicationDrivers() == State->OwnerAExpectedDependentsBefore - 1;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("server: C publishes B, is no longer A's construction dependent, and A expects one dependent fewer")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto* DriverA = Get_Driver(State->Server.OwnerA);
        const auto* DriverB = Get_Driver(State->Server.OwnerB);
        const auto* DriverC = Get_Driver(State->Server.Child);

        if (NOT Check(this, DriverA != nullptr && DriverB != nullptr && DriverC != nullptr,
            TEXT("server: A, B and C must still have a replication driver after the transfer")))
        { return false; }

        const auto& Data = DriverC->Get_ReplicationData_EntityScript();
        const auto CountA = DriverA->Get_ExpectedNumberOfDependentReplicationDrivers();
        const auto CountB = DriverB->Get_ExpectedNumberOfDependentReplicationDrivers();

        auto Passed = Check(this, NOT Data.Get_IsOwningEntityDriverDependentOnThis(),
            TEXT("server: after the transfer C is still marked a construction dependent of its old owner A"));

        Passed = Check(this, Data.Get_OwningEntityDriver().Get() == DriverB,
            ck::Format_UE(TEXT("server: after the transfer C should publish B's driver as its owner; it publishes {}"),
                Describe_Driver(State->Server, Data.Get_OwningEntityDriver().Get()))) && Passed;

        Passed = Check(this, CountA == State->OwnerAExpectedDependentsBefore - 1,
            ck::Format_UE(TEXT("server: A's expected dependent driver count should drop by one ({} -> {}); it is {}"),
                State->OwnerAExpectedDependentsBefore, State->OwnerAExpectedDependentsBefore - 1, CountA)) && Passed;

        Passed = Check(this, CountB == State->OwnerBExpectedDependentsBefore,
            ck::Format_UE(TEXT("server: B's expected dependent driver count must not change ({}); it is {}"),
                State->OwnerBExpectedDependentsBefore, CountB)) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict,
        TEXT("transfer republishes the owner and releases the construction dependency on the server")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_DestroyHostActor_LeavesNoReplicatedMemberOnClient,
    "Ck.EntityLifetime.Net.DestroyHostActor_LeavesNoReplicatedMemberOnClient",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_DestroyHostActor_LeavesNoReplicatedMemberOnClient::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    const auto CountBefore = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        State->ClientMembersBefore = Get_AliveMembersWithDriver(ck::auto_test::net::Get_ClientWorld(0)).Num();

        return Check(this, State->ClientMembersBefore == NumReplicatedMembers,
            ck::Format_UE(TEXT("client: expected {} live replicated members before the host actor is destroyed, counted {}"),
                NumReplicatedMembers, State->ClientMembersBefore));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, CountBefore, TEXT("member count before the host actor is destroyed")));

    const auto DestroyHost = FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
    {
        for (auto It = TActorIterator<ACk_AutoTest_NetSubject_LifetimeTransfer_UE>(InServer); It; ++It)
        {
            It->Destroy();
            return;
        }

        AddError(TEXT("server: no lifetime-transfer subject actor to destroy"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(DestroyHost));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsReclaimed(State->Client.Root)
            && Get_AliveMembersWithDriver(ck::auto_test::net::Get_ClientWorld(0)).IsEmpty();
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: the bridged root and every replicated member are destroyed after the server destroys the host actor")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto& Client = State->Client;
        const auto Survivors = Get_AliveMembersWithDriver(ck::auto_test::net::Get_ClientWorld(0));

        auto SurvivorDescriptions = TArray<FString>{};
        for (const auto& Survivor : Survivors)
        {
            SurvivorDescriptions.Emplace(ck::Format_UE(TEXT("{} ({})"),
                Describe_Entity(Client, Survivor), Describe_State(Client, Survivor)));
        }

        auto Passed = Check(this, Get_IsReclaimed(Client.Root),
            ck::Format_UE(TEXT("client: the bridged root should be destroyed with its host actor; Root is {}"),
                Describe_State(Client, Client.Root)));

        Passed = Check(this, Survivors.IsEmpty(),
            ck::Format_UE(TEXT("client: live replicated members went from {} to {}, expected 0; survivors: {}"),
                State->ClientMembersBefore, Survivors.Num(), FString::Join(SurvivorDescriptions, TEXT(", ")))) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict, TEXT("host actor destroy leaves no replicated member behind on the client")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_Transfer_DoesNotReconstructChildOnClient,
    "Ck.EntityLifetime.Net.Transfer_DoesNotReconstructChildOnClient",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_Transfer_DoesNotReconstructChildOnClient::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, true);

    const auto Capture = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        State->ClientChildBeforeTransfer = State->Client.Child;
        const auto Count = Get_ConstructCount(State->ClientChildBeforeTransfer);

        return Check(this, Count == 1,
            ck::Format_UE(TEXT("client precondition: C's Construct should have run once before the transfer; counted {}"), Count));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Capture, TEXT("capture the client's C before the transfer")));

    const auto Transfer = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.Child, State->Server.OwnerB, TEXT("C from A to B"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(Transfer));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        return Get_IsAliveUnder(State->ClientChildBeforeTransfer, State->Client.OwnerB);
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("client: the C captured before the transfer is alive under B")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto& Client = State->Client;
        const auto& Before = State->ClientChildBeforeTransfer;

        auto NumAliveChildren = 0;
        const auto Current = Find_Alive<FTag_LifetimeTransfer_Child>(ck::auto_test::net::Get_ClientWorld(0), NumAliveChildren);

        auto Passed = Check(this, Get_IsAliveUnder(Before, Client.OwnerB),
            ck::Format_UE(TEXT("client: C should be alive under B after the transfer; the C captured before it is {}"),
                Describe_State(Client, Before)));

        Passed = Check(this, NumAliveChildren == 1 && Current == Before,
            ck::Format_UE(TEXT("client: C should be the same entity as before the transfer; before [{}], now [{}] ({} live C)"),
                Before, Current, NumAliveChildren)) && Passed;

        const auto Count = Get_ConstructCount(Before);
        Passed = Check(this, Count == 1,
            ck::Format_UE(TEXT("client: C's Construct should have run exactly once; counted {}"), Count)) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict, TEXT("a transfer re-parents C on the client without reconstructing it")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkEntityLifetimeNet_TransferDefinitionBuiltChild_ReleasesConstructionDependencyOnServer,
    "Ck.EntityLifetime.Net.TransferDefinitionBuiltChild_ReleasesConstructionDependencyOnServer",
    ck_entity_lifetime_net_transfer_spec::TestFlags)

bool FCkEntityLifetimeNet_TransferDefinitionBuiltChild_ReleasesConstructionDependencyOnServer::RunTest(const FString& Parameters)
{
    using namespace ck_entity_lifetime_net_transfer_spec;

    const auto State = MakeShared<FState>();
    Add_StartAndBuild(this, State, false);

    // D (generic replication data) was built inside A's construction and A has published since: the case where the
    // old owner counted D and must release it.
    const auto Precondition = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto* DriverA = Get_Driver(State->Server.OwnerA);
        const auto* DriverB = Get_Driver(State->Server.OwnerB);
        const auto* DriverD = Get_Driver(State->Server.DefinitionChild);

        if (NOT Check(this, DriverA != nullptr && DriverB != nullptr && DriverD != nullptr,
            TEXT("server: A, B and D must all have a replication driver")))
        { return false; }

        const auto& Data = DriverD->Get_ReplicationData();

        auto Passed = Check(this, NOT Data.Get_ConstructionInfos().IsEmpty(),
            TEXT("server precondition: D has no generic replication data, so it was not built through Request_BuildAndReplicate"));

        Passed = Check(this, Data.Get_IsOwningEntityDriverDependentOnThis(),
            TEXT("server precondition: D is not marked a construction dependent of A, so this row would pass for the wrong reason")) && Passed;

        Passed = Check(this, Data.Get_OwningEntityDriver().Get() == DriverA,
            ck::Format_UE(TEXT("server precondition: D should publish A's driver as its owner; it publishes {}"),
                Describe_Driver(State->Server, Data.Get_OwningEntityDriver().Get()))) && Passed;

        Passed = Check(this, Get_IsPublished(State->Server.OwnerA),
            TEXT("server precondition: A has not published its own replication data, so it never counted D")) && Passed;

        State->OwnerAExpectedDependentsBefore = DriverA->Get_ExpectedNumberOfDependentReplicationDrivers();
        State->OwnerBExpectedDependentsBefore = DriverB->Get_ExpectedNumberOfDependentReplicationDrivers();

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Precondition, TEXT("D starts as A's construction dependent")));

    const auto Transfer = FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*) -> void
    {
        Request_Transfer(this, State->Server.DefinitionChild, State->Server.OwnerB, TEXT("D from A to B"));
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(Transfer));

    const auto Converged = FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
    {
        const auto* DriverA = Get_Driver(State->Server.OwnerA);
        const auto* DriverB = Get_Driver(State->Server.OwnerB);
        const auto* DriverD = Get_Driver(State->Server.DefinitionChild);

        if (DriverA == nullptr || DriverB == nullptr || DriverD == nullptr)
        { return false; }

        const auto& Data = DriverD->Get_ReplicationData();
        return NOT Data.Get_IsOwningEntityDriverDependentOnThis()
            && Data.Get_OwningEntityDriver().Get() == DriverB
            && DriverA->Get_ExpectedNumberOfDependentReplicationDrivers() == State->OwnerAExpectedDependentsBefore - 1;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, Converged, ConvergeTimeoutSeconds,
        TEXT("server: D publishes B, is no longer A's construction dependent, and A expects one dependent fewer")));

    const auto Verdict = FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
    {
        const auto* DriverA = Get_Driver(State->Server.OwnerA);
        const auto* DriverB = Get_Driver(State->Server.OwnerB);
        const auto* DriverD = Get_Driver(State->Server.DefinitionChild);

        if (NOT Check(this, DriverA != nullptr && DriverB != nullptr && DriverD != nullptr,
            TEXT("server: A, B and D must still have a replication driver after the transfer")))
        { return false; }

        const auto& Data = DriverD->Get_ReplicationData();
        const auto CountA = DriverA->Get_ExpectedNumberOfDependentReplicationDrivers();
        const auto CountB = DriverB->Get_ExpectedNumberOfDependentReplicationDrivers();

        auto Passed = Check(this, NOT Data.Get_IsOwningEntityDriverDependentOnThis(),
            TEXT("server: after the transfer D is still marked a construction dependent of its old owner A"));

        Passed = Check(this, Data.Get_OwningEntityDriver().Get() == DriverB,
            ck::Format_UE(TEXT("server: after the transfer D should publish B's driver as its owner; it publishes {}"),
                Describe_Driver(State->Server, Data.Get_OwningEntityDriver().Get()))) && Passed;

        Passed = Check(this, CountA == State->OwnerAExpectedDependentsBefore - 1,
            ck::Format_UE(TEXT("server: A's expected dependent driver count should drop by exactly one ({} -> {}); it is {}"),
                State->OwnerAExpectedDependentsBefore, State->OwnerAExpectedDependentsBefore - 1, CountA)) && Passed;

        Passed = Check(this, CountB == State->OwnerBExpectedDependentsBefore,
            ck::Format_UE(TEXT("server: B's expected dependent driver count must not change ({}); it is {}"),
                State->OwnerBExpectedDependentsBefore, CountB)) && Passed;

        return Passed;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, Verdict,
        TEXT("transferring a definition-built child releases the old owner's count on the server")));

    Add_End(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
