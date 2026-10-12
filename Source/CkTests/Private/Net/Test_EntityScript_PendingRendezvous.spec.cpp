// Exercises real client pending requests against replicated, non-actor-backed child scripts.
// The first child matches a pending request once; the second is intentionally unsolicited
// and must retain the ordinary PendingReplicationRetry Warning after its ten-second window.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "UObject/StrongObjectPtr.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkTests/Net/CkAutoTest_NetSubject_DependentSubtree.h"
#include "CkTests/Net/CkAutoTest_NetSubject_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include <StructUtils/InstancedStruct.h>

namespace ck_pending_rendezvous_test
{
    constexpr auto TotalWorlds = 3;
    constexpr auto RemoteClients = 2;
    constexpr auto StartupSeconds = 60.0f;
    constexpr auto SettleSeconds = 20.0;
    constexpr auto ExpirySeconds = 12.0;

    class FScopedWarningCounter final : public FOutputDevice
    {
    public:
        auto Serialize(const TCHAR* InText, ELogVerbosity::Type InVerbosity, const FName& InCategory) -> void override
        {
            if (InText == nullptr || InCategory != FName{TEXT("CkEcs")})
            { return; }
            const auto Line = FString{InText};
            if (NOT Line.Contains(TEXT("PendingReplicationRetry")))
            { return; }
            FScopeLock Guard{&Mutex};
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (NOT MatchedTokens[Index].IsEmpty() && Line.Contains(MatchedTokens[Index]))
                {
                    if (InVerbosity <= ELogVerbosity::Warning)
                    { ++MatchedWarnings[Index]; }
                    if (InVerbosity == ELogVerbosity::Display)
                    { ++MatchedDisplays[Index]; }
                }
                if (Tokens[Index].IsEmpty() || NOT Line.Contains(Tokens[Index]))
                { continue; }
                if (InVerbosity <= ELogVerbosity::Warning &&
                    Line.Contains(TEXT("PendingReplicationRetry timed out")))
                { ++WarningCounts[Index]; }
                if (InVerbosity == ELogVerbosity::Display &&
                    Line.Contains(TEXT("unsolicited client WithActor")))
                { ++DisplayCounts[Index]; }
            }
        }

        auto Reset() -> void
        {
            FScopeLock Guard{&Mutex};
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                Tokens[Index].Empty();
                WarningCounts[Index] = 0;
                DisplayCounts[Index] = 0;
                MatchedTokens[Index].Empty();
                MatchedWarnings[Index] = 0;
                MatchedDisplays[Index] = 0;
            }
        }

        auto Arm(int32 InIndex, const FString& InToken) -> void
        {
            FScopeLock Guard{&Mutex};
            Tokens[InIndex] = InToken;
            WarningCounts[InIndex] = 0;
            DisplayCounts[InIndex] = 0;
        }

        auto ArmMatched(int32 InIndex, const FString& InToken) -> void
        {
            FScopeLock Guard{&Mutex};
            MatchedTokens[InIndex] = InToken;
            MatchedWarnings[InIndex] = 0;
            MatchedDisplays[InIndex] = 0;
        }

        auto HasOneWarningPerClient() -> bool
        {
            FScopeLock Guard{&Mutex};
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (Tokens[Index].IsEmpty() || WarningCounts[Index] != 1 || DisplayCounts[Index] != 0 ||
                    MatchedTokens[Index].IsEmpty() || MatchedWarnings[Index] != 0 ||
                    MatchedDisplays[Index] != 0)
                { return false; }
            }
            return true;
        }

    private:
        FCriticalSection Mutex;
        FString Tokens[RemoteClients];
        int32 WarningCounts[RemoteClients]{};
        int32 DisplayCounts[RemoteClients]{};
        FString MatchedTokens[RemoteClients];
        int32 MatchedWarnings[RemoteClients]{};
        int32 MatchedDisplays[RemoteClients]{};
    };

    static FScopedWarningCounter GWarnings;

    struct FRun
    {
        FCk_Handle Owners[RemoteClients];
        FCk_Handle Pending[RemoteClients];
        FCk_Handle Stale[RemoteClients];
        FCk_Handle Matched[RemoteClients];
        FCk_Handle Unmatched[RemoteClients];
        TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE> Capturers[RemoteClients];
        double WarningObservationAt = -1.0;
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

    auto FindUnmatchedChild(const FCk_Handle& InOwner, const FCk_Handle& InMatched) -> FCk_Handle
    {
        if (NOT ck::IsValid(InOwner))
        { return {}; }
        const auto Dependents = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(InOwner);
        for (const auto& Child : Dependents)
        {
            if (NOT ck::IsValid(Child) || Child == InMatched ||
                NOT Child.Has<ck::FFragment_EntityScript>() ||
                NOT Child.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                NOT Child.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>())
            { continue; }
            auto* Script = Child.Get<ck::FFragment_EntityScript>().Get_Script().Get();
            if (ck::IsValid(Script) &&
                Script->GetClass() == UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass() &&
                NOT Script->Get_IsUnsolicitedClientReplica(Child))
            { return Child; }
        }
        return {};
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkEntityScriptNet_PendingRendezvous,
    "Ck.EntityScript.Net.PendingRendezvous.GenericAndLate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptNet_PendingRendezvous::RunTest(const FString& Parameters)
{
    using namespace ck_pending_rendezvous_test;
    bSuppressLogErrors = false;
    bSuppressLogWarnings = false;
    GLog->RemoveOutputDevice(&GWarnings);
    GWarnings.Reset();
    GLog->AddOutputDevice(&GWarnings);
    auto Run = MakeShared<FRun>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        TotalWorlds, FString{TEXT("/Engine/Maps/Entry")}, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(TotalWorlds, StartupSeconds));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
        {
            if (NOT ck::IsValid(InServer))
            { ck::auto_test::net::FailSession(TEXT("pending rendezvous has no server world")); return; }
            auto Spawn = FActorSpawnParameters{};
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (NOT ck::IsValid(InServer->SpawnActor<ACk_AutoTest_NetSubject>(
                ACk_AutoTest_NetSubject::StaticClass(), FTransform::Identity, Spawn)))
            { ck::auto_test::net::FailSession(TEXT("pending rendezvous subject spawn failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            if (ck::auto_test::net::Get_NumClientWorlds() != RemoteClients)
            { return false; }
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (NOT ck::IsValid(TryGetOwner(ck::auto_test::net::Get_ClientWorld(Index))))
                { return false; }
            }
            return ck::IsValid(TryGetOwner(ck::auto_test::net::Get_ServerWorld()));
        }), SettleSeconds, TEXT("real subject owner entity linked on all three worlds")));

    for (auto Index = 0; Index < RemoteClients; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([Run, Index](UWorld* InClient) -> void
            {
                auto Owner = TryGetOwner(InClient);
                if (NOT ck::IsValid(Owner))
                { ck::auto_test::net::FailSession(TEXT("client child owner unavailable")); return; }
                Run->Owners[Index] = Owner;
                Run->Stale[Index] = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Owner);
                if (NOT ck::IsValid(Run->Stale[Index]))
                { ck::auto_test::net::FailSession(TEXT("stale-input witness allocation failed")); return; }
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->Stale[Index]);
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (ck::IsValid(Run->Stale[Index], ck::IsValid_Policy_IncludePendingKill{}))
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("test-created stale handles are physically deallocated")));

    // The authority child arrives FIRST. Both clients must observe its actual retry
    // receipt before they allocate a matching pending request.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
        {
            auto Owner = TryGetOwner(InServer);
            if (NOT ck::IsValid(Owner) ||
                NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        Owner, UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass(),
                        FInstancedStruct{}, {})))
            { ck::auto_test::net::FailSession(TEXT("authority first child request failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (NOT ck::IsValid(FindUnmatchedChild(Run->Owners[Index], {})))
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("first real child has client retry tag before pending allocation")));

    for (auto Index = 0; Index < RemoteClients; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([Run, Index](UWorld* InClient) -> void
            {
                auto Owner = TryGetOwner(InClient);
                auto Existing = FindUnmatchedChild(Owner, {});
                if (NOT ck::IsValid(Owner) || Owner != Run->Owners[Index] ||
                    NOT ck::IsValid(Existing))
                { ck::auto_test::net::FailSession(TEXT("existing retry child changed before pending allocation")); return; }
                Run->Matched[Index] = Existing;
                const auto MatchedToken = FString::Printf(TEXT("LateMatchedChildClient%d"), Index);
                UCk_Utils_Handle_UE::Set_DebugName(Existing, FName{*MatchedToken});
                if (Existing.Get_DebugName() != FName{*MatchedToken})
                { ck::auto_test::net::FailSession(TEXT("matched child diagnostic identity unavailable")); return; }
                GWarnings.ArmMatched(Index, MatchedToken);
                auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                    Owner, UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass(),
                    FInstancedStruct{}, {});
                if (NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))
                { ck::auto_test::net::FailSession(TEXT("real late client pending request rejected")); return; }
                Run->Pending[Index] = Pending.Get_EntityUnderConstruction();
                Run->Capturers[Index] = TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE>{
                    NewObject<UCk_AutoTest_BodyCapturer_UE>()};
                if (NOT ck::IsValid(Run->Pending[Index]) || NOT Run->Capturers[Index].IsValid() ||
                    NOT Owner.Has<ck::FFragment_PendingReplication>())
                { ck::auto_test::net::FailSession(TEXT("late pending record or receiver unavailable")); return; }
                auto OnConstructed = FCk_Delegate_EntityScript_Constructed{};
                OnConstructed.BindDynamic(Run->Capturers[Index].Get(),
                    &UCk_AutoTest_BodyCapturer_UE::OnBodyConstructed);
                UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, OnConstructed);
                auto& Record = Owner.Get<ck::FFragment_PendingReplication>();
                auto* Class = UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass();
                auto* CDO = Cast<UCk_EntityScript_UE>(Class->GetDefaultObject(false));
                if (NOT ck::IsValid(CDO))
                { ck::auto_test::net::FailSession(TEXT("dependent-child CDO unavailable")); return; }
                auto Had = true;
                if (ck::IsValid(Record.ConsumeFirst(nullptr, nullptr, {}, &Had)) || Had)
                { ck::auto_test::net::FailSession(TEXT("default class consumed real pending entry")); return; }
                Had = false;
                if (ck::IsValid(Record.ConsumeFirst(Class, nullptr, Owner, &Had)) || NOT Had)
                { ck::auto_test::net::FailSession(TEXT("missing CDO consumed real pending entry")); return; }
                Had = false;
                if (ck::IsValid(Record.ConsumeFirst(Class, CDO, {}, &Had)) || NOT Had)
                { ck::auto_test::net::FailSession(TEXT("default constructed handle consumed pending entry")); return; }
                Had = false;
                if (ck::IsValid(Record.ConsumeFirst(Class, CDO, Run->Stale[Index], &Had)) || NOT Had)
                { ck::auto_test::net::FailSession(TEXT("stale constructed handle consumed pending entry")); return; }
                Had = false;
                if (ck::IsValid(Record.ConsumeFirst(Class, CDO, Owner, &Had)) || NOT Had)
                { ck::auto_test::net::FailSession(TEXT("wrong-script constructed handle consumed pending entry")); return; }
                if (NOT Run->Capturers[Index]->_CapturedBodies.IsEmpty() ||
                    NOT Existing.Has<ck::FTag_EntityScript_PendingReplicationRetry>())
                { ck::auto_test::net::FailSession(TEXT("late pending fired before retry tick")); }
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (NOT Run->Capturers[Index].IsValid() ||
                    Run->Capturers[Index]->_CapturedBodies.Num() != 1 ||
                    NOT ck::IsValid(Run->Capturers[Index]->_CapturedBodies[0]))
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("one real late pending callback per client")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (Run->Capturers[Index]->_CapturedBodies.Num() != 1)
                { return false; }
                const auto Match = Run->Capturers[Index]->_CapturedBodies[0];
                if (NOT ck::IsValid(Match) || Match != Run->Matched[Index] ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Match) != Run->Owners[Index] ||
                    NOT Match.Has<ck::FFragment_EntityScript>() ||
                    Match.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                    Match.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>())
                { return false; }
                auto* Script = Match.Get<ck::FFragment_EntityScript>().Get_Script().Get();
                if (NOT ck::IsValid(Script) ||
                    Script->GetClass() != UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass())
                { return false; }
            }
            return true;
        }), TEXT("preexisting client retry child matches once and clears its retry receipt")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (ck::IsValid(Run->Pending[Index], ck::IsValid_Policy_IncludePendingKill{}))
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("matched real pending targets physically retire after callback")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
        {
            auto Owner = TryGetOwner(InServer);
            if (NOT ck::IsValid(Owner) ||
                NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(
                    UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                        Owner, UCk_AutoTest_NetSubject_DependentChild_EntityScript_UE::StaticClass(),
                        FInstancedStruct{}, {})))
            { ck::auto_test::net::FailSession(TEXT("authority unmatched child request failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (NOT ck::IsValid(FindUnmatchedChild(Run->Owners[Index], Run->Matched[Index])))
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("second real replicated child has no matching pending request")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto Child = FindUnmatchedChild(Run->Owners[Index], Run->Matched[Index]);
                if (NOT ck::IsValid(Child))
                { return false; }
                const auto Token = FString::Printf(TEXT("GenericPendingWarningClient%d"), Index);
                UCk_Utils_Handle_UE::Set_DebugName(Child, FName{*Token});
                if (Child.Get_DebugName() != FName{*Token})
                { return false; }
                Run->Unmatched[Index] = Child;
                GWarnings.Arm(Index, Token);
            }
            Run->WarningObservationAt = FPlatformTime::Seconds();
            return true;
        }), TEXT("exact generic missing-pending retry identities captured")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return Run->WarningObservationAt > 0.0 &&
                FPlatformTime::Seconds() - Run->WarningObservationAt >= ExpirySeconds;
        }), ExpirySeconds + 5.0, TEXT("observe generic missing-pending retry expiry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                const auto Child = Run->Unmatched[Index];
                if (NOT ck::IsValid(Child) ||
                    Child.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                    Child.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>() ||
                    NOT Run->Capturers[Index].IsValid() ||
                    Run->Capturers[Index]->_CapturedBodies.Num() != 1)
                { return false; }
            }
            return true;
        }), TEXT("generic children expired as warnings without duplicate pending callbacks")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
        {
            GLog->RemoveOutputDevice(&GWarnings);
            if (ck::auto_test::net::Get_HasSessionFailed())
            { return true; }
            return GWarnings.HasOneWarningPerClient();
        }), TEXT("unmatched children warn once; matched late children have no retry output"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
