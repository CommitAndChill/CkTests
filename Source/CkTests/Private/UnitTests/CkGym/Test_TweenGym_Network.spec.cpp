#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_NetworkRuntime.h"
#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkTween/CkTween_Fragment_Data.h"
#include "CkTween/CkTween_Utils.h"

#include <Engine/GameInstance.h>
#include <Engine/NetDriver.h>
#include <Engine/World.h>
#include <EngineUtils.h>
#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <GameFramework/PlayerController.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/UnrealType.h>

namespace ck_tween_gym_network_test
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_TweenTest_GymGameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_TweenTest_GymGameState");
    constexpr auto ActorPath = TEXT("/Script/Angelscript.Ck_TweenTest_GymActor");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_TweenTest_GymPlayerController");

    struct FFixture
    {
        ECkGym_StartupMode OriginalMode = ECkGym_StartupMode::Cycler;
        FString OriginalLast;
        TArray<FString> OriginalRecents;
        UClass* Mode = nullptr;
        UClass* State = nullptr;
        UClass* Actor = nullptr;
        UClass* Controller = nullptr;
        FIntProperty* Generation = nullptr;
        FBoolProperty* Ready = nullptr;
        FBoolProperty* Failed = nullptr;
        FArrayProperty* TweenActors = nullptr;
        FObjectProperty* TweenActorItem = nullptr;
        FIntProperty* ActorGeneration = nullptr;
        FBoolProperty* ActorPublished = nullptr;
        FEnumProperty* ActorEasingEnum = nullptr;
        FByteProperty* ActorEasingByte = nullptr;
        FIntProperty* LastResetSerial = nullptr;
        FIntProperty* LastResetAttemptSerial = nullptr;
        FIntProperty* LastResetResult = nullptr;
        UFunction* ResetRpc = nullptr;
        int32 EasingCount = 0;
        int32 InitialGeneration = 0;
        int32 ResetSerial = 1501;
        TArray<TWeakObjectPtr<AActor>> OriginalActors;
        TArray<FCk_Handle> OriginalEntities;
        TArray<FCk_Handle> OriginalTweens;
        TArray<TWeakObjectPtr<AActor>> SurvivorActors;
        TWeakObjectPtr<AGameStateBase> SurvivorState;
        TWeakObjectPtr<UGameInstance> SurvivorInstance;
        TWeakObjectPtr<UGameInstance> DepartingInstance;
        TWeakObjectPtr<APlayerController> DepartingServerController;
        FVector InitialServerLocation = FVector::ZeroVector;
        FVector InitialClient0Location = FVector::ZeroVector;
        FVector InitialClient1Location = FVector::ZeroVector;
        FVector DepartureLocation = FVector::ZeroVector;

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

    auto GetState(UWorld* InWorld, const FFixture& InFixture) -> ACkGym_GameState_UE*
    {
        auto* State = IsValid(InWorld) ? InWorld->GetGameState() : nullptr;
        return IsValid(State) && State->IsA(InFixture.State)
            ? Cast<ACkGym_GameState_UE>(State) : nullptr;
    }

    auto GetRegistry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto CatalogsMatch(UWorld* InServer, UWorld* InClient) -> bool
    {
        const auto* Server = GetRegistry(InServer);
        const auto* Client = GetRegistry(InClient);
        if (!IsValid(Server) || !IsValid(Client)) { return false; }
        const auto TweenIndex = Server->Find_GymIndexByName(TEXT("Tween"));
        return TweenIndex != INDEX_NONE && Server->Get_CurrentGymIndex() == TweenIndex &&
            Client->Get_CurrentGymIndex() == TweenIndex &&
            Client->Get_Entries().Num() == Server->Get_Entries().Num() &&
            Client->Get_SuppressHUDDuringStartup() == Server->Get_SuppressHUDDuringStartup();
    }

    auto GetEasing(const AActor* InActor, const FFixture& InFixture) -> int32
    {
        if (!IsValid(InActor)) { return INDEX_NONE; }
        if (InFixture.ActorEasingEnum)
        {
            const auto* Value = InFixture.ActorEasingEnum->ContainerPtrToValuePtr<void>(InActor);
            return static_cast<int32>(InFixture.ActorEasingEnum->GetUnderlyingProperty()
                ->GetUnsignedIntPropertyValue(Value));
        }
        return InFixture.ActorEasingByte
            ? InFixture.ActorEasingByte->GetPropertyValue_InContainer(InActor) : INDEX_NONE;
    }

    auto GetActorAt(UWorld* InWorld, const FFixture& InFixture, int32 InEasing) -> AActor*
    {
        if (!IsValid(InWorld)) { return nullptr; }
        AActor* Match = nullptr;
        for (TActorIterator<AActor> It(InWorld, InFixture.Actor); It; ++It)
        {
            if (!IsValid(*It) || GetEasing(*It, InFixture) != InEasing) { continue; }
            if (Match) { return nullptr; }
            Match = *It;
        }
        return Match;
    }

    auto CollectTweenChildren(const FCk_Handle& InOwner, TArray<FCk_Handle>& OutTweens) -> void
    {
        if (ck::Is_NOT_Valid(InOwner) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InOwner, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InOwner, ECk_EntityLifetime_DestructionPhase::Teardown) ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                InOwner, ECk_EntityLifetime_DestructionPhase::Destroyed))
        { return; }
        if (UCk_Utils_Tween_UE::Has(InOwner)) { OutTweens.Add(InOwner); }
        for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(InOwner))
        { CollectTweenChildren(Child, OutTweens); }
    }

    auto CountTweenChildren(const FCk_Handle& InOwner) -> int32
    {
        TArray<FCk_Handle> Tweens;
        CollectTweenChildren(InOwner, Tweens);
        return Tweens.Num();
    }

    auto SetIsReady(UWorld* InWorld, const FFixture& InFixture, int32 InGeneration,
                    bool InRequireServerEcs) -> bool
    {
        auto* State = GetState(InWorld, InFixture);
        if (!IsValid(State) || !State->Get_HasPublishedCatalog() ||
            InFixture.Generation->GetPropertyValue_InContainer(State) != InGeneration ||
            !InFixture.Ready->GetPropertyValue_InContainer(State) ||
            InFixture.Failed->GetPropertyValue_InContainer(State))
        { return false; }

        TArray<bool> Seen;
        Seen.Init(false, InFixture.EasingCount);
        int32 Count = 0;
        for (TActorIterator<AActor> It(InWorld, InFixture.Actor); It; ++It)
        {
            auto* Actor = *It;
            if (!IsValid(Actor)) { continue; }
            const auto Easing = GetEasing(Actor, InFixture);
            if (Easing < 0 || Easing >= InFixture.EasingCount || Seen[Easing] ||
                InFixture.ActorGeneration->GetPropertyValue_InContainer(Actor) != InGeneration ||
                !InFixture.ActorPublished->GetPropertyValue_InContainer(Actor))
            { return false; }
            Seen[Easing] = true;
            ++Count;
            if (InRequireServerEcs)
            {
                const auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
                if (ck::Is_NOT_Valid(Entity) || CountTweenChildren(Entity) != 1)
                { return false; }
            }
        }
        if (Count != InFixture.EasingCount) { return false; }
        if (!InRequireServerEcs) { return true; }

        auto* Array = InFixture.TweenActors->ContainerPtrToValuePtr<void>(State);
        FScriptArrayHelper Items(InFixture.TweenActors, Array);
        if (Items.Num() != InFixture.EasingCount) { return false; }
        TSet<AActor*> Referenced;
        for (int32 Index = 0; Index < Items.Num(); ++Index)
        {
            auto* Actor = Cast<AActor>(InFixture.TweenActorItem->GetObjectPropertyValue(Items.GetRawPtr(Index)));
            if (!IsValid(Actor) || Actor->GetWorld() != InWorld || Referenced.Contains(Actor) ||
                GetActorAt(InWorld, InFixture, GetEasing(Actor, InFixture)) != Actor)
            { return false; }
            Referenced.Add(Actor);
        }
        return Referenced.Num() == InFixture.EasingCount;
    }

    auto AllWorldsReady(const FFixture& InFixture, int32 InGeneration) -> bool
    {
        auto* Server = ck::auto_test::net::Get_ServerWorld();
        auto* Client0 = ck::auto_test::net::Get_ClientWorld(0);
        auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
        return ck::auto_test::net::Get_AllPIEWorlds().Num() == 3 &&
            SetIsReady(Server, InFixture, InGeneration, true) &&
            SetIsReady(Client0, InFixture, InGeneration, false) &&
            SetIsReady(Client1, InFixture, InGeneration, false) &&
            CatalogsMatch(Server, Client0) && CatalogsMatch(Server, Client1);
    }

    auto InvokeReset(APlayerController* InController, const FFixture& InFixture,
                     int32 InSerial) -> bool
    {
        if (!IsValid(InController) || !InController->IsA(InFixture.Controller)) { return false; }
        struct FParams { int32 InSerial; } Params{InSerial};
        InController->ProcessEvent(InFixture.ResetRpc, &Params);
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkTweenGym_Network,
    "Ck.Gym.Network.Tween.SharedSet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkTweenGym_Network::RunTest(const FString& Parameters)
{
    using namespace ck_tween_gym_network_test;
    auto Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Tween network topology")); return false; }

    const auto Fixture = MakeShared<FFixture>();
    Fixture->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    Fixture->State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    Fixture->Actor = FSoftClassPath{ActorPath}.TryLoadClass<AActor>();
    Fixture->Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    auto bSchema = true;
    bSchema &= TestNotNull(TEXT("Tween authored GameMode"), Fixture->Mode);
    bSchema &= TestNotNull(TEXT("Tween shared GameState"), Fixture->State);
    bSchema &= TestNotNull(TEXT("Tween actor"), Fixture->Actor);
    bSchema &= TestNotNull(TEXT("Tween owning controller"), Fixture->Controller);
    if (!bSchema) { return false; }

    Fixture->Generation = FindFProperty<FIntProperty>(Fixture->State, TEXT("Generation"));
    Fixture->Ready = FindFProperty<FBoolProperty>(Fixture->State, TEXT("Ready"));
    Fixture->Failed = FindFProperty<FBoolProperty>(Fixture->State, TEXT("Failed"));
    Fixture->TweenActors = FindFProperty<FArrayProperty>(Fixture->State, TEXT("TweenActors"));
    Fixture->ActorGeneration = FindFProperty<FIntProperty>(Fixture->Actor, TEXT("Generation"));
    Fixture->ActorPublished = FindFProperty<FBoolProperty>(Fixture->Actor, TEXT("Published"));
    Fixture->ActorEasingEnum = FindFProperty<FEnumProperty>(Fixture->Actor, TEXT("TweenEasingMethod"));
    Fixture->ActorEasingByte = FindFProperty<FByteProperty>(Fixture->Actor, TEXT("TweenEasingMethod"));
    Fixture->LastResetSerial = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastResetSerial"));
    Fixture->LastResetAttemptSerial = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastResetAttemptSerial"));
    Fixture->LastResetResult = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastResetResult"));
    Fixture->ResetRpc = Fixture->Controller->FindFunctionByName(TEXT("Server_RequestTweenReset"));
    bSchema &= TestNotNull(TEXT("GameState.Generation"), Fixture->Generation);
    bSchema &= TestNotNull(TEXT("GameState.Ready"), Fixture->Ready);
    bSchema &= TestNotNull(TEXT("GameState.Failed"), Fixture->Failed);
    bSchema &= TestNotNull(TEXT("GameState.TweenActors"), Fixture->TweenActors);
    bSchema &= TestNotNull(TEXT("Actor.Generation"), Fixture->ActorGeneration);
    bSchema &= TestNotNull(TEXT("Actor.Published"), Fixture->ActorPublished);
    bSchema &= TestTrue(TEXT("Actor.TweenEasingMethod is an enum or byte property"),
        Fixture->ActorEasingEnum || Fixture->ActorEasingByte);
    bSchema &= TestNotNull(TEXT("Controller.LastResetSerial"), Fixture->LastResetSerial);
    bSchema &= TestNotNull(TEXT("Controller.LastResetAttemptSerial"), Fixture->LastResetAttemptSerial);
    bSchema &= TestNotNull(TEXT("Controller.LastResetResult"), Fixture->LastResetResult);
    bSchema &= TestNotNull(TEXT("Controller.Server_RequestTweenReset"), Fixture->ResetRpc);
    if (!bSchema) { return false; }
    Fixture->TweenActorItem = CastField<FObjectProperty>(Fixture->TweenActors->Inner);
    const auto* RpcSerial = FindFProperty<FIntProperty>(Fixture->ResetRpc, TEXT("InSerial"));
    const auto* EasingEnum = StaticEnum<ECk_TweenEasing>();
    Fixture->EasingCount = EasingEnum ? static_cast<int32>(EasingEnum->GetMaxEnumValue()) : 0;
    if (!TestTrue(TEXT("Tween network reflection has a complete replicated contract"),
        Fixture->TweenActorItem && Fixture->TweenActorItem->PropertyClass->IsChildOf(Fixture->Actor) &&
        Fixture->Generation->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->Ready->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->Failed->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->ActorGeneration->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->ActorPublished->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->LastResetSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->LastResetAttemptSerial->HasAnyPropertyFlags(CPF_Net) &&
        Fixture->LastResetResult->HasAnyPropertyFlags(CPF_Net) &&
        RpcSerial && RpcSerial->HasAnyPropertyFlags(CPF_Parm) &&
        Fixture->ResetRpc->NumParms == 1 &&
        Fixture->ResetRpc->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer) &&
        Fixture->EasingCount > 0))
    { return false; }

    Fixture->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters, Fixture->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* State = GetState(ck::auto_test::net::Get_ServerWorld(), *Fixture);
            const auto Generation = IsValid(State)
                ? Fixture->Generation->GetPropertyValue_InContainer(State) : 0;
            return Generation > 0 && AllWorldsReady(*Fixture, Generation);
        }), 60.0, TEXT("one unique Tween actor for every easing reaches both clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture, Dedicated]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* Client1 = ck::auto_test::net::Get_ClientWorld(1);
            if (!TestTrue(TEXT("Tween server and survivor are available"),
                IsValid(State) && IsValid(Client1))) { return false; }
            Fixture->InitialGeneration = Fixture->Generation->GetPropertyValue_InContainer(State);
            Fixture->SurvivorState = State;
            Fixture->SurvivorInstance = Client1->GetGameInstance();
            TestEqual(TEXT("exact Tween PIE world count"), ck::auto_test::net::Get_AllPIEWorlds().Num(), 3);
            TestEqual(TEXT("Tween server topology"), Server->GetNetMode(),
                Dedicated ? NM_DedicatedServer : NM_ListenServer);
            TestTrue(TEXT("one authoritative actor set and two matching client sets"),
                AllWorldsReady(*Fixture, Fixture->InitialGeneration));
            for (int32 Easing = 0; Easing < Fixture->EasingCount; ++Easing)
            {
                auto* Actor = GetActorAt(Server, *Fixture, Easing);
                const auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
                TArray<FCk_Handle> Tweens;
                CollectTweenChildren(Entity, Tweens);
                if (!TestTrue(TEXT("each original actor has one live actor-owned Tween"),
                    IsValid(Actor) && ck::IsValid(Entity) && Tweens.Num() == 1))
                { return false; }
                Fixture->OriginalActors.Add(Actor);
                Fixture->OriginalEntities.Add(Entity);
                Fixture->OriginalTweens.Add(Tweens[0]);
            }
            auto* ServerSample = GetActorAt(Server, *Fixture, 0);
            auto* Client0Sample = GetActorAt(ck::auto_test::net::Get_ClientWorld(0), *Fixture, 0);
            auto* Client1Sample = GetActorAt(Client1, *Fixture, 0);
            if (TestTrue(TEXT("all initial Tween movement samples exist"),
                IsValid(ServerSample) && IsValid(Client0Sample) && IsValid(Client1Sample)))
            {
                Fixture->InitialServerLocation = ServerSample->GetActorLocation();
                Fixture->InitialClient0Location = Client0Sample->GetActorLocation();
                Fixture->InitialClient1Location = Client1Sample->GetActorLocation();
            }
            return true;
        }), TEXT("Tween shared set has unique easing and actor-owned ECS tweens")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* ServerSample = GetActorAt(ck::auto_test::net::Get_ServerWorld(), *Fixture, 0);
            auto* Client0Sample = GetActorAt(ck::auto_test::net::Get_ClientWorld(0), *Fixture, 0);
            auto* Client1Sample = GetActorAt(ck::auto_test::net::Get_ClientWorld(1), *Fixture, 0);
            return IsValid(ServerSample) && IsValid(Client0Sample) && IsValid(Client1Sample) &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration) &&
                FMath::Abs(ServerSample->GetActorLocation().Z - Fixture->InitialServerLocation.Z) > 20.0 &&
                FMath::Abs(Client0Sample->GetActorLocation().Z - Fixture->InitialClient0Location.Z) > 20.0 &&
                FMath::Abs(Client1Sample->GetActorLocation().Z - Fixture->InitialClient1Location.Z) > 20.0 &&
                FMath::Abs(ServerSample->GetActorLocation().Z - Client0Sample->GetActorLocation().Z) < 100.0 &&
                FMath::Abs(ServerSample->GetActorLocation().Z - Client1Sample->GetActorLocation().Z) < 100.0;
        }), 20.0, TEXT("server and both clients observe matching replicated Tween motion")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning client submits invalid Tween replay serial"),
                InvokeReset(Controller, *Fixture, 0));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* Controller = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            return IsValid(Controller) && Controller->IsA(Fixture->Controller) &&
                Fixture->LastResetAttemptSerial->GetPropertyValue_InContainer(Controller) == 0 &&
                Fixture->LastResetResult->GetPropertyValue_InContainer(Controller) == 4 &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration);
        }), 15.0, TEXT("invalid owning replay is rejected without changing Tween generation or count")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("owning client sends serialled Tween replay"),
                InvokeReset(Controller, *Fixture, Fixture->ResetSerial));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* Controller = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            if (!IsValid(Controller) || !Controller->IsA(Fixture->Controller) ||
                Fixture->LastResetSerial->GetPropertyValue_InContainer(Controller) != Fixture->ResetSerial ||
                Fixture->LastResetResult->GetPropertyValue_InContainer(Controller) != 1 ||
                !AllWorldsReady(*Fixture, Fixture->InitialGeneration + 1))
            { return false; }
            if (Fixture->OriginalActors.Num() != Fixture->EasingCount ||
                Fixture->OriginalEntities.Num() != Fixture->EasingCount ||
                Fixture->OriginalTweens.Num() != Fixture->EasingCount)
            { return false; }
            for (int32 Easing = 0; Easing < Fixture->EasingCount; ++Easing)
            {
                if (Fixture->OriginalActors[Easing].IsValid() ||
                    ck::IsValid(Fixture->OriginalEntities[Easing]) ||
                    ck::IsValid(Fixture->OriginalTweens[Easing]) ||
                    GetActorAt(ck::auto_test::net::Get_ServerWorld(), *Fixture, Easing) ==
                        Fixture->OriginalActors[Easing].Get())
                { return false; }
            }
            return true;
        }), 45.0, TEXT("accepted replay retires original actor-owned tweens and converges once")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            if (ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            if (!TestTrue(TEXT("same Tween GameState owns the replacement set"),
                IsValid(State) && State == Fixture->SurvivorState.Get() &&
                AllWorldsReady(*Fixture, Fixture->InitialGeneration + 1)))
            { return false; }
            for (int32 Easing = 0; Easing < Fixture->EasingCount; ++Easing)
            { Fixture->SurvivorActors.Add(GetActorAt(Server, *Fixture, Easing)); }
            return true;
        }), TEXT("Tween replay has no duplicate actor or ECS set")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Driver = IsValid(Server) ? Server->GetNetDriver() : nullptr;
            auto* RemotePC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* LocalPC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            auto* Instance = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
            auto* Sample = GetActorAt(Server, *Fixture, 0);
            if (!TestTrue(TEXT("departing Tween owner has live server transport and shared actor"),
                IsValid(Driver) && IsValid(RemotePC) && IsValid(LocalPC) &&
                IsValid(Instance) && IsValid(Sample))) { return; }
            Fixture->DepartingServerController = RemotePC;
            Fixture->DepartingInstance = Instance;
            Fixture->DepartureLocation = Sample->GetActorLocation();
            LocalPC->ClientTravel(TEXT("/Engine/Maps/Entry"), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* SurvivorInstance = Fixture->SurvivorInstance.Get();
            auto* Survivor = IsValid(SurvivorInstance) ? SurvivorInstance->GetWorld() : nullptr;
            auto* DepartingInstance = Fixture->DepartingInstance.Get();
            auto* Departed = IsValid(DepartingInstance) ? DepartingInstance->GetWorld() : nullptr;
            auto* Sample = GetActorAt(Server, *Fixture, 0);
            auto* SurvivorSample = GetActorAt(Survivor, *Fixture, 0);
            if (!IsValid(State) || State != Fixture->SurvivorState.Get() ||
                Fixture->DepartingServerController.IsValid() ||
                !IsValid(Departed) || !Departed->GetMapName().Contains(TEXT("Entry")) ||
                !SetIsReady(Server, *Fixture, Fixture->InitialGeneration + 1, true) ||
                !SetIsReady(Survivor, *Fixture, Fixture->InitialGeneration + 1, false) ||
                !IsValid(Sample) || !IsValid(SurvivorSample) ||
                FMath::Abs(Sample->GetActorLocation().Z - Fixture->DepartureLocation.Z) < 20.0 ||
                FMath::Abs(SurvivorSample->GetActorLocation().Z - Fixture->DepartureLocation.Z) < 20.0)
            { return false; }
            for (int32 Easing = 0; Easing < Fixture->SurvivorActors.Num(); ++Easing)
            { if (Fixture->SurvivorActors[Easing].Get() != GetActorAt(Server, *Fixture, Easing)) { return false; } }
            return true;
        }), 45.0, TEXT("real owner departure leaves same Tween set moving for surviving client")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            Fixture->RestoreSettings();
            TestTrue(TEXT("Tween PIE worlds are gone after cleanup"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
            return true;
        }), TEXT("Tween network test cleanup and user settings restoration"), true));
    return true;
}

#endif
