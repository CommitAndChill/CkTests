#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/UI/CkPauseMenuProof.h"

#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"
#include "CkUI/Layout/CkUI_PrimaryGameLayout.h"
#include "CkUI/Layout/CkUI_LayerStack.h"

#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"
#include "Layout/Children.h"
#include "Kismet/GameplayStatics.h"

namespace ck_pause_menu_proof_test
{
    const FName ResumeAction{TEXT("ResumeRequested")};
    const FName SettingsAction{TEXT("SettingsRequested")};
    const FName ReturnAction{TEXT("ReturnToTitleRequested")};
    auto GetResourcePath(const TCHAR* InFilename) -> FString
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
        return Plugin.IsValid()
            ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/UiAuthoring/PauseMenuProof"), InFilename)
            : FString{};
    }

    struct FState
    {
        TStrongObjectPtr<UCk_PauseMenuProof_UE> Menu;
    };

    class FReleaseSpy final : public IInputProcessor
    {
    public:
        int32 MouseUps = 0;
        int32 KeyUps = 0;
        int32 KeyRepeats = 0;

        auto Tick(const float, FSlateApplication&, TSharedRef<ICursor>) -> void override {}
        auto HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InEvent) -> bool override
        {
            if (InEvent.IsRepeat()) { ++KeyRepeats; }
            return false;
        }
        auto HandleKeyUpEvent(FSlateApplication&, const FKeyEvent&) -> bool override
        {
            ++KeyUps;
            return false;
        }
        auto HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent&) -> bool override
        {
            ++MouseUps;
            return false;
        }
    };

    auto Tick(FSlateApplication& InSlate) -> void
    {
        InSlate.PumpMessages();
        InSlate.Tick();
        InSlate.Tick();
    }

    auto FocusContains(FSlateApplication& InSlate, const int32 InUser, const TSharedRef<SWidget>& InTarget) -> bool
    {
        const TSharedPtr<SWidget> Focused = InSlate.GetUserFocusedWidget(InUser);
        FWidgetPath Path;
        if (NOT Focused.IsValid() || NOT InSlate.GeneratePathToWidgetUnchecked(Focused.ToSharedRef(), Path))
        { return false; }
        for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
        {
            if (Path.Widgets[Index].Widget == InTarget) { return true; }
        }
        return false;
    }

    auto PathContains(const FWidgetPath& InPath, const TSharedRef<SWidget>& InTarget) -> bool
    {
        for (int32 Index = 0; Index < InPath.Widgets.Num(); ++Index)
        {
            if (InPath.Widgets[Index].Widget == InTarget) { return true; }
        }
        return false;
    }

    auto ContainsText(const TSharedRef<SWidget>& InRoot, const FString& InText) -> bool
    {
        if (InRoot->GetTypeAsString() == TEXT("SCkFlexText")
            && StaticCastSharedRef<SCkFlexText>(InRoot)->GetText().ToString() == InText)
        { return true; }
        if (InRoot->GetTypeAsString() == TEXT("STextBlock")
            && StaticCastSharedRef<STextBlock>(InRoot)->GetText().ToString() == InText)
        { return true; }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (ContainsText(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InText))
            { return true; }
        }
        return false;
    }

    auto Capture(FSlateApplication& InSlate, const TSharedRef<SWindow>& InWindow,
        const TSharedRef<SWidget>& InViewport,
        const FString& InName, const FIntPoint InRequestedSize,
        const int32 InClearHoverUser = INDEX_NONE) -> bool
    {
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Tick(InSlate);
            Pixels.Reset();
            Size = FIntVector::ZeroValue;
            if (NOT InSlate.TakeScreenshot(InViewport, Pixels, Size) || Size.X <= 0 || Size.Y <= 0
                || Pixels.Num() < Size.X * Size.Y)
            {
                UE_LOG(LogTemp, Error, TEXT("Pause proof capture %s failed viewport readback (%d x %d, %d pixels)."),
                    *InName, Size.X, Size.Y, Pixels.Num());
                return false;
            }
            if (Size.X == InRequestedSize.X && Size.Y == InRequestedSize.Y) { break; }
            // Resize accepts a client size; GetSizeInScreen includes native borders and the title bar.
            const FVector2D WindowSize = InWindow->GetClientSizeInScreen();
            InWindow->Resize(WindowSize + FVector2D{
                static_cast<double>(InRequestedSize.X - Size.X),
                static_cast<double>(InRequestedSize.Y - Size.Y)});
        }
        if (Size.X != InRequestedSize.X || Size.Y != InRequestedSize.Y)
        {
            UE_LOG(LogTemp, Error, TEXT("Pause proof capture %s expected %d x %d; viewport is %d x %d."),
                *InName, InRequestedSize.X, InRequestedSize.Y, Size.X, Size.Y);
            return false;
        }
        if (InClearHoverUser != INDEX_NONE)
        {
            const FVector2D Position = InViewport->GetCachedGeometry().LocalToAbsolute(FVector2D{8.0f, 8.0f});
            const TSet<FKey> NoButtons;
            const FPointerEvent MoveEvent(InClearHoverUser, FSlateApplication::CursorPointerIndex,
                Position, Position, NoButtons, EKeys::Invalid, 0.0f, FModifierKeysState{});
            InSlate.SetCursorPos(Position);
            InSlate.ProcessMouseMoveEvent(MoveEvent, true);
            Tick(InSlate);
            Pixels.Reset();
            Size = FIntVector::ZeroValue;
            if (NOT InSlate.TakeScreenshot(InViewport, Pixels, Size)
                || Size.X != InRequestedSize.X || Size.Y != InRequestedSize.Y
                || Pixels.Num() < Size.X * Size.Y)
            { return false; }
        }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/PauseMenuProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Path = FPaths::Combine(Directory, InName + TEXT(".png"));
        const bool Saved = FImageUtils::SaveImageByExtension(*Path,
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
        if (InName.EndsWith(TEXT("-focused")))
        {
            const double Scale = FMath::Min(Size.X / 1920.0, Size.Y / 1080.0);
            const int32 X = FMath::RoundToInt((Size.X - 1920.0 * Scale) * 0.5 + 700.0 * Scale);
            const int32 Y = FMath::RoundToInt((Size.Y - 1080.0 * Scale) * 0.5 + 526.0 * Scale);
            const FColor Actual = Pixels[Y * Size.X + X];
            const FColor Expected{0xE7, 0xAD, 0x4A};
            const bool FocusFillMatches = FMath::Abs(int32(Actual.R) - int32(Expected.R)) <= 3
                && FMath::Abs(int32(Actual.G) - int32(Expected.G)) <= 3
                && FMath::Abs(int32(Actual.B) - int32(Expected.B)) <= 3;
            if (NOT FocusFillMatches)
            {
                UE_LOG(LogTemp, Error, TEXT("Pause proof %s focus fill expected #%s, got #%s at (%d,%d)."),
                    *InName, *Expected.ToHex(), *Actual.ToHex(), X, Y);
            }
            return Saved && FocusFillMatches;
        }
        if (InName.EndsWith(TEXT("-disabled")))
        {
            const double Scale = FMath::Min(Size.X / 1920.0, Size.Y / 1080.0);
            const double OffsetX = (Size.X - 1920.0 * Scale) * 0.5;
            const int32 Left = FMath::Clamp(FMath::RoundToInt(OffsetX + 700.0 * Scale), 0, Size.X);
            const int32 Right = FMath::Clamp(FMath::RoundToInt(OffsetX + 1220.0 * Scale), 0, Size.X);
            const int32 Top = FMath::Clamp(FMath::RoundToInt(624.0 * Scale), 0, Size.Y);
            const int32 Bottom = FMath::Clamp(FMath::RoundToInt(680.0 * Scale), 0, Size.Y);
            const FColor Expected{0x72, 0x7F, 0x8A};
            int32 MatchingTextPixels = 0;
            for (int32 Y = Top; Y < Bottom; ++Y)
            {
                for (int32 X = Left; X < Right; ++X)
                {
                    const FColor Actual = Pixels[Y * Size.X + X];
                    if (FMath::Abs(int32(Actual.R) - int32(Expected.R)) <= 3
                        && FMath::Abs(int32(Actual.G) - int32(Expected.G)) <= 3
                        && FMath::Abs(int32(Actual.B) - int32(Expected.B)) <= 3)
                    { ++MatchingTextPixels; }
                }
            }
            const bool DisabledTextMatches = MatchingTextPixels >= FMath::Max(16, FMath::RoundToInt(100.0 * Scale * Scale));
            if (NOT DisabledTextMatches)
            {
                UE_LOG(LogTemp, Error, TEXT("Pause proof %s disabled label expected #%s; only %d matching glyph pixels."),
                    *InName, *Expected.ToHex(), MatchingTextPixels);
            }
            return Saved && DisabledTextMatches;
        }
        return Saved;
    }

    auto MovePointerTo(FSlateApplication& InSlate, const TSharedRef<SWidget>& InTarget,
        const int32 InUser) -> bool
    {
        const FGeometry Geometry = InTarget->GetCachedGeometry();
        if (Geometry.GetLocalSize().X <= 0.0f || Geometry.GetLocalSize().Y <= 0.0f) { return false; }
        const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
        const TSet<FKey> NoButtons;
        const FPointerEvent MoveEvent(InUser, FSlateApplication::CursorPointerIndex, Position, Position,
            NoButtons, EKeys::Invalid, 0.0f, FModifierKeysState{});
        InSlate.SetCursorPos(Position);
        InSlate.ProcessMouseMoveEvent(MoveEvent, true);
        Tick(InSlate);
        return PathContains(InSlate.LocateWindowUnderMouse(
            Position, InSlate.GetInteractiveTopLevelWindows(), false, InUser), InTarget);
    }

    auto Click(FSlateApplication& InSlate, const TSharedRef<SButton>& InButton,
        const int32 InUser, const TSharedPtr<SWindow>& InWindow,
        const TSharedRef<SWidget>& InViewport,
        const FString& InPressedCapture = {}) -> bool
    {
        if (NOT InWindow.IsValid() || NOT InWindow->GetNativeWindow().IsValid()) { return false; }
        InWindow->BringToFront(true);
        Tick(InSlate);
        InSlate.ReleaseAllPointerCapture(InUser);
        if (NOT MovePointerTo(InSlate, InButton, InUser)) { return false; }

        const FGeometry Geometry = InButton->GetCachedGeometry();
        const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
        const TSet<FKey> NoButtons;
        const TSet<FKey> LeftDown{EKeys::LeftMouseButton};
        const FPointerEvent DownEvent(InUser, FSlateApplication::CursorPointerIndex, Position, Position,
            LeftDown, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
        const FPointerEvent UpEvent(InUser, FSlateApplication::CursorPointerIndex, Position, Position,
            NoButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
        const bool DownHandled = InSlate.ProcessMouseButtonDownEvent(InWindow->GetNativeWindow(), DownEvent);
        const bool Captured = InButton->HasMouseCapture();
        const bool PressedCaptured = InPressedCapture.IsEmpty()
            || Capture(InSlate, InWindow.ToSharedRef(), InViewport, InPressedCapture, FIntPoint{1920, 1080});
        const bool UpHandled = InSlate.ProcessMouseButtonUpEvent(UpEvent);
        Tick(InSlate);
        return DownHandled && Captured && UpHandled && PressedCaptured;
    }

    auto PressKey(FSlateApplication& InSlate, const FKey& InKey, const int32 InUser,
        const bool InShift = false) -> bool
    {
        const FModifierKeysState Modifiers{InShift, false, false, false, false, false, false, false, false};
        const uint32 SlateUser = static_cast<uint32>(InUser);
        const bool DownHandled = InSlate.ProcessKeyDownEvent(FKeyEvent{InKey, Modifiers, SlateUser, false, 0, 0});
        const bool UpHandled = InSlate.ProcessKeyUpEvent(FKeyEvent{InKey, Modifiers, SlateUser, false, 0, 0});
        Tick(InSlate);
        const TSharedPtr<SWidget> Focused = InSlate.GetUserFocusedWidget(InUser);
        UE_LOG(LogTemp, Display, TEXT("Pause proof key %s: down=%d up=%d focus=%s"),
            *InKey.ToString(), DownHandled, UpHandled,
            Focused.IsValid() ? *Focused->GetTag().ToString() : TEXT("<none>"));
        return DownHandled && UpHandled;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_PauseMenuProof_PIE,
    "Ck.UiAuthoring.PauseMenuProof.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCk_PauseMenuProof_PIE::RunTest(const FString&)
{
    using namespace ck_pause_menu_proof_test;
    const FString MarkupPath = GetResourcePath(TEXT("PauseMenuProof.ui.html"));
    const FString StylePath = GetResourcePath(TEXT("PauseMenuProof.ui.css"));
    if (NOT FSlateApplication::IsInitialized() || NOT FPaths::FileExists(MarkupPath)
        || NOT FPaths::FileExists(StylePath))
    {
        AddError(TEXT("Pause-menu PIE preflight requires Slate and both source-owned UI files."));
        return false;
    }

    const TSharedRef<FState> State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            APlayerController* Player = InWorld != nullptr ? InWorld->GetFirstPlayerController() : nullptr;
            if (NOT TestNotNull(TEXT("PIE player controller"), Player)) { return; }
            ULocalPlayer* LocalPlayer = Player->GetLocalPlayer();
            if (NOT TestNotNull(TEXT("PIE local player"), LocalPlayer)) { return; }
            UCk_UI_Layout_Subsystem_UE* LayoutSubsystem = LocalPlayer->GetSubsystem<UCk_UI_Layout_Subsystem_UE>();
            if (NOT TestNotNull(TEXT("production CkUI layout subsystem"), LayoutSubsystem)) { return; }
            if (NOT TestFalse(TEXT("entry map has no competing primary layout"), LayoutSubsystem->Has_Layout()))
            { return; }
            State->Menu.Reset(UCk_PauseMenuProof_UE::Open(Player));
            TestNotNull(TEXT("host opens on production layout"), State->Menu.Get());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            FSlateApplication& Slate = FSlateApplication::Get();
            APlayerController* Player = InWorld != nullptr ? InWorld->GetFirstPlayerController() : nullptr;
            ULocalPlayer* LocalPlayer = IsValid(Player) ? Player->GetLocalPlayer() : nullptr;
            UCk_UI_Layout_Subsystem_UE* LayoutSubsystem = IsValid(LocalPlayer)
                ? LocalPlayer->GetSubsystem<UCk_UI_Layout_Subsystem_UE>() : nullptr;
            TStrongObjectPtr<UCk_PauseMenuProof_UE>& Menu = State->Menu;
            if (NOT IsValid(LayoutSubsystem) || NOT IsValid(Menu.Get())) { return; }
            IConsoleVariable* Watermark = IConsoleManager::Get().FindConsoleVariable(TEXT("ck.UI.WatermarkDisplayPolicy"));
            const FString PreviousWatermark = Watermark != nullptr ? Watermark->GetString() : FString{};
            const uint32 PreviousWatermarkPriority = Watermark != nullptr ? Watermark->GetFlags() & ECVF_SetByMask : 0;
            const FName WatermarkTag{TEXT("CkTests.PauseMenuProof.Capture")};
            const auto RestoreWatermark = ScopeExitSupport::FScopeGuardSyntaxSupport() + [&]()
            {
                if (Watermark != nullptr)
                {
                    Watermark->Unset(ECVF_SetByPreview, WatermarkTag);
                    TestEqual(TEXT("capture restores developer watermark value"), Watermark->GetString(), PreviousWatermark);
                    TestEqual(TEXT("capture restores developer watermark priority"),
                        Watermark->GetFlags() & ECVF_SetByMask, PreviousWatermarkPriority);
                }
            };
            if (Watermark != nullptr)
            {
                Watermark->Set(0, ECVF_SetByPreview, WatermarkTag);
                TestEqual(TEXT("capture temporarily hides developer watermark"), Watermark->GetInt(), 0);
            }
            UCk_PauseMenuProof_UE* ActiveMenu = Menu.Get();
            bool TestOwnedPause = false;
            const auto CleanupMenu = ScopeExitSupport::FScopeGuardSyntaxSupport() + [&]()
            {
                if (IsValid(ActiveMenu) && ActiveMenu->IsActivated())
                { ActiveMenu->Request_Close(); }
                if (TestOwnedPause && IsValid(Player))
                { Player->SetPause(false); }
                if (IsValid(LayoutSubsystem) && LayoutSubsystem->Has_Layout())
                { LayoutSubsystem->DestroyLayout(); }
            };
            TSharedPtr<FCkUiView> View = Menu->Get_AuthoredView();
            if (NOT TestTrue(TEXT("source-authored view loads"), View.IsValid()
                && View->GetLastResult().Succeeded)) { return; }
            TestTrue(TEXT("host created a real primary layout"), LayoutSubsystem->Has_Layout());
            if (const UCk_UI_LayerStack_UE* Layer = LayoutSubsystem->Get_Layer(
                FGameplayTag::RequestGameplayTag(TEXT("CkTests.UI.Layer.PauseMenuProof"))))
            { TestEqual(TEXT("source layout applies its instant transition"), Layer->GetTransitionDuration(), 0.0f); }
            TestTrue(TEXT("menu owns paused state"), UGameplayStatics::IsGamePaused(InWorld));
            TestFalse(TEXT("unknown action cannot alter enabled state"),
                Menu->Request_SetActionEnabled(TEXT("UnknownAction"), false));
            TestFalse(TEXT("unknown text binding is rejected"),
                Menu->Request_SetLabel(TEXT("Unknown.Binding"), FText::FromString(TEXT("invalid"))));
            const int32 User = Menu->Get_SlateUserIndex();
            if (NOT TestTrue(TEXT("Slate user resolves"), User >= 0)) { return; }
            TSharedPtr<SButton> Resume = View->GetButton(TEXT("resume-button"));
            TSharedPtr<SButton> Settings = View->GetButton(TEXT("settings-button"));
            TSharedPtr<SButton> Return = View->GetButton(TEXT("return-button"));
            if (NOT TestTrue(TEXT("three authored buttons are mounted"), Resume.IsValid()
                && Settings.IsValid() && Return.IsValid())) { return; }
            Tick(Slate);
            TestTrue(TEXT("first focus lands on Resume"), FocusContains(Slate, User, Resume.ToSharedRef()));
            TestTrue(TEXT("Down routes from Resume to Settings"), PressKey(Slate, EKeys::Down, User)
                && FocusContains(Slate, User, Settings.ToSharedRef()));
            TestTrue(TEXT("Tab routes from Settings to Return"), PressKey(Slate, EKeys::Tab, User)
                && FocusContains(Slate, User, Return.ToSharedRef()));
            TestTrue(TEXT("Shift+Tab routes from Return to Settings"), PressKey(Slate, EKeys::Tab, User, true)
                && FocusContains(Slate, User, Settings.ToSharedRef()));
            TestTrue(TEXT("Up routes from Settings to Resume"), PressKey(Slate, EKeys::Up, User)
                && FocusContains(Slate, User, Resume.ToSharedRef()));
            TestTrue(TEXT("Up wraps from Resume to Return"), PressKey(Slate, EKeys::Up, User)
                && FocusContains(Slate, User, Return.ToSharedRef()));
            TestTrue(TEXT("Down wraps from Return to Resume"), PressKey(Slate, EKeys::Down, User)
                && FocusContains(Slate, User, Resume.ToSharedRef()));

            TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Resume.ToSharedRef());
            if (NOT TestTrue(TEXT("real PIE widget has a native window"), Window.IsValid()
                && Window->GetNativeWindow().IsValid())) { return; }
            const FVector2D OriginalWindowSize = Window->GetClientSizeInScreen();
            const auto RestoreWindowSize = ScopeExitSupport::FScopeGuardSyntaxSupport() + [&]()
            {
                if (Window.IsValid()) { Window->Resize(OriginalWindowSize); }
            };
            UGameViewportClient* GameViewport = InWorld->GetGameViewport();
            const TSharedPtr<SViewport> Viewport = IsValid(GameViewport)
                ? GameViewport->GetGameViewportWidget() : nullptr;
            if (NOT TestTrue(TEXT("capture targets the owning PIE game viewport"), Viewport.IsValid()))
            { return; }
            if (NOT TestTrue(TEXT("button and PIE viewport share the resized window"),
                Slate.FindWidgetWindow(Viewport.ToSharedRef()) == Window)) { return; }
            TestTrue(TEXT("1920x1080 baseline capture"), Capture(Slate, Window.ToSharedRef(),
                Viewport.ToSharedRef(), TEXT("1920x1080-focused"), FIntPoint{1920, 1080}, User));
            TestTrue(TEXT("1280x720 baseline capture"), Capture(Slate, Window.ToSharedRef(),
                Viewport.ToSharedRef(), TEXT("1280x720-focused"), FIntPoint{1280, 720}, User));
            TestTrue(TEXT("2560x1080 baseline capture"), Capture(Slate, Window.ToSharedRef(),
                Viewport.ToSharedRef(), TEXT("2560x1080-focused"), FIntPoint{2560, 1080}, User));
            TestTrue(TEXT("1920x1080 interactive viewport restored"), Capture(Slate, Window.ToSharedRef(),
                Viewport.ToSharedRef(), TEXT("1920x1080-focused"), FIntPoint{1920, 1080}, User));
            TestTrue(TEXT("Settings receives real pointer hover"), MovePointerTo(Slate, Settings.ToSharedRef(), User));
            TestTrue(TEXT("hover screenshot"), Capture(Slate, Window.ToSharedRef(),
                Viewport.ToSharedRef(), TEXT("1920x1080-hover"), FIntPoint{1920, 1080}));
            TestTrue(TEXT("Settings routes Slate pointer down/up"), Click(Slate, Settings.ToSharedRef(),
                User, Window, Viewport.ToSharedRef(), TEXT("1920x1080-pressed")));
            TestEqual(TEXT("Settings action fires exactly once"), Menu->Get_ActionCount(SettingsAction), 1);
            TestEqual(TEXT("unrelated Return action does not fire"), Menu->Get_ActionCount(ReturnAction), 0);
            TestEqual(TEXT("Settings action opens the nested settings screen"), Menu->Get_Screen(),
                ECk_UiAuthoringProof_Screen::Settings);
            TestTrue(TEXT("nested settings keeps pause ownership"), UGameplayStatics::IsGamePaused(InWorld));
            TestTrue(TEXT("Escape returns to Pause from nested Settings"), PressKey(Slate, EKeys::Escape, User)
                && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Pause && Menu->IsActivated());
            TestTrue(TEXT("returning from Settings retains pause ownership"), UGameplayStatics::IsGamePaused(InWorld));
            TestTrue(TEXT("old Pause view releases interactions on screen switch"), View->IsOwnerReleased());
            View = Menu->Get_AuthoredView();
            Resume = View.IsValid() ? View->GetButton(TEXT("resume-button")) : nullptr;
            Settings = View.IsValid() ? View->GetButton(TEXT("settings-button")) : nullptr;
            Return = View.IsValid() ? View->GetButton(TEXT("return-button")) : nullptr;
            if (NOT TestTrue(TEXT("Pause actions remount after nested Settings"), View.IsValid()
                && Resume.IsValid() && Settings.IsValid() && Return.IsValid())) { return; }

            Slate.SetUserFocus(User, Settings.ToSharedRef(), EFocusCause::SetDirectly);
            Tick(Slate);
            TestTrue(TEXT("Settings becomes focused"), FocusContains(Slate, User, Settings.ToSharedRef()));
            TestTrue(TEXT("Settings can be disabled through host state"),
                Menu->Request_SetActionEnabled(SettingsAction, false));
            Tick(Slate);
            TestFalse(TEXT("disabled Settings is not enabled"), Settings->IsEnabled());
            TestTrue(TEXT("disabled focus advances to Return"),
                FocusContains(Slate, User, Return.ToSharedRef()));
            TestTrue(TEXT("disabled screenshot"), Capture(Slate, Window.ToSharedRef(),
                Viewport.ToSharedRef(), TEXT("1920x1080-disabled"), FIntPoint{1920, 1080}));
            Click(Slate, Settings.ToSharedRef(), User, Window, Viewport.ToSharedRef());
            TestEqual(TEXT("disabled Settings does not dispatch"), Menu->Get_ActionCount(SettingsAction), 1);
            TestTrue(TEXT("Settings can be reenabled"), Menu->Request_SetActionEnabled(SettingsAction, true));
            Tick(Slate);
            TestTrue(TEXT("known source text binding accepts runtime text"),
                Menu->Request_SetLabel(TEXT("PauseSubtitle"), FText::FromString(TEXT("Paused proof"))));
            const FString LongReturnLabel = TEXT("Return to the Main Menu and Leave This Session");
            TestTrue(TEXT("localized Return label accepts a longer value"),
                Menu->Request_SetLabel(TEXT("PauseReturnToTitle"), FText::FromString(LongReturnLabel)));
            Slate.SetUserFocus(User, Resume.ToSharedRef(), EFocusCause::SetDirectly);
            Tick(Slate);
            TestTrue(TEXT("runtime subtitle reaches the mounted authored tree"),
                ContainsText(View->GetRegion(TEXT("main")), TEXT("Paused proof")));
            TestTrue(TEXT("long localized action label reaches authored button"),
                ContainsText(View->GetRegion(TEXT("main")), LongReturnLabel));
            TestTrue(TEXT("long-label capture retains Resume focus"),
                FocusContains(Slate, User, Resume.ToSharedRef()));
            TestTrue(TEXT("1920x1080 long-label capture"), Capture(Slate, Window.ToSharedRef(),
                Viewport.ToSharedRef(), TEXT("1920x1080-long-label"), FIntPoint{1920, 1080}));
            TestTrue(TEXT("reference subtitle restored"),
                Menu->Request_SetLabel(TEXT("PauseSubtitle"), FText::FromString(TEXT("The game is paused."))));
            TestTrue(TEXT("reference Return label restored"),
                Menu->Request_SetLabel(TEXT("PauseReturnToTitle"), FText::FromString(TEXT("Return to title"))));
            Tick(Slate);

            FString Markup;
            FString Style;
            if (NOT TestTrue(TEXT("source UI files load for valid reload"),
                FFileHelper::LoadFileToString(Markup, *GetResourcePath(TEXT("PauseMenuProof.ui.html")))
                    && FFileHelper::LoadFileToString(Style, *GetResourcePath(TEXT("PauseMenuProof.ui.css"))))) { return; }
            const int64 RevisionBefore = View->GetRevision();
            const auto RejectedWithoutMutation = [&]() -> bool
            {
                return View->GetRevision() == RevisionBefore
                    && View->GetButton(TEXT("resume-button")) == Resume
                    && View->GetButton(TEXT("settings-button")) == Settings
                    && View->GetButton(TEXT("return-button")) == Return
                    && NOT Menu->Get_SourceError().IsEmpty();
            };
            TestFalse(TEXT("malformed authored reload is rejected through the host"),
                Menu->Request_ApplySource(TEXT("<ui>"), TEXT("")));
            TestTrue(TEXT("malformed reload retains complete accepted UI and diagnostic"), RejectedWithoutMutation());
            const FString WrongRegion = Markup.Replace(TEXT("name=\"main\""), TEXT("name=\"other\""));
            TestFalse(TEXT("well-formed source missing main region is rejected"),
                Menu->Request_ApplySource(WrongRegion, Style));
            TestTrue(TEXT("wrong-region reload retains complete accepted UI and diagnostic"), RejectedWithoutMutation());
            const FString MissingButton = Markup.Replace(TEXT("id=\"settings-button\""),
                TEXT("id=\"settings-removed\""));
            TestFalse(TEXT("required button removal is rejected"), Menu->Request_ApplySource(MissingButton, Style));
            TestTrue(TEXT("button-removal reload retains complete accepted UI and diagnostic"), RejectedWithoutMutation());
            const FString WrongAction = Markup.Replace(TEXT("action=\"SettingsRequested\""),
                TEXT("action=\"ResumeRequested\""));
            TestFalse(TEXT("required button action reassignment is rejected"),
                Menu->Request_ApplySource(WrongAction, Style));
            TestTrue(TEXT("action-reassignment reload retains complete accepted UI and diagnostic"), RejectedWithoutMutation());
            const FString MissingEnabledBinding = Markup.Replace(
                TEXT(" enabled-bind=\"SettingsEnabled\""), TEXT(""));
            TestFalse(TEXT("required enabled binding removal is rejected"),
                Menu->Request_ApplySource(MissingEnabledBinding, Style));
            TestTrue(TEXT("enabled-binding removal retains complete accepted UI and diagnostic"),
                RejectedWithoutMutation());
            const FString UnsupportedStyle = Style + TEXT("\n.game-button { transform: rotate(5deg); }\n");
            TestFalse(TEXT("unsupported CSS is rejected through the host"),
                Menu->Request_ApplySource(Markup, UnsupportedStyle));
            TestTrue(TEXT("unsupported CSS retains complete accepted UI and diagnostic"), RejectedWithoutMutation());

            const float ResumeYBeforeStyleEdit = Resume->GetCachedGeometry().GetAbsolutePosition().Y;
            const float ResumeScaleBeforeStyleEdit = Resume->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
            const FString EditedStyle = Style.Replace(TEXT(".divider { height: 2px;"), TEXT(".divider { height: 6px;"));
            if (NOT TestTrue(TEXT("style fixture changes a supported divider height"), EditedStyle != Style)) { return; }
            TestTrue(TEXT("supported authored style edit succeeds through the host"),
                Menu->Request_ApplySource(Markup, EditedStyle));
            TestTrue(TEXT("accepted reload clears source diagnostic"), Menu->Get_SourceError().IsEmpty());
            TestTrue(TEXT("valid reload advances view revision"), View->GetRevision() > RevisionBefore);
            Resume = View->GetButton(TEXT("resume-button"));
            Settings = View->GetButton(TEXT("settings-button"));
            Return = View->GetButton(TEXT("return-button"));
            if (NOT TestTrue(TEXT("reload remounts all three actions"), Resume.IsValid()
                && Settings.IsValid() && Return.IsValid())) { return; }
            Tick(Slate);
            TestTrue(TEXT("supported style edit changes mounted Yoga geometry"), FMath::IsNearlyEqual(
                Resume->GetCachedGeometry().GetAbsolutePosition().Y - ResumeYBeforeStyleEdit,
                4.0f * ResumeScaleBeforeStyleEdit, 0.25f));
            TestTrue(TEXT("style edit restores logical Resume focus"), FocusContains(Slate, User, Resume.ToSharedRef()));
            TestTrue(TEXT("reference stylesheet restores through the host"), Menu->Request_ApplySource(Markup, Style));
            Resume = View->GetButton(TEXT("resume-button"));
            Settings = View->GetButton(TEXT("settings-button"));
            Return = View->GetButton(TEXT("return-button"));
            if (NOT TestTrue(TEXT("reference style remounts all three actions"), Resume.IsValid()
                && Settings.IsValid() && Return.IsValid())) { return; }
            Tick(Slate);
            TestTrue(TEXT("restored stylesheet restores mounted geometry"), FMath::IsNearlyEqual(
                Resume->GetCachedGeometry().GetAbsolutePosition().Y, ResumeYBeforeStyleEdit, 0.25f));

            Slate.SetUserFocus(User, Return.ToSharedRef(), EFocusCause::SetDirectly);
            Tick(Slate);
            TestTrue(TEXT("Return receives keyboard focus"), FocusContains(Slate, User, Return.ToSharedRef()));
            TestTrue(TEXT("Enter routes to focused Return action"), PressKey(Slate, EKeys::Enter, User));
            TestEqual(TEXT("Return action fires exactly once"), Menu->Get_ActionCount(ReturnAction), 1);
            TestEqual(TEXT("Settings action remains once after reload"), Menu->Get_ActionCount(SettingsAction), 1);
            Slate.SetUserFocus(User, Settings.ToSharedRef(), EFocusCause::SetDirectly);
            Tick(Slate);
            TestTrue(TEXT("Settings receives keyboard focus for Space"),
                FocusContains(Slate, User, Settings.ToSharedRef()));
            TestTrue(TEXT("Space routes to focused Settings action"),
                PressKey(Slate, EKeys::SpaceBar, User));
            TestEqual(TEXT("Settings action fires a second time from Space"),
                Menu->Get_ActionCount(SettingsAction), 2);
            TestEqual(TEXT("Space opens nested Settings"), Menu->Get_Screen(),
                ECk_UiAuthoringProof_Screen::Settings);
            TestTrue(TEXT("Escape returns to Pause after Space"), PressKey(Slate, EKeys::Escape, User)
                && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Pause && Menu->IsActivated());
            TestTrue(TEXT("second old Pause view releases interactions"), View->IsOwnerReleased());
            View = Menu->Get_AuthoredView();
            Resume = View.IsValid() ? View->GetButton(TEXT("resume-button")) : nullptr;
            Settings = View.IsValid() ? View->GetButton(TEXT("settings-button")) : nullptr;
            Return = View.IsValid() ? View->GetButton(TEXT("return-button")) : nullptr;
            if (NOT TestTrue(TEXT("Pause actions remount after second Settings visit"), View.IsValid()
                && Resume.IsValid() && Settings.IsValid() && Return.IsValid())) { return; }

            TSharedPtr<SButton> StaleResume = Resume;
            {
                const TSharedRef<FReleaseSpy> ReleaseSpy = MakeShared<FReleaseSpy>();
                const bool SpyRegistered = Slate.RegisterInputPreProcessor(ReleaseSpy, 1);
                TestTrue(TEXT("lower-priority pointer release observer registers"), SpyRegistered);
                const auto UnregisterReleaseSpy = ScopeExitSupport::FScopeGuardSyntaxSupport() + [&]()
                {
                    if (SpyRegistered) { Slate.UnregisterInputPreProcessor(ReleaseSpy); }
                };
                TestTrue(TEXT("Resume is targeted before held pointer press"),
                    MovePointerTo(Slate, Resume.ToSharedRef(), User));
                const FGeometry ResumeGeometry = Resume->GetCachedGeometry();
                const FVector2D PressPosition = ResumeGeometry.LocalToAbsolute(
                    ResumeGeometry.GetLocalSize() * 0.5f);
                const TSet<FKey> LeftDown{EKeys::LeftMouseButton};
                const TSet<FKey> NoButtons;
                const FPointerEvent DownEvent(User, FSlateApplication::CursorPointerIndex,
                    PressPosition, PressPosition, LeftDown, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
                const FPointerEvent UpEvent(User, FSlateApplication::CursorPointerIndex,
                    PressPosition, PressPosition, NoButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
                TestTrue(TEXT("Resume receives routed pointer down"),
                    Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), DownEvent));
                TestTrue(TEXT("held Resume owns pointer capture"), Resume->HasMouseCapture());
                TestEqual(TEXT("held press has not dispatched Resume"), Menu->Get_ActionCount(ResumeAction), 0);
                Menu->Request_Close();
                TestTrue(TEXT("held pointer release is consumed after close"),
                    Slate.ProcessMouseButtonUpEvent(UpEvent));
                TestEqual(TEXT("lower-priority observer receives no held release"), ReleaseSpy->MouseUps, 0);
                TestEqual(TEXT("held release cannot dispatch old Resume"), Menu->Get_ActionCount(ResumeAction), 0);
            }
            Tick(Slate);
            TestFalse(TEXT("close restores unpaused state"), UGameplayStatics::IsGamePaused(InWorld));
            TestTrue(TEXT("host-owned fixture layout remains for reopen"), LayoutSubsystem->Has_Layout());
            if (UCk_UI_PrimaryGameLayout_UE* Layout = LayoutSubsystem->Get_Layout())
            { TestEqual(TEXT("closed menu restores GameOnly input"), Layout->Get_EffectiveInputMode(), ECk_UI_InputMode::GameOnly); }
            const int32 ResumeBeforeStaleClick = Menu->Get_ActionCount(ResumeAction);
            StaleResume->SimulateClick();
            TestEqual(TEXT("retained stale control cannot call closed owner"),
                Menu->Get_ActionCount(ResumeAction), ResumeBeforeStaleClick);

            TStrongObjectPtr<UCk_PauseMenuProof_UE> Reopened{UCk_PauseMenuProof_UE::Open(Player)};
            if (NOT TestNotNull(TEXT("screen reopens on retained production layout"), Reopened.Get())) { return; }
            ActiveMenu = Reopened.Get();
            TestTrue(TEXT("reopen uses a fresh screen owner"), Reopened.Get() != Menu.Get());
            TSharedPtr<FCkUiView> ReopenedView = Reopened->Get_AuthoredView();
            TestTrue(TEXT("reopen uses a fresh authored view"), ReopenedView.IsValid() && ReopenedView != View);
            if (NOT ReopenedView.IsValid()) { return; }
            TSharedPtr<SButton> ReopenedResume = ReopenedView->GetButton(TEXT("resume-button"));
            if (NOT TestTrue(TEXT("reopen remounts Resume"), ReopenedResume.IsValid())) { return; }
            TestTrue(TEXT("reopen owns paused state"), UGameplayStatics::IsGamePaused(InWorld));
            Tick(Slate);
            TestTrue(TEXT("reopen restores initial focus"),
                FocusContains(Slate, User, ReopenedResume.ToSharedRef()));
            TestTrue(TEXT("Enter routes to focused Resume action"), PressKey(Slate, EKeys::Enter, User));
            TestEqual(TEXT("Resume action fires exactly once"), Reopened->Get_ActionCount(ResumeAction), 1);
            TestFalse(TEXT("Resume closes and restores pause"), UGameplayStatics::IsGamePaused(InWorld));
            TestEqual(TEXT("old screen has no new Resume action"), Menu->Get_ActionCount(ResumeAction), ResumeBeforeStaleClick);
            TStrongObjectPtr<UCk_PauseMenuProof_UE> EscapeMenu{UCk_PauseMenuProof_UE::Open(Player)};
            if (NOT TestNotNull(TEXT("screen reopens for Escape route"), EscapeMenu.Get())) { return; }
            ActiveMenu = EscapeMenu.Get();
            Tick(Slate);
            {
                const TSharedRef<FReleaseSpy> ReleaseSpy = MakeShared<FReleaseSpy>();
                const bool SpyRegistered = Slate.RegisterInputPreProcessor(ReleaseSpy, 1);
                TestTrue(TEXT("lower-priority keyboard release observer registers"), SpyRegistered);
                const auto UnregisterReleaseSpy = ScopeExitSupport::FScopeGuardSyntaxSupport() + [&]()
                {
                    if (SpyRegistered) { Slate.UnregisterInputPreProcessor(ReleaseSpy); }
                };
                const uint32 SlateUser = static_cast<uint32>(User);
                TestTrue(TEXT("Escape down routes to Resume and closes the screen"),
                    Slate.ProcessKeyDownEvent(FKeyEvent{EKeys::Escape, FModifierKeysState{}, SlateUser, false, 0, 0}));
                TestFalse(TEXT("Escape down closes before held-key release"), EscapeMenu->IsActivated());
                TestTrue(TEXT("held Escape repeat is consumed after close"),
                    Slate.ProcessKeyDownEvent(FKeyEvent{EKeys::Escape, FModifierKeysState{}, SlateUser, true, 0, 0}));
                TestEqual(TEXT("lower-priority observer receives no held-key repeat"), ReleaseSpy->KeyRepeats, 0);
                TestTrue(TEXT("held Escape up is consumed after close"),
                    Slate.ProcessKeyUpEvent(FKeyEvent{EKeys::Escape, FModifierKeysState{}, SlateUser, false, 0, 0}));
                TestEqual(TEXT("lower-priority observer receives no held-key release"), ReleaseSpy->KeyUps, 0);
            }
            Tick(Slate);
            TestEqual(TEXT("Escape requests Resume once"), EscapeMenu->Get_ActionCount(ResumeAction), 1);
            TestFalse(TEXT("Escape restores unpaused state"), UGameplayStatics::IsGamePaused(InWorld));
            TestOwnedPause = Player->SetPause(true);
            TestTrue(TEXT("test can establish preexisting pause"), TestOwnedPause
                && UGameplayStatics::IsGamePaused(InWorld));
            TStrongObjectPtr<UCk_PauseMenuProof_UE> AlreadyPausedMenu{UCk_PauseMenuProof_UE::Open(Player)};
            if (NOT TestNotNull(TEXT("screen opens while world was already paused"), AlreadyPausedMenu.Get()))
            { return; }
            ActiveMenu = AlreadyPausedMenu.Get();
            TestTrue(TEXT("Escape closes pre-paused menu"), PressKey(Slate, EKeys::Escape, User));
            TestEqual(TEXT("pre-paused Escape records one Resume request"),
                AlreadyPausedMenu->Get_ActionCount(ResumeAction), 1);
            TestTrue(TEXT("close preserves pause it did not acquire"), UGameplayStatics::IsGamePaused(InWorld));
            TestTrue(TEXT("test releases its own preexisting pause"), Player->SetPause(false));
            TestOwnedPause = false;
            TestFalse(TEXT("pause cleanup restores unpaused state"), UGameplayStatics::IsGamePaused(InWorld));
            LayoutSubsystem->DestroyLayout();
            TestFalse(TEXT("test releases owned production layout"), LayoutSubsystem->Has_Layout());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
