#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkGym_Switchboard_Subsystem.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "UnitTests/CkYoga/CkScriptedPause_TestAdapter.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace ck_ui_authoring_gym_test
{
    constexpr TCHAR GymName[] = TEXT("UI Authoring");
    constexpr TCHAR GymClassName[] = TEXT("Ck_UiAuthoringGym_GameMode");
    constexpr TCHAR ControllerClassName[] = TEXT("Ck_UiAuthoringGym_PlayerController");

    struct FState;
    auto RestoreSettings(FState& InState) -> void;

    struct FState
    {
        ~FState()
        {
            RemoveCleanupObserver();
            FirstMenu.Reset();
            SecondMenu.Reset();
            RestoreWindow();
            RestoreSettings(*this);
        }

        auto RestoreWindow() -> void
        {
            if (NOT bWindowCaptured) { return; }
            if (const TSharedPtr<SWindow> Window = WindowToRestore.Pin())
            { Window->Resize(OriginalWindowSize); }
            bWindowCaptured = false;
        }

        auto RemoveCleanupObserver() -> void
        {
            if (WorldCleanupHandle.IsValid())
            {
                FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
                WorldCleanupHandle.Reset();
            }
        }

        ECkGym_StartupMode StartupMode = ECkGym_StartupMode::Cycler;
        ECkGym_ControlPanel_Mode PanelMode = ECkGym_ControlPanel_Mode::Full;
        FString DefaultGymName;
        FString LastGymName;
        TArray<FString> RecentGymNames;
        TWeakPtr<SWidget> PreviousFocus;
        bool bSettingsCaptured = false;
        bool bSettingsRestored = false;
        bool bFocusCaptured = false;
        TWeakPtr<SWindow> WindowToRestore;
        FVector2D OriginalWindowSize = FVector2D::ZeroVector;
        bool bWindowCaptured = false;
        int32 RegistryIndex = INDEX_NONE;
        TStrongObjectPtr<UCommonActivatableWidget> FirstMenu;
        TStrongObjectPtr<UCommonActivatableWidget> SecondMenu;
        TWeakObjectPtr<UWorld> CleanupWorld;
        TWeakObjectPtr<UCommonActivatableWidget> MenuAtCleanup;
        FDelegateHandle WorldCleanupHandle;
        bool bObservedWorldCleanup = false;
        bool bMenuDeactivatedAtCleanup = false;
        bool bViewReleasedAtCleanup = false;
    };

    auto GetRegistry(UWorld* InWorld) -> UCkGym_Registry_Subsystem*
    {
        UGameInstance* Instance = IsValid(InWorld) ? InWorld->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }

    auto GetPlayer(UWorld* InWorld) -> APlayerController*
    {
        return IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
    }

    auto GetSwitchboard(APlayerController* InPlayer) -> UCkGym_Switchboard_Subsystem*
    {
        ULocalPlayer* Local = IsValid(InPlayer) ? InPlayer->GetLocalPlayer() : nullptr;
        return IsValid(Local) ? Local->GetSubsystem<UCkGym_Switchboard_Subsystem>() : nullptr;
    }

    auto IsGymReady(UWorld* InWorld) -> bool
    {
        const AGameModeBase* Mode = IsValid(InWorld) ? InWorld->GetAuthGameMode() : nullptr;
        const APlayerController* Player = GetPlayer(InWorld);
        return IsValid(Mode) && Mode->GetClass()->GetName().Contains(GymClassName)
            && IsValid(Player) && Player->GetClass()->GetName().Contains(ControllerClassName)
            && IsValid(Player->GetLocalPlayer()) && IsValid(GetSwitchboard(GetPlayer(InWorld)));
    }

    auto GetPreviewActive(APlayerController* InPlayer) -> bool
    {
        UFunction* Function = IsValid(InPlayer)
            ? InPlayer->FindFunction(FName{TEXT("Get_IsPreviewActive")}) : nullptr;
        const FBoolProperty* Return = Function != nullptr ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr) { return false; }
        TArray<uint8> Params;
        Params.SetNumZeroed(Function->ParmsSize);
        Function->InitializeStruct(Params.GetData());
        InPlayer->ProcessEvent(Function, Params.GetData());
        const bool Result = Return->GetPropertyValue_InContainer(Params.GetData());
        Function->DestroyStruct(Params.GetData());
        return Result;
    }

    auto GetActiveScriptPause(APlayerController* InPlayer) -> UCommonActivatableWidget*
    { return Cast<UCommonActivatableWidget>(ck_scripted_pause_test::Object(InPlayer, TEXT("Get_ActiveScriptPause"))); }

    auto GetScriptSession(UCommonActivatableWidget* InMenu) -> UCk_UI_AuthoredSession_UE*
    { return Cast<UCk_UI_AuthoredSession_UE>(ck_scripted_pause_test::Object(InMenu, TEXT("Get_Session"))); }

    auto GetScriptView(UCommonActivatableWidget* InMenu) -> TSharedPtr<FCkUiView>
    { const auto* Session = GetScriptSession(InMenu); return IsValid(Session) ? Session->Get_View() : nullptr; }

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

    auto FindType(const TSharedRef<SWidget>& InRoot, const FString& InType) -> TSharedPtr<SWidget>
    {
        if (InRoot->GetTypeAsString() == InType) { return InRoot; }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SWidget> Found = FindType(
                ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InType); Found.IsValid())
            { return Found; }
        }
        return nullptr;
    }

    auto CheckPresentation(FAutomationTestBase& InTest, const TSharedPtr<FCkUiView>& InView) -> bool
    {
        if (NOT InView.IsValid()) { return false; }
        const TSharedRef<SWidget> Root = InView->GetRegion(TEXT("main"));
        bool Passed = true;
        for (const TCHAR* Id : {TEXT("pause-title"), TEXT("pause-subtitle"),
            TEXT("resume-button"), TEXT("settings-button"), TEXT("return-button"),
            TEXT("resume-hint-label")})
        {
            const TSharedPtr<SWidget> Tagged = FindTagged(Root, FName{Id});
            const TSharedPtr<SWidget> Found = Tagged.IsValid()
                ? FindType(Tagged.ToSharedRef(), TEXT("SCkFlexText")) : nullptr;
            if (NOT Found.IsValid())
            {
                Passed = InTest.TestTrue(FString{Id} + TEXT(" native text exists"), false) && Passed;
                continue;
            }
            const TSharedPtr<SCkFlexText> Text = StaticCastSharedPtr<SCkFlexText>(Found);
            const FGeometry& Geometry = Text->GetCachedGeometry();
            const FVector2D Allocated = Geometry.GetLocalSize();
            const bool Button = FString{Id}.EndsWith(TEXT("-button"));
            const FVector2D Required = Text->Measure(Button ? YGUndefined : Allocated.X,
                Button ? YGMeasureModeUndefined : YGMeasureModeAtMost,
                YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
            Passed = InTest.TestTrue(FString::Printf(TEXT("%s native height %.2f fits %.2f"),
                Id, Required.Y, Allocated.Y), Required.Y > 0.0 && Required.Y <= Allocated.Y + 0.5) && Passed;
            if (Button)
            {
                Passed = InTest.TestTrue(FString{Id} + TEXT(" unwrapped label fits content width"),
                    Required.X <= Allocated.X + 0.5) && Passed;
            }
            Passed = InTest.TestTrue(FString{Id} + TEXT(" uses admitted menu font"),
                Text->GetFont().GetCompositeFont()
                    != FCoreStyle::GetDefaultFontStyle("Regular", 18).GetCompositeFont()) && Passed;
        }
        const TSharedPtr<SWidget> Hint = FindTagged(Root, FName{TEXT("resume-keycap")});
        const TSharedPtr<SWidget> NativeHint = Hint.IsValid()
            ? FindType(Hint.ToSharedRef(), TEXT("SKeyHint")) : nullptr;
        Passed = InTest.TestTrue(TEXT("Pause uses shared passive key hint"),
            NativeHint.IsValid() && NOT NativeHint->SupportsKeyboardFocus()
                && NativeHint->GetVisibility() == EVisibility::SelfHitTestInvisible) && Passed;
        return Passed;
    }

    auto Capture(FAutomationTestBase& InTest, UWorld* InWorld, FState& InState,
        const TSharedPtr<FCkUiView>& InView, const FString& InName, FIntPoint InSize) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (NOT Viewport.IsValid()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid()) { return false; }
        if (NOT InState.bWindowCaptured)
        {
            InState.WindowToRestore = Window;
            InState.OriginalWindowSize = Window->GetClientSizeInScreen();
            InState.bWindowCaptured = true;
        }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Slate.PumpMessages();
            Slate.Tick();
            Slate.Tick();
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
        const bool PresentationFits = CheckPresentation(InTest, InView);
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/UiAuthoringGym"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const bool Saved = FImageUtils::SaveImageByExtension(
            *FPaths::Combine(Directory, InName + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
        return PresentationFits && Saved;
    }

    auto PressKey(const FKey& InKey) -> bool
    {
        if (NOT FSlateApplication::IsInitialized()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Event{InKey, FModifierKeysState{}, 0, false, 0, 0};
        const bool DownHandled = Slate.ProcessKeyDownEvent(Event);
        const bool UpHandled = Slate.ProcessKeyUpEvent(Event);
        return DownHandled && UpHandled;
    }

    auto FocusContains(const TSharedRef<SWidget>& InTarget, int32 InUser) -> bool
    {
        if (NOT FSlateApplication::IsInitialized()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWidget> Focused = Slate.GetUserFocusedWidget(InUser);
        FWidgetPath Path;
        if (NOT Focused.IsValid() || NOT Slate.GeneratePathToWidgetUnchecked(Focused.ToSharedRef(), Path))
        { return false; }
        for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
        {
            if (Path.Widgets[Index].Widget == InTarget) { return true; }
        }
        return false;
    }

    auto FocusViewport(UWorld* InWorld, FState& InState, FAutomationTestBase& InTest) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (NOT Viewport.IsValid() || NOT FSlateApplication::IsInitialized()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid()) { return false; }
        if (!InState.bFocusCaptured)
        {
            InState.PreviousFocus = Slate.GetUserFocusedWidget(0);
            InState.bFocusCaptured = true;
        }
        Window->BringToFront(true);
        Slate.SetUserFocus(0, Viewport.ToSharedRef(), EFocusCause::SetDirectly);
        const TSharedPtr<SWidget> Focused = Slate.GetUserFocusedWidget(0);
        InTest.AddInfo(FString::Printf(
            TEXT("gym viewport input ownership: app active=%d, window active=%d, focused=%s"),
            Slate.IsActive(), Window->IsActive(),
            Focused.IsValid() ? *Focused->GetTypeAsString() : TEXT("<none>")));
        return Slate.IsActive() && Window->IsActive() && Focused.Get() == Viewport.Get();
    }

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_UiAuthoringGymSelector_PIE,
    "Ck.UiAuthoring.PauseMenuProof.GymSelector.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

bool FCk_UiAuthoringGymSelector_PIE::RunTest(const FString&)
{
    using namespace ck_ui_authoring_gym_test;
    if (NOT FSlateApplication::IsInitialized())
    {
        AddError(TEXT("Gym selector PIE test requires Slate."));
        return false;
    }

    const TSharedRef<FState> State = MakeShared<FState>();
    UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    if (NOT TestNotNull(TEXT("per-user gym startup settings"), Settings)) { return false; }
    State->StartupMode = Settings->StartupMode;
    State->PanelMode = Settings->ControlPanelMode;
    State->DefaultGymName = Settings->DefaultGymName;
    State->LastGymName = Settings->LastGymName;
    State->RecentGymNames = Settings->RecentGymNames;
    State->bSettingsCaptured = true;
    // Keep the launcher in its selector state regardless of the developer's saved auto-start choice.
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
        }), 15.0, TEXT("UI Authoring registers in the real gym registry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            UCkGym_Registry_Subsystem* Registry = GetRegistry(InWorld);
            if (NOT TestNotNull(TEXT("launcher registry"), Registry)) { return; }
            State->RegistryIndex = Registry->Find_GymIndexByName(GymName);
            if (NOT TestTrue(TEXT("UI Authoring registry entry exists"),
                Registry->Get_Entries().IsValidIndex(State->RegistryIndex))) { return; }
            const FCkGym_Entry& Entry = Registry->Get_Entries()[State->RegistryIndex];
            AddInfo(FString::Printf(TEXT("UI Authoring registered GameMode class: %s"),
                *GetNameSafe(Entry.GameModeClass.Get())));
            TestEqual(TEXT("UI Authoring is in CkUI category"), Entry.Category, FString{TEXT("CkUI")});
            TestTrue(TEXT("UI Authoring registers the AS gym GameMode"),
                IsValid(Entry.GameModeClass.Get())
                    && Entry.GameModeClass.Get()->GetName().Contains(GymClassName));
            bool bInSelector = false;
            for (const FCkGym_Switchboard_Group& Group :
                UCkGym_Switchboard_Subsystem::Build_Groups(Registry->Get_Entries()))
            {
                if (Group.Category != TEXT("CkUI")) { continue; }
                for (const FCkGym_Switchboard_Row& Row : Group.Rows)
                {
                    bInSelector |= Row.RegistryIndex == State->RegistryIndex
                        && Row.DisplayName == GymName;
                }
            }
            TestTrue(TEXT("real selector model contains the UI Authoring row"), bInSelector);
            if (bInSelector && IsValid(Entry.GameModeClass.Get()))
            { Registry->Request_TravelToGym(State->RegistryIndex); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsGymReady(ck::auto_test::net::Get_ServerWorld()); }),
        30.0, TEXT("registry travel loads UI Authoring GameMode and PlayerController")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            APlayerController* Player = GetPlayer(InWorld);
            UCkGym_Switchboard_Subsystem* Switchboard = GetSwitchboard(Player);
            AddInfo(FString::Printf(TEXT("UI Authoring traveled GameMode: %s; PlayerController: %s"),
                *GetNameSafe(IsValid(InWorld) && IsValid(InWorld->GetAuthGameMode())
                    ? InWorld->GetAuthGameMode()->GetClass() : nullptr),
                *GetNameSafe(IsValid(Player) ? Player->GetClass() : nullptr)));
            TestTrue(TEXT("registry travel landed on the actual gym and controller"), IsGymReady(InWorld));
            TestTrue(TEXT("gym selector starts closed"), IsValid(Switchboard) && NOT Switchboard->Get_IsOpen());
            if (NOT IsGymReady(InWorld) || NOT TestTrue(TEXT("PIE viewport takes keyboard focus"),
                FocusViewport(InWorld, *State, *this))) { return; }
            TestFalse(TEXT("gym menu starts closed"), GetPreviewActive(Player));
            if (NOT TestNotNull(TEXT("scripted pause accessor"),
                IsValid(Player) ? Player->FindFunction(FName{TEXT("Get_ActiveScriptPause")}) : nullptr)) { return; }
            // The panel's CkInput capture, not Slate's handled flag, proves delivery below.
            PressKey(EKeys::One);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            APlayerController* Player = GetPlayer(ck::auto_test::net::Get_ServerWorld());
            return GetPreviewActive(Player) && IsValid(GetActiveScriptPause(Player));
        }), 10.0, TEXT("panel [1] opens the scripted authored pause menu")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            APlayerController* Player = GetPlayer(InWorld);
            UCommonActivatableWidget* Menu = GetActiveScriptPause(Player);
            State->FirstMenu.Reset(Menu);
            UCkGym_Switchboard_Subsystem* Switchboard = GetSwitchboard(Player);
            TestTrue(TEXT("[1] activated scripted pause menu"), IsValid(Menu) && Menu->IsActivated()
                && ck_scripted_pause_test::Bool(Menu, TEXT("Get_IsCurrent")));
            if (NOT TestNotNull(TEXT("gym switchboard for menu input assertions"), Switchboard)
                || NOT IsValid(Menu) || NOT Menu->IsActivated()) { return; }
            const UCkGym_Registry_Subsystem* Registry = GetRegistry(InWorld);
            TestTrue(TEXT("registry travel selected the registered GameMode"),
                IsValid(Registry) && Registry->Get_Entries().IsValidIndex(Registry->Get_CurrentGymIndex())
                    && IsValid(InWorld->GetAuthGameMode())
                    && Registry->Get_Entries()[Registry->Get_CurrentGymIndex()].GameModeClass.Get()
                        == InWorld->GetAuthGameMode()->GetClass());
            TestTrue(TEXT("menu pauses the world"), IsValid(InWorld) && InWorld->IsPaused());
            TestTrue(TEXT("selector stays closed while pause menu is active"),
                IsValid(Switchboard) && NOT Switchboard->Get_IsOpen());
            UCk_UI_AuthoredSession_UE* Session = GetScriptSession(Menu);
            TestTrue(TEXT("scripted pause owns a usable authored session"),
                IsValid(Session) && Session->Get_IsUsable() && Session->Get_IsInputActive());
            const TSharedPtr<FCkUiView> View = GetScriptView(Menu);
            const TSharedPtr<SButton> Resume = View.IsValid() ? View->GetButton(TEXT("resume-button")) : nullptr;
            const TSharedPtr<SButton> SettingsButton = View.IsValid()
                ? View->GetButton(TEXT("settings-button")) : nullptr;
            const int32 User = IsValid(Session) ? Session->Get_SlateUserIndex() : INDEX_NONE;
            TestTrue(TEXT("initial gym menu focus lands on Resume"),
                User >= 0 && Resume.IsValid() && FocusContains(Resume.ToSharedRef(), User)
                    && IsValid(Session) && Session->Get_IsButtonFocused(TEXT("resume-button")));
            const auto CaptureFocused = [&](const TCHAR* Name, FIntPoint Size)
            {
                TestTrue(FString::Printf(TEXT("scripted pause native capture %s"), Name),
                    Capture(*this, InWorld, *State, View, Name, Size));
                TestTrue(FString::Printf(TEXT("Resume keeps focus after %s resize"), Name),
                    User >= 0 && Resume.IsValid() && FocusContains(Resume.ToSharedRef(), User)
                        && IsValid(Session) && Session->Get_IsButtonFocused(TEXT("resume-button")));
            };
            CaptureFocused(TEXT("scripted-pause-1280x720"), FIntPoint{1280, 720});
            CaptureFocused(TEXT("scripted-pause-1920x1080"), FIntPoint{1920, 1080});
            CaptureFocused(TEXT("scripted-pause-2560x1080"), FIntPoint{2560, 1080});
            CaptureFocused(TEXT("scripted-pause-restored-1920x1080"), FIntPoint{1920, 1080});
            TestTrue(TEXT("Tab navigates from Resume to Settings"), PressKey(EKeys::Tab)
                && User >= 0 && SettingsButton.IsValid()
                && FocusContains(SettingsButton.ToSharedRef(), User)
                && IsValid(Session) && Session->Get_IsButtonFocused(TEXT("settings-button")));
            TestFalse(TEXT("Tab did not open the gym switchboard"),
                IsValid(Switchboard) && Switchboard->Get_IsOpen());
            TestTrue(TEXT("Escape routes through the active authored menu"), PressKey(EKeys::Escape));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && NOT GetPreviewActive(GetPlayer(World)) && NOT World->IsPaused();
        }), 10.0, TEXT("Escape closes and unpauses the scripted menu")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            APlayerController* Player = GetPlayer(InWorld);
            TestFalse(TEXT("Escape released the first menu"), GetPreviewActive(Player));
            TestTrue(TEXT("Escape released world pause"), IsValid(InWorld) && NOT InWorld->IsPaused());
            if (UCommonActivatableWidget* FirstMenu = State->FirstMenu.Get())
            {
                TestFalse(TEXT("Escape deactivated scripted pause"), FirstMenu->IsActivated());
                TestFalse(TEXT("Escape released authored-view ownership"), GetScriptView(FirstMenu).IsValid());
            }
            if (NOT TestTrue(TEXT("PIE viewport regains keyboard focus after Escape"),
                FocusViewport(InWorld, *State, *this))) { return; }
            PressKey(EKeys::One);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && World->IsPaused() && GetPreviewActive(GetPlayer(World));
        }), 10.0, TEXT("fresh [1] opens a new pause menu after Escape")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            APlayerController* Player = GetPlayer(InWorld);
            UCommonActivatableWidget* Menu = GetActiveScriptPause(Player);
            State->SecondMenu.Reset(Menu);
            TestTrue(TEXT("fresh menu is active before teardown"), IsValid(Menu) && Menu->IsActivated()
                && ck_scripted_pause_test::Bool(Menu, TEXT("Get_IsCurrent")));
            TestTrue(TEXT("fresh open created a distinct scripted menu"),
                IsValid(Menu) && Menu != State->FirstMenu.Get());
            TestTrue(TEXT("fresh menu owns an authored view before teardown"), GetScriptView(Menu).IsValid());
            TestTrue(TEXT("fresh menu still owns pause before actual PIE teardown"),
                IsValid(InWorld) && InWorld->IsPaused());
            if (IsValid(InWorld) && IsValid(Menu))
            {
                State->CleanupWorld = InWorld;
                State->MenuAtCleanup = Menu;
                const TWeakPtr<FState> WeakState = State;
                State->WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
                    [WeakState](UWorld* InCleanupWorld, bool, bool)
                    {
                        const TSharedPtr<FState> Observed = WeakState.Pin();
                        if (NOT Observed.IsValid() || InCleanupWorld != Observed->CleanupWorld.Get())
                        { return; }
                        Observed->bObservedWorldCleanup = true;
                        UCommonActivatableWidget* ObservedMenu = Observed->MenuAtCleanup.Get();
                        Observed->bMenuDeactivatedAtCleanup = ObservedMenu != nullptr
                            && NOT ObservedMenu->IsActivated();
                        Observed->bViewReleasedAtCleanup = ObservedMenu != nullptr
                            && NOT GetScriptView(ObservedMenu).IsValid();
                    });
            }
            // The controller remains the menu owner until its actual EndPlay. Test roots must not
            // keep the widget (and its PIE GameInstance outer) alive into engine garbage collection.
            State->FirstMenu.Reset();
            State->SecondMenu.Reset();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            State->RemoveCleanupObserver();
            AddInfo(FString::Printf(TEXT("gym PIE cleanup observed=%d, menu deactivated=%d, view released=%d"),
                State->bObservedWorldCleanup, State->bMenuDeactivatedAtCleanup,
                State->bViewReleasedAtCleanup));
            return State->bObservedWorldCleanup && State->bMenuDeactivatedAtCleanup
                && State->bViewReleasedAtCleanup;
        }), TEXT("actual PIE EndPlay closes the second menu and releases its authored view before world cleanup")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            RestoreSettings(*State);
            const UCkGym_StartupSettings* SettingsNow = GetDefault<UCkGym_StartupSettings>();
            return IsValid(SettingsNow) && SettingsNow->StartupMode == State->StartupMode
                && SettingsNow->ControlPanelMode == State->PanelMode
                && SettingsNow->DefaultGymName == State->DefaultGymName
                && SettingsNow->LastGymName == State->LastGymName
                && SettingsNow->RecentGymNames == State->RecentGymNames;
        }), TEXT("gym startup mode, panel mode, default, last and recents restored")));
    return true;
}

#endif
