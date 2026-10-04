#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkGym_Switchboard_Subsystem.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/UI/CkPauseMenuProof.h"

#include "Engine/GameInstance.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace ck_settings_menu_proof_test
{
    constexpr TCHAR GymName[] = TEXT("UI Authoring");
    constexpr TCHAR GymModeName[] = TEXT("Ck_UiAuthoringGym_GameMode");
    constexpr TCHAR GymPlayerName[] = TEXT("Ck_UiAuthoringGym_PlayerController");

    struct FState;
    auto RestoreSettings(FState& InState) -> void;

    struct FState
    {
        ~FState()
        {
            RemoveCleanupObserver();
            RestoreWindow();
            RestoreSettings(*this);
        }

        auto RemoveCleanupObserver() -> void
        {
            if (CleanupHandle.IsValid())
            {
                FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
                CleanupHandle.Reset();
            }
        }

        auto RestoreWindow() -> void
        {
            if (bWindowSizeCaptured)
            {
                if (const TSharedPtr<SWindow> Window = WindowToRestore.Pin())
                { Window->Resize(OriginalWindowSize); }
                bWindowSizeCaptured = false;
            }
        }

        ECkGym_StartupMode StartupMode = ECkGym_StartupMode::Cycler;
        ECkGym_ControlPanel_Mode PanelMode = ECkGym_ControlPanel_Mode::Full;
        FString DefaultGymName;
        FString LastGymName;
        TArray<FString> RecentGymNames;
        bool bSettingsCaptured = false;
        bool bSettingsRestored = false;
        TWeakPtr<SWindow> WindowToRestore;
        FVector2D OriginalWindowSize = FVector2D::ZeroVector;
        bool bWindowSizeCaptured = false;
        int32 RegistryIndex = INDEX_NONE;
        TWeakObjectPtr<UWorld> CleanupWorld;
        TWeakObjectPtr<UCk_PauseMenuProof_UE> MenuAtCleanup;
        FDelegateHandle CleanupHandle;
        bool bObservedCleanup = false;
        bool bInactiveAtCleanup = false;
        bool bViewReleasedAtCleanup = false;
    };

    auto RestoreSettings(FState& InState) -> void
    {
        if (NOT InState.bSettingsCaptured || InState.bSettingsRestored) { return; }
        UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
        if (NOT IsValid(Settings)) { return; }
        Settings->StartupMode = InState.StartupMode;
        Settings->ControlPanelMode = InState.PanelMode;
        Settings->DefaultGymName = InState.DefaultGymName;
        Settings->LastGymName = InState.LastGymName;
        Settings->RecentGymNames = InState.RecentGymNames;
        Settings->SaveConfig();
        InState.bSettingsRestored = Settings->StartupMode == InState.StartupMode
            && Settings->ControlPanelMode == InState.PanelMode
            && Settings->DefaultGymName == InState.DefaultGymName
            && Settings->LastGymName == InState.LastGymName
            && Settings->RecentGymNames == InState.RecentGymNames;
    }

    auto GetRegistry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        UGameInstance* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto GetPlayer(UWorld* InWorld) -> APlayerController*
    { return IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr; }

    auto GetSwitchboard(APlayerController* InPlayer) -> UCkGym_Switchboard_Subsystem*
    {
        ULocalPlayer* Local = IsValid(InPlayer) ? InPlayer->GetLocalPlayer() : nullptr;
        return IsValid(Local) ? Local->GetSubsystem<UCkGym_Switchboard_Subsystem>() : nullptr;
    }

    auto IsGymReady(UWorld* InWorld) -> bool
    {
        const AGameModeBase* Mode = IsValid(InWorld) ? InWorld->GetAuthGameMode() : nullptr;
        const APlayerController* Player = GetPlayer(InWorld);
        return IsValid(Mode) && Mode->GetClass()->GetName().Contains(GymModeName)
            && IsValid(Player) && Player->GetClass()->GetName().Contains(GymPlayerName)
            && IsValid(Player->GetLocalPlayer());
    }

    auto GetActiveMenu(APlayerController* InPlayer) -> UCk_PauseMenuProof_UE*
    {
        UFunction* Function = IsValid(InPlayer)
            ? InPlayer->FindFunction(FName{TEXT("Get_ActivePauseMenu")}) : nullptr;
        const FObjectProperty* Return = Function != nullptr
            ? CastField<FObjectProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr) { return nullptr; }
        TArray<uint8> Params;
        Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        InPlayer->ProcessEvent(Function, Params.GetData());
        auto* Menu = Cast<UCk_PauseMenuProof_UE>(Return->GetObjectPropertyValue_InContainer(Params.GetData()));
        Function->DestroyStruct(Params.GetData());
        return Menu;
    }

    auto Tick(FSlateApplication& InSlate) -> void
    { InSlate.PumpMessages(); InSlate.Tick(); InSlate.Tick(); }

    auto PressKey(const FKey& InKey, int32 InUser = 0) -> bool
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Event{InKey, FModifierKeysState{}, static_cast<uint32>(InUser), false, 0, 0};
        const bool Down = Slate.ProcessKeyDownEvent(Event);
        const bool Up = Slate.ProcessKeyUpEvent(Event);
        Tick(Slate);
        return Down && Up;
    }

    auto FocusContains(const TSharedRef<SWidget>& InTarget, int32 InUser) -> bool
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWidget> Focused = Slate.GetUserFocusedWidget(InUser);
        FWidgetPath Path;
        if (NOT Focused.IsValid() || NOT Slate.GeneratePathToWidgetUnchecked(Focused.ToSharedRef(), Path))
        { return false; }
        for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
        { if (Path.Widgets[Index].Widget == InTarget) { return true; } }
        return false;
    }

    auto FocusViewport(UWorld* InWorld) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (NOT Viewport.IsValid()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true);
        Slate.SetUserFocus(0, Viewport.ToSharedRef(), EFocusCause::SetDirectly);
        return Slate.IsActive() && Window->IsActive()
            && Slate.GetUserFocusedWidget(0).Get() == Viewport.Get();
    }

    auto IsSettingsActive(UWorld* InWorld) -> bool
    {
        UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
        return IsValid(Menu) && Menu->IsActivated()
            && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Settings;
    }

    auto Capture(UWorld* InWorld, FState& InState, const FString& InName, FIntPoint InSize) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (NOT Viewport.IsValid()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid()) { return false; }
        if (NOT InState.bWindowSizeCaptured)
        {
            InState.WindowToRestore = Window;
            InState.OriginalWindowSize = Window->GetClientSizeInScreen();
            InState.bWindowSizeCaptured = true;
        }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Tick(Slate);
            Pixels.Reset();
            Size = FIntVector::ZeroValue;
            if (NOT Slate.TakeScreenshot(Viewport.ToSharedRef(), Pixels, Size)
                || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y)
            { return false; }
            if (Size.X == InSize.X && Size.Y == InSize.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D{
                static_cast<double>(InSize.X - Size.X), static_cast<double>(InSize.Y - Size.Y)});
        }
        if (Size.X != InSize.X || Size.Y != InSize.Y) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/SettingsMenuProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, InName + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }

    auto GetControl(UCk_PauseMenuProof_UE* InMenu, const TCHAR* InId) -> TSharedPtr<SWidget>
    { return IsValid(InMenu) ? InMenu->Get_ControlWidget(FName{InId}) : nullptr; }

    auto FindTagged(const TSharedRef<SWidget>& InRoot, FName InTag) -> TSharedPtr<SWidget>
    {
        if (InRoot->GetTag() == InTag) { return InRoot; }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SWidget> Found = FindTagged(
                ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag); Found.IsValid())
            { return Found; }
        }
        return nullptr;
    }

    auto PanelMatchesReference(UCk_PauseMenuProof_UE* InMenu, UWorld* InWorld,
        FIntPoint InSize) -> bool
    {
        const TSharedPtr<FCkUiView> View = IsValid(InMenu) ? InMenu->Get_AuthoredView() : nullptr;
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (NOT View.IsValid() || NOT Viewport.IsValid()) { return false; }
        const TSharedPtr<SWidget> Panel = FindTagged(View->GetRegion(TEXT("main")),
            FName{TEXT("settings-panel")});
        if (NOT Panel.IsValid()) { return false; }
        const double Scale = FMath::Min(InSize.X / 1920.0, InSize.Y / 1080.0);
        const FVector2D ExpectedPosition{
            (InSize.X - 1920.0 * Scale) * 0.5 + 480.0 * Scale,
            (InSize.Y - 1080.0 * Scale) * 0.5 + 152.0 * Scale};
        const FVector2D Position = Panel->GetCachedGeometry().GetAbsolutePosition()
            - Viewport->GetCachedGeometry().GetAbsolutePosition();
        const FVector2D Size = Panel->GetCachedGeometry().GetLocalSize()
            * Panel->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
        constexpr double Tolerance = 3.0;
        return FMath::Abs(Position.X - ExpectedPosition.X) <= Tolerance
            && FMath::Abs(Position.Y - ExpectedPosition.Y) <= Tolerance
            && FMath::Abs(Size.X - 960.0 * Scale) <= Tolerance
            && FMath::Abs(Size.Y - 776.0 * Scale) <= Tolerance;
    }

    auto CapturePopup(const TSharedRef<SWidget>& InSelect) -> bool
    {
        if (InSelect->GetTypeAsString() != TEXT("SCkUiSelect")) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedRef<SMenuAnchor> Anchor = StaticCastSharedRef<SMenuAnchor>(InSelect);
        const TSharedPtr<SWindow> Popup = Anchor->GetMenuWindow();
        if (NOT Anchor->IsOpen() || NOT Popup.IsValid()) { return false; }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        Tick(Slate);
        if (NOT Slate.TakeScreenshot(Popup.ToSharedRef(), Pixels, Size)
            || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/SettingsMenuProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory,
            TEXT("settings-popup-window.png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }

    auto FocusControl(UCk_PauseMenuProof_UE* InMenu, const TCHAR* InId) -> bool
    {
        const TSharedPtr<SWidget> Control = GetControl(InMenu, InId);
        const int32 User = IsValid(InMenu) ? InMenu->Get_SlateUserIndex() : INDEX_NONE;
        if (NOT Control.IsValid() || User < 0) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        Slate.SetUserFocus(User, Control, EFocusCause::SetDirectly);
        Tick(Slate);
        return FocusContains(Control.ToSharedRef(), User);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_SettingsMenuProof_PIE,
    "Ck.UiAuthoring.SettingsMenuProof.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

bool FCk_SettingsMenuProof_PIE::RunTest(const FString&)
{
    using namespace ck_settings_menu_proof_test;
    if (NOT FSlateApplication::IsInitialized())
    { AddError(TEXT("Settings proof requires Slate.")); return false; }
    const auto TickLimit = IsValid(GEditor) ? GEditor->GetMaxTickRate(1.0f / 120.0f) : 0.0f;
    TestTrue(FString::Printf(TEXT("Editor launch caps fast frames at 30 FPS (effective tick limit %.3f)"), TickLimit),
        IsValid(GEditor) && GEditor->bSmoothFrameRate && GEditor->SmoothedFrameRateRange.HasUpperBound()
            && FMath::IsNearlyEqual(GEditor->SmoothedFrameRateRange.GetUpperBoundValue(), 30.0f)
            && TickLimit > 0.0f && TickLimit <= 30.0f);

    const TSharedRef<FState> State = MakeShared<FState>();
    UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    if (NOT TestNotNull(TEXT("per-user gym startup settings"), Settings)) { return false; }
    State->StartupMode = Settings->StartupMode;
    State->PanelMode = Settings->ControlPanelMode;
    State->DefaultGymName = Settings->DefaultGymName;
    State->LastGymName = Settings->LastGymName;
    State->RecentGymNames = Settings->RecentGymNames;
    State->bSettingsCaptured = true;
    Settings->StartupMode = ECkGym_StartupMode::Cycler;
    Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Full;

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1,
        TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UCkGym_Registry_Subsystem* Registry = GetRegistry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(Registry) && Registry->Find_GymIndexByName(GymName) != INDEX_NONE;
        }), 15.0, TEXT("UI Authoring gym registers before Settings travel")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            UCkGym_Registry_Subsystem* Registry = GetRegistry(InWorld);
            if (NOT TestNotNull(TEXT("gym registry"), Registry)) { return; }
            State->RegistryIndex = Registry->Find_GymIndexByName(GymName);
            if (NOT TestTrue(TEXT("UI Authoring entry exists"),
                Registry->Get_Entries().IsValidIndex(State->RegistryIndex))) { return; }
            const FCkGym_Entry& Entry = Registry->Get_Entries()[State->RegistryIndex];
            TestEqual(TEXT("Settings gym is in CkUI selector group"), Entry.Category, FString{TEXT("CkUI")});
            bool bSelectorRow = false;
            for (const FCkGym_Switchboard_Group& Group :
                UCkGym_Switchboard_Subsystem::Build_Groups(Registry->Get_Entries()))
            {
                if (Group.Category != TEXT("CkUI")) { continue; }
                for (const FCkGym_Switchboard_Row& Row : Group.Rows)
                { bSelectorRow |= Row.RegistryIndex == State->RegistryIndex && Row.DisplayName == GymName; }
            }
            if (TestTrue(TEXT("real selector contains the UI Authoring gym"), bSelectorRow)
                && TestTrue(TEXT("entry names the AS gym GameMode"),
                    IsValid(Entry.GameModeClass.Get())
                        && Entry.GameModeClass.Get()->GetName().Contains(GymModeName)))
            { Registry->Request_TravelToGym(State->RegistryIndex); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsGymReady(ck::auto_test::net::Get_ServerWorld()); }),
        30.0, TEXT("registry travel loads real UI Authoring gym and controller")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            APlayerController* Player = GetPlayer(InWorld);
            UCkGym_Switchboard_Subsystem* Switchboard = GetSwitchboard(Player);
            if (NOT TestTrue(TEXT("real gym and controller are ready"), IsGymReady(InWorld))
                || NOT TestNotNull(TEXT("real switchboard is ready"), Switchboard)
                || NOT TestTrue(TEXT("PIE viewport owns keyboard input"), FocusViewport(InWorld))) { return; }
            TestFalse(TEXT("selector begins closed"), Switchboard->Get_IsOpen());
            PressKey(EKeys::Two);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsSettingsActive(ck::auto_test::net::Get_ServerWorld()); }),
        10.0, TEXT("real panel [2] opens Settings through Slate and CkInput")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            APlayerController* Player = GetPlayer(InWorld);
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(Player);
            UCkGym_Switchboard_Subsystem* Switchboard = GetSwitchboard(Player);
            if (NOT TestNotNull(TEXT("[2] produced native Settings menu"), Menu)
                || NOT TestNotNull(TEXT("Settings retains switchboard subsystem"), Switchboard)) { return; }
            const int32 User = Menu->Get_SlateUserIndex();
            TestTrue(TEXT("direct Settings owns game pause"), IsValid(InWorld) && InWorld->IsPaused());
            TestFalse(TEXT("Settings does not open gym selector"), Switchboard->Get_IsOpen());
            TestEqual(TEXT("initial applied Settings volume"), Menu->Get_AppliedSettings().Get_Volume(), 75.0f);
            TestTrue(TEXT("initial subtitles applied"), Menu->Get_AppliedSettings().Get_Subtitles());
            TestEqual(TEXT("initial display mode applied"), Menu->Get_AppliedSettings().Get_DisplayMode(),
                FString{TEXT("Windowed")});
            TestTrue(TEXT("initial Settings draft equals applied"),
                Menu->Get_DraftSettings() == Menu->Get_AppliedSettings() && NOT Menu->Get_IsSettingsDirty());
            const TSharedPtr<SWidget> Slider = GetControl(Menu, TEXT("settings-volume"));
            const TSharedPtr<SWidget> Checkbox = GetControl(Menu, TEXT("settings-subtitles"));
            const TSharedPtr<SWidget> Select = GetControl(Menu, TEXT("settings-display-mode"));
            TSharedPtr<SWidget> Cancel = GetControl(Menu, TEXT("settings-cancel"));
            TSharedPtr<SWidget> Apply = GetControl(Menu, TEXT("settings-apply"));
            const auto DescribeControl = [this](const TCHAR* InId, const TSharedPtr<SWidget>& InWidget)
            {
                AddInfo(FString::Printf(TEXT("Settings control '%s': type=%s tag=%s"), InId,
                    InWidget.IsValid() ? *InWidget->GetTypeAsString() : TEXT("<missing>"),
                    InWidget.IsValid() ? *InWidget->GetTag().ToString() : TEXT("<missing>")));
            };
            DescribeControl(TEXT("settings-volume"), Slider);
            DescribeControl(TEXT("settings-subtitles"), Checkbox);
            DescribeControl(TEXT("settings-display-mode"), Select);
            DescribeControl(TEXT("settings-cancel"), Cancel);
            DescribeControl(TEXT("settings-apply"), Apply);
            if (NOT TestTrue(TEXT("Settings mounts native slider, checkbox, select and action buttons"),
                Slider.IsValid() && Slider->GetTypeAsString() == TEXT("SCkUiSlider")
                    && Checkbox.IsValid() && Checkbox->GetTypeAsString() == TEXT("SCheckBox")
                    && Select.IsValid() && Select->GetTypeAsString() == TEXT("SCkUiSelect")
                    && Cancel.IsValid() && Apply.IsValid())) { return; }
            TestFalse(TEXT("Apply disabled when draft is clean"), Apply->IsEnabled());
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
            if (NOT TestTrue(TEXT("CkTests source resources are available"), Plugin.IsValid())) { return; }
            const FString ResourceRoot = FPaths::Combine(Plugin->GetBaseDir(),
                TEXT("Resources/UiAuthoring/SettingsMenuProof/SettingsMenuProof.ui"));
            FString Markup;
            FString Style;
            if (NOT TestTrue(TEXT("canonical Settings source files load"),
                FFileHelper::LoadFileToString(Markup, *(ResourceRoot + TEXT(".html")))
                    && FFileHelper::LoadFileToString(Style, *(ResourceRoot + TEXT(".css"))))) { return; }
            const TSharedPtr<FCkUiView> AcceptedView = Menu->Get_AuthoredView();
            if (NOT TestTrue(TEXT("Settings has an accepted authored view"), AcceptedView.IsValid())) { return; }
            const int64 AcceptedRevision = AcceptedView->GetRevision();
            const FCk_UiAuthoringProof_Settings AcceptedDraft = Menu->Get_DraftSettings();
            const FCk_UiAuthoringProof_Settings AcceptedApplied = Menu->Get_AppliedSettings();
            const int32 CancelCount = Menu->Get_ActionCount(FName{TEXT("CancelSettingsRequested")});
            const int32 ApplyCount = Menu->Get_ActionCount(FName{TEXT("ApplySettingsRequested")});
            const FString WrongCheckboxBinding = Markup.Replace(
                TEXT("value-bind=\"SettingsSubtitles\""), TEXT("value-bind=\"MissingSubtitles\""));
            if (NOT TestTrue(TEXT("negative fixture changes the required checkbox binding"),
                WrongCheckboxBinding != Markup)) { return; }
            TestFalse(TEXT("required Settings checkbox binding is rejected"),
                Menu->Request_ApplySource(WrongCheckboxBinding, Style));
            TestTrue(TEXT("rejected source retains accepted view, controls, model and callbacks"),
                Menu->Get_AuthoredView() == AcceptedView
                    && AcceptedView->GetRevision() == AcceptedRevision
                    && GetControl(Menu, TEXT("settings-volume")) == Slider
                    && GetControl(Menu, TEXT("settings-subtitles")) == Checkbox
                    && GetControl(Menu, TEXT("settings-display-mode")) == Select
                    && GetControl(Menu, TEXT("settings-cancel")) == Cancel
                    && GetControl(Menu, TEXT("settings-apply")) == Apply
                    && Menu->Get_DraftSettings() == AcceptedDraft
                    && Menu->Get_AppliedSettings() == AcceptedApplied
                    && Menu->Get_ActionCount(FName{TEXT("CancelSettingsRequested")}) == CancelCount
                    && Menu->Get_ActionCount(FName{TEXT("ApplySettingsRequested")}) == ApplyCount
                    && NOT Menu->Get_SourceError().IsEmpty());
            TestTrue(TEXT("clean Settings 1920x1080 capture"),
                Capture(InWorld, *State, TEXT("settings-clean-1920x1080"), FIntPoint{1920, 1080}));
            TestTrue(TEXT("1920x1080 Settings card matches authored reference bounds"),
                PanelMatchesReference(Menu, InWorld, FIntPoint{1920, 1080}));
            TestTrue(TEXT("clean Settings 1280x720 capture"),
                Capture(InWorld, *State, TEXT("settings-clean-1280x720"), FIntPoint{1280, 720}));
            TestTrue(TEXT("1280x720 Settings card scales inside viewport"),
                PanelMatchesReference(Menu, InWorld, FIntPoint{1280, 720}));
            TestTrue(TEXT("clean Settings 2560x1080 capture"),
                Capture(InWorld, *State, TEXT("settings-clean-2560x1080"), FIntPoint{2560, 1080}));
            TestTrue(TEXT("2560x1080 Settings card stays centered in authored frame"),
                PanelMatchesReference(Menu, InWorld, FIntPoint{2560, 1080}));
            TestTrue(TEXT("interactive 1920x1080 size restored"),
                Capture(InWorld, *State, TEXT("settings-clean-1920x1080"), FIntPoint{1920, 1080}));
            PressKey(EKeys::One, User);
            PressKey(EKeys::Two, User);
            TestTrue(TEXT("gym number shortcuts stay suspended while Settings owns input"),
                GetActiveMenu(Player) == Menu
                    && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Settings
                    && NOT Switchboard->Get_IsOpen());
            if (NOT TestTrue(TEXT("native slider receives keyboard focus"), FocusControl(Menu, TEXT("settings-volume"))))
            { return; }
            TestTrue(TEXT("clean Tab navigates slider to checkbox"), PressKey(EKeys::Tab, User)
                && FocusContains(Checkbox.ToSharedRef(), User));
            TestTrue(TEXT("clean Tab navigates checkbox to select"), PressKey(EKeys::Tab, User)
                && FocusContains(Select.ToSharedRef(), User));
            TestTrue(TEXT("clean Tab navigates select to Cancel"), PressKey(EKeys::Tab, User)
                && FocusContains(Cancel.ToSharedRef(), User));
            TestTrue(TEXT("clean Tab skips disabled Apply and wraps to slider"), PressKey(EKeys::Tab, User)
                && FocusContains(Slider.ToSharedRef(), User));
            if (NOT TestTrue(TEXT("native select focuses for popup interaction"),
                FocusControl(Menu, TEXT("settings-display-mode")))) { return; }
            TestTrue(TEXT("select Space opens the native dropdown"), PressKey(EKeys::SpaceBar, User));
            TestTrue(TEXT("open dropdown viewport capture"),
                Capture(InWorld, *State, TEXT("settings-popup-open-1920x1080"), FIntPoint{1920, 1080}));
            TestTrue(TEXT("open dropdown native popup capture"), CapturePopup(Select.ToSharedRef()));
            const TSharedRef<SMenuAnchor> SelectAnchor = StaticCastSharedRef<SMenuAnchor>(Select.ToSharedRef());
            const TSharedPtr<SWindow> PopupBeforeReload = SelectAnchor->GetMenuWindow();
            const int64 PopupRevision = AcceptedView->GetRevision();
            if (NOT TestTrue(TEXT("native dropdown is open before compatible reload"),
                SelectAnchor->IsOpen() && PopupBeforeReload.IsValid())) { return; }
            if (NOT TestTrue(TEXT("compatible Settings source reload succeeds with dropdown open"),
                Menu->Request_ApplySource(Markup, Style))) { return; }
            TestTrue(TEXT("compatible reload keeps accepted select, popup and draft"),
                Menu->Get_AuthoredView() == AcceptedView
                    && AcceptedView->GetRevision() > PopupRevision
                    && GetControl(Menu, TEXT("settings-display-mode")) == Select
                    && SelectAnchor->IsOpen()
                    && SelectAnchor->GetMenuWindow() == PopupBeforeReload
                    && Menu->Get_DraftSettings() == AcceptedDraft
                    && Menu->Get_SourceError().IsEmpty());
            const TSharedPtr<SWidget> ReloadedCancel = GetControl(Menu, TEXT("settings-cancel"));
            const TSharedPtr<SWidget> ReloadedApply = GetControl(Menu, TEXT("settings-apply"));
            if (NOT TestTrue(TEXT("compatible reload replaces both authored action buttons"),
                ReloadedCancel.IsValid() && ReloadedApply.IsValid()
                    && ReloadedCancel != Cancel && ReloadedApply != Apply)) { return; }
            Cancel = ReloadedCancel;
            Apply = ReloadedApply;
            PressKey(EKeys::Escape, User);
            TestTrue(TEXT("popup Escape is handled by select before the Settings host"),
                Menu->IsActivated() && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Settings
                    && InWorld->IsPaused());
            if (NOT TestTrue(TEXT("select refocuses for reference draft"),
                FocusControl(Menu, TEXT("settings-display-mode")))) { return; }
            TestTrue(TEXT("reference select Down is handled"), PressKey(EKeys::Down, User));
            TestTrue(TEXT("reference draft changes only display mode"),
                Menu->Get_DraftSettings().Get_Volume() == 75.0f
                    && Menu->Get_DraftSettings().Get_Subtitles()
                    && Menu->Get_DraftSettings().Get_DisplayMode() == TEXT("Borderless")
                    && Menu->Get_IsSettingsDirty());
            TestTrue(TEXT("reference Tab moves to Cancel"), PressKey(EKeys::Tab, User)
                && FocusContains(Cancel.ToSharedRef(), User));
            TestTrue(TEXT("reference Tab focuses Apply"), PressKey(EKeys::Tab, User)
                && FocusContains(Apply.ToSharedRef(), User));
            TestTrue(TEXT("dirty Settings reference capture"),
                Capture(InWorld, *State, TEXT("settings-dirty-reference-1920x1080"), FIntPoint{1920, 1080}));
            if (NOT TestTrue(TEXT("select refocuses to discard reference-only draft"),
                FocusControl(Menu, TEXT("settings-display-mode")))) { return; }
            TestTrue(TEXT("select Up returns reference draft to clean Windowed"), PressKey(EKeys::Up, User));
            TestTrue(TEXT("reference capture did not apply fixture values"),
                Menu->Get_DraftSettings() == Menu->Get_AppliedSettings()
                    && NOT Menu->Get_IsSettingsDirty());
            if (NOT TestTrue(TEXT("slider refocuses after popup Escape"),
                FocusControl(Menu, TEXT("settings-volume")))) { return; }
            TestTrue(TEXT("slider Right is handled"), PressKey(EKeys::Right, User));
            TestEqual(TEXT("slider Right drafts volume 76"), Menu->Get_DraftSettings().Get_Volume(), 76.0f);
            TestEqual(TEXT("slider draft does not apply automatically"), Menu->Get_AppliedSettings().Get_Volume(), 75.0f);
            if (NOT TestTrue(TEXT("native checkbox receives keyboard focus"), FocusControl(Menu, TEXT("settings-subtitles"))))
            { return; }
            TestTrue(TEXT("checkbox Space is handled"), PressKey(EKeys::SpaceBar, User));
            TestFalse(TEXT("checkbox drafts subtitles off"), Menu->Get_DraftSettings().Get_Subtitles());
            if (NOT TestTrue(TEXT("native select receives keyboard focus"), FocusControl(Menu, TEXT("settings-display-mode"))))
            { return; }
            TestTrue(TEXT("closed select Down is handled"), PressKey(EKeys::Down, User));
            TestEqual(TEXT("select drafts Borderless"), Menu->Get_DraftSettings().Get_DisplayMode(),
                FString{TEXT("Borderless")});
            TestTrue(TEXT("draft changes enable Apply"), Menu->Get_IsSettingsDirty() && Apply->IsEnabled());
            TestTrue(TEXT("dirty Tab advances select to Cancel"), PressKey(EKeys::Tab, User)
                && FocusContains(Cancel.ToSharedRef(), User));
            TestTrue(TEXT("dirty Tab reaches newly enabled Apply"), PressKey(EKeys::Tab, User)
                && FocusContains(Apply.ToSharedRef(), User));
            TestTrue(TEXT("all-fields dirty Settings capture"),
                Capture(InWorld, *State, TEXT("settings-dirty-all-fields-1920x1080"), FIntPoint{1920, 1080}));
            TestTrue(TEXT("Apply Enter is handled"), PressKey(EKeys::Enter, User));
            TestTrue(TEXT("Apply commits all three fixture fields"),
                Menu->Get_AppliedSettings() == Menu->Get_DraftSettings()
                    && Menu->Get_AppliedSettings().Get_Volume() == 76.0f
                    && NOT Menu->Get_AppliedSettings().Get_Subtitles()
                    && Menu->Get_AppliedSettings().Get_DisplayMode() == TEXT("Borderless"));
            TestTrue(TEXT("Apply stays in Settings and clears dirty state"),
                Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Settings
                    && NOT Menu->Get_IsSettingsDirty() && NOT Apply->IsEnabled());
            if (NOT TestTrue(TEXT("Select receives focus for a new uncommitted draft"),
                FocusControl(Menu, TEXT("settings-display-mode")))) { return; }
            PressKey(EKeys::Up, User);
            TestTrue(TEXT("new draft differs before direct Escape"), Menu->Get_IsSettingsDirty());
            PressKey(EKeys::Escape, User);
            State->RestoreWindow();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && NOT World->IsPaused()
                && NOT IsValid(GetActiveMenu(GetPlayer(World)));
        }), 10.0, TEXT("direct Settings Escape discards draft, closes and unpauses")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            if (NOT TestTrue(TEXT("viewport focus is restored after direct Settings Escape"),
                FocusViewport(InWorld))) { return; }
            PressKey(EKeys::Two);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsSettingsActive(ck::auto_test::net::Get_ServerWorld()); }),
        10.0, TEXT("fresh [2] reopens Settings with controller-owned applied fixture values")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (NOT TestNotNull(TEXT("reopened Settings host"), Menu)) { return; }
            TestTrue(TEXT("reopen preserves applied fixture values and discards prior draft"),
                Menu->Get_AppliedSettings().Get_Volume() == 76.0f
                    && NOT Menu->Get_AppliedSettings().Get_Subtitles()
                    && Menu->Get_AppliedSettings().Get_DisplayMode() == TEXT("Borderless")
                    && Menu->Get_DraftSettings() == Menu->Get_AppliedSettings()
                    && NOT Menu->Get_IsSettingsDirty());
            if (NOT TestTrue(TEXT("native checkbox focuses for canceled edit"),
                FocusControl(Menu, TEXT("settings-subtitles")))) { return; }
            PressKey(EKeys::SpaceBar, Menu->Get_SlateUserIndex());
            TestTrue(TEXT("checkbox supplies uncommitted draft"), Menu->Get_IsSettingsDirty());
            if (NOT TestTrue(TEXT("Cancel button receives focus"), FocusControl(Menu, TEXT("settings-cancel"))))
            { return; }
            TestTrue(TEXT("Cancel action routes via Enter"), PressKey(EKeys::Enter, Menu->Get_SlateUserIndex()));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && NOT World->IsPaused()
                && NOT IsValid(GetActiveMenu(GetPlayer(World)));
        }), 10.0, TEXT("direct Cancel closes and releases pause")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            if (NOT TestTrue(TEXT("viewport focus returns for [1]"), FocusViewport(InWorld))) { return; }
            PressKey(EKeys::One);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(ck::auto_test::net::Get_ServerWorld()));
            return IsValid(Menu) && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Pause;
        }), 10.0, TEXT("real [1] opens Pause after Settings Cancel")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (NOT TestNotNull(TEXT("nested flow has Pause host"), Menu)) { return; }
            const TSharedPtr<FCkUiView> PauseView = Menu->Get_AuthoredView();
            const TSharedPtr<SButton> SettingsButton = PauseView.IsValid()
                ? PauseView->GetButton(TEXT("settings-button")) : nullptr;
            if (NOT TestTrue(TEXT("Pause has authored Settings action"), SettingsButton.IsValid())) { return; }
            FSlateApplication& Slate = FSlateApplication::Get();
            Slate.SetUserFocus(Menu->Get_SlateUserIndex(), SettingsButton.ToSharedRef(), EFocusCause::SetDirectly);
            Tick(Slate);
            TestTrue(TEXT("Pause Settings action routes through native Enter"),
                PressKey(EKeys::Enter, Menu->Get_SlateUserIndex()));
            if (NOT TestEqual(TEXT("nested Settings uses same activated host"),
                Menu->Get_Screen(), ECk_UiAuthoringProof_Screen::Settings)) { return; }
            TestTrue(TEXT("nested transition retains pause and releases old Pause view"),
                InWorld->IsPaused() && PauseView->IsOwnerReleased());
            const int32 SettingsCount = Menu->Get_ActionCount(FName{TEXT("SettingsRequested")});
            const FKeyEvent StaleButtonEvent{EKeys::Enter, FModifierKeysState{},
                static_cast<uint32>(Menu->Get_SlateUserIndex()), false, 0, 0};
            SettingsButton->OnKeyDown(SettingsButton->GetCachedGeometry(), StaleButtonEvent);
            SettingsButton->OnKeyUp(SettingsButton->GetCachedGeometry(), StaleButtonEvent);
            TestTrue(TEXT("stale Pause action cannot redispatch after owner release"),
                Menu->Get_ActionCount(FName{TEXT("SettingsRequested")}) == SettingsCount
                    && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Settings);
            TestTrue(TEXT("nested Settings keeps applied fixture values"),
                Menu->Get_AppliedSettings().Get_Volume() == 76.0f
                    && NOT Menu->Get_AppliedSettings().Get_Subtitles()
                    && Menu->Get_AppliedSettings().Get_DisplayMode() == TEXT("Borderless"));
            const TSharedPtr<FCkUiView> SettingsView = Menu->Get_AuthoredView();
            const TSharedPtr<SWidget> SettingsSelect = GetControl(Menu, TEXT("settings-display-mode"));
            if (NOT TestTrue(TEXT("nested Settings select is available for replacement lifetime"),
                SettingsView.IsValid() && SettingsSelect.IsValid()
                    && SettingsSelect->GetTypeAsString() == TEXT("SCkUiSelect"))
                || NOT TestTrue(TEXT("nested select accepts focus"),
                    FocusControl(Menu, TEXT("settings-display-mode")))) { return; }
            TestTrue(TEXT("nested dropdown opens through Space"),
                PressKey(EKeys::SpaceBar, Menu->Get_SlateUserIndex()));
            const TSharedRef<SMenuAnchor> NestedAnchor = StaticCastSharedRef<SMenuAnchor>(SettingsSelect.ToSharedRef());
            if (NOT TestTrue(TEXT("nested dropdown is open before screen replacement"),
                NestedAnchor->IsOpen())) { return; }
            if (NOT TestTrue(TEXT("replacing Settings view with Pause succeeds"),
                Menu->Request_ShowScreen(ECk_UiAuthoringProof_Screen::Pause))) { return; }
            TestTrue(TEXT("old Settings view replacement closes its popup and preserves pause"),
                SettingsView->IsOwnerReleased() && NOT NestedAnchor->IsOpen()
                    && Menu->Get_AuthoredView() != SettingsView
                    && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Pause
                    && Menu->IsActivated() && InWorld->IsPaused());
            const TSharedPtr<FCkUiView> FreshPauseView = Menu->Get_AuthoredView();
            const TSharedPtr<SButton> FreshSettingsButton = FreshPauseView.IsValid()
                ? FreshPauseView->GetButton(TEXT("settings-button")) : nullptr;
            if (NOT TestTrue(TEXT("replacement Pause offers fresh Settings action"),
                FreshSettingsButton.IsValid())) { return; }
            FSlateApplication::Get().SetUserFocus(Menu->Get_SlateUserIndex(),
                FreshSettingsButton.ToSharedRef(), EFocusCause::SetDirectly);
            Tick(FSlateApplication::Get());
            if (NOT TestTrue(TEXT("fresh Pause action reenters nested Settings through Enter"),
                PressKey(EKeys::Enter, Menu->Get_SlateUserIndex())
                    && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Settings)) { return; }
            PressKey(EKeys::Escape, Menu->Get_SlateUserIndex());
            TestTrue(TEXT("nested Escape returns to Pause without releasing pause"),
                Menu->IsActivated() && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Pause
                    && InWorld->IsPaused() && Menu->Get_AuthoredView().IsValid());
            PressKey(EKeys::Escape, Menu->Get_SlateUserIndex());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && NOT World->IsPaused()
                && NOT IsValid(GetActiveMenu(GetPlayer(World)));
        }), 10.0, TEXT("second Escape closes Pause and releases pause ownership")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            if (NOT TestTrue(TEXT("viewport focus returns for final Settings teardown"), FocusViewport(InWorld)))
            { return; }
            PressKey(EKeys::Two);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsSettingsActive(ck::auto_test::net::Get_ServerWorld()); }),
        10.0, TEXT("final [2] opens Settings before actual PIE teardown")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (NOT TestTrue(TEXT("final direct Settings is active before real PIE teardown"),
                IsValid(Menu) && Menu->IsActivated()
                    && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Settings
                    && InWorld->IsPaused())) { return; }
            State->CleanupWorld = InWorld;
            State->MenuAtCleanup = Menu;
            const TWeakPtr<FState> WeakState = State;
            State->CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
                [WeakState](UWorld* InCleanupWorld, bool, bool)
                {
                    const TSharedPtr<FState> Observed = WeakState.Pin();
                    if (NOT Observed.IsValid() || InCleanupWorld != Observed->CleanupWorld.Get())
                    { return; }
                    Observed->bObservedCleanup = true;
                    UCk_PauseMenuProof_UE* ObservedMenu = Observed->MenuAtCleanup.Get();
                    Observed->bInactiveAtCleanup = ObservedMenu != nullptr
                        && NOT ObservedMenu->IsActivated();
                    Observed->bViewReleasedAtCleanup = ObservedMenu != nullptr
                        && NOT ObservedMenu->Get_AuthoredView().IsValid();
                });
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            State->RemoveCleanupObserver();
            State->RestoreWindow();
            AddInfo(FString::Printf(TEXT("Settings EndPlay observation: cleanup=%d inactive=%d view released=%d"),
                State->bObservedCleanup, State->bInactiveAtCleanup, State->bViewReleasedAtCleanup));
            return State->bObservedCleanup && State->bInactiveAtCleanup && State->bViewReleasedAtCleanup;
        }), TEXT("actual gym EndPlay closes Settings and releases authored ownership before GC")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            RestoreSettings(*State);
            const UCkGym_StartupSettings* Saved = GetDefault<UCkGym_StartupSettings>();
            return IsValid(Saved) && Saved->StartupMode == State->StartupMode
                && Saved->ControlPanelMode == State->PanelMode
                && Saved->DefaultGymName == State->DefaultGymName
                && Saved->LastGymName == State->LastGymName
                && Saved->RecentGymNames == State->RecentGymNames;
        }), TEXT("Settings proof restores startup, panel, default, last and recents preferences")));
    return true;
}

#endif
