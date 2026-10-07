#include "CkTests/Net/CkNetAutomation_Common.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h" // PlayerState identity (UniqueId/PlayerId) — remote-client<->world correlation
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealEdGlobals.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkCore/Format/CkFormat.h"

#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkAutoTest_Bridge.h"
#include "CkAutoTest_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject_Utils.h"

#include "UObject/SoftObjectPath.h"
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
    bool GRequireExactTopology = false;
    ENetMode GExpectedServerMode = NM_ListenServer;
    int32 GExpectedTotalWorlds = 0;

    auto FailSession(const FString& Message) -> void
    {
        GSessionFailed = true;
        if (auto* Test = FAutomationTestFramework::Get().GetCurrentTest())
        { Test->AddError(Message); }
        else { Log_Error(TEXT("{}"), Message); }
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
    { return ck_net_automation_common::GSessionFailed; }

    auto FailSession(const FString& InMessage) -> void
    { ck_net_automation_common::FailSession(InMessage); }

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
    bool DedicatedServer = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(_Topology, DedicatedServer))
    { FailSession(ck::Format_UE(TEXT("Unsupported network topology [{}]"), _Topology)); return true; }
    if (_NumClients < 1 || _NumClients > 64 || (DedicatedServer && _NumClients < 2))
    { FailSession(TEXT("Network test requires 1..64 total worlds; ClientServer requires at least 2")); return true; }
    if (GUnrealEd == nullptr)
    { FailSession(TEXT("Cannot start network test without GUnrealEd")); return true; }
    if (GOwnsSession || GUnrealEd->IsPlaySessionInProgress() ||
        GUnrealEd->IsPlaySessionRequestQueued() ||
        !ck::auto_test::net::Get_AllPIEWorlds().IsEmpty())
    { FailSession(TEXT("Cannot start network test while PIE is active")); return true; }
    auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
    if (Settings == nullptr)
    { FailSession(TEXT("Cannot resolve editor play settings")); return true; }
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
    if (!_MapPath.IsEmpty() && !FEditorFileUtils::LoadMap(_MapPath, false, false))
    { FailSession(ck::Format_UE(TEXT("Failed to load network test map [{}]"), _MapPath)); return true; }
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
    GRequireExactTopology = !_Topology.IsEmpty();
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
    FRequestPlaySessionParams Params{};
    Params.WorldType = EPlaySessionWorldType::PlayInEditor;
    GOwnsSession = true;
    GUnrealEd->RequestPlaySession(Params);
    return true;
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
    if (ck_net_automation_common::GSessionFailed) { return true; }
    if (_Test == nullptr)
    { return true; }

    // ---- Phase 1: spawn (first call only) -----------------------------------------------------------------------

    if (NOT _Spawned)
    {
        _Spawned = true;
        _StartTime = FPlatformTime::Seconds();

        const auto AsClassPath = FSoftClassPath{_ClassPath};
        auto* AsClass = AsClassPath.TryLoadClass<UCk_EntityScript_UE>();
        if (AsClass == nullptr)
        {
            _Test->AddError(ck::Format_UE(
                TEXT("FCk_Latent_RunAsTestOnAllWorlds: could not load AS class at [{}] — check the path string."),
                _ClassPath));
            return true;
        }

        const auto PieWorlds = ck::auto_test::net::Get_AllPIEWorlds();
        if (PieWorlds.IsEmpty())
        {
            _Test->AddError(TEXT("FCk_Latent_RunAsTestOnAllWorlds: no PIE worlds available — PIE not started?"));
            return true;
        }

        _BodyCapturer = TStrongObjectPtr{NewObject<UCk_AutoTest_BodyCapturer_UE>()};
        _ExpectedBodyCount = PieWorlds.Num();

        for (auto* World : PieWorlds)
        {
            auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
            if (ck::Is_NOT_Valid(TransientEntity))
            {
                _Test->AddError(ck::Format_UE(
                    TEXT("FCk_Latent_RunAsTestOnAllWorlds: world [{}] has no valid TransientEntity."),
                    World->GetName()));
                continue;
            }

            auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                TransientEntity, AsClass, FInstancedStruct{}, {});

            auto OnConstructed = FCk_Delegate_EntityScript_Constructed{};
            OnConstructed.BindDynamic(_BodyCapturer.Get(), &UCk_AutoTest_BodyCapturer_UE::OnBodyConstructed);
            UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, OnConstructed);
        }

        return false; // wait for construction + result-fragment writes
    }

    // ---- Phase 2: poll each captured body's result fragment ------------------------------------------------------

    const auto Elapsed = FPlatformTime::Seconds() - _StartTime;
    if (Elapsed > _TimeoutSeconds)
    {
        _Test->AddError(ck::Format_UE(
            TEXT("FCk_Latent_RunAsTestOnAllWorlds: timed out after [{}]s. Captured [{}/{}] bodies."),
            _TimeoutSeconds,
            _BodyCapturer.IsValid() ? _BodyCapturer->_CapturedBodies.Num() : 0,
            _ExpectedBodyCount));
        return true;
    }

    if (NOT _BodyCapturer.IsValid())
    { return true; }

    if (_BodyCapturer->_CapturedBodies.Num() < _ExpectedBodyCount)
    { return false; } // not all worlds have constructed yet

    auto AllTerminal = true;
    auto AnyFailed   = false;
    auto FailureMessages = TArray<FString>{};

    for (auto Index = 0; Index < _BodyCapturer->_CapturedBodies.Num(); ++Index)
    {
        const auto& Body = _BodyCapturer->_CapturedBodies[Index];
        if (NOT UCk_Utils_AutoTest_UE::Has_Result(Body))
        {
            AllTerminal = false;
            break;
        }

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
                AnyFailed = true;
                // Build the world label as an FString first — feeding ck::Format_UE's result
                // directly through `*` would dereference a temporary, garbling the label.
                const auto WorldLabel = (Index == 0)
                    ? FString{TEXT("Server")}
                    : ck::Format_UE(TEXT("Client[{}]"), Index - 1);
                FailureMessages.Add(ck::Format_UE(
                    TEXT("[{}] {} ({}/{} assertions failed)"),
                    WorldLabel,
                    Result.FailureMessage,
                    Result.AssertionsFailed,
                    Result.AssertionsRun));
                break;
            }
        }
    }

    if (NOT AllTerminal)
    { return false; }

    for (const auto& Msg : FailureMessages)
    { _Test->AddError(Msg); }

    if (NOT AnyFailed)
    {
        Log_Display(TEXT("FCk_Latent_RunAsTestOnAllWorlds: all [{}] worlds passed in [{}]s."),
            _ExpectedBodyCount, Elapsed);
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
