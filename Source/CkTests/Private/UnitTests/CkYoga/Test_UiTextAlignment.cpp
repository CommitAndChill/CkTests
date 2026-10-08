#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/SCkUiSurface.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_text_alignment
{
    auto FindText(const TSharedRef<SWidget>& InRoot, const FName InTag) -> TSharedPtr<SCkFlexText>
    {
        if (InRoot->GetTypeAsString() == TEXT("SCkFlexText") && InRoot->GetTag() == InTag)
        { return StaticCastSharedRef<SCkFlexText>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        if (Children == nullptr) { return nullptr; }
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SCkFlexText> Found = FindText(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag); Found.IsValid())
            { return Found; }
        }
        return nullptr;
    }

    struct FWindowScope
    {
        FSlateApplication& Slate;
        TSharedPtr<SWindow> Window;
        ~FWindowScope() { if (Window.IsValid()) { Slate.DestroyWindowImmediately(Window.ToSharedRef()); } }
    };

    auto Tick(FSlateApplication& InSlate) -> void { InSlate.PumpMessages(); InSlate.Tick(); InSlate.Tick(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiTextAlignment_Runtime,
    "Ck.UiAuthoring.TextAlignment.RuntimeAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiTextAlignment_Runtime::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_text_alignment;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Text alignment requires Slate.")); return false; }

    const FString Markup = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\"><text id=\"left\" class=\"left\">Short</text><text id=\"center\" class=\"center\">Short</text><text id=\"right\" class=\"right\">Short</text><text id=\"wrapped\" class=\"wrapped\">A long resource name with enough words to wrap across several narrow lines</text></column></region></ui>");
    const FString Styles = TEXT(".left { width: 180; } .center { width: 180; text-align: center; } .right { width: 180; text-align: right; } .wrapped { width: 90; text-align: center; text-wrap: wrap; }");
    const TSharedRef<FCkUiView> View = FCkUiView::Create({}, {}, {}, FCoreStyle::GetDefaultFontStyle("Regular", 12), {});
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    FSlateApplication& Slate = FSlateApplication::Get();
    FWindowScope Scope{Slate};
    Scope.Window = SNew(SWindow).ClientSize(FVector2D{300, 300}).CreateTitleBar(false)[Region];
    Slate.AddWindow(Scope.Window.ToSharedRef(), true);
    if (!TestTrue(TEXT("Aligned text document loads"), View->TryReload(Markup, Styles).Succeeded)) { return false; }
    Tick(Slate);

    const TSharedPtr<SCkFlexText> Left = FindText(Region, TEXT("left"));
    const TSharedPtr<SCkFlexText> Center = FindText(Region, TEXT("center"));
    const TSharedPtr<SCkFlexText> Right = FindText(Region, TEXT("right"));
    const TSharedPtr<SCkFlexText> Wrapped = FindText(Region, TEXT("wrapped"));
    if (!TestTrue(TEXT("All authored text widgets realize"), Left.IsValid() && Center.IsValid() && Right.IsValid() && Wrapped.IsValid())) { return false; }
    TestEqual(TEXT("Default text justification is left"), Left->GetJustification(), ETextJustify::Left);
    TestEqual(TEXT("Center reaches Slate text layout"), Center->GetJustification(), ETextJustify::Center);
    TestEqual(TEXT("Right reaches Slate text layout"), Right->GetJustification(), ETextJustify::Right);
    TestTrue(TEXT("Fixed width text has a common Slate layout width"),
        FMath::IsNearlyEqual(Left->GetCachedGeometry().GetLocalSize().X, 180.0f, 1.0f)
        && FMath::IsNearlyEqual(Center->GetCachedGeometry().GetLocalSize().X, 180.0f, 1.0f)
        && FMath::IsNearlyEqual(Right->GetCachedGeometry().GetLocalSize().X, 180.0f, 1.0f));
    const float WrappedHeight = Wrapped->Measure(90, YGMeasureModeExactly, YGUndefined, YGMeasureModeUndefined, 1).Y;
    const float SingleLineHeight = Center->Measure(90, YGMeasureModeExactly, YGUndefined, YGMeasureModeUndefined, 1).Y;
    TestTrue(TEXT("Centered text retains width-constrained wrapping"), WrappedHeight > SingleLineHeight * 2);

    const int64 Revision = View->GetRevision();
    const FCkUiLoadResult Invalid = View->TryReload(Markup, Styles + TEXT(".right { text-align: justify; }"));
    TestFalse(TEXT("Unsupported text alignment rejects"), Invalid.Succeeded);
    TestTrue(TEXT("Unsupported text alignment reports an error"), !Invalid.Errors.IsEmpty());
    TestEqual(TEXT("Rejected alignment preserves view revision"), View->GetRevision(), Revision);
    TestTrue(TEXT("Rejected alignment preserves the live text widget"), FindText(Region, TEXT("right")) == Right);
    TestEqual(TEXT("Rejected alignment preserves Slate justification"), Right->GetJustification(), ETextJustify::Right);

    const FString Reloaded = Styles.Replace(TEXT("text-align: right"), TEXT("text-align: left"));
    if (!TestTrue(TEXT("Valid alignment reload succeeds"), View->TryReload(Markup, Reloaded).Succeeded)) { return false; }
    Tick(Slate);
    const TSharedPtr<SCkFlexText> ReloadedRight = FindText(Region, TEXT("right"));
    TestTrue(TEXT("Reload updates text justification"), ReloadedRight.IsValid() && ReloadedRight->GetJustification() == ETextJustify::Left);
    return true;
}

#endif
