#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkOnline/CkOnlineDirectIpTest_Subsystem.h"
#include "CkGym_StartupSettings.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "IpNetDriver.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealEdGlobals.h"

namespace ck_online_direct_ip_test
{
    constexpr auto GymClassPath = TEXT("/Script/Angelscript.Ck_OnlineGym_DirectIp_GameMode");
    constexpr auto MapPath = TEXT("/Engine/Maps/Entry");
    constexpr auto HostPort = 17887;
    constexpr double StepTimeout = 25.0;

    struct FFixture
    {
        FAutomationTestBase* Test = nullptr;
        int32 HostPIE = INDEX_NONE;
        int32 ClientPIE = INDEX_NONE;
        int32 OldClientCount = 1;
        EPlayNetMode OldNetMode = EPlayNetMode::PIE_Standalone;
        bool OldRunUnderOneProcess = true;
        bool OldLaunchSeparateServer = false;
        ECkGym_StartupMode OldStartupMode = ECkGym_StartupMode::Cycler;
        bool SettingsCaptured = false;
        bool Aborted = false;
        bool PlayRequested = false;
        bool EndRequested = false;
        double EndStarted = 0.0;
        FDelegateHandle EndPIEHandle;
        FGuid SessionMarker;
        UWorld* InitialHostWorld = nullptr; // Identity only; never dereference after travel.
        int32 ProofPlayerId = INDEX_NONE;

        auto RestoreSettings() -> void
        {
            if (EndPIEHandle.IsValid())
            {
                FEditorDelegates::EndPIE.Remove(EndPIEHandle);
                EndPIEHandle.Reset();
            }
            if (!SettingsCaptured) { return; }
            auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
            if (Settings != nullptr)
            {
                Settings->SetPlayNumberOfClients(OldClientCount);
                Settings->SetPlayNetMode(OldNetMode);
                Settings->SetRunUnderOneProcess(OldRunUnderOneProcess);
                Settings->bLaunchSeparateServer = OldLaunchSeparateServer;
            }
            if (auto* GymSettings = GetMutableDefault<UCkGym_StartupSettings>())
            { GymSettings->StartupMode = OldStartupMode; }
            SettingsCaptured = false;
        }

        auto World(int32 InPIE) const -> UWorld*
        {
            if (GEngine == nullptr || InPIE == INDEX_NONE) { return nullptr; }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE && Context.PIEInstance == InPIE)
                { return Context.World(); }
            }
            return nullptr;
        }

        auto HostWorld() const -> UWorld* { return World(HostPIE); }
        auto ClientWorld() const -> UWorld* { return World(ClientPIE); }

        static auto Probe(UWorld* InWorld) -> UCkOnlineDirectIpTest_Subsystem*
        {
            auto* GameInstance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
            return IsValid(GameInstance) ? GameInstance->GetSubsystem<UCkOnlineDirectIpTest_Subsystem>() : nullptr;
        }

        static auto ProofState(UWorld* InWorld) -> ACkOnlineDirectIpTest_GameState*
        { return IsValid(InWorld) ? InWorld->GetGameState<ACkOnlineDirectIpTest_GameState>() : nullptr; }

        static auto HasOpenIpClient(UWorld* InWorld) -> bool
        {
            auto* Driver = IsValid(InWorld) ? Cast<UIpNetDriver>(InWorld->GetNetDriver()) : nullptr;
            return IsValid(Driver) && IsValid(Driver->ServerConnection)
                && Driver->ServerConnection->GetConnectionState() == USOCK_Open;
        }

        static auto HasOpenIpHost(UWorld* InWorld, int32 InRemoteCount) -> bool
        {
            auto* Driver = IsValid(InWorld) ? Cast<UIpNetDriver>(InWorld->GetNetDriver()) : nullptr;
            if (!IsValid(Driver) || Driver->ClientConnections.Num() != InRemoteCount) { return false; }
            for (const auto& Connection : Driver->ClientConnections)
            {
                if (!IsValid(Connection) || Connection->GetConnectionState() != USOCK_Open)
                { return false; }
            }
            return true;
        }
    };

    using FFixtureRef = TSharedRef<FFixture>;

    class FStart final : public IAutomationLatentCommand
    {
    public:
        explicit FStart(FFixtureRef InFixture) : Fixture(InFixture) {}
        auto Update() -> bool override
        {
            if (GUnrealEd == nullptr || GEngine == nullptr)
            { Fixture->Test->AddError(TEXT("Direct IP fixture requires an editor engine.")); Fixture->Aborted = true; return true; }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE)
                {
                    Fixture->Test->AddError(TEXT("Direct IP fixture requires no pre-existing PIE session."));
                    Fixture->Aborted = true;
                    return true;
                }
            }
            auto* GymClass = LoadClass<AGameModeBase>(nullptr, GymClassPath);
            if (!IsValid(GymClass))
            { Fixture->Test->AddError(TEXT("Online Direct IP AngelScript GameMode was not compiled.")); Fixture->Aborted = true; return true; }
            if (!FEditorFileUtils::LoadMap(MapPath, false, false))
            { Fixture->Test->AddError(TEXT("Could not load the engine Entry map for Direct IP PIE.")); Fixture->Aborted = true; return true; }
            auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
            if (Settings == nullptr)
            { Fixture->Test->AddError(TEXT("PIE play settings are unavailable.")); Fixture->Aborted = true; return true; }
            Settings->GetPlayNumberOfClients(Fixture->OldClientCount);
            Settings->GetPlayNetMode(Fixture->OldNetMode);
            Settings->GetRunUnderOneProcess(Fixture->OldRunUnderOneProcess);
            Fixture->OldLaunchSeparateServer = Settings->bLaunchSeparateServer;
            auto* GymSettings = GetMutableDefault<UCkGym_StartupSettings>();
            if (GymSettings == nullptr)
            { Fixture->Test->AddError(TEXT("Gym startup settings are unavailable.")); Fixture->Aborted = true; return true; }
            Fixture->OldStartupMode = GymSettings->StartupMode;
            Fixture->SettingsCaptured = true;
            TWeakPtr<FFixture> WeakFixture = Fixture;
            Fixture->EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([WeakFixture](bool)
            {
                if (auto Pinned = WeakFixture.Pin()) { Pinned->RestoreSettings(); }
            });
            Settings->SetPlayNumberOfClients(2);
            Settings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
            Settings->SetRunUnderOneProcess(true);
            Settings->bLaunchSeparateServer = false;
            // The gym base GameMode otherwise defers a saved Default/Last gym travel from BeginPlay.
            // Change the in-memory preference only; RestoreSettings returns it without SaveConfig.
            GymSettings->StartupMode = ECkGym_StartupMode::Cycler;
            FRequestPlaySessionParams Params;
            Params.WorldType = EPlaySessionWorldType::PlayInEditor;
            Params.GameModeOverride = GymClass;
            Fixture->PlayRequested = true;
            GUnrealEd->RequestPlaySession(Params);
            return true;
        }
    private:
        FFixtureRef Fixture;
    };

    class FStep final : public IAutomationLatentCommand
    {
    public:
        FStep(FFixtureRef InFixture, const TCHAR* InLabel, TFunction<bool(FFixture&)> InCondition,
            TFunction<void(FFixture&)> InAction = {})
            : Fixture(InFixture), Label(InLabel), Condition(MoveTemp(InCondition)), Action(MoveTemp(InAction)) {}
        auto Update() -> bool override
        {
            if (Fixture->Aborted) { return true; }
            if (Started < 0.0) { Started = FPlatformTime::Seconds(); }
            if (Condition(*Fixture))
            {
                if (Action) { Action(*Fixture); }
                return true;
            }
            if (FPlatformTime::Seconds() - Started < StepTimeout) { return false; }
            Fixture->Test->AddError(FString::Printf(TEXT("Direct IP timed out: %s"), *Label));
            Fixture->Aborted = true;
            return true;
        }
    private:
        FFixtureRef Fixture;
        FString Label;
        TFunction<bool(FFixture&)> Condition;
        TFunction<void(FFixture&)> Action;
        double Started = -1.0;
    };

    class FEnd final : public IAutomationLatentCommand
    {
    public:
        explicit FEnd(FFixtureRef InFixture) : Fixture(InFixture) {}
        auto Update() -> bool override
        {
            if (!Fixture->PlayRequested) { Fixture->RestoreSettings(); return true; }
            if (GUnrealEd == nullptr) { Fixture->RestoreSettings(); return true; }
            if (!Fixture->EndRequested)
            {
                Fixture->EndRequested = true;
                Fixture->EndStarted = FPlatformTime::Seconds();
                GUnrealEd->RequestEndPlayMap();
                return false;
            }
            bool HasPIE = false;
            if (GEngine != nullptr)
            {
                for (const auto& Context : GEngine->GetWorldContexts())
                { HasPIE |= Context.WorldType == EWorldType::PIE; }
            }
            if (HasPIE && FPlatformTime::Seconds() - Fixture->EndStarted < StepTimeout)
            { return false; }
            if (HasPIE) { Fixture->Test->AddError(TEXT("Direct IP PIE teardown timed out.")); }
            Fixture->RestoreSettings();
            return true;
        }
    private:
        FFixtureRef Fixture;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineDirectIp_Transport,
    "Ck.Online.DirectIp.RealHostJoinProofDisconnectRejoin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkOnlineDirectIp_Transport::RunTest(const FString& Parameters)
{
    using namespace ck_online_direct_ip_test;
    const auto Fixture = MakeShared<FFixture>();
    Fixture->Test = this;

    ADD_LATENT_AUTOMATION_COMMAND(FStart(Fixture));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("two independent, ready Offline PIE instances"),
        [](FFixture& F)
        {
            TArray<int32> Instances;
            if (GEngine == nullptr) { return false; }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                auto* World = Context.World();
                if (Context.WorldType != EWorldType::PIE || !IsValid(World) || !World->HasBegunPlay())
                { continue; }
                auto* Probe = FFixture::Probe(World);
                if (World->GetNetMode() != NM_Standalone || World->GetNetDriver() != nullptr
                    || !IsValid(Probe) || !Probe->Get_IsOffline() || !IsValid(FFixture::ProofState(World)))
                { return false; }
                Instances.Add(Context.PIEInstance);
            }
            if (Instances.Num() != 2 || Instances[0] == Instances[1]) { return false; }
            Instances.Sort();
            F.HostPIE = Instances[0];
            F.ClientPIE = Instances[1];
            return true;
        }, [this](FFixture& F)
        {
            TestNotEqual(TEXT("PIE contexts are distinct"), F.HostPIE, F.ClientPIE);
            auto* Host = F.HostWorld();
            auto* Client = F.ClientWorld();
            F.InitialHostWorld = Host;
            TestTrue(TEXT("host starts Offline without driver"), IsValid(Host) && Host->GetNetDriver() == nullptr);
            TestTrue(TEXT("client starts Offline without driver"), IsValid(Client) && Client->GetNetDriver() == nullptr);
            auto* ClientProbe = FFixture::Probe(Client);
            if (IsValid(ClientProbe))
            {
                TestFalse(TEXT("rejoin before join is rejected"), ClientProbe->Request_Rejoin());
                TestFalse(TEXT("invalid URL is rejected"), ClientProbe->Request_Join(TEXT("example.com:17887?listen")));
                TestFalse(TEXT("out-of-range port is rejected"), ClientProbe->Request_Join(TEXT("127.0.0.1:65536")));
                TestTrue(TEXT("invalid requests leave client Offline"), ClientProbe->Get_IsOffline());
                TestTrue(TEXT("invalid requests create no driver"), Client->GetNetDriver() == nullptr);
                TestEqual(TEXT("invalid requests create no proof"), ClientProbe->Get_ProofSequence(), 0);
            }
            auto* HostProbe = FFixture::Probe(Host);
            if (IsValid(HostProbe))
            {
                TestFalse(TEXT("invalid host port is rejected"), HostProbe->Request_Host(0));
                TestTrue(TEXT("invalid host request leaves host Offline"), HostProbe->Get_IsOffline());
                TestTrue(TEXT("invalid host request creates no driver"), Host->GetNetDriver() == nullptr);
                auto* State = FFixture::ProofState(Host);
                TestTrue(TEXT("invalid host request creates no session GUID"), IsValid(State) && !State->SessionMarker.IsValid());
                TestEqual(TEXT("invalid host request creates no proof"), HostProbe->Get_ProofSequence(), 0);
            }
            TestTrue(TEXT("explicit IP host request accepted"), IsValid(HostProbe) && HostProbe->Request_Host(HostPort));
            TestTrue(TEXT("host request remains pending until world travel"),
                IsValid(HostProbe) && !HostProbe->Get_IsHosting() && Host->GetNetDriver() == nullptr);
            TestTrue(TEXT("host request does not mark departing world"),
                IsValid(FFixture::ProofState(Host)) && !FFixture::ProofState(Host)->SessionMarker.IsValid());
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host has listening IpNetDriver and one local peer"),
        [](FFixture& F)
        {
            auto* Host = F.HostWorld();
            auto* Probe = FFixture::Probe(Host);
            auto* State = FFixture::ProofState(Host);
            return IsValid(Probe) && Probe->Get_IsHosting() && FFixture::HasOpenIpHost(Host, 0)
                && IsValid(State) && State->SessionMarker.IsValid() && State->PlayerArray.Num() == 1
                && Probe->Get_PeerCount() == 1 && Probe->Get_JoinCount() == 1;
        }, [this](FFixture& F)
        {
            auto* HostProbe = FFixture::Probe(F.HostWorld());
            auto* ClientProbe = FFixture::Probe(F.ClientWorld());
            auto* HostDriver = F.HostWorld()->GetNetDriver();
            TestTrue(TEXT("host travel replaced the departing world"), F.HostWorld() != F.InitialHostWorld);
            F.SessionMarker = FFixture::ProofState(F.HostWorld())->SessionMarker;
            TestTrue(TEXT("host session GUID valid after bound listen"), F.SessionMarker.IsValid());
            TestFalse(TEXT("duplicate host request rejected"), HostProbe->Request_Host(HostPort));
            TestTrue(TEXT("duplicate host request keeps same driver"), F.HostWorld()->GetNetDriver() == HostDriver);
            TestTrue(TEXT("duplicate host request keeps same session GUID"),
                FFixture::ProofState(F.HostWorld())->SessionMarker == F.SessionMarker);
            TestFalse(TEXT("host proof request rejected"), HostProbe->Request_Proof());
            TestEqual(TEXT("host proof stays zero"), HostProbe->Get_ProofSequence(), 0);
            TestEqual(TEXT("host exposes requested endpoint"), HostProbe->Get_Endpoint(), FString(TEXT("127.0.0.1:17887")));
            TestTrue(TEXT("explicit IP join request accepted"), IsValid(ClientProbe) && ClientProbe->Request_Join(HostProbe->Get_Endpoint()));
        }));

    const auto Connected = [](FFixture& F, int32 InJoinCount)
    {
        auto* Host = F.HostWorld();
        auto* Client = F.ClientWorld();
        auto* HostProbe = FFixture::Probe(Host);
        auto* ClientProbe = FFixture::Probe(Client);
        auto* HostState = FFixture::ProofState(Host);
        auto* ClientState = FFixture::ProofState(Client);
        return FFixture::HasOpenIpHost(Host, 1) && FFixture::HasOpenIpClient(Client)
            && IsValid(HostProbe) && IsValid(ClientProbe) && HostProbe->Get_IsHosting()
            && ClientProbe->Get_IsConnected() && IsValid(HostState) && IsValid(ClientState)
            && HostState->SessionMarker == F.SessionMarker && ClientState->SessionMarker == F.SessionMarker
            && HostProbe->Get_PeerCount() == 2 && ClientProbe->Get_PeerCount() == 2
            && HostProbe->Get_JoinCount() == InJoinCount && ClientProbe->Get_JoinCount() == InJoinCount;
    };
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("real IP admission and replicated roster"),
        [Connected](FFixture& F) { return Connected(F, 2); }, [this](FFixture& F)
        {
            auto* Client = F.ClientWorld();
            auto* Controller = Client->GetGameInstance()->GetFirstLocalPlayerController();
            TestTrue(TEXT("admitted client has replicated player state"),
                IsValid(Controller) && IsValid(Controller->PlayerState));
            if (IsValid(Controller) && IsValid(Controller->PlayerState))
            { F.ProofPlayerId = Controller->PlayerState->GetPlayerId(); }
            TestTrue(TEXT("proof peer ID is valid"), F.ProofPlayerId != INDEX_NONE);
            TestTrue(TEXT("client sends proof RPC"), FFixture::Probe(Client)->Request_Proof());
        }));

    const auto ProofAt = [](FFixture& F, int32 InSequence)
    {
        auto* HostState = FFixture::ProofState(F.HostWorld());
        auto* ClientState = FFixture::ProofState(F.ClientWorld());
        return F.ProofPlayerId != INDEX_NONE && IsValid(HostState) && IsValid(ClientState)
            && HostState->ProofSequence == InSequence && ClientState->ProofSequence == InSequence
            && HostState->LastProofPlayerId == F.ProofPlayerId
            && ClientState->LastProofPlayerId == F.ProofPlayerId;
    };
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("proof RPC reaches host and replicates sequence one"),
        [ProofAt](FFixture& F) { return ProofAt(F, 1); }, [this](FFixture& F)
        {
            TestEqual(TEXT("both worlds have proof sequence one"),
                FFixture::ProofState(F.HostWorld())->ProofSequence,
                FFixture::ProofState(F.ClientWorld())->ProofSequence);
            TestTrue(TEXT("client disconnect request accepted"), FFixture::Probe(F.ClientWorld())->Request_Disconnect());
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("client offline, driver gone, host recorded leave"),
        [](FFixture& F)
        {
            auto* Host = F.HostWorld();
            auto* Client = F.ClientWorld();
            auto* HostProbe = FFixture::Probe(Host);
            auto* ClientProbe = FFixture::Probe(Client);
            return IsValid(Client) && Client->HasBegunPlay() && Client->GetNetMode() == NM_Standalone
                && Client->GetNetDriver() == nullptr && IsValid(ClientProbe) && ClientProbe->Get_IsOffline()
                && IsValid(FFixture::ProofState(Client)) && FFixture::HasOpenIpHost(Host, 0)
                && IsValid(HostProbe) && HostProbe->Get_PeerCount() == 1
                && HostProbe->Get_LeaveCount() == 1;
        }, [this](FFixture& F)
        {
            auto* ClientProbe = FFixture::Probe(F.ClientWorld());
            TestFalse(TEXT("proof while offline rejected"), ClientProbe->Request_Proof());
            TestEqual(TEXT("offline proof remains zero"), ClientProbe->Get_ProofSequence(), 0);
            TestTrue(TEXT("rejoin request accepted"), ClientProbe->Request_Rejoin());
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("real IP rejoin and same replicated session"),
        [Connected](FFixture& F) { return Connected(F, 3); }, [this](FFixture& F)
        {
            auto* Client = F.ClientWorld();
            auto* Controller = Client->GetGameInstance()->GetFirstLocalPlayerController();
            TestTrue(TEXT("rejoined client has player state"),
                IsValid(Controller) && IsValid(Controller->PlayerState));
            if (IsValid(Controller) && IsValid(Controller->PlayerState))
            { F.ProofPlayerId = Controller->PlayerState->GetPlayerId(); }
            TestTrue(TEXT("rejoined peer ID valid"), F.ProofPlayerId != INDEX_NONE);
            TestTrue(TEXT("rejoined client sends second proof RPC"), FFixture::Probe(Client)->Request_Proof());
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("second proof RPC reaches host and replicates"),
        [ProofAt](FFixture& F) { return ProofAt(F, 2); }, [this](FFixture& F)
        {
            TestEqual(TEXT("host join count after rejoin"), FFixture::Probe(F.HostWorld())->Get_JoinCount(), 3);
            TestEqual(TEXT("host leave count after rejoin"), FFixture::Probe(F.HostWorld())->Get_LeaveCount(), 1);
            TestEqual(TEXT("client proof sequence after rejoin"), FFixture::Probe(F.ClientWorld())->Get_ProofSequence(), 2);
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FEnd(Fixture));
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
