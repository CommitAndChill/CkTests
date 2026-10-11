#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkChainGym_Wire.h"
#include "CkChainGym_Runtime.h"
#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_WireTransformRuntime.h"
#include "CkSelectionGym_BankCensus.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkChain/CkChain_Fragment.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
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

namespace ck_chain_authored_network
{
    constexpr int32 Worlds = 3, Clients = 2, Stations = 4;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_ChainGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_ChainGym_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_ChainGym_PlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_Gym_Base_Pawn");
    constexpr int32 Actions[]{0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 8};
    const TCHAR* Tags[Stations]{TEXT("Gym.CkChain.Train"), TEXT("Gym.CkChain.Rope"),
        TEXT("Gym.CkChain.Held"), TEXT("Gym.CkChain.WagonsWithChildren")};

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

    bool Bool(UObject* Object, const TCHAR* Name, bool& Out)
    {
        Out = false;
        FCall C(Object, Name);
        auto* P = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool String(UObject* Object, const TCHAR* Name, FString& Out)
    {
        Out.Reset();
        FCall C(Object, Name);
        auto* P = Field<FStrProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = P->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Handle(AGameStateBase* GS, const TCHAR* Name, int32 Index, FCk_Handle& Out)
    {
        Out = {};
        FCall C(GS, Name);
        auto* I = Field<FIntProperty>(C.Function, TEXT("Index"));
        auto* R = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!I || !R || R->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 2 || !C.Args) { return false; }
        I->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Index);
        if (!C.Send()) { return false; }
        Out = *R->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory());
        return true;
    }

    bool Activate(APlayerController* PC, int32 Row)
    {
        FCall C(PC, TEXT("Request_ControlActivated"));
        auto* P = Field<FIntProperty>(C.Function, TEXT("RowIndex"));
        if (!P || C.Function->NumParms != 1 || !C.Args) { return false; }
        P->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Row);
        return C.Send();
    }

    bool SendControl(APlayerController* PC, int32 Serial,
        int64 SourceGeneration, int64 Generation, int32 Action)
    {
        FCall C(PC, TEXT("Server_RequestChainControl"));
        auto* S = Field<FIntProperty>(C.Function, TEXT("Serial"));
        auto* SG = Field<FInt64Property>(C.Function, TEXT("SourceGeneration"));
        auto* G = Field<FInt64Property>(C.Function, TEXT("Generation"));
        auto* A = Field<FIntProperty>(C.Function, TEXT("Action"));
        if (!S || !SG || !G || !A || C.Function->NumParms != 4 || !C.Args)
        { return false; }
        void* Data = C.Args->GetStructMemory();
        S->SetPropertyValue_InContainer(Data, Serial);
        SG->SetPropertyValue_InContainer(Data, SourceGeneration);
        G->SetPropertyValue_InContainer(Data, Generation);
        A->SetPropertyValue_InContainer(Data, Action);
        return C.Send();
    }

    bool ExpectedIris(const FCkChainGym_WireSnapshot& Authority,
        const FCkChainGym_WireSnapshot& Received)
    {
        if (Authority.Stations.Num() != Received.Stations.Num()) { return false; }
        auto Expected = Authority;
        for (int32 I = 0; I < Authority.Stations.Num(); ++I)
        {
            const auto& A = Authority.Stations[I];
            const auto& B = Received.Stations[I];
            if (A.Links.Num() != B.Links.Num() || A.Lamps.Num() != B.Lamps.Num() ||
                !UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                    A.Head, B.Head)) { return false; }
            Expected.Stations[I].Head = B.Head;
            for (int32 J = 0; J < A.Links.Num(); ++J)
            {
                if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                        A.Links[J].Pose, B.Links[J].Pose)) { return false; }
                Expected.Stations[I].Links[J].Pose = B.Links[J].Pose;
            }
            for (int32 J = 0; J < A.Lamps.Num(); ++J)
            {
                if (!UCk_Gym_WireTransform_Runtime_UE::Get_IsExactIrisTransform(
                        A.Lamps[J], B.Lamps[J])) { return false; }
                Expected.Stations[I].Lamps[J] = B.Lamps[J];
            }
        }
        return FCkChainGym_WireSnapshot::StaticStruct()->
            CompareScriptStruct(&Expected, &Received, PPF_None);
    }

    struct FBank
    {
        FCk_Handle Root, Head, Chain;
        TArray<FCk_Handle> Forest;
    };

    struct FRun
    {
        bool Dedicated = false, Captured = false, SettingsCaptured = false, Failed = false;
        UClass *BaseMode = nullptr, *Mode = nullptr, *StateClass = nullptr;
        UClass *PCClass = nullptr, *PawnClass = nullptr;
        FStructProperty *SnapshotProperty = nullptr, *AckProperty = nullptr;
        TWeakObjectPtr<UWorld> World[Worlds];
        TWeakObjectPtr<AGameStateBase> State[Worlds];
        TWeakObjectPtr<APlayerController> Local[Clients], Remote[Clients], Host;
        FCk_Handle WorldRoot[Worlds];
        FCk_Handle LocalStation[Worlds][Stations];
        FBank Bank[Stations], Before[Stations];
        FCkChainGym_WireSnapshot BeforeFrame;
        TMap<int64, FCkChainGym_WireSnapshot> History;
        int64 Observed[Worlds]{};
        int32 NextSerial[3]{1, 1, 1};
        int32 ActionIndex = 0, Owner = -1, Serial = 0, DepartedOwner = -1;
        FString LastBlock;
        TWeakObjectPtr<APawn> DepartedPawn;
        double Deadline = 0.0, SettleUntil = 0.0;
        FVector InitialTrain = FVector::ZeroVector;
        enum class EStep : uint8 { Baseline, Toggle, Send, Wait, Motion,
            FollowSend, FollowWait, Depart, Fallback, PeerSend, PeerWait,
            Invalid, Terminal, Failed, PostFailed, Done }
            Step = EStep::Baseline;
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

    const FCkChainGym_WireSnapshot* Frame(const FRun& R, int32 W)
    { return R.SnapshotProperty && R.State[W].IsValid()
        ? R.SnapshotProperty->ContainerPtrToValuePtr<FCkChainGym_WireSnapshot>(
            R.State[W].Get()) : nullptr; }

    const FCkChainGym_ControlAck* Ack(const FRun& R, APlayerController* PC)
    { return R.AckProperty && IsValid(PC)
        ? R.AckProperty->ContainerPtrToValuePtr<FCkChainGym_ControlAck>(PC)
        : nullptr; }

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

    bool DepartedPlayer(APlayerController* PC, UWorld* World, const FRun& R)
    {
        return IsValid(PC) && PC->GetClass() == R.PCClass &&
            PC->GetWorld() == World && PC->HasActorBegunPlay() &&
            IsValid(PC->PlayerState.Get()) &&
            PC->PlayerState->GetWorld() == World;
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
                !(R.DepartedOwner == I
                    ? DepartedPlayer(LocalsNow[I], WorldsNow[I + 1], R)
                    : PlayerReady(LocalsNow[I], WorldsNow[I + 1], R)) ||
                !LocalsNow[I]->IsLocalController() ||
                !(R.DepartedOwner == I
                    ? DepartedPlayer(RemotesNow[I], Server, R)
                    : PlayerReady(RemotesNow[I], Server, R)) ||
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

    bool Observe(FRun& R)
    {
        const auto* A = Frame(R, 0);
        if (!A || A->SourceGeneration != 1 || A->Sequence < R.Observed[0])
        { return false; }
        if (A->Sequence > 0) { R.History.Add(A->Sequence, *A); }
        R.Observed[0] = A->Sequence;
        for (int32 W = 1; W < Worlds; ++W)
        {
            const auto* B = Frame(R, W);
            if (!B || B->SourceGeneration != 1 || B->Sequence < R.Observed[W] ||
                B->Sequence > A->Sequence) { return false; }
            const auto* Known = R.History.Find(B->Sequence);
            if (Known && !ExpectedIris(*Known, *B)) { return false; }
            R.Observed[W] = B->Sequence;
        }
        return true;
    }

    bool Bank(FRun& R, int32 I, FBank& Out)
    {
        Out = {};
        if (!Handle(R.State[0].Get(), TEXT("Get_SourceRoot"), I, Out.Root) ||
            !Handle(R.State[0].Get(), TEXT("Get_SourceHead"), I, Out.Head) ||
            !Handle(R.State[0].Get(), TEXT("Get_SourceChain"), I, Out.Chain) ||
            !Direct(Out.Root, R.WorldRoot[0]) || !Direct(Out.Head, Out.Root) ||
            !Direct(Out.Chain, Out.Head)) { return false; }
        FString Why;
        if (!ck_selection_gym_census::TryCensusAllocatedBank(
                R.World[0].Get(), Out.Root, {}, Out.Forest, Why) ||
            !Out.Forest.Contains(Out.Root) || !Out.Forest.Contains(Out.Head) ||
            !Out.Forest.Contains(Out.Chain)) { return false; }
        return true;
    }

    bool Station(FRun& R, int32 W, int32 I)
    {
        auto* PC = W == 0 ? R.Host.Get() : R.Local[W - 1].Get();
        if (!IsValid(PC)) { return false; }
        const auto Owner = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(PC);
        const FName Tag(*FString::Printf(TEXT("%s_%s"), Tags[I], *PC->GetName()));
        const auto Matches = UCk_Utils_EntityTag_UE::ForEach_Entity(R.WorldRoot[W], Tag);
        if (!Allocated(Owner) || Matches.Num() != 1 || !Allocated(Matches[0]) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Matches[0]))
        { return false; }
        const auto Cohort = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Matches[0]);
        if (!Direct(Matches[0], Cohort) || !Direct(Cohort, Owner) ||
            (Allocated(R.LocalStation[W][I]) &&
                R.LocalStation[W][I] != Matches[0])) { return false; }
        R.LocalStation[W][I] = Matches[0];
        return true;
    }

    bool SourceCensus(const FRun& R, int32 W, int32 Expected)
    {
        if (!Allocated(R.WorldRoot[W])) { return false; }
        const auto Registry = R.WorldRoot[W].Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        int32 Count = 0;
        bool Foreign = false;
        Registry.View<ck::FFragment_Chain>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Chain&)
            {
                const FCk_Handle Chain{Entity, RegistryHandle};
                if (!Allocated(Chain)) { return; }
                ++Count;
                if (W == 0)
                {
                    bool InSource = false;
                    for (const auto& Bank : R.Bank)
                    { InSource |= Bank.Forest.Contains(Chain); }
                    Foreign |= !InSource;
                }
            });
        return Count == Expected && !Foreign;
    }

    bool Block(FRun& R, const TCHAR* Why)
    { R.LastBlock = Why; return false; }

    bool Converged(FRun& R, int64 Generation)
    {
        if (!Context(R)) { return Block(R, TEXT("world/player context")); }
        const auto* A = Frame(R, 0);
        if (!A || A->Phase != 2 || A->Replacing != -1 ||
            A->ControlGeneration != Generation || A->Sequence <= 0 ||
            A->Stations.Num() != Stations)
        { return Block(R, TEXT("authority active frame/generation")); }
        for (int32 I = 0; I < Stations; ++I)
        {
            const auto& S = A->Stations[I];
            if (S.Index != I || S.Generation <= 0 ||
                S.Links.Num() != (I == 1 ? 12 : 8) ||
                (S.HasSplit ? (I != 0 || S.LinkCount != 3 ||
                    S.SplitLinkCount != 4) :
                    (S.LinkCount != S.Links.Num() || S.SplitLinkCount != 0)) ||
                S.Lamps.Num() != (I == 3 ? 8 : 0) ||
                !Bank(R, I, R.Bank[I]))
            { return Block(R, *FString::Printf(TEXT("authority station %d source/frame"), I)); }
            for (int32 J = 0; J < I; ++J)
            { if (R.Bank[I].Root == R.Bank[J].Root)
              { return Block(R, TEXT("authority source roots overlap")); } }
            if (I == 3)
            {
                for (int32 J = 0; J < S.Lamps.Num(); ++J)
                {
                    if (!S.Lamps[J].GetLocation().Equals(
                            S.Links[J].Pose.TransformPosition(FVector(0, 0, 65)), 2.0))
                    { return Block(R, TEXT("lamp-child transform")); }
                }
            }
        }
        if (!SourceCensus(R, 0, A->Stations[0].HasSplit ? 5 : 4))
        { return Block(R, TEXT("authority Chain fragment census")); }
        for (int32 W = 1; W < Worlds; ++W)
        {
            const auto* B = Frame(R, W);
            bool Ready = false;
            const auto* Known = B ? R.History.Find(B->Sequence) : nullptr;
            if (!B || B->Phase != 2 || B->ControlGeneration != Generation ||
                B->Stations.Num() != Stations ||
                B->Replacing != -1 || !Known || !ExpectedIris(*Known, *B))
            { return Block(R, *FString::Printf(TEXT("client %d known same-generation frame"), W)); }
            for (int32 I = 0; I < Stations; ++I)
            {
                if (B->Stations[I].Generation != A->Stations[I].Generation)
                { return Block(R, *FString::Printf(TEXT("client %d station generation"), W)); }
            }
            if (R.DepartedOwner != W - 1 &&
                (!Bool(R.Local[W - 1].Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                    !Ready))
            { return Block(R, *FString::Printf(TEXT("client %d local presentation"), W)); }
            for (int32 I = 0; I < Stations; ++I)
            {
                FCk_Handle LocalRoot, LocalHead, LocalChain;
                if (!Handle(R.State[W].Get(), TEXT("Get_SourceRoot"), I, LocalRoot) ||
                    !Handle(R.State[W].Get(), TEXT("Get_SourceHead"), I, LocalHead) ||
                    !Handle(R.State[W].Get(), TEXT("Get_SourceChain"), I, LocalChain) ||
                    Allocated(LocalRoot) || Allocated(LocalHead) || Allocated(LocalChain))
                { return Block(R, *FString::Printf(TEXT("client %d source handle"), W)); }
                if (R.DepartedOwner != W - 1 && !Station(R, W, I))
                { return Block(R, *FString::Printf(TEXT("client %d station ownership"), W)); }
            }
            if (!SourceCensus(R, W, 0))
            { return Block(R, *FString::Printf(TEXT("client %d Chain census"), W)); }
        }
        if (!R.Dedicated)
        {
            bool Ready = false;
            if (!Bool(R.Host.Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                !Ready) { return Block(R, TEXT("host local presentation")); }
            for (int32 I = 0; I < Stations; ++I)
            { if (!Station(R, 0, I))
              { return Block(R, TEXT("host station ownership")); } }
        }
        R.LastBlock.Reset();
        return true;
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
            { return Stop(TEXT("authored Chain world/control did not settle")); }
            if (!Context(*Run) || !Observe(*Run))
            { return Stop(TEXT("Chain captured world, ordered frame or exact value changed")); }
            const auto* A = Frame(*Run, 0);
            if (A && A->Phase == 4 && Run->Step != FRun::EStep::Failed &&
                Run->Step != FRun::EStep::PostFailed)
            { return Stop(TEXT("Chain source entered unexpected terminal phase")); }
            if (Run->Step == FRun::EStep::Baseline)
            {
                if (!Converged(*Run, 1)) { return false; }
                if (A->Stations[2].Moving ||
                    !A->Stations[2].Links.ContainsByPredicate(
                        [](const FCkChainGym_LinkFrame& L) { return L.IsHeld; }))
                { return Stop(TEXT("Held station did not start parked")); }
                Run->InitialTrain = A->Stations[0].Head.GetLocation();
                Run->Step = FRun::EStep::Toggle;
                return false;
            }
            if (Run->Step == FRun::EStep::Toggle)
            {
                bool AHistory = false, BHistory = false, ATargets = false, BTargets = false;
                auto* First = Run->Local[0].Get();
                auto* Second = Run->Local[1].Get();
                if (!Bool(First, TEXT("Get_DrawHistory"), AHistory) ||
                    !Bool(Second, TEXT("Get_DrawHistory"), BHistory) ||
                    !Bool(First, TEXT("Get_DrawTargets"), ATargets) ||
                    !Bool(Second, TEXT("Get_DrawTargets"), BTargets) ||
                    !Activate(First, 20) || !Activate(Second, 21))
                { return Stop(TEXT("Chain local overlay toggles unavailable")); }
                bool AfterAHistory = false, AfterBHistory = false;
                bool AfterATargets = false, AfterBTargets = false;
                if (!Bool(First, TEXT("Get_DrawHistory"), AfterAHistory) ||
                    !Bool(Second, TEXT("Get_DrawHistory"), AfterBHistory) ||
                    !Bool(First, TEXT("Get_DrawTargets"), AfterATargets) ||
                    !Bool(Second, TEXT("Get_DrawTargets"), AfterBTargets) ||
                    AfterAHistory == AHistory || AfterBHistory != BHistory ||
                    AfterATargets != ATargets || AfterBTargets == BTargets ||
                    A->ControlGeneration != 1)
                { return Stop(TEXT("Chain local B/J controls crossed players or source")); }
                Run->Step = FRun::EStep::Motion;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Motion)
            {
                if (!Converged(*Run, 1) ||
                    A->Stations[0].Head.GetLocation().Equals(Run->InitialTrain, 5.0))
                { return false; }
                Run->Step = FRun::EStep::Send;
                return false;
            }
            if (Run->Step == FRun::EStep::Send)
            {
                if (Run->ActionIndex >= UE_ARRAY_COUNT(Actions))
                { Run->Step = FRun::EStep::FollowSend; return false; }
                const int64 BeforeGeneration = A->ControlGeneration;
                if (!Converged(*Run, BeforeGeneration)) { return false; }
                Run->BeforeFrame = *A;
                for (int32 I = 0; I < Stations; ++I) { Run->Before[I] = Run->Bank[I]; }
                Run->Owner = !Run->Dedicated &&
                    Run->ActionIndex == UE_ARRAY_COUNT(Actions) - 1
                    ? 2 : Run->ActionIndex % 2;
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                Run->Serial = Run->NextSerial[Run->Owner]++;
                const int32 Action = Actions[Run->ActionIndex];
                if (!Activate(Sender, 11 + Action))
                { return Stop(TEXT("authored Chain action control unavailable")); }
                Run->Step = FRun::EStep::Wait;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Wait)
            {
                const int32 Action = Actions[Run->ActionIndex];
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                const auto* Receipt = Ack(*Run, Sender);
                if (!Receipt || Receipt->Serial != Run->Serial ||
                    Receipt->Action != Action || Receipt->Result != 1 ||
                    Receipt->RequestedGeneration !=
                        Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration !=
                        Run->BeforeFrame.ControlGeneration + 1 ||
                    !Converged(*Run, Run->BeforeFrame.ControlGeneration + 1))
                { return false; }
                A = Frame(*Run, 0);
                if (Receipt->CompletedSequence > A->Sequence ||
                    Receipt->CompletedSequence <= Run->BeforeFrame.Sequence)
                { return false; }
                if (Run->Owner < Clients)
                {
                    const auto* Received = Frame(*Run, Run->Owner + 1);
                    if (!Received || Receipt->CompletedSequence > Received->Sequence)
                    { return false; }
                }
                for (int32 I = 0; I < Stations; ++I)
                {
                    const bool Replaced = Action <= 2 ? I == 0 : Action == 8;
                    if (Replaced)
                    {
                        if (Run->Bank[I].Root == Run->Before[I].Root ||
                            A->Stations[I].Generation !=
                                Run->BeforeFrame.Stations[I].Generation + 1)
                        { return Stop(TEXT("Chain replacement did not mint a station bank")); }
                        for (const auto& H : Run->Before[I].Forest)
                        { if (Allocated(H))
                          { return Stop(TEXT("old Chain bank survived replacement")); } }
                    }
                    else if (Run->Bank[I].Root != Run->Before[I].Root ||
                        A->Stations[I].Generation !=
                            Run->BeforeFrame.Stations[I].Generation)
                    { return Stop(TEXT("unselected Chain bank changed identity")); }
                }
                if (Action <= 2 && !FMath::IsNearlyEqual(A->Stations[0].Spacing,
                        Action == 0 ? 4.0f : (Action == 1 ? 10.0f : 25.0f), 0.01f))
                { return Stop(TEXT("Chain spacing control did not reach source")); }
                if (Action == 4 && !A->Stations[0].HasSplit)
                { return Stop(TEXT("Chain split did not publish")); }
                if (Action == 4 && (A->Stations[0].LinkCount != 3 ||
                        A->Stations[0].SplitLinkCount != 4 ||
                        A->Stations[0].Links.Num() != 8))
                { return Stop(TEXT("Chain split lost its fixed visual slots")); }
                if (Action == 6 && !A->Stations[2].Moving)
                { return Stop(TEXT("held Chain did not launch")); }
                if (Action == 7 && !A->Stations[1].FollowingPawn)
                { return Stop(TEXT("Rope did not follow requesting pawn")); }
                Run->ActionIndex++;
                Run->Step = FRun::EStep::Send;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::FollowSend)
            {
                if (!Converged(*Run, A->ControlGeneration)) { return false; }
                Run->BeforeFrame = *A;
                for (int32 I = 0; I < Stations; ++I) { Run->Before[I] = Run->Bank[I]; }
                Run->Owner = 1;
                Run->Serial = Run->NextSerial[1]++;
                if (!Activate(Run->Local[1].Get(), 18))
                { return Stop(TEXT("departing Rope owner could not start follow")); }
                Run->Step = FRun::EStep::FollowWait;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::FollowWait)
            {
                const auto* Receipt = Ack(*Run, Run->Local[1].Get());
                if (!Receipt || Receipt->Serial != Run->Serial ||
                    Receipt->Action != 7 || Receipt->Result != 1 ||
                    Receipt->RequestedGeneration != Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration != Run->BeforeFrame.ControlGeneration + 1 ||
                    !Converged(*Run, Run->BeforeFrame.ControlGeneration + 1))
                { return false; }
                A = Frame(*Run, 0);
                const auto* Received = Frame(*Run, 2);
                if (!Received || Receipt->CompletedSequence > Received->Sequence ||
                    !A->Stations[1].FollowingPawn)
                { return false; }
                for (int32 I = 0; I < Stations; ++I)
                {
                    if (Run->Bank[I].Root != Run->Before[I].Root ||
                        A->Stations[I].Generation !=
                            Run->BeforeFrame.Stations[I].Generation)
                    { return Stop(TEXT("Rope follow changed a source identity")); }
                }
                Run->Step = FRun::EStep::Depart;
                return false;
            }
            if (Run->Step == FRun::EStep::Depart)
            {
                auto* Remote = Run->Remote[1].Get();
                Run->DepartedPawn = IsValid(Remote) ? Remote->GetPawn().Get() : nullptr;
                if (!IsValid(Remote) || !Run->DepartedPawn.IsValid())
                { return Stop(TEXT("Rope follow owner pawn unavailable")); }
                Remote->UnPossess();
                if (!Run->DepartedPawn.IsValid() ||
                    Remote->GetPawn().Get() != nullptr ||
                    Run->DepartedPawn->GetController() != nullptr)
                { return Stop(TEXT("Rope follow owner was not unpossessed")); }
                Run->DepartedOwner = 1;
                Run->BeforeFrame = *A;
                Run->Step = FRun::EStep::Fallback;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Fallback)
            {
                if (!Converged(*Run, Run->BeforeFrame.ControlGeneration + 1))
                { return false; }
                A = Frame(*Run, 0);
                if (A->Stations[1].FollowingPawn || !A->Stations[1].Moving ||
                    A->SourceGeneration != Run->BeforeFrame.SourceGeneration ||
                    Run->Remote[1]->GetPawn().Get() != nullptr)
                { return Stop(TEXT("Rope did not return to source spline after departure")); }
                for (int32 I = 0; I < Stations; ++I)
                {
                    if (Run->Bank[I].Root != Run->Before[I].Root ||
                        A->Stations[I].Generation !=
                            Run->BeforeFrame.Stations[I].Generation)
                    { return Stop(TEXT("Rope fallback replaced a station")); }
                }
                Run->Step = FRun::EStep::PeerSend;
                return false;
            }
            if (Run->Step == FRun::EStep::PeerSend)
            {
                if (!Converged(*Run, A->ControlGeneration)) { return false; }
                Run->BeforeFrame = *A;
                for (int32 I = 0; I < Stations; ++I) { Run->Before[I] = Run->Bank[I]; }
                Run->Owner = 0;
                Run->Serial = Run->NextSerial[0]++;
                if (!Activate(Run->Local[0].Get(), 11))
                { return Stop(TEXT("remaining Chain owner could not request spacing")); }
                Run->Step = FRun::EStep::PeerWait;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::PeerWait)
            {
                const auto* Receipt = Ack(*Run, Run->Local[0].Get());
                if (!Receipt || Receipt->Serial != Run->Serial ||
                    Receipt->Action != 0 || Receipt->Result != 1 ||
                    Receipt->RequestedGeneration != Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration != Run->BeforeFrame.ControlGeneration + 1 ||
                    !Converged(*Run, Run->BeforeFrame.ControlGeneration + 1))
                { return false; }
                A = Frame(*Run, 0);
                const auto* Received = Frame(*Run, 1);
                if (!Received || Receipt->CompletedSequence > Received->Sequence ||
                    !FMath::IsNearlyEqual(A->Stations[0].Spacing, 4.0f, 0.01f))
                { return false; }
                for (int32 I = 0; I < Stations; ++I)
                {
                    if (I == 0)
                    {
                        if (Run->Bank[I].Root == Run->Before[I].Root ||
                            A->Stations[I].Generation !=
                                Run->BeforeFrame.Stations[I].Generation + 1)
                        { return Stop(TEXT("remaining owner did not replace Train")); }
                        for (const auto& H : Run->Before[I].Forest)
                        { if (Allocated(H))
                          { return Stop(TEXT("remaining owner left old Train receipt")); } }
                    }
                    else if (Run->Bank[I].Root != Run->Before[I].Root ||
                        A->Stations[I].Generation !=
                            Run->BeforeFrame.Stations[I].Generation)
                    { return Stop(TEXT("remaining owner replaced a peer")); }
                }
                Run->Step = FRun::EStep::Invalid;
                return false;
            }
            if (Run->Step == FRun::EStep::Invalid)
            {
                if (!Converged(*Run, A->ControlGeneration)) { return false; }
                Run->BeforeFrame = *A;
                for (int32 I = 0; I < Stations; ++I) { Run->Before[I] = Run->Bank[I]; }
                if (!SendControl(Run->Local[0].Get(), Run->NextSerial[0], 1,
                        A->ControlGeneration, 99) ||
                    !SendControl(Run->Local[0].Get(), Run->NextSerial[0], 1,
                        A->ControlGeneration - 1, 0))
                { return Stop(TEXT("malformed/stale Chain RPC unavailable")); }
                Run->SettleUntil = FPlatformTime::Seconds() + 0.25;
                Run->Step = FRun::EStep::Terminal;
                return false;
            }
            if (Run->Step == FRun::EStep::Terminal)
            {
                if (FPlatformTime::Seconds() < Run->SettleUntil) { return false; }
                if (A->Phase != 2 || A->ControlGeneration !=
                        Run->BeforeFrame.ControlGeneration)
                { return Stop(TEXT("invalid Chain request changed source generation")); }
                const auto* Rejection = Ack(*Run, Run->Local[0].Get());
                if (!Rejection || Rejection->Serial != Run->NextSerial[0] ||
                    Rejection->Result != 2 || Rejection->Action != 0)
                { return false; }
                for (int32 I = 0; I < Stations; ++I)
                {
                    FCk_Handle Current;
                    if (!Handle(Run->State[0].Get(), TEXT("Get_SourceRoot"), I, Current) ||
                        Current != Run->Before[I].Root ||
                        A->Stations[I].Generation !=
                            Run->BeforeFrame.Stations[I].Generation)
                    { return Stop(TEXT("invalid Chain request replaced a source bank")); }
                }
                FCall Fail(Run->State[0].Get(), TEXT("Request_Fail"));
                auto* Reason = Field<FStrProperty>(Fail.Function, TEXT("Reason"));
                if (!Reason || Fail.Function->NumParms != 1 || !Fail.Args)
                { return Stop(TEXT("Chain terminal boundary unavailable")); }
                Reason->SetPropertyValue_InContainer(Fail.Args->GetStructMemory(),
                    TEXT("authored terminal Chain gate"));
                if (!Fail.Send()) { return Stop(TEXT("Chain terminal failure call failed")); }
                Run->Step = FRun::EStep::Failed;
                return false;
            }
            if (Run->Step == FRun::EStep::Failed)
            {
                bool Failed = false;
                if (A->Phase != 4 || !Bool(Run->State[0].Get(),
                        TEXT("Get_HasSessionFailure"), Failed) || !Failed)
                { return false; }
                for (int32 W = 1; W < Worlds; ++W)
                {
                    const auto* B = Frame(*Run, W);
                    bool Closed = false;
                    if (!B || !ExpectedIris(*A, *B) ||
                        !Bool(Run->State[W].Get(),
                            TEXT("Get_HasSessionFailure"), Closed) || !Closed)
                    { return false; }
                }
                for (const auto& Bank : Run->Before)
                {
                    for (const auto& H : Bank.Forest)
                    { if (Allocated(H)) { return false; } }
                }
                if (!SourceCensus(*Run, 0, 0) ||
                    !SourceCensus(*Run, 1, 0) || !SourceCensus(*Run, 2, 0))
                { return false; }
                const auto TerminalFrame = *A;
                if (!SendControl(Run->Local[0].Get(), Run->NextSerial[0] + 1, 1,
                        A->ControlGeneration, 8) ||
                    A->ControlGeneration != TerminalFrame.ControlGeneration ||
                    A->Phase != 4)
                { return Stop(TEXT("terminal Chain session accepted a later action")); }
                Run->BeforeFrame = TerminalFrame;
                Run->SettleUntil = FPlatformTime::Seconds() + 0.25;
                Run->Step = FRun::EStep::PostFailed;
                return false;
            }
            if (Run->Step == FRun::EStep::PostFailed)
            {
                if (FPlatformTime::Seconds() < Run->SettleUntil) { return false; }
                const auto* Rejection = Ack(*Run, Run->Local[0].Get());
                if (!Rejection || Rejection->Serial != Run->NextSerial[0] + 1 ||
                    Rejection->Action != 8 || Rejection->Result != 2)
                { return false; }
                if (A->Phase != 4 || A->ControlGeneration !=
                    Run->BeforeFrame.ControlGeneration ||
                    A->Sequence != Run->BeforeFrame.Sequence)
                { return Stop(TEXT("terminal Chain session mutated after late control")); }
                Run->Step = FRun::EStep::Done;
                return false;
            }
            return Stop(TEXT("unexpected Chain authored gate phase"));
        }
    private:
        bool Stop(const TCHAR* Why)
        {
            Run->Failed = true;
            const auto* A = Frame(*Run, 0);
            FString Diagnostic;
            String(Run->State[0].Get(), TEXT("Get_DiagnosticState"), Diagnostic);
            FString ClientsText;
            for (int32 W = 1; W < Worlds; ++W)
            {
                const auto* B = Frame(*Run, W);
                bool Ready = false;
                Bool(Run->Local[W - 1].Get(),
                    TEXT("Get_IsLocalPresentationReady"), Ready);
                ClientsText += FString::Printf(TEXT(" c%d=%lld/%d"), W,
                    static_cast<long long>(B ? B->Sequence : -1), Ready ? 1 : 0);
            }
            auto* Sender = Run->Owner == 2 ? Run->Host.Get() :
                (Run->Owner >= 0 && Run->Owner < Clients
                    ? Run->Local[Run->Owner].Get() : nullptr);
            const auto* Receipt = Ack(*Run, Sender);
            FString OwnerDiagnostic;
            String(Sender, TEXT("Get_DiagnosticState"), OwnerDiagnostic);
            Test->AddError(FString::Printf(
                TEXT("%s [step=%d actionIndex=%d blocked=%s authority=%d/%lld/%lld%s ack=%d/%d state=%s owner=%s]"),
                Why, static_cast<int32>(Run->Step), Run->ActionIndex,
                *Run->LastBlock, A ? A->Phase : -1,
                static_cast<long long>(A ? A->ControlGeneration : -1),
                static_cast<long long>(A ? A->Sequence : -1), *ClientsText,
                Receipt ? Receipt->Serial : -1, Receipt ? Receipt->Result : -1,
                *Diagnostic, *OwnerDiagnostic));
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

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkChainGym_SharedWorld,
    "Ck.Chain.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkChainGym_SharedWorld::RunTest(const FString& Parameters)
{
    using namespace ck_chain_authored_network;
    auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported Chain topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->SnapshotProperty = Field<FStructProperty>(Run->StateClass, TEXT("Snapshot"));
    Run->AckProperty = Field<FStructProperty>(Run->PCClass, TEXT("LastControlAck"));
    TArray<FVector> History, SplitHistory, Targets;
    TArray<bool> HasTargets, Held;
    History.Add(FVector(1, 2, 3));
    Targets.Add(FVector(4, 5, 6));
    HasTargets.Add(true);
    Held.Add(true);
    if (!TestTrue(TEXT("default Chain debug request rejects and clears outputs"),
            !UCk_ChainGym_Runtime_UE::TryGet_DebugValues(
                FCk_Handle_Chain{}, FCk_Handle_Chain{}, {}, History,
                SplitHistory, Targets, HasTargets, Held) && History.IsEmpty() &&
            SplitHistory.IsEmpty() && Targets.IsEmpty() && HasTargets.IsEmpty() &&
            Held.IsEmpty()))
    { return false; }
    if (!TestTrue(TEXT("authored Chain classes and reflected controls"),
            Run->BaseMode && Run->Mode && Run->StateClass && Run->PCClass &&
            Run->PawnClass && Run->SnapshotProperty && Run->AckProperty &&
            Run->SnapshotProperty->Struct == FCkChainGym_WireSnapshot::StaticStruct() &&
            Run->AckProperty->Struct == FCkChainGym_ControlAck::StaticStruct() &&
            Run->PCClass->FindFunctionByName(TEXT("Request_ControlActivated")) &&
            Run->PCClass->FindFunctionByName(TEXT("Get_IsLocalPresentationReady")) &&
            Run->PCClass->FindFunctionByName(TEXT("Server_RequestChainControl")) &&
            Run->StateClass->FindFunctionByName(TEXT("Get_SourceRoot")) &&
            Run->StateClass->FindFunctionByName(TEXT("Get_SourceHead")) &&
            Run->StateClass->FindFunctionByName(TEXT("Get_SourceChain")) &&
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
                Registry->Find_GymIndexByName(TEXT("Chain")) != INDEX_NONE;
        }), 30, TEXT("real Chain gym registry entry available")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner requests real Chain gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Chain")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Context(*Run); }), 60,
        TEXT("exact Chain worlds and owners converge")));
    ADD_LATENT_AUTOMATION_COMMAND(FDriver(this, Run));
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
            return true;
        }), TEXT("Chain authored worlds close after terminal failure"), true));
    return true;
}

#endif
