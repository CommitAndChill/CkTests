#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && WITH_ANGELSCRIPT_CK

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <Misc/AutomationTest.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck::hex_grid_geometry_test
{
    constexpr auto GymName = TEXT("Hex Grid Geometry");
    constexpr auto ControllerClassName = TEXT("Ck_HexGridGeometryGym_PlayerController");

    struct FScopedGymPreferences
    {
        FScopedGymPreferences()
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            StartupMode = Settings->StartupMode;
            LastGymName = Settings->LastGymName;
            RecentGymNames = Settings->RecentGymNames;
            Settings->StartupMode = ECkGym_StartupMode::Cycler;
        }

        ~FScopedGymPreferences()
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = StartupMode;
            if (Settings->LastGymName != LastGymName || Settings->RecentGymNames != RecentGymNames)
            {
                Settings->LastGymName = MoveTemp(LastGymName);
                Settings->RecentGymNames = MoveTemp(RecentGymNames);
                Settings->SaveConfig();
            }
        }

        ECkGym_StartupMode StartupMode{};
        FString LastGymName;
        TArray<FString> RecentGymNames;
    };

    auto
        GetGymRegistry(
            UWorld* InWorld)
        -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto
        GetGymController(
            UWorld* InWorld)
        -> APlayerController*
    {
        auto* Controller = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
        return IsValid(Controller) && Controller->GetClass()->GetName() == ControllerClassName ? Controller : nullptr;
    }

    auto
        GetIsGymReady(
            UWorld* InWorld)
        -> bool
    {
        auto* Controller = GetGymController(InWorld);
        auto* Readiness = IsValid(Controller) ? Controller->FindFunction(TEXT("Get_IsGeometryReady")) : nullptr;
        const auto* Result = IsValid(Readiness) ? CastField<FBoolProperty>(Readiness->GetReturnProperty()) : nullptr;
        if (Result == nullptr)
        { return false; }

        auto Params = FStructOnScope{Readiness};
        Controller->ProcessEvent(Readiness, Params.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Params.GetStructMemory());
    }

    auto
        ReadGymString(
            FAutomationTestBase& InTest,
            APlayerController& InController,
            FName InFunctionName)
        -> FString
    {
        auto* Function = InController.FindFunction(InFunctionName);
        const auto* Result = IsValid(Function) ? CastField<FStrProperty>(Function->GetReturnProperty()) : nullptr;
        if (Result == nullptr)
        {
            InTest.AddError(FString::Printf(TEXT("Missing AngelScript function %s returning FString"), *InFunctionName.ToString()));
            return {};
        }

        auto Params = FStructOnScope{Function};
        InController.ProcessEvent(Function, Params.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Params.GetStructMemory());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexGrid_GeometryScript,
    "Ck.HexGrid.Geometry.AngelScriptAndGym",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool
    FCk_HexGrid_GeometryScript::
    RunTest(const FString&)
{
    using namespace ck::hex_grid_geometry_test;

    TSharedPtr<FScopedGymPreferences> Preferences = MakeShared<FScopedGymPreferences>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1,
        TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            const auto* Registry = GetGymRegistry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(Registry) && Registry->Find_GymIndexByName(GymName) != INDEX_NONE;
        }), 10.0f, TEXT("Hex Grid Geometry registers on the real gym launcher")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            auto* Registry = GetGymRegistry(InWorld);
            if (NOT TestNotNull(TEXT("Gym registry"), Registry))
            { return; }
            const auto Index = Registry->Find_GymIndexByName(GymName);
            if (NOT TestTrue(TEXT("Geometry registration is in the gym registry"), Registry->Get_Entries().IsValidIndex(Index)))
            { return; }
            TestEqual(TEXT("Geometry gym category"), Registry->Get_Entries()[Index].Category, FString{TEXT("CkGrid")});
            Registry->Request_TravelToGym(Index);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return GetIsGymReady(ck::auto_test::net::Get_ServerWorld());
        }), 20.0f, TEXT("Registry travel starts the geometry station and builds both seven-cell charts")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Preferences = MoveTemp(Preferences)](UWorld* InWorld) mutable -> void
        {
            Preferences.Reset();
            auto* Controller = GetGymController(InWorld);
            if (NOT TestNotNull(TEXT("Live AngelScript geometry gym controller"), Controller))
            { return; }

            TestEqual(TEXT("AngelScript geometry binding probe completed successfully"),
                ReadGymString(*this, *Controller, TEXT("Run_GeometryBindingProbe")),
                FString{TEXT("Geometry binding checks passed")});

            auto* NextTurn = Controller->FindFunction(TEXT("Ck_GymHexGrid_NextTurn"));
            auto* ResetTurn = Controller->FindFunction(TEXT("Ck_GymHexGrid_ResetTurn"));
            if (NOT TestNotNull(TEXT("Next turn command"), NextTurn)
                || NOT TestNotNull(TEXT("Reset turn command"), ResetTurn))
            { return; }

            const TCHAR* TurnLabels[] = {
                TEXT("0 degrees (1,0)"),
                TEXT("60 degrees (0,1)"),
                TEXT("120 degrees (-1,1)"),
                TEXT("180 degrees (-1,0)"),
                TEXT("240 degrees (0,-1)"),
                TEXT("300 degrees (1,-1)"),
            };
            for (const auto* TurnLabel : TurnLabels)
            {
                TestEqual(TEXT("Live axial turn selection"),
                    ReadGymString(*this, *Controller, TEXT("Get_TurnLabel")), FString{TurnLabel});
                Controller->ProcessEvent(NextTurn, nullptr);
            }
            TestEqual(TEXT("Six turns wrap to the initial selection"),
                ReadGymString(*this, *Controller, TEXT("Get_TurnLabel")), FString{TurnLabels[0]});

            Controller->ProcessEvent(NextTurn, nullptr);
            Controller->ProcessEvent(ResetTurn, nullptr);
            TestEqual(TEXT("Reset restores the initial selection"),
                ReadGymString(*this, *Controller, TEXT("Get_TurnLabel")), FString{TurnLabels[0]});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

    return true;
}

#endif
