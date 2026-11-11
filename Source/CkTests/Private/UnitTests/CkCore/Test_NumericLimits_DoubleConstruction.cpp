#include "CkCore/Math/NumericLimits/CkNumericLimits.h"

#include "Misc/AutomationTest.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#if WITH_DEV_AUTOMATION_TESTS

// --------------------------------------------------------------------------------------------------------------------

namespace
{
    static_assert(std::is_same_v<
        decltype(std::declval<const FCk_DoubleNumericLimits&>().Get_Min()), const double&>);
    static_assert(std::is_same_v<
        decltype(std::declval<const FCk_DoubleNumericLimits&>().Get_Max()), const double&>);

    auto
        CheckEndpoints(
            FAutomationTestBase& InTest,
            const FString& InConstruction,
            const FCk_DoubleNumericLimits& InLimits)
        -> void
    {
        InTest.TestTrue(InConstruction + TEXT(" minimum is finite"), std::isfinite(InLimits.Get_Min()));
        InTest.TestTrue(InConstruction + TEXT(" maximum is finite"), std::isfinite(InLimits.Get_Max()));
        InTest.TestTrue(InConstruction + TEXT(" minimum is exactly the lowest double"),
            InLimits.Get_Min() == std::numeric_limits<double>::lowest());
        InTest.TestTrue(InConstruction + TEXT(" maximum is exactly the largest double"),
            InLimits.Get_Max() == std::numeric_limits<double>::max());
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_NumericLimits_DoubleConstruction,
    "CkTests.UnitTests.CkCore.NumericLimits.DoubleConstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

auto
    FCkTest_NumericLimits_DoubleConstruction::
    RunTest(
        const FString& InParameters)
    -> bool
{
    const auto NativeLimits = FCk_DoubleNumericLimits{};
    CheckEndpoints(*this, TEXT("Native construction"), NativeLimits);

    auto* ScriptStruct = FCk_DoubleNumericLimits::StaticStruct();
    if (NOT TestNotNull(TEXT("Double limits have a reflected struct"), ScriptStruct))
    {
        return false;
    }
    const auto* MinProperty = FindFProperty<FDoubleProperty>(ScriptStruct, TEXT("_Min"));
    const auto* MaxProperty = FindFProperty<FDoubleProperty>(ScriptStruct, TEXT("_Max"));
    if (NOT TestNotNull(TEXT("Reflected minimum retains double precision"), MinProperty))
    {
        return false;
    }
    if (NOT TestNotNull(TEXT("Reflected maximum retains double precision"), MaxProperty))
    {
        return false;
    }

    // Reflection's InitializeStruct path must preserve the same endpoints as native construction.
    const auto ReflectedStorage = FStructOnScope{ScriptStruct};
    if (NOT TestTrue(TEXT("Reflection constructs owned struct memory"), ReflectedStorage.IsValid()))
    {
        return false;
    }
    const auto* ReflectedLimits = reinterpret_cast<const FCk_DoubleNumericLimits*>(ReflectedStorage.GetStructMemory());
    CheckEndpoints(*this, TEXT("Reflected construction"), *ReflectedLimits);
    TestTrue(TEXT("Reflected minimum property reads the exact double endpoint"),
        MinProperty->GetPropertyValue_InContainer(ReflectedStorage.GetStructMemory()) == std::numeric_limits<double>::lowest());
    TestTrue(TEXT("Reflected maximum property reads the exact double endpoint"),
        MaxProperty->GetPropertyValue_InContainer(ReflectedStorage.GetStructMemory()) == std::numeric_limits<double>::max());

    return NOT HasAnyErrors();
}

#endif
