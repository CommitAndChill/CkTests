#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkGym_NetworkRuntime.h"
#include "CkPoi/CkPoi_Fragment.h"
#include "CkGymRegistry_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCompassGym_Wire.h"
#include "CkCompassGym_Mirror.h"
#include "CkCompassGym_SourceMembership.h"
#include "../../CkCompassGym_Mirror_Private.h"
#include "../../CkCompassGym_SourceMembership_Private.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkCompass/CkCompass_Utils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_compass_authored_network
{
    constexpr int32 Worlds = 3, Clients = 2;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_CompassGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_CompassGym_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_CompassGym_PlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_CompassGym_Pawn");

    template<class T> T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    struct FCall
    {
        UObject* Target = nullptr;
        UFunction* Function = nullptr;
        TUniquePtr<FStructOnScope> Args;
        FCall(UObject* Object, const TCHAR* Name)
        {
            if (!IsValid(Object)) { return; }
            Function = Object->FindFunction(Name);
            if (!IsValid(Function)) { Function = nullptr; return; }
            Target = Object;
            Args = MakeUnique<FStructOnScope>(Function);
        }
        bool Send() const
        {
            if (!IsValid(Target) || !IsValid(Function) || !Args ||
                !Args->GetStructMemory()) { return false; }
            Target->ProcessEvent(Function, Args->GetStructMemory());
            return true;
        }
    };

    bool Activate(UObject* PC, int32 Row)
    {
        FCall Call(PC, TEXT("Request_ControlActivated"));
        auto* Index = Field<FIntProperty>(Call.Function, TEXT("RowIndex"));
        if (!Index) { Index = Field<FIntProperty>(Call.Function, TEXT("InRowIndex")); }
        if (!Index || Call.Function->NumParms != 1 || !Call.Args ||
            !Call.Args->GetStructMemory()) { return false; }
        Index->SetPropertyValue_InContainer(Call.Args->GetStructMemory(), Row);
        return Call.Send();
    }

    bool Ack(UObject* PC, int32 Serial, int32 Action)
    {
        const auto* Property = IsValid(PC)
            ? Field<FStructProperty>(PC->GetClass(), TEXT("LastControlAck")) : nullptr;
        if (!Property) { return false; }
        const void* Data = Property->ContainerPtrToValuePtr<void>(PC);
        const auto* S = Field<FIntProperty>(Property->Struct, TEXT("Serial"));
        const auto* A = Field<FIntProperty>(Property->Struct, TEXT("Action"));
        const auto* R = Field<FIntProperty>(Property->Struct, TEXT("Result"));
        return S && A && R &&
            S->GetPropertyValue_InContainer(Data) == Serial &&
            A->GetPropertyValue_InContainer(Data) == Action &&
            R->GetPropertyValue_InContainer(Data) == 1;
    }

    bool Allocated(const FCk_Handle& Handle)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    FCk_Handle WorldRoot(UWorld* World)
    {
        return IsValid(World)
            ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World)
            : FCk_Handle{};
    }

    struct FRun
    {
        bool Dedicated = false;
        UClass* BaseMode = nullptr;
        UClass* Mode = nullptr;
        UClass* StateClass = nullptr;
        UClass* PCClass = nullptr;
        UClass* PawnClass = nullptr;
        FStructProperty* SnapshotProperty = nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<AGameStateBase> State[Worlds];
        TWeakObjectPtr<APlayerController> Local[Clients], Remote[Clients], Host;
        FCk_Handle SourceRoot;
        FCk_Handle MirrorRoot[Clients];
        FCk_Handle_Compass Compass[Clients];
        FCk_Handle_Compass HostCompass;
        bool HostCompassCaptured = false;
        FCkCompassGym_WireSnapshot BaseFrame;
        FCkCompassGym_WireSnapshot PingFrame;
        FCkCompassGym_WireSnapshot StressFrame;
        int64 BaseIds[9]{};
        FCk_Handle BaseSourceHosts[9];
        FCk_Handle BaseMirrorHosts[Clients][9];
        FCk_Handle PingHost;
        FCk_Handle PingTimer;
        TArray<FCk_Handle> PeerEntriesBeforeFilter;
        ECkGym_StartupMode Startup{};
        FString LastGym;
        TArray<FString> Recents;
        void Restore() const
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup;
            Settings->LastGymName = LastGym;
            Settings->RecentGymNames = Recents;
            Settings->SaveConfig();
        }
    };

    const FCkCompassGym_WireSnapshot* Snapshot(const FRun& Run, int32 Index)
    {
        return Run.SnapshotProperty && Run.State[Index].IsValid()
            ? Run.SnapshotProperty->ContainerPtrToValuePtr<FCkCompassGym_WireSnapshot>(
                Run.State[Index].Get()) : nullptr;
    }

    bool PlayerReady(APlayerController* PC, UWorld* World, const FRun& Run)
    {
        if (!IsValid(PC) || PC->GetWorld() != World ||
            PC->GetClass() != Run.PCClass || !PC->HasActorBegunPlay()) { return false; }
        auto* Pawn = PC->GetPawn().Get();
        auto* PS = PC->PlayerState.Get();
        return IsValid(Pawn) && Pawn->GetClass() == Run.PawnClass &&
            Pawn->GetWorld() == World && Pawn->GetController() == PC &&
            Pawn->HasActorBegunPlay() && IsValid(PS) &&
            PS->GetWorld() == World && Pawn->GetPlayerState() == PS;
    }

    bool Context(FRun& Run)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server) ||
            Server->GetNetMode() != (Run.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            !IsValid(Server->GetAuthGameMode()) ||
            Server->GetAuthGameMode()->GetClass() != Run.Mode ||
            ck::auto_test::net::Get_AllPIEWorlds().Num() != Worlds ||
            ck::auto_test::net::Get_NumClientWorlds() != Clients) { return false; }
        Run.World[0] = Server;
        Run.State[0] = Server->GetGameState();
        if (!Run.State[0].IsValid() || Run.State[0]->GetClass() != Run.StateClass)
        { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            auto* Local = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Remote = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            if (!IsValid(Client) || Client->GetNetMode() != NM_Client ||
                !PlayerReady(Local, Client, Run) || !Local->IsLocalController() ||
                !PlayerReady(Remote, Server, Run) || Remote->IsLocalController() ||
                !IsValid(Client->GetGameState()) ||
                Client->GetGameState()->GetClass() != Run.StateClass) { return false; }
            Run.World[I + 1] = Client;
            Run.State[I + 1] = Client->GetGameState();
            Run.Local[I] = Local;
            Run.Remote[I] = Remote;
        }
        if (Run.Local[0] == Run.Local[1] || Run.Remote[0] == Run.Remote[1])
        { return false; }
        auto* GI = Server->GetGameInstance();
        if (!IsValid(GI) || GI->GetLocalPlayers().Num() != (Run.Dedicated ? 0 : 1))
        { return false; }
        if (!Run.Dedicated)
        {
            auto* LP = GI->GetLocalPlayers()[0];
            auto* Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
            if (!PlayerReady(Host, Server, Run) || !Host->IsLocalController())
            { return false; }
            Run.Host = Host;
        }
        return true;
    }

    bool Direct(const FCk_Handle& Child, const FCk_Handle& Parent)
    {
        return Allocated(Child) && Allocated(Parent) &&
            UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) &&
            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Parent;
    }

    bool FindSourceRoot(UWorld* World, AGameStateBase* State, FCk_Handle& Out)
    {
        Out = {};
        const auto Root = WorldRoot(World);
        if (!Allocated(Root) || !IsValid(State)) { return false; }
        const auto Registry = Root.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        int32 Count = 0, AllocatedCount = 0;
        Registry.View<ck::FFragment_CompassGym_SourceMembership>().ForEach(
            [&](FCk_Entity Entity,
                const ck::FFragment_CompassGym_SourceMembership& Data)
        {
            FCk_Handle Candidate{Entity, RegistryHandle};
            if (Allocated(Candidate)) { ++AllocatedCount; }
            if (Allocated(Candidate) && Data.GameState.Get() == State &&
                Data.World.Get() == World && Direct(Candidate, Root))
            { Out = Candidate; ++Count; }
        });
        return Count == 1 && AllocatedCount == 1;
    }

    bool FindMirrorRoot(UWorld* World, AGameStateBase* State, FCk_Handle& Out)
    {
        Out = {};
        const auto Root = WorldRoot(World);
        if (!Allocated(Root) || !IsValid(State)) { return false; }
        const auto Registry = Root.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        int32 Count = 0, AllocatedCount = 0;
        Registry.View<ck::FFragment_CompassGym_Mirror>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_CompassGym_Mirror& Data)
        {
            FCk_Handle Candidate{Entity, RegistryHandle};
            if (Allocated(Candidate)) { ++AllocatedCount; }
            if (Allocated(Candidate) && Data.GameState.Get() == State &&
                Data.World.Get() == World && Data.WorldRoot == Root &&
                Direct(Candidate, Root))
            { Out = Candidate; ++Count; }
        });
        return Count == 1 && AllocatedCount == 1;
    }

    FString DescribeDiscoveryFailure(FRun& Run)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        const auto AllWorlds = ck::auto_test::net::Get_AllPIEWorlds();
        const int32 ClientWorlds = ck::auto_test::net::Get_NumClientWorlds();
        auto* Mode = IsValid(Server) ? Server->GetAuthGameMode() : nullptr;
        auto* GI = IsValid(Server) ? Server->GetGameInstance() : nullptr;
        const int32 LocalPlayers = IsValid(GI) ? GI->GetLocalPlayers().Num() : -1;
        FString Report = FString::Printf(
            TEXT("Compass travel convergence: topology=%s worlds=%d/%d clients=%d/%d "
                 "serverWorld=%d map=%s netMode=%d/%d mode=%s expectedMode=%s "
                 "serverLocalPlayers=%d/%d"),
            Run.Dedicated ? TEXT("ClientServer") : TEXT("ListenServer"),
            AllWorlds.Num(), Worlds, ClientWorlds, Clients,
            int32(IsValid(Server)), IsValid(Server) ? *Server->GetMapName() : TEXT("<none>"),
            IsValid(Server) ? int32(Server->GetNetMode()) : -1,
            Run.Dedicated ? int32(NM_DedicatedServer) : int32(NM_ListenServer),
            *GetNameSafe(Mode), *GetNameSafe(Run.Mode), LocalPlayers,
            Run.Dedicated ? 0 : 1);

        for (int32 Index = 0; Index < Worlds; ++Index)
        {
            auto* World = Index == 0 ? Server :
                ck::auto_test::net::Get_ClientWorld(Index - 1);
            auto* State = IsValid(World) ? World->GetGameState() : nullptr;
            const bool ExactState = IsValid(State) &&
                State->GetClass() == Run.StateClass && State->GetWorld() == World;
            FCkCompassGym_WireSnapshot Wire;
            const bool TypedSnapshot = ExactState && Run.SnapshotProperty &&
                Run.SnapshotProperty->Struct == FCkCompassGym_WireSnapshot::StaticStruct();
            if (TypedSnapshot)
            {
                const auto* Value = Run.SnapshotProperty->
                    ContainerPtrToValuePtr<FCkCompassGym_WireSnapshot>(State);
                if (Value) { Wire = *Value; }
            }
            Report += FString::Printf(
                TEXT("; world%d valid=%d map=%s mode=%d state=%s exact=%d "
                     "snapshotTyped=%d lifecycle=%d g=%lld q=%lld declared=%d rows=%d"),
                Index, int32(IsValid(World)),
                IsValid(World) ? *World->GetMapName() : TEXT("<none>"),
                IsValid(World) ? int32(World->GetNetMode()) : -1,
                *GetNameSafe(State), int32(ExactState), int32(TypedSnapshot),
                int32(Wire.Lifecycle),
                static_cast<long long>(Wire.SourceGeneration),
                static_cast<long long>(Wire.Sequence),
                Wire.DeclaredRowCount, Wire.Rows.Num());
            const auto Root = WorldRoot(World);
            Report += FString::Printf(TEXT(" worldRootAllocated=%d"),
                int32(Allocated(Root)));
            if (Index == 0)
            {
                FCk_Handle Source;
                const bool Found = ExactState &&
                    FindSourceRoot(World, State, Source);
                Report += FString::Printf(TEXT(" sourceFound=%d"), int32(Found));
                if (Found && Allocated(Source) && ck::IsValid(Source) &&
                    Source.Has<ck::FFragment_CompassGym_SourceMembership>())
                {
                    const auto& Journal = Source.Get<
                        ck::FFragment_CompassGym_SourceMembership>();
                    Report += FString::Printf(
                        TEXT(" sourceResult=%d serial=%lld base=%d journal=%d "
                             "closed=%d failure=%s currentRevision=%lld currentRows=%d"),
                        int32(Journal.Result), static_cast<long long>(Journal.Serial),
                        int32(Journal.BaseInitialized), int32(Journal.JournalEnabled),
                        int32(Journal.ClosedUnproven), *Journal.Failure,
                        static_cast<long long>(Journal.CurrentRevision),
                        Journal.CurrentRows.Num());
                }
            }
            else
            {
                FCk_Handle Mirror;
                const bool Found = ExactState &&
                    FindMirrorRoot(World, State, Mirror);
                Report += FString::Printf(TEXT(" mirrorFound=%d"), int32(Found));
                if (Found && Allocated(Mirror) && ck::IsValid(Mirror) &&
                    Mirror.Has<ck::FFragment_CompassGym_Mirror>())
                {
                    const auto& M = Mirror.Get<ck::FFragment_CompassGym_Mirror>();
                    Report += FString::Printf(
                        TEXT(" mirrorPhase=%d closed=%d failure=%s installed=%d "
                             "installedRows=%d pendingRows=%d"),
                        int32(M.Phase), int32(M.ClosedUnproven), *M.Failure,
                        int32(M.HasInstalled), M.Installed.Num(), M.PendingRows.Num());
                }
            }
        }

        for (int32 I = 0; I < Clients; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            auto* Local = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Remote = IsValid(Server) ?
                ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I) : nullptr;
            auto* Pawn = IsValid(Local) ? Local->GetPawn().Get() : nullptr;
            const auto Entity = IsValid(Pawn) ?
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn) : FCk_Handle{};
            const bool EntityAllocated = Allocated(Entity) && ck::IsValid(Entity);
            const bool HasCompass = EntityAllocated && UCk_Utils_Compass_UE::Has(Entity);
            bool GetterBound = false, GetterOwned = false;
            if (IsValid(Pawn))
            {
                FCall Call(Pawn, TEXT("Get_Compass"));
                const auto* Return = Field<FStructProperty>(Call.Function,
                    TEXT("ReturnValue"));
                GetterBound = Return &&
                    Return->Struct == FCk_Handle_Compass::StaticStruct() &&
                    Call.Function->NumParms == 1 && Call.Args &&
                    Call.Args->GetStructMemory();
                if (GetterBound && Call.Send())
                {
                    const auto Value = *Return->
                        ContainerPtrToValuePtr<FCk_Handle_Compass>(
                            Call.Args->GetStructMemory());
                    GetterOwned = EntityAllocated &&
                        FCk_Handle(Value) == Entity;
                }
            }
            Report += FString::Printf(
                TEXT("; owner%d local=%s ready=%d isLocal=%d remote=%s "
                     "remoteReady=%d remoteIsLocal=%d pawn=%s pawnBegun=%d "
                     "actorEntityAllocated=%d hasCompass=%d getterBound=%d getterOwned=%d"),
                I, *GetNameSafe(Local), int32(PlayerReady(Local, Client, Run)),
                int32(IsValid(Local) && Local->IsLocalController()),
                *GetNameSafe(Remote), int32(PlayerReady(Remote, Server, Run)),
                int32(IsValid(Remote) && Remote->IsLocalController()),
                *GetNameSafe(Pawn), int32(IsValid(Pawn) && Pawn->HasActorBegunPlay()),
                int32(EntityAllocated), int32(HasCompass),
                int32(GetterBound), int32(GetterOwned));
        }
        if (!Run.Dedicated && IsValid(Server) && IsValid(GI) &&
            GI->GetLocalPlayers().Num() == 1)
        {
            auto* LP = GI->GetLocalPlayers()[0];
            auto* Host = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
            Report += FString::Printf(
                TEXT("; listenHost=%s ready=%d local=%d"),
                *GetNameSafe(Host), int32(PlayerReady(Host, Server, Run)),
                int32(IsValid(Host) && Host->IsLocalController()));
        }
        return Report;
    }

    bool NoAuthorityMirror(UWorld* World)
    {
        const auto Root = WorldRoot(World);
        if (!Allocated(Root)) { return false; }
        const auto Registry = Root.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        bool Empty = true;
        Registry.View<ck::FFragment_CompassGym_Mirror>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_CompassGym_Mirror&)
        { Empty &= !Allocated(FCk_Handle{Entity, RegistryHandle}); });
        return Empty;
    }

    bool Discover(FRun& Run)
    {
        if (!Context(Run)) { return false; }
        if (!FindSourceRoot(
                Run.World[0].Get(), Run.State[0].Get(), Run.SourceRoot))
        { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            if (!FindMirrorRoot(
                    Run.World[I + 1].Get(), Run.State[I + 1].Get(),
                    Run.MirrorRoot[I])) { return false; }
            const auto Pawn = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(
                Run.Local[I]->GetPawn());
            if (!Allocated(Pawn) || !UCk_Utils_Compass_UE::Has(Pawn))
            { return false; }
            FCall GetCompass(Run.Local[I]->GetPawn(), TEXT("Get_Compass"));
            const auto* Return = Field<FStructProperty>(GetCompass.Function,
                TEXT("ReturnValue"));
            if (!Return || Return->Struct != FCk_Handle_Compass::StaticStruct() ||
                GetCompass.Function->NumParms != 1 || !GetCompass.Send())
            { return false; }
            const auto Compass = *Return->ContainerPtrToValuePtr<FCk_Handle_Compass>(
                GetCompass.Args->GetStructMemory());
            if (FCk_Handle(Compass) != Pawn) { return false; }
            Run.Compass[I] = Compass;
        }
        return true;
    }

    bool AuthorityFrame(FRun& Run, int32 Count,
        FCkCompassGym_WireSnapshot& Out)
    {
        if (!Discover(Run)) { return false; }
        const auto* Wire = Snapshot(Run, 0);
        if (!Wire || Wire->Lifecycle != ECkCompassGym_WireLifecycle::Active ||
            Wire->SourceGeneration != 1 || Wire->Sequence <= 0 ||
            Wire->DeclaredRowCount != Count || Wire->Rows.Num() != Count)
        { return false; }
        int64 Revision = 0;
        TArray<FCkCompassGym_SourceRow> Rows;
        FString Why;
        if (!UCkCompassGym_SourceMembership::TryGet_CurrentRows(
                Run.World[0].Get(), Run.State[0].Get(), Run.SourceRoot,
                Revision, Rows, Why) || Revision != Wire->Sequence)
        { return false; }
        FCkCompassGym_WireSnapshot Physical = *Wire;
        Physical.Rows = Rows;
        Physical.DeclaredRowCount = Rows.Num();
        if (!UCkCompassGym_Wire::Get_IsSameFrame(*Wire, Physical))
        { return false; }
        Out = *Wire;
        return true;
    }

    bool HasLocalCompass(APlayerController* PC, FCk_Handle_Compass* Out);

    bool AllClientsInstalled(FRun& Run, const FCkCompassGym_WireSnapshot& Expected)
    {
        if (!Discover(Run)) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            const auto* Received = Snapshot(Run, I + 1);
            if (!Received || !UCkCompassGym_Wire::Get_IsSameFrame(*Received, Expected))
            { return false; }
            bool Busy = false, Failed = false;
            int64 Generation = 0, Sequence = 0;
            TArray<FCkCompassGym_SourceRow> Rows;
            FString Why;
            if (!UCkCompassGym_Mirror::TryGet_Status(Run.World[I + 1].Get(),
                    Run.State[I + 1].Get(), Run.MirrorRoot[I], Busy, Failed, Why) ||
                Busy || Failed ||
                !UCkCompassGym_Mirror::TryGet_Installed(Run.World[I + 1].Get(),
                    Run.State[I + 1].Get(), Run.MirrorRoot[I], Generation,
                    Sequence, Rows, Why)) { return false; }
            FCkCompassGym_WireSnapshot Installed = *Received;
            Installed.SourceGeneration = Generation;
            Installed.Sequence = Sequence;
            Installed.Rows = Rows;
            Installed.DeclaredRowCount = Rows.Num();
            if (!UCkCompassGym_Wire::Get_IsSameFrame(Expected, Installed) ||
                FCk_Handle(Run.Compass[I]) !=
                    UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(
                        Run.Local[I]->GetPawn())) { return false; }
        }
        if (!Run.Dedicated)
        {
            FCk_Handle_Compass Current;
            if (!Run.Host.IsValid() ||
                !HasLocalCompass(Run.Host.Get(), &Current) ||
                (Run.HostCompassCaptured &&
                    FCk_Handle(Current) != FCk_Handle(Run.HostCompass)))
            { return false; }
        }
        return true;
    }

    bool SameBaseHosts(const FRun& Run, const FCkCompassGym_WireSnapshot& Wire)
    {
        if (Wire.Rows.Num() < 9) { return false; }
        for (int32 I = 0; I < 9; ++I)
        { if (Wire.Rows[I].CatalogId != Run.BaseIds[I]) { return false; } }
        if (!Allocated(Run.SourceRoot) ||
            !Run.SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
        { return false; }
        const auto& Source = Run.SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (Source.Base.Num() != 9) { return false; }
        for (int32 I = 0; I < 9; ++I)
        {
            if (Source.Base[I].CatalogId != Run.BaseIds[I] ||
                Source.Base[I].Host != Run.BaseSourceHosts[I] ||
                !Direct(Source.Base[I].Host, Run.SourceRoot)) { return false; }
        }
        for (int32 C = 0; C < Clients; ++C)
        {
            if (!Allocated(Run.MirrorRoot[C]) ||
                !Run.MirrorRoot[C].Has<ck::FFragment_CompassGym_Mirror>())
            { return false; }
            const auto& Mirror = Run.MirrorRoot[C].Get<ck::FFragment_CompassGym_Mirror>();
            for (int32 I = 0; I < 9; ++I)
            {
                int32 Matches = 0;
                for (const auto& Entry : Mirror.Installed)
                {
                    if (Entry.Row.CatalogId == Run.BaseIds[I])
                    {
                        Matches += Entry.Host == Run.BaseMirrorHosts[C][I] &&
                            Direct(Entry.Host, Run.MirrorRoot[C]) ? 1 : 0;
                    }
                }
                if (Matches != 1) { return false; }
            }
        }
        return true;
    }

    bool HasLocalCompass(APlayerController* PC, FCk_Handle_Compass* Out)
    {
        auto* Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
        if (!IsValid(Pawn)) { return false; }
        const auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Pawn);
        if (!Allocated(Entity) || !UCk_Utils_Compass_UE::Has(Entity))
        { return false; }
        FCall Call(Pawn, TEXT("Get_Compass"));
        const auto* Return = Field<FStructProperty>(Call.Function, TEXT("ReturnValue"));
        if (!Return || Return->Struct != FCk_Handle_Compass::StaticStruct() ||
            Call.Function->NumParms != 1 || !Call.Send()) { return false; }
        const auto Compass = *Return->ContainerPtrToValuePtr<FCk_Handle_Compass>(
            Call.Args->GetStructMemory());
        if (FCk_Handle(Compass) != Entity) { return false; }
        if (Out) { *Out = Compass; }
        return true;
    }

    bool CaptureHostCompass(FRun& Run)
    {
        if (Run.Dedicated) { return !Run.Host.IsValid(); }
        FCk_Handle_Compass Current;
        if (!Run.Host.IsValid() ||
            !HasLocalCompass(Run.Host.Get(), &Current)) { return false; }
        Run.HostCompass = Current;
        Run.HostCompassCaptured = true;
        return true;
    }

    bool EntriesMatchFilter(const FRun& Run, int32 Client,
        bool QuestOnly, bool RequireNonQuest)
    {
        if (Client < 0 || Client >= Clients ||
            !Allocated(Run.MirrorRoot[Client]) ||
            !Run.MirrorRoot[Client].Has<ck::FFragment_CompassGym_Mirror>() ||
            !Allocated(FCk_Handle(Run.Compass[Client])) ||
            !Run.Local[Client].IsValid() ||
            FCk_Handle(Run.Compass[Client]) !=
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(
                    Run.Local[Client]->GetPawn())) { return false; }
        const auto& Mirror = Run.MirrorRoot[Client].Get<ck::FFragment_CompassGym_Mirror>();
        const auto Entries = UCk_Utils_Compass_UE::Get_Entries(Run.Compass[Client]);
        if (Entries.IsEmpty()) { return false; }
        int32 NonQuest = 0;
        for (const auto& Entry : Entries)
        {
            int32 Matches = 0;
            bool Quest = false;
            for (const auto& Source : Mirror.Installed)
            {
                if (FCk_Handle(Entry.Get_Poi()) == Source.Host)
                {
                    ++Matches;
                    Quest = Source.Row.Category.GetTagName() ==
                        FName(TEXT("Poi.Category.Quest"));
                }
            }
            if (Matches != 1 || (QuestOnly && !Quest)) { return false; }
            if (!Quest) { ++NonQuest; }
        }
        return !RequireNonQuest || NonQuest > 0;
    }

    bool CapturePeerEntries(FRun& Run)
    {
        if (!EntriesMatchFilter(Run, 0, false, true)) { return false; }
        const auto Entries = UCk_Utils_Compass_UE::Get_Entries(Run.Compass[0]);
        Run.PeerEntriesBeforeFilter.Reset();
        for (const auto& Entry : Entries)
        { Run.PeerEntriesBeforeFilter.Add(FCk_Handle(Entry.Get_Poi())); }
        return !Run.PeerEntriesBeforeFilter.IsEmpty();
    }

    bool PeerEntriesUnchanged(const FRun& Run)
    {
        if (Run.PeerEntriesBeforeFilter.IsEmpty() ||
            !EntriesMatchFilter(Run, 0, false, true)) { return false; }
        const auto Entries = UCk_Utils_Compass_UE::Get_Entries(Run.Compass[0]);
        if (Entries.Num() != Run.PeerEntriesBeforeFilter.Num()) { return false; }
        for (int32 I = 0; I < Entries.Num(); ++I)
        { if (FCk_Handle(Entries[I].Get_Poi()) != Run.PeerEntriesBeforeFilter[I])
            { return false; } }
        return true;
    }

    bool CaptureBaseHosts(FRun& Run)
    {
        if (Run.BaseFrame.Rows.Num() != 9 || !Allocated(Run.SourceRoot) ||
            !Run.SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
        { return false; }
        const auto& Source = Run.SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (Source.Base.Num() != 9) { return false; }
        for (int32 I = 0; I < 9; ++I)
        {
            Run.BaseIds[I] = Run.BaseFrame.Rows[I].CatalogId;
            if (Source.Base[I].CatalogId != Run.BaseIds[I] ||
                !Direct(Source.Base[I].Host, Run.SourceRoot)) { return false; }
            Run.BaseSourceHosts[I] = Source.Base[I].Host;
        }
        for (int32 C = 0; C < Clients; ++C)
        {
            if (!Allocated(Run.MirrorRoot[C]) ||
                !Run.MirrorRoot[C].Has<ck::FFragment_CompassGym_Mirror>())
            { return false; }
            const auto& Mirror = Run.MirrorRoot[C].Get<ck::FFragment_CompassGym_Mirror>();
            if (Mirror.Installed.Num() != 9) { return false; }
            for (int32 I = 0; I < 9; ++I)
            {
                if (Mirror.Installed[I].Row.CatalogId != Run.BaseIds[I] ||
                    !Direct(Mirror.Installed[I].Host, Run.MirrorRoot[C]))
                { return false; }
                Run.BaseMirrorHosts[C][I] = Mirror.Installed[I].Host;
            }
        }
        return true;
    }

    bool RealPing(FRun& Run, const FCkCompassGym_WireSnapshot& Wire)
    {
        if (Wire.Rows.Num() != 10 || !Allocated(Run.SourceRoot) ||
            !Run.SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>() ||
            !Run.Remote[0].IsValid() || !IsValid(Run.Remote[0]->GetPawn()))
        { return false; }
        const auto& Journal = Run.SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (Journal.ActivePings.Num() != 1 ||
            !Allocated(Journal.ActivePings[0].Host) ||
            !Allocated(Journal.ActivePings[0].Timer)) { return false; }
        const auto* Ping = Wire.Rows.FindByPredicate([](const FCkCompassGym_SourceRow& Row)
        { return Row.Kind == ECkCompassGym_SourceKind::Ping; });
        const auto* Pawn = Run.Remote[0]->GetPawn().Get();
        const FVector Ahead = Pawn->GetActorLocation() +
            Pawn->GetActorForwardVector() * 1500.0;
        if (!Ping || Ping->CatalogId != Journal.ActivePings[0].CatalogId ||
            !Ping->Location.Equals(Ahead, 1.0)) { return false; }
        Run.PingHost = Journal.ActivePings[0].Host;
        Run.PingTimer = Journal.ActivePings[0].Timer;
        return true;
    }

    bool ExactStressRows(const FCkCompassGym_WireSnapshot& Wire)
    {
        if (Wire.Rows.Num() != 509) { return false; }
        int32 Stress = 0;
        for (const auto& Row : Wire.Rows)
        { Stress += Row.Kind == ECkCompassGym_SourceKind::Stress ? 1 : 0; }
        return Stress == 500;
    }

    bool StressPhysicallyRetired(const FRun& Run)
    {
        if (!Allocated(Run.SourceRoot) ||
            !Run.SourceRoot.Has<ck::FFragment_CompassGym_SourceMembership>())
        { return false; }
        const auto& Journal = Run.SourceRoot.Get<ck::FFragment_CompassGym_SourceMembership>();
        if (!Journal.FrozenStressRetirementComplete ||
            Journal.FrozenRetiring.Num() != 500) { return false; }
        for (const auto& R : Journal.FrozenRetiring)
        { if (Allocated(R.Host)) { return false; } }
        return true;
    }

    struct FRestore final : IAutomationLatentCommand
    {
        TSharedRef<FRun> Run;
        explicit FRestore(TSharedRef<FRun> In) : Run(In) {}
        bool Update() override { Run->Restore(); return true; }
    };
}

bool RunCompassAuthoredNetworkFixture(FAutomationTestBase& Test,
    const FString& Parameters)
{
    using namespace ck_compass_authored_network;
    auto Run = MakeShared<FRun>();
    auto* TestPtr = &Test;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters,
            Run->Dedicated))
    { Test.AddError(TEXT("unsupported Compass network topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->SnapshotProperty = Field<FStructProperty>(Run->StateClass, TEXT("Snapshot"));
    if (!Test.TestTrue(TEXT("authored Compass classes and typed replicated wire"),
            Run->BaseMode && Run->Mode && Run->StateClass && Run->PCClass &&
            Run->PawnClass && Run->SnapshotProperty &&
            Run->SnapshotProperty->Struct == FCkCompassGym_WireSnapshot::StaticStruct()))
    { return false; }
    if (!Test.TestTrue(TEXT("authored control and pawn Compass getter are reflected"),
            Run->PCClass->FindFunctionByName(FName(TEXT("Request_ControlActivated"))) &&
            Run->PawnClass->FindFunctionByName(FName(TEXT("Get_Compass")))))
    { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    Run->Startup = Settings->StartupMode;
    Run->LastGym = Settings->LastGymName;
    Run->Recents = Settings->RecentGymNames;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode =
        ECkGym_StartupMode::Cycler;
    Test.AddCommand(new FCk_Latent_StartPIEMultiClient(
        Worlds, EntryMap, Parameters, Run->BaseMode));
    Test.AddCommand(new FCk_Latent_WaitForPIEReady(Worlds, 60));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            auto* W = ck::auto_test::net::Get_ServerWorld();
            auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
            auto* Registry = IsValid(GI)
                ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
            return IsValid(Registry) &&
                Registry->Find_GymIndexByName(TEXT("Compass")) != INDEX_NONE;
        }), 30, TEXT("authored Compass gym registered")));
    Test.AddCommand(new FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([TestPtr](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestPtr->TestTrue(TEXT("owner requests real Compass gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Compass")));
        })));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Discover(*Run); }), 60,
        TEXT("both owners and one shared source converge after Compass travel")));
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([Run, TestPtr]()
        {
            if (ck::auto_test::net::Get_HasSessionFailed())
            { TestPtr->AddError(DescribeDiscoveryFailure(*Run)); }
            return true;
        }), TEXT("Compass travel convergence diagnostic"), true));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            if (!AuthorityFrame(*Run, 9, Run->BaseFrame) ||
                !AllClientsInstalled(*Run, Run->BaseFrame)) { return false; }
            return CaptureBaseHosts(*Run) && SameBaseHosts(*Run, Run->BaseFrame) &&
                NoAuthorityMirror(Run->World[0].Get()) &&
                CaptureHostCompass(*Run);
        }), 60, TEXT("Iris delivers canonical Base9 and both local mirrors install")));
    // J is a real owner RPC. Its authority Ping is composed at the current
    // server pawn pose, exposed through a real timer, then removed after TTL.
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        { return Activate(Run->Local[0].Get(), 2); }),
        TEXT("owner zero activates J through authored control")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            return Ack(Run->Local[0].Get(), 1, 1) &&
                AuthorityFrame(*Run, 10, Run->PingFrame) &&
                AllClientsInstalled(*Run, Run->PingFrame) &&
                SameBaseHosts(*Run, Run->PingFrame) &&
                RealPing(*Run, Run->PingFrame);
        }), 20, TEXT("J RPC admits one timer-backed source Ping on both clients")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            FCkCompassGym_WireSnapshot After;
            return AuthorityFrame(*Run, 9, After) &&
                After.Sequence > Run->PingFrame.Sequence &&
                SameBaseHosts(*Run, After) && AllClientsInstalled(*Run, After) &&
                !Allocated(Run->PingHost) && !Allocated(Run->PingTimer);
        }), 20, TEXT("real Ping TTL retires on source and both mirrors")));
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        { return Activate(Run->Local[0].Get(), 7); }),
        TEXT("owner zero activates M through authored control")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            return Ack(Run->Local[0].Get(), 2, 2) &&
                AuthorityFrame(*Run, 509, Run->StressFrame) &&
                AllClientsInstalled(*Run, Run->StressFrame) &&
                SameBaseHosts(*Run, Run->StressFrame) &&
                ExactStressRows(Run->StressFrame);
        }), 90, TEXT("M publishes whole 509 and both Iris clients install")));
    Test.AddCommand(new FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* W)
        {
            if (auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr)
            { PC->SetControlRotation(FRotator(0, 180, 0)); }
        })));
    Test.AddCommand(new FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld* W)
        {
            if (auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr)
            { PC->SetControlRotation(FRotator(0, 180, 0)); }
        })));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return EntriesMatchFilter(*Run, 1, false, true) &&
                 CapturePeerEntries(*Run); }),
        20, TEXT("both owners project non-Quest entries before local T")));
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        { return Activate(Run->Local[1].Get(), 3) &&
                 Activate(Run->Local[1].Get(), 4); }),
        TEXT("owner one activates local T filter and P readout")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            FCkCompassGym_WireSnapshot Current;
            return AuthorityFrame(*Run, 509, Current) &&
                UCkCompassGym_Wire::Get_IsSameFrame(Current, Run->StressFrame) &&
                AllClientsInstalled(*Run, Run->StressFrame) &&
                EntriesMatchFilter(*Run, 1, true, false) &&
                PeerEntriesUnchanged(*Run);
        }), 20, TEXT("T affects only owner one's local Compass; P leaves source unchanged")));
    Test.AddCommand(new FCk_Latent_AssertCondition(&Test,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        { return Activate(Run->Local[0].Get(), 8); }),
        TEXT("owner zero activates U through authored control")));
    Test.AddCommand(new FCk_Latent_WaitUntil(&Test,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            FCkCompassGym_WireSnapshot After;
            return Ack(Run->Local[0].Get(), 3, 3) &&
                AuthorityFrame(*Run, 9, After) &&
                After.Sequence > Run->StressFrame.Sequence &&
                SameBaseHosts(*Run, After) && AllClientsInstalled(*Run, After) &&
                StressPhysicallyRetired(*Run) &&
                EntriesMatchFilter(*Run, 1, true, false) &&
                EntriesMatchFilter(*Run, 0, false, true);
        }), 90, TEXT("U retires exact stress set and preserves Base9 identities")));
    Test.AddCommand(new FCk_Latent_EndPIE());
    Test.AddCommand(new FRestore(Run));
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_SharedWorld_Network,
    "Ck.Compass.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
    EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_SharedWorld_Network::RunTest(const FString& Parameters)
{ return RunCompassAuthoredNetworkFixture(*this, Parameters); }

namespace ck_compass_authored_network
{
    struct FLifecycle
    {
        FRun Initial;
        TWeakObjectPtr<UWorld> OldWorld[4];
        FCk_Handle OldSource;
        FCk_Handle OldMirror[3];
        FCk_Handle OldMirrorHosts[3][9];
        FCk_Handle OldBase[9];
        FCk_Handle OldCompass[3];
        FCk_Handle OldHostCompass;
        FCk_Handle NewSource;
        FCk_Handle NewMirror[3];
    };

    bool ExactBaseClient(UWorld* World, const FRun& Run,
        const FCkCompassGym_WireSnapshot& Source,
        FCk_Handle& OutMirror, FCk_Handle& OutCompass)
    {
        OutMirror = {};
        OutCompass = {};
        auto* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
        auto* State = IsValid(World) ? World->GetGameState() : nullptr;
        auto* GymState = Cast<ACkGym_GameState_UE>(State);
        if (!IsValid(World) || World->GetNetMode() != NM_Client ||
            !PlayerReady(PC, World, Run) || !PC->IsLocalController() ||
            !IsValid(State) || State->GetClass() != Run.StateClass ||
            !IsValid(GymState) || !GymState->Get_HasPublishedCatalog() ||
            !Run.SnapshotProperty ||
            Run.SnapshotProperty->Struct != FCkCompassGym_WireSnapshot::StaticStruct() ||
            !FindMirrorRoot(World, State, OutMirror))
        { return false; }
        const auto* Received = Run.SnapshotProperty->
            ContainerPtrToValuePtr<FCkCompassGym_WireSnapshot>(State);
        if (!Received || !UCkCompassGym_Wire::Get_IsSameFrame(*Received, Source))
        { return false; }
        bool Busy = true, Failed = true;
        int64 Generation = 0, Sequence = 0;
        TArray<FCkCompassGym_SourceRow> Rows;
        FString Why;
        if (!UCkCompassGym_Mirror::TryGet_Status(World, State, OutMirror,
                Busy, Failed, Why) || Busy || Failed ||
            !UCkCompassGym_Mirror::TryGet_Installed(World, State, OutMirror,
                Generation, Sequence, Rows, Why) || Rows.Num() != 9)
        { return false; }
        auto Installed = *Received;
        Installed.SourceGeneration = Generation;
        Installed.Sequence = Sequence;
        Installed.Rows = Rows;
        Installed.DeclaredRowCount = Rows.Num();
        FCk_Handle_Compass Compass;
        if (!UCkCompassGym_Wire::Get_IsSameFrame(*Received, Installed) ||
            !HasLocalCompass(PC, &Compass)) { return false; }
        OutCompass = FCk_Handle(Compass);
        return Direct(OutMirror, WorldRoot(World)) &&
            OutCompass == UCk_Utils_OwningActor_UE::
                TryGet_ActorEntityHandle(PC->GetPawn()) &&
            Allocated(OutCompass);
    }

    bool ExactBaseAuthority(UWorld* Server, const FRun& Run,
        FCk_Handle& OutSource, FCkCompassGym_WireSnapshot& OutWire)
    {
        OutSource = {};
        OutWire = {};
        auto* Mode = IsValid(Server) ? Server->GetAuthGameMode() : nullptr;
        auto* State = IsValid(Server) ? Server->GetGameState() : nullptr;
        if (!IsValid(Server) || Server->GetNetMode() !=
                (Run.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            !IsValid(Mode) || Mode->GetClass() != Run.Mode ||
            !IsValid(State) || State->GetClass() != Run.StateClass ||
            !Run.SnapshotProperty ||
            Run.SnapshotProperty->Struct != FCkCompassGym_WireSnapshot::StaticStruct() ||
            !FindSourceRoot(Server, State, OutSource))
        { return false; }
        const auto* Published = Run.SnapshotProperty->
            ContainerPtrToValuePtr<FCkCompassGym_WireSnapshot>(State);
        if (!Published || Published->Lifecycle !=
                ECkCompassGym_WireLifecycle::Active ||
            Published->SourceGeneration != 1 || Published->Sequence <= 0 ||
            Published->DeclaredRowCount != 9 || Published->Rows.Num() != 9)
        { return false; }
        int64 Revision = 0;
        TArray<FCkCompassGym_SourceRow> Rows;
        FString Why;
        if (!UCkCompassGym_SourceMembership::TryGet_CurrentRows(Server,
                State, OutSource, Revision, Rows, Why) ||
            Revision != Published->Sequence || Rows.Num() != 9)
        { return false; }
        OutWire = *Published;
        auto Physical = OutWire;
        Physical.Rows = Rows;
        Physical.DeclaredRowCount = Rows.Num();
        return UCkCompassGym_Wire::Get_IsSameFrame(OutWire, Physical) &&
            NoAuthorityMirror(Server);
    }

    bool JoinedBase(FLifecycle& L)
    {
        using namespace ck::auto_test::net;
        auto* Server = Get_ServerWorld();
        const auto WorldsNow = Get_AllPIEWorlds();
        if (!IsValid(Server) || WorldsNow.Num() != 4 ||
            Get_NumClientWorlds() != 3 ||
            Server != L.Initial.World[0].Get() ||
            !Allocated(L.OldSource) || !SameBaseHosts(L.Initial, L.Initial.BaseFrame))
        { return false; }
        FCk_Handle Source;
        FCkCompassGym_WireSnapshot Published;
        if (!ExactBaseAuthority(Server, L.Initial, Source, Published) ||
            Source != L.OldSource ||
            !UCkCompassGym_Wire::Get_IsSameFrame(Published,
                L.Initial.BaseFrame)) { return false; }
        for (int32 I = 0; I < 3; ++I)
        {
            auto* World = Get_ClientWorld(I);
            FCk_Handle Mirror, Compass;
            if (!IsValid(World) ||
                (I < 2 && World != L.Initial.World[I + 1].Get()) ||
                !ExactBaseClient(World, L.Initial, Published, Mirror, Compass) ||
                (I < 2 &&
                    (Mirror != L.OldMirror[I] || Compass != L.OldCompass[I])))
            { return false; }
            if (!Allocated(Mirror) || !ck::IsValid(Mirror) ||
                !Mirror.Has<ck::FFragment_CompassGym_Mirror>())
            { return false; }
            const auto& Installed = Mirror.Get<
                ck::FFragment_CompassGym_Mirror>().Installed;
            if (Installed.Num() != 9) { return false; }
            for (int32 Slot = 0; Slot < 9; ++Slot)
            {
                const auto& Entry = Installed[Slot];
                if (Entry.Row.CatalogId != L.Initial.BaseIds[Slot] ||
                    !Allocated(Entry.Host) || !ck::IsValid(Entry.Host) ||
                    !Entry.Host.Has<ck::FTag_Poi>() ||
                    !Direct(Entry.Host, Mirror) ||
                    (I < 2 && Entry.Host != L.OldMirrorHosts[I][Slot]))
                { return false; }
                if (I == 2) { L.OldMirrorHosts[I][Slot] = Entry.Host; }
            }
            if (I == 2)
            {
                L.OldWorld[3] = World;
                L.OldMirror[2] = Mirror;
                L.OldCompass[2] = Compass;
            }
        }
        if (!L.Initial.Dedicated)
        {
            FCk_Handle_Compass Host;
            if (!HasLocalCompass(L.Initial.Host.Get(), &Host) ||
                FCk_Handle(Host) != L.OldHostCompass)
            { return false; }
        }
        return true;
    }

    bool TraveledBase(FLifecycle& L)
    {
        using namespace ck::auto_test::net;
        auto* Server = Get_ServerWorld();
        if (!IsValid(Server) || Get_AllPIEWorlds().Num() != 4 ||
            Get_NumClientWorlds() != 3 || Server == L.OldWorld[0].Get())
        { return false; }
        FCk_Handle Source;
        FCkCompassGym_WireSnapshot Published;
        if (!ExactBaseAuthority(Server, L.Initial, Source, Published) ||
            Source == L.OldSource || Allocated(L.OldSource))
        { return false; }
        for (int32 I = 0; I < 9; ++I)
        {
            if (Published.Rows[I].CatalogId != L.Initial.BaseIds[I] ||
                Allocated(L.OldBase[I])) { return false; }
        }
        for (int32 I = 0; I < 3; ++I)
        {
            auto* World = Get_ClientWorld(I);
            FCk_Handle Mirror, Compass;
            if (!IsValid(World) || World == L.OldWorld[I + 1].Get() ||
                !ExactBaseClient(World, L.Initial, Published, Mirror, Compass) ||
                Mirror == L.OldMirror[I] || Compass == L.OldCompass[I] ||
                Allocated(L.OldMirror[I]) || Allocated(L.OldCompass[I]))
            { return false; }
            L.NewMirror[I] = Mirror;
            for (int32 Slot = 0; Slot < 9; ++Slot)
            { if (Allocated(L.OldMirrorHosts[I][Slot])) { return false; } }
        }
        if (!L.Initial.Dedicated)
        {
            auto* GI = Server->GetGameInstance();
            auto* Host = IsValid(GI) && GI->GetLocalPlayers().Num() == 1
                ? GI->GetLocalPlayers()[0]->GetPlayerController(Server) : nullptr;
            FCk_Handle_Compass HostCompass;
            if (!PlayerReady(Host, Server, L.Initial) ||
                !HasLocalCompass(Host, &HostCompass) ||
                FCk_Handle(HostCompass) == L.OldHostCompass ||
                Allocated(L.OldHostCompass)) { return false; }
        }
        L.NewSource = Source;
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_SharedWorld_TravelLateJoin,
    "Ck.Compass.Gym.TravelLateJoin.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
    EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_SharedWorld_TravelLateJoin::RunTest(const FString& Parameters)
{
    using namespace ck_compass_authored_network;
    auto L = MakeShared<FLifecycle>();
    auto& Test = *this;
    auto& Run = L->Initial;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run.Dedicated))
    { AddError(TEXT("unsupported Compass lifecycle topology")); return false; }
    Run.BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run.Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run.StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run.PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run.PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run.SnapshotProperty = Field<FStructProperty>(Run.StateClass,
        TEXT("Snapshot"));
    if (!TestTrue(TEXT("lifecycle authored classes and typed snapshot exist"),
            Run.BaseMode && Run.Mode && Run.StateClass && Run.PCClass &&
            Run.PawnClass && Run.SnapshotProperty &&
            Run.SnapshotProperty->Struct == FCkCompassGym_WireSnapshot::StaticStruct()))
    { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    Run.Startup = Settings->StartupMode;
    Run.LastGym = Settings->LastGymName;
    Run.Recents = Settings->RecentGymNames;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode =
        ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        Worlds, EntryMap, Parameters, Run.BaseMode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(Worlds, 60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() {
            auto* W = ck::auto_test::net::Get_ServerWorld();
            auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
            auto* Registry = IsValid(GI)
                ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
            return IsValid(Registry) &&
                Registry->Find_GymIndexByName(TEXT("Compass")) != INDEX_NONE;
        }), 30, TEXT("lifecycle Compass gym registered")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("lifecycle owner requests Compass travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Compass")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([L]() {
            auto& R = L->Initial;
            if (!AuthorityFrame(R, 9, R.BaseFrame) ||
                !AllClientsInstalled(R, R.BaseFrame) ||
                !CaptureBaseHosts(R) || !SameBaseHosts(R, R.BaseFrame) ||
                !CaptureHostCompass(R)) { return false; }
            for (int32 I = 0; I < Worlds; ++I)
            { L->OldWorld[I] = R.World[I]; }
            L->OldSource = R.SourceRoot;
            for (int32 I = 0; I < 2; ++I)
            {
                L->OldMirror[I] = R.MirrorRoot[I];
                L->OldCompass[I] = FCk_Handle(R.Compass[I]);
                for (int32 Slot = 0; Slot < 9; ++Slot)
                { L->OldMirrorHosts[I][Slot] = R.BaseMirrorHosts[I][Slot]; }
            }
            for (int32 I = 0; I < 9; ++I)
            { L->OldBase[I] = R.BaseSourceHosts[I]; }
            L->OldHostCompass = FCk_Handle(R.HostCompass);
            return true;
        }), 60, TEXT("initial authored Base9 and two owner mirrors settle")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RequestLateJoin(60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([L]() {
            return JoinedBase(*L);
        }), 60, TEXT("real late third owner installs same Base9 without changing original identities")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(2,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W) {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("late owner requests registered same-gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Compass")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([L]() {
            return TraveledBase(*L);
        }), 60, TEXT("same-gym travel retires old source and all three mirrors, then rebuilds whole Base9")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([L]() {
            return Allocated(L->NewSource) &&
                L->NewSource != L->OldSource &&
                !Allocated(L->OldSource) &&
                !Allocated(L->OldMirror[0]) &&
                !Allocated(L->OldMirror[1]) &&
                !Allocated(L->OldMirror[2]) &&
                Allocated(L->NewMirror[0]) &&
                Allocated(L->NewMirror[1]) &&
                Allocated(L->NewMirror[2]);
        }), TEXT("new world uses only fresh source and mirror identities")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([L]() {
            if (!ck::auto_test::net::Get_AllPIEWorlds().IsEmpty() ||
                Allocated(L->OldSource) || Allocated(L->NewSource))
            { return false; }
            for (int32 I = 0; I < 3; ++I)
            { if (Allocated(L->OldMirror[I]) || Allocated(L->NewMirror[I]))
                { return false; } }
            for (int32 I = 0; I < 3; ++I)
            { for (int32 Slot = 0; Slot < 9; ++Slot)
                { if (Allocated(L->OldMirrorHosts[I][Slot]))
                    { return false; } } }
            return true;
        }), TEXT("EndPIE releases old and new world membership"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([L]() {
            L->Initial.Restore();
            return true;
        }), TEXT("restore startup preference after lifecycle gate"), true));
    return true;
}

#endif
