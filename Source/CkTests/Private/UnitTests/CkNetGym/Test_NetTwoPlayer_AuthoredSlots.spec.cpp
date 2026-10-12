// Exercises the authored GameMode, director, and pawn in both network topologies.
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

namespace ck_tests_net_two_player_slots
{
    constexpr auto MapPath = TEXT("/Engine/Maps/Entry");
    constexpr auto GameModePath = TEXT("/Script/Angelscript.Ck_NetGym_TwoPlayer_GameMode");
    constexpr auto PawnPath = TEXT("/Script/Angelscript.Ck_NetGym_TwoPlayer_Pawn");

    struct FRoster
    {
        APawn* Slot0 = nullptr;
        APawn* Slot1 = nullptr;
    };

    struct FScenario
    {
        UClass* GameModeClass = nullptr;
        UClass* PawnClass = nullptr;
        FIntProperty* SlotProperty = nullptr;
        FBoolProperty* ReadyProperty = nullptr;
        TWeakObjectPtr<APawn> Survivor;
        TWeakObjectPtr<APlayerController> RestartController;
        TArray<TWeakObjectPtr<APawn>> InitialSlot0;
        TArray<TWeakObjectPtr<APawn>> InitialSlot1;
        int32 SurvivorSlot = -1;
    };

    auto GetSlot(const FScenario& InScenario, APawn* InPawn) -> int32
    { return InScenario.SlotProperty->GetPropertyValue_InContainer(InPawn); }

    auto IsReady(const FScenario& InScenario, APawn* InPawn) -> bool
    { return InScenario.ReadyProperty->GetPropertyValue_InContainer(InPawn); }

    auto TryGetReadyRoster(UWorld* InWorld, const FScenario& InScenario, FRoster& OutRoster) -> bool
    {
        OutRoster = {};
        if (!IsValid(InWorld))
        { return false; }

        int32 PawnCount = 0;
        for (TActorIterator<APawn> It(InWorld); It; ++It)
        {
            auto* Pawn = *It;
            if (!IsValid(Pawn) || !Pawn->IsA(InScenario.PawnClass))
            { continue; }

            ++PawnCount;
            if (!IsReady(InScenario, Pawn))
            { return false; }
            const auto Slot = GetSlot(InScenario, Pawn);
            if (Slot == 0 && OutRoster.Slot0 == nullptr)
            { OutRoster.Slot0 = Pawn; }
            else if (Slot == 1 && OutRoster.Slot1 == nullptr)
            { OutRoster.Slot1 = Pawn; }
            else
            { return false; }
        }
        return PawnCount == 2 && OutRoster.Slot0 != nullptr && OutRoster.Slot1 != nullptr;
    }

    auto AllWorldsHaveReadyRoster(const FScenario& InScenario, int32 InExpectedWorlds) -> bool
    {
        const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
        if (Worlds.Num() != InExpectedWorlds)
        { return false; }
        for (auto* World : Worlds)
        {
            FRoster Roster;
            if (!TryGetReadyRoster(World, InScenario, Roster))
            { return false; }
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkNetTwoPlayer_AuthoredSlots,
    "Ck.NetGym.TwoPlayer.AuthoredSlots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkNetTwoPlayer_AuthoredSlots::RunTest(const FString& Parameters)
{
    using namespace ck_tests_net_two_player_slots;

    const auto bDedicated = Parameters == TEXT("ClientServer");
    const auto ExpectedWorlds = bDedicated ? 3 : 2;
    const auto Scenario = MakeShared<FScenario>();
    Scenario->GameModeClass = FSoftClassPath{GameModePath}.TryLoadClass<AGameModeBase>();
    Scenario->PawnClass = FSoftClassPath{PawnPath}.TryLoadClass<APawn>();
    if (!TestNotNull(TEXT("authored NetTwoPlayer GameMode class loads"), Scenario->GameModeClass) ||
        !TestNotNull(TEXT("authored NetTwoPlayer pawn class loads"), Scenario->PawnClass))
    { return false; }

    Scenario->SlotProperty = FindFProperty<FIntProperty>(Scenario->PawnClass, TEXT("PlayerSlot"));
    Scenario->ReadyProperty = FindFProperty<FBoolProperty>(Scenario->PawnClass, TEXT("RosterReady"));
    if (!TestNotNull(TEXT("replicated numeric slot is reflected"), Scenario->SlotProperty) ||
        !TestNotNull(TEXT("replicated roster status is reflected"), Scenario->ReadyProperty))
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        ExpectedWorlds, MapPath, Parameters, Scenario->GameModeClass));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(ExpectedWorlds, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario, ExpectedWorlds]() -> bool
        { return AllWorldsHaveReadyRoster(*Scenario, ExpectedWorlds); }),
        60.0, TEXT("authored director assigns two distinct replicated slots in every PIE world")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Scenario, ExpectedWorlds, bDedicated]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!TestTrue(TEXT("server runs authored NetTwoPlayer GameMode"),
                IsValid(Server) && IsValid(Server->GetAuthGameMode()) &&
                Server->GetAuthGameMode()->IsA(Scenario->GameModeClass)))
            { return false; }

            if (!TestTrue(TEXT("all network worlds have exactly two ready, distinct slots"),
                AllWorldsHaveReadyRoster(*Scenario, ExpectedWorlds)))
            { return false; }

            int32 ServerControllers = 0;
            for (TActorIterator<APlayerController> It(Server); It; ++It)
            { if (IsValid(*It)) { ++ServerControllers; } }
            TestEqual(TEXT("server has two player controllers"), ServerControllers, 2);

            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            bool bRolesCorrect = ServerControllers == 2;
            Scenario->InitialSlot0.Reset();
            Scenario->InitialSlot1.Reset();
            for (int32 WorldIndex = 0; WorldIndex < Worlds.Num(); ++WorldIndex)
            {
                FRoster Roster;
                if (!TryGetReadyRoster(Worlds[WorldIndex], *Scenario, Roster))
                { return false; }
                Scenario->InitialSlot0.Add(Roster.Slot0);
                Scenario->InitialSlot1.Add(Roster.Slot1);
                int32 LocallyControlled = 0;
                for (auto* Pawn : {Roster.Slot0, Roster.Slot1})
                { if (Pawn->IsLocallyControlled()) { ++LocallyControlled; } }
                const auto ExpectedLocal = WorldIndex == 0 && bDedicated ? 0 : 1;
                bRolesCorrect &= TestEqual(FString::Printf(TEXT("world %d local pawn count"), WorldIndex),
                    LocallyControlled, ExpectedLocal);
            }
            return bRolesCorrect;
        }), TEXT("authored GameMode, player count, and per-world ownership")));

    // Remove one real player pawn. The surviving pawn retains its numeric slot,
    // but the director must withdraw readiness and stop presenting a full roster.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld* InServer)
        {
            FRoster Roster;
            auto* RemotePC = ck::auto_test::net::Get_RemoteClientPlayerController(InServer, 0);
            if (!TestTrue(TEXT("remote player and authored roster exist before departure"),
                IsValid(RemotePC) && TryGetReadyRoster(InServer, *Scenario, Roster)))
            { return; }

            auto* Departing = RemotePC->GetPawn().Get();
            if (!TestTrue(TEXT("departing controller possesses an authored pawn"),
                IsValid(Departing) && Departing->IsA(Scenario->PawnClass)))
            { return; }

            Scenario->Survivor = Departing == Roster.Slot0 ? Roster.Slot1 : Roster.Slot0;
            Scenario->SurvivorSlot = GetSlot(*Scenario, Scenario->Survivor.Get());
            Scenario->RestartController = RemotePC;
            RemotePC->UnPossess();
            Departing->Destroy();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario, ExpectedWorlds]() -> bool
        {
            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            if (Worlds.Num() != ExpectedWorlds ||
                Scenario->InitialSlot0.Num() != ExpectedWorlds ||
                Scenario->InitialSlot1.Num() != ExpectedWorlds)
            { return false; }
            for (int32 WorldIndex = 0; WorldIndex < ExpectedWorlds; ++WorldIndex)
            {
                auto* Survivor = (Scenario->SurvivorSlot == 0
                    ? Scenario->InitialSlot0[WorldIndex] : Scenario->InitialSlot1[WorldIndex]).Get();
                if (!IsValid(Survivor) || Survivor->GetWorld() != Worlds[WorldIndex] ||
                    GetSlot(*Scenario, Survivor) != Scenario->SurvivorSlot ||
                    IsReady(*Scenario, Survivor))
                { return false; }
            }
            return true;
        }), 10.0, TEXT("director pauses incomplete roster while preserving survivor slot")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld* InServer)
        {
            auto* Controller = Scenario->RestartController.Get();
            auto* GameMode = IsValid(InServer) ? InServer->GetAuthGameMode() : nullptr;
            if (!TestTrue(TEXT("authored GameMode and remote controller survive departure"),
                IsValid(Controller) && IsValid(GameMode) && GameMode->IsA(Scenario->GameModeClass)))
            { return; }
            GameMode->RestartPlayer(Controller);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Scenario, ExpectedWorlds]() -> bool
        {
            if (!AllWorldsHaveReadyRoster(*Scenario, ExpectedWorlds) ||
                Scenario->InitialSlot0.Num() != ExpectedWorlds ||
                Scenario->InitialSlot1.Num() != ExpectedWorlds)
            { return false; }

            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            for (int32 WorldIndex = 0; WorldIndex < Worlds.Num(); ++WorldIndex)
            {
                FRoster Roster;
                if (!TryGetReadyRoster(Worlds[WorldIndex], *Scenario, Roster))
                { return false; }
                auto* Survivor = Scenario->SurvivorSlot == 0 ? Roster.Slot0 : Roster.Slot1;
                auto* InitialSurvivor = (Scenario->SurvivorSlot == 0
                    ? Scenario->InitialSlot0[WorldIndex] : Scenario->InitialSlot1[WorldIndex]).Get();
                if (Survivor != InitialSurvivor)
                { return false; }
                auto* Replacement = Scenario->SurvivorSlot == 0 ? Roster.Slot1 : Roster.Slot0;
                auto* Original = Scenario->SurvivorSlot == 0
                    ? Scenario->InitialSlot1[WorldIndex].Get() : Scenario->InitialSlot0[WorldIndex].Get();
                if (Replacement == Original)
                { return false; }
            }
            return true;
        }),
        60.0, TEXT("authored replacement pawn rejoins two-player roster in every world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Scenario]() -> bool
        {
            FRoster Roster;
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!TestTrue(TEXT("replacement restores complete server roster"),
                TryGetReadyRoster(Server, *Scenario, Roster)))
            { return false; }
            auto* Survivor = Scenario->Survivor.Get();
            auto* Replacement = Scenario->SurvivorSlot == 0 ? Roster.Slot1 : Roster.Slot0;
            auto* Original = Scenario->SurvivorSlot == 0
                ? Scenario->InitialSlot1[0].Get() : Scenario->InitialSlot0[0].Get();
            return TestTrue(TEXT("survivor keeps its original replicated slot"),
                IsValid(Survivor) && GetSlot(*Scenario, Survivor) == Scenario->SurvivorSlot &&
                (Scenario->SurvivorSlot == 0 ? Roster.Slot0 : Roster.Slot1) == Survivor) &&
                TestTrue(TEXT("restarted controller possesses a new pawn in the other slot"),
                IsValid(Replacement) && Replacement != Survivor && Replacement != Original &&
                IsValid(Scenario->RestartController.Get()) &&
                Scenario->RestartController->GetPawn() == Replacement);
        }), TEXT("stable slots across departure and authored GameMode restart")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif
