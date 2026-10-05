#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/SCkUiTabs.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_tabs_visual_style
{
    const FLinearColor InitialInactive{FColor{0x66, 0x73, 0x85}};
    const FLinearColor InitialActive{FColor{0x66, 0xdc, 0xfa}};
    const FLinearColor ReloadInactive{FColor{0xc2, 0x77, 0x45}};
    const FLinearColor ReloadActive{FColor{0xfa, 0xc8, 0x66}};

    auto Markup() -> FString
    {
        return TEXT("<ui version=\"1\"><region name=\"main\"><tabs id=\"tabs\" class=\"tab-strip\" value-bind=\"active\" changed=\"activate\"><tab id=\"overview\" key=\"overview\" label=\"Overview\"><text id=\"overview-panel\">Overview panel</text></tab><tab id=\"properties\" key=\"properties\" label=\"Properties\" enabled-bind=\"properties-enabled\"><text id=\"properties-panel\">Properties panel</text></tab><tab id=\"system\" key=\"system\" label=\"System\"><text id=\"system-panel\">System panel</text></tab></tabs></region></ui>");
    }

    auto Styles(const bool bReloaded = false) -> FString
    {
        return bReloaded
            ? TEXT(".tab-strip { -ck-tabs-inactive-color: #c27745; -ck-tabs-active-color: #fac866; -ck-tabs-underline-color: #f0a23d; -ck-tabs-underline-height: 3px; -ck-tabs-font-size: 11px; -ck-tabs-font-weight: normal; -ck-tabs-header-padding-x: 9px; -ck-tabs-header-padding-y: 4px; -ck-tabs-header-width: 90px; -ck-tabs-header-align: right; }")
            : TEXT(".tab-strip { -ck-tabs-inactive-color: #667385; -ck-tabs-active-color: #66dcfa; -ck-tabs-underline-color: #1fb8eb; -ck-tabs-underline-height: 2px; -ck-tabs-font-size: 10px; -ck-tabs-font-weight: bold; -ck-tabs-header-padding-x: 7px; -ck-tabs-header-padding-y: 3px; -ck-tabs-header-width: 80px; -ck-tabs-header-align: center; }");
    }

    auto FindHeader(const TSharedRef<SWidget>& InRoot, const FString& InLabel) -> TSharedPtr<SButton>
    {
        if (InRoot->GetTypeAsString() == TEXT("SButton"))
        {
            const FChildren* Children = InRoot->GetChildren();
            for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
            {
                const TSharedRef<SWidget> Child = ConstCastSharedRef<SWidget>(Children->GetChildAt(Index));
                if (Child->GetTypeAsString() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Child)->GetText().ToString() == InLabel)
                { return StaticCastSharedRef<SButton>(InRoot); }
            }
        }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SButton> Found = FindHeader(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InLabel); Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto FindLabel(const TSharedRef<SButton>& InHeader) -> TSharedPtr<STextBlock>
    {
        const FChildren* Children = InHeader->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const TSharedRef<SWidget> Child = ConstCastSharedRef<SWidget>(Children->GetChildAt(Index));
            if (Child->GetTypeAsString() == TEXT("STextBlock")) { return StaticCastSharedRef<STextBlock>(Child); }
        }
        return {};
    }

    auto FindUnderline(const TSharedRef<SButton>& InHeader) -> TSharedPtr<SBox>
    {
        const TSharedPtr<SWidget> Presentation = InHeader->GetParentWidget();
        const FChildren* Children = Presentation.IsValid() ? Presentation->GetChildren() : nullptr;
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const TSharedRef<SWidget> Child = ConstCastSharedRef<SWidget>(Children->GetChildAt(Index));
            if (Child->GetTypeAsString() != TEXT("SBox")) { continue; }
            const FChildren* BoxChildren = Child->GetChildren();
            if (BoxChildren != nullptr && BoxChildren->Num() == 1 && BoxChildren->GetChildAt(0)->GetTypeAsString() == TEXT("SImage"))
            { return StaticCastSharedRef<SBox>(Child); }
        }
        return {};
    }

    auto Tick(FSlateApplication& InSlate) -> void { InSlate.PumpMessages(); InSlate.Tick(); InSlate.Tick(); }

    auto Click(const TSharedRef<SButton>& InButton) -> bool
    {
        const FGeometry Geometry = InButton->GetCachedGeometry();
        const FVector2D Position = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5f;
        const TSet<FKey> DownButtons{EKeys::LeftMouseButton};
        const FPointerEvent Down{0, Position, Position, DownButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const TSet<FKey> UpButtons;
        const FPointerEvent Up{0, Position, Position, UpButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        FSlateApplication& Slate = FSlateApplication::Get();
        FWidgetPath Path;
        if (!Slate.GeneratePathToWidgetUnchecked(InButton, Path)) { return false; }
        Slate.ProcessMouseMoveEvent(Down);
        return Slate.ProcessMouseButtonDownEvent(Path.GetWindow()->GetNativeWindow(), Down) && Slate.ProcessMouseButtonUpEvent(Up);
    }

    struct FWindowScope final
    {
        explicit FWindowScope(FSlateApplication& InSlate) : Slate(InSlate) {}
        ~FWindowScope() { if (Window.IsValid()) { Slate.DestroyWindowImmediately(Window.ToSharedRef()); } }

        FSlateApplication& Slate;
        TSharedPtr<SWindow> Window;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkUiTabs_VisualStyle,
    "Ck.UiAuthoring.Tabs.VisualStyle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiTabs_VisualStyle::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_tabs_visual_style;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Tabs visual-style test requires Slate.")); return false; }

    FString Active = TEXT("overview");
    bool bPropertiesEnabled = true;
    int32 ActivationCalls = 0;
    auto Data = FCkUiView::FDataBindings{};
    Data.String.Add(TEXT("active"), TAttribute<FString>::CreateLambda([&Active] { return Active; }));
    Data.Visibility.Add(TEXT("properties-enabled"), TAttribute<bool>::CreateLambda([&bPropertiesEnabled] { return bPropertiesEnabled; }));
    Data.StringChanged.Add(TEXT("activate"), FCkUiOnStringChanged::CreateLambda([&Active, &ActivationCalls](const FString& InKey)
    { ++ActivationCalls; Active = InKey; }));

    const TSharedRef<FCkUiView> View = FCkUiView::Create({}, {}, {}, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12), MoveTemp(Data));
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    FSlateApplication& Slate = FSlateApplication::Get();
    FWindowScope Scope{Slate};
    Scope.Window = SNew(SWindow).AutoCenter(EAutoCenter::None).ClientSize(FVector2D{360.0f, 180.0f}).CreateTitleBar(false).HasCloseButton(false)[Region];
    Slate.AddWindow(Scope.Window.ToSharedRef(), true);
    if (!TestTrue(TEXT("Valid underline-tabs document loads"), View->TryReload(Markup(), Styles(), TEXT("UiTabsVisualStyle")).Succeeded)) { return false; }
    Tick(Slate);

    const TSharedPtr<SCkUiTabs> Tabs = View->GetTabs(TEXT("tabs"));
    const TSharedPtr<SButton> Overview = FindHeader(Region, TEXT("Overview"));
    const TSharedPtr<SButton> Properties = FindHeader(Region, TEXT("Properties"));
    const TSharedPtr<SButton> System = FindHeader(Region, TEXT("System"));
    const TSharedPtr<STextBlock> OverviewLabel = Overview.IsValid() ? FindLabel(Overview.ToSharedRef()) : nullptr;
    const TSharedPtr<STextBlock> PropertiesLabel = Properties.IsValid() ? FindLabel(Properties.ToSharedRef()) : nullptr;
    const TSharedPtr<SBox> OverviewUnderline = Overview.IsValid() ? FindUnderline(Overview.ToSharedRef()) : nullptr;
    const TSharedPtr<SBox> PropertiesUnderline = Properties.IsValid() ? FindUnderline(Properties.ToSharedRef()) : nullptr;
    if (!TestTrue(TEXT("Styled tabs mount retained headers, labels, and underline presenters"), Tabs.IsValid() && Overview.IsValid() && Properties.IsValid() && System.IsValid()
        && OverviewLabel.IsValid() && PropertiesLabel.IsValid() && OverviewUnderline.IsValid() && PropertiesUnderline.IsValid())) { return false; }
    const auto HeaderX = [&Tabs](const TSharedRef<SWidget>& Header) -> double
    { return Tabs->GetCachedGeometry().AbsoluteToLocal(Header->GetCachedGeometry().GetAbsolutePosition()).X; };
    TestTrue(TEXT("Three equal-width authored headers form a centered rail"),
        FMath::IsNearlyEqual(Overview->GetCachedGeometry().GetLocalSize().X, 80.0f, 1.0f)
        && FMath::IsNearlyEqual(Properties->GetCachedGeometry().GetLocalSize().X, 80.0f, 1.0f)
        && FMath::IsNearlyEqual(System->GetCachedGeometry().GetLocalSize().X, 80.0f, 1.0f)
        && FMath::IsNearlyEqual(HeaderX(Overview.ToSharedRef()), 60.0, 2.0)
        && FMath::IsNearlyEqual(HeaderX(Properties.ToSharedRef()), 140.0, 2.0)
        && FMath::IsNearlyEqual(HeaderX(System.ToSharedRef()), 220.0, 2.0));
    Scope.Window->Resize(FVector2D{170.0f, 240.0f});
    for (int32 Frame = 0; Frame < 4; ++Frame) { Tick(Slate); }
    TestTrue(TEXT("Fixed-width centered headers wrap in a narrow native viewport without full-width slots"),
        FMath::IsNearlyEqual(Overview->GetCachedGeometry().GetLocalSize().X, 80.0f, 1.0f)
        && FMath::IsNearlyEqual(Properties->GetCachedGeometry().GetLocalSize().X, 80.0f, 1.0f)
        && FMath::IsNearlyEqual(HeaderX(Overview.ToSharedRef()), 5.0, 2.0)
        && FMath::IsNearlyEqual(HeaderX(Properties.ToSharedRef()), 85.0, 2.0)
        && System->GetCachedGeometry().GetAbsolutePosition().Y > Overview->GetCachedGeometry().GetAbsolutePosition().Y);
    Scope.Window->Resize(FVector2D{360.0f, 180.0f});
    for (int32 Frame = 0; Frame < 4; ++Frame) { Tick(Slate); }
    TestTrue(TEXT("Styled tab headers are flat presentations with authored active and inactive label colors"),
        OverviewLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(InitialActive)
        && PropertiesLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(InitialInactive)
        && OverviewLabel->GetFont().Size == 10 && PropertiesLabel->GetFont().Size == 10
        && OverviewLabel->GetFont().TypefaceFontName == TEXT("Bold") && PropertiesLabel->GetFont().TypefaceFontName == TEXT("Bold"));
    TestTrue(TEXT("Active tab reserves the authored two-pixel underline strip while inactive tab remains transparent"),
        FMath::IsNearlyEqual(OverviewUnderline->GetCachedGeometry().GetLocalSize().Y, 2.0f)
        && FMath::IsNearlyEqual(PropertiesUnderline->GetCachedGeometry().GetLocalSize().Y, 2.0f));

    TestTrue(TEXT("Styled native header proposes the selected key through its real click path"), Click(Properties.ToSharedRef()));
    Tick(Slate);
    TestTrue(TEXT("Click updates the authoritative selection and swaps active/inactive visual labels"), Active == TEXT("properties") && ActivationCalls == 1
        && OverviewLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(InitialInactive)
        && PropertiesLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(InitialActive));

    bPropertiesEnabled = false;
    Tick(Slate);
    const int32 CallsBeforeDisabledClick = ActivationCalls;
    Click(Properties.ToSharedRef());
    TestTrue(TEXT("Disabled styled header remains inert without changing the authoritative selection"), !Properties->IsEnabled()
        && Active == TEXT("properties") && ActivationCalls == CallsBeforeDisabledClick);
    bPropertiesEnabled = true;
    Tick(Slate);

    const int64 AcceptedRevision = View->GetRevision();
    if (!TestTrue(TEXT("Compatible styled tabs reload succeeds"), View->TryReload(Markup(), Styles(true), TEXT("UiTabsVisualStyleReload")).Succeeded)) { return false; }
    Tick(Slate);
    TestTrue(TEXT("Compatible style reload retains the native tabs and keyed header identities"), View->GetRevision() == AcceptedRevision + 1
        && View->GetTabs(TEXT("tabs")) == Tabs && FindHeader(Region, TEXT("Overview")) == Overview && FindHeader(Region, TEXT("Properties")) == Properties
        && FindHeader(Region, TEXT("System")) == System && Active == TEXT("properties") && ActivationCalls == CallsBeforeDisabledClick);
    TestTrue(TEXT("Compatible style reload updates uniform width and right rail alignment in native geometry"),
        FMath::IsNearlyEqual(Overview->GetCachedGeometry().GetLocalSize().X, 90.0f, 1.0f)
        && FMath::IsNearlyEqual(Properties->GetCachedGeometry().GetLocalSize().X, 90.0f, 1.0f)
        && FMath::IsNearlyEqual(System->GetCachedGeometry().GetLocalSize().X, 90.0f, 1.0f)
        && FMath::IsNearlyEqual(HeaderX(Overview.ToSharedRef()), 90.0, 2.0)
        && FMath::IsNearlyEqual(HeaderX(Properties.ToSharedRef()), 180.0, 2.0)
        && FMath::IsNearlyEqual(HeaderX(System.ToSharedRef()), 270.0, 2.0));
    TestTrue(TEXT("Compatible style reload updates flat label colors and underline height in place"),
        OverviewLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(ReloadInactive)
        && PropertiesLabel->GetColorAndOpacity().GetSpecifiedColor().Equals(ReloadActive)
        && OverviewLabel->GetFont().Size == 11 && PropertiesLabel->GetFont().Size == 11
        && OverviewLabel->GetFont().TypefaceFontName == TEXT("Regular") && PropertiesLabel->GetFont().TypefaceFontName == TEXT("Regular")
        && FMath::IsNearlyEqual(OverviewUnderline->GetCachedGeometry().GetLocalSize().Y, 3.0f)
        && FMath::IsNearlyEqual(PropertiesUnderline->GetCachedGeometry().GetLocalSize().Y, 3.0f));

    const int64 StableRevision = View->GetRevision();
    const auto ExpectRejected = [this, &View, &Tabs, &Region, &Overview, &Properties, &System, &HeaderX, &Active, &ActivationCalls, StableRevision](const TCHAR* InName, const FString& InStyles)
    {
        const FCkUiLoadResult Rejected = View->TryReload(Markup(), InStyles, InName);
        TestFalse(TEXT("Malformed underline-tabs CSS rejects before publication"), Rejected.Succeeded);
        TestTrue(TEXT("Malformed underline-tabs CSS rejection is atomic"), View->GetRevision() == StableRevision && View->GetTabs(TEXT("tabs")) == Tabs
            && FindHeader(Region, TEXT("Overview")) == Overview && FindHeader(Region, TEXT("Properties")) == Properties
            && FindHeader(Region, TEXT("System")) == System && Active == TEXT("properties") && ActivationCalls == 1
            && FMath::IsNearlyEqual(Overview->GetCachedGeometry().GetLocalSize().X, 90.0f, 1.0f)
            && FMath::IsNearlyEqual(HeaderX(Overview.ToSharedRef()), 90.0, 2.0));
    };
    ExpectRejected(TEXT("UiTabsVisualStyleInvalidColor"), TEXT(".tab-strip { -ck-tabs-active-color: not-a-color; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleNegativeUnderline"), TEXT(".tab-strip { -ck-tabs-underline-height: -1px; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleZeroFont"), TEXT(".tab-strip { -ck-tabs-font-size: 0px; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleInvalidWeight"), TEXT(".tab-strip { -ck-tabs-font-weight: heavy; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleZeroWidth"), TEXT(".tab-strip { -ck-tabs-header-width: 0px; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleNegativeWidth"), TEXT(".tab-strip { -ck-tabs-header-width: -1px; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleOversizeWidth"), TEXT(".tab-strip { -ck-tabs-header-width: 4097px; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleNonFiniteWidth"), TEXT(".tab-strip { -ck-tabs-header-width: 1e999px; }"));
    ExpectRejected(TEXT("UiTabsVisualStyleMissingToken"), TEXT(".tab-strip { -ck-tabs-header-width: var(--missing); }"));
    ExpectRejected(TEXT("UiTabsVisualStyleInvalidAlignment"), TEXT(".tab-strip { -ck-tabs-header-align: middle; }"));
    const auto WrongNode = View->TryReload(Markup().Replace(TEXT("id=\"overview\""),
        TEXT("id=\"overview\" class=\"tab-entry\"")),
        TEXT(".tab-strip { -ck-tabs-active-color: #fac866; } .tab-entry { -ck-tabs-header-width: 80px; }"),
        TEXT("UiTabsVisualStyleWrongNode"));
    TestFalse(TEXT("Header rail CSS on a tab panel rejects"), WrongNode.Succeeded);
    TestTrue(TEXT("Wrong-node CSS preserves accepted native rail and selection"),
        View->GetRevision() == StableRevision && View->GetTabs(TEXT("tabs")) == Tabs
        && FindHeader(Region, TEXT("Overview")) == Overview && Active == TEXT("properties")
        && ActivationCalls == 1);
    const int64 BeforeDefault = View->GetRevision();
    if (!TestTrue(TEXT("Omitted rail properties restore natural left-aligned headers"),
        View->TryReload(Markup(), TEXT(".tab-strip { -ck-tabs-active-color: #fac866; }"), TEXT("UiTabsVisualStyleDefault")).Succeeded)) { return false; }
    Tick(Slate);
    TestTrue(TEXT("Default header widths are natural and begin at the left without replacing native headers"),
        View->GetRevision() == BeforeDefault + 1 && View->GetTabs(TEXT("tabs")) == Tabs
        && FindHeader(Region, TEXT("Overview")) == Overview && FindHeader(Region, TEXT("Properties")) == Properties
        && FindHeader(Region, TEXT("System")) == System && Active == TEXT("properties") && ActivationCalls == 1
        && FMath::IsNearlyEqual(HeaderX(Overview.ToSharedRef()), 0.0, 2.0)
        && Overview->GetCachedGeometry().GetLocalSize().X != System->GetCachedGeometry().GetLocalSize().X);
    Scope.Window->Resize(FVector2D{140.0f, 240.0f});
    for (int32 Frame = 0; Frame < 4; ++Frame) { Tick(Slate); }
    TestTrue(TEXT("Natural-width headers still wrap within a narrow viewport"),
        Overview->GetCachedGeometry().GetLocalSize().X <= Tabs->GetCachedGeometry().GetLocalSize().X + 1.0f
        && Properties->GetCachedGeometry().GetLocalSize().X <= Tabs->GetCachedGeometry().GetLocalSize().X + 1.0f
        && System->GetCachedGeometry().GetLocalSize().X <= Tabs->GetCachedGeometry().GetLocalSize().X + 1.0f
        && System->GetCachedGeometry().GetAbsolutePosition().Y > Overview->GetCachedGeometry().GetAbsolutePosition().Y);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
