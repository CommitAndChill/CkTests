#pragma once

class FAutomationTestBase;

namespace ck::tests::hex_persistence
{
    auto EnsureNetConsumer(FAutomationTestBase& InTest) -> bool;
    auto EnsureAssets(FAutomationTestBase& InTest) -> bool;
    auto EnsureGymMap(FAutomationTestBase& InTest) -> bool;
}
