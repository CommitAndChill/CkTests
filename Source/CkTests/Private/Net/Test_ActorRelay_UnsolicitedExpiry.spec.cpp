// Production relay actors create their WithActor entity on authority; the client obtains it
// from replication without making a local pending-spawn request. The retry may expire, but
// must not elevate that proven absence to a warning. Run in both CS and LS three-world PIE.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDevice.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Handle/CkHandle_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcsExt/EntityScript/CkEntityScript_WithActor.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Test_PendingRendezvous_RelaySubject.h"

namespace ck_actorrelay_unsolicited_expiry
{
    constexpr auto TotalWorlds = 3;
    constexpr auto RemoteClients = 2;
    constexpr auto StartupSeconds = 60.0f;
    constexpr auto SettleSeconds = 20.0;
    constexpr auto ExpiryObservationSeconds = 12.0;

    // Static storage keeps the output device valid even if a latent prerequisite aborts.
    // The final unconditional latent assertion removes it before EndPIE.
    class FRetryLogCounter final : public FOutputDevice
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
                if (InVerbosity <= ELogVerbosity::Warning)
                { ++WarningCounts[Index]; }
                if (InVerbosity == ELogVerbosity::Display &&
                    Line.Contains(TEXT("PendingReplicationRetry expired for unsolicited client WithActor")))
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
            }
        }

        auto Arm(const int32 InIndex, const FString& InToken) -> void
        {
            FScopeLock Guard{&Mutex};
            Tokens[InIndex] = InToken;
            WarningCounts[InIndex] = 0;
            DisplayCounts[InIndex] = 0;
        }

        auto HasExactOutput() -> bool
        {
            FScopeLock Guard{&Mutex};
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (Tokens[Index].IsEmpty() || WarningCounts[Index] != 0 || DisplayCounts[Index] < 1)
                { return false; }
            }
            return true;
        }

    private:
        FCriticalSection Mutex;
        FString Tokens[RemoteClients];
        int32 WarningCounts[RemoteClients]{};
        int32 DisplayCounts[RemoteClients]{};
    };

    static FRetryLogCounter GRetryLogCounter;

    auto
    TryGetTaggedClientRelay(UWorld* InWorld)
    -> ACk_ActorRelay_TestProbe_UE*
    {
        if (NOT ck::IsValid(InWorld) || InWorld->GetNetMode() != NM_Client)
        { return nullptr; }

        for (TActorIterator<ACk_Test_PendingRendezvous_RelaySubject_UE> It(InWorld); It; ++It)
        {
            auto* Actor = *It;
            if (NOT ck::IsValid(Actor) || Actor->IsActorBeingDestroyed())
            { continue; }
            const auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
            if (NOT ck::IsValid(Entity) || NOT Entity.Has<ck::FFragment_EntityScript>())
            { continue; }
            auto* Script = Cast<UCk_EntityScript_WithActor_UE>(
                Entity.Get<ck::FFragment_EntityScript>().Get_Script().Get());
            if (NOT ck::IsValid(Script) ||
                NOT Script->Get_IsUnsolicitedClientReplica(Entity))
            { continue; }
            if (NOT Entity.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                NOT Entity.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>())
            { continue; }
            return Actor;
        }
        return nullptr;
    }

    auto LogRetryPrerequisites(UWorld* InWorld, const int32 InIndex) -> void
    {
        ACk_ActorRelay_TestProbe_UE* Actor = nullptr;
        if (ck::IsValid(InWorld))
        {
            for (TActorIterator<ACk_Test_PendingRendezvous_RelaySubject_UE> It(InWorld); It; ++It)
            {
                auto* Candidate = *It;
                if (ck::IsValid(Candidate) && NOT Candidate->IsActorBeingDestroyed())
                { Actor = Candidate; break; }
            }
        }
        const bool bActor = ck::IsValid(Actor);
        const auto Entity = bActor
            ? UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) : FCk_Handle{};
        const bool bEntity = ck::IsValid(Entity);
        const bool bScriptFragment = bEntity && Entity.Has<ck::FFragment_EntityScript>();
        auto* Script = bScriptFragment
            ? Cast<UCk_EntityScript_WithActor_UE>(
                Entity.Get<ck::FFragment_EntityScript>().Get_Script().Get()) : nullptr;
        const bool bScript = ck::IsValid(Script);
        const bool bScriptAssociation = bScript && Script->Get_AssociatedEntity() == Entity;
        const bool bWorldFragment = bEntity && Entity.Has<TWeakObjectPtr<UWorld>>() &&
            Entity.Get<TWeakObjectPtr<UWorld>>().Get() == InWorld;
        const auto Root = ck::IsValid(InWorld)
            ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld) : FCk_Handle{};
        const bool bLifetimeRoot = bEntity && ck::IsValid(Root) &&
            Entity.Has<ck::FFragment_LifetimeOwner>() &&
            Entity.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Root;
        const bool bOwningActor = bEntity && bActor && Entity.Has<ck::FFragment_OwningActor>() &&
            Entity.Get<ck::FFragment_OwningActor>().Get_EntityOwningActor().Get() == Actor;
        const bool bNetReplicates = bEntity && Entity.Has<ck::FFragment_Net_Params>() &&
            Entity.Get<ck::FFragment_Net_Params>().Get_ConnectionSettings().Get_Replication() ==
                ECk_Replication::Replicates;
        const bool bDriver = bScript && UCk_Utils_EntityReplicationDriver_UE::
            Get_HasExactClientDriverAssociation(Entity, InWorld, Actor, Script->GetClass());
        const bool bProof = bScript && Script->Get_IsUnsolicitedClientReplica(Entity);
        UE_LOG(LogTemp, Display,
            TEXT("PendingRendezvous proof client=%d exactActor=%d authority=%d actorReplicates=%d actorWorld=%d "
                 "entity=%d retry=%d timestamp=%d script=%d association=%d worldFragment=%d "
                 "lifetimeRoot=%d owningActor=%d netReplicates=%d driver=%d proof=%d"),
            InIndex, bActor, bActor && Actor->HasAuthority(), bActor && Actor->GetIsReplicated(),
            bActor && Actor->GetWorld() == InWorld, bEntity,
            bEntity && Entity.Has<ck::FTag_EntityScript_PendingReplicationRetry>(),
            bEntity && Entity.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>(),
            bScript, bScriptAssociation, bWorldFragment, bLifetimeRoot, bOwningActor,
            bNetReplicates, bDriver, bProof);
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkActorRelayNet_UnsolicitedExpiry,
    "Ck.EntityScript.Net.PendingRendezvous.UnsolicitedWithActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkActorRelayNet_UnsolicitedExpiry::RunTest(const FString& Parameters)
{
    using namespace ck_actorrelay_unsolicited_expiry;
    bSuppressLogErrors = false;
    bSuppressLogWarnings = false;
    GLog->RemoveOutputDevice(&GRetryLogCounter);
    GRetryLogCounter.Reset();
    GLog->AddOutputDevice(&GRetryLogCounter);

    auto ReadyAt = MakeShared<double>(-1.0);
    auto SpawnedAt = MakeShared<double>(-1.0);
    auto bRetryDiagnosticEmitted = MakeShared<bool>(false);
    auto CapturedActors = MakeShared<TArray<TWeakObjectPtr<ACk_ActorRelay_TestProbe_UE>>>();
    auto CapturedEntities = MakeShared<TArray<FCk_Handle>>();
    CapturedActors->SetNum(RemoteClients);
    CapturedEntities->SetNum(RemoteClients);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        TotalWorlds, FString{TEXT("/Engine/Maps/Entry")}, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(TotalWorlds, StartupSeconds));
    // Create a fresh authority relay after PIE is ready so its client retry tag can be
    // observed before the 10-second watchdog expires, independent of startup timing.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([SpawnedAt](UWorld* InServer) -> void
        {
            if (NOT ck::IsValid(InServer))
            {
                ck::auto_test::net::FailSession(TEXT("relay expiry probe has no live server world"));
                return;
            }
            auto Spawn = FActorSpawnParameters{};
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* TargetActor = InServer->SpawnActor<ACk_Test_PendingRendezvous_RelaySubject_UE>(
                ACk_Test_PendingRendezvous_RelaySubject_UE::StaticClass(), FTransform::Identity, Spawn);
            if (NOT ck::IsValid(TargetActor))
            { ck::auto_test::net::FailSession(TEXT("fresh authority relay probe spawn failed")); return; }
            *SpawnedAt = FPlatformTime::Seconds();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([SpawnedAt, bRetryDiagnosticEmitted]() -> bool
        {
            if (ck::auto_test::net::Get_NumClientWorlds() != RemoteClients)
            { return false; }
            if (NOT *bRetryDiagnosticEmitted && *SpawnedAt > 0.0 &&
                FPlatformTime::Seconds() - *SpawnedAt >= 18.0)
            {
                *bRetryDiagnosticEmitted = true;
                for (auto Index = 0; Index < RemoteClients; ++Index)
                { LogRetryPrerequisites(ck::auto_test::net::Get_ClientWorld(Index), Index); }
            }
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                if (TryGetTaggedClientRelay(ck::auto_test::net::Get_ClientWorld(Index)) == nullptr)
                { return false; }
            }
            return true;
        }), SettleSeconds, TEXT("all remote clients have a proven relay with a real retry tag")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([ReadyAt, CapturedActors, CapturedEntities]() -> bool
        {
            if (ck::auto_test::net::Get_NumClientWorlds() != RemoteClients)
            { return false; }
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto* Actor = TryGetTaggedClientRelay(ck::auto_test::net::Get_ClientWorld(Index));
                if (Actor == nullptr)
                { return false; }
                (*CapturedActors)[Index] = Actor;
                (*CapturedEntities)[Index] = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
                auto& Entity = (*CapturedEntities)[Index];
                const auto Token = FString::Printf(TEXT("RelayExpiryExactClient%d"), Index);
                UCk_Utils_Handle_UE::Set_DebugName(Entity, FName{*Token});
                if (Entity.Get_DebugName() != FName{*Token})
                { return false; }
                GRetryLogCounter.Arm(Index, Token);
            }
            *ReadyAt = FPlatformTime::Seconds();
            return true;
        }), TEXT("exact client relay retry identities captured before expiry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([ReadyAt]() -> bool
        {
            return *ReadyAt > 0.0 &&
                FPlatformTime::Seconds() - *ReadyAt >= ExpiryObservationSeconds;
        }), ExpiryObservationSeconds + 5.0, TEXT("observe the real pending retry expiry window")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([CapturedActors, CapturedEntities]() -> bool
        {
            if (ck::auto_test::net::Get_NumClientWorlds() != RemoteClients)
            { return false; }
            for (auto Index = 0; Index < RemoteClients; ++Index)
            {
                auto* Actor = (*CapturedActors)[Index].Get();
                const auto Entity = (*CapturedEntities)[Index];
                if (NOT ck::IsValid(Actor) || Actor->IsActorBeingDestroyed() ||
                    Actor->GetWorld() != ck::auto_test::net::Get_ClientWorld(Index) ||
                    NOT ck::IsValid(Entity) ||
                    UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) != Entity ||
                    NOT Entity.Has<ck::FFragment_EntityScript>() ||
                    Entity.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                    Entity.Has<ck::FFragment_EntityScript_PendingReplicationRetryTimestamp>())
                { return false; }
                auto* Script = Cast<UCk_EntityScript_WithActor_UE>(
                    Entity.Get<ck::FFragment_EntityScript>().Get_Script().Get());
                if (NOT ck::IsValid(Script) ||
                    NOT Script->Get_IsUnsolicitedClientReplica(Entity))
                { return false; }
            }
            return true;
        }), TEXT("client relay remains linked after warning-free retry expiry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
        {
            GLog->RemoveOutputDevice(&GRetryLogCounter);
            if (ck::auto_test::net::Get_HasSessionFailed())
            { return true; }
            return GRetryLogCounter.HasExactOutput();
        }), TEXT("each exact client relay has a Display expiry and no Warning"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
