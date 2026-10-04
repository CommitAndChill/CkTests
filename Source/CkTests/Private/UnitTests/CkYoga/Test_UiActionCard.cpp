#include "CkSlateLayout/CkUiActionCard.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/CkFlexLayoutTypes.h"
#include "CkSlateLayout/SCkUiRepeat.h"
#include "CkSlateLayout/SCkUiSurface.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_action_card
{
    auto FindButton(const TSharedRef<SWidget>& InRoot, const FName InTag) -> TSharedPtr<SButton>
    {
        if (InRoot->GetTag() == InTag && InRoot->GetTypeAsString() == TEXT("SActionCard"))
        { return StaticCastSharedRef<SButton>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SButton> Found = FindButton(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag))
            { return Found; }
        }
        return {};
    }

    auto Tick(FSlateApplication& InSlate) -> void
    {
        InSlate.PumpMessages();
        InSlate.Tick();
        InSlate.Tick();
    }

    auto PointerEvent(FSlateApplication& InSlate, const TSharedRef<SWidget>& InWidget,
        const bool IsDown, const uint32 InPointerIndex = FSlateApplication::CursorPointerIndex,
        const FKey InButton = EKeys::LeftMouseButton, const int32 InUserIndex = INDEX_NONE) -> FPointerEvent
    {
        const FGeometry Geometry = InWidget->GetCachedGeometry();
        const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
        InSlate.SetCursorPos(Position);
        const TSet<FKey> Buttons = IsDown ? TSet<FKey>{InButton} : TSet<FKey>{};
        const int32 UserIndex = InUserIndex == INDEX_NONE ? InSlate.GetCursorUser()->GetUserIndex() : InUserIndex;
        return FPointerEvent(UserIndex, InPointerIndex,
            Position, Position, Buttons, InButton, 0.0f, FModifierKeysState{});
    }

    auto Press(FSlateApplication& InSlate, const TSharedRef<SWindow>& InWindow,
        const TSharedRef<SWidget>& InWidget, const int32 InUserIndex = INDEX_NONE) -> bool
    {
        if (!InWindow->GetNativeWindow().IsValid() || InWidget->GetCachedGeometry().GetLocalSize().IsNearlyZero())
        { return false; }
        const FPointerEvent Down = PointerEvent(InSlate, InWidget, true,
            FSlateApplication::CursorPointerIndex, EKeys::LeftMouseButton, InUserIndex);
        const TSet<FKey> Buttons;
        const FPointerEvent Move(Down.GetUserIndex(), Down.GetPointerIndex(), Down.GetScreenSpacePosition(),
            Down.GetScreenSpacePosition(), Buttons, EKeys::Invalid, 0.0f, FModifierKeysState{});
        InSlate.ProcessMouseMoveEvent(Move, true);
        return InSlate.ProcessMouseButtonDownEvent(InWindow->GetNativeWindow(), Down);
    }

    auto Release(FSlateApplication& InSlate, const TSharedRef<SWidget>& InWidget,
        const int32 InUserIndex = INDEX_NONE) -> bool
    {
        const bool Handled = InSlate.ProcessMouseButtonUpEvent(PointerEvent(InSlate, InWidget, false,
            FSlateApplication::CursorPointerIndex, EKeys::LeftMouseButton, InUserIndex));
        Tick(InSlate);
        return Handled;
    }

    auto Click(FSlateApplication& InSlate, const TSharedRef<SWindow>& InWindow,
        const TSharedRef<SWidget>& InWidget) -> bool
    {
        return Press(InSlate, InWindow, InWidget) && Release(InSlate, InWidget);
    }

    struct FWindowScope
    {
        FSlateApplication& Slate;
        TSharedPtr<SWindow> Window;
        TSharedPtr<SWidget> PreviousFocus;
        ~FWindowScope()
        {
            if (Window.IsValid()) { Slate.DestroyWindowImmediately(Window.ToSharedRef()); }
            if (PreviousFocus.IsValid()) { Slate.SetKeyboardFocus(PreviousFocus, EFocusCause::SetDirectly); }
            else { Slate.ClearKeyboardFocus(EFocusCause::SetDirectly); }
        }
    };

    auto Markup() -> FString
    {
        return TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
            "<action-card id=\"card\" action=\"open\" selected-bind=\"selected\" enabled-bind=\"enabled\" class=\"card-style\">"
            "<slot name=\"content\"><column id=\"body\"><text id=\"caption\">Open resource</text></column></slot>"
            "</action-card></column></region></ui>");
    }

    auto Styles(const TCHAR* InBorder = TEXT("#aa5500")) -> FString
    {
        return FString::Printf(TEXT(".card-style { -ck-action-card-background: #123456; "
            "-ck-action-card-selected-background: #654321; -ck-action-card-border-color: #112233; "
            "-ck-action-card-selected-border-color: %s; -ck-action-card-focus-border-color: #ffcc44; "
            "-ck-action-card-padding: 10px; }"), InBorder);
    }

    auto Color(const uint8 R, const uint8 G, const uint8 B) -> FLinearColor
    { return FLinearColor(FColor(R, G, B)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiActionCardNative,
    "Ck.UiAuthoring.ActionCard.NativeRetentionAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiActionCardNative::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_action_card;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Action card test requires Slate.")); return false; }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("Action card registers"), FCkUiActionCard::Register(Registry).Succeeded)) { return false; }
    const TSharedRef<const FCkUiWidgetRegistrySnapshot> Snapshot = Registry.CreateSnapshot();
    const FCkUiCustomWidgetRegistration* Registration = Snapshot->Find(TEXT("action-card"));
    if (!TestTrue(TEXT("Retained factory is registered"), Registration != nullptr && !!Registration->RetainedFactory)) { return false; }
    FCkUiCustomWidgetArguments Invalid;
    FString Failure;
    TestFalse(TEXT("Empty required arguments reject"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Factory reports exact required contract"), Failure,
        FString(TEXT("Action card requires an id, action binding, and content slot.")));
    Invalid.Id = TEXT("wrong-action");
    Invalid.Actions.Add(TEXT("other"), FSimpleDelegate::CreateLambda([]() {}));
    Invalid.BindingNames.Add(TEXT("other"), TEXT("other"));
    Invalid.Slots.Add(TEXT("content"), SNew(SButton));
    Failure.Reset();
    TestFalse(TEXT("Nonmatching required action rejects"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Nonmatching action reports contract failure"), Failure,
        FString(TEXT("Action card requires an id, action binding, and content slot.")));
    FCkUiCustomWidgetArguments Styled;
    Styled.Id = TEXT("styled");
    Styled.Actions.Add(TEXT("action"), FSimpleDelegate::CreateLambda([]() {}));
    Styled.BindingNames.Add(TEXT("action"), TEXT("open"));
    Styled.Slots.Add(TEXT("content"), SNew(SButton));
    FCkUiCustomStyleValue InvalidColor;
    InvalidColor.Kind = ECkUiCustomStyleKind::Length;
    Styled.Style.CustomProperties.Add(TEXT("-ck-action-card-background"), InvalidColor);
    Failure.Reset();
    TestFalse(TEXT("Direct factory rejects wrong color kind"), Registration->RetainedFactory(Styled, Failure).IsValid());
    TestEqual(TEXT("Wrong color kind reports exact failure"), Failure,
        FString(TEXT("Action card -ck-action-card-background requires a finite color.")));
    InvalidColor.Kind = ECkUiCustomStyleKind::Color;
    InvalidColor.Color = FLinearColor(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f);
    Styled.Style.CustomProperties.Add(TEXT("-ck-action-card-background"), InvalidColor);
    Failure.Reset();
    TestFalse(TEXT("Direct factory rejects nonfinite color"), Registration->RetainedFactory(Styled, Failure).IsValid());
    TestEqual(TEXT("Nonfinite color reports exact failure"), Failure,
        FString(TEXT("Action card -ck-action-card-background requires a finite color.")));
    Styled.Style.CustomProperties.Remove(TEXT("-ck-action-card-background"));
    FCkUiCustomStyleValue InvalidLength;
    InvalidLength.Kind = ECkUiCustomStyleKind::Color;
    Styled.Style.CustomProperties.Add(TEXT("-ck-action-card-padding"), InvalidLength);
    Failure.Reset();
    TestFalse(TEXT("Direct factory rejects wrong length kind"), Registration->RetainedFactory(Styled, Failure).IsValid());
    TestEqual(TEXT("Wrong length kind reports exact failure"), Failure,
        FString(TEXT("Action card -ck-action-card-padding requires a finite length between 0 and 512.")));

    bool Selected = false;
    bool Enabled = true;
    int32 Calls = 0;
    int32 OtherCalls = 0;
    FCkUiView::FDataBindings Data;
    Data.Visibility.Add(TEXT("selected"), TAttribute<bool>::CreateLambda([&Selected]() { return Selected; }));
    Data.Visibility.Add(TEXT("enabled"), TAttribute<bool>::CreateLambda([&Enabled]() { return Enabled; }));
    FCkUiView::FActions Actions;
    Actions.Add(TEXT("open"), FSimpleDelegate::CreateLambda([&Calls]() { ++Calls; }));
    Actions.Add(TEXT("other"), FSimpleDelegate::CreateLambda([&OtherCalls]() { ++OtherCalls; }));
    TSharedPtr<FCkUiView> View = FCkUiView::Create({}, MoveTemp(Actions), {},
        FCoreStyle::GetDefaultFontStyle("Regular", 12), MoveTemp(Data), Registry.CreateSnapshot());
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    const FCkUiLoadResult Initial = View->TryReload(Markup(), Styles(), TEXT("ActionCardInitial"));
    if (!TestTrue(TEXT("Action card document loads"), Initial.Succeeded))
    { AddError(FString::Join(Initial.Errors, TEXT("\n"))); return false; }
    FSlateApplication& Slate = FSlateApplication::Get();
    FWindowScope Scope{Slate, {}, Slate.GetUserFocusedWidget(0)};
    Scope.Window = SNew(SWindow).ClientSize(FVector2D{320.0f, 240.0f}).CreateTitleBar(false)[Region];
    Slate.AddWindow(Scope.Window.ToSharedRef(), true);
    Tick(Slate);
    const TSharedPtr<SButton> Card = FindButton(Region, TEXT("card"));
    if (!TestTrue(TEXT("Whole native card mounts with authored id"), Card.IsValid())) { return false; }
    TestTrue(TEXT("Card itself accepts keyboard focus"), Slate.SetKeyboardFocus(Card.ToSharedRef(), EFocusCause::SetDirectly));
    TestTrue(TEXT("Focused card paints its configured border"), Card->GetBorderImage() != nullptr
        && Card->GetBorderImage()->OutlineSettings.Color.GetSpecifiedColor().Equals(Color(0xff, 0xcc, 0x44)));
    const FKeyEvent Enter{EKeys::Enter, FModifierKeysState{}, 0, false, 0, 0};
    TestTrue(TEXT("Enter reaches focused native card"), Slate.ProcessKeyDownEvent(Enter));
    Slate.ProcessKeyUpEvent(Enter);
    const FKeyEvent Space{EKeys::SpaceBar, FModifierKeysState{}, 0, false, 0, 0};
    TestTrue(TEXT("Space reaches focused native card"), Slate.ProcessKeyDownEvent(Space));
    Slate.ProcessKeyUpEvent(Space);
    TestEqual(TEXT("Enter and Space each dispatch once"), Calls, 2);
    TestTrue(TEXT("Pointer click reaches whole card"), Click(Slate, Scope.Window.ToSharedRef(), Card.ToSharedRef()));
    TestEqual(TEXT("Pointer action dispatches once"), Calls, 3);
    Selected = true;
    Slate.ClearKeyboardFocus(EFocusCause::SetDirectly);
    Tick(Slate);
    TestTrue(TEXT("Live selected binding changes native fill"), Card->GetBorderImage() != nullptr
        && Card->GetBorderImage()->TintColor.GetSpecifiedColor().Equals(Color(0x65, 0x43, 0x21)));
    Enabled = false;
    Tick(Slate);
    Card->SimulateClick();
    TestEqual(TEXT("Disabled card does not dispatch"), Calls, 3);
    Enabled = true;
    Tick(Slate);
    const int64 Revision = View->GetRevision();
    const FCkUiLoadResult Rejected = View->TryReload(Markup(),
        Styles().Replace(TEXT("10px"), TEXT("-1px")), TEXT("ActionCardRejected"));
    TestFalse(TEXT("Invalid candidate rejects"), Rejected.Succeeded);
    TestEqual(TEXT("Rejected candidate preserves revision"), View->GetRevision(), Revision);
    TestTrue(TEXT("Rejected candidate preserves native card"), FindButton(Region, TEXT("card")) == Card);
    if (!TestTrue(TEXT("Compatible style reload commits"), View->TryReload(Markup(), Styles(TEXT("#eeaa22")),
        TEXT("ActionCardUpdated")).Succeeded)) { return false; }
    Tick(Slate);
    TestTrue(TEXT("Compatible reload retains native identity"), FindButton(Region, TEXT("card")) == Card);
    if (!TestTrue(TEXT("Native card is enabled before pointer interaction"), Card->IsEnabled()))
    { return false; }
    Card->SimulateClick();
    TestEqual(TEXT("Retained card dispatches after commit"), Calls, 4);
    const TSharedPtr<FSlateUser> CursorUser = Slate.GetCursorUser();
    if (!TestTrue(TEXT("Pointer press is handled before retained reload"),
        Press(Slate, Scope.Window.ToSharedRef(), Card.ToSharedRef()))) { return false; }
    TestTrue(TEXT("Native card owns the active pointer capture"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card);
    TestTrue(TEXT("Native card is pressed while captured"), Card->IsPressed());
    const uint32 OtherPointer = 7;
    Slate.ProcessMouseButtonDownEvent(Scope.Window->GetNativeWindow(), PointerEvent(Slate, Card.ToSharedRef(), true, OtherPointer));
    TestTrue(TEXT("Second pointer cannot displace the held card press"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card
        && CursorUser->GetPointerCaptor(OtherPointer) != Card);
    Slate.ProcessMouseButtonUpEvent(PointerEvent(Slate, Card.ToSharedRef(), false, OtherPointer));
    Slate.ProcessMouseButtonUpEvent(PointerEvent(Slate, Card.ToSharedRef(), false,
        FSlateApplication::CursorPointerIndex, EKeys::RightMouseButton));
    TestTrue(TEXT("Right release cannot cancel a held left press"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card && Calls == 4);
    const int64 PressRevision = View->GetRevision();
    TestFalse(TEXT("Rejected candidate during press remains rejected"), View->TryReload(Markup(),
        Styles().Replace(TEXT("10px"), TEXT("-2px")), TEXT("ActionCardRejectedPress")).Succeeded);
    TestEqual(TEXT("Rejected candidate preserves held revision"), View->GetRevision(), PressRevision);
    TestTrue(TEXT("Rejected candidate preserves native capture"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card);
    const FCkUiLoadResult Retargeted = View->TryReload(Markup().Replace(TEXT("action=\"open\""),
        TEXT("action=\"other\"")), Styles(), TEXT("ActionCardRetargetPress"));
    TestFalse(TEXT("Active press cannot retarget its action"), Retargeted.Succeeded);
    TestTrue(TEXT("Active retarget reports exact rejection"), FString::Join(Retargeted.Errors, TEXT("\n"))
        .Contains(TEXT("Action card action cannot change during an active press.")));
    TestTrue(TEXT("Retarget rejection preserves capture and action scope"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card && OtherCalls == 0);
    if (!TestTrue(TEXT("Compatible reload retains a held press"), View->TryReload(Markup(),
        Styles(TEXT("#ffbb33")), TEXT("ActionCardHeldReload")).Succeeded)) { return false; }
    TestTrue(TEXT("Capture follows retained card across reload"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card);
    TestTrue(TEXT("Retained card preserves its native pressed state"), Card->IsPressed());
    TestTrue(TEXT("Release after compatible reload reaches native card"), Release(Slate, Card.ToSharedRef()));
    TestEqual(TEXT("Held press dispatches at most once after reload"), Calls, 5);
    const TSharedRef<FSlateVirtualUserHandle> OtherUserHandle = Slate.FindOrCreateVirtualUser(97);
    const int32 OtherUserIndex = OtherUserHandle->GetUserIndex();
    Slate.SetUserFocus(OtherUserIndex, Card, EFocusCause::SetDirectly);
    if (!TestTrue(TEXT("Another Slate user focuses the card"),
        Slate.GetUserFocusedWidget(OtherUserIndex) == Card)) { return false; }
    if (!TestTrue(TEXT("Another Slate user starts a held press"),
        Press(Slate, Scope.Window.ToSharedRef(), Card.ToSharedRef(), OtherUserIndex))) { return false; }
    const TSharedPtr<FSlateUser> OtherUser = Slate.GetUser(OtherUserIndex);
    if (!TestTrue(TEXT("Other user's card capture and native press are active"),
        OtherUser.IsValid() && OtherUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card
        && Card->IsPressed())) { return false; }
    Slate.SetUserFocus(0, Card, EFocusCause::SetDirectly);
    Slate.ClearUserFocus(0, EFocusCause::SetDirectly);
    TestTrue(TEXT("Foreign user's focus loss preserves captured press"),
        OtherUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card && Card->IsPressed());
    if (!TestTrue(TEXT("Multi-user held card survives compatible reload"), View->TryReload(Markup(),
        Styles(TEXT("#ffbb44")), TEXT("ActionCardOtherUserReload")).Succeeded)) { return false; }
    TestTrue(TEXT("Captured user's focus and press survive ancestry repair"),
        Slate.GetUserFocusedWidget(OtherUserIndex) == Card
        && OtherUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card && Card->IsPressed());
    Slate.ClearUserFocus(OtherUserIndex, EFocusCause::SetDirectly);
    TestTrue(TEXT("Captured user's real focus loss cancels only its press"),
        OtherUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) != Card && !Card->IsPressed());
    Release(Slate, Card.ToSharedRef(), OtherUserIndex);
    TestEqual(TEXT("Canceled other-user press emits no action"), Calls, 5);
    if (!TestTrue(TEXT("Second native press is handled"), Press(Slate, Scope.Window.ToSharedRef(), Card.ToSharedRef())))
    { return false; }
    TestTrue(TEXT("Second press owns capture"), CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card);
    View->ReleaseOwnerInteractions();
    TestFalse(TEXT("Owner release drops card pointer capture"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == Card);
    Release(Slate, Card.ToSharedRef());
    Card->SimulateClick();
    TestEqual(TEXT("Owner release cancels held press and gates card"), Calls, 5);
    Tick(Slate);
    TestFalse(TEXT("Released native card is disabled"), Card->IsEnabled());
    View.Reset();
    Card->SimulateClick();
    TestEqual(TEXT("Held native card remains inert after view destruction"), Calls, 5);
    TestTrue(TEXT("Held native card keeps a valid owned style"), Card->GetBorderImage() != nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiActionCardRepeat,
    "Ck.UiAuthoring.ActionCard.RepeatItemScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiActionCardRepeat::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_action_card;
    TSharedPtr<FCkUiCollection> Items;
    if (!TestTrue(TEXT("Repeat collection creates"), FCkUiCollection::TryCreate(
        {{TEXT("title"), ECkUiFieldKind::Text}}, Items).Succeeded)) { return false; }
    const auto Record = [](const TCHAR* InKey, const TCHAR* InTitle)
    {
        FCkUiRecordData Result;
        Result.Key = InKey;
        Result.Fields.Add(TEXT("title"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = FText::FromString(InTitle)});
        return Result;
    };
    if (!TestTrue(TEXT("Repeat records publish"), Items->TrySetRecords({Record(TEXT("a"),
        TEXT("Alpha resource includes a long description that wraps inside the action card at narrow widths")),
        Record(TEXT("b"), TEXT("Bravo"))}).Succeeded)) { return false; }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("Repeat action card registers"), FCkUiActionCard::Register(Registry).Succeeded)) { return false; }
    TArray<FString> Keys;
    FCkUiView::FDataBindings Data;
    Data.Collections.Add(TEXT("items"), Items);
    Data.ItemActions.Add(TEXT("choose"), FCkUiOnItemAction::CreateLambda([&Keys](FString InKey) { Keys.Add(InKey); }));
    TSharedPtr<FCkUiView> View = FCkUiView::Create({}, {}, {}, {}, MoveTemp(Data), Registry.CreateSnapshot());
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    const FString Markup = TEXT("<ui version=\"1\"><region name=\"main\"><repeat id=\"rows\" bind=\"items\">"
        "<action-card id=\"choice\" item-action=\"choose\"><slot name=\"content\"><text id=\"label\" class=\"label\" bind-field=\"title\"/></slot></action-card>"
        "</repeat></region></ui>");
    const FCkUiLoadResult Loaded = View->TryReload(Markup,
        TEXT(".label { text-wrap: wrap; overflow-wrap: anywhere; }"), TEXT("ActionCardRepeat"));
    if (!TestTrue(TEXT("Repeat card document loads"), Loaded.Succeeded))
    { AddError(FString::Join(Loaded.Errors, TEXT("\n"))); return false; }
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Repeat card capture test requires Slate.")); return false; }
    FSlateApplication& Slate = FSlateApplication::Get();
    FWindowScope Scope{Slate, {}, Slate.GetUserFocusedWidget(0)};
    Scope.Window = SNew(SWindow).ClientSize(FVector2D{320.0f, 400.0f}).CreateTitleBar(false)[Region];
    Slate.AddWindow(Scope.Window.ToSharedRef(), true);
    Tick(Slate);
    const TSharedPtr<SCkUiRepeat> Repeat = View->GetRepeat(TEXT("rows"));
    if (!TestTrue(TEXT("Repeat presenter mounts"), Repeat.IsValid())) { return false; }
    const TSharedPtr<SWidget> A = Repeat->GetItemWidget(TEXT("a"));
    const TSharedPtr<SWidget> B = Repeat->GetItemWidget(TEXT("b"));
    if (!TestTrue(TEXT("Both repeat rows mount"), A.IsValid() && B.IsValid())) { return false; }
    const TSharedPtr<SButton> ACard = FindButton(A.ToSharedRef(), TEXT("choice"));
    const TSharedPtr<SButton> BCard = FindButton(B.ToSharedRef(), TEXT("choice"));
    if (!TestTrue(TEXT("Repeat native cards mount"), ACard.IsValid() && BCard.IsValid())) { return false; }
    const TSharedPtr<FCkFlexMeasureMetaData> Measure = ACard->GetMetaData<FCkFlexMeasureMetaData>();
    if (!TestTrue(TEXT("Repeat card exposes slot measurement"), Measure.IsValid())) { return false; }
    const FVector2D Narrow = Measure->Measure({140.0f, YGMeasureModeExactly, YGUndefined, YGMeasureModeUndefined, 1.0f});
    const FVector2D Wide = Measure->Measure({600.0f, YGMeasureModeExactly, YGUndefined, YGMeasureModeUndefined, 1.0f});
    TestTrue(TEXT("Opaque slot wraps within native card padding"), Narrow.X == 140.0f && Wide.X == 600.0f
        && Narrow.Y > Wide.Y);
    ACard->SimulateClick();
    BCard->SimulateClick();
    TestTrue(TEXT("Item actions keep the correct record scope"), Keys == TArray<FString>{TEXT("a"), TEXT("b")});
    if (!TestTrue(TEXT("Repeat item native press is handled"),
        Press(Slate, Scope.Window.ToSharedRef(), ACard.ToSharedRef()))) { return false; }
    const TSharedPtr<FSlateUser> CursorUser = Slate.GetCursorUser();
    TestTrue(TEXT("Repeat item card owns pointer capture"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == ACard);
    if (!TestTrue(TEXT("Removing key publishes"), Items->TrySetRecords({Record(TEXT("b"), TEXT("Bravo"))}).Succeeded)
        || !TestTrue(TEXT("Removing key refreshes"), Repeat->TryRefresh())) { return false; }
    TestFalse(TEXT("Removed repeat card releases its pointer capture"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == ACard);
    Release(Slate, ACard.ToSharedRef());
    ACard->SimulateClick();
    TestEqual(TEXT("Held removed card cannot invoke item action"), Keys.Num(), 2);
    if (!TestTrue(TEXT("Reinserting key publishes"), Items->TrySetRecords({Record(TEXT("a"), TEXT("Alpha again")),
        Record(TEXT("b"), TEXT("Bravo"))}).Succeeded)
        || !TestTrue(TEXT("Reinserting key refreshes"), Repeat->TryRefresh())) { return false; }
    const TSharedPtr<SWidget> NewA = Repeat->GetItemWidget(TEXT("a"));
    const TSharedPtr<SButton> NewCard = NewA.IsValid() ? FindButton(NewA.ToSharedRef(), TEXT("choice")) : nullptr;
    if (!TestTrue(TEXT("Reinserted key gets a new native card"), NewCard.IsValid() && NewCard != ACard)) { return false; }
    ACard->SimulateClick();
    NewCard->SimulateClick();
    TestTrue(TEXT("Old card stays inert while new key action works"), Keys == TArray<FString>{TEXT("a"), TEXT("b"), TEXT("a")});
    Tick(Slate);
    if (!TestTrue(TEXT("Reinserted card accepts a real held press"),
        Press(Slate, Scope.Window.ToSharedRef(), NewCard.ToSharedRef()))) { return false; }
    TestTrue(TEXT("Reinserted card owns cursor capture"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == NewCard);
    View->ReleaseOwnerInteractions();
    TestFalse(TEXT("Parent owner release clears child card capture"),
        CursorUser->GetPointerCaptor(FSlateApplication::CursorPointerIndex) == NewCard);
    Release(Slate, NewCard.ToSharedRef());
    NewCard->SimulateClick();
    Tick(Slate);
    TestTrue(TEXT("Parent release leaves child card disabled and inert"),
        !NewCard->IsEnabled() && Keys == TArray<FString>{TEXT("a"), TEXT("b"), TEXT("a")});
    return true;
}

#endif
