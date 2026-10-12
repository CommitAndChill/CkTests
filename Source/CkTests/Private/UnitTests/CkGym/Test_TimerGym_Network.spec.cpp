#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTimer/CkTimer_Utils.h"

#include <HAL/PlatformTime.h>
#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_timer_gym_network_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_TimerGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_TimerGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_TimerGym_PlayerController");

    struct FStation
    {
        FName Tag;
        const TCHAR* ScriptPath;
        int32 AuthorityTimers;
        int32 DemoTimers;
        int32 Counters;
    };
    const FStation Stations[5] = {
        {TEXT("Gym.Timer.Basics"), TEXT("/Script/Angelscript.Ck_EntityScript_TimerGym_Basics"), 3, 2, 1},
        {TEXT("Gym.Timer.Behaviors"), TEXT("/Script/Angelscript.Ck_EntityScript_TimerGym_Behaviors"), 5, 4, 3},
        {TEXT("Gym.Timer.Signals"), TEXT("/Script/Angelscript.Ck_EntityScript_TimerGym_Signals"), 3, 2, 8},
        {TEXT("Gym.Timer.Control"), TEXT("/Script/Angelscript.Ck_EntityScript_TimerGym_Control"), 3, 2, 1},
        {TEXT("Gym.Timer.Countdown"), TEXT("/Script/Angelscript.Ck_EntityScript_TimerGym_Countdown"), 6, 5, 4},
    };

    struct FObserved
    {
        int32 Generation = 0;
        int32 Sequence = 0;
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
        FArrayProperty* Samples = nullptr;
        FStructProperty* SampleItem = nullptr;
        FIntProperty* Epoch = nullptr;
        FArrayProperty* Elapsed = nullptr;
        FArrayProperty* Goal = nullptr;
        FArrayProperty* TimerStates = nullptr;
        FArrayProperty* Counters = nullptr;
        FStrProperty* Body = nullptr;
        FIntProperty* ServerQuintetCount = nullptr;
        FArrayProperty* ServerScripts = nullptr;
        FIntProperty* LocalGeneration = nullptr;
        FBoolProperty* LocalReady = nullptr;
        FBoolProperty* LocalFailed = nullptr;
        FStrProperty* LocalFailureReason = nullptr;
        FIntProperty* LastSerial = nullptr;
        FIntProperty* AttemptSerial = nullptr;
        FIntProperty* Result = nullptr;
        UFunction* Rpc = nullptr;
        FIntProperty* RpcSerial = nullptr;
        FIntProperty* RpcAction = nullptr;
        int32 InitialGeneration = 0;
        int32 InitialSignalUpdates = 0;
        int32 InitialClientSignalUpdates[2] = {INDEX_NONE, INDEX_NONE};
        int32 InitialClientSequences[2] = {INDEX_NONE, INDEX_NONE};
        int32 SelectedClientSequences[2] = {INDEX_NONE, INDEX_NONE};
        int32 SelectedSequence = 0;
        int32 DepartureSequence = 0;
        int32 FailureSequence = 0;
        int32 HistoryGeneration = INDEX_NONE;
        double NextDiagnostic = 0.0;
        TArray<FObserved> History;
        TArray<FCk_Handle> InitialScripts;
        TArray<FCk_Handle> InitialTimers;
        TArray<FCk_Handle> BeforeResetScripts;
        TArray<FCk_Handle> BeforeResetTimers;
        TArray<FCk_Handle> BeforeFailureScripts;
        TArray<FCk_Handle> BeforeFailureTimers;
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
    auto SnapshotOf(AGameStateBase* InState, const FFixture& InFixture) -> const void*
    {
        return IsValid(InState) ? InFixture.Snapshot->ContainerPtrToValuePtr<void>(InState) : nullptr;
    }
    auto GenerationOf(AGameStateBase* InState, const FFixture& InFixture) -> int32
    {
        const auto* Snapshot = SnapshotOf(InState, InFixture);
        return Snapshot ? InFixture.Generation->GetPropertyValue_InContainer(Snapshot) : INDEX_NONE;
    }
    auto SequenceOf(AGameStateBase* InState, const FFixture& InFixture) -> int32
    {
        const auto* Snapshot = SnapshotOf(InState, InFixture);
        return Snapshot ? InFixture.Sequence->GetPropertyValue_InContainer(Snapshot) : INDEX_NONE;
    }
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
    auto CountTimers(const FCk_Handle& InRoot, TArray<FCk_Handle>* OutTimers = nullptr) -> int32
    {
        if (!IsLive(InRoot)) { return 0; }
        int32 Count = 0;
        if (UCk_Utils_Timer_UE::Has(InRoot))
        {
            ++Count;
            if (OutTimers) { OutTimers->AddUnique(InRoot); }
        }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(InRoot))
        { Count += CountTimers(Child, OutTimers); }
        return Count;
    }
    auto GetChildren(UWorld* InWorld, int32 InIndex, TArray<FCk_Handle>& OutChildren) -> bool
    {
        OutChildren.Reset();
        const auto Root = IsValid(InWorld)
            ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld) : FCk_Handle{};
        if (!IsLive(Root)) { return false; }
        const auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, Stations[InIndex].Tag);
        if (Anchors.Num() != 1 || !IsLive(Anchors[0])) { return false; }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
        {
            if (!IsLive(Child) || !UCk_Utils_EntityScript_UE::Has(Child)) { continue; }
            const auto Script = UCk_Utils_EntityScript_UE::CastChecked(Child);
            const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
            if (!IsValid(Class) || Class->GetPathName() != Stations[InIndex].ScriptPath)
            { return false; }
            OutChildren.Add(Child);
        }
        return true;
    }
    auto CheckBranches(UWorld* InWorld, bool InServer, TArray<FCk_Handle>* OutScripts = nullptr,
                       TArray<FCk_Handle>* OutTimers = nullptr) -> bool
    {
        for (int32 Index = 0; Index < 5; ++Index)
        {
            TArray<FCk_Handle> Children;
            if (!GetChildren(InWorld, Index, Children) || Children.Num() != 1) { return false; }
            if (OutScripts) { OutScripts->Add(Children[0]); }
            const auto Expected = InServer ? Stations[Index].AuthorityTimers : 1;
            if (CountTimers(Children[0], OutTimers) != Expected) { return false; }
        }
        return true;
    }
    auto NoBranches(UWorld* InWorld) -> bool
    {
        for (int32 Index = 0; Index < 5; ++Index)
        {
            TArray<FCk_Handle> Children;
            if (!GetChildren(InWorld, Index, Children) || !Children.IsEmpty()) { return false; }
        }
        return true;
    }
    auto ReadIntArray(const FArrayProperty* InProperty, const void* InSample,
                      TArray<int32>& Out) -> bool
    {
        FScriptArrayHelper Array(InProperty, InProperty->ContainerPtrToValuePtr<void>(InSample));
        Out.Reset();
        if (Array.Num() > 16) { return false; }
        const auto* Inner = CastField<FIntProperty>(InProperty->Inner);
        if (!Inner) { return false; }
        for (int32 Index = 0; Index < Array.Num(); ++Index)
        { Out.Add(Inner->GetPropertyValue(Array.GetRawPtr(Index))); }
        return true;
    }
    auto ValidateSample(const void* InSample, const FFixture& InFixture, int32 InIndex,
                        int32* OutEpoch = nullptr, TArray<double>* OutElapsed = nullptr,
                        TArray<int32>* OutCounters = nullptr) -> bool
    {
        const auto Epoch = InFixture.Epoch->GetPropertyValue_InContainer(InSample);
        if (Epoch <= 0 || InFixture.Body->GetPropertyValue_InContainer(InSample).Len() > 4096)
        { return false; }
        FScriptArrayHelper Elapsed(InFixture.Elapsed,
            InFixture.Elapsed->ContainerPtrToValuePtr<void>(InSample));
        FScriptArrayHelper Goal(InFixture.Goal,
            InFixture.Goal->ContainerPtrToValuePtr<void>(InSample));
        TArray<int32> States, Counters;
        if (!ReadIntArray(InFixture.TimerStates, InSample, States) ||
            !ReadIntArray(InFixture.Counters, InSample, Counters) ||
            Elapsed.Num() != Stations[InIndex].DemoTimers ||
            Goal.Num() != Stations[InIndex].DemoTimers ||
            States.Num() != Stations[InIndex].DemoTimers ||
            Counters.Num() != Stations[InIndex].Counters)
        { return false; }
        for (int32 Index = 0; Index < Elapsed.Num(); ++Index)
        {
            const auto Value = CastField<FNumericProperty>(InFixture.Elapsed->Inner)->GetFloatingPointPropertyValue(
                Elapsed.GetRawPtr(Index));
            const auto Target = CastField<FNumericProperty>(InFixture.Goal->Inner)->GetFloatingPointPropertyValue(
                Goal.GetRawPtr(Index));
            if (!FMath::IsFinite(Value) || !FMath::IsFinite(Target) ||
                Value < 0.0 || Value >= 1000000.0 ||
                Target < 0.0 || Target >= 1000000.0 ||
                (States[Index] != static_cast<int32>(ECk_Timer_State::Paused) &&
                 States[Index] != static_cast<int32>(ECk_Timer_State::Running)))
            { return false; }
            if (OutElapsed) { OutElapsed->Add(Value); }
        }
        for (const auto Count : Counters) { if (Count < 0) { return false; } }
        if (OutEpoch) { *OutEpoch = Epoch; }
        if (OutCounters) { OutCounters->Append(Counters); }
        return true;
    }
    auto SamplesValid(AGameStateBase* InState, const FFixture& InFixture,
                      TArray<int32>* OutEpochs = nullptr,
                      TArray<double>* OutElapsed = nullptr,
                      TArray<int32>* OutCounters = nullptr) -> bool
    {
        const auto* Snapshot = SnapshotOf(InState, InFixture);
        if (!Snapshot) { return false; }
        FScriptArrayHelper Samples(InFixture.Samples,
            InFixture.Samples->ContainerPtrToValuePtr<void>(Snapshot));
        if (Samples.Num() != 5) { return false; }
        if (OutEpochs) { OutEpochs->Reset(); }
        if (OutElapsed) { OutElapsed->Reset(); }
        if (OutCounters) { OutCounters->Reset(); }
        for (int32 Index = 0; Index < 5; ++Index)
        {
            int32 Epoch = 0;
            if (!ValidateSample(Samples.GetRawPtr(Index), InFixture, Index, &Epoch,
                OutElapsed, OutCounters)) { return false; }
            if (OutEpochs) { OutEpochs->Add(Epoch); }
        }
        return true;
    }
    auto IsReady(AGameStateBase* InState, const FFixture& InFixture, int32 InGeneration) -> bool
    {
        const auto* Snapshot = SnapshotOf(InState, InFixture);
        return Snapshot && GenerationOf(InState, InFixture) == InGeneration &&
            SequenceOf(InState, InFixture) > 0 &&
            InFixture.Ready->GetPropertyValue_InContainer(Snapshot) &&
            !InFixture.Failed->GetPropertyValue_InContainer(Snapshot) &&
            SamplesValid(InState, InFixture);
    }
    auto IsFailed(AGameStateBase* InState, const FFixture& InFixture, int32 InGeneration) -> bool
    {
        const auto* Snapshot = SnapshotOf(InState, InFixture);
        return Snapshot && GenerationOf(InState, InFixture) == InGeneration &&
            !InFixture.Ready->GetPropertyValue_InContainer(Snapshot) &&
            InFixture.Failed->GetPropertyValue_InContainer(Snapshot);
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
        const auto Index = Server->Find_GymIndexByName(TEXT("Timer"));
        return Index != INDEX_NONE && Server->Get_CurrentGymIndex() == Index &&
            Client->Get_CurrentGymIndex() == Index &&
            Client->Get_Entries().Num() == Server->Get_Entries().Num() &&
            Client->Get_SuppressHUDDuringStartup() == Server->Get_SuppressHUDDuringStartup();
    }
    auto WorldReady(UWorld* InWorld, const FFixture& InFixture,
                    int32 InGeneration, bool InServer) -> bool
    {
        auto* State = GetState(InWorld, InFixture);
        if (!IsReady(State, InFixture, InGeneration) ||
            !CheckBranches(InWorld, InServer) ||
            (InServer && InFixture.ServerQuintetCount->GetPropertyValue_InContainer(State) != 5) ||
            (!InServer && (InFixture.LocalGeneration->GetPropertyValue_InContainer(State) != InGeneration ||
                !InFixture.LocalReady->GetPropertyValue_InContainer(State) ||
                InFixture.LocalFailed->GetPropertyValue_InContainer(State))))
        { return false; }
        if (InServer)
        {
            FScriptArrayHelper Scripts(InFixture.ServerScripts,
                InFixture.ServerScripts->ContainerPtrToValuePtr<void>(State));
            if (Scripts.Num() != 5) { return false; }
            for (int32 Index = 0; Index < 5; ++Index)
            {
                TArray<FCk_Handle> Children;
                if (!GetChildren(InWorld, Index, Children) || Children.Num() != 1 ||
                    *reinterpret_cast<const FCk_Handle*>(Scripts.GetRawPtr(Index)) != Children[0])
                { return false; }
            }
        }
        return true;
    }
    auto AllReady(FFixture& InFixture, int32 InGeneration, bool InSurvivorOnly = false) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* Client0 = InSurvivorOnly ? nullptr : ck::auto_test::net::Get_ClientWorld(0);
        auto* Client1 = InSurvivorOnly
            ? (InFixture.SurvivorInstance.IsValid() ? InFixture.SurvivorInstance->GetWorld() : nullptr)
            : ck::auto_test::net::Get_ClientWorld(1);
        if ((!InSurvivorOnly && ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) ||
            !WorldReady(Server, InFixture, InGeneration, true) ||
            (!InSurvivorOnly && !WorldReady(Client0, InFixture, InGeneration, false)) ||
            !WorldReady(Client1, InFixture, InGeneration, false) ||
            (!InSurvivorOnly && !CatalogsMatch(Server, Client0)) ||
            !CatalogsMatch(Server, Client1))
        { return false; }
        auto* ServerState = GetState(Server, InFixture);
        if (InFixture.HistoryGeneration != InGeneration)
        {
            InFixture.History.Reset();
            InFixture.HistoryGeneration = InGeneration;
        }
        const auto Sequence = SequenceOf(ServerState, InFixture);
        if (InFixture.History.IsEmpty() || InFixture.History.Last().Sequence != Sequence)
        {
            if (InFixture.History.Num() >= 32) { InFixture.History.RemoveAt(0); }
            auto& Item = InFixture.History.AddDefaulted_GetRef();
            Item.Generation = InGeneration;
            Item.Sequence = Sequence;
            Item.Copy = MakeShared<FStructOnScope>(InFixture.Snapshot->Struct.Get());
            InFixture.Snapshot->Struct->CopyScriptStruct(
                Item.Copy->GetStructMemory(), SnapshotOf(ServerState, InFixture));
        }
        for (auto* Client : {Client0, Client1})
        {
            if (!Client) { continue; }
            auto* State = GetState(Client, InFixture);
            bool Matched = false;
            for (const auto& Published : InFixture.History)
            {
                if (Published.Generation == InGeneration &&
                    Published.Sequence == SequenceOf(State, InFixture) &&
                    Published.Copy.IsValid() &&
                    InFixture.Snapshot->Identical(SnapshotOf(State, InFixture),
                        Published.Copy->GetStructMemory(), 0))
                { Matched = true; break; }
            }
            if (!Matched) { return false; }
        }
        return true;
    }
    auto Invoke(APlayerController* InPC, const FFixture& InFixture,
                int32 InSerial, int32 InAction) -> bool
    {
        if (!IsValid(InPC) || !InPC->IsA(InFixture.Controller)) { return false; }
        FStructOnScope Params(InFixture.Rpc);
        auto* Memory = Params.GetStructMemory();
        InFixture.RpcSerial->SetPropertyValue_InContainer(Memory, InSerial);
        InFixture.RpcAction->SetPropertyValue_InContainer(Memory, InAction);
        InPC->ProcessEvent(InFixture.Rpc, Memory);
        return true;
    }
    auto Describe(UWorld* InWorld, const FFixture& InFixture) -> FString
    {
        auto* State = GetState(InWorld, InFixture);
        const auto* Snapshot = SnapshotOf(State, InFixture);
        if (!Snapshot) { return FString::Printf(TEXT("Timer world=%s state=missing"), *GetNameSafe(InWorld)); }
        FString Branches;
        for (int32 Index = 0; Index < 5; ++Index)
        {
            TArray<FCk_Handle> Children;
            const auto Found = GetChildren(InWorld, Index, Children);
            const auto Count = Children.Num() == 1 ? CountTimers(Children[0]) : INDEX_NONE;
            Branches += FString::Printf(TEXT(" [%d anchor=%d children=%d timers=%d]"),
                Index, Found, Children.Num(), Count);
        }
        return FString::Printf(TEXT("Timer world=%s gen=%d seq=%d ready=%d failed=%d reason='%s' samples=%d quintet=%d local=%d/%d/%d reason='%s'%s"),
            *GetNameSafe(InWorld), GenerationOf(State, InFixture), SequenceOf(State, InFixture),
            InFixture.Ready->GetPropertyValue_InContainer(Snapshot),
            InFixture.Failed->GetPropertyValue_InContainer(Snapshot),
            *InFixture.FailureReason->GetPropertyValue_InContainer(Snapshot),
            SamplesValid(State, InFixture),
            InFixture.ServerQuintetCount->GetPropertyValue_InContainer(State),
            InFixture.LocalGeneration->GetPropertyValue_InContainer(State),
            InFixture.LocalReady->GetPropertyValue_InContainer(State),
            InFixture.LocalFailed->GetPropertyValue_InContainer(State),
            *InFixture.LocalFailureReason->GetPropertyValue_InContainer(State), *Branches);
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkTimerGym_Network,
    "Ck.Gym.Network.Timer.SharedQuintet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkTimerGym_Network::RunTest(const FString& Parameters)
{
    using namespace ck_timer_gym_network_test;
    bool bDedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, bDedicated))
    { AddError(TEXT("unsupported Timer network topology")); return false; }
    const auto Fixture = MakeShared<FFixture>();
    Fixture->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    Fixture->State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    Fixture->Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    bool Schema = true;
    Schema &= TestNotNull(TEXT("Timer GameMode"), Fixture->Mode);
    Schema &= TestNotNull(TEXT("Timer GameState"), Fixture->State);
    Schema &= TestNotNull(TEXT("Timer PlayerController"), Fixture->Controller);
    if (!Schema) { return false; }
    Fixture->Snapshot = FindFProperty<FStructProperty>(Fixture->State, TEXT("Snapshot"));
    if (!TestNotNull(TEXT("replicated Snapshot"), Fixture->Snapshot)) { return false; }
    auto* Snap = Fixture->Snapshot->Struct.Get();
    Fixture->Generation = FindFProperty<FIntProperty>(Snap, TEXT("Generation"));
    Fixture->Sequence = FindFProperty<FIntProperty>(Snap, TEXT("Sequence"));
    Fixture->Ready = FindFProperty<FBoolProperty>(Snap, TEXT("Ready"));
    Fixture->Failed = FindFProperty<FBoolProperty>(Snap, TEXT("Failed"));
    Fixture->FailureReason = FindFProperty<FStrProperty>(Snap, TEXT("FailureReason"));
    Fixture->Samples = FindFProperty<FArrayProperty>(Snap, TEXT("Stations"));
    Schema &= TestNotNull(TEXT("Snapshot.Stations"), Fixture->Samples);
    if (!Schema) { return false; }
    Fixture->SampleItem = CastField<FStructProperty>(Fixture->Samples->Inner);
    Schema &= TestNotNull(TEXT("Station sample struct"), Fixture->SampleItem);
    if (!Schema) { return false; }
    auto* Sample = Fixture->SampleItem->Struct.Get();
    Fixture->Epoch = FindFProperty<FIntProperty>(Sample, TEXT("SlotEpoch"));
    Fixture->Elapsed = FindFProperty<FArrayProperty>(Sample, TEXT("ElapsedSeconds"));
    Fixture->Goal = FindFProperty<FArrayProperty>(Sample, TEXT("GoalSeconds"));
    Fixture->TimerStates = FindFProperty<FArrayProperty>(Sample, TEXT("TimerStates"));
    Fixture->Counters = FindFProperty<FArrayProperty>(Sample, TEXT("Counters"));
    Fixture->Body = FindFProperty<FStrProperty>(Sample, TEXT("Body"));
    Fixture->ServerQuintetCount = FindFProperty<FIntProperty>(Fixture->State, TEXT("ServerQuintetCount"));
    Fixture->ServerScripts = FindFProperty<FArrayProperty>(
        Fixture->State, TEXT("ServerScripts"));
    Fixture->LocalGeneration = FindFProperty<FIntProperty>(Fixture->State, TEXT("LocalProjectionGeneration"));
    Fixture->LocalReady = FindFProperty<FBoolProperty>(Fixture->State, TEXT("LocalProjectionReady"));
    Fixture->LocalFailed = FindFProperty<FBoolProperty>(Fixture->State, TEXT("LocalProjectionFailed"));
    Fixture->LocalFailureReason = FindFProperty<FStrProperty>(Fixture->State, TEXT("LocalProjectionFailureReason"));
    Fixture->LastSerial = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastControlSerial"));
    Fixture->AttemptSerial = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastControlAttemptSerial"));
    Fixture->Result = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastControlResult"));
    Fixture->Rpc = Fixture->Controller->FindFunctionByName(TEXT("Server_RequestTimerControl"));
    Schema &= TestNotNull(TEXT("Snapshot.Generation"), Fixture->Generation);
    Schema &= TestNotNull(TEXT("Snapshot.Sequence"), Fixture->Sequence);
    Schema &= TestNotNull(TEXT("Snapshot.Ready"), Fixture->Ready);
    Schema &= TestNotNull(TEXT("Snapshot.Failed"), Fixture->Failed);
    Schema &= TestNotNull(TEXT("Snapshot.FailureReason"), Fixture->FailureReason);
    Schema &= TestNotNull(TEXT("Station.SlotEpoch"), Fixture->Epoch);
    Schema &= TestNotNull(TEXT("Station.ElapsedSeconds"), Fixture->Elapsed);
    Schema &= TestNotNull(TEXT("Station.GoalSeconds"), Fixture->Goal);
    Schema &= TestNotNull(TEXT("Station.TimerStates"), Fixture->TimerStates);
    Schema &= TestNotNull(TEXT("Station.Counters"), Fixture->Counters);
    Schema &= TestNotNull(TEXT("Station.Body"), Fixture->Body);
    Schema &= TestNotNull(TEXT("GameState.ServerQuintetCount"), Fixture->ServerQuintetCount);
    Schema &= TestNotNull(TEXT("GameState.ServerScripts"), Fixture->ServerScripts);
    Schema &= TestNotNull(TEXT("GameState.LocalProjectionGeneration"), Fixture->LocalGeneration);
    Schema &= TestNotNull(TEXT("GameState.LocalProjectionReady"), Fixture->LocalReady);
    Schema &= TestNotNull(TEXT("GameState.LocalProjectionFailed"), Fixture->LocalFailed);
    Schema &= TestNotNull(TEXT("GameState.LocalProjectionFailureReason"), Fixture->LocalFailureReason);
    Schema &= TestNotNull(TEXT("PlayerController.LastControlSerial"), Fixture->LastSerial);
    Schema &= TestNotNull(TEXT("PlayerController.LastControlAttemptSerial"), Fixture->AttemptSerial);
    Schema &= TestNotNull(TEXT("PlayerController.LastControlResult"), Fixture->Result);
    Schema &= TestNotNull(TEXT("PlayerController RPC"), Fixture->Rpc);
    if (!Schema) { return false; }
    Fixture->RpcSerial = FindFProperty<FIntProperty>(Fixture->Rpc, TEXT("InSerial"));
    Fixture->RpcAction = FindFProperty<FIntProperty>(Fixture->Rpc, TEXT("InAction"));
    Schema &= TestTrue(TEXT("exact Timer snapshot and RPC schema"),
        Fixture->Snapshot->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->Snapshot->Struct->GetName() == TEXT("CkTimerGym_Snapshot") &&
        Fixture->SampleItem->Struct->GetName() == TEXT("CkTimerGym_StationSample") &&
        CastField<FNumericProperty>(Fixture->Elapsed->Inner) &&
        CastField<FNumericProperty>(Fixture->Elapsed->Inner)->IsFloatingPoint() &&
        CastField<FNumericProperty>(Fixture->Goal->Inner) &&
        CastField<FNumericProperty>(Fixture->Goal->Inner)->IsFloatingPoint() &&
        CastField<FIntProperty>(Fixture->TimerStates->Inner) &&
        CastField<FIntProperty>(Fixture->Counters->Inner) &&
        Fixture->LastSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->AttemptSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->Result->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->RpcSerial && Fixture->RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->RpcAction && Fixture->RpcAction->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->Rpc->NumParms == 2 &&
        Fixture->Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer));
    Schema &= TestTrue(TEXT("ordered ServerScripts contains handles"),
        CastField<FStructProperty>(Fixture->ServerScripts->Inner) &&
        CastField<FStructProperty>(Fixture->ServerScripts->Inner)->Struct ==
            FCk_Handle::StaticStruct());
    if (!Schema) { return false; }

    Fixture->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, Fixture->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([this, Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            const auto Gen = GenerationOf(State, *Fixture);
            const auto Ready = Gen > 0 && AllReady(*Fixture, Gen) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1));
            const auto Now = FPlatformTime::Seconds();
            if (!Ready && Now >= Fixture->NextDiagnostic)
            {
                Fixture->NextDiagnostic = Now + 5.0;
                AddInfo(FString::Printf(TEXT("Timer history=%d controllers=%d/%d"),
                    Fixture->History.Num(),
                    IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)),
                    IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1))));
                AddInfo(Describe(Server, *Fixture));
                AddInfo(Describe(ck::auto_test::net::Get_ClientWorld(0), *Fixture));
                AddInfo(Describe(ck::auto_test::net::Get_ClientWorld(1), *Fixture));
            }
            return Ready;
        }), 45.0, TEXT("Timer quintet and two projections become ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture, bDedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            Fixture->InitialGeneration = GenerationOf(State, *Fixture);
            Fixture->SurvivorState = State;
            auto* Survivor = ck::auto_test::net::Get_ClientWorld(1);
            Fixture->SurvivorInstance = IsValid(Survivor) ? Survivor->GetGameInstance() : nullptr;
            Fixture->InitialScripts.Reset();
            Fixture->InitialTimers.Reset();
            TArray<int32> TypedCounters;
            bool ClientsCaptured = true;
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* ClientState = GetState(ck::auto_test::net::Get_ClientWorld(Index), *Fixture);
                TArray<int32> ClientCounters;
                const auto ClientSequence = SequenceOf(ClientState, *Fixture);
                if (ClientSequence <= 0 ||
                    !SamplesValid(ClientState, *Fixture, nullptr, nullptr, &ClientCounters) ||
                    ClientCounters.Num() != 17)
                { ClientsCaptured = false; break; }
                Fixture->InitialClientSequences[Index] = ClientSequence;
                Fixture->InitialClientSignalUpdates[Index] = ClientCounters[4];
            }
            return TestTrue(TEXT("one server quintet, exact 3/5/3/3/6 timer descendants and two projections"),
                IsValid(Server) && IsValid(State) && IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(Fixture->Mode) &&
                Server->GetNetMode() == (bDedicated ? NM_DedicatedServer : NM_ListenServer) &&
                Fixture->InitialGeneration > 0 &&
                CheckBranches(Server, true, &Fixture->InitialScripts, &Fixture->InitialTimers) &&
                Fixture->InitialScripts.Num() == 5 && Fixture->InitialTimers.Num() == 20 &&
                SamplesValid(State, *Fixture, nullptr, nullptr, &TypedCounters) &&
                TypedCounters.Num() == 17 &&
                (Fixture->InitialSignalUpdates = TypedCounters[4]) >= 0 &&
                ClientsCaptured &&
                AllReady(*Fixture, Fixture->InitialGeneration) &&
                IsValid(Fixture->SurvivorInstance.Get()));
        }), TEXT("capture original authority timer identities")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            TArray<int32> TypedCounters;
            if (!SamplesValid(State, *Fixture, nullptr, nullptr, &TypedCounters) ||
                TypedCounters.Num() != 17 ||
                TypedCounters[4] <= Fixture->InitialSignalUpdates ||
                !AllReady(*Fixture, Fixture->InitialGeneration)) { return false; }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* ClientState = GetState(ck::auto_test::net::Get_ClientWorld(Index), *Fixture);
                TArray<int32> ClientCounters;
                if (SequenceOf(ClientState, *Fixture) <= Fixture->InitialClientSequences[Index] ||
                    !SamplesValid(ClientState, *Fixture, nullptr, nullptr, &ClientCounters) ||
                    ClientCounters.Num() != 17 ||
                    ClientCounters[4] <= Fixture->InitialClientSignalUpdates[Index])
                { return false; }
            }
            return true;
        }), 12.0, TEXT("authoritative Signals update counter advances with coherent client snapshots")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InWorld)
        { TestTrue(TEXT("malformed serial invoked from owner"), Invoke(
            IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr, *Fixture, 0, 0)); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            return IsValid(PC) && PC->IsA(Fixture->Controller) &&
                Fixture->AttemptSerial->GetPropertyValue_InContainer(PC) == 0 &&
                Fixture->Result->GetPropertyValue_InContainer(PC) == 4 &&
                Fixture->LastSerial->GetPropertyValue_InContainer(PC) == 0 &&
                AllReady(*Fixture, Fixture->InitialGeneration) &&
                GetState(Server, *Fixture) == Fixture->SurvivorState.Get() &&
                CheckBranches(Server, true);
        }), 15.0, TEXT("invalid serial rejected without replacing authority quintet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InWorld)
        { TestTrue(TEXT("unsupported action invoked from owner"), Invoke(
            IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr, *Fixture, 1500, 99)); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
            return IsValid(PC) && PC->IsA(Fixture->Controller) &&
                Fixture->AttemptSerial->GetPropertyValue_InContainer(PC) == 1500 &&
                Fixture->Result->GetPropertyValue_InContainer(PC) == 4 &&
                Fixture->LastSerial->GetPropertyValue_InContainer(PC) == 0 &&
                AllReady(*Fixture, Fixture->InitialGeneration);
        }), 15.0, TEXT("invalid action rejected without changing snapshot generation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InWorld)
        {
            bool Captured = true;
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* ClientState = GetState(ck::auto_test::net::Get_ClientWorld(Index), *Fixture);
                const auto ClientSequence = SequenceOf(ClientState, *Fixture);
                if (ClientSequence <= 0) { Captured = false; break; }
                Fixture->SelectedClientSequences[Index] = ClientSequence;
            }
            TestTrue(TEXT("capture both client sequences before selected rerun"), Captured);
            TestTrue(TEXT("selected rerun invoked from owner"), Invoke(
                IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr, *Fixture, 1501, 2));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            if (!IsValid(PC) || !PC->IsA(Fixture->Controller) ||
                Fixture->AttemptSerial->GetPropertyValue_InContainer(PC) != 1501 ||
                Fixture->LastSerial->GetPropertyValue_InContainer(PC) != 1501 ||
                Fixture->Result->GetPropertyValue_InContainer(PC) != 1 ||
                !AllReady(*Fixture, Fixture->InitialGeneration)) { return false; }
            TArray<int32> Epochs;
            if (!SamplesValid(State, *Fixture, &Epochs) || Epochs.Num() != 5 ||
                Epochs[2] != 2 || Fixture->InitialScripts.Num() != 5) { return false; }
            for (int32 ClientIndex = 0; ClientIndex < 2; ++ClientIndex)
            {
                auto* ClientState = GetState(ck::auto_test::net::Get_ClientWorld(ClientIndex), *Fixture);
                TArray<int32> ClientEpochs;
                if (SequenceOf(ClientState, *Fixture) <=
                        Fixture->SelectedClientSequences[ClientIndex] ||
                    !SamplesValid(ClientState, *Fixture, &ClientEpochs) ||
                    ClientEpochs.Num() != 5) { return false; }
                for (int32 Index = 0; Index < 5; ++Index)
                { if (ClientEpochs[Index] != (Index == 2 ? 2 : 1)) { return false; } }
            }
            FScriptArrayHelper Scripts(Fixture->ServerScripts,
                Fixture->ServerScripts->ContainerPtrToValuePtr<void>(State));
            if (Scripts.Num() != 5) { return false; }
            for (int32 Index = 0; Index < 5; ++Index)
            {
                if (Index != 2 && (Epochs[Index] != 1 ||
                    *reinterpret_cast<const FCk_Handle*>(Scripts.GetRawPtr(Index)) !=
                        Fixture->InitialScripts[Index])) { return false; }
            }
            TArray<FCk_Handle> Children;
            return GetChildren(Server, 2, Children) && Children.Num() == 1 &&
                Children[0] != Fixture->InitialScripts[2] &&
                !ck::IsValid(Fixture->InitialScripts[2]) &&
                CheckBranches(Server, true);
        }), 45.0, TEXT("selected station alone reruns and all worlds receive coherent samples")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            Fixture->SelectedSequence = SequenceOf(State, *Fixture);
            TArray<FCk_Handle> NewScripts;
            bool Retired = CheckBranches(
                ck::auto_test::net::Get_ServerWorld(), true, &NewScripts);
            for (int32 Index = 0; Index < 5 && Retired; ++Index)
            {
                if (Index != 2) { Retired = NewScripts[Index] == Fixture->InitialScripts[Index]; }
            }
            for (int32 Index = 0; Index < Fixture->InitialTimers.Num() && Retired; ++Index)
            { Retired = (Index >= 8 && Index < 11)
                ? !ck::IsValid(Fixture->InitialTimers[Index])
                : ck::IsValid(Fixture->InitialTimers[Index]); }
            return TestTrue(TEXT("four sibling roots retain identity and selected root retires"),
                Retired && Fixture->SelectedSequence > 0);
        }), TEXT("capture selected rerun sequence")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InWorld)
        { TestTrue(TEXT("stale serial invoked from owner"), Invoke(
            IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr, *Fixture, 1501, 5)); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            TArray<int32> Epochs;
            return IsValid(PC) && PC->IsA(Fixture->Controller) &&
                Fixture->AttemptSerial->GetPropertyValue_InContainer(PC) == 1501 &&
                Fixture->Result->GetPropertyValue_InContainer(PC) == 4 &&
                Fixture->LastSerial->GetPropertyValue_InContainer(PC) == 1501 &&
                IsReady(State, *Fixture, Fixture->InitialGeneration) &&
                SamplesValid(State, *Fixture, &Epochs) && Epochs.Num() == 5 &&
                Epochs[2] == 2 &&
                AllReady(*Fixture, Fixture->InitialGeneration);
        }), 15.0, TEXT("replayed serial rejected with no generation or slot change")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InWorld)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            TestTrue(TEXT("capture complete pre-reset branch"),
                CheckBranches(Server, true, &Fixture->BeforeResetScripts,
                    &Fixture->BeforeResetTimers) &&
                Fixture->BeforeResetScripts.Num() == 5 &&
                Fixture->BeforeResetTimers.Num() == 20);
            TestTrue(TEXT("full R invoked from owner"), Invoke(
                IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr, *Fixture, 1502, 5));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            if (!IsValid(PC) || !PC->IsA(Fixture->Controller) ||
                Fixture->AttemptSerial->GetPropertyValue_InContainer(PC) != 1502 ||
                Fixture->LastSerial->GetPropertyValue_InContainer(PC) != 1502 ||
                Fixture->Result->GetPropertyValue_InContainer(PC) != 1 ||
                !AllReady(*Fixture, Fixture->InitialGeneration + 1) ||
                Fixture->BeforeResetScripts.Num() != 5 ||
                Fixture->BeforeResetTimers.Num() != 20) { return false; }
            TArray<int32> Epochs;
            if (!SamplesValid(State, *Fixture, &Epochs) || Epochs.Num() != 5) { return false; }
            for (int32 Index = 0; Index < 5; ++Index)
            {
                if (Epochs[Index] != 1 || ck::IsValid(Fixture->BeforeResetScripts[Index]))
                { return false; }
            }
            for (const auto& Timer : Fixture->BeforeResetTimers)
            { if (ck::IsValid(Timer)) { return false; } }
            return CheckBranches(Server, true);
        }), 45.0, TEXT("full reset retires all old timers before new generation is ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* RemotePC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* LocalPC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            auto* Instance = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
            if (!TestTrue(TEXT("departing owner exists"),
                IsValid(RemotePC) && IsValid(LocalPC) && IsValid(Instance))) { return; }
            Fixture->DepartingServerController = RemotePC;
            Fixture->DepartingInstance = Instance;
            Fixture->DepartureSequence = SequenceOf(Fixture->SurvivorState.Get(), *Fixture);
            LocalPC->ClientTravel(TEXT("/Engine/Maps/Entry"), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* SurvivorWorld = Fixture->SurvivorInstance.IsValid()
                ? Fixture->SurvivorInstance->GetWorld() : nullptr;
            auto* DepartedWorld = Fixture->DepartingInstance.IsValid()
                ? Fixture->DepartingInstance->GetWorld() : nullptr;
            return State == Fixture->SurvivorState.Get() &&
                !Fixture->DepartingServerController.IsValid() &&
                IsValid(DepartedWorld) && DepartedWorld != Server &&
                DepartedWorld->GetMapName().Contains(TEXT("Entry")) &&
                AllReady(*Fixture, Fixture->InitialGeneration + 1, true) &&
                SequenceOf(State, *Fixture) > Fixture->DepartureSequence &&
                SequenceOf(GetState(SurvivorWorld, *Fixture), *Fixture) > Fixture->DepartureSequence &&
                CheckBranches(Server, true);
        }), 45.0, TEXT("real owner departure leaves one progressing server quintet and survivor projection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InServer)
        {
            auto* State = GetState(InServer, *Fixture);
            Fixture->BeforeFailureScripts.Reset();
            Fixture->BeforeFailureTimers.Reset();
            if (!TestTrue(TEXT("failure probe starts with complete quintet"),
                State == Fixture->SurvivorState.Get() &&
                CheckBranches(InServer, true, &Fixture->BeforeFailureScripts,
                    &Fixture->BeforeFailureTimers) &&
                Fixture->BeforeFailureScripts.Num() == 5 &&
                Fixture->BeforeFailureTimers.Num() == 20)) { return; }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Fixture->BeforeFailureScripts[0]);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* SurvivorWorld = Fixture->SurvivorInstance.IsValid()
                ? Fixture->SurvivorInstance->GetWorld() : nullptr;
            auto* SurvivorState = GetState(SurvivorWorld, *Fixture);
            if (!IsFailed(State, *Fixture, Fixture->InitialGeneration + 1) ||
                !IsFailed(SurvivorState, *Fixture, Fixture->InitialGeneration + 1) ||
                Fixture->ServerQuintetCount->GetPropertyValue_InContainer(State) != 0 ||
                !NoBranches(Server) ||
                !Fixture->LocalFailed->GetPropertyValue_InContainer(SurvivorState) ||
                Fixture->LocalReady->GetPropertyValue_InContainer(SurvivorState))
            { return false; }
            FScriptArrayHelper Scripts(Fixture->ServerScripts,
                Fixture->ServerScripts->ContainerPtrToValuePtr<void>(State));
            // The ordered five-slot contract remains after retirement; every
            // reflected slot must be cleared, with no live subtree left behind.
            if (Scripts.Num() != 5) { return false; }
            for (int32 Index = 0; Index < Scripts.Num(); ++Index)
            {
                const auto& Handle = *reinterpret_cast<const FCk_Handle*>(Scripts.GetRawPtr(Index));
                if (ck::IsValid(Handle) || Handle != FCk_Handle{}) { return false; }
            }
            for (const auto& Script : Fixture->BeforeFailureScripts)
            { if (ck::IsValid(Script)) { return false; } }
            for (const auto& Timer : Fixture->BeforeFailureTimers)
            { if (ck::IsValid(Timer)) { return false; } }
            return true;
        }), 25.0, TEXT("one child loss retires and fails entire generation and local projection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            Fixture->FailureSequence = SequenceOf(
                GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture), *Fixture);
            return TestTrue(TEXT("failure snapshot sequence captured"), Fixture->FailureSequence > 0);
        }), TEXT("capture terminal failure sequence")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            return TestTrue(TEXT("failure remains terminal"),
                State == Fixture->SurvivorState.Get() &&
                IsFailed(State, *Fixture, Fixture->InitialGeneration + 1) &&
                SequenceOf(State, *Fixture) == Fixture->FailureSequence &&
                Fixture->ServerQuintetCount->GetPropertyValue_InContainer(State) == 0 &&
                NoBranches(Server));
        }), TEXT("atomic failure does not republish a partial quintet")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            Fixture->RestoreSettings();
            return TestTrue(TEXT("Timer PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore original startup preferences"), true));
    return true;
}

#endif
