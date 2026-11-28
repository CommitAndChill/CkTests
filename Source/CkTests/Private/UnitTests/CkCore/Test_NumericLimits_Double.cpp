#include "CkCore/Math/NumericLimits/CkNumericLimits.h"

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

// --------------------------------------------------------------------------------------------------------------------

static_assert(std::is_same_v<
    decltype(std::declval<const FCk_DoubleNumericLimits&>().Get_Min()), const double&>,
    "Double numeric limits expose the minimum without narrowing");

static_assert(std::is_same_v<
    decltype(std::declval<const FCk_DoubleNumericLimits&>().Get_Max()), const double&>,
    "Double numeric limits expose the maximum without narrowing");

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_NumericLimits_DoublePrecision,
    "CkTests.UnitTests.CkCore.NumericLimits.DoublePrecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCkTest_NumericLimits_DoublePrecision::RunTest(const FString& Parameters)
{
    const auto Limits = FCk_DoubleNumericLimits{};

    TestTrue(TEXT("Default minimum is finite"), std::isfinite(Limits.Get_Min()));
    TestTrue(TEXT("Default maximum is finite"), std::isfinite(Limits.Get_Max()));
    TestTrue(TEXT("Default minimum equals the exact lowest double"),
        Limits.Get_Min() == std::numeric_limits<double>::lowest());
    TestTrue(TEXT("Default maximum equals the exact greatest double"),
        Limits.Get_Max() == std::numeric_limits<double>::max());

    TestNotNull(TEXT("Reflected minimum uses double storage"),
        FindFProperty<FDoubleProperty>(FCk_DoubleNumericLimits::StaticStruct(), TEXT("_Min")));
    TestNotNull(TEXT("Reflected maximum uses double storage"),
        FindFProperty<FDoubleProperty>(FCk_DoubleNumericLimits::StaticStruct(), TEXT("_Max")));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------
