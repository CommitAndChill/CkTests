// Retires both callback-owned identities from a real pending rendezvous callback.
// The two rows reach the callback through FinishConstruction and late Retry respectively.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkTests/Net/CkAutoTest_NetSubject_DependentSubtree.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Test_EntityScript_PendingRendezvous_RetireProbe.h"
#include <StructUtils/InstancedStruct.h>

namespace ck_pending_callback_retire_test
{
    constexpr auto TotalWorlds = 3;
    constexpr auto RemoteClients = 2;
    constexpr auto StartupSeconds = 60.0f;
    constexpr auto SettleSeconds = 20.0;

    struct FRun
    {
        FCk_Handle Owners[RemoteClients];
        FCk_Handle Pending[RemoteClients];
        FCk_Handle PreexistingChild[RemoteClients];
        TStrongObjectPtr<UCk_Test_PendingRendezvous_RetireProbe_UE> Receivers[RemoteClients];
    };

    auto TryGetOwner(UWorld* InWorld) -> FCk_Handle
    {
        if (NOT ck::IsValid(InWorld))
        { return {}; }
        auto* Actor = ACk_AutoTest_NetSubject::Find(InWorld);
        if (NOT ck::IsValid(Actor) || Actor->IsActorBeingDestroyed())
        { return {}; }
        return UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
    }

    auto FindRetryChild(const FCk_Handle& InOwner) -> FCk_Handle
    {
        if (NOT ck::IsValid(InOwner))
        { return {}; }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(InOwner))
        {
            if (NOT ck::IsValid(Child) ||
                NOT Child.Has<ck::FFragment_EntityScript>() ||
                NOT Child.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                NOT Child.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>())
            { continue; }
            auto* Script = Child.Get<ck::FFragment_EntityScript>().Get_Script().Get();
            if (ck::IsValid(Script) &&
                Script->GetClass() == UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass())
            { return Child; }
        }
        return {};
    }

    auto HasNoDependentChild(const FCk_Handle& InOwner) -> bool
    {
        if (NOT ck::IsValid(InOwner))
        { return false; }
        if (NOT InOwner.Has<ck::FFragment_LifetimeDependents>())
        { return true; }
        // The public filtered getter omits pending-kill children; inspect the known
        // owner's raw direct list so a still-allocated child cannot pass this oracle.
        for (const auto& Child : InOwner.Get<ck::FFragment_LifetimeDependents>().Get_Entities())
        {
            if (ck::IsValid(Child, ck::IsValid_Policy_IncludePendingKill{}))
            { return false; }
        }
        return true;
    }

    auto Queue(FAutomationTestBase* InTest, const FString& InParameters, bool bLateRetry) -> bool
    {
        auto Run = MakeShared<FRun>();
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
            TotalWorlds, FString{TEXT("/Engine/Maps/Entry")}, InParameters));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(TotalWorlds, StartupSeconds));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
            {
                if (NOT ck::IsValid(InServer))
                { ck::auto_test::net::FailSession(TEXT("callback retirement server world unavailable")); return; }
                auto Spawn = FActorSpawnParameters{};
                Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                if (NOT ck::IsValid(InServer->SpawnActor<ACk_Test_PendingRendezvous_MinimalSubject_UE>(
                    ACk_Test_PendingRendezvous_MinimalSubject_UE::StaticClass(), FTransform::Identity, Spawn)))
                { ck::auto_test::net::FailSession(TEXT("callback retirement subject spawn failed")); }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
            {
                if (ck::auto_test::net::Get_NumClientWorlds() != RemoteClients ||
                    NOT ck::IsValid(TryGetOwner(ck::auto_test::net::Get_ServerWorld())))
                { return false; }
                for (auto Index = 0; Index < RemoteClients; ++Index)
                {
                    if (NOT ck::IsValid(TryGetOwner(ck::auto_test::net::Get_ClientWorld(Index))))
                    { return false; }
                }
                return true;
            }), SettleSeconds, TEXT("real owner linked in all callback retirement worlds")));

        if (bLateRetry)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
                {
                    auto Owner = TryGetOwner(InServer);
                    if (NOT ck::IsValid(Owner) ||
                        NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(
                            UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner,
                                UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass(),
                                FInstancedStruct{}, {})))
                    { ck::auto_test::net::FailSession(TEXT("late callback authority child request failed")); }
                })));
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
                FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
                {
                    for (auto Index = 0; Index < RemoteClients; ++Index)
                    {
                        if (NOT ck::IsValid(FindRetryChild(
                            TryGetOwner(ck::auto_test::net::Get_ClientWorld(Index)))))
                        { return false; }
                    }
                    return true;
                }), SettleSeconds, TEXT("late callback children hold actual retry receipts")));
        }

        for (auto Index = 0; Index < RemoteClients; ++Index)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
                FCk_NetAutoTest_ServerAction::CreateLambda([Run, Index, bLateRetry](UWorld* InClient) -> void
                {
                    auto Owner = TryGetOwner(InClient);
                    if (NOT ck::IsValid(Owner))
                    { ck::auto_test::net::FailSession(TEXT("callback retirement client owner unavailable")); return; }
                    Run->Owners[Index] = Owner;
                    if (bLateRetry)
                    {
                        Run->PreexistingChild[Index] = FindRetryChild(Owner);
                        if (NOT ck::IsValid(Run->PreexistingChild[Index]))
                        { ck::auto_test::net::FailSession(TEXT("late retry identity changed")); return; }
                    }
                    auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner,
                        UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass(),
                        FInstancedStruct{}, {});
                    if (NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))
                    { ck::auto_test::net::FailSession(TEXT("real client pending request rejected")); return; }
                    Run->Pending[Index] = Pending.Get_EntityUnderConstruction();
                    Run->Receivers[Index] = TStrongObjectPtr<UCk_Test_PendingRendezvous_RetireProbe_UE>{
                        NewObject<UCk_Test_PendingRendezvous_RetireProbe_UE>()};
                    if (NOT ck::IsValid(Run->Pending[Index]) || NOT Run->Receivers[Index].IsValid())
                    { ck::auto_test::net::FailSession(TEXT("callback retirement pending or receiver unavailable")); return; }
                    auto* Receiver = Run->Receivers[Index].Get();
                    Receiver->ExpectedOwner = Owner;
                    Receiver->Pending = Run->Pending[Index];
                    Receiver->ExpectedConstructed = Run->PreexistingChild[Index];
                    auto Callback = FCk_Delegate_EntityScript_Constructed{};
                    Callback.BindDynamic(Receiver, &UCk_Test_PendingRendezvous_RetireProbe_UE::OnConstructed);
                    UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, Callback);
                    if (bLateRetry && (Receiver->CallbackCount != 0 ||
                        NOT Run->PreexistingChild[Index].Has<ck::FTag_EntityScript_PendingReplicationRetry>()))
                    { ck::auto_test::net::FailSession(TEXT("late callback fired before retry processor")); }
                })));
        }

        if (NOT bLateRetry)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
                {
                    auto Owner = TryGetOwner(InServer);
                    if (NOT ck::IsValid(Owner) ||
                        NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(
                            UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner,
                                UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass(),
                                FInstancedStruct{}, {})))
                    { ck::auto_test::net::FailSession(TEXT("immediate callback authority child request failed")); }
                })));
        }

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
            {
                for (auto Index = 0; Index < RemoteClients; ++Index)
                {
                    if (NOT Run->Receivers[Index].IsValid() ||
                        Run->Receivers[Index]->CallbackCount != 1 ||
                        NOT Run->Receivers[Index]->bExactAtCallback ||
                        NOT Run->Receivers[Index]->bRequestedBoth)
                    { return false; }
                }
                return true;
            }), SettleSeconds, TEXT("both real callbacks retire exact pending and constructed targets")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
            {
                for (auto Index = 0; Index < RemoteClients; ++Index)
                {
                    if (NOT Run->Receivers[Index].IsValid() ||
                        Run->Receivers[Index]->CallbackCount != 1 ||
                        ck::IsValid(Run->Pending[Index], ck::IsValid_Policy_IncludePendingKill{}) ||
                        ck::IsValid(Run->Receivers[Index]->Constructed,
                            ck::IsValid_Policy_IncludePendingKill{}) ||
                        NOT HasNoDependentChild(Run->Owners[Index]))
                    { return false; }
                }
                return true;
            }), SettleSeconds, TEXT("both callback-retired allocations and child slots fully drain")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(6));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool
            {
                for (auto Index = 0; Index < RemoteClients; ++Index)
                {
                    if (NOT ck::IsValid(Run->Owners[Index]) ||
                        TryGetOwner(ck::auto_test::net::Get_ClientWorld(Index)) != Run->Owners[Index] ||
                        NOT Run->Receivers[Index].IsValid() ||
                        Run->Receivers[Index]->CallbackCount != 1 ||
                        NOT Run->Receivers[Index]->bExactAtCallback ||
                        NOT Run->Receivers[Index]->bRequestedBoth ||
                        ck::IsValid(Run->Pending[Index], ck::IsValid_Policy_IncludePendingKill{}) ||
                        ck::IsValid(Run->Receivers[Index]->Constructed,
                            ck::IsValid_Policy_IncludePendingKill{}) ||
                        NOT HasNoDependentChild(Run->Owners[Index]))
                    { return false; }
                }
                return true;
            }), TEXT("retired callback targets stay absent without duplicate callback or runnable child")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkEntityScriptNet_CallbackRetireFinish,
    "Ck.EntityScript.Net.PendingRendezvous.FinishCallbackRetires",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptNet_CallbackRetireFinish::RunTest(const FString& Parameters)
{
    bSuppressLogErrors = false;
    bSuppressLogWarnings = false;
    return ck_pending_callback_retire_test::Queue(this, Parameters, false);
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkEntityScriptNet_CallbackRetireRetry,
    "Ck.EntityScript.Net.PendingRendezvous.RetryCallbackRetires",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptNet_CallbackRetireRetry::RunTest(const FString& Parameters)
{
    bSuppressLogErrors = false;
    bSuppressLogWarnings = false;
    return ck_pending_callback_retire_test::Queue(this, Parameters, true);
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
