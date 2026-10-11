#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "CkGym_StartupSettings.h"
#include "CkGym_NetworkRuntime.h"
#include "CkGym_Registry.h"

#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Utils.h"
#include "CkAttribute/IntegerAttribute/CkIntegerAttribute_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

namespace ck_tests_replication_gym
{
    constexpr auto MapPath = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    constexpr auto ModePath = TEXT("/Script/Angelscript.Ck_ReplicationGym_GameMode");
    constexpr auto StatePath = TEXT("/Script/Angelscript.Ck_ReplicationGym_GameState");
    constexpr auto ActorPath = TEXT("/Script/Angelscript.Ck_ReplicationGym_ReplicatedActor");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_ReplicationGym_Pawn");
    constexpr auto ControllerPath = TEXT("/Script/Angelscript.Ck_ReplicationGym_PlayerController");

    struct FFixture
    {
        ECkGym_StartupMode OriginalMode = ECkGym_StartupMode::Cycler;
        FString OriginalLastGym;
        TArray<FString> OriginalRecents;
        UClass* Mode = nullptr;
        UClass* State = nullptr;
        UClass* Actor = nullptr;
        UClass* Pawn = nullptr;
        UClass* Controller = nullptr;
        FObjectProperty* SharedActor = nullptr;
        FBoolProperty* Ready = nullptr;
        FBoolProperty* Failed = nullptr;
        FIntProperty* Preset = nullptr;
        FIntProperty* ActionResult = nullptr;
        FIntProperty* ActionSerial = nullptr;
        FBoolProperty* ExtraEntityReady = nullptr;
        TWeakObjectPtr<AActor> OriginalActor;
        TWeakObjectPtr<AActor> LastResetActor;
        TWeakObjectPtr<AActor> SurvivorActor;
        TWeakObjectPtr<APawn> DetachedPawn;
        TWeakObjectPtr<APlayerController> DetachedController;
        TWeakObjectPtr<APlayerController> DepartingServerController;
        TWeakObjectPtr<UGameInstance> DepartingClientInstance;

        void CaptureSettings()
        {
            const auto* Settings = GetDefault<UCkGym_StartupSettings>();
            OriginalMode = Settings->StartupMode;
            OriginalLastGym = Settings->LastGymName;
            OriginalRecents = Settings->RecentGymNames;
        }

        void RestoreSettings() const
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = OriginalMode;
            Settings->LastGymName = OriginalLastGym;
            Settings->RecentGymNames = OriginalRecents;
            Settings->SaveConfig();
        }
    };

    auto GetState(UWorld* InWorld, const FFixture& InFixture) -> ACkGym_GameState_UE*
    {
        auto* State = IsValid(InWorld) ? InWorld->GetGameState() : nullptr;
        return IsValid(State) && State->IsA(InFixture.State) ? Cast<ACkGym_GameState_UE>(State) : nullptr;
    }

    auto GetSharedActor(AGameStateBase* InState, const FFixture& InFixture) -> AActor*
    {
        return IsValid(InState)
            ? Cast<AActor>(InFixture.SharedActor->GetObjectPropertyValue_InContainer(InState)) : nullptr;
    }

    auto CountSharedActors(UWorld* InWorld, const FFixture& InFixture) -> int32
    {
        int32 Count = 0;
        if (!IsValid(InWorld)) { return 0; }
        for (TActorIterator<AActor> It(InWorld, InFixture.Actor); It; ++It)
        { if (IsValid(*It)) { ++Count; } }
        return Count;
    }

    auto HasValue(UWorld* InWorld, const FFixture& InFixture, int32 InValue) -> bool
    {
        auto* State = GetState(InWorld, InFixture);
        auto* Actor = GetSharedActor(State, InFixture);
        if (!IsValid(Actor) || CountSharedActors(InWorld, InFixture) != 1)
        { return false; }
        auto Entity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor);
        if (ck::Is_NOT_Valid(Entity))
        { return false; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("IntegerAttribute.ReplicationGym.ActorValue")));
        const auto Attribute = UCk_Utils_IntegerAttribute_UE::TryGet(Entity, Tag);
        return ck::IsValid(Attribute) &&
            UCk_Utils_IntegerAttribute_UE::Get_FinalValue(Attribute) == InValue;
    }

    auto HasOwnedPawnValue(UWorld* InWorld, const FFixture& InFixture, int32 InValue) -> bool
    {
        auto* Controller = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
        auto* Pawn = IsValid(Controller) ? Controller->GetPawn().Get() : nullptr;
        auto* Shared = GetSharedActor(GetState(InWorld, InFixture), InFixture);
        if (!IsValid(Pawn) || !IsValid(Shared))
        { return false; }
        const auto Context = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Shared);
        if (ck::Is_NOT_Valid(Context))
        { return false; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("IntegerAttribute.ReplicationGym.PawnValue")));
        int32 Matches = 0;
        int32 Value = 0;
        for (const auto& Entity : UCk_Utils_EntityTag_UE::ForEach_Entity(
            Context, FName(TEXT("TAG_ReplicationGym_PawnExtra"))))
        {
            if (UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Entity) != Pawn)
            { continue; }
            const auto Attribute = UCk_Utils_IntegerAttribute_UE::TryGet(Entity, Tag);
            if (ck::Is_NOT_Valid(Attribute))
            { continue; }
            ++Matches;
            Value = UCk_Utils_IntegerAttribute_UE::Get_FinalValue(Attribute);
        }
        return Matches == 1 && Value == InValue;
    }

    auto AllPawnExtrasReadyAtDefault(const FFixture& InFixture, int32 InExpectedWorlds, int32 InExpectedPawns) -> bool
    {
        const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
        if (Worlds.Num() != InExpectedWorlds)
        { return false; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("IntegerAttribute.ReplicationGym.PawnValue")));
        for (auto* World : Worlds)
        {
            auto* Shared = GetSharedActor(GetState(World, InFixture), InFixture);
            if (!IsValid(Shared))
            { return false; }
            const auto Context = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Shared);
            if (ck::Is_NOT_Valid(Context))
            { return false; }
            const auto Entities = UCk_Utils_EntityTag_UE::ForEach_Entity(
                Context, FName(TEXT("TAG_ReplicationGym_PawnExtra")));
            if (Entities.Num() != InExpectedPawns)
            { return false; }

            int32 PawnCount = 0;
            for (TActorIterator<APawn> It(World, InFixture.Pawn); It; ++It)
            {
                auto* Pawn = *It;
                if (!IsValid(Pawn) || Pawn->GetWorld() != World ||
                    !InFixture.ExtraEntityReady->GetPropertyValue_InContainer(Pawn))
                { return false; }
                ++PawnCount;
                int32 Matches = 0;
                for (const auto& Entity : Entities)
                {
                    if (UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Entity) != Pawn)
                    { continue; }
                    ++Matches;
                    const auto Attribute = UCk_Utils_IntegerAttribute_UE::TryGet(Entity, Tag);
                    if (ck::Is_NOT_Valid(Attribute) ||
                        UCk_Utils_IntegerAttribute_UE::Get_FinalValue(Attribute) != 50 ||
                        Entity.Has<ck::FTag_EntityScript_PendingReplicationRetry>() ||
                        !Entity.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
                    { return false; }
                    auto* Driver = Entity.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
                    if (!IsValid(Driver) || Driver->Get_AssociatedEntity() != Entity)
                    { return false; }
                    const auto* Entry = Driver->FindEntry(FCk_RepData_IntegerAttributes::StaticStruct());
                    if (Entry == nullptr || !Entry->Data.IsValid() ||
                        (World->GetNetMode() == NM_Client &&
                            (Entry->_PendingApply || !Entry->_WasEverApplied ||
                                !Entry->_LastAppliedData.IsValid())))
                    { return false; }
                }
                if (Matches != 1)
                { return false; }
            }
            if (PawnCount != InExpectedPawns)
            { return false; }
        }
        return true;
    }

    auto AllWorldsReady(const FFixture& InFixture, int32 InExpectedWorlds, int32 InValue, int32 InPreset) -> bool
    {
        const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
        if (Worlds.Num() != InExpectedWorlds)
        { return false; }
        for (auto* World : Worlds)
        {
            auto* State = GetState(World, InFixture);
            if (!IsValid(State) || !State->Get_HasPublishedCatalog() ||
                !InFixture.Ready->GetPropertyValue_InContainer(State) ||
                InFixture.Failed->GetPropertyValue_InContainer(State) ||
                InFixture.Preset->GetPropertyValue_InContainer(State) != InPreset ||
                !HasValue(World, InFixture, InValue))
            { return false; }
        }
        return true;
    }

    auto InvokeAction(APlayerController* InController, int32 InAction, int32 InPreset) -> bool
    {
        if (!IsValid(InController)) { return false; }
        auto* Function = InController->FindFunction(TEXT("Server_RequestReplicationAction"));
        if (!IsValid(Function)) { return false; }
        struct FParams { int32 InAction; int32 InPresetIndex; } Params{InAction, InPreset};
        InController->ProcessEvent(Function, &Params);
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkReplicationGym_SharedActor,
    "Ck.ReplicationGym.SharedActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkReplicationGym_SharedActor::RunTest(const FString& Parameters)
{
    using namespace ck_tests_replication_gym;
    const int32 WorldCount = 3; // Listen: host plus two remotes; dedicated: server plus two remotes.
    const bool bDedicated = Parameters == TEXT("ClientServer");
    const auto Fixture = MakeShared<FFixture>();
    Fixture->Mode = FSoftClassPath{ModePath}.TryLoadClass<AGameModeBase>();
    Fixture->State = FSoftClassPath{StatePath}.TryLoadClass<AGameStateBase>();
    Fixture->Actor = FSoftClassPath{ActorPath}.TryLoadClass<AActor>();
    Fixture->Pawn = FSoftClassPath{PawnPath}.TryLoadClass<APawn>();
    Fixture->Controller = FSoftClassPath{ControllerPath}.TryLoadClass<APlayerController>();
    if (!TestNotNull(TEXT("authored GameMode"), Fixture->Mode) ||
        !TestNotNull(TEXT("authored GameState"), Fixture->State) ||
        !TestNotNull(TEXT("authored shared actor"), Fixture->Actor) ||
        !TestNotNull(TEXT("authored pawn"), Fixture->Pawn) ||
        !TestNotNull(TEXT("authored controller"), Fixture->Controller))
    { return false; }
    Fixture->SharedActor = FindFProperty<FObjectProperty>(Fixture->State, TEXT("SharedActor"));
    Fixture->Ready = FindFProperty<FBoolProperty>(Fixture->State, TEXT("SharedReady"));
    Fixture->Failed = FindFProperty<FBoolProperty>(Fixture->State, TEXT("SharedFailed"));
    Fixture->Preset = FindFProperty<FIntProperty>(Fixture->State, TEXT("ActorPresetIndex"));
    Fixture->ActionResult = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastActionResult"));
    Fixture->ActionSerial = FindFProperty<FIntProperty>(Fixture->Controller, TEXT("LastActionSerial"));
    Fixture->ExtraEntityReady = FindFProperty<FBoolProperty>(Fixture->Pawn, TEXT("ExtraEntityReady"));
    if (!TestNotNull(TEXT("shared actor property"), Fixture->SharedActor) ||
        !TestNotNull(TEXT("ready property"), Fixture->Ready) ||
        !TestNotNull(TEXT("failed property"), Fixture->Failed) ||
        !TestNotNull(TEXT("preset property"), Fixture->Preset) ||
        !TestNotNull(TEXT("action result property"), Fixture->ActionResult) ||
        !TestNotNull(TEXT("action serial property"), Fixture->ActionSerial) ||
        !TestNotNull(TEXT("pawn extra construction receipt"), Fixture->ExtraEntityReady))
    { return false; }

    Fixture->CaptureSettings();
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(WorldCount, MapPath, Parameters, Fixture->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(WorldCount, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount, bDedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            return AllWorldsReady(*Fixture, WorldCount, 100, -1) &&
                AllPawnExtrasReadyAtDefault(*Fixture, WorldCount, bDedicated ? 2 : 3) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0)) &&
                IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1));
        }),
        45.0, TEXT("shared actor and every replicated gym pawn have linked ECS values on every world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture, bDedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            auto* Instance = IsValid(Server) ? Server->GetGameInstance() : nullptr;
            auto* Registry = IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
            const auto Current = IsValid(Registry) ? Registry->Get_CurrentGymIndex() : INDEX_NONE;
            const auto Replication = IsValid(Registry)
                ? Registry->Find_GymIndexByName(TEXT("Replication")) : INDEX_NONE;
            const bool bMode = IsValid(Server) && IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(Fixture->Mode);
            int32 ControllerCount = 0;
            if (IsValid(Server))
            {
                for (TActorIterator<APlayerController> It(Server); It; ++It)
                { if (IsValid(*It)) { ++ControllerCount; } }
            }
            auto* Remote0 = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* Remote1 = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 1);
            Fixture->OriginalActor = GetSharedActor(State, *Fixture);
            return TestTrue(TEXT("authored server GameMode and singleton"),
                bMode && Current != INDEX_NONE && Current == Replication &&
                ControllerCount == (bDedicated ? 2 : 3) &&
                IsValid(Remote0) && IsValid(Remote1) && Remote0 != Remote1 &&
                IsValid(Fixture->OriginalActor.Get()) && CountSharedActors(Server, *Fixture) == 1 &&
                Fixture->OriginalActor->GetActorLocation().Equals(FVector(500.0, -400.0, 124.0), 1.0));
        }), TEXT("authored server owns exactly one actor")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("remote owning controller sends valid preset"),
                IsValid(Controller) && Controller->IsA(Fixture->Controller) && InvokeAction(Controller, 4, 3));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount]() -> bool
        { return AllWorldsReady(*Fixture, WorldCount, 9999, 3); }),
        30.0, TEXT("client action reaches server and replicated ECS value converges")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("second remote owner sends its own preset"),
                IsValid(Controller) && Controller->IsA(Fixture->Controller) && InvokeAction(Controller, 4, 1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount]() -> bool
        { return AllWorldsReady(*Fixture, WorldCount, 0, 1); }),
        30.0, TEXT("second owner's shared action converges on both clients")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("invalid actions are submitted over real owner transport"),
                InvokeAction(Controller, 4, 99) && InvokeAction(Controller, 99, 0));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture, WorldCount]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* Controller = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            return TestTrue(TEXT("invalid actions reject without mutating shared actor"),
                IsValid(Controller) && Fixture->ActionSerial->GetPropertyValue_InContainer(Controller) >= 3 &&
                Fixture->ActionResult->GetPropertyValue_InContainer(Controller) == 1 &&
                AllWorldsReady(*Fixture, WorldCount, 0, 1) &&
                GetSharedActor(GetState(Server, *Fixture), *Fixture) == Fixture->OriginalActor.Get());
        }), TEXT("rejected controls preserve singleton and expose failure result")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InServer)
        {
            auto* Controller = ck::auto_test::net::Get_RemoteClientPlayerController(InServer, 0);
            auto* Pawn = IsValid(Controller) ? Controller->GetPawn().Get() : nullptr;
            if (!TestTrue(TEXT("remote owner has a pawn before stale-owner attempt"),
                IsValid(Pawn) && Pawn->GetController() == Controller))
            { return; }
            Fixture->DetachedController = Controller;
            Fixture->DetachedPawn = Pawn;
            Controller->UnPossess();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("temporarily unpossessed owner sends rejected action"),
                InvokeAction(Controller, 4, 3));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount]() -> bool
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            auto* Controller = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            return IsValid(Controller) &&
                Fixture->ActionResult->GetPropertyValue_InContainer(Controller) == 4 &&
                AllWorldsReady(*Fixture, WorldCount, 0, 1);
        }), 15.0, TEXT("stale owner action is explicitly rejected without shared mutation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InServer)
        {
            auto* Controller = Fixture->DetachedController.Get();
            auto* Pawn = Fixture->DetachedPawn.Get();
            if (TestTrue(TEXT("detached owner and pawn remain available for restoration"),
                IsValid(InServer) && IsValid(Controller) && IsValid(Pawn)))
            { Controller->Possess(Pawn); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount, bDedicated]() -> bool
        {
            auto* Controller = Fixture->DetachedController.Get();
            return IsValid(Controller) && Controller->GetPawn() == Fixture->DetachedPawn.Get() &&
                AllPawnExtrasReadyAtDefault(*Fixture, WorldCount, bDedicated ? 2 : 3);
        }), 15.0, TEXT("owner pawn restored with all secondary replicas before network departure")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            return HasOwnedPawnValue(ck::auto_test::net::Get_ClientWorld(0), *Fixture, 50) &&
                HasOwnedPawnValue(ck::auto_test::net::Get_ClientWorld(1), *Fixture, 50);
        }), 20.0, TEXT("each remote pawn owns a distinct ready second entity")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("first owner cycles only its pawn to zero"),
                InvokeAction(Controller, 1, -1) && InvokeAction(Controller, 1, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount]() -> bool
        {
            return HasOwnedPawnValue(ck::auto_test::net::Get_ClientWorld(0), *Fixture, 0) &&
                HasOwnedPawnValue(ck::auto_test::net::Get_ClientWorld(1), *Fixture, 50) &&
                AllWorldsReady(*Fixture, WorldCount, 0, 1);
        }), 25.0, TEXT("pawn command changes only initiating owner's replicated ECS value")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("G route sends two pending-safe shared resets"),
                InvokeAction(Controller, 2, -1) && InvokeAction(Controller, 2, -1));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Actor = GetSharedActor(GetState(Server, *Fixture), *Fixture);
            return IsValid(Actor) && Actor != Fixture->OriginalActor.Get() &&
                AllWorldsReady(*Fixture, WorldCount, 100, -1);
        }), 30.0, TEXT("G respawns one new ready actor with default value during pending reset")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([Fixture](UWorld* InServer)
        { Fixture->LastResetActor = GetSharedActor(GetState(InServer, *Fixture), *Fixture); })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            TestTrue(TEXT("second owner can change the replacement actor"), InvokeAction(Controller, 4, 3));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount]() -> bool
        { return AllWorldsReady(*Fixture, WorldCount, 9999, 3); }),
        30.0, TEXT("replacement actor value is shared before console restart")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InClient)
        {
            auto* Controller = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            auto* Restart = IsValid(Controller) ? Controller->FindFunction(TEXT("Ck_Gym_Restart")) : nullptr;
            if (TestNotNull(TEXT("inherited console restart route"), Restart))
            { Controller->ProcessEvent(Restart, nullptr); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Actor = GetSharedActor(GetState(Server, *Fixture), *Fixture);
            return IsValid(Actor) && Actor != Fixture->LastResetActor.Get() &&
                AllWorldsReady(*Fixture, WorldCount, 100, -1);
        }), 30.0, TEXT("console restart respawns one fresh default actor")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([Fixture](UWorld* InServer)
        { Fixture->SurvivorActor = GetSharedActor(GetState(InServer, *Fixture), *Fixture); })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InClient)
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Driver = IsValid(Server) ? Server->GetNetDriver() : nullptr;
            auto* RemoteServerPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(Server, 0);
            auto* LocalPC = IsValid(InClient) ? InClient->GetFirstPlayerController() : nullptr;
            auto* Instance = IsValid(InClient) ? InClient->GetGameInstance() : nullptr;
            if (!TestTrue(TEXT("real reconnect has live server driver and matching remote owner"),
                IsValid(Driver) && IsValid(RemoteServerPC) && IsValid(LocalPC) && IsValid(Instance)))
            { return; }
            const FURL ServerUrl(nullptr, *Driver->LowLevelGetNetworkNumber(), TRAVEL_Absolute);
            if (!TestTrue(TEXT("server advertises a bound loopback port"), ServerUrl.Port > 0))
            { return; }
            Fixture->DepartingServerController = RemoteServerPC;
            Fixture->DepartingClientInstance = Instance;
            LocalPC->ClientTravel(FString::Printf(TEXT("127.0.0.1:%d"), ServerUrl.Port), TRAVEL_Absolute);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Actor = GetSharedActor(GetState(Server, *Fixture), *Fixture);
            return IsValid(Server) && !Fixture->DepartingServerController.IsValid() &&
                IsValid(Actor) && Actor == Fixture->SurvivorActor.Get() &&
                CountSharedActors(Server, *Fixture) == 1 && HasValue(Server, *Fixture, 100);
        }), 30.0, TEXT("actual owning-controller disconnect leaves shared actor alive")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Fixture, WorldCount, bDedicated]() -> bool
        {
            auto* Instance = Fixture->DepartingClientInstance.Get();
            auto* Client = IsValid(Instance) ? Instance->GetWorld() : nullptr;
            auto* NewPC = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Actor = GetSharedActor(GetState(Server, *Fixture), *Fixture);
            return IsValid(NewPC) && NewPC->IsA(Fixture->Controller) &&
                IsValid(Actor) && Actor == Fixture->SurvivorActor.Get() &&
                AllWorldsReady(*Fixture, WorldCount, 100, -1) &&
                AllPawnExtrasReadyAtDefault(*Fixture, WorldCount, bDedicated ? 2 : 3);
        }), 60.0, TEXT("departed PIE client reconnects with every secondary pawn replica")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture, WorldCount]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* State = GetState(Server, *Fixture);
            return TestTrue(TEXT("shared singleton survives real PC departure and rejoin"),
                !Fixture->DepartingServerController.IsValid() &&
                GetSharedActor(State, *Fixture) == Fixture->SurvivorActor.Get() &&
                CountSharedActors(Server, *Fixture) == 1 &&
                AllWorldsReady(*Fixture, WorldCount, 100, -1));
        }), TEXT("shared scenario lifetime is independent of a requesting controller")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Fixture]() -> bool
        {
            Fixture->RestoreSettings();
            return true;
        }), TEXT("restore original gym startup preferences after PIE"), true));
    return true;
}

#endif
