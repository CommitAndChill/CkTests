#include "CkSlateLayout/CkUiDocument.h"
#include "CkSlateLayout/SCkUiStyledButton.h"
#include "CkSlateLayout/SCkUiSurface.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_button_focus_targeting
{
    auto Markup() -> FString
    {
        return TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
            "<button id=\"resume\" action=\"resume\" enabled-bind=\"resume-enabled\" class=\"action\">Resume</button>"
            "</column></region></ui>");
    }

    auto Styles(const TCHAR* InBackground = TEXT("#123456")) -> FString
    {
        return FString::Printf(TEXT(".action { color: #eeeeee; -ck-button-background: %s; "
            "-ck-button-focus-background: #345678; -ck-button-focus-border-color: #abcdef; "
            "-ck-button-focus-color: #fedcba; -ck-button-focus-outline-color: #ffcc44; "
            "-ck-button-focus-outline-width: 2px; -ck-button-focus-outline-gap: 2px; "
            "-ck-button-pressed-color: #010203; -ck-button-disabled-color: #405060; "
            "-ck-button-text-align: center; -ck-button-content-vertical-align: center; }"), InBackground);
    }

    auto Color(const uint8 InRed, const uint8 InGreen, const uint8 InBlue) -> FLinearColor
    { return FLinearColor(FColor(InRed, InGreen, InBlue)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiButtonFocusTargeting,
    "Ck.UiAuthoring.Button.FocusAndTargeting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiButtonFocusTargeting::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_button_focus_targeting;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Button focus test requires Slate.")); return false; }

    FCkUiDocument ParsedDocument;
    const FCkUiLoadResult Parsed = FCkUiDocumentParser::TryParse(Markup(), Styles(), {}, ParsedDocument,
        TEXT("ButtonFocusParsed"));
    const FCkUiNode* Root = ParsedDocument.Regions.Find(TEXT("main"));
    const FCkUiNode* Definition = Root != nullptr && Root->Kind == ECkUiNodeKind::Column
        && Root->Children.Num() == 1 ? &Root->Children[0] : nullptr;
    if (!TestTrue(TEXT("Focus and pressed CSS parse as explicit button properties"), Parsed.Succeeded
        && Definition != nullptr && Definition->Kind == ECkUiNodeKind::Button
        && Definition->ButtonVisualStyle.FocusBackground.Get(FLinearColor::Transparent).Equals(Color(0x34, 0x56, 0x78))
        && Definition->ButtonVisualStyle.FocusBorderColor.Get(FLinearColor::Transparent).Equals(Color(0xab, 0xcd, 0xef))
        && Definition->ButtonVisualStyle.FocusColor.Get(FLinearColor::Transparent).Equals(Color(0xfe, 0xdc, 0xba))
        && Definition->ButtonVisualStyle.FocusOutlineColor.Get(FLinearColor::Transparent).Equals(Color(0xff, 0xcc, 0x44))
        && Definition->ButtonVisualStyle.FocusOutlineWidth.Get(-1.0f) == 2.0f
        && Definition->ButtonVisualStyle.FocusOutlineGap.Get(-1.0f) == 2.0f
        && Definition->ButtonVisualStyle.PressedColor.Get(FLinearColor::Transparent).Equals(Color(1, 2, 3))
        && Definition->ButtonVisualStyle.TextAlignment == ETextJustify::Center
        && Definition->ButtonVisualStyle.ContentVerticalAlignment == VAlign_Center))
    { return false; }

    FCkUiDocument DefaultAlignmentDocument;
    const FString DefaultAlignmentStyles = Styles()
        .Replace(TEXT("-ck-button-text-align: center; "), TEXT(""))
        .Replace(TEXT("-ck-button-content-vertical-align: center; "), TEXT(""));
    const FCkUiLoadResult DefaultAlignment = FCkUiDocumentParser::TryParse(Markup(), DefaultAlignmentStyles,
        {}, DefaultAlignmentDocument, TEXT("ButtonDefaultAlignment"));
    const FCkUiNode* DefaultRoot = DefaultAlignmentDocument.Regions.Find(TEXT("main"));
    const FCkUiNode* DefaultButton = DefaultRoot != nullptr && DefaultRoot->Kind == ECkUiNodeKind::Column
        && DefaultRoot->Children.Num() == 1 ? &DefaultRoot->Children[0] : nullptr;
    if (!TestTrue(TEXT("Omitted button alignment preserves left and fill defaults"), DefaultAlignment.Succeeded
        && DefaultButton != nullptr && DefaultButton->Kind == ECkUiNodeKind::Button
        && DefaultButton->ButtonVisualStyle.TextAlignment == ETextJustify::Left
        && DefaultButton->ButtonVisualStyle.ContentVerticalAlignment == VAlign_Fill))
    { return false; }

    int32 Calls = 0;
    bool bResumeEnabled = true;
    auto Data = FCkUiView::FDataBindings{};
    Data.RestoreButtonFocusOnReload = true;
    Data.Visibility.Add(TEXT("resume-enabled"), TAttribute<bool>::CreateLambda([&bResumeEnabled]() { return bResumeEnabled; }));
    auto Actions = FCkUiView::FActions{};
    Actions.Add(TEXT("resume"), FSimpleDelegate::CreateLambda([&Calls]() { ++Calls; }));
    const TSharedRef<FCkUiView> View = FCkUiView::Create({}, MoveTemp(Actions), {}, FSlateFontInfo(), MoveTemp(Data));
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    TestFalse(TEXT("A mount alone is not an accepted region"), View->HasRegion(TEXT("main")));
    if (!TestTrue(TEXT("Button document mounts"), View->TryReload(Markup(), Styles(), TEXT("ButtonFocusInitial")).Succeeded))
    { return false; }
    TestTrue(TEXT("Accepted document exposes its region"), View->HasRegion(TEXT("main")));
    const TSharedPtr<SButton> Initial = View->GetButton(TEXT("resume"));
    if (!TestTrue(TEXT("Committed button is publicly targetable by stable ID"), Initial.IsValid())
        || !TestFalse(TEXT("Unknown button does not resolve"), View->GetButton(TEXT("unknown")).IsValid()))
    { return false; }

    FSlateApplication& Slate = FSlateApplication::Get();
    const TSharedPtr<SWidget> PreviousFocus = Slate.GetUserFocusedWidget(0);
    const TSharedRef<SWindow> Window = SNew(SWindow).AutoCenter(EAutoCenter::None)
        .ClientSize(FVector2D{320.0f, 160.0f}).CreateTitleBar(false).HasCloseButton(false)[Region];
    Slate.AddWindow(Window, true);
    const auto Close = [&Slate, &Window, &PreviousFocus]()
    {
        Slate.DestroyWindowImmediately(Window);
        if (PreviousFocus.IsValid()) { Slate.SetKeyboardFocus(PreviousFocus, EFocusCause::SetDirectly); }
        else { Slate.ClearKeyboardFocus(EFocusCause::SetDirectly); }
    };
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    if (!TestTrue(TEXT("Authored button accepts keyboard focus"),
        Slate.SetKeyboardFocus(Initial.ToSharedRef(), EFocusCause::SetDirectly)))
    { Close(); return false; }
    TestTrue(TEXT("Focus uses authored fill"), Initial->GetBorderImage() != nullptr
        && Initial->GetBorderImage()->TintColor.GetSpecifiedColor().Equals(Color(0x34, 0x56, 0x78)));

    const int64 AcceptedRevision = View->GetRevision();
    const FCkUiLoadResult Invalid = View->TryReload(Markup(),
        Styles().Replace(TEXT("-ck-button-focus-color: #fedcba"), TEXT("-ck-button-focus-color: bad-color")),
        TEXT("ButtonFocusInvalid"));
    TestFalse(TEXT("Invalid candidate is rejected"), Invalid.Succeeded);
    TestEqual(TEXT("Rejected candidate preserves revision"), View->GetRevision(), AcceptedRevision);
    TestTrue(TEXT("Rejected candidate preserves targeted control"), View->GetButton(TEXT("resume")) == Initial);
    TestTrue(TEXT("Rejected candidate preserves focus"), Slate.GetUserFocusedWidget(0) == Initial);
    const FCkUiLoadResult InvalidGap = View->TryReload(Markup(),
        Styles().Replace(TEXT("-ck-button-focus-outline-gap: 2px"), TEXT("-ck-button-focus-outline-gap: -2px")),
        TEXT("ButtonFocusInvalidGap"));
    TestFalse(TEXT("Invalid focus outline gap rejects"), InvalidGap.Succeeded);
    TestTrue(TEXT("Invalid gap preserves accepted target and revision"),
        View->GetRevision() == AcceptedRevision && View->GetButton(TEXT("resume")) == Initial);
    const FCkUiLoadResult InvalidTextAlignment = View->TryReload(Markup(),
        Styles().Replace(TEXT("-ck-button-text-align: center"), TEXT("-ck-button-text-align: justify")),
        TEXT("ButtonInvalidTextAlignment"));
    TestFalse(TEXT("Invalid button text alignment rejects"), InvalidTextAlignment.Succeeded);
    TestTrue(TEXT("Invalid text alignment preserves accepted target, revision and focus"),
        View->GetRevision() == AcceptedRevision && View->GetButton(TEXT("resume")) == Initial
        && Slate.GetUserFocusedWidget(0) == Initial);
    const FCkUiLoadResult InvalidVerticalAlignment = View->TryReload(Markup(),
        Styles().Replace(TEXT("-ck-button-content-vertical-align: center"),
            TEXT("-ck-button-content-vertical-align: baseline")), TEXT("ButtonInvalidVerticalAlignment"));
    TestFalse(TEXT("Invalid button vertical alignment rejects"), InvalidVerticalAlignment.Succeeded);
    TestTrue(TEXT("Invalid vertical alignment preserves accepted target, revision and focus"),
        View->GetRevision() == AcceptedRevision && View->GetButton(TEXT("resume")) == Initial
        && Slate.GetUserFocusedWidget(0) == Initial);
    Initial->SimulateClick();
    TestEqual(TEXT("Accepted button remains interactive after rejection"), Calls, 1);

    if (!TestTrue(TEXT("Compatible reload commits"), View->TryReload(Markup(), Styles(TEXT("#234567")),
        TEXT("ButtonFocusAccepted")).Succeeded)) { Close(); return false; }
    const TSharedPtr<SButton> Replacement = View->GetButton(TEXT("resume"));
    TestTrue(TEXT("Committed target updates to the replacement"), Replacement.IsValid() && Replacement != Initial);
    TestTrue(TEXT("Logical button focus follows stable ID on accepted reload"), Slate.GetUserFocusedWidget(0) == Replacement);
    Initial->SimulateClick();
    TestEqual(TEXT("Retired button cannot dispatch"), Calls, 1);
    if (Replacement.IsValid()) { Replacement->SimulateClick(); }
    TestEqual(TEXT("Replacement button dispatches once"), Calls, 2);

    bResumeEnabled = false;
    Slate.Tick();
    TestTrue(TEXT("Logical enabled binding disables the committed button"),
        Replacement.IsValid() && !Replacement->IsEnabled());
    if (Replacement.IsValid()) { Replacement->SimulateClick(); }
    TestEqual(TEXT("Disabled committed button cannot dispatch"), Calls, 2);
    if (!TestTrue(TEXT("Reload with disabled action commits"), View->TryReload(Markup(), Styles(TEXT("#345678")),
        TEXT("ButtonFocusDisabled")).Succeeded)) { Close(); return false; }
    const TSharedPtr<SButton> DisabledReplacement = View->GetButton(TEXT("resume"));
    TestTrue(TEXT("Disabled replacement cannot receive restored focus"), DisabledReplacement.IsValid()
        && !DisabledReplacement->IsEnabled() && Slate.GetUserFocusedWidget(0) != DisabledReplacement);

    View->ReleaseOwnerInteractions();
    TestTrue(TEXT("Owner release is visible to a host adapter"), View->IsOwnerReleased());
    if (DisabledReplacement.IsValid()) { DisabledReplacement->SimulateClick(); }
    TestEqual(TEXT("Released button cannot dispatch"), Calls, 2);
    Close();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
