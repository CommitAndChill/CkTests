#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/Direct/CkOnlineDirect_Feature.h"
#include "CkOnline/Direct/CkOnlineDirect_Subsystem.h"
#include "CkOnline/Direct/CkOnlineDirect_Utils.h"
#include "CkOnline/CkOnlineDirectGym_Subsystem.h"
#include "CkOnline/CkOnlineDirectIpTest_Bridge.h"
#include "CkOnline/CkOnlineDirectIpTest_Subsystem.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "IPAddress.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "UnrealEdGlobals.h"
#include "UObject/ObjectKey.h"

namespace ck_online_direct_recovery_test
{
    constexpr auto LobbyModePath = TEXT("/Script/Angelscript.Ck_OnlineGym_DirectLobby_GameMode");
    constexpr auto MapPath = TEXT("/Engine/Maps/Entry");
    constexpr int32 TcpPort = 17901;
    constexpr double StepTimeoutSeconds = 25.0;

    struct FFixture
    {
        FAutomationTestBase* Test = nullptr;
        int32 PIEInstance = INDEX_NONE;
        int32 OldClientCount = 1;
        EPlayNetMode OldNetMode = EPlayNetMode::PIE_Standalone;
        bool OldRunUnderOneProcess = true;
        bool OldLaunchSeparateServer = false;
        ECkGym_StartupMode OldStartupMode = ECkGym_StartupMode::Cycler;
        bool SettingsCaptured = false;
        bool PlayRequested = false;
        bool EndRequested = false;
        bool Aborted = false;
        bool SawPendingNetGame = false;
        bool SawJoiningState = false;
        double EndStarted = 0.0;
        FDelegateHandle EndPIEHandle;
        TWeakObjectPtr<UWorld> EditorWorldBefore;
        FString EditorPackageBefore;
        TWeakObjectPtr<UGameInstance> OwnedGameInstance;
        FObjectKey InitialLobbyWorldKey; // Object index/serial survives address reuse across travel.
        TSharedPtr<ck::direct::FFeature> Feature;
        TSharedPtr<ck::FOnlineServiceHost> Host;
        FCk_Handle_OnlineDirect HostHandle;
        FSocket* SilentUdp = nullptr;
        ISocketSubsystem* Sockets = nullptr;
        int32 SilentPort = 0;
        FGuid HostOperation;
        FGuid ClientGeneration;
        ck::test::ip::FLobbyBinding Binding;
        FCk_OnlineDirect_Snapshot JoinedBeforeTravel;
        FCk_OnlineDirect_Snapshot HostedBeforeTravel;

        ~FFixture()
        {
            ReleaseOwnedResources();
            if (PlayRequested && !EndRequested && GUnrealEd != nullptr)
            { GUnrealEd->RequestEndPlayMap(); }
            RestoreSettings();
        }

        auto World() const -> UWorld*
        {
            if (GEngine == nullptr || PIEInstance == INDEX_NONE) { return nullptr; }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE && Context.PIEInstance == PIEInstance)
                { return Context.World(); }
            }
            return nullptr;
        }

        auto HasPendingNetGame() const -> bool
        {
            if (GEngine == nullptr || PIEInstance == INDEX_NONE) { return false; }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE && Context.PIEInstance == PIEInstance)
                { return Context.PendingNetGame != nullptr; }
            }
            return false;
        }

        auto GameInstance() const -> UGameInstance*
        { return OwnedGameInstance.Get(); }

        auto EditorMapUnchanged() const -> bool
        {
            auto* Current = GUnrealEd != nullptr ? GUnrealEd->GetEditorWorldContext().World() : nullptr;
            return EditorWorldBefore.IsValid() && Current == EditorWorldBefore.Get() &&
                IsValid(Current) && Current->GetPackage()->GetName() == EditorPackageBefore;
        }

        auto Gym() const -> UCkOnlineDirectGym_Subsystem*
        {
            auto* Instance = GameInstance();
            return IsValid(Instance) ? Instance->GetSubsystem<UCkOnlineDirectGym_Subsystem>() : nullptr;
        }

        auto Direct() const -> UCk_OnlineDirect_Subsystem_UE*
        {
            auto* Instance = GameInstance();
            return IsValid(Instance) ? Instance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>() : nullptr;
        }

        auto Ip() const -> UCkOnlineDirectIpTest_Subsystem*
        {
            auto* Instance = GameInstance();
            return IsValid(Instance) ? Instance->GetSubsystem<UCkOnlineDirectIpTest_Subsystem>() : nullptr;
        }

        auto HostSnapshot() const -> FCk_OnlineDirect_Snapshot
        { return UCk_Utils_OnlineDirect_UE::Get_Snapshot(HostHandle); }

        auto TickHost() -> void
        { if (Host) { Host->Tick(FCk_Time{FPlatformTime::Seconds()}); } }

        auto HostRequest(ECk_OnlineDirect_Action InAction, bool InReady = false) -> bool
        {
            auto Request = FCk_Request_OnlineDirect{};
            HostOperation = FGuid::NewGuid();
            Request.Set_OperationId(HostOperation).Set_Action(InAction).Set_Ready(InReady);
            return UCk_Utils_OnlineDirect_UE::TryRequest(HostHandle, Request);
        }

        auto ReserveSilentUdp() -> bool
        {
            Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
            if (Sockets == nullptr) { return false; }
            SilentUdp = Sockets->CreateSocket(NAME_DGram, TEXT("CkOnlineDirectRecoverySilentUdp"), false);
            if (SilentUdp == nullptr) { return false; }
            auto Address = Sockets->CreateInternetAddr();
            bool ValidAddress = false;
            Address->SetIp(TEXT("127.0.0.1"), ValidAddress);
            Address->SetPort(0);
            if (!ValidAddress || !SilentUdp->Bind(*Address)) { return false; }
            SilentUdp->GetAddress(*Address);
            SilentPort = Address->GetPort();
            return SilentPort > 0;
        }

        auto ReleaseOwnedResources() -> void
        {
            if (auto* DirectService = Direct())
            {
                auto* GymService = Gym();
                if (IsValid(GymService) && GymService->Get_IsOwnedProfile() &&
                    DirectService->Get_IsStarted() && ClientGeneration.IsValid() &&
                    DirectService->Get_Snapshot().Get_ServiceGeneration() == ClientGeneration)
                { GymService->Request_StopOwnedProfile(); }
            }
            ClientGeneration.Invalidate();
            if (Host) { Host->Request_Shutdown(); Host.Reset(); }
            Feature.Reset();
            if (SilentUdp != nullptr && Sockets != nullptr)
            { Sockets->DestroySocket(SilentUdp); }
            SilentUdp = nullptr;
            Sockets = nullptr;
            SilentPort = 0;
        }

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
    };

    using FFixtureRef = TSharedRef<FFixture>;

    class FStart final : public IAutomationLatentCommand
    {
    public:
        explicit FStart(FFixtureRef InFixture) : Fixture(InFixture) {}
        auto Update() -> bool override
        {
            if (GUnrealEd == nullptr || GEngine == nullptr)
            { Fixture->Test->AddError(TEXT("Recovery test requires an editor engine.")); Fixture->Aborted = true; return true; }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE)
                { Fixture->Test->AddError(TEXT("Recovery test requires no existing PIE session.")); Fixture->Aborted = true; return true; }
            }
            auto* LobbyMode = LoadClass<AGameModeBase>(nullptr, LobbyModePath);
            if (!IsValid(LobbyMode))
            { Fixture->Test->AddError(TEXT("Online Lobby AngelScript GameMode is unavailable.")); Fixture->Aborted = true; return true; }
            auto* EditorWorld = GUnrealEd->GetEditorWorldContext().World();
            if (!IsValid(EditorWorld))
            { Fixture->Test->AddError(TEXT("Editor world is unavailable before recovery PIE.")); Fixture->Aborted = true; return true; }
            Fixture->EditorWorldBefore = EditorWorld;
            Fixture->EditorPackageBefore = EditorWorld->GetPackage()->GetName();
            auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
            auto* GymSettings = GetMutableDefault<UCkGym_StartupSettings>();
            if (Settings == nullptr || GymSettings == nullptr)
            { Fixture->Test->AddError(TEXT("PIE or Gym settings unavailable.")); Fixture->Aborted = true; return true; }
            Settings->GetPlayNumberOfClients(Fixture->OldClientCount);
            Settings->GetPlayNetMode(Fixture->OldNetMode);
            Settings->GetRunUnderOneProcess(Fixture->OldRunUnderOneProcess);
            Fixture->OldLaunchSeparateServer = Settings->bLaunchSeparateServer;
            Fixture->OldStartupMode = GymSettings->StartupMode;
            Fixture->SettingsCaptured = true;
            TWeakPtr<FFixture> WeakFixture = Fixture;
            Fixture->EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([WeakFixture](bool)
            {
                if (auto Pinned = WeakFixture.Pin())
                {
                    Pinned->PlayRequested = false;
                    Pinned->RestoreSettings();
                }
            });
            Settings->SetPlayNumberOfClients(1);
            Settings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
            Settings->SetRunUnderOneProcess(true);
            Settings->bLaunchSeparateServer = false;
            GymSettings->StartupMode = ECkGym_StartupMode::Cycler;
            FRequestPlaySessionParams Params;
            Params.WorldType = EPlaySessionWorldType::PlayInEditor;
            Params.GlobalMapOverride = TEXT("/Engine/Maps/Entry.Entry");
            Params.GameModeOverride = LobbyMode;
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
            TFunction<void(FFixture&)> InAction = {}, double InTimeoutSeconds = StepTimeoutSeconds)
            : Fixture(InFixture), Label(InLabel), Condition(MoveTemp(InCondition)),
              Action(MoveTemp(InAction)), TimeoutSeconds(InTimeoutSeconds) {}
        auto Update() -> bool override
        {
            if (Fixture->Aborted) { return true; }
            if (Started < 0.0) { Started = FPlatformTime::Seconds(); }
            Fixture->TickHost();
            Fixture->SawPendingNetGame |= Fixture->HasPendingNetGame();
            if (auto* Ip = Fixture->Ip())
            { Fixture->SawJoiningState |= Ip->Get_StateText().StartsWith(TEXT("Joining")); }
            if (Condition(*Fixture))
            {
                if (Action) { Action(*Fixture); }
                return true;
            }
            if (FPlatformTime::Seconds() - Started < TimeoutSeconds) { return false; }
            auto* World = Fixture->World();
            auto* Mode = IsValid(World) ? World->GetAuthGameMode() : nullptr;
            auto* Ip = Fixture->Ip();
            auto* Direct = Fixture->Direct();
            FString BridgeText = TEXT("unavailable");
            if (IsValid(Ip))
            {
                const auto RawBridgeText = Ip->Get_LobbyBridgeText();
                const int32 LastSeparator = RawBridgeText.Find(TEXT(" / "), ESearchCase::CaseSensitive,
                    ESearchDir::FromEnd);
                BridgeText = LastSeparator == INDEX_NONE ? RawBridgeText : FString::Printf(
                    TEXT("%s / captured [redacted] / %s"),
                    RawBridgeText.StartsWith(TEXT("active")) ? TEXT("active") : TEXT("inactive"),
                    *RawBridgeText.Mid(LastSeparator + 3));
            }
            Fixture->Test->AddError(FString::Printf(
                TEXT("Recovery timed out: %s; world=%s begun=%d initial=%d mode=%s netmode=%d driver=%d pending=%d "
                     "ip=%s error=%s bridge=%s directStarted=%d directState=%d"),
                *Label,
                IsValid(World) ? *UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) : TEXT("unavailable"),
                IsValid(World) && World->HasBegunPlay(), IsValid(World) && FObjectKey(World) == Fixture->InitialLobbyWorldKey,
                IsValid(Mode) ? *Mode->GetClass()->GetPathName() : TEXT("unavailable"),
                IsValid(World) ? static_cast<int32>(World->GetNetMode()) : -1,
                IsValid(World) && World->GetNetDriver() != nullptr, Fixture->HasPendingNetGame(),
                IsValid(Ip) ? *Ip->Get_StateText() : TEXT("unavailable"),
                IsValid(Ip) ? *Ip->Get_LastError() : TEXT("unavailable"), *BridgeText,
                IsValid(Direct) && Direct->Get_IsStarted(),
                IsValid(Direct) && Direct->Get_IsStarted()
                    ? static_cast<int32>(Direct->Get_Snapshot().Get_State()) : -1));
            Fixture->Aborted = true;
            return true;
        }
    private:
        FFixtureRef Fixture;
        FString Label;
        TFunction<bool(FFixture&)> Condition;
        TFunction<void(FFixture&)> Action;
        double Started = -1.0;
        double TimeoutSeconds;
    };

    class FEnd final : public IAutomationLatentCommand
    {
    public:
        explicit FEnd(FFixtureRef InFixture) : Fixture(InFixture) {}
        auto Update() -> bool override
        {
            Fixture->ReleaseOwnedResources();
            if (!Fixture->PlayRequested || GUnrealEd == nullptr)
            { Fixture->RestoreSettings(); return true; }
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
            if (HasPIE && FPlatformTime::Seconds() - Fixture->EndStarted < StepTimeoutSeconds)
            { return false; }
            if (HasPIE) { Fixture->Test->AddError(TEXT("Recovery PIE teardown timed out.")); }
            Fixture->RestoreSettings();
            return true;
        }
    private:
        FFixtureRef Fixture;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineDirectRecovery,
    "Ck.Online.Direct.GameplayEntryFailureReturnsToLobby",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkOnlineDirectRecovery::RunTest(const FString& Parameters)
{
    using namespace ck_online_direct_recovery_test;
    auto Fixture = MakeShared<FFixture>();
    Fixture->Test = this;

    ADD_LATENT_AUTOMATION_COMMAND(FStart(Fixture));
    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("one ready Offline Online Lobby PIE world"),
        [](FFixture& F)
        {
            if (GEngine == nullptr) { return false; }
            int32 Found = INDEX_NONE;
            UWorld* Ready = nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                auto* World = Context.World();
                if (Context.WorldType != EWorldType::PIE || !IsValid(World) || !World->HasBegunPlay())
                { continue; }
                if (Found != INDEX_NONE) { return false; }
                Found = Context.PIEInstance;
                Ready = World;
            }
            if (Found == INDEX_NONE || !IsValid(Ready) || Ready->GetNetMode() != NM_Standalone ||
                Ready->GetNetDriver() != nullptr || !IsValid(Ready->GetGameInstance()) ||
                UWorld::RemovePIEPrefix(Ready->GetPackage()->GetName()) != MapPath ||
                !IsValid(Ready->GetAuthGameMode()) ||
                Ready->GetAuthGameMode()->GetClass()->GetPathName() != LobbyModePath)
            { return false; }
            F.PIEInstance = Found;
            F.OwnedGameInstance = Ready->GetGameInstance();
            auto* Gym = F.Gym();
            auto* Direct = F.Direct();
            auto* Ip = F.Ip();
            return IsValid(Gym) && IsValid(Direct) && IsValid(Ip);
        }, [this](FFixture& F)
        {
            auto* Gym = F.Gym();
            auto* Direct = F.Direct();
            F.InitialLobbyWorldKey = FObjectKey(F.World());
            TestTrue(TEXT("single PIE world has same owning GameInstance"),
                F.World()->GetGameInstance() == F.GameInstance());
            TestTrue(TEXT("PIE map override leaves resident editor map unchanged"),
                F.EditorMapUnchanged());
            if (Direct->Get_IsStarted() || !F.Ip()->Get_IsOffline())
            {
                AddError(TEXT("Recovery fixture needs a fresh Offline GameInstance with no Direct profile."));
                F.Aborted = true;
                return;
            }
            const auto Before = Direct->Get_Snapshot();
            const auto DraftBefore = Gym->Get_JoinDescriptorDraft();
            TestFalse(TEXT("empty invite rejected before profile start"),
                Gym->Request_JoinInvite(TEXT("")));
            TestFalse(TEXT("empty invite starts no Direct profile or gym ownership"),
                Direct->Get_IsStarted() || Gym->Get_IsOwnedProfile());
            TestEqual(TEXT("empty invite leaves Direct revision unchanged"),
                Direct->Get_Snapshot().Get_Revision(), Before.Get_Revision());
            TestEqual(TEXT("empty invite leaves saved draft unchanged"),
                Gym->Get_JoinDescriptorDraft(), DraftBefore);
            TestFalse(TEXT("malformed invite rejected before profile start"),
                Gym->Request_JoinInvite(TEXT("ckd:1|bad")));
            TestFalse(TEXT("bad invite starts no Direct profile"), Direct->Get_IsStarted());
            TestFalse(TEXT("bad invite gives gym no profile ownership"), Gym->Get_IsOwnedProfile());
            TestEqual(TEXT("bad invite leaves snapshot revision unchanged"),
                Direct->Get_Snapshot().Get_Revision(), Before.Get_Revision());
            TestEqual(TEXT("bad invite leaves saved draft unchanged"),
                Gym->Get_JoinDescriptorDraft(), DraftBefore);
            if (Direct->Get_IsStarted())
            {
                if (Gym->Get_IsOwnedProfile())
                { F.ClientGeneration = Direct->Get_Snapshot().Get_ServiceGeneration(); }
                F.Aborted = true;
                return;
            }

            auto Spec = FCk_OnlineDirect_Spec{};
            Spec.Set_Name(TEXT("RecoveryHost")).Set_AdvertisedAddress(TEXT("127.0.0.1"))
                .Set_Port(TcpPort).Set_Capacity(2).Set_InviteLifetimeSeconds(60.0)
                .Set_OperationTimeoutSeconds(3.0);
            F.Feature = ck::direct::FFeature::TryCreate(Spec);
            if (!F.Feature)
            { AddError(TEXT("Native Direct host feature could not start.")); F.Aborted = true; return; }
            F.Host = ck::FOnlineServiceHost::TryCreateFeature(
                StaticCastSharedRef<ck::IOnlineServiceFeature>(F.Feature.ToSharedRef()),
                FCk_Time{FPlatformTime::Seconds()});
            if (!F.Host)
            { AddError(TEXT("Native Direct ECS host could not start.")); F.Aborted = true; return; }
            F.HostHandle = UCk_Utils_OnlineDirect_UE::Cast(F.Host->Get_Handle());
            if (!UCk_Utils_OnlineDirect_UE::Has(F.HostHandle) ||
                !F.HostRequest(ECk_OnlineDirect_Action::Host))
            { AddError(TEXT("Native Direct Host request was not accepted.")); F.Aborted = true; }
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("native TCP host bound with a real invite"),
        [](FFixture& F)
        { return UCk_Utils_OnlineDirect_UE::Get_CompletedCount(F.HostHandle) == 1; },
        [this](FFixture& F)
        {
            const auto Hosted = F.HostSnapshot();
            const auto Completion = UCk_Utils_OnlineDirect_UE::Get_LastCompletion(F.HostHandle);
            if (Completion.Get_Result() != ECk_OnlineDirect_Result::Succeeded ||
                Hosted.Get_State() != ECk_OnlineDirect_State::Hosting)
            {
                AddError(TEXT("Native Direct TCP host could not bind port 17901; check for a conflicting listener."));
                F.Aborted = true;
                return;
            }
            TestEqual(TEXT("native Host completion matches accepted operation"),
                Completion.Get_OperationId(), F.HostOperation);
            TestEqual(TEXT("native Host advertises its bound target"), Hosted.Get_HostAddress(),
                FString(TEXT("127.0.0.1")));
            if (!TestTrue(TEXT("GameInstance gym admits real invite without clipboard"),
                F.Gym()->Request_JoinInvite(Hosted.Get_JoinDescriptor())))
            { F.Aborted = true; return; }
            F.ClientGeneration = F.Direct()->Get_Snapshot().Get_ServiceGeneration();
            if (!TestTrue(TEXT("gym owns a valid newly started Direct generation"),
                F.Gym()->Get_IsOwnedProfile() && F.ClientGeneration.IsValid()))
            { F.Aborted = true; }
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("GameInstance client joins real TCP host"),
        [](FFixture& F)
        {
            auto* Direct = F.Direct();
            return IsValid(Direct) && Direct->Get_Snapshot().Get_State() == ECk_OnlineDirect_State::Joined &&
                F.HostSnapshot().Get_Members().Num() == 2 && Direct->Get_Snapshot().Get_Members().Num() == 2;
        }, [this](FFixture& F)
        {
            TestEqual(TEXT("client joins native host epoch"), F.Direct()->Get_Snapshot().Get_LobbyEpoch(),
                F.HostSnapshot().Get_LobbyEpoch());
            TestTrue(TEXT("native host readiness request accepted"),
                F.HostRequest(ECk_OnlineDirect_Action::SetReady, true));
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("host readiness reaches client"),
        [](FFixture& F)
        {
            if (UCk_Utils_OnlineDirect_UE::Get_CompletedCount(F.HostHandle) < 2) { return false; }
            const auto HostId = F.HostSnapshot().Get_LocalMemberId();
            return F.Direct()->Get_Snapshot().Get_Members().ContainsByPredicate(
                [HostId](const FCk_OnlineDirect_Member& M) { return M.Get_Id() == HostId && M.Get_Ready(); });
        }, [this](FFixture& F)
        {
            TestEqual(TEXT("native ready completed successfully"),
                UCk_Utils_OnlineDirect_UE::Get_LastCompletion(F.HostHandle).Get_Result(),
                ECk_OnlineDirect_Result::Succeeded);
            TestTrue(TEXT("GameInstance client readiness request accepted"), F.Gym()->Request_ToggleReady());
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("two actual members ready and matching lobby binding"),
        [](FFixture& F)
        {
            const auto Hosted = F.HostSnapshot();
            const auto Joined = F.Direct()->Get_Snapshot();
            ck::test::ip::FLobbyBinding HostBinding;
            ck::test::ip::FLobbyBinding ClientBinding;
            return ck::test::ip::FLobbyBinding::TryCapture(Hosted, true, HostBinding) &&
                ck::test::ip::FLobbyBinding::TryCapture(Joined, false, ClientBinding) &&
                HostBinding.Epoch == ClientBinding.Epoch && HostBinding.Members == ClientBinding.Members;
        }, [this](FFixture& F)
        {
            F.HostedBeforeTravel = F.HostSnapshot();
            F.JoinedBeforeTravel = F.Direct()->Get_Snapshot();
            TestTrue(TEXT("client binding captures live two-ready-member lobby"),
                ck::test::ip::FLobbyBinding::TryCapture(F.JoinedBeforeTravel, false, F.Binding));
            TestTrue(TEXT("host and client address and epoch agree"),
                F.HostedBeforeTravel.Get_HostAddress() == F.Binding.HostAddress &&
                F.HostedBeforeTravel.Get_LobbyEpoch() == F.Binding.Epoch);
            if (!TestTrue(TEXT("silent UDP reservation binds a real local gameplay port"),
                F.ReserveSilentUdp()))
            { F.Aborted = true; return; }
            const auto Before = F.Direct()->Get_Snapshot();
            const auto WrongEndpoint = FString::Printf(TEXT("127.0.0.2:%d"), F.SilentPort);
            TestFalse(TEXT("wrong-address gameplay target rejects before travel"),
                F.Ip()->Request_JoinFromReadyLobby(WrongEndpoint));
            TestTrue(TEXT("rejected target keeps original Lobby world"), FObjectKey(F.World()) == F.InitialLobbyWorldKey);
            TestTrue(TEXT("rejected target leaves IP helper Offline without bridge"),
                F.Ip()->Get_IsOffline() && !F.Ip()->Get_HasLobbyBridgeContext());
            TestTrue(TEXT("rejected target creates no driver or pending net game"),
                F.World()->GetNetDriver() == nullptr && !F.HasPendingNetGame());
            TestTrue(TEXT("rejected target keeps owned Direct profile"), F.Gym()->Get_IsOwnedProfile());
            TestEqual(TEXT("rejected target leaves Direct revision unchanged"),
                F.Direct()->Get_Snapshot().Get_Revision(), Before.Get_Revision());
            const auto Endpoint = FString::Printf(TEXT("127.0.0.1:%d"), F.SilentPort);
            if (!TestTrue(TEXT("admitted-address gameplay Join issues real ClientTravel"),
                F.Ip()->Request_JoinFromReadyLobby(Endpoint)))
            { F.Aborted = true; return; }
            TestFalse(TEXT("travel request does not report connected gameplay"), F.Ip()->Get_IsConnected());
            TestTrue(TEXT("accepted travel enters IP Joining state"),
                F.Ip()->Get_StateText().StartsWith(TEXT("Joining")));
        }));

    ADD_LATENT_AUTOMATION_COMMAND(FStep(Fixture, TEXT("bounded failed ClientTravel returns to new Offline Lobby world"),
        [](FFixture& F)
        {
            auto* World = F.World();
            auto* Ip = F.Ip();
            if (!IsValid(World) || !World->HasBegunPlay() || FObjectKey(World) == F.InitialLobbyWorldKey ||
                World->GetNetMode() != NM_Standalone || World->GetNetDriver() != nullptr ||
                F.HasPendingNetGame() || !IsValid(Ip) || !Ip->Get_IsOffline())
            { return false; }
            auto* Mode = World->GetAuthGameMode();
            return UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) == MapPath &&
                IsValid(Mode) && Mode->GetClass()->GetPathName() == LobbyModePath;
        }, [this](FFixture& F)
        {
            TestTrue(TEXT("return retains original GameInstance"),
                F.World()->GetGameInstance() == F.GameInstance());
            TestTrue(TEXT("failed travel leaves resident editor map unchanged"),
                F.EditorMapUnchanged());
            TestTrue(TEXT("failed entry observed real pending network travel"), F.SawPendingNetGame);
            TestTrue(TEXT("failed entry observed IP Joining state"), F.SawJoiningState);
            TestTrue(TEXT("failed entry retains owned Direct generation"),
                F.Gym()->Get_IsOwnedProfile() && F.Direct()->Get_IsStarted() &&
                F.Direct()->Get_Snapshot().Get_ServiceGeneration() == F.ClientGeneration);
            TestTrue(TEXT("client remains Joined to live TCP host"),
                F.Direct()->Get_Snapshot().Get_State() == ECk_OnlineDirect_State::Joined &&
                F.Binding.Matches(F.Direct()->Get_Snapshot()));
            TestTrue(TEXT("host still has same two ready members"),
                F.HostSnapshot().Get_State() == ECk_OnlineDirect_State::Hosting &&
                F.HostSnapshot().Get_LobbyEpoch() == F.HostedBeforeTravel.Get_LobbyEpoch() &&
                F.HostSnapshot().Get_Members().Num() == 2);
            ck::test::ip::FLobbyBinding HostBinding;
            TestTrue(TEXT("native host retains the same ready roster and address"),
                ck::test::ip::FLobbyBinding::TryCapture(F.HostSnapshot(), true, HostBinding) &&
                HostBinding.Epoch == F.Binding.Epoch && HostBinding.Members == F.Binding.Members &&
                HostBinding.HostAddress == F.Binding.HostAddress);
            TestTrue(TEXT("return keeps immutable lobby identity and address"),
                F.Direct()->Get_Snapshot().Get_LobbyEpoch() == F.JoinedBeforeTravel.Get_LobbyEpoch() &&
                F.Direct()->Get_Snapshot().Get_LocalMemberId() == F.JoinedBeforeTravel.Get_LocalMemberId() &&
                F.Direct()->Get_Snapshot().Get_HostAddress() == F.JoinedBeforeTravel.Get_HostAddress());
            TestTrue(TEXT("IP helper retains failure context but cannot rejoin stale endpoint"),
                F.Ip()->Get_HasLobbyBridgeContext() && !F.Ip()->Get_CanRejoin());
            TestTrue(TEXT("failed entry exposes an error and preserved-control readback"),
                !F.Ip()->Get_LastError().IsEmpty() &&
                F.Ip()->Get_LobbyBridgeText().Contains(TEXT("preserved")));
            TestEqual(TEXT("no stale gameplay proof was observed"), F.Ip()->Get_ProofSequence(), 0);
            TestFalse(TEXT("returned IP helper is not connected"), F.Ip()->Get_IsConnected());
        }, 35.0));
    ADD_LATENT_AUTOMATION_COMMAND(FEnd(Fixture));
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
