#include "Misc/AutomationTest.h"

#include "CkCueGym_SourceVisuals.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_cue_source_visual_rows_test
{
    FCkCueGym_SourceVisualRow MakeSphereWireRow()
    {
        FCkCueGym_SourceVisualRow Row;
        Row.Key = 1;
        Row.Role = 0;
        Row.Kind = ECkCueGym_SourceVisualKind::Sphere;
        Row.OutlineRecipeVersion = 1;
        Row.Pose = FTransform::Identity;
        Row.Color = FLinearColor::White;
        Row.Radius = 20.0f;
        Row.Segments = 12;
        Row.Rings = 12;
        Row.Axis = ECk_Plane_Axis::XY;
        Row.DrawLines = true;
        Row.RenderMode = ECk_Pmg_RenderMode::DoubleSided;
        Row.VisibleByRenderMode = true;
        Row.LineThickness = 2.0f;
        Row.Duration = -1.0;
        return Row;
    }

    constexpr auto Flags = EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ClientContext |
        EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkCueGym_SourceVisualRows_ValuePreflight,
    "Ck.Cue.Gym.SourceVisualRows.ValuePreflight",
    ck_cue_source_visual_rows_test::Flags)

bool FCkCueGym_SourceVisualRows_ValuePreflight::RunTest(const FString& Parameters)
{
    const FCkCueGym_SourceVisualRow Valid =
        ck_cue_source_visual_rows_test::MakeSphereWireRow();
    TArray<FCkCueGym_SourceVisualRow> Expanded;
    Expanded.AddDefaulted();
    TestTrue(TEXT("valid authored sphere recipe expands"),
        UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows({Valid}, Expanded));
    TestEqual(TEXT("valid expansion replaces prefilled output"), Expanded.Num(), 1);
    if (Expanded.Num() == 1)
    {
        TestEqual(TEXT("sphere recipe creates three 32-segment outlines"),
            Expanded[0].Lines.Num(), 96);
        TestEqual(TEXT("expanded row preserves the source key"), Expanded[0].Key, Valid.Key);
    }

    auto NoLines = Valid;
    NoLines.DrawLines = false;
    NoLines.LineThickness = 0.0f;
    TestTrue(TEXT("no-outline source accepts zero line thickness"),
        UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows({NoLines}, Expanded));
    TestEqual(TEXT("no-outline source has no expanded lines"),
        Expanded.Num() == 1 ? Expanded[0].Lines.Num() : -1, 0);

    auto RequiredLinesWithZeroThickness = Valid;
    RequiredLinesWithZeroThickness.LineThickness = 0.0f;
    TArray<FCkCueGym_SourceVisualRow> RejectedLines{Valid};
    TestFalse(TEXT("required outlines reject zero line thickness"),
        UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(
            {RequiredLinesWithZeroThickness}, RejectedLines));
    TestTrue(TEXT("rejected required outlines clear output"),
        RejectedLines.IsEmpty());

    const auto Reject = [this, &Valid](const TCHAR* Label,
        const TArray<FCkCueGym_SourceVisualRow>& Input)
    {
        TArray<FCkCueGym_SourceVisualRow> Output{Valid};
        const bool Accepted = UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(
            Input, Output);
        TestFalse(Label, Accepted);
        TestTrue(*FString::Printf(TEXT("%s clears prefilled output"), Label),
            Output.IsEmpty());
    };

    auto Bad = Valid;
    Bad.OutlineRecipeVersion = 0;
    Reject(TEXT("unsupported recipe version rejected"), {Bad});

    Bad = Valid;
    Bad.Kind = ECkCueGym_SourceVisualKind::Invalid;
    Reject(TEXT("default primitive kind rejected"), {Bad});

    Reject(TEXT("duplicate keys rejected"), {Valid, Valid});

    Bad = Valid;
    Bad.Pose.SetScale3D(FVector(0.0, 1.0, 1.0));
    Reject(TEXT("zero scale rejected"), {Bad});

    Bad = Valid;
    Bad.Pose.SetScale3D(FVector(-1.0, 1.0, 1.0));
    Reject(TEXT("negative scale rejected"), {Bad});

    Bad = Valid;
    Bad.Color.R = std::numeric_limits<float>::quiet_NaN();
    Reject(TEXT("NaN color rejected"), {Bad});

    Bad = Valid;
    Bad.Radius = -1.0f;
    Reject(TEXT("invalid sphere geometry rejected"), {Bad});

    Bad = Valid;
    Bad.Key = 2;
    Bad.Kind = ECkCueGym_SourceVisualKind::Invalid;
    Reject(TEXT("valid first row cannot publish an invalid second row"),
        {Valid, Bad});

    TArray<FCkCueGym_SourceVisualRow> Alias{Valid};
    TestFalse(TEXT("aliased input/output rejected"),
        UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(Alias, Alias));
    TestTrue(TEXT("aliased output cleared"), Alias.IsEmpty());
    return true;
}

#endif
