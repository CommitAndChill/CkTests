#include "CkTests/Net/CkNetAutomation_Common.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerState.h" // PlayerState identity (UniqueId/PlayerId) — remote-client<->world correlation
#include "GameFramework/WorldSettings.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealEdGlobals.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkCore/Format/CkFormat.h"

#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkAutoTest_Bridge.h"
#include "CkAutoTest_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"

#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include <StructUtils/InstancedStruct.h>

DEFINE_LOG_CATEGORY_STATIC(LogCkNetAutomation, Log, All);

namespace
{
    // A world may remain in a PIE context while UObject destruction or world
    // teardown has started. Check ordinary validity before reading either flag.
    bool HasUsableWorld(const UWorld* World)
    {
        return ck::IsValid(World) &&
            !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
            !World->bIsTearingDown;
    }

    template <typename TString, typename... TArgs>
    void Log_Error(const TString& Fmt, TArgs&&... Args)
    {
        UE_LOG(LogCkNetAutomation, Error, TEXT("%s"), *ck::Format_UE(Fmt, Forward<TArgs>(Args)...));
    }

    template <typename TString, typename... TArgs>
    void Log_Display(const TString& Fmt, TArgs&&... Args)
    {
        UE_LOG(LogCkNetAutomation, Display, TEXT("%s"), *ck::Format_UE(Fmt, Forward<TArgs>(Args)...));
    }
}

// --------------------------------------------------------------------------------------------------------------------

namespace ck_net_automation_common
{
    bool GSessionFailed = false;
    bool GOwnsSession = false;
    bool GLateJoinPending = false;
    bool GRequireExactTopology = false;
    ENetMode GExpectedServerMode = NM_ListenServer;
    int32 GExpectedTotalWorlds = 0;

    auto FailSession(const FString& InMessage) -> void
    {
        GSessionFailed = true;
        if (auto* Test = FAutomationTestFramework::Get().GetCurrentTest())
        { Test->AddError(InMessage); }
        else
        { Log_Error(TEXT("{}"), InMessage); }
    }

    // ULevelEditorPlaySettings is a config-backed CDO that UEditorEngine::EndPlayMap writes straight to
    // EditorPerProjectUserSettings.ini on EVERY PIE teardown (PlayLevel.cpp, PlaySettingsConfig->SaveConfig).
    // Overriding it in place for a multi-client test therefore leaks into the developer's next manual Play:
    // their client count, net mode and one-process flag come back as whatever the last net test asked for.
    // We snapshot the user's values before overriding and put them back on FEditorDelegates::EndPIE, which
    // the engine broadcasts BEFORE that save — so the file written at teardown holds the user's values and
    // no extra SaveConfig of our own is needed.
    struct FPlaySettings_Snapshot
    {
        int32        _NumClients           = 1;
        EPlayNetMode _NetMode              = EPlayNetMode::PIE_Standalone;
        bool         _RunUnderOneProcess   = true;
        bool         _LaunchSeparateServer = false;
    };

    TOptional<FPlaySettings_Snapshot> GPlaySettings_Snapshot;
    FDelegateHandle                   GPlaySettings_RestoreHandle;
    TWeakObjectPtr<AWorldSettings> GGameMode_WorldSettings;
    TStrongObjectPtr<UClass> GGameMode_OriginalClass;
    bool GHasGameModeOverride = false;

    auto
        Restore_PlaySettings()
        -> void
    {
        GLateJoinPending = false;
        if (GHasGameModeOverride)
        {
            if (auto* WorldSettings = GGameMode_WorldSettings.Get(); ck::IsValid(WorldSettings))
            { WorldSettings->DefaultGameMode = GGameMode_OriginalClass.Get(); }
            GGameMode_WorldSettings.Reset();
            GGameMode_OriginalClass.Reset();
            GHasGameModeOverride = false;
        }
        if (GPlaySettings_RestoreHandle.IsValid())
        {
            FEditorDelegates::EndPIE.Remove(GPlaySettings_RestoreHandle);
            GPlaySettings_RestoreHandle.Reset();
        }

        if (NOT GPlaySettings_Snapshot.IsSet())
        { return; }

        const auto Snapshot = *GPlaySettings_Snapshot;
        GPlaySettings_Snapshot.Reset();

        auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
        if (Settings == nullptr)
        { return; }

        Settings->SetPlayNumberOfClients(Snapshot._NumClients);
        Settings->SetPlayNetMode(Snapshot._NetMode);
        Settings->SetRunUnderOneProcess(Snapshot._RunUnderOneProcess);
        Settings->bLaunchSeparateServer = Snapshot._LaunchSeparateServer;

        Log_Display(TEXT("Restored the editor's Play settings after PIE — number of clients back to [{}]"),
            Snapshot._NumClients);
    }

    // ----------------------------------------------------------------------------------------------------------------

    auto
        OnEndPIE(
            const bool /*InIsSimulating*/)
        -> void
    {
        GLateJoinPending = false;
        Restore_PlaySettings();
    }

    // ----------------------------------------------------------------------------------------------------------------

    auto
        Override_PlaySettings(
            ULevelEditorPlaySettings* InSettings,
            int32 InNumClients,
            bool InDedicatedServer)
        -> void
    {
        // A second start before the matching restore must snapshot the USER's values, not the previous
        // override's — hence the guard rather than an unconditional capture.
        if (NOT GPlaySettings_Snapshot.IsSet())
        {
            auto Snapshot = FPlaySettings_Snapshot{};
            InSettings->GetPlayNumberOfClients(Snapshot._NumClients);
            InSettings->GetPlayNetMode(Snapshot._NetMode);
            InSettings->GetRunUnderOneProcess(Snapshot._RunUnderOneProcess);
            Snapshot._LaunchSeparateServer = InSettings->bLaunchSeparateServer;

            GPlaySettings_Snapshot = Snapshot;
        }

        if (NOT GPlaySettings_RestoreHandle.IsValid())
        { GPlaySettings_RestoreHandle = FEditorDelegates::EndPIE.AddStatic(&OnEndPIE); }

        InSettings->SetPlayNumberOfClients(InDedicatedServer ? InNumClients - 1 : InNumClients);
        InSettings->SetPlayNetMode(InDedicatedServer ? EPlayNetMode::PIE_Client : EPlayNetMode::PIE_ListenServer);
        InSettings->SetRunUnderOneProcess(true);
        InSettings->bLaunchSeparateServer = false;
    }
}

// --------------------------------------------------------------------------------------------------------------------

namespace ck::auto_test::net
{
    auto Get_HasSessionFailed() -> bool
    {
        return ck_net_automation_common::GSessionFailed;
    }

    auto FailSession(const FString& InMessage) -> void
    {
        ck_net_automation_common::FailSession(InMessage);
    }

    auto Get_TopologyTestCases(TArray<FString>& OutNames, TArray<FString>& OutCommands) -> void
    {
        OutNames.Add(TEXT("ListenServer"));
        OutCommands.Add(TEXT("ListenServer"));
        OutNames.Add(TEXT("ClientServer"));
        OutCommands.Add(TEXT("ClientServer"));
    }

    auto TryGet_DedicatedServer(const FString& InTopology, bool& OutDedicatedServer) -> bool
    {
        if (InTopology.IsEmpty() || InTopology == TEXT("ListenServer"))
        {
            OutDedicatedServer = false;
            return true;
        }
        if (InTopology == TEXT("ClientServer"))
        {
            OutDedicatedServer = true;
            return true;
        }
        return false;
    }


    auto
        Get_AllPIEWorlds()
        -> TArray<UWorld*>
    {
        auto Worlds = TArray<UWorld*>{};
        if (GEngine == nullptr)
        { return Worlds; }

        // Server first, then clients in PIEInstance order. The world contexts list isn't
        // sorted by role, so we collect server + clients separately and append.
        auto* ServerWorld = static_cast<UWorld*>(nullptr);
        auto ClientWorlds = TArray<TPair<int32, UWorld*>>{};

        for (const auto& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType != EWorldType::PIE)
            { continue; }

            auto* World = Context.World();
            if (!HasUsableWorld(World))
            { continue; }

            const auto NetMode = World->GetNetMode();
            const auto IsServer = NetMode == NM_ListenServer || NetMode == NM_DedicatedServer;

            if (IsServer && ServerWorld == nullptr)
            { ServerWorld = World; }
            else
            { ClientWorlds.Add({Context.PIEInstance, World}); }
        }

        ClientWorlds.Sort([](const TPair<int32, UWorld*>& A, const TPair<int32, UWorld*>& B) -> bool
        {
            return A.Key < B.Key;
        });

        if (ServerWorld != nullptr)
        { Worlds.Add(ServerWorld); }
        for (const auto& Pair : ClientWorlds)
        { Worlds.Add(Pair.Value); }

        return Worlds;
    }

    // ----------------------------------------------------------------------------------------------------------------

    auto
        Get_ServerWorld()
        -> UWorld*
    {
        if (GEngine == nullptr)
        { return nullptr; }

        for (const auto& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType != EWorldType::PIE)
            { continue; }

            auto* World = Context.World();
            if (!HasUsableWorld(World))
            { continue; }

            const auto NetMode = World->GetNetMode();
            if (NetMode == NM_ListenServer || NetMode == NM_DedicatedServer)
            { return World; }
        }

        return nullptr;
    }

    // ----------------------------------------------------------------------------------------------------------------

    auto
        Get_ClientWorld(
            int32 ClientIdx)
        -> UWorld*
    {
        if (ClientIdx < 0)
        { return nullptr; }

        auto Index = 0;
        for (auto* World : Get_AllPIEWorlds())
        {
            if (World->GetNetMode() != NM_Client)
            { continue; }
            if (Index++ == ClientIdx)
            { return World; }
        }
        return nullptr;
    }

    // ----------------------------------------------------------------------------------------------------------------

    auto
        Get_NumClientWorlds()
        -> int32
    {
        auto Count = 0;
        for (auto* World : Get_AllPIEWorlds())
        {
            if (World->GetNetMode() == NM_Client)
            { ++Count; }
        }
        return Count;
    }

    // ----------------------------------------------------------------------------------------------------------------

    auto
        TryGet_RemoteClientPlayerController(
            UWorld* InServerWorld,
            int32 ClientIdx)
        -> APlayerController*
    {
        if (ClientIdx < 0 || !HasUsableWorld(InServerWorld))
        { return nullptr; }
        if (InServerWorld->WorldType != EWorldType::PIE)
        { return nullptr; }
        const auto ServerMode = InServerWorld->GetNetMode();
        if (ServerMode != NM_ListenServer && ServerMode != NM_DedicatedServer)
        { return nullptr; }
        if (ck_net_automation_common::GOwnsSession &&
            ((ck_net_automation_common::GRequireExactTopology &&
              ServerMode != ck_net_automation_common::GExpectedServerMode) ||
             Get_AllPIEWorlds().Num() != ck_net_automation_common::GExpectedTotalWorlds ||
             Get_NumClientWorlds() != ck_net_automation_common::GExpectedTotalWorlds - 1))
        { return nullptr; }
        auto* ClientWorld = Get_ClientWorld(ClientIdx);
        if (ck::Is_NOT_Valid(ClientWorld))
        { return nullptr; }
        auto* ClientLocalPC = ClientWorld->GetFirstPlayerController();
        if (ck::Is_NOT_Valid(ClientLocalPC) || NOT ClientLocalPC->IsLocalController())
        { return nullptr; }

        // On a listen server, the local player's PlayerController has no UNetConnection; each
        // remote client's PC carries one. Collect the net-connection-backed PCs.
        auto RemotePCs = TArray<APlayerController*>{};
        for (auto It = InServerWorld->GetPlayerControllerIterator(); It; ++It)
        {
            auto* PC = It->Get();
            if (ck::Is_NOT_Valid(PC) || NOT PC->HasActorBegunPlay())
            { continue; }

            if (PC->GetNetConnection() != nullptr)
            { RemotePCs.Add(PC); }
        }

        const auto ExpectedRemoteCount = Get_NumClientWorlds();
        if (RemotePCs.Num() != ExpectedRemoteCount)
        { return nullptr; }
        // The server's PlayerControllerIterator order is NOT correlated to Get_ClientWorld's
        // PIEInstance order. Correlate every result by player identity, even while connecting.
        // The server-side remote PC's PlayerState and the client world's local PC's PlayerState are the same
        // replicated actor, so their identity matches once replicated.
        APlayerState* ClientPS = ClientLocalPC->PlayerState.Get();
        if (ck::Is_NOT_Valid(ClientPS))
        { return nullptr; }

        const auto ClientUniqueId = ClientPS->GetUniqueId();
        const auto ClientPlayerId = ClientPS->GetPlayerId();

        auto* MatchedPC = static_cast<APlayerController*>(nullptr);
        for (auto* PC : RemotePCs)
        {
            APlayerState* ServerPS = PC->PlayerState.Get();
            if (ck::Is_NOT_Valid(ServerPS))
            { return nullptr; }

            // Prefer the replicated unique net id; fall back to the GameMode-assigned PlayerId (int32, always
            // replicated) — a NULL online subsystem in PIE may not populate a valid UniqueId.
            const auto CanCompareUniqueIds = ClientUniqueId.IsValid() && ServerPS->GetUniqueId().IsValid();
            const auto MatchesUniqueId = CanCompareUniqueIds && ServerPS->GetUniqueId() == ClientUniqueId;
            const auto MatchesPlayerId = NOT CanCompareUniqueIds && ClientPlayerId >= 0 &&
                ServerPS->GetPlayerId() == ClientPlayerId;

            if (MatchesUniqueId || MatchesPlayerId)
            {
                if (MatchedPC != nullptr)
                { return nullptr; }
                MatchedPC = PC;
            }
        }
        return MatchedPC;
    }

    auto
        Get_RemoteClientPlayerController(
            UWorld* InServerWorld,
            int32 ClientIdx)
        -> APlayerController*
    {
        auto* Controller = TryGet_RemoteClientPlayerController(InServerWorld, ClientIdx);
        if (Controller == nullptr)
        {
            Log_Error(TEXT("Get_RemoteClientPlayerController: remote client [{}] has no ready, uniquely correlated server controller"),
                ClientIdx);
        }
        return Controller;
    }
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_StartPIEMultiClient::
    Update()
{
    using namespace ck_net_automation_common;
    GSessionFailed = false;
    auto DedicatedServer = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(_Topology, DedicatedServer))
    {
        FailSession(ck::Format_UE(TEXT("Unsupported network topology [{}]"), _Topology));
        return true;
    }
    if (_NumClients < 1 || _NumClients > 64 || (DedicatedServer && _NumClients < 2))
    {
        FailSession(TEXT("Network test requires 1..64 total worlds, and ClientServer requires at least 2"));
        return true;
    }
    if (GUnrealEd == nullptr)
    {
        FailSession(TEXT("Cannot start a network test without GUnrealEd"));
        return true;
    }
    if (GOwnsSession || GUnrealEd->IsPlaySessionInProgress() ||
        GUnrealEd->IsPlaySessionRequestQueued() ||
        NOT ck::auto_test::net::Get_AllPIEWorlds().IsEmpty())
    {
        FailSession(TEXT("Cannot start a network test while a PIE session is already active"));
        return true;
    }
    auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
    if (Settings == nullptr)
    {
        FailSession(TEXT("Cannot resolve editor play settings for the network test"));
        return true;
    }
    if (_GameModeClass.Get() != nullptr)
    {
        const auto ClassIsValid = ck::IsValid(_GameModeClass.Get()) &&
            _GameModeClass->IsChildOf(AGameModeBase::StaticClass());
        if (NOT ClassIsValid)
        {
            FailSession(TEXT("Network test requires a valid GameMode class"));
            return true;
        }
    }
    if (NOT _MapPath.IsEmpty() && NOT FEditorFileUtils::LoadMap(_MapPath, false, false))
    {
        FailSession(ck::Format_UE(TEXT("Network test failed to load map [{}]"), _MapPath));
        return true;
    }

    auto* EditorWorldSettings = static_cast<AWorldSettings*>(nullptr);
    if (_GameModeClass.Get() != nullptr)
    {
        auto* EditorWorld = GUnrealEd->GetEditorWorldContext().World();
        if (ck::Is_NOT_Valid(EditorWorld))
        {
            FailSession(TEXT("Cannot stage an authored GameMode without a valid editor world"));
            return true;
        }
        EditorWorldSettings = EditorWorld->GetWorldSettings();
        if (ck::Is_NOT_Valid(EditorWorldSettings))
        {
            FailSession(TEXT("Cannot stage an authored GameMode without editor WorldSettings"));
            return true;
        }
    }

    GRequireExactTopology = NOT _Topology.IsEmpty();
    GExpectedServerMode = DedicatedServer ? NM_DedicatedServer : NM_ListenServer;
    GExpectedTotalWorlds = _NumClients;
    Override_PlaySettings(Settings, _NumClients, DedicatedServer);
    if (EditorWorldSettings != nullptr)
    {
        GGameMode_WorldSettings = EditorWorldSettings;
        GGameMode_OriginalClass.Reset(EditorWorldSettings->DefaultGameMode.Get());
        GHasGameModeOverride = true;
        EditorWorldSettings->DefaultGameMode = _GameModeClass.Get();
    }

    auto Params = FRequestPlaySessionParams{};
    Params.WorldType = EPlaySessionWorldType::PlayInEditor;
    Log_Display(TEXT("Starting [{}] with [{}] total PIE worlds on [{}]"),
        DedicatedServer ? TEXT("ClientServer") : TEXT("ListenServer"), _NumClients, _MapPath);
    GOwnsSession = true;
    GUnrealEd->RequestPlaySession(Params);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

bool FCk_Latent_RequestLateJoin::Update()
{
    using namespace ck_net_automation_common;
    using namespace ck::auto_test::net;
    const auto Fail = [](const TCHAR* Message) -> bool
    { ck::auto_test::net::FailSession(Message); return true; };
    if (GSessionFailed)
    { return true; }
    if (!FMath::IsFinite(_TimeoutSeconds) || _TimeoutSeconds <= 0.0)
    { return Fail(TEXT("Late join requires a finite positive timeout")); }
    if (!IsValid(GUnrealEd) || !IsValid(GEngine) || !GOwnsSession || !GRequireExactTopology ||
        !GPlaySettings_Snapshot.IsSet() || !GPlaySettings_RestoreHandle.IsValid() ||
        !GUnrealEd->IsPlayingSessionInEditor() || GUnrealEd->IsSimulatingInEditor() ||
        GUnrealEd->IsPlaySessionRequestQueued())
    { return Fail(TEXT("Late join requires the harness-owned active exact PIE session")); }
    const auto Worlds = Get_AllPIEWorlds();
    auto* Server = Get_ServerWorld();
    if (!IsValid(Server) || Server->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Server->GetNetMode() != GExpectedServerMode)
    { return Fail(TEXT("Late join lost the admitted server topology")); }
    const auto PIEInstance = [](UWorld* World) -> int32
    {
        for (const auto& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE && Context.World() == World)
            { return Context.PIEInstance; }
        }
        return INDEX_NONE;
    };
    const auto PlayerReady = [](APlayerController* PC, UWorld* World) -> bool
    {
        if (!IsValid(PC) || PC->IsActorBeingDestroyed() || PC->GetWorld() != World || !PC->HasActorBegunPlay())
        { return false; }
        APawn* Pawn = PC->GetPawn();
        auto* PS = PC->PlayerState.Get();
        return IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->GetWorld() == World &&
            Pawn->HasActorBegunPlay() && Pawn->GetController() == PC &&
            IsValid(PS) && !PS->IsActorBeingDestroyed() && PS->GetWorld() == World && PS->HasActorBegunPlay() &&
            Pawn->GetPlayerState() == PS;
    };
    if (!_Requested)
    {
        if (GLateJoinPending || GExpectedTotalWorlds < 2 || GExpectedTotalWorlds >= 64 ||
            Worlds.Num() != GExpectedTotalWorlds || Get_NumClientWorlds() != GExpectedTotalWorlds - 1)
        { return Fail(TEXT("Late join admission requires the exact original world count and no pending join")); }
        TArray<FWorldIdentity> Candidate;
        TArray<TWeakObjectPtr<APlayerController>> Remote;
        int32 LastInstance = INDEX_NONE;
        for (int32 I = 0; I < Worlds.Num(); ++I)
        {
            auto* World = Worlds[I];
            if (!IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
                !World->HasBegunPlay() || (I > 0 && World->GetNetMode() != NM_Client))
            { return Fail(TEXT("Late join admission found an invalid original world")); }
            FWorldIdentity Identity;
            Identity.World = World; Identity.Mode = World->GetNetMode(); Identity.PIEInstance = PIEInstance(World);
            if (Identity.PIEInstance < 0)
            { return Fail(TEXT("Late join admission has no original PIE instance identity")); }
            LastInstance = FMath::Max(LastInstance, Identity.PIEInstance);
            for (auto It = World->GetPlayerControllerIterator(); It; ++It)
            {
                auto* PC = It->Get();
                if (!PlayerReady(PC, World))
                { return Fail(TEXT("Late join admission requires ready original controllers, pawns and PlayerStates")); }
                Identity.Players.Add({PC, PC->GetPawn(), PC->PlayerState.Get()});
            }
            const auto ExpectedPlayers = I == 0 ? Worlds.Num() - (GExpectedServerMode == NM_DedicatedServer ? 1 : 0) : 1;
            if (Identity.Players.Num() != ExpectedPlayers)
            { return Fail(TEXT("Late join admission found an unexpected original player census")); }
            if (I > 0)
            {
                auto* PC = World->GetFirstPlayerController();
                auto* GI = World->GetGameInstance();
                auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
                if (!IsValid(PC) || !PC->IsLocalController() || !IsValid(GI) || GI->GetWorld() != World ||
                    GI->GetLocalPlayers().Num() != 1 || !IsValid(LP) || GI->GetLocalPlayers()[0] != LP ||
                    LP->GetPlayerController(World) != PC)
                { return Fail(TEXT("Late join admission requires exact original local player ownership")); }
            }
            Candidate.Add(MoveTemp(Identity));
            if (I > 0)
            {
                auto* PC = TryGet_RemoteClientPlayerController(Server, I - 1);
                if (!PlayerReady(PC, Server) || Remote.Contains(PC))
                { return Fail(TEXT("Late join admission requires uniquely correlated original remote players")); }
                Remote.Add(PC);
            }
        }
        auto* Mode = Server->GetAuthGameMode();
        if (!IsValid(Mode))
        { return Fail(TEXT("Late join admission requires the original authority GameMode")); }
        _Worlds = MoveTemp(Candidate); _RemoteControllers = MoveTemp(Remote);
        _OldCount = Worlds.Num(); _LastPIEInstance = LastInstance;
        _Editor = GUnrealEd; _GameMode = Mode; _StartTime = FPlatformTime::Seconds();
        // All admission prerequisites passed. Publish exactly one extra expected client before
        // requesting the engine's deferred production creation. No settings or controllers are forged.
        GExpectedTotalWorlds = _OldCount + 1;
        GLateJoinPending = true; _Requested = true;
        GUnrealEd->RequestLateJoin();
        return false;
    }
    if (!GLateJoinPending || _Editor.Get() != GUnrealEd || GExpectedTotalWorlds != _OldCount + 1 ||
        _Worlds.Num() != _OldCount || _Worlds[0].World.Get() != Server ||
        !_GameMode.IsValid() || Server->GetAuthGameMode() != _GameMode.Get())
    { return Fail(TEXT("Late join lost its captured session or authority identity")); }
    if (Worlds.Num() < _OldCount || Worlds.Num() > _OldCount + 1)
    { return Fail(TEXT("Late join observed an unexpected world census")); }
    for (int32 I = 0; I < _Worlds.Num(); ++I)
    {
        const auto& Original = _Worlds[I];
        auto* World = Original.World.Get();
        if (!IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !World->HasBegunPlay() ||
            Worlds[I] != World || World->GetNetMode() != Original.Mode || PIEInstance(World) != Original.PIEInstance)
        { return Fail(TEXT("Late join replaced or reordered an original world")); }
        int32 Count = 0;
        for (auto It = World->GetPlayerControllerIterator(); It; ++It)
        { ++Count; }
        if ((I > 0 && Count != Original.Players.Num()) ||
            (I == 0 && (Count < Original.Players.Num() || Count > Original.Players.Num() + 1)))
        { return Fail(TEXT("Late join changed an original controller cohort")); }
        for (const auto& Player : Original.Players)
        {
            auto* PC = Player.Controller.Get();
            if (!PlayerReady(PC, World) || !Player.Pawn.IsValid() || !Player.PlayerState.IsValid() ||
                PC->GetPawn() != Player.Pawn.Get() || PC->PlayerState.Get() != Player.PlayerState.Get())
            { return Fail(TEXT("Late join replaced an original controller, pawn or PlayerState")); }
        }
        if (I > 0)
        {
            auto* PC = World->GetFirstPlayerController();
            auto* GI = World->GetGameInstance();
            auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
            if (!IsValid(PC) || !PC->IsLocalController() || !IsValid(GI) || GI->GetWorld() != World ||
                GI->GetLocalPlayers().Num() != 1 || !IsValid(LP) || GI->GetLocalPlayers()[0] != LP ||
                LP->GetPlayerController(World) != PC)
            { return Fail(TEXT("Late join lost original local player ownership")); }
        }
    }
    bool Ready = Worlds.Num() == _OldCount + 1 && Get_NumClientWorlds() == _OldCount;
    if (Ready)
    {
        auto* Added = Worlds.Last();
        if (!IsValid(Added) || Added->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            PIEInstance(Added) <= _LastPIEInstance || (Added->HasBegunPlay() && Added->GetNetMode() != NM_Client))
        { return Fail(TEXT("Late join did not add exactly one later PIE client")); }
        auto* PC = Added->GetFirstPlayerController();
        auto* GI = Added->GetGameInstance();
        auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
        int32 AddedPCs = 0;
        for (auto It = Added->GetPlayerControllerIterator(); It; ++It)
        { ++AddedPCs; }
        if (AddedPCs > 1)
        { return Fail(TEXT("Late join added an unexpected local player cohort")); }
        Ready = Added->HasBegunPlay() && Added->GetNetMode() == NM_Client && AddedPCs == 1 &&
            PlayerReady(PC, Added) && PC->IsLocalController() &&
            IsValid(GI) && GI->GetWorld() == Added && GI->GetLocalPlayers().Num() == 1 &&
            IsValid(LP) && GI->GetLocalPlayers()[0] == LP && LP->GetPlayerController(Added) == PC;
        TArray<APlayerController*> Correlated;
        for (int32 I = 0; Ready && I < _OldCount; ++I)
        {
            auto* Remote = TryGet_RemoteClientPlayerController(Server, I);
            Ready = PlayerReady(Remote, Server) && Remote->GetNetConnection() != nullptr &&
                !Remote->IsLocalController() && !Correlated.Contains(Remote);
            if (Ready && I < _RemoteControllers.Num() && _RemoteControllers[I].Get() != Remote)
            { return Fail(TEXT("Late join changed an original remote player correlation")); }
            if (Ready)
            { Correlated.Add(Remote); }
        }
    }
    if (Ready)
    { GLateJoinPending = false; return true; }
    if (FPlatformTime::Seconds() - _StartTime > _TimeoutSeconds)
    { return Fail(TEXT("Late join timed out waiting for normal client possession and unique server correlation")); }
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_WaitForPIEReady::
    Update()
{
    if (ck_net_automation_common::GSessionFailed)
    { return true; }

    if (_StartTime < 0.0)
    { _StartTime = FPlatformTime::Seconds(); }

    const auto PIEWorlds = ck::auto_test::net::Get_AllPIEWorlds();
    const auto* Server = ck::auto_test::net::Get_ServerWorld();
    const auto ClientCount = ck::auto_test::net::Get_NumClientWorlds();
    bool bReady = PIEWorlds.Num() >= _ExpectedTotalWorlds;
    if (ck_net_automation_common::GRequireExactTopology)
    {
        bReady = bReady && PIEWorlds.Num() == _ExpectedTotalWorlds &&
            IsValid(Server) && Server->GetNetMode() == ck_net_automation_common::GExpectedServerMode &&
            ClientCount == _ExpectedTotalWorlds - 1;
    }

    // All worlds present - confirm BeginPlay has fired on each.
    for (auto* World : PIEWorlds)
    {
        if (!IsValid(World) || !World->HasBegunPlay())
        { bReady = false; }
    }

    const auto Elapsed = FPlatformTime::Seconds() - _StartTime;
    if (bReady)
    {
        Log_Display(TEXT("FCk_Latent_WaitForPIEReady: [{}] PIE world(s) ready after [{}]s"),
            PIEWorlds.Num(), Elapsed);
        return true;
    }
    if (Elapsed <= _TimeoutSeconds)
    { return false; }

    const auto ModeName = [](ENetMode InMode) -> const TCHAR*
    {
        switch (InMode)
        {
        case NM_Standalone: return TEXT("Standalone");
        case NM_DedicatedServer: return TEXT("DedicatedServer");
        case NM_ListenServer: return TEXT("ListenServer");
        case NM_Client: return TEXT("Client");
        default: return TEXT("unknown");
        }
    };
    TArray<FString> WorldStates;
    for (int32 Index = 0; Index < PIEWorlds.Num(); ++Index)
    {
        const auto* World = PIEWorlds[Index];
        WorldStates.Add(FString::Printf(TEXT("%d:%s mode=%s BeginPlay=%s"), Index,
            *GetNameSafe(World), IsValid(World) ? ModeName(World->GetNetMode()) : TEXT("missing"),
            IsValid(World) && World->HasBegunPlay() ? TEXT("yes") : TEXT("no")));
    }
    ck_net_automation_common::FailSession(ck::Format_UE(
        TEXT("PIE readiness timed out after [{}]s: expected [{}] worlds, observed [{}]; ")
        TEXT("expected server mode [{}], actual server mode [{}], clients [{}]; worlds [{}]"),
        _TimeoutSeconds, _ExpectedTotalWorlds, PIEWorlds.Num(),
        ck_net_automation_common::GRequireExactTopology
            ? ModeName(ck_net_automation_common::GExpectedServerMode) : TEXT("any"),
        IsValid(Server) ? ModeName(Server->GetNetMode()) : TEXT("missing"),
        ClientCount, FString::Join(WorldStates, TEXT("; "))));
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_WaitForCondition::
    Update()
{
    if (ck_net_automation_common::GLateJoinPending && !ck_net_automation_common::GSessionFailed)
    { return false; }
    if (ck_net_automation_common::GSessionFailed)
    { return true; }

    if (_StartTime < 0.0)
    { _StartTime = FPlatformTime::Seconds(); }

    if (NOT _Predicate.IsBound())
    {
        ck_net_automation_common::FailSession(TEXT("FCk_Latent_WaitForCondition: unbound predicate"));
        return true;
    }
    if (_Predicate.Execute())
    { return true; }

    const auto Elapsed = FPlatformTime::Seconds() - _StartTime;
    if (Elapsed > _TimeoutSeconds)
    {
        ck_net_automation_common::FailSession(ck::Format_UE(
            TEXT("FCk_Latent_WaitForCondition: timed out after [{}]s"), _TimeoutSeconds));
        return true;
    }

    return false;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_TickWorlds::
    Update()
{
    if (ck_net_automation_common::GSessionFailed)
    { return true; }

    // The engine ticks PIE worlds normally between latent command Update() calls — this command
    // is effectively a "wait N frames" so replication can converge. Per-world Tick is intentionally
    // not invoked here; doing so would double-tick and break per-frame invariants in CkFoundation
    // processors that rely on stable DeltaT.
    ++_FramesElapsed;
    return _FramesElapsed >= _NumFrames;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_RunOnServer::
    Update()
{
    if (ck_net_automation_common::GLateJoinPending && !ck_net_automation_common::GSessionFailed)
    { return false; }
    if (ck_net_automation_common::GSessionFailed)
    { return true; }

    auto* Server = ck::auto_test::net::Get_ServerWorld();
    if (Server == nullptr)
    {
        ck_net_automation_common::FailSession(
            TEXT("FCk_Latent_RunOnServer: no server world available"));
        return true;
    }

    if (NOT _Action.IsBound())
    { ck_net_automation_common::FailSession(TEXT("FCk_Latent_RunOnServer: unbound action")); return true; }
    _Action.Execute(Server);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_RunOnClient::
    Update()
{
    if (ck_net_automation_common::GLateJoinPending && !ck_net_automation_common::GSessionFailed)
    { return false; }
    if (ck_net_automation_common::GSessionFailed)
    { return true; }

    auto* Client = ck::auto_test::net::Get_ClientWorld(_ClientIdx);
    if (Client == nullptr)
    {
        ck_net_automation_common::FailSession(ck::Format_UE(
            TEXT("FCk_Latent_RunOnClient: no client world at index [{}]"), _ClientIdx));
        return true;
    }

    if (NOT _Action.IsBound())
    { ck_net_automation_common::FailSession(TEXT("FCk_Latent_RunOnClient: unbound action")); return true; }
    _Action.Execute(Client);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_AssertCondition::
    Update()
{
    if (ck_net_automation_common::GSessionFailed && NOT _RunAfterSessionFailure)
    { return true; }
    if (ck_net_automation_common::GLateJoinPending && !ck_net_automation_common::GSessionFailed)
    { return false; }
    if (_Test == nullptr)
    { return true; }

    if (_Assertion.IsBound() == false)
    {
        _Test->AddError(ck::Format_UE(TEXT("FCk_Latent_AssertCondition [{}]: unbound assertion delegate"), _Message));
        return true;
    }

    const auto Passed = _Assertion.Execute();
    if (Passed == false)
    { _Test->AddError(ck::Format_UE(TEXT("FCk_Latent_AssertCondition failed: {}"), _Message)); }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_WaitUntil::
    Update()
{
    if (ck_net_automation_common::GLateJoinPending && !ck_net_automation_common::GSessionFailed)
    { return false; }
    if (ck_net_automation_common::GSessionFailed)
    { return true; }

    if (_StartTime < 0.0)
    { _StartTime = FPlatformTime::Seconds(); }

    if (_Condition.IsBound() == false)
    {
        ck_net_automation_common::FailSession(ck::Format_UE(
            TEXT("FCk_Latent_WaitUntil [{}]: unbound condition delegate"), _Message));
        return true;
    }

    // Poll the condition every frame — the engine ticks the PIE worlds between Update() calls, so
    // returning false re-checks on the next frame (replication advances in the interim).
    if (_Condition.Execute())
    {
        Log_Display(TEXT("FCk_Latent_WaitUntil: condition met after [{}]s — {}"),
            FPlatformTime::Seconds() - _StartTime, _Message);
        return true;
    }

    const auto Elapsed = FPlatformTime::Seconds() - _StartTime;
    if (Elapsed > _TimeoutSeconds)
    {
        ck_net_automation_common::FailSession(ck::Format_UE(
            TEXT("FCk_Latent_WaitUntil: timed out after [{}]s waiting for: {}"),
            _TimeoutSeconds, _Message));
        return true;
    }

    return false;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_EndPIE::
    Update()
{
    if (NOT ck_net_automation_common::GOwnsSession)
    {
        ck_net_automation_common::GSessionFailed = false;
        return true;
    }

    if (GUnrealEd == nullptr)
    {
        ck_net_automation_common::Restore_PlaySettings();
        ck_net_automation_common::GOwnsSession = false;
        ck_net_automation_common::GSessionFailed = false;
        return true;
    }

    if (_Requested == false)
    {
        Log_Display(TEXT("FCk_Latent_EndPIE: requesting end of PIE session."));
        // Cancel only a queued startup. CancelRequestPlaySession also clears the active
        // session's window state, which EndPlayMap still needs during viewport teardown.
        if (NOT GUnrealEd->IsPlayingSessionInEditor() && GUnrealEd->IsPlaySessionRequestQueued())
        { GUnrealEd->CancelRequestPlaySession(); }
        if (GUnrealEd->IsPlayingSessionInEditor())
        { GUnrealEd->RequestEndPlayMap(); }
        _Requested = true;
        return false;
    }

    // Wait until all PIE worlds are gone before returning — otherwise the next test inherits
    // half-torn-down state.
    if (GUnrealEd->IsPlaySessionInProgress() || GUnrealEd->IsPlaySessionRequestQueued() ||
        NOT ck::auto_test::net::Get_AllPIEWorlds().IsEmpty())
    { return false; }

    // FEditorDelegates::EndPIE has normally restored the Play settings already. This covers the paths
    // where the requested session never started, so that broadcast never happened.
    ck_net_automation_common::Restore_PlaySettings();
    ck_net_automation_common::GOwnsSession = false;
    ck_net_automation_common::GSessionFailed = false;
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

bool
    FCk_Latent_RunAsTestOnAllWorlds::
    Update()
{
    const auto Cleanup = [this]() -> void
    {
        for (auto& Body : _SpawnedBodies)
        {
            if (ck::IsValid(Body))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Body); }
        }
        _SpawnedBodies.Reset();
        _BodyCapturers.Reset();
        _Worlds.Reset();
    };
    const auto Fail = [&Cleanup](const FString& InMessage) -> bool
    {
        ck_net_automation_common::FailSession(InMessage);
        Cleanup();
        return true;
    };

    if (ck_net_automation_common::GSessionFailed)
    { Cleanup(); return true; }
    if (_Test == nullptr)
    { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: test context was lost.")); }

    if (ck_net_automation_common::GLateJoinPending)
    { return false; }

    // ---- Phase 1: spawn (first call only) -----------------------------------------------------------------------

    if (NOT _Spawned)
    {
        _Spawned = true;
        _StartTime = FPlatformTime::Seconds();

        const auto AsClassPath = FSoftClassPath{_ClassPath};
        auto* AsClass = AsClassPath.TryLoadClass<UCk_EntityScript_UE>();
        if (NOT ck::IsValid(AsClass) ||
            AsClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: missing, stale, or non-instantiable AS class at [{}]."),
                _ClassPath));
        }
        const auto TestBasePath = FSoftClassPath{TEXT("/Script/Angelscript.Ck_AutoTest_Base")};
        auto* TestBase = TestBasePath.TryLoadClass<UCk_EntityScript_UE>();
        if (NOT ck::IsValid(TestBase) || AsClass == TestBase || NOT AsClass->IsChildOf(TestBase))
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: class [{}] must derive from UCk_AutoTest_Base."),
                _ClassPath));
        }
        auto* AsCdo = AsClass->GetDefaultObject<UCk_EntityScript_UE>();
        if (NOT ck::IsValid(AsCdo))
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: AS class [{}] has no valid default object."),
                _ClassPath));
        }
        if (AsCdo->Get_EffectiveReplication() != ECk_Replication::DoesNotReplicate)
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: per-world AS test body [{}] must not replicate."),
                _ClassPath));
        }

        const auto PieWorlds = ck::auto_test::net::Get_AllPIEWorlds();
        if (PieWorlds.IsEmpty())
        { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: no PIE worlds available — PIE not started?")); }

        if (NOT ck_net_automation_common::GOwnsSession ||
            NOT ck_net_automation_common::GRequireExactTopology ||
            ck_net_automation_common::GExpectedTotalWorlds < 2 ||
            PieWorlds.Num() != ck_net_automation_common::GExpectedTotalWorlds ||
            ck::auto_test::net::Get_NumClientWorlds() != ck_net_automation_common::GExpectedTotalWorlds - 1)
        { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: exact active PIE session census is unavailable.")); }
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (NOT IsValid(Server) || PieWorlds[0] != Server ||
            (ck_net_automation_common::GExpectedServerMode != NM_ListenServer &&
             ck_net_automation_common::GExpectedServerMode != NM_DedicatedServer) ||
            Server->GetNetMode() != ck_net_automation_common::GExpectedServerMode)
        { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: exact authority world or mode is invalid.")); }
        auto SeenWorlds = TSet<UWorld*>{};
        for (auto Index = 0; Index < PieWorlds.Num(); ++Index)
        {
            auto* World = PieWorlds[Index];
            if (NOT IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
                NOT World->HasBegunPlay() || SeenWorlds.Contains(World) ||
                (Index == 0 ? World != Server : World->GetNetMode() != NM_Client))
            { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: exact PIE world role, lifetime or identity is invalid.")); }
            SeenWorlds.Add(World);
        }

        // Preflight every owner before requesting the first body. A missing later world must not
        // leave an earlier world's test running as a successful subset.
        auto Owners = TArray<FCk_Handle>{};
        Owners.Reserve(PieWorlds.Num());
        for (auto Index = 0; Index < PieWorlds.Num(); ++Index)
        {
            auto* World = PieWorlds[Index];
            if (NOT IsValid(World))
            {
                return Fail(ck::Format_UE(
                    TEXT("FCk_Latent_RunAsTestOnAllWorlds: PIE world [{}] is missing or stale."), Index));
            }

            auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            if (NOT ck::IsValid(Owner) || NOT UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Owner))
            {
                return Fail(ck::Format_UE(
                    TEXT("FCk_Latent_RunAsTestOnAllWorlds: world [{}] has no admissible TransientEntity."),
                    World->GetName()));
            }
            Owners.Add(Owner);
        }

        const auto* ModeEnum = StaticEnum<ECk_AutoTest_NetMode>();
        const auto* ModeProperty = AsClass->FindPropertyByName(TEXT("_NetMode"));
        if (ModeEnum == nullptr || ModeProperty == nullptr)
        { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: body network mode schema is missing.")); }
        uint8 RawMode = 255;
        if (const auto* ByteProperty = CastField<FByteProperty>(ModeProperty))
        {
            if (ByteProperty->Enum != ModeEnum)
            { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: body network mode has the wrong enum.")); }
            RawMode = ByteProperty->GetPropertyValue_InContainer(AsCdo);
        }
        else if (const auto* EnumProperty = CastField<FEnumProperty>(ModeProperty))
        {
            const auto* Underlying = CastField<FByteProperty>(EnumProperty->GetUnderlyingProperty());
            if (EnumProperty->GetEnum() != ModeEnum || Underlying == nullptr)
            { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: body network mode enum schema is invalid.")); }
            RawMode = Underlying->GetPropertyValue_InContainer(
                EnumProperty->ContainerPtrToValuePtr<void>(AsCdo));
        }
        else
        { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: body network mode property is not a byte enum.")); }
        if (RawMode > static_cast<uint8>(ECk_AutoTest_NetMode::Replicated))
        { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: body network mode value is unknown.")); }
        const auto ActualMode = static_cast<ECk_AutoTest_NetMode>(RawMode);
        if (_RequireNoNetSubject != (ActualMode == ECk_AutoTest_NetMode::ServerAndClientsIndependent))
        { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: stub no-subject policy disagrees with actual body mode.")); }

        if (_RequireNoNetSubject)
        {
            for (auto Index = 0; Index < PieWorlds.Num(); ++Index)
            {
                for (TActorIterator<ACk_AutoTest_NetSubject> It(PieWorlds[Index]); It; ++It)
                {
                    return Fail(ck::Format_UE(
                        TEXT("FCk_Latent_RunAsTestOnAllWorlds: independent world [{}] has a NetSubject before body admission."),
                        PieWorlds[Index]->GetName()));
                }
            }
            auto WorldNames = FString{};
            for (auto* World : PieWorlds)
            {
                if (NOT WorldNames.IsEmpty())
                { WorldNames += TEXT(", "); }
                WorldNames += World->GetName();
            }
            Log_Display(TEXT("FCk_Latent_RunAsTestOnAllWorlds: Independent mode preflight passed; [{}] exact PIE worlds [{}] have zero NetSubject actors before body admission."), PieWorlds.Num(), WorldNames);
        }

        _ExpectedBodyCount = PieWorlds.Num();
        _Worlds.Reserve(_ExpectedBodyCount);
        _BodyCapturers.Reserve(_ExpectedBodyCount);
        _SpawnedBodies.Reserve(_ExpectedBodyCount);
        for (auto Index = 0; Index < _ExpectedBodyCount; ++Index)
        {
            _Worlds.Add(TWeakObjectPtr<UWorld>{PieWorlds[Index]});
            auto Capturer = TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE>{NewObject<UCk_AutoTest_BodyCapturer_UE>()};
            if (NOT Capturer.IsValid())
            { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: could not create body capturer.")); }

            _BodyCapturers.Add(MoveTemp(Capturer));
        }

        for (auto Index = 0; Index < _ExpectedBodyCount; ++Index)
        {
            auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Owners[Index], AsClass, FInstancedStruct{}, {});
            if (NOT UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))
            {
                return Fail(ck::Format_UE(
                    TEXT("FCk_Latent_RunAsTestOnAllWorlds: spawn request rejected for world [{}]."),
                    PieWorlds[Index]->GetName()));
            }
            _SpawnedBodies.Add(Pending.Get_EntityUnderConstruction());

            auto OnConstructed = FCk_Delegate_EntityScript_Constructed{};
            OnConstructed.BindDynamic(_BodyCapturers[Index].Get(), &UCk_AutoTest_BodyCapturer_UE::OnBodyConstructed);
            UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, OnConstructed);
        }
        return false;
    }

    const auto Elapsed = FPlatformTime::Seconds() - _StartTime;
    if (Elapsed > _TimeoutSeconds)
    {
        auto CapturedCount = 0;
        for (const auto& Capturer : _BodyCapturers)
        { CapturedCount += Capturer.IsValid() ? Capturer->_CapturedBodies.Num() : 0; }
        return Fail(ck::Format_UE(
            TEXT("FCk_Latent_RunAsTestOnAllWorlds: timed out after [{}]s. Captured [{}/{}] bodies."),
            _TimeoutSeconds, CapturedCount, _ExpectedBodyCount));
    }

    const auto CurrentWorlds = ck::auto_test::net::Get_AllPIEWorlds();
    if (CurrentWorlds.Num() != _ExpectedBodyCount || _BodyCapturers.Num() != _ExpectedBodyCount ||
        _Worlds.Num() != _ExpectedBodyCount || _SpawnedBodies.Num() != _ExpectedBodyCount)
    { return Fail(TEXT("FCk_Latent_RunAsTestOnAllWorlds: PIE worlds changed before all bodies finished.")); }

    for (auto Index = 0; Index < _ExpectedBodyCount; ++Index)
    {
        if (NOT _Worlds[Index].IsValid() || NOT IsValid(CurrentWorlds[Index]) ||
            CurrentWorlds[Index] != _Worlds[Index].Get())
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: PIE world [{}] disappeared or changed."), Index));
        }
    }
    if (_RequireNoNetSubject)
    {
        for (auto Index = 0; Index < _ExpectedBodyCount; ++Index)
        {
            for (TActorIterator<ACk_AutoTest_NetSubject> It(CurrentWorlds[Index]); It; ++It)
            {
                return Fail(ck::Format_UE(
                    TEXT("FCk_Latent_RunAsTestOnAllWorlds: independent world [{}] gained a NetSubject while bodies ran."),
                    CurrentWorlds[Index]->GetName()));
            }
        }
    }

    auto AllTerminal = true;
    for (auto Index = 0; Index < _ExpectedBodyCount; ++Index)
    {
        const auto& Capturer = _BodyCapturers[Index];
        if (NOT Capturer.IsValid() || Capturer->_CapturedBodies.Num() > 1)
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: world [{}] has invalid body-capture state."), Index));
        }
        const auto& PendingBody = _SpawnedBodies[Index];
        const auto BodyReady = ck::IsValid(PendingBody) &&
            NOT UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                PendingBody, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            NOT UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                PendingBody, ECk_EntityLifetime_DestructionPhase::Teardown) &&
            NOT UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                PendingBody, ECk_EntityLifetime_DestructionPhase::Destroyed);
        if (NOT BodyReady)
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: world [{}] lost its admitted body."), Index));
        }
        if (Capturer->_CapturedBodies.IsEmpty())
        { AllTerminal = false; continue; }

        const auto& Body = Capturer->_CapturedBodies[0];
        if (NOT ck::IsValid(Body) || Body != PendingBody)
        {
            return Fail(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: world [{}] constructed a stale or foreign body."), Index));
        }
        if (NOT UCk_Utils_AutoTest_UE::Has_Result(Body))
        { AllTerminal = false; continue; }

        const auto Result = UCk_Utils_AutoTest_UE::Get_Result(Body);
        switch (Result.Status)
        {
            case ECk_AutoTest_Status::Pending:
            case ECk_AutoTest_Status::Running:
                AllTerminal = false;
                break;
            case ECk_AutoTest_Status::Passed:
                break;
            case ECk_AutoTest_Status::Failed:
            case ECk_AutoTest_Status::TimedOut:
            {
                const auto WorldLabel = (Index == 0)
                    ? FString{TEXT("Server")}
                    : ck::Format_UE(TEXT("Client[{}]"), Index - 1);
                return Fail(ck::Format_UE(
                    TEXT("[{}] {} ({}/{} assertions failed)"),
                    WorldLabel, Result.FailureMessage, Result.AssertionsFailed, Result.AssertionsRun));
            }
            default:
                return Fail(ck::Format_UE(
                    TEXT("FCk_Latent_RunAsTestOnAllWorlds: world [{}] reported an invalid result status."), Index));
        }
    }

    if (NOT AllTerminal)
    { return false; }

    if (_RequireNoNetSubject)
    {
        auto WorldNames = FString{};
        for (auto* World : CurrentWorlds)
        {
            if (NOT WorldNames.IsEmpty())
            { WorldNames += TEXT(", "); }
            WorldNames += World->GetName();
        }
        Log_Display(TEXT("FCk_Latent_RunAsTestOnAllWorlds: Independent mode final census passed; [{}] exact PIE worlds [{}] have zero NetSubject actors after all bodies passed."), _ExpectedBodyCount, WorldNames);
    }
    Log_Display(TEXT("FCk_Latent_RunAsTestOnAllWorlds: all [{}] worlds passed in [{}]s."),
        _ExpectedBodyCount, Elapsed);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
