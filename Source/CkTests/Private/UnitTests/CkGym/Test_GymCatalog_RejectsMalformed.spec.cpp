#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"

#include <Engine/GameInstance.h>
#include <GameFramework/GameModeBase.h>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkGymCatalog_RejectsMalformed,
    "Ck.Gym.Catalog.RejectsMalformed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkGymCatalog_RejectsMalformed::RunTest(const FString& Parameters)
{
    auto Valid = FCkGym_Entry{};
    Valid.DisplayName = TEXT("Valid Gym");
    Valid.GameModeClass = AGameModeBase::StaticClass();
    Valid.LevelName = TEXT("TestGyms_CkTests_Level");

    auto Entries = TArray<FCkGym_Entry>{Valid};
    TestTrue(TEXT("complete valid catalog accepts no landed gym"),
        UCkGym_Registry_Subsystem::IsValidCatalog(Entries, INDEX_NONE));
    TestTrue(TEXT("complete valid catalog accepts its sole landed index"),
        UCkGym_Registry_Subsystem::IsValidCatalog(Entries, 0));
    TestFalse(TEXT("out-of-range landed index rejected"),
        UCkGym_Registry_Subsystem::IsValidCatalog(Entries, 1));

    Entries.Add(Valid);
    TestFalse(TEXT("one duplicate invalidates the entire catalog"),
        UCkGym_Registry_Subsystem::IsValidCatalog(Entries, INDEX_NONE));
    Entries.Pop();

    Entries[0].GameModeClass = nullptr;
    TestFalse(TEXT("missing GameMode class invalidates the entire catalog"),
        UCkGym_Registry_Subsystem::IsValidCatalog(Entries, INDEX_NONE));
    Entries[0] = Valid;
    Entries[0].LevelName = TEXT("Map?game=Injected");
    TestFalse(TEXT("travel URL syntax is rejected as a level name"),
        UCkGym_Registry_Subsystem::IsValidCatalog(Entries, INDEX_NONE));

    Entries[0] = Valid;
    auto Alias = Valid;
    Alias.DisplayName = TEXT("Same Destination Alias");
    Entries.Add(Alias);
    TestEqual(TEXT("requested alias wins over first class-and-map match"),
        UCkGym_Registry_Subsystem::ChooseLandedIndex(Entries, AGameModeBase::StaticClass(),
            TEXT("TestGyms_CkTests_Level"), Alias.DisplayName, FString{}), 1);
    TestEqual(TEXT("direct open uses first class-and-map match"),
        UCkGym_Registry_Subsystem::ChooseLandedIndex(Entries, AGameModeBase::StaticClass(),
            TEXT("TestGyms_CkTests_Level"), FString{}, FString{}), 0);

    auto* GameInstance = NewObject<UGameInstance>();
    if (!TestNotNull(TEXT("fixture GameInstance allocated"), GameInstance))
    { return false; }
    // The subsystem's ClassWithin contract requires a GameInstance outer.
    auto* Registry = NewObject<UCkGym_Registry_Subsystem>(GameInstance);
    TestNotNull(TEXT("test registry allocated"), Registry);
    if (Registry != nullptr)
    {
        TestFalse(TEXT("missing world rejects replica replacement"),
            Registry->TryReplaceReplica(nullptr, TArray<FCkGym_Entry>{Valid}, INDEX_NONE, false));
        TestTrue(TEXT("failed replacement leaves the registry empty"), Registry->Get_Entries().IsEmpty());
        TestFalse(TEXT("missing world rejects travel"), Registry->TryRequest_TravelToGym(nullptr, 0));
        TestEqual(TEXT("failed travel leaves current index unchanged"), Registry->Get_CurrentGymIndex(), INDEX_NONE);
    }
    return true;
}

#endif
