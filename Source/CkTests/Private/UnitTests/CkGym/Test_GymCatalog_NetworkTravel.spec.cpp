#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGymRegistry_Utils.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkAggro/CkAggro_Utils.h"
#include "CkAggro/CkAggroTarget_Utils.h"

#include <Engine/GameInstance.h>
#include <Engine/LocalPlayer.h>
#include <Engine/NetDriver.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <GameFramework/GameStateBase.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_gym_network_travel_test
{
    constexpr auto AggroStatePath = TEXT("/Script/Angelscript.Ck_AggroGym_GameState");
    constexpr auto AggroControllerPath = TEXT("/Script/Angelscript.Ck_AggroGym_PlayerController");

    struct FObservedSnapshot
    {
        int32 Generation = 0;
        int32 Sequence = 0;
        TSharedPtr<FStructOnScope> Copy;
    };

    struct FSharedDemo
    {
        UClass* StateClass = nullptr;
        UClass* ControllerClass = nullptr;
        FStructProperty* Snapshot = nullptr;
        FIntProperty* Generation = nullptr;
        FIntProperty* Sequence = nullptr;
        FBoolProperty* Ready = nullptr;
        FBoolProperty* Failed = nullptr;
        FArrayProperty* ChaseDummies = nullptr;
        FArrayProperty* ChaseThreats = nullptr;
        FStructProperty* ChaseGuard = nullptr;
        FIntProperty* ChaseActiveIndex = nullptr;
        FIntProperty* ChaseTrackedCount = nullptr;
        FStructProperty* PerceptionGuard = nullptr;
        FStructProperty* PerceptionOrbiter = nullptr;
        FBoolProperty* PerceptionInCone = nullptr;
        FNumericProperty* PerceptionThreat = nullptr;
        FIntProperty* StressGuardCount = nullptr;
        FIntProperty* StressTargetsPerGuard = nullptr;
        FArrayProperty* StressGuards = nullptr;
        FIntProperty* StressActiveMask = nullptr;
        FIntProperty* StressActiveCount = nullptr;
        FIntProperty* ServerTrioCount = nullptr;
        FStructProperty* ServerChase = nullptr;
        FStructProperty* ServerPerception = nullptr;
        FStructProperty* ServerStress = nullptr;
        FIntProperty* LastResetSerial = nullptr;
        FIntProperty* LastResetResult = nullptr;
        UFunction* ResetRpc = nullptr;
        int32 InitialGeneration = 0;
        int32 HistoryGeneration = 0;
        TArray<FObservedSnapshot> ObservedSnapshots;
        int32 ResetSerial = 1401;
        int32 DepartureSequence = 0;
        int32 FailureSequence = 0;
        TArray<FCk_Handle> OldChildren;
        TArray<FCk_Handle> FailedGenerationChildren;
        TWeakObjectPtr<AGameStateBase> SurvivorState;
        TWeakObjectPtr<UGameInstance> SurvivorClientInstance;
        TWeakObjectPtr<APlayerController> DepartingServerController;
        TWeakObjectPtr<UGameInstance> DepartingClientInstance;
    };

    auto GetAggroState(UWorld* InWorld, const FSharedDemo& InDemo) -> AGameStateBase*
    {
        auto* State = IsValid(InWorld) ? InWorld->GetGameState() : nullptr;
        return IsValid(State) && State->IsA(InDemo.StateClass) ? State : nullptr;
    }

    auto GetSnapshot(AGameStateBase* InState, const FSharedDemo& InDemo) -> const void*
    {
        return IsValid(InState) ? InDemo.Snapshot->ContainerPtrToValuePtr<void>(InState) : nullptr;
    }

    auto GetServerChild(AGameStateBase* InState, FStructProperty* InProperty) -> FCk_Handle
    {
        return IsValid(InState) ? *InProperty->ContainerPtrToValuePtr<FCk_Handle>(InState) : FCk_Handle{};
    }

    auto SnapshotGeneration(AGameStateBase* InState, const FSharedDemo& InDemo) -> int32
    {
        const auto* Snapshot = GetSnapshot(InState, InDemo);
        return Snapshot ? InDemo.Generation->GetPropertyValue_InContainer(Snapshot) : INDEX_NONE;
    }

    auto SnapshotSequence(AGameStateBase* InState, const FSharedDemo& InDemo) -> int32
    {
        const auto* Snapshot = GetSnapshot(InState, InDemo);
        return Snapshot ? InDemo.Sequence->GetPropertyValue_InContainer(Snapshot) : INDEX_NONE;
    }

    auto SnapshotReady(AGameStateBase* InState, const FSharedDemo& InDemo, int32 InGeneration) -> bool
    {
        const auto* Snapshot = GetSnapshot(InState, InDemo);
        return Snapshot && InDemo.Generation->GetPropertyValue_InContainer(Snapshot) == InGeneration &&
            InDemo.Sequence->GetPropertyValue_InContainer(Snapshot) > 0 &&
            InDemo.Ready->GetPropertyValue_InContainer(Snapshot) &&
            !InDemo.Failed->GetPropertyValue_InContainer(Snapshot);
    }

    auto SnapshotFailed(AGameStateBase* InState, const FSharedDemo& InDemo, int32 InGeneration) -> bool
    {
        const auto* Snapshot = GetSnapshot(InState, InDemo);
        return Snapshot && InDemo.Generation->GetPropertyValue_InContainer(Snapshot) == InGeneration &&
            !InDemo.Ready->GetPropertyValue_InContainer(Snapshot) &&
            InDemo.Failed->GetPropertyValue_InContainer(Snapshot);
    }

    auto SnapshotHasCompleteSamples(AGameStateBase* InState, const FSharedDemo& InDemo) -> bool
    {
        const auto* Snapshot = GetSnapshot(InState, InDemo);
        if (!Snapshot) { return false; }
        const auto Count = [Snapshot](FArrayProperty* InArray) -> int32
        { return FScriptArrayHelper(InArray, InArray->ContainerPtrToValuePtr<void>(Snapshot)).Num(); };
        return Count(InDemo.ChaseDummies) == 3 && Count(InDemo.ChaseThreats) == 3 &&
            InDemo.ChaseTrackedCount->GetPropertyValue_InContainer(Snapshot) == 3 &&
            InDemo.ChaseActiveIndex->GetPropertyValue_InContainer(Snapshot) >= -1 &&
            InDemo.ChaseActiveIndex->GetPropertyValue_InContainer(Snapshot) < 3 &&
            InDemo.PerceptionThreat->GetFloatingPointPropertyValue(
                InDemo.PerceptionThreat->ContainerPtrToValuePtr<void>(Snapshot)) >= 0.0f &&
            Count(InDemo.StressGuards) == 24 &&
            InDemo.StressGuardCount->GetPropertyValue_InContainer(Snapshot) == 24 &&
            InDemo.StressTargetsPerGuard->GetPropertyValue_InContainer(Snapshot) == 5 &&
            InDemo.StressActiveCount->GetPropertyValue_InContainer(Snapshot) >= 0 &&
            InDemo.StressActiveCount->GetPropertyValue_InContainer(Snapshot) <= 24;
    }

    auto AllSnapshotsReady(FSharedDemo& InDemo, int32 InGeneration) -> bool
    {
        UWorld* Worlds[] = {ck::auto_test::net::Get_ServerWorld(),
            ck::auto_test::net::Get_ClientWorld(0), ck::auto_test::net::Get_ClientWorld(1)};
        if (ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) { return false; }
        auto* ServerState = GetAggroState(Worlds[0], InDemo);
        if (!SnapshotReady(ServerState, InDemo, InGeneration) ||
            !SnapshotHasCompleteSamples(ServerState, InDemo)) { return false; }
        if (InDemo.HistoryGeneration != InGeneration)
        {
            InDemo.ObservedSnapshots.Reset();
            InDemo.HistoryGeneration = InGeneration;
        }
        const auto ServerSequence = SnapshotSequence(ServerState, InDemo);
        FObservedSnapshot* Observed = nullptr;
        for (auto& Candidate : InDemo.ObservedSnapshots)
        {
            if (Candidate.Generation == InGeneration && Candidate.Sequence == ServerSequence)
            { Observed = &Candidate; break; }
        }
        if (!Observed)
        {
            if (InDemo.ObservedSnapshots.Num() >= 32)
            { InDemo.ObservedSnapshots.RemoveAt(0); }
            Observed = &InDemo.ObservedSnapshots.AddDefaulted_GetRef();
        }
        Observed->Generation = InGeneration;
        Observed->Sequence = ServerSequence;
        Observed->Copy = MakeShared<FStructOnScope>(InDemo.Snapshot->Struct.Get());
        InDemo.Snapshot->Struct->CopyScriptStruct(
            Observed->Copy->GetStructMemory(), GetSnapshot(ServerState, InDemo));
        for (int32 Index = 1; Index < UE_ARRAY_COUNT(Worlds); ++Index)
        {
            auto* State = GetAggroState(Worlds[Index], InDemo);
            if (!SnapshotReady(State, InDemo, InGeneration) ||
                !SnapshotHasCompleteSamples(State, InDemo)) { return false; }
            const auto ClientSequence = SnapshotSequence(State, InDemo);
            const auto* Match = InDemo.ObservedSnapshots.FindByPredicate(
                [InGeneration, ClientSequence](const FObservedSnapshot& Candidate)
                {
                    return Candidate.Generation == InGeneration &&
                        Candidate.Sequence == ClientSequence;
                });
            if (!Match || !Match->Copy.IsValid() ||
                !InDemo.Snapshot->Struct->CompareScriptStruct(
                    GetSnapshot(State, InDemo), Match->Copy->GetStructMemory(), 0))
            { return false; }
        }
        return true;
    }

    auto InvokeReset(APlayerController* InController, const FSharedDemo& InDemo, int32 InSerial) -> bool
    {
        if (!IsValid(InController) || !InController->IsA(InDemo.ControllerClass)) { return false; }
        struct FParams { int32 InSerial; } Params{InSerial};
        InController->ProcessEvent(InDemo.ResetRpc, &Params);
        return true;
    }
    struct FAggroStation
    {
        FName Tag;
        const TCHAR* ScriptClassPath;
    };

    const FAggroStation AggroStations[] = {
        {FName(TEXT("Gym.Aggro.Chase")), TEXT("/Script/Angelscript.Ck_EntityScript_AggroGym_Chase_Station")},
        {FName(TEXT("Gym.Aggro.Perception")), TEXT("/Script/Angelscript.Ck_EntityScript_AggroGym_Perception_Station")},
        {FName(TEXT("Gym.Aggro.Stress")), TEXT("/Script/Angelscript.Ck_EntityScript_AggroGym_Stress_Station")},
    };

    auto CountStations(UWorld* InWorld, FName InTag) -> int32
    {
        if (NOT IsValid(InWorld))
        { return -1; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld);
        return ck::IsValid(Root) ? UCk_Utils_EntityTag_UE::ForEach_Entity(Root, InTag).Num() : -1;
    }

    auto CountAuthoredStationScripts(UWorld* InWorld, const FAggroStation& InStation) -> int32
    {
        if (NOT IsValid(InWorld))
        { return -1; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld);
        if (ck::Is_NOT_Valid(Root))
        { return -1; }
        const auto Tagged = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, InStation.Tag);
        if (Tagged.Num() != 1 || ck::Is_NOT_Valid(Tagged[0]))
        { return -1; }

        auto Matches = 0;
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Tagged[0]))
        {
            if (ck::Is_NOT_Valid(Child) || NOT UCk_Utils_EntityScript_UE::Has(Child))
            { continue; }
            const auto Script = UCk_Utils_EntityScript_UE::CastChecked(Child);
            const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
            if (IsValid(Class) && Class->GetPathName() == InStation.ScriptClassPath)
            { ++Matches; }
        }
        return Matches;
    }

    auto AllAggroStationsReady(bool InRequireAuthoredScripts) -> bool
    {
        UWorld* Worlds[] = {
            ck::auto_test::net::Get_ServerWorld(),
            ck::auto_test::net::Get_ClientWorld(0),
            ck::auto_test::net::Get_ClientWorld(1),
        };
        for (auto* World : Worlds)
        {
            if (NOT IsValid(World))
            { return false; }
            for (const auto& Station : AggroStations)
            {
                if (CountStations(World, Station.Tag) != 1 ||
                    (InRequireAuthoredScripts && CountAuthoredStationScripts(World, Station) != 1))
                { return false; }
            }
        }
        return true;
    }

    struct FBranchCounts
    {
        int32 Aggro = 0;
        int32 Targets = 0;
    };

    auto CountLiveBranch(const FCk_Handle& InRoot, FBranchCounts& OutCounts) -> void
    {
        if (ck::Is_NOT_Valid(InRoot) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InRoot, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InRoot, ECk_EntityLifetime_DestructionPhase::Teardown) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InRoot, ECk_EntityLifetime_DestructionPhase::Destroyed))
        { return; }
        OutCounts.Aggro += UCk_Utils_Aggro_UE::Has(InRoot) ? 1 : 0;
        OutCounts.Targets += UCk_Utils_AggroTarget_UE::Has(InRoot) ? 1 : 0;
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(InRoot))
        { CountLiveBranch(Child, OutCounts); }
    }

    auto GetOriginalChild(UWorld* InWorld, const FAggroStation& InStation) -> FCk_Handle
    {
        const auto Root = IsValid(InWorld)
            ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld) : FCk_Handle{};
        if (ck::Is_NOT_Valid(Root)) { return {}; }
        const auto Stations = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, InStation.Tag);
        if (Stations.Num() != 1 || ck::Is_NOT_Valid(Stations[0])) { return {}; }
        FCk_Handle Match;
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Stations[0]))
        {
            if (ck::Is_NOT_Valid(Child) || !UCk_Utils_EntityScript_UE::Has(Child)) { continue; }
            const auto Script = UCk_Utils_EntityScript_UE::CastChecked(Child);
            const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
            if (IsValid(Class) && Class->GetPathName() == InStation.ScriptClassPath)
            {
                if (ck::IsValid(Match)) { return {}; }
                Match = Child;
            }
        }
        return Match;
    }

    auto CountDemo(UWorld* InWorld, bool InServer, TArray<FCk_Handle>* OutChildren = nullptr) -> bool
    {
        constexpr int32 ExpectedAggro[] = {1, 1, 24};
        constexpr int32 ExpectedTargets[] = {3, 1, 120};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(AggroStations); ++Index)
        {
            const auto Child = GetOriginalChild(InWorld, AggroStations[Index]);
            if (ck::Is_NOT_Valid(Child)) { return false; }
            if (OutChildren) { OutChildren->Add(Child); }
            FBranchCounts Counts;
            CountLiveBranch(Child, Counts);
            if (Counts.Aggro != (InServer ? ExpectedAggro[Index] : 0) ||
                Counts.Targets != (InServer ? ExpectedTargets[Index] : 0))
            { return false; }
        }
        return true;
    }

    auto NoLiveAggroDemo(UWorld* InWorld) -> bool
    {
        const auto Root = IsValid(InWorld)
            ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld) : FCk_Handle{};
        if (ck::Is_NOT_Valid(Root)) { return false; }
        for (const auto& Station : AggroStations)
        {
            const auto Stations = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, Station.Tag);
            if (Stations.Num() != 1 || ck::Is_NOT_Valid(Stations[0]) ||
                CountAuthoredStationScripts(InWorld, Station) != 0)
            { return false; }
            FBranchCounts Counts;
            CountLiveBranch(Stations[0], Counts);
            if (Counts.Aggro != 0 || Counts.Targets != 0) { return false; }
        }
        return true;
    }

    struct FSettingsBefore
    {
        ECkGym_StartupMode Mode;
        FString Last;
        TArray<FString> Recents;

        FSettingsBefore()
        {
            const auto* Settings = GetDefault<UCkGym_StartupSettings>();
            Mode = Settings->StartupMode;
            Last = Settings->LastGymName;
            Recents = Settings->RecentGymNames;
        }

        auto Restore() const -> void
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = Mode;
            Settings->LastGymName = Last;
            Settings->RecentGymNames = Recents;
            Settings->SaveConfig();
        }
    };

    auto GetRegistry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto HasSameCatalog(const UCkGym_Registry_Subsystem* InServer,
                        const UCkGym_Registry_Subsystem* InClient) -> bool
    {
        if (NOT IsValid(InServer) || NOT IsValid(InClient))
        { return false; }

        const auto& ServerEntries = InServer->Get_Entries();
        const auto& ClientEntries = InClient->Get_Entries();
        if (ServerEntries.IsEmpty() || ServerEntries.Num() != ClientEntries.Num() ||
            InServer->Get_CurrentGymIndex() != InClient->Get_CurrentGymIndex() ||
            InServer->Get_SuppressHUDDuringStartup() != InClient->Get_SuppressHUDDuringStartup())
        { return false; }

        for (auto Index = 0; Index < ServerEntries.Num(); ++Index)
        {
            if (ServerEntries[Index].DisplayName != ClientEntries[Index].DisplayName ||
                ServerEntries[Index].GameModeClass.Get() != ClientEntries[Index].GameModeClass.Get() ||
                ServerEntries[Index].LevelName != ClientEntries[Index].LevelName ||
                ServerEntries[Index].Category != ClientEntries[Index].Category ||
                ServerEntries[Index].HintCode != ClientEntries[Index].HintCode)
            { return false; }
        }
        return true;
    }

    auto MenusAreReady() -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
        auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
        if (NOT IsValid(Server) || NOT IsValid(Client0) || NOT IsValid(Client1) ||
            NOT IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) ||
            NOT IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1)))
        { return false; }

        const auto* State0 = Client0->GetGameState<ACkGym_GameState_UE>();
        const auto* State1 = Client1->GetGameState<ACkGym_GameState_UE>();
        const auto* PC0 = Cast<ACkGym_PlayerController_UE>(Client0->GetFirstPlayerController());
        const auto* PC1 = Cast<ACkGym_PlayerController_UE>(Client1->GetFirstPlayerController());
        const auto* Registry = GetRegistry(Server);
        return IsValid(State0) && State0->Get_HasPublishedCatalog() &&
            IsValid(State1) && State1->Get_HasPublishedCatalog() &&
            IsValid(PC0) && PC0->IsLocalController() &&
            IsValid(PC1) && PC1->IsLocalController() && PC0 != PC1 &&
            HasSameCatalog(Registry, GetRegistry(Client0)) &&
            HasSameCatalog(Registry, GetRegistry(Client1));
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkGymCatalog_NetworkTravel,
    "Ck.Gym.Network.CatalogAndTravel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkGymCatalog_NetworkTravel::RunTest(const FString& Parameters)
{
    using namespace ck_gym_network_travel_test;

    auto DedicatedServer = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, DedicatedServer))
    { AddError(TEXT("unsupported gym network topology")); return false; }

    auto* BaseGymMode = FSoftClassPath{
        TEXT("/Script/Angelscript.CkTests_Gym_Base_GameMode")}.TryLoadClass<AGameModeBase>();
    if (NOT IsValid(BaseGymMode))
    { AddError(TEXT("authored CkTests gym GameMode class is unavailable")); return false; }

    const auto Demo = MakeShared<FSharedDemo>();
    Demo->StateClass = FSoftClassPath{AggroStatePath}.TryLoadClass<AGameStateBase>();
    Demo->ControllerClass = FSoftClassPath{AggroControllerPath}.TryLoadClass<APlayerController>();
    if (!TestNotNull(TEXT("Aggro GameState class"), Demo->StateClass) ||
        !TestNotNull(TEXT("Aggro PlayerController class"), Demo->ControllerClass))
    { return false; }
    Demo->Snapshot = FindFProperty<FStructProperty>(Demo->StateClass, TEXT("Snapshot"));
    if (!TestNotNull(TEXT("replicated Aggro Snapshot struct"), Demo->Snapshot)) { return false; }
    auto* Schema = Demo->Snapshot->Struct.Get();
    Demo->Generation = FindFProperty<FIntProperty>(Schema, TEXT("Generation"));
    Demo->Sequence = FindFProperty<FIntProperty>(Schema, TEXT("Sequence"));
    Demo->Ready = FindFProperty<FBoolProperty>(Schema, TEXT("Ready"));
    Demo->Failed = FindFProperty<FBoolProperty>(Schema, TEXT("Failed"));
    Demo->ChaseDummies = FindFProperty<FArrayProperty>(Schema, TEXT("ChaseDummies"));
    Demo->ChaseThreats = FindFProperty<FArrayProperty>(Schema, TEXT("ChaseThreats"));
    Demo->ChaseGuard = FindFProperty<FStructProperty>(Schema, TEXT("ChaseGuard"));
    Demo->ChaseActiveIndex = FindFProperty<FIntProperty>(Schema, TEXT("ChaseActiveIndex"));
    Demo->ChaseTrackedCount = FindFProperty<FIntProperty>(Schema, TEXT("ChaseTrackedCount"));
    Demo->PerceptionGuard = FindFProperty<FStructProperty>(Schema, TEXT("PerceptionGuard"));
    Demo->PerceptionOrbiter = FindFProperty<FStructProperty>(Schema, TEXT("PerceptionOrbiter"));
    Demo->PerceptionInCone = FindFProperty<FBoolProperty>(Schema, TEXT("PerceptionInCone"));
    Demo->PerceptionThreat = FindFProperty<FNumericProperty>(Schema, TEXT("PerceptionThreat"));
    Demo->StressGuards = FindFProperty<FArrayProperty>(Schema, TEXT("StressGuards"));
    Demo->StressActiveMask = FindFProperty<FIntProperty>(Schema, TEXT("StressActiveMask"));
    Demo->StressActiveCount = FindFProperty<FIntProperty>(Schema, TEXT("StressActiveCount"));
    Demo->StressGuardCount = FindFProperty<FIntProperty>(Schema, TEXT("StressGuardCount"));
    Demo->StressTargetsPerGuard = FindFProperty<FIntProperty>(Schema, TEXT("StressTargetsPerGuard"));
    Demo->ServerTrioCount = FindFProperty<FIntProperty>(Demo->StateClass, TEXT("ServerTrioCount"));
    Demo->ServerChase = FindFProperty<FStructProperty>(Demo->StateClass, TEXT("ServerChase"));
    Demo->ServerPerception = FindFProperty<FStructProperty>(Demo->StateClass, TEXT("ServerPerception"));
    Demo->ServerStress = FindFProperty<FStructProperty>(Demo->StateClass, TEXT("ServerStress"));
    Demo->LastResetSerial = FindFProperty<FIntProperty>(Demo->ControllerClass, TEXT("LastResetSerial"));
    Demo->LastResetResult = FindFProperty<FIntProperty>(Demo->ControllerClass, TEXT("LastResetResult"));
    Demo->ResetRpc = Demo->ControllerClass->FindFunctionByName(TEXT("Server_RequestAggroReset"));
    auto bSchema = true;
    bSchema &= TestNotNull(TEXT("Snapshot.Generation"), Demo->Generation);
    bSchema &= TestNotNull(TEXT("Snapshot.Sequence"), Demo->Sequence);
    bSchema &= TestNotNull(TEXT("Snapshot.Ready"), Demo->Ready);
    bSchema &= TestNotNull(TEXT("Snapshot.Failed"), Demo->Failed);
    bSchema &= TestNotNull(TEXT("Snapshot.ChaseDummies"), Demo->ChaseDummies);
    bSchema &= TestNotNull(TEXT("Snapshot.ChaseThreats"), Demo->ChaseThreats);
    bSchema &= TestNotNull(TEXT("Snapshot.ChaseGuard"), Demo->ChaseGuard);
    bSchema &= TestNotNull(TEXT("Snapshot.ChaseActiveIndex"), Demo->ChaseActiveIndex);
    bSchema &= TestNotNull(TEXT("Snapshot.ChaseTrackedCount"), Demo->ChaseTrackedCount);
    bSchema &= TestNotNull(TEXT("Snapshot.PerceptionGuard"), Demo->PerceptionGuard);
    bSchema &= TestNotNull(TEXT("Snapshot.PerceptionOrbiter"), Demo->PerceptionOrbiter);
    bSchema &= TestNotNull(TEXT("Snapshot.PerceptionInCone"), Demo->PerceptionInCone);
    bSchema &= TestTrue(TEXT("Snapshot.PerceptionThreat is floating point"),
        Demo->PerceptionThreat && Demo->PerceptionThreat->IsFloatingPoint());
    bSchema &= TestNotNull(TEXT("Snapshot.StressGuards"), Demo->StressGuards);
    bSchema &= TestNotNull(TEXT("Snapshot.StressActiveMask"), Demo->StressActiveMask);
    bSchema &= TestNotNull(TEXT("Snapshot.StressActiveCount"), Demo->StressActiveCount);
    bSchema &= TestNotNull(TEXT("Snapshot.StressGuardCount"), Demo->StressGuardCount);
    bSchema &= TestNotNull(TEXT("Snapshot.StressTargetsPerGuard"), Demo->StressTargetsPerGuard);
    bSchema &= TestNotNull(TEXT("GameState.ServerTrioCount"), Demo->ServerTrioCount);
    bSchema &= TestNotNull(TEXT("GameState.ServerChase"), Demo->ServerChase);
    bSchema &= TestNotNull(TEXT("GameState.ServerPerception"), Demo->ServerPerception);
    bSchema &= TestNotNull(TEXT("GameState.ServerStress"), Demo->ServerStress);
    bSchema &= TestNotNull(TEXT("PlayerController.LastResetSerial"), Demo->LastResetSerial);
    bSchema &= TestNotNull(TEXT("PlayerController.LastResetResult"), Demo->LastResetResult);
    bSchema &= TestNotNull(TEXT("PlayerController.Server_RequestAggroReset"), Demo->ResetRpc);
    if (!bSchema) { return false; }
    const auto* RpcSerial = FindFProperty<FIntProperty>(Demo->ResetRpc, TEXT("InSerial"));
    if (!TestTrue(TEXT("reset RPC has exactly one int32 input and no return"),
        RpcSerial && RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
        Demo->ResetRpc->NumParms == 1 &&
        Demo->ResetRpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer)))
    { return false; }
    if (!TestTrue(TEXT("Snapshot is replicated with typed vector and float samples"),
        Demo->Snapshot->HasAnyPropertyFlags(CPF_Net) &&
        Demo->ChaseGuard->Struct == TBaseStructure<FVector>::Get() &&
        Demo->PerceptionGuard->Struct == TBaseStructure<FVector>::Get() &&
        Demo->PerceptionOrbiter->Struct == TBaseStructure<FVector>::Get() &&
        CastField<FStructProperty>(Demo->ChaseDummies->Inner) &&
        CastField<FStructProperty>(Demo->ChaseDummies->Inner)->Struct == TBaseStructure<FVector>::Get() &&
        CastField<FNumericProperty>(Demo->ChaseThreats->Inner) &&
        CastField<FNumericProperty>(Demo->ChaseThreats->Inner)->IsFloatingPoint() &&
        CastField<FStructProperty>(Demo->StressGuards->Inner) &&
        CastField<FStructProperty>(Demo->StressGuards->Inner)->Struct == TBaseStructure<FVector>::Get()))
    { return false; }
    if (!TestTrue(TEXT("server trio fields carry actual handles and client results replicate"),
        Demo->ServerChase->Struct == FCk_Handle::StaticStruct() &&
        Demo->ServerPerception->Struct == FCk_Handle::StaticStruct() &&
        Demo->ServerStress->Struct == FCk_Handle::StaticStruct() &&
        Demo->LastResetSerial->HasAnyPropertyFlags(CPF_Net) &&
        Demo->LastResetResult->HasAnyPropertyFlags(CPF_Net)))
    { return false; }

    const auto SettingsBefore = FSettingsBefore{};
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters, BaseGymMode));
    // Allow the full plugin bootstrap to reach BeginPlay in both client worlds.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool { return MenusAreReady(); }),
        30.0, TEXT("two owning client menus receive the complete authoritative catalog")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, DedicatedServer]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            if (NOT IsValid(Server) || NOT IsValid(Client0) || NOT IsValid(Client1))
            { AddError(TEXT("gym worlds disappeared before catalog assertion")); return false; }
            TestEqual(TEXT("exact gym world count"), ck::auto_test::net::Get_AllPIEWorlds().Num(), 3);
            TestEqual(TEXT("gym server topology"), Server->GetNetMode(),
                DedicatedServer ? NM_DedicatedServer : NM_ListenServer);
            TestTrue(TEXT("server and two owning client menus match"), MenusAreReady());
            auto* PC0 = Cast<ACkGym_PlayerController_UE>(Client0->GetFirstPlayerController());
            auto* PC1 = Cast<ACkGym_PlayerController_UE>(Client1->GetFirstPlayerController());
            auto* Player0 = Client0->GetGameInstance()->GetFirstGamePlayer();
            auto* Player1 = Client1->GetGameInstance()->GetFirstGamePlayer();
            if (NOT IsValid(PC0) || NOT IsValid(PC1) || NOT IsValid(Player0) || NOT IsValid(Player1))
            { AddError(TEXT("gym local controller or player missing")); return false; }
            TestTrue(TEXT("client 0 controller belongs to its explicit local player"),
                Player0->GetPlayerController(Client0) == PC0);
            TestTrue(TEXT("client 1 controller belongs to its explicit local player"),
                Player1->GetPlayerController(Client1) == PC1);
            auto* ServerRegistry = GetRegistry(Server);
            auto* ClientRegistry = GetRegistry(Client0);
            if (NOT IsValid(ServerRegistry) || NOT IsValid(ClientRegistry) ||
                ServerRegistry->Get_Entries().IsEmpty())
            { AddError(TEXT("gym registries disappeared before malformed replica checks")); return false; }
            auto Duplicate = ServerRegistry->Get_Entries();
            const auto DuplicateEntry = Duplicate[0];
            Duplicate.Add(DuplicateEntry);
            TestFalse(TEXT("duplicate replica rejects the whole candidate"),
                ClientRegistry->TryReplaceReplica(Client0, Duplicate, INDEX_NONE, false));
            TestFalse(TEXT("foreign server world cannot replace a client registry"),
                ClientRegistry->TryReplaceReplica(Server, ServerRegistry->Get_Entries(), INDEX_NONE, false));
            TestFalse(TEXT("another client's world cannot replace the owning client's registry"),
                ClientRegistry->TryReplaceReplica(Client1, ServerRegistry->Get_Entries(), INDEX_NONE, false));
            TestTrue(TEXT("rejected replicas preserve the complete prior menu"),
                HasSameCatalog(ServerRegistry, ClientRegistry));
            auto* State0 = Client0->GetGameState<ACkGym_GameState_UE>();
            auto* State1 = Client1->GetGameState<ACkGym_GameState_UE>();
            if (NOT IsValid(State0) || NOT IsValid(State1))
            { AddError(TEXT("client GameStates disappeared before ticket ownership checks")); return false; }
            auto TicketRequestId = 0;
            TestFalse(TEXT("foreign local player cannot create a ticket in this registry"),
                ClientRegistry->TryBeginLocalTravel(Player1, TEXT("Aggro"), TicketRequestId));
            if (NOT TestTrue(TEXT("local ticket begins for its owning player"),
                ClientRegistry->TryBeginLocalTravel(Player0, TEXT("Aggro"), TicketRequestId)))
            { return false; }
            TestFalse(TEXT("malformed travel result is rejected without terminating the ticket"),
                ClientRegistry->TryApplyLocalTravelResult(Player0, TicketRequestId,
                    static_cast<ECkGym_TravelResult>(255), TEXT("invalid result probe")));
            TestEqual(TEXT("malformed travel result leaves the owning ticket queued"),
                ClientRegistry->GetLocalTravelResult(Player0), ECkGym_TravelResult::Queued);
            ClientRegistry->ResolveLocalTravelOnCatalog(Client0, State1,
                ServerRegistry->Get_Entries(), ServerRegistry->Get_CurrentGymIndex());
            TestEqual(TEXT("foreign GameState cannot terminate the owning player's ticket"),
                ClientRegistry->GetLocalTravelResult(Player0), ECkGym_TravelResult::Queued);
            ClientRegistry->ResolveLocalTravelOnCatalog(Client1, State1,
                ServerRegistry->Get_Entries(), ServerRegistry->Get_CurrentGymIndex());
            TestEqual(TEXT("foreign world cannot terminate the owning player's ticket"),
                ClientRegistry->GetLocalTravelResult(Player0), ECkGym_TravelResult::Queued);
            ClientRegistry->ResolveLocalTravelOnCatalog(Client0, State0,
                Duplicate, ServerRegistry->Get_CurrentGymIndex());
            TestEqual(TEXT("malformed catalog cannot terminate the owning player's ticket"),
                ClientRegistry->GetLocalTravelResult(Player0), ECkGym_TravelResult::Queued);
            TestTrue(TEXT("rejected catalogs preserve the complete owning menu"),
                HasSameCatalog(ServerRegistry, ClientRegistry));
            TestTrue(TEXT("ticket test releases its local request"),
                ClientRegistry->TryApplyLocalTravelResult(Player0, TicketRequestId,
                    ECkGym_TravelResult::Rejected, TEXT("ticket ownership probe complete")));
            TestFalse(TEXT("arbitrary context never selects a primary player"),
                UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(
                    GetTransientPackage(), TEXT("Aggro")));
            TestFalse(TEXT("default gym name rejected locally"),
                UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(PC0, FString{}));
            TestFalse(TEXT("unregistered gym name rejected locally"),
                UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(PC1, TEXT("NoSuchGym")));
            TestTrue(TEXT("unregistered direct RPC queues for server validation"),
                PC1->TryQueueTravelRequest(TEXT("NoSuchGym")));
            return true;
        }), TEXT("exact client ownership and invalid travel rejection")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(1);
            auto* PC = IsValid(Client) ? Cast<ACkGym_PlayerController_UE>(Client->GetFirstPlayerController()) : nullptr;
            return IsValid(PC) && PC->Get_LastTravelResult() == ECkGym_TravelResult::Rejected;
        }), 10.0, TEXT("owning client receives authoritative invalid-name rejection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SettingsBefore, BaseGymMode]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Registry = GetRegistry(Server);
            auto* LandedGameMode = IsValid(Server) ? Server->GetAuthGameMode() : nullptr;
            if (NOT IsValid(Server) || NOT IsValid(Registry) || NOT IsValid(LandedGameMode))
            { AddError(TEXT("gym server or GameMode disappeared after invalid RPC")); return false; }
            TestEqual(TEXT("invalid RPC did not change current gym"),
                Registry->Get_CurrentGymIndex(), INDEX_NONE);
            TestTrue(TEXT("invalid RPC did not travel"), LandedGameMode->GetClass() == BaseGymMode);
            TestTrue(TEXT("invalid RPC did not change recents"),
                GetDefault<UCkGym_StartupSettings>()->RecentGymNames == SettingsBefore.Recents);
            TestEqual(TEXT("invalid RPC did not change last gym"),
                GetDefault<UCkGym_StartupSettings>()->LastGymName, SettingsBefore.Last);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            auto* PC1 = IsValid(Client1) ? Cast<ACkGym_PlayerController_UE>(Client1->GetFirstPlayerController()) : nullptr;
            if (NOT IsValid(PC1))
            { AddError(TEXT("second owning controller disappeared before travel rejection")); return false; }
            TestEqual(TEXT("owning client observes authoritative rejection"),
                PC1->Get_LastTravelResult(), ECkGym_TravelResult::Rejected);

            auto* Probe = NewObject<UCkGym_Registry_Subsystem>(Server->GetGameInstance());
            auto MissingMap = FCkGym_Entry{};
            MissingMap.DisplayName = TEXT("MissingMapProbe");
            MissingMap.GameModeClass = BaseGymMode;
            MissingMap.LevelName = TEXT("/Game/CkGym_Map_That_Does_Not_Exist");
            Probe->Register(MissingMap);
            TestFalse(TEXT("missing registered map rejects before ServerTravel"),
                Probe->TryRequest_TravelToGym(Server, 0));
            TestEqual(TEXT("missing map leaves probe current gym unset"),
                Probe->Get_CurrentGymIndex(), INDEX_NONE);
            return true;
        }), TEXT("invalid client RPC leaves travel and saved gym state unchanged")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Client = ck::auto_test::net::Get_ClientWorld(1);
            auto* PC = IsValid(Client) ? Cast<ACkGym_PlayerController_UE>(Client->GetFirstPlayerController()) : nullptr;
            if (NOT IsValid(PC))
            { AddError(TEXT("second owning client is unavailable for registered travel")); return false; }
            TestTrue(TEXT("registered name is admitted from second owning client"),
                UCk_Utils_GymRegistry_UE::TryRequest_TravelToGymFromPlayer(PC, TEXT("Aggro")));
            return true;
        }), TEXT("registered client request reaches authoritative travel path")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Registry = GetRegistry(Server);
            const auto Aggro = IsValid(Registry) ? Registry->Find_GymIndexByName(TEXT("Aggro")) : INDEX_NONE;
            return IsValid(Server) && IsValid(Registry) && Aggro != INDEX_NONE &&
                Registry->Get_CurrentGymIndex() == Aggro && MenusAreReady();
        }), 60.0, TEXT("server lands in registered Aggro gym and both client menus reconnect")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return AllAggroStationsReady(false); }),
        25.0, TEXT("all three authored Aggro station tags settle uniquely in each PIE world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return AllAggroStationsReady(true); }),
        15.0, TEXT("each Aggro station receives exactly one authored startup script")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            UWorld* Worlds[] = {
                ck::auto_test::net::Get_ServerWorld(),
                ck::auto_test::net::Get_ClientWorld(0),
                ck::auto_test::net::Get_ClientWorld(1),
            };
            const TCHAR* WorldNames[] = {TEXT("server"), TEXT("client 0"), TEXT("client 1")};
            for (auto Index = 0; Index < 3; ++Index)
            {
                for (const auto& Station : AggroStations)
                {
                    TestEqual(FString::Printf(TEXT("%s has one %s station"),
                        WorldNames[Index], *Station.Tag.ToString()),
                        CountStations(Worlds[Index], Station.Tag), 1);
                    TestEqual(FString::Printf(TEXT("%s has one %s authored startup script"),
                        WorldNames[Index], *Station.Tag.ToString()),
                        CountAuthoredStationScripts(Worlds[Index], Station), 1);
                }
            }
            return true;
        }), TEXT("authored Aggro startup is unique on server and both clients")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Client = ck::auto_test::net::Get_ClientWorld(1);
            auto* PC = IsValid(Client) ? Cast<ACkGym_PlayerController_UE>(Client->GetFirstPlayerController()) : nullptr;
            if (NOT IsValid(PC))
            { AddError(TEXT("owning controller missing after gym travel")); return false; }
            TestEqual(TEXT("new owning controller sees durable landed result"),
                PC->Get_LastTravelResult(), ECkGym_TravelResult::Landed);
            TestEqual(TEXT("landed ticket identifies requested gym"),
                PC->Get_LastTravelDetail(), FString{TEXT("Aggro")});
            TestTrue(TEXT("new controller can queue another request after landing"),
                PC->TryQueueTravelRequest(TEXT("NoSuchGym")));
            return true;
        }), TEXT("durable owning-player completion survives controller replacement")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(1);
            auto* PC = IsValid(Client) ? Cast<ACkGym_PlayerController_UE>(Client->GetFirstPlayerController()) : nullptr;
            return IsValid(PC) && PC->Get_LastTravelResult() == ECkGym_TravelResult::Rejected;
        }), 10.0, TEXT("second request reaches server and rejects without travel")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Demo]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            const auto Generation = SnapshotGeneration(GetAggroState(Server, *Demo), *Demo);
            return Generation > 0 && AllSnapshotsReady(*Demo, Generation) &&
                CountDemo(Server, true) &&
                CountDemo(ck::auto_test::net::Get_ClientWorld(0), false) &&
                CountDemo(ck::auto_test::net::Get_ClientWorld(1), false);
        }), 45.0, TEXT("one shared Aggro trio and complete snapshot reach both clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Demo]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetAggroState(Server, *Demo);
            auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            if (!TestTrue(TEXT("three valid Aggro worlds and a shared server state"),
                IsValid(State) && IsValid(Client0) && IsValid(Client1) &&
                ck::auto_test::net::Get_AllPIEWorlds().Num() == 3)) { return false; }
            Demo->InitialGeneration = SnapshotGeneration(State, *Demo);
            Demo->SurvivorState = State;
            Demo->SurvivorClientInstance = Client1->GetGameInstance();
            TestEqual(TEXT("server creates exactly one shared station trio"),
                Demo->ServerTrioCount->GetPropertyValue_InContainer(State), 3);
            TestTrue(TEXT("server has 26 Aggro guards and 124 target fragments under station children"),
                CountDemo(Server, true, &Demo->OldChildren));
            TestEqual(TEXT("captured exactly three original station child handles"),
                Demo->OldChildren.Num(), 3);
            TestTrue(TEXT("GameState references the three original server children"),
                Demo->OldChildren.Num() == 3 &&
                GetServerChild(State, Demo->ServerChase) == Demo->OldChildren[0] &&
                GetServerChild(State, Demo->ServerPerception) == Demo->OldChildren[1] &&
                GetServerChild(State, Demo->ServerStress) == Demo->OldChildren[2]);
            TestTrue(TEXT("client 0 has only presentation entities"), CountDemo(Client0, false));
            TestTrue(TEXT("client 1 has only presentation entities"), CountDemo(Client1, false));
            TestTrue(TEXT("all worlds have one complete generation"),
                AllSnapshotsReady(*Demo, Demo->InitialGeneration));
            return true;
        }), TEXT("shared Aggro ownership and two client projections")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Demo](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning client submits invalid Aggro reset serial"),
                InvokeReset(Controller, *Demo, 0));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Demo]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* Controller = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            return IsValid(Controller) && Controller->IsA(Demo->ControllerClass) &&
                Demo->LastResetSerial->GetPropertyValue_InContainer(Controller) == 0 &&
                Demo->LastResetResult->GetPropertyValue_InContainer(Controller) == 4 &&
                AllSnapshotsReady(*Demo, Demo->InitialGeneration) &&
                CountDemo(ck::auto_test::net::Get_ServerWorld(), true);
        }), 15.0, TEXT("invalid reset is rejected without generation or server composition change")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Demo](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("first remote owner submits serialled Aggro reset"),
                InvokeReset(Controller, *Demo, Demo->ResetSerial));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Demo]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* Controller = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            return IsValid(Controller) && Controller->IsA(Demo->ControllerClass) &&
                Demo->LastResetSerial->GetPropertyValue_InContainer(Controller) == Demo->ResetSerial &&
                Demo->LastResetResult->GetPropertyValue_InContainer(Controller) == 1 &&
                AllSnapshotsReady(*Demo, Demo->InitialGeneration + 1) &&
                Demo->OldChildren.Num() == 3 &&
                ck::Is_NOT_Valid(Demo->OldChildren[0]) &&
                ck::Is_NOT_Valid(Demo->OldChildren[1]) &&
                ck::Is_NOT_Valid(Demo->OldChildren[2]) &&
                CountDemo(Server, true) && CountDemo(Client, false) &&
                CountDemo(ck::auto_test::net::Get_ClientWorld(1), false);
        }), 45.0, TEXT("accepted owning-client reset replaces one trio and converges on both clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Demo]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetAggroState(Server, *Demo);
            if (!TestTrue(TEXT("original Aggro GameState survives reset"),
                IsValid(State) && State == Demo->SurvivorState.Get())) { return false; }
            TestEqual(TEXT("reset leaves one trio"),
                Demo->ServerTrioCount->GetPropertyValue_InContainer(State), 3);
            TestTrue(TEXT("all original station children are retired"),
                Demo->OldChildren.Num() == 3 &&
                ck::Is_NOT_Valid(Demo->OldChildren[0]) &&
                ck::Is_NOT_Valid(Demo->OldChildren[1]) &&
                ck::Is_NOT_Valid(Demo->OldChildren[2]));
            TArray<FCk_Handle> FreshChildren;
            TestTrue(TEXT("reset has the exact live 26/124 station composition"),
                CountDemo(Server, true, &FreshChildren));
            TestTrue(TEXT("each station has one fresh original child"),
                FreshChildren.Num() == 3 && Demo->OldChildren.Num() == 3 &&
                FreshChildren[0] != Demo->OldChildren[0] &&
                FreshChildren[1] != Demo->OldChildren[1] &&
                FreshChildren[2] != Demo->OldChildren[2]);
            TestTrue(TEXT("GameState references only replacement children"),
                FreshChildren.Num() == 3 &&
                GetServerChild(State, Demo->ServerChase) == FreshChildren[0] &&
                GetServerChild(State, Demo->ServerPerception) == FreshChildren[1] &&
                GetServerChild(State, Demo->ServerStress) == FreshChildren[2]);
            return true;
        }), TEXT("reset retires old station children without duplicate server state")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Demo](UWorld* InClient)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Driver = IsValid(Server) ? Server->GetNetDriver() : nullptr;
            auto* RemoteServerPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* LocalPC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            auto* Instance = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
            if (!TestTrue(TEXT("departing client has a live remote server owner"),
                IsValid(Driver) && IsValid(RemoteServerPC) && IsValid(LocalPC) && IsValid(Instance)))
            { return; }
            Demo->DepartingServerController = RemoteServerPC;
            Demo->DepartingClientInstance = Instance;
            Demo->DepartureSequence = SnapshotSequence(Demo->SurvivorState.Get(), *Demo);
            LocalPC->ClientTravel(TEXT("/Engine/Maps/Entry"), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Demo]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetAggroState(Server, *Demo);
            auto* SurvivorInstance = Demo->SurvivorClientInstance.Get();
            auto* SurvivorWorld = IsValid(SurvivorInstance) ? SurvivorInstance->GetWorld() : nullptr;
            auto* DepartingInstance = Demo->DepartingClientInstance.Get();
            auto* DepartedWorld = IsValid(DepartingInstance) ? DepartingInstance->GetWorld() : nullptr;
            return IsValid(State) && State == Demo->SurvivorState.Get() &&
                !Demo->DepartingServerController.IsValid() &&
                IsValid(DepartedWorld) && DepartedWorld != Server &&
                DepartedWorld->GetMapName().Contains(TEXT("Entry")) &&
                IsValid(SurvivorWorld) && SurvivorWorld != DepartedWorld &&
                SnapshotReady(GetAggroState(SurvivorWorld, *Demo), *Demo,
                    Demo->InitialGeneration + 1) &&
                SnapshotSequence(State, *Demo) > Demo->DepartureSequence &&
                SnapshotSequence(GetAggroState(SurvivorWorld, *Demo), *Demo) >
                    Demo->DepartureSequence &&
                CountDemo(Server, true) && CountDemo(SurvivorWorld, false);
        }), 45.0, TEXT("real client departure to Entry leaves shared server demo and survivor running")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Demo]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetAggroState(Server, *Demo);
            auto* Instance = Demo->SurvivorClientInstance.Get();
            auto* Survivor = IsValid(Instance) ? Instance->GetWorld() : nullptr;
            return TestTrue(TEXT("same GameState and one trio survive requesting player loss"),
                IsValid(State) && State == Demo->SurvivorState.Get() &&
                Demo->ServerTrioCount->GetPropertyValue_InContainer(State) == 3 &&
                SnapshotReady(State, *Demo, Demo->InitialGeneration + 1) &&
                SnapshotReady(GetAggroState(Survivor, *Demo), *Demo,
                    Demo->InitialGeneration + 1) &&
                SnapshotSequence(GetAggroState(Survivor, *Demo), *Demo) >
                    Demo->DepartureSequence &&
                CountDemo(Server, true) && CountDemo(Survivor, false));
        }), TEXT("Aggro shared lifetime is independent of departed controller")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Demo](UWorld* InServer)
        {
            auto* State = GetAggroState(InServer, *Demo);
            if (!TestTrue(TEXT("failure probe starts from the surviving ready server demo"),
                IsValid(State) && State == Demo->SurvivorState.Get() &&
                SnapshotReady(State, *Demo, Demo->InitialGeneration + 1) &&
                CountDemo(InServer, true, &Demo->FailedGenerationChildren)))
            { return; }
            if (!TestTrue(TEXT("failure probe has exactly three mapped live station children"),
                Demo->FailedGenerationChildren.Num() == 3 &&
                GetServerChild(State, Demo->ServerChase) == Demo->FailedGenerationChildren[0] &&
                GetServerChild(State, Demo->ServerPerception) == Demo->FailedGenerationChildren[1] &&
                GetServerChild(State, Demo->ServerStress) == Demo->FailedGenerationChildren[2]))
            { return; }
            auto LostChild = Demo->FailedGenerationChildren[0];
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(LostChild);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Demo]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetAggroState(Server, *Demo);
            auto* Instance = Demo->SurvivorClientInstance.Get();
            auto* Survivor = IsValid(Instance) ? Instance->GetWorld() : nullptr;
            auto* SurvivorState = GetAggroState(Survivor, *Demo);
            if (!SnapshotFailed(State, *Demo, Demo->InitialGeneration + 1) ||
                !SnapshotFailed(SurvivorState, *Demo, Demo->InitialGeneration + 1) ||
                Demo->ServerTrioCount->GetPropertyValue_InContainer(State) != 0 ||
                !NoLiveAggroDemo(Server) || Demo->FailedGenerationChildren.Num() != 3)
            { return false; }
            for (const auto& Child : Demo->FailedGenerationChildren)
            { if (ck::IsValid(Child)) { return false; } }
            return ck::Is_NOT_Valid(GetServerChild(State, Demo->ServerChase)) &&
                ck::Is_NOT_Valid(GetServerChild(State, Demo->ServerPerception)) &&
                ck::Is_NOT_Valid(GetServerChild(State, Demo->ServerStress));
        }), 20.0, TEXT("real child loss fails shared snapshot and retires all server Aggro branches")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Demo]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* State = GetAggroState(ck::auto_test::net::Get_ServerWorld(), *Demo);
            if (!TestTrue(TEXT("failed shared state stays on the existing generation"),
                IsValid(State) && State == Demo->SurvivorState.Get() &&
                SnapshotFailed(State, *Demo, Demo->InitialGeneration + 1)))
            { return false; }
            Demo->FailureSequence = SnapshotSequence(State, *Demo);
            return true;
        }), TEXT("child-loss failure publication is captured before progress watchdog")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Demo]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetAggroState(Server, *Demo);
            auto* Instance = Demo->SurvivorClientInstance.Get();
            auto* Survivor = IsValid(Instance) ? Instance->GetWorld() : nullptr;
            return TestTrue(TEXT("failed child cannot leave a running sibling or advance success samples"),
                IsValid(State) && State == Demo->SurvivorState.Get() &&
                SnapshotFailed(State, *Demo, Demo->InitialGeneration + 1) &&
                SnapshotFailed(GetAggroState(Survivor, *Demo), *Demo,
                    Demo->InitialGeneration + 1) &&
                SnapshotSequence(State, *Demo) == Demo->FailureSequence &&
                Demo->ServerTrioCount->GetPropertyValue_InContainer(State) == 0 &&
                NoLiveAggroDemo(Server));
        }), TEXT("child-loss failure remains atomic after a finite progress watchdog")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SettingsBefore]() -> bool
        {
            SettingsBefore.Restore();
            TestTrue(TEXT("gym PIE worlds are gone after cleanup"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
            return true;
        }), TEXT("gym network test cleanup and user settings restoration"), true));
    return true;
}

#endif
