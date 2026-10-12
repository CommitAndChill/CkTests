#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkAutoTest_Sm_Recorder.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Editor/UnrealEdEngine.h"
#include "HAL/PlatformTime.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealEdGlobals.h"

namespace ck_net_smoke
{
    struct FPlaySettingsSnapshot
    {
        int32 Clients = 0;
        EPlayNetMode Mode = EPlayNetMode::PIE_Standalone;
        bool OneProcess = false;
        bool SeparateServer = false;

        FPlaySettingsSnapshot()
        {
            const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
            Settings->GetPlayNumberOfClients(Clients);
            Settings->GetPlayNetMode(Mode);
            Settings->GetRunUnderOneProcess(OneProcess);
            SeparateServer = Settings->bLaunchSeparateServer;
        }

        auto MatchesCurrent() const -> bool
        {
            const auto Current = FPlaySettingsSnapshot{};
            return Clients == Current.Clients && Mode == Current.Mode &&
                OneProcess == Current.OneProcess && SeparateServer == Current.SeparateServer;
        }
    };

    // Exercise the real EndPIE command while the engine still owns its PIE
    // session. Slate closes the game windows during the following editor tick,
    // so the first Update must leave that session state intact.
    class FEndPIEWithWindowStateCheck final : public IAutomationLatentCommand
    {
    public:
        explicit FEndPIEWithWindowStateCheck(FAutomationTestBase* InTest)
            : _Test(InTest) {}

        virtual bool Update() override
        {
            if (GUnrealEd == nullptr || _Test == nullptr)
            { return true; }

            if (NOT _Requested)
            {
                _Requested = true;
                _StartSeconds = FPlatformTime::Seconds();
                _Test->TestTrue(TEXT("PIE session is active before end request"),
                    GUnrealEd->IsPlayingSessionInEditor());
                _Test->TestFalse(TEXT("first EndPIE update queues teardown"), _End.Update());
                _Test->TestTrue(TEXT("end request preserves active session state for window close"),
                    GUnrealEd->IsPlayingSessionInEditor());
                return false;
            }

            if (_End.Update())
            {
                _Test->TestFalse(TEXT("PIE session state clears after teardown"),
                    GUnrealEd->IsPlayingSessionInEditor());
                return true;
            }
            if (NOT _DeadlineReported && FPlatformTime::Seconds() - _StartSeconds > 30.0)
            {
                _Test->AddError(TEXT("PIE window teardown exceeded the smoke-test deadline"));
                _DeadlineReported = true;
            }
            return false;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        FCk_Latent_EndPIE _End;
        bool _Requested = false;
        bool _DeadlineReported = false;
        double _StartSeconds = -1.0;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkStateMachineNet_SmokeTest_PIEStartup,
    "Ck.StateMachine.Net.SmokeTest_PIEStartup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_SmokeTest_PIEStartup::RunTest(const FString& Parameters)
{
    auto DedicatedServer = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, DedicatedServer))
    {
        AddError(TEXT("Smoke test received an unsupported topology"));
        return false;
    }

    bSuppressLogErrors = false;
    bSuppressLogWarnings = false;
    const auto SettingsBefore = ck_net_smoke::FPlaySettingsSnapshot{};
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([DedicatedServer]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            if (Server == nullptr || Client == nullptr)
            { return false; }
            const auto* ClientController = Client->GetFirstPlayerController();
            return ClientController != nullptr && ClientController->IsLocalController() &&
                Server->GetNumPlayerControllers() == (DedicatedServer ? 1 : 2) &&
                ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0) != nullptr;
        }), 30.0, TEXT("the remote client has connected and owns its local controller")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, DedicatedServer]() -> bool
        {
            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            TestEqual(TEXT("exact total world count"), Worlds.Num(), 2);
            TestEqual(TEXT("exact remote client count"), ck::auto_test::net::Get_NumClientWorlds(), 1);
            TestNull(TEXT("negative client index never aliases the server"), ck::auto_test::net::Get_ClientWorld(-1));
            TestNull(TEXT("out-of-range client index is rejected"), ck::auto_test::net::Get_ClientWorld(1));
            if (Server == nullptr || Client == nullptr)
            { return false; }

            TestEqual(TEXT("requested server topology is actually running"), Server->GetNetMode(),
                DedicatedServer ? NM_DedicatedServer : NM_ListenServer);
            TestEqual(TEXT("remote world is a client"), Client->GetNetMode(), NM_Client);
            TestNotNull(TEXT("server recorder exists"), Server->GetSubsystem<UCk_AutoTest_Sm_RecorderSubsystem>());
            TestNotNull(TEXT("client recorder exists"), Client->GetSubsystem<UCk_AutoTest_Sm_RecorderSubsystem>());
            TestNotNull(TEXT("remote client resolves on the server"),
                ck::auto_test::net::Get_RemoteClientPlayerController(Server, 0));
            if (const auto* GameInstance = Server->GetGameInstance())
            {
                TestEqual(TEXT("dedicated server has no local player; listen server has one"),
                    GameInstance->GetNumLocalPlayers(), DedicatedServer ? 0 : 1);
            }
            else
            { AddError(TEXT("server has no GameInstance")); }
            return true;
        }), TEXT("network roles, world accessors, and local-player ownership")));

    ADD_LATENT_AUTOMATION_COMMAND(ck_net_smoke::FEndPIEWithWindowStateCheck(this));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SettingsBefore]() -> bool
        {
            TestTrue(TEXT("PIE is fully torn down"), ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
            TestTrue(TEXT("all overridden editor play settings are restored"), SettingsBefore.MatchesCurrent());
            return true;
        }), TEXT("network test cleanup")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkNetAutomation_InvalidStart,
    "Ck.Tests.Net.InvalidStartRejectsWithoutCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkNetAutomation_InvalidStart::RunTest(const FString& Parameters)
{
    if (GUnrealEd == nullptr || GUnrealEd->IsPlaySessionInProgress() ||
        GUnrealEd->IsPlaySessionRequestQueued() ||
        NOT ck::auto_test::net::Get_AllPIEWorlds().IsEmpty())
    {
        AddError(TEXT("Invalid-start regression requires no active or queued PIE session"));
        return false;
    }
    const auto SettingsBefore = ck_net_smoke::FPlaySettingsSnapshot{};
    TestNull(TEXT("quiet controller lookup rejects a missing world"),
        ck::auto_test::net::TryGet_RemoteClientPlayerController(nullptr, 0));
    TestNull(TEXT("quiet controller lookup rejects a negative index"),
        ck::auto_test::net::TryGet_RemoteClientPlayerController(nullptr, -1));
    TestNull(TEXT("quiet controller lookup rejects the actual editor world"),
        ck::auto_test::net::TryGet_RemoteClientPlayerController(
            GUnrealEd->GetEditorWorldContext().World(), 0));
    auto Dedicated = true;
    TestFalse(TEXT("unknown topology is rejected"),
        ck::auto_test::net::TryGet_DedicatedServer(TEXT("Unknown"), Dedicated));
    TestTrue(TEXT("failed parse leaves output unchanged"), Dedicated);
    TestTrue(TEXT("client-server topology parses"),
        ck::auto_test::net::TryGet_DedicatedServer(TEXT("ClientServer"), Dedicated));
    TestTrue(TEXT("client-server selects dedicated server"), Dedicated);
    TestTrue(TEXT("listen topology parses"),
        ck::auto_test::net::TryGet_DedicatedServer(TEXT("ListenServer"), Dedicated));
    TestFalse(TEXT("listen topology does not select dedicated server"), Dedicated);

    AddExpectedError(TEXT("Unsupported network topology"), EAutomationExpectedErrorFlags::Contains, 1);
    auto InvalidTopology = FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), TEXT("Unknown"));
    InvalidTopology.Update();

    auto CallbackCount = 0;
    auto ServerAction = FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [&CallbackCount](UWorld*) -> void { ++CallbackCount; }));
    ServerAction.Update();
    auto ClientAction = FCk_Latent_RunOnClient(0, FCk_NetAutoTest_ServerAction::CreateLambda(
        [&CallbackCount](UWorld*) -> void { ++CallbackCount; }));
    ClientAction.Update();
    TestEqual(TEXT("rejected session executes no downstream callback"), CallbackCount, 0);

    AddExpectedError(TEXT("Network test requires 1..64 total worlds"), EAutomationExpectedErrorFlags::Contains, 3);
    for (const auto InvalidWorldCount : {0, 1, 65})
    {
        auto InvalidCount = FCk_Latent_StartPIEMultiClient(
            InvalidWorldCount, TEXT("/Engine/Maps/Entry"), TEXT("ClientServer"));
        InvalidCount.Update();
    }
    TestTrue(TEXT("invalid requests never create a PIE world"), ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
    TestTrue(TEXT("invalid requests never mutate play settings"), SettingsBefore.MatchesCurrent());
    auto End = FCk_Latent_EndPIE{};
    TestTrue(TEXT("failed session has immediate cleanup"), End.Update());

    // A pending request belongs to its caller even before a world exists. Run
    // this synchronously so no editor tick can launch the request staged here.
    auto PendingRequest = FRequestPlaySessionParams{};
    PendingRequest.WorldType = EPlaySessionWorldType::PlayInEditor;
    GUnrealEd->RequestPlaySession(PendingRequest);
    AddExpectedError(TEXT("Cannot start a network test while a PIE session is already active"),
        EAutomationExpectedErrorFlags::Contains, 1);
    auto OverlappingStart = FCk_Latent_StartPIEMultiClient(2, TEXT(""), TEXT("ClientServer"));
    OverlappingStart.Update();
    ServerAction.Update();
    ClientAction.Update();
    TestEqual(TEXT("a rejected overlapping request executes no callbacks"), CallbackCount, 0);
    auto UnownedEnd = FCk_Latent_EndPIE{};
    TestTrue(TEXT("rejected overlapping request completes cleanup"), UnownedEnd.Update());
    TestTrue(TEXT("cleanup preserves the other caller's pending request"),
        GUnrealEd->IsPlaySessionRequestQueued());
    TestTrue(TEXT("overlapping request never mutates play settings"), SettingsBefore.MatchesCurrent());
    GUnrealEd->CancelRequestPlaySession();

    const auto CheckFailedPrerequisite = [this, &ServerAction, &ClientAction, &CallbackCount]()
    {
        TestTrue(TEXT("rejected prerequisite marks the session failed"),
            ck::auto_test::net::Get_HasSessionFailed());
        ServerAction.Update();
        ClientAction.Update();
        TestEqual(TEXT("rejected prerequisite executes no downstream callback"), CallbackCount, 0);
        auto Cleanup = FCk_Latent_EndPIE{};
        TestTrue(TEXT("unowned failed prerequisite can finish cleanup"), Cleanup.Update());
    };
    AddExpectedError(TEXT("Custom command rejected prerequisite"),
        EAutomationExpectedErrorFlags::Contains, 1);
    ck::auto_test::net::FailSession(TEXT("Custom command rejected prerequisite"));
    auto RejectedAssertion = FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([&CallbackCount]() -> bool
        { ++CallbackCount; return true; }), TEXT("rejected scenario callback"));
    RejectedAssertion.Update();
    TestEqual(TEXT("failed session executes no scenario assertion callback"), CallbackCount, 0);
    auto CleanupCallbackCount = 0;
    auto ExplicitCleanup = FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([&CleanupCallbackCount]() -> bool
        { ++CleanupCallbackCount; return true; }), TEXT("explicit failure cleanup"), true);
    ExplicitCleanup.Update();
    TestEqual(TEXT("failed session still executes explicit cleanup"), CleanupCallbackCount, 1);
    CheckFailedPrerequisite();
    AddExpectedError(TEXT("FCk_Latent_WaitForCondition: unbound predicate"),
        EAutomationExpectedErrorFlags::Contains, 1);
    auto UnboundWait = FCk_Latent_WaitForCondition(FCk_NetAutoTest_Assertion{}, 30.0f);
    UnboundWait.Update();
    CheckFailedPrerequisite();

    AddExpectedError(TEXT("FCk_Latent_WaitUntil: timed out"), EAutomationExpectedErrorFlags::Contains, 1);
    auto ExpiredWait = FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool { return false; }), -1.0,
        TEXT("deliberately expired prerequisite"));
    ExpiredWait.Update();
    CheckFailedPrerequisite();

    AddExpectedError(TEXT("FCk_Latent_RunOnServer: no server world"),
        EAutomationExpectedErrorFlags::Contains, 1);
    ServerAction.Update();
    CheckFailedPrerequisite();

    // Own a queued startup, then cancel it before the editor can launch PIE.
    // This is the branch where CancelRequestPlaySession is required.
    auto QueuedStart = FCk_Latent_StartPIEMultiClient(2, TEXT(""), TEXT("ListenServer"));
    TestTrue(TEXT("valid network startup enqueues a play request"), QueuedStart.Update());
    TestTrue(TEXT("owned startup is queued before the editor tick"),
        GUnrealEd->IsPlaySessionRequestQueued());
    TestFalse(TEXT("queued startup has no active PIE session"),
        GUnrealEd->IsPlayingSessionInEditor());
    auto QueuedEnd = FCk_Latent_EndPIE{};
    TestFalse(TEXT("queued-start EndPIE performs its first cleanup step"), QueuedEnd.Update());
    TestFalse(TEXT("owned queued startup is cancelled"), GUnrealEd->IsPlaySessionRequestQueued());
    TestTrue(TEXT("queued-start EndPIE completes without a world"), QueuedEnd.Update());
    TestTrue(TEXT("cancelled startup restores editor Play settings"), SettingsBefore.MatchesCurrent());
    TestTrue(TEXT("cancelled startup creates no PIE world"),
        ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
    return true;
}

#endif
