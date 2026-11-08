#include "CkTestsEditor_Module.h"

#include "CkTestsEditor_Log.h"

#include "CkCore/Macros/CkMacros.h"

#include <Framework/Application/SlateApplication.h>
#include <Misc/App.h>
#include <Misc/AutomationTest.h>

// --------------------------------------------------------------------------------------------------------------------

#define LOCTEXT_NAMESPACE "FCkTestsEditorModule"

void FCkTestsEditorModule::StartupModule()
{
    ck::tests_editor::Log(TEXT("[CkTestsEditor] Module started."));

    // Tests drive Slate's pointer with FSlateApplication::SetCursorPos. Two problems follow:
    // - The next test in the same editor inherits the position: a widget that opens on hover or closes on
    //   mouse-leave then behaves differently in a suite than alone (Ck.UiAuthoring.CharacterEquipment.PIE
    //   after ActionCard, 2026-11-06). So the cursor is put back after every test; OnTestEndEvent fires once
    //   the test's latent commands have drained.
    // - With the platform cursor, every one of those moves (and every put-back) moves the person's real
    //   mouse while a gate runs. So unattended runs give Slate's cursor user a virtual cursor, and nothing
    //   here reaches the OS cursor.
    auto& Framework = FAutomationTestFramework::Get();
    _TestStartHandle = Framework.OnTestStartEvent.AddRaw(this, &FCkTestsEditorModule::OnTestStart);
    _TestEndHandle = Framework.OnTestEndEvent.AddRaw(this, &FCkTestsEditorModule::OnTestEnd);
}

void FCkTestsEditorModule::ShutdownModule()
{
    auto& Framework = FAutomationTestFramework::Get();
    Framework.OnTestStartEvent.Remove(_TestStartHandle);
    Framework.OnTestEndEvent.Remove(_TestEndHandle);

    ck::tests_editor::Log(TEXT("[CkTestsEditor] Module stopped."));
}

auto FCkTestsEditorModule::OnTestStart(FAutomationTestBase*) -> void
{
    // Unattended runs only: a person running tests by hand keeps their own mouse.
    _CursorCaptured = FApp::IsUnattended() && FSlateApplication::IsInitialized();
    if (NOT _CursorCaptured)
    { return; }

    auto& Slate = FSlateApplication::Get();
    // FFauxSlateCursor: SetCursorPos/GetCursorPos go through the cursor user, so they stay in this process.
    // A no-op once switched; done here rather than at startup because the cursor user may not exist yet then.
    Slate.UsePlatformCursorForCursorUser(false);

    _CursorAtTestStart = Slate.GetCursorPos();
}

auto FCkTestsEditorModule::OnTestEnd(FAutomationTestBase*) -> void
{
    if (NOT _CursorCaptured || NOT FSlateApplication::IsInitialized())
    { return; }

    _CursorCaptured = false;
    auto& Slate = FSlateApplication::Get();
    // PIE teardown can hand the cursor user back to the platform cursor before this fires (CommonUI's action router
    // does on exit). Re-assert the virtual cursor so the put-back never reaches the OS cursor.
    Slate.UsePlatformCursorForCursorUser(false);
    Slate.SetCursorPos(_CursorAtTestStart);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCkTestsEditorModule, CkTestsEditor)

// --------------------------------------------------------------------------------------------------------------------
