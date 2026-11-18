#include "CkAutoTestMapConfig.h"
#include "CkAutoTestMapPopulator.h"

#include "CkCore/Macros/CkMacros.h"

#include <Engine/World.h>
#include <Misc/AutomationTest.h>
#include <UObject/Package.h>
#include <WorldPartition/WorldPartitionSubsystem.h>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_autotest_populator_unload_tests
{
    inline constexpr auto kFlags =
        EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ProductFilter;

    const auto PackageName = FString{TEXT("/Temp/CkAutoTestPopulator_UnloadTargetPackage")};
}

// --------------------------------------------------------------------------------------------------------------------

// A map package the populator loads leaves an initialized Inactive world behind (the editor
// initializes a loaded map's world). If nothing unloads it, a collection that ignores
// RF_Standalone reclaims it mid-purge, after its subsystems are unreachable: the tickable
// WorldPartitionSubsystem is destroyed still initialized, and its ensure fails the test.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_AutoTestPopulator_UnloadTargetPackage_ReleasesTheInactiveWorld,
    "Ck.AutoTest.Populator.UnloadTargetPackage.ReleasesTheInactiveWorld",
    ck_autotest_populator_unload_tests::kFlags)

bool FCkTest_AutoTestPopulator_UnloadTargetPackage_ReleasesTheInactiveWorld::RunTest(const FString& Parameters)
{
    using namespace ck_autotest_populator_unload_tests;

    auto* Package = CreatePackage(*PackageName);
    auto* World = UWorld::CreateWorld(EWorldType::Inactive, /*bInformEngineOfWorld=*/false, NAME_None, Package,
        /*bAddToRoot=*/false);
    if (NOT TestNotNull(TEXT("an Inactive world is created in its own package"), World))
    { return false; }

    // What a loaded map's world carries: kept by RF_Standalone, not by the root set.
    World->SetFlags(RF_Public | RF_Standalone);

    TestTrue(TEXT("the world is initialized, as the editor initializes a loaded map's world"),
        World->IsInitialized());
    TestNotNull(TEXT("the world has the tickable WorldPartitionSubsystem an Inactive world gets"),
        World->GetSubsystem<UWorldPartitionSubsystem>());

    const auto* Config = NewObject<UCkAutoTestMapConfig>(GetTransientPackage(), NAME_None, RF_Transient);
    UCkAutoTestMapPopulator::DoUnload_TargetPackage(Config, Package);

    // RF_NoFlags is the collection several editor tests run between cases; it is the one that
    // reclaimed a left-over world.
    CollectGarbage(RF_NoFlags);

    TestNull(TEXT("the unloaded target package is no longer resident"), FindPackage(nullptr, *PackageName));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------
