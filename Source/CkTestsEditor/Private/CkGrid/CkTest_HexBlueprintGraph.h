#pragma once

#include "CoreMinimal.h"

class FAutomationTestBase;
class UBlueprint;

namespace ck_test_hex_geometry_blueprint
{
    auto GraphName(FName InFunction) -> FName;
    auto AddGraph(FAutomationTestBase& InTest, UBlueprint& InBlueprint, UFunction& InFunction) -> bool;
}
