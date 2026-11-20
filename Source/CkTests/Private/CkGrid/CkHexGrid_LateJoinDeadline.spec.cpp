#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexGrid_LateJoinDeadline, "Ck.HexGrid.Net.LateJoinDeadline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexGrid_LateJoinDeadline::RunTest(const FString&)
{
    if (!TestTrue(TEXT("The timeout fixture starts outside a PIE session"),
        GEditor && !GEditor->IsPlaySessionInProgress())) { return false; }
    const auto Join = MakeShared<FCk_NetAutoTest_LateJoinResult>();
    const auto Start = FPlatformTime::Seconds();
    constexpr double Timeout = 0.05;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RequestLateJoin(Join, Timeout));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([Join, Start]()
    {
        return Join->Status == ECk_NetAutoTest_LateJoinStatus::TimedOut &&
            !Join->ClientWorld.IsValid() && !Join->ServerConnection.IsValid() &&
            FPlatformTime::Seconds() - Start >= Timeout;
    }), TEXT("Without an eligible connected listen session the deadline elapses and no joined client is reported")));
    return true;
}

#endif
