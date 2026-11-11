#include "CkSlateLayout/CkUiHudControls.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SWindow.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiHudControlsNative,
    "Ck.UiAuthoring.HudControls.NativeRetentionAndAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiHudControlsNative::RunTest(const FString&) -> bool
{
    if (!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("HUD controls require initialized Slate."));
        return false;
    }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("Both HUD controls register"), FCkUiHudControls::Register(Registry).Succeeded))
    { return false; }
    const TSharedRef<const FCkUiWidgetRegistrySnapshot> Snapshot = Registry.CreateSnapshot();
    const FCkUiCustomWidgetRegistration* MeterRegistration = Snapshot->Find(TEXT("hud-meter"));
    const FCkUiCustomWidgetRegistration* GraphicRegistration = Snapshot->Find(TEXT("hud-graphic"));
    if (!TestTrue(TEXT("Meter has a retained factory"), MeterRegistration != nullptr && !!MeterRegistration->RetainedFactory)
        || !TestTrue(TEXT("Graphic has a retained factory"), GraphicRegistration != nullptr && !!GraphicRegistration->RetainedFactory))
    { return false; }

    FString Failure;
    FCkUiCustomWidgetArguments Invalid;
    TestFalse(TEXT("Missing meter value rejects"), MeterRegistration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Meter reports exact required contract"), Failure,
        FString(TEXT("HUD meter requires an id and value binding.")));
    Failure.Reset();
    TestFalse(TEXT("Missing graphic image rejects"), GraphicRegistration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Graphic reports exact required contract"), Failure,
        FString(TEXT("HUD graphic requires an id and image binding.")));

    float Value = 1.0f;
    FCkUiCustomWidgetArguments MeterArguments;
    MeterArguments.Id = TEXT("health");
    MeterArguments.NumberBindings.Add(TEXT("value"), TAttribute<float>::CreateLambda([&Value]() { return Value; }));
    FCkUiCustomStyleValue InvalidNumber;
    InvalidNumber.Kind = ECkUiCustomStyleKind::Number;
    InvalidNumber.Number = 2.5f;
    MeterArguments.Style.CustomProperties.Add(TEXT("-ck-hud-meter-segments"), InvalidNumber);
    Failure.Reset();
    TestFalse(TEXT("Fractional segment count rejects"), MeterRegistration->RetainedFactory(MeterArguments, Failure).IsValid());
    TestEqual(TEXT("Segment failure is explicit"), Failure,
        FString(TEXT("HUD meter segments must be an integer and radial must be 0 or 1.")));
    InvalidNumber.Number = std::numeric_limits<float>::quiet_NaN();
    MeterArguments.Style.CustomProperties.Add(TEXT("-ck-hud-meter-response"), InvalidNumber);
    MeterArguments.Style.CustomProperties.Remove(TEXT("-ck-hud-meter-segments"));
    Failure.Reset();
    TestFalse(TEXT("Nonfinite response rejects"), MeterRegistration->RetainedFactory(MeterArguments, Failure).IsValid());
    TestFalse(TEXT("Nonfinite failure is explicit"), Failure.IsEmpty());
    MeterArguments.Style.CustomProperties.Remove(TEXT("-ck-hud-meter-response"));
    InvalidNumber.Number = 4.0f;
    MeterArguments.Style.CustomProperties.Add(TEXT("-ck-hud-meter-segments"), InvalidNumber);
    Failure.Reset();
    TSharedPtr<ICkUiRetainedWidget> Meter = MeterRegistration->RetainedFactory(MeterArguments, Failure);
    if (!TestTrue(TEXT("Valid segmented meter constructs"), Meter.IsValid()))
    { AddError(Failure); return false; }
    const TSharedRef<SCkUiHudMeter> Native = StaticCastSharedRef<SCkUiHudMeter>(Meter->GetWidget());
    TestEqual(TEXT("Meter carries the authored tag"), Native->GetTag(), FName(TEXT("health")));
    TestEqual(TEXT("Meter is passive"), Native->GetVisibility(), EVisibility::SelfHitTestInvisible);

    FSlateApplication& Slate = FSlateApplication::Get();
    const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(240.0f, 80.0f))
        .CreateTitleBar(false)[Native];
    Slate.AddWindow(Window, true);
    Slate.PumpMessages();
    Slate.Tick();
    TestTrue(TEXT("Real Slate tick initializes value"), FMath::IsNearlyEqual(Native->GetDisplayedFraction(), 1.0f));
    Value = 0.2f;
    Native->AdvancePresentation(0.10f);
    TestTrue(TEXT("Damage moves the displayed fill"), Native->GetDisplayedFraction() < 1.0f);
    TestTrue(TEXT("Damage leaves a delayed loss trail"), Native->GetTrailFraction() > Native->GetDisplayedFraction());
    const float BeforeReload = Native->GetDisplayedFraction();
    FCkUiCustomStyleValue Radial;
    Radial.Kind = ECkUiCustomStyleKind::Number;
    Radial.Number = 1.0f;
    MeterArguments.Style.CustomProperties.Add(TEXT("-ck-hud-meter-radial"), Radial);
    Failure.Reset();
    TUniquePtr<ICkUiPreparedWidgetUpdate> Prepared = Meter->PrepareReload(MeterArguments, Failure);
    if (!TestTrue(TEXT("Compatible radial style prepares"), Prepared.IsValid()))
    { Slate.DestroyWindowImmediately(Window); return false; }
    TestTrue(TEXT("Prepare leaves animation untouched"), FMath::IsNearlyEqual(Native->GetDisplayedFraction(), BeforeReload));
    Prepared->Commit();
    TestTrue(TEXT("Commit retains the same native meter"), &Meter->GetWidget().Get() == &Native.Get());
    TestTrue(TEXT("Commit retains current animation"), FMath::IsNearlyEqual(Native->GetDisplayedFraction(), BeforeReload));
    Slate.Tick();
    const float BeforeRejected = Native->GetDisplayedFraction();
    InvalidNumber.Number = 0.5f;
    MeterArguments.Style.CustomProperties.Add(TEXT("-ck-hud-meter-segments"), InvalidNumber);
    Failure.Reset();
    TestFalse(TEXT("Rejected reload does not publish"), Meter->PrepareReload(MeterArguments, Failure).IsValid());
    TestTrue(TEXT("Rejected reload preserves animation"), FMath::IsNearlyEqual(Native->GetDisplayedFraction(), BeforeRejected));
    Value = std::numeric_limits<float>::quiet_NaN();
    Native->AdvancePresentation(0.1f);
    TestTrue(TEXT("Nonfinite live value is total"), FMath::IsFinite(Native->GetDisplayedFraction())
        && Native->GetTargetFraction() == 0.0f);
    Meter->ReleaseOwnerInteraction();
    const float Frozen = Native->GetDisplayedFraction();
    Value = 1.0f;
    Native->AdvancePresentation(1.0f);
    TestTrue(TEXT("Released meter remains frozen"), Native->IsFrozen()
        && FMath::IsNearlyEqual(Native->GetDisplayedFraction(), Frozen));
    Meter.Reset();
    TestTrue(TEXT("Held native meter stays inert after adapter destruction"), Native->IsFrozen());
    Slate.DestroyWindowImmediately(Window);

    FCkUiCustomWidgetArguments GraphicArguments;
    GraphicArguments.Id = TEXT("frame");
    GraphicArguments.ImageBindings.Add(TEXT("image"),
        TAttribute<const FSlateBrush*>(FCoreStyle::Get().GetDefaultBrush()));
    float Opacity = 0.6f;
    GraphicArguments.NumberBindings.Add(TEXT("opacity"), TAttribute<float>::CreateLambda([&Opacity]() { return Opacity; }));
    Failure.Reset();
    TSharedPtr<ICkUiRetainedWidget> Graphic = GraphicRegistration->RetainedFactory(GraphicArguments, Failure);
    if (!TestTrue(TEXT("Graphic retains an image widget"), Graphic.IsValid()))
    { AddError(Failure); return false; }
    const TSharedRef<SWidget> HeldGraphic = Graphic->GetWidget();
    TestEqual(TEXT("Graphic carries authored tag"), HeldGraphic->GetTag(), FName(TEXT("frame")));
    Graphic->ReleaseOwnerInteraction();
    Graphic.Reset();
    TestTrue(TEXT("Graphic can outlive its adapter"), HeldGraphic->GetTypeAsString() == TEXT("SImage"));
    return true;
}

#endif
