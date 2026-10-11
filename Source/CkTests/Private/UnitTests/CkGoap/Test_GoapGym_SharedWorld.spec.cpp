#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGoapGym_Wire.h"
#include "CkGoapGym_Runtime.h"
#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_Switchboard_Subsystem.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkGoap/CkGoap_Fragment_Data.h"
#include "CkGoap/Planner/CkGoap_Planner_Fragment.h"
#include "CkGoap/WorldState/CkGoap_WorldState_Fragment.h"
#include "CkGoap/WorldState/CkGoap_WorldState_Utils.h"
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
#include "GameplayTagContainer.h"
#include "HAL/PlatformTime.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <initializer_list>

namespace ck_goap_main_authored_network
{
    constexpr int32 Worlds = 3, Clients = 2, Stations = 7;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_GoapGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_GoapGym_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_GoapGym_PlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_Gym_Base_Pawn");
    const TCHAR* Tags[Stations]{
        TEXT("Gym.Goap.Station.OpenDoor"), TEXT("Gym.Goap.Station.MakeTea"),
        TEXT("Gym.Goap.Station.CrossRiver"), TEXT("Gym.Goap.Station.Patrol"),
        TEXT("Gym.Goap.Station.Survival"), TEXT("Gym.Goap.Station.CombatBrain"),
        TEXT("Gym.Goap.Station.OptOutDemo")};
    constexpr int32 InitialMasks[Stations]{0, 15, 3, 0, 13, 0, 0};
    constexpr int32 KeyCounts[Stations]{1, 8, 3, 3, 5, 6, 2};
    constexpr int32 PlannerCounts[Stations]{1, 1, 1, 3, 2, 4, 1};
    constexpr int32 ActionCounts[Stations]{2, 4, 3, 8, 6, 11, 1};
    constexpr int32 ForestSizes[Stations]{5, 7, 6, 11, 10, 14, 4};
    const TCHAR* Keys[Stations][8]{
        {TEXT("Gym.Goap.WS.Door.IsOpen")},
        {TEXT("Gym.Goap.WS.Tea.HasKettle"), TEXT("Gym.Goap.WS.Tea.HasWater"),
            TEXT("Gym.Goap.WS.Tea.HasTeaLeaves"), TEXT("Gym.Goap.WS.Tea.HasCup"),
            TEXT("Gym.Goap.WS.Tea.WaterBoiled"), TEXT("Gym.Goap.WS.Tea.TeaSteeped"),
            TEXT("Gym.Goap.WS.Tea.TeaPoured"), TEXT("Gym.Goap.WS.Tea.TeaServed")},
        {TEXT("Gym.Goap.WS.CrossRiver.BridgeIsOpen"),
            TEXT("Gym.Goap.WS.CrossRiver.HasCoin"),
            TEXT("Gym.Goap.WS.CrossRiver.Crossed")},
        {TEXT("Gym.Goap.WS.Patrol.AtWaypoint"),
            TEXT("Gym.Goap.WS.Patrol.AreaScanned"),
            TEXT("Gym.Goap.WS.Patrol.AreaPatrolled")},
        {TEXT("Gym.Goap.WS.Survival.Hungry"),
            TEXT("Gym.Goap.WS.Survival.HasFood"),
            TEXT("Gym.Goap.WS.Survival.ThreatActive"),
            TEXT("Gym.Goap.WS.Survival.HasWeapon"),
            TEXT("Gym.Goap.WS.Survival.SafeFromThreat")},
        {TEXT("Gym.Goap.WS.CombatBrain.EnemyVisible"),
            TEXT("Gym.Goap.WS.CombatBrain.WeaponEquipped"),
            TEXT("Gym.Goap.WS.CombatBrain.StaminaHigh"),
            TEXT("Gym.Goap.WS.CombatBrain.EnemyHit"),
            TEXT("Gym.Goap.WS.CombatBrain.EnemyAttacked"),
            TEXT("Gym.Goap.WS.CombatBrain.EnemyDead")},
        {TEXT("Gym.Goap.WS.OptOutDemo.Unreachable"),
            TEXT("Gym.Goap.WS.OptOutDemo.Touch")}};
    constexpr int32 ActionRows[]{1, 3, 4, 5, 6, 7, 9, 10, 11, 13, 14, 15,
        16, 18, 19, 20, 21, 22, 24, 25, 26, 27, 28};
    constexpr int32 ActionStations[]{0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3,
        3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5};
    constexpr int32 Actions[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
        12, 13, 14, 16, 15, 17, 18, 20, 19, 21, 22, 22};
    constexpr int32 ActionCount = UE_ARRAY_COUNT(Actions);

    int32 ParentAction(int32 Station, int32 Planner)
    {
        if (Planner == 0 || Station == 4) { return -1; }
        if (Station == 3) { return Planner == 1 ? 0 : 3; }
        if (Station == 5) { return Planner == 1 ? 0 : Planner == 2 ? 3 : 4; }
        return -2;
    }

    int32 PlannerForAction(int32 Station, int32 Code)
    {
        if (Code < 0 || Code >= ActionCounts[Station]) { return -1; }
        if (Station == 3 && (Code == 1 || Code == 2)) { return 1; }
        if (Station == 3 && (Code == 4 || Code == 5)) { return 2; }
        if (Station == 4) { return Code <= 2 ? 0 : 1; }
        if (Station == 5 && Code >= 3 && Code <= 5) { return 1; }
        if (Station == 5 && Code >= 6 && Code <= 8) { return 2; }
        if (Station == 5 && Code >= 9) { return 3; }
        return 0;
    }

    int32 ApplyAction(int32 Action, int32 Before)
    {
        if (Action == 0 || Action == 6) { return Before ^ 1; }
        if (Action >= 1 && Action <= 4) { return Before ^ (1 << (Action - 1)); }
        if (Action == 5) { return 15; }
        if (Action == 7) { return Before & ~2; }
        if (Action == 8) { return 3; }
        if (Action >= 9 && Action <= 11) { return Before | (1 << (Action - 9)); }
        if (Action == 12 || Action == 22) { return 0; }
        if (Action >= 13 && Action <= 16) { return Before ^ (1 << (Action - 13)); }
        if (Action == 17) { return 13; }
        if (Action >= 18 && Action <= 20) { return Before ^ (1 << (Action - 18)); }
        if (Action == 21) { return 63; }
        return Before;
    }

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
        FCall C(PC, TEXT("Server_RequestGoapControl"));
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

    bool Same(const FCkGoapGym_WireSnapshot& A,
        const FCkGoapGym_WireSnapshot& B)
    { return UCk_GoapGym_Runtime_UE::Get_IsSameFrame(A, B); }

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
        TMap<int64, FCkGoapGym_WireSnapshot> History;
        int64 Observed[Worlds]{};
        int32 NextSerial[3]{1, 1, 1};
        int32 ActionIndex = 0, Owner = -1, Serial = 0, OneWayCount = 0;
        int32 ExpectedMask[Stations]{0, 15, 3, 0, 13, 0, 0};
        int64 CompletedControls[Stations]{};
        double Deadline = 0.0, SettleUntil = 0.0;
        FString LastBlock;
        FCkGoapGym_WireSnapshot BeforeFrame;
        enum class EStep : uint8 { Baseline, Send, Wait, OneWaySend,
            OneWayWait, Invalid, Terminal,
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

    const FCkGoapGym_WireSnapshot* Frame(const FRun& R, int32 W)
    { return R.SnapshotProperty && R.State[W].IsValid()
        ? R.SnapshotProperty->ContainerPtrToValuePtr<FCkGoapGym_WireSnapshot>(
            R.State[W].Get()) : nullptr; }

    const FCkGoapGym_ControlAck* Ack(const FRun& R, APlayerController* PC)
    { return R.AckProperty && IsValid(PC)
        ? R.AckProperty->ContainerPtrToValuePtr<FCkGoapGym_ControlAck>(PC)
        : nullptr; }

    bool Presented(APlayerController* PC, FCkGoapGym_WireSnapshot& Out)
    {
        Out = {};
        FCall C(PC, TEXT("Get_PresentedFrame"));
        auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || P->Struct != FCkGoapGym_WireSnapshot::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = *P->ContainerPtrToValuePtr<FCkGoapGym_WireSnapshot>(
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
        int32 PlannerCount = 0, WorldStateCount = 0, SourceActionCount = 0;
        int32 PerSource[Stations]{}, PerWorldState[Stations]{}, PerAction[Stations]{};
        bool Foreign = false;
        auto CountOwner = [&](const FCk_Handle& H, int32* Counts)
        {
            if (W != 0) { return; }
            int32 Owners = 0;
            for (int32 I = 0; I < Stations; ++I)
            { if (R.Bank[I].Forest.Contains(H)) { ++Counts[I]; ++Owners; } }
            Foreign |= Owners != 1;
        };
        Registry.View<ck::FFragment_Goap_Planner>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Goap_Planner&)
            {
                const FCk_Handle H{Entity, RegistryHandle};
                if (!Allocated(H)) { return; }
                ++PlannerCount;
                CountOwner(H, PerSource);
            });
        Registry.View<ck::FFragment_Goap_WorldState_Values>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Goap_WorldState_Values&)
            {
                const FCk_Handle H{Entity, RegistryHandle};
                if (Allocated(H)) { ++WorldStateCount; CountOwner(H, PerWorldState); }
            });
        Registry.View<ck::FFragment_Goap_Action>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_Goap_Action&)
            {
                const FCk_Handle H{Entity, RegistryHandle};
                if (Allocated(H)) { ++SourceActionCount; CountOwner(H, PerAction); }
            });
        if (W == 0 && Expected == Stations)
        {
            for (int32 I = 0; I < Stations; ++I)
            { if (PerSource[I] != PlannerCounts[I] || PerWorldState[I] != 1 ||
                  PerAction[I] != ActionCounts[I]) { return false; } }
        }
        return PlannerCount == (Expected == 0 ? 0 : 13) &&
            WorldStateCount == Expected &&
            SourceActionCount == (Expected == 0 ? 0 : 35) && !Foreign;
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
        return UCk_GoapGym_Runtime_UE::TryGet_SourceForest(
            R.World[0].Get(), Out.Root, Out.Forest) &&
            Out.Forest.Contains(Out.Root);
    }

    bool NativeMask(const FRun& R, int32 StationIndex, int32& Out)
    {
        Out = 0;
        FCk_Handle WorldState;
        int32 Found = 0;
        for (const auto& H : R.Bank[StationIndex].Forest)
        {
            if (UCk_GoapGym_Runtime_UE::Get_IsLiveWorldState(
                    H, R.World[0].Get()))
            { WorldState = H; ++Found; }
        }
        if (Found != 1 || !Direct(WorldState, R.Bank[StationIndex].Root))
        { return false; }
        const auto Typed = UCk_Utils_Goap_WorldState_UE::Cast(WorldState);
        if (!Allocated(Typed)) { return false; }
        for (int32 Bit = 0; Bit < KeyCounts[StationIndex]; ++Bit)
        {
            const auto Tag = FGameplayTag::RequestGameplayTag(
                FName(Keys[StationIndex][Bit]), false);
            if (!Tag.IsValid() ||
                !UCk_Utils_Goap_WorldState_UE::Has_Key(Typed, Tag))
            { return false; }
            if (UCk_Utils_Goap_WorldState_UE::Get_Value(Typed, Tag))
            { Out |= 1 << Bit; }
        }
        return true;
    }

    bool Catalog(FRun& R)
    {
        constexpr const TCHAR* Names[]{TEXT("Goap"), TEXT("Goap F.E.A.R."),
            TEXT("Input Key Binding"), TEXT("Game Settings")};
        constexpr bool Multiplayer[]{true, true, false, false};
        int32 ServerIndices[UE_ARRAY_COUNT(Names)]{};
        for (int32 W = 0; W < Worlds; ++W)
        {
            const auto Entries = UCk_Utils_GymRegistry_UE::Get_GymRegistry(
                R.World[W].Get());
            const auto Groups = UCkGym_Switchboard_Subsystem::Build_Groups(Entries);
            for (int32 N = 0; N < UE_ARRAY_COUNT(Names); ++N)
            {
                int32 Index = INDEX_NONE, Found = 0, Grouped = 0;
                for (int32 I = 0; I < Entries.Num(); ++I)
                {
                    if (Entries[I].DisplayName == Names[N])
                    { Index = I; ++Found; }
                }
                if (Found != 1 || Index == INDEX_NONE ||
                    Entries[Index].HasMultiplayerComponent != Multiplayer[N] ||
                    (W > 0 && Index != ServerIndices[N]))
                { return false; }
                if (W == 0) { ServerIndices[N] = Index; }
                for (const auto& Group : Groups)
                {
                    for (const auto& Row : Group.Rows)
                    {
                        if (Row.DisplayName == Names[N])
                        {
                            if (Row.RegistryIndex != Index ||
                                Row.HasMultiplayerComponent != Multiplayer[N])
                            { return false; }
                            ++Grouped;
                        }
                    }
                }
                if (Grouped != 1) { return false; }
                if (N == 1 || N == 2)
                {
                    const auto Filtered = UCkGym_Switchboard_Subsystem::
                        Build_FilteredRows(Groups, Names[N]);
                    int32 Matched = 0;
                    for (const auto& Row : Filtered)
                    {
                        if (Row.DisplayName == Names[N])
                        {
                            if (Row.RegistryIndex != Index ||
                                Row.HasMultiplayerComponent != Multiplayer[N])
                            { return false; }
                            ++Matched;
                        }
                    }
                    if (Matched != 1) { return false; }
                }
            }
        }
        return true;
    }

    bool Block(FRun& R, const TCHAR* Why)
    { R.LastBlock = Why; return false; }

    bool PlanIs(const FCkGoapGym_PlannerFrame& P,
        std::initializer_list<int32> ExpectedActions, float Cost)
    {
        if (P.PlanStatus != static_cast<int32>(ECk_GoapPlanStatus::PlanFound) ||
            P.PlanActions.Num() != static_cast<int32>(ExpectedActions.size()) ||
            !FMath::IsNearlyEqual(P.PlanCost, Cost, 0.01f)) { return false; }
        int32 J = 0;
        for (int32 Code : ExpectedActions)
        { if (P.PlanActions[J++] != Code) { return false; } }
        return true;
    }

    bool SelectedPromotionsFresh(const FCkGoapGym_StationFrame& After,
        const FCkGoapGym_StationFrame& Before)
    {
        if (After.Planners.Num() != Before.Planners.Num() ||
            After.Planners.IsEmpty()) { return false; }
        for (int32 Code : After.Planners[0].ActiveChain)
        {
            for (int32 I = 1; I < After.Planners.Num(); ++I)
            {
                if (After.Planners[I].ParentAction != Code) { continue; }
                if (After.Planners[I].PlanCount +
                        After.Planners[I].FailureCount <=
                        Before.Planners[I].PlanCount +
                        Before.Planners[I].FailureCount)
                { return false; }
            }
        }
        return true;
    }

    bool ValidFrame(const FCkGoapGym_StationFrame& S,
        int32 Index, int32 Mask, int64 Controls)
    {
        if (S.Index != Index || S.Generation != 1 ||
            S.WorldStateMask != Mask || Mask < 0 ||
            Mask >= (1 << KeyCounts[Index]) ||
            S.ControlCount != Controls || S.Title.IsEmpty() ||
            S.Planners.Num() != PlannerCounts[Index])
        { return false; }
        for (int32 J = 0; J < S.Planners.Num(); ++J)
        {
            const auto& P = S.Planners[J];
            if (P.Index != J || P.ParentAction != ParentAction(Index, J) ||
                P.PlanCount < 0 || P.FailureCount < 0 ||
                P.PlanStatus < 0 || P.PlanStatus > 4 ||
                P.PlanActions.Num() > ActionCounts[Index] ||
                P.ActiveChain.Num() > 4 ||
                !FMath::IsFinite(P.PlanCost) || P.PlanCost < 0.0f)
            { return false; }
            for (int32 Code : P.PlanActions)
            { if (PlannerForAction(Index, Code) != J) { return false; } }
            for (int32 Code : P.ActiveChain)
            { if (Code < 0 || Code >= ActionCounts[Index]) { return false; } }
        }
        const auto& Top = S.Planners[0];
        if (Top.PlanCount < 1 && Top.FailureCount < 1) { return false; }
        if (!Top.PlanActions.IsEmpty() &&
            (Top.ActiveChain.IsEmpty() ||
                Top.ActiveChain[0] != Top.PlanActions[0]))
        { return false; }
        if (Index == 6)
        {
            return Top.PlanStatus == static_cast<int32>(ECk_GoapPlanStatus::PlanFailed) &&
                Top.FailureCount >= 1 && Top.PlanActions.IsEmpty() &&
                Top.ActiveChain.IsEmpty();
        }
        if (Index == 1 && (Mask & 15) != 15 && (Mask & 128) == 0)
        {
            return Top.PlanStatus == static_cast<int32>(ECk_GoapPlanStatus::PlanFailed) &&
                Top.FailureCount >= 1 && Top.PlanActions.IsEmpty();
        }
        if (Index == 0 && Mask == 0) { return PlanIs(Top, {0}, 1.0f); }
        if (Index == 1 && Mask == 15)
        { return PlanIs(Top, {0, 1, 2, 3}, 5.0f); }
        if (Index == 2 && (Mask & 4) == 0)
        {
            if (Mask & 1) { return PlanIs(Top, {0}, 2.0f); }
            if (Mask & 2) { return PlanIs(Top, {1}, 4.0f); }
            return PlanIs(Top, {2}, 8.0f);
        }
        if (Index == 3 && Mask == 0)
        {
            const auto& Run = S.Planners[1];
            return PlanIs(Top, {0, 3, 6}, 3.0f) &&
                Run.PlanCount > 0 && PlanIs(Run, {1}, 1.0f);
        }
        if (Index == 3 && Mask == 1)
        {
            const auto& Observe = S.Planners[2];
            return PlanIs(Top, {3, 6}, 2.0f) &&
                Observe.PlanCount > 0 && PlanIs(Observe, {4}, 1.0f);
        }
        if (Index == 4 && Mask == 13)
        { return S.Planners[1].PlanCount > 0 &&
            PlanIs(S.Planners[1], {3}, 1.0f); }
        if (Index == 4 && Mask == 6)
        { return S.Planners[1].PlanCount > 0 &&
            PlanIs(S.Planners[1], {4}, 3.0f); }
        if (Index == 5 && Mask == 0)
        { return PlanIs(Top, {2}, 999.0f); }
        if (Index == 5 && (Mask == 3 || Mask == 7))
        {
            return PlanIs(Top, {0, 1}, 2.0f) &&
                S.Planners[1].PlanCount > 0 &&
                PlanIs(S.Planners[1], {3}, 1.0f) &&
                S.Planners[2].PlanCount > 0 &&
                PlanIs(S.Planners[2], {8}, 0.5f);
        }
        if (Index == 5 && Mask == 5)
        {
            return PlanIs(Top, {0, 1}, 2.0f) &&
                S.Planners[1].PlanCount > 0 &&
                PlanIs(S.Planners[1], {4}, 1.0f) &&
                S.Planners[3].PlanCount > 0 &&
                PlanIs(S.Planners[3], {9}, 3.0f);
        }
        return Top.PlanStatus == static_cast<int32>(ECk_GoapPlanStatus::PlanFound);
    }

    bool Converged(FRun& R, int64 Generation,
        bool ExpectInjectedRequesterNotReady = false)
    {
        if (!Context(R)) { return Block(R, TEXT("world/player context")); }
        const auto* A = Frame(R, 0);
        if (!A || A->Phase != 2 || A->Replacing != -1 ||
            A->ControlGeneration != Generation || A->SourceGeneration != 1 ||
            A->Sequence <= 0 || A->Stations.Num() != Stations)
        { return Block(R, TEXT("authority active snapshot")); }
        for (int32 I = 0; I < Stations; ++I)
        {
            if (!ValidFrame(A->Stations[I], I, R.ExpectedMask[I],
                    R.CompletedControls[I]) ||
                !Bank(R, I, R.Bank[I]) ||
                R.Bank[I].Forest.Num() != ForestSizes[I])
            { return Block(R, TEXT("authority source forest and planner output")); }
            int32 Native = -1;
            if (!NativeMask(R, I, Native) ||
                Native != A->Stations[I].WorldStateMask)
            { return Block(R, TEXT("authority native WS differs from settled frame")); }
        }
        if (!SourceCensus(R, 0, Stations))
        { return Block(R, TEXT("authority planner/WS/action census")); }
        for (int32 W = 1; W < Worlds; ++W)
        {
            const auto* B = Frame(R, W);
            const auto* Known = B ? R.History.Find(B->Sequence) : nullptr;
            bool Ready = false;
            // Synthetic injected RPCs bypass client 0's local UI serial.
            const bool ExpectedReady = !(ExpectInjectedRequesterNotReady && W == 1);
            if (!B || B->Phase != 2 || B->ControlGeneration != Generation ||
                B->Stations.Num() != Stations || !Known || !Same(*Known, *B) ||
                !Bool(R.Local[W - 1].Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                Ready != ExpectedReady)
            { return Block(R, TEXT("client known frame/presentation")); }
            FCkGoapGym_WireSnapshot Drawn;
            if (!Presented(R.Local[W - 1].Get(), Drawn) ||
                Drawn.ControlGeneration != Generation ||
                Drawn.Stations.Num() != Stations || Drawn.Sequence > B->Sequence ||
                !R.History.Contains(Drawn.Sequence) ||
                !Same(*R.History.Find(Drawn.Sequence), Drawn))
            { return Block(R, TEXT("client presented value frame")); }
            for (int32 I = 0; I < Stations; ++I)
            {
                FCk_Handle LocalRoot;
                if (B->Stations[I].Generation != A->Stations[I].Generation ||
                    !ValidFrame(B->Stations[I], I, R.ExpectedMask[I],
                        R.CompletedControls[I]) ||
                    !Handle(R.State[W].Get(), I, LocalRoot) ||
                    Allocated(LocalRoot) || !Station(R, W, I))
                { return Block(R, TEXT("client passive source/panel")); }
            }
            if (!SourceCensus(R, W, 0))
            { return Block(R, TEXT("client source ECS census")); }
        }
        if (!R.Dedicated)
        {
            bool Ready = false;
            FCkGoapGym_WireSnapshot Drawn;
            if (!Bool(R.Host.Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                !Ready || !Presented(R.Host.Get(), Drawn) ||
                Drawn.ControlGeneration != Generation ||
                Drawn.Stations.Num() != Stations || Drawn.Sequence > A->Sequence ||
                !R.History.Contains(Drawn.Sequence) ||
                !Same(*R.History.Find(Drawn.Sequence), Drawn))
            { return Block(R, TEXT("host local presented value frame")); }
            for (int32 I = 0; I < Stations; ++I)
            { if (!Station(R, 0, I)) { return Block(R, TEXT("host station panel")); } }
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
            if (Run->Deadline == 0.0)
            { Run->Deadline = FPlatformTime::Seconds() + 90.0; }
            if (FPlatformTime::Seconds() > Run->Deadline)
            { return Stop(TEXT("Goap world/control did not settle")); }
            if (!Context(*Run) || !Observe(*Run))
            { return Stop(TEXT("Goap context or ordered frame changed")); }
            const auto* A = Frame(*Run, 0);
            if (A && A->Phase == 4 && Run->Step != FRun::EStep::Failed &&
                Run->Step != FRun::EStep::PostFailed)
            { return Stop(TEXT("Goap unexpected terminal phase")); }

            if (Run->Step == FRun::EStep::Baseline)
            {
                if (!Converged(*Run, 1)) { return false; }
                if (!Catalog(*Run))
                { return Stop(TEXT("Goap catalog multiplayer metadata lost across worlds")); }
                for (int32 I = 0; I < Stations; ++I)
                {
                    const auto& Forest = Run->Bank[I].Forest;
                    int32 Planners = 0, WorldStates = 0, ActionsFound = 0, Timers = 0;
                    for (const auto& H : Forest)
                    {
                        const bool P = UCk_GoapGym_Runtime_UE::
                            Get_IsLivePlanner(H, Run->World[0].Get());
                        const bool WS = UCk_GoapGym_Runtime_UE::
                            Get_IsLiveWorldState(H, Run->World[0].Get());
                        const bool Act = UCk_GoapGym_Runtime_UE::
                            Get_IsLiveAction(H, Run->World[0].Get());
                        Planners += P; WorldStates += WS; ActionsFound += Act;
                        Timers += UCk_Utils_Timer_UE::Has(H);
                        if (WS && !Direct(H, Run->Bank[I].Root))
                        { return Stop(TEXT("Goap world state escaped source")); }
                        if (Act)
                        {
                            if (!UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H) ||
                                !UCk_GoapGym_Runtime_UE::Get_IsLivePlanner(
                                    UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H),
                                    Run->World[0].Get()))
                            { return Stop(TEXT("Goap action escaped planner tree")); }
                        }
                    }
                    if (Planners != PlannerCounts[I] || WorldStates != 1 ||
                        ActionsFound != ActionCounts[I] || Timers != 0)
                    { return Stop(TEXT("Goap forest role counts")); }
                    Run->Before[I] = Run->Bank[I];
                }
                TArray<FCk_Handle> Rejected;
                Rejected.Add(Run->Bank[0].Root);
                if (UCk_GoapGym_Runtime_UE::TryGet_SourceForest(
                        Run->World[0].Get(), FCk_Handle{}, Rejected) ||
                    !Rejected.IsEmpty() ||
                    UCk_GoapGym_Runtime_UE::Get_IsLiveInWorld(
                        FCk_Handle{}, Run->World[0].Get()) ||
                    UCk_GoapGym_Runtime_UE::Get_IsLivePlanner(
                        FCk_Handle{}, Run->World[0].Get()) ||
                    UCk_GoapGym_Runtime_UE::Get_IsLiveWorldState(
                        FCk_Handle{}, Run->World[0].Get()) ||
                    UCk_GoapGym_Runtime_UE::Get_IsLiveAction(
                        FCk_Handle{}, Run->World[0].Get()))
                { return Stop(TEXT("default Goap runtime request was not total")); }
                Rejected.Add(Run->Bank[0].Root);
                if (UCk_GoapGym_Runtime_UE::TryGet_SourceForest(
                        Run->World[0].Get(), Run->WorldRoot[1], Rejected) ||
                    !Rejected.IsEmpty() ||
                    UCk_GoapGym_Runtime_UE::Get_IsLiveInWorld(
                        Run->WorldRoot[1], Run->World[0].Get()))
                { return Stop(TEXT("foreign Goap world was accepted")); }
                Run->Step = FRun::EStep::Send;
                return false;
            }
            if (Run->Step == FRun::EStep::Send)
            {
                if (Run->ActionIndex >= ActionCount)
                { Run->Step = FRun::EStep::OneWaySend; return false; }
                if (!Converged(*Run, A->ControlGeneration))
                { return false; }
                Run->BeforeFrame = *A;
                for (int32 I = 0; I < Stations; ++I)
                { Run->Before[I] = Run->Bank[I]; }
                const int32 Action = Actions[Run->ActionIndex];
                const int32 Selected = ActionStations[Action];
                Run->ExpectedMask[Selected] =
                    ApplyAction(Action, Run->ExpectedMask[Selected]);
                ++Run->CompletedControls[Selected];
                Run->Owner = !Run->Dedicated && Run->ActionIndex % 3 == 2
                    ? 2 : Run->ActionIndex % 2;
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                Run->Serial = Run->NextSerial[Run->Owner]++;
                if (!Activate(Sender, ActionRows[Action]))
                { return Stop(TEXT("authored Goap control unavailable")); }
                Run->Step = FRun::EStep::Wait;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Wait)
            {
                const int32 Action = Actions[Run->ActionIndex];
                const int32 Selected = ActionStations[Action];
                auto* Sender = Run->Owner == 2 ? Run->Host.Get()
                    : Run->Local[Run->Owner].Get();
                const auto* Receipt = Ack(*Run, Sender);
                if (!Receipt || Receipt->Serial != Run->Serial ||
                    Receipt->Action != Action || Receipt->Result != 1 ||
                    Receipt->RequestedGeneration !=
                        Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration !=
                        Run->BeforeFrame.ControlGeneration + 1 ||
                    !Converged(*Run,
                        Run->BeforeFrame.ControlGeneration + 1))
                { return false; }
                A = Frame(*Run, 0);
                const auto* Received = Run->Owner == 2 ? A
                    : Frame(*Run, Run->Owner + 1);
                if (!Received ||
                    Receipt->CompletedSequence > Received->Sequence ||
                    Receipt->CompletedSequence <= Run->BeforeFrame.Sequence)
                { return false; }
                if (A->Stations[Selected].Planners[0].PlanCount +
                        A->Stations[Selected].Planners[0].FailureCount <=
                        Run->BeforeFrame.Stations[Selected].Planners[0].PlanCount +
                        Run->BeforeFrame.Stations[Selected].Planners[0].FailureCount ||
                    A->Stations[Selected].ControlCount !=
                        Run->BeforeFrame.Stations[Selected].ControlCount + 1)
                { return Stop(TEXT("Goap selected source did not complete fresh plan")); }
                if (!SelectedPromotionsFresh(A->Stations[Selected],
                        Run->BeforeFrame.Stations[Selected]))
                { return Stop(TEXT("selected promoted Goap branch did not complete")); }
                for (int32 I = 0; I < Stations; ++I)
                {
                    if (Run->Bank[I].Root != Run->Before[I].Root ||
                        Run->Bank[I].Forest.Num() != Run->Before[I].Forest.Num() ||
                        A->Stations[I].Generation !=
                            Run->BeforeFrame.Stations[I].Generation)
                    { return Stop(TEXT("Goap control replaced source")); }
                    for (const auto& H : Run->Before[I].Forest)
                    {
                        if (!Allocated(H) || !Run->Bank[I].Forest.Contains(H))
                        { return Stop(TEXT("Goap control retired source descendant")); }
                    }
                    if (I != Selected && !FCkGoapGym_StationFrame::StaticStruct()->
                            CompareScriptStruct(&A->Stations[I],
                                &Run->BeforeFrame.Stations[I], PPF_None))
                    { return Stop(TEXT("Goap control mutated sibling station")); }
                }
                ++Run->ActionIndex;
                Run->Step = FRun::EStep::Send;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::OneWaySend)
            {
                if (Run->OneWayCount >= 2)
                { Run->Step = FRun::EStep::Invalid; return false; }
                if (!Converged(*Run, A->ControlGeneration,
                        Run->OneWayCount > 0))
                { return false; }
                Run->BeforeFrame = *A;
                for (int32 I = 0; I < Stations; ++I)
                { Run->Before[I] = Run->Bank[I]; }
                Run->Owner = 0;
                Run->Serial = Run->NextSerial[0]++;
                Run->ExpectedMask[3] |= 1;
                ++Run->CompletedControls[3];
                if (!SendControl(Run->Local[0].Get(), Run->Serial, 1,
                        A->ControlGeneration, 9))
                { return Stop(TEXT("direct one-way Goap RPC unavailable")); }
                Run->Step = FRun::EStep::OneWayWait;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::OneWayWait)
            {
                const auto* Receipt = Ack(*Run, Run->Local[0].Get());
                if (!Receipt || Receipt->Serial != Run->Serial ||
                    Receipt->Action != 9 || Receipt->Result != 1 ||
                    Receipt->RequestedGeneration !=
                        Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration !=
                        Run->BeforeFrame.ControlGeneration + 1 ||
                    !Converged(*Run,
                        Run->BeforeFrame.ControlGeneration + 1, true))
                { return false; }
                A = Frame(*Run, 0);
                if (Receipt->CompletedSequence > Frame(*Run, 1)->Sequence ||
                    Receipt->CompletedSequence <= Run->BeforeFrame.Sequence ||
                    !SelectedPromotionsFresh(A->Stations[3],
                        Run->BeforeFrame.Stations[3]) ||
                    A->Stations[3].Planners[0].PlanCount +
                        A->Stations[3].Planners[0].FailureCount <=
                        Run->BeforeFrame.Stations[3].Planners[0].PlanCount +
                        Run->BeforeFrame.Stations[3].Planners[0].FailureCount)
                { return Stop(TEXT("idempotent one-way Goap completion missing")); }
                for (int32 I = 0; I < Stations; ++I)
                {
                    if (Run->Bank[I].Root != Run->Before[I].Root ||
                        Run->Bank[I].Forest.Num() != Run->Before[I].Forest.Num())
                    { return Stop(TEXT("one-way Goap control replaced source")); }
                    for (const auto& H : Run->Before[I].Forest)
                    { if (!Allocated(H) || !Run->Bank[I].Forest.Contains(H))
                      { return Stop(TEXT("one-way Goap control retired source")); } }
                }
                ++Run->OneWayCount;
                Run->Step = FRun::EStep::OneWaySend;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::Invalid)
            {
                if (!Converged(*Run, A->ControlGeneration, true))
                { return false; }
                Run->BeforeFrame = *A;
                for (int32 I = 0; I < Stations; ++I)
                { Run->Before[I] = Run->Bank[I]; }
                const int32 Serial = Run->NextSerial[0];
                if (!SendControl(Run->Local[0].Get(), Serial, 1,
                        A->ControlGeneration, 99) ||
                    !SendControl(Run->Local[0].Get(), Serial, 0,
                        A->ControlGeneration, 0) ||
                    !SendControl(Run->Local[0].Get(), Serial, 1,
                        A->ControlGeneration - 1, 0))
                { return Stop(TEXT("malformed/stale Goap RPC unavailable")); }
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
                        Run->BeforeFrame.ControlGeneration)
                { return Stop(TEXT("invalid Goap request advanced bank")); }
                for (int32 I = 0; I < Stations; ++I)
                {
                    FCk_Handle Current;
                    if (!Handle(Run->State[0].Get(), I, Current) ||
                        Current != Run->Before[I].Root ||
                        A->Stations[I].WorldStateMask != Run->ExpectedMask[I] ||
                        A->Stations[I].ControlCount != Run->CompletedControls[I])
                    { return Stop(TEXT("invalid Goap request mutated source")); }
                }
                if (!Converged(*Run, A->ControlGeneration, true))
                { return false; }
                for (int32 I = 0; I < Stations; ++I)
                { Run->Before[I] = Run->Bank[I]; }
                auto LostRoot = Run->Before[0].Root;
                if (!Allocated(LostRoot))
                { return Stop(TEXT("Goap source loss target unavailable")); }
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
                for (int32 I = 0; I < Stations; ++I)
                {
                    for (const auto& H : Run->Before[I].Forest)
                    { if (Allocated(H)) { return false; } }
                }
                TArray<FCk_Handle> Rejected;
                Rejected.Add(Run->Before[0].Root);
                if (UCk_GoapGym_Runtime_UE::TryGet_SourceForest(
                        Run->World[0].Get(), Run->Before[0].Root, Rejected) ||
                    !Rejected.IsEmpty() ||
                    UCk_GoapGym_Runtime_UE::Get_IsLiveInWorld(
                        Run->Before[0].Root, Run->World[0].Get()))
                { return Stop(TEXT("retired Goap handle remained live")); }
                for (int32 W = 0; W < Worlds; ++W)
                { if (!SourceCensus(*Run, W, 0)) { return false; } }
                Run->BeforeFrame = *A;
                if (!SendControl(Run->Local[0].Get(),
                        Run->NextSerial[0] + 1, 1,
                        A->ControlGeneration, 0))
                { return Stop(TEXT("late Goap RPC unavailable")); }
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
                { return Stop(TEXT("terminal Goap session changed after late control")); }
                Run->Step = FRun::EStep::Done;
                return false;
            }
            return Stop(TEXT("unexpected Goap authored phase"));
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
            const int32 Action = Run->ActionIndex < ActionCount
                ? Actions[Run->ActionIndex] : -1;
            const int32 Selected = Action >= 0 ? ActionStations[Action] : -1;
            Test->AddError(FString::Printf(
                TEXT("%s [step=%d actionIndex=%d action=%d selected=%d mask=%d blocked=%s authority=%d/%lld/%lld%s ack=%d/%d state=%s]"),
                Why, static_cast<int32>(Run->Step), Run->ActionIndex,
                Action, Selected, Selected >= 0 ? Run->ExpectedMask[Selected] : -1,
                *Run->LastBlock, A ? A->Phase : -1,
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

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkGoapGym_SharedWorld,
    "Ck.Goap.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkGoapGym_SharedWorld::RunTest(const FString& Parameters)
{
    using namespace ck_goap_main_authored_network;
    auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported Goap topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->SnapshotProperty = Field<FStructProperty>(Run->StateClass, TEXT("Snapshot"));
    Run->AckProperty = Field<FStructProperty>(Run->PCClass, TEXT("LastControlAck"));
    if (!TestTrue(TEXT("authored Goap classes and reflected controls"),
            Run->BaseMode && Run->Mode && Run->StateClass && Run->PCClass &&
            Run->PawnClass &&
            Run->SnapshotProperty && Run->AckProperty &&
            Run->SnapshotProperty->Struct == FCkGoapGym_WireSnapshot::StaticStruct() &&
            Run->AckProperty->Struct == FCkGoapGym_ControlAck::StaticStruct() &&
            Run->PCClass->FindFunctionByName(TEXT("Request_ControlActivated")) &&
            Run->PCClass->FindFunctionByName(TEXT("Get_IsLocalPresentationReady")) &&
            Run->PCClass->FindFunctionByName(TEXT("Server_RequestGoapControl")) &&
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
                Registry->Find_GymIndexByName(TEXT("Goap")) != INDEX_NONE;
        }), 30, TEXT("real Goap gym registry entry available")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner requests real Goap gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC, TEXT("Goap")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Context(*Run); }), 60,
        TEXT("exact Goap worlds and owners converge")));
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
        }), TEXT("Goap authored worlds close after terminal failure"), true));
    return true;
}

#endif
