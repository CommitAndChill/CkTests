#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <GameFramework/GameModeBase.h>

class APawn;
class APlayerState;

#include "CkTests/Net/CkAutoTest_NetSubject_Utils.h"

// --------------------------------------------------------------------------------------------------------------------
//
// Multi-client AutoTest harness for CkStateMachine replication tests (spec §13). UE 5.5/5.6 ships
// no FNetworkedFunctionalTest equivalent, so this set of latent commands + accessors is the
// project's primitive for "drive PIE with server + N clients and assert across worlds."
//
// Composable as latent commands inside a standard IMPLEMENT_SIMPLE_AUTOMATION_TEST RunTest body:
//
//   ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/CkTests/AutoTests/AutoTests_CkTests_Level")));
//   ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
//   ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
//       [](UWorld* InServer) -> void { /* spawn SM, drive transitions */ })));
//   ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(60));
//   ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda(
//       [this]() -> bool { return /* per-world checks */; }), TEXT("scenario assertions")));
//   ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
//
// The count is TOTAL WORLDS for either topology: 2 = server + one remote client;
// 3 = server + two remote clients. ClientServer adds a dedicated server and requests count - 1
// player clients. ListenServer includes its local player in the server world.
// The optional third argument is the complex-test parameter: ListenServer or ClientServer.
// An empty parameter preserves the legacy listen/single-world callers.
//
// World accessors return nullptr if PIE isn't currently up or the requested world index is out
// of range. Failed setup or prerequisite waits record an error and suppress downstream setup;
// scenario assertions also stop after failure. Explicit cleanup assertions and observations
// after EndPIE remain runnable. EndPIE restores editor settings.
//
// --------------------------------------------------------------------------------------------------------------------

DECLARE_DELEGATE_OneParam(FCk_NetAutoTest_ServerAction, UWorld*);
DECLARE_DELEGATE_RetVal(bool, FCk_NetAutoTest_Assertion);

// Side-effect-free predicate polled once per frame by FCk_Latent_WaitUntil. Distinct from
// FCk_NetAutoTest_Assertion (same signature) to document intent: a Condition must NOT call
// TestEqual/AddError — it returns true only once the awaited state is reached, false to keep waiting.
DECLARE_DELEGATE_RetVal(bool, FCk_NetAutoTest_Condition);

namespace ck::auto_test::net
{
    // Explicit cases keep topology visible in discovery, filtering, and the result report.
    CKTESTS_API auto Get_TopologyTestCases(
        TArray<FString>& OutNames, TArray<FString>& OutCommands) -> void;

    // Pure validation: failure leaves OutDedicatedServer unchanged.
    CKTESTS_API auto TryGet_DedicatedServer(
        const FString& InTopology, bool& OutDedicatedServer) -> bool;

    // Custom latent commands must not run a test body after a rejected startup.
    CKTESTS_API auto Get_HasSessionFailed() -> bool;

    // Custom commands use the same abort state as the standard harness commands.
    // Cleanup remains runnable after this reports a rejected prerequisite.
    CKTESTS_API auto FailSession(const FString& InMessage) -> void;

    // Returns the PIE world running as the server (ListenServer or DedicatedServer). nullptr if
    // no PIE session is active or no server world exists.
    CKTESTS_API auto Get_ServerWorld() -> UWorld*;

    // Returns the Nth client world. ClientIdx 0 is the first non-server PIE client. nullptr if
    // out of range or no PIE active.
    CKTESTS_API auto Get_ClientWorld(int32 ClientIdx) -> UWorld*;

    // All PIE worlds (server + clients). Order: server first, then clients in PIEInstance order.
    CKTESTS_API auto Get_AllPIEWorlds() -> TArray<UWorld*>;

    // Count of client worlds (excludes server). Returns 0 if no PIE active.
    CKTESTS_API auto Get_NumClientWorlds() -> int32;

    // Quiet readiness lookup. Returns nullptr until the requested remote player's controller
    // can be correlated uniquely; safe to poll without logging transient connection state.
    CKTESTS_API auto TryGet_RemoteClientPlayerController(
        UWorld* InServerWorld, int32 ClientIdx) -> class APlayerController*;

    // Server-side PlayerController representing the Nth remote PIE client (the one whose world is
    // Get_ClientWorld(ClientIdx)). On a listen server the local player's PC has no UNetConnection;
    // remote clients' PCs do. Used to possess a subject Pawn with a client's PC so
    // OwningClientAuthoritative replication contracts can be exercised (the SM's owning-client authority
    // resolves through the bridged actor's IsLocallyControlled-by-player check). Returns nullptr if PIE
    // isn't up or there's no such remote client.
    //
    // For a single remote client (every 2-world net test) this returns the sole net-connection PC. For
    // >1 remote client the server's PlayerControllerIterator order does NOT match Get_ClientWorld's
    // PIEInstance order, so the result is CORRELATED by player identity (PlayerState UniqueId, PlayerId
    // fallback) to the SAME client as Get_ClientWorld(ClientIdx); returns nullptr (loud) if that identity
    // has not replicated yet, rather than silently binding the wrong client.
    CKTESTS_API auto Get_RemoteClientPlayerController(UWorld* InServerWorld, int32 ClientIdx) -> class APlayerController*;
}

// --------------------------------------------------------------------------------------------------------------------
// A standard complex test with one independently reported case per network topology.
#define CK_IMPLEMENT_NET_AUTOMATION_TEST(TestClass, PrettyName, Flags) \
    IMPLEMENT_COMPLEX_AUTOMATION_TEST(TestClass, PrettyName, Flags) \
    void TestClass::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const \
    { ck::auto_test::net::Get_TopologyTestCases(OutNames, OutCommands); }

// Latent commands. Defined manually rather than via DEFINE_LATENT_AUTOMATION_COMMAND_* macros so
// commands that need progress state (frame counter, elapsed time) can carry mutable fields. The
// public-facing usage is identical: construct with parameters, ADD_LATENT_AUTOMATION_COMMAND.

class CKTESTS_API FCk_Latent_StartPIEMultiClient : public IAutomationLatentCommand
{
public:
    FCk_Latent_StartPIEMultiClient(
        int32 InNumClients, const FString& InMapPath, const FString& InTopology = FString{},
        TSubclassOf<AGameModeBase> InGameModeClass = nullptr)
        : _NumClients(InNumClients), _MapPath(InMapPath), _Topology(InTopology),
          _GameModeClass(InGameModeClass.Get()) {}

    virtual ~FCk_Latent_StartPIEMultiClient() = default;
    virtual bool Update() override;

private:
    int32 _NumClients = 1;
    FString _MapPath;
    FString _Topology;
    TStrongObjectPtr<UClass> _GameModeClass;
};

// Adds exactly one real PIE client to a healthy, exact, harness-owned topology. This command
// remains pending until the added player's normal possession and server correlation are ready.
// On failure downstream bodies are suppressed; callers must always enqueue EndPIE.
class CKTESTS_API FCk_Latent_RequestLateJoin : public IAutomationLatentCommand
{
public:
    explicit FCk_Latent_RequestLateJoin(double InTimeoutSeconds = 60.0)
        : _TimeoutSeconds(InTimeoutSeconds) {}
    virtual bool Update() override;

private:
    struct FPlayerIdentity
    {
        TWeakObjectPtr<APlayerController> Controller;
        TWeakObjectPtr<APawn> Pawn;
        TWeakObjectPtr<APlayerState> PlayerState;
    };
    struct FWorldIdentity
    {
        TWeakObjectPtr<UWorld> World;
        int32 PIEInstance = INDEX_NONE;
        ENetMode Mode = NM_Standalone;
        TArray<FPlayerIdentity> Players;
    };
    TArray<FWorldIdentity> _Worlds;
    TArray<TWeakObjectPtr<APlayerController>> _RemoteControllers;
    TWeakObjectPtr<UObject> _Editor;
    TWeakObjectPtr<AGameModeBase> _GameMode;
    int32 _OldCount = 0, _LastPIEInstance = INDEX_NONE;
    double _TimeoutSeconds = 60.0, _StartTime = -1.0;
    bool _Requested = false;
};

class CKTESTS_API FCk_Latent_WaitForPIEReady : public IAutomationLatentCommand
{
public:
    FCk_Latent_WaitForPIEReady(int32 InExpectedTotalWorlds, float InTimeoutSeconds)
        : _ExpectedTotalWorlds(InExpectedTotalWorlds), _TimeoutSeconds(InTimeoutSeconds) {}

    virtual ~FCk_Latent_WaitForPIEReady() = default;
    virtual bool Update() override;

private:
    int32 _ExpectedTotalWorlds = 1;
    float _TimeoutSeconds = 30.0f;
    double _StartTime = -1.0;
};

// Polls a bool predicate until it succeeds. An unbound predicate or timeout fails the session,
// suppressing downstream setup while preserving assertions and EndPIE cleanup. Reuses the assertion
// delegate signature; the predicate itself must have no side effects. Use for waits such as
// "the client reconnected and the replicated actor exists".
class CKTESTS_API FCk_Latent_WaitForCondition : public IAutomationLatentCommand
{
public:
    FCk_Latent_WaitForCondition(const FCk_NetAutoTest_Assertion& InPredicate, float InTimeoutSeconds)
        : _Predicate(InPredicate), _TimeoutSeconds(InTimeoutSeconds) {}

    virtual ~FCk_Latent_WaitForCondition() = default;
    virtual bool Update() override;

private:
    FCk_NetAutoTest_Assertion _Predicate;
    float _TimeoutSeconds = 30.0f;
    double _StartTime = -1.0;
};

class CKTESTS_API FCk_Latent_TickWorlds : public IAutomationLatentCommand
{
public:
    explicit FCk_Latent_TickWorlds(int32 InNumFrames)
        : _NumFrames(InNumFrames) {}

    virtual ~FCk_Latent_TickWorlds() = default;
    virtual bool Update() override;

private:
    int32 _NumFrames = 0;
    int32 _FramesElapsed = 0;
};

class CKTESTS_API FCk_Latent_RunOnServer : public IAutomationLatentCommand
{
public:
    explicit FCk_Latent_RunOnServer(const FCk_NetAutoTest_ServerAction& InAction)
        : _Action(InAction) {}

    virtual ~FCk_Latent_RunOnServer() = default;
    virtual bool Update() override;

private:
    FCk_NetAutoTest_ServerAction _Action;
};

// Same shape as FCk_Latent_RunOnServer but targets the Nth client world. ClientIdx 0 is the
// first non-server PIE client (matches ck::auto_test::net::Get_ClientWorld). Reuses the
// ServerAction delegate type because the lambda payload (UWorld*) is identical — the delegate
// name is a historical artefact, not a contract.
class CKTESTS_API FCk_Latent_RunOnClient : public IAutomationLatentCommand
{
public:
    FCk_Latent_RunOnClient(int32 InClientIdx, const FCk_NetAutoTest_ServerAction& InAction)
        : _ClientIdx(InClientIdx), _Action(InAction) {}

    virtual ~FCk_Latent_RunOnClient() = default;
    virtual bool Update() override;

private:
    int32 _ClientIdx = 0;
    FCk_NetAutoTest_ServerAction _Action;
};

class CKTESTS_API FCk_Latent_AssertCondition : public IAutomationLatentCommand
{
public:
    FCk_Latent_AssertCondition(
        FAutomationTestBase* InTest,
        const FCk_NetAutoTest_Assertion& InAssertion,
        const FString& InMessage,
        bool InRunAfterSessionFailure = false)
        : _Test(InTest), _Assertion(InAssertion), _Message(InMessage),
          _RunAfterSessionFailure(InRunAfterSessionFailure) {}

    virtual ~FCk_Latent_AssertCondition() = default;
    virtual bool Update() override;

private:
    FAutomationTestBase* _Test = nullptr;
    FCk_NetAutoTest_Assertion _Assertion;
    FString _Message;
    bool _RunAfterSessionFailure = false;
};

// Poll-until-converged latent command. Calls the supplied Condition once per frame; advances
// (returns true) as soon as it returns true. If TimeoutSeconds (wall-clock) elapses first, it
// fails the session and advances to cleanup. Replaces a fixed FCk_Latent_TickWorlds(N)
// "settle" wait when the real intent is "wait until replication has converged" — removes the
// frame-budget race. Pair with a final FCk_Latent_AssertCondition that records the actual TestEqual
// checks (which then pass immediately, since we only proceed once converged).
class CKTESTS_API FCk_Latent_WaitUntil : public IAutomationLatentCommand
{
public:
    FCk_Latent_WaitUntil(
        FAutomationTestBase* InTest,
        const FCk_NetAutoTest_Condition& InCondition,
        double InTimeoutSeconds,
        const FString& InMessage)
        : _Test(InTest), _Condition(InCondition), _TimeoutSeconds(InTimeoutSeconds), _Message(InMessage) {}

    virtual ~FCk_Latent_WaitUntil() = default;
    virtual bool Update() override;

private:
    FAutomationTestBase* _Test = nullptr;
    FCk_NetAutoTest_Condition _Condition;
    double _TimeoutSeconds = 5.0;
    FString _Message;
    double _StartTime = -1.0;
};

class CKTESTS_API FCk_Latent_EndPIE : public IAutomationLatentCommand
{
public:
    FCk_Latent_EndPIE() = default;
    virtual ~FCk_Latent_EndPIE() = default;
    virtual bool Update() override;

private:
    bool _Requested = false;
};

// Single combined command that owns the lifecycle of an AS-authored multi-client net test:
// resolves the AS class by `/Script/Angelscript.<Name>` path, spawns one instance on each PIE
// world's TransientEntity, captures the constructed handles via Promise_OnConstructed, then
// polls each world's per-body FCk_AutoTest_Result fragment until all terminal or until
// `InTimeoutSeconds` elapses. Per-world failures + timeouts are reported via `AddError` on the
// supplied automation test so they appear in the Session Frontend output.
//
// The AS class must be a subclass of `UCk_AutoTest_Base` (or `UCk_AutoTest_NetBase`), so its
// DoBeginPlay writes a Pass/Fail to the result fragment by calling `FinishSuccess()` /
// `FinishFailure()`. The command is spawn-only — multi-PIE startup and any replicated
// subject-actor spawn/settle must be issued first. Independent stubs pass the optional
// no-subject requirement; the command checks the actual CDO mode and both PIE worlds.
class CKTESTS_API FCk_Latent_RunAsTestOnAllWorlds : public IAutomationLatentCommand
{
public:
    FCk_Latent_RunAsTestOnAllWorlds(
        FAutomationTestBase* InTest,
        const FString& InClassPath,
        float InTimeoutSeconds,
        bool InRequireNoNetSubject = false)
        : _Test(InTest), _ClassPath(InClassPath), _TimeoutSeconds(InTimeoutSeconds),
          _RequireNoNetSubject(InRequireNoNetSubject) {}

    virtual ~FCk_Latent_RunAsTestOnAllWorlds() = default;
    virtual bool Update() override;

private:
    FAutomationTestBase* _Test = nullptr;
    FString _ClassPath;
    float _TimeoutSeconds = 30.0f;
    bool _RequireNoNetSubject = false;

    bool _Spawned = false;
    int32 _ExpectedBodyCount = 0;
    double _StartTime = -1.0;

    // Slot i always belongs to Get_AllPIEWorlds()[i], regardless of callback arrival order.
    TArray<TWeakObjectPtr<UWorld>> _Worlds;
    TArray<TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE>> _BodyCapturers;
    TArray<FCk_Handle> _SpawnedBodies;
};

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
