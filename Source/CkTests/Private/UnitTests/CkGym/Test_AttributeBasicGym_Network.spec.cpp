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
#include "CkAttribute/FloatAttribute/CkFloatAttribute_Utils.h"
#include "CkAttribute/ByteAttribute/CkByteAttribute_Utils.h"
#include "CkAttribute/VectorAttribute/CkVectorAttribute_Utils.h"
#include "CkTimer/CkTimer_Utils.h"

#include <HAL/PlatformTime.h>
#include <limits>
#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_attribute_basic_network_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_AttributeGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_AttributeGym_GameState");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_AttributeGym_PlayerController");
    constexpr auto OriginalPath = TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_BasicAttributes");
    constexpr auto ProjectionPath = TEXT("/Script/Angelscript.Ck_EntityScript_AttributeGym_BasicProjection");
    const FName AnchorTag{TEXT("Gym.Attribute.BasicAttributes")};

    struct FValues
    {
        double Health = 0.0;
        int32 Armor = 0;
        FVector Velocity = FVector::ZeroVector;
        int32 HealthChanges = 0;
        int32 ArmorChanges = 0;
        int32 VelocityChanges = 0;
        int32 Clamps = 0;
        int32 AutoStep = 0;
        bool AutoRunning = false;
    };
    struct FFeatures { int32 Health = 0; int32 Armor = 0; int32 Velocity = 0; int32 Timers = 0; };
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
        FStructProperty* Sample = nullptr;
        FNumericProperty* Health = nullptr;
        FIntProperty* Armor = nullptr;
        FStructProperty* Velocity = nullptr;
        FIntProperty* HealthChanges = nullptr;
        FIntProperty* ArmorChanges = nullptr;
        FIntProperty* VelocityChanges = nullptr;
        FIntProperty* Clamps = nullptr;
        FIntProperty* AutoStep = nullptr;
        FBoolProperty* AutoRunning = nullptr;
        FStructProperty* ServerBasic = nullptr;
        FIntProperty* ServerChildCount = nullptr;
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
        FIntProperty* RpcPreset = nullptr;
        FStructProperty* RpcVelocity = nullptr;
        FBoolProperty* RpcAuto = nullptr;
        int32 InitialGeneration = 0;
        int32 DepartureSequence = 0;
        int32 DepartureAutoStep = 0;
        int32 FailureSequence = 0;
        FValues Expected;
        int32 ExpectedCounter = -1;
        bool ExpectedReset = false;
        int32 BeforeServerSequence = INDEX_NONE;
        int32 BeforeSequence[2] = {INDEX_NONE, INDEX_NONE};
        TArray<FObserved> ResetHistory;
        bool ResetServerSeen = false;
        bool ResetSeen[2] = {false, false};
        double PauseSettledSince = 0.0;
        int32 PauseStep = INDEX_NONE;
        FValues Before;
        FCk_Handle InitialServerBasic;
        TArray<FCk_Handle> InitialSimulation;
        TArray<FCk_Handle> BeforeFailureSimulation;
        TArray<FCk_Handle> BeforeFailureHostProjection;
        TArray<FCk_Handle> BeforeFailureSurvivorProjection;
        TWeakObjectPtr<AGameStateBase> ServerState;
        TWeakObjectPtr<UGameInstance> SurvivorInstance;
        TWeakObjectPtr<UGameInstance> DepartingInstance;
        TWeakObjectPtr<APlayerController> DepartingServerPC;
        int32 HistoryGeneration = INDEX_NONE;
        TArray<FObserved> History;
        double NextDiagnostic = 0.0;
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
    auto ReadSnapshotValues(const void* Snapshot, const FFixture& InFixture, FValues& Out) -> bool
    {
        if (!Snapshot) { return false; }
        const auto* Sample = InFixture.Sample->ContainerPtrToValuePtr<void>(Snapshot);
        Out.Health = InFixture.Health->GetFloatingPointPropertyValue(
            InFixture.Health->ContainerPtrToValuePtr<void>(Sample));
        Out.Armor = InFixture.Armor->GetPropertyValue_InContainer(Sample);
        Out.Velocity = *static_cast<const FVector*>(
            InFixture.Velocity->ContainerPtrToValuePtr<void>(Sample));
        Out.HealthChanges = InFixture.HealthChanges->GetPropertyValue_InContainer(Sample);
        Out.ArmorChanges = InFixture.ArmorChanges->GetPropertyValue_InContainer(Sample);
        Out.VelocityChanges = InFixture.VelocityChanges->GetPropertyValue_InContainer(Sample);
        Out.Clamps = InFixture.Clamps->GetPropertyValue_InContainer(Sample);
        Out.AutoStep = InFixture.AutoStep->GetPropertyValue_InContainer(Sample);
        Out.AutoRunning = InFixture.AutoRunning->GetPropertyValue_InContainer(Sample);
        return FMath::IsFinite(Out.Health) && Out.Health >= 0.0 && Out.Health <= 100.0 &&
            Out.Armor >= 0 && Out.Armor <= 255 &&
            FMath::IsFinite(Out.Velocity.X) && FMath::IsFinite(Out.Velocity.Y) &&
            FMath::IsFinite(Out.Velocity.Z) &&
            Out.HealthChanges >= 0 && Out.ArmorChanges >= 0 && Out.VelocityChanges >= 0 &&
            Out.Clamps >= 0 && Out.AutoStep >= 0;
    }
    auto ReadValues(AGameStateBase* InState, const FFixture& InFixture, FValues& Out) -> bool
    { return ReadSnapshotValues(SnapshotOf(InState, InFixture), InFixture, Out); }
    auto GetChildren(UWorld* InWorld, TArray<FCk_Handle>& OutOriginal,
                     TArray<FCk_Handle>& OutProjection) -> bool
    {
        OutOriginal.Reset(); OutProjection.Reset();
        const auto Root = IsValid(InWorld)
            ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld) : FCk_Handle{};
        if (!IsLive(Root)) { return false; }
        const auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, AnchorTag);
        if (Anchors.Num() != 1 || !IsLive(Anchors[0])) { return false; }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
        {
            if (!IsLive(Child) || !UCk_Utils_EntityScript_UE::Has(Child)) { continue; }
            const auto Script = UCk_Utils_EntityScript_UE::CastChecked(Child);
            const auto* Class = UCk_Utils_EntityScript_UE::Get_ScriptClass(Script).Get();
            if (!IsValid(Class)) { return false; }
            if (Class->GetPathName() == OriginalPath) { OutOriginal.Add(Child); }
            else if (Class->GetPathName() == ProjectionPath) { OutProjection.Add(Child); }
            else { return false; }
        }
        return true;
    }
    auto CountFeatures(const FCk_Handle& InRoot, FFeatures& Out, TArray<FCk_Handle>* OutHandles = nullptr) -> void
    {
        if (!IsLive(InRoot)) { return; }
        const auto bHealth = UCk_Utils_FloatAttribute_UE::Has(InRoot);
        const auto bArmor = UCk_Utils_ByteAttribute_UE::Has(InRoot);
        const auto bVelocity = UCk_Utils_VectorAttribute_UE::Has(InRoot);
        const auto bTimer = UCk_Utils_Timer_UE::Has(InRoot);
        Out.Health += bHealth ? 1 : 0; Out.Armor += bArmor ? 1 : 0;
        Out.Velocity += bVelocity ? 1 : 0; Out.Timers += bTimer ? 1 : 0;
        if ((bHealth || bArmor || bVelocity || bTimer) && OutHandles) { OutHandles->AddUnique(InRoot); }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(InRoot))
        { CountFeatures(Child, Out, OutHandles); }
    }
    auto Branches(UWorld* InWorld, bool InServer, bool InDedicated,
                  FCk_Handle* OutServerBasic = nullptr,
                  TArray<FCk_Handle>* OutSimulation = nullptr,
                  TArray<FCk_Handle>* OutProjection = nullptr) -> bool
    {
        TArray<FCk_Handle> Original, Projection;
        if (!GetChildren(InWorld, Original, Projection) ||
            Original.Num() != (InServer ? 1 : 0) ||
            Projection.Num() != (InServer && InDedicated ? 0 : 1)) { return false; }
        if (InServer)
        {
            FFeatures Features;
            CountFeatures(Original[0], Features, OutSimulation);
            if (Features.Health != 1 || Features.Armor != 1 ||
                Features.Velocity != 1 || Features.Timers != 1) { return false; }
            if (OutServerBasic) { *OutServerBasic = Original[0]; }
        }
        for (const auto& Child : Projection)
        {
            FFeatures Features;
            if (OutProjection) { OutProjection->AddUnique(Child); }
            CountFeatures(Child, Features, OutProjection);
            if (Features.Health != 0 || Features.Armor != 0 ||
                Features.Velocity != 0 || Features.Timers != 1) { return false; }
        }
        return true;
    }
    auto NoBranches(UWorld* InWorld) -> bool
    {
        TArray<FCk_Handle> Original, Projection;
        const auto Root = IsValid(InWorld)
            ? UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InWorld) : FCk_Handle{};
        if (!IsLive(Root)) { return false; }
        for (const FName Tag : {FName(TEXT("TAG_AttributeGym_BasicAttributes")),
                                FName(TEXT("TAG_AttributeGym_BasicProjection"))})
        {
            for (const auto& Match : UCk_Utils_EntityTag_UE::ForEach_Entity(Root, Tag))
            { if (IsLive(Match)) { return false; } }
        }
        const auto Anchors = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, AnchorTag);
        if (Anchors.Num() > 1 || (Anchors.Num() == 1 && !IsLive(Anchors[0])))
        { return false; }
        if (Anchors.IsEmpty()) { return true; }
        if (!GetChildren(InWorld, Original, Projection) ||
            !Original.IsEmpty() || !Projection.IsEmpty()) { return false; }
        FFeatures Features;
        int32 StationDisplayTicks = 0;
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Anchors[0]))
        {
            // The surviving Common station owns its own text display tick
            // (CkGymStation_EntityScript.as), independently of the retired demo.
            if (IsLive(Child) && UCk_Utils_Timer_UE::Has(Child) &&
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Child) == Anchors[0])
            { ++StationDisplayTicks; continue; }
            CountFeatures(Child, Features);
        }
        return StationDisplayTicks == 1 && Features.Health == 0 && Features.Armor == 0 &&
            Features.Velocity == 0 && Features.Timers == 0;
    }
    auto IsReady(AGameStateBase* InState, const FFixture& InFixture, int32 InGeneration) -> bool
    {
        const auto* Snapshot = SnapshotOf(InState, InFixture);
        FValues Values;
        return Snapshot && GenerationOf(InState, InFixture) == InGeneration &&
            SequenceOf(InState, InFixture) > 0 &&
            InFixture.Ready->GetPropertyValue_InContainer(Snapshot) &&
            !InFixture.Failed->GetPropertyValue_InContainer(Snapshot) &&
            ReadValues(InState, InFixture, Values);
    }
    auto IsFailed(AGameStateBase* InState, const FFixture& InFixture, int32 InGeneration) -> bool
    {
        const auto* Snapshot = SnapshotOf(InState, InFixture);
        return Snapshot && GenerationOf(InState, InFixture) == InGeneration &&
            !InFixture.Ready->GetPropertyValue_InContainer(Snapshot) &&
            InFixture.Failed->GetPropertyValue_InContainer(Snapshot);
    }
    auto CatalogMatches(UWorld* InServer, UWorld* InClient) -> bool
    {
        auto* ServerGI = IsValid(InServer) ? InServer->GetGameInstance() : nullptr;
        auto* ClientGI = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
        const auto* Server = IsValid(ServerGI) ? ServerGI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        const auto* Client = IsValid(ClientGI) ? ClientGI->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
        if (!IsValid(Server) || !IsValid(Client)) { return false; }
        const auto Index = Server->Find_GymIndexByName(TEXT("Attribute Basic"));
        return Index != INDEX_NONE && Server->Get_CurrentGymIndex() == Index &&
            Client->Get_CurrentGymIndex() == Index &&
            Server->Get_Entries().Num() == Client->Get_Entries().Num() &&
            Server->Get_SuppressHUDDuringStartup() == Client->Get_SuppressHUDDuringStartup();
    }
    auto WorldReady(UWorld* InWorld, const FFixture& InFixture, int32 InGeneration,
                    bool InServer, bool InDedicated) -> bool
    {
        auto* State = GetState(InWorld, InFixture);
        if (!IsReady(State, InFixture, InGeneration) ||
            !Branches(InWorld, InServer, InDedicated) ||
            (InServer && InFixture.ServerChildCount->GetPropertyValue_InContainer(State) != 1) ||
            (!InServer && (InFixture.LocalGeneration->GetPropertyValue_InContainer(State) != InGeneration ||
                !InFixture.LocalReady->GetPropertyValue_InContainer(State) ||
                InFixture.LocalFailed->GetPropertyValue_InContainer(State))))
        { return false; }
        if (InServer)
        {
            TArray<FCk_Handle> Original, Projection;
            return GetChildren(InWorld, Original, Projection) && Original.Num() == 1 &&
                *InFixture.ServerBasic->ContainerPtrToValuePtr<FCk_Handle>(State) == Original[0];
        }
        return true;
    }
    auto AllReady(FFixture& InFixture, int32 InGeneration, bool InDedicated,
                  bool InSurvivorOnly = false) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* Client0 = InSurvivorOnly ? nullptr : ck::auto_test::net::Get_ClientWorld(0);
        auto* Client1 = InSurvivorOnly
            ? (InFixture.SurvivorInstance.IsValid() ? InFixture.SurvivorInstance->GetWorld() : nullptr)
            : ck::auto_test::net::Get_ClientWorld(1);
        if ((!InSurvivorOnly && ck::auto_test::net::Get_AllPIEWorlds().Num() != 3) ||
            !WorldReady(Server, InFixture, InGeneration, true, InDedicated) ||
            (!InSurvivorOnly && !WorldReady(Client0, InFixture, InGeneration, false, InDedicated)) ||
            !WorldReady(Client1, InFixture, InGeneration, false, InDedicated) ||
            (!InSurvivorOnly && !CatalogMatches(Server, Client0)) ||
            !CatalogMatches(Server, Client1))
        { return false; }
        auto* ServerState = GetState(Server, InFixture);
        if (InFixture.HistoryGeneration != InGeneration)
        {
            InFixture.History.Reset(); InFixture.HistoryGeneration = InGeneration;
        }
        const auto Sequence = SequenceOf(ServerState, InFixture);
        if (InFixture.History.IsEmpty() || InFixture.History.Last().Sequence != Sequence)
        {
            if (InFixture.History.Num() >= 64) { InFixture.History.RemoveAt(0); }
            auto& Item = InFixture.History.AddDefaulted_GetRef();
            Item.Generation = InGeneration; Item.Sequence = Sequence;
            Item.Copy = MakeShared<FStructOnScope>(InFixture.Snapshot->Struct.Get());
            InFixture.Snapshot->Struct->CopyScriptStruct(
                Item.Copy->GetStructMemory(), SnapshotOf(ServerState, InFixture));
        }
        for (auto* Client : {Client0, Client1})
        {
            if (!Client) { continue; }
            auto* State = GetState(Client, InFixture);
            bool Found = false;
            for (const auto& Item : InFixture.History)
            {
                if (Item.Generation == InGeneration && Item.Sequence == SequenceOf(State, InFixture) &&
                    Item.Copy.IsValid() &&
                    InFixture.Snapshot->Identical(SnapshotOf(State, InFixture),
                        Item.Copy->GetStructMemory(), 0))
                { Found = true; break; }
            }
            if (!Found) { return false; }
        }
        return true;
    }
    auto CaptureBefore(FFixture& InFixture) -> bool
    {
        auto* ServerState = GetState(ck::auto_test::net::Get_ServerWorld(), InFixture);
        if (!ReadValues(ServerState, InFixture, InFixture.Before)) { return false; }
        InFixture.BeforeServerSequence = SequenceOf(ServerState, InFixture);
        if (InFixture.BeforeServerSequence <= 0) { return false; }
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto* ClientState = GetState(ck::auto_test::net::Get_ClientWorld(Index), InFixture);
            InFixture.BeforeSequence[Index] = SequenceOf(ClientState, InFixture);
            if (InFixture.BeforeSequence[Index] <= 0) { return false; }
        }
        return true;
    }
    auto Converged(FFixture& InFixture, int32 InGeneration, bool InDedicated,
                   double InHealth, int32 InArmor, const FVector& InVelocity,
                   bool InAuto, int32 InCounter, bool InReset = false) -> bool
    {
        if (!AllReady(InFixture, InGeneration, InDedicated)) { return false; }
        if (InReset)
        {
            for (const auto& Item : InFixture.History)
            {
                if (Item.Generation != InGeneration ||
                    Item.Sequence <= InFixture.BeforeServerSequence ||
                    !Item.Copy.IsValid() || InFixture.Before.Clamps <= 0) { continue; }
                FValues Values;
                if (!ReadSnapshotValues(Item.Copy->GetStructMemory(), InFixture, Values) ||
                    !FMath::IsNearlyEqual(Values.Health, InHealth, 0.01) ||
                    Values.Armor != InArmor || !Values.Velocity.Equals(InVelocity, 0.01) ||
                    Values.AutoRunning != InAuto || Values.AutoStep != 0 ||
                    Values.HealthChanges != 0 || Values.ArmorChanges != 1 ||
                    Values.VelocityChanges != 1 || Values.Clamps != 0) { continue; }
                if (!InFixture.ResetHistory.ContainsByPredicate(
                    [&Item](const FObserved& Existing)
                    { return Existing.Sequence == Item.Sequence; }))
                {
                    if (InFixture.ResetHistory.Num() >= 64)
                    { InFixture.ResetHistory.RemoveAt(0); }
                    InFixture.ResetHistory.Add(Item);
                }
                InFixture.ResetServerSeen = true;
            }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* State = GetState(ck::auto_test::net::Get_ClientWorld(Index), InFixture);
                const auto ClientSequence = SequenceOf(State, InFixture);
                if (ClientSequence <= InFixture.BeforeSequence[Index]) { continue; }
                for (const auto& Item : InFixture.ResetHistory)
                {
                    if (Item.Generation == InGeneration && Item.Sequence == ClientSequence &&
                        Item.Copy.IsValid() &&
                        InFixture.Snapshot->Identical(SnapshotOf(State, InFixture),
                            Item.Copy->GetStructMemory(), 0))
                    { InFixture.ResetSeen[Index] = true; break; }
                }
            }
            return InFixture.ResetServerSeen &&
                InFixture.ResetSeen[0] && InFixture.ResetSeen[1];
        }
        for (int32 Index = -1; Index < 2; ++Index)
        {
            auto* World = Index < 0 ? ck::auto_test::net::Get_ServerWorld()
                : ck::auto_test::net::Get_ClientWorld(Index);
            auto* State = GetState(World, InFixture);
            FValues Values;
            if (!ReadValues(State, InFixture, Values) ||
                (Index >= 0 && SequenceOf(State, InFixture) <= InFixture.BeforeSequence[Index]) ||
                !FMath::IsNearlyEqual(Values.Health, InHealth, 0.01) ||
                Values.Armor != InArmor || !Values.Velocity.Equals(InVelocity, 0.01) ||
                Values.AutoRunning != InAuto) { return false; }
            if (!InReset && ((InCounter == 0 && Values.HealthChanges <= InFixture.Before.HealthChanges) ||
                     (InCounter == 1 && Values.ArmorChanges <= InFixture.Before.ArmorChanges) ||
                     (InCounter == 2 && Values.VelocityChanges <= InFixture.Before.VelocityChanges) ||
                     (InCounter == 3 && Values.Clamps <= InFixture.Before.Clamps)))
            { return false; }
        }
        return true;
    }
    auto AutoConverged(FFixture& InFixture, int32 InGeneration, bool InDedicated, bool InAuto) -> bool
    {
        if (!AllReady(InFixture, InGeneration, InDedicated)) { return false; }
        for (int32 Index = -1; Index < 2; ++Index)
        {
            auto* World = Index < 0 ? ck::auto_test::net::Get_ServerWorld()
                : ck::auto_test::net::Get_ClientWorld(Index);
            auto* State = GetState(World, InFixture);
            FValues Values;
            if ((Index >= 0 && SequenceOf(State, InFixture) <= InFixture.BeforeSequence[Index]) ||
                !ReadValues(State, InFixture, Values) || Values.AutoRunning != InAuto) { return false; }
        }
        return true;
    }
    auto SameValues(const FValues& A, const FValues& B) -> bool
    {
        return FMath::IsNearlyEqual(A.Health, B.Health, 0.01) &&
            A.Armor == B.Armor && A.Velocity.Equals(B.Velocity, 0.01) &&
            A.HealthChanges == B.HealthChanges && A.ArmorChanges == B.ArmorChanges &&
            A.VelocityChanges == B.VelocityChanges && A.Clamps == B.Clamps &&
            A.AutoStep == B.AutoStep && A.AutoRunning == B.AutoRunning;
    }
    auto Invoke(APlayerController* InPC, const FFixture& InFixture,
                int32 InSerial, int32 InAction, int32 InPreset,
                const FVector& InVelocity, bool InAuto) -> bool
    {
        if (!IsValid(InPC) || !InPC->IsA(InFixture.Controller)) { return false; }
        FStructOnScope Params(InFixture.Rpc);
        auto* Memory = Params.GetStructMemory();
        InFixture.RpcSerial->SetPropertyValue_InContainer(Memory, InSerial);
        InFixture.RpcAction->SetPropertyValue_InContainer(Memory, InAction);
        InFixture.RpcPreset->SetPropertyValue_InContainer(Memory, InPreset);
        InFixture.RpcVelocity->Struct->CopyScriptStruct(
            InFixture.RpcVelocity->ContainerPtrToValuePtr<void>(Memory), &InVelocity);
        InFixture.RpcAuto->SetPropertyValue_InContainer(Memory, InAuto);
        InPC->ProcessEvent(InFixture.Rpc, Memory);
        return true;
    }
    auto ResultIs(UWorld* InClient, const FFixture& InFixture, int32 InAttempt,
                  int32 InAccepted, int32 InResult) -> bool
    {
        auto* PC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
        return IsValid(PC) && PC->IsA(InFixture.Controller) &&
            InFixture.AttemptSerial->GetPropertyValue_InContainer(PC) == InAttempt &&
            InFixture.LastSerial->GetPropertyValue_InContainer(PC) == InAccepted &&
            InFixture.Result->GetPropertyValue_InContainer(PC) == InResult;
    }
    auto Describe(UWorld* InWorld, const FFixture& InFixture) -> FString
    {
        auto* State = GetState(InWorld, InFixture);
        const auto* Snapshot = SnapshotOf(State, InFixture);
        if (!Snapshot) { return FString::Printf(TEXT("Attribute Basic world=%s state=missing"), *GetNameSafe(InWorld)); }
        TArray<FCk_Handle> Original, Projection;
        const auto bAnchor = GetChildren(InWorld, Original, Projection);
        FValues Values; const auto bSample = ReadValues(State, InFixture, Values);
        return FString::Printf(
            TEXT("Attribute Basic world=%s gen=%d seq=%d ready=%d failed=%d reason='%s' anchor=%d original=%d projection=%d sample=%d health=%.2f armor=%d velocity=%s changes=%d/%d/%d clamps=%d step=%d auto=%d local=%d/%d/%d reason='%s'"),
            *GetNameSafe(InWorld), GenerationOf(State, InFixture), SequenceOf(State, InFixture),
            InFixture.Ready->GetPropertyValue_InContainer(Snapshot),
            InFixture.Failed->GetPropertyValue_InContainer(Snapshot),
            *InFixture.FailureReason->GetPropertyValue_InContainer(Snapshot),
            bAnchor, Original.Num(), Projection.Num(), bSample, Values.Health, Values.Armor,
            *Values.Velocity.ToString(), Values.HealthChanges, Values.ArmorChanges,
            Values.VelocityChanges, Values.Clamps, Values.AutoStep, Values.AutoRunning,
            InFixture.LocalGeneration->GetPropertyValue_InContainer(State),
            InFixture.LocalReady->GetPropertyValue_InContainer(State),
            InFixture.LocalFailed->GetPropertyValue_InContainer(State),
            *InFixture.LocalFailureReason->GetPropertyValue_InContainer(State));
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkAttributeBasicGym_Network,
    "Ck.Gym.Network.AttributeBasic.SharedStation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkAttributeBasicGym_Network::RunTest(const FString& Parameters)
{
    using namespace ck_attribute_basic_network_test;
    bool bDedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, bDedicated))
    { AddError(TEXT("unsupported Attribute Basic network topology")); return false; }
    const auto Fixture = MakeShared<FFixture>();
    Fixture->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    Fixture->State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    Fixture->Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    bool Schema = true;
    Schema &= TestNotNull(TEXT("Attribute Basic GameMode"), Fixture->Mode);
    Schema &= TestNotNull(TEXT("Attribute Basic GameState"), Fixture->State);
    Schema &= TestNotNull(TEXT("Attribute Basic PlayerController"), Fixture->Controller);
    if (!Schema) { return false; }
    Fixture->Snapshot = FindFProperty<FStructProperty>(Fixture->State, TEXT("Snapshot"));
    if (!TestNotNull(TEXT("replicated Snapshot"), Fixture->Snapshot)) { return false; }
    auto* Snap = Fixture->Snapshot->Struct.Get();
    Fixture->Generation = FindFProperty<FIntProperty>(Snap, TEXT("Generation"));
    Fixture->Sequence = FindFProperty<FIntProperty>(Snap, TEXT("Sequence"));
    Fixture->Ready = FindFProperty<FBoolProperty>(Snap, TEXT("Ready"));
    Fixture->Failed = FindFProperty<FBoolProperty>(Snap, TEXT("Failed"));
    Fixture->FailureReason = FindFProperty<FStrProperty>(Snap, TEXT("FailureReason"));
    Fixture->Sample = FindFProperty<FStructProperty>(Snap, TEXT("Sample"));
    Schema &= TestNotNull(TEXT("Snapshot.Sample"), Fixture->Sample);
    if (!Schema) { return false; }
    auto* Sample = Fixture->Sample->Struct.Get();
    Fixture->Health = FindFProperty<FNumericProperty>(Sample, TEXT("Health"));
    Fixture->Armor = FindFProperty<FIntProperty>(Sample, TEXT("Armor"));
    Fixture->Velocity = FindFProperty<FStructProperty>(Sample, TEXT("Velocity"));
    Fixture->HealthChanges = FindFProperty<FIntProperty>(Sample, TEXT("HealthChangeCount"));
    Fixture->ArmorChanges = FindFProperty<FIntProperty>(Sample, TEXT("ArmorChangeCount"));
    Fixture->VelocityChanges = FindFProperty<FIntProperty>(Sample, TEXT("VelocityChangeCount"));
    Fixture->Clamps = FindFProperty<FIntProperty>(Sample, TEXT("ClampedCount"));
    Fixture->AutoStep = FindFProperty<FIntProperty>(Sample, TEXT("AutoStep"));
    Fixture->AutoRunning = FindFProperty<FBoolProperty>(Sample, TEXT("AutoRunning"));
    Fixture->ServerBasic = FindFProperty<FStructProperty>(Fixture->State, TEXT("ServerBasic"));
    Fixture->ServerChildCount = FindFProperty<FIntProperty>(Fixture->State, TEXT("ServerChildCount"));
    Fixture->LocalGeneration = FindFProperty<FIntProperty>(Fixture->State, TEXT("LocalProjectionGeneration"));
    Fixture->LocalReady = FindFProperty<FBoolProperty>(Fixture->State, TEXT("LocalProjectionReady"));
    Fixture->LocalFailed = FindFProperty<FBoolProperty>(Fixture->State, TEXT("LocalProjectionFailed"));
    Fixture->LocalFailureReason = FindFProperty<FStrProperty>(Fixture->State, TEXT("LocalProjectionFailureReason"));
    Fixture->LastSerial = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastControlSerial"));
    Fixture->AttemptSerial = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastControlAttemptSerial"));
    Fixture->Result = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastControlResult"));
    Fixture->Rpc = Fixture->Controller->FindFunctionByName(TEXT("Server_RequestAttributeBasicControl"));
    Schema &= TestNotNull(TEXT("Snapshot.Generation"), Fixture->Generation);
    Schema &= TestNotNull(TEXT("Snapshot.Sequence"), Fixture->Sequence);
    Schema &= TestNotNull(TEXT("Snapshot.Ready"), Fixture->Ready);
    Schema &= TestNotNull(TEXT("Snapshot.Failed"), Fixture->Failed);
    Schema &= TestNotNull(TEXT("Snapshot.FailureReason"), Fixture->FailureReason);
    Schema &= TestNotNull(TEXT("Sample.Health"), Fixture->Health);
    Schema &= TestNotNull(TEXT("Sample.Armor"), Fixture->Armor);
    Schema &= TestNotNull(TEXT("Sample.Velocity"), Fixture->Velocity);
    Schema &= TestNotNull(TEXT("Sample.HealthChangeCount"), Fixture->HealthChanges);
    Schema &= TestNotNull(TEXT("Sample.ArmorChangeCount"), Fixture->ArmorChanges);
    Schema &= TestNotNull(TEXT("Sample.VelocityChangeCount"), Fixture->VelocityChanges);
    Schema &= TestNotNull(TEXT("Sample.ClampedCount"), Fixture->Clamps);
    Schema &= TestNotNull(TEXT("Sample.AutoStep"), Fixture->AutoStep);
    Schema &= TestNotNull(TEXT("Sample.AutoRunning"), Fixture->AutoRunning);
    Schema &= TestNotNull(TEXT("GameState.ServerBasic"), Fixture->ServerBasic);
    Schema &= TestNotNull(TEXT("GameState.ServerChildCount"), Fixture->ServerChildCount);
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
    Fixture->RpcPreset = FindFProperty<FIntProperty>(Fixture->Rpc, TEXT("InPresetIndex"));
    Fixture->RpcVelocity = FindFProperty<FStructProperty>(Fixture->Rpc, TEXT("InVelocity"));
    Fixture->RpcAuto = FindFProperty<FBoolProperty>(Fixture->Rpc, TEXT("InAutoRunning"));
    Schema &= TestTrue(TEXT("exact replicated Attribute Basic schema and RPC"),
        Fixture->Snapshot->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->Snapshot->Struct->GetName() == TEXT("CkAttributeGym_BasicSnapshot") &&
        Fixture->Sample->Struct->GetName() == TEXT("CkAttributeGym_BasicSample") &&
        Fixture->Health->IsFloatingPoint() &&
        Fixture->Velocity->Struct == TBaseStructure<FVector>::Get() &&
        Fixture->ServerBasic->Struct == FCk_Handle::StaticStruct() &&
        Fixture->LastSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->AttemptSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->Result->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->RpcSerial && Fixture->RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->RpcAction && Fixture->RpcAction->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->RpcPreset && Fixture->RpcPreset->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->RpcVelocity && Fixture->RpcVelocity->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->RpcVelocity->Struct == TBaseStructure<FVector>::Get() &&
        Fixture->RpcAuto && Fixture->RpcAuto->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->Rpc->NumParms == 5 &&
        Fixture->Rpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer));
    if (!Schema) { return false; }

    Fixture->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, Fixture->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([this, Fixture, bDedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            const auto Generation = GenerationOf(State, *Fixture);
            const auto Ready = Generation > 0 && AllReady(*Fixture, Generation, bDedicated) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1));
            if (!Ready && FPlatformTime::Seconds() >= Fixture->NextDiagnostic)
            {
                Fixture->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                AddInfo(Describe(Server, *Fixture));
                AddInfo(Describe(ck::auto_test::net::Get_ClientWorld(0), *Fixture));
                AddInfo(Describe(ck::auto_test::net::Get_ClientWorld(1), *Fixture));
            }
            return Ready;
        }), 45.0, TEXT("one authority Basic and two complete pure projections become ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture, bDedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            Fixture->InitialGeneration = GenerationOf(State, *Fixture);
            Fixture->ServerState = State;
            auto* Survivor = ck::auto_test::net::Get_ClientWorld(1);
            Fixture->SurvivorInstance = IsValid(Survivor) ? Survivor->GetGameInstance() : nullptr;
            FValues Values;
            return TestTrue(TEXT("authored Basic has one authority root and coherent typed startup sample"),
                IsValid(Server) && IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(Fixture->Mode) &&
                Server->GetNetMode() == (bDedicated ? NM_DedicatedServer : NM_ListenServer) &&
                Fixture->InitialGeneration > 0 &&
                Branches(Server, true, bDedicated, &Fixture->InitialServerBasic,
                    &Fixture->InitialSimulation) &&
                Fixture->InitialSimulation.Num() == 4 &&
                IsLive(Fixture->InitialServerBasic) &&
                ReadValues(State, *Fixture, Values) &&
                AllReady(*Fixture, Fixture->InitialGeneration, bDedicated) &&
                Fixture->SurvivorInstance.IsValid());
        }), TEXT("capture initial authority Basic identity and coherent typed startup state")));
    auto QueueControl = [this, Fixture, bDedicated](
        int32 InClient, int32 InSerial, int32 InAction, int32 InPreset,
        FVector InVelocity, bool InAuto, int32 InCounter, bool InReset,
        const TCHAR* InName)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(InClient,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, Fixture, InClient, InSerial, InAction, InPreset, InVelocity, InAuto,
                 InCounter, InReset](UWorld* InWorld)
                {
                    if (!TestTrue(TEXT("capture typed pre-control sample and both client sequences"),
                        CaptureBefore(*Fixture))) { return; }
                    Fixture->Expected = Fixture->Before;
                    Fixture->ExpectedCounter = InCounter;
                    Fixture->ExpectedReset = InReset;
                    Fixture->ResetHistory.Reset();
                    Fixture->ResetServerSeen = false;
                    Fixture->ResetSeen[0] = Fixture->ResetSeen[1] = false;
                    Fixture->PauseSettledSince = 0.0;
                    Fixture->PauseStep = INDEX_NONE;
                    Fixture->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                    auto Preset = InPreset;
                    if (InAction == 0) { Fixture->Expected.AutoRunning = InAuto; }
                    else if (InAction == 1)
                    {
                        // Auto may have paused at 50. A real value transition is required
                        // before asserting that its value-change signal advances.
                        Preset = FMath::IsNearlyEqual(Fixture->Before.Health, 50.0, 0.01) ? 2 : 1;
                        Fixture->Expected.Health = Preset == 2 ? 0.0 : 50.0;
                        Fixture->Expected.AutoRunning = false;
                    }
                    else if (InAction == 2)
                    {
                        Preset = Fixture->Before.Armor == 64 ? 2 : 1;
                        Fixture->Expected.Armor = Preset == 2 ? 128 : 64;
                        Fixture->Expected.AutoRunning = false;
                    }
                    else if (InAction == 4)
                    {
                        Fixture->Expected.Health = 100.0;
                        Fixture->Expected.Armor = 255;
                        Fixture->Expected.Velocity = FVector(500, 250, 100);
                        Fixture->Expected.AutoRunning = false;
                    }
                    else if (InAction == 5)
                    {
                        Fixture->Expected.Health = 100.0;
                        Fixture->Expected.Armor = 150;
                        Fixture->Expected.Velocity = FVector(100, 50, 0);
                        Fixture->Expected.AutoRunning = true;
                    }
                    else if (InAction == 6)
                    { Fixture->Expected.Velocity = InVelocity; Fixture->Expected.AutoRunning = false; }
                    AddInfo(FString::Printf(
                        TEXT("Basic control client=%d serial=%d action=%d preset=%d before health=%.2f armor=%d changes=%d/%d/%d clamps=%d step=%d auto=%d expected health=%.2f armor=%d"),
                        InClient, InSerial, InAction, Preset, Fixture->Before.Health,
                        Fixture->Before.Armor, Fixture->Before.HealthChanges,
                        Fixture->Before.ArmorChanges, Fixture->Before.VelocityChanges,
                        Fixture->Before.Clamps, Fixture->Before.AutoStep,
                        Fixture->Before.AutoRunning, Fixture->Expected.Health,
                        Fixture->Expected.Armor));
                    TestTrue(TEXT("real owning remote client invoked Basic control"),
                        Invoke(IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr,
                            *Fixture, InSerial, InAction, Preset, InVelocity, InAuto));
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda(
                [this, Fixture, bDedicated, InClient, InSerial, InAction]() -> bool
                {
                    auto* Server = ck::auto_test::net::Get_ServerWorld();
                    auto* State = GetState(Server, *Fixture);
                    if (FPlatformTime::Seconds() >= Fixture->NextDiagnostic)
                    {
                        Fixture->NextDiagnostic = FPlatformTime::Seconds() + 5.0;
                        for (auto* World : {Server, ck::auto_test::net::Get_ClientWorld(0),
                             ck::auto_test::net::Get_ClientWorld(1)})
                        { AddInfo(Describe(World, *Fixture)); }
                        auto* Owner = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, InClient);
                        if (IsValid(Owner) && Owner->IsA(Fixture->Controller))
                        {
                            AddInfo(FString::Printf(TEXT("Basic authority owner=%s client=%d expectedSerial=%d accepted=%d attempted=%d result=%d"),
                                *GetNameSafe(Owner), InClient, InSerial,
                                Fixture->LastSerial->GetPropertyValue_InContainer(Owner),
                                Fixture->AttemptSerial->GetPropertyValue_InContainer(Owner),
                                Fixture->Result->GetPropertyValue_InContainer(Owner)));
                        }
                    }
                    if (!ResultIs(ck::auto_test::net::Get_ClientWorld(InClient),
                            *Fixture, InSerial, InSerial, 1) ||
                        GenerationOf(State, *Fixture) != Fixture->InitialGeneration ||
                        !IsLive(Fixture->InitialServerBasic) ||
                        *Fixture->ServerBasic->ContainerPtrToValuePtr<FCk_Handle>(State) !=
                            Fixture->InitialServerBasic) { return false; }
                    if (InAction == 0)
                    {
                        if (!AutoConverged(*Fixture, Fixture->InitialGeneration,
                            bDedicated, Fixture->Expected.AutoRunning)) { return false; }
                        if (Fixture->Expected.AutoRunning) { return true; }
                        FValues Values;
                        if (!ReadValues(State, *Fixture, Values)) { return false; }
                        if (Fixture->PauseSettledSince == 0.0)
                        {
                            Fixture->PauseSettledSince = Server->GetTimeSeconds();
                            Fixture->PauseStep = Values.AutoStep;
                        }
                        if (Values.AutoStep != Fixture->PauseStep) { return false; }
                        for (int32 Index = 0; Index < 2; ++Index)
                        {
                            FValues ClientValues;
                            if (!ReadValues(GetState(ck::auto_test::net::Get_ClientWorld(Index),
                                *Fixture), *Fixture, ClientValues) ||
                                ClientValues.AutoStep != Fixture->PauseStep) { return false; }
                        }
                        return Server->GetTimeSeconds() - Fixture->PauseSettledSince >= 2.5;
                    }
                    return Converged(*Fixture, Fixture->InitialGeneration, bDedicated,
                        Fixture->Expected.Health, Fixture->Expected.Armor,
                        Fixture->Expected.Velocity, Fixture->Expected.AutoRunning,
                        Fixture->ExpectedCounter, Fixture->ExpectedReset);
                }), 20.0, InName));
    };

    // Pause the authored auto cycle before comparing exact manual values.
    QueueControl(0, 101, 0, 0, FVector::ZeroVector, false, -1, false,
        TEXT("client zero pauses the shared Basic auto cycle"));
    QueueControl(1, 201, 1, 1, FVector::ZeroVector, false, 0, false,
        TEXT("client one distinct Health preset reaches both projections with signal progress"));
    QueueControl(0, 102, 2, 1, FVector::ZeroVector, false, 1, false,
        TEXT("client zero distinct Armor preset reaches both projections with signal progress"));
    QueueControl(1, 202, 6, 0, FVector(321, -45, 7), false, 2, false,
        TEXT("client one finite free Velocity reaches both projections"));
    QueueControl(0, 103, 4, 0, FVector::ZeroVector, false, 3, false,
        TEXT("client zero boundary control clamps Health and advances clamp counter"));
    QueueControl(1, 203, 5, 0, FVector::ZeroVector, true, -1, true,
        TEXT("original R resets values without replacing authority Basic or generation"));
    QueueControl(0, 104, 0, 0, FVector::ZeroVector, false, -1, false,
        TEXT("client zero pauses auto before rejection invariants"));

    auto QueueRejected = [this, Fixture, bDedicated](
        int32 InSerial, int32 InAction, int32 InPreset, FVector InVelocity,
        const TCHAR* InName)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, Fixture, InSerial, InAction, InPreset, InVelocity](UWorld* InWorld)
                {
                    if (!TestTrue(TEXT("capture stable state before invalid control"),
                        CaptureBefore(*Fixture))) { return; }
                    TestTrue(TEXT("invalid control traversed actual owning-client RPC"),
                        Invoke(IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr,
                            *Fixture, InSerial, InAction, InPreset, InVelocity, false));
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda(
                [Fixture, bDedicated, InSerial]() -> bool
                {
                    auto* Server = ck::auto_test::net::Get_ServerWorld();
                    auto* State = GetState(Server, *Fixture);
                    if (!ResultIs(ck::auto_test::net::Get_ClientWorld(0),
                            *Fixture, InSerial, 104, 4) ||
                        GenerationOf(State, *Fixture) != Fixture->InitialGeneration ||
                        !IsValid(State) ||
                        *Fixture->ServerBasic->ContainerPtrToValuePtr<FCk_Handle>(State) !=
                            Fixture->InitialServerBasic ||
                        !AllReady(*Fixture, Fixture->InitialGeneration, bDedicated))
                    { return false; }
                    for (int32 Index = -1; Index < 2; ++Index)
                    {
                        auto* World = Index < 0 ? Server : ck::auto_test::net::Get_ClientWorld(Index);
                        FValues Actual;
                        if (!ReadValues(GetState(World, *Fixture), *Fixture, Actual) ||
                            !SameValues(Actual, Fixture->Before)) { return false; }
                    }
                    return true;
                }), 15.0, InName));
    };
    QueueRejected(0, 1, 0, FVector::ZeroVector,
        TEXT("zero serial rejected without accepted watermark or typed mutation"));
    QueueRejected(105, 99, 0, FVector::ZeroVector,
        TEXT("unsupported action rejected without accepted watermark or typed mutation"));
    QueueRejected(104, 1, 0, FVector::ZeroVector,
        TEXT("stale serial rejected without accepted watermark or typed mutation"));
    QueueRejected(106, 1, 99, FVector::ZeroVector,
        TEXT("out-of-range preset rejected across owning-client RPC"));
    auto QueueDirectRejectedVelocity = [this, Fixture, bDedicated](
        int32 InSerial, double InX, const TCHAR* InName)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, Fixture, InSerial, InX](UWorld* InServer)
                {
                    auto* RemotePC = ck::auto_test::net::TryGet_RemoteClientPlayerController(
                        InServer, 0);
                    if (!TestTrue(TEXT("capture Basic before direct nonfinite authority rejection"),
                        IsValid(RemotePC) && IsValid(RemotePC->GetPawn()) &&
                        RemotePC->GetPawn()->GetController() == RemotePC &&
                        CaptureBefore(*Fixture))) { return; }
                    FVector BadVelocity = FVector::ZeroVector;
                    BadVelocity.X = InX;
                    TestTrue(TEXT("real possessed server PC validates nonfinite Velocity"),
                        Invoke(RemotePC, *Fixture, InSerial, 6, 0, BadVelocity, false));
                })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
            FCk_NetAutoTest_Condition::CreateLambda([Fixture, bDedicated, InSerial]() -> bool
            {
                auto* Server = ck::auto_test::net::Get_ServerWorld();
                auto* State = GetState(Server, *Fixture);
                auto* ServerPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(
                    Server, 0);
                if (!ResultIs(ck::auto_test::net::Get_ClientWorld(0),
                        *Fixture, InSerial, 104, 4) ||
                    !IsValid(ServerPC) ||
                    Fixture->AttemptSerial->GetPropertyValue_InContainer(ServerPC) != InSerial ||
                    Fixture->LastSerial->GetPropertyValue_InContainer(ServerPC) != 104 ||
                    Fixture->Result->GetPropertyValue_InContainer(ServerPC) != 4 ||
                    !AllReady(*Fixture, Fixture->InitialGeneration, bDedicated) ||
                    GenerationOf(State, *Fixture) != Fixture->InitialGeneration ||
                    *Fixture->ServerBasic->ContainerPtrToValuePtr<FCk_Handle>(State) !=
                        Fixture->InitialServerBasic) { return false; }
                for (int32 Index = -1; Index < 2; ++Index)
                {
                    auto* World = Index < 0 ? Server : ck::auto_test::net::Get_ClientWorld(Index);
                    FValues Actual;
                    if (!ReadValues(GetState(World, *Fixture), *Fixture, Actual) ||
                        !SameValues(Actual, Fixture->Before)) { return false; }
                }
                return true;
            }), 15.0, InName));
    };
    QueueDirectRejectedVelocity(107, std::numeric_limits<double>::quiet_NaN(),
        TEXT("NaN Velocity rejected by direct authority application validation"));
    QueueDirectRejectedVelocity(108, std::numeric_limits<double>::infinity(),
        TEXT("positive infinite Velocity rejected by direct authority application validation"));
    QueueDirectRejectedVelocity(109, -std::numeric_limits<double>::infinity(),
        TEXT("negative infinite Velocity rejected by direct authority application validation"));
    QueueControl(1, 204, 0, 0, FVector::ZeroVector, true, -1, false,
        TEXT("surviving client resumes authority auto before owner departure"));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* RemotePC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* LocalPC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            auto* Instance = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
            if (!TestTrue(TEXT("departing real remote owner exists"),
                IsValid(RemotePC) && IsValid(LocalPC) && IsValid(Instance))) { return; }
            Fixture->DepartingServerPC = RemotePC;
            Fixture->DepartingInstance = Instance;
            Fixture->DepartureSequence = SequenceOf(Fixture->ServerState.Get(), *Fixture);
            FValues DepartureValues;
            if (!TestTrue(TEXT("capture advancing authority auto step before departure"),
                ReadValues(Fixture->ServerState.Get(), *Fixture, DepartureValues))) { return; }
            Fixture->DepartureAutoStep = DepartureValues.AutoStep;
            LocalPC->ClientTravel(TEXT("/Engine/Maps/Entry"), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, bDedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* SurvivorWorld = Fixture->SurvivorInstance.IsValid()
                ? Fixture->SurvivorInstance->GetWorld() : nullptr;
            auto* DepartedWorld = Fixture->DepartingInstance.IsValid()
                ? Fixture->DepartingInstance->GetWorld() : nullptr;
            FValues ServerValues, SurvivorValues;
            return State == Fixture->ServerState.Get() &&
                !Fixture->DepartingServerPC.IsValid() &&
                IsValid(DepartedWorld) && DepartedWorld != Server &&
                DepartedWorld->GetMapName().Contains(TEXT("Entry")) &&
                AllReady(*Fixture, Fixture->InitialGeneration, bDedicated, true) &&
                SequenceOf(State, *Fixture) > Fixture->DepartureSequence &&
                SequenceOf(GetState(SurvivorWorld, *Fixture), *Fixture) > Fixture->DepartureSequence &&
                ReadValues(State, *Fixture, ServerValues) &&
                ReadValues(GetState(SurvivorWorld, *Fixture), *Fixture, SurvivorValues) &&
                ServerValues.AutoStep > Fixture->DepartureAutoStep &&
                SurvivorValues.AutoStep > Fixture->DepartureAutoStep &&
                IsLive(Fixture->InitialServerBasic) &&
                *Fixture->ServerBasic->ContainerPtrToValuePtr<FCk_Handle>(State) ==
                    Fixture->InitialServerBasic;
        }), 45.0, TEXT("real owner departure leaves same authority Basic and advancing survivor")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture, bDedicated](UWorld* InServer)
        {
            Fixture->BeforeFailureSimulation.Reset();
            Fixture->BeforeFailureHostProjection.Reset();
            Fixture->BeforeFailureSurvivorProjection.Reset();
            FCk_Handle Current;
            auto* SurvivorWorld = Fixture->SurvivorInstance.IsValid()
                ? Fixture->SurvivorInstance->GetWorld() : nullptr;
            if (!TestTrue(TEXT("failure probe begins on complete original Basic"),
                GetState(InServer, *Fixture) == Fixture->ServerState.Get() &&
                Branches(InServer, true, bDedicated, &Current,
                    &Fixture->BeforeFailureSimulation,
                    &Fixture->BeforeFailureHostProjection) &&
                Branches(SurvivorWorld, false, bDedicated, nullptr, nullptr,
                    &Fixture->BeforeFailureSurvivorProjection) &&
                Current == Fixture->InitialServerBasic &&
                Fixture->BeforeFailureSimulation.Num() == 4 &&
                Fixture->BeforeFailureHostProjection.Num() == (bDedicated ? 0 : 2) &&
                Fixture->BeforeFailureSurvivorProjection.Num() == 2)) { return; }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Current);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, bDedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* SurvivorWorld = Fixture->SurvivorInstance.IsValid()
                ? Fixture->SurvivorInstance->GetWorld() : nullptr;
            auto* SurvivorState = GetState(SurvivorWorld, *Fixture);
            if (!IsFailed(State, *Fixture, Fixture->InitialGeneration) ||
                !IsFailed(SurvivorState, *Fixture, Fixture->InitialGeneration) ||
                Fixture->ServerChildCount->GetPropertyValue_InContainer(State) != 0 ||
                !NoBranches(Server) || !NoBranches(SurvivorWorld) ||
                !Fixture->LocalFailed->GetPropertyValue_InContainer(SurvivorState) ||
                Fixture->LocalReady->GetPropertyValue_InContainer(SurvivorState) ||
                *Fixture->ServerBasic->ContainerPtrToValuePtr<FCk_Handle>(State) != FCk_Handle{} ||
                !ck::Is_NOT_Valid(Fixture->InitialServerBasic) ||
                Fixture->BeforeFailureSimulation.Num() != 4 ||
                Fixture->BeforeFailureHostProjection.Num() != (bDedicated ? 0 : 2) ||
                Fixture->BeforeFailureSurvivorProjection.Num() != 2) { return false; }
            for (const auto& Handle : Fixture->BeforeFailureSimulation)
            { if (!ck::Is_NOT_Valid(Handle)) { return false; } }
            for (const auto& Handle : Fixture->BeforeFailureHostProjection)
            { if (!ck::Is_NOT_Valid(Handle)) { return false; } }
            for (const auto& Handle : Fixture->BeforeFailureSurvivorProjection)
            { if (!ck::Is_NOT_Valid(Handle)) { return false; } }
            return true;
        }), 25.0, TEXT("required child loss withdraws readiness and atomically retires all Basic branches")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            Fixture->FailureSequence = SequenceOf(Fixture->ServerState.Get(), *Fixture);
            return TestTrue(TEXT("terminal failure sequence captured"),
                Fixture->FailureSequence > 0);
        }), TEXT("capture terminal Basic failure sequence")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            return TestTrue(TEXT("Basic failure stays terminal with no republished subset"),
                State == Fixture->ServerState.Get() &&
                IsFailed(State, *Fixture, Fixture->InitialGeneration) &&
                SequenceOf(State, *Fixture) == Fixture->FailureSequence &&
                Fixture->ServerChildCount->GetPropertyValue_InContainer(State) == 0 &&
                *Fixture->ServerBasic->ContainerPtrToValuePtr<FCk_Handle>(State) == FCk_Handle{} &&
                NoBranches(Server));
        }), TEXT("failed shared Basic remains terminal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            Fixture->RestoreSettings();
            return TestTrue(TEXT("Attribute Basic PIE worlds drained"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
        }), TEXT("restore original startup preferences"), true));
    return true;
}

#endif
