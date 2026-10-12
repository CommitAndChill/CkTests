#include "CkTests/Net/CkNetAutomation_Common.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace ck_tests_late_join
{
    struct FPlayer
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<APlayerController> Controller;
        TWeakObjectPtr<APawn> Pawn;
        TWeakObjectPtr<APlayerState> PlayerState;
    };
    struct FScenario
    {
        TArray<TWeakObjectPtr<UWorld>> Worlds;
        TArray<FPlayer> Players;
        TArray<TWeakObjectPtr<APlayerController>> Remote;
        TWeakObjectPtr<AGameModeBase> Mode;
    };
    auto Ready(int32 Count) -> bool
    {
        using namespace ck::auto_test::net;
        const auto Worlds = Get_AllPIEWorlds();
        auto* Server = Get_ServerWorld();
        if (Worlds.Num() != Count || Get_NumClientWorlds() != Count - 1 || !IsValid(Server))
        { return false; }
        for (auto* World : Worlds)
        {
            if (!IsValid(World) || !World->HasBegunPlay())
            { return false; }
            int32 PCs = 0;
            for (auto It = World->GetPlayerControllerIterator(); It; ++It)
            {
                auto* PC = It->Get();
                if (!IsValid(PC) || !PC->HasActorBegunPlay() || PC->GetWorld() != World)
                { return false; }
                APawn* Pawn = PC->GetPawn();
                auto* PS = PC->PlayerState.Get();
                if (!IsValid(Pawn) || Pawn->GetWorld() != World || !Pawn->HasActorBegunPlay() ||
                    Pawn->GetController() != PC || !Pawn->IsA(ADefaultPawn::StaticClass()) ||
                    !IsValid(PS) || PS->GetWorld() != World || !PS->HasActorBegunPlay() || Pawn->GetPlayerState() != PS)
                { return false; }
                ++PCs;
            }
            const auto Expected = World == Server ? Count - (Server->GetNetMode() == NM_DedicatedServer ? 1 : 0) : 1;
            if (PCs != Expected)
            { return false; }
            if (World != Server)
            {
                auto* PC = World->GetFirstPlayerController();
                auto* GI = World->GetGameInstance();
                auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
                if (!IsValid(PC) || !PC->IsLocalController() || !IsValid(GI) || GI->GetWorld() != World ||
                    GI->GetLocalPlayers().Num() != 1 || !IsValid(LP) || GI->GetLocalPlayers()[0] != LP ||
                    LP->GetPlayerController(World) != PC)
                { return false; }
            }
        }
        TArray<APlayerController*> Remote;
        for (int32 I = 0; I < Count - 1; ++I)
        {
            auto* PC = TryGet_RemoteClientPlayerController(Server, I);
            auto* Client = Get_ClientWorld(I);
            auto* Local = IsValid(Client) ? Client->GetFirstPlayerController() : nullptr;
            if (!IsValid(PC) || Remote.Contains(PC) || !IsValid(Local) || !Local->IsLocalController())
            { return false; }
            Remote.Add(PC);
        }
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkNetAutomation_LateJoin, "Ck.Net.Harness.LateJoin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkNetAutomation_LateJoin::RunTest(const FString& Parameters)
{
    using namespace ck_tests_late_join;
    using namespace ck::auto_test::net;
    const auto Scenario = MakeShared<FScenario>();
    // AGameModeBase's production startup uses DefaultPawn, PlayerController and PlayerState.
    // This harness test deliberately has no two-player gym capacity/cadence contract.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters,
        AGameModeBase::StaticClass()));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        []() { return Ready(2); }), 60.0, TEXT("initial real player possession and server correlation")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [Scenario](UWorld* Server)
        {
            if (!Ready(2))
            { FailSession(TEXT("late-join smoke lost initial readiness before capture")); return; }
            auto* Mode = Server->GetAuthGameMode();
            if (!IsValid(Mode) || Mode->GetClass() != AGameModeBase::StaticClass())
            { FailSession(TEXT("late-join smoke did not start the native base GameMode")); return; }
            Scenario->Mode = Mode;
            for (auto* World : Get_AllPIEWorlds())
            {
                Scenario->Worlds.Add(World);
                for (auto It = World->GetPlayerControllerIterator(); It; ++It)
                {
                    auto* PC = It->Get();
                    if (!IsValid(PC))
                    { FailSession(TEXT("late-join smoke lost original controller before capture")); return; }
                    Scenario->Players.Add({World, PC, PC->GetPawn(), PC->PlayerState.Get()});
                }
            }
            Scenario->Remote.Add(TryGet_RemoteClientPlayerController(Server, 0));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RequestLateJoin(60.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda(
        [Scenario]() -> bool
        {
            if (!Ready(3) || Scenario->Worlds.Num() != 2 || Scenario->Remote.Num() != 1)
            { return false; }
            auto* Server = Get_ServerWorld();
            const auto Worlds = Get_AllPIEWorlds();
            if (!Scenario->Mode.IsValid() || Server->GetAuthGameMode() != Scenario->Mode.Get() ||
                Worlds[0] != Scenario->Worlds[0].Get() || Worlds[1] != Scenario->Worlds[1].Get() ||
                Worlds[2] == Scenario->Worlds[0].Get() || Worlds[2] == Scenario->Worlds[1].Get() ||
                Scenario->Remote[0].Get() != TryGet_RemoteClientPlayerController(Server, 0))
            { return false; }
            for (const auto& Original : Scenario->Players)
            {
                auto* PC = Original.Controller.Get();
                if (!Original.World.IsValid() || !IsValid(PC) || !Original.Pawn.IsValid() || !Original.PlayerState.IsValid() ||
                    PC->GetWorld() != Original.World.Get() || PC->GetPawn() != Original.Pawn.Get() ||
                    PC->PlayerState.Get() != Original.PlayerState.Get())
                { return false; }
            }
            auto* AddedRemote = TryGet_RemoteClientPlayerController(Server, 1);
            return IsValid(AddedRemote) && AddedRemote != Scenario->Remote[0].Get();
        }), TEXT("one real late client joins while every original world/player identity survives")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda(
        [Scenario]() -> bool
        {
            if (!Get_AllPIEWorlds().IsEmpty())
            { return false; }
            for (const auto& World : Scenario->Worlds)
            { if (World.IsValid()) { return false; } }
            for (const auto& Player : Scenario->Players)
            {
                if (Player.Controller.IsValid() || Player.Pawn.IsValid() || Player.PlayerState.IsValid())
                { return false; }
            }
            return !Scenario->Mode.IsValid();
        }), TEXT("unconditional EndPIE destroys the entire captured session"), true));
    return true;
}
// Invalid admission must reject before changing the expected census or invoking engine creation.
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkNetAutomation_LateJoinInvalidTimeout, "Ck.Net.Harness.LateJoin.InvalidTimeout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkNetAutomation_LateJoinInvalidTimeout::RunTest(const FString& Parameters)
{
    using namespace ck_tests_late_join;
    using namespace ck::auto_test::net;
    const auto Worlds = MakeShared<TArray<TWeakObjectPtr<UWorld>>>();
    const auto BodyRan = MakeShared<bool>(false);
    AddExpectedError(TEXT("Late join requires a finite positive timeout"), EAutomationExpectedErrorFlags::Contains, 1);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters,
        AGameModeBase::StaticClass()));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        []() { return Ready(2); }), 60.0, TEXT("ready original topology before invalid late-join admission")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [Worlds](UWorld*)
        { for (auto* World : Get_AllPIEWorlds()) { Worlds->Add(World); } })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RequestLateJoin(0.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [BodyRan](UWorld*) { *BodyRan = true; })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda(
        [Worlds, BodyRan]() -> bool
        {
            const auto Actual = Get_AllPIEWorlds();
            return Get_HasSessionFailed() && !*BodyRan && Ready(2) && Actual.Num() == 2 && Worlds->Num() == 2 &&
                Actual[0] == (*Worlds)[0].Get() && Actual[1] == (*Worlds)[1].Get();
        }), TEXT("invalid admission retains original healthy census and suppresses downstream body"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda(
        []() { return Get_AllPIEWorlds().IsEmpty(); }), TEXT("invalid late join still tears down PIE"), true));
    return true;
}
#endif
