#include "CkSlateLayout/CkUiCheckbox.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/CkUiSelect.h"
#include "CkSlateLayout/SCkUiSurface.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Rendering/DrawElementTypes.h"
#include "Styling/CoreStyle.h"
#include "Types/PaintArgs.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_ui_form_styles_test
{
    constexpr TCHAR Markup[] = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"form\"><checkbox id=\"subtitles\" class=\"check\" value-bind=\"subtitles\" changed=\"toggle-subtitles\" label-bind=\"subtitles-label\"/><select id=\"mode\" class=\"select\" value-bind=\"mode\" options-bind=\"modes\" changed=\"set-mode\"/></column></region></ui>");
    constexpr TCHAR FirstCss[] = TEXT(".check { -ck-checkbox-size: 36px; -ck-checkbox-color: #627b81; -ck-checkbox-background-color: #627b81; -ck-checkbox-checked-color: #e8a43a; -ck-checkbox-label-color: #f5e9d0; } .select { -ck-select-font-size: 26; -ck-select-color: #f5e9d0; -ck-select-background-color: #14212d; -ck-select-border-color: #7c918f; -ck-select-accent-color: #e8a43a; -ck-select-padding: 12px; }");
    constexpr TCHAR SecondCss[] = TEXT(".check { -ck-checkbox-size: 44px; -ck-checkbox-color: #6f858e; -ck-checkbox-background-color: #6f858e; -ck-checkbox-checked-color: #d88b2d; -ck-checkbox-label-color: #fff1d9; } .select { -ck-select-font-size: 30; -ck-select-color: #fff1d9; -ck-select-background-color: #1b2833; -ck-select-border-color: #81999a; -ck-select-accent-color: #d88b2d; -ck-select-padding: 16px; }");

    template <typename TWidget>
    auto Find(const TSharedRef<SWidget>& InRoot, const FName& InTag, const FString& InType) -> TSharedPtr<TWidget>
    {
        if (InRoot->GetTag() == InTag && InRoot->GetTypeAsString() == InType)
        { return StaticCastSharedRef<TWidget>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<TWidget> Found = Find<TWidget>(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag, InType))
            { return Found; }
        }
        return {};
    }

    auto FindText(const TSharedRef<SWidget>& InRoot) -> TSharedPtr<STextBlock>
    {
        if (InRoot->GetTypeAsString() == TEXT("STextBlock")) { return StaticCastSharedRef<STextBlock>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<STextBlock> Found = FindText(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index))))
            { return Found; }
        }
        return {};
    }

    auto PaintHasTint(const TSharedRef<SWidget>& InWidget, const TSharedPtr<SWindow>& InWindow,
        const FLinearColor& InTint, FString* OutObservedTints = nullptr) -> bool
    {
        FSlateWindowElementList Elements(InWindow);
        FHittestGrid HitTestGrid;
        const FGeometry Geometry = InWidget->GetCachedGeometry();
        const FPaintArgs Args(InWindow.Get(), HitTestGrid, FVector2f::ZeroVector, 0.0, 0.0f);
        InWidget->Paint(Args, Geometry, FSlateRect(-10000.0f, -10000.0f, 10000.0f, 10000.0f), Elements, 0, FWidgetStyle(), true);
        const auto& Boxes = Elements.GetUncachedDrawElements().Get<static_cast<uint8>(EElementType::ET_Box)>();
        for (const FSlateBoxElement& Box : Boxes)
        {
            if (Box.GetTint().Equals(InTint)) { return true; }
            if (OutObservedTints != nullptr)
            { *OutObservedTints += FString::Printf(TEXT("Box=%s Effects=%u; "),
                *Box.GetTint().ToString(), static_cast<uint32>(Box.GetDrawEffects())); }
        }
        const auto& Rounded = Elements.GetUncachedDrawElements().Get<static_cast<uint8>(EElementType::ET_RoundedBox)>();
        for (const FSlateRoundedBoxElement& Box : Rounded)
        {
            if (Box.GetTint().Equals(InTint)) { return true; }
            if (OutObservedTints != nullptr)
            { *OutObservedTints += FString::Printf(TEXT("Rounded=%s Effects=%u; "),
                *Box.GetTint().ToString(), static_cast<uint32>(Box.GetDrawEffects())); }
        }
        return false;
    }

    auto Color(uint8 InRed, uint8 InGreen, uint8 InBlue) -> FLinearColor
    { return FLinearColor::FromSRGBColor(FColor(InRed, InGreen, InBlue)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiFormStyles,
    "Ck.UiAuthoring.FormStyles.RetainedNativeControls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiFormStyles::RunTest(const FString&) -> bool
{
    using namespace ck_ui_form_styles_test;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Form styles test requires Slate.")); return false; }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("Checkbox registration"), FCkUiCheckbox::Register(Registry).Succeeded)
        || !TestTrue(TEXT("Select registration"), FCkUiSelect::Register(Registry).Succeeded)) { return false; }
    TSharedPtr<FCkUiCollection> Options;
    if (!TestTrue(TEXT("Select options collection"), FCkUiCollection::TryCreate({{TEXT("label"), ECkUiFieldKind::Text}}, Options).Succeeded))
    { return false; }
    FCkUiRecordData Option;
    Option.Key = TEXT("windowed");
    Option.Fields.Add(TEXT("label"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = FText::FromString(TEXT("Windowed"))});
    if (!TestTrue(TEXT("Select option publication"), Options->TrySetRecords({Option}).Succeeded)) { return false; }

    bool Subtitles = true;
    FString Mode = TEXT("windowed");
    int32 Changes = 0;
    FCkUiView::FDataBindings Data;
    Data.Visibility.Add(TEXT("subtitles"), TAttribute<bool>::CreateLambda([&Subtitles]() { return Subtitles; }));
    Data.Text.Add(TEXT("subtitles-label"), TAttribute<FText>::CreateLambda([]() { return FText::FromString(TEXT("Subtitles")); }));
    Data.BoolChanged.Add(TEXT("toggle-subtitles"), FCkUiOnBoolChanged::CreateLambda([&Subtitles, &Changes](bool InValue) { Subtitles = InValue; ++Changes; }));
    Data.String.Add(TEXT("mode"), TAttribute<FString>::CreateLambda([&Mode]() { return Mode; }));
    Data.Collections.Add(TEXT("modes"), Options);
    Data.StringChanged.Add(TEXT("set-mode"), FCkUiOnStringChanged::CreateLambda([&Mode, &Changes](const FString& InValue) { Mode = InValue; ++Changes; }));
    TSharedPtr<FCkUiView> View = FCkUiView::Create({}, {}, {}, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16), MoveTemp(Data), Registry.CreateSnapshot());
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    FSlateApplication& Slate = FSlateApplication::Get();
    const TSharedPtr<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(500.0f, 280.0f)).CreateTitleBar(false).HasCloseButton(false)[Region];
    Slate.AddWindow(Window.ToSharedRef(), true);
    const auto CloseWindow = [&Slate, &Window]() { Slate.DestroyWindowImmediately(Window.ToSharedRef()); };

    if (!TestTrue(TEXT("First authored form style loads"), View->TryReload(Markup, FirstCss, TEXT("FormStylesFirst")).Succeeded))
    { CloseWindow(); return false; }
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    const TSharedPtr<SCheckBox> Checkbox = Find<SCheckBox>(Region, TEXT("subtitles"), TEXT("SCheckBox"));
    const TSharedPtr<SWidget> Select = Find<SWidget>(Region, TEXT("mode"), TEXT("SCkUiSelect"));
    if (!TestTrue(TEXT("Native controls mounted"), Checkbox.IsValid() && Select.IsValid()))
    { CloseWindow(); return false; }
    const TSharedPtr<STextBlock> CheckLabel = FindText(Checkbox.ToSharedRef());
    const TSharedPtr<STextBlock> SelectLabel = FindText(Select.ToSharedRef());
    if (!TestTrue(TEXT("Native labels mounted"), CheckLabel.IsValid() && SelectLabel.IsValid()))
    { CloseWindow(); return false; }
    TestTrue(TEXT("Initial checkbox style affects native glyph and label"),
        Checkbox->GetDesiredSize().Y >= 36.0f
            && CheckLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(Color(0xf5, 0xe9, 0xd0))
            && PaintHasTint(Checkbox.ToSharedRef(), Window, Color(0xe8, 0xa4, 0x3a)));
    TestTrue(TEXT("Initial checkbox paints authored background tint"),
        PaintHasTint(Checkbox.ToSharedRef(), Window, Color(0x62, 0x7b, 0x81)));
    TestTrue(TEXT("Initial select style affects native text and button"),
        SelectLabel->GetFont().Size == 26
            && SelectLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(Color(0xf5, 0xe9, 0xd0))
            && PaintHasTint(Select.ToSharedRef(), Window, Color(0x14, 0x21, 0x2d)));
    TestEqual(TEXT("Style loading emits no model changes"), Changes, 0);

    const FCkUiLoadResult SecondReload = View->TryReload(Markup, SecondCss, TEXT("FormStylesSecond"));
    if (!TestTrue(FString::Printf(TEXT("Compatible form style reload: %s"), *FString::Join(SecondReload.Errors, TEXT("; "))), SecondReload.Succeeded))
    { CloseWindow(); return false; }
    Slate.Tick(); Slate.Tick();
    TestTrue(TEXT("Reload retains checkbox identity"), Find<SCheckBox>(Region, TEXT("subtitles"), TEXT("SCheckBox")) == Checkbox);
    TestTrue(TEXT("Reload retains select identity"), Find<SWidget>(Region, TEXT("mode"), TEXT("SCkUiSelect")) == Select);
    TestTrue(FString::Printf(TEXT("Reload checkbox desired height %.2f reflects 44-unit glyph"), Checkbox->GetDesiredSize().Y),
        Checkbox->GetDesiredSize().Y >= 44.0f);
    TestTrue(TEXT("Reload applies checkbox label foreground"),
        CheckLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(Color(0xff, 0xf1, 0xd9)));
    TestEqual(TEXT("Reload applies select font size"), SelectLabel->GetFont().Size, 30.0f);
    TestTrue(TEXT("Reload applies select text foreground"),
        SelectLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(Color(0xff, 0xf1, 0xd9)));
    TestTrue(TEXT("Reload paints checked checkbox tint"),
        PaintHasTint(Checkbox.ToSharedRef(), Window, Color(0xd8, 0x8b, 0x2d)));
    TestTrue(TEXT("Reload paints checkbox background tint"),
        PaintHasTint(Checkbox.ToSharedRef(), Window, Color(0x6f, 0x85, 0x8e)));
    TestTrue(TEXT("Reload paints select background tint"),
        PaintHasTint(Select.ToSharedRef(), Window, Color(0x1b, 0x28, 0x33)));
    TestEqual(TEXT("Style reload emits no model changes"), Changes, 0);

    const int64 AcceptedRevision = View->GetRevision();
    const FCkUiLoadResult InvalidSelect = View->TryReload(Markup,
        TEXT(".check {} .select { -ck-select-font-size: 0; }"), TEXT("FormStylesInvalid"));
    TestFalse(TEXT("Invalid select font size rejects complete reload"), InvalidSelect.Succeeded);
    TestTrue(TEXT("Invalid select reload reports the font size error"),
        FString::Join(InvalidSelect.Errors, TEXT("; ")).Contains(TEXT("Select font size must be from 1 through 512.")));
    const FCkUiLoadResult InvalidCheckbox = View->TryReload(Markup,
        TEXT(".check { -ck-checkbox-size: 0px; } .select {}"), TEXT("FormStylesInvalidCheckbox"));
    TestFalse(TEXT("Zero checkbox glyph size rejects complete reload"), InvalidCheckbox.Succeeded);
    TestTrue(TEXT("Invalid checkbox reload reports the glyph size error"),
        FString::Join(InvalidCheckbox.Errors, TEXT("; ")).Contains(TEXT("Checkbox size must be greater than zero and at most 512.")));
    TestEqual(TEXT("Rejected styles keep accepted revision"), View->GetRevision(), AcceptedRevision);
    TestTrue(TEXT("Rejected styles keep checkbox identity"), Find<SCheckBox>(Region, TEXT("subtitles"), TEXT("SCheckBox")) == Checkbox);
    TestTrue(TEXT("Rejected styles keep select identity"), Find<SWidget>(Region, TEXT("mode"), TEXT("SCkUiSelect")) == Select);
    TestEqual(TEXT("Rejected styles keep accepted select font"), SelectLabel->GetFont().Size, 30.0f);

    const FCkUiLoadResult DefaultReload = View->TryReload(Markup, TEXT(".check {} .select {}"), TEXT("FormStylesDefaults"));
    if (!TestTrue(FString::Printf(TEXT("Omitted overrides reload CoreStyle defaults: %s"), *FString::Join(DefaultReload.Errors, TEXT("; "))),
        DefaultReload.Succeeded))
    { CloseWindow(); return false; }
    Slate.Tick(); Slate.Tick();
    TestTrue(TEXT("Default reload retains controls and resets select font"),
        Find<SCheckBox>(Region, TEXT("subtitles"), TEXT("SCheckBox")) == Checkbox
            && Find<SWidget>(Region, TEXT("mode"), TEXT("SCkUiSelect")) == Select
            && SelectLabel->GetFont().Size == 16 && Checkbox->GetDesiredSize().Y < 44.0f);
    TestFalse(TEXT("Default reload removes authored checkbox background tint"),
        PaintHasTint(Checkbox.ToSharedRef(), Window, Color(0x6f, 0x85, 0x8e)));
    TestEqual(TEXT("Default reload emits no model changes"), Changes, 0);
    if (!TestTrue(TEXT("Styled controls can be reapplied after defaults"),
        View->TryReload(Markup, SecondCss, TEXT("FormStylesRestyled")).Succeeded))
    { CloseWindow(); return false; }
    Slate.Tick(); Slate.Tick();
    TestTrue(TEXT("Restyle restores authored checkbox background tint"),
        PaintHasTint(Checkbox.ToSharedRef(), Window, Color(0x6f, 0x85, 0x8e)));

    View->ReleaseOwnerInteractions();
    View.Reset();
    Checkbox->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
    Select->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
    Checkbox->SlatePrepass();
    Select->SlatePrepass();
    // The released owner makes the checkbox value getter fall back to Unchecked.
    TestTrue(TEXT("Held checkbox retains authored glyph geometry after owner release"),
        Checkbox->GetDesiredSize().Y >= 44.0f);
    TestEqual(TEXT("Held select retains authored font after owner release"), SelectLabel->GetFont().Size, 30.0f);
    TestEqual(TEXT("Held checkbox falls back to unchecked state after owner release"),
        Checkbox->GetCheckedState(), ECheckBoxState::Unchecked);
    TestFalse(TEXT("Held checkbox is disabled after owner release"), Checkbox->IsEnabled());
    FString ObservedCheckboxTints;
    const bool HasBackgroundTint = PaintHasTint(Checkbox.ToSharedRef(), Window,
        Color(0x6f, 0x85, 0x8e), &ObservedCheckboxTints);
    TestTrue(FString::Printf(TEXT("Held checkbox paints authored background tint after owner release; observed %s"),
        *ObservedCheckboxTints), HasBackgroundTint);
    TestFalse(TEXT("Held select is disabled after owner release"), Select->IsEnabled());
    FLinearColor DisabledSelectColor = Color(0x1b, 0x28, 0x33);
    DisabledSelectColor.A *= 0.5f;
    TestTrue(TEXT("Held select paints authored disabled brush after owner release"),
        PaintHasTint(Select.ToSharedRef(), Window, DisabledSelectColor));
    TestEqual(TEXT("Owner release emits no model changes"), Changes, 0);
    CloseWindow();
    return true;
}

#endif
