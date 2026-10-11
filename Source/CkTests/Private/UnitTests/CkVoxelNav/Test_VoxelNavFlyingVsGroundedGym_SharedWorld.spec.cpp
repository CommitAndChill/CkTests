#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkVoxelNavGym_Wire.h"
#include "CkVoxelNavGym_Runtime.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkGym_StartupSettings.h"
#include "CkGymRegistry_Utils.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/Net/ReplicatedFragmentContainer/CkReplicatedFragmentContainer.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkVoxelNav/Volume/CkVoxelNavVolume_Utils.h"
#include "CkPhysics/Acceleration/CkAcceleration_Utils.h"
#include "CkPhysics/EulerIntegrator/CkEulerIntegrator_Fragment.h"
#include "CkPhysics/Velocity/CkVelocity_Utils.h"
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

namespace ck_voxel_nav_authored_network
{
    constexpr int32 Worlds = 3, Clients = 2;
    constexpr auto EntryMap = TEXT("/Engine/Maps/Entry");
    constexpr auto BaseModePath = TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_VoxelNavGym_FlyingVsGrounded_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_VoxelNavGym_FlyingVsGrounded_GameState");
    constexpr auto PCPath = TEXT("/Script/Angelscript.Ck_VoxelNavGym_FlyingVsGrounded_PlayerController");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_Gym_Base_Pawn");
    constexpr auto Tag = TEXT("VoxelNavFlyingVsGrounded");
    constexpr const TCHAR* AgentTag[2] = {
        TEXT("VoxelNavFlyingVsGrounded.Agent.Flying"),
        TEXT("VoxelNavFlyingVsGrounded.Agent.Grounded")};
    const FVector Seed[2] = {FVector{-1000.0, -400.0, 1000.0},
        FVector{-1000.0, 400.0, 0.0}};

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

    bool DescendsFrom(FCk_Handle Child, const FCk_Handle& Parent)
    {
        for (int32 Depth = 0; Depth < 16 && Allocated(Child); ++Depth)
        {
            if (Child == Parent) { return true; }
            if (!UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Child))
            { return false; }
            Child = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child);
        }
        return false;
    }

    bool TaggedAgent(UWorld* World, FCk_Handle WorldRoot, int32 Index, FCk_Handle& Out)
    {
        Out = {};
        if (Index < 0 || Index > 1 || !Allocated(WorldRoot)) { return false; }
        const auto Matches = UCk_Utils_EntityTag_UE::ForEach_Entity(WorldRoot, FName{AgentTag[Index]});
        if (Matches.Num() != 1 || !Allocated(Matches[0]) ||
            !UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(Matches[0], World))
        { return false; }
        Out = Matches[0];
        return true;
    }

    bool TransformOf(const FCk_Handle& Entity, FTransform& Out)
    {
        Out = FTransform::Identity;
        if (!Allocated(Entity) || !UCk_Utils_Transform_UE::Has(Entity)) { return false; }
        auto Mutable = Entity;
        Out = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::CastChecked(Mutable));
        return Out.IsValid() && !Out.ContainsNaN();
    }

    bool PresentedPoseClose(const FTransform& PresentedPose, const FTransform& EntityPose)
    {
        return PresentedPose.IsValid() && !PresentedPose.ContainsNaN() &&
            FVector::Dist(PresentedPose.GetLocation(), EntityPose.GetLocation()) <= 150.0 &&
            PresentedPose.GetScale3D().Equals(EntityPose.GetScale3D(), 0.1);
    }

    bool AgentIdentity(const FCk_Handle& Entity, int32 Index, int64 Pair)
    {
        if (!Allocated(Entity) || Index < 0 || Index > 1) { return false; }
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity);
        auto* ScriptIndex = Field<FIntProperty>(IsValid(Script) ? Script->GetClass() : nullptr,
            TEXT("Index"));
        auto* ScriptPair = Field<FInt64Property>(IsValid(Script) ? Script->GetClass() : nullptr,
            TEXT("PairGeneration"));
        return ScriptIndex && ScriptPair &&
            ScriptIndex->GetPropertyValue_InContainer(Script) == Index &&
            ScriptPair->GetPropertyValue_InContainer(Script) == Pair;
    }

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

    bool Source(AGameStateBase* GS, int32 Index, FCk_Handle& Out)
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

    bool PresentedAgent(APlayerController* PC, int32 Index, FCk_Handle& Out)
    {
        Out = {};
        FCall C(PC, TEXT("Get_PresentedAgent"));
        auto* I = Field<FIntProperty>(C.Function, TEXT("Index"));
        auto* R = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!I || !R || R->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 2 || !C.Args) { return false; }
        I->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Index);
        if (!C.Send()) { return false; }
        Out = *R->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory());
        return true;
    }

    bool PresentedAgentTransform(APlayerController* PC, int32 Index, FTransform& Out)
    {
        Out = FTransform::Identity;
        FCall C(PC, TEXT("Get_PresentedAgentTransform"));
        auto* I = Field<FIntProperty>(C.Function, TEXT("Index"));
        auto* R = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!I || !R || R->Struct != TBaseStructure<FTransform>::Get() ||
            C.Function->NumParms != 2 || !C.Args) { return false; }
        I->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Index);
        if (!C.Send()) { return false; }
        Out = *R->ContainerPtrToValuePtr<FTransform>(C.Args->GetStructMemory());
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
        FCall C(PC, TEXT("Server_RequestVoxelControl"));
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

    bool Same(const FCkVoxelNavGym_WireSnapshot& A,
        const FCkVoxelNavGym_WireSnapshot& B)
    { return UCk_VoxelNavGym_Runtime_UE::Get_IsSameFrame(A, B); }

    bool Fixtures(AGameStateBase* State, TArray<AActor*>& Out)
    {
        Out.Reset();
        auto* Array = Field<FArrayProperty>(State ? State->GetClass() : nullptr,
            TEXT("FixtureActors"));
        auto* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
        if (!Inner) { return false; }
        FScriptArrayHelper Actors(Array, Array->ContainerPtrToValuePtr<void>(State));
        for (int32 I = 0; I < Actors.Num(); ++I)
        { Out.Add(Cast<AActor>(Inner->GetObjectPropertyValue(Actors.GetRawPtr(I)))); }
        return true;
    }

    struct FBank
    {
        FCk_Handle Root, Volume;
        FCk_Handle Agent[2];
        int32 BuildEpoch = 0;
        TArray<FCk_Handle> Forest, AgentForest[2];
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
        FCk_Handle WorldRoot[Worlds], LocalStation[Worlds];
        FBank Bank, Before;
        FCk_Handle ClientAgent[Worlds][2], OldClientAgent[Worlds][2];
        FCk_Handle StaleProbe;
        TWeakObjectPtr<AActor> Fixture[2];
        TMap<int64, FCkVoxelNavGym_WireSnapshot> History;
        int64 Observed[Worlds]{};
        int32 NextSerial[3]{1, 1, 1};
        int32 Owner = -1, Serial = 0, Action = -1;
        bool SawTravel[Worlds][2]{}, SawHostTravel[2]{},
            SawReplicaMatch[Worlds][2]{};
        TArray<FVector> AuthorityHistory[2];
        double Deadline = 0.0, SettleUntil = 0.0;
        FString LastBlock;
        FCkVoxelNavGym_WireSnapshot BeforeFrame;
        FCkVoxelNavGym_AgentFrame BeforePhysical[2];
        enum class EStep : uint8 { Baseline, Restart, WaitRestart, Reverse,
            WaitReverse, HostReverse, WaitHostReverse, Invalid, Rejected,
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

    const FCkVoxelNavGym_WireSnapshot* Frame(const FRun& R, int32 W)
    { return R.SnapshotProperty && R.State[W].IsValid()
        ? R.SnapshotProperty->ContainerPtrToValuePtr<FCkVoxelNavGym_WireSnapshot>(
            R.State[W].Get()) : nullptr; }

    const FCkVoxelNavGym_ControlAck* Ack(const FRun& R, APlayerController* PC)
    { return R.AckProperty && IsValid(PC)
        ? R.AckProperty->ContainerPtrToValuePtr<FCkVoxelNavGym_ControlAck>(PC)
        : nullptr; }

    bool Presented(APlayerController* PC, FCkVoxelNavGym_WireSnapshot& Out)
    {
        Out = {};
        FCall C(PC, TEXT("Get_PresentedFrame"));
        auto* P = Field<FStructProperty>(C.Function, TEXT("ReturnValue"));
        if (!P || P->Struct != FCkVoxelNavGym_WireSnapshot::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = *P->ContainerPtrToValuePtr<FCkVoxelNavGym_WireSnapshot>(
            C.Args->GetStructMemory());
        return true;
    }

    bool ReplicaAgent(FRun& R, int32 W, int32 Index, int64 Pair,
        FCk_Handle& Out,
        FTransform& Pose)
    {
        Out = {};
        Pose = FTransform::Identity;
        if (W <= 0 || W >= Worlds || !TaggedAgent(R.World[W].Get(),
                R.WorldRoot[W], Index, Out) ||
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveCrowdAgent(Out, R.World[W].Get()) ||
            UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVoxelPath(Out, R.World[W].Get()) ||
            UCk_Utils_Velocity_UE::Has(Out) ||
            UCk_Utils_Acceleration_UE::Has(Out) ||
            Out.Has<ck::FFragment_EulerIntegrator>() ||
            !AgentIdentity(Out, Index, Pair) ||
            !UCk_Utils_EntityReplicationDriver_UE::Has(Out) ||
            !UCk_Utils_EntityReplicationDriver_UE::Get_IsReplicationComplete(Out) ||
            Out.Has<ck::FTag_RepFragments_PendingApply>() || !TransformOf(Out, Pose))
        { return false; }
        FCk_Handle PresentedHandle;
        FTransform PresentedPose;
        return PresentedAgent(R.Local[W - 1].Get(), Index, PresentedHandle) &&
            PresentedHandle == Out &&
            PresentedAgentTransform(R.Local[W - 1].Get(), Index, PresentedPose) &&
            PresentedPoseClose(PresentedPose, Pose);
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

    FString ContextDiagnostic(const FRun& R)
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* State = IsValid(Server) ? Server->GetGameState() : nullptr;
        auto* Mode = IsValid(Server) ? Server->GetAuthGameMode() : nullptr;
        FString Result = FString::Printf(
            TEXT("server=%p begun=%d mode=%d gameMode=%s gameState=%s pie=%d clients=%d captured=%d"),
            Server, IsValid(Server) && Server->HasBegunPlay(),
            IsValid(Server) ? static_cast<int32>(Server->GetNetMode()) : -1,
            IsValid(Mode) ? *Mode->GetClass()->GetName() : TEXT("none"),
            IsValid(State) ? *State->GetClass()->GetName() : TEXT("none"),
            ck::auto_test::net::Get_AllPIEWorlds().Num(),
            ck::auto_test::net::Get_NumClientWorlds(), R.Captured);
        for (int32 I = 0; I < Clients; ++I)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(I);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Remote = IsValid(Server)
                ? ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, I) : nullptr;
            Result += FString::Printf(TEXT(" c%d=%p/state=%s/pc=%s/remote=%s/root=%d"),
                I, Client,
                IsValid(Client) && IsValid(Client->GetGameState())
                    ? *Client->GetGameState()->GetClass()->GetName() : TEXT("none"),
                IsValid(PC) ? *PC->GetClass()->GetName() : TEXT("none"),
                IsValid(Remote) ? *Remote->GetClass()->GetName() : TEXT("none"),
                Allocated(Root(Client)));
        }
        return Result;
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

    bool Station(FRun& R, int32 W)
    {
        auto* PC = W == 0 ? R.Host.Get() : R.Local[W - 1].Get();
        if (!IsValid(PC)) { return W == 0 && R.Dedicated; }
        const auto Owner = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(PC);
        const FName Name(*FString::Printf(TEXT("%s_%s"), Tag, *PC->GetName()));
        const auto Matches = UCk_Utils_EntityTag_UE::ForEach_Entity(R.WorldRoot[W], Name);
        if (!Allocated(Owner) || Matches.Num() != 1 || !Allocated(Matches[0]) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Matches[0]))
        { return false; }
        const auto Cohort = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Matches[0]);
        if (!Direct(Matches[0], Cohort) || !Direct(Cohort, Owner) ||
            (Allocated(R.LocalStation[W]) && R.LocalStation[W] != Matches[0]))
        { return false; }
        R.LocalStation[W] = Matches[0];
        return true;
    }

    bool Bank(FRun& R, FBank& Out)
    {
        Out = {};
        const auto* Snapshot = Frame(R, 0);
        if (!Snapshot || Snapshot->Stations.Num() != 1) { return false; }
        FCk_Handle OutOfRange;
        if (!Source(R.State[0].Get(), 0, Out.Root) ||
            !Source(R.State[0].Get(), 1, OutOfRange) || Allocated(OutOfRange) ||
            !DescendsFrom(Out.Root, R.WorldRoot[0]) ||
            !UCk_VoxelNavGym_Runtime_UE::TryGet_SourceForest(
                Out.Root, R.WorldRoot[0], Out.Forest) ||
            !Out.Forest.Contains(Out.Root)) { return false; }
        for (const auto& H : Out.Forest)
        {
            if (UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVolume(H, R.World[0].Get()))
            { if (Allocated(Out.Volume) || !Direct(H, Out.Root)) { return false; } Out.Volume = H; }
            if (UCk_VoxelNavGym_Runtime_UE::Get_IsLiveCrowdAgent(H, R.World[0].Get()))
            {
                if (!UCk_VoxelNavGym_Runtime_UE::Get_IsLiveVoxelPath(H, R.World[0].Get()) ||
                    !Direct(H, Out.Root)) { return false; }
                FCkVoxelNavGym_AgentFrame Physical;
                if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                        H, R.World[0].Get(), 0, Physical)) { return false; }
                const int32 I = Physical.Transform.GetLocation().Y < 0.0 ? 0 : 1;
                FCk_Handle Tagged;
                if (!TaggedAgent(R.World[0].Get(), R.WorldRoot[0], I, Tagged) ||
                    Tagged != H || !AgentIdentity(H, I,
                        Snapshot->Stations[0].PairGeneration) ||
                    !UCk_Utils_EntityReplicationDriver_UE::Has(H) ||
                    !UCk_Utils_EntityReplicationDriver_UE::Get_IsReplicationComplete(H))
                { return false; }
                if (Allocated(Out.Agent[I])) { return false; }
                Out.Agent[I] = H;
            }
        }
        if (!Allocated(Out.Volume) || !Allocated(Out.Agent[0]) ||
            !Allocated(Out.Agent[1])) { return false; }
        auto MutableVolume = Out.Volume;
        Out.BuildEpoch = UCk_Utils_VoxelNavVolume_UE::Get_BuildEpoch(
            UCk_Utils_VoxelNavVolume_UE::CastChecked(MutableVolume));
        if (Out.BuildEpoch <= 0) { return false; }
        for (int32 I = 0; I < 2; ++I)
        { if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentForest(Out.Root,
                Out.Agent[I], R.WorldRoot[0], Out.AgentForest[I]) ||
              !Out.AgentForest[I].Contains(Out.Agent[I])) { return false; } }
        for (const auto& H : Out.Forest)
        { if (H != Out.Root && H != Out.Volume &&
              !Out.AgentForest[0].Contains(H) && !Out.AgentForest[1].Contains(H))
          { return false; } }
        return true;
    }

    bool Block(FRun& R, const TCHAR* Why)
    { R.LastBlock = Why; return false; }

    bool Converged(FRun& R, int64 Generation, int64 Pair, int64 Controls,
        bool Outbound, bool InjectedRequester = false)
    {
        if (!Context(R)) { return Block(R, TEXT("world/player context")); }
        const auto* A = Frame(R, 0);
        if (!A || A->Phase != 2 || A->Replacing != -1 ||
            A->ControlGeneration != Generation || A->Sequence <= 0 ||
            !UCk_VoxelNavGym_Runtime_UE::Get_IsValidFrame(*A) ||
            A->Stations[0].PairGeneration != Pair ||
            A->Stations[0].ControlCount != Controls ||
            A->Stations[0].GoalsAreOutbound != Outbound)
        { return Block(R, TEXT("authority complete frame")); }
        if (!Bank(R, R.Bank)) { return Block(R, TEXT("authority exact source forest")); }
        for (int32 W = 1; W < Worlds; ++W)
        {
            const auto* B = Frame(R, W);
            const auto* Known = B ? R.History.Find(B->Sequence) : nullptr;
            bool Ready = false;
            const bool ExpectedReady = !(InjectedRequester && W == 1);
            FCkVoxelNavGym_WireSnapshot Drawn;
            FCk_Handle LocalSource, OutOfRange;
            if (!B || B->Phase != 2 || B->ControlGeneration != Generation ||
                B->Stations.Num() != 1 || !Known || !Same(*Known, *B) ||
                B->Stations[0].PairGeneration != Pair ||
                B->Stations[0].ControlCount != Controls ||
                !Bool(R.Local[W - 1].Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                Ready != ExpectedReady ||
                !Presented(R.Local[W - 1].Get(), Drawn) ||
                Drawn.ControlGeneration != Generation ||
                Drawn.Sequence > B->Sequence || !R.History.Contains(Drawn.Sequence) ||
                !Same(*R.History.Find(Drawn.Sequence), Drawn) ||
                !Source(R.State[W].Get(), 0, LocalSource) || Allocated(LocalSource) ||
                !Source(R.State[W].Get(), 1, OutOfRange) || Allocated(OutOfRange) ||
                !Station(R, W))
            { return Block(R, TEXT("client passive matching frame and station")); }
            for (int32 I = 0; I < 2; ++I)
            {
                FCk_Handle Agent;
                FTransform Pose;
                if (!ReplicaAgent(R, W, I, Pair, Agent, Pose) ||
                    (Allocated(R.ClientAgent[W][I]) && R.ClientAgent[W][I] != Agent))
                { return Block(R, TEXT("client replicated agent/Transform and presented ECS handle")); }
                R.ClientAgent[W][I] = Agent;
            }
        }
        if (!R.Dedicated)
        {
            bool Ready = false;
            FCkVoxelNavGym_WireSnapshot Drawn;
            if (!Bool(R.Host.Get(), TEXT("Get_IsLocalPresentationReady"), Ready) ||
                !Ready || !Presented(R.Host.Get(), Drawn) ||
                Drawn.ControlGeneration != Generation ||
                Drawn.Sequence > A->Sequence || !R.History.Contains(Drawn.Sequence) ||
                !Same(*R.History.Find(Drawn.Sequence), Drawn) || !Station(R, 0))
            { return Block(R, TEXT("host matching frame and station")); }
            for (int32 I = 0; I < 2; ++I)
            {
                FCk_Handle PresentedHandle;
                FTransform PresentedPose, ActualPose;
                if (!PresentedAgent(R.Host.Get(), I, PresentedHandle) ||
                    PresentedHandle != R.Bank.Agent[I] ||
                    !PresentedAgentTransform(R.Host.Get(), I, PresentedPose) ||
                    !TransformOf(PresentedHandle, ActualPose) ||
                    !PresentedPoseClose(PresentedPose, ActualPose))
                { return Block(R, TEXT("host presentation uses authority ECS Transform")); }
            }
        }
        R.LastBlock.Reset();
        return true;
    }

    void ObserveMotion(FRun& R)
    {
        for (int32 I = 0; I < 2; ++I)
        {
            FCk_Handle Authority;
            FTransform Pose;
            if (!TaggedAgent(R.World[0].Get(), R.WorldRoot[0], I, Authority) ||
                !AgentIdentity(Authority, I, 2) || !TransformOf(Authority, Pose))
            { continue; }
            const auto Position = Pose.GetLocation();
            R.AuthorityHistory[I].Add(Position);
            if (R.AuthorityHistory[I].Num() > 1024) { R.AuthorityHistory[I].RemoveAt(0); }
            R.SawTravel[0][I] |= FVector::Dist(Position, Seed[I]) > 125.0;
            if (!R.Dedicated)
            {
                FCk_Handle PresentedHandle;
                FTransform PresentedPose;
                if (PresentedAgent(R.Host.Get(), I, PresentedHandle) && PresentedHandle == Authority &&
                    PresentedAgentTransform(R.Host.Get(), I, PresentedPose) &&
                    PresentedPoseClose(PresentedPose, Pose))
                { R.SawHostTravel[I] |= FVector::Dist(PresentedPose.GetLocation(), Seed[I]) > 125.0; }
            }
        }
        for (int32 W = 1; W < Worlds; ++W)
        {
            for (int32 I = 0; I < 2; ++I)
            {
                FCk_Handle Agent;
                FTransform Pose;
                if (!ReplicaAgent(R, W, I, 2, Agent, Pose)) { continue; }
                R.ClientAgent[W][I] = Agent;
                const auto Position = Pose.GetLocation();
                if (FVector::Dist(Position, Seed[I]) <= 125.0) { continue; }
                R.SawTravel[W][I] = true;
                for (const auto& AuthorityPosition : R.AuthorityHistory[I])
                {
                    if (FVector::Dist(AuthorityPosition, Seed[I]) > 125.0 &&
                        FVector::Dist(Position, AuthorityPosition) <= 50.0)
                    { R.SawReplicaMatch[W][I] = true; break; }
                }
            }
        }
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
            { Run->Deadline = FPlatformTime::Seconds() + 120.0; }
            if (FPlatformTime::Seconds() > Run->Deadline)
            { return Stop(TEXT("VoxelNav authored fixture timed out")); }
            if (!Context(*Run))
            { Run->LastBlock = ContextDiagnostic(*Run); return false; }
            if (!Observe(*Run))
            {
                const auto* ServerFrame = Frame(*Run, 0);
                Run->LastBlock = FString::Printf(
                    TEXT("Observe: authority sequence=%lld phase=%d client sequences=%lld,%lld"),
                    static_cast<long long>(ServerFrame ? ServerFrame->Sequence : -1),
                    ServerFrame ? ServerFrame->Phase : -1,
                    static_cast<long long>(Frame(*Run, 1) ? Frame(*Run, 1)->Sequence : -1),
                    static_cast<long long>(Frame(*Run, 2) ? Frame(*Run, 2)->Sequence : -1));
                return false;
            }
            const auto* A = Frame(*Run, 0);
            if (!A) { Run->LastBlock = TEXT("authority snapshot property unavailable"); return false; }
            if (Run->Step == FRun::EStep::Baseline)
            {
                if (!Converged(*Run, 1, 1, 0, true)) { return false; }
                TArray<AActor*> Actors;
                if (!Fixtures(Run->State[0].Get(), Actors) || Actors.Num() != 2 ||
                    !IsValid(Actors[0]) || !IsValid(Actors[1]))
                { return Stop(TEXT("authority fixture pair unavailable")); }
                Run->Fixture[0] = Actors[0]; Run->Fixture[1] = Actors[1];
                TArray<FCk_Handle> Invalid;
                Invalid.Add(Run->Bank.Root);
                if (UCk_VoxelNavGym_Runtime_UE::TryGet_SourceForest(
                        FCk_Handle{}, Run->WorldRoot[0], Invalid) || !Invalid.IsEmpty() ||
                    UCk_VoxelNavGym_Runtime_UE::TryGet_AgentForest(
                        Run->Bank.Root, FCk_Handle{}, Run->WorldRoot[0], Invalid) ||
                    !Invalid.IsEmpty() ||
                    UCk_VoxelNavGym_Runtime_UE::Get_IsLiveInWorld(
                        Run->WorldRoot[1], Run->World[0].Get()))
                { return Stop(TEXT("default or foreign VoxelNav runtime request accepted")); }
                FCkVoxelNavGym_AgentFrame Rejected;
                Rejected.Index = 77;
                if (UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                        FCk_Handle{}, Run->World[0].Get(), 0, Rejected) ||
                    Rejected.Index != -1 ||
                    UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                        Run->Bank.Agent[0], Run->World[0].Get(), 2, Rejected) ||
                    Rejected.Index != -1)
                { return Stop(TEXT("default or invalid-index agent frame accepted")); }
                Run->StaleProbe = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                    Run->WorldRoot[0]);
                if (!Allocated(Run->StaleProbe) ||
                    UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                        Run->StaleProbe, Run->World[0].Get(), 0, Rejected) ||
                    Rejected.Index != -1)
                { return Stop(TEXT("partially composed agent frame accepted")); }
                Run->StaleProbe.Add<ck::FFragment_CrowdAgent_Tunables>(
                    FCk_CrowdAgent_Spec{42.0f, 192.0f});
                Rejected.Index = 77;
                if (UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                        Run->StaleProbe, Run->World[0].Get(), 0, Rejected) ||
                    Rejected.Index != -1)
                { return Stop(TEXT("incomplete crowd feature accepted")); }
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->StaleProbe);
                Run->Before = Run->Bank;
                Run->Step = FRun::EStep::Restart;
                return false;
            }
            if (Run->Step == FRun::EStep::Restart)
            {
                if (Allocated(Run->StaleProbe)) { return false; }
                FCkVoxelNavGym_AgentFrame Rejected;
                Rejected.Index = 77;
                if (UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                        Run->StaleProbe, Run->World[0].Get(), 0, Rejected) ||
                    Rejected.Index != -1)
                { return Stop(TEXT("stale agent frame accepted")); }
                // All owner clients and the listen host have joined before the fresh
                // pair starts, so their replicated observations include the motion.
                Run->BeforeFrame = *A;
                for (int32 W = 1; W < Worlds; ++W)
                { for (int32 I = 0; I < 2; ++I)
                  { Run->OldClientAgent[W][I] = Run->ClientAgent[W][I];
                    Run->ClientAgent[W][I] = {}; } }
                for (int32 I = 0; I < 2; ++I)
                {
                    Run->AuthorityHistory[I].Reset();
                    Run->SawHostTravel[I] = false;
                    for (int32 W = 0; W < Worlds; ++W)
                    { Run->SawTravel[W][I] = false;
                      Run->SawReplicaMatch[W][I] = false; }
                }
                Run->Owner = 0; Run->Action = 0;
                Run->Serial = Run->NextSerial[0]++;
                if (!Activate(Run->Local[0].Get(), 0))
                { return Stop(TEXT("authored R control unavailable")); }
                Run->Step = FRun::EStep::WaitRestart;
                Run->Deadline = FPlatformTime::Seconds() + 120.0;
                return false;
            }
            if (Run->Step == FRun::EStep::WaitRestart)
            {
                ObserveMotion(*Run);
                const auto* Receipt = Ack(*Run, Run->Local[0].Get());
                if (!Receipt || Receipt->Serial != Run->Serial || Receipt->Result != 1 ||
                    !Converged(*Run, 2, 2, 1, true)) { return false; }
                if (Receipt->Action != 0 ||
                    Receipt->RequestedGeneration != Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration != 2 ||
                    Receipt->CompletedSequence <= Run->BeforeFrame.Sequence ||
                    Receipt->CompletedSequence > A->Sequence ||
                    Run->Bank.Root != Run->Before.Root ||
                    Run->Bank.Volume != Run->Before.Volume ||
                    Run->Bank.BuildEpoch != Run->Before.BuildEpoch ||
                    Run->Bank.Agent[0] == Run->Before.Agent[0] ||
                    Run->Bank.Agent[1] == Run->Before.Agent[1])
                { return Stop(TEXT("R changed source/volume or failed to replace pair")); }
                for (int32 I = 0; I < 2; ++I)
                { for (const auto& H : Run->Before.AgentForest[I])
                  { if (Allocated(H)) { return Stop(TEXT("R retained old agent forest")); } } }
                for (int32 W = 1; W < Worlds; ++W)
                { for (int32 I = 0; I < 2; ++I)
                  { if (Allocated(Run->OldClientAgent[W][I])) { return false; } } }
                for (int32 W = 0; W < Worlds; ++W)
                { for (int32 I = 0; I < 2; ++I)
                  { if (!Run->SawTravel[W][I] ||
                        (W > 0 && !Run->SawReplicaMatch[W][I])) { return false; } } }
                if (!Run->Dedicated && (!Run->SawHostTravel[0] || !Run->SawHostTravel[1]))
                { return false; }
                Run->Before = Run->Bank;
                Run->Step = FRun::EStep::Reverse;
                return false;
            }
            if (Run->Step == FRun::EStep::Reverse)
            {
                Run->BeforeFrame = *A;
                Run->Owner = 1; Run->Action = 1;
                Run->Serial = Run->NextSerial[1]++;
                if (!Activate(Run->Local[1].Get(), 1))
                { return Stop(TEXT("authored T control unavailable")); }
                Run->Step = FRun::EStep::WaitReverse;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::WaitReverse)
            {
                const auto* Receipt = Ack(*Run, Run->Local[1].Get());
                if (!Receipt || Receipt->Serial != Run->Serial || Receipt->Result != 1 ||
                    !Converged(*Run, 3, 2, 2, false)) { return false; }
                if (Receipt->Action != 1 ||
                    Receipt->RequestedGeneration != Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration != 3 ||
                    Receipt->CompletedSequence <= Run->BeforeFrame.Sequence ||
                    Receipt->CompletedSequence > A->Sequence ||
                    Run->Bank.Root != Run->Before.Root ||
                    Run->Bank.Volume != Run->Before.Volume ||
                    Run->Bank.BuildEpoch != Run->Before.BuildEpoch ||
                    Run->Bank.Agent[0] != Run->Before.Agent[0] ||
                    Run->Bank.Agent[1] != Run->Before.Agent[1])
                { return Stop(TEXT("T replaced pair or failed to reverse both goals")); }
                Run->Step = Run->Dedicated ? FRun::EStep::Invalid :
                    FRun::EStep::HostReverse;
                return false;
            }
            if (Run->Step == FRun::EStep::HostReverse)
            {
                Run->BeforeFrame = *A; Run->Before = Run->Bank;
                Run->Owner = 2; Run->Action = 1;
                Run->Serial = Run->NextSerial[2]++;
                if (!Activate(Run->Host.Get(), 1))
                { return Stop(TEXT("listen host T control unavailable")); }
                Run->Step = FRun::EStep::WaitHostReverse;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
                return false;
            }
            if (Run->Step == FRun::EStep::WaitHostReverse)
            {
                const auto* Receipt = Ack(*Run, Run->Host.Get());
                if (!Receipt || Receipt->Serial != Run->Serial || Receipt->Result != 1 ||
                    !Converged(*Run, 4, 2, 3, true)) { return false; }
                if (Receipt->Action != 1 ||
                    Receipt->RequestedGeneration != Run->BeforeFrame.ControlGeneration ||
                    Receipt->CompletedGeneration != 4 ||
                    Receipt->CompletedSequence <= Run->BeforeFrame.Sequence ||
                    Receipt->CompletedSequence > A->Sequence ||
                    Run->Bank.Root != Run->Before.Root ||
                    Run->Bank.Volume != Run->Before.Volume ||
                    Run->Bank.BuildEpoch != Run->Before.BuildEpoch ||
                    Run->Bank.Agent[0] != Run->Before.Agent[0] ||
                    Run->Bank.Agent[1] != Run->Before.Agent[1])
                { return Stop(TEXT("listen host T changed bank or build epoch")); }
                Run->Step = FRun::EStep::Invalid;
                return false;
            }
            if (Run->Step == FRun::EStep::Invalid)
            {
                Run->BeforeFrame = *A;
                Run->Before = Run->Bank;
                for (int32 I = 0; I < 2; ++I)
                { if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                          Run->Bank.Agent[I], Run->World[0].Get(), I,
                          Run->BeforePhysical[I]))
                  { return Stop(TEXT("authority agent unavailable before malformed control")); } }
                if (!SendControl(Run->Local[0].Get(), Run->NextSerial[0], 1,
                        A->ControlGeneration, 99) ||
                    !SendControl(Run->Local[0].Get(), Run->NextSerial[0], 1,
                        A->ControlGeneration - 1, 0))
                { return Stop(TEXT("malformed/stale VoxelNav RPC unavailable")); }
                Run->SettleUntil = FPlatformTime::Seconds() + 0.25;
                Run->Step = FRun::EStep::Rejected;
                return false;
            }
            if (Run->Step == FRun::EStep::Rejected)
            {
                if (FPlatformTime::Seconds() < Run->SettleUntil) { return false; }
                const auto* Receipt = Ack(*Run, Run->Local[0].Get());
                if (!Receipt || Receipt->Serial != Run->NextSerial[0] ||
                    Receipt->Action != 0 || Receipt->Result != 2) { return false; }
                if (A->Phase != 2 ||
                    A->ControlGeneration != Run->BeforeFrame.ControlGeneration ||
                    A->Stations.Num() != 1 || A->Stations[0].PairGeneration != 2 ||
                    A->Stations[0].ControlCount != (Run->Dedicated ? 2 : 3) ||
                    A->Stations[0].GoalsAreOutbound != !Run->Dedicated ||
                    Run->BeforeFrame.Stations.Num() != 1 ||
                    !Bank(*Run, Run->Bank) ||
                    Run->Bank.Root != Run->Before.Root ||
                    Run->Bank.Volume != Run->Before.Volume ||
                    Run->Bank.BuildEpoch != Run->Before.BuildEpoch ||
                    Run->Bank.Agent[0] != Run->Before.Agent[0] ||
                    Run->Bank.Agent[1] != Run->Before.Agent[1])
                { return Stop(TEXT("invalid VoxelNav request mutated source")); }
                for (int32 I = 0; I < 2; ++I)
                {
                    FCkVoxelNavGym_AgentFrame Now;
                    if (!UCk_VoxelNavGym_Runtime_UE::TryGet_AgentFrame(
                            Run->Bank.Agent[I], Run->World[0].Get(), I, Now) ||
                        Now.MoveEpisode != Run->BeforePhysical[I].MoveEpisode ||
                        Now.MoveCorrelation != Run->BeforePhysical[I].MoveCorrelation ||
                        !Now.Goal.Equals(Run->BeforePhysical[I].Goal, 0.01))
                    { return Stop(TEXT("rejected VoxelNav request submitted movement")); }
                }
                if (!Converged(*Run, Run->BeforeFrame.ControlGeneration, 2,
                        Run->Dedicated ? 2 : 3, !Run->Dedicated, true))
                { return false; }
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->Before.Root);
                Run->Step = FRun::EStep::Failed;
                Run->Deadline = FPlatformTime::Seconds() + 90.0;
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
                for (const auto& H : Run->Before.Forest)
                { if (Allocated(H)) { return false; } }
                for (int32 W = 1; W < Worlds; ++W)
                { for (int32 I = 0; I < 2; ++I)
                  {
                      if (Allocated(Run->ClientAgent[W][I])) { return false; }
                      FCk_Handle Tagged;
                      if (TaggedAgent(Run->World[W].Get(), Run->WorldRoot[W], I, Tagged))
                      { return false; }
                      FCk_Handle PresentedHandle;
                      if (!PresentedAgent(Run->Local[W - 1].Get(), I, PresentedHandle) ||
                          Allocated(PresentedHandle)) { return false; }
                  } }
                TArray<AActor*> Actors;
                if (!Fixtures(Run->State[0].Get(), Actors) || !Actors.IsEmpty())
                { return false; }
                for (const auto& Fixture : Run->Fixture)
                { if (Fixture.IsValid() && !Fixture->IsActorBeingDestroyed())
                  { return false; } }
                for (int32 W = 1; W < Worlds; ++W)
                { if (!Fixtures(Run->State[W].Get(), Actors) || !Actors.IsEmpty())
                  { return false; } }
                Run->BeforeFrame = *A;
                if (!SendControl(Run->Local[0].Get(), Run->NextSerial[0] + 1,
                        1, A->ControlGeneration, 0))
                { return Stop(TEXT("late VoxelNav RPC unavailable")); }
                Run->SettleUntil = FPlatformTime::Seconds() + 0.25;
                Run->Step = FRun::EStep::PostFailed;
                return false;
            }
            if (Run->Step == FRun::EStep::PostFailed)
            {
                if (FPlatformTime::Seconds() < Run->SettleUntil) { return false; }
                const auto* Receipt = Ack(*Run, Run->Local[0].Get());
                if (!Receipt || Receipt->Serial != Run->NextSerial[0] + 1 ||
                    Receipt->Action != 0 || Receipt->Result != 2)
                { return false; }
                if (A->Phase != 4 ||
                    A->ControlGeneration != Run->BeforeFrame.ControlGeneration ||
                    A->Sequence != Run->BeforeFrame.Sequence)
                { return Stop(TEXT("terminal VoxelNav session changed after late control")); }
                Run->Step = FRun::EStep::Done;
                return false;
            }
            return Stop(TEXT("unexpected VoxelNav authored phase"));
        }
    private:
        bool Stop(const TCHAR* Why)
        {
            Run->Failed = true;
            FString Diagnostic;
            String(Run->State[0].Get(), TEXT("Get_DiagnosticState"), Diagnostic);
            const auto* A = Frame(*Run, 0);
            Test->AddError(FString::Printf(
                TEXT("%s [step=%d block=%s authority=%d/%lld/%lld motion=%d%d/%d%d/%d%d host=%d%d match=%d%d/%d%d state=%s]"),
                Why, static_cast<int32>(Run->Step), *Run->LastBlock,
                A ? A->Phase : -1,
                static_cast<long long>(A ? A->ControlGeneration : -1),
                static_cast<long long>(A ? A->Sequence : -1),
                Run->SawTravel[0][0], Run->SawTravel[0][1],
                Run->SawTravel[1][0], Run->SawTravel[1][1],
                Run->SawTravel[2][0], Run->SawTravel[2][1],
                Run->SawHostTravel[0], Run->SawHostTravel[1],
                Run->SawReplicaMatch[1][0], Run->SawReplicaMatch[1][1],
                Run->SawReplicaMatch[2][0], Run->SawReplicaMatch[2][1],
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

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkVoxelNavFlyingVsGroundedGym_SharedWorld,
    "Ck.VoxelNav.FlyingVsGrounded.Gym.SharedWorld.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkVoxelNavFlyingVsGroundedGym_SharedWorld::RunTest(const FString& Parameters)
{
    using namespace ck_voxel_nav_authored_network;
    auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported VoxelNav topology")); return false; }
    Run->BaseMode = FSoftClassPath(BaseModePath).TryLoadClass<AGameModeBase>();
    Run->Mode = FSoftClassPath(ModePath).TryLoadClass<AGameModeBase>();
    Run->StateClass = FSoftClassPath(StatePath).TryLoadClass<AGameStateBase>();
    Run->PCClass = FSoftClassPath(PCPath).TryLoadClass<APlayerController>();
    Run->PawnClass = FSoftClassPath(PawnPath).TryLoadClass<APawn>();
    Run->SnapshotProperty = Field<FStructProperty>(Run->StateClass, TEXT("Snapshot"));
    Run->AckProperty = Field<FStructProperty>(Run->PCClass, TEXT("LastControlAck"));
    if (!TestTrue(TEXT("authored VoxelNav classes and reflected controls"),
            Run->BaseMode && Run->Mode && Run->StateClass && Run->PCClass &&
            Run->PawnClass && Run->SnapshotProperty && Run->AckProperty &&
            Run->SnapshotProperty->Struct == FCkVoxelNavGym_WireSnapshot::StaticStruct() &&
            Run->AckProperty->Struct == FCkVoxelNavGym_ControlAck::StaticStruct() &&
            Run->PCClass->FindFunctionByName(TEXT("Request_ControlActivated")) &&
            Run->PCClass->FindFunctionByName(TEXT("Get_PresentedAgent")) &&
            Run->PCClass->FindFunctionByName(TEXT("Get_PresentedAgentTransform")) &&
            Run->PCClass->FindFunctionByName(TEXT("Get_IsLocalPresentationReady")) &&
            Run->PCClass->FindFunctionByName(TEXT("Server_RequestVoxelControl")) &&
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
            return IsValid(Registry) && Registry->Find_GymIndexByName(
                TEXT("VoxelNav Flying Vs Grounded")) != INDEX_NONE;
        }), 30, TEXT("real VoxelNav gym registry entry available")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* W)
        {
            auto* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owner requests real VoxelNav gym travel"),
                IsValid(PC) && UCk_Utils_GymRegistry_UE::
                    TryRequest_TravelToGymFromPlayer(PC,
                        TEXT("VoxelNav Flying Vs Grounded")));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]()
        { return Context(*Run); }), 60,
        TEXT("exact VoxelNav worlds and owners converge")));
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
        }), TEXT("VoxelNav authored worlds close after terminal failure"), true));
    return true;
}

#endif
