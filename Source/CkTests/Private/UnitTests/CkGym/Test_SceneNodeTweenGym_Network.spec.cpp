#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkActorRelay/CkActorRelay_Actor.h"
#include "CkGym_NetworkRuntime.h"
#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkTween/CkTween_Utils.h"

#include <HAL/PlatformTime.h>
#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <EngineUtils.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/UnrealType.h>
#include <UObject/StructOnScope.h>

namespace ck_scene_node_tween_gym_network_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_SceneNodeTweenGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_SceneNodeTweenGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_SceneNodeTweenGym_PlayerController");

    auto RelayOwnersBelongToWorld(UWorld* InWorld, bool InRequireChannels = false) -> bool
    {
        if (!IsValid(InWorld)) { return false; }
        auto Count = 0;
        for (TActorIterator<ACk_ActorRelay_UE> It(InWorld); It; ++It)
        {
            if (!IsValid(*It)) { continue; }
            ++Count;
            auto* Owner = It->GetOwner();
            if (Owner && (!IsValid(Owner) || Owner->GetWorld() != InWorld)) { return false; }
        }
        return !InRequireChannels || Count > 0;
    }

    struct FStation
    {
        FName Tag;
        const TCHAR* ScriptPath;
        int32 NodeCount;
    };

    const FStation Stations[] = {
        {FName(TEXT("Gym.SceneNodeTween.Simple")),
            TEXT("/Script/Angelscript.Ck_EntityScript_SceneNodeTweenGym_SimpleStation"), 2},
        {FName(TEXT("Gym.SceneNodeTween.Chain")),
            TEXT("/Script/Angelscript.Ck_EntityScript_SceneNodeTweenGym_ChainStation"), 3},
        {FName(TEXT("Gym.SceneNodeTween.Deep")),
            TEXT("/Script/Angelscript.Ck_EntityScript_SceneNodeTweenGym_DeepStation"), 6},
    };

    struct FObservedSnapshot
    {
        int32 Generation = 0;
        int32 Sequence = 0;
        bool AutoRunning = false;
        TSharedPtr<FStructOnScope> Copy;
    };

    struct FFixture
    {
        UClass* Mode = nullptr;
        UClass* State = nullptr;
        UClass* Controller = nullptr;
        FStructProperty* Snapshot = nullptr;
        FIntProperty* Generation = nullptr;
        FIntProperty* Sequence = nullptr;
        FBoolProperty* Ready = nullptr;
        FBoolProperty* Failed = nullptr;
        FStrProperty* FailureReason = nullptr;
        FBoolProperty* AutoRunning = nullptr;
        FArrayProperty* Samples = nullptr;
        FStructProperty* SampleItem = nullptr;
        FStructProperty* RootPosition = nullptr;
        FArrayProperty* ExpectedLinks = nullptr;
        FArrayProperty* ActualLinks = nullptr;
        FNumericProperty* Drift = nullptr;
        FIntProperty* ServerTrioCount = nullptr;
        FStructProperty* ServerHandles[3]{};
        FIntProperty* LocalProjectionGeneration = nullptr;
        FBoolProperty* LocalProjectionReady = nullptr;
        FBoolProperty* LocalProjectionFailed = nullptr;
        FStrProperty* LocalProjectionFailureReason = nullptr;
        FIntProperty* LastControlSerial = nullptr;
        FIntProperty* LastControlAttemptSerial = nullptr;
        FIntProperty* LastControlResult = nullptr;
        UFunction* ControlRpc = nullptr;
        FIntProperty* RpcSerial = nullptr;
        FIntProperty* RpcAction = nullptr;
        FBoolProperty* RpcAutoRunning = nullptr;
        double NextReadinessDiagnosticSeconds = 0.0;
        int32 InitialGeneration = 0;
        int32 DepartureSequence = 0;
        int32 FailureSequence = 0;
        FVector MovingRoot = FVector::ZeroVector;
        FVector PausedRoot = FVector::ZeroVector;
        TArray<FCk_Handle> OldScripts;
        TArray<FCk_Handle> OldSimulation;
        TArray<FCk_Handle> FailedScripts;
        TArray<FCk_Handle> FailedSimulation;
        int32 HistoryGeneration = INDEX_NONE;
        bool HistoryAutoRunning = false;
        TArray<FObservedSnapshot> ObservedSnapshots;
        TWeakObjectPtr<AGameStateBase> SurvivorState;
        TWeakObjectPtr<UGameInstance> SurvivorInstance;
        TWeakObjectPtr<UGameInstance> DepartingInstance;
        TWeakObjectPtr<APlayerController> DepartingServerController;
        ECkGym_StartupMode OriginalMode = ECkGym_StartupMode::Cycler;
        FString OriginalLast;
        TArray<FString> OriginalRecents;

        void CaptureSettings()
        {
            const auto* Settings = GetDefault<UCkGym_StartupSettings>();
            OriginalMode = Settings->StartupMode;
            OriginalLast = Settings->LastGymName;
            OriginalRecents = Settings->RecentGymNames;
        }

        void RestoreSettings() const
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = OriginalMode;
            Settings->LastGymName = OriginalLast;
            Settings->RecentGymNames = OriginalRecents;
            Settings->SaveConfig();
        }
    };

    auto GetState(UWorld* InWorld, const FFixture& InFixture) -> AGameStateBase*
    {
        auto* State = IsValid(InWorld) ? InWorld->GetGameState() : nullptr;
        return IsValid(State) && State->IsA(InFixture.State) ? State : nullptr;
    }

    auto GetSnapshot(AGameStateBase* InState, const FFixture& InFixture) -> const void*
    {
        return IsValid(InState) ? InFixture.Snapshot->ContainerPtrToValuePtr<void>(InState) : nullptr;
    }

    auto GetGeneration(AGameStateBase* InState, const FFixture& InFixture) -> int32
    {
        const auto* Snapshot = GetSnapshot(InState, InFixture);
        return Snapshot ? InFixture.Generation->GetPropertyValue_InContainer(Snapshot) : INDEX_NONE;
    }

    auto GetSequence(AGameStateBase* InState, const FFixture& InFixture) -> int32
    {
        const auto* Snapshot = GetSnapshot(InState, InFixture);
        return Snapshot ? InFixture.Sequence->GetPropertyValue_InContainer(Snapshot) : INDEX_NONE;
    }

    auto IsReady(AGameStateBase* InState, const FFixture& InFixture, int32 InGeneration,
                 bool InAutoRunning) -> bool
    {
        const auto* Snapshot = GetSnapshot(InState, InFixture);
        return Snapshot &&
            InFixture.Generation->GetPropertyValue_InContainer(Snapshot) == InGeneration &&
            InFixture.Sequence->GetPropertyValue_InContainer(Snapshot) > 0 &&
            InFixture.Ready->GetPropertyValue_InContainer(Snapshot) &&
            !InFixture.Failed->GetPropertyValue_InContainer(Snapshot) &&
            InFixture.AutoRunning->GetPropertyValue_InContainer(Snapshot) == InAutoRunning;
    }

    auto IsFailed(AGameStateBase* InState, const FFixture& InFixture, int32 InGeneration) -> bool
    {
        const auto* Snapshot = GetSnapshot(InState, InFixture);
        return Snapshot &&
            InFixture.Generation->GetPropertyValue_InContainer(Snapshot) == InGeneration &&
            !InFixture.Ready->GetPropertyValue_InContainer(Snapshot) &&
            InFixture.Failed->GetPropertyValue_InContainer(Snapshot);
    }

    auto ReadVector(const FStructProperty* InProperty, const void* InContainer) -> FVector
    {
        return *InProperty->ContainerPtrToValuePtr<FVector>(InContainer);
    }

    auto SamplesValid(AGameStateBase* InState, const FFixture& InFixture,
                      FVector* OutFirstRoot = nullptr) -> bool
    {
        const auto* Snapshot = GetSnapshot(InState, InFixture);
        if (!Snapshot) { return false; }
        FScriptArrayHelper Samples(InFixture.Samples,
            InFixture.Samples->ContainerPtrToValuePtr<void>(Snapshot));
        if (Samples.Num() != 3) { return false; }
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const auto* Sample = Samples.GetRawPtr(Index);
            const auto Root = ReadVector(InFixture.RootPosition, Sample);
            if (Root.ContainsNaN()) { return false; }
            if (Index == 0 && OutFirstRoot) { *OutFirstRoot = Root; }
            FScriptArrayHelper Expected(InFixture.ExpectedLinks,
                InFixture.ExpectedLinks->ContainerPtrToValuePtr<void>(Sample));
            FScriptArrayHelper Actual(InFixture.ActualLinks,
                InFixture.ActualLinks->ContainerPtrToValuePtr<void>(Sample));
            if (Expected.Num() != Stations[Index].NodeCount - 1 ||
                Actual.Num() != Stations[Index].NodeCount - 1)
            { return false; }
            const auto* ExpectedItem = CastField<FStructProperty>(InFixture.ExpectedLinks->Inner);
            const auto* ActualItem = CastField<FStructProperty>(InFixture.ActualLinks->Inner);
            float LeafDistance = 0.0f;
            for (int32 Link = 0; Link < Expected.Num(); ++Link)
            {
                const auto ExpectedPoint = ReadVector(ExpectedItem, Expected.GetRawPtr(Link));
                const auto ActualPoint = ReadVector(ActualItem, Actual.GetRawPtr(Link));
                if (ExpectedPoint.ContainsNaN() || ActualPoint.ContainsNaN())
                { return false; }
                const auto Distance = FVector::Dist(ExpectedPoint, ActualPoint);
                if (!FMath::IsFinite(Distance) || Distance > 5.05f) { return false; }
                LeafDistance = Distance;
            }
            const auto Drift = InFixture.Drift->GetFloatingPointPropertyValue(
                InFixture.Drift->ContainerPtrToValuePtr<void>(Sample));
            if (!FMath::IsFinite(Drift) || Drift < 0.0f || Drift > 5.05f ||
                !FMath::IsNearlyEqual(Drift, LeafDistance, 0.05f))
            { return false; }
        }
        return true;
    }

    auto ProjectionReady(AGameStateBase* InState, const FFixture& InFixture,
                         int32 InGeneration) -> bool
    {
        return IsValid(InState) &&
            InFixture.LocalProjectionGeneration->GetPropertyValue_InContainer(InState) == InGeneration &&
            InFixture.LocalProjectionReady->GetPropertyValue_InContainer(InState) &&
            !InFixture.LocalProjectionFailed->GetPropertyValue_InContainer(InState);
    }

    auto GetServerHandle(AGameStateBase* InState, FStructProperty* InProperty) -> FCk_Handle
    {
        return IsValid(InState) ? *InProperty->ContainerPtrToValuePtr<FCk_Handle>(InState) : FCk_Handle{};
    }

    struct FCounts { int32 Nodes = 0; int32 Tweens = 0; };

    auto IsLive(const FCk_Handle& InHandle) -> bool
    {
        return ck::IsValid(InHandle) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::Teardown) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::Destroyed);
    }

    auto CountBranch(const FCk_Handle& InRoot, FCounts& OutCounts,
                     TArray<FCk_Handle>* OutSimulation = nullptr) -> void
    {
        if (!IsLive(InRoot)) { return; }
        const bool bNode = UCk_Utils_SceneNode_UE::Has(InRoot);
        const bool bTween = UCk_Utils_Tween_UE::Has(InRoot);
        OutCounts.Nodes += bNode ? 1 : 0;
        OutCounts.Tweens += bTween ? 1 : 0;
        if (OutSimulation && (bNode || bTween)) { OutSimulation->AddUnique(InRoot); }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(InRoot))
        { CountBranch(Child, OutCounts, OutSimulation); }
    }

    auto GetExactChildren(UWorld* InWorld, const FStation& InStation,
                          TArray<FCk_Handle>& OutChildren) -> bool
    {
        OutChildren.Reset();
        const auto Root = IsValid(InWorld)
            ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld) : FCk_Handle{};
        if (!IsLive(Root)) { return false; }
        const auto Tagged = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, InStation.Tag);
        if (Tagged.Num() != 1 || !IsLive(Tagged[0])) { return false; }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Tagged[0]))
        {
            if (!IsLive(Child) || !UCk_Utils_EntityScript_UE::Has(Child)) { continue; }
            const auto Script = UCk_Utils_EntityScript_UE::CastChecked(Child);
            const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
            if (IsValid(Class) && Class->GetPathName() == InStation.ScriptPath)
            { OutChildren.Add(Child); }
        }
        return true;
    }

    auto GetChild(UWorld* InWorld, const FStation& InStation) -> FCk_Handle
    {
        TArray<FCk_Handle> Children;
        return GetExactChildren(InWorld, InStation, Children) && Children.Num() == 1
            ? Children[0] : FCk_Handle{};
    }

    auto CheckBranches(UWorld* InWorld, bool InServer, TArray<FCk_Handle>* OutScripts = nullptr,
                       TArray<FCk_Handle>* OutSimulation = nullptr) -> bool
    {
        auto* State = IsValid(InWorld) ? InWorld->GetGameState() : nullptr;
        if (!IsValid(State)) { return false; }
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const auto Child = GetChild(InWorld, Stations[Index]);
            if (!IsLive(Child)) { return false; }
            if (OutScripts) { OutScripts->Add(Child); }
            FCounts Counts;
            CountBranch(Child, Counts, OutSimulation);
            if (Counts.Nodes != (InServer ? Stations[Index].NodeCount : 0) ||
                Counts.Tweens != (InServer ? 1 : 0))
            { return false; }
        }
        return true;
    }

    auto NoServerBranches(UWorld* InWorld) -> bool
    {
        for (const auto& Station : Stations)
        {
            TArray<FCk_Handle> Children;
            if (!GetExactChildren(InWorld, Station, Children)) { return false; }
            FCounts LiveSimulation;
            for (const auto& Child : Children) { CountBranch(Child, LiveSimulation); }
            if (!Children.IsEmpty() || LiveSimulation.Nodes != 0 || LiveSimulation.Tweens != 0)
            { return false; }
        }
        return true;
    }

    auto CatalogsMatch(UWorld* InServer, UWorld* InClient) -> bool
    {
        auto* ServerInstance = IsValid(InServer) ? InServer->GetGameInstance() : nullptr;
        auto* ClientInstance = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
        const auto* Server = IsValid(ServerInstance)
            ? ServerInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        const auto* Client = IsValid(ClientInstance)
            ? ClientInstance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        if (!IsValid(Server) || !IsValid(Client)) { return false; }
        const auto Index = Server->Find_GymIndexByName(TEXT("Scene Node + Tween"));
        return Index != INDEX_NONE && Server->Get_CurrentGymIndex() == Index &&
            Client->Get_CurrentGymIndex() == Index &&
            Client->Get_Entries().Num() == Server->Get_Entries().Num() &&
            Client->Get_SuppressHUDDuringStartup() == Server->Get_SuppressHUDDuringStartup();
    }

    auto DescribeWorldReadiness(UWorld* InWorld, const FFixture& InFixture, bool InServer) -> FString
    {
        auto* State = GetState(InWorld, InFixture);
        const auto* Snapshot = GetSnapshot(State, InFixture);
        if (!Snapshot) { return FString::Printf(TEXT("world=%s authored-state=missing"), *GetNameSafe(InWorld)); }
        auto* Instance = InWorld->GetGameInstance();
        const auto* Registry = IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        FString Branches;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            TArray<FCk_Handle> Children;
            const auto bStation = GetExactChildren(InWorld, Stations[Index], Children);
            FCounts Counts;
            for (const auto& Child : Children) { CountBranch(Child, Counts); }
            Branches += FString::Printf(TEXT(" [%d:station=%d scripts=%d nodes=%d tweens=%d]"),
                Index, bStation, Children.Num(), Counts.Nodes, Counts.Tweens);
        }
        FScriptArrayHelper Samples(InFixture.Samples,
            InFixture.Samples->ContainerPtrToValuePtr<void>(Snapshot));
        FString SampleStates;
        for (int32 Index = 0; Index < Samples.Num(); ++Index)
        {
            const auto* Sample = Samples.GetRawPtr(Index);
            FScriptArrayHelper Expected(InFixture.ExpectedLinks,
                InFixture.ExpectedLinks->ContainerPtrToValuePtr<void>(Sample));
            FScriptArrayHelper Actual(InFixture.ActualLinks,
                InFixture.ActualLinks->ContainerPtrToValuePtr<void>(Sample));
            double MaxDistance = 0.0;
            for (int32 Link = 0; Link < FMath::Min(Expected.Num(), Actual.Num()); ++Link)
            {
                const auto Distance = FVector::Dist(
                    ReadVector(CastField<FStructProperty>(InFixture.ExpectedLinks->Inner), Expected.GetRawPtr(Link)),
                    ReadVector(CastField<FStructProperty>(InFixture.ActualLinks->Inner), Actual.GetRawPtr(Link)));
                MaxDistance = FMath::IsFinite(Distance) ? FMath::Max(MaxDistance, Distance) : -1.0;
                if (MaxDistance < 0.0) { break; }
            }
            SampleStates += FString::Printf(TEXT(" [sample%d:links=%d/%d max-distance=%.3f drift=%.3f]"),
                Index, Expected.Num(), Actual.Num(), MaxDistance,
                InFixture.Drift->GetFloatingPointPropertyValue(InFixture.Drift->ContainerPtrToValuePtr<void>(Sample)));
        }
        return FString::Printf(TEXT("world=%s server=%d gen=%d seq=%d ready=%d failed=%d reason='%s' "
            "samples-valid=%d trio=%d projection-gen=%d projection-ready=%d projection-failed=%d "
            "projection-reason='%s' catalog-index=%d entries=%d%s%s"),
            *GetNameSafe(InWorld), InServer, GetGeneration(State, InFixture), GetSequence(State, InFixture),
            InFixture.Ready->GetPropertyValue_InContainer(Snapshot),
            InFixture.Failed->GetPropertyValue_InContainer(Snapshot),
            *InFixture.FailureReason->GetPropertyValue_InContainer(Snapshot), SamplesValid(State, InFixture),
            InFixture.ServerTrioCount->GetPropertyValue_InContainer(State),
            InFixture.LocalProjectionGeneration->GetPropertyValue_InContainer(State),
            InFixture.LocalProjectionReady->GetPropertyValue_InContainer(State),
            InFixture.LocalProjectionFailed->GetPropertyValue_InContainer(State),
            *InFixture.LocalProjectionFailureReason->GetPropertyValue_InContainer(State),
            IsValid(Registry) ? Registry->Get_CurrentGymIndex() : INDEX_NONE,
            IsValid(Registry) ? Registry->Get_Entries().Num() : 0, *Branches, *SampleStates);
    }

    auto WorldReady(UWorld* InWorld, const FFixture& InFixture, int32 InGeneration,
                    bool InAutoRunning, bool InServer) -> bool
    {
        auto* State = GetState(InWorld, InFixture);
        if (!IsReady(State, InFixture, InGeneration, InAutoRunning) ||
            !SamplesValid(State, InFixture) ||
            !CheckBranches(InWorld, InServer) ||
            (InServer && InFixture.ServerTrioCount->GetPropertyValue_InContainer(State) != 3) ||
            (!InServer && !ProjectionReady(State, InFixture, InGeneration)))
        { return false; }
        if (InServer)
        {
            for (int32 Index = 0; Index < 3; ++Index)
            {
                if (GetServerHandle(State, InFixture.ServerHandles[Index]) !=
                    GetChild(InWorld, Stations[Index]))
                { return false; }
            }
        }
        return true;
    }

    auto AllWorldsReady(FFixture& InFixture, int32 InGeneration,
                        bool InAutoRunning) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
        auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
        if (ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 ||
            !WorldReady(Server, InFixture, InGeneration, InAutoRunning, true) ||
            !WorldReady(Client0, InFixture, InGeneration, InAutoRunning, false) ||
            !WorldReady(Client1, InFixture, InGeneration, InAutoRunning, false) ||
            !CatalogsMatch(Server, Client0) || !CatalogsMatch(Server, Client1))
        { return false; }
        auto* ServerState = GetState(Server, InFixture);
        if (InFixture.HistoryGeneration != InGeneration ||
            InFixture.HistoryAutoRunning != InAutoRunning)
        {
            InFixture.ObservedSnapshots.Reset();
            InFixture.HistoryGeneration = InGeneration;
            InFixture.HistoryAutoRunning = InAutoRunning;
        }
        const auto ServerSequence = GetSequence(ServerState, InFixture);
        FObservedSnapshot* Observed = nullptr;
        for (auto& Candidate : InFixture.ObservedSnapshots)
        {
            if (Candidate.Generation == InGeneration && Candidate.Sequence == ServerSequence)
            { Observed = &Candidate; break; }
        }
        if (!Observed)
        {
            if (InFixture.ObservedSnapshots.Num() >= 32)
            { InFixture.ObservedSnapshots.RemoveAt(0); }
            Observed = &InFixture.ObservedSnapshots.AddDefaulted_GetRef();
        }
        Observed->Generation = InGeneration;
        Observed->Sequence = ServerSequence;
        Observed->AutoRunning = InAutoRunning;
        Observed->Copy = MakeShared<FStructOnScope>(InFixture.Snapshot->Struct.Get());
        InFixture.Snapshot->Struct->CopyScriptStruct(
            Observed->Copy->GetStructMemory(), GetSnapshot(ServerState, InFixture));
        for (auto* Client : {Client0, Client1})
        {
            auto* State = GetState(Client, InFixture);
            bool bMatched = false;
            for (const auto& Published : InFixture.ObservedSnapshots)
            {
                if (Published.Generation == InGeneration &&
                    Published.Sequence == GetSequence(State, InFixture) &&
                    Published.AutoRunning == InAutoRunning && Published.Copy.IsValid() &&
                    InFixture.Snapshot->Identical(GetSnapshot(State, InFixture),
                        Published.Copy->GetStructMemory(), 0))
                { bMatched = true; break; }
            }
            if (!bMatched) { return false; }
        }
        return true;
    }

    auto InvokeControl(APlayerController* InController, const FFixture& InFixture,
                       int32 InSerial, int32 InAction, bool InAutoRunning) -> bool
    {
        if (!IsValid(InController) || !InController->IsA(InFixture.Controller)) { return false; }
        FStructOnScope Params(InFixture.ControlRpc);
        auto* Memory = Params.GetStructMemory();
        InFixture.RpcSerial->SetPropertyValue_InContainer(Memory, InSerial);
        InFixture.RpcAction->SetPropertyValue_InContainer(Memory, InAction);
        InFixture.RpcAutoRunning->SetPropertyValue_InContainer(Memory, InAutoRunning);
        InController->ProcessEvent(InFixture.ControlRpc, Memory);
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSceneNodeTweenGym_Network,
    "Ck.Gym.Network.SceneNodeTween.SharedTrio",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkSceneNodeTweenGym_Network::RunTest(const FString& Parameters)
{
    using namespace ck_scene_node_tween_gym_network_test;

    bool bDedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, bDedicated))
    { AddError(TEXT("unsupported Scene Node + Tween topology")); return false; }

    const auto Fixture = MakeShared<FFixture>();
    Fixture->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    Fixture->State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    Fixture->Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    if (!TestNotNull(TEXT("authored Scene Node + Tween GameMode"), Fixture->Mode) ||
        !TestNotNull(TEXT("authored shared GameState"), Fixture->State) ||
        !TestNotNull(TEXT("authored owning PlayerController"), Fixture->Controller))
    { return false; }

    Fixture->Snapshot = FindFProperty<FStructProperty>(Fixture->State, TEXT("Snapshot"));
    if (!TestNotNull(TEXT("replicated Snapshot struct"), Fixture->Snapshot)) { return false; }
    auto* Schema = Fixture->Snapshot->Struct.Get();
    Fixture->Generation = FindFProperty<FIntProperty>(Schema, TEXT("Generation"));
    Fixture->Sequence = FindFProperty<FIntProperty>(Schema, TEXT("Sequence"));
    Fixture->Ready = FindFProperty<FBoolProperty>(Schema, TEXT("Ready"));
    Fixture->Failed = FindFProperty<FBoolProperty>(Schema, TEXT("Failed"));
    Fixture->FailureReason = FindFProperty<FStrProperty>(Schema, TEXT("FailureReason"));
    Fixture->AutoRunning = FindFProperty<FBoolProperty>(Schema, TEXT("AutoRunning"));
    Fixture->Samples = FindFProperty<FArrayProperty>(Schema, TEXT("Stations"));
    if (!TestNotNull(TEXT("Snapshot.Stations array"), Fixture->Samples)) { return false; }
    Fixture->SampleItem = CastField<FStructProperty>(Fixture->Samples->Inner);
    if (!TestNotNull(TEXT("Stations element struct"), Fixture->SampleItem)) { return false; }
    auto* SampleSchema = Fixture->SampleItem->Struct.Get();
    Fixture->RootPosition = FindFProperty<FStructProperty>(SampleSchema, TEXT("RootPosition"));
    Fixture->ExpectedLinks = FindFProperty<FArrayProperty>(SampleSchema, TEXT("ExpectedLinks"));
    Fixture->ActualLinks = FindFProperty<FArrayProperty>(SampleSchema, TEXT("ActualLinks"));
    Fixture->Drift = FindFProperty<FNumericProperty>(SampleSchema, TEXT("Drift"));
    Fixture->ServerTrioCount = FindFProperty<FIntProperty>(Fixture->State, TEXT("ServerTrioCount"));
    Fixture->ServerHandles[0] = FindFProperty<FStructProperty>(Fixture->State, TEXT("ServerSimple"));
    Fixture->ServerHandles[1] = FindFProperty<FStructProperty>(Fixture->State, TEXT("ServerChain"));
    Fixture->ServerHandles[2] = FindFProperty<FStructProperty>(Fixture->State, TEXT("ServerDeep"));
    Fixture->LocalProjectionGeneration = FindFProperty<FIntProperty>(
        Fixture->State, TEXT("LocalProjectionGeneration"));
    Fixture->LocalProjectionReady = FindFProperty<FBoolProperty>(
        Fixture->State, TEXT("LocalProjectionReady"));
    Fixture->LocalProjectionFailed = FindFProperty<FBoolProperty>(
        Fixture->State, TEXT("LocalProjectionFailed"));
    Fixture->LocalProjectionFailureReason = FindFProperty<FStrProperty>(
        Fixture->State, TEXT("LocalProjectionFailureReason"));
    Fixture->LastControlSerial = FindFProperty<FIntProperty>(
        Fixture->Controller, TEXT("LastControlSerial"));
    Fixture->LastControlAttemptSerial = FindFProperty<FIntProperty>(
        Fixture->Controller, TEXT("LastControlAttemptSerial"));
    Fixture->LastControlResult = FindFProperty<FIntProperty>(
        Fixture->Controller, TEXT("LastControlResult"));
    Fixture->ControlRpc = Fixture->Controller->FindFunctionByName(
        TEXT("Server_RequestSceneNodeTweenControl"));

    bool bSchema = true;
    bSchema &= TestNotNull(TEXT("Snapshot.Generation"), Fixture->Generation);
    bSchema &= TestNotNull(TEXT("Snapshot.Sequence"), Fixture->Sequence);
    bSchema &= TestNotNull(TEXT("Snapshot.Ready"), Fixture->Ready);
    bSchema &= TestNotNull(TEXT("Snapshot.Failed"), Fixture->Failed);
    bSchema &= TestNotNull(TEXT("Snapshot.FailureReason"), Fixture->FailureReason);
    bSchema &= TestNotNull(TEXT("Snapshot.AutoRunning"), Fixture->AutoRunning);
    bSchema &= TestNotNull(TEXT("StationSample.RootPosition"), Fixture->RootPosition);
    bSchema &= TestNotNull(TEXT("StationSample.ExpectedLinks"), Fixture->ExpectedLinks);
    bSchema &= TestNotNull(TEXT("StationSample.ActualLinks"), Fixture->ActualLinks);
    bSchema &= TestTrue(TEXT("StationSample.Drift is floating point"),
        Fixture->Drift && Fixture->Drift->IsFloatingPoint());
    bSchema &= TestNotNull(TEXT("GameState.ServerTrioCount"), Fixture->ServerTrioCount);
    for (int32 Index = 0; Index < 3; ++Index)
    { bSchema &= TestNotNull(TEXT("GameState station handle"), Fixture->ServerHandles[Index]); }
    bSchema &= TestNotNull(TEXT("GameState.LocalProjectionGeneration"),
        Fixture->LocalProjectionGeneration);
    bSchema &= TestNotNull(TEXT("GameState.LocalProjectionReady"),
        Fixture->LocalProjectionReady);
    bSchema &= TestNotNull(TEXT("GameState.LocalProjectionFailed"),
        Fixture->LocalProjectionFailed);
    bSchema &= TestNotNull(TEXT("GameState.LocalProjectionFailureReason"),
        Fixture->LocalProjectionFailureReason);
    bSchema &= TestNotNull(TEXT("PlayerController.LastControlSerial"), Fixture->LastControlSerial);
    bSchema &= TestNotNull(TEXT("PlayerController.LastControlAttemptSerial"),
        Fixture->LastControlAttemptSerial);
    bSchema &= TestNotNull(TEXT("PlayerController.LastControlResult"), Fixture->LastControlResult);
    bSchema &= TestNotNull(TEXT("PlayerController control RPC"), Fixture->ControlRpc);
    if (!bSchema) { return false; }
    Fixture->RpcSerial = FindFProperty<FIntProperty>(Fixture->ControlRpc, TEXT("InSerial"));
    Fixture->RpcAction = FindFProperty<FIntProperty>(Fixture->ControlRpc, TEXT("InAction"));
    Fixture->RpcAutoRunning = FindFProperty<FBoolProperty>(
        Fixture->ControlRpc, TEXT("InAutoRunning"));
    if (!TestTrue(TEXT("exact replicated snapshot/sample and RPC schema"),
        Fixture->Snapshot->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->Snapshot->Struct->GetName() == TEXT("CkSceneNodeTweenGym_Snapshot") &&
        Fixture->SampleItem->Struct->GetName() == TEXT("CkSceneNodeTweenGym_StationSample") &&
        Fixture->RootPosition->Struct == TBaseStructure<FVector>::Get() &&
        CastField<FStructProperty>(Fixture->ExpectedLinks->Inner) &&
        CastField<FStructProperty>(Fixture->ExpectedLinks->Inner)->Struct == TBaseStructure<FVector>::Get() &&
        CastField<FStructProperty>(Fixture->ActualLinks->Inner) &&
        CastField<FStructProperty>(Fixture->ActualLinks->Inner)->Struct == TBaseStructure<FVector>::Get() &&
        Fixture->ServerHandles[0]->Struct == FCk_Handle::StaticStruct() &&
        Fixture->ServerHandles[1]->Struct == FCk_Handle::StaticStruct() &&
        Fixture->ServerHandles[2]->Struct == FCk_Handle::StaticStruct() &&
        Fixture->LastControlSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->LastControlAttemptSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->LastControlResult->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->RpcSerial && Fixture->RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->RpcAction && Fixture->RpcAction->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->RpcAutoRunning && Fixture->RpcAutoRunning->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->ControlRpc->NumParms == 3 &&
        Fixture->ControlRpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer)))
    { return false; }

    Fixture->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, Fixture->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([this, Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            const auto Generation = GetGeneration(State, *Fixture);
            const auto bReady = Generation > 0 && AllWorldsReady(*Fixture, Generation, true) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1));
            const auto Now = FPlatformTime::Seconds();
            if (!bReady && Now >= Fixture->NextReadinessDiagnosticSeconds)
            {
                Fixture->NextReadinessDiagnosticSeconds = Now + 5.0;
                AddInfo(FString::Printf(TEXT("Scene readiness: history=%d controllers=%d/%d catalogs=%d/%d"),
                    Fixture->ObservedSnapshots.Num(),
                    IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)),
                    IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1)),
                    CatalogsMatch(Server, ck::auto_test::net::Get_ClientWorld(0)),
                    CatalogsMatch(Server, ck::auto_test::net::Get_ClientWorld(1))));
                AddInfo(DescribeWorldReadiness(Server, *Fixture, true));
                AddInfo(DescribeWorldReadiness(ck::auto_test::net::Get_ClientWorld(0), *Fixture, false));
                AddInfo(DescribeWorldReadiness(ck::auto_test::net::Get_ClientWorld(1), *Fixture, false));
            }
            return bReady;
        }), 45.0, TEXT("one ready shared trio, two complete local projections and catalog convergence")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture, bDedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            if (!TestTrue(TEXT("authored server GameMode and state"),
                IsValid(Server) && IsValid(State) && IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(Fixture->Mode) &&
                Server->GetNetMode() == (bDedicated ? NM_DedicatedServer : NM_ListenServer)))
            { return false; }
            Fixture->InitialGeneration = GetGeneration(State, *Fixture);
            Fixture->SurvivorState = State;
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            Fixture->SurvivorInstance = IsValid(Client1) ? Client1->GetGameInstance() : nullptr;
            Fixture->OldScripts.Reset();
            Fixture->OldSimulation.Reset();
            FVector Root;
            return TestTrue(TEXT("exact server 2/3/6 SceneNodes and one Tween per station"),
                Fixture->InitialGeneration > 0 &&
                CheckBranches(Server, true, &Fixture->OldScripts, &Fixture->OldSimulation) &&
                Fixture->OldScripts.Num() == 3 && Fixture->OldSimulation.Num() == 14 &&
                SamplesValid(State, *Fixture, &Root) &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration, true) &&
                IsValid(Fixture->SurvivorInstance.Get()));
        }), TEXT("authoritative station ownership and projected client samples")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* PC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning client submits invalid serial"),
                InvokeControl(PC, *Fixture, 0, 0, false));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            return IsValid(PC) && PC->IsA(Fixture->Controller) &&
                Fixture->OldScripts.Num() == 3 &&
                Fixture->LastControlAttemptSerial->GetPropertyValue_InContainer(PC) == 0 &&
                Fixture->LastControlResult->GetPropertyValue_InContainer(PC) == 4 &&
                Fixture->LastControlSerial->GetPropertyValue_InContainer(PC) == 0 &&
                IsReady(State, *Fixture, Fixture->InitialGeneration, true) &&
                CheckBranches(ck::auto_test::net::Get_ServerWorld(), true) &&
                GetServerHandle(State, Fixture->ServerHandles[0]) == Fixture->OldScripts[0] &&
                GetServerHandle(State, Fixture->ServerHandles[1]) == Fixture->OldScripts[1] &&
                GetServerHandle(State, Fixture->ServerHandles[2]) == Fixture->OldScripts[2];
        }), 15.0, TEXT("invalid serial rejects without changing the shared trio")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* PC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning client submits unsupported action"),
                InvokeControl(PC, *Fixture, 1500, 99, false));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            return IsValid(PC) && PC->IsA(Fixture->Controller) &&
                Fixture->OldScripts.Num() == 3 &&
                Fixture->LastControlAttemptSerial->GetPropertyValue_InContainer(PC) == 1500 &&
                Fixture->LastControlResult->GetPropertyValue_InContainer(PC) == 4 &&
                Fixture->LastControlSerial->GetPropertyValue_InContainer(PC) == 0 &&
                IsReady(State, *Fixture, Fixture->InitialGeneration, true) &&
                CheckBranches(ck::auto_test::net::Get_ServerWorld(), true) &&
                GetServerHandle(State, Fixture->ServerHandles[0]) == Fixture->OldScripts[0] &&
                GetServerHandle(State, Fixture->ServerHandles[1]) == Fixture->OldScripts[1] &&
                GetServerHandle(State, Fixture->ServerHandles[2]) == Fixture->OldScripts[2];
        }), 15.0, TEXT("unsupported action rejects without accepting serial or changing trio")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* PC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("remote owner pauses the shared trio"),
                InvokeControl(PC, *Fixture, 1501, 0, false));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            return IsValid(PC) && PC->IsA(Fixture->Controller) &&
                Fixture->LastControlAttemptSerial->GetPropertyValue_InContainer(PC) == 1501 &&
                Fixture->LastControlSerial->GetPropertyValue_InContainer(PC) == 1501 &&
                Fixture->LastControlResult->GetPropertyValue_InContainer(PC) == 1 &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration, false);
        }), 30.0, TEXT("owning-client pause converges on both client projections")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            Fixture->DepartureSequence = GetSequence(State, *Fixture);
            return TestTrue(TEXT("paused server sample remains complete"),
                SamplesValid(State, *Fixture, &Fixture->PausedRoot));
        }), TEXT("capture settled paused root")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            FVector Current;
            return TestTrue(TEXT("paused root is stable while snapshot sequence advances"),
                SamplesValid(State, *Fixture, &Current) &&
                Current.Equals(Fixture->PausedRoot, 1.0) &&
                GetSequence(State, *Fixture) > Fixture->DepartureSequence &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration, false));
        }), TEXT("pause halts movement without stopping coherent publication")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* PC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("remote owner replays the paused shared generation"),
                InvokeControl(PC, *Fixture, 1502, 1, false));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(PC) || !PC->IsA(Fixture->Controller) ||
                Fixture->LastControlAttemptSerial->GetPropertyValue_InContainer(PC) != 1502 ||
                Fixture->LastControlSerial->GetPropertyValue_InContainer(PC) != 1502 ||
                Fixture->LastControlResult->GetPropertyValue_InContainer(PC) != 1 ||
                !AllWorldsReady(*Fixture, Fixture->InitialGeneration + 1, false))
            { return false; }
            for (const auto& Old : Fixture->OldScripts)
            { if (ck::IsValid(Old)) { return false; } }
            for (const auto& Old : Fixture->OldSimulation)
            { if (ck::IsValid(Old)) { return false; } }
            return CheckBranches(Server, true);
        }), 45.0, TEXT("replay retires all old scripts, SceneNodes and Tweens, with one new paused trio")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            TArray<FCk_Handle> NewScripts;
            if (!TestTrue(TEXT("new shared trio is complete"),
                State == Fixture->SurvivorState.Get() &&
                CheckBranches(Server, true, &NewScripts) && NewScripts.Num() == 3 &&
                Fixture->OldScripts.Num() == 3))
            { return false; }
            for (int32 Index = 0; Index < 3; ++Index)
            {
                if (!TestTrue(TEXT("GameState maps a fresh original station child"),
                    NewScripts[Index] != Fixture->OldScripts[Index] &&
                    GetServerHandle(State, Fixture->ServerHandles[Index]) == NewScripts[Index]))
                { return false; }
            }
            return true;
        }), TEXT("replay replaces exactly one authoritative trio")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* PC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("remote owner resumes shared animation"),
                InvokeControl(PC, *Fixture, 1503, 0, true));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            return IsValid(PC) && PC->IsA(Fixture->Controller) &&
                Fixture->LastControlAttemptSerial->GetPropertyValue_InContainer(PC) == 1503 &&
                Fixture->LastControlSerial->GetPropertyValue_InContainer(PC) == 1503 &&
                Fixture->LastControlResult->GetPropertyValue_InContainer(PC) == 1 &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration + 1, true);
        }), 30.0, TEXT("resume converges on both clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            return TestTrue(TEXT("moving snapshot has finite first root"),
                SamplesValid(State, *Fixture, &Fixture->MovingRoot));
        }), TEXT("capture resumed root before motion watchdog")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            FVector Current;
            return SamplesValid(State, *Fixture, &Current) &&
                !Current.Equals(Fixture->MovingRoot, 1.0) &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration + 1, true);
        }), 20.0, TEXT("resumed authoritative root moves and both projections remain ready")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* RemotePC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* LocalPC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            auto* Instance = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
            if (!TestTrue(TEXT("departing owner and GameInstance exist"),
                IsValid(RemotePC) && IsValid(LocalPC) && IsValid(Instance)))
            { return; }
            Fixture->DepartingServerController = RemotePC;
            Fixture->DepartingInstance = Instance;
            Fixture->DepartureSequence = GetSequence(Fixture->SurvivorState.Get(), *Fixture);
            LocalPC->ClientTravel(TEXT("/Engine/Maps/Entry"), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* SurvivorInstance = Fixture->SurvivorInstance.Get();
            auto* SurvivorWorld = IsValid(SurvivorInstance) ? SurvivorInstance->GetWorld() : nullptr;
            auto* DepartingInstance = Fixture->DepartingInstance.Get();
            auto* DepartedWorld = IsValid(DepartingInstance) ? DepartingInstance->GetWorld() : nullptr;
            return State == Fixture->SurvivorState.Get() &&
                !Fixture->DepartingServerController.IsValid() &&
                IsValid(DepartedWorld) && DepartedWorld != Server &&
                DepartedWorld->GetMapName().Contains(TEXT("Entry")) &&
                WorldReady(Server, *Fixture, Fixture->InitialGeneration + 1, true, true) &&
                WorldReady(SurvivorWorld, *Fixture, Fixture->InitialGeneration + 1, true, false) &&
                GetSequence(State, *Fixture) > Fixture->DepartureSequence &&
                GetSequence(GetState(SurvivorWorld, *Fixture), *Fixture) > Fixture->DepartureSequence &&
                CatalogsMatch(Server, SurvivorWorld);
        }), 45.0, TEXT("actual client departure leaves same server trio and advancing survivor projection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Survivor = Fixture->SurvivorInstance.Get();
            auto* Departed = Fixture->DepartingInstance.Get();
            return TestTrue(TEXT("relay owners stay in their own worlds after client departure"),
                IsValid(Survivor) && IsValid(Departed) &&
                RelayOwnersBelongToWorld(ck::auto_test::net::Get_ServerWorld(), true) &&
                RelayOwnersBelongToWorld(Survivor->GetWorld()) &&
                RelayOwnersBelongToWorld(Departed->GetWorld()));
        }), TEXT("foreign destination login cannot populate another world's relay pools")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InServer)
        {
            auto* State = GetState(InServer, *Fixture);
            Fixture->FailedScripts.Reset();
            Fixture->FailedSimulation.Reset();
            if (!TestTrue(TEXT("failure probe starts with complete surviving trio"),
                State == Fixture->SurvivorState.Get() &&
                WorldReady(InServer, *Fixture, Fixture->InitialGeneration + 1, true, true) &&
                CheckBranches(InServer, true, &Fixture->FailedScripts,
                    &Fixture->FailedSimulation) &&
                Fixture->FailedScripts.Num() == 3 && Fixture->FailedSimulation.Num() == 14))
            { return; }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Fixture->FailedScripts[0]);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* Instance = Fixture->SurvivorInstance.Get();
            auto* SurvivorWorld = IsValid(Instance) ? Instance->GetWorld() : nullptr;
            auto* SurvivorState = GetState(SurvivorWorld, *Fixture);
            if (!IsFailed(State, *Fixture, Fixture->InitialGeneration + 1) ||
                !IsFailed(SurvivorState, *Fixture, Fixture->InitialGeneration + 1) ||
                Fixture->ServerTrioCount->GetPropertyValue_InContainer(State) != 0 ||
                !NoServerBranches(Server) || Fixture->FailedScripts.Num() != 3)
            { return false; }
            for (const auto& Script : Fixture->FailedScripts)
            { if (ck::IsValid(Script)) { return false; } }
            for (const auto& Simulation : Fixture->FailedSimulation)
            { if (ck::IsValid(Simulation)) { return false; } }
            for (int32 Index = 0; Index < 3; ++Index)
            { if (ck::IsValid(GetServerHandle(State, Fixture->ServerHandles[Index]))) { return false; } }
            return Fixture->LocalProjectionFailed->GetPropertyValue_InContainer(SurvivorState) &&
                !Fixture->LocalProjectionReady->GetPropertyValue_InContainer(SurvivorState);
        }), 25.0, TEXT("one real child loss fails and retires entire shared trio and survivor projection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            Fixture->FailureSequence = GetSequence(State, *Fixture);
            return TestTrue(TEXT("failed snapshot has a captured sequence"), Fixture->FailureSequence > 0);
        }), TEXT("capture terminal failure sequence")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* Instance = Fixture->SurvivorInstance.Get();
            auto* SurvivorWorld = IsValid(Instance) ? Instance->GetWorld() : nullptr;
            return TestTrue(TEXT("failure stays terminal without sibling progress"),
                State == Fixture->SurvivorState.Get() &&
                IsFailed(State, *Fixture, Fixture->InitialGeneration + 1) &&
                IsFailed(GetState(SurvivorWorld, *Fixture), *Fixture,
                    Fixture->InitialGeneration + 1) &&
                GetSequence(State, *Fixture) == Fixture->FailureSequence &&
                Fixture->ServerTrioCount->GetPropertyValue_InContainer(State) == 0 &&
                NoServerBranches(Server));
        }), TEXT("child-loss failure remains atomic after finite watchdog")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            Fixture->RestoreSettings();
            return TestTrue(TEXT("Scene Node + Tween PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("cleanup and restore original startup preferences"), true));
    return true;
}

#endif
