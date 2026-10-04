#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkGym_Switchboard_Subsystem.h"
#include "CkSlateLayout/SCkUiRepeat.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/UI/CkInventoryMenuProof.h"
#include "CkTests/UI/CkPauseMenuProof.h"
#include "UI/CkInventoryMenuProofModel.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
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
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_inventory_menu_proof_test
{
    constexpr TCHAR GymName[] = TEXT("UI Authoring");
    constexpr TCHAR GymModeName[] = TEXT("Ck_UiAuthoringGym_GameMode");
    constexpr TCHAR GymPlayerName[] = TEXT("Ck_UiAuthoringGym_PlayerController");

    struct FState
    {
        ~FState()
        {
            if (CleanupHandle.IsValid()) { FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle); }
            RestoreWindow();
            RestoreSettings();
        }

        void RestoreWindow()
        {
            if (bWindowCaptured)
            {
                if (const TSharedPtr<SWindow> Window = WindowToRestore.Pin())
                { Window->Resize(OriginalWindowSize); }
                bWindowCaptured = false;
            }
        }

        void RestoreSettings()
        {
            if (!bSettingsCaptured || bSettingsRestored) { return; }
            UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            if (!IsValid(Settings)) { return; }
            Settings->StartupMode = StartupMode;
            Settings->ControlPanelMode = PanelMode;
            Settings->DefaultGymName = DefaultGymName;
            Settings->LastGymName = LastGymName;
            Settings->RecentGymNames = RecentGymNames;
            Settings->SaveConfig();
            bSettingsRestored = Settings->StartupMode == StartupMode
                && Settings->ControlPanelMode == PanelMode
                && Settings->DefaultGymName == DefaultGymName
                && Settings->LastGymName == LastGymName
                && Settings->RecentGymNames == RecentGymNames;
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
        bool bWindowCaptured = false;
        TWeakObjectPtr<UWorld> CleanupWorld;
        TWeakObjectPtr<UCk_PauseMenuProof_UE> MenuAtCleanup;
        FDelegateHandle CleanupHandle;
        bool bObservedCleanup = false;
        bool bInactiveAtCleanup = false;
        bool bViewReleasedAtCleanup = false;
    };

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
            && IsValid(Player->GetLocalPlayer()) && IsValid(GetSwitchboard(GetPlayer(InWorld)));
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

    auto IsInventoryActive(UWorld* InWorld) -> bool
    {
        UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
        return IsValid(Menu) && Menu->IsActivated()
            && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Inventory;
    }

    void Tick(FSlateApplication& InSlate)
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
        if (!Focused.IsValid() || !Slate.GeneratePathToWidgetUnchecked(Focused.ToSharedRef(), Path))
        { return false; }
        for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
        { if (Path.Widgets[Index].Widget == InTarget) { return true; } }
        return false;
    }

    auto FocusViewport(UWorld* InWorld) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Viewport.IsValid()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true);
        Slate.SetUserFocus(0, Viewport.ToSharedRef(), EFocusCause::SetDirectly);
        return Slate.IsActive() && Window->IsActive()
            && Slate.GetUserFocusedWidget(0).Get() == Viewport.Get();
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

    auto HasText(const TSharedRef<SWidget>& InRoot, const FString& InExpected) -> bool
    {
        const FString Type = InRoot->GetTypeAsString();
        if (Type == TEXT("SCkFlexText")
            && StaticCastSharedRef<SCkFlexText>(InRoot)->GetText().ToString() == InExpected)
        { return true; }
        if (Type == TEXT("STextBlock")
            && StaticCastSharedRef<STextBlock>(InRoot)->GetText().ToString() == InExpected)
        { return true; }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (HasText(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InExpected))
            { return true; }
        }
        return false;
    }

    auto GetCard(UCk_PauseMenuProof_UE* InMenu, const TCHAR* InKey) -> TSharedPtr<SWidget>
    {
        if (!IsValid(InMenu)) { return nullptr; }
        const FString Path = FString::Printf(TEXT("inventory-items/%s/inventory-card"), InKey);
        const TSharedPtr<SWidget> Card = InMenu->Get_ControlWidget(FName{*Path});
        return Card.IsValid() && Card->GetTypeAsString() == TEXT("SActionCard") ? Card : nullptr;
    }

    auto GetRepeat(UCk_PauseMenuProof_UE* InMenu) -> TSharedPtr<SCkUiRepeat>
    {
        const TSharedPtr<FCkUiView> View = IsValid(InMenu) ? InMenu->Get_AuthoredView() : nullptr;
        return View.IsValid() ? View->GetRepeat(TEXT("inventory-items")) : nullptr;
    }

    auto GetSearch(UCk_PauseMenuProof_UE* InMenu) -> TSharedPtr<SSearchBox>
    {
        const TSharedPtr<FCkUiView> View = IsValid(InMenu) ? InMenu->Get_AuthoredView() : nullptr;
        const TSharedPtr<SWidget> Tagged = View.IsValid()
            ? FindTagged(View->GetRegion(TEXT("main")), FName{TEXT("inventory-search")}) : nullptr;
        const TSharedPtr<SWidget> Found = Tagged.IsValid()
            ? FindType(Tagged.ToSharedRef(), TEXT("SSearchBox")) : nullptr;
        return Found.IsValid() ? StaticCastSharedPtr<SSearchBox>(Found) : nullptr;
    }

    auto SendHeldButtonEnter(const TSharedPtr<SWidget>& InWidget, int32 InUser) -> void
    {
        if (!InWidget.IsValid()) { return; }
        const FString Type = InWidget->GetTypeAsString();
        if (Type != TEXT("SButton") && Type != TEXT("SCkUiStyledButton")
            && Type != TEXT("SActionCard")) { return; }
        const auto Button = StaticCastSharedPtr<SButton>(InWidget);
        const FKeyEvent Event{EKeys::Enter, FModifierKeysState{},
            static_cast<uint32>(InUser), false, 0, 0};
        Button->OnKeyDown(Button->GetCachedGeometry(), Event);
        Button->OnKeyUp(Button->GetCachedGeometry(), Event);
    }

    auto FocusAndActivate(UCk_PauseMenuProof_UE* InMenu, const TSharedPtr<SWidget>& InControl) -> bool
    {
        const int32 User = IsValid(InMenu) ? InMenu->Get_SlateUserIndex() : INDEX_NONE;
        if (!InControl.IsValid() || User < 0 || !InControl->IsEnabled()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        Slate.SetUserFocus(User, InControl, EFocusCause::SetDirectly);
        Tick(Slate);
        return FocusContains(InControl.ToSharedRef(), User) && PressKey(EKeys::Enter, User);
    }

    auto Capture(UWorld* InWorld, FState& InState, const FString& InName, FIntPoint InSize) -> bool
    {
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Viewport.IsValid()) { return false; }
        FSlateApplication& Slate = FSlateApplication::Get();
        const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        if (!InState.bWindowCaptured)
        {
            InState.WindowToRestore = Window;
            InState.OriginalWindowSize = Window->GetClientSizeInScreen();
            InState.bWindowCaptured = true;
        }
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Tick(Slate);
            Pixels.Reset();
            Size = FIntVector::ZeroValue;
            if (!Slate.TakeScreenshot(Viewport.ToSharedRef(), Pixels, Size)
                || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y)
            { return false; }
            if (Size.X == InSize.X && Size.Y == InSize.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D{
                static_cast<double>(InSize.X - Size.X), static_cast<double>(InSize.Y - Size.Y)});
        }
        if (Size.X != InSize.X || Size.Y != InSize.Y) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/InventoryMenuProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, InName + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }

    auto PanelMatchesReference(UCk_PauseMenuProof_UE* InMenu, UWorld* InWorld,
        FIntPoint InSize) -> bool
    {
        const TSharedPtr<FCkUiView> View = IsValid(InMenu) ? InMenu->Get_AuthoredView() : nullptr;
        UGameViewportClient* Client = IsValid(InWorld) ? InWorld->GetGameViewport() : nullptr;
        const TSharedPtr<SViewport> Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        const TSharedPtr<SWidget> Panel = View.IsValid()
            ? FindTagged(View->GetRegion(TEXT("main")), FName{TEXT("inventory-panel")}) : nullptr;
        if (!Panel.IsValid() || !Viewport.IsValid()) { return false; }
        const double Scale = FMath::Min(InSize.X / 1920.0, InSize.Y / 1080.0);
        const FVector2D ExpectedPosition{
            (InSize.X - 1920.0 * Scale) * 0.5 + 320.0 * Scale,
            (InSize.Y - 1080.0 * Scale) * 0.5 + 120.0 * Scale};
        const FVector2D Position = Panel->GetCachedGeometry().GetAbsolutePosition()
            - Viewport->GetCachedGeometry().GetAbsolutePosition();
        const FVector2D Size = Panel->GetCachedGeometry().GetLocalSize()
            * Panel->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
        constexpr double Tolerance = 3.0;
        return FMath::Abs(Position.X - ExpectedPosition.X) <= Tolerance
            && FMath::Abs(Position.Y - ExpectedPosition.Y) <= Tolerance
            && FMath::Abs(Size.X - 1280.0 * Scale) <= Tolerance
            && FMath::Abs(Size.Y - 800.0 * Scale) <= Tolerance;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_InventoryMenuProof_PIE,
    "Ck.UiAuthoring.InventoryMenuProof.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
        | EAutomationTestFlags::NonNullRHI)

bool FCk_InventoryMenuProof_PIE::RunTest(const FString&)
{
    using namespace ck_inventory_menu_proof_test;
    if (!FSlateApplication::IsInitialized())
    { AddError(TEXT("Inventory proof requires Slate.")); return false; }

    const TSharedRef<FState> State = MakeShared<FState>();
    UCkGym_StartupSettings* Settings = GetMutableDefault<UCkGym_StartupSettings>();
    if (!TestNotNull(TEXT("per-user gym startup settings"), Settings)) { return false; }
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
        }), 15.0, TEXT("UI Authoring gym registers before Inventory travel")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            UCkGym_Registry_Subsystem* Registry = GetRegistry(InWorld);
            if (!TestNotNull(TEXT("gym registry"), Registry)) { return; }
            const int32 RegistryIndex = Registry->Find_GymIndexByName(GymName);
            if (!TestTrue(TEXT("UI Authoring entry exists"),
                Registry->Get_Entries().IsValidIndex(RegistryIndex))) { return; }
            const FCkGym_Entry& Entry = Registry->Get_Entries()[RegistryIndex];
            TestEqual(TEXT("Inventory gym is in CkUI selector group"), Entry.Category, FString{TEXT("CkUI")});
            if (TestTrue(TEXT("entry names the AS gym GameMode"),
                IsValid(Entry.GameModeClass.Get())
                    && Entry.GameModeClass.Get()->GetName().Contains(GymModeName)))
            { Registry->Request_TravelToGym(RegistryIndex); }
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
            if (!TestTrue(TEXT("real gym and controller are ready"), IsGymReady(InWorld))
                || !TestNotNull(TEXT("real switchboard is ready"), Switchboard)
                || !TestTrue(TEXT("PIE viewport owns keyboard input"), FocusViewport(InWorld))) { return; }
            TestFalse(TEXT("selector begins closed"), Switchboard->Get_IsOpen());
            // The viewport queues gameplay input; the following latent check proves the shortcut effect.
            PressKey(EKeys::Three);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsInventoryActive(ck::auto_test::net::Get_ServerWorld()); }),
        10.0, TEXT("real [3] shortcut opens authored Inventory")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            FSlateApplication& Slate = FSlateApplication::Get();
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (!TestNotNull(TEXT("Inventory host opened by the gym"), Menu)) { return; }
            const TSharedPtr<FCkInventoryMenuProofModel> Model = Menu->Get_InventoryModel();
            const TSharedPtr<FCkUiView> View = Menu->Get_AuthoredView();
            const TSharedPtr<SCkUiRepeat> Repeat = GetRepeat(Menu);
            const TSharedPtr<SSearchBox> Search = GetSearch(Menu);
            const TSharedPtr<SWidget> Primary = GetControl(Menu, TEXT("inventory-primary-action"));
            const TSharedPtr<SWidget> Close = GetControl(Menu, TEXT("inventory-close"));
            if (!TestTrue(TEXT("Inventory mounts model, view, repeat, search and actions"),
                Model.IsValid() && View.IsValid() && Repeat.IsValid() && Search.IsValid()
                    && Primary.IsValid() && Close.IsValid())) { return; }
            const TSharedPtr<SWidget> EditableWidget = FindType(Search.ToSharedRef(), TEXT("SEditableText"));
            if (!TestTrue(TEXT("authored search owns native editable text"), EditableWidget.IsValid())) { return; }
            const auto Editable = StaticCastSharedPtr<SEditableText>(EditableWidget);
            TestEqual(TEXT("native search applies authored 18px font"), Editable->GetFont().Size, 18.0f);
            const int32 User = Menu->Get_SlateUserIndex();
            TestTrue(TEXT("Inventory owns paused CommonUI session and focus"),
                InWorld->IsPaused() && Menu->IsActivated() && User >= 0
                    && !GetSwitchboard(GetPlayer(InWorld))->Get_IsOpen());
            TestTrue(TEXT("Inventory starts with twelve keyed cards and sword selected"),
                Model->Get_Items().IsValid() && Model->Get_Items()->GetRecords().Num() == 12
                    && Model->Get_VisibleKeys().Num() == 12 && Repeat->GetItemCount() == 12
                    && Model->Get_SelectedKey() == TEXT("iron-longsword")
                    && Model->Get_Category() == TEXT("All") && Model->Get_SearchText().IsEmpty());
            const auto InitialQuantities = Model->Get_State().Get_ItemQuantities();
            TestTrue(TEXT("fixture starts with six equipment, four supplies and two materials"),
                InitialQuantities.Num() == 12
                    && InitialQuantities.FindRef(TEXT("iron-longsword")) == 1
                    && InitialQuantities.FindRef(TEXT("oak-bow")) == 1
                    && InitialQuantities.FindRef(TEXT("steel-shield")) == 1
                    && InitialQuantities.FindRef(TEXT("trail-hood")) == 1
                    && InitialQuantities.FindRef(TEXT("leather-boots")) == 1
                    && InitialQuantities.FindRef(TEXT("scout-vest")) == 1
                    && InitialQuantities.FindRef(TEXT("health-tonic")) == 3
                    && InitialQuantities.FindRef(TEXT("focus-draught")) == 2
                    && InitialQuantities.FindRef(TEXT("travel-rations")) == 5
                    && InitialQuantities.FindRef(TEXT("bandages")) == 4
                    && InitialQuantities.FindRef(TEXT("iron-ore")) == 8
                    && InitialQuantities.FindRef(TEXT("moonstone")) == 2);
            const FCk_UiAuthoringProof_InventoryState AcceptedState = Model->Get_State();
            const TSharedPtr<FCkUiCollection> AcceptedCollection = Model->Get_Items();
            const int64 AcceptedCollectionRevision = AcceptedCollection->GetRevision();
            const FString AcceptedSelection = Model->Get_SelectedKey();
            const auto RejectSnapshot = [this, &Model, &AcceptedState, &AcceptedCollection,
                AcceptedCollectionRevision, &AcceptedSelection](
                    const TCHAR* InLabel, const FCk_UiAuthoringProof_InventoryState& InSnapshot) -> void
            {
                FString Error;
                TestFalse(InLabel, FCkInventoryMenuProofModel::TryValidateState(InSnapshot, Error));
                TestFalse(TEXT("rejected snapshot reports a validation error"), Error.IsEmpty());
                TSharedPtr<FCkInventoryMenuProofModel> Candidate = Model;
                Error.Reset();
                TestFalse(TEXT("TryCreate rejects malformed snapshot before resource load"),
                    FCkInventoryMenuProofModel::TryCreate(InSnapshot, FString{}, Candidate, Error));
                TestTrue(TEXT("failed TryCreate retains the accepted output and mounted state"),
                    Candidate == Model && Model->Get_Items() == AcceptedCollection
                        && AcceptedCollection->GetRevision() == AcceptedCollectionRevision
                        && Model->Get_State().Get_ItemQuantities().OrderIndependentCompareEqual(AcceptedState.Get_ItemQuantities())
                        && Model->Get_State().Get_EquippedKey() == AcceptedState.Get_EquippedKey()
                        && Model->Get_SelectedKey() == AcceptedSelection && !Error.IsEmpty());
            };
            auto Partial = FCk_UiAuthoringProof_InventoryState{};
            auto PartialQuantities = TMap<FString, int32>{};
            PartialQuantities.Add(TEXT("iron-longsword"), 1);
            Partial.Set_ItemQuantities(MoveTemp(PartialQuantities));
            RejectSnapshot(TEXT("partial key set rejects"), Partial);
            auto Negative = AcceptedState;
            auto NegativeQuantities = Negative.Get_ItemQuantities();
            NegativeQuantities.Add(TEXT("health-tonic"), -1);
            Negative.Set_ItemQuantities(MoveTemp(NegativeQuantities));
            RejectSnapshot(TEXT("negative quantity rejects"), Negative);
            auto Excess = AcceptedState;
            auto ExcessQuantities = Excess.Get_ItemQuantities();
            ExcessQuantities.Add(TEXT("health-tonic"), 4);
            Excess.Set_ItemQuantities(MoveTemp(ExcessQuantities));
            RejectSnapshot(TEXT("quantity over fixture maximum rejects"), Excess);
            auto Unknown = AcceptedState;
            auto UnknownQuantities = Unknown.Get_ItemQuantities();
            UnknownQuantities.Add(TEXT("unknown-item"), 1);
            Unknown.Set_ItemQuantities(MoveTemp(UnknownQuantities));
            RejectSnapshot(TEXT("unknown key rejects"), Unknown);
            auto EquippedSupply = AcceptedState;
            EquippedSupply.Set_EquippedKey(TEXT("health-tonic"));
            RejectSnapshot(TEXT("equipped supply rejects"), EquippedSupply);
            auto NotOwned = AcceptedState;
            auto NotOwnedQuantities = NotOwned.Get_ItemQuantities();
            NotOwnedQuantities.Add(TEXT("iron-longsword"), 0);
            NotOwned.Set_ItemQuantities(MoveTemp(NotOwnedQuantities));
            NotOwned.Set_EquippedKey(TEXT("iron-longsword"));
            RejectSnapshot(TEXT("equipped unowned item rejects"), NotOwned);
            auto EmptyEquipped = FCk_UiAuthoringProof_InventoryState{};
            EmptyEquipped.Set_EquippedKey(TEXT("oak-bow"));
            RejectSnapshot(TEXT("empty seed cannot name equipment"), EmptyEquipped);
            auto ChangedValidSeed = AcceptedState;
            auto SeedQuantities = ChangedValidSeed.Get_ItemQuantities();
            SeedQuantities.Add(TEXT("health-tonic"), 1);
            ChangedValidSeed.Set_ItemQuantities(MoveTemp(SeedQuantities));
            ChangedValidSeed.Set_EquippedKey(TEXT("oak-bow"));
            FString SeedError;
            if (TestTrue(TEXT("changed active-host seed itself validates"),
                FCkInventoryMenuProofModel::TryValidateState(ChangedValidSeed, SeedError)))
            {
                const int64 ViewRevision = View->GetRevision();
                TestTrue(TEXT("active Inventory host is reused for valid changed seed"),
                    UCk_PauseMenuProof_UE::OpenWithProofState(GetPlayer(InWorld),
                        ECk_UiAuthoringProof_Screen::Inventory,
                        Menu->Get_AppliedSettings(), ChangedValidSeed) == Menu);
                TestTrue(TEXT("active-host seed does not replace accepted inventory or view"),
                    Menu->Get_InventoryModel() == Model && Menu->Get_AuthoredView() == View
                        && View->GetRevision() == ViewRevision
                        && AcceptedCollection->GetRevision() == AcceptedCollectionRevision
                        && Model->Get_State().Get_ItemQuantities().OrderIndependentCompareEqual(AcceptedState.Get_ItemQuantities())
                        && Model->Get_State().Get_EquippedKey() == AcceptedState.Get_EquippedKey());
            }
            TestTrue(TEXT("initial selected sword has an enabled native primary action"),
                GetCard(Menu, TEXT("iron-longsword")).IsValid() && Primary->IsEnabled());
            TestTrue(TEXT("Inventory 1920x1080 native capture"),
                Capture(InWorld, *State, TEXT("inventory-all-1920x1080"), FIntPoint{1920, 1080}));
            TestTrue(TEXT("Inventory panel follows authored 1920x1080 bounds"),
                PanelMatchesReference(Menu, InWorld, FIntPoint{1920, 1080}));
            TestTrue(TEXT("Inventory 1280x720 native capture"),
                Capture(InWorld, *State, TEXT("inventory-all-1280x720"), FIntPoint{1280, 720}));
            TestTrue(TEXT("Inventory panel scales into 1280x720"),
                PanelMatchesReference(Menu, InWorld, FIntPoint{1280, 720}));
            TestTrue(TEXT("Inventory 2560x1080 native capture"),
                Capture(InWorld, *State, TEXT("inventory-all-2560x1080"), FIntPoint{2560, 1080}));
            TestTrue(TEXT("Inventory panel centers in 2560x1080"),
                PanelMatchesReference(Menu, InWorld, FIntPoint{2560, 1080}));
            TestTrue(TEXT("Inventory interactive size restored"),
                Capture(InWorld, *State, TEXT("inventory-all-restored-1920x1080"), FIntPoint{1920, 1080}));
            const TSharedPtr<SScrollBox> Scroll = View->GetScroll(TEXT("inventory-scroll"));
            if (TestTrue(TEXT("authored Inventory owns a native scroll box"), Scroll.IsValid()))
            {
                Scroll->SetScrollOffset(220.0f);
                Tick(Slate);
                TestTrue(TEXT("scrolled Inventory 1920x1080 native capture"),
                    Capture(InWorld, *State, TEXT("inventory-scrolled-1920x1080"), FIntPoint{1920, 1080}));
            }

            const FString BeforeInvalid = Model->Get_Summary();
            TestFalse(TEXT("unknown inventory key is rejected"), Model->Request_Select(TEXT("missing-item")));
            TestFalse(TEXT("unknown category is rejected"), Model->Request_Filter(TEXT("Unknown")));
            TestEqual(TEXT("invalid requests preserve inventory state"), Model->Get_Summary(), BeforeInvalid);

            const TSharedPtr<SWidget> MaterialsFilter = GetControl(Menu, TEXT("inventory-filter-materials"));
            if (TestTrue(TEXT("last filter can receive focus before search navigation"), MaterialsFilter.IsValid()))
            {
                Slate.SetUserFocus(User, MaterialsFilter, EFocusCause::SetDirectly);
                TestTrue(TEXT("Tab enters the native inventory search"),
                    PressKey(EKeys::Tab, User) && FocusContains(Search.ToSharedRef(), User));
                for (const TCHAR Character : FString{TEXT("bow")})
                {
                    TestTrue(TEXT("native search accepts routed character input"),
                        Slate.ProcessKeyCharEvent(FCharacterEvent{Character, FModifierKeysState{},
                            static_cast<uint32>(User), false}));
                }

            }
        })));
    // Native SSearchBox debounces typing for 0.25 seconds. Advance real frames rather than
    // assuming two synchronous Slate ticks have elapsed that timer.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(ck::auto_test::net::Get_ServerWorld()));
            const auto Model = IsValid(Menu) ? Menu->Get_InventoryModel() : nullptr;
            const auto Repeat = GetRepeat(Menu);
            return Model.IsValid() && Repeat.IsValid()
                && Model->Get_SearchText().ToString() == TEXT("bow") && Repeat->GetItemCount() == 1
                && Model->Get_SelectedKey() == TEXT("oak-bow");
        }), 5.0, TEXT("routed typing projects bow after native search debounce")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            const auto Search = GetSearch(Menu);
            if (!TestTrue(TEXT("debounced native search remains mounted"), Search.IsValid())) { return; }
            // GetText reads the authored bound value, which publishes after native debounce.
            TestEqual(TEXT("native search binding contains the routed characters after publish"),
                Search->GetText().ToString(), FString{TEXT("bow")});
            Search->SetText(FText::GetEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(ck::auto_test::net::Get_ServerWorld()));
            const auto Model = IsValid(Menu) ? Menu->Get_InventoryModel() : nullptr;
            const auto Repeat = GetRepeat(Menu);
            return Model.IsValid() && Repeat.IsValid() && Model->Get_SearchText().IsEmpty()
                && Repeat->GetItemCount() == 12;
        }), 5.0, TEXT("cleared native search restores the full collection")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            FSlateApplication& Slate = FSlateApplication::Get();
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (!TestNotNull(TEXT("Inventory host survives search debounce"), Menu)) { return; }
            const auto Model = Menu->Get_InventoryModel();
            const auto View = Menu->Get_AuthoredView();
            const auto Repeat = GetRepeat(Menu);
            const auto Search = GetSearch(Menu);
            const auto Primary = GetControl(Menu, TEXT("inventory-primary-action"));
            if (!TestTrue(TEXT("Inventory retains its live model and native controls"),
                Model.IsValid() && View.IsValid() && Repeat.IsValid() && Search.IsValid() && Primary.IsValid()))
            { return; }
            const int32 User = Menu->Get_SlateUserIndex();
            Model->Request_Select(TEXT("iron-longsword"));
            Tick(Slate);
            if (!TestTrue(TEXT("Equipment filter receives native Enter"),
                FocusAndActivate(Menu, GetControl(Menu, TEXT("inventory-filter-equipment"))))) { return; }
            Tick(Slate);
            TestTrue(TEXT("Equipment shows six cards and keeps visible sword selected"),
                Model->Get_Category() == TEXT("Equipment") && Model->Get_VisibleKeys().Num() == 6
                    && Repeat->GetItemCount() == 6 && Model->Get_SelectedKey() == TEXT("iron-longsword"));
            const TSharedPtr<SWidget> OldBowCard = GetCard(Menu, TEXT("oak-bow"));
            const TSharedPtr<SWidget> SwordCard = GetCard(Menu, TEXT("iron-longsword"));
            if (TestTrue(TEXT("native sword card can receive keyboard focus"), SwordCard.IsValid()))
            {
                Slate.SetUserFocus(User, SwordCard, EFocusCause::SetDirectly);
                Tick(Slate);
                TestTrue(TEXT("inventory Right moves card focus in fixture order"),
                    FocusContains(SwordCard.ToSharedRef(), User) && PressKey(EKeys::Right, User)
                        && OldBowCard.IsValid() && FocusContains(OldBowCard.ToSharedRef(), User));
                const TSharedPtr<SWidget> VestCard = GetCard(Menu, TEXT("scout-vest"));
                TestTrue(TEXT("inventory Down moves focus by four cards"),
                    PressKey(EKeys::Down, User) && VestCard.IsValid()
                        && FocusContains(VestCard.ToSharedRef(), User));
                TestTrue(TEXT("inventory Up returns to the prior card row"),
                    PressKey(EKeys::Up, User) && OldBowCard.IsValid()
                        && FocusContains(OldBowCard.ToSharedRef(), User));
            }
            if (!TestTrue(TEXT("native item card is focusable and selects its stable key"),
                FocusAndActivate(Menu, OldBowCard))) { return; }
            TestEqual(TEXT("bow selected through native card input"), Model->Get_SelectedKey(),
                FString{TEXT("oak-bow")});
            TestTrue(TEXT("selected bow updates authored details"),
                HasText(View->GetRegion(TEXT("main")), TEXT("Oak Bow"))
                    && HasText(View->GetRegion(TEXT("main")),
                        TEXT("A sturdy hunting bow carved from seasoned oak.")));
            TestTrue(TEXT("Equip action routes through native Enter"), FocusAndActivate(Menu, Primary));
            TestEqual(TEXT("bow is equipped in persisted inventory state"),
                Model->Get_State().Get_EquippedKey(), FString{TEXT("oak-bow")});
            TestTrue(TEXT("equipped Inventory 1920x1080 capture"),
                Capture(InWorld, *State, TEXT("inventory-equipped-1920x1080"), FIntPoint{1920, 1080}));
            TestTrue(TEXT("Unequip routes through same native action"), FocusAndActivate(Menu, Primary));
            TestTrue(TEXT("unequip clears persisted equipment key"),
                Model->Get_State().Get_EquippedKey().IsEmpty());

            if (!TestTrue(TEXT("Supplies filter receives native Enter"),
                FocusAndActivate(Menu, GetControl(Menu, TEXT("inventory-filter-supplies"))))) { return; }
            Tick(Slate);
            TestTrue(TEXT("Supplies shows four cards and reconciles selection"),
                Model->Get_Category() == TEXT("Supplies") && Model->Get_VisibleKeys().Num() == 4
                    && Repeat->GetItemCount() == 4 && Model->Get_SelectedKey() == TEXT("health-tonic"));
            TestTrue(TEXT("Supplies 1920x1080 native capture"),
                Capture(InWorld, *State, TEXT("inventory-supplies-1920x1080"), FIntPoint{1920, 1080}));
            const FString BeforeStaleBow = Model->Get_SelectedKey();
            SendHeldButtonEnter(OldBowCard, User);
            TestEqual(TEXT("filtered-out held card cannot select absent key"),
                Model->Get_SelectedKey(), BeforeStaleBow);
            const TSharedPtr<SWidget> OldTonicCard = GetCard(Menu, TEXT("health-tonic"));
            for (int32 Remaining = 2; Remaining >= 0; --Remaining)
            {
                if (!TestTrue(TEXT("Consume tonic through native primary Enter"),
                    FocusAndActivate(Menu, GetControl(Menu, TEXT("inventory-primary-action"))))) { return; }
                Tick(Slate);
                TestEqual(TEXT("tonic quantity decrements exactly once"),
                    Model->Get_State().Get_ItemQuantities().FindRef(TEXT("health-tonic")), Remaining);
            }
            TestTrue(TEXT("last tonic removes its keyed card and reconciles selection"),
                Model->Get_VisibleKeys().Num() == 3 && Repeat->GetItemCount() == 3
                    && !Repeat->GetItemWidget(TEXT("health-tonic")).IsValid()
                    && Model->Get_SelectedKey() != TEXT("health-tonic"));
            const FString AfterLastTonic = Model->Get_Summary();
            SendHeldButtonEnter(OldTonicCard, User);
            TestEqual(TEXT("removed held card is dormant after inventory mutation"),
                Model->Get_Summary(), AfterLastTonic);

            if (!TestTrue(TEXT("Materials filter receives native Enter"),
                FocusAndActivate(Menu, GetControl(Menu, TEXT("inventory-filter-materials"))))) { return; }
            Tick(Slate);
            TestTrue(TEXT("Materials shows two cards"),
                Model->Get_Category() == TEXT("Materials") && Model->Get_VisibleKeys().Num() == 2
                    && Repeat->GetItemCount() == 2);
            const FString BeforeMaterialAction = Model->Get_Summary();
            const TSharedPtr<SWidget> MaterialPrimary = GetControl(Menu, TEXT("inventory-primary-action"));
            TestTrue(TEXT("material Inspect action is visibly disabled"),
                MaterialPrimary.IsValid() && !MaterialPrimary->IsEnabled());
            TestFalse(TEXT("material primary action rejects without a gameplay mutation"), Model->Request_PrimaryAction());
            SendHeldButtonEnter(MaterialPrimary, User);
            TestEqual(TEXT("disabled material input leaves the accepted model unchanged"),
                Model->Get_Summary(), BeforeMaterialAction);
            if (!TestTrue(TEXT("All filter receives native Enter"),
                FocusAndActivate(Menu, GetControl(Menu, TEXT("inventory-filter-all"))))) { return; }
            Tick(Slate);
            TestTrue(TEXT("All restores surviving cards in stable inventory"),
                Model->Get_Category() == TEXT("All") && Model->Get_VisibleKeys().Num() == 11
                    && Repeat->GetItemCount() == 11 && GetCard(Menu, TEXT("oak-bow")).IsValid());
            const FString BeforeReinsertStale = Model->Get_SelectedKey();
            SendHeldButtonEnter(OldBowCard, User);
            TestEqual(TEXT("old filtered scope cannot act after key reappears"),
                Model->Get_SelectedKey(), BeforeReinsertStale);

            Search->SetText(FText::FromString(TEXT("MOONSTONE")));
            Tick(Slate);
            TestTrue(TEXT("case-insensitive native search narrows to moonstone"),
                Model->Get_SearchText().ToString() == TEXT("MOONSTONE")
                    && Model->Get_VisibleKeys().Num() == 1 && Repeat->GetItemCount() == 1
                    && Model->Get_SelectedKey() == TEXT("moonstone"));
            Search->SetText(FText::FromString(TEXT("no such item")));
            Tick(Slate);
            const TSharedPtr<SWidget> EmptyPrimary = GetControl(Menu, TEXT("inventory-primary-action"));
            TestTrue(TEXT("no-match search clears selection, shows empty state and disables action"),
                Model->Get_VisibleKeys().IsEmpty() && Repeat->GetItemCount() == 0
                    && Model->Get_SelectedKey().IsEmpty() && EmptyPrimary.IsValid()
                    && !EmptyPrimary->IsEnabled());
            const TSharedPtr<SWidget> EmptyState = FindTagged(View->GetRegion(TEXT("main")),
                FName{TEXT("inventory-empty-state")});
            TestTrue(TEXT("authored empty state replaces the card content"),
                EmptyState.IsValid() && EmptyState->GetVisibility() == EVisibility::Visible
                    && HasText(View->GetRegion(TEXT("main")), TEXT("No items match your search.")));
            TestTrue(TEXT("empty Inventory 1920x1080 native capture"),
                Capture(InWorld, *State, TEXT("inventory-empty-1920x1080"), FIntPoint{1920, 1080}));
            Search->SetText(FText::GetEmpty());
            Tick(Slate);
            TestTrue(TEXT("clearing search restores eleven remaining cards"),
                Model->Get_VisibleKeys().Num() == 11 && Repeat->GetItemCount() == 11);

            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
            if (!TestTrue(TEXT("CkTests installed Inventory sources resolve"), Plugin.IsValid())) { return; }
            const FString ResourceRoot = FPaths::Combine(Plugin->GetBaseDir(),
                TEXT("Resources/UiAuthoring/InventoryMenuProof/InventoryMenuProof.ui"));
            FString Markup;
            FString Style;
            if (!TestTrue(TEXT("installed Inventory source pair loads"),
                FFileHelper::LoadFileToString(Markup, *(ResourceRoot + TEXT(".html")))
                    && FFileHelper::LoadFileToString(Style, *(ResourceRoot + TEXT(".css"))))) { return; }
            const int64 AcceptedRevision = View->GetRevision();
            const TSharedPtr<SSearchBox> RetainedSearch = GetSearch(Menu);
            const TSharedPtr<SScrollBox> RetainedScroll = View->GetScroll(TEXT("inventory-scroll"));
            const FString BeforeReload = Model->Get_Summary();
            const auto Editable = StaticCastSharedPtr<SEditableText>(FindType(Search.ToSharedRef(), TEXT("SEditableText")));
            if (!TestTrue(TEXT("retained search has its editable native child"), Editable.IsValid())) { return; }
            const FString LargerSearchFont = Style.Replace(TEXT("font-size: 18px"), TEXT("font-size: 26px"));
            TestTrue(TEXT("compatible Inventory font style reload succeeds"),
                Menu->Request_ApplySource(Markup, LargerSearchFont));
            Tick(Slate);
            TestTrue(TEXT("compatible reload keeps model, repeat and retained search/scroll"),
                View == Menu->Get_AuthoredView() && View->GetRevision() > AcceptedRevision
                    && Model == Menu->Get_InventoryModel() && Repeat == GetRepeat(Menu)
                    && RetainedSearch == GetSearch(Menu)
                    && RetainedScroll == View->GetScroll(TEXT("inventory-scroll"))
                    && Model->Get_Summary() == BeforeReload);
            TestEqual(TEXT("retained native search adopts accepted font reload"), Editable->GetFont().Size, 26.0f);
            const int64 RejectedRevision = View->GetRevision();
            TestFalse(TEXT("malformed Inventory source rejects atomically"),
                Menu->Request_ApplySource(TEXT("<ui version=\"1\"><region name=\"main\"><repeat"), Style));
            TestTrue(TEXT("rejected source retains accepted revision and presentation"),
                Menu->Get_AuthoredView() == View && View->GetRevision() == RejectedRevision
                    && GetRepeat(Menu) == Repeat && GetSearch(Menu).IsValid()
                    && Model->Get_Summary() == BeforeReload && !Menu->Get_SourceError().IsEmpty());
            Tick(Slate);
            TestEqual(TEXT("rejected candidate does not change retained native search font"), Editable->GetFont().Size, 26.0f);
            TestTrue(TEXT("installed source recovers after rejection"),
                Menu->Request_ApplySource(Markup, Style));
            Tick(Slate);
            TestEqual(TEXT("recovered source restores authored search font"), Editable->GetFont().Size, 18.0f);
            State->RestoreWindow();
            TestTrue(TEXT("Inventory close action routes through native Enter"),
                FocusAndActivate(Menu, GetControl(Menu, TEXT("inventory-close"))));
            TestTrue(TEXT("closed Inventory releases its authored view"), View->IsOwnerReleased());
            const FString AfterClose = Model->Get_Summary();
            SendHeldButtonEnter(OldBowCard, User);
            TestEqual(TEXT("held card cannot change model after host close"),
                Model->Get_Summary(), AfterClose);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && !World->IsPaused()
                && !IsValid(GetActiveMenu(GetPlayer(World)));
        }), 10.0, TEXT("Inventory Close releases paused CommonUI owner")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            if (!TestTrue(TEXT("viewport focus returns after Inventory Close"), FocusViewport(InWorld)))
            { return; }
            PressKey(EKeys::Three);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsInventoryActive(ck::auto_test::net::Get_ServerWorld()); }),
        10.0, TEXT("fresh Inventory session opens after Close")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (!TestNotNull(TEXT("fresh Inventory host"), Menu)) { return; }
            const TSharedPtr<FCkInventoryMenuProofModel> Model = Menu->Get_InventoryModel();
            const TSharedPtr<SCkUiRepeat> Repeat = GetRepeat(Menu);
            if (!TestTrue(TEXT("fresh Inventory remounts model and cards"),
                Model.IsValid() && Repeat.IsValid())) { return; }
            TestTrue(TEXT("reopen preserves consumed quantity and resets browsing state"),
                Model->Get_State().Get_ItemQuantities().FindRef(TEXT("health-tonic")) == 0
                    && Model->Get_State().Get_EquippedKey().IsEmpty()
                    && Model->Get_Category() == TEXT("All")
                    && Model->Get_SearchText().IsEmpty()
                    && Model->Get_SelectedKey() == TEXT("iron-longsword")
                    && Repeat->GetItemCount() == 11);
            const TSharedPtr<FCkUiView> InventoryView = Menu->Get_AuthoredView();
            if (!TestTrue(TEXT("Inventory screen can replace with Pause inside same host"),
                Menu->Request_ShowScreen(ECk_UiAuthoringProof_Screen::Pause))) { return; }
            TestTrue(TEXT("screen replacement retires Inventory interactions but keeps pause"),
                InventoryView.IsValid() && InventoryView->IsOwnerReleased()
                    && Menu->Get_Screen() == ECk_UiAuthoringProof_Screen::Pause
                    && Menu->IsActivated() && InWorld->IsPaused());
            if (!TestTrue(TEXT("Pause can restore Inventory in same owner"),
                Menu->Request_ShowScreen(ECk_UiAuthoringProof_Screen::Inventory))) { return; }
            TestTrue(TEXT("restored Inventory keeps persisted state"),
                Model == Menu->Get_InventoryModel()
                    && Menu->Get_InventoryModel()->Get_State().Get_ItemQuantities().FindRef(TEXT("health-tonic")) == 0
                    && GetRepeat(Menu).IsValid());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(ck::auto_test::net::Get_ServerWorld()));
            const auto All = GetControl(Menu, TEXT("inventory-filter-all"));
            return IsValid(Menu) && Menu->IsActivated() && All.IsValid()
                && FocusContains(All.ToSharedRef(), Menu->Get_SlateUserIndex());
        }), 5.0, TEXT("restored Inventory acquires its initial native focus")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (!TestNotNull(TEXT("focused restored Inventory host"), Menu)) { return; }
            TestTrue(TEXT("Inventory Escape uses host close path"),
                PressKey(EKeys::Escape, Menu->Get_SlateUserIndex()));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            UWorld* World = ck::auto_test::net::Get_ServerWorld();
            return IsValid(World) && !World->IsPaused()
                && !IsValid(GetActiveMenu(GetPlayer(World)));
        }), 10.0, TEXT("Inventory Escape closes and unpauses")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InWorld) -> void
        {
            if (!TestTrue(TEXT("viewport focus returns for teardown session"), FocusViewport(InWorld)))
            { return; }
            PressKey(EKeys::Three);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return IsInventoryActive(ck::auto_test::net::Get_ServerWorld()); }),
        10.0, TEXT("Inventory opens for actual PIE EndPlay coverage")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld) -> void
        {
            UCk_PauseMenuProof_UE* Menu = GetActiveMenu(GetPlayer(InWorld));
            if (!TestNotNull(TEXT("teardown Inventory host"), Menu)) { return; }
            State->CleanupWorld = InWorld;
            State->MenuAtCleanup = Menu;
            const TWeakPtr<FState> WeakState = State;
            State->CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
                [WeakState](UWorld* InCleanupWorld, bool, bool)
                {
                    const TSharedPtr<FState> Observed = WeakState.Pin();
                    if (!Observed.IsValid() || InCleanupWorld != Observed->CleanupWorld.Get()) { return; }
                    Observed->bObservedCleanup = true;
                    UCk_PauseMenuProof_UE* ObservedMenu = Observed->MenuAtCleanup.Get();
                    Observed->bInactiveAtCleanup = ObservedMenu != nullptr && !ObservedMenu->IsActivated();
                    Observed->bViewReleasedAtCleanup = ObservedMenu != nullptr
                        && !ObservedMenu->Get_AuthoredView().IsValid();
                });
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            AddInfo(FString::Printf(TEXT("Inventory PIE cleanup observed=%d, inactive=%d, view released=%d"),
                State->bObservedCleanup, State->bInactiveAtCleanup, State->bViewReleasedAtCleanup));
            return State->bObservedCleanup && State->bInactiveAtCleanup && State->bViewReleasedAtCleanup;
        }), TEXT("actual PIE EndPlay releases Inventory view before world cleanup")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([State]() -> bool
        {
            State->RestoreWindow();
            State->RestoreSettings();
            return State->bSettingsRestored;
        }), TEXT("Inventory proof restores gym startup preferences")));

    return true;
}

#endif
