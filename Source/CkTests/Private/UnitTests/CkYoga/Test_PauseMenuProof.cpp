#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/UI/CkPauseMenuProof.h"
#include "CkUiOwnedSession_TestOwner.h"
#include "CkUiAuthoredSession_TestListener.h"
#include "CkUiLayerMembership_TestScenarios.h"

#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"
#include "CkUI/Layout/CkUI_LayoutConfigAsset.h"
#include "CkUI/Layout/CkUI_PrimaryGameLayout.h"
#include "CkUI/Layout/CkUI_LayerStack.h"
#include "CkScriptedPause_TestAdapter.h"

#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "GameMapsSettings.h"
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
#include "Tests/AutomationCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/Application/SlateUser.h"
#include "Containers/Ticker.h"
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
        TSharedPtr<ck_scripted_pause_test::FMenu> Menu;
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

    auto AdvanceRetirement(FSlateApplication& InSlate) -> void
    {
        FTSTicker::GetCoreTicker().Tick(1.0f / 60.0f);
        Tick(InSlate);
        FTSTicker::GetCoreTicker().Tick(1.0f / 60.0f);
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

namespace ck_owned_session_proof_test
{
    struct FContext
    {
        TWeakObjectPtr<UGameInstance> Instance;
        TWeakObjectPtr<ULocalPlayer> Player;
        TWeakObjectPtr<UGameViewportClient> Viewport;
        TWeakObjectPtr<UGameMapsSettings> MapsSettings;
        TSharedPtr<FSlateUser> User;
        bool CreatedUser = false;
        bool SavedViewportSettings = false;
        bool PreviousForceDisabled = false;
        bool PreviousShowAllWidgets = false;

        ~FContext() { Cleanup(); }

        auto Cleanup() -> void
        {
            auto* GameInstance = Instance.Get();
            auto* LocalPlayer = Player.Get();
            if (GameInstance != nullptr && LocalPlayer != nullptr && GameInstance->GetLocalPlayers().Contains(LocalPlayer))
            { GameInstance->RemoveLocalPlayer(LocalPlayer); }
            Player.Reset();
            Instance.Reset();
            if (SavedViewportSettings)
            {
                SavedViewportSettings = false;
                if (auto* Settings = MapsSettings.Get())
                { Settings->bShowAllPlayerWidgetsWhenSplitscreenDisabled = PreviousShowAllWidgets; }
                if (auto* GameViewport = Viewport.Get())
                { GameViewport->SetForceDisableSplitscreen(PreviousForceDisabled); }
            }
            MapsSettings.Reset();
            Viewport.Reset();
            if (CreatedUser && User.IsValid() && FSlateApplication::IsInitialized()
                && FSlateApplication::Get().GetUser(1) == User)
            { FSlateApplication::Get().UnregisterUser(1); }
            User.Reset();
            CreatedUser = false;
        }
    };

    template <typename TWidget>
    auto Find(const TSharedRef<SWidget>& InRoot, const FString& InType) -> TSharedPtr<TWidget>
    {
        if (InRoot->GetTypeAsString() == InType) { return StaticCastSharedRef<TWidget>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = Find<TWidget>(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InType);
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto PreparePlayer(FAutomationTestBase& Test, UWorld* World, FContext& Context) -> void
    {
        auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        if (!Test.TestNotNull(TEXT("owned session real PIE game instance"), Instance)) { return; }
        if (!Test.TestFalse(TEXT("owned player admission runs in normally ticking unpaused world"), World->IsPaused())) { return; }
        Context.Instance = Instance;
        auto* Viewport = Instance->GetGameViewportClient();
        auto* Settings = GetMutableDefault<UGameMapsSettings>();
        if (!Test.TestNotNull(TEXT("owned renderer fixture has real viewport"), Viewport)
            || !Test.TestNotNull(TEXT("owned renderer fixture has game maps settings"), Settings)) { return; }
        Context.Viewport = Viewport;
        Context.MapsSettings = Settings;
        Context.PreviousForceDisabled = Viewport->IsSplitscreenForceDisabled();
        Context.PreviousShowAllWidgets = Settings->bShowAllPlayerWidgetsWhenSplitscreenDisabled;
        Context.SavedViewportSettings = true;
        Settings->bShowAllPlayerWidgetsWhenSplitscreenDisabled = true;
        Viewport->SetForceDisableSplitscreen(true);
        auto& Slate = FSlateApplication::Get();
        Context.CreatedUser = !Slate.GetUser(1).IsValid();
        if (Context.CreatedUser)
        {
            // Public input processing registers a hardware user before routing its first event.
            const FKeyEvent Bootstrap{EKeys::F12, FModifierKeysState{}, 1u, false, 0, 0};
            Slate.ProcessKeyDownEvent(Bootstrap);
            Slate.ProcessKeyUpEvent(Bootstrap);
        }
        Context.User = Slate.GetUser(1);
        FString PlayerError;
        Context.Player = Instance->CreateLocalPlayer(1, PlayerError, true);
        if (!Test.TestNotNull(TEXT("second real local player spawns"), Context.Player.Get()))
        {
            if (!PlayerError.IsEmpty()) { Test.AddError(PlayerError); }
            Context.Cleanup();
        }
    }

    auto Run(FAutomationTestBase& Test, UWorld* World, FContext& Context) -> void
    {
        using namespace ck_pause_menu_proof_test;
        auto& Slate = FSlateApplication::Get();
        ULocalPlayer* LocalPlayer = Context.Player.Get();
        const auto UserRef = Context.User;
        TStrongObjectPtr<UCk_UiOwnedSession_TestOwner_UE> Owner;
        TStrongObjectPtr<UCk_UiOwnedSession_TestOwner_UE> Cover;
        UCk_UI_Layout_Subsystem_UE* Layout = nullptr;
        ON_SCOPE_EXIT
        {
            if (IsValid(Owner.Get())) { Owner->Shutdown(); }
            if (IsValid(Cover.Get())) { Cover->Shutdown(); }
            if (IsValid(Layout)) { Layout->DestroyLayout(); }
            Context.Cleanup();
        };
        if (!IsValid(LocalPlayer)) { return; }
        if (!Test.TestTrue(TEXT("owned scenarios retain the exact admitted PIE context"), IsValid(World)
            && LocalPlayer->GetWorld() == World && World->GetGameInstance() == Context.Instance.Get())) { return; }
        if (!Test.TestTrue(TEXT("owned renderer fixture disables second scene view while preserving all player UI"),
            Context.SavedViewportSettings && Context.Viewport.IsValid() && Context.MapsSettings.IsValid()
            && Context.Viewport->IsSplitscreenForceDisabled()
            && Context.MapsSettings->bShowAllPlayerWidgetsWhenSplitscreenDisabled)) { return; }
        auto* Controller = LocalPlayer->GetPlayerController(World);
        if (!Test.TestNotNull(TEXT("second local player owns a real controller"), Controller)) { return; }
        const auto User = LocalPlayer->GetSlateUser();
        if (!Test.TestTrue(TEXT("second local player resolves the registered nonzero Slate user"),
            User.IsValid() && User == UserRef && User->GetUserIndex() > 0)) { return; }
        const int32 UserIndex = User->GetUserIndex();
        const auto PreviousZeroFocus = Slate.GetUserFocusedWidget(0);
        Layout = LocalPlayer->GetSubsystem<UCk_UI_Layout_Subsystem_UE>();
        if (!Test.TestNotNull(TEXT("second player owns production CkUI layout subsystem"), Layout)) { return; }
        auto* LayoutConfig = LoadObject<UCk_UI_LayoutConfigAsset_UE>(nullptr,
            TEXT("/Script/AngelscriptAssets.CkTests_PauseMenuProof_Layout"));
        if (!Test.TestNotNull(TEXT("owned proof reuses source-authored instant layout"), LayoutConfig)) { return; }
        Layout->CreateLayout(LayoutConfig);
        const auto LayerTag = FGameplayTag::RequestGameplayTag(TEXT("CkTests.UI.Layer.PauseMenuProof"));
        if (!Test.TestTrue(TEXT("second player creates the production layer"), Layout->HasLayer(LayerTag))) { return; }
        Owner.Reset(CreateWidget<UCk_UiOwnedSession_TestOwner_UE>(Controller));
        Cover.Reset(CreateWidget<UCk_UiOwnedSession_TestOwner_UE>(Controller));
        if (!Test.TestTrue(TEXT("real UMG activatable fixtures create"), IsValid(Owner.Get()) && IsValid(Cover.Get()))) { return; }
        FCk_UI_AuthoredSession_Config Config;
        Config.Set_Text({FCk_UI_AuthoredText{TEXT("label"), FText::FromString(TEXT("Initial"))}});
        Config.Set_Number({FCk_UI_AuthoredNumber{TEXT("amount"), 20.0f}});
        Config.Set_Bool({FCk_UI_AuthoredBool{TEXT("flag"), false}, FCk_UI_AuthoredBool{TEXT("nextEnabled"), true}});
        Config.Set_Actions({TEXT("fire")});
        const FString Markup = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
            "<search id=\"query\" bind=\"label\"/>"
            "<slider id=\"amount-control\" class=\"slider\" value-bind=\"amount\" interaction=\"amount\" min=\"0\" max=\"100\" step=\"10\"/>"
            "<checkbox id=\"flag-control\" value-bind=\"flag\" changed=\"flag\" label-bind=\"label\"/>"
            "<button id=\"first\" class=\"button\" action=\"fire\">First</button>"
            "<button id=\"second\" class=\"button\" action=\"fire\" enabled-bind=\"nextEnabled\">Second</button>"
            "<button id=\"third\" class=\"button\" action=\"fire\">Third</button></column></region></ui>");
        const FString Style = TEXT(".slider { width: 300px; height: 32px; } .button { height: 48px; }");
        TArray<FString> Errors;
        {
            TStrongObjectPtr<UCk_UiOwnedSession_TestOwner_UE> UnboundOwner{NewObject<UCk_UiOwnedSession_TestOwner_UE>()};
            TStrongObjectPtr<UCk_UI_AuthoredSession_UE> UnboundSession{NewObject<UCk_UI_AuthoredSession_UE>()};
            auto UnboundConfig = Config;
            UnboundConfig.Set_InteractionOwner(UnboundOwner.Get());
            Test.TestFalse(TEXT("owner without a real local-player context rejects admission"),
                UnboundSession->TryInitialize(UnboundConfig, Errors));
            Test.TestTrue(TEXT("invalid owner admission publishes no view"), !Errors.IsEmpty()
                && !UnboundSession->Get_IsUsable() && !UnboundSession->Get_View().IsValid());
            UnboundSession->Request_Release();
        }
        Test.TestFalse(TEXT("failed fixture reload rolls back preparation"), Cover->Prepare(Config, TEXT("<ui>"), Style, Errors));
        Test.TestTrue(TEXT("failed fixture publishes no runnable session or surface"),
            Cover->Session == nullptr && Cover->Surface == nullptr && !Errors.IsEmpty());
        if (!Test.TestTrue(TEXT("owned session admits and mounts before activation"), Owner->Prepare(Config, Markup, Style, Errors)))
        { for (const FString& Error : Errors) { Test.AddError(Error); } return; }
        auto* Session = Owner->Session;
        if (!Test.TestTrue(TEXT("covered fixture prepares after rollback"), Cover->Prepare(Config, Markup, Style, Errors)))
        { for (const FString& Error : Errors) { Test.AddError(Error); } return; }
        {
            TStrongObjectPtr<UCk_UI_AuthoredSession_UE> ForeignMountSession{NewObject<UCk_UI_AuthoredSession_UE>()};
            auto OwnedConfig = Config;
            OwnedConfig.Set_InteractionOwner(Owner.Get());
            if (!Test.TestTrue(TEXT("fresh owned candidate admits before first mount"),
                ForeignMountSession->TryInitialize(OwnedConfig, Errors) && ForeignMountSession->TryReload(Markup, Style, Errors))) { return; }
            const auto ForeignView = ForeignMountSession->Get_View();
            const auto CoverView = Cover->Session->Get_View();
            Test.TestFalse(TEXT("foreign-owner surface rejects a first mount"),
                ForeignMountSession->TryMount(Cover->Surface, TEXT("main"), Errors));
            Test.TestTrue(TEXT("foreign surface rejection preserves both accepted views and surface"), !Errors.IsEmpty()
                && ForeignMountSession->Get_View() == ForeignView && Cover->Session->Get_View() == CoverView
                && Cover->Surface->IsConfiguredWith(CoverView) && !ForeignView->IsOwnerReleased());
            ForeignMountSession->Request_Release();
        }
        TStrongObjectPtr<UCk_UiAuthoredSession_TestListener_UE> Listener{NewObject<UCk_UiAuthoredSession_TestListener_UE>()};
        Listener->Session = Session;
        Session->OnAction.AddDynamic(Listener.Get(), &UCk_UiAuthoredSession_TestListener_UE::HandleAction);
        Session->OnTextChanged.AddDynamic(Listener.Get(), &UCk_UiAuthoredSession_TestListener_UE::HandleText);
        Session->OnNumberChanged.AddDynamic(Listener.Get(), &UCk_UiAuthoredSession_TestListener_UE::HandleNumber);
        Session->OnBoolChanged.AddDynamic(Listener.Get(), &UCk_UiAuthoredSession_TestListener_UE::HandleBool);
        const auto View = Session->Get_View();
        if (!Test.TestTrue(TEXT("owned session publishes usable inactive view"), View.IsValid()
            && Session->Get_IsUsable() && !Session->Get_IsInputActive() && !Session->TryBeginInput())) { return; }
        auto First = View->GetButton(TEXT("first"));
        if (!Test.TestTrue(TEXT("owned authored buttons exist"), First.IsValid()
            && View->GetButton(TEXT("second")).IsValid() && View->GetButton(TEXT("third")).IsValid())) { return; }
        First->SimulateClick();
        Test.TestEqual(TEXT("prepush owner gates action"), Listener->ActionCalls, 0);
        Test.TestTrue(TEXT("inactive owner model remains mutable"), Session->TrySetText(TEXT("label"), FText::FromString(TEXT("Inactive"))));
        if (!Test.TestTrue(TEXT("production stack accepts the owned session widget"),
            Layout->PushWidgetToLayer_Instance(LayerTag, Owner.Get()) == Owner.Get())) { return; }
        Tick(Slate);
        const bool Activated = Test.TestTrue(TEXT("stack activates owned widget"), Owner->IsActivated());
        const bool Usable = Test.TestTrue(TEXT("stack retains usable owned session"), Session->Get_IsUsable());
        const bool Configured = Test.TestTrue(TEXT("stack retains exact configured surface view"),
            IsValid(Owner->Surface) && Owner->Surface->IsConfiguredWith(View));
        const bool RetainedView = Test.TestTrue(TEXT("stack retains accepted native view without retirement"),
            Session->Get_View() == View && !View->IsOwnerReleased());
        const bool InputActive = Test.TestTrue(TEXT("activation begins admitted user input"), Session->Get_IsInputActive());
        Test.TestEqual(TEXT("activation preserves admitted nonzero Slate user"), Session->Get_SlateUserIndex(), UserIndex);
        if (!Activated || !Usable || !Configured || !RetainedView || !InputActive) { return; }
        if (!Test.TestTrue(TEXT("owner can focus current ordinary button by ID"), Session->TryFocusButton(TEXT("first")))) { return; }
        Test.TestTrue(TEXT("owned focus query identifies only the currently focused ordinary button"),
            Session->Get_IsButtonFocused(TEXT("first"))
                && !Session->Get_IsButtonFocused(TEXT("third"))
                && !Session->Get_IsButtonFocused(TEXT("missing"))
                && !Session->Get_IsButtonFocused(TEXT("query")));
        Test.TestTrue(TEXT("owner-user Enter reaches reflected action"), PressKey(Slate, EKeys::Enter, UserIndex));
        Test.TestEqual(TEXT("owner-user action dispatches once"), Listener->ActionCalls, 1);
        const auto Root = View->GetRegion(TEXT("main"));
        const auto Search = Find<SSearchBox>(Root, TEXT("SSearchBox"));
        const auto Slider = Find<SSlider>(Root, TEXT("SCkUiSlider"));
        const auto Checkbox = Find<SCheckBox>(Root, TEXT("SCheckBox"));
        if (!Test.TestTrue(TEXT("owned session realizes all scalar controls"), Search.IsValid() && Slider.IsValid() && Checkbox.IsValid())) { return; }
        int32 ProposalAttempt = 0;
        const auto Propose = [&]()
        {
            First->SimulateClick();
            Search->SetText(FText::FromString(FString::Printf(TEXT("Proposed %d"), ++ProposalAttempt)));
            Slider->OnNavigation(Slider->GetCachedGeometry(), FNavigationEvent(FModifierKeysState{}, UserIndex,
                EUINavigation::Right, ENavigationGenesis::Keyboard));
            const FKeyEvent Space{EKeys::SpaceBar, FModifierKeysState{}, static_cast<uint32>(UserIndex), false, 0, 0};
            Checkbox->OnKeyDown(Checkbox->GetCachedGeometry(), Space);
            Checkbox->OnKeyUp(Checkbox->GetCachedGeometry(), Space);
        };
        if (!Test.TestTrue(TEXT("second production stack widget covers owner"),
            Layout->PushWidgetToLayer_Instance(LayerTag, Cover.Get()) == Cover.Get())) { return; }
        Tick(Slate);
        const int64 Revision = Session->Get_Revision();
        Propose();
        FText Value;
        Test.TestTrue(TEXT("cover suspends all callbacks without retiring model or view"), !Owner->IsActivated()
            && !Session->Get_IsInputActive() && Session->Get_IsUsable() && Session->Get_View() == View
            && Session->Get_Revision() == Revision && !View->IsOwnerReleased()
            && Session->TryGetText(TEXT("label"), Value) && Value.ToString() == TEXT("Inactive")
            && Listener->ActionCalls == 1 && Listener->TextCalls == 0 && Listener->NumberCalls == 0 && Listener->BoolCalls == 0);
        Test.TestFalse(TEXT("covered owner has no currently focused ordinary button"),
            Session->Get_IsButtonFocused(TEXT("first")));
        Test.TestFalse(TEXT("covered owner cannot focus or begin input"), Session->TryFocusButton(TEXT("first")) || Session->TryBeginInput());
        Test.TestTrue(TEXT("covered owner can change accepted model"), Session->TrySetNumber(TEXT("amount"), 30.0f));
        Layout->PopWidgetFromLayer(LayerTag);
        Tick(Slate);
        Test.TestTrue(TEXT("uncover resumes same model and view"), Owner->IsActivated() && Session->Get_IsInputActive()
            && Session->Get_View() == View && Session->Get_Revision() == Revision);
        Propose();
        Test.TestTrue(TEXT("uncovered owner resumes every scalar proposal"), Listener->ActionCalls == 2
            && Listener->TextCalls == 1 && Listener->NumberCalls == 1 && Listener->BoolCalls == 1);
        Test.TestTrue(TEXT("owner focus changes preserve user zero focus"), Slate.GetUserFocusedWidget(0) == PreviousZeroFocus);

        Session->TryFocusButton(TEXT("first"));
        const auto BeforeInvalidFocus = Slate.GetUserFocusedWidget(UserIndex);
        Test.TestFalse(TEXT("unknown focus ID rejects"), Session->TryFocusButton(TEXT("missing")));
        Test.TestFalse(TEXT("duplicate navigation IDs reject atomically"), Session->TryNavigateButtons({TEXT("first"), TEXT("first")}, 1));
        Test.TestFalse(TEXT("unknown navigation IDs reject atomically"), Session->TryNavigateButtons({TEXT("first"), TEXT("missing")}, 1));
        Test.TestFalse(TEXT("nonbutton navigation IDs reject atomically"), Session->TryNavigateButtons({TEXT("first"), TEXT("query")}, 1));
        Test.TestFalse(TEXT("invalid navigation direction rejects atomically"), Session->TryNavigateButtons({TEXT("first"), TEXT("third")}, 0));
        Test.TestFalse(TEXT("empty navigation set rejects atomically"), Session->TryNavigateButtons({}, 1));
        Test.TestTrue(TEXT("all navigation rejections preserve focus"), Slate.GetUserFocusedWidget(UserIndex) == BeforeInvalidFocus);
        Session->TrySetBool(TEXT("nextEnabled"), false);
        Tick(Slate);
        Test.TestTrue(TEXT("navigation skips disabled current button"), Session->TryNavigateButtons(
            {TEXT("first"), TEXT("second"), TEXT("third")}, 1)
            && FocusContains(Slate, UserIndex, View->GetButton(TEXT("third")).ToSharedRef()));
        Test.TestTrue(TEXT("navigation wraps backward over disabled button"), Session->TryNavigateButtons(
            {TEXT("first"), TEXT("second"), TEXT("third")}, -1) && FocusContains(Slate, UserIndex, First.ToSharedRef()));
        {
            const auto Target = View->GetButton(TEXT("third"));
            auto* GameViewport = World->GetGameViewport();
            const auto External = IsValid(GameViewport) ? GameViewport->GetGameViewportWidget() : nullptr;
            if (!Test.TestTrue(TEXT("focus redirection uses mounted external PIE viewport"), External.IsValid())) { return; }
            bool Redirected = false;
            const FDelegateHandle FocusHandle = Slate.OnFocusChanging().AddLambda(
                [&](const FFocusEvent& Event, const FWeakWidgetPath&, const TSharedPtr<SWidget>&,
                    const FWidgetPath&, const TSharedPtr<SWidget>& NewFocus)
                {
                    if (!Redirected && Event.GetUser() == UserIndex && NewFocus == Target)
                    {
                        Redirected = true;
                        Slate.SetUserFocus(UserIndex, External, EFocusCause::SetDirectly);
                    }
                });
            ON_SCOPE_EXIT { Slate.OnFocusChanging().Remove(FocusHandle); };
            Test.TestFalse(TEXT("navigation reports interrupted target focus"), Session->TryNavigateButtons(
                {TEXT("first"), TEXT("second"), TEXT("third")}, 1));
            Test.TestTrue(TEXT("focus callback wins without fallback navigation or owner retirement"), Redirected
                && Slate.GetUserFocusedWidget(UserIndex) == External && Session->Get_IsInputActive()
                && Session->Get_View() == View && Session->Get_Revision() == Revision && !View->IsOwnerReleased());
        }
        if (!Test.TestTrue(TEXT("owner focuses current button after callback unsubscribes"), Session->TryFocusButton(TEXT("first")))) { return; }
        {
            const auto Target = View->GetButton(TEXT("third"));
            FString CallerId = TEXT("third");
            bool Mutated = false;
            const FDelegateHandle Handle = Slate.OnFocusChanging().AddLambda(
                [&](const FFocusEvent& Event, const FWeakWidgetPath&, const TSharedPtr<SWidget>&,
                    const FWidgetPath&, const TSharedPtr<SWidget>& NewFocus)
                {
                    if (Event.GetUser() == UserIndex && NewFocus == Target)
                    {
                        Mutated = true;
                        CallerId = TEXT("missing");
                    }
                });
            ON_SCOPE_EXIT { Slate.OnFocusChanging().Remove(Handle); };
            Test.TestTrue(TEXT("focus snapshots caller ID before focus callback mutation"), Session->TryFocusButton(CallerId));
            Test.TestTrue(TEXT("mutated caller ID cannot replace accepted focus target"), Mutated && CallerId == TEXT("missing")
                && FocusContains(Slate, UserIndex, Target.ToSharedRef()) && Session->Get_IsInputActive()
                && Session->Get_View() == View && Session->Get_Revision() == Revision);
        }
        if (!Test.TestTrue(TEXT("focus mutation case restores first after unsubscribing"), Session->TryFocusButton(TEXT("first")))) { return; }
        {
            const auto Target = View->GetButton(TEXT("third"));
            TArray<FString> CallerIds{TEXT("first"), TEXT("second"), TEXT("third")};
            bool Mutated = false;
            const FDelegateHandle Handle = Slate.OnFocusChanging().AddLambda(
                [&](const FFocusEvent& Event, const FWeakWidgetPath&, const TSharedPtr<SWidget>&,
                    const FWidgetPath&, const TSharedPtr<SWidget>& NewFocus)
                {
                    if (Event.GetUser() == UserIndex && NewFocus == Target)
                    {
                        Mutated = true;
                        CallerIds.Reset();
                        CallerIds.Add(TEXT("missing"));
                    }
                });
            ON_SCOPE_EXIT { Slate.OnFocusChanging().Remove(Handle); };
            Test.TestTrue(TEXT("navigation snapshots caller IDs before callback resets array"), Session->TryNavigateButtons(CallerIds, 1));
            Test.TestTrue(TEXT("reset caller array cannot replace accepted navigation target"), Mutated
                && CallerIds.Num() == 1 && CallerIds[0] == TEXT("missing")
                && FocusContains(Slate, UserIndex, Target.ToSharedRef()) && Session->Get_IsInputActive()
                && Session->Get_View() == View && Session->Get_Revision() == Revision);
        }
        if (!Test.TestTrue(TEXT("navigation mutation case restores first after unsubscribing"), Session->TryFocusButton(TEXT("first")))) { return; }
        const auto OldFirst = First;
        Test.TestTrue(TEXT("accepted owned reload advances native revision"), Session->TryReload(Markup, Style, Errors)
            && Session->Get_Revision() == Revision + 1);
        First = View->GetButton(TEXT("first"));
        if (!Test.TestTrue(TEXT("accepted reload replaces ordinary button"), First.IsValid() && First != OldFirst)) { return; }
        Tick(Slate);
        Test.TestTrue(TEXT("accepted reload restores owner-user focus by stable ID"), FocusContains(Slate, UserIndex, First.ToSharedRef()));
        Test.TestFalse(TEXT("owned malformed reload rejects"), Session->TryReload(TEXT("<ui>"), Style, Errors));
        Test.TestTrue(TEXT("rejected reload retains revision current button and focus"), Session->Get_Revision() == Revision + 1
            && View->GetButton(TEXT("first")) == First && FocusContains(Slate, UserIndex, First.ToSharedRef()));

        auto* GameViewport = World->GetGameViewport();
        const TSharedPtr<SViewport> OutsideFocus = IsValid(GameViewport)
            ? GameViewport->GetGameViewportWidget() : nullptr;
        if (!Test.TestTrue(TEXT("silent end-input fixture has the real outside PIE viewport"), OutsideFocus.IsValid())) { return; }
        Session->Request_EndInput();
        Slate.SetUserFocus(UserIndex, OutsideFocus.ToSharedRef(), EFocusCause::SetDirectly);
        Tick(Slate);
        if (!Test.TestTrue(TEXT("outside focus is established before owned input admission"),
            Slate.GetUserFocusedWidget(UserIndex) == OutsideFocus)) { return; }
        if (!Test.TestTrue(TEXT("owned input readmits over restorable outside focus"),
            Session->TryBeginInput() && Session->TryFocusButton(TEXT("first")))) { return; }
        Tick(Slate);
        const TSharedPtr<SWidget> OwnerFocusBeforeSilentEnd = Slate.GetUserFocusedWidget(UserIndex);
        if (!Test.TestTrue(TEXT("owned focus is established before silent input suspension"),
            FocusContains(Slate, UserIndex, First.ToSharedRef()))) { return; }
        const int32 ActionsBeforeSilentEnd = Listener->ActionCalls;
        int32 SilentEndFocusCallbacks = 0;
        const FDelegateHandle SilentEndHandle = Slate.OnFocusChanging().AddLambda(
            [&](const FFocusEvent& Event, const FWeakWidgetPath&, const TSharedPtr<SWidget>&,
                const FWidgetPath&, const TSharedPtr<SWidget>&)
            { if (Event.GetUser() == UserIndex) { ++SilentEndFocusCallbacks; } });
        Session->Request_EndInput(false);
        Slate.OnFocusChanging().Remove(SilentEndHandle);
        First->SimulateClick();
        Test.TestTrue(TEXT("silent end-input leaves owned focus in place without a callback or dispatch"),
            !Session->Get_IsInputActive() && Slate.GetUserFocusedWidget(UserIndex) == OwnerFocusBeforeSilentEnd
                && SilentEndFocusCallbacks == 0 && Listener->ActionCalls == ActionsBeforeSilentEnd
                && Session->Get_IsUsable() && Session->Get_View() == View);
        Slate.SetUserFocus(UserIndex, OutsideFocus.ToSharedRef(), EFocusCause::SetDirectly);
        Tick(Slate);
        if (!Test.TestTrue(TEXT("outside focus remains available for a later default restoration"),
            Slate.GetUserFocusedWidget(UserIndex) == OutsideFocus)) { return; }
        if (!Test.TestTrue(TEXT("owned input can readmit after silent suspension"),
            Session->TryBeginInput() && Session->TryFocusButton(TEXT("first")))) { return; }
        Session->Request_EndInput();
        Test.TestTrue(TEXT("default end-input still restores the outside focus captured at readmission"),
            !Session->Get_IsInputActive() && Slate.GetUserFocusedWidget(UserIndex) == OutsideFocus);
        if (!Test.TestTrue(TEXT("owned input resumes for held-release coverage"),
            Session->TryBeginInput() && Session->TryFocusButton(TEXT("first")))) { return; }

        const auto Spy = MakeShared<FReleaseSpy>();
        const bool SpyRegistered = Slate.RegisterInputPreProcessor(Spy, 1);
        ON_SCOPE_EXIT { if (SpyRegistered) { Slate.UnregisterInputPreProcessor(Spy); } };
        if (!Test.TestTrue(TEXT("owned input release observer registers"), SpyRegistered)) { return; }
        const FKeyEvent Space{EKeys::SpaceBar, FModifierKeysState{}, static_cast<uint32>(UserIndex), false, 0, 0};
        Slate.ProcessKeyDownEvent(Space);
        Session->Request_EndInput();
        const int32 ActionsBeforeDrain = Listener->ActionCalls;
        Test.TestTrue(TEXT("suspended held key repeat drains"), Slate.ProcessKeyDownEvent(
            FKeyEvent{EKeys::SpaceBar, FModifierKeysState{}, static_cast<uint32>(UserIndex), true, 0, 0}));
        Test.TestTrue(TEXT("suspended held key release drains"), Slate.ProcessKeyUpEvent(Space));
        Test.TestTrue(TEXT("held key never leaks or invokes suspended owner"), Spy->KeyUps == 0 && Spy->KeyRepeats == 0
            && Listener->ActionCalls == ActionsBeforeDrain && Session->Get_IsUsable());
        Slate.ProcessKeyUpEvent(FKeyEvent{EKeys::SpaceBar, FModifierKeysState{}, 0, false, 0, 0});
        Test.TestEqual(TEXT("foreign-user key release remains available"), Spy->KeyUps, 1);
        Tick(Slate);
        if (!Test.TestTrue(TEXT("active owner explicitly resumes suspended input"), Session->TryBeginInput()
            && Session->TryFocusButton(TEXT("first")))) { return; }
        auto Window = Slate.FindWidgetWindow(First.ToSharedRef());
        if (!Test.TestTrue(TEXT("owned button mounts in native PIE window"), Window.IsValid() && Window->GetNativeWindow().IsValid()
            && MovePointerTo(Slate, First.ToSharedRef(), UserIndex))) { return; }
        const auto Position = First->GetCachedGeometry().LocalToAbsolute(First->GetCachedGeometry().GetLocalSize() * 0.5f);
        const FPointerEvent Down{static_cast<uint32>(UserIndex), FSlateApplication::CursorPointerIndex, Position, Position,
            TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const FPointerEvent Up{static_cast<uint32>(UserIndex), FSlateApplication::CursorPointerIndex, Position, Position,
            TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        Test.TestTrue(TEXT("owned held pointer begins capture"), Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down)
            && First->HasMouseCaptureByUser(UserIndex, FSlateApplication::CursorPointerIndex));
        Owner->Shutdown();
        Test.TestTrue(TEXT("terminal close drains matching pointer release"), Slate.ProcessMouseButtonUpEvent(Up)
            && Spy->MouseUps == 0 && !Session->Get_IsUsable() && View->IsOwnerReleased());
        Slate.ProcessMouseButtonUpEvent(FPointerEvent{0, FSlateApplication::CursorPointerIndex, Position, Position,
            TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}});
        Test.TestEqual(TEXT("foreign-user pointer release remains available"), Spy->MouseUps, 1);
        const int32 EventTotal = Listener->ActionCalls + Listener->TextCalls + Listener->NumberCalls + Listener->BoolCalls;
        First->SimulateClick();
        OldFirst->SimulateClick();
        Propose();
        Test.TestEqual(TEXT("terminal retained controls cannot dispatch"), Listener->ActionCalls + Listener->TextCalls
            + Listener->NumberCalls + Listener->BoolCalls, EventTotal);
        const TWeakObjectPtr<UCk_UiOwnedSession_TestOwner_UE> WeakOwner{Owner.Get()};
        const TWeakObjectPtr<UCk_UI_AuthoredSession_UE> WeakSession{Session};
        Cover->Shutdown();
        Layout->DestroyLayout();
        Owner.Reset();
        Cover.Reset();
        Session = nullptr;
        Window.Reset();
        Tick(Slate);
        CollectGarbage(RF_NoFlags);
        Test.TestTrue(TEXT("removed owned widget and session collect despite retained native controls"),
            !WeakOwner.IsValid() && !WeakSession.IsValid());
        First->SimulateClick();
        OldFirst->SimulateClick();
        Search->SetText(FText::FromString(TEXT("After GC")));
        Test.TestEqual(TEXT("GC-stale retained controls remain inert"), Listener->ActionCalls + Listener->TextCalls
            + Listener->NumberCalls + Listener->BoolCalls, EventTotal);
        ck_ui_layer_membership_test::Run(Test, LocalPlayer, LayerTag, LayoutConfig);
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
    const auto OwnedContext = MakeShared<ck_owned_session_proof_test::FContext>();
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
            State->Menu = ck_scripted_pause_test::FMenu::Open(Player);
            TestTrue(TEXT("scripted pause widget opens on production layout"), State->Menu.IsValid());
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
            TSharedPtr<ck_scripted_pause_test::FMenu>& Menu = State->Menu;
            if (NOT IsValid(LayoutSubsystem) || NOT Menu.IsValid()) { return; }
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
            TSharedPtr<ck_scripted_pause_test::FMenu> ActiveMenu = Menu;
            bool TestOwnedPause = false;
            const auto CleanupMenu = ScopeExitSupport::FScopeGuardSyntaxSupport() + [&]()
            {
                if (ActiveMenu.IsValid() && ActiveMenu->Get_IsCurrent())
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
            TStrongObjectPtr<UCk_PauseMenuProof_UE> FirstSettingsChild{Menu->Get_SettingsChild()};
            const auto* ParentSession = Menu->Get_Session();
            const auto* ParentMembership = Menu->Get_Membership();
            const int64 ParentRevision = View->GetRevision();
            TestTrue(TEXT("Settings action pushes exact native settings child over script parent"),
                IsValid(FirstSettingsChild.Get()) && FirstSettingsChild->IsActivated()
                    && Menu->Get_IsCurrent() && !Menu->IsActivated()
                    && Menu->Get_AuthoredView() == View
                    && IsValid(ParentSession) && IsValid(ParentMembership));
            TestTrue(TEXT("nested settings keeps pause ownership"), UGameplayStatics::IsGamePaused(InWorld));
            TestTrue(TEXT("Escape returns to Pause from nested Settings"), PressKey(Slate, EKeys::Escape, User)
                && Menu->Get_SettingsChild() == nullptr && Menu->IsActivated()
                && Menu->Get_Session() == ParentSession && Menu->Get_Membership() == ParentMembership
                && Menu->Get_AuthoredView() == View && View->GetRevision() == ParentRevision);
            TestTrue(TEXT("returning from Settings retains pause ownership"), UGameplayStatics::IsGamePaused(InWorld));
            TestFalse(TEXT("covered Pause view remains live after nested Settings"), View->IsOwnerReleased());
            TArray<FString> RejectedChildErrors;
            TestTrue(TEXT("invalid settings child preparation rejects without publishing a child"),
                UCk_PauseMenuProof_UE::TryPrepareSettingsChild(nullptr, Menu->Script.Get(),
                    Menu->Get_AppliedSettings(), RejectedChildErrors) == nullptr
                    && !RejectedChildErrors.IsEmpty() && Menu->Get_SettingsChild() == nullptr
                    && Menu->IsActivated() && Menu->Get_AuthoredView() == View
                    && View->GetRevision() == ParentRevision);
            Slate.SetUserFocus(User, Resume.ToSharedRef(), EFocusCause::SetDirectly);
            Tick(Slate);
            const TSharedPtr<SWidget> FocusBeforeExpiry = Slate.GetUserFocusedWidget(User);
            TArray<FString> ExpiringChildErrors;
            TStrongObjectPtr<UCk_PauseMenuProof_UE> ExpiringChild{
                UCk_PauseMenuProof_UE::TryPrepareSettingsChild(Player, Menu->Script.Get(),
                    Menu->Get_AppliedSettings(), ExpiringChildErrors)};
            if (NOT TestTrue(TEXT("pending expiry fixture prepares without stack admission"),
                IsValid(ExpiringChild.Get()) && ExpiringChildErrors.IsEmpty()
                    && !ExpiringChild->IsActivated() && Menu->Get_SettingsChild() == nullptr)) { return; }
            const TWeakObjectPtr<UCk_PauseMenuProof_UE> WeakExpiringChild{ExpiringChild.Get()};
            const TSharedPtr<FCkUiView> ExpiringChildView = ExpiringChild->Get_AuthoredView();
            AdvanceRetirement(Slate);
            TArray<FString> ExpiredCommitErrors;
            TestFalse(TEXT("uncommitted prepared child expires at next CoreTicker pump"),
                ExpiringChild->TryPushPreparedSettingsChild(ExpiredCommitErrors));
            TestTrue(TEXT("expired candidate is inert and leaves exact parent state untouched"),
                !ExpiredCommitErrors.IsEmpty() && !ExpiringChild->IsActivated()
                    && ExpiringChildView.IsValid() && ExpiringChildView->IsOwnerReleased()
                    && Menu->Get_IsCurrent() && Menu->IsActivated()
                    && Menu->Get_Session() == ParentSession && Menu->Get_AuthoredView() == View
                    && View->GetRevision() == ParentRevision && UGameplayStatics::IsGamePaused(InWorld)
                    && Slate.GetUserFocusedWidget(User) == FocusBeforeExpiry);
            ExpiringChild.Reset();
            CollectGarbage(RF_NoFlags);
            if (UCk_PauseMenuProof_UE* Survivor = WeakExpiringChild.Get(); IsValid(Survivor))
            {
                TArray<FString> SurvivorErrors;
                TestFalse(TEXT("candidate surviving GC remains terminal after expiry"),
                    Survivor->TryPushPreparedSettingsChild(SurvivorErrors));
                TestTrue(TEXT("surviving expired candidate reports a rejection"), !SurvivorErrors.IsEmpty());
            }
            TestTrue(TEXT("candidate GC does not change script parent session, focus, or pause"),
                Menu->Get_IsCurrent() && Menu->IsActivated()
                    && Menu->Get_Session() == ParentSession && Menu->Get_AuthoredView() == View
                    && View->GetRevision() == ParentRevision && UGameplayStatics::IsGamePaused(InWorld)
                    && Slate.GetUserFocusedWidget(User) == FocusBeforeExpiry);
            Resume = View->GetButton(TEXT("resume-button"));
            Settings = View->GetButton(TEXT("settings-button"));
            Return = View->GetButton(TEXT("return-button"));
            if (NOT TestTrue(TEXT("Pause actions retain identity after nested Settings"), View.IsValid()
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
            TStrongObjectPtr<UCk_PauseMenuProof_UE> SecondSettingsChild{Menu->Get_SettingsChild()};
            const int64 SecondParentRevision = View->GetRevision();
            TestTrue(TEXT("Space pushes another exact settings child"),
                IsValid(SecondSettingsChild.Get()) && SecondSettingsChild.Get() != FirstSettingsChild.Get()
                    && SecondSettingsChild->IsActivated() && Menu->Get_AuthoredView() == View
                    && View->GetRevision() == SecondParentRevision);
            TestTrue(TEXT("Escape returns to Pause after Space"), PressKey(Slate, EKeys::Escape, User)
                && Menu->Get_SettingsChild() == nullptr && Menu->IsActivated()
                && Menu->Get_Session() == ParentSession && Menu->Get_Membership() == ParentMembership
                && Menu->Get_AuthoredView() == View && View->GetRevision() == SecondParentRevision);
            TestFalse(TEXT("second Settings visit retains live Pause view"), View->IsOwnerReleased());
            Resume = View->GetButton(TEXT("resume-button"));
            Settings = View->GetButton(TEXT("settings-button"));
            Return = View->GetButton(TEXT("return-button"));
            if (NOT TestTrue(TEXT("Pause actions retain identity after second Settings visit"), View.IsValid()
                && Resume.IsValid() && Settings.IsValid() && Return.IsValid())) { return; }

            TArray<FString> PreparedChildErrors;
            TStrongObjectPtr<UCk_PauseMenuProof_UE> StalePreparedChild{
                UCk_PauseMenuProof_UE::TryPrepareSettingsChild(Player, Menu->Script.Get(),
                    Menu->Get_AppliedSettings(), PreparedChildErrors)};
            TestTrue(TEXT("valid settings child can prepare without changing the active script parent"),
                IsValid(StalePreparedChild.Get()) && PreparedChildErrors.IsEmpty()
                    && Menu->Get_SettingsChild() == nullptr && Menu->IsActivated()
                    && Menu->Get_AuthoredView() == View);
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
            if (IsValid(StalePreparedChild.Get()))
            {
                TArray<FString> StaleCommitErrors;
                TestFalse(TEXT("prepared child rejects commit after exact script parent closes"),
                    StalePreparedChild->TryPushPreparedSettingsChild(StaleCommitErrors));
                TestTrue(TEXT("stale child rejection gives a diagnostic and leaves it inactive"),
                    !StaleCommitErrors.IsEmpty() && !StalePreparedChild->IsActivated()
                        && Menu->Get_SettingsChild() == nullptr);
            }
            TestTrue(TEXT("host-owned fixture layout remains for reopen"), LayoutSubsystem->Has_Layout());
            if (UCk_UI_PrimaryGameLayout_UE* Layout = LayoutSubsystem->Get_Layout())
            { TestEqual(TEXT("closed menu restores GameOnly input"), Layout->Get_EffectiveInputMode(), ECk_UI_InputMode::GameOnly); }
            const int32 ResumeBeforeStaleClick = Menu->Get_ActionCount(ResumeAction);
            StaleResume->SimulateClick();
            TestEqual(TEXT("retained stale control cannot call closed owner"),
                Menu->Get_ActionCount(ResumeAction), ResumeBeforeStaleClick);

            TSharedPtr<ck_scripted_pause_test::FMenu> Reopened = ck_scripted_pause_test::FMenu::Open(Player);
            if (NOT TestTrue(TEXT("script screen reopens on retained production layout"), Reopened.IsValid())) { return; }
            ActiveMenu = Reopened;
            TestTrue(TEXT("reopen uses a fresh script screen owner"), Reopened->Script.Get() != Menu->Script.Get());
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
            TSharedPtr<ck_scripted_pause_test::FMenu> EscapeMenu = ck_scripted_pause_test::FMenu::Open(Player);
            if (NOT TestTrue(TEXT("script screen reopens for Escape route"), EscapeMenu.IsValid())) { return; }
            ActiveMenu = EscapeMenu;
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
            TSharedPtr<ck_scripted_pause_test::FMenu> AlreadyPausedMenu = ck_scripted_pause_test::FMenu::Open(Player);
            if (NOT TestTrue(TEXT("script screen opens while world was already paused"), AlreadyPausedMenu.IsValid()))
            { return; }
            ActiveMenu = AlreadyPausedMenu;
            TestTrue(TEXT("Escape closes pre-paused menu"), PressKey(Slate, EKeys::Escape, User));
            TestEqual(TEXT("pre-paused Escape records one Resume request"),
                AlreadyPausedMenu->Get_ActionCount(ResumeAction), 1);
            TestTrue(TEXT("close preserves pause it did not acquire"), UGameplayStatics::IsGamePaused(InWorld));
            TestTrue(TEXT("test releases its own preexisting pause"), Player->SetPause(false));
            TestOwnedPause = false;
            TestFalse(TEXT("pause cleanup restores unpaused state"), UGameplayStatics::IsGamePaused(InWorld));

            const FGameplayTag PauseLayerTag = FGameplayTag::RequestGameplayTag(
                TEXT("CkTests.UI.Layer.PauseMenuProof"));
            TSharedPtr<ck_scripted_pause_test::FMenu> ClearedMenu = ck_scripted_pause_test::FMenu::Open(Player);
            if (NOT TestTrue(TEXT("script parent opens for production layer clear"), ClearedMenu.IsValid())) { return; }
            ActiveMenu = ClearedMenu;
            const TSharedPtr<FCkUiView> ClearedView = ClearedMenu->Get_AuthoredView();
            LayoutSubsystem->ClearLayer(PauseLayerTag);
            AdvanceRetirement(Slate);
            TestTrue(TEXT("layer clear retires held script and authored view"),
                !ClearedMenu->Get_IsCurrent() && !ClearedMenu->IsActivated()
                    && ClearedView.IsValid() && ClearedView->IsOwnerReleased()
                    && !UGameplayStatics::IsGamePaused(InWorld));
            TestFalse(TEXT("retired script cannot open Settings after layer clear"),
                ClearedMenu->Request_ShowSettings());

            TSharedPtr<ck_scripted_pause_test::FMenu> CoveredMenu = ck_scripted_pause_test::FMenu::Open(Player);
            if (NOT TestTrue(TEXT("script parent opens for covered removal"), CoveredMenu.IsValid())) { return; }
            ActiveMenu = CoveredMenu;
            const TSharedPtr<FCkUiView> CoveredView = CoveredMenu->Get_AuthoredView();
            TestTrue(TEXT("covered-removal fixture pushes native settings child"),
                CoveredMenu->Request_ShowSettings());
            TStrongObjectPtr<UCk_PauseMenuProof_UE> CoveredChild{CoveredMenu->Get_SettingsChild()};
            if (NOT TestTrue(TEXT("covered-removal fixture has active native child"),
                IsValid(CoveredChild.Get()) && CoveredChild->IsActivated())) { return; }
            TestTrue(TEXT("production layout removes exact covered script parent"),
                LayoutSubsystem->RemoveWidget(CoveredMenu->Script.Get()));
            AdvanceRetirement(Slate);
            TestTrue(TEXT("covered parent removal retires script and exact settings child"),
                !CoveredMenu->Get_IsCurrent() && !CoveredMenu->IsActivated()
                    && CoveredView.IsValid() && CoveredView->IsOwnerReleased()
                    && !CoveredChild->IsActivated() && !UGameplayStatics::IsGamePaused(InWorld));
            TestFalse(TEXT("removed covered script cannot reopen Settings"),
                CoveredMenu->Request_ShowSettings());

            TSharedPtr<ck_scripted_pause_test::FMenu> ReenteredMenu = ck_scripted_pause_test::FMenu::Open(Player);
            if (NOT TestTrue(TEXT("script parent opens for child-pop admission reentry"), ReenteredMenu.IsValid())) { return; }
            ActiveMenu = ReenteredMenu;
            TStrongObjectPtr<UCommonActivatableWidget> ReenteredParent{ReenteredMenu->Script.Get()};
            TStrongObjectPtr<UCk_UI_LayerStack_UE> ReenteredLayer{LayoutSubsystem->Get_Layer(PauseLayerTag)};
            const TSharedPtr<SWidget> RetainedParentRoot = ReenteredParent->GetCachedWidget();
            const TSharedPtr<FCkUiView> ReenteredView = ReenteredMenu->Get_AuthoredView();
            UCk_UI_AuthoredSession_UE* ReenteredSession = ReenteredMenu->Get_Session();
            if (NOT TestTrue(TEXT("reentry fixture retains exact parent admission and Slate root"),
                IsValid(ReenteredLayer.Get()) && RetainedParentRoot.IsValid()
                    && ReenteredView.IsValid() && IsValid(ReenteredSession))) { return; }
            const uint64 OldAdmission = ReenteredLayer->Get_WidgetAdmissionGeneration(ReenteredParent.Get());
            if (NOT TestTrue(TEXT("reentry fixture parent has a nonzero original admission"), OldAdmission != 0)
                || NOT TestTrue(TEXT("reentry fixture pushes exact Settings child"),
                    ReenteredMenu->Request_ShowSettings())) { return; }
            TStrongObjectPtr<UCk_PauseMenuProof_UE> ReenteredChild{ReenteredMenu->Get_SettingsChild()};
            if (NOT TestTrue(TEXT("reentry fixture has an active native child"),
                IsValid(ReenteredChild.Get()) && ReenteredChild->IsActivated())) { return; }
            bool bObservedExactChildPop = false;
            bool bRemovedOldAdmission = false;
            bool bPushedNewAdmission = false;
            uint64 NewAdmission = 0;
            const FDelegateHandle ChildPopHandle = ReenteredLayer->OnWidgetPopped.AddLambda(
                [&](UCommonActivatableWidget* InPopped)
                {
                    if (InPopped != ReenteredChild.Get() || bObservedExactChildPop) { return; }
                    bObservedExactChildPop = true;
                    bRemovedOldAdmission = ReenteredLayer->PopSpecificWidget(ReenteredParent.Get());
                    if (NOT bRemovedOldAdmission) { return; }
                    bPushedNewAdmission = ReenteredLayer->PushWidgetInstance(ReenteredParent.Get()) == ReenteredParent.Get();
                    if (bPushedNewAdmission)
                    { NewAdmission = ReenteredLayer->Get_WidgetAdmissionGeneration(ReenteredParent.Get()); }
                });
            ReenteredMenu->Request_Close();
            ReenteredLayer->OnWidgetPopped.Remove(ChildPopHandle);
            TestTrue(TEXT("exact child pop removes and re-adds the same parent once"),
                bObservedExactChildPop && bRemovedOldAdmission && bPushedNewAdmission);
            TestTrue(TEXT("terminal old parent leaves the newer admission in the layer"),
                NewAdmission != 0 && NewAdmission != OldAdmission
                    && ReenteredLayer->ContainsWidget(ReenteredParent.Get())
                    && ReenteredLayer->Get_WidgetAdmissionGeneration(ReenteredParent.Get()) == NewAdmission);
            TestTrue(TEXT("retained parent Slate root cannot revive its old session or view"),
                RetainedParentRoot.IsValid() && !ReenteredSession->Get_IsUsable()
                    && ReenteredView->IsOwnerReleased() && !ReenteredMenu->Get_IsCurrent()
                    && !ReenteredChild->IsActivated() && !UGameplayStatics::IsGamePaused(InWorld));
            TestFalse(TEXT("re-admitted terminal parent cannot dispatch Settings"),
                ReenteredMenu->Request_ShowSettings());
            if (ReenteredLayer->ContainsWidget(ReenteredParent.Get()))
            {
                TestTrue(TEXT("fixture removes the inert newer parent admission explicitly"),
                    ReenteredLayer->PopSpecificWidget(ReenteredParent.Get()));
            }
            TestFalse(TEXT("child-pop fixture leaves no script parent in the layer"),
                ReenteredLayer->ContainsWidget(ReenteredParent.Get()));

            TSharedPtr<ck_scripted_pause_test::FMenu> DestroyedMenu = ck_scripted_pause_test::FMenu::Open(Player);
            if (NOT TestTrue(TEXT("script parent opens for production layout destruction"), DestroyedMenu.IsValid())) { return; }
            ActiveMenu = DestroyedMenu;
            const TSharedPtr<FCkUiView> DestroyedView = DestroyedMenu->Get_AuthoredView();
            LayoutSubsystem->DestroyLayout();
            AdvanceRetirement(Slate);
            TestFalse(TEXT("test releases owned production layout"), LayoutSubsystem->Has_Layout());
            TestTrue(TEXT("layout destruction permanently retires held script and view"),
                !DestroyedMenu->Get_IsCurrent() && !DestroyedMenu->IsActivated()
                    && DestroyedView.IsValid() && DestroyedView->IsOwnerReleased()
                    && !UGameplayStatics::IsGamePaused(InWorld));
            TestFalse(TEXT("destroyed layout cannot revive script Settings action"),
                DestroyedMenu->Request_ShowSettings());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, OwnedContext](UWorld* InWorld)
        { ck_owned_session_proof_test::PreparePlayer(*this, InWorld, *OwnedContext); })));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, OwnedContext](UWorld* InWorld)
        { ck_owned_session_proof_test::Run(*this, InWorld, *OwnedContext); })));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
