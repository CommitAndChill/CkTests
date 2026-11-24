#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityScript/CkEntityScript_Utils.h"

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_PendingHandleForConstructed_InvalidConstructed,
    "Ck.EntityScript.PendingHandleForConstructed.InvalidConstructedEnsuresAndReturnsInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_PendingHandleForConstructed_InvalidConstructed::RunTest(const FString& Parameters)
{
    // Occurrences=0 suppresses every matching line while still requiring at least one match.
    AddExpectedError(TEXT("Get_PendingHandle_ForConstructed needs a valid constructed entity"),
        EAutomationExpectedErrorFlags::Contains, 0);

    const auto Pending = UCk_Utils_PendingEntityScript_UE::Get_PendingHandle_ForConstructed(FCk_Handle_EntityScript{});

    TestFalse(TEXT("an invalid constructed entity resolves to no pending handle"),
        UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending));

    return true;
}

#endif
