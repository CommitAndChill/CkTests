#include "CkTestsEditor_Module.h"

#include "CkTestsEditor_Log.h"
#include "Private/CkGrid/CkTest_HexPersistenceAssets.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"

#include "CkCore/Macros/CkMacros.h"

#include <Framework/Application/SlateApplication.h>
#include <Misc/App.h>
#include <Engine/Blueprint.h>
#include <Kismet2/KismetEditorUtilities.h>
#include <Misc/AutomationTest.h>
#include <Misc/PackageName.h>
#include <UObject/ICookInfo.h>

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

    // The runtime recipe finds the board by object path. Entry has no asset dependency on it.
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("CK_HEX_PACKAGED_ACCEPTANCE_COOK")) == TEXT("1"))
    {
        _HexAcceptanceCookHandle = UE::Cook::FDelegates::ModifyCook.AddLambda(
            [](UE::Cook::ICookInfo&, TArray<UE::Cook::FPackageCookRule>& Rules)
            {
                for (const auto* Path : {ck::tests::hex_persistence::BoardAssetPath, ck::tests::hex_persistence::Radius32BoardAssetPath})
                {
                    const auto BoardName = FName(*FPackageName::ObjectPathToPackageName(FString{Path}));
                    auto& Rule = Rules.AddDefaulted_GetRef();
                    Rule.PackageName = BoardName;
                    Rule.InstigatorName = FName(TEXT("CkHexPackagedAcceptance"));
                    Rule.CookRule = UE::Cook::EPackageCookRule::AddToCook;
                    UE_LOG(LogTemp, Display, TEXT("HexPackaged cook includes %s"), *BoardName.ToString());
                }
            });
    }
}

void FCkTestsEditorModule::ShutdownModule()
{
    auto& Framework = FAutomationTestFramework::Get();
    Framework.OnTestStartEvent.Remove(_TestStartHandle);
    Framework.OnTestEndEvent.Remove(_TestEndHandle);
    if (_HexAcceptanceCookHandle.IsValid())
    {
        UE::Cook::FDelegates::ModifyCook.Remove(_HexAcceptanceCookHandle);
        _HexAcceptanceCookHandle.Reset();
    }

    ck::tests_editor::Log(TEXT("[CkTestsEditor] Module stopped."));
}

auto FCkTestsEditorModule::OnTestStart(FAutomationTestBase* InTest) -> void
{
    const auto TestPath = InTest ? InTest->GetBeautifiedTestName() : FString{};
    if (TestPath.StartsWith(TEXT("Ck.HexGrid.Persistence.")) || TestPath.StartsWith(TEXT("Ck.HexGrid.Net.")))
    {
        ck::tests::hex_persistence::EnsureAssets(*InTest);
        if (TestPath == TEXT("Ck.HexGrid.Net.PreHexSquareSave"))
        {
            auto* Library = LoadObject<UBlueprint>(nullptr, TEXT("/CkFoundation/CkGrid/Utils_CkGrid_ML.Utils_CkGrid_ML"));
            if (InTest->TestNotNull(TEXT("Existing square macro library loads"), Library))
            {
                FKismetEditorUtilities::CompileBlueprint(Library);
                InTest->TestTrue(TEXT("Existing square macro library compiles"), Library->Status == BS_UpToDate);
            }
        }
#if WITH_DEV_AUTOMATION_TESTS
        if (TestPath == TEXT("Ck.HexGrid.Net.MixedSquareHex_MPReload"))
        { InTest->TestTrue(TEXT("Saved net consumer Blueprint compiled"), ck::tests::hex_persistence::EnsureNetConsumer(*InTest)); }
#endif
        if (TestPath == TEXT("Ck.HexGrid.Persistence.RenderedGym"))
        { ck::tests::hex_persistence::EnsureGymMap(*InTest); }
    }

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
