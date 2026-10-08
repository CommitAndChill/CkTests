#include "CkSlateLayout/CkUiKeyHint.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_key_hint
{
    auto FindText(const TSharedRef<SWidget>& InWidget) -> TSharedPtr<STextBlock>
    {
        if (InWidget->GetTypeAsString() == TEXT("STextBlock"))
        { return StaticCastSharedRef<STextBlock>(InWidget); }
        const FChildren* Children = InWidget->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<STextBlock> Found = FindText(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index))))
            { return Found; }
        }
        return {};
    }

    auto Click(FSlateApplication& InSlate, const TSharedRef<SWindow>& InWindow,
        const TSharedRef<SWidget>& InWidget) -> bool
    {
        if (!InWindow->GetNativeWindow().IsValid())
        { return false; }
        const FGeometry Geometry = InWidget->GetCachedGeometry();
        const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
        InSlate.SetCursorPos(Position);
        const int32 UserIndex = InSlate.GetCursorUser()->GetUserIndex();
        const uint32 PointerIndex = FSlateApplication::CursorPointerIndex;
        const FPointerEvent Move(UserIndex, PointerIndex, Position, Position,
            TSet<FKey>{}, EKeys::Invalid, 0.0f, FModifierKeysState{});
        InSlate.ProcessMouseMoveEvent(Move, true);
        const FPointerEvent Down(UserIndex, PointerIndex, Position, Position,
            TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
        const bool Pressed = InSlate.ProcessMouseButtonDownEvent(InWindow->GetNativeWindow(), Down);
        const FPointerEvent Up(UserIndex, PointerIndex, Position, Position,
            TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
        const bool Released = InSlate.ProcessMouseButtonUpEvent(Up);
        return Pressed && Released;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiKeyHintNative,
    "Ck.UiAuthoring.KeyHint.NativeLayoutReloadAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiKeyHintNative::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_key_hint;
    if (!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("Key hint test requires Slate."));
        return false;
    }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("Key hint registers"), RegisterKeyHintUiControl(Registry).Succeeded))
    { return false; }
    const TSharedRef<const FCkUiWidgetRegistrySnapshot> Snapshot = Registry.CreateSnapshot();
    const FCkUiCustomWidgetRegistration* Registration = Snapshot->Find(TEXT("key-hint"));
    if (!TestTrue(TEXT("Key hint has retained factory"), Registration != nullptr && !!Registration->RetainedFactory))
    { return false; }

    FString Failure;
    FCkUiCustomWidgetArguments Invalid;
    TestFalse(TEXT("Missing label rejects"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Missing label has explicit failure"), Failure,
        FString(TEXT("Key hint requires an id and nonempty literal label.")));

    FCkUiCustomWidgetArguments Args;
    Args.Id = TEXT("hint");
    Args.TextProperties.Add(TEXT("label"), FText::FromString(TEXT("E")));
    Args.CanDispatchEvents = TAttribute<bool>(true);
    int32 Dispatches = 0;
    Args.Actions.Add(TEXT("action"), FSimpleDelegate::CreateLambda([&Dispatches]() { ++Dispatches; }));
    Args.BindingNames.Add(TEXT("action"), TEXT("interact"));
    FCkUiCustomStyleValue InvalidStyle;
    InvalidStyle.Kind = ECkUiCustomStyleKind::Length;
    InvalidStyle.Number = std::numeric_limits<float>::quiet_NaN();
    Args.Style.CustomProperties.Add(TEXT("-ck-key-hint-font-size"), InvalidStyle);
    Failure.Reset();
    TestFalse(TEXT("Nonfinite size rejects before publication"), Registration->RetainedFactory(Args, Failure).IsValid());
    TestFalse(TEXT("Nonfinite size reports failure"), Failure.IsEmpty());
    Args.Style.CustomProperties.Remove(TEXT("-ck-key-hint-font-size"));
    Args.TextProperties.Add(TEXT("font-family"), FText::FromString(TEXT("unregistered")));
    Failure.Reset();
    TestFalse(TEXT("Unadmitted font family rejects"), Registration->RetainedFactory(Args, Failure).IsValid());
    Args.TextProperties.Remove(TEXT("font-family"));

    Failure.Reset();
    TSharedPtr<ICkUiRetainedWidget> Hint = Registration->RetainedFactory(Args, Failure);
    if (!TestTrue(TEXT("Interactive hint constructs"), Hint.IsValid()))
    {
        AddError(Failure);
        return false;
    }
    const TSharedRef<SWidget> Native = Hint->GetWidget();
    const TSharedPtr<STextBlock> Text = FindText(Native);
    if (!TestTrue(TEXT("Hint has a native text child"), Text.IsValid()))
    { return false; }
    TestEqual(TEXT("Authored tag is retained"), Native->GetTag(), FName(TEXT("hint")));
    TestTrue(TEXT("Interactive hint supports keyboard focus"), Native->SupportsKeyboardFocus());
    TestEqual(TEXT("Literal text is displayed"), Text->GetText().ToString(), FString(TEXT("E")));
    Native->SlatePrepass();
    const float SingleWidth = Native->GetDesiredSize().X;

    FCkUiCustomWidgetArguments PassiveArgs = Args;
    PassiveArgs.Id = TEXT("passive");
    PassiveArgs.TextProperties.Add(TEXT("label"), FText::FromString(TEXT("SPACE")));
    PassiveArgs.Actions.Remove(TEXT("action"));
    PassiveArgs.BindingNames.Remove(TEXT("action"));
    Failure.Reset();
    TSharedPtr<ICkUiRetainedWidget> Passive = Registration->RetainedFactory(PassiveArgs, Failure);
    if (!TestTrue(TEXT("Passive hint constructs"), Passive.IsValid()))
    {
        AddError(Failure);
        return false;
    }
    TestEqual(TEXT("Passive hint does not accept pointer hits"), Passive->GetWidget()->GetVisibility(),
        EVisibility::SelfHitTestInvisible);
    TestFalse(TEXT("Passive hint does not support keyboard focus"),
        Passive->GetWidget()->SupportsKeyboardFocus());
    Passive->GetWidget()->SlatePrepass();
    TestTrue(TEXT("SPACE has wider natural desired size than E"), Passive->GetWidget()->GetDesiredSize().X > SingleWidth);
    PassiveArgs.Actions.Add(TEXT("action"), Args.Actions.FindRef(TEXT("action")));
    PassiveArgs.BindingNames.Add(TEXT("action"), TEXT("interact"));
    Failure.Reset();
    TUniquePtr<ICkUiPreparedWidgetUpdate> PassiveUpdate = Passive->PrepareReload(PassiveArgs, Failure);
    if (!TestTrue(TEXT("Passive hint can prepare interactive reload"), PassiveUpdate.IsValid()))
    { AddError(Failure); return false; }
    PassiveUpdate->Commit();
    TestTrue(TEXT("Interactive reload enables keyboard focus"), Passive->GetWidget()->SupportsKeyboardFocus());
    PassiveArgs.Actions.Remove(TEXT("action"));
    PassiveArgs.BindingNames.Remove(TEXT("action"));
    Failure.Reset();
    PassiveUpdate = Passive->PrepareReload(PassiveArgs, Failure);
    if (!TestTrue(TEXT("Interactive hint can prepare passive reload"), PassiveUpdate.IsValid()))
    { AddError(Failure); return false; }
    PassiveUpdate->Commit();
    TestFalse(TEXT("Passive reload disables keyboard focus"), Passive->GetWidget()->SupportsKeyboardFocus());

    FSlateApplication& Slate = FSlateApplication::Get();
    const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(200.0f, 80.0f))
        .CreateTitleBar(false)
        [SNew(SBox).WidthOverride(120.0f).HeightOverride(50.0f)[Native]];
    Slate.AddWindow(Window, true);
    Slate.PumpMessages();
    Slate.Tick();
    Slate.Tick();
    const FGeometry ButtonGeometry = Native->GetCachedGeometry();
    const FGeometry TextGeometry = Text->GetCachedGeometry();
    const FVector2D ButtonCenter = ButtonGeometry.LocalToAbsolute(ButtonGeometry.GetLocalSize() * 0.5f);
    const FVector2D TextCenter = TextGeometry.LocalToAbsolute(TextGeometry.GetLocalSize() * 0.5f);
    TestTrue(TEXT("Text is centered horizontally in the button"), FMath::Abs(ButtonCenter.X - TextCenter.X) <= 1.0f);
    TestTrue(TEXT("Text is centered vertically in the button"), FMath::Abs(ButtonCenter.Y - TextCenter.Y) <= 1.0f);
    TestTrue(TEXT("Interactive key hint accepts click"), Click(Slate, Window, Native));
    TestEqual(TEXT("Click dispatches one action"), Dispatches, 1);

    Args.TextProperties.Add(TEXT("label"), FText::FromString(TEXT("SPACE")));
    Failure.Reset();
    TUniquePtr<ICkUiPreparedWidgetUpdate> Update = Hint->PrepareReload(Args, Failure);
    if (!TestTrue(TEXT("Compatible label reload prepares"), Update.IsValid()))
    { Slate.DestroyWindowImmediately(Window); AddError(Failure); return false; }
    TestEqual(TEXT("Prepare does not alter live label"), Text->GetText().ToString(), FString(TEXT("E")));
    Update->Commit();
    TestTrue(TEXT("Reload keeps native widget identity"), &Hint->GetWidget().Get() == &Native.Get());
    TestTrue(TEXT("Interactive reload preserves keyboard focus support"), Native->SupportsKeyboardFocus());
    TestEqual(TEXT("Commit updates the label"), Text->GetText().ToString(), FString(TEXT("SPACE")));
    Slate.Tick();
    const FGeometry SpaceButton = Native->GetCachedGeometry();
    const FGeometry SpaceText = Text->GetCachedGeometry();
    const FVector2D SpaceButtonCenter = SpaceButton.LocalToAbsolute(SpaceButton.GetLocalSize() * 0.5f);
    const FVector2D SpaceTextCenter = SpaceText.LocalToAbsolute(SpaceText.GetLocalSize() * 0.5f);
    TestTrue(TEXT("SPACE remains centered on both axes"),
        FMath::Abs(SpaceButtonCenter.X - SpaceTextCenter.X) <= 1.0f
        && FMath::Abs(SpaceButtonCenter.Y - SpaceTextCenter.Y) <= 1.0f);

    Args.TextProperties.Add(TEXT("label"), FText::FromString(TEXT("7")));
    Failure.Reset();
    Update = Hint->PrepareReload(Args, Failure);
    if (!TestTrue(TEXT("Digit label reload prepares"), Update.IsValid()))
    { Slate.DestroyWindowImmediately(Window); AddError(Failure); return false; }
    Update->Commit();
    Slate.Tick();
    const FGeometry DigitButton = Native->GetCachedGeometry();
    const FGeometry DigitText = Text->GetCachedGeometry();
    const FVector2D DigitButtonCenter = DigitButton.LocalToAbsolute(DigitButton.GetLocalSize() * 0.5f);
    const FVector2D DigitTextCenter = DigitText.LocalToAbsolute(DigitText.GetLocalSize() * 0.5f);
    TestTrue(TEXT("Digit remains centered on both axes"),
        FMath::Abs(DigitButtonCenter.X - DigitTextCenter.X) <= 1.0f
        && FMath::Abs(DigitButtonCenter.Y - DigitTextCenter.Y) <= 1.0f);

    Args.Id = TEXT("different-id");
    Failure.Reset();
    TestFalse(TEXT("Retained id cannot change"), Hint->PrepareReload(Args, Failure).IsValid());
    Args.Id = TEXT("hint");
    Args.Style.CustomProperties.Add(TEXT("-ck-key-hint-font-size"), InvalidStyle);
    Failure.Reset();
    TestFalse(TEXT("Invalid reload rejects atomically"), Hint->PrepareReload(Args, Failure).IsValid());
    TestEqual(TEXT("Rejected reload retains label"), Text->GetText().ToString(), FString(TEXT("7")));
    Args.Style.CustomProperties.Remove(TEXT("-ck-key-hint-font-size"));
    Failure.Reset();
    Update = Hint->PrepareReload(Args, Failure);
    if (!TestTrue(TEXT("Valid update prepares before release"), Update.IsValid()))
    { Slate.DestroyWindowImmediately(Window); AddError(Failure); return false; }
    Hint->ReleaseOwnerInteraction();
    Update->Commit();
    TestEqual(TEXT("Released hint stops hit testing"), Native->GetVisibility(), EVisibility::SelfHitTestInvisible);
    TestFalse(TEXT("Released hint does not support keyboard focus"), Native->SupportsKeyboardFocus());
    TestFalse(TEXT("Released hint cannot reload"), Hint->PrepareReload(Args, Failure).IsValid());
    Click(Slate, Window, Native);
    Hint.Reset();
    TestEqual(TEXT("Held native widget remains inert"), Native->GetVisibility(), EVisibility::SelfHitTestInvisible);
    TestFalse(TEXT("Held native widget remains unfocusable"), Native->SupportsKeyboardFocus());
    TestEqual(TEXT("Release does not dispatch"), Dispatches, 1);
    Slate.DestroyWindowImmediately(Window);
    return true;
}

#endif
