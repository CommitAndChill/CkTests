#pragma once

#include <CoreMinimal.h>
#include <Modules/ModuleManager.h>

class FAutomationTestBase;

// --------------------------------------------------------------------------------------------------------------------

class FCkTestsEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    auto OnTestStart(FAutomationTestBase* InTest) -> void;
    auto OnTestEnd(FAutomationTestBase* InTest) -> void;

    FDelegateHandle _TestStartHandle;
    FDelegateHandle _TestEndHandle;
    FVector2D _CursorAtTestStart = FVector2D::ZeroVector;
    bool _CursorCaptured = false;
};

// --------------------------------------------------------------------------------------------------------------------
