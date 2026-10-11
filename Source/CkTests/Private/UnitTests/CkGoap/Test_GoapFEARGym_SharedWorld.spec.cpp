#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGoapFEARGym_Wire.h"
#include "CkGoapFEARGym_Runtime.h"
#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkGoap/CkGoap_Fragment_Data.h"
#include "CkGoap/Planner/CkGoap_Planner_Fragment.h"
#include "CkGoap/WorldState/CkGoap_WorldState_Fragment.h"
#include "CkGoap/Action/CkGoap_Action_Fragment.h"
#include "CkTimer/CkTimer_Utils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_goap_fear_authored_network
{
    constexpr int32 Worlds = 3, Clients = 2, Stations = 1;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_GoapFEARGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_GoapFEARGym_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_GoapFEARGym_PlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_Gym_Base_Pawn");
    const TCHAR* Tags[Stations]{TEXT("Gym.GoapFEAR.Station.Combatant")};

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

    bool Handle(AGameStateBase* GS, int32 Index, FCk_Handle& Out)
    {
        Out = {};
        FCall C(GS, TEXT("Get_SourceRoot"));
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
        FCall C(PC, TEXT("Server_RequestGoapFEARControl"));
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

    bool Same(const FCkGoapFEARGym_WireSnapshot& A,
        const FCkGoapFEARGym_WireSnapshot& B)
    { return UCk_GoapFEARGym_Runtime_UE::Get_IsSameFrame(A, B); }

    struct FBank
    {
        FCk_Handle Root;
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
        FCk_Handle WorldRoot[Worlds], LocalStation[Worlds][Stations];
        FBank Bank[Stations], Before[Stations];
        TMap<int64, FCkGoapFEARGym_WireSnapshot> History;
        int64 Observed[Worlds]{};
        int32 NextSerial[3]{1, 1, 1};
        int32 ActionIndex = 0, Owner = -1, Serial = 0;
        int32 ExpectedMask = 0;
        double Deadline = 0.0, SettleUntil = 0.0;
        FString LastBlock;
        FCkGoapFEARGym_WireSnapshot BeforeFrame;
        enum class EStep : uint8 { Baseline, Send, Wait, Invalid, Terminal,
            Failed, PostFailed, Done } Step = EStep::Baseline;
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

    const FCkGoapFEARGym_WireSnapshot* Frame(const FRun& R, int32 W)
    { return R.SnapshotProperty && R.State[W].IsValid()
        ? R.SnapshotProperty->ContainerPtrToValuePtr<FCkGoapFEARGym_WireSnapshot>(
            R.State[W].Get()) : nullptr; }

    const FCkGoapFEARGym_ControlAck* Ack(const FRun& R, APlayerController* PC)
    { return R.AckProperty && IsValid(PC)
        ? R.AckProperty->ContainerPtrToValuePtr<FCkGoapFEARGym_ControlAck>(PC)
        : nullptr; }

    bool Presented(APlayerController* PC, FCkGoapFEARGym_WireSnapshot& Out)
    {
        Out = {};
        FCall C(PC, TEXT("Get_PresentedFrame"));
        auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || P->Struct != FCkGoapFEARGym_WireSnapshot::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = *P->ContainerPtrToValuePtr<FCkGoapFEARGym_WireSnapshot>(
            C.Args->GetStructMemory());
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
            if (Known && !Same(*Known, *B)) { return false; }
            R.Observed[W] = B->Sequence;
        }
        return true;
    }

    bool SourceCensus(const FRun& R, int32 W, int32 Expected)
    {
        if (!Allocated(R.WorldRoot[W])) { return false; }
        const auto Registry = R.WorldRoot[W].Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        int32 PlannerCount = 0, WorldStateCount = 0, ActionCount = 0;
        int32 PerSource[Stations]{};
        bool Foreign = false;
        Registry.View<ck::FFragment_Goap_Planner>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Goap_Planner&)
            {
                const FCk_Handle H{Entity, RegistryHandle};
                if (!Allocated(H)) { return; }
                ++PlannerCount;
                if (W == 0)
                {
                    int32 Owners = 0;
                    for (int32 I = 0; I < Stations; ++I)
                    { if (R.Bank[I].Forest.Contains(H) &&
                          UCk_GoapFEARGym_Runtime_UE::Get_IsLivePlanner(
                              H, R.World[0].Get()))
                      { ++PerSource[I]; ++Owners; } }
                    Foreign |= Owners != 1;
                }
            });
        Registry.View<ck::FFragment_Goap_WorldState_Values>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Goap_WorldState_Values&)
            { WorldStateCount += Allocated(FCk_Handle{Entity, RegistryHandle}); });
        Registry.View<ck::FFragment_Goap_Action>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Goap_Action&)
            { ActionCount += Allocated(FCk_Handle{Entity, RegistryHandle}); });
        if (W == 0 && Expected == Stations)
        {
            for (int32 I = 0; I < Stations; ++I)
            { if (PerSource[I] != 2) { return false; } }
        }
        return PlannerCount == Expected * 2 && WorldStateCount == Expected &&
            ActionCount == (Expected == 0 ? 0 : 11) && !Foreign;
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

    bool Bank(FRun& R, int32 I, FBank& Out)
    {
        Out = {};
        if (!Handle(R.State[0].Get(), I, Out.Root) ||
            !Direct(Out.Root, R.WorldRoot[0])) { return false; }
        return UCk_GoapFEARGym_Runtime_UE::TryGet_SourceForest(
            R.World[0].Get(), Out.Root, Out.Forest) &&
            Out.Forest.Contains(Out.Root);
    }

    bool Block(FRun& R, const TCHAR* Why)
    { R.LastBlock = Why; return false; }

    bool ValidFrame(const FCkGoapFEARGym_StationFrame& S,
        int32 Mask, int64 Controls)
    {
        if (S.Index != 0 || S.Generation != 1 ||
            S.WorldStateMask != Mask || Mask < 0 || Mask > 255 ||
            S.PlanCount < 1 || S.ControlCount != Controls ||
            S.PlanStatus != static_cast<int32>(ECk_GoapPlanStatus::PlanFound) ||
            S.Title.IsEmpty() || !FMath::IsFinite(S.PlanCost) ||
            !FMath::IsFinite(S.SubPlanCost) ||
            S.PlanCost < 0.0f || S.SubPlanCost < 0.0f ||
            S.PlanActions.Num() < 1 || S.PlanActions.Num() > 7 ||
            S.SubPlanActions.Num() > 4 || S.ActiveChain.Num() > 2)
        { return false; }
        for (const int32 Code : S.PlanActions)
        { if (Code < 0 || Code > 6) { return false; } }
        for (const int32 Code : S.SubPlanActions)
        { if (Code < 0 || Code > 3) { return false; } }
        for (const int32 Code : S.ActiveChain)
        { if (Code < 0 || Code > 10) { return false; } }

        const int32 First = S.PlanActions[0];
        if (S.ActiveChain.IsEmpty() || S.ActiveChain[0] != First)
        { return false; }
        int32 ExpectedTop[2]{}, ExpectedLength = 0;
        float ExpectedCost = 0.0f;
        int32 ExpectedLeaf = -1;
        float ExpectedSubCost = 0.0f;
        switch (Mask)
        {
        case 24: case 39:
            ExpectedTop[0] = 6; ExpectedLength = 1;
            ExpectedCost = 999.0f; break;
        case 56:
            ExpectedTop[0] = 4; ExpectedTop[1] = 0;
            ExpectedLength = 2; ExpectedCost = 2.5f; break;
        case 55:
            ExpectedTop[0] = 3; ExpectedTop[1] = 0;
            ExpectedLength = 2; ExpectedCost = 1.5f; break;
        case 57:
            ExpectedTop[0] = 0; ExpectedLength = 1;
            ExpectedCost = 1.0f; ExpectedLeaf = 2;
            ExpectedSubCost = 2.0f; break;
        case 59:
            ExpectedTop[0] = 0; ExpectedLength = 1;
            ExpectedCost = 1.0f; ExpectedLeaf = 0;
            ExpectedSubCost = 1.0f; break;
        case 63: case 47:
            ExpectedTop[0] = 0; ExpectedLength = 1;
            ExpectedCost = 1.0f; ExpectedLeaf = 1;
            ExpectedSubCost = 0.5f; break;
        default: return false;
        }
        if (S.PlanActions.Num() != ExpectedLength ||
            !FMath::IsNearlyEqual(S.PlanCost, ExpectedCost, 0.01f))
        { return false; }
        for (int32 I = 0; I < ExpectedLength; ++I)
        { if (S.PlanActions[I] != ExpectedTop[I]) { return false; } }
        if (ExpectedLeaf < 0) { return true; }
        return S.SubPlanCount > 0 &&
            S.SubPlanStatus == static_cast<int32>(ECk_GoapPlanStatus::PlanFound) &&
            S.SubPlanActions.Num() == 1 &&
            S.SubPlanActions[0] == ExpectedLeaf &&
            FMath::IsNearlyEqual(S.SubPlanCost, ExpectedSubCost, 0.01f) &&
            S.ActiveChain.Num() == 2 &&
            S.ActiveChain[0] == 0 &&
            S.ActiveChain[1] == 7 + ExpectedLeaf;
    }

    bool Converged(FRun& R, int64 Generation, int32 ExpectedMask,
        int64 CompletedControls, bool ExpectInjectedRequesterNotReady = false)
    {
        if (!Context(R)) { return Block(R, TEXT("world/player context")); }
        const auto* A = Frame(R, 0);
        if (!A || A->Phase != 2 || A->Replacing != -1 ||
            A->ControlGeneration != Generation || A->SourceGeneration != 1 ||
            A->Sequence <= 0 || A->Stations.Num() != Stations ||
            !ValidFrame(A->Stations[0], ExpectedMask, CompletedControls))
        { return Block(R, TEXT("authority active top/sub plan, chain or control count")); }
        if (!Bank(R, 0, R.Bank[0]))
        { return Block(R, TEXT("authority source forest")); }
        if (!SourceCensus(R, 0, Stations))
        { return Block(R, TEXT("authority planner/WS/action census")); }
        for (int32 W = 1; W < Worlds; ++W)
        {
            const auto* B = Frame(R, W);
            const auto* Known = B ? R.History.Find(B->Sequence) : nullptr;
            bool Ready = false;
            // Injected RPCs bypass client 0's local UI serial. Its passive
            // frame remains exact although local presentation is not ready.
            const bool ExpectedReady = !(ExpectInjectedRequesterNotReady && W == 1);
            if (!B || B->Phase != 2 || B->ControlGeneration != Generation ||
                B->Stations.Num() != Stations || !Known || !Same(*Known, *B) ||
                B->Stations[0].WorldStateMask != ExpectedMask ||
                B->Stations[0].ControlCount != CompletedControls ||
                !Bool(R.Local[W - 1].Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                Ready != ExpectedReady)
            { return Block(R, TEXT("client known frame/plan/presentation")); }
            FCkGoapFEARGym_WireSnapshot Drawn;
            if (!Presented(R.Local[W - 1].Get(), Drawn) ||
                Drawn.ControlGeneration != Generation ||
                Drawn.Stations.Num() != Stations ||
                Drawn.Stations[0].WorldStateMask != ExpectedMask ||
                Drawn.Stations[0].ControlCount != CompletedControls ||
                Drawn.Sequence > B->Sequence ||
                !R.History.Contains(Drawn.Sequence) ||
                !Same(*R.History.Find(Drawn.Sequence), Drawn))
            { return Block(R, TEXT("client local presented value frame")); }
            FCk_Handle LocalRoot;
            if (B->Stations[0].Generation != A->Stations[0].Generation ||
                !Handle(R.State[W].Get(), 0, LocalRoot) || Allocated(LocalRoot) ||
                !Station(R, W, 0) || !SourceCensus(R, W, 0))
            { return Block(R, TEXT("client passive source/panel")); }
        }
        if (!R.Dedicated)
        {
            bool Ready = false;
            if (!Bool(R.Host.Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                !Ready) { return Block(R, TEXT("host presentation")); }
            FCkGoapFEARGym_WireSnapshot Drawn;
            if (!Presented(R.Host.Get(), Drawn) ||
                Drawn.ControlGeneration != Generation ||
                Drawn.Stations.Num() != Stations ||
                Drawn.Stations[0].WorldStateMask != ExpectedMask ||
                Drawn.Stations[0].ControlCount != CompletedControls ||
                Drawn.Sequence > A->Sequence ||
                !R.History.Contains(Drawn.Sequence) ||
                !Same(*R.History.Find(Drawn.Sequence), Drawn) ||
                !Station(R, 0, 0))
            { return Block(R, TEXT("host local presented value frame/panel")); }
        }
        R.LastBlock.Reset();
        return true;
    }

    constexpr int32 Actions[]{5, 0, 1, 2, 3, 4, 6, 7, 7};
    constexpr int32 ActionCount = UE_ARRAY_COUNT(Actions);

    class FDriver final : public IAutomationLatentCommand
    {
    public:
        FDriver(FAutomationTestBase* InTest, TSharedRef<FRun> InRun)
            : Test(InTest), Run(InRun) {}
        bool Update() override
        {
            if (Run->Step == FRun::EStep::Done) { return true; }
            if (Run->Deadline == 0.0)
            { Run->Deadline = FPlatformTime::Seconds() + 90.0; }
            if (FPlatformTime::Seconds() > Run->Deadline)
            { return Stop(TEXT("Goap FEAR world/control did not settle")); }
            if (!Context(*Run) || !Observe(*Run))
            { return Stop(TEXT("Goap FEAR context or ordered frame changed")); }
            const auto* A = Frame(*Run, 0);
            if (A && A->Phase == 4 && Run->Step != FRun::EStep::Failed &&
                Run->Step != FRun::EStep::PostFailed)
            { return Stop(TEXT("Goap FEAR unexpected terminal phase")); }
            if (Run->Step == FRun::EStep::Baseline)
            {
                if (!Converged(*Run, 1, 24, 0)) { return false; }
                auto& Forest = Run->Bank[0].Forest;
                if (Forest.Num() != 14)
                { return Stop(TEXT("FEAR source has noncanonical forest size")); }
                FCk_Handle Top, Promoted;
                int32 Planners = 0, WorldStates = 0, FoundActions = 0, Timers = 0;
                for (const auto& H : Forest)
                {
                    if (UCk_GoapFEARGym_Runtime_UE::
                            Get_IsLivePlanner(H, Run->World[0].Get()))
                    {
                        ++Planners;
                        if (Direct(H, Run->Bank[0].Root)) { Top = H; }
                        else { Promoted = H; }
                    }
                    if (UCk_GoapFEARGym_Runtime_UE::
                            Get_IsLiveWorldState(H, Run->World[0].Get()))
                    {
                        ++WorldStates;
                        if (!Direct(H, Run->Bank[0].Root))
                        { return Stop(TEXT("FEAR world state owner")); }
                    }
                    FoundActions += UCk_GoapFEARGym_Runtime_UE::
                        Get_IsLiveAction(H, Run->World[0].Get());
                    Timers += UCk_Utils_Timer_UE::Has(H);
                }
                if (Planners != 2 || WorldStates != 1 || FoundActions != 11 ||
                    Timers != 0 || !Allocated(Top) || !Allocated(Promoted) ||
                    !Direct(Promoted, Top) ||
                    !UCk_GoapFEARGym_Runtime_UE::Get_IsLiveAction(
                        Promoted, Run->World[0].Get()))
                { return Stop(TEXT("authority FEAR promoted forest roles")); }
                int32 Tier1 = 0, Tier2 = 0;
                for (const auto& H : Forest)
                {
                    if (!UCk_GoapFEARGym_Runtime_UE::
                            Get_IsLiveAction(H, Run->World[0].Get()))
                    { continue; }
                    if (Direct(H, Top)) { ++Tier1; }
                    else if (Direct(H, Promoted)) { ++Tier2; }
                    else { return Stop(TEXT("FEAR action escaped planner tree")); }
                }
                if (Tier1 != 7 || Tier2 != 4)
                { return Stop(TEXT("FEAR tier action census")); }
                TArray<FCk_Handle> Rejected;
                Rejected.Add(Run->Bank[0].Root);
                if (UCk_GoapFEARGym_Runtime_UE::TryGet_SourceForest(
                        Run->World[0].Get(), FCk_Handle{}, Rejected) ||
                    !Rejected.IsEmpty() ||
                    UCk_GoapFEARGym_Runtime_UE::Get_IsLiveInWorld(
                        FCk_Handle{}, Run->World[0].Get()) ||
                    UCk_GoapFEARGym_Runtime_UE::Get_IsLivePlanner(
                        FCk_Handle{}, Run->World[0].Get()) ||
                    UCk_GoapFEARGym_Runtime_UE::Get_IsLiveWorldState(
                        FCk_Handle{}, Run->World[0].Get()) ||
                    UCk_GoapFEARGym_Runtime_UE::Get_IsLiveAction(
                        FCk_Handle{}, Run->World[0].Get()))
                { return Stop(TEXT("default FEAR runtime request was not total")); }
                Rejected.Add(Run->Bank[0].Root);
                if (UCk_GoapFEARGym_Runtime_UE::TryGet_SourceForest(
                        Run->World[0].Get(), Run->WorldRoot[1], Rejected) ||
                    !Rejected.IsEmpty() ||
                    UCk_GoapFEARGym_Runtime_UE::Get_IsLiveInWorld(
                        Run->WorldRoot[1], Run->World[0].Get()))
                { return Stop(TEXT("foreign FEAR world was accepted")); }
                Run->ExpectedMask = 24;
                Run->Step = FRun::EStep::Send;
                return false;
            }
            if (Run->Step == FRun::EStep::Send)
            {
                if (Run->ActionIndex >= ActionCount)
                { Run->Step = FRun::EStep::Invalid; return false; }
                if (!Converged(*Run, A->ControlGeneration,
                        Run->ExpectedMask, Run->ActionIndex))
                { return false; }
                Run->BeforeFrame = *A;
                Run->Before[0] = Run->Bank[0];
                const int32 Action = Actions[Run->ActionIndex];
                Run->ExpectedMask = Action == 7 ? 24 :
                    Action == 6 ? (Run->ExpectedMask | 15) :
                    (Run->ExpectedMask ^ (1 << Action));
                Run->Owner = !Run->Dedicated && Run->ActionIndex % 3 == 2
                    ? 2 : Run->ActionIndex % 2;
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                Run->Serial = Run->NextSerial[Run->Owner]++;
                const int32 Row = Action <= 5 ? Action + 1 :
                    Action == 6 ? 8 : 9;
                if (!Activate(Sender, Row))
                { return Stop(TEXT("authored FEAR control unavailable")); }
                Run->Step = FRun::EStep::Wait;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Wait)
            {
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                const auto* Receipt = Ack(*Run, Sender);
                if (!Receipt || Receipt->Serial != Run->Serial ||
                    Receipt->Action != Actions[Run->ActionIndex] ||
                    Receipt->Result != 1 ||
                    Receipt->RequestedGeneration != Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration !=
                        Run->BeforeFrame.ControlGeneration + 1 ||
                    !Converged(*Run, Run->BeforeFrame.ControlGeneration + 1,
                        Run->ExpectedMask, Run->ActionIndex + 1))
                { return false; }
                A = Frame(*Run, 0);
                const auto* Received = Run->Owner == 2 ? A
                    : Frame(*Run, Run->Owner + 1);
                if (!Received || Receipt->CompletedSequence > Received->Sequence ||
                    Receipt->CompletedSequence <= Run->BeforeFrame.Sequence)
                { return false; }
                const bool SelectedAttack = A->Stations[0].PlanActions.Num() > 0 &&
                    A->Stations[0].PlanActions[0] == 0;
                if (A->Stations[0].PlanCount <=
                        Run->BeforeFrame.Stations[0].PlanCount ||
                    (SelectedAttack && A->Stations[0].SubPlanCount <=
                        Run->BeforeFrame.Stations[0].SubPlanCount) ||
                    A->Stations[0].ControlCount !=
                        Run->BeforeFrame.Stations[0].ControlCount + 1)
                { return Stop(TEXT("FEAR control completion not observed once")); }
                if (Run->Bank[0].Root != Run->Before[0].Root ||
                    Run->Bank[0].Forest.Num() != Run->Before[0].Forest.Num() ||
                    A->Stations[0].Generation !=
                        Run->BeforeFrame.Stations[0].Generation)
                { return Stop(TEXT("FEAR control replaced source forest")); }
                for (const auto& H : Run->Before[0].Forest)
                { if (!Allocated(H) || !Run->Bank[0].Forest.Contains(H))
                  { return Stop(TEXT("FEAR control retired source descendant")); } }
                Run->ActionIndex++;
                Run->Step = FRun::EStep::Send;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Invalid)
            {
                if (!Converged(*Run, A->ControlGeneration,
                        Run->ExpectedMask, Run->ActionIndex))
                { return false; }
                Run->BeforeFrame = *A;
                Run->Before[0] = Run->Bank[0];
                if (!SendControl(Run->Local[0].Get(), Run->NextSerial[0], 1,
                        A->ControlGeneration, 99) ||
                    !SendControl(Run->Local[0].Get(), Run->NextSerial[0], 1,
                        A->ControlGeneration - 1, 0))
                { return Stop(TEXT("malformed/stale FEAR RPC unavailable")); }
                Run->SettleUntil = FPlatformTime::Seconds() + 0.25;
                Run->Step = FRun::EStep::Terminal;
                return false;
            }
            if (Run->Step == FRun::EStep::Terminal)
            {
                if (FPlatformTime::Seconds() < Run->SettleUntil) { return false; }
                const auto* Rejection = Ack(*Run, Run->Local[0].Get());
                if (!Rejection || Rejection->Serial != Run->NextSerial[0] ||
                    Rejection->Result != 2 || Rejection->Action != 0)
                { return false; }
                if (A->Phase != 2 || A->ControlGeneration !=
                        Run->BeforeFrame.ControlGeneration ||
                    A->Stations[0].WorldStateMask != Run->ExpectedMask ||
                    A->Stations[0].PlanCount !=
                        Run->BeforeFrame.Stations[0].PlanCount ||
                    A->Stations[0].ControlCount !=
                        Run->BeforeFrame.Stations[0].ControlCount)
                { return Stop(TEXT("invalid FEAR request mutated source")); }
                FCk_Handle Current;
                if (!Handle(Run->State[0].Get(), 0, Current) ||
                    Current != Run->Before[0].Root ||
                    A->Stations[0].Generation !=
                        Run->BeforeFrame.Stations[0].Generation)
                { return Stop(TEXT("invalid FEAR request changed source")); }
                if (!Converged(*Run, A->ControlGeneration,
                        Run->ExpectedMask, Run->ActionIndex, true))
                { return false; }
                Run->Before[0] = Run->Bank[0];
                auto LostRoot = Run->Before[0].Root;
                if (!Allocated(LostRoot))
                { return Stop(TEXT("FEAR source loss target unavailable")); }
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(LostRoot);
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
                    if (!B || !Same(*A, *B) ||
                        !Bool(Run->State[W].Get(),
                            TEXT("Get_HasSessionFailure"), Closed) || !Closed)
                    { return false; }
                }
                for (const auto& H : Run->Before[0].Forest)
                { if (Allocated(H)) { return false; } }
                for (int32 W = 0; W < Worlds; ++W)
                { if (!SourceCensus(*Run, W, 0)) { return false; } }
                Run->BeforeFrame = *A;
                if (!SendControl(Run->Local[0].Get(), Run->NextSerial[0] + 1, 1,
                        A->ControlGeneration, 0))
                { return Stop(TEXT("late FEAR RPC unavailable")); }
                Run->SettleUntil = FPlatformTime::Seconds() + 0.25;
                Run->Step = FRun::EStep::PostFailed;
                return false;
            }
            if (Run->Step == FRun::EStep::PostFailed)
            {
                if (FPlatformTime::Seconds() < Run->SettleUntil) { return false; }
                const auto* Rejection = Ack(*Run, Run->Local[0].Get());
                if (!Rejection || Rejection->Serial != Run->NextSerial[0] + 1 ||
                    Rejection->Result != 2 || Rejection->Action != 0)
                { return false; }
                if (A->Phase != 4 || A->ControlGeneration !=
                        Run->BeforeFrame.ControlGeneration ||
                    A->Sequence != Run->BeforeFrame.Sequence)
                { return Stop(TEXT("terminal FEAR session changed after late control")); }
                Run->Step = FRun::EStep::Done;
                return false;
            }
            return Stop(TEXT("unexpected FEAR authored phase"));
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
            Test->AddError(FString::Printf(
                TEXT("%s [step=%d actionIndex=%d mask=%d blocked=%s authority=%d/%lld/%lld%s ack=%d/%d state=%s]"),
                Why, static_cast<int32>(Run->Step), Run->ActionIndex,
                Run->ExpectedMask, *Run->LastBlock,
                A ? A->Phase : -1,
                static_cast<long long>(A ? A->ControlGeneration : -1),
                static_cast<long long>(A ? A->Sequence : -1), *ClientsText,
                Receipt ? Receipt->Serial : -1, Receipt ? Receipt->Result : -1,
                *Diagnostic));
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

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkGoapFEARGym_SharedWorld,
    "Ck.GoapFEAR.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkGoapFEARGym_SharedWorld::RunTest(const FString& Parameters)
{
    using namespace ck_goap_fear_authored_network;
    auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported GoapFEAR topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->SnapshotProperty = Field<FStructProperty>(Run->StateClass, TEXT("Snapshot"));
    Run->AckProperty = Field<FStructProperty>(Run->PCClass, TEXT("LastControlAck"));
    if (!TestTrue(TEXT("authored GoapFEAR classes and reflected controls"),
            Run->BaseMode && Run->Mode && Run->StateClass && Run->PCClass &&
            Run->PawnClass &&
            Run->SnapshotProperty && Run->AckProperty &&
            Run->SnapshotProperty->Struct == FCkGoapFEARGym_WireSnapshot::StaticStruct() &&
            Run->AckProperty->Struct == FCkGoapFEARGym_ControlAck::StaticStruct() &&
            Run->PCClass->FindFunctionByName(TEXT("Request_ControlActivated")) &&
            Run->PCClass->FindFunctionByName(TEXT("Get_IsLocalPresentationReady")) &&
            Run->PCClass->FindFunctionByName(TEXT("Server_RequestGoapFEARControl")) &&
            Run->StateClass->FindFunctionByName(TEXT("Get_SourceRoot")) &&
            Run->StateClass->FindFunctionByName(TEXT("Get_HasSessionFailure"))))
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
                Registry->Find_GymIndexByName(TEXT("Goap F.E.A.R.")) != INDEX_NONE;
        }), 30, TEXT("real Goap FEAR gym registry entry available")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner requests real GoapFEAR gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Goap F.E.A.R.")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Context(*Run); }), 60,
        TEXT("exact Goap FEAR worlds and owners converge")));
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
        }), TEXT("Goap FEAR authored worlds close after terminal failure"), true));
    return true;
}

#endif
