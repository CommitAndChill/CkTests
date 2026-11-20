#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && WITH_ANGELSCRIPT_CK

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/HexGrid/CkHexPersistenceGym.h"
#include "CkTests/Snapshot/CkSnapshot_TestHarness_Common.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkGrid/HexGridSystem/Geometry/CkHexGrid_Geometry_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkSnapshot/Subsystem/CkSnapshot_Subsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"

namespace ck::hex_grid_rendered_gym
{
    constexpr auto PaintGymPath = TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_PaintGym.DA_HexGrid_PaintGym");

    struct FPreferences
    {
        UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
        ECkGym_StartupMode Mode = Settings->StartupMode;
        FString LastGym = Settings->LastGymName;
        TArray<FString> RecentGyms = Settings->RecentGymNames;

        FPreferences()
        { Settings->StartupMode = ECkGym_StartupMode::Cycler; }

        ~FPreferences()
        {
            Settings->StartupMode = Mode;
            if (Settings->LastGymName != LastGym || Settings->RecentGymNames != RecentGyms)
            {
                Settings->LastGymName = MoveTemp(LastGym);
                Settings->RecentGymNames = MoveTemp(RecentGyms);
                Settings->SaveConfig();
            }
        }
    };

    auto Registry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto Controller(UWorld* InWorld, FName InClass = TEXT("Ck_HexBoardGym_Controller")) -> APlayerController*
    {
        auto* PC = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
        return IsValid(PC) && PC->GetClass()->GetFName() == InClass ? PC : nullptr;
    }

    auto Status(UWorld* InWorld, FName InClass = TEXT("Ck_HexBoardGym_Controller"), FName InGetter = TEXT("Get_PickStatus")) -> FString
    {
        auto* PC = Controller(InWorld, InClass);
        auto* Fn = PC ? PC->FindFunction(InGetter) : nullptr;
        const auto* Prop = Fn ? CastField<FStrProperty>(Fn->GetReturnProperty()) : nullptr;
        if (Prop == nullptr)
        { return {}; }
        auto Params = FStructOnScope{Fn};
        PC->ProcessEvent(Fn, Params.GetStructMemory());
        return Prop->GetPropertyValue_InContainer(Params.GetStructMemory());
    }

    auto SavedBoardInsideViewport(UWorld* InWorld) -> bool
    {
        auto* PC = Controller(InWorld);
        auto* Getter = PC ? PC->FindFunction(TEXT("Get_Board")) : nullptr;
        const auto* Return = Getter ? CastField<FStructProperty>(Getter->GetReturnProperty()) : nullptr;
        if (!Return) { return false; }
        FStructOnScope Params{Getter};
        PC->ProcessEvent(Getter, Params.GetStructMemory());
        const auto Board = *Return->ContainerPtrToValuePtr<FCk_Handle_HexGrid>(Params.GetStructMemory());
        const auto Cells = UCk_Utils_HexGrid_UE::Get_Cells(Board);
        int32 Width = 0;
        int32 Height = 0;
        PC->GetViewportSize(Width, Height);
        if (Cells.IsEmpty() || Width <= 0 || Height <= 0) { return false; }
        auto Failure = ECk_HexBoardFailure::None;
        for (const auto& Cell : Cells)
        {
            FCk_HexCoord Coordinate;
            FVector Center;
            TArray<FVector> Points;
            if (!UCk_Utils_HexCell_UE::TryGet_Coordinate(Cell, Coordinate) ||
                !UCk_Utils_HexGrid_UE::TryGet_CellWorldCenter(Board, Coordinate, Center, Failure) ||
                !UCk_Utils_HexGrid_UE::TryGet_CellWorldCorners(Board, Coordinate, Points, Failure)) { return false; }
            Points.Add(Center);
            for (const auto& Point : Points)
            {
                FVector2D Screen;
                if (!PC->ProjectWorldLocationToScreen(Point, Screen, true) ||
                    Screen.X < Width * 0.15 || Screen.X > Width * 0.85 ||
                    Screen.Y < Height * 0.15 || Screen.Y > Height * 0.85) { return false; }
            }
        }
        return true;
    }

    auto Capture(FAutomationTestBase& InTest, UWorld* InWorld, const FString& InName, bool InRules = false, bool InBlocked = false,
        const TCHAR* InDirectory = nullptr) -> void
    {
        auto* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const auto Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!InTest.TestTrue(TEXT("Live PIE viewport available"), Viewport.IsValid() && FSlateApplication::IsInitialized()))
        { return; }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        if (!InTest.TestTrue(TEXT("Read rendered PIE viewport"), FSlateApplication::Get().TakeScreenshot(
                Viewport.ToSharedRef(), Pixels, Size) && Size.X > 0 && Size.Y > 0 &&
                Pixels.Num() == static_cast<int64>(Size.X) * Size.Y))
        { return; }

        const auto Directory = FPaths::Combine(FPaths::ProjectSavedDir(), InDirectory ? InDirectory :
            (InRules ? TEXT("Automation/HexRules") : TEXT("Automation/HexBoard")));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const auto Path = FPaths::Combine(Directory, InName + TEXT(".png"));
        for (auto& Pixel : Pixels)
        { Pixel.A = 255; }
        InTest.TestTrue(Path, FImageUtils::SaveImageByExtension(*Path,
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8)));
        InTest.TestTrue(InName + TEXT(" includes cyan board outlines"), Pixels.ContainsByPredicate([](const FColor& Color)
        { return Color.G > 100 && Color.B > 110 && Color.R + 35 < Color.G && Color.R + 35 < Color.B; }));
        if (InRules)
        {
            InTest.TestTrue(InName + TEXT(" includes yellow footprint"), Pixels.ContainsByPredicate([](FColor Color)
            { return Color.R > 150 && Color.G > 150 && Color.B + 50 < FMath::Min(Color.R, Color.G); }));
            if (InBlocked)
            {
                InTest.TestTrue(InName + TEXT(" includes red votes"), Pixels.ContainsByPredicate([](FColor Color)
                { return Color.R > 140 && Color.G + 75 < Color.R && Color.B + 75 < Color.R; }));
            }
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexGrid_SavedRecipe_RenderedGym, "Ck.HexGrid.Editor.SavedRecipeGym",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCk_HexGrid_SavedRecipe_RenderedGym::RunTest(const FString&)
{
    using namespace ck::hex_grid_rendered_gym;
    if (!TestNotNull(TEXT("Saved paint gym recipe exists on disk"), LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, PaintGymPath))) { return false; }
    auto Preferences = MakeShared<FPreferences>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        auto* Reg = Registry(ck::auto_test::net::Get_ServerWorld());
        return IsValid(Reg) && Reg->Find_GymIndexByName(TEXT("Hex Board")) != INDEX_NONE;
    }), 10.0f, TEXT("Saved board gym registry ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [](UWorld* World) { Registry(World)->Request_TravelToGym(Registry(World)->Find_GymIndexByName(TEXT("Hex Board"))); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        const auto Value = Status(ck::auto_test::net::Get_ServerWorld());
        return Value.StartsWith(TEXT("Cell (")) || Value == TEXT("Outside domain");
    }), 20.0f, TEXT("Initial procedural board and station camera ready before saved toggle")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World)
        {
            auto* PC = Controller(World);
            auto* Function = PC ? PC->FindFunction(TEXT("Ck_HexBoard_SavedRecipe")) : nullptr;
            if (TestNotNull(TEXT("Saved recipe toggle"), Function)) { PC->ProcessEvent(Function, nullptr); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        const auto Value = Status(ck::auto_test::net::Get_ServerWorld());
        return Value.StartsWith(TEXT("Cell (")) || Value == TEXT("Outside domain");
    }), 20.0f, TEXT("Saved board settles through production picking")));
    auto ConsecutiveFramedTicks = MakeShared<int32>(0);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([ConsecutiveFramedTicks]()
    {
        auto* World = ck::auto_test::net::Get_ServerWorld();
        if (!Status(World).StartsWith(TEXT("Cell (")) || !SavedBoardInsideViewport(World))
        { *ConsecutiveFramedTicks = 0; return false; }
        return ++*ConsecutiveFramedTicks >= 3;
    }), 20.0f, TEXT("Every saved board center and corner stays inside 15 percent viewport margins for three ticks")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World)
        {
            auto* PC = Controller(World);
            auto* Getter = PC ? PC->FindFunction(TEXT("Get_Board")) : nullptr;
            const auto* Return = Getter ? CastField<FStructProperty>(Getter->GetReturnProperty()) : nullptr;
            if (!TestNotNull(TEXT("Board getter"), Return)) { return; }
            FStructOnScope Params{Getter};
            PC->ProcessEvent(Getter, Params.GetStructMemory());
            const auto Board = *Return->ContainerPtrToValuePtr<FCk_Handle_HexGrid>(Params.GetStructMemory());
            auto* SourceGetter = PC->FindFunction(TEXT("Get_UsesSavedRecipe"));
            const auto* SourceReturn = SourceGetter ? CastField<FBoolProperty>(SourceGetter->GetReturnProperty()) : nullptr;
            if (!TestNotNull(TEXT("Saved source getter"), SourceReturn)) { return; }
            FStructOnScope SourceParams{SourceGetter};
            PC->ProcessEvent(SourceGetter, SourceParams.GetStructMemory());
            TestTrue(TEXT("Gym explicitly consumes saved recipe"), SourceReturn->GetPropertyValue_InContainer(SourceParams.GetStructMemory()));
            auto* Asset = LoadObject<UCk_HexGrid_AuthoringSpec>(nullptr, PaintGymPath);
            FCk_HexGrid_Spec Spec;
            auto Failure = ECk_HexBoardFailure::None;
            if (!TestTrue(TEXT("Saved recipe resolves"), Asset && Asset->TryResolve(Spec, Failure))) { return; }
            TArray<FCk_HexCoord> Domain;
            auto Spatial = ECk_HexGeometryFailure::None;
            UCk_Utils_HexGeometry_UE::TryResolve_Domain(Spec.Get_Domain(), Domain, Spatial);
            TestEqual(TEXT("Runtime and saved domain sizes match"), UCk_Utils_HexGrid_UE::Get_Cells(Board).Num(), Domain.Num());
            if (Domain.Num() >= 2)
            {
                FVector FirstWorld, SecondWorld;
                FVector2D FirstLocal, SecondLocal;
                const auto Resolved = UCk_Utils_HexGrid_UE::TryGet_CellWorldCenter(Board, Domain[0], FirstWorld, Failure) &&
                    UCk_Utils_HexGrid_UE::TryGet_CellWorldCenter(Board, Domain[1], SecondWorld, Failure) &&
                    UCk_Utils_HexGeometry_UE::TryGet_LocalCenter(Domain[0], Spec.Get_Layout(), FirstLocal, Spatial) &&
                    UCk_Utils_HexGeometry_UE::TryGet_LocalCenter(Domain[1], Spec.Get_Layout(), SecondLocal, Spatial);
                const auto Delta = SecondLocal - FirstLocal;
                TestTrue(TEXT("Saved rotated uniformly scaled local pivot reaches runtime"), Resolved &&
                    (SecondWorld - FirstWorld).Equals(Spec.Get_Pivot().TransformVector(FVector{Delta.X, Delta.Y, 0.0}), 0.001));
            }
            for (const auto& Coordinate : Domain)
            {
                FCk_Handle_HexCell Cell;
                if (!TestTrue(TEXT("Saved coordinate exists at runtime"), UCk_Utils_HexGrid_UE::TryGet_CellAt(Board, Coordinate, Cell, Failure)))
                { continue; }
                bool Disabled = false;
                UCk_Utils_HexCell_UE::TryGet_IsDisabled(Cell, Disabled);
                const bool HasBlockerVote = Spec.Get_Blockers().ContainsByPredicate([&](const auto& Blocker)
                { return Blocker.Get_InitiallyActive() && Blocker.Get_Cells().Contains(Coordinate); });
                TestEqual(TEXT("Saved disabled state matches runtime"), Disabled,
                    HasBlockerVote || Spec.Get_InitialDisabledCells().Contains(Coordinate));
                auto ExpectedTags = Spec.Get_DefaultCellTags();
                for (const auto& Override : Spec.Get_PerCellTags())
                { if (Override.Get_Coordinate() == Coordinate) { ExpectedTags.AppendTags(Override.Get_Tags()); } }
                FGameplayTagContainer ActualTags;
                UCk_Utils_HexCell_UE::TryGet_Tags(Cell, ActualTags);
                TestTrue(TEXT("Saved tags match runtime"), ActualTags == ExpectedTags);
                FVector Center;
                UCk_Utils_HexGrid_UE::TryGet_CellWorldCenter(Board, Coordinate, Center, Failure);
                FVector Hit;
                FCk_Handle_HexCell Picked;
                TestTrue(TEXT("Saved cell center picks same real cell"), UCk_Utils_HexGrid_UE::TryGet_CellFromWorldRay(
                    Board, Center + Spec.Get_Pivot().GetRotation().GetAxisZ() * 100.0,
                    -Spec.Get_Pivot().GetRotation().GetAxisZ(), Picked, Hit, Spatial, Failure) && Picked == Cell);
            }
            if (TestTrue(TEXT("Saved board bounds remain inside safe viewport margins at readback"), SavedBoardInsideViewport(World)))
            { Capture(*this, World, TEXT("SavedRecipe"), false, false, TEXT("Automation/HexEditor")); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Preferences]() { return ck::auto_test::net::Get_AllPIEWorlds().IsEmpty(); }), 15.f, TEXT("Saved recipe gym PIE retired")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexGrid_Board_RenderedGym, "Ck.HexGrid.Board.RenderedGym",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool
    FCk_HexGrid_Board_RenderedGym::
    RunTest(const FString&)
{
    using namespace ck::hex_grid_rendered_gym;
    auto Preferences = MakeShared<FPreferences>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]()
        {
            auto* Reg = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(Reg) && Reg->Find_GymIndexByName(TEXT("Hex Board")) != INDEX_NONE;
        }), 10.0f, TEXT("Hex Board appears in the real gym registry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World)
        {
            auto* Reg = Registry(World);
            const auto Index = IsValid(Reg) ? Reg->Find_GymIndexByName(TEXT("Hex Board")) : INDEX_NONE;
            if (TestTrue(TEXT("Travel to registered board gym"), Index != INDEX_NONE))
            { Reg->Request_TravelToGym(Index); }
        })));

    auto Act = [this](FName InCommand)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, InCommand](UWorld* World)
            {
                if (HasAnyErrors()) { return; }
                auto* PC = Controller(World);
                auto* Fn = PC ? PC->FindFunction(InCommand) : nullptr;
                if (TestNotNull(InCommand.ToString(), Fn)) { PC->ProcessEvent(Fn, nullptr); }
            })));
    };
    auto Observe = [this](FString Expected, FString Snapshot)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
            [this, Expected]() { return HasAnyErrors() || Status(ck::auto_test::net::Get_ServerWorld()) == Expected; }),
            20.0f, FString::Printf(TEXT("Rendered gym pick must become %s"), *Expected)));
        if (Snapshot.IsEmpty()) { return; }
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Snapshot](UWorld* World) { if (!HasAnyErrors()) { Capture(*this, World, Snapshot); } })));
    };

    // Literal winners use axial lexicographic ties. Removing (0,0) leaves one edge pick missing in each chart.
    const TCHAR* Pointy[] = {TEXT("Cell (1,0)"), TEXT("Cell (0,1)"), TEXT("Outside domain"),
        TEXT("Cell (1,-1)"), TEXT("Cell (1,0)"), TEXT("Cell (1,0)")};
    const TCHAR* Flat[] = {TEXT("Cell (1,0)"), TEXT("Cell (1,0)"), TEXT("Cell (0,1)"),
        TEXT("Outside domain"), TEXT("Cell (1,-1)"), TEXT("Cell (1,0)")};
    for (int32 Orientation = 0; Orientation < 2; ++Orientation)
    {
        const auto Label = FString{Orientation == 0 ? TEXT("Pointy") : TEXT("Flat")};
        if (Orientation == 1)
        {
            Act(TEXT("Ck_HexBoard_NextProbe"));
            Act(TEXT("Ck_HexBoard_Rebuild"));
            Act(TEXT("Ck_HexBoard_Move"));
        }
        for (int32 Probe = 0; Probe < 8; ++Probe)
        {
            if (Probe > 0) { Act(TEXT("Ck_HexBoard_NextProbe")); }
            auto Snapshot = FString{};
            if (Probe == 0) { Snapshot = Label + TEXT("-Edge"); }
            if (Probe == 6) { Snapshot = Label + TEXT("-Hole"); }
            if (Probe == 7) { Snapshot = Label + TEXT("-Exterior"); }
            Observe(Probe >= 6 ? TEXT("Outside domain") : (Orientation == 0 ? Pointy[Probe] : Flat[Probe]), Snapshot);
        }
    }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Preferences]() { return ck::auto_test::net::Get_AllPIEWorlds().IsEmpty(); }),
        15.0f, TEXT("Rendered board gym PIE world is released")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexRules_RenderedGym, "Ck.HexGrid.Rules.RenderedGym",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool
    FCk_HexRules_RenderedGym::
    RunTest(const FString&)
{
    using namespace ck::hex_grid_rendered_gym;
    auto Preferences = MakeShared<FPreferences>();
    const auto ClassName = FName{TEXT("Ck_HexRulesGym_Controller")};
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        auto* Reg = Registry(ck::auto_test::net::Get_ServerWorld());
        return IsValid(Reg) && Reg->Find_GymIndexByName(TEXT("Hex Footprints and Blockers")) != INDEX_NONE;
    }), 10.0f, TEXT("Rules gym is registered")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World)
        {
            auto* Reg = Registry(World);
            const auto Index = IsValid(Reg) ? Reg->Find_GymIndexByName(TEXT("Hex Footprints and Blockers")) : INDEX_NONE;
            if (TestTrue(TEXT("Travel to rules gym"), Index != INDEX_NONE))
            { Reg->Request_TravelToGym(Index); }
        })));

    auto Act = [this, ClassName](FName InCommand)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, ClassName, InCommand](UWorld* World)
            {
                if (HasAnyErrors()) { return; }
                auto* PC = Controller(World, ClassName);
                auto* Fn = PC ? PC->FindFunction(InCommand) : nullptr;
                if (TestNotNull(InCommand.ToString(), Fn)) { PC->ProcessEvent(Fn, nullptr); }
            })));
    };
    auto Observe = [this, ClassName](FString Tip, FString Votes, FString Snapshot = {}, bool Blocked = false)
    {
        auto Expected = TEXT("Tip ") + Tip + TEXT(" | ") + Votes;
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
            [this, ClassName, Expected]()
            {
                return HasAnyErrors() ||
                    Status(ck::auto_test::net::Get_ServerWorld(), ClassName, TEXT("Get_RuleStatus")) == Expected;
            }), 20.0f, Expected));
        if (Snapshot.IsEmpty()) { return; }
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Snapshot, Blocked](UWorld* World)
            { if (!HasAnyErrors()) { Capture(*this, World, Snapshot, true, Blocked); } })));
    };

    const auto Open = FString{TEXT("A off, B off | center open | blocked 0")};
    const auto Both = FString{TEXT("A on, B on | center blocked | blocked 3")};
    Observe(TEXT("(1,0)"), Open, TEXT("Open"));
    Act(TEXT("Ck_HexRules_ToggleA"));
    Observe(TEXT("(1,0)"), TEXT("A on, B off | center blocked | blocked 2"));
    Act(TEXT("Ck_HexRules_ToggleB"));
    Observe(TEXT("(1,0)"), Both);
    Act(TEXT("Ck_HexRules_Rotate"));
    Observe(TEXT("(0,1)"), Both, TEXT("RotatedOverlap"), true);
    Act(TEXT("Ck_HexRules_ToggleA"));
    Observe(TEXT("(0,1)"), TEXT("A off, B on | center blocked | blocked 2"), TEXT("OnlyB"), true);
    Act(TEXT("Ck_HexRules_ToggleB"));
    Observe(TEXT("(0,1)"), Open, TEXT("Released"));
    for (auto Tip : {TEXT("(-1,1)"), TEXT("(-1,0)"), TEXT("(0,-1)"), TEXT("(1,-1)"), TEXT("(1,0)")})
    {
        Act(TEXT("Ck_HexRules_Rotate"));
        Observe(Tip, Open);
    }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Preferences]() { return ck::auto_test::net::Get_AllPIEWorlds().IsEmpty(); }),
        15.0f, TEXT("Rules gym world released")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexPlacement_RenderedGym, "Ck.HexGrid.Placement.RenderedGym",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCk_HexPlacement_RenderedGym::RunTest(const FString&)
{
    using namespace ck::hex_grid_rendered_gym;
    auto Preferences = MakeShared<FPreferences>();
    const FName ClassName{TEXT("Ck_HexPlacementGym_Controller")};
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        auto* Reg = Registry(ck::auto_test::net::Get_ServerWorld());
        return IsValid(Reg) && Reg->Find_GymIndexByName(TEXT("Hex Placement")) != INDEX_NONE;
    }), 10.f, TEXT("Placement gym registered")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this](UWorld* World)
        {
            if (HasAnyErrors()) { return; }
            auto* Reg = Registry(World);
            Reg->Request_TravelToGym(Reg->Find_GymIndexByName(TEXT("Hex Placement")));
        })));
    auto Observe = [this, ClassName](FString Command, FString Expected, FString Snapshot, bool Placed)
    {
        if (!Command.IsEmpty())
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
                [this, ClassName, Command](UWorld* World)
                {
                    if (HasAnyErrors()) { return; }
                    auto* PC = Controller(World, ClassName);
                    auto* Fn = PC ? PC->FindFunction(FName(*Command)) : nullptr;
                    if (TestNotNull(Command, Fn)) { PC->ProcessEvent(Fn, nullptr); }
                })));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
            [this, ClassName, Expected]()
            {
                return HasAnyErrors() || Status(ck::auto_test::net::Get_ServerWorld(), ClassName, TEXT("Get_PlacementStatus")) == Expected;
            }), 12.f, Expected));
        if (Snapshot.IsEmpty()) { return; }
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this, Snapshot, Placed](UWorld* World)
            {
                if (!HasAnyErrors()) { Capture(*this, World, Snapshot, Placed, false, TEXT("Automation/HexPlacement")); }
            })));
    };
    Observe({}, TEXT("Open"), {}, false);
    Observe(TEXT("Ck_HexPlacement_Place"), TEXT("Placed at (0,0)"), TEXT("Placed"), true);
    Observe(TEXT("Ck_HexPlacement_Move"), TEXT("Placed at (1,0)"), TEXT("Moved"), true);
    Observe(TEXT("Ck_HexPlacement_Destroy"), TEXT("Open"), TEXT("Released"), false);
    Observe(TEXT("Ck_HexPlacement_Place"), TEXT("Placed at (0,0)"), {}, true);
    Observe(TEXT("Ck_HexPlacement_Remove"), TEXT("Open"), {}, false);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Preferences]() { return ck::auto_test::net::Get_AllPIEWorlds().IsEmpty(); }), 15.f, TEXT("Gym PIE retired")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexPlacement_Script, "Ck.HexGrid.Placement.AS_Consumer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCk_HexPlacement_Script::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunAsTestOnAllWorlds(this, TEXT("/Script/Angelscript.Ck_AutoTest_HexGrid_Placement"), 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexPersistence_Script, "Ck.HexGrid.Persistence.AS_Consumer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCk_HexPersistence_Script::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunAsTestOnAllWorlds(this, TEXT("/Script/Angelscript.Ck_AutoTest_HexGrid_Persistence"), 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexPersistence_RenderedGym, "Ck.HexGrid.Persistence.RenderedGym",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCk_HexPersistence_RenderedGym::RunTest(const FString&)
{
    using namespace ck::hex_grid_rendered_gym;
    using ck::auto_test::snapshot::Get_PostTravelServerWorld;
    auto Preferences = MakeShared<FPreferences>();
    auto OriginalId = MakeShared<FGuid>();
    const FName ClassName = TEXT("Ck_HexPersistenceGym_Controller");
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([this]()
    {
        auto* Reg = Registry(Get_PostTravelServerWorld());
        const auto Index = Reg ? Reg->Find_GymIndexByName(TEXT("Hex Persistence")) : INDEX_NONE;
        if (!TestTrue(TEXT("Persistence gym registered"), Index != INDEX_NONE)) { return false; }
        Reg->Request_TravelToGym(Index);
        return true;
    }), TEXT("Travel through gym registry")));

    const auto Await = [this, ClassName](const FString& Expected)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
            [ClassName, Expected]() { return Status(Get_PostTravelServerWorld(), ClassName, TEXT("Get_PersistenceStatus")) == Expected; }),
            30.f, Expected));
    };
    const auto Act = [this, ClassName](FName Command)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([this, ClassName, Command]()
        {
            if (HasAnyErrors()) { return false; }
            auto* PC = Controller(Get_PostTravelServerWorld(), ClassName);
            auto* Function = PC ? PC->FindFunction(Command) : nullptr;
            if (!TestNotNull(Command.ToString(), Function)) { return false; }
            PC->ProcessEvent(Function, nullptr);
            return true;
        }), Command.ToString()));
    };
    Await(TEXT("Ready | cells 19 | placements 0"));
    Act(TEXT("Ck_HexPersistence_Place"));
    Await(TEXT("Ready | cells 19 | placements 1"));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([this, OriginalId]()
    {
        auto* World = Get_PostTravelServerWorld();
        auto* Session = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCk_HexPersistenceGymSession>() : nullptr;
        if (!Session) { return false; }
        const auto Placements = UCk_Utils_HexGridOccupancy_UE::Get_Placements(Session->Get_Board());
        if (!Placements.Get_Succeeded() || Placements.Get_Placements().Num() != 1) { return false; }
        *OriginalId = Placements.Get_Placements()[0].Get_PlacementId();
        Capture(*this, World, TEXT("BeforeLoad"), true, false, TEXT("Automation/HexPersistence"));
        return OriginalId->IsValid();
    }), TEXT("Capture placed board before save")));
    Act(TEXT("Ck_HexPersistence_Save"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([]()
    {
        auto* World = Get_PostTravelServerWorld();
        auto* Session = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCk_HexPersistenceGymSession>() : nullptr;
        return Session && Session->Get_HasSave() && !Session->Get_IsBusy();
    }), 15.f, TEXT("Actual save completes")));
    Act(TEXT("Ck_HexPersistence_Load"));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([ClassName]()
    {
        auto* World = Get_PostTravelServerWorld();
        auto* Session = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCk_HexPersistenceGymSession>() : nullptr;
        return Session && !Session->Get_IsBusy() && Session->Get_PhysicallyReacquired()
            && Status(World, ClassName, TEXT("Get_PersistenceStatus")) == TEXT("Ready | cells 19 | placements 1");
    }), 60.f, TEXT("New physical board and persistence gym controller survive load")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([this, OriginalId]()
    {
        auto* World = Get_PostTravelServerWorld();
        auto* Session = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCk_HexPersistenceGymSession>() : nullptr;
        auto* Snapshot = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCk_Snapshot_Subsystem_UE>() : nullptr;
        if (!Session || !Snapshot) { return false; }
        const auto Placements = UCk_Utils_HexGridOccupancy_UE::Get_Placements(Session->Get_Board());
        const auto Valid = Placements.Get_Succeeded() && Placements.Get_Placements().Num() == 1 &&
            Placements.Get_Placements()[0].Get_PlacementId() == *OriginalId;
        TestTrue(TEXT("Logical placement ID survives physical reload"), Valid);
        TestTrue(TEXT("Load report closes without loss"), Snapshot->Get_LastLoadReport().Get_Result() == ECk_SnapshotResult::Success
            && Snapshot->Get_LastLoadReport().Get_IsAccountingClosed());
        Capture(*this, World, TEXT("AfterLoad"), true, false, TEXT("Automation/HexPersistence"));
        Snapshot->Request_DeleteSaveSlot(TEXT("CkTests_HexPersistenceGym"));
        return Valid;
    }), TEXT("Rendered restored board retains its saved placement")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Preferences]() { return ck::auto_test::net::Get_AllPIEWorlds().IsEmpty(); }), 15.f, TEXT("Persistence gym PIE retired")));
    return true;
}

#endif
