#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && WITH_ANGELSCRIPT_CK

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck::hex_grid_rendered_gym
{
    auto Registry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*;
    auto Controller(UWorld* InWorld, FName InClass) -> APlayerController*;
    auto Status(UWorld* InWorld, FName InClass, FName InGetter) -> FString;
    auto Capture(FAutomationTestBase& InTest, UWorld* InWorld, const FString& InName, bool InRules,
        bool InBlocked, const TCHAR* InDirectory) -> void;
}

namespace ck::hex_net_gym_test
{
    using namespace ck::hex_grid_rendered_gym;
    using Occupancy = UCk_Utils_HexGridOccupancy_UE;
    const FName ControllerClass(TEXT("Ck_HexNetGym_Controller"));

    auto DisplayedId(UWorld* World) -> TOptional<FGuid>
    {
        auto* PC = Controller(World, ControllerClass);
        auto* Fn = PC ? PC->FindFunction(TEXT("Get_NetPlacementId")) : nullptr;
        const auto* Prop = Fn ? CastField<FStructProperty>(Fn->GetReturnProperty()) : nullptr;
        if (!Prop || Prop->Struct != TBaseStructure<FGuid>::Get()) { return {}; }
        FStructOnScope Params(Fn);
        PC->ProcessEvent(Fn, Params.GetStructMemory());
        return *Prop->ContainerPtrToValuePtr<FGuid>(Params.GetStructMemory());
    }

    bool Matches(UWorld* World, FGuid Expected, FCk_HexCoord Cell)
    {
        if (Status(World, ControllerClass, TEXT("Get_NetBoardStatus")) != TEXT("Ready") || DisplayedId(World) != Expected)
        { return false; }
        TActorIterator<ACk_AutoTest_NetSubject_MixedGrid_UE> Board(World);
        TActorIterator<ACk_AutoTest_NetSubject_MixedGridObject_UE> Piece(World);
        if (!Board || !Piece || !ck::IsValid(Piece->Get_HexObject())) { return false; }
        const auto Grid = Board->Get_HexGrid();
        const auto Rows = Occupancy::Get_Placements(Grid);
        if (!Rows.Get_Succeeded() || Rows.Get_Placements().Num() != (Expected.IsValid() ? 1 : 0)) { return false; }
        if (!Expected.IsValid()) { return !Occupancy::TryGet_PlacementForOccupant(Piece->Get_HexObject()).Get_Succeeded(); }
        const auto& Row = Rows.Get_Placements()[0];
        return Row.Get_PlacementId() == Expected && Row.Get_Cells() == TArray<FCk_HexCoord>{Cell} &&
            Row.Get_OccupantState() == ECk_HexGridOccupantState::Bound && Row.Get_Occupant() == Piece->Get_HexObject() &&
            Occupancy::TryGet_PlacementAt(Grid, Cell).Get_PlacementId() == Expected &&
            Occupancy::TryGet_PlacementForOccupant(Piece->Get_HexObject()).Get_PlacementId() == Expected;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexNetRenderedGym, "Ck.HexGrid.Net.RenderedGym",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCkHexNetRenderedGym::RunTest(const FString&)
{
    using namespace ck::hex_net_gym_test;
    using namespace ck::auto_test::net;
    auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    const auto Mode = Settings->StartupMode;
    const auto Last = Settings->LastGymName;
    const auto Recents = Settings->RecentGymNames;
    Settings->StartupMode = ECkGym_StartupMode::Cycler;
    const auto Id = MakeShared<FGuid>();
    auto Wait = [this](const TCHAR* Message, auto Condition)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
            [this, Condition]() { return HasAnyErrors() || Condition(); }), 30.f, Message));
    };
    auto Drive = [this](FName Command)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Command](UWorld* World)
            {
                if (HasAnyErrors()) { return; }
                auto* PC = Controller(World, ControllerClass);
                auto* Fn = PC ? PC->FindFunction(Command) : nullptr;
                if (TestNotNull(Command.ToString(), Fn)) { PC->ProcessEvent(Fn, nullptr); }
            })));
    };
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.f));
    Wait(TEXT("Listen server registered Hex Replication and connected the original player"), []()
    {
        auto* World = Get_ServerWorld();
        auto* Reg = Registry(World);
        auto* Net = World ? World->GetNetDriver() : nullptr;
        return Net && Net->ClientConnections.Num() == 1 && Reg && Reg->Find_GymIndexByName(TEXT("Hex Replication")) != INDEX_NONE;
    });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        auto* Reg = Registry(World);
        if (!HasAnyErrors() && Reg) { Reg->Request_TravelToGym(Reg->Find_GymIndexByName(TEXT("Hex Replication"))); }
    })));
    Wait(TEXT("Registered gym constructed both controllers with empty ready boards"), []()
    {
        return Get_ClientWorld(0) && Get_ClientWorld(0)->GetNetMode() == NM_Client &&
            Matches(Get_ServerWorld(), {}, {}) && Matches(Get_ClientWorld(0), {}, {});
    });
    Drive(TEXT("Ck_HexNet_Place"));
    Wait(TEXT("Both controllers show the same placement ID and bound local piece at the origin"), [Id]()
    {
        const auto Seen = DisplayedId(Get_ServerWorld());
        if (!Seen.IsSet() || !Seen->IsValid()) { return false; }
        *Id = Seen.GetValue();
        return Matches(Get_ServerWorld(), *Id, {}) && Matches(Get_ClientWorld(0), *Id, {});
    });
    Drive(TEXT("Ck_HexNet_Move"));
    Wait(TEXT("Both displays retain the ID while the occupied cell moves"), [Id]()
    { return Matches(Get_ServerWorld(), *Id, {1,0}) && Matches(Get_ClientWorld(0), *Id, {1,0}); });
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
    {
        if (HasAnyErrors()) { return; }
        Capture(*this, World, TEXT("Authority"), true, false, TEXT("Automation/HexNet"));
        Capture(*this, Get_ClientWorld(0), TEXT("Client"), true, false, TEXT("Automation/HexNet"));
    })));
    Drive(TEXT("Ck_HexNet_Remove"));
    Wait(TEXT("Both displays and occupant indices clear after removal"), []()
    { return Matches(Get_ServerWorld(), {}, {}) && Matches(Get_ClientWorld(0), {}, {}); });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Settings, Mode, Last, Recents]()
    {
        Settings->StartupMode = Mode;
        Settings->LastGymName = Last;
        Settings->RecentGymNames = Recents;
        Settings->SaveConfig();
        return true;
    }));
    return true;
}

#endif
