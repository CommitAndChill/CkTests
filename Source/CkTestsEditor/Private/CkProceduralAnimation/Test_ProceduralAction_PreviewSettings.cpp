#include "Misc/AutomationTest.h"

#include "CkProceduralAnimationEditor/CkProceduralActionEditor_UserSettings.h"

#include "Misc/Guid.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PreviewSettings,
    "Ck.ProceduralAnimation.Action.Editor.PreviewSettings.IsolatedMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkTest_ProceduralAction_PreviewSettings::
    RunTest(
        const FString&)
    -> bool
{
    auto* Settings = NewObject<UCk_ProceduralActionEditor_UserSettings>(GetTransientPackage());
    const auto ClipPath = FString::Printf(TEXT("/Transient/CkTest/%s"),
        *FGuid::NewGuid().ToString());
    const auto PreviewPath = FString::Printf(TEXT("%s_Preview"), *ClipPath);
    TestEqual(TEXT("unassigned clip has no preview selection"),
        Settings->GetPreviewPath(ClipPath), FString{});
    TestTrue(TEXT("selecting preview changes mapping"),
        Settings->SetPreviewPath(ClipPath, PreviewPath));
    TestEqual(TEXT("selection restores by clip path"),
        Settings->GetPreviewPath(ClipPath), PreviewPath);
    TestFalse(TEXT("selecting the same preview is unchanged"),
        Settings->SetPreviewPath(ClipPath, PreviewPath));
    TestTrue(TEXT("clearing selection removes mapping"),
        Settings->SetPreviewPath(ClipPath, FString{}));
    TestEqual(TEXT("cleared selection is empty"),
        Settings->GetPreviewPath(ClipPath), FString{});
    return true;
}
