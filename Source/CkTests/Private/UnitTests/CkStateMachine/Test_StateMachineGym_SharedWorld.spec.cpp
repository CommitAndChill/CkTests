#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkStateMachineGym_Runtime.h"
#include "CkStateMachineGym_Wire.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_sm_authored_network
{
    constexpr int32 Worlds = 3, Clients = 2, Stations = 9;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_SmTest_GymGameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_SmTest_GymGameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_SmTest_GymPlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_Gym_Base_Pawn");
    constexpr auto SourceBasePath = TEXT("/Script/Angelscript.Ck_SmTest_GymSourceActor");
    constexpr const TCHAR* Tags[Stations] = {
        TEXT("Gym.StateMachine.AutoCycle"), TEXT("Gym.StateMachine.PauseResume"),
        TEXT("Gym.StateMachine.Complex"), TEXT("Gym.StateMachine.Hierarchical"),
        TEXT("Gym.StateMachine.GraphWalkRegression"),
        TEXT("Gym.StateMachine.DivergenceFirstBranch"),
        TEXT("Gym.StateMachine.DivergenceTimed"),
        TEXT("Gym.StateMachine.RacingEventDriven"),
        TEXT("Gym.StateMachine.EventDrivenMultiCondition")};

    template<class T> T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    FCk_Handle Root(UWorld* World)
    { return IsValid(World) ? UCk_Utils_EcsWorld_Subsystem_UE::
        TryGet_TransientEntityForWorld(*World) : FCk_Handle{}; }

    bool Direct(const FCk_Handle& Child, const FCk_Handle& Parent)
    { return Allocated(Child) && Allocated(Parent) &&
        UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child) &&
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Parent; }

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
        { return IsValid(Target) && IsValid(Function) && Args &&
            Args->GetStructMemory() &&
            (Target->ProcessEvent(Function, Args->GetStructMemory()), true); }
    };

    bool Bool(UObject* Object, const TCHAR* Name)
    {
        FCall C(Object, Name);
        auto* P = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        return P && C.Function->NumParms == 1 && C.Send() &&
            P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
    }

    bool Activate(APlayerController* PC, int32 Station)
    {
        FCall C(PC, TEXT("Request_ControlActivated"));
        auto* P = Field<FIntProperty>(C.Function, TEXT("RowIndex"));
        if (!P || C.Function->NumParms != 1 || !C.Args) { return false; }
        P->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Station + 1);
        return C.Send();
    }

    bool InvalidControl(APlayerController* PC, int32 Serial,
        int64 SourceGeneration, int32 Station, int64 Generation)
    {
        FCall C(PC, TEXT("Server_RequestStateMachineControl"));
        auto* S = Field<FIntProperty>(C.Function, TEXT("Serial"));
        auto* SG = Field<FInt64Property>(C.Function, TEXT("SourceGeneration"));
        auto* I = Field<FIntProperty>(C.Function, TEXT("StationIndex"));
        auto* G = Field<FInt64Property>(C.Function, TEXT("Generation"));
        if (!S || !SG || !I || !G || C.Function->NumParms != 4 || !C.Args)
        { return false; }
        void* Data = C.Args->GetStructMemory();
        S->SetPropertyValue_InContainer(Data, Serial);
        SG->SetPropertyValue_InContainer(Data, SourceGeneration);
        I->SetPropertyValue_InContainer(Data, Station);
        G->SetPropertyValue_InContainer(Data, Generation);
        return C.Send();
    }

    AActor* SourceActor(AGameStateBase* GS, int32 Index)
    {
        FCall C(GS, TEXT("Get_SourceActor"));
        auto* I = Field<FIntProperty>(C.Function, TEXT("Index"));
        auto* Result = Field<FObjectProperty>(C.Function, TEXT("ReturnValue"));
        if (!I || !Result || C.Function->NumParms != 2 || !C.Args)
        { return nullptr; }
        I->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Index);
        return C.Send() ? Cast<AActor>(Result->GetObjectPropertyValue_InContainer(
            C.Args->GetStructMemory())) : nullptr;
    }

    bool SourceRoots(AActor* Actor, FCk_Handle& Primary,
        TArray<FCk_Handle>& Passes)
    {
        Primary = {}; Passes.Reset();
        FCall P(Actor, TEXT("Get_SourcePrimary"));
        auto* PH = Field<FStructProperty>(P.Function, TEXT("ReturnValue"));
        FCall R(Actor, TEXT("Get_SourceRoots"));
        auto* RA = Field<FArrayProperty>(R.Function, TEXT("ReturnValue"));
        auto* Inner = RA ? CastField<FStructProperty>(RA->Inner) : nullptr;
        if (!PH || PH->Struct != FCk_Handle::StaticStruct() ||
            !RA || !Inner || Inner->Struct != FCk_Handle::StaticStruct() ||
            P.Function->NumParms != 1 || R.Function->NumParms != 1 ||
            !P.Send() || !R.Send()) { return false; }
        Primary = *PH->ContainerPtrToValuePtr<FCk_Handle>(P.Args->GetStructMemory());
        FScriptArrayHelper Array(RA,
            RA->ContainerPtrToValuePtr<void>(R.Args->GetStructMemory()));
        for (int32 N = 0; N < Array.Num(); ++N)
        { Passes.Add(*reinterpret_cast<FCk_Handle*>(Array.GetRawPtr(N))); }
        return true;
    }

    bool Same(const FCkStateMachineGym_WireSnapshot& A,
        const FCkStateMachineGym_WireSnapshot& B)
    { return FCkStateMachineGym_WireSnapshot::StaticStruct()->
        CompareScriptStruct(&A, &B, PPF_None); }

    uint64 IrisId(UWorld* World, AActor* Actor)
    {
        if (!IsValid(World) || !IsValid(Actor) || Actor->GetWorld() != World)
        { return 0; }
        auto* Driver = World->GetNetDriver();
        auto* System = IsValid(Driver) ? Driver->GetReplicationSystem() : nullptr;
        auto* Bridge = IsValid(System) ? System->GetReplicationBridge() : nullptr;
        if (!IsValid(Bridge)) { return 0; }
        const auto Handle = Bridge->GetReplicatedRefHandle(Actor);
        return Handle.IsValid() ? Handle.GetId() : 0;
    }

    AActor* Replica(UWorld* World, UClass* Class, uint64 ExpectedId)
    {
        if (!IsValid(World) || !IsValid(Class) || ExpectedId == 0)
        { return nullptr; }
        AActor* Match = nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetClass() != Class || IrisId(World, *It) != ExpectedId)
            { continue; }
            if (Match) { return nullptr; }
            Match = *It;
        }
        return Match;
    }

    struct FBank
    {
        TWeakObjectPtr<AActor> Actor;
        FCk_Handle Primary;
        TArray<FCk_Handle> Passes;
        TArray<FCk_Handle> Forest;
    };

    struct FRun
    {
        bool Dedicated = false, Captured = false, SettingsCaptured = false, Failed = false;
        UClass *BaseMode = nullptr, *Mode = nullptr, *StateClass = nullptr;
        UClass *PCClass = nullptr, *PawnClass = nullptr, *SourceBase = nullptr;
        FStructProperty* SnapshotProperty = nullptr;
        FStructProperty* AckProperty = nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<AGameStateBase> State[Worlds];
        TWeakObjectPtr<APlayerController> Local[Clients], Remote[Clients], Host;
        FCk_Handle WorldRoot[Worlds], Station[Worlds][Stations];
        FBank Bank[Stations], Before[Stations];
        FCkStateMachineGym_WireSnapshot Previous;
        int64 LastSequence = 0;
        int64 ObservedSequence[Worlds]{};
        TMap<int64, FCkStateMachineGym_WireSnapshot> AuthorityHistory;
        int32 NextSerial[3]{1, 1, 1};
        int32 Rerun = 0, Owner = -1, Serial = 0;
        enum class EStep : uint8 { Baseline, Send, Pending, Wait, Invalid, Terminal, Failed, Done }
            Step = EStep::Baseline;
        double Deadline = 0.0, InvalidUntil = 0.0;
        ECkGym_StartupMode Startup{};
        FString LastGym;
        TArray<FString> Recents;
        void Restore()
        {
            if (!SettingsCaptured) { return; }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Startup;
            Settings->LastGymName = LastGym;
            Settings->RecentGymNames = Recents;
            Settings->SaveConfig();
            SettingsCaptured = false;
        }
        ~FRun() { Restore(); }
    };

    const FCkStateMachineGym_WireSnapshot* Frame(const FRun& R, int32 World)
    { return R.SnapshotProperty && R.State[World].IsValid()
        ? R.SnapshotProperty->ContainerPtrToValuePtr<FCkStateMachineGym_WireSnapshot>(
            R.State[World].Get()) : nullptr; }

    const FCkStateMachineGym_ControlAck* Ack(const FRun& R, APlayerController* PC)
    { return R.AckProperty && IsValid(PC)
        ? R.AckProperty->ContainerPtrToValuePtr<FCkStateMachineGym_ControlAck>(PC)
        : nullptr; }

    bool ObserveFrames(FRun& R)
    {
        const auto* Authority = Frame(R, 0);
        if (!Authority || Authority->SourceGeneration != 1 ||
            Authority->Sequence < R.ObservedSequence[0]) { return false; }
        if (Authority->Sequence > 0)
        { R.AuthorityHistory.Add(Authority->Sequence, *Authority); }
        R.ObservedSequence[0] = Authority->Sequence;
        for (int32 W = 1; W < Worlds; ++W)
        {
            const auto* Received = Frame(R, W);
            if (!Received || Received->SourceGeneration != 1 ||
                Received->Sequence < R.ObservedSequence[W] ||
                Received->Sequence > Authority->Sequence) { return false; }
            // A client may receive a frame between test ticks. When its
            // authority frame was sampled, require an exact value copy.
            const auto* Known = R.AuthorityHistory.Find(Received->Sequence);
            if (Known && !Same(*Known, *Received)) { return false; }
            R.ObservedSequence[W] = Received->Sequence;
        }
        return true;
    }

    bool PlayerReady(APlayerController* PC, UWorld* World, const FRun& R)
    {
        if (!IsValid(PC) || PC->GetClass() != R.PCClass ||
            PC->GetWorld() != World || !PC->HasActorBegunPlay()) { return false; }
        auto* Pawn = PC->GetPawn().Get();
        auto* PS = PC->PlayerState.Get();
        return IsValid(Pawn) && Pawn->GetClass() == R.PawnClass &&
            Pawn->GetWorld() == World && Pawn->GetController() == PC &&
            Pawn->HasActorBegunPlay() && IsValid(PS) &&
            PS->GetWorld() == World && Pawn->GetPlayerState() == PS;
    }

    bool Context(FRun& R)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        if (!IsValid(Server) || !Server->HasBegunPlay() ||
            Server->GetNetMode() != (R.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            ck::auto_test::net::Get_AllPIEWorlds().Num() != Worlds ||
            ck::auto_test::net::Get_NumClientWorlds() != Clients ||
            !IsValid(Server->GetAuthGameMode()) ||
            Server->GetAuthGameMode()->GetClass() != R.Mode ||
            !IsValid(Server->GetGameState()) ||
            Server->GetGameState()->GetClass() != R.StateClass) { return false; }
        UWorld* WorldsNow[Worlds]{Server};
        AGameStateBase* StatesNow[Worlds]{Server->GetGameState()};
        APlayerController* LocalsNow[Clients]{}, *RemotesNow[Clients]{};
        FCk_Handle RootsNow[Worlds]{Root(Server)};
        if (!Allocated(RootsNow[0])) { return false; }
        for (int32 I = 0; I < Clients; ++I)
        {
            WorldsNow[I + 1] = ck::auto_test::net::Get_ClientWorld(I);
            StatesNow[I + 1] = IsValid(WorldsNow[I + 1])
                ? WorldsNow[I + 1]->GetGameState() : nullptr;
            LocalsNow[I] = IsValid(WorldsNow[I + 1])
                ? WorldsNow[I + 1]->GetFirstPlayerController() : nullptr;
            RemotesNow[I] = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I);
            RootsNow[I + 1] = Root(WorldsNow[I + 1]);
            if (!IsValid(WorldsNow[I + 1]) || WorldsNow[I + 1]->GetNetMode() != NM_Client ||
                !IsValid(StatesNow[I + 1]) || StatesNow[I + 1]->GetClass() != R.StateClass ||
                !PlayerReady(LocalsNow[I], WorldsNow[I + 1], R) ||
                !LocalsNow[I]->IsLocalController() ||
                !PlayerReady(RemotesNow[I], Server, R) ||
                RemotesNow[I]->IsLocalController() || !Allocated(RootsNow[I + 1]))
            { return false; }
        }
        auto* GI = Server->GetGameInstance();
        if (!IsValid(GI) || GI->GetLocalPlayers().Num() != (R.Dedicated ? 0 : 1))
        { return false; }
        APlayerController* HostNow = nullptr;
        if (!R.Dedicated)
        {
            auto* LP = GI->GetLocalPlayers()[0];
            HostNow = IsValid(LP) ? LP->GetPlayerController(Server) : nullptr;
            if (!PlayerReady(HostNow, Server, R) || !HostNow->IsLocalController())
            { return false; }
        }
        if (R.Captured)
        {
            if (R.Host.Get() != HostNow) { return false; }
            for (int32 I = 0; I < Worlds; ++I)
            { if (R.World[I].Get() != WorldsNow[I] || R.State[I].Get() != StatesNow[I] ||
                  R.WorldRoot[I] != RootsNow[I]) { return false; } }
            for (int32 I = 0; I < Clients; ++I)
            { if (R.Local[I].Get() != LocalsNow[I] || R.Remote[I].Get() != RemotesNow[I])
              { return false; } }
            return true;
        }
        for (int32 I = 0; I < Worlds; ++I)
        { R.World[I] = WorldsNow[I]; R.State[I] = StatesNow[I]; R.WorldRoot[I] = RootsNow[I]; }
        for (int32 I = 0; I < Clients; ++I)
        { R.Local[I] = LocalsNow[I]; R.Remote[I] = RemotesNow[I]; }
        R.Host = HostNow;
        R.Captured = true;
        return true;
    }

    bool Station(FRun& R, int32 WorldIndex, int32 Index)
    {
        APlayerController* PC = WorldIndex == 0 ? R.Host.Get()
            : R.Local[WorldIndex - 1].Get();
        if (!IsValid(PC)) { return false; }
        const auto Owner = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(PC);
        const FName Tag(*FString::Printf(TEXT("%s_%s"), Tags[Index], *PC->GetName()));
        const auto Matches = UCk_Utils_EntityTag_UE::ForEach_Entity(R.WorldRoot[WorldIndex], Tag);
        if (!Allocated(Owner) || Matches.Num() != 1 || !Allocated(Matches[0]) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Matches[0]))
        { return false; }
        const auto Cohort = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Matches[0]);
        if (!Direct(Matches[0], Cohort) || !Direct(Cohort, Owner) ||
            (Allocated(R.Station[WorldIndex][Index]) &&
                R.Station[WorldIndex][Index] != Matches[0])) { return false; }
        R.Station[WorldIndex][Index] = Matches[0];
        return true;
    }

    bool ExactActorBank(const FRun& R, int32 WorldIndex)
    {
        auto* World = R.World[WorldIndex].Get();
        if (!IsValid(World) || !IsValid(R.SourceBase)) { return false; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        { Count += It->IsA(R.SourceBase) && !It->IsActorBeingDestroyed(); }
        return Count == Stations;
    }

    bool Bank(FRun& R, int32 Index)
    {
        auto* Actor = SourceActor(R.State[0].Get(), Index);
        if (!IsValid(Actor) || Actor->GetWorld() != R.World[0].Get() ||
            !Actor->HasAuthority() || Actor->IsActorBeingDestroyed() ||
            !Bool(Actor, TEXT("Get_SourcePrepared")) ||
            !Bool(Actor, TEXT("Get_SourceStarted")) ||
            Bool(Actor, TEXT("Get_SourceClosed"))) { return false; }
        FCk_Handle Primary;
        TArray<FCk_Handle> Passes;
        if (!SourceRoots(Actor, Primary, Passes) ||
            Passes.Num() != ((Index == 4 || Index == 5 || Index == 6) ? 2 : 1) ||
            !UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePrimary(Primary, Actor))
        { return false; }
        for (const auto& Pass : Passes)
        { if (!UCk_StateMachineGym_Runtime_UE::TryValidate_DivergencePass(Pass, Primary))
          { return false; } }
        TArray<FCk_Handle> Forest, Parents, Census;
        FString Why;
        if (!UCk_StateMachineGym_Runtime_UE::TryCollect_DivergenceHealthyForest(
                Primary, Actor, Passes, Forest, Parents) ||
            !ck_selection_gym_census::TryCensusAllocatedBank(
                R.World[0].Get(), Primary, {}, Census, Why) ||
            Forest.IsEmpty() || Forest.Last() != Primary ||
            Forest.Num() != Parents.Num() || Forest.Num() != Census.Num() ||
            !Direct(Primary, R.WorldRoot[0])) { return false; }
        for (int32 N = 0; N < Forest.Num(); ++N)
        { if (!Allocated(Forest[N]) || !Census.Contains(Forest[N]) ||
              !Direct(Forest[N], Parents[N])) { return false; } }
        auto& Receipt = R.Bank[Index];
        if (Receipt.Actor.IsValid() && Receipt.Actor.Get() != Actor &&
            R.Step != FRun::EStep::Wait) { return false; }
        Receipt.Actor = Actor;
        Receipt.Primary = Primary;
        Receipt.Passes = Passes;
        Receipt.Forest = MoveTemp(Forest);
        return true;
    }

    bool Converged(FRun& R, int32 Selected = INDEX_NONE)
    {
        if (!Context(R)) { return false; }
        const auto* Source = Frame(R, 0);
        if (!Source || Source->SourceGeneration != 1 || Source->Phase != 2 ||
            Source->Stations.Num() != Stations || Source->Replacing != INDEX_NONE ||
            Source->Sequence <= 0) { return false; }
        if (R.LastSequence > 0 && Source->Sequence < R.LastSequence) { return false; }
        TSet<AActor*> Seen;
        for (int32 I = 0; I < Stations; ++I)
        {
            if (Source->Stations[I].Index != I || Source->Stations[I].Generation <= 0 ||
                !Bank(R, I) || Seen.Contains(R.Bank[I].Actor.Get())) { return false; }
            Seen.Add(R.Bank[I].Actor.Get());
        }
        if (!ExactActorBank(R, 0)) { return false; }
        for (int32 W = 1; W < Worlds; ++W)
        {
            if (!ExactActorBank(R, W)) { return false; }
            const auto* Received = Frame(R, W);
            if (!Received || !Same(*Source, *Received) ||
                !Bool(R.Local[W - 1].Get(), TEXT("Get_IsLocalPresentationReady")) ||
                IrisId(R.World[0].Get(), R.State[0].Get()) == 0 ||
                IrisId(R.World[0].Get(), R.State[0].Get()) !=
                    IrisId(R.World[W].Get(), R.State[W].Get())) { return false; }
            for (int32 I = 0; I < Stations; ++I)
            {
                if (!Station(R, W, I)) { return false; }
                const uint64 Id = IrisId(R.World[0].Get(), R.Bank[I].Actor.Get());
                auto* ClientActor = Replica(R.World[W].Get(),
                    R.Bank[I].Actor->GetClass(), Id);
                if (!IsValid(ClientActor) || ClientActor->HasAuthority() ||
                    ClientActor->GetWorld() != R.World[W].Get())
                { return false; }
                FCk_Handle LocalPrimary;
                TArray<FCk_Handle> LocalPasses;
                if (!SourceRoots(ClientActor, LocalPrimary, LocalPasses) ||
                    Allocated(LocalPrimary) || !LocalPasses.IsEmpty())
                { return false; }
            }
        }
        if (!R.Dedicated)
        {
            if (!Bool(R.Host.Get(), TEXT("Get_IsLocalPresentationReady")))
            { return false; }
            for (int32 I = 0; I < Stations; ++I)
            { if (!Station(R, 0, I)) { return false; } }
        }
        if (Selected != INDEX_NONE &&
            (Source->Stations[Selected].Generation !=
                R.Previous.Stations[Selected].Generation + 1 ||
             Source->Sequence <= R.Previous.Sequence)) { return false; }
        R.LastSequence = Source->Sequence;
        return true;
    }

    FString Summary(const FRun& R)
    {
        const auto* F = Frame(R, 0);
        return FString::Printf(TEXT("StateMachine phase=%d rerun=%d sequence=%lld selected=%d"),
            F ? F->Phase : -1, R.Rerun, F ? F->Sequence : int64{-1},
            F ? F->Replacing : -1);
    }

    class FDriver final : public IAutomationLatentCommand
    {
    public:
        FDriver(FAutomationTestBase* InTest, TSharedRef<FRun> InRun)
            : Test(InTest), Run(InRun) {}

        bool Update() override
        {
            if (Run->Step == FRun::EStep::Done) { return true; }
            if (Run->Deadline == 0.0) { Run->Deadline = FPlatformTime::Seconds() + 90.0; }
            if (FPlatformTime::Seconds() > Run->Deadline)
            { const auto S = Summary(*Run); return Stop(*S); }
            if (!Context(*Run)) { return Stop(TEXT("StateMachine world or owner changed")); }
            if (!ObserveFrames(*Run))
            { return Stop(TEXT("StateMachine frame order or replicated value changed")); }
            const auto* Source = Frame(*Run, 0);
            if (!Source) { return Stop(TEXT("StateMachine authority frame missing")); }
            if (Run->Step == FRun::EStep::Baseline)
            {
                if (!Converged(*Run)) { return false; }
                Run->Step = FRun::EStep::Send;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Send)
            {
                if (Run->Rerun >= Stations)
                { Run->Step = FRun::EStep::Invalid; return false; }
                if (!Converged(*Run)) { return false; }
                Run->Previous = *Source;
                for (int32 I = 0; I < Stations; ++I) { Run->Before[I] = Run->Bank[I]; }
                Run->Owner = !Run->Dedicated && Run->Rerun == 8
                    ? 2 : Run->Rerun % 2;
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                Run->Serial = Run->NextSerial[Run->Owner]++;
                if (!Activate(Sender, Run->Rerun))
                { return Stop(TEXT("StateMachine owned control unavailable")); }
                Run->Step = FRun::EStep::Pending;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Pending)
            {
                if (Source->Phase != 3 || Source->Replacing != Run->Rerun)
                { return false; }
                // The same owner must not admit a second request while its
                // first request is in flight. Using its authority counterpart
                // avoids changing another client's serial/ack stream.
                auto* Owner = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Remote[Run->Owner].Get();
                if (!InvalidControl(Owner, Run->Serial + 1, 1,
                        Run->Rerun,
                        Run->Previous.Stations[Run->Rerun].Generation))
                { return Stop(TEXT("StateMachine in-flight rejection seam unavailable")); }
                Run->Step = FRun::EStep::Wait;
                return false;
            }
            if (Run->Step == FRun::EStep::Wait)
            {
                const int32 Selected = Run->Rerun;
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                const auto* Receipt = Ack(*Run, Sender);
                if (!Receipt || Receipt->Serial != Run->Serial ||
                    Receipt->Result != 1 || Receipt->StationIndex != Selected ||
                    Receipt->RequestedGeneration !=
                        Run->Previous.Stations[Selected].Generation) { return false; }
                if (!Converged(*Run, Selected)) { return false; }
                Source = Frame(*Run, 0);
                if (Receipt->CompletedGeneration != Source->Stations[Selected].Generation ||
                    Receipt->CompletedSequence > Source->Sequence ||
                    Receipt->CompletedSequence <= Run->Previous.Sequence)
                { return false; }
                for (int32 I = 0; I < Stations; ++I)
                {
                    if (I == Selected)
                    {
                        if (Run->Before[I].Actor.Get() == Run->Bank[I].Actor.Get() ||
                            Run->Before[I].Primary == Run->Bank[I].Primary)
                        { return Stop(TEXT("selected StateMachine bank was not replaced")); }
                        for (const auto& Old : Run->Before[I].Forest)
                        { if (Allocated(Old))
                          { return Stop(TEXT("old StateMachine forest survived replacement")); } }
                    }
                    else if (Run->Before[I].Actor.Get() != Run->Bank[I].Actor.Get() ||
                        Run->Before[I].Primary != Run->Bank[I].Primary ||
                        Source->Stations[I].Generation !=
                            Run->Previous.Stations[I].Generation ||
                        !Allocated(Run->Before[I].Primary))
                    { return Stop(TEXT("a peer StateMachine bank changed during rerun")); }
                }
                Run->Rerun++;
                Run->Step = FRun::EStep::Send;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Invalid)
            {
                if (!Converged(*Run)) { return false; }
                Run->Previous = *Source;
                for (int32 I = 0; I < Stations; ++I) { Run->Before[I] = Run->Bank[I]; }
                // Send malformed index and stale generation directly through
                // the real server RPC on an owned authority controller.
                if (!InvalidControl(Run->Remote[0].Get(),
                        Run->NextSerial[0], 1, Stations, 1) ||
                    !InvalidControl(Run->Remote[1].Get(),
                        Run->NextSerial[1], 1, 0, 0))
                { return Stop(TEXT("StateMachine invalid control seam unavailable")); }
                Run->InvalidUntil = FPlatformTime::Seconds() + 0.25;
                Run->Step = FRun::EStep::Terminal;
                return false;
            }
            if (Run->Step == FRun::EStep::Terminal)
            {
                if (FPlatformTime::Seconds() < Run->InvalidUntil) { return false; }
                if (Source->Phase != 2 || Source->Replacing != INDEX_NONE ||
                    Source->Sequence < Run->Previous.Sequence)
                { return Stop(TEXT("malformed or stale StateMachine control changed lifecycle")); }
                for (int32 I = 0; I < Stations; ++I)
                {
                    auto* Actor = SourceActor(Run->State[0].Get(), I);
                    FCk_Handle Primary;
                    FCall P(Actor, TEXT("Get_SourcePrimary"));
                    auto* H = Field<FStructProperty>(P.Function, TEXT("ReturnValue"));
                    if (Source->Stations[I].Generation !=
                            Run->Previous.Stations[I].Generation ||
                        Actor != Run->Before[I].Actor.Get() ||
                        !H || H->Struct != FCk_Handle::StaticStruct() ||
                        !P.Send())
                    { return Stop(TEXT("invalid control changed a source bank")); }
                    Primary = *H->ContainerPtrToValuePtr<FCk_Handle>(
                        P.Args->GetStructMemory());
                    if (Primary != Run->Before[I].Primary || !Allocated(Primary))
                    { return Stop(TEXT("invalid control changed a source primary")); }
                }
                FCall Fail(Run->State[0].Get(), TEXT("Request_Fail"));
                auto* Reason = Field<FStrProperty>(Fail.Function, TEXT("Reason"));
                if (!Reason || Fail.Function->NumParms != 1 || !Fail.Args)
                { return Stop(TEXT("StateMachine terminal failure boundary unavailable")); }
                Reason->SetPropertyValue_InContainer(Fail.Args->GetStructMemory(),
                    TEXT("authored terminal failure gate"));
                if (!Fail.Send())
                { return Stop(TEXT("StateMachine terminal failure call failed")); }
                Run->Step = FRun::EStep::Failed;
                return false;
            }
            if (Run->Step == FRun::EStep::Failed)
            {
                if (Source->Phase != 4 ||
                    !Bool(Run->State[0].Get(), TEXT("Get_HasSessionFailure")))
                { return false; }
                for (int32 W = 1; W < Worlds; ++W)
                {
                    const auto* Received = Frame(*Run, W);
                    if (!Received || !Same(*Source, *Received) ||
                        !Bool(Run->State[W].Get(), TEXT("Get_HasSessionFailure")))
                    { return false; }
                }
                for (int32 I = 0; I < Stations; ++I)
                {
                    auto* Actor = Run->Bank[I].Actor.Get();
                    if (IsValid(Actor) && !Bool(Actor, TEXT("Get_SourceClosed")))
                    { return false; }
                    for (const auto& Pass : Run->Bank[I].Passes)
                    {
                        if (!Allocated(Pass)) { continue; }
                        if (!UCk_Utils_StateMachine_UE::Has(Pass) ||
                            UCk_Utils_StateMachine_UE::Get_RunStatus(
                                UCk_Utils_StateMachine_UE::Cast(Pass)) !=
                                ECk_SmRunStatus::Stopped)
                        { return false; }
                    }
                }
                const auto FailedFrame = *Source;
                if (!InvalidControl(Run->Remote[0].Get(),
                        Run->NextSerial[0], 1, 0,
                        FailedFrame.Stations[0].Generation) ||
                    !Same(FailedFrame, *Frame(*Run, 0)))
                { return Stop(TEXT("terminal StateMachine session accepted another control")); }
                Run->Step = FRun::EStep::Done;
                return false;
            }
            return Stop(TEXT("unexpected StateMachine gate phase"));
        }
    private:
        bool Stop(const TCHAR* Why)
        {
            Run->Failed = true;
            Test->AddError(Why);
            ck::auto_test::net::FailSession(Why);
            Run->Step = FRun::EStep::Done;
            return true;
        }
        FAutomationTestBase* Test;
        TSharedRef<FRun> Run;
    };

    struct FRestore final : IAutomationLatentCommand
    {
        TSharedRef<FRun> Run;
        explicit FRestore(TSharedRef<FRun> In) : Run(In) {}
        bool Update() override { Run->Restore(); return true; }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_SharedWorld,
    "Ck.StateMachine.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_SharedWorld::RunTest(const FString& Parameters)
{
    using namespace ck_sm_authored_network;
    auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported StateMachine topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->SourceBase = FSoftClassPath(SourceBasePath).TryLoadClass<AActor>();
    Run->SnapshotProperty = Field<FStructProperty>(Run->StateClass, TEXT("Snapshot"));
    Run->AckProperty = Field<FStructProperty>(Run->PCClass, TEXT("LastControlAck"));
    if (!TestTrue(TEXT("authored StateMachine classes and reflected controls"),
            Run->BaseMode && Run->Mode && Run->StateClass && Run->PCClass &&
            Run->PawnClass && Run->SourceBase &&
            Run->SnapshotProperty && Run->AckProperty &&
            Run->SnapshotProperty->Struct == FCkStateMachineGym_WireSnapshot::StaticStruct() &&
            Run->AckProperty->Struct == FCkStateMachineGym_ControlAck::StaticStruct() &&
            Run->PCClass->FindFunctionByName(TEXT("Request_ControlActivated")) &&
            Run->PCClass->FindFunctionByName(TEXT("Get_IsLocalPresentationReady")) &&
            Run->PCClass->FindFunctionByName(TEXT("Server_RequestStateMachineControl")) &&
            Run->StateClass->FindFunctionByName(TEXT("Get_SourceActor")) &&
            Run->StateClass->FindFunctionByName(TEXT("Request_Fail"))))
    { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    Run->Startup = Settings->StartupMode;
    Run->LastGym = Settings->LastGymName;
    Run->Recents = Settings->RecentGymNames;
    Run->SettingsCaptured = true;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode =
        ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        Worlds, EntryMap, Parameters, Run->BaseMode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(Worlds, 60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            auto* W = ck::auto_test::net::Get_ServerWorld();
            auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
            auto* Registry = IsValid(GI)
                ? GI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
            return IsValid(Registry) &&
                Registry->Find_GymIndexByName(TEXT("State Machine")) != INDEX_NONE;
        }), 30, TEXT("real StateMachine gym registry entry available")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner requests real StateMachine gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("State Machine")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Context(*Run); }), 60,
        TEXT("exact StateMachine worlds and owners converge")));
    ADD_LATENT_AUTOMATION_COMMAND(FDriver(this, Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        {
            if (Run->Failed) { return true; }
            const auto* F = Frame(*Run, 0);
            if (!F || F->Phase != 4 || !Bool(Run->State[0].Get(),
                    TEXT("Get_HasSessionFailure"))) { return false; }
            for (int32 I = 1; I < Worlds; ++I)
            { const auto* C = Frame(*Run, I);
              if (!C || !Same(*F, *C) || !Bool(Run->State[I].Get(),
                    TEXT("Get_HasSessionFailure"))) { return false; } }
            return true;
        }), 30, TEXT("terminal StateMachine failure replicates")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FRestore(Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        {
            if (Run->Failed || !Run->Captured ||
                ck::auto_test::net::Get_ServerWorld() != nullptr ||
                ck::auto_test::net::Get_NumClientWorlds() != 0) { return false; }
            for (const auto& H : Run->WorldRoot)
            { if (Allocated(H)) { return false; } }
            for (const auto& Bank : Run->Bank)
            { for (const auto& H : Bank.Forest)
              { if (Allocated(H)) { return false; } } }
            return true;
        }), TEXT("StateMachine authored worlds close after terminal failure"), true));
    return true;
}

#endif
