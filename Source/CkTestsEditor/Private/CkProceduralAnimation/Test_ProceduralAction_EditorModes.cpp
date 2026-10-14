#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModes.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"

#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_EditorModes_SolvedFramesAndSource,
    "Ck.ProceduralAnimation.Action.Editor.Modes.SolvedFramesAndSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkTest_ProceduralAction_EditorModes_SolvedFramesAndSource::
    RunTest(
        const FString&)
    -> bool
{
    int64 PureStep = INDEX_NONE;
    TestTrue(TEXT("negative seek time maps to initial step"),
        FCkProceduralActionEditorModes::ToStep(FCk_Time{-0.25}, PureStep)
            && PureStep == 0);
    TestTrue(TEXT("ordinary seek time maps to 60 Hz step"),
        FCkProceduralActionEditorModes::ToStep(FCk_Time{0.2}, PureStep)
            && PureStep == 12);
    TestFalse(TEXT("huge finite seek time rejects before int64 conversion"),
        FCkProceduralActionEditorModes::ToStep(FCk_Time{1.0e300}, PureStep));
    TestEqual(TEXT("rejected pure conversion preserves previous step"), PureStep, int64{12});
    constexpr auto ClipPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    constexpr auto PreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    auto* Clip = LoadObject<UCk_ProceduralAction_Data>(nullptr, ClipPath);
    auto* Preview = LoadObject<UCk_ProceduralActionPreview_Config>(nullptr, PreviewPath);
    if (NOT TestNotNull(TEXT("saved clip"), Clip)
        || NOT TestNotNull(TEXT("saved preview"), Preview))
    { return false; }
    auto Session = MakeShared<FCk_ProceduralActionAuthoringSession>(Clip);
    Session->SetPreviewConfig(Preview);
    auto Modes = FCkProceduralActionEditorModes{Session};
    if (NOT TestTrue(TEXT("production Solver starts"), Modes.StartSolver()))
    { Modes.Close(); return false; }
    TestNull(TEXT("no solved frame before the Solver has run"), Modes.GetDisplayedFrame());
    TestTrue(TEXT("a requested time is accepted before it is solved"), Modes.Show(FCk_Time{0.3}));
    TestEqual(TEXT("an unsolved time displays the start"), Modes.GetTime().Get_Seconds(), 0.0);
    TestFalse(TEXT("an unrepresentable time is rejected"), Modes.Show(FCk_Time{1.0e300}));
    TestTrue(TEXT("the rejected time is reported"), Modes.GetStatusText().ToString().Contains(TEXT("cannot be represented")));
    Session->Refresh();
    Modes.SourceChanged();
    TestNotNull(TEXT("a source revision restarts the Solver"), Modes.GetSolver());
    TestEqual(TEXT("the restart drops the solved frames"), Modes.GetFrameCount(), 0);
    TestFalse(TEXT("the restart clears the rejected-time report"), Modes.GetStatusText().ToString().Contains(TEXT("cannot be represented")));
    const auto* WorldAfter = Modes.GetSolver()->GetWorld();
    Modes.SourceChanged();
    TestTrue(TEXT("an unchanged revision keeps the Solver"), Modes.GetSolver() != nullptr && Modes.GetSolver()->GetWorld() == WorldAfter);
    Modes.Close();
    return NOT HasAnyErrors();
}

#endif
