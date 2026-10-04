#include "CkSlateLayout/CkUiRadialSelector.h"
#include "CkSlateLayout/CkUiCollection.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_radial
{
    auto TextField(const TCHAR* InValue) -> FCkUiFieldValue
    {
        FCkUiFieldValue Field;
        Field.Kind = ECkUiFieldKind::Text;
        Field.Text = FText::FromString(InValue);
        return Field;
    }

    auto NumberField(float InValue) -> FCkUiFieldValue
    {
        FCkUiFieldValue Field;
        Field.Kind = ECkUiFieldKind::Number;
        Field.Number = InValue;
        return Field;
    }

    auto BoolField(bool InValue) -> FCkUiFieldValue
    {
        FCkUiFieldValue Field;
        Field.Kind = ECkUiFieldKind::Bool;
        Field.Bool = InValue;
        return Field;
    }

    auto Record(const TCHAR* InKey, float InAngle, bool InEnabled = true, float InCooldown = 0.0f) -> FCkUiRecordData
    {
        FCkUiRecordData Item;
        Item.Key = InKey;
        Item.Fields.Add(TEXT("actor"), TextField(TEXT("Shepard")));
        Item.Fields.Add(TEXT("glyph"), TextField(InKey));
        Item.Fields.Add(TEXT("angle"), NumberField(InAngle));
        Item.Fields.Add(TEXT("cooldown"), NumberField(InCooldown));
        Item.Fields.Add(TEXT("enabled"), BoolField(InEnabled));
        Item.Fields.Add(TEXT("queued"), BoolField(false));
        return Item;
    }

    auto Pointer(FSlateApplication& InSlate, const TSharedRef<SWidget>& InWidget,
        const FVector2D& InLocalPosition, bool InDown) -> FPointerEvent
    {
        const FVector2D Position = InWidget->GetCachedGeometry().LocalToAbsolute(InLocalPosition);
        InSlate.SetCursorPos(Position);
        return FPointerEvent(InSlate.GetCursorUser()->GetUserIndex(), FSlateApplication::CursorPointerIndex,
            Position, Position, InDown ? TSet<FKey>{EKeys::LeftMouseButton} : TSet<FKey>{},
            EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
    }

    auto CountCyan(const TArray<FColor>& InPixels) -> int32
    {
        int32 Count = 0;
        for (const FColor& Pixel : InPixels)
        {
            if (Pixel.B > 80 && static_cast<int32>(Pixel.G) > static_cast<int32>(Pixel.R) + 20
                && static_cast<int32>(Pixel.B) > static_cast<int32>(Pixel.R) + 25)
            { ++Count; }
        }
        return Count;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiRadialSelectorNative,
    "Ck.UiAuthoring.RadialSelector.NativeSelectionAndRetention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiRadialSelectorNative::RunTest(const FString&) -> bool
{
    if (!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("Radial selector requires initialized Slate."));
        return false;
    }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("Radial selector registers"), RegisterRadialUiControls(Registry).Succeeded))
    { return false; }
    const TSharedRef<const FCkUiWidgetRegistrySnapshot> Snapshot = Registry.CreateSnapshot();
    const FCkUiCustomWidgetRegistration* Registration = Snapshot->Find(TEXT("radial-selector"));
    if (!TestTrue(TEXT("Retained factory registered"), Registration != nullptr && !!Registration->RetainedFactory))
    { return false; }
    FString Failure;
    FCkUiCustomWidgetArguments Missing;
    TestFalse(TEXT("Missing items reject"), Registration->RetainedFactory(Missing, Failure).IsValid());
    TestEqual(TEXT("Missing items diagnostic"), Failure,
        FString(TEXT("Radial selector requires an id and items binding.")));

    TSharedPtr<FCkUiCollection> Collection;
    if (!TestTrue(TEXT("Item collection schema creates"), FCkUiCollection::TryCreate({
        {TEXT("actor"), ECkUiFieldKind::Text, true},
        {TEXT("glyph"), ECkUiFieldKind::Text, true},
        {TEXT("angle"), ECkUiFieldKind::Number, true},
        {TEXT("cooldown"), ECkUiFieldKind::Number, true},
        {TEXT("enabled"), ECkUiFieldKind::Bool, true},
        {TEXT("queued"), ECkUiFieldKind::Bool, true},
    }, Collection).Succeeded))
    { return false; }
    if (!TestTrue(TEXT("Three positions publish"), Collection->TrySetRecords({
        ck_tests_ui_radial::Record(TEXT("overload"), -90.0f),
        ck_tests_ui_radial::Record(TEXT("warp"), 30.0f, false),
        ck_tests_ui_radial::Record(TEXT("throw"), 150.0f, true, 0.5f),
    }).Succeeded))
    { return false; }
    FString Selected;
    FString Activated;
    FCkUiCustomWidgetArguments Arguments;
    Arguments.Id = TEXT("power-wheel");
    Arguments.Collections.Add(TEXT("items"), Collection);
    Arguments.CanDispatchEvents = TAttribute<bool>(true);
    Arguments.StringChanged.Add(TEXT("select"), FCkUiOnStringChanged::CreateLambda(
        [&Selected](const FString& InKey) { Selected = InKey; }));
    Arguments.StringChanged.Add(TEXT("activate"), FCkUiOnStringChanged::CreateLambda(
        [&Activated](const FString& InKey) { Activated = InKey; }));
    Failure.Reset();
    TSharedPtr<ICkUiRetainedWidget> Component = Registration->RetainedFactory(Arguments, Failure);
    if (!TestTrue(TEXT("Valid selector constructs"), Component.IsValid()))
    { AddError(Failure); return false; }
    const TSharedRef<SCkUiRadialSelector> Native = StaticCastSharedRef<SCkUiRadialSelector>(Component->GetWidget());
    TestEqual(TEXT("Authored id tags root"), Native->GetTag(), FName(TEXT("power-wheel")));

    FSlateApplication& Slate = FSlateApplication::Get();
    float ParentOpacity = 1.0f;
    const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(460.0f, 460.0f))
        .CreateTitleBar(false)[SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBrush")))
            .ColorAndOpacity_Lambda([&ParentOpacity]()
            { return FLinearColor(1.0f, 1.0f, 1.0f, ParentOpacity); })
            [SNew(SBox).WidthOverride(440.0f).HeightOverride(440.0f)[Native]]];
    Slate.AddWindow(Window, true);
    Slate.PumpMessages();
    Slate.Tick();
    Slate.Tick();
    const FVector2D Size = Native->GetCachedGeometry().GetLocalSize();
    if (!TestTrue(TEXT("Native wheel has real geometry"), Size.X > 300.0f && Size.Y > 300.0f))
    { Slate.DestroyWindowImmediately(Window); return false; }
    TArray<FColor> OpaquePixels;
    FIntVector OpaqueSize = FIntVector::ZeroValue;
    if (!TestTrue(TEXT("Opaque radial widget captures real pixels"),
        Slate.TakeScreenshot(Native, OpaquePixels, OpaqueSize)
            && OpaqueSize.X > 300 && OpaqueSize.Y > 300
            && OpaquePixels.Num() >= OpaqueSize.X * OpaqueSize.Y))
    { Slate.DestroyWindowImmediately(Window); return false; }
    TestTrue(TEXT("Opaque cyan ring is visible"), ck_tests_ui_radial::CountCyan(OpaquePixels) > 40);
    ParentOpacity = 0.0f;
    Slate.Tick();
    Slate.Tick();
    TArray<FColor> HiddenPixels;
    FIntVector HiddenSize = FIntVector::ZeroValue;
    if (!TestTrue(TEXT("Transparent radial widget captures real pixels"),
        Slate.TakeScreenshot(Native, HiddenPixels, HiddenSize)
            && HiddenSize == OpaqueSize && HiddenPixels.Num() >= HiddenSize.X * HiddenSize.Y))
    { Slate.DestroyWindowImmediately(Window); return false; }
    TestEqual(TEXT("Parent opacity zero hides all cyan ring and glyph pixels"),
        ck_tests_ui_radial::CountCyan(HiddenPixels), 0);
    ParentOpacity = 1.0f;
    Slate.Tick();
    Slate.Tick();
    const float Radius = 0.39f * static_cast<float>(FMath::Min(Size.X, Size.Y));
    const FVector2D Top(Size.X * 0.5f, Size.Y * 0.5f - Radius);
    const FVector2D Right(Size.X * 0.5f + Radius * 0.866025f, Size.Y * 0.5f + Radius * 0.5f);
    const FVector2D Left(Size.X * 0.5f - Radius * 0.866025f, Size.Y * 0.5f + Radius * 0.5f);
    const auto Move = [&Slate, &Native](const FVector2D& Local)
    {
        const FPointerEvent Event = ck_tests_ui_radial::Pointer(Slate, Native, Local, false);
        Slate.ProcessMouseMoveEvent(Event, true);
    };
    const auto Click = [&Slate, &Window, &Native](const FVector2D& Local)
    {
        const FPointerEvent Down = ck_tests_ui_radial::Pointer(Slate, Native, Local, true);
        const bool Started = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const bool Ended = Slate.ProcessMouseButtonUpEvent(ck_tests_ui_radial::Pointer(Slate, Native, Local, false));
        return Started && Ended;
    };
    Move(Top);
    TestEqual(TEXT("Real pointer hover selects available tile"), Selected, FString(TEXT("overload")));
    TestTrue(TEXT("Real click on available tile handled"), Click(Top));
    TestEqual(TEXT("Real click activates available tile"), Activated, FString(TEXT("overload")));
    Move(Right);
    TestEqual(TEXT("Blocked tile still describes"), Selected, FString(TEXT("warp")));
    Click(Right);
    TestEqual(TEXT("Blocked tile does not activate"), Activated, FString(TEXT("overload")));
    Move(Left);
    TestEqual(TEXT("Cooldown tile still describes"), Selected, FString(TEXT("throw")));
    Click(Left);
    TestEqual(TEXT("Cooldown tile does not activate"), Activated, FString(TEXT("overload")));
    Slate.SetKeyboardFocus(Native, EFocusCause::SetDirectly);
    TestTrue(TEXT("Root owns keyboard focus"), Slate.GetKeyboardFocusedWidget().Get() == &Native.Get());
    const FKeyEvent RightKey(EKeys::Right, FModifierKeysState{}, Slate.GetCursorUser()->GetUserIndex(), false, 0, 0);
    TestTrue(TEXT("Real keyboard navigation reaches wheel"), Slate.ProcessKeyDownEvent(RightKey));
    TestEqual(TEXT("Keyboard navigation wraps to enabled tile"), Native->GetSelectedKey(), FString(TEXT("overload")));
    const FKeyEvent Enter(EKeys::Enter, FModifierKeysState{}, Slate.GetCursorUser()->GetUserIndex(), false, 0, 0);
    Activated.Reset();
    TestTrue(TEXT("Real keyboard activation reaches wheel"), Slate.ProcessKeyDownEvent(Enter));
    TestEqual(TEXT("Enter activates selected enabled tile"), Activated, FString(TEXT("overload")));

    const TSharedRef<SWidget> HeldRoot = Component->GetWidget();
    const FCkUiCustomStyleValue BadStyle{ECkUiCustomStyleKind::Length,
        std::numeric_limits<float>::quiet_NaN()};
    Arguments.Style.CustomProperties.Add(TEXT("-ck-radial-tile-size"), BadStyle);
    Failure.Reset();
    TestFalse(TEXT("Malformed style reload rejects"), Component->PrepareReload(Arguments, Failure).IsValid());
    TestFalse(TEXT("Style rejection diagnostic is explicit"), Failure.IsEmpty());
    TestTrue(TEXT("Rejected reload preserves root"), &Component->GetWidget().Get() == &HeldRoot.Get());
    Arguments.Style.CustomProperties.Remove(TEXT("-ck-radial-tile-size"));
    TSharedPtr<FCkUiCollection> WrongSchema;
    TestTrue(TEXT("Alternative collection creates"), FCkUiCollection::TryCreate({
        {TEXT("actor"), ECkUiFieldKind::Text, true},
        {TEXT("glyph"), ECkUiFieldKind::Text, true},
        {TEXT("angle"), ECkUiFieldKind::Number, true},
        {TEXT("cooldown"), ECkUiFieldKind::Number, true},
        {TEXT("enabled"), ECkUiFieldKind::Bool, true},
    }, WrongSchema).Succeeded);
    Arguments.Collections.Add(TEXT("items"), WrongSchema);
    Failure.Reset();
    TestFalse(TEXT("Malformed collection reload rejects"), Component->PrepareReload(Arguments, Failure).IsValid());
    TestFalse(TEXT("Collection rejection diagnostic is explicit"), Failure.IsEmpty());
    TestEqual(TEXT("Rejected collection leaves selected tile"), Native->GetSelectedKey(), Selected);
    Arguments.Collections.Add(TEXT("items"), Collection);
    Failure.Reset();
    TUniquePtr<ICkUiPreparedWidgetUpdate> Prepared = Component->PrepareReload(Arguments, Failure);
    if (!TestTrue(TEXT("Compatible reload prepares"), Prepared.IsValid()))
    { Slate.DestroyWindowImmediately(Window); return false; }
    Prepared->Commit();
    TestTrue(TEXT("Compatible reload retains root"), &Component->GetWidget().Get() == &HeldRoot.Get());
    Component->ReleaseOwnerInteraction();
    Activated.Reset();
    Native->OnKeyDown(Native->GetCachedGeometry(), FKeyEvent(EKeys::Enter, FModifierKeysState{},
        Slate.GetCursorUser()->GetUserIndex(), false, 0, 0));
    TestTrue(TEXT("Owner-released native root is frozen"), Native->IsFrozen());
    TestTrue(TEXT("Owner release suppresses held-root actions"), Activated.IsEmpty());
    Component.Reset();
    TestFalse(TEXT("Held native root cannot receive keyboard focus"), Native->SupportsKeyboardFocus());
    Slate.DestroyWindowImmediately(Window);

    TWeakPtr<ICkUiRetainedWidget> ReentrantOwner;
    FString ReentrantActivation;
    FCkUiCustomWidgetArguments ReentrantArguments;
    ReentrantArguments.Id = TEXT("reentrant-wheel");
    ReentrantArguments.Collections.Add(TEXT("items"), Collection);
    ReentrantArguments.CanDispatchEvents = TAttribute<bool>(true);
    ReentrantArguments.StringChanged.Add(TEXT("select"), FCkUiOnStringChanged::CreateLambda(
        [&ReentrantOwner](const FString&)
        {
            if (const TSharedPtr<ICkUiRetainedWidget> Owner = ReentrantOwner.Pin())
            { Owner->ReleaseOwnerInteraction(); }
        }));
    ReentrantArguments.StringChanged.Add(TEXT("activate"), FCkUiOnStringChanged::CreateLambda(
        [&ReentrantActivation](const FString& InKey) { ReentrantActivation = InKey; }));
    Failure.Reset();
    TSharedPtr<ICkUiRetainedWidget> Reentrant = Registration->RetainedFactory(ReentrantArguments, Failure);
    if (!TestTrue(TEXT("Reentrant selector constructs"), Reentrant.IsValid()))
    { AddError(Failure); return false; }
    ReentrantOwner = Reentrant;
    const TSharedRef<SCkUiRadialSelector> ReentrantNative = StaticCastSharedRef<SCkUiRadialSelector>(Reentrant->GetWidget());
    const TSharedRef<SWindow> ReentrantWindow = SNew(SWindow).ClientSize(FVector2D(460.0f, 460.0f))
        .CreateTitleBar(false)[SNew(SBox).WidthOverride(440.0f).HeightOverride(440.0f)[ReentrantNative]];
    Slate.AddWindow(ReentrantWindow, true);
    Slate.PumpMessages();
    Slate.Tick();
    Slate.Tick();
    Slate.ClearKeyboardFocus(EFocusCause::SetDirectly);
    const FVector2D ReentrantSize = ReentrantNative->GetCachedGeometry().GetLocalSize();
    const FVector2D ReentrantTop(ReentrantSize.X * 0.5f,
        ReentrantSize.Y * 0.5f - 0.39f * FMath::Min(ReentrantSize.X, ReentrantSize.Y));
    const FPointerEvent ReentrantDown = ck_tests_ui_radial::Pointer(Slate, ReentrantNative, ReentrantTop, true);
    Slate.ProcessMouseButtonDownEvent(ReentrantWindow->GetNativeWindow(), ReentrantDown);
    TestTrue(TEXT("Selection callback releases its owner during mouse down"), ReentrantNative->IsFrozen());
    TestTrue(TEXT("Released wheel does not reacquire focus"),
        Slate.GetUserFocusedWidget(ReentrantDown.GetUserIndex()).Get() != &ReentrantNative.Get());
    Slate.ProcessMouseButtonUpEvent(ck_tests_ui_radial::Pointer(Slate, ReentrantNative, ReentrantTop, false));
    TestTrue(TEXT("Released wheel cannot activate on trailing mouse up"), ReentrantActivation.IsEmpty());
    Reentrant.Reset();
    TestFalse(TEXT("Held reentrant root stays nonfocusable"), ReentrantNative->SupportsKeyboardFocus());
    Slate.DestroyWindowImmediately(ReentrantWindow);
    return true;
}

#endif
