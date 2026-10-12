// A real same-class client pending request with different WithActor parameters must
// keep the original retry Warning, even when the replicated actor script itself is valid.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include <StructUtils/InstancedStruct.h>

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor_Data.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Test_PendingRendezvous_RelaySubject.h"

namespace ck_seen_pending_warning_test
{
    constexpr auto TotalWorlds = 3;
    constexpr auto RemoteClients = 2;
    constexpr auto StartupSeconds = 60.0f;
    constexpr auto SettleSeconds = 20.0;
    constexpr auto ExpirySeconds = 12.0;

    class FWarningCounter final : public FOutputDevice
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
                if (Tokens[Index].IsEmpty() || NOT Line.Contains(Tokens[Index]))
                { continue; }
                if (InVerbosity <= ELogVerbosity::Warning &&
                    Line.Contains(TEXT("PendingReplicationRetry timed out")))
                { ++Warnings[Index]; }
                if (InVerbosity == ELogVerbosity::Display &&
                    Line.Contains(TEXT("unsolicited client WithActor")))
                { ++Displays[Index]; }
            }
        }

        auto Reset() -> void
        {
            FScopeLock Guard{&Mutex};
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                Tokens[Index].Empty();
                Warnings[Index] = 0;
                Displays[Index] = 0;
            }
        }

        auto Arm(int32 InIndex, const FString& InToken) -> void
        {
            FScopeLock Guard{&Mutex};
            Tokens[InIndex] = InToken;
            Warnings[InIndex] = 0;
            Displays[InIndex] = 0;
        }

        auto HasOneWarningPerClient() -> bool
        {
            FScopeLock Guard{&Mutex};
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (Tokens[Index].IsEmpty() || Warnings[Index] != 1 || Displays[Index] != 0)
                { return false; }
            }
            return true;
        }

    private:
        FCriticalSection Mutex;
        FString Tokens[RemoteClients];
        int32 Warnings[RemoteClients]{};
        int32 Displays[RemoteClients]{};
    };

    static FWarningCounter GWarnings;

    struct FRun
    {
        TWeakObjectPtr<ACk_ActorRelay_TestProbe_UE> TargetActors[RemoteClients];
        FCk_Handle TargetEntities[RemoteClients];
        FCk_Handle PendingEntities[RemoteClients];
        double StickyObservedAt = -1.0;
    };

    auto TryGetRelay(UWorld* InWorld) -> ACk_ActorRelay_TestProbe_UE*
    {
        if (NOT ck::IsValid(InWorld))
        { return nullptr; }
        for (TActorIterator<ACk_Test_PendingRendezvous_RelaySubject_UE> It(InWorld); It; ++It)
        {
            auto* Actor = *It;
            if (ck::IsValid(Actor) && NOT Actor->IsActorBeingDestroyed())
            { return Actor; }
        }
        return nullptr;
    }

    auto TryGetTarget(UWorld* InWorld) -> FCk_Handle
    {
        auto* Actor = TryGetRelay(InWorld);
        return ck::IsValid(Actor)
            ? UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) : FCk_Handle{};
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkEntityScriptNet_SeenPendingWarning,
    "Ck.EntityScript.Net.PendingRendezvous.SeenClassWarning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScriptNet_SeenPendingWarning::RunTest(const FString& Parameters)
{
    using namespace ck_seen_pending_warning_test;
    bSuppressLogErrors = false;
    bSuppressLogWarnings = false;
    GLog->RemoveOutputDevice(&GWarnings);
    GWarnings.Reset();
    GLog->AddOutputDevice(&GWarnings);
    auto Run = MakeShared<FRun>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        TotalWorlds, FString{TEXT("/Engine/Maps/Entry")}, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(TotalWorlds, StartupSeconds));
    // A different replicated subject actor supplies a real client actor parameter. Its
    // script class is a subclass, so it cannot consume the base WithActor class bucket.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
        {
            if (NOT ck::IsValid(InServer))
            { ck::auto_test::net::FailSession(TEXT("same-class warning has no server world")); return; }
            auto Spawn = FActorSpawnParameters{};
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (NOT ck::IsValid(InServer->SpawnActor<ACk_AutoTest_NetSubject>(
                ACk_AutoTest_NetSubject::StaticClass(), FTransform::Identity, Spawn)))
            { ck::auto_test::net::FailSession(TEXT("alternate subject spawn failed")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            if (ck::auto_test::net::Get_NumClientWorlds() != RemoteClients)
            { return false; }
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto* Alternate = ACk_AutoTest_NetSubject::Find(ck::auto_test::net::Get_ClientWorld(Index));
                if (NOT ck::IsValid(Alternate) || Alternate->IsActorBeingDestroyed() ||
                    NOT ck::IsValid(UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Alternate)))
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("different actor parameter replicated and bridged on both clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* InServer) -> void
        {
            if (NOT ck::IsValid(InServer))
            { ck::auto_test::net::FailSession(TEXT("target relay has no server world")); return; }
            auto Spawn = FActorSpawnParameters{};
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* TargetActor = InServer->SpawnActor<ACk_Test_PendingRendezvous_RelaySubject_UE>(
                ACk_Test_PendingRendezvous_RelaySubject_UE::StaticClass(), FTransform::Identity, Spawn);
            if (NOT ck::IsValid(TargetActor))
            { ck::auto_test::net::FailSession(TEXT("target relay spawn failed")); return; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto Entity = TryGetTarget(ck::auto_test::net::Get_ClientWorld(Index));
                if (NOT ck::IsValid(Entity) ||
                    NOT Entity.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                    NOT Entity.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>())
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("two real client relay scripts entered retry before pending request")));

    for (auto Index = 0; Index < RemoteClients; ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(Index,
            FCk_NetAutoTest_ServerAction::CreateLambda([Run, Index](UWorld* InClient) -> void
            {
                auto* Alternate = ACk_AutoTest_NetSubject::Find(InClient);
                auto* TargetActor = TryGetRelay(InClient);
                auto Target = TryGetTarget(InClient);
                if (NOT ck::IsValid(Alternate) || Alternate->IsActorBeingDestroyed() ||
                    NOT ck::IsValid(TargetActor) || NOT ck::IsValid(Target) ||
                    NOT Target.Has<ck::FTag_EntityScript_PendingReplicationRetry>())
                { ck::auto_test::net::FailSession(TEXT("client actor pair changed before pending request")); return; }
                auto Root = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InClient);
                if (NOT ck::IsValid(Root) ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Target) != Root)
                { ck::auto_test::net::FailSession(TEXT("target has wrong transient root")); return; }
                auto Params = FInstancedStruct::Make<FCk_EntityScript_WithActor_SpawnParams>(Alternate);
                auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                    Root, UCk_EntityScript_WithActor_UE::StaticClass(), Params, {});
                if (NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))
                { ck::auto_test::net::FailSession(TEXT("same-class client pending request rejected")); return; }
                Run->TargetActors[Index] = TargetActor;
                Run->TargetEntities[Index] = Target;
                Run->PendingEntities[Index] = Pending.Get_EntityUnderConstruction();
                if (NOT ck::IsValid(Run->PendingEntities[Index]) ||
                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Run->PendingEntities[Index]) != Root)
                { ck::auto_test::net::FailSession(TEXT("same-class pending raw identity lost")); }
            })));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto Target = Run->TargetEntities[Index];
                if (NOT ck::IsValid(Target) ||
                    NOT Target.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                    NOT Target.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>() ||
                    NOT Target.Get<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>().Get_SawClassPending() ||
                    NOT ck::IsValid(Run->PendingEntities[Index]))
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("real mismatching class bucket observed by each retry processor")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto* Actor = Run->TargetActors[Index].Get();
                auto Target = Run->TargetEntities[Index];
                if (NOT ck::IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
                    NOT ck::IsValid(Target) ||
                    UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) != Target)
                { return false; }
                const auto Token = FString::Printf(TEXT("SeenClassWarningClient%d"), Index);
                UCk_Utils_Handle_UE::Set_DebugName(Target, FName{*Token});
                if (Target.Get_DebugName() != FName{*Token})
                { return false; }
                GWarnings.Arm(Index, Token);
            }
            Run->StickyObservedAt = FPlatformTime::Seconds();
            return true;
        }), TEXT("sticky observed-class receipts and exact output identities captured")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return Run->StickyObservedAt > 0.0 &&
                FPlatformTime::Seconds() - Run->StickyObservedAt >= ExpirySeconds;
        }), ExpirySeconds + 5.0, TEXT("observe original retry deadline after same-class mismatch")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool
        {
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto* Actor = Run->TargetActors[Index].Get();
                auto Target = Run->TargetEntities[Index];
                if (NOT ck::IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
                    Actor->GetWorld() != ck::auto_test::net::Get_ClientWorld(Index) ||
                    NOT ck::IsValid(Target) ||
                    UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) != Target ||
                    Target.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                    Target.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>() ||
                    NOT ck::IsValid(Run->PendingEntities[Index]))
                { return false; }
            }
            return true;
        }), TEXT("real mismatching pending entries stay retained after Warning expiry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]() -> bool
        {
            const auto OutputIsExact = GWarnings.HasOneWarningPerClient();
            GLog->RemoveOutputDevice(&GWarnings);
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto* World = ck::auto_test::net::Get_ClientWorld(Index);
                if (ck::IsValid(World) && NOT World->bIsTearingDown &&
                    ck::IsValid(Run->PendingEntities[Index]))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->PendingEntities[Index]); }
            }
            return ck::auto_test::net::Get_HasSessionFailed() || OutputIsExact;
        }), TEXT("each exact seen-class client relay warns once; test-owned pending targets retire"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
